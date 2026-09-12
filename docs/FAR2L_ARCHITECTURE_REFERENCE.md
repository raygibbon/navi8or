# Far2l architecture reference

Far2l was studied as a GPL architecture and behaviour reference only.  No
Far2l source code, comments, class layouts, or implementation has been
incorporated into Navi8or. Navi8or owns its terminal UI presentation, controls,
colours, and screen behavior. TDX/TDE is a sibling project and provenance
reference for selected mechanics.

## What was studied

The dual-panel core separates a panel's state from its file-list update and
rendering work (`far2l/src/panels/panel.*`, `filelist.*`, `flshow.cpp`, and
`flupdate.cpp`).  Plugin-backed panel support is considered separately from
normal directory lists (`flplugin.cpp`).  Quick View, information, tree, and
display-mode ideas are represented in `qview.*`, `infolist.*`, `treelist.*`,
and `flmodes.cpp`.

NetRocks demonstrates that one panel can represent a remote connection while
the other remains local.  Its location, connection-pool, directory-enumeration,
transfer-operation, and background-task areas were reviewed conceptually:
`plugins/NetRocks/src/Location.*`, `ConnectionsPool.*`, `Op/OpEnumDirectory.*`,
`Op/OpXfer.*`, and `BackgroundTasks.*`.

## Decisions for Navi8or

| Far2l concept | Navi8or decision |
| --- | --- |
| File panel | Adopt the separation of pane state and listing access. |
| Plugin panel | Simplify to a compiled-in `NavProvider` table; no plugin ABI. |
| Capability differences | Adopt explicit capability flags. |
| Quick View | Adopt later using the existing `NavViewer` and `NavViewSource`. |
| Info panel | Consider later; Properties is a small possible basis. |
| Tree panel | Not needed currently. |
| NetRocks | Architecture reference only; initial remote scope is HTTP/HTTPS. |
| Background jobs | Future `NavTransfer` extension, not this pass. |
| Many display modes | Keep only Files, then possibly Quick View and Info. |
| Far UI/theme | Do not copy; Navi8or owns its UI and semantic theme model. |

## Lessons applied

An active pane owns selection, filtering, sorting, scrolling, history and its
current location.  A provider owns listing, resource identity resolution and
resource operations.  The provider never owns menu state, terminal geometry,
or active-pane state.  Copy remains a cross-panel action: a transfer engine
uses a readable source and writable destination rather than branching on a
provider name.

Remote sessions, credentials, protocol errors, and refresh policies must stay
behind a future HTTP provider.  Saved repository roots likewise configure a
provider above the panel; they are not built into its resource model.  A remote
F3 path should open a provider resource and adapt it into `NavViewSource`, so
it shares `NavViewer` with local files.  F4 remote editing should initially use
an explicit temporary-copy workflow, with upload decisions deferred.

Navi8or deliberately does not adopt Far2l's dynamic plugin framework, broad
protocol matrix, tree implementation, full column configuration, or UI.
