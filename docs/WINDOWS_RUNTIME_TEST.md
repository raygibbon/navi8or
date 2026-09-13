# Native Windows smoke test

Status: **Not run on native Windows.** Cross-build and UMU/Proton results do not
validate this checklist. Use a real Windows machine or Windows VM.

## Setup and record

Copy the complete `dist/windows/` directory (including `themes/`) and this
checklist to Windows. Open an interactive Command Prompt in the copied directory.
Record the Windows version/build, console host/version (Windows Terminal or
Windows Console Host), font, display scaling, executable SHA-256, date, and tester.
Use an existing local directory and a harmless text file; include existing UTF-8
filenames if available. Do not test copy, move, delete, editing, or remote access.

To keep first-run configuration separate from the user's normal configuration,
save the following as `smoke-test.cmd` in the copied directory and run it from
that Command Prompt. The batch file scopes the temporary environment:

```bat
setlocal
mkdir smoke-appdata
set "APPDATA=%CD%\smoke-appdata"
nav.exe .
echo Exit code: %ERRORLEVEL%
endlocal
```

Use `nav.exe .` with attached console input/output; do not redirect stdout.
The application may create defaults under `smoke-appdata\Navi8or`.
After returning to the shell, record the exit code and whether the prompt,
cursor, and keyboard input work normally.

## Manual matrix

Replace each result with **Pass**, **Fail**, or **Skipped** and the exact observed
behavior. Give a reason for skips. For Left/Right, record the current view and
actual selection/navigation behavior. Do not infer a pass from process survival.

| Area | Action / expected result | Result / observation |
| --- | --- | --- |
| Startup | `nav.exe .` starts without a crash | Not run |
| Startup | Both panes render correctly | Not run |
| Startup | No literal ANSI/escape sequences appear | Not run |
| Input | Tab switches the active pane | Not run |
| Input | Up/Down move selection | Not run |
| Input | Left/Right behave correctly for the current view | Not run |
| Input | Enter opens a selected directory | Not run |
| Input | Backspace goes to the parent directory | Not run |
| Input | Ctrl+PgUp goes to the parent directory | Not run |
| Input | Ctrl+PgDn activates/opens the selected directory entry | Not run |
| Input | F3 opens the selected local text file in the viewer | Not run |
| Input | F10 quits cleanly from pane view (perform last) | Not run |
| Rendering | Pane borders and text render correctly | Not run |
| Rendering | Function-key bar renders correctly | Not run |
| Rendering | Active pane indication is visible | Not run |
| Rendering | Paths render correctly | Not run |
| Rendering | Existing UTF-8 filenames render correctly, if available | Not run |
| Resize | Shrink and enlarge the console; layout redraws correctly | Not run |
| Resize | No stale or garbled screen contents remain | Not run |
| Filesystem | Browse existing local directories | Not run |
| Filesystem | Open a normal local text file in the viewer | Not run |
| Filesystem | Escape returns from the viewer to pane view | Not run |
| Shutdown | F10 exits, with no crash dialog | Not run |
| Shutdown | Process terminates normally; record exit code | Not run |
| Shutdown | Shell prompt, cursor, and input remain usable | Not run |

## Current implementation assumptions

- Input uses `ReadConsoleInputW`, key-down records, repeat counts, modifier flags,
  explicit F1–F12/navigation mappings, and UTF-16 surrogate-pair decoding.
- Output sets UTF-8 console code pages and enables processed output, VT processing,
  and disabled automatic newline return; `WriteFile` sends bounded UTF-8 chunks.
  An interactive console that supports these modes is required.
- Resize uses `WINDOW_BUFFER_SIZE_EVENT`, re-reads the visible console dimensions,
  and resizes the cell buffers.
- Local paths use wide Win32 APIs and strict UTF-8 conversions. `.` resolves
  against the launching process's working directory, not the executable directory.
- Shutdown emits terminal cleanup sequences and restores saved input/output modes.
  Console code pages are set to UTF-8; the backend does not save/restore them.

For a failure, record the exact command, working directory, view, key sequence,
console host, visible error text, exit code, and a screenshot without sensitive
filenames. Reproduce on native Windows before changing console behavior.
No optional debug logging has been added for this initial pass.
