# Navi8or

Navi8or is an independent, keyboard-first terminal resource navigator, launched
with the `nav` command. Its dual-pane Commander works across local and remote
resources with Far-style navigation and Brief behavior. Navi8or owns its UI,
layout, and semantic themes; portions of its compact control mechanics were
adapted from TDX/TDE.

The permanent menu bar occupies its own top row. Each Commander pane below it has a provider title band, a provider-friendly location
row, responsive column headings, a restrained separator, a dense listing body,
and a pane summary. Full is the default pane view, with aligned Name, human-readable Size, and Modified columns; Far-style multi-column Brief remains available through the View menu.
Compact pane labels and application status sit above the segmented operation
bar; the semantic message role is reserved for transient prompts rather than normal
Commander chrome.

## Build and run

Navi8or currently targets Linux/POSIX. The compatible termbox2 single-header
source is vendored under `third_party/termbox2`; HTTP repositories use libcurl,
and the encrypted credential Vault uses libsodium.

On Debian, install the build dependency with:

```sh
sudo apt install build-essential pkg-config libcurl4-openssl-dev libsodium-dev
```

```sh
make
./nav
./nav /var/log /tmp
```

Development checks:

```sh
make check
make resize-test
make asan-check
make verify-vendor
make dist
make dist-check
make http-test
make vault-test
```

### Linux development with Ubuntu 22.04 and rootless Podman

Run from the repository root on either Bazzite or Debian:

```sh
make container-create     # initially: create and install build dependencies
make container-build
./nav
```

`container-create` uses `ubuntu:22.04` to create the persistent `navi8or-build`
container, bind-mounting `$PWD` at `/src` with `:z` for SELinux compatibility:

```sh
podman run -d --name navi8or-build --userns=host \
    -v "$PWD:/src:z" -w /src ubuntu:22.04 sleep infinity
```

There is no Containerfile or custom image build. Packages and the installed
libsmb2 library persist in the container. Repeating `container-create` reuses
it and ensures dependencies are installed; it never recreates an existing
container. A stopped container is started automatically. `BUILD_CONTAINER`,
`BUILD_IMAGE`, and `PODMAN` are defined once near the top of the native Makefile;
rootless Podman is required, without sudo. Each machine creates its own local
Ubuntu 22.04 container. Apt updates mean the package patch versions may differ
if creation happens at different times; this is a common distribution baseline,
not a locked reproducible toolchain.

```sh
make container-clean     # remove Linux outputs, preserve Windows outputs
make container-shell     # interactive bash in /src; run make check here
make container-remove    # delete the container and its installed dependencies
```

Every action verifies that `/src` is a writable bind mount of the current
repository. A container created from another checkout produces a clear error;
use `make BUILD_CONTAINER=another-name container-create` for a second checkout,
or remove the old container from its original checkout before creating it here.
Removing a container leaves host sources and build outputs intact.

Compilation uses `podman exec --user 0:0 --workdir /src navi8or-build` to run
`make TARGET=native`. With rootless Podman's default mapping, container UID/GID
0 map to the invoking host user, not host root. A write probe verifies actual
host ownership before building. Sources stay on the host; `build/` and `nav`
appear there immediately. Each `container-build` clears stale Linux outputs to
prevent reuse of host-built objects while preserving `build/windows` and
`dist/windows`. Native `make` and Windows builds are unchanged. Run `make clean`
before switching back to native Linux compilation.

The setup installs build-essential, pkg-config, cmake, Python 3, curl, CA
certificates, file/binutils, and Ubuntu's libcurl/OpenSSL/libsodium development
packages. Ubuntu 22.04 does not package libsmb2, so setup builds the same pinned
upstream revision as the Windows dependency script in a temporary container
folder and installs it in `/usr/local`. Only libsmb2 is static, avoiding a new
custom runtime library requirement; Kerberos/GSSAPI and DCE/RPC are disabled
(Navi8or uses NTLMSSP). Windows dependency scripts are unchanged. termbox2 and
TOML remain vendored C sources. libcurl, libsodium, OpenSSL and libc remain
dynamic; the application is not fully static.

Verify on both the build environment and destination host:

```sh
file ./nav
ldd ./nav
readelf -d ./nav
```

A common Ubuntu baseline removes Fedora-specific compile/link inputs, but
runtime SONAMEs and symbol versions must still be available on the destination.
The installed Ubuntu `libsodium.a` could be selected through `SODIUM_LIBS` in a
future change to remove its runtime requirement; static libraries must be
rebuilt for security updates. This workflow leaves libsodium dynamic to keep
the immediate change focused.

Ubuntu 22.04 compilation also uses the older libcurl protocol bitmask API
with the same HTTP/HTTPS restrictions, and suppresses ignored-I/O-result
warnings only within the vendored termbox2 implementation.

## Implemented local v0.1 features

