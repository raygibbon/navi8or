# Navi8or

A keyboard-first, two-pane local/HTTP/SMB navigator for Linux and Windows.

Linux: run ./nav from this extracted directory. Requires x86-64 and a compatible
glibc runtime (Ubuntu 22.04 baseline). Windows: run nav.exe in an interactive
Windows Terminal/VT-capable console. No application dependency DLL installation
is needed. Keep themes/ beside the executable.

Use ./nav --version (Windows: nav.exe --version) to see the application version.
Two optional directory arguments set the left/right panes. Select a profile with
./nav -i themes/classic-dos.toml (Windows: nav.exe -i themes/classic-dos.toml).

Tab switches panes; arrows navigate; Enter opens; Backspace goes to parent.
F1 shows Help; F2 opens the current menu; F3 views text; Escape closes Viewer.
F5 copies / F6 moves or renames in Files; in Viewer they find next/previous match.
F7 creates directories / F8 deletes where supported. F10 quits from Files or
closes the active Viewer. Ctrl+L opens a URL/location or Download action.
Ctrl+N R refreshes. Menus expose fullscreen and explicit link actions.
Options → Preferences edits profiles, appearance, keys and proxy policy.
Two Viewers are independent; pane switching is ignored during fullscreen.

Recursive copy/deletion, SMB writes, remote editing and browser-cookie login are
not supported. HTTP mutations require opt-in repository capabilities.
TLS verification defaults on. Never put credentials into a URL.

Full current documentation and releases:
https://github.com/raygibbon/navi8or

Verify the download against the release SHA256SUMS. Navi8or is MIT licensed:
see LICENSE. Dependencies retain their own licenses: see THIRD_PARTY_NOTICES.md.
libsmb2 and its use are LGPL-2.1-or-later. The accompanying relinking kit on the
same release page supplies its source/changes and relinkable objects/instructions.
Keep that kit available when redistributing these static binaries.
