# Navi8or provider architecture

Navi8or is a small C dual-pane navigator. Providers are compiled in; each
saved HTTP repository creates an independently owned provider instance.

```text
NavPane (selection, filter, sort, scroll, history, NavLocation)
    |
    +-- NavProvider (list, location resolution, resource operations)
            +-- LocalProvider (read/write filesystem operations)
            +-- HttpProvider (anonymous list/stat/read and bounded random read)

selected NavEntry --resource_id--> provider read handle --> NavTransfer / NavViewSource --> NavViewer
```

`NavLocation` binds a provider to an opaque `resource_id` and a separate
`display_path`.  Commander displays the latter and passes the former back to
the provider.  It does not normalize, concatenate, or interpret provider
locations.  `NavEntry.name` is display text; `resource_id` is the provider's
stable selection/operation identity.  Size and modified time remain optional
metadata represented by their current fields.

`NavHistory` stores complete `NavLocation` values, so back/forward navigation
retains provider identity.  `nav_pane_refresh` relists through the pane's
provider and the UI restores the selected resource ID when possible.

Capabilities (`LIST`, `READ`, `WRITE`, `DELETE`, `MKDIR`, `RENAME`, `STAT`,
`RANDOM_READ`)
answer whether an operation is supported without provider-name branches.  F5
requires source read and destination write; F6 currently requires same-provider
rename; F7 and F8 require their corresponding active-provider capabilities.

`NavTransfer` remains above providers and owns the buffered read/write loop,
progress callbacks and error propagation.  Providers supply resource handles.
Future errors should be represented by a small provider-neutral `NavError`
instead of exposing errno, HTTP status, or transport-library values to UI.

`NavViewSource` has both a local-file source and a provider-backed HTTP source.
Local sources retain the existing complete line index. Remote sources instead
expose byte-backed line cursors: navigation discovers only the adjacent line
boundaries it needs, End starts from known EOF, and search scans incrementally.
The source asks `RANDOM_READ` providers for 64 KiB blocks and keeps 16 blocks in
an LRU cache. Thus roughly 1 MiB is a fixed cache bound rather than a document
ceiling, and no global remote newline index is built.

A small server response that ignores Range is safe only when it fits the first
bounded request. F5 uses the ordinary sequential `READ` handle and
`NavTransfer`, so a download is streamed in full rather than routed through the
Viewer cache. No second Viewer or HTTP-specific copy loop is involved.

Saved repository definitions remain above providers and outside panes,
transfers, and viewer state. HttpProvider exposes PUT, MKCOL, DELETE, and MOVE
only when the corresponding saved-repository capability is enabled; remote
editing remains unavailable. Authentication is not present.
