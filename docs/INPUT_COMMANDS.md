# Commands, input contexts, and configuration

## Reference and migration

TDX was inspected read-only at `/var/home/rgibbon/git/tdx/`:

- `src/main.c`: leading `-i file` and `-ifile` options select `cmd_config`;
  relative names use the launching directory.
- `src/core/config.c:tdecfgfile`: TDX loads its executable-directory CFG, then
  a working-directory CFG if different, then the explicit file last. Without
  `-i`, its ordinary discovery still runs. Navi8or deliberately differs here:
  **explicit `-i` bypasses automatic discovery**, as required for this migration.
- `src/terminal/termbox_backend.c`: normalizes keys and Shift/Ctrl/Alt flags;
  ordinary Escape/Enter/Tab/Backspace aliases lose synthetic Ctrl modifiers.
- `src/core/query.c:getfunc`: maps normalized keys and two-key tree entries to
  functions, with syntax-specific lookup before the global key tree.
- `src/core/pull.c`: modal menu navigation, accelerators, disabled items, and
  shortcut text derived from the function/key map. TDX's window headers, mode
  line, and keyboard menu informed Navi8or's existing reusable controls.

Before this migration Navi8or already had a low-level terminal boundary, pure
menu/field/view-state helpers, TOML config, provider-independent pane operations,
and a read-only viewer. However, panel function keys used a raw-key chain,
menus had private command IDs and shortcut strings, the viewer duplicated its
keyboard/menu dispatch, and dialogs each interpreted physical controls.

## Runtime flow

```text
termbox / Windows console event
  -> terminal/termbox_input.c (normalized key + modifiers)
  -> ui/shell.c (active workspace/modal input boundary)
  -> input/keymap.c (context, binding, optional two-key prefix)
  -> NavAction (named NavCommand, or text/resize payload)
  -> workspace/dialog command handler
  -> existing provider/viewer/file-operation implementation
```

`include/nav_commands.def` is the command-name registry. `input/key.c` parses
physical key names; `input/keymap.c` owns defaults, overrides and prefix state.
No configuration string calls an implementation function. Menus return command
IDs to the same workspace dispatcher used by keyboard input. The command bar and
Help derive their content from the effective bindings; menu shortcut hints do too.

The shared shell owns input configuration, workspace transitions, and the command
bar. `ui/layout.c` owns shell and panel geometry. Panels and viewer use the same
menu/workspace/status/command-bar rows; the viewer puts its title inside the
workspace. Editor and terminal context/mode IDs reserve integration points only.
Existing synchronous dialogs remain modal and keep their saved-area redraw rules.

Panels and viewer look up their context before global bindings. Menus, dialogs,
confirmation prompts, information windows, pickers, and Vault capture unmatched
input rather than falling through to an underlying destructive action. Context
changes and resize cancel a pending prefix. Escape cancels a prefix; an unknown
second stroke consumes and cancels it. There is no timeout. The command bar shows
pending prefixes where it is visible.

Text insertion and menu mnemonic characters are `text.insert` payloads, not
raw-key shortcuts. Existing text fields remain ASCII-only. Raw terminal decoding
is confined to `terminal/termbox_input.c`; `ui/shell.c` alone polls terminal events; `ui/layout.c` synthesizes function-key
strokes only to render their bindings. No legacy
screen-level key chain remains.

## Default keys

| Context | Keys | Actions |
| --- | --- | --- |
| Global | F1 / Ctrl+H | Help (effective binding list) |
| Global | F2 / Ctrl+\ | Top menu |
| Panels | F10 / Ctrl+Q | Quit |
| Panels | F3, F4, F5, F6, F7, F8 | View, Edit, Copy, Move/Rename, Mkdir, Delete |
| Panels | Tab / Ctrl+U | Switch pane / swap panes |
| Panels | Up/Down, Home/End, PgUp/PgDn | Selection and paging |
| Panels | Left/Right | Column movement in Brief view |
| Panels | Enter / Ctrl+PgDn | Open directory or view file |
| Panels | Backspace / Alt+Up / Ctrl+PgUp | Parent directory |
| Panels | Alt+Left / Alt+Right | History back/forward |
| Panels | `/` | Filter |
| Viewer | Escape / Ctrl+Q / F10 | Close viewer |
| Viewer | Ctrl+S / `/`, F5, F6 | Find, next, previous |
| Viewer | G, W, L | Go to line, wrap, line numbers |
| Viewer | Arrows, Home/End, PgUp/PgDn | Movement; Ctrl+Left/Right scroll eight columns |
| Menu | Left/Right (also Ctrl), Up/Down, Enter | Heading/item movement, activate |
| Menu | Letters / 1–9 | Item mnemonic / heading number |
| Modal | Escape / Ctrl+Q | Cancel/close current modal |
| Text field | Left/Right, Home/End, Backspace/Delete, Enter | Edit position/content, accept |
| Confirmation | Y / Enter, N / Escape | Accept, cancel |
| Picker | Up/Down, Enter, Escape/F10 | Select, accept, cancel |
| Vault | F7, F4, F8, U/Enter, L, Escape/F10 | New, edit, delete, unlock, lock, close |

