# Experimental Rust core

`nav-rs` is the first architecture milestone for a gradual Rust
reimplementation. The C `nav` application remains the behavioral reference and
the release application. Build the experiment with:

```sh
make rust
./nav-rs [left-directory] [right-directory]
make rust-check
```

## Implemented architecture

- `AppState` owns two independent panes, focus, status and command dispatch.
- `Pane` owns a provider-backed location, listing, selection and viewport.
- `Provider` describes capabilities and filesystem-style operations without a C
  vtable or UI/runtime dependency.
- `LocalProvider` implements listing and the local primitives needed by later
  file-operation jobs.
- `terminal` owns Crossterm startup/shutdown, keyboard and resize events, and
  Navi8or-specific DOS-style rendering.

The intended long-operation boundary remains `UI -> commands/jobs -> providers
-> I/O`. No async runtime is present yet because this milestone performs no
network or transfer work. A future job service can own async execution while
the UI consumes progress/result messages synchronously.

## Current behavior

The two panes start in the supplied directories (or the current directory),
hide dot files, sort `..` first and directories before files, and keep selection
visible. Arrow keys, Home/End, Page Up/Down, Enter, Backspace, Tab, Ctrl+R,
Ctrl+Q and F10 provide the initial navigation and lifecycle controls. Resize
events recompute pane geometry and redraw the screen.

Opening a regular file reports that the Viewer is deferred. F3/F4 and file
copy/move/delete/mkdir commands are displayed for visual continuity but are not
active in this milestone. Configuration, TOML themes, external editing, jobs,
transfers, HTTP, SMB, authentication and vault compatibility are also deferred.
No files under `src/view/` or existing C Viewer behavior are used or changed.

## Dependency and license review

The only direct dependency is Crossterm 0.29.0 (MIT), selected as a terminal
backend rather than a UI framework. Default features are disabled; only its
event and Windows support features are enabled. `Cargo.lock` fixes the exact
graph.

All resolved transitive packages declare permissive licenses: MIT,
Apache-2.0, MIT OR Apache-2.0, Apache-2.0 WITH LLVM-exception, or equivalent
dual-license expressions. The reviewed packages are `bitflags`, `cfg-if`,
`crossterm_winapi`, `document-features`, `errno`, `libc`, `linux-raw-sys`,
`litrs`, `lock_api`, `log`, `mio`, `parking_lot`, `parking_lot_core`,
`redox_syscall`, `rustix`, `scopeguard`, `signal-hook`, `signal-hook-mio`,
`signal-hook-registry`, `smallvec`, `wasi`, `winapi`, its GNU architecture
packages, `windows-link`, and `windows-sys`. No GPL, AGPL or LGPL crate is in
the Rust dependency graph.

