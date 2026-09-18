# Navi8or Rust foundation

`nav-rs` remains an experimental companion to the C `nav` application. The C
application, its themes, keymap and Viewer remain the behavioral reference.
The Rust core can be built without building the C application:

```sh
cargo check --workspace
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo run --manifest-path rust/nav-rs/Cargo.toml -- -i themes/classic-dos.toml . .
```

`make rust` copies the Rust binary to `./nav-rs`; `make rust-check` runs unit and
PTY integration tests. Arguments are `[-i profile.toml] [left-location]
[right-location]`. Locations may be local directories, HTTP/HTTPS autoindex
roots, or `repo:NAME` entries from the normal Navi8or repository file.

## Module boundaries

- `terminal` defines Navi8or-owned `Terminal`, `Event`, `Key`, `Modifiers`,
  `Cell`, `Color`, `Attributes`, `Size` and `Frame`. `terminal/input.rs` parses
  UTF-8, controls, Alt, CSI and SS3 sequences without application commands.
  `terminal/unix.rs` owns termios raw mode, terminal sizing, polling, alternate
  screen, 256-color ANSI output, cursor state and suspend/resume. The Windows
  module is a backend boundary for the existing Navi8or/Termbox2 console work;
  it currently reports that the Rust backend is unavailable at runtime.
- `ui/layout.rs` computes two pane and viewport rectangles for each terminal
  size. `ui/render.rs` draws cells using semantic theme roles. `ui/keybar.rs`
  models all ten function slots, including disabled commands. `ui/menu.rs`
  builds Help and menu text from the effective keymap. `UiState` owns the active
  overlay and translates terminal events to commands before dispatch.
- `profile.rs` loads the existing format-2 TOML color roles, UI style and frame
  style. It layers the normal platform `nav.toml` and optional `-i` profile
  over bundled Solar Dark defaults. Classic DOS, Solar Light and Monochrome
  profiles are shipped unchanged. Key bindings are read from the existing
  `[keys]` shorthand and scoped `[keys.*.commands]` tables. This is a focused
  reader for the current Rust UI, not the C Preferences editor.
- `keys.rs` stores commands and one or two stroke bindings separately, with a
  generic sequence matcher. Its defaults follow the C panel bindings,
  including Ctrl+N P/R/B/F, Ctrl+F C, Ctrl+V V and Ctrl+P S/W. Menus and Help
  ask the map for displayed bindings. Unimplemented commands have no active
  Rust binding.
- `Pane` owns provider-backed location, history, listing, selection, filter,
  sort and viewport state. `AppState` coordinates panes, jobs, navigation and
  copy completion; it no longer owns overlays or screen dimensions. The UI
  gives each pane the row capacity calculated by layout. Rendering and ANSI
  output are outside `AppState`.
- `Provider`, opaque `ResourceId`, `Location`, `WriteSession`, `transfer` and
  `JobManager` retain the provider-neutral streaming and worker-thread design.
  No async runtime or C provider interface is involved.

Crossterm and its terminal/event dependency tree were removed. `libc` handles
small Unix terminal and Linux filesystem calls; `unicode-width` accounts for
wide Unicode cells. HTTP still uses blocking `ureq`, `native-tls`, `url`,
`html-escape`, `percent-encoding`, `base64` and `toml`. These direct crates have
specific jobs and permissive licenses. The C build still retains its vendored
Termbox2 backend as the reference for future Windows work.

## Screen and commands

The Rust screen has a top menu bar, two framed panes with provider title,
location, headings, rows and summary, a status line, and a full F1–F10 bar.
Size appears when space permits; Modified appears in wider panes. Active pane,
selection and disabled function keys use semantic roles from the loaded theme.
Narrow terminal sizes show a safe small-screen message.

F1 Help, F2 Menu, F3 View, F5 Copy and F10 Quit work. F4 Edit, F6 Move, F7
MkDir, F8 Delete and currently unbound F9 remain visible and disabled. The
simplified View menu handles refresh, hidden files and sort. Arrow keys,
Home/End, Page Up/Down, Enter, Backspace, Tab, Alt+Left/Right and C-style
command sequences navigate the panes. The C dialog and Preferences UI have
not been ported, so unsupported actions do not appear as active shortcuts.

F3 on a local file suspends the Rust terminal, launches `nav-viewer-c`, waits,
then resumes and redraws. If the helper is absent, the pane remains usable and
shows `Viewer unavailable` in the status line. Remote Viewer is deferred.

F5 streams a local-to-local or HTTP-to-local file into the opposite pane. One
copy runs at a time; the status line reports progress. Escape requests
cancellation. Local writes stage to a same-directory temporary file, then
publish without overwriting an existing destination. `WriteSession::finish`
distinguishes committed, definitely uncommitted and uncertain outcomes; the
last case is reported without automatic abort or retry. Publication is atomic
for readers but does not promise crash-durable persistence.

HTTP directory listings run in cancellable worker threads. Navigation keeps
previous authoritative pane state until a matching result succeeds; stale
completions are discarded by job ID. The provider accepts autoindex HTML
links, scopes URLs and redirects to the configured root/origin, and streams
remote reads. Credential references are rejected until the C vault can be
integrated. HTTP uploads, remote mutation, SMB and recursive file operations
remain deferred. The Rust provider currently uses the system proxy environment;
it does not yet apply the C `proxy_mode = "none"` setting.

## Portability

The Unix backend uses termios, `poll` and terminal `ioctl` at one boundary.
Linux-specific `renameat2(RENAME_NOREPLACE)` stays guarded by
`cfg(target_os = "linux")`; Android/Termux uses the Unix terminal backend and
the safe hard-link publication fallback. No Termux conditions leak into
providers or application commands. The Rust Windows modules compile behind a
separate backend boundary, but interactive Windows terminal support awaits a
port of the existing Navi8or/Termbox2 console behavior. Only Linux runtime
and PTY behavior are verified here; Android/Termux and Windows runtime still
need native device testing.

## Source distribution

`make dist` writes both `build/release/navi8or-source.tar.gz` and
`build/release/navi8or-source.zip` from the same filtered tree. Both include
`include/`, `third_party/`, Rust sources, Cargo files, tests, themes and docs,
and omit `__pycache__`. `make source-snapshot` refreshes the tracked
`Archive.zip` from this canonical ZIP. `make dist-check` checks both archives
and rebuilds the C Viewer helper, Rust core and C application from the
extracted tar tree.

## Verification

`cargo check --workspace`, `cargo test --workspace`, `cargo clippy --workspace
--all-targets -- -D warnings`, `make rust-check`, and `make dist-check` are the
main gates. Rust tests cover input parsing, key sequences, profile loading,
layout, function-slot state, active/inactive presentation, pane behavior,
providers, transfers and cancellation. PTY tests cover startup, pane switching,
resize, copy, HTTP listing/cancellation/download, Viewer handoff, missing
helper behavior, Classic DOS profile and F10 exit.
