use crate::job::{CopyRequest, JobId, JobManager, JobMessage};
use crate::provider::{
    Entry, EntryKind, ListOptions, Location, LocationInput, Provider, ResourceId,
};
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
    View,
    Copy,
    CancelJob,
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
    visible_indices: Vec<usize>,
    pub selected: usize,
    pub offset: usize,
    pub viewport_rows: usize,
    pub filter: String,
    pub sort_mode: SortMode,
    history: Vec<Location>,
    history_index: usize,
}

impl Pane {
    pub fn open(
        provider: Arc<dyn Provider>,
        input: impl Into<LocationInput>,
        show_hidden: bool,
    ) -> io::Result<Self> {
        let location = provider.resolve(&input.into())?;
        let entries = provider.list(&location, &ListOptions { show_hidden })?;
        let mut pane = Self {
            provider,
            location: location.clone(),
            entries,
            visible_indices: Vec::new(),
            selected: 0,
            offset: 0,
            viewport_rows: 1,
            filter: String::new(),
            sort_mode: SortMode::Name,
            history: vec![location],
            history_index: 0,
        };
        pane.sort_entries();
        pane.rebuild_visible_indices();
        Ok(pane)
    }

    pub fn provider(&self) -> &dyn Provider {
        self.provider.as_ref()
    }

    pub fn entries(&self) -> &[Entry] {
        &self.entries
    }

    pub fn visible_count(&self) -> usize {
        self.visible_indices.len()
    }

    pub fn visible_entry(&self, visible_index: usize) -> Option<&Entry> {
        self.visible_indices
            .get(visible_index)
            .and_then(|index| self.entries.get(*index))
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
        self.rebuild_visible_indices();
        self.restore_selection(selected.as_ref());
    }

    pub fn set_sort_mode(&mut self, mode: SortMode) {
        let selected = self.selected_entry().map(|entry| entry.resource.clone());
        self.sort_mode = mode;
        self.sort_entries();
        self.rebuild_visible_indices();
        self.restore_selection(selected.as_ref());
    }

