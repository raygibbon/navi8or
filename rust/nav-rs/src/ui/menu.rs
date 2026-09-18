use crate::app::Command;
use crate::keys::{Context, Keymap};
pub fn help_lines(map: &Keymap) -> Vec<String> {
    let label = |command| {
        map.label(Context::Panel, command)
            .unwrap_or_else(|| "Unbound".into())
    };
    vec![
        " Navi8or keys ".into(),
        format!(
            "{} Help   {} Menu   {} View",
            label(Command::Help),
            label(Command::Menu),
            label(Command::View)
        ),
        format!(
            "{} Copy   {} Quit",
            label(Command::Copy),
            label(Command::Quit)
        ),
        format!(
            "{} Switch pane   {} Open",
            label(Command::SwitchPane),
            label(Command::Open)
        ),
        format!(
            "{} Parent   {} Refresh",
            label(Command::Parent),
            label(Command::Refresh)
        ),
        "Esc closes this help".into(),
    ]
}
#[derive(Clone, Copy, Debug)]
pub struct MenuItem {
    pub label: &'static str,
    pub command: Option<Command>,
    pub accelerator: char,
}
#[derive(Clone, Copy, Debug)]
pub struct Menu {
    pub heading: &'static str,
    pub items: &'static [MenuItem],
}
macro_rules! item {
    ($label:expr,$command:ident,$key:expr) => {
        MenuItem {
            label: $label,
            command: Some(Command::$command),
            accelerator: $key,
        }
    };
}
const SEP: MenuItem = MenuItem {
    label: "",
    command: None,
    accelerator: ' ',
};
pub const MENUS: &[Menu] = &[
    Menu {
        heading: "File",
        items: &[
            item!("Enter URL / Location...", OpenLocation, 'o'),
            item!("Properties", Properties, 'p'),
            SEP,
            item!("Open Configuration", OpenConfig, 'c'),
            item!("Reload Configuration", ReloadConfig, 'r'),
            item!("Current Theme", ThemeInfo, 't'),
            SEP,
            item!("Quit", Quit, 'q'),
        ],
    },
    Menu {
        heading: "View",
        items: &[
            item!("Refresh", Refresh, 'r'),
            item!("Show Hidden", ToggleHidden, 'h'),
            SEP,
            item!("Brief", Brief, 'b'),
            item!("Full", Full, 'f'),
            SEP,
            item!("Sort By Name", SortName, 'n'),
            item!("Sort By Size", SortSize, 's'),
            item!("Sort By Date", SortModified, 'd'),
        ],
    },
    Menu {
        heading: "Command",
        items: &[
            item!("View", View, 'v'),
            item!("Edit", Edit, 'e'),
            item!("Copy", Copy, 'c'),
            item!("Move/Rename", Move, 'm'),
            item!("Make Directory", MkDir, 'a'),
            item!("Delete", Delete, 'd'),
            SEP,
            item!("Filter", Filter, 'f'),
        ],
    },
    Menu {
        heading: "Repositories",
        items: &[
            item!("Open Repository", RepositoryOpen, 'o'),
            item!("Add Repository", RepositoryAdd, 'a'),
            item!("Edit Repository", RepositoryEdit, 'e'),
            item!("Remove Repository", RepositoryRemove, 'r'),
            SEP,
            item!("Credential Vault", Vault, 'v'),
        ],
    },
    Menu {
        heading: "Options",
        items: &[
            item!("Preferences", Preferences, 'p'),
            item!("Save Profile", ProfileSave, 's'),
            item!("Save Profile As", ProfileSaveAs, 'a'),
        ],
    },
    Menu {
        heading: "Help",
        items: &[item!("Keys", Help, 'k'), item!("About Navi8or", About, 'a')],
    },
];
#[derive(Clone, Debug, Default)]
pub struct MenuState {
    pub active_major: usize,
    pub active_item: [usize; 6],
}
impl MenuState {
    pub fn selected(&self) -> MenuItem {
        MENUS[self.active_major].items[self.active_item[self.active_major]]
    }
    pub fn act(&mut self, command: Option<Command>, key: crate::terminal::Key) -> Option<Command> {
        use crate::terminal::KeyCode;
        let menu = MENUS[self.active_major];
        match command {
            Some(Command::Left) => {
                self.active_major = (self.active_major + MENUS.len() - 1) % MENUS.len()
            }
            Some(Command::Right) => self.active_major = (self.active_major + 1) % MENUS.len(),
            Some(Command::Up | Command::Down) => {
                let direction = if command == Some(Command::Down) {
                    1
                } else {
                    menu.items.len() - 1
                };
                let mut next = self.active_item[self.active_major];
                loop {
                    next = (next + direction) % menu.items.len();
                    if menu.items[next].command.is_some() {
                        break;
                    }
                }
                self.active_item[self.active_major] = next;
            }
            Some(Command::Home | Command::PageUp) => self.active_item[self.active_major] = 0,
            Some(Command::End | Command::PageDown) => {
                self.active_item[self.active_major] = menu.items.len() - 1
            }
            Some(Command::Accept) => return self.selected().command.filter(|c| c.available()),
            Some(other) if other != Command::CloseOverlay => {
                if let Some((index, _)) = menu
                    .items
                    .iter()
                    .enumerate()
                    .find(|(_, item)| item.command == Some(other) && other.available())
                {
                    self.active_item[self.active_major] = index;
                    return Some(other);
                }
            }
            _ => {}
        }
        if command.is_none()
            && !key.modifiers.ctrl
            && !key.modifiers.alt
            && let KeyCode::Char(ch) = key.code
            && let Some((index, item)) = menu.items.iter().enumerate().find(|(_, item)| {
                item.command.is_some_and(Command::available)
                    && item.accelerator.eq_ignore_ascii_case(&ch)
            })
        {
            self.active_item[self.active_major] = index;
            return item.command;
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::terminal::{Key, KeyCode};
    #[test]
    fn separators_are_skipped_and_unavailable_items_do_not_activate() {
        let mut state = MenuState::default();
        let key = Key::plain(KeyCode::Down);
        state.act(Some(Command::Down), key);
        state.act(Some(Command::Down), key);
        assert_eq!(state.active_item[0], 3); // skips File separator
        assert_eq!(
            state.act(Some(Command::Accept), Key::plain(KeyCode::Enter)),
            None
        );
        assert_eq!(
            state.act(None, Key::plain(KeyCode::Char('q'))),
            Some(Command::Quit)
        );
        state.active_major = 1;
        assert_eq!(
            state.act(None, Key::plain(KeyCode::Char('h'))),
            Some(Command::ToggleHidden)
        );
    }
}
