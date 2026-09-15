# Network preferences

Preferences → Network shows `Proxy Mode [ System ▼ ]`. Enter or Space opens the
shared enum picker; Up/Down moves, Enter confirms and Escape cancels the picker.
Custom proxy configuration is intentionally not implemented. No credentials are
stored in settings files.

System is the default. The operational schema on Linux and Windows is identical:

```toml
[network]
proxy_mode = "system" # or "none"
```

Missing values inherit System on normal startup. Invalid strings, types or
non-table `network` values produce a path-qualified configuration error.
Proxy settings belong to normal operational `nav.toml`, never the four bundled
templates or personal UI profiles. Profiles containing `[network]` are rejected.
Save preserves comments and unknown TOML data and atomically replaces the normal
config file only after validation. General and external Editor options use that
same operational writer; appearance and bindings use the separate profile writer.

The common HTTP `configure_request` sets `CURLOPT_PROXY` to `NULL` for System
(libcurl's default), or to `""` for No Proxy. It does not set `CURLOPT_NOPROXY`,
change environment variables, or modify TLS/CA/authentication options.
System therefore retains libcurl's HTTPS_PROXY/ALL_PROXY/NO_PROXY behavior and
lowercase counterparts. For HTTP, libcurl deliberately accepts `http_proxy`, not
uppercase HTTP_PROXY. No Proxy explicitly bypasses environment proxies; there is
no automatic retry with bypass on a network failure.

This behavior is documented in the official
[CURLOPT_PROXY reference](https://curl.se/libcurl/c/CURLOPT_PROXY.html), and was
verified in pinned curl 8.22.0 `lib/proxy.c`, `Curl_proxy_init_conn`: an empty
STRING_PROXY disables environment detection; absence permits normal discovery.

Apply changes subsequent requests immediately, including on already-open HTTP
panes. Providers retain a pointer to the app's long-lived operational config;
repository creation and direct URL resolution attach it before network requests.
Every listing, stream/range read, stat, write/upload, mutation and download uses
the common request configurator. Requests already started retain their configured
curl handle until completion. Basic/Bearer credentials, vault locking, repository
matching, redirect restrictions and TLS verification remain independent.

## Verification

`make network-test` uses an isolated self-signed HTTPS fixture (explicit fixture
repository `tls_verify=false`, never a default change) and unreachable
`HTTPS_PROXY=http://127.0.0.1:1`, with NO_PROXY empty. It exercises System → None →
System → None on existing providers for list/stat/stream/range/upload/download and
checks that the environment is unchanged. The API scenarios also repeat with
Basic and Bearer vault references and direct URL resolution/download, verifying
authentication on all successful requests. Its real PTY session proves picker
cancel, Space opening, Cancel-button rollback, unsaved Apply, restored System
failure, Save/restart persistence, unchanged Cancel and operation at 60×16.
`make preferences-integration-test` retains color/key-capture/conflict/profile
save and live label checks. Config/profile tests cover defaults, invalid modes,
operational save/reload, cancellation and separation. The HTTP suite retains
Basic/Bearer/vault/TLS coverage; location tests retain URL and Viewer downloads.

Repository add/edit/remove, vault management and profile Save/Save As remain
available through the existing menus. They were not duplicated into an empty
Repositories category. External editor arguments, history limits and transfer
buffer size remain configurable in operational TOML rather than placeholder UI.
