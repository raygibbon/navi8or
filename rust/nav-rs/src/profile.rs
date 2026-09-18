//! Reads Navi8or's existing TOML profile and normal configuration format.
use crate::keys::{Context, Keymap, command_from_name};
use crate::terminal::{Attributes, Cell, Color};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

const PALETTE: [u8; 16] = [
    16, 19, 34, 37, 124, 127, 130, 145, 240, 21, 46, 51, 196, 201, 226, 231,
];
const NAMES: [&str; 16] = [
    "black",
    "blue",
    "green",
    "cyan",
    "red",
    "magenta",
    "brown",
    "light_gray",
    "dark_gray",
    "light_blue",
    "light_green",
    "light_cyan",
    "light_red",
    "light_magenta",
    "yellow",
    "white",
];
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Role {
    Background,
    Surface,
    Border,
    BorderActive,
    Text,
    TextDim,
    Selection,
    SelectionInactive,
    PaneTitle,
    PaneTitleActive,
    Path,
    ColumnHeader,
    Status,
    Keybar,
    KeybarKey,
    KeybarDisabled,
    Menu,
    MenuSelected,
    Directory,
    File,
    Error,
    Warning,
    Progress,
}
impl Role {
    const ALL: [Self; 23] = [
        Self::Background,
        Self::Surface,
        Self::Border,
        Self::BorderActive,
        Self::Text,
        Self::TextDim,
        Self::Selection,
        Self::SelectionInactive,
        Self::PaneTitle,
        Self::PaneTitleActive,
        Self::Path,
        Self::ColumnHeader,
        Self::Status,
        Self::Keybar,
        Self::KeybarKey,
        Self::KeybarDisabled,
        Self::Menu,
        Self::MenuSelected,
        Self::Directory,
        Self::File,
        Self::Error,
        Self::Warning,
        Self::Progress,
    ];
    fn name(self) -> &'static str {
        match self {
            Self::Background => "background",
            Self::Surface => "surface",
            Self::Border => "border",
            Self::BorderActive => "border_active",
            Self::Text => "text",
            Self::TextDim => "text_dim",
            Self::Selection => "selection",
            Self::SelectionInactive => "selection_inactive",
            Self::PaneTitle => "pane_title",
            Self::PaneTitleActive => "pane_title_active",
            Self::Path => "path",
            Self::ColumnHeader => "column_header",
            Self::Status => "status",
            Self::Keybar => "keybar",
            Self::KeybarKey => "keybar_key",
            Self::KeybarDisabled => "keybar_disabled",
            Self::Menu => "menu",
            Self::MenuSelected => "menu_selected",
            Self::Directory => "directory",
            Self::File => "file",
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Progress => "progress",
        }
    }
    fn index(self) -> usize {
        Self::ALL.iter().position(|role| *role == self).unwrap()
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UiStyle {
    Modern,
    Classic,
}
#[derive(Clone, Debug)]
pub struct Theme {
    colors: [(u8, u8); 23],
    pub style: UiStyle,
    pub frame_style: String,
    pub name: String,
}
impl Theme {
    pub fn bundled(name: &str) -> io::Result<Self> {
        let contents = match name {
            "classic-dos" => include_str!("../../../themes/classic-dos.toml"),
            "solar-light" => include_str!("../../../themes/solar-light.toml"),
            "monochrome" => include_str!("../../../themes/monochrome.toml"),
            "solar-dark" => include_str!("../../../themes/solar-dark.toml"),
            _ => {
                return Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("unknown theme {name}"),
                ));
            }
        };
        let doc: toml::Value = contents.parse().map_err(io::Error::other)?;
        let mut theme = Self {
            colors: [(7, 0); 23],
            style: UiStyle::Modern,
            frame_style: "single".into(),
            name: name.into(),
        };
        theme.apply(&doc)?;
        Ok(theme)
    }
    pub fn cell(&self, role: Role) -> Cell {
        let (foreground, background) = self.colors[role.index()];
        Cell {
            character: ' ',
            foreground: Color(PALETTE[foreground as usize]),
            background: Color(PALETTE[background as usize]),
            attributes: Attributes {
                bold: matches!(role, Role::PaneTitleActive | Role::KeybarKey),
            },
        }
    }
    pub fn apply(&mut self, doc: &toml::Value) -> io::Result<()> {
        if let Some(name) = doc
            .get("profile")
            .and_then(|p| p.get("name"))
            .and_then(toml::Value::as_str)
        {
            self.name = name.into();
        }
        if let Some(style) = doc
            .get("ui")
            .and_then(|ui| ui.get("style"))
            .and_then(toml::Value::as_str)
        {
            self.style = if style.eq_ignore_ascii_case("classic") {
                UiStyle::Classic
            } else {
                UiStyle::Modern
            };
        }
        if let Some(frame) = doc
            .get("ui")
            .and_then(|ui| ui.get("frame"))
            .and_then(|f| f.get("style"))
            .and_then(toml::Value::as_str)
        {
            self.frame_style = frame.into();
        }
        for role in Role::ALL {
            if let Some(colors) = doc.get("colors").and_then(|colors| colors.get(role.name())) {
                for (key, index) in [("foreground", 0), ("background", 1)] {
                    if let Some(value) = colors.get(key).and_then(toml::Value::as_str) {
                        let number = NAMES
                            .iter()
                            .position(|name| name.eq_ignore_ascii_case(value))
                            .ok_or_else(|| {
                                io::Error::new(
                                    io::ErrorKind::InvalidData,
                                    format!("unknown color {value}"),
                                )
                            })? as u8;
                        if index == 0 {
                            self.colors[role.index()].0 = number;
                        } else {
                            self.colors[role.index()].1 = number;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}
#[derive(Clone, Debug)]
pub struct Profile {
    pub theme: Theme,
    pub keymap: Keymap,
    pub show_hidden: bool,
    pub menu_remember_position: bool,
    pub source: Option<PathBuf>,
}
impl Profile {
    pub fn load(explicit: Option<&Path>) -> io::Result<Self> {
        Self::load_from(explicit, config_dir())
    }
    fn load_from(explicit: Option<&Path>, directory: Option<PathBuf>) -> io::Result<Self> {
        let normal = directory
            .as_ref()
            .map(|directory| directory.join("nav.toml"));
        let normal_document = normal
            .as_deref()
            .filter(|path| path.exists())
            .map(read_toml)
            .transpose()?;
        let theme_name = normal_document
            .as_ref()
            .and_then(|doc| {
                doc.get("theme")
                    .and_then(|value| value.get("name"))
                    .or_else(|| doc.get("app").and_then(|app| app.get("theme")))
            })
            .and_then(toml::Value::as_str)
            .unwrap_or("solar-dark");
        let theme = Theme::bundled(theme_name).or_else(|_| {
            let path = directory
                .as_ref()
                .ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::NotFound,
                        "configuration directory unavailable",
                    )
                })?
                .join("themes")
                .join(format!("{theme_name}.toml"));
            let document = read_toml(&path)?;
            let mut theme = Theme::bundled("solar-dark")?;
            theme.apply(&document)?;
            Ok::<Theme, io::Error>(theme)
        })?;
        let mut profile = Self {
            theme,
            keymap: Keymap::default(),
            show_hidden: false,
            menu_remember_position: true,
            source: None,
        };
        if let Some(doc) = &normal_document {
            profile.apply(doc)?;
            profile.show_hidden = doc
                .get("app")
                .and_then(|app| app.get("show_hidden"))
                .and_then(toml::Value::as_bool)
                .unwrap_or(false);
            profile.source = normal.clone();
        }
        if let Some(path) = explicit {
            let doc = read_toml(path)?;
            profile.apply(&doc)?;
            profile.source = Some(path.into());
        }
        Ok(profile)
    }
    fn apply(&mut self, doc: &toml::Value) -> io::Result<()> {
        self.theme.apply(doc)?;
        if let Some(value) = doc
            .get("menu")
            .and_then(|menu| menu.get("remember_position"))
            .and_then(toml::Value::as_bool)
        {
            self.menu_remember_position = value;
        }
        if let Some(shortcuts) = doc.get("keys").and_then(toml::Value::as_table) {
            for (name, value) in shortcuts {
                if let Some(command) = command_from_name(name) {
                    let sequences = sequences(value);
                    if !sequences.is_empty() {
                        self.keymap
                            .replace(
                                if matches!(
                                    command,
                                    crate::app::Command::Help
                                        | crate::app::Command::Menu
                                        | crate::app::Command::Quit
                                        | crate::app::Command::SwitchPane
                                        | crate::app::Command::Preferences
                                ) {
                                    Context::Global
                                } else {
                                    Context::Panel
                                },
                                command,
                                &sequences,
                            )
                            .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
                    }
                }
            }
        }
        for (scope, context) in [
            ("panel", Context::Panel),
            ("global", Context::Global),
            ("menu", Context::Menu),
            ("viewer", Context::Viewer),
            ("dialog", Context::Dialog),
            ("confirm", Context::Confirm),
            ("info", Context::Info),
            ("picker", Context::Picker),
            ("vault", Context::Vault),
            ("preferences", Context::Preferences),
        ] {
            if let Some(table) = doc
                .get("keys")
                .and_then(|keys| keys.get(scope))
                .and_then(toml::Value::as_table)
            {
                for (sequence, value) in table {
                    if sequence == "commands" {
                        continue;
                    }
                    if let Some(command) = value.as_str().and_then(command_from_name) {
                        self.keymap
                            .bind(context, sequence, command)
                            .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
                    }
                }
            }
            if let Some(commands) = doc
                .get("keys")
                .and_then(|keys| keys.get(scope))
                .and_then(|scope| scope.get("commands"))
                .and_then(toml::Value::as_table)
            {
                for (name, value) in commands {
                    if let Some(command) = command_from_name(name) {
                        let sequences = sequences(value);
                        if !sequences.is_empty() {
                            self.keymap
                                .replace(context, command, &sequences)
                                .map_err(|error| {
                                    io::Error::new(io::ErrorKind::InvalidData, error)
                                })?;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}
fn read_toml(path: &Path) -> io::Result<toml::Value> {
    fs::read_to_string(path)?.parse().map_err(io::Error::other)
}
fn sequences(value: &toml::Value) -> Vec<String> {
    if let Some(single) = value.as_str() {
        vec![single.into()]
    } else if let Some(array) = value.as_array() {
        array
            .iter()
            .filter_map(toml::Value::as_str)
            .map(str::to_owned)
            .collect()
    } else {
        Vec::new()
    }
}
pub fn config_dir() -> Option<PathBuf> {
    if cfg!(windows) {
        std::env::var_os("APPDATA")
            .map(PathBuf::from)
            .map(|path| path.join("Navi8or"))
    } else {
        std::env::var_os("XDG_CONFIG_HOME")
            .filter(|value| !value.is_empty())
            .map(PathBuf::from)
            .or_else(|| {
                std::env::var_os("HOME")
                    .map(PathBuf::from)
                    .map(|path| path.join(".config"))
            })
            .map(|path| path.join("nav"))
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bundled_classic_and_modern_have_distinct_palette() {
        let classic = Theme::bundled("classic-dos").unwrap();
        let modern = Theme::bundled("solar-dark").unwrap();
        assert_eq!(classic.style, UiStyle::Classic);
        assert_ne!(
            classic.cell(Role::Background),
            modern.cell(Role::Background)
        );
    }
    #[test]
    fn shipped_classic_profile_loads_scoped_navigation_bindings() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../themes/classic-dos.toml");
        let profile = Profile::load_from(Some(&path), None).unwrap();
        assert_eq!(
            profile
                .keymap
                .label(Context::Panel, crate::app::Command::HistoryBack)
                .as_deref(),
            Some("Alt+Left")
        );
        assert_eq!(
            profile
                .keymap
                .label(Context::Panel, crate::app::Command::Refresh)
                .as_deref(),
            Some("Ctrl+N R")
        );
    }
    #[test]
    fn classic_global_menu_binding_keeps_scope() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../themes/classic-dos.toml");
        let profile = Profile::load_from(Some(&path), None).unwrap();
        assert_eq!(
            profile
                .keymap
                .label(Context::Global, crate::app::Command::Menu)
                .as_deref(),
            Some("Ctrl+\\")
        );
        assert_eq!(
            profile
                .keymap
                .label(Context::Panel, crate::app::Command::Menu)
                .as_deref(),
            Some("Ctrl+\\")
        );
        assert_eq!(
            profile
                .keymap
                .label(
                    Context::Viewer,
                    crate::app::Command::Deferred("viewer.close")
                )
                .as_deref(),
            Some("Esc")
        );
        assert_eq!(
            profile.keymap.label(
                Context::Panel,
                crate::app::Command::Deferred("viewer.close")
            ),
            None
        );
        let mut map = profile.keymap;
        assert_eq!(
            map.resolve(
                Context::Menu,
                crate::terminal::Key::plain(crate::terminal::KeyCode::F(2))
            ),
            None
        );
    }
    #[test]
    fn missing_normal_configuration_uses_bundled_default() {
        let profile = Profile::load_from(
            None,
            Some(PathBuf::from("/nonexistent/nav-rs-profile-test")),
        )
        .unwrap();
        assert_eq!(profile.theme.style, UiStyle::Modern);
        assert!(profile.source.is_none());
    }
    #[test]
    fn explicit_profile_layers_on_normal_config_and_rebinds_keys() {
        let root = std::env::temp_dir().join(format!(
            "nav-rs-profile-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&root).unwrap();
        fs::write(
            root.join("nav.toml"),
            "[theme]\nname='classic-dos'\n[app]\nshow_hidden=true\n",
        )
        .unwrap();
        let explicit = root.join("personal.toml");
        fs::write(&explicit,"[profile]\nformat=2\nname='Personal'\n[colors.selection]\nforeground='yellow'\n[keys]\ncopy=['Ctrl+K C']\n").unwrap();
        let profile = Profile::load_from(Some(&explicit), Some(root.clone())).unwrap();
        assert_eq!(profile.theme.style, UiStyle::Classic);
        assert_eq!(profile.theme.name, "Personal");
        assert!(profile.show_hidden);
        assert_eq!(
            profile
                .keymap
                .label(Context::Panel, crate::app::Command::Copy)
                .as_deref(),
            Some("Ctrl+K C")
        );
        assert_eq!(profile.keymap.function(Context::Panel, 5), None);
        fs::remove_dir_all(root).unwrap();
    }
}
