# Far-style navigation reference

Far2l panel input was studied as a behavioural reference only. No GPL source
is incorporated into Navi8or. Navi8or owns its rendering, controls, layout, and
semantic colours; TDX/TDE remains a provenance and behavioral reference.

| Context | Control | Current Navi8or behaviour |
| --- | --- | --- |
| Brief | Up/Down | Move one logical entry vertically. |
| Brief | Left/Right | Move to the closest entry at the same row in the previous/next column. |
| Brief | PgUp/PgDn | Move one complete visible multi-column page. |
| Full | Up/Down | Move one entry in the vertical list. |
| Full | PgUp/PgDn | Move one body-height page. |
| Full | Left/Right | No operation. |
| Both | Home/End | Select the first/final entry and keep it visible. |
| Both | Tab | Switch active pane while preserving each pane's state. |
| Both | Enter, Ctrl+PgDn | Enter a directory or view a readable file. |
| Both | Backspace, Alt+Up, Ctrl+PgUp | Navigate to the provider parent. |
| Both | Alt+Left/Right | Navi8or deviation: history back/forward. |
| Both | Ctrl+R | Refresh active pane and retain selection when possible. |
| Both | Ctrl+U | Swap complete pane state; focus stays on the same screen side. |
| Both | F3–F8, F10 | View, Edit, Copy, Move/Rename, MkDir, Delete and Quit. |

Brief entries fill top-to-bottom and then across. Its viewport moves by whole
columns and only far enough to expose the selection. A partial final column
clamps to its last entry. The active pane is the operation source and the other
pane is the default destination. Multi-selection and Quick View remain
deferred; `Ctrl+\\` retains TDX menu behavior.
