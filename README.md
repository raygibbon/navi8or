# Navi8or

Navi8or is a keyboard-first, two-pane file and repository navigator for Linux
and Windows. Browse local files, HTTP directory indexes and read-only SMB shares;
view text or copy files between supported providers.

## Features

- Independent panes with Full/Brief views, sorting, filtering and history.
- Two independent text Viewers, fullscreen, search and explicit URL actions.
- Streaming HTTP listings, viewing and transfers with bounded buffering.
- Encrypted Basic/Bearer credential vault.
- TOML profiles, live Preferences and context-sensitive configurable shortcuts.

## Download / Install

Download a package from [GitHub Releases](https://github.com/raygibbon/navi8or/releases)
and verify it against the accompanying `SHA256SUMS`.

Linux x86-64: extract the tar.gz and run `./nav` in the extracted directory.
Releases target Ubuntu 22.04 or newer compatible glibc systems.
Keep `themes/` beside the executable; optionally install `nav` on your PATH.

Windows x86-64: extract the zip and run `nav.exe` in an interactive Windows
Terminal or console with VT support. Keep `themes/` beside it.
No separate curl/sodium/SMB DLLs are required. Native Windows runtime
validation remains a maintainer pre-release check.

## Quick start

```sh
./nav
./nav /path/to/left /path/to/right
./nav -i themes/classic-dos.toml
./nav --version
```

On Windows use `nav.exe` and Windows directory paths.

## Basic controls

| Key | Action |
| --- | --- |
| Tab | Switch pane, including between independent Viewers |
| Arrows, Home/End, PgUp/PgDn | Navigate |
| Enter / Backspace | Open / parent directory |
| Ctrl+L | Enter URL or location; Open or Download |
| F1 / F2 | Help / current-context menu |
| F3 / F4 | View / external editor |
| F5 / F6 | Copy / move or rename (Files); next / previous match (Viewer) |
| F7 / F8 | Create directory / delete, where supported |
| / | Filter (Files) or Find (Viewer) |
| Escape | Close Viewer or cancel dialog |
| F10 | Quit (Files) or close active Viewer |

Keys are remappable. Menus, Help and hints show effective bindings.
Ctrl+N R or Ctrl+R R refreshes; Ctrl+R alone begins a sequence.

## Local / HTTP / SMB

Repositories saves HTTP/HTTPS index roots and optional vault references.
Ctrl+L also opens direct resources. TLS verification defaults on; HTTP mutations
require explicit capabilities. SMB is read-only. Recursive copy/deletion,
remote editing and browser-cookie authentication are unsupported.
See [Remote access](docs/REMOTE.md) and [Viewer](docs/VIEWER.md).

## Configuration and profiles

Options → Preferences edits General, Panels, Viewer, Editor, Appearance/Profile,
Key Bindings and Network. Apply changes the session; Save persists; Cancel
rolls back. Solar Dark is default; Classic DOS, Solar Light and Monochrome are
bundled. See [Configuration](docs/CONFIGURATION.md).

## Building from source

Linux: `make container-create`, then `make container-build` uses Ubuntu 22.04.
Windows: `make TARGET=windows windows-deps`, then `make TARGET=windows`
uses MinGW-w64.
See [Building](docs/BUILDING.md) and [Development](docs/DEVELOPMENT.md).

An experimental, local-filesystem-only Rust core is built alongside the C
application with `make rust` and run as `./nav-rs`. It does not replace `nav`;
F3 delegates local regular files to the existing C Viewer through a standalone
helper, while remote/provider operations remain intentionally deferred. Run its
tests with `make rust-check`. See [Rust rewrite](docs/RUST_REWRITE.md).

## License

Navi8or is MIT licensed; see [LICENSE](LICENSE).
Dependencies retain their licenses: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Static libsmb2 distribution includes a separate relinking kit. TDX/TDE provenance
and original source-header attribution are retained in [Development](docs/DEVELOPMENT.md).
