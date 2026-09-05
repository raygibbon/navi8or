# NAV

NAV is a keyboard-first, dual-pane terminal navigator in the visual spirit of TDE/TDX. This first milestone implements independent local filesystem panes, navigation history, filtering, a read-only text viewer with find, local file copy/move/delete/mkdir, and TDX editing integration.

Build with `make`; run `./nav [local-directory]`. NAV vendors the same termbox2 snapshot used by TDX and does not depend on ncurses. Its terminal boundary adapts TDX's event model and CP437 conversion, preserving DOS-style borders plus F-keys, navigation keys, Escape and Alt-arrow handling. F1/F3/F4/F5/F6/F7/F8/F9/F10 follow the function strip; Tab switches panes, `/` filters, Ctrl-L refreshes, and Alt-Left/Right/Up provide Commander history/parent navigation. F3 views; F4 launches `tdx`.

The UI only uses `NavProvider`; local filesystem operations live in `src/provider/local.c`. The provider capability model leaves room for local, HTTP, and HTTPS implementations. HTTP (via libcurl), saved repos, transfer queue, TOML settings, encrypted vault (via libsodium), and recursive search are deliberately future work rather than simulated features.

TDX inspection informed NAV’s independent implementation. NAV directly reuses its vendored termbox2 snapshot and CP437 conversion; its menu/dialog code is editor-global and not safely reusable without importing much of TDX. NAV therefore keeps a small standalone menu/prompt layer while following TDX’s Escape-cancels-temporary-operation model. TDX is never modified.
