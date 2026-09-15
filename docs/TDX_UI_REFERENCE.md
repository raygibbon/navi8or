# TDX UI Reference

This document is a provenance guide to TDX/TDE mechanics adapted by Navi8or.
Navi8or owns the resulting implementation and is not required to remain
visually identical to TDX.

## Screen cells and roles

- `tdx/src/terminal/termbox_backend.c`: TDX keeps a screen-cell back buffer; `c_output`, `s_output`, `c_repeat`, and `hlight_line` mutate cells before presentation.
- `tdx/src/core/define.h`: the semantic colour roles are `Head`, `Mode`, `Message`, `Text`, `Help`, `Dialog`, `EditLabel`, `Disabled`, `Menu_header`, `Menu_sel`, `Menu`, `Menu_dis`, `Menu_item`, and `Menu_nitem`.
- `tdx/src/core/global.c`: default DOS attributes are the source for Navi8or's compiled Classic DOS fallback.
- `tdx/src/platform/cp437.c`: CP437 glyph conversion is shared in spirit; Navi8or keeps its own terminal boundary.

## Frames and windows

- `tdx/src/core/hwind.c`: `eol_clear` fills the remainder of a line with one role and `window_eol_clear` clears editor content without changing geometry.
- `tdx/src/core/utils.c`: `show_window_header()` clears a normal window's top row with `Color(Head)` and then places the window identity, file name, and right-side metadata fields.
- `tdx/src/core/window.c`: `show_vertical_separator()` divides adjacent normal windows with one Head-coloured vertical rule. Normal editor windows are not dialog frames.
- Navi8or retained the useful flat-window idea for its panes and Viewer. Its
  Modern and Classic profiles now own the exact geometry; `create_frame()` is
  reserved for transient dialogs, menus, help, and progress UI.
- `create_frame()` uses TDX's exact `graphic_char[][]` sets. Classic TDX selects `Combine`: double outer corners/edges and single internal separator lines, with `FrameSpace` and shadow enabled.

## Menu bar and pull-downs

- `tdx/src/core/pull.c`: `calc_major_cols` computes spaced major headings; `draw_lite_head` writes headings with `Menu_header`; `lite_bar_menu` clears row zero and highlights the selected heading with padded width using `Menu_sel`.
- `tdx/src/core/pull.c`: `make_menu()` calls `create_frame()`, adds `A)` accelerator fields and right-aligned key names, and encodes separator rows as frame junctions.
- `tdx/src/core/pull.c`: `pull_me()` saves the underlying cells, draws the prebuilt framed menu, applies shadow, keeps disabled edge cells in `Menu`, highlights only the interior with `Menu_item` or `Menu_nitem`, and restores the saved cells on close.
- `tdx/src/core/hwind.c`: `adjust_area()` expands that logical menu rectangle for scoped `output_space`, the available right shadow width, and a bottom shadow row when the screen has room. The same calculation governs save and shadow geometry.
- `tdx/src/terminal/termbox_backend.c`: `s_output()` owns FrameSpace's leading and trailing cells; the frame renderer does not paint independent side gutters.
- Navi8or adapts that complete flow. Pull-downs are framed; the former claim that TDX pull-downs were unbordered was incorrect.

## Dialogs and prompts

- `tdx/src/core/query.c`: `display_dialog` calls `create_frame`, renders labels in `Dialog` or selected `EditLabel`, renders fields in `Message`, and uses underscores for empty field capacity.
- `tdx/src/core/query.c`: `display_prompt` writes the prompt using `Message` and clears the remaining field using the text role.
- `tdx/src/core/query.c`: `get_response()` saves the affected line, displays `question (y/n):`, accepts its first response on Enter, and restores the line afterwards.
- Navi8or's extracted prompt/query control follows those one-line bottom-row semantics. It deliberately omits TDX editor macros, history, filename completion, and multi-field checkbox dialogs; Navi8or has no equivalent command data for them.

## Help and status

- `tdx/src/core/hwind.c`: the mode/status line is cleared as a complete role-coloured line, then fixed-width fields are written into it.
- `tdx/src/core/hwind.c`: `show_help()` centers Help content above vertical centre, saves the adjusted FrameSpace/shadow region, and restores it after dismissal. Navi8or keeps scrolling because its supplied help/properties lines have variable length.
- Navi8or's help, properties, viewer status, and function hints use these role conventions.

## Historical comparison checklist

These comparisons are useful when validating retained behavior or provenance:

- TDX normal split/editor windows vs Navi8or's normal dual-pane screen (compare this first)
- TDX menu header/selected header vs Navi8or menu header/selected header
- TDX pull-down item/selected/disabled vs Navi8or equivalents
- TDX dialog/prompt/field vs Navi8or confirmation/prompt
- TDX help/info window vs Navi8or Help/Properties/Current Theme
- TDX status roles vs Navi8or's pane summaries, compact status, and segmented key bar

Family resemblance is welcome in Classic mode, but it is not an acceptance
requirement for Modern mode. TDX is an editor; Navi8or is a resource navigator
and file manager. They are sibling terminal-first, keyboard-first applications
that may selectively exchange proven ideas and implementations.