    pub fn refresh(&mut self, show_hidden: bool) -> io::Result<()> {
        let selected = self.selected_entry().map(|entry| entry.resource.clone());
        self.entries = self
            .provider
            .list(&self.location, &ListOptions { show_hidden })?;
        self.sort_entries();
        self.rebuild_visible_indices();
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
                let location = self.provider.location(&entry.resource)?;
                self.load(location, show_hidden, true)?;
                Ok(true)
            }
            EntryKind::Symlink
                if self.provider.stat_target(&entry.resource)?.kind == EntryKind::Directory =>
            {
                let location = self.provider.location(&entry.resource)?;
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
        self.rebuild_visible_indices();
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

    fn sort_entries(&mut self) {
        sort_entries(&mut self.entries, self.sort_mode);
    }

    fn rebuild_visible_indices(&mut self) {
        self.visible_indices.clear();
        for (index, entry) in self.entries.iter().enumerate() {
            if self.filter.is_empty() || entry.name.contains(&self.filter) {
                self.visible_indices.push(index);
            }
        }
    }

    fn restore_selection(&mut self, resource: Option<&ResourceId>) {
        self.selected = resource
            .and_then(|resource| {
                self.visible_indices
                    .iter()
                    .position(|index| &self.entries[*index].resource == resource)
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

#[derive(Clone)]
pub struct ViewerRequest {
    pub provider: Arc<dyn Provider>,
    pub entry: Entry,
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
    viewer_request: Option<ViewerRequest>,
    pub jobs: JobManager,
    copy_destination: Option<CopyDestination>,
}

struct CopyDestination {
    job: JobId,
    pane: usize,
    location: ResourceId,
}

impl AppState {
    pub fn new(panes: [Pane; 2]) -> Self {
        Self {
            panes,
            active: 0,
            running: true,
            show_hidden: false,
            status: "Ready".into(),
            viewer_request: None,
            jobs: JobManager::default(),
            copy_destination: None,
        }
    }

    pub fn resize(&mut self, _width: u16, height: u16) {
        let rows = usize::from(height.saturating_sub(7)).max(1);
        for pane in &mut self.panes {
            pane.set_viewport_rows(rows);
        }
    }

    pub fn service_background_work(&mut self) -> bool {
        let messages = self.jobs.poll();
        let changed = !messages.is_empty();
        for message in messages {
            match message {
                JobMessage::Started { total, .. } => {
                    self.status = total.map_or_else(
                        || "Copy started".into(),
                        |total| format!("Copy started: 0 / {total} bytes"),
                    );
                }
                JobMessage::Progress {
                    completed, total, ..
                } => {
                    self.status = total.map_or_else(
                        || format!("Copying: {completed} bytes"),
                        |total| {
                            let percent = completed
                                .saturating_mul(100)
                                .checked_div(total)
                                .unwrap_or(100);
                            format!("Copying: {completed} / {total} bytes ({percent}%)")
                        },
                    );
                }
                JobMessage::Completed(id) => {
                    self.status = "Copy complete".into();
                    if let Some(destination) = self.copy_destination.take()
                        && destination.job == id
                        && self.panes[destination.pane].location.resource == destination.location
                        && let Err(error) = self.panes[destination.pane].refresh(self.show_hidden)
                    {
                        self.status = format!("Copy complete; refresh failed: {error}");
                    }
                }
                JobMessage::Failed { id, error } => {
                    self.status = format!("Copy failed: {error}");
                    if self
                        .copy_destination
                        .as_ref()
                        .is_some_and(|destination| destination.job == id)
                    {
                        self.copy_destination = None;
                    }
                }
                JobMessage::Cancelled(id) => {
                    self.status = "Copy cancelled; partial destination removed".into();
                    if self
                        .copy_destination
                        .as_ref()
                        .is_some_and(|destination| destination.job == id)
                    {
                        self.copy_destination = None;
                    }
                }
            }
        }
        changed
    }

    pub fn take_viewer_request(&mut self) -> Option<ViewerRequest> {
        self.viewer_request.take()
    }

    pub fn dispatch(&mut self, command: Command) {
        match command {
            Command::SwitchPane => {
                self.active ^= 1;
                return;
            }
            Command::SwapPanes => {
                self.panes.swap(0, 1);
                if let Some(destination) = &mut self.copy_destination {
                    destination.pane ^= 1;
                }
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
            Command::Copy => {
                if let Err(error) = self.start_copy() {
                    self.status = error.to_string();
                }
                return;
            }
            Command::CancelJob => {
                self.status = if self.jobs.cancel_active() {
                    "Cancelling copy...".into()
                } else {
                    "No active job".into()
                };
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
                    self.viewer_request =
                        pane.selected_entry().cloned().map(|entry| ViewerRequest {
                            provider: pane.provider.clone(),
                            entry,
                        });
                    Ok(())
                }
                Err(error) => Err(error),
            },
            Command::View => (|| -> io::Result<()> {
                self.viewer_request = None;
                match pane.selected_entry() {
                    Some(entry) => {
                        let viewable = entry.kind == EntryKind::File
                            || (entry.kind == EntryKind::Symlink
                                && pane.provider.stat_target(&entry.resource)?.kind
                                    == EntryKind::File);
                        if viewable {
                            self.viewer_request = Some(ViewerRequest {
                                provider: pane.provider.clone(),
                                entry: entry.clone(),
                            });
                            Ok(())
                        } else {
                            Err(io::Error::new(
                                io::ErrorKind::Unsupported,
                                "The C Viewer bridge supports regular local files only",
                            ))
                        }
                    }
                    None => Err(io::Error::new(io::ErrorKind::NotFound, "No entry selected")),
                }
            })(),
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
            Command::SwitchPane
            | Command::SwapPanes
            | Command::ToggleHidden
            | Command::Copy
            | Command::CancelJob
            | Command::Quit => {
                unreachable!()
            }
        };

        if let Err(error) = result {
            self.status = error.to_string();
        }
    }

    fn start_copy(&mut self) -> io::Result<()> {
        let source_index = self.active;
        let destination_index = source_index ^ 1;
        let source = &self.panes[source_index];
        let entry = source
            .selected_entry()
            .cloned()
            .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "No selected entry"))?;
        let target_kind = if entry.kind == EntryKind::Symlink {
            source.provider.stat_target(&entry.resource)?.kind
        } else {
            entry.kind
        };
        if target_kind != EntryKind::File {
            return Err(io::Error::new(
                io::ErrorKind::Unsupported,
                "Directory copy is not implemented",
            ));
        }
        let source_provider = source.provider.clone();
        let name = source_provider.resource_name(&entry.resource)?;
        let destination = &self.panes[destination_index];
        let destination_provider = destination.provider.clone();
        let destination_location = destination.location.resource.clone();
        let target = destination_provider.child(&destination.location, &name)?;
        let id = self.jobs.start_copy(CopyRequest {
            source_provider,
            source: entry.resource,
            destination_provider,
            destination: target.resource,
            current_item: entry.name,
        })?;
        self.copy_destination = Some(CopyDestination {
            job: id,
            pane: destination_index,
            location: destination_location,
        });
        self.status = "Copy queued".into();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::LocalProvider;
    use crate::provider::{Capabilities, ResourceMetadata};
    use std::fs;
    use std::path::PathBuf;
    use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

    struct MemoryProvider;

    impl MemoryProvider {
        fn location(name: &str) -> Location {
            Location {
                resource: ResourceId::from_provider(name),
                display: format!("memory://{name}"),
            }
        }

        fn entry(name: &str, resource: &str, kind: EntryKind, size: u64) -> Entry {
            Entry {
                name: name.into(),
                resource: ResourceId::from_provider(resource),
                kind,
                size: Some(size),
                modified: None,
            }
        }
    }

    impl Provider for MemoryProvider {
        fn as_any(&self) -> &dyn std::any::Any {
            self
        }

        fn scheme(&self) -> &'static str {
            "memory"
        }

        fn display_name(&self) -> &'static str {
            "Memory"
        }

        fn capabilities(&self) -> Capabilities {
            Capabilities::LIST
        }

        fn resolve(&self, input: &LocationInput) -> io::Result<Location> {
            Ok(Self::location(&input.display()))
        }

        fn location(&self, resource: &ResourceId) -> io::Result<Location> {
            Ok(Self::location(&resource.as_os_str().to_string_lossy()))
        }

        fn parent(&self, location: &Location) -> io::Result<Option<Location>> {
            Ok((location.resource.as_os_str() != "root").then(|| Self::location("root")))
        }

        fn child(&self, _location: &Location, name: &crate::ResourceName) -> io::Result<Location> {
            Ok(Self::location(&name.as_os_str().to_string_lossy()))
        }

        fn list(&self, location: &Location, options: &ListOptions) -> io::Result<Vec<Entry>> {
            Ok(if location.resource.as_os_str() == "root" {
                let mut entries = vec![
                    Self::entry("large.txt", "large", EntryKind::File, 50),
                    Self::entry("..", "root", EntryKind::Parent, 0),
                    Self::entry("visible-child", "child", EntryKind::Directory, 0),
                    Self::entry("small.txt", "small", EntryKind::File, 5),
                ];
                if options.show_hidden {
                    entries.push(Self::entry(".secret", "secret", EntryKind::File, 1));
                }
                entries
            } else {
                vec![Self::entry(
                    "..",
                    &location.resource.as_os_str().to_string_lossy(),
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

    fn fixture() -> PathBuf {
        let suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let root = std::env::temp_dir().join(format!("nav-rs-app-{}-{suffix}", std::process::id()));
        fs::create_dir(&root).unwrap();
        root
    }

    #[test]
    fn provider_neutral_pane_navigation_and_history_work() {
        let mut pane = pane();
        assert_eq!(pane.location.display, "memory://root");
        pane.move_selection(1);
        assert_eq!(pane.selected_entry().unwrap().name, "visible-child");
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
        assert_eq!(names, ["..", "visible-child", "small.txt", "large.txt"]);

        pane.set_filter("large");
        assert_eq!(pane.visible_count(), 1);
        assert_eq!(pane.visible_indices.len(), 1);
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

    #[test]
    fn viewer_rejects_non_files_with_a_clear_status() {
        let mut app = AppState::new([pane(), pane()]);
        app.dispatch(Command::View);
        assert_eq!(
            app.status,
            "The C Viewer bridge supports regular local files only"
        );
        assert!(app.take_viewer_request().is_none());
    }

    #[test]
    fn completed_copy_updates_job_state_and_refreshes_destination_pane() {
        let root = fixture();
        let left = root.join("left");
        let right = root.join("right");
        fs::create_dir(&left).unwrap();
        fs::create_dir(&right).unwrap();
        fs::write(left.join("copied.bin"), b"background copy").unwrap();
        let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
        let mut source = Pane::open(provider.clone(), left.into_os_string(), false).unwrap();
        source.move_selection(1);
        let destination = Pane::open(provider, right.clone().into_os_string(), false).unwrap();
        let mut app = AppState::new([source, destination]);

        app.dispatch(Command::Copy);
        assert_eq!(app.status, "Copy queued");
        app.dispatch(Command::SwitchPane);
        app.resize(80, 25);
        assert_eq!(app.active, 1, "copy must not block UI commands");
        app.dispatch(Command::SwitchPane);
        let deadline = Instant::now() + Duration::from_secs(3);
        while app.jobs.active_id().is_some() {
            app.service_background_work();
            assert!(Instant::now() < deadline, "copy did not complete");
            std::thread::sleep(Duration::from_millis(2));
        }
        app.service_background_work();

        assert_eq!(app.status, "Copy complete");
        assert_eq!(
            fs::read(right.join("copied.bin")).unwrap(),
            b"background copy"
        );
        assert!(
            app.panes[1]
                .entries()
                .iter()
                .any(|entry| entry.name == "copied.bin")
        );
        fs::remove_dir_all(root).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn local_file_symlinks_are_viewable_and_directory_symlinks_navigate() {
        use std::os::unix::fs::symlink;

        let root = fixture();
        let directory = root.join("directory");
        fs::create_dir(&directory).unwrap();
        fs::write(root.join("file.txt"), b"linked").unwrap();
        symlink(&directory, root.join("directory-link")).unwrap();
        symlink(root.join("file.txt"), root.join("file-link")).unwrap();
        let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
        let left = Pane::open(provider.clone(), root.clone().into_os_string(), false).unwrap();
        let right = Pane::open(provider, root.clone().into_os_string(), false).unwrap();
        let mut app = AppState::new([left, right]);

        app.panes[0].set_filter("file-link");
        app.dispatch(Command::View);
        assert_eq!(
            app.take_viewer_request().unwrap().entry.kind,
            EntryKind::Symlink
        );

        app.panes[0].set_filter("directory-link");
        app.dispatch(Command::Open);
        assert_eq!(
            app.panes[0].location.resource.as_os_str(),
            root.join("directory-link").as_os_str()
        );
        fs::remove_dir_all(root).unwrap();
    }
}
