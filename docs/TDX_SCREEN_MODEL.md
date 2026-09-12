# TDX Screen Model

This records the historical TDX/TDE screen ideas that informed Navi8or. It is a
provenance and behavior reference, not Navi8or's visual specification. Navi8or
owns its UI implementation, API, layout, semantic roles, and modern/classic
presentations; it may selectively reuse proven mechanics without reproducing
TDX's screen exactly.

| TDX | Navi8or |
|---|---|
| `show_window_header()` / `Head` | pane and Viewer header |
| `Text` | pane rows and Viewer body |
| `Hilited_file` | selected file in the focused pane |
| `Curl` | Viewer current line |
| `show_vertical_separator()` | single divider between panes |
| `Mode` | pane summaries above Navi8or's compact status and key rows |
| `lite_bar_menu()` | permanent row-zero menu headings, activated by F2 or Ctrl+\\ |
| `make_menu()` / `pull_me()` | framed pull-down rows, selection, saved area, and shadow |
| `create_frame()` | dialogs and transient windows only |
| `get_response()` / query routines | prompts and confirmations |

## Normal screen

Row zero is Navi8or's permanent menu bar. Pane title bands occupy row one,
provider-friendly display paths row two, and restrained column headings row
three. Modern content begins immediately below the headings; Classic adds a
historical horizontal separator. Semantic `text`, `selection`, and
`selection_inactive` roles replace editor-era role names. One subtle vertical
divider separates the panes. Pane summaries, compact global status, and the
segmented function-key bar form the three bottom rows.

Large frames never surround normal panes or the Viewer. Viewer content uses the
same semantic header, text, dim-text, search-match, and status roles.

## Transient screen

Activating the menu highlights its existing row-zero headings. `make_menu()`-style
preparation creates accelerator, label, and right-aligned key fields.
`create_frame()` supplies the configured frame and separator junctions;
`pull_me()`-style rendering saves underlying cells, draws the shadow, changes
only item interiors during selection, and restores the saved region on close.

`FrameSpace` is the scoped TDX `output_space` behavior: each complete menu row
gets one leading and trailing role-coloured cell where the screen edge permits.
The shared `adjust_area()` port expands the logical frame rectangle for those
cells, the right shadow, and the optional bottom shadow row. Save, shadow, and
restore therefore operate on identical effective geometry.

Dialogs, help, and progress controls may use `create_frame()`. Classic TDX uses
the exact CP437 graphic table with `Combine`, `FrameSpace = On`, and
`Shadow = On`.
