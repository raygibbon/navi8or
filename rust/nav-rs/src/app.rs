use crate::provider::{Entry, EntryKind, Provider};
use std::io;
use std::path::{Path, PathBuf};
use std::sync::Arc;

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
    Refresh,
    Quit,
}

pub struct Pane {
    provider: Arc<dyn Provider>,
    pub location: PathBuf,
    pub entries: Vec<Entry>,
    pub selected: usize,
    pub offset: usize,
    pub viewport_rows: usize,
}

impl Pane {
    pub fn open(
        provider: Arc<dyn Provider>,
        location: &Path,
        show_hidden: bool,
    ) -> io::Result<Self> {
        let location = provider.location(location)?;
        let entries = provider.list(&location, show_hidden)?;
        Ok(Self {
            provider,
            location,
            entries,
            selected: 0,
            offset: 0,
            viewport_rows: 1,
        })
    }

    pub fn provider(&self) -> &dyn Provider {
        self.provider.as_ref()
    }

    pub fn selected_entry(&self) -> Option<&Entry> {
        self.entries.get(self.selected)
    }

    pub fn set_viewport_rows(&mut self, rows: usize) {
        self.viewport_rows = rows.max(1);
        self.ensure_visible();
    }

    pub fn move_selection(&mut self, amount: isize) {
        let final_index = self.entries.len().saturating_sub(1);
        self.selected = self.selected.saturating_add_signed(amount).min(final_index);
        self.ensure_visible();
    }

    pub fn select_home(&mut self) {
        self.selected = 0;
        self.ensure_visible();
    }

    pub fn select_end(&mut self) {
        self.selected = self.entries.len().saturating_sub(1);
        self.ensure_visible();
    }

    pub fn refresh(&mut self, show_hidden: bool) -> io::Result<()> {
        let selected_resource = self.selected_entry().map(|entry| entry.resource.clone());
        self.entries = self.provider.list(&self.location, show_hidden)?;
        self.selected = selected_resource
            .and_then(|resource| {
                self.entries
                    .iter()
                    .position(|entry| entry.resource == resource)
            })
            .unwrap_or(0);
        self.ensure_visible();
        Ok(())
    }

    pub fn parent(&mut self, show_hidden: bool) -> io::Result<()> {
        let location = self.provider.parent(&self.location)?;
        self.load(location, show_hidden)
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
                self.load(entry.resource, show_hidden)?;
                Ok(true)
            }
            EntryKind::File => Ok(false),
        }
    }

    fn load(&mut self, location: PathBuf, show_hidden: bool) -> io::Result<()> {
        let location = self.provider.location(&location)?;
        let entries = self.provider.list(&location, show_hidden)?;
        self.location = location;
        self.entries = entries;
        self.selected = 0;
        self.offset = 0;
        Ok(())
    }

    fn ensure_visible(&mut self) {
        if self.entries.is_empty() {
            self.selected = 0;
            self.offset = 0;
            return;
        }
        self.selected = self.selected.min(self.entries.len() - 1);
        if self.selected < self.offset {
            self.offset = self.selected;
        } else if self.selected >= self.offset + self.viewport_rows {
            self.offset = self.selected + 1 - self.viewport_rows;
        }
    }
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

    pub fn dispatch(&mut self, command: Command) {
        if command == Command::SwitchPane {
            self.active ^= 1;
            return;
        }
        if command == Command::Quit {
            self.running = false;
            return;
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
            Command::Refresh => pane.refresh(self.show_hidden),
            Command::SwitchPane | Command::Quit => unreachable!(),
        };

        if let Err(error) = result {
            self.status = error.to_string();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::LocalProvider;
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn fixture() -> PathBuf {
        let suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let root = std::env::temp_dir().join(format!("nav-rs-app-{}-{suffix}", std::process::id()));
        fs::create_dir(&root).unwrap();
        fs::create_dir(root.join("child")).unwrap();
        fs::write(root.join("file.txt"), b"hello").unwrap();
        root
    }

    #[test]
    fn selection_is_clamped_and_kept_visible() {
        let root = fixture();
        let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
        let mut pane = Pane::open(provider, &root, false).unwrap();
        pane.set_viewport_rows(2);
        pane.move_selection(99);
        assert_eq!(pane.selected, pane.entries.len() - 1);
        assert!(pane.selected < pane.offset + pane.viewport_rows);
        pane.move_selection(-99);
        assert_eq!((pane.selected, pane.offset), (0, 0));
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn pane_navigation_and_switching_are_independent() {
        let root = fixture();
        let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
        let left = Pane::open(provider.clone(), &root, false).unwrap();
        let right = Pane::open(provider, &root, false).unwrap();
        let mut app = AppState::new([left, right]);
        app.dispatch(Command::Down);
        app.dispatch(Command::Open);
        assert_eq!(
            app.panes[0].location,
            root.join("child").canonicalize().unwrap()
        );
        app.dispatch(Command::SwitchPane);
        assert_eq!(app.active, 1);
        assert_eq!(app.panes[1].location, root.canonicalize().unwrap());
        fs::remove_dir_all(root).unwrap();
    }
}
