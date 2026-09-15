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

Longest matching saved HTTP root supplies auth/TLS, with canonical boundaries
and traversal checks. Unmatched URLs use anonymous origin-scoped providers;
redirects outside the selected root are rejected. No cookies/SSO or cross-root
credential reuse. Locked credentials require Vault unlock/retry; 401/403 errors
do not print secrets.

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
