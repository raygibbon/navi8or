# Navi8or

`nav --version` prints the application identity without opening the TUI or
loading configuration. The root `VERSION` file is the only editable application
version. Native/container and MinGW builds generate `build/generated/nav_version.h`
from it, shared by CLI, menu-bar branding and HTTP user agent. Changing only
`VERSION` updates the next build; unchanged content preserves the header timestamp.
Source packaging includes `VERSION` and the generator, not the generated header.
Both Linux and Windows use the same value. Run `make version-header` to generate
the header independently, or `make TARGET=windows version-header` for cross builds.

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

Linux builds own their application libraries; distro `-dev` packages are not
used. termbox2 and TOML keep their vendored C build paths. Windows retains its
separate dependency script and MinGW build.

### Canonical Linux development environment

Run from either Bazzite or Debian, in the repository root:

```sh
make container-create     # initially, or to ensure bootstrap tools are installed
make container-deps       # optional: container-build also builds missing dependencies
make container-build
./nav
```

The persistent rootless `navi8or-build` container uses `ubuntu:22.04`. No
Containerfile or custom image is built. Creation binds the current checkout:

```sh
podman run -d --name navi8or-build --userns=host \
    -v "$PWD:/src:z" -w /src ubuntu:22.04 sleep infinity
```

Every action verifies that `/src` is this checkout's writable bind mount.
Existing containers are reused and started if stopped. `BUILD_CONTAINER`,
`BUILD_IMAGE`, and `PODMAN` are configurable near the top of the Makefile.
For another checkout use a different `BUILD_CONTAINER`, or remove the original
container from its original checkout. Each machine creates its local container.
Ubuntu 22.04 supplies glibc 2.35 and bootstrap tools (compiler, make, pkg-config,
CMake, autotools, Perl, Git, curl, certificates, Python, file/binutils). Apt patch
versions can differ; this pins application sources, not the entire toolchain.
Previously installed application development packages can remain in an existing
container, but are not searched or linked by this build.

Builds run `podman exec --user 0:0 --workdir /src navi8or-build make
TARGET=native USE_LINUX_DEPS=1`. Rootless container UID/GID 0 map to the invoking
host user; a write probe checks ownership. Sources remain editable on the host,
and `build/` and `nav` appear there immediately. Repeated builds are incremental.

```sh
make container-shell         # in /src: make check; make resize-test
make container-clean         # application/test outputs; keep dependencies and Windows
make container-deps-clean    # discard dependencies, Linux objects and nav; keep cache
make container-remove        # remove container; host sources/build outputs remain
```

### Pinned Linux application libraries

`scripts/build-linux-deps.sh` holds exact versions, source URLs and SHA256 hashes:
curl 8.22.0, libsodium 1.0.22, libsmb2 7.0.0 at immutable revision
`b3d560c02fb1268320d2fd1c17fe841b0d93b85f`, OpenSSL 3.5.8, and zlib 1.3.2.
Downloads are cached in `.deps/linux-sources/`; every archive used for a rebuild
is verified before extraction. A mismatch fails, including a corrupted cached
archive. Remove that archive and retry; never disable verification.

Sources, compilation trees, headers, static archives and pkg-config metadata
are regenerated under `build/linux-deps/{src,build,include,lib}`. None are source
artifacts to commit. Nothing is installed into `/usr` or `/usr/local`. A stamp
checks the script/configuration, compiler, architecture, glibc and absolute
prefix; changed build environments rebuild dependencies and Linux objects.
Unchanged builds check the stamp and required artifacts without recompiling.

Linux links explicit `libcurl.a`, `libsodium.a`, `libsmb2.a`, `libssl.a`,
`libcrypto.a` and `libz.a` paths. glibc remains dynamic. Curl enables HTTP/HTTPS,
OpenSSL, zlib, IPv6, threaded DNS, HTTP/HTTPS/SOCKS proxies and authentication;
LDAP, SSH, IDN, PSL, brotli, zstd, HTTP/2 and HTTP/3 dependencies are disabled.
libsmb2 keeps NTLMSSP; Kerberos/GSSAPI and DCE/RPC are disabled. OpenSSL has
built-in providers and no external provider modules or automatic global config
loading. This does not provide system FIPS/provider integration.

