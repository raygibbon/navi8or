# Configuration and profiles

Linux uses $XDG_CONFIG_HOME/nav, otherwise ~/.config/nav. Windows uses
%APPDATA%/Navi8or. nav.toml contains operational settings, repositories.toml
repository definitions and scoped HTTP credential references, vault.bin encrypted credentials and profiles/ personal
UI profiles. Never commit operational files or vaults.

Precedence: compiled defaults → normal configuration → exact -i profile →
live edits. CLI directory arguments select startup locations.

```sh
nav -i themes/classic-dos.toml
nav -i ~/.config/nav/profiles/custom.toml
```

Relative paths use the launching directory. Invalid explicit files fail with
path-qualified errors. Omitted values inherit. The four bundled templates are
cross-platform: Solar Dark (default), Classic DOS, Solar Light, Monochrome.

## Preferences and saving

Options → Preferences (Ctrl+T P) contains General, Panels, Viewer, Editor,
Appearance/Profile, Key Bindings and Network. Appearance previews immediately;
Network is staged. Apply (F4) changes the session without writing. Cancel rolls
back. Save (F2) writes changed operational settings and/or a personal profile.
Each file replacement is atomic; the two files are not one transaction.
Failed/canceled Save leaves the editor open. Network-only Save creates no profile.

Up/Down selects a category; Right/Enter enters settings; Left returns.
Enter/Space toggles or opens a picker. Down after the last setting reaches buttons.
Escape backs out. Small terminals scroll. Save Profile / Save Profile As also
work from Options. Normal saves never overwrite templates. Use -i to select a
saved profile next launch. Reload validates before replacing live settings.
Unsaved changes prompt before reload, replacement or quit.

## Sparse profile

```toml
[profile]
name = "Custom DOS"
format = 2
[layout]
show_app_identity = true
show_menu = true
show_status = true
show_function_bar = true
border_style = "single"
[panes]
view = "full"
sort = "name"
directories_first = true
show_hidden = false
size_format = "auto"
date_format = "%Y-%m-%d %H:%M"
[viewer]
wrap = false
line_numbers = false
current_line = true
[keys]
pane_switch = "Ctrl+P S"
copy = ["F5", "Ctrl+F C"]
[keys.viewer.commands]
"viewer.toggle_fullscreen" = "F7"
"viewer.open_link" = "F9"
```

UI profiles exclude network/repository/credential/cache/transfer/editor operations.
Legacy full configurations remain accepted where supported. Pane view is
brief/full; sort name/size/date; size format auto/bytes.
[shortcuts] accepts show_function_bar, show_menu_keys, show_dialog_keys,
show_help_keys.

## Colors and layout

Format-2 palettes use [theme] format = 2, [ui] style = "modern" or "classic",
and semantic role tables:

```toml
[ui.selection]
foreground = "black"
background = "cyan"
```

Roles: background, surface, surface_alt, text, text_dim, accent, border,
border_active, selection, selection_inactive, header, menu, menu_selected,
menu_disabled, menu_selected_disabled, keybar, keybar_key, keybar_disabled,
keybar_selected, path, column_header, dialog, dialog_title, status, message,
error, warning, pane_title, pane_title_active, viewer_line_number,
viewer_search_match, progress. Omitted roles use semantic fallbacks.
Colors: black, blue, green, cyan, red, magenta, brown, light_gray, dark_gray,
light_blue, light_green, light_cyan, light_red, light_magenta, yellow, white.
Flat [colors] aliases and Classic format-1 palettes remain supported.
[ui.frame] accepts ascii/single/double/combine/combine_reverse/block, space/shadow.
[symbols] accepts one-character selected/directory/parent/upload/download glyphs.
Bundled profiles are complete examples.

## Shortcuts

Active Files/Viewer overrides Global. Pane Switch is app-level, default global
Tab and Ctrl+P S. Modals capture input rather than falling through to destructive
workspace actions. Sequences have one/two strokes; release prefix before second
key. Escape, resize or context change cancels a prefix; no timeout.

Key Bindings shows inherited/override/unbound by context. Enter captures,
Delete clears, F5 appends, F6 captures two strokes. Conflicts require confirmation;
separate contexts reuse keys. Sparse arrays persist overrides; [] means unbound.
Use [keys.CONTEXT.commands] for command-to-key overrides.
See [keymap example](examples/keymap.toml) and include/nav_commands.def.
Physical names support F1–F12/navigation/ASCII and Ctrl/Alt/Shift. Classic terminal
aliases (Ctrl+I/Tab, Ctrl+M/Enter, Ctrl+H/Backspace) retain navigation meanings.
Help, menus and key hints use the live map.

## Network and editor

Operational nav.toml:

```toml
[network]
proxy_mode = "system" # or "none"
```

System retains libcurl HTTPS_PROXY/ALL_PROXY/NO_PROXY behavior; HTTP uses lowercase
http_proxy, not uppercase HTTP_PROXY. No Proxy explicitly bypasses proxies;
there is no automatic fallback bypass. Apply affects subsequent requests on
open providers without changing environment, TLS or authentication.
UI profiles reject network settings. External editor command/wait and arguments,
history limits and transfer buffer size belong in operational TOML.
