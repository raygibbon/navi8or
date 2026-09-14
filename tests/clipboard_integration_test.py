"""Real terminal/field paste with deterministic desktop clipboard helper doubles."""
import os
from pathlib import Path
import sys
import tempfile
import time
from preferences_integration_test import Session, ENTER, ESC

CTRL_A, CTRL_C, CTRL_V, CTRL_X = b"\x01", b"\x03", b"\x16", b"\x18"

def wait_text(session, text, timeout=6):
    deadline = time.monotonic() + timeout
    while text not in session.text() and time.monotonic() < deadline: session.send(b"", .1)
    assert text in session.text(), session.text()

def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="nav-clipboard-") as temporary:
        root = Path(temporary); left, right, tools = root / "left", root / "right", root / "tools"
        for directory in (left, right, tools): directory.mkdir()
        fixture = root / "clipboard.txt"
        helper = '#!/usr/bin/python3\nimport os,sys\nfrom pathlib import Path\np=Path(os.environ["NAV_TEST_CLIPBOARD"])\n'
        (tools / "wl-paste").write_text(helper + 'sys.stdout.buffer.write(p.read_bytes())\n')
        (tools / "wl-copy").write_text(helper + 'p.write_bytes(sys.stdin.buffer.read())\n')
        for tool in tools.iterdir(): tool.chmod(0o755)
        env = dict(os.environ, TERM="xterm-256color", XDG_CONFIG_HOME=str(root / "config"),
                   WAYLAND_DISPLAY="test", DISPLAY="", PATH=str(tools) + ":" + os.environ["PATH"], NAV_TEST_CLIPBOARD=str(fixture))
        (left / "sample.txt").write_text("panel clipboard regression\n")
        profile = root / "profile.toml"; profile.write_text('[profile]\nname="Clipboard test"\n[keys]\ncopy="Ctrl+C"\n')
        session = Session(executable, left, right, env, profile)
        try:
            session.send(b"\x0c"); wait_text(session, "Enter URL / Location")
            url = "https://example.test/job/output.log?x=1&y=2"
            fixture.write_text(url); session.send(CTRL_A); session.send(CTRL_V); wait_text(session, url)
            session.send(CTRL_A); session.send(CTRL_C); assert fixture.read_text() == url
            session.send(CTRL_X); assert "Enter URL / Location" in session.text()
            session.send(CTRL_V); wait_text(session, url)
            unicode_url = "https://example.test/猫/é/😀.log"
            fixture.write_text(unicode_url); session.send(CTRL_A); session.send(CTRL_V); wait_text(session, unicode_url)
            session.send(CTRL_A); session.send(CTRL_C); assert fixture.read_text() == unicode_url
            fixture.write_text("ab😀cd"); session.send(CTRL_V)
            session.send(b"\x1bOH"); session.send(b"\x1bOC"); session.send(b"\x1bOC")
            fixture.write_text("猫"); session.send(CTRL_V); wait_text(session, "ab猫😀cd")
            session.send(CTRL_A); session.send(CTRL_C); assert fixture.read_text() == "ab猫😀cd"
            long_url = "https://example.test/" + "x" * 3900
            fixture.write_text(long_url); session.send(CTRL_V)
            session.send(CTRL_A); session.send(CTRL_C); assert fixture.read_text() == long_url
            fixture.write_text("x" * 5000); session.send(CTRL_V); wait_text(session, "exceeds this field")
            session.send(ESC); session.send(CTRL_C); assert fixture.read_text() == long_url
            session.send(b"\x1b[20", .05); session.send(b"0~", .05)
            payload = unicode_url.encode(); boundary = payload.index("猫".encode()) + 1
            session.send(payload[:boundary], .05); session.send(payload[boundary:], .05)
            # Pasted control/function keys never trigger Select All, Quit or Accept.
            session.send(b"\x03\r\x1b[21~", .05); session.send(b"\x1b[201~", .3)
            wait_text(session, unicode_url)
            session.send(CTRL_A); session.send(CTRL_C); assert fixture.read_text() == unicode_url
            session.send(b"\x1b[200~" + b"x" * 6000 + b"\x1b[201~", .2)
            wait_text(session, "Paste too large", 10); session.send(ESC)
            session.send(CTRL_C); assert fixture.read_text() == unicode_url
            # Terminal-native unbracketed UTF-8 insertion still works.
            session.send(CTRL_X); session.send(unicode_url.encode()); wait_text(session, unicode_url)
            fixture.write_text("Shift Insert"); session.send(CTRL_A); session.send(b"\x1b[2;2~"); wait_text(session, "Shift Insert")
            unicode_directory = root / "猫-é-😀"; unicode_directory.mkdir(); (unicode_directory / "works.txt").write_text("unicode navigation\n")
            fixture.write_text(str(unicode_directory)); session.send(CTRL_A); session.send(CTRL_V); session.send(ENTER, .4)
            wait_text(session, "works.txt")
            session.send(b"\x0c"); session.send(CTRL_A); fixture.write_text(str(left)); session.send(CTRL_V); session.send(ENTER)
            session.send(b"\x1bOB"); session.send(CTRL_C); session.send(ENTER, .3)
            assert (right / "sample.txt").read_bytes() == (left / "sample.txt").read_bytes()
            # Incoming bracketed paste outside a field cannot invoke file commands.
            session.send(b"\x1b[200~\x03\x11\x1b[21~\x1b[201~")
            session.quit()
        finally: session.close()
    print("Clipboard TUI: Ctrl+L paste/copy/cut/select-all, UTF-8, middle/selection, long/atomic bracketed paste, terminal paste and panel bindings passed")
if __name__ == "__main__": main()
