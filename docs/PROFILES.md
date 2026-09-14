# Profiles and Preferences

Navi8or keeps one live UI configuration. TOML and the Preferences editor both
change that configuration; providers, repositories, credentials and network
settings stay in normal `nav.toml`.

```sh
nav
nav -i themes/classic-dos.toml
nav -i ~/.config/nav/profiles/my-profile.toml
```

`-i` opens exactly that path. Relative paths use the current working directory;
unreadable, malformed or invalid files report their path and reason without
substitution. Settings apply in order: compiled defaults → normal configuration
→ explicit profile → temporary live edits. CLI left/right directory arguments
still select startup locations. Missing profile properties inherit earlier layers.

The four templates are Classic DOS, Solar Dark (the unchanged default), Solar
Light and Monochrome. Copy one or choose **Profile Template** in Preferences.
Normal editing does not overwrite a bundled template.

## Editing inside Navi8or

Open **Options → Preferences**, or use the default `Ctrl+T P` sequence.
One scrollable editor contains Profile Template/Name, Colours, Layout, Panels,
Viewer, Key Bindings and Shortcut Display. Colours expose the semantic roles
and their foreground/background in the existing 16-color model. Enter/Left/Right
cycle real settings; date format and profile name use existing text fields.
No internal-editor presentation options are offered: F4 launches the configured
external editor, whose operational arguments stay in `nav.toml`.

Changes preview immediately. **Apply** (default F4 inside Preferences) keeps the
live configuration and closes the editor. **Cancel** on its home page restores
the opening configuration and pane presentation; Escape within a subpage first
returns home. **Save** applies, saves atomically and closes on success. A canceled
or failed save leaves the editor open. The asterisk marks unsaved differences;
restoring saved values clears it.

The Key Bindings page has a context row (Panel, Viewer, Global, dialogs, etc.).
Each command shows its effective bindings and an inherited/override/unbound
indicator. Enter starts physical key capture. Escape always cancels capture.
Default editor actions are Delete to clear that command's bindings in the chosen
context, F5 to append a binding, and F6 to capture a two-key replacement. These
editor actions themselves are command mappings, not fixed action handlers.
A conflicting capture describes the key and both commands; Yes displaces the
conflicting binding, No preserves the map. Independent modal contexts may reuse
keys. Clear is saved as an empty binding array.

**Options → Save Profile / Save Profile As** also work after Apply. An ordinary
Save updates the active personal file. Starting from a template or normal config
prompts for a personal name (suggestion: Custom). Save As always prompts. Storage
uses the existing platform configuration directory:

- Linux: `$XDG_CONFIG_HOME/nav/profiles/NAME.toml`, otherwise
  `~/.config/nav/profiles/NAME.toml`.
- Windows: `%APPDATA%/Navi8or/profiles/NAME.toml`.

Use `-i` with that saved path on the next launch; Save does not rewrite normal
configuration to select it automatically. Unsaved live changes prompt before
reload, profile replacement or quit. Reload reads TOML again, validates it and
rebuilds the live map; a failure preserves the current configuration. Manually
editing the saved TOML and choosing **File → Reload Configuration** remains a
normal workflow.

## Hand-editable TOML

A small profile can be:

```toml
[profile]
name = "My profile"

[colors]
directory = "yellow"

[panes]
show_size = true
date_format = "%Y-%m-%d %H:%M"

[keys]
copy = ["F5", "Ctrl+C"]
refresh = "Ctrl+R R"

[shortcuts]
show_function_bar = true
show_menu_keys = true
show_dialog_keys = true
show_help_keys = true
```

`[shortcuts]` visibility does not disable bindings. Its function-bar setting
wins over the legacy `[layout] show_function_bar` alias in the same layer.
Menu hints use an effective binding, Help lists effective bindings grouped by
command/context, and the F bar resolves each F1–F12 to its current command.
There is no fixed "slot 5 is Copy" table. Long binding lists are bounded for
screen rendering and indicate truncation.

Flat `[keys]` accepts short operation names and quoted canonical names from
[the shared command metadata](../include/nav_commands.def). Multiple strings and
two-key sequences are supported. For explicit context-specific command overrides:

```toml
[keys.viewer.commands]
"search.next" = ["F5", "Ctrl+N"]
"search.previous" = []
```

This uses the same command/sequence parser as flat bindings. The historical
Legacy compatibility syntax `[keys.viewer] "F5" = "search.next"` remains supported. Explicit duplicate
or prefix-conflicting keys in overlapping contexts are rejected with the key
and commands. Saves use sparse command arrays rather than dumping inherited defaults.

Key syntax includes F1–F12, Enter, Escape/Esc, Tab, Backspace, Delete, Insert,
Home, End, PageUp/PgUp, PageDown/PgDn, arrows, ASCII characters, Ctrl/Alt/Shift
modifiers, and one/two strokes. Case is normalized. Modified special keys and
some Ctrl/Shift combinations require backend/terminal support; terminals can
alias combinations or consume them before Navi8or receives them. Unicode text
input works independently of the ASCII binding syntax. Physical capture uses
the same normalized representation and refuses unrepresentable keys.

## Persistence and compatibility

The vendored TOML parser has no format-preserving writer. Navi8or compares live
settings with the original effective file and patches changed known assignments,
adding only needed sections. Existing flat key names, comments, unrelated order,
and unknown future sections survive. Reassigned historical sequence entries are
removed while retaining their comments. Changed context bindings use readable
command arrays; unrelated default bindings are not dumped into the file.

Updates validate through the same profile loader, verify the effective values,
then sync and atomically replace the destination. Complex multiline known values
and unsupported quoted spellings may require manual editing; unsafe updates fail
without replacing the original. Unrelated multiline values remain untouched.
This is a scoped updater, not a general TOML formatting engine. Converting a
format-1 palette to a personal format-2 profile materializes its semantic colors.
Existing palette formats and legacy full `-i` configuration files remain readable;
Save creates a separate UI file instead of overwriting operational configuration.

See [the profile color/layout reference](THEME_FORMAT.md) for supported roles and
[default commands](INPUT_COMMANDS.md) for the unchanged base keyboard workflow.

The four [bundled TOMLs](../themes/README.md) explicitly show primary keys and
shortcut-display defaults. Solar Dark remains the compiled startup default;
Classic DOS is an alternate profile. External editor settings stay in normal
configuration. See [URL/location commands](LOCATIONS.md) for the bindable Download action.
