# NAV

NAV is an independent, keyboard-first, dual-pane terminal file navigator built around a TDX-derived terminal control layer. It reuses and adapts TDX/TDE terminal, menu, input-field, information-window, resize, and CP437 concepts without embedding the TDX editor.

## Build and run

NAV currently targets Linux/POSIX and has no external UI-library dependency. The compatible termbox2 single-header source is vendored under `third_party/termbox2`.

```sh
make
./nav
./nav /var/log /tmp
```

Development checks:

```sh
make check
make resize-test
make asan
```

## Implemented local v0.1 features

- Two independently navigable local panes with selection scrolling and history.
- TDX-style logical key handling through a termbox-only terminal boundary.
- CP437 Classic DOS borders, an injected semantic colour theme, and a separate symbol set.
- Live filename filtering over the current listing.
- Read-only F3 text viewer with line numbers and visible Find prompt.
- Binary detection and a metadata view containing name, path, size, and modification time.
- Shell-free F4 launch of `tdx`.
- Streamed local file copy with an on-screen byte/percentage progress dialog and overwrite confirmation.
- Move or rename to an editable destination, defaulting to the opposite pane.
- Directory creation and default-No file/empty-directory deletion confirmation.
- Name, size, and modification-date sorting with directories grouped before files.
- Global Show Hidden toggle that refreshes both panes.
- TDX-style keyboard menu with non-selectable separators and visibly disabled future items.
- Cursor-aware, horizontally scrolling text fields shared by Filter, Find, and file-operation prompts.
- Scrollable Help and information windows, reusable confirmation dialogs, and two-path startup.

Directory copying and recursive deletion are not implemented; NAV reports these limitations rather than silently succeeding.

## Keys

| Key | Action |
|---|---|
| Tab | Switch active pane |
| Up/Down | Move selection |
| Home/End | First/last visible entry |
| Page Up/Page Down | Move one visible page |
| Enter | Open directory or view file |
| Backspace / Alt+Up | Parent directory |
| Alt+Left / Alt+Right | Back/forward pane history |
| `/` | Edit active-pane filter |
| F1 | Help |
| F3 | View |
| F4 | Edit in TDX |
| F5 | Copy to opposite pane |
| F6 | Move/Rename; destination defaults to opposite pane |
| F7 | Make directory |
| F8 | Delete file or empty directory |
| Ctrl+\\ | Open menu (canonical TDX binding) |
| F9 | Open menu (optional Commander alias) |
| Ctrl+Right | Next top-level menu while a menu is active |
| Ctrl+Left | Previous top-level menu while a menu is active |
| F10 | Quit |
| Esc | Cancel or close the current temporary view |

## Architecture

The Commander UI works with `NavProvider`, not POSIX directory calls. Providers expose metadata and stream open/read/write/close operations; the central transfer engine moves bytes and reports progress. This boundary is intended to support future cross-provider transfers without changing Commander commands.

Terminal rendering is isolated in `src/terminal`, local filesystem work in `src/provider`, transfers in `src/transfer`, platform process launching in `src/platform`, and reusable UI primitives in `src/ui`. Theme colours and CP437 symbols are separate application-level models.

### TDX-derived UI components

NAV's terminal event model and modifier-aware key translation are adapted from TDX's termbox backend. Resize is a first-class event: persistent controls recalculate and redraw, while pull-down menus close after preserving their current major/minor selection, following TDX's transient-window lifecycle.

The CP437 conversion is ported from TDX. The standalone `tdxui` layer closely adapts TDX's major/minor menu model and pull-down flow, `query.c` field-editing semantics, and Help/window viewport movement. Box, confirmation, information, and viewer controls preserve TDX's redraw and Escape semantics without importing editor buffers or editor-global state. NAV and TDX remain independently built executables; only F4 optionally launches the installed `tdx` program.

## Planned

- HTTP/HTTPS provider using libcurl
- Saved generic repositories and Artifactory-compatible paths
- Remote viewing and download/upload
- Basic, bearer-token, and custom-header authentication
- Optional encrypted credential vault using a mature crypto library
- Background transfer queue
- TOML configuration and external themes
- Windows and macOS platform implementations