Panel command families (press and release the prefix, then the letter):

| Prefix | Second stroke -> action |
| --- | --- |
| Ctrl+F | C copy; M move; R rename; D delete; K mkdir; O open |
| Ctrl+N | P parent; O location; R refresh; B/F history back/forward |
| Ctrl+V | V viewer |
| Ctrl+S | F filter/search current listing |
| Ctrl+R | O repository; R refresh; V Vault |
| Ctrl+P | S switch; W swap; B brief; F full |
| Ctrl+T | C open config; R reload config; T theme info |

Compatibility decisions: traditional file-manager function keys remain.
**Ctrl+R alone now starts a family; use Ctrl+R R or Ctrl+N R to refresh.**
`file.rename` reuses the existing move/rename action. No new-file, protocol,
editor, terminal, or Git features were added. Commands only act in contexts with
an appropriate handler; reserved editor/terminal contexts have no workspace yet.
Classic terminals cannot distinguish Ctrl+H from Backspace, Ctrl+I from Tab,
or Ctrl+M from Enter. Their ordinary navigation/edit meaning wins; F1 remains
available for Help. Explicit Ctrl+Shift mappings win over printable Shift fallback.

## TOML and `-i`

```sh
nav -i custom.toml left-directory right-directory
nav -icustom.toml
nav -i docs/examples/keymap.toml
```

Options precede zero, one or two directory operands; `--` ends options. Missing
`-i` arguments, unreadable/malformed explicit files, and invalid keymaps return
exit status 2 before terminal startup. Explicit files are never auto-created and
do not fall back to another config file. Shell expansion handles `~` on POSIX;
Navi8or does not implement shell expansion itself.

Without `-i`, existing automatic discovery and first-run defaults remain:
`$XDG_CONFIG_HOME/nav/nav.toml` or `~/.config/nav/nav.toml` on POSIX,
`%APPDATA%/Navi8or/nav.toml` on Windows. Reload preserves the running configuration
if parsing fails. Open Configuration and Reload use the selected path.
`repositories.toml` is loaded/saved beside the selected config; Vault storage and
theme asset lookup keep their existing locations.

Existing TOML sections remain compatible. Optional `[app]` keys `theme`,
`confirm_delete`, and `show_hidden` override their legacy equivalents.
See [the runnable example](examples/keymap.toml). Key sections are `keys.global`,
`keys.panel`, `keys.viewer`, `keys.menu`, `keys.dialog`, `keys.confirm`, `keys.info`,
`keys.picker`, `keys.vault`, `keys.editor`, and `keys.terminal`. Families use full
sequences such as `"Ctrl+F C"` in the appropriate section, not a special file-family
parser. The parser supports one or two strokes, Ctrl/Alt/Shift, F1–F12, named
navigation keys, Escape/Esc, Tab, Enter, Backspace, Delete, Space, Plus, and ASCII
characters. Key names are case-insensitive; command names are case-sensitive.

Overrides replace defaults for that physical sequence. `"none"` unbinds a stroke.
Unknown commands/contexts, non-string values, duplicate physical bindings
(including differently spelled aliases), and single-key/prefix conflicts are
errors. A context cannot bind both `Ctrl+F` and `Ctrl+F C`. There are at most 256
bindings including defaults. Unspecified settings/bindings retain defaults.

## Verification

`make input-test`, `make terminal-test`, `make control-test`, and `make config-test`
cover decoding, names, context precedence, chords, cancellation, rebinding,
invalid config and explicit selection. `make input-integration-test` uses a PTY
to verify both CLI spellings, real viewer/copy dispatch, modal cancellation,
shared-shell rows and dynamic shortcut labels. `make check` includes these and
existing local/HTTP/Vault tests. `make resize-test` covers UI behavior and resize.
Windows uses the same command/keymap code; no Proton-specific backend changes
are part of this migration. Native Windows validation remains separate.
