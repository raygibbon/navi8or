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
- Startup uses `args_os`, and `LocationInput` distinguishes provider text from
  native local paths. The current directory and command-line filesystem paths
  therefore reach `LocalProvider` without a UTF-8 conversion. `ResourceName`
  likewise preserves a local leaf name when constructing a copy destination.
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
- `JobManager` owns process-lifetime job information, one active cancellation
  token and its worker `JoinHandle`. The worker sends reliable start/terminal
  events on an unbounded channel and best-effort progress through a one-slot
  channel with `try_send`. A full progress queue never stalls copying; missing
  intermediate updates do not affect the final state.
- `transfer` owns the provider-neutral streaming loop. Providers supply
  `open_read` and `open_write`; the transfer layer moves data with a reusable
  128 KiB buffer and reports monotonic byte progress. `open_write` returns a
  `WriteSession`: the job explicitly calls `finish` after a successful copy, or
  `abort` after cancellation or read/write failure. A successful last write
  alone does not mark the job complete.

The long-operation boundary is `UI -> commands/jobs -> providers -> I/O`. Copy
is deliberately not a provider operation: a transfer combines a source
provider/resource stream with a destination provider/resource stream. A future
same-provider server-side copy can be an advertised optimization without
replacing this generic path. No async runtime is present.

The event loop polls Crossterm with a bounded timeout, redraws only after state
changes, and drains job messages without blocking. Quitting requests
cancellation and joins the worker before process exit; `JobManager::Drop` does
the same defensively. Cancellation is checked between chunks, so a provider
read that does not return promptly can delay shutdown. The C Viewer may own the
terminal while a transfer runs: progress may be discarded during that pause,
but reliable completion is processed and the destination refreshed on return.

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

F5 starts a local-to-local file copy into the opposite pane under the same leaf
name. The worker streams the file while navigation, pane switching and terminal
resize remain responsive; the status line shows byte and percentage progress.
The destination pane refreshes after completion. One transfer may be active at
a time, and Escape requests cancellation. Incomplete destinations created by a
cancelled or failed job are removed by the local write session. Local writes go
to a same-directory temporary file; `finish` flushes and publishes it, while
`abort` removes it. Existing destinations are never overwritten in this
milestone, including if one appears while the session is open. Write sessions
own this cleanup, so a destination needs WRITE but not DELETE capability.

Opening a regular local file or pressing F3 launches `nav-viewer-c`, which wraps
the mature C Viewer. Rust first restores its terminal, waits for the helper, then
re-enters raw/alternate-screen mode and forces a complete redraw. This recovery
also runs when the helper fails to start or exits with an error. Directories,
non-local providers and non-regular resources produce a status message instead.
Local symlinks are inspected only when an operation needs their target type:
file symlinks can be viewed, while directory symlinks can be navigated without
canonicalizing their displayed location.

The helper reuses `src/view/*` unchanged and adds only a thin entry point in
`src/ui/viewer.c` plus `src/viewer-helper/main.c`. Its current bridge accepts
local files only; remote materialization/caching is deliberately deferred. F4
and move/delete/mkdir commands remain visually present but inactive.
Configuration integration, external editing, recursive copy, overwrite
confirmation, multiple concurrent jobs, HTTP, SMB, authentication and vault
compatibility are also deferred. Future move behavior is rename when supported
by the same provider, otherwise copy followed by source deletion only after a
successful transfer.

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
