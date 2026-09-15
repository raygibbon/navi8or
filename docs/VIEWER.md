# Pane Viewer and text links

F3/View and direct URL Open use the same read-only Viewer, inside the active
pane by default. Each pane independently contains Files or its own Viewer
session. Panel Switch (Tab by default) changes focus without closing either
Viewer; the opposite pane remains fully interactive when active. Both panes can
contain Viewers, with independent resource, search, scrolling, link and Back
history state. Closing Viewer restores only that pane's existing file listing,
selection, scrolling, location, sort, filter and pane navigation history.

View → Toggle Fullscreen changes the drawing rectangle without reopening the
source. Toggle again to return to the originating pane. Resize redraws both
modes. Small panes prioritize position/action feedback over shortcut hints.
Fullscreen belongs to the active pane's existing Viewer. Panel Switch is
ignored while fullscreen, preventing focus moving to invisible content.
Exiting fullscreen retains the active pane and both sessions.

## Ownership and event routing

`NavPane` owns `content_mode` and an opaque heap-allocated `NavPaneViewer` session.
The one main application loop selects `NAV_CONTEXT_PANEL` or
`NAV_CONTEXT_VIEWER` from the active pane before reading each event. Focus
commands route at application level before content dispatch; Viewer never
changes Commander focus. Each pane is rendered independently, with inactive
Viewer cursor/highlight styling and active-context menus/status/F-key labels.
Dialogs and menus still have their usual temporary modal controls.

Viewer sources, transient providers, origin snapshots, history and menu
positions are owned per session. Opening a transient direct URL transfers
provider ownership to that session only on success. Close and application Quit
release each session before pane providers and the vault are destroyed. No
mutable global Viewer session remains; only immutable menu definitions are
shared. Redrawing no longer snapshots/restores both Commander panes.

`panel.switch` defaults to global Tab and Ctrl+P S. The existing `[keys]`
`pane_switch` setting can replace those keys for both Files and Viewer; e.g.
`pane_switch = "Ctrl+P S"`. Context-specific bindings remain available and
override globals as usual. Viewer-specific commands affect only the active
Viewer. No raw Tab key checks are used.

View → Next Link / Previous Link selects and highlights HTTP/HTTPS URLs in
text; File → Open Link offers View, Download, Open in Browser, Copy URL and
Cancel. Open Link also recognizes a URL at the current horizontal column.
Detection uses text boundaries and trims prose punctuation/unmatched closing
brackets; it does not parse HTML, MIME types or extension policies. URLs are
limited to Navi8or's existing resource URL capacity. Link scans are lazy and
do not create an index of the whole file; scanning a long remote resource can
take time, and there is no separate cancellation UI for link navigation.

View replaces the current resource in the same Viewer session. File → Back
restores previous resources, including their position, selected link, wrap,
line-number and search state. Up to 16 previous resources are retained; older
ones are released. This history is separate from pane navigation history.

File → Download / Save Copy and link Download share the direct-location
download form and staged transfer implementation. Destination defaults to the
unchanged originating pane, even after following links. Destination and
filename are editable; a remote/read-only origin may require a local writable
destination. Saved files appear in pane listings after an explicit refresh.

View and Download reuse direct URL resolution: the longest matching saved
HTTP repository supplies TLS/authentication settings and unlocked vault
credentials. Locked credentials require unlocking the vault in Commander.
The normal live System/No Proxy configuration applies; Viewer has no separate
network policy. Sources use the existing bounded range cache or disk-backed
streaming cache, not whole-resource memory buffering. Binary/NUL content is
rejected safely; initial binary detection offers Download instead. Binary data
encountered later stops that line with an explanatory Viewer error.

Open in Browser launches `xdg-open` on Linux or the native Windows shell using
only the URL, without a command shell or Navi8or auth headers/vault secrets.
Embedded URL credentials are rejected. Copy URL uses the shared UTF-8
clipboard API (including its existing headless fallback).

## Commands and shortcuts

Fullscreen and link commands have no default shortcuts. They are available through menus and
the Preferences Key Bindings Viewer category; assign keys using the existing
profile schema. For example (optional bindings, not bundled defaults):

```toml
[keys.viewer.commands]
"viewer.toggle_fullscreen" = "F7"
"viewer.next_link" = "F8"
"viewer.open_link" = "F9"
"viewer.back" = "F11"
"viewer.previous_link" = "F12"
"resource.download" = "Ctrl+D"
```

Menus, Help and visible shortcut hints derive labels from the active keymap.
Existing Viewer Close shortcuts remain unchanged; Back is a distinct action.
