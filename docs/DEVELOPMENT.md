# Development and release

## Architecture

Providers own resource identities/operations, never UI state. NavPane owns
listing, selection, filter, sort, scroll, history and Files/Viewer content.
One main input loop selects active context and handles focus before dispatch.
Each Viewer owns source, origin, search, link, Back and menu state.
Only temporary menus/dialogs are modal. Transfers use capability flags and
bounded buffers; remote viewing uses range/disk caches, not whole-file memory.
See [Viewer](VIEWER.md).

src/terminal decodes events, src/input normalizes/resolves bindings, platform
backends isolate POSIX/Win32 operations. include/nav_commands.def centralizes
commands. Themes use semantic roles.

## Tests

Inside the [Ubuntu baseline](BUILDING.md):

```sh
make check
make location-test
make TARGET=windows windows-error-test
```

Includes local/provider, Full/Brief, HTTP listing/stream/auth/TLS, SMB paths,
profiles/keymap/Preferences, vault, clipboard, identity and two-Viewer tests.
Fixtures create isolated loopback servers/certificates/files and synthetic
credentials; never use production secrets or change default TLS policy.
make resize-test is an additional interactive regression script.
Do not commit caches/binaries.

Wine/UMU is compatibility smoke, not native Windows validation.
On Windows unpack in an interactive VT console with isolated APPDATA.
Record OS/console/binary hash; verify both views, UTF-8 names, navigation/parent,
Tab, F3/two Viewers/fullscreen, resize, close and F10 shutdown/cursor restoration.
Record failures/skips; process survival is not proof.

## Provenance

TDX/TDE informed menu/pull-downs, saved cell regions, frames/query fields,
Help/status movement, color defaults and modifier-aware key translation.
CP437 retains its TDX headers: Copyright (c) 2026 Ray Gibbon and new TDX file,
no TDE 5.1/7 ancestor. This attribution is intentional, not accidental personal
data. Navi8or owns current panes/providers/presentation.

Historical records explicitly identify Far2l as behavioral/architecture reference
only: no GPL source/comments/class layouts/implementation incorporated.
Reference/planning documents were pruned, not attribution.
The original project MIT license is restored from the initial commit unchanged.

## Release

VERSION is canonical. Complete tests and native Windows smoke before publishing.

```sh
make release
make release-check
git tag "v$(cat VERSION)"
git push upstream "v$(cat VERSION)"
```

Review and commit the clean source before tagging; tag must point to that commit.
Local packaging never publishes. GitHub runs only on explicit version tags,
verifies tag = vVERSION, tests/builds Ubuntu 22.04 and MinGW, then uploads
packages, checksums and the required LGPL relinking kit. GitHub supplies project
source archives; the kit is not a redundant project source release.
Cleanup does not rewrite history: earlier personal paths remain in Git history.
Use a dedicated secret scanner before public publication.

The kit accompanies every static libsmb2 binary release under LGPL-2.1 section
6(a)/equivalent-access terms. It supplies pinned library source, Windows changes,
application objects, required other archives, notices and tested relink commands.
Personal modification and reverse engineering for debugging those modifications
are permitted; MIT does not prohibit them. Full license terms govern.
See [Third-party notices](../THIRD_PARTY_NOTICES.md).
