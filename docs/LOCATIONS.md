# Enter URL / Location

For streamed directory metadata, listing progress/cancellation and memory
scaling, see [Large HTTP directory listings](HTTP_LISTINGS.md).
For pane/fullscreen viewing and explicit text-link actions, see
[Viewer](VIEWER.md).

Ctrl+L (also the existing Ctrl+N O sequence) and File → Enter URL / Location...
invoke the same `location.open` command. The profile can rebind it with
`[keys] location = ...`. The dialog offers Open, Download and Cancel.
Enter opens the location; Tab/Down moves to the buttons and Left/Right selects
an action. Download confirmation has editable Destination and Filename fields;
Enter or Tab advances, and Escape cancels. Menu and Viewer key hints come from
the effective profile keymap.

Open navigates the current pane for local directories, configured SMB directories,
and HTTP/HTTPS URLs with a trailing slash. For HTTP URLs without one, a HEAD
probe can recognize a redirect to a trailing-slash directory. Other HTTP URLs
open directly in the streaming Viewer without browsing their parent or saving a
repository. Local file paths can also open Viewer. HTTP query strings are
retained on direct resources; fragments are discarded. Embedded URL credentials
are rejected; use the vault instead.

There is no universal HTTP directory marker. A slashless HTML index that does
not redirect opens as an HTML resource; supply its directory URL ending in `/`
to browse. A resource served at a trailing-slash URL is treated as a directory.
HTTP directories with query parameters are not supported by the existing HTML
listing provider. No filename-extension guessing or speculative full-body GET
is used to classify a resource. HEAD failures other than explicit authentication
failures leave Open free to try Viewer; downloads currently require HEAD to
succeed, as does the existing generic transfer engine.

Download transfers the supplied resource immediately after confirmation, skipping
Viewer. Viewer File → Download / Save Copy... uses the same confirmation and
transfer. It works for pane files and transient URLs. The default destination
is a snapshot of the launching pane's directory when Viewer opened; direct
Download uses the current pane. A read-only pane remains the default, so choose
a writable destination. No implicit switch to the other pane occurs.

Both directory and filename can be changed. Absolute local paths switch to the
local provider; an SMB destination can reuse the launching SMB provider. This
interface passes provider resource identities throughout. Currently destinations
must provide write, rename and delete operations for safe staging; the current local provider supports these. The existing
SMB provider is read-only, so SMB writes remain future work. HTTP PUT destinations are intentionally rejected
because they do not provide the required safe commit semantics here. Directory
recursive downloads are not implemented.

Direct HTTP resources use the longest matching saved HTTP repository root.
Matching uses canonical URLs and path boundaries, then the existing HTTP provider
checks encoded traversal rules. It preserves that repository's vault reference,
TLS verification and HTTP options. URLs outside the saved root do not match it for credential reuse. Unmatched URLs use an anonymous provider scoped to the URL origin;
redirects outside the selected root are rejected by the existing provider.
Locked saved credentials can open the existing Vault UI before retrying Open or
Download. 401/403 errors are reported without printing secrets; anonymous URLs
that require login need an existing matching authenticated repository. No browser
cookies or SSO integration is provided.

The default System proxy mode leaves normal libcurl environment behavior unchanged:
HTTPS_PROXY, ALL_PROXY, NO_PROXY and libcurl-supported lowercase forms are respected.
As usual, libcurl uses lowercase `http_proxy` for HTTP proxy selection; Navi8or
does not override this security behavior or bypass proxies automatically.
Preferences → Network can explicitly select No Proxy, applied to saved and direct
URL resources alike. See [Network preferences](NETWORK.md).

`resource.download` (short name `download`) is bindable for selected Panel files
and Viewer resources, with no new default
shortcut. For example:

```toml
[keys.viewer.commands]
download = "Alt+D"
```

Downloads use provider read/write chunks bounded by the normal transfer buffer
(default 4 MiB), and show transferred bytes and total when known. Unknown-length
responses stream to EOF. The target is first written to `filename.part` with
exclusive creation, closed, then renamed on success. A pre-existing `.part` file
is left untouched and reported as a conflict. Failure/cancellation removes the
partial created by this operation and leaves an existing final file intact.
Abrupt process termination can leave `.part` for manual cleanup. A crash does
not leave a newly completed-looking final file. Final overwrite semantics are
provided by the destination provider; as with existing file operations, concurrent
external changes between confirmation and commit are not locked.

Escape or a configured Dialog Cancel key cancels between chunks. HTTP downloads
also check cancellation within their existing curl polling loop, including while
a server has stopped sending bytes (100 ms poll interval). Other providers must
return from read before cancellation is observed. The initial HEAD probe is
bounded by a ten-second timeout and is not itself interactive. Progress does not introduce another queue
or download manager. Viewer remains open after its Download action.

Termbox's timed Alt-mode polling was fixed to deliver standalone Escape, using
the same short ambiguity delay as blocking input. The minimal patch is recorded
in `third_party/termbox2-patches/0002-timed-alt-escape.patch` and is already
applied to the vendored header. The Windows patch only rebases its removed
POSIX context for that line; its Windows implementation remains unchanged.