- Two independently navigable local panes with selection scrolling and history.
- TDX/TDE-derived logical key handling through a termbox-only terminal boundary.
- Semantic header, text, selection, border, pane-title, and status roles.
- Responsive Full metadata columns and Far-style column-major Brief navigation.
- A width-adaptive segmented Commander key bar whose labels always match working commands.
- Exact CP437 TDX frame sets for framed menus and transient dialogs; Classic TDX defaults to Combine frames with spacing and shadows.
- Live filename filtering over the current listing.
- Read-only F3 text viewer with line numbers and visible Find prompt.
- Binary detection and a metadata view containing name, path, size, and modification time.
- Shell-free F4 launch of `tdx`.
- Streamed local file copy with an on-screen byte/percentage progress dialog and overwrite confirmation.
- Move or rename to an editable destination, defaulting to the opposite pane.
- Directory creation and default-No file/empty-directory deletion confirmation.
- Name, size, and modification-date sorting with directories grouped before files.
- Global Show Hidden toggle that refreshes both panes.
- Keyboard menus with non-selectable separators and visibly disabled future items.
- Context-sensitive menus: Commander permanently displays its menu bar and activates it with F2 or `Ctrl+\\`; the read-only Viewer uses `Ctrl+\\` for its own File/View/Search/Options/Help menu.
- Cursor-aware, horizontally scrolling text fields shared by Filter, Find, and file-operation prompts.
- An encrypted Basic/Bearer Vault backend behind the CredentialStore abstraction, with explicit lock/unlock and masked secret entry.
- Scrollable Help and information windows, reusable confirmation dialogs, and two-path startup.

## Configuration and themes

Use `nav -i path/to/nav.toml` (or `-ipath/to/nav.toml`) to select a TOML file
without automatic config discovery. Keyboard bindings and generic Ctrl-prefix
families resolve to shared named commands. See [input/config conventions and
default keys](docs/INPUT_COMMANDS.md) and the [example keymap](docs/examples/keymap.toml).
Refresh is now `Ctrl+R R`; the traditional file-manager function keys remain.

On first run Navi8or creates `$XDG_CONFIG_HOME/nav/nav.toml` and
`$XDG_CONFIG_HOME/nav/themes/solar-dark.toml`; without `XDG_CONFIG_HOME` it
uses `~/.config/nav/`. These files control hidden files, sort mode, Viewer line
numbers and wrapping, the F4 editor argv, confirmations, transfer buffer size,
history behavior, and the active theme. Syntax highlighting is not implemented
and is intentionally not a configuration option. Navi8or uses the vendored tomlc99
parser. Malformed files report an error and retain the running configuration
during live reload; invalid supported values produce a warning and retain a
bounded default. `make dist` creates a clean source archive, and `make
dist-check` builds and tests the extracted archive.

Solar Dark is the restrained Modern default. Themes choose the small
`ui.style = "modern"` or `"classic"` presentation profile independently from
their semantic colours; Classic DOS, Navi8or Classic, Commander, CRT,
Workbench, and CDE palettes retain their traditional presentation. New themes
use semantic format-2 `ui.*` roles. Existing format-1 themes using
historical `tdx.*`, `viewer.*`, `search.*`, `progress`, and `navigator.*` keys
remain supported through one compatibility translation layer. See
[`docs/THEME_FORMAT.md`](docs/THEME_FORMAT.md).

`Tab` switches Commander panels. Closing a pull-down leaves the permanent
Commander menu bar visible, closing Viewer returns to the previously active panel, and `Ctrl+\\`
inside Viewer never opens the Commander menu.

Commander `File` provides `Open Configuration`, `Reload Configuration`, and
`Current Theme`; `Command` groups the working file operations. Reload applies valid settings and theme changes atomically;
malformed configuration leaves the running settings untouched.

Directory copying and recursive deletion are not implemented; Navi8or reports these limitations rather than silently succeeding.

## Repositories

The Repositories menu can add, edit, remove, and open saved HTTP/HTTPS
directory-index roots. The Credential picker lists existing Basic/Bearer records
and offers **<none>** for anonymous access and **+ Add credential...**. Inline
creation creates or unlocks the Vault when needed, then selects the new record.
The credential type determines authentication; repository config stores no auth type. Only the credential name is stored in
`repositories.toml`, and credentialed repositories require HTTPS. See
[`docs/VAULT.md`](docs/VAULT.md).

Opening a repository replaces only the active pane; the
other pane is untouched. Remote panes use the same Enter, Backspace,
`Ctrl+PageUp`, `Ctrl+R`, Tab, Full, and Brief behavior as local panes. Enter and
refresh fetch only the current directory index—merely moving the selection never
downloads a file body.

Repository v1 recognizes ordinary nginx/Apache-like `<a href>` directory
indexes. It decodes URL-escaped leaf names and accepts relative or same-root
absolute links. Query links, external links, unsupported schemes, and links
escaping the configured root are ignored. Listing responses are limited to 4
MB, redirects to five hops, connection time to three seconds, and total listing
time to ten seconds.

