# Credential Vault

Navi8or's credential Vault is the first backend for the provider-neutral
`NavCredentialStore` API. It stores Basic credentials (name, username, and
password) and Bearer credentials (name and token). Credential names are stable
and immutable in format v1; rename can be added alongside future repository
references.

The portable Vault lives at `~/.config/nav/vault.bin` (or
`$XDG_CONFIG_HOME/nav/vault.bin`). Secrets and the master password are never
stored in `nav.toml`. The file uses a versioned, explicitly serialized binary
format, Argon2id key derivation through libsodium `crypto_pwhash`, and
XChaCha20-Poly1305 authenticated encryption. KDF algorithm, salt, operation
limit, and memory limit are stored in the header. The backend-neutral store
dispatches to this Vault backend; crypto and format handling stay in the Vault,
while bounded reads and secure atomic replacement are isolated in the POSIX
platform layer. Updates use a same-directory mode-0600 temporary file, `fsync`,
atomic rename, and a best-effort parent-directory `fsync`.

`NavCredentialStore` is backend-neutral. Explicit Vault constructors create
the current encrypted-file backend; after construction, lock, unlock, listing,
mutation, and resolution all use backend-independent store operations.

Open **Repositories → Credential Vault**. A missing Vault offers F7 to create
one. A locked Vault accepts Enter or U to unlock. An unlocked Vault lists only
Name, Type, and Basic username; it never displays passwords or tokens. F7
creates a credential, F4 edits it without revealing the existing secret, F8
deletes it, and L immediately locks and wipes the in-memory key and records.
Leaving the screen does not lock automatically; explicit time-based auto-lock
is future work. Application exit always closes and wipes the store.

There is no password recovery or plaintext export. Losing the master password
makes the Vault contents unrecoverable. Do not reuse production passwords in
development fixtures. Encryption protects the file at rest, but cannot protect
secrets while the unlocked process must hold them in memory; further process
memory and core-dump hardening is future work.

Builds require libsodium. On Debian:

```sh
sudo apt install libsodium-dev
```

The build discovers libsodium through `pkg-config`. A future release may also
offer a pinned bundled build for Windows or mobile packaging.

HTTP repositories may name a Vault credential in their optional `credential`
field. Basic records configure HTTP Basic authentication; Bearer records
configure libcurl's Bearer authentication. The provider retains only the name
and store reference. It resolves an owned copy at the start of every LIST,
STAT, GET/Range, PUT, MOVE, MKCOL, or DELETE operation and wipes that copy when
the operation or streaming handle closes. Credentialed plain HTTP is rejected,
authenticated redirects cannot downgrade from HTTPS, and libcurl's default
cross-origin credential suppression remains in force.

OS-native CredentialStore backends such as Windows Credential Manager, macOS
Keychain, and Linux Secret Service/KWallet may be added later by implementing
the small internal store operations table, without changing providers or Vault
crypto.

## Repository credential selection

Add/Edit Repository offers a scrolling, keyboard-driven Credential picker in both
Modern and Classic styles. Up/Down moves, Enter selects, and Esc keeps the current
reference. `<none>` removes only the reference, never the Vault record. Rows show
metadata (name, type, Basic username), with no secret reveal or copy action.
`+ Add credential...` creates/unlocks the Vault inline, offers the provider's
supported types, and selects the newly saved record. `Unlock Vault...` makes
existing records available. Cancel and failed unlock preserve repository fields;
retry from the picker. Unrelated edits can retain a locked reference without unlock.
Missing and unsupported current references stay visible and are preserved until
explicitly changed; runtime access still reports credential errors.

Repositories store only `credential = "name"` in `repositories.toml`, never
passwords, tokens, or a second auth-method field. Names can be shared across
repositories; editing a Vault secret updates subsequent operations for all users
of that reference. Creating credentials leaves the Vault unlocked.

The provider registry supplies `nav_provider_supports_credential_type` using a
small provider-owned acceptance callback. HTTP accepts Basic/Bearer. The picker
checks metadata without resolving secrets, while protocol authentication mapping
remains in the HTTP provider. Future SFTP/SMB authentication can reuse this picker
and reference model by supplying acceptance rules; this pass adds no protocols
or credential types and changes no crypto or Vault format.