TLS verification remains enabled according to the existing provider settings.
No CA bundle is embedded: OpenSSL loads the running system's hashed
`/etc/ssl/certs` directory. `SSL_CERT_FILE` and `SSL_CERT_DIR` override its default
trust paths, including a corporate CA bundle/directory. Ensure corporate roots
are installed in each system/container that performs HTTPS requests. Fedora
systems whose trust store is elsewhere can set `SSL_CERT_FILE` to
`/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem`. Proxy behavior is unchanged: libcurl honors
`http_proxy` (lowercase for security), `HTTPS_PROXY`, `ALL_PROXY`, and `NO_PROXY`
and their supported lowercase equivalents; uppercase `HTTP_PROXY` has never
been accepted by libcurl.

Clean canonical build and verification:

```sh
make container-create
make container-deps-clean
make container-deps
make container-build
podman exec -w /src navi8or-build make check
file ./nav
ldd ./nav
readelf -d ./nav
```

Native Linux `make` builds the same owned dependencies with the native compiler;
`make linux-deps` and `make linux-deps-clean` are its explicit dependency commands.
A native build on a newer distro can raise the glibc baseline: release binaries
must use the Ubuntu container. macOS retains native library discovery.
`USE_LINUX_DEPS=0` retains the previous system-library build as an explicit escape
hatch (run `make clean` before switching modes); its binaries do not have the application-library portability guarantee.

To upgrade, deliberately update the version/revision, URL if needed, and SHA256
in `scripts/build-linux-deps.sh`, checking official release checksums/signatures
where supplied. Rebuild with `make container-deps-clean`, then
`make container-build`, run `make check` in the container and repeat linker/ABI
inspection and destination smoke tests. Static security fixes require releasing
a rebuilt executable. The result is architecture-specific and still depends on
the target's glibc/kernel ABI, DNS configuration and runtime trust store.

## Implemented local features

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
- [Pane/fullscreen Viewer](docs/VIEWER.md) with separate resource Back history
  and explicit View/Download/Browser/Copy actions for HTTP/HTTPS text links.
- Cursor-aware, horizontally scrolling text fields shared by Filter, Find, and file-operation prompts.
- An encrypted Basic/Bearer Vault backend behind the CredentialStore abstraction, with explicit lock/unlock and masked secret entry.
- Scrollable Help and information windows, reusable confirmation dialogs, and two-path startup.

## Configuration and themes

Run `nav` for the unchanged Solar Dark default, or load a UI profile directly:

```bash
nav -i themes/classic-dos.toml
nav -i ~/.config/nav/my-profile.toml
```

Settings apply in order: compiled defaults, normal `nav.toml`, the exact `-i`
profile, then explicit command-line directory arguments. Missing or malformed
explicit profiles fail with their path and reason. Copy one of the four shipped
profiles (Classic DOS, Solar Dark, Solar Light, Monochrome) and customize it.
Open **Options → Preferences** (default `Ctrl+T P`) to preview colors, display
settings and captured key bindings; Apply, Save/Save As and Cancel share the TOML
profile model. See [editing and saving profiles](docs/PROFILES.md),
[profile settings](docs/THEME_FORMAT.md) and
[commands and default keys](docs/INPUT_COMMANDS.md).

On first run Navi8or creates `$XDG_CONFIG_HOME/nav/nav.toml` and a Solar Dark
palette; otherwise it uses `~/.config/nav/`. Normal configuration retains
repositories, credentials, vault and transfer/editor operational settings.
Profiles configure colors, chrome, pane display, Viewer presentation and keys.
Existing semantic format-2 palettes and historical format-1 palettes remain
accepted. No syntax highlighting or internal editor options are introduced.

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
escaping the configured root are ignored. Directory HTML is parsed incrementally
with 16 KiB bounds on unfinished tags and rows; there is no whole-document size limit.
nginx-style metadata after each closing anchor supplies optional exact byte sizes
and `DD-Mon-YYYY HH:MM` modified times. Missing, malformed, or rounded size values
remain unknown. HTML wall-clock dates use the client's local timezone because
the format does not identify the server's timezone. Explicit stat operations
also read Last-Modified from their existing HEAD response; browsing never issues
per-entry metadata requests.
Duplicate resolved URLs use a lightweight hash index. Individual oversized tags
or rows abort the listing, and incomplete final tags are ignored. Redirects are limited
to five hops, connection time to three seconds, and total listing
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
- Broader preference coverage
- Windows and macOS platform implementations

Browse or download pasted URLs with Ctrl+L or File → Enter URL / Location...;
see [location and download behavior](docs/LOCATIONS.md).