F3 and Enter open remote files in the existing read-only Viewer. The HTTP view
source uses a 16-block LRU cache of 64 KiB byte ranges. The roughly 1 MiB bound
is cache capacity, not a document-size limit: PageDown, PageUp, End, and search
can fetch or refetch blocks anywhere in a known-size remote file without a
whole-file line index, full download, or temporary mirror. Recent backward
movement reuses cached blocks. The status line reports the real byte position
and size; exact line ordinals are shown while known and `?` after a direct
near-EOF seek. Servers that ignore Range are accepted only when the complete
response fits in the first 64 KiB request; larger non-Range responses are
stopped and reported. F5 streams between providers through the generic transfer
engine, including objects without a known Content-Length. Failed HTTP-to-local
downloads remove their incomplete local destination. Writable repositories also
accept streamed local-to-HTTP PUT uploads. An interrupted HTTP PUT may leave a
partial remote object because Navi8or cannot safely assume a failed PUT
created a deletable object. Repositories may separately enable F7 directory
creation through MKCOL, confirmed F8 deletion through DELETE, and same-repository
F6 rename through MOVE; each mutation capability defaults to disabled. Remote
editing and cross-provider move remain unavailable.

Repositories are stored in the app-managed
`$XDG_CONFIG_HOME/nav/repositories.toml`. This keeps repository menu changes
from rewriting comments or formatting in `nav.toml`; it is the single source of
truth for saved repositories. Names are case-insensitively unique. Editing or
removing a saved entry does not mutate an already-open provider instance; reopen
it to use new settings. From a remote pane, `File -> Open Location` with an
absolute local path switches that pane back to the local provider.

```toml
[[repositories]]
name = "Local Navi8or HTTPS"
url = "https://localhost:8443/"
credential = "local-basic" # optional Vault record name
tls_verify = false # self-signed development server only
writable = true
mkdir = true
delete = true
rename = true
```

TLS verification defaults to `true`; mutation flags default to `false` and the
credential defaults to anonymous when omitted. Credentials are resolved for
each HTTP operation and never written to repository configuration or error
messages. See
[`docs/HTTP_TEST_SERVER.md`](docs/HTTP_TEST_SERVER.md) for the local nginx HTTPS
setup.

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
| F2 | Open the Commander menu (alias for `Ctrl+\\`) |
| F3 | View |
| F4 | Edit with the configured editor |
| F5 | Copy to opposite pane |
| F6 | Move/Rename; local same-provider moves default to the opposite pane, while HTTP repositories with `rename = true` rename in the active directory using MOVE |
| F7 | Make directory |
| F8 | Delete file or empty directory |
| Ctrl+\\ | Open the current screen's menu (canonical TDX binding) |
| Ctrl+Right | Next top-level menu while a menu is active |
| Ctrl+Left | Previous top-level menu while a menu is active |
| F10 | Quit |
| Esc | Cancel or close the current temporary view |

## Architecture

The Commander UI works with `NavProvider`, not POSIX directory calls. Providers expose metadata and stream open/read/write/close operations; the central transfer engine moves bytes and reports progress. This boundary is intended to support future cross-provider transfers without changing Commander commands.

Terminal rendering is isolated in `src/terminal`, local filesystem work in `src/provider`, transfers in `src/transfer`, platform process launching in `src/platform`, and Navi8or's reusable controls in `src/ui/core`. Commander and Viewer presentation remain in `src/ui`. Theme colours and CP437 symbols are separate application-level models. Modern presentation removes the extra pane-heading rule and uses flatter summaries, status, menus, dialogs, and keybar; Classic preserves the stronger separator and historical frame/shadow character. See [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md).

### TDX-derived UI components

Navi8or's terminal event model and modifier-aware key translation are adapted from TDX's termbox backend. Resize is a first-class event: persistent controls recalculate and redraw, while pull-down menus close after preserving their current major/minor selection, following TDX's transient-window lifecycle.

The CP437 conversion is ported from TDX. Navi8or's UI core closely adapts TDX/TDE's major/minor menu model and pull-down flow, `query.c` field-editing semantics, and Help/window viewport movement. Box, confirmation, information, and Viewer controls preserve the useful redraw and Escape semantics without importing editor buffers or editor-global state.

Navi8or and TDX are separate applications. Navi8or owns its UI API and semantic theme vocabulary, while retaining attribution for code and design ideas adapted from TDX/TDE. The projects may continue to share ideas; only F4 optionally launches the separately installed `tdx` program.

## Planned

- ETag/Last-Modified remote Viewer cache validation
- Artifactory-specific APIs
- Optional custom-header authentication
- Background transfer queue
- Additional TOML themes and broader preference coverage
- Windows and macOS platform implementations
