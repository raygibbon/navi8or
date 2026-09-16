use crate::provider::{Entry, EntryKind, ListOptions, Location, Provider, ResourceId};
use std::cmp::Ordering;
use std::io;
use std::sync::Arc;

const HISTORY_LIMIT: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SortMode {
    Name,
    Size,
    Modified,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Command {
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Open,
    Parent,
    SwitchPane,
    SwapPanes,
    HistoryBack,
    HistoryForward,
    Refresh,
    ToggleHidden,
    SortName,
    SortSize,
    SortModified,
    Quit,
}

pub struct Pane {
    provider: Arc<dyn Provider>,
    pub location: Location,
    entries: Vec<Entry>,
    pub selected: usize,
    pub offset: usize,
    pub viewport_rows: usize,
    pub filter: String,
    pub sort_mode: SortMode,
    history: Vec<Location>,
    history_index: usize,
}

impl Pane {
    pub fn open(provider: Arc<dyn Provider>, input: &str, show_hidden: bool) -> io::Result<Self> {
        let location = provider.resolve(input)?;
        let entries = provider.list(&location, &ListOptions { show_hidden })?;
        let mut pane = Self {
            provider,
            location: location.clone(),
            entries,
            selected: 0,
            offset: 0,
            viewport_rows: 1,
            filter: String::new(),
            sort_mode: SortMode::Name,
            history: vec![location],
            history_index: 0,
        };
        pane.sort_entries();
        Ok(pane)
    }

    pub fn provider(&self) -> &dyn Provider {
        self.provider.as_ref()
    }

    pub fn entries(&self) -> &[Entry] {
        &self.entries
    }

    pub fn visible_count(&self) -> usize {
        self.entries
            .iter()
            .filter(|entry| self.entry_visible(entry))
            .count()
    }

    pub fn visible_entry(&self, visible_index: usize) -> Option<&Entry> {
        self.entries
            .iter()
            .filter(|entry| self.entry_visible(entry))
            .nth(visible_index)
    }

    pub fn selected_entry(&self) -> Option<&Entry> {
        self.visible_entry(self.selected)
    }

    pub fn set_viewport_rows(&mut self, rows: usize) {
        self.viewport_rows = rows.max(1);
        self.ensure_visible();
    }

    pub fn move_selection(&mut self, amount: isize) {
        let final_index = self.visible_count().saturating_sub(1);
        self.selected = self.selected.saturating_add_signed(amount).min(final_index);
        self.ensure_visible();
    }

    pub fn select_home(&mut self) {
        self.selected = 0;
        self.ensure_visible();
    }

    pub fn select_end(&mut self) {
        self.selected = self.visible_count().saturating_sub(1);
        self.ensure_visible();
    }

    pub fn set_filter(&mut self, filter: impl Into<String>) {
        let selected = self.selected_entry().map(|entry| entry.resource.clone());
        self.filter = filter.into();
        self.restore_selection(selected.as_ref());
    }

    pub fn set_sort_mode(&mut self, mode: SortMode) {
        let selected = self.selected_entry().map(|entry| entry.resource.clone());
        self.sort_mode = mode;
        self.sort_entries();
        self.restore_selection(selected.as_ref());
    }

    pub fn refresh(&mut self, show_hidden: bool) -> io::Result<()> {
        let selected = self.selected_entry().map(|entry| entry.resource.clone());
        self.entries = self
            .provider
            .list(&self.location, &ListOptions { show_hidden })?;
        self.sort_entries();
        self.restore_selection(selected.as_ref());
        Ok(())
    }

    pub fn parent(&mut self, show_hidden: bool) -> io::Result<()> {
        if let Some(location) = self.provider.parent(&self.location)? {
            self.load(location, show_hidden, true)?;
        }
        Ok(())
    }

    pub fn activate(&mut self, show_hidden: bool) -> io::Result<bool> {
        let entry = match self.selected_entry().cloned() {
            Some(entry) => entry,
            None => return Ok(false),
        };
        match entry.kind {
            EntryKind::Parent => {
                self.parent(show_hidden)?;
                Ok(true)
            }
            EntryKind::Directory => {
                let location = self.provider.child(&self.location, &entry.name)?;
                self.load(location, show_hidden, true)?;
                Ok(true)
            }
            EntryKind::File | EntryKind::Symlink | EntryKind::Other => Ok(false),
        }
    }

    pub fn history_back(&mut self, show_hidden: bool) -> io::Result<()> {
        if self.history_index == 0 {
            return Ok(());
        }
        let next_index = self.history_index - 1;
        let location = self.history[next_index].clone();
        self.load(location, show_hidden, false)?;
        self.history_index = next_index;
        Ok(())
    }

    pub fn history_forward(&mut self, show_hidden: bool) -> io::Result<()> {
        if self.history_index + 1 >= self.history.len() {
            return Ok(());
        }
        let next_index = self.history_index + 1;
        let location = self.history[next_index].clone();
        self.load(location, show_hidden, false)?;
        self.history_index = next_index;
        Ok(())
    }

    fn load(&mut self, location: Location, show_hidden: bool, add_history: bool) -> io::Result<()> {
        let mut entries = self
            .provider
            .list(&location, &ListOptions { show_hidden })?;
        sort_entries(&mut entries, self.sort_mode);
        self.location = location;
        self.entries = entries;
        self.selected = 0;
        self.offset = 0;
        if add_history {
            self.push_history();
        }
        Ok(())
    }

    fn push_history(&mut self) {
        if self.history.get(self.history_index) == Some(&self.location) {
            return;
        }
        self.history.truncate(self.history_index + 1);
        if self.history.len() == HISTORY_LIMIT {
            self.history.remove(0);
        }
        self.history.push(self.location.clone());
        self.history_index = self.history.len() - 1;
    }

    fn entry_visible(&self, entry: &Entry) -> bool {
        self.filter.is_empty() || entry.name.contains(&self.filter)
    }

    fn sort_entries(&mut self) {
        sort_entries(&mut self.entries, self.sort_mode);
    }

    fn restore_selection(&mut self, resource: Option<&ResourceId>) {
        self.selected = resource
            .and_then(|resource| {
                self.entries
                    .iter()
                    .filter(|entry| self.entry_visible(entry))
                    .position(|entry| &entry.resource == resource)
            })
            .unwrap_or(0);
        self.ensure_visible();
    }

    fn ensure_visible(&mut self) {
        let count = self.visible_count();
        if count == 0 {
            self.selected = 0;
            self.offset = 0;
            return;
        }
        self.selected = self.selected.min(count - 1);
        if self.selected < self.offset {
            self.offset = self.selected;
        } else if self.selected >= self.offset + self.viewport_rows {
            self.offset = self.selected + 1 - self.viewport_rows;
        }
    }
}

fn sort_entries(entries: &mut [Entry], mode: SortMode) {
    entries.sort_by(|left, right| compare_entries(left, right, mode));
}

fn compare_entries(left: &Entry, right: &Entry, mode: SortMode) -> Ordering {
    match (left.kind, right.kind) {
        (EntryKind::Parent, EntryKind::Parent) => return Ordering::Equal,
        (EntryKind::Parent, _) => return Ordering::Less,
        (_, EntryKind::Parent) => return Ordering::Greater,
        (EntryKind::Directory, EntryKind::Directory) => {}
        (EntryKind::Directory, _) => return Ordering::Less,
        (_, EntryKind::Directory) => return Ordering::Greater,
        _ => {}
    }
    let ordered = match mode {
        SortMode::Name => Ordering::Equal,
        SortMode::Size => left.size.cmp(&right.size),
        SortMode::Modified => right.modified.cmp(&left.modified),
    };
    ordered.then_with(|| {
        left.name
            .to_lowercase()
            .cmp(&right.name.to_lowercase())
            .then_with(|| left.name.cmp(&right.name))
    })
}

pub struct AppState {
    pub panes: [Pane; 2],
    pub active: usize,
    pub running: bool,
    pub show_hidden: bool,
    pub status: String,
}

impl AppState {
    pub fn new(panes: [Pane; 2]) -> Self {
        Self {
            panes,
            active: 0,
            running: true,
            show_hidden: false,
            status: "Ready".into(),
        }
    }

    pub fn resize(&mut self, _width: u16, height: u16) {
        let rows = usize::from(height.saturating_sub(7)).max(1);
        for pane in &mut self.panes {
            pane.set_viewport_rows(rows);
        }
    }

    /// Hook for future channel-backed jobs. It is deliberately non-blocking.
    pub fn service_background_work(&mut self) -> bool {
        false
    }

    pub fn dispatch(&mut self, command: Command) {
        match command {
            Command::SwitchPane => {
                self.active ^= 1;
                return;
            }
            Command::SwapPanes => {
                self.panes.swap(0, 1);
                return;
            }
            Command::Quit => {
                self.running = false;
                return;
            }
            Command::ToggleHidden => {
                self.show_hidden = !self.show_hidden;
                for pane in &mut self.panes {
                    if let Err(error) = pane.refresh(self.show_hidden) {
                        self.status = error.to_string();
                        break;
                    }
                }
                return;
            }
            _ => {}
        }

        let pane = &mut self.panes[self.active];
        let result = match command {
            Command::Up => {
                pane.move_selection(-1);
                Ok(())
            }
            Command::Down => {
                pane.move_selection(1);
                Ok(())
            }
            Command::Home => {
                pane.select_home();
                Ok(())
            }
            Command::End => {
                pane.select_end();
                Ok(())
            }
            Command::PageUp => {
                pane.move_selection(-(pane.viewport_rows as isize));
                Ok(())
            }
            Command::PageDown => {
                pane.move_selection(pane.viewport_rows as isize);
                Ok(())
            }
            Command::Open => match pane.activate(self.show_hidden) {
                Ok(true) => Ok(()),
                Ok(false) => {
                    self.status = "Viewer intentionally deferred in nav-rs".into();
                    Ok(())
                }
                Err(error) => Err(error),
            },
            Command::Parent => pane.parent(self.show_hidden),
            Command::HistoryBack => pane.history_back(self.show_hidden),
            Command::HistoryForward => pane.history_forward(self.show_hidden),
            Command::Refresh => pane.refresh(self.show_hidden),
            Command::SortName => {
                pane.set_sort_mode(SortMode::Name);
                Ok(())
            }
            Command::SortSize => {
                pane.set_sort_mode(SortMode::Size);
                Ok(())
            }
            Command::SortModified => {
                pane.set_sort_mode(SortMode::Modified);
                Ok(())
            }
            Command::SwitchPane | Command::SwapPanes | Command::ToggleHidden | Command::Quit => {
                unreachable!()
            }
        };

        if let Err(error) = result {
            self.status = error.to_string();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::provider::{Capabilities, ResourceMetadata};

    struct MemoryProvider;

    impl MemoryProvider {
        fn location(name: &str) -> Location {
            Location {
                resource: ResourceId::from_provider(name.into()),
                display: format!("memory://{name}"),
            }
        }

        fn entry(name: &str, resource: &str, kind: EntryKind, size: u64) -> Entry {
            Entry {
                name: name.into(),
                resource: ResourceId::from_provider(resource.into()),
                kind,
                size: Some(size),
                modified: None,
            }
        }
    }

    impl Provider for MemoryProvider {
        fn scheme(&self) -> &'static str {
            "memory"
        }

        fn display_name(&self) -> &'static str {
            "Memory"
        }

        fn capabilities(&self) -> Capabilities {
            Capabilities::LIST
        }

        fn resolve(&self, input: &str) -> io::Result<Location> {
            Ok(Self::location(input))
        }

        fn parent(&self, location: &Location) -> io::Result<Option<Location>> {
            Ok((location.resource.as_str() != "root").then(|| Self::location("root")))
        }

        fn child(&self, _location: &Location, name: &str) -> io::Result<Location> {
            Ok(Self::location(name))
        }

        fn list(&self, location: &Location, options: &ListOptions) -> io::Result<Vec<Entry>> {
            Ok(if location.resource.as_str() == "root" {
                let mut entries = vec![
                    Self::entry("large.txt", "large", EntryKind::File, 50),
                    Self::entry("..", "root", EntryKind::Parent, 0),
                    Self::entry("child", "child", EntryKind::Directory, 0),
                    Self::entry("small.txt", "small", EntryKind::File, 5),
                ];
                if options.show_hidden {
                    entries.push(Self::entry(".secret", "secret", EntryKind::File, 1));
                }
                entries
            } else {
                vec![Self::entry(
                    "..",
                    location.resource.as_str(),
                    EntryKind::Parent,
                    0,
                )]
            })
        }

        fn stat(&self, _resource: &ResourceId) -> io::Result<ResourceMetadata> {
            unreachable!()
        }
    }

    fn pane() -> Pane {
        Pane::open(Arc::new(MemoryProvider), "root", false).unwrap()
    }

    #[test]
    fn provider_neutral_pane_navigation_and_history_work() {
        let mut pane = pane();
        assert_eq!(pane.location.display, "memory://root");
        pane.move_selection(1);
        assert_eq!(pane.selected_entry().unwrap().name, "child");
        assert!(pane.activate(false).unwrap());
        assert_eq!(pane.location.display, "memory://child");
        pane.history_back(false).unwrap();
        assert_eq!(pane.location.display, "memory://root");
        pane.history_forward(false).unwrap();
        assert_eq!(pane.location.display, "memory://child");
    }

    #[test]
    fn selection_filter_sort_and_refresh_match_commander_rules() {
        let mut pane = pane();
        pane.set_viewport_rows(2);
        pane.move_selection(99);
        assert_eq!(pane.selected_entry().unwrap().name, "small.txt");
        assert!(pane.selected < pane.offset + pane.viewport_rows);

        pane.set_sort_mode(SortMode::Size);
        assert_eq!(pane.selected_entry().unwrap().name, "small.txt");
        let names: Vec<_> = (0..pane.visible_count())
            .filter_map(|index| pane.visible_entry(index))
            .map(|entry| entry.name.as_str())
            .collect();
        assert_eq!(names, ["..", "child", "small.txt", "large.txt"]);

        pane.set_filter("large");
        assert_eq!(pane.visible_count(), 1);
        assert_eq!(pane.selected_entry().unwrap().name, "large.txt");
        pane.refresh(false).unwrap();
        assert_eq!(pane.selected_entry().unwrap().name, "large.txt");
    }

    #[test]
    fn panes_switch_without_sharing_navigation_state() {
        let mut app = AppState::new([pane(), pane()]);
        app.dispatch(Command::Down);
        app.dispatch(Command::Open);
        assert_eq!(app.panes[0].location.display, "memory://child");
        app.dispatch(Command::SwitchPane);
        assert_eq!(app.active, 1);
        assert_eq!(app.panes[1].location.display, "memory://root");
    }

    #[test]
    fn hidden_toggle_refreshes_both_panes() {
        let mut app = AppState::new([pane(), pane()]);
        assert!(
            !app.panes[0]
                .entries()
                .iter()
                .any(|entry| entry.name == ".secret")
        );
        app.dispatch(Command::ToggleHidden);
        assert!(app.show_hidden);
        for pane in &app.panes {
            assert!(pane.entries().iter().any(|entry| entry.name == ".secret"));
        }
    }
}
