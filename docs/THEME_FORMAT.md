# Navi8or theme format

Navi8or discovers themes in `~/.config/nav/themes/` (or
`$XDG_CONFIG_HOME/nav/themes/`) and selects one from `~/.config/nav/nav.toml`:

```toml
[theme]
name = "solar-dark"
```

New themes should use format 2. A theme is a flat mapping from semantic UI
roles to the terminal's 16-colour foreground/background model; it does not
control pane geometry, menus, or Viewer layout.

## Format 2

For example, `~/.config/nav/themes/solar-dark.toml` can begin:

```toml
[theme]
format = 2
name = "Solar Dark"

[ui]
style = "modern"

[ui.background]
foreground = "light_gray"
background = "black"

[ui.selection]
foreground = "black"
background = "cyan"

[ui.border_active]
foreground = "yellow"
background = "blue"
```

Supported colour names are `black`, `blue`, `green`, `cyan`, `red`,
`magenta`, `brown`, `light_gray`, `dark_gray`, `light_blue`, `light_green`,
`light_cyan`, `light_red`, `light_magenta`, `yellow`, and `white`.

Semantic roles are:

| Role | Purpose |
|---|---|
| `ui.background` | cleared application canvas |
| `ui.surface`, `ui.surface_alt` | ordinary and alternate control surfaces |
| `ui.text`, `ui.text_dim` | normal and de-emphasized content |
| `ui.accent` | emphasized paths, current lines, and key labels |
| `ui.border`, `ui.border_active` | inactive and active pane structure |
| `ui.selection`, `ui.selection_inactive` | selected entries in active/inactive panes |
| `ui.header` | Viewer header |
| `ui.menu`, `ui.menu_selected`, `ui.menu_disabled` | pull-down menu states |
| `ui.menu_selected_disabled` | selected but unavailable menu item |
| `ui.keybar` | function-key labels and the permanent bar background |
| `ui.keybar_key` | available function-key tokens |
| `ui.keybar_disabled` | unavailable function-key tokens and labels |
| `ui.keybar_selected` | selected persistent top-menu heading (retained for compatibility) |
| `ui.path` | pane location/path text |
| `ui.column_header` | Full/Brief column headings |
| `ui.dialog`, `ui.dialog_title` | dialog body/frame and emphasized title/label |
| `ui.status`, `ui.message`, `ui.error`, `ui.warning` | application feedback states |
| `ui.pane_title`, `ui.pane_title_active` | pane title bands |
| `ui.viewer_line_number`, `ui.viewer_search_match` | Viewer gutter and search match |
| `ui.progress` | transfer progress |

The optional `[ui.frame]` table accepts `style = "ascii"`, `"single"`,
`"double"`, `"combine"`, `"combine_reverse"`, or `"block"`, plus Boolean
`space` and `shadow` values. `[symbols]` may define the one-character
`selected`, `directory`, `parent`, `upload`, and `download` glyphs.

`[ui] style` is intentionally limited to `modern` or `classic`. It selects
chrome and geometry, not colours: Modern omits the extra pane-heading rule and
uses flatter summaries, menus, dialogs, status, and keybar, while Classic keeps
the traditional separator and stronger frame/shadow presentation. Format-2
themes default to Modern when `style` is omitted. Format-1 themes always use
Classic for compatibility.

Every role is initialized even when omitted. Format-2 fallbacks are deliberate:

- `text` and `background` fall back to one another; `surface` falls back to
  `background`, and `surface_alt` to `surface`.
- `text_dim` and `selection_inactive` fall back to `text`; `accent` falls back
  to `text`, and `selection` to `accent`.
- `border` falls back to `surface_alt`; `border_active` to `accent`.
- `status` falls back to `surface`; `message` to `status`; `error` and
  `warning` to `message`.
- `path` falls back to `accent`; `column_header` falls back to `text_dim`.
- `keybar_key` falls back to `keybar_selected`; `keybar_disabled` falls back to
  `text_dim`. Dialog, menu, pane-title, Viewer, and progress variants fall back to
  the closest base role described by their names.

An invalid format number, colour, frame value, symbol, or malformed TOML file
fails cleanly and leaves Navi8or on its compiled fallback theme.

## Format 1 compatibility

Files with `format = 1`, and historical files with no `theme.format`, are
always interpreted as legacy themes. Navi8or translates them in the theme
loader before rendering; UI code never requests a historical role.

| Format-1 role | Semantic role |
|---|---|
| `tdx.head` | `ui.header` |
| `tdx.mode` | `ui.status` |
| `tdx.message` | `ui.message` |
| `tdx.text` | `ui.text` (and the normal background fallback) |
| `tdx.curl` | `ui.accent` |
| `tdx.help` | `ui.surface_alt` |
| `tdx.dialog` | `ui.dialog` |
| `tdx.edit_label` | `ui.dialog_title` |
| `tdx.disabled` | `ui.text_dim` |
| `tdx.hilited_file` | `ui.selection` |
| `tdx.menu_header` | `ui.keybar` |
| `tdx.menu_selected` | `ui.keybar_selected` |
| `tdx.curl` | `ui.keybar_key` |
| `tdx.disabled` | `ui.keybar_disabled` |
| `tdx.menu` | `ui.menu` |
| `tdx.menu_disabled` | `ui.menu_disabled` |
| `tdx.menu_item` | `ui.menu_selected` |
| `tdx.menu_item_bad` | `ui.menu_selected_disabled` |
| `tdx.frame` | `ui.frame` |
| `viewer.line_number` | `ui.viewer_line_number` |
| `search.match` | `ui.viewer_search_match` |
| `progress` | `ui.progress` |
| `navigator.pane_title` | `ui.pane_title` |
| `navigator.pane_title_active` | `ui.pane_title_active` |

Existing format-1 files do not need immediate migration. The shipped themes
are format 2 and are useful examples for new themes.

For a classic example, select `classic-dos` and use:

```toml
[theme]
format = 2
name = "Classic TDX"

[ui]
style = "classic"
```

## Project relationship

Navi8or and TDX are separate applications. Navi8or's UI contains code and
design ideas adapted from TDX/TDE, with provenance retained in the source.
Navi8or owns its UI API and theme vocabulary; the projects may continue to
share ideas independently.
