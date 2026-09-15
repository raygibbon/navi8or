#!/usr/bin/env python3
"""Canonical CLI identity and persistent/active menu branding across resize."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from preferences_integration_test import Session, ENTER, ESC, F2, F4
from resize_test import resize, drain, RIGHT, DOWN
from terminal_screen import TerminalScreen

def main():
    nav = str(Path(sys.argv[1]).resolve())
    project = Path(__file__).resolve().parent.parent
    source = (project / "build" / "generated" / "nav_version.h").read_text()
    name = re.search(r'^#define NAV_APP_NAME "([^"]+)"', source, re.M)[1]
    version = (project / "VERSION").read_text().strip()
    assert re.search(r'^#define NAV_VERSION "([^"]+)"', source, re.M)[1] == version
    assert re.fullmatch(r"\d+\.\d+\.\d+", version), version
    full = f"{name} {version}"
    with tempfile.TemporaryDirectory(prefix="nav-identity-") as temporary:
        root = Path(temporary); left, right = root / "left", root / "right"
        left.mkdir(); right.mkdir(); (left / "sample.txt").write_text("identity viewer\n")
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), TERM="xterm-256color")
        result = subprocess.run([nav, "--version"], env=environment, capture_output=True, check=True)
        assert result.stdout.decode() == full + "\n" and not result.stderr, result
        assert not (root / "config").exists(), "--version initialized runtime configuration"
        positions = {}
        for style in ("modern", "classic"):
            for visible in (True, False):
                profile = root / f"{style}-{visible}.toml"
                profile.write_text(f'[profile]\nname="Identity test"\n[ui]\nstyle="{style}"\n'
                                   f'[layout]\nshow_app_identity={str(visible).lower()}\n')
                session = Session(nav, left, right, environment, profile)
                modes = set()
                def check():
                    row = session.text().splitlines()[0]
                    for label in ("File", "View", "Command", "Repositories", "Options", "Help"):
                        assert label in row, (style, row)
                    labels = tuple(row.index(label) for label in ("File", "View", "Command", "Repositories", "Options", "Help"))
                    key = (style, len(row))
                    assert positions.setdefault(key, labels) == labels, "visibility moved menu headings"
                    end = row.index("Help") + 4
                    identity = full if len(row) - len(full) >= end + 2 else name if len(row) - len(name) >= end + 2 else ""
                    if not visible: identity = ""
                    modes.add(identity)
                    start = len(row) - len(identity) if identity else len(row)
                    assert row[end:start].strip() == "", (style, row) # also catches stale identity fragments
                    if identity:
                        assert row.endswith(identity), (style, identity, row)
                        assert session.screen.cells[0][start][1] == session.screen.cells[0][row.index("File")][1], "identity palette differs from inactive menu bar"
                    else: assert name not in row, row
                try:
                    check()
                    widths = [120, 90, 89, 88, 84, 83, 82, 70, 69, 68, 64, 63, 62, 55]
                    for width in widths + list(reversed(widths)) + [120]:
                        if session.screen.columns == width: check(); continue
                        resize(session.fd, width, 30); session.screen = TerminalScreen(width, 30)
                        time.sleep(.14); session.screen.feed(drain(session.fd)); check()
                    if visible: assert modes == {full, name, ""}, (style, modes)
                    # Active menu headings use the same renderer, not another selectable item.
                    session.send(b"\x1bOQ") # F2 menu
                    row = session.text().splitlines()[0]
                    assert row.endswith(full if visible else " " * len(full)), row
                    selected_style = session.screen.cells[0][row.index("File")][1]
                    if visible:
                        assert session.screen.cells[0][len(row) - len(full)][1] == session.screen.cells[0][row.index("View")][1], "identity became selected"
                    for _ in range(6): session.send(RIGHT, .08)
                    assert "File" in session.text().splitlines()[0], session.text()
                    assert session.screen.cells[0][row.index("File")][1] == selected_style, "identity changed menu cycling"
                    session.send(ESC)
                    session.send(DOWN); session.send(b"\x1bOR") # file Viewer
                    assert "identity viewer" in session.text(), session.text()
                    row = session.text().splitlines()[0]
                    assert row.endswith(full if visible else " " * len(full)), row
                    session.send(ESC)
                    session.quit()
                finally: session.close()
        # Every bundled profile loads and displays the same source identity.
        for filename in ("classic-dos", "solar-dark", "solar-light", "monochrome"):
            profile = Path(nav).parent / "themes" / f"{filename}.toml"
            session = Session(nav, left, right, environment, profile)
            try:
                assert session.text().splitlines()[0].endswith(full), session.text()
                session.quit()
            finally: session.close()
        # Preferences previews/cancels/applies/saves the same profile property.
        profile = root / "live.toml"
        profile.write_text('[profile]\nname="Live identity"\n[layout]\nshow_app_identity=true\n')
        session = Session(nav, left, right, environment, profile)
        def toggle():
            session.preferences(); session.row(4); session.send(ENTER); session.row(3); session.send(ENTER)
            session.row(3); session.send(ENTER)
            assert name not in session.text().splitlines()[0], session.text()
        try:
            toggle(); session.send(ESC); session.send(ESC); session.send(ESC); session.send(b"y")
            assert session.text().splitlines()[0].endswith(full), session.text()
            toggle(); session.send(F4)
            assert name not in session.text().splitlines()[0], session.text()
            session.preferences(); session.send(F2)
            assert 'show_app_identity = false' in profile.read_text(), profile.read_text()
            session.quit()
        finally: session.close()
        session = Session(nav, left, right, environment, profile)
        try:
            assert name not in session.text().splitlines()[0], session.text()
            session.quit()
        finally: session.close()
    print("Identity: canonical --version, profile visibility, both styles, right alignment, full/name/hidden and repeated resize: passed")

if __name__ == "__main__": main()
