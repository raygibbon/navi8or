//! Configurable Navi8or commands and generic key-sequence matching.
use crate::app::Command;
use crate::terminal::{Key, KeyCode, Modifiers};
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Context {
    Global,
    Panel,
    Menu,
    Help,
    Viewer,
    Dialog,
    Confirm,
    Info,
    Picker,
    Vault,
    Preferences,
}
#[derive(Clone, Debug)]
struct Binding {
    context: Context,
    keys: Vec<Key>,
    command: Command,
}
#[derive(Clone, Debug)]
pub struct Keymap {
    bindings: Vec<Binding>,
    pending: Option<(Context, Key)>,
}
impl Default for Keymap {
    fn default() -> Self {
        let mut map = Self {
            bindings: Vec::new(),
            pending: None,
        };
        for (context, sequence, command) in DEFAULTS {
            map.bind(*context, sequence, *command)
                .expect("valid default binding");
        }
        map
    }
}
impl Keymap {
    pub fn bind(
        &mut self,
        context: Context,
        sequence: &str,
        command: Command,
    ) -> Result<(), String> {
        let keys: Vec<Key> = sequence
            .split_whitespace()
            .map(parse_key)
            .collect::<Result<_, _>>()?;
        if keys.is_empty() || keys.len() > 2 {
            return Err(format!("invalid key sequence: {sequence}"));
        }
        self.bindings
            .retain(|binding| binding.context != context || binding.keys != keys);
        self.bindings.push(Binding {
            context,
            keys,
            command,
        });
        Ok(())
    }
    pub fn replace(
        &mut self,
        context: Context,
        command: Command,
        sequences: &[String],
    ) -> Result<(), String> {
        let mut parsed = Vec::new();
        for sequence in sequences {
            let keys: Vec<Key> = sequence
                .split_whitespace()
                .map(parse_key)
                .collect::<Result<_, _>>()?;
            if keys.is_empty() || keys.len() > 2 {
                return Err(format!("invalid key sequence: {sequence}"));
            }
            parsed.push(keys);
        }
        self.bindings
            .retain(|binding| binding.context != context || binding.command != command);
        for keys in parsed {
            self.bindings
                .retain(|binding| binding.context != context || binding.keys != keys);
            self.bindings.push(Binding {
                context,
                keys,
                command,
            });
        }
        Ok(())
    }
    pub fn reset(&mut self) {
        self.pending = None;
    }
    fn scopes(context: Context) -> Vec<Context> {
        match context {
            Context::Panel => vec![Context::Panel, Context::Global],
            Context::Global => vec![Context::Global],
            _ => vec![context], // Modal contexts capture unmatched input.
        }
    }
    pub fn resolve(&mut self, context: Context, key: Key) -> Option<Command> {
        let key = normalize(key);
        if let Some((_, prefix)) = self.pending.take() {
            if key.code == KeyCode::Escape {
                return None;
            }
            for scope in Self::scopes(context) {
                if let Some(binding) = self
                    .bindings
                    .iter()
                    .find(|b| b.context == scope && b.keys.as_slice() == [prefix, key])
                {
                    return Some(binding.command);
                }
            }
            return None;
        }
        for scope in Self::scopes(context) {
            if let Some(binding) = self
                .bindings
                .iter()
                .find(|b| b.context == scope && b.keys.first() == Some(&key))
            {
                if binding.keys.len() == 2 {
                    self.pending = Some((scope, key));
                    return None;
                }
                return Some(binding.command);
            }
        }
        None
    }
    pub fn label(&self, context: Context, command: Command) -> Option<String> {
        Self::scopes(context).iter().find_map(|scope| {
            self.bindings
                .iter()
                .filter(|b| b.context == *scope && b.command == command)
                .min_by_key(|b| b.keys.len())
                .map(|b| b.keys.iter().map(label_key).collect::<Vec<_>>().join(" "))
        })
    }
    pub fn function(&self, context: Context, number: u8) -> Option<Command> {
        Self::scopes(context).iter().find_map(|scope| {
            self.bindings
                .iter()
                .find(|b| b.context == *scope && b.keys == [Key::plain(KeyCode::F(number))])
                .map(|b| b.command)
        })
    }
}
fn normalize(mut key: Key) -> Key {
    if let KeyCode::Char(ch) = key.code {
        key.code = KeyCode::Char(ch.to_ascii_lowercase());
        key.modifiers.shift = false;
    }
    key
}
fn parse_key(text: &str) -> Result<Key, String> {
    let mut modifiers = Modifiers::default();
    let mut name = text;
    loop {
        if let Some(rest) = name.strip_prefix("Ctrl+") {
            modifiers.ctrl = true;
            name = rest;
        } else if let Some(rest) = name.strip_prefix("Alt+") {
            modifiers.alt = true;
            name = rest;
        } else if let Some(rest) = name.strip_prefix("Shift+") {
            modifiers.shift = true;
            name = rest;
        } else {
            break;
        }
    }
    let code = match name.to_ascii_lowercase().as_str() {
        "up" => KeyCode::Up,
        "down" => KeyCode::Down,
        "left" => KeyCode::Left,
        "right" => KeyCode::Right,
        "home" => KeyCode::Home,
        "end" => KeyCode::End,
        "pageup" | "pgup" => KeyCode::PageUp,
        "pagedown" | "pgdn" => KeyCode::PageDown,
        "insert" => KeyCode::Insert,
        "delete" => KeyCode::Delete,
        "enter" => KeyCode::Enter,
        "tab" => KeyCode::Tab,
        "backspace" => KeyCode::Backspace,
        "escape" | "esc" => KeyCode::Escape,
        other if other.starts_with('f') && other[1..].parse::<u8>().is_ok() => {
            KeyCode::F(other[1..].parse().unwrap())
        }
        _ if name.chars().count() == 1 => {
            KeyCode::Char(name.chars().next().unwrap().to_ascii_lowercase())
        }
        _ => return Err(format!("unknown key: {text}")),
    };
    Ok(normalize(Key { code, modifiers }))
}
fn label_key(key: &Key) -> String {
    let mut label = String::new();
    if key.modifiers.ctrl {
        label.push_str("Ctrl+");
    }
    if key.modifiers.alt {
        label.push_str("Alt+");
    }
    if key.modifiers.shift {
        label.push_str("Shift+");
    }
    label.push_str(&match key.code {
        KeyCode::Char(ch) => ch.to_ascii_uppercase().to_string(),
        KeyCode::F(number) => format!("F{number}"),
        KeyCode::Up => "Up".into(),
        KeyCode::Down => "Down".into(),
        KeyCode::Left => "Left".into(),
        KeyCode::Right => "Right".into(),
        KeyCode::Home => "Home".into(),
        KeyCode::End => "End".into(),
        KeyCode::PageUp => "PgUp".into(),
        KeyCode::PageDown => "PgDn".into(),
        KeyCode::Insert => "Insert".into(),
        KeyCode::Delete => "Delete".into(),
        KeyCode::Enter => "Enter".into(),
        KeyCode::Tab => "Tab".into(),
        KeyCode::Backspace => "Backspace".into(),
        KeyCode::Escape => "Esc".into(),
    });
    label
}
pub fn command_from_name(name: &str) -> Option<Command> {
    Some(match name {
        "help.open" | "help" => Command::Help,
        "menu.open" | "menu" => Command::Menu,
        "app.quit" | "quit" => Command::Quit,
        "cursor.up" | "panel.up" => Command::Up,
        "cursor.down" | "panel.down" => Command::Down,
        "cursor.left" => Command::Left,
        "cursor.right" => Command::Right,
        "cursor.home" => Command::Home,
        "cursor.end" => Command::End,
        "cursor.page_up" => Command::PageUp,
        "cursor.page_down" => Command::PageDown,
        "file.open" | "panel.open" => Command::Open,
        "file.view" | "view" => Command::View,
        "file.copy" | "copy" => Command::Copy,
        "file.edit" => Command::Edit,
        "file.move" => Command::Move,
        "file.rename" => Command::Rename,
        "file.delete" => Command::Delete,
        "file.mkdir" => Command::MkDir,
        "file.properties" => Command::Properties,
        "location.open" => Command::OpenLocation,
        "panel.filter" => Command::Filter,
        "panel.parent" | "parent" => Command::Parent,
        "panel.switch" | "pane_switch" => Command::SwitchPane,
        "panel.swap" => Command::SwapPanes,
        "navigation.back" | "panel.history_back" => Command::HistoryBack,
        "navigation.forward" | "panel.history_forward" => Command::HistoryForward,
        "panel.refresh" | "refresh" => Command::Refresh,
        "panel.hidden" => Command::ToggleHidden,
        "panel.sort.name" | "panel.sort_name" => Command::SortName,
        "panel.sort.size" | "panel.sort_size" => Command::SortSize,
        "panel.sort.date" | "panel.sort_date" => Command::SortModified,
        "panel.brief" => Command::Brief,
        "panel.full" => Command::Full,
        "config.open" => Command::OpenConfig,
        "config.reload" => Command::ReloadConfig,
        "theme.info" => Command::ThemeInfo,
        "remote.connect" => Command::RepositoryOpen,
        "repository.add" => Command::RepositoryAdd,
        "repository.edit" => Command::RepositoryEdit,
        "repository.remove" => Command::RepositoryRemove,
        "vault.open" => Command::Vault,
        "profile.preferences" => Command::Preferences,
        "profile.save" => Command::ProfileSave,
        "profile.save_as" => Command::ProfileSaveAs,
        "help.about" => Command::About,
        "dialog.accept" => Command::Accept,
        "dialog.cancel" => Command::CloseOverlay,
        "viewer.close"
        | "viewer.toggle_fullscreen"
        | "viewer.open_link"
        | "viewer.next_link"
        | "viewer.previous_link"
        | "viewer.back"
        | "search.find"
        | "search.next"
        | "search.previous"
        | "viewer.goto"
        | "viewer.wrap"
        | "viewer.lines"
        | "vault.new"
        | "vault.edit"
        | "vault.delete"
        | "vault.unlock"
        | "vault.lock"
        | "profile.apply"
        | "profile.key_append"
        | "profile.key_sequence"
        | "resource.download"
        | "text.backspace"
        | "text.delete"
        | "text.insert"
        | "text.paste"
        | "text.copy"
        | "text.cut"
        | "text.select_all"
        | "cursor.left_fast"
        | "cursor.right_fast" => Command::Deferred(match name {
            "viewer.close" => "viewer.close",
            "viewer.toggle_fullscreen" => "viewer.toggle_fullscreen",
            "viewer.open_link" => "viewer.open_link",
            "viewer.next_link" => "viewer.next_link",
            "viewer.previous_link" => "viewer.previous_link",
            "viewer.back" => "viewer.back",
            "search.find" => "search.find",
            "search.next" => "search.next",
            "search.previous" => "search.previous",
            "viewer.goto" => "viewer.goto",
            "viewer.wrap" => "viewer.wrap",
            "viewer.lines" => "viewer.lines",
            "vault.new" => "vault.new",
            "vault.edit" => "vault.edit",
            "vault.delete" => "vault.delete",
            "vault.unlock" => "vault.unlock",
            "vault.lock" => "vault.lock",
            "profile.apply" => "profile.apply",
            "profile.key_append" => "profile.key_append",
            "profile.key_sequence" => "profile.key_sequence",
            "resource.download" => "resource.download",
            "text.backspace" => "text.backspace",
            "text.delete" => "text.delete",
            "text.insert" => "text.insert",
            "text.paste" => "text.paste",
            "text.copy" => "text.copy",
            "text.cut" => "text.cut",
            "text.select_all" => "text.select_all",
            "cursor.left_fast" => "cursor.left_fast",
            "cursor.right_fast" => "cursor.right_fast",
            _ => unreachable!(),
        }),
        _ => return None,
    })
}
const DEFAULTS: &[(Context, &str, Command)] = &[
    (Context::Global, "Ctrl+Q", Command::Quit),
    (Context::Global, "F10", Command::Quit),
    (Context::Global, "F1", Command::Help),
    (Context::Global, "Ctrl+H", Command::Help),
    (Context::Global, "Ctrl+T P", Command::Preferences),
    // Ctrl+\ is Navi8or's TDX-derived menu key, not a process-quit request.
    (Context::Global, "Ctrl+\\", Command::Menu),
    (Context::Global, "F2", Command::Menu),
    (Context::Global, "Tab", Command::SwitchPane),
    (Context::Global, "Ctrl+P S", Command::SwitchPane),
    (Context::Panel, "F3", Command::View),
    (Context::Panel, "F4", Command::Edit),
    (Context::Panel, "F5", Command::Copy),
    (Context::Panel, "F6", Command::Move),
    (Context::Panel, "F7", Command::MkDir),
    (Context::Panel, "F8", Command::Delete),
    (Context::Panel, "Ctrl+L", Command::OpenLocation),
    (Context::Panel, "Up", Command::Up),
    (Context::Panel, "Down", Command::Down),
    (Context::Panel, "Left", Command::Left),
    (Context::Panel, "Right", Command::Right),
    (Context::Panel, "Home", Command::Home),
    (Context::Panel, "End", Command::End),
    (Context::Panel, "PageUp", Command::PageUp),
    (Context::Panel, "PageDown", Command::PageDown),
    (Context::Panel, "Enter", Command::Open),
    (Context::Panel, "Ctrl+PgDn", Command::Open),
    (Context::Panel, "Backspace", Command::Parent),
    (Context::Panel, "Ctrl+PgUp", Command::Parent),
    (Context::Panel, "Alt+Up", Command::Parent),
    (Context::Panel, "Alt+Left", Command::HistoryBack),
    (Context::Panel, "Alt+Right", Command::HistoryForward),
    (Context::Panel, "Ctrl+U", Command::SwapPanes),
    (Context::Panel, "/", Command::Filter),
    (Context::Panel, "Escape", Command::CancelJob),
    (Context::Panel, "Ctrl+F C", Command::Copy),
    (Context::Panel, "Ctrl+F M", Command::Move),
    (Context::Panel, "Ctrl+F R", Command::Rename),
    (Context::Panel, "Ctrl+F D", Command::Delete),
    (Context::Panel, "Ctrl+F K", Command::MkDir),
    (Context::Panel, "Ctrl+F O", Command::Open),
    (Context::Panel, "Ctrl+N P", Command::Parent),
    (Context::Panel, "Ctrl+N O", Command::OpenLocation),
    (Context::Panel, "Ctrl+N R", Command::Refresh),
    (Context::Panel, "Ctrl+N B", Command::HistoryBack),
    (Context::Panel, "Ctrl+N F", Command::HistoryForward),
    (Context::Panel, "Ctrl+V V", Command::View),
    (Context::Panel, "Ctrl+S F", Command::Filter),
    (Context::Panel, "Ctrl+R O", Command::RepositoryOpen),
    (Context::Panel, "Ctrl+R R", Command::Refresh),
    (Context::Panel, "Ctrl+R V", Command::Vault),
    (Context::Panel, "Ctrl+P W", Command::SwapPanes),
    (Context::Panel, "Ctrl+P B", Command::Brief),
    (Context::Panel, "Ctrl+P F", Command::Full),
    (Context::Panel, "Ctrl+T C", Command::OpenConfig),
    (Context::Panel, "Ctrl+T R", Command::ReloadConfig),
    (Context::Panel, "Ctrl+T T", Command::ThemeInfo),
    (Context::Menu, "Up", Command::Up),
    (Context::Menu, "Down", Command::Down),
    (Context::Menu, "Left", Command::Left),
    (Context::Menu, "Right", Command::Right),
    (Context::Menu, "Ctrl+Left", Command::Left),
    (Context::Menu, "Ctrl+Right", Command::Right),
    (Context::Menu, "Home", Command::Home),
    (Context::Menu, "End", Command::End),
    (Context::Menu, "PageUp", Command::PageUp),
    (Context::Menu, "PageDown", Command::PageDown),
    (Context::Menu, "Enter", Command::Accept),
    (Context::Menu, "Escape", Command::CloseOverlay),
    (Context::Menu, "Ctrl+Q", Command::CloseOverlay),
    (Context::Help, "Escape", Command::CloseOverlay),
    (Context::Help, "F1", Command::CloseOverlay),
];

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn global_menu_fallback_and_modal_capture() {
        let mut map = Keymap::default();
        assert_eq!(
            map.resolve(Context::Panel, parse_key("Ctrl+\\").unwrap()),
            Some(Command::Menu)
        );
        assert_eq!(
            map.resolve(Context::Panel, parse_key("F2").unwrap()),
            Some(Command::Menu)
        );
        assert_eq!(map.resolve(Context::Menu, parse_key("F2").unwrap()), None);
        assert_eq!(
            map.resolve(Context::Menu, parse_key("Ctrl+Q").unwrap()),
            Some(Command::CloseOverlay)
        );
        map.bind(Context::Panel, "F2", Command::Refresh).unwrap();
        assert_eq!(
            map.resolve(Context::Panel, parse_key("F2").unwrap()),
            Some(Command::Refresh)
        );
    }
    #[test]
    fn defaults_and_sequences() {
        let mut map = Keymap::default();
        assert_eq!(
            map.resolve(
                Context::Panel,
                Key {
                    code: KeyCode::Char('n'),
                    modifiers: Modifiers {
                        ctrl: true,
                        ..Modifiers::default()
                    }
                }
            ),
            None
        );
        assert_eq!(
            map.resolve(Context::Panel, Key::plain(KeyCode::Char('r'))),
            Some(Command::Refresh)
        );
        assert_eq!(map.function(Context::Panel, 4), Some(Command::Edit));
        assert_eq!(
            map.label(Context::Panel, Command::Copy).as_deref(),
            Some("F5")
        );
    }
    #[test]
    fn all_c_command_families_resolve() {
        for (sequence, expected) in [
            ("Ctrl+N P", Command::Parent),
            ("Ctrl+N R", Command::Refresh),
            ("Ctrl+N B", Command::HistoryBack),
            ("Ctrl+N F", Command::HistoryForward),
            ("Ctrl+F C", Command::Copy),
            ("Ctrl+V V", Command::View),
            ("Ctrl+P S", Command::SwitchPane),
            ("Ctrl+P W", Command::SwapPanes),
        ] {
            let mut map = Keymap::default();
            let mut keys = sequence
                .split_whitespace()
                .map(|text| parse_key(text).unwrap());
            assert_eq!(
                map.resolve(Context::Panel, keys.next().unwrap()),
                None,
                "{sequence}"
            );
            assert_eq!(
                map.resolve(Context::Panel, keys.next().unwrap()),
                Some(expected),
                "{sequence}"
            );
        }
    }
    #[test]
    fn configured_binding_replaces_default() {
        let mut map = Keymap::default();
        map.replace(Context::Panel, Command::Copy, &["Ctrl+K C".into()])
            .unwrap();
        assert_eq!(map.function(Context::Panel, 5), None);
        assert_eq!(
            map.label(Context::Panel, Command::Copy).as_deref(),
            Some("Ctrl+K C")
        );
    }
}
