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
- `Pane` owns a provider-backed `Location`, listing, selection, filter, sort
  mode, history and viewport without knowing how resource identities are encoded.
- `Provider` describes operations using opaque `ResourceId` values and
  Navi8or-owned `Entry`/`ResourceMetadata` types without a C vtable,
  filesystem type, or UI/runtime dependency.
- `ResourceId` stores an `OsString`, keeping local filesystem identity lossless;
  UTF-8 conversion is confined to presentation. Existing entries are opened
  through their provider-supplied resource IDs rather than reconstructed names.
- `LocalProvider` implements listing and the local primitives needed by later
  file-operation jobs. It is the only layer that translates resources into
  `PathBuf` values or reads `std::fs` metadata. Resolution makes paths absolute
  and normalizes `.` and `..` lexically without canonicalizing or dereferencing
  symlinks.
- `terminal` owns Crossterm startup/shutdown, keyboard and resize events, and
  Navi8or-specific DOS-style rendering.
- `viewer_bridge` launches the existing C Viewer in the `nav-viewer-c` helper
  process. The boundary is one local path argument; no C Viewer structs or
  ownership cross into Rust.

The intended long-operation boundary remains `UI -> commands/jobs -> providers
-> I/O`. Copy is deliberately not a provider operation: future transfers will
combine a source provider/resource with a destination provider/resource, with
any server-side copy exposed only as an optimization. No async runtime is
present because this milestone performs no network or transfer work.

The event loop polls Crossterm with a bounded timeout, redraws only after state
changes, and invokes a non-blocking background-service hook. A future channel-
backed job service can therefore deliver progress/results without coupling the
UI to Tokio or leaving input blocked indefinitely.

## Current behavior

The two panes start in the supplied directories (or the current directory),
hide dot files, sort `..` first and directories before files, and keep selection
visible. Arrow keys, Home/End, Page Up/Down, Enter, Backspace, Tab, Ctrl+R,
Ctrl+Q and F10 provide the initial navigation and lifecycle controls. Alt+Left
and Alt+Right traverse pane history, while Ctrl+U swaps panes. Provider-neutral
sorting and filtering preserve selection; refresh and hidden-file changes retain
the selected resource when it remains visible. Resize events recompute pane
geometry and redraw the screen. Each pane caches its filtered/sorted visible
indices, so rendering a viewport does not repeatedly scan from the start of a
large directory listing.

Opening a regular local file or pressing F3 launches `nav-viewer-c`, which wraps
the mature C Viewer. Rust first restores its terminal, waits for the helper, then
re-enters raw/alternate-screen mode and forces a complete redraw. This recovery
also runs when the helper fails to start or exits with an error. Directories,
non-local providers and non-regular resources produce a status message instead.

The helper reuses `src/view/*` unchanged and adds only a thin entry point in
`src/ui/viewer.c` plus `src/viewer-helper/main.c`. Its current bridge accepts
local files only; remote materialization/caching is deliberately deferred. F4
and file copy/move/delete/mkdir commands remain visually present but inactive.
Configuration integration, external editing, jobs, transfers, HTTP, SMB,
authentication and vault compatibility are also deferred.

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
