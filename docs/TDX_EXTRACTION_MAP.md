# TDX Extraction Map

This map records where Navi8or adapted useful terminal-control mechanics from
TDX/TDE. TDX is not authoritative for Navi8or's UI: new layout, API, theme, and
presentation decisions belong to Navi8or, and proven improvements may flow in
either direction selectively.

| Navi8or component | TDX source and functions | Reuse strategy | Status |
|---|---|---|---|
| Major menu bar | `src/core/pull.c`: `get_bar_spacing()`, `draw_lite_head()`, `lite_bar_menu()` | Port `major_col[]`/`major_width[]`, spacing limits, row clearing, padded selected heading, and generic menu data. Remove editor dispatch. | Close port |
| Pull-down menu | `src/core/pull.c`: `make_menu()`, `pull_me()` | Store complete framed rows with accelerators and key labels; preserve edge placement, disabled edge cells, selected interior, separators, saved regions, and resize-close behavior. | Close port; pop-outs remain absent |
| `adjust_area()` | `src/core/hwind.c`: `adjust_area()` | Apply FrameSpace, right shadow width, optional bottom shadow row, and screen-edge rules once for save/restore/shadow geometry. | Exact/close port with pure geometry tests |
| Cell highlight/output | `src/terminal/termbox_backend.c`: `c_output()`, `s_output()`, `c_repeat()`, `hlight_line()`; `src/core/hwind.c`: `eol_clear()` | Keep the Navi8or terminal backend as the cell boundary; reproduce `s_output()` leading/trailing output-space cells and expose cell reads/writes. | Adapted |
| Screen save/restore/shadow | `src/terminal/termbox_backend.c`: `save_area()`, `restore_area()`, `shadow_area()` | Save the `adjust_area()` result, restore every modified cell, and place attr-8 right/bottom shadows outside the logical frame. | Close port |
| Normal window model | `src/core/utils.c`: `show_window_header()`; `src/core/window.c`: `show_vertical_separator()` | Retain the useful flat-window precedent while Navi8or owns its dual-pane hierarchy and modern/classic geometry. | Adapted; Navi8or metadata replaces editor fields |
| Window/frame | `src/core/utils.c`: `create_frame()`; `src/core/global.c`: `graphic_char[][]` | Preserve all frame sets, Combine outer/inner distinction, filled interior, separator junctions, FrameSpace, and shadow. | Close port |
| Directory-list visuals | `src/core/dirlist.c`: `write_directory_list()`, `select_file()` | Preserve stable row geometry and clipped names; Navi8or semantic roles distinguish active and inactive selection. | Adapted to dual-pane rows |
| Dialog/query | `src/core/query.c`: `get_response()` | Use the TDX saved one-line `(y/n):` Message query; Enter accepts the first response and Esc cancels. Multi-field editor dialogs/check boxes remain out of scope for Navi8or's current commands. | Close port for confirmation queries; multi-field dialog missing |
| Prompt/field | `src/core/query.c`: `get_name()`, `display_prompt()`, `get_string()` | Use one `NavUiField`; save the bottom row, render Message prompt/Text clearing, place cursor at the answer, then restore the row on completion. History/macros/completion are editor-specific omissions. | Close port for Navi8or prompts |
| Help/info window | `src/core/hwind.c`: `show_help()`, `show_strings()` | Center a Help-role frame above vertical centre, save adjusted frame/FrameSpace/shadow cells, restore on close, and keep Navi8or-owned scrolling/content. | Adapted/close port |
| Status/lite bar | `src/core/hwind.c`: mode-line routines | Retain compactness while Navi8or uses distinct summary, status/message severity, and keybar roles. | Adapted |
| Colour roles | `include/define.h`; `src/core/global.c` `colour` defaults | Keep the Classic DOS palette as provenance while all rendering consumes Navi8or's semantic format-2 roles. | Historical defaults translated into Navi8or roles |
| CP437 | `src/platform/cp437.c` | Retain the TDX CP437-to-Unicode mapping at Navi8or's terminal boundary. | Close port |
| Key translation | `src/terminal/termbox_backend.c`: `translate_event()` | Translate raw Termbox events at one boundary; normalize the Ctrl aliases for Esc, Enter, Tab, and Backspace while preserving real navigation modifiers. | Close port with raw-event tests |

## Extraction order

The normal window model, frame/menu path, cell regions, prompt/query path,
help/info presentation, and semantic colour table are extracted. The remaining
multi-field dialog model is intentionally absent because Navi8or currently has no
matching generic command surface.

## Known deviations

- Navi8or's panes, Viewer, controls, layout, and themes are Navi8or-owned; some mechanics retain TDX/TDE provenance.
- Navi8or uses synchronous callbacks and generic integer command IDs instead of TDX's global command dispatcher.
- Navi8or's terminal backend is independently maintained; compatible TDX cell behavior is retained where it remains useful.
