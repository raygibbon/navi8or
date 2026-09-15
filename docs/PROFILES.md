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
The category sidebar contains General, Panels, Viewer, Editor,
Appearance/Profile, Key Bindings and Network. Appearance/Profile groups the
existing template/name, Colours, Layout and Shortcut Display editors. There are
no placeholder categories. Repository management remains in its existing menu.
Colours expose semantic foreground/background roles in the 16-color model.
`[layout] show_app_identity = true` (default, explicit in all four templates)
shows the application name and source version on the right of the menu bar.
It falls back to name-only, then hidden, without moving or shortening menus.
Set it to `false` to hide it, or toggle Show App Identity under Appearance/Profile
→ Layout. Like other appearance options, it previews and saves in the UI profile.
Enter (or Space) toggles checkboxes or opens a select picker; Up/Down chooses,
Enter confirms, and Cancel leaves the previous value intact. Text settings reuse
the existing text fields. Editor exposes the external command and wait option;
its arguments remain in operational `nav.toml`, not a UI profile.

Existing configurable navigation commands are retained without changing default
bindings: Up/Down selects a category, Right/Enter enters settings, Left returns
to categories, and Down past the last setting reaches Apply/Save/Cancel buttons.
Left/Right selects a button and Enter activates it. Escape backs out of an
appearance subpage, then settings, then the dialog. No new default Tab binding
is introduced. Scrolling preserves access at small terminal sizes.

Appearance changes preview immediately; Network is staged until Apply or Save.
**Apply** (default F4 inside Preferences) keeps runtime changes and closes without
writing files. **Cancel** restores the opening configuration and pane presentation,
with confirmation only when this session has changed values. **Save** (default F2)
applies, saves changed operational settings to `nav.toml` and changed UI settings
to a personal profile, and closes on success. Each file replacement is atomic;
the two files are not a single filesystem transaction. A canceled or failed save
leaves the editor open. Network-only Save never prompts for or creates a profile.
The asterisk marks unsaved differences; restoring saved values clears it.
After Apply, unsaved operational changes also participate in quit/reload prompts.

See [Network preferences](NETWORK.md) for proxy policy and its verification.

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
two-key sequences are supported. The bundled `themes/classic-dos.toml` is the
complete reference: it spells out every compiled fallback binding, grouped by
global, panel, Viewer, menu, dialog, confirmation, picker, vault, and Preferences
contexts. For explicit context-specific command overrides:

```toml
[keys.viewer.commands]
"search.next" = ["F5", "Ctrl+N"]
"search.previous" = []
```

This uses the same command/sequence parser as flat bindings. The historical
Legacy compatibility syntax `[keys.viewer] "F5" = "search.next"` remains supported.
Explicit duplicate or prefix-conflicting keys in the same context are rejected
with the key, commands, and profile path. The same key in separate contexts is
valid; the active context wins before the global fallback. Saves use sparse
command arrays rather than dumping inherited defaults.

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

Text fields use Dialog context clipboard commands. These do not replace Panel
Copy or the Panel Ctrl+V sequence:

```toml
[keys.dialog.commands]
"text.paste" = ["Ctrl+V", "Shift+Insert"]
"text.copy" = "Ctrl+C"
"text.cut" = "Ctrl+X"
"text.select_all" = "Ctrl+A"
```

Ctrl+A selects the full field; Copy/Cut operate on that selection. Typing and
paste replace it, and ordinary cursor movement collapses it. Cursor movement,
Backspace and Delete respect UTF-8 character boundaries. This is a small
one-line editor, not a grapheme-aware editor with mouse/Shift selection.

Windows uses CF_UNICODETEXT and strict UTF-16/UTF-8 conversion. Linux explicit
clipboard access prefers `wl-paste`/`wl-copy` from optional **wl-clipboard**, then
`xclip` using UTF8_STRING on X11. No GUI libraries are linked on Linux. Without
these helpers, Copy/Cut/Paste can use an internal Navi8or clipboard; browser
clipboard reads require a helper. An unavailable clipboard produces a text-entry
error. Install the appropriate helper through your host's package manager; no
host packages are installed by Navi8or. macOS uses pbcopy/pbpaste.

Terminal paste, including Ctrl+Shift+V/right-click paste handled by the terminal,
needs no clipboard helper. Navi8or enables bracketed paste and treats its payload
as text, suppressing bindings (and ignoring pasted controls/newlines in these
one-line fields). The pending paste is bounded by field capacity and committed
at the end marker, so an oversized paste leaves the old value unchanged. Outside
text contexts, bracketed-paste payloads are ignored. Terminals without bracketed
paste cannot distinguish injected keys from typing; their ordinary UTF-8 text
still inserts normally. Clipboard reads are bounded at 1 MiB; each field retains
its existing byte limit (4095 bytes for Enter URL / Location). Clipboard paste
rejects control characters and oversized/invalid UTF-8 rather than truncating.
