# Remote access

## HTTP repositories

Repositories manages saved HTTP/HTTPS directory-index roots. Opening replaces
only the active pane. Example repositories.toml:

```toml
[[repositories]]
name = "Example repository"
url = "https://example.com/files/"
tls_verify = true
# credential = "example-basic"
writable = false
mkdir = false
delete = false
rename = false
```

Anonymous by default; credential references require HTTPS. Opt-in capabilities:
writable PUT, mkdir MKCOL, delete DELETE, rename same-repository MOVE.
Remote editing/cross-provider moves are unavailable; interrupted PUT can leave a
partial server object. Reopen after editing a saved definition.

Directory HTML is streamed: nginx/Apache anchor rows, tables and preformatted
metadata. Recognized exact/human-readable sizes display; missing/malformed values
remain unknown, never zero. Rounded values are approximations. HTML wall-clock
dates use the client timezone. Browsing never sends per-entry HEAD requests.
Progress is cancellable; parser memory is bounded apart from returned entries.
Selection never fetches file bodies. External/query/root-escaping links are
ignored; oversized unfinished rows/tags fail instead of growing indefinitely.

## Locations and downloads

Ctrl+L (Ctrl+N O) or File → Enter URL / Location offers Open/Download/Cancel.
Directories navigate; resources open directly in Viewer without saving a repo.
Use a trailing slash for HTTP indexes; HEAD redirects can recognize slashless
directories. Slashless HTML without that redirect is a resource.
Resource queries are retained; fragments discarded; directory queries unsupported.
Embedded URL credentials are rejected. Local file paths also open Viewer.

Direct HTTP resources resolve an operation's explicit credential first, then
the longest matching saved repository credential, then the longest matching
HTTP authentication scope, then anonymous access. Matching uses canonical
scheme/host/effective-port and path boundaries, including encoded traversal
checks. A link never inherits the originating repository's credential merely
because its hostname matches. Repository/auth-scope TLS settings and the normal
System/No Proxy configuration still apply.

### Authentication Required

A 401 from Ctrl+L, Viewer/link View or Download opens the shared authentication
dialog. Locked configured credentials use the same flow. Enter on Credential
opens the compatible Basic/Bearer vault-name picker, or the normal masked master
password prompt when locked. Unlock returns to the operation, not a Vault menu.
No password is cached separately. Up/Down or Tab moves fields; Enter selects or
retries; Left/Right chooses Retry/Cancel; Esc cancels. A missing vault/compatible
credential is reported; create credentials through the existing Vault first.

Scope choices:

- **Use once** (default): the selected credential is bound to this exact URL and
  operation. No config is written. Viewer range/cache reads for that resource
  retain it, but following another link resolves independently.
- **Remember URL root**: edit the proposed parent URL, then confirm the canonical
  root before saving and retrying. Only same-origin parent directory prefixes
  are accepted; an origin-wide root must be explicitly entered and confirmed.
- **Add as Repository**: reuse the normal repository form with parent URL,
  selected credential and TLS setting prefilled. Saving retries the original
  resource without navigating away. The edited root must still contain it.

Remembered rules share repositories.toml, but do not appear as browsable repos:

```toml
[[http_auth_scopes]]
url = "https://example.com/logs/"
credential = "Work HTTP"
tls_verify = true
```

Only URL, vault reference and TLS verification are saved; secrets remain in the
vault. Remove/edit rules in repositories.toml and reload/restart to manage them.
Repository credentials take precedence over rules; a matching repository with
no credential can fall back to a rule. Failed names are marked already attempted;
retrying one requires manual confirmation, and another compatible credential can
be chosen. Ordinary 403 is reported as forbidden, never an automatic chooser loop.
Retry retains Viewer/history/fullscreen/pane and the confirmed download destination.

Redirects outside the provider root, or to any different URL during Use once,
are rejected before sending HTTP headers. Streaming redirects are also checked
before following. Unrestricted authentication forwarding is never enabled;
an out-of-scope redirect must be opened explicitly as a separate operation.
Open in Browser bypasses all this: the browser receives only the URL, never a
vault credential/header. Existing HTTPS-only credential transport remains; HTTP
URLs can be anonymous but cannot send Basic/Bearer secrets. No OAuth, cookies,
SSO, NTLM or browser-session import is implemented.

Download and Viewer Save Copy share editable Destination/Filename confirmation.
The default is the launching pane directory: choose a writable local destination
for remote origins. Safe download requires write/rename/delete, rejecting current
read-only SMB and HTTP PUT destinations. Exclusive filename.part streams, closes
and renames on success. Existing .part is untouched. Failure/cancel removes only
the new partial, preserving existing final files. Crash can leave .part.
Recursive downloads are unsupported. HTTP polls cancellation while waiting;
initial bounded HEAD is not interactive. Refresh to see saved files.

System/No Proxy applies to all HTTP actions: [Configuration](CONFIGURATION.md).
See [Viewer](VIEWER.md) for range/disk caching and explicit link actions.

## Credential vault

Repositories store vault names, never passwords/tokens. Credential picker offers
<none>, records and + Add credential. Inline creation creates/unlocks encrypted
Vault, confirms masked Basic/Bearer secrets and selects the record.
Cancel preserves fields. No secret reveal/copy action. Lock Vault when finished.

libsodium provides Argon2id key derivation and authenticated encryption.
Master passwords are not recoverable: use a strong password and encrypted backup.
TLS defaults on; disable only for disposable self-signed fixtures. Providers
resolve/wipe credential copies per operation/handle. Credentialed HTTP and
authenticated HTTPS downgrade are rejected. OS-native credential stores are
not implemented.

## SMB

libsmb2 browses/reads accepted SMB locations with Viewer/copy-to-local support.
The provider is read-only: no write/rename/mkdir/delete capabilities.
SMB authentication is separate from the HTTP Basic/Bearer vault.
Accepted syntax is defined in src/provider/smb_path.c; no broader protocol
matrix is claimed. Use smb://server/share/path/ or a UNC directory path.
