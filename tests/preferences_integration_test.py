#!/usr/bin/env python3
"""Real TUI profile preview, cancel, capture, apply, save and manual reload."""
import os
from pathlib import Path
import pty
import sys
import tempfile
import time
from resize_test import resize, drain, write, wait_for_exit, stop_nav, DOWN, UP, END
from terminal_screen import TerminalScreen

ENTER = b"\r"
ESC = b"\x1b"
F1 = b"\x1bOP"
F2 = b"\x1bOQ"
F4 = b"\x1bOS"
CTRL_T = b"\x14"

class Session:
    def __init__(self, executable, left, right, environment, profile=None):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            args = [executable] + (["-i", str(profile)] if profile else []) + [str(left), str(right)]
            os.execve(executable, args, environment)
        self.exited = False
        self.screen = TerminalScreen(120, 30)
        resize(self.fd, 120, 30); time.sleep(.35); self.screen.feed(drain(self.fd))
    def send(self, key, delay=.2):
        write(self.fd, key, delay, screen=self.screen)
    def text(self): return self.screen.text()
    def preferences(self):
        self.send(CTRL_T); self.send(b"p")
        assert "Preferences" in self.text(), self.text()
    def row(self, n):
        for _ in range(n): self.send(DOWN, .1)
    def close(self): stop_nav(self.pid, self.fd, self.exited)
    def quit(self):
        self.send(b"\x11"); wait_for_exit(self.pid, "preferences test"); self.exited = True

def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="nav-prefs-tui-") as temporary:
        root = Path(temporary); left, right = root / "left", root / "right"
        left.mkdir(); right.mkdir(); (left / "sample.txt").write_text("profile transfer test\n")
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), TERM="xterm-256color")
        session = Session(executable, left, right, environment)
        try:
            before = session.screen.cells[5][3][1]
            session.preferences(); session.row(4); session.send(ENTER); session.row(2); session.send(ENTER) # Appearance / Colors
            session.send(END); session.send(UP); session.send(UP); session.send(UP) # File foreground
            assert "file foreground" in session.text(), session.text()
            session.send(ENTER)
            assert "Preferences *" in session.text(), session.text()
            assert session.screen.cells[5][3][1] != before, "file color did not preview"
            session.send(ESC); session.send(ESC); session.send(ESC); session.send(b"y")
            assert session.screen.cells[5][3][1] == before, "Cancel did not restore color"
            session.preferences(); session.row(5); session.send(ENTER) # Keys
            session.row(5); session.send(ENTER)
            assert "Press new key for Copy" in session.text(), session.text()
            session.send(b"\x03") # physical Ctrl+C
            assert "Copy updated" in session.text(), session.text()
            session.send(F4) # Apply closes editor
            assert "F5 Copy" not in session.text(), session.text()
            session.send(b"\x1c"); session.send(b"\x1bOC"); session.send(b"\x1bOC")
            assert "Ctrl+C" in session.text(), session.text()
            session.send(ESC)
            session.send(F1); session.send(END)
            # Help command list is dynamic and scrollable.
            session.send(b"\x1bOH")
            assert "Copy  Ctrl+C" in session.text(), session.text()
            session.send(ESC)
            session.send(DOWN); session.send(b"\x03"); session.send(ENTER, .3)
            assert (right / "sample.txt").read_bytes() == (left / "sample.txt").read_bytes()
            # Save the preview as a personal profile; shipped files are untouched.
            session.preferences(); session.send(F2)
            assert "Profile name:" in session.text(), session.text()
            # Default suggested name is Custom; accept it.
            session.send(ENTER, .4)
            profile = root / "config" / "nav" / "profiles" / "Custom.toml"
            assert profile.exists(), session.text()
            session.quit()
        finally: session.close()
        session = Session(executable, left, right, environment, profile)
        try:
            assert "F5 Copy" not in session.text(), session.text()
            text = profile.read_text()
            text = text.replace('copy = ["Ctrl+C"]', 'copy = ["F5"]')
            profile.write_text(text)
            session.send(CTRL_T); session.send(b"r", .3)
            assert "F5 Copy" in session.text(), session.text()
            session.quit()
        finally: session.close()
        # Conflict No preserves the map; Yes displaces the previous binding.
        session = Session(executable, left, right, environment)
        try:
            session.preferences(); session.row(5); session.send(ENTER); session.row(5); session.send(ENTER)
            session.send(b"\x1b[17~") # F6 is Move
            assert "assigned to Move" in session.text(), session.text()
            session.send(b"n")
            assert "Preferences *" not in session.text(), session.text()
            session.send(ENTER); session.send(b"\x1b[17~"); session.send(b"y")
            assert "Copy updated" in session.text(), session.text()
            session.send(F4)
            assert "F6 Copy" in session.text() and "F5 Copy" not in session.text(), session.text()
            session.send(CTRL_T); session.send(b"r")
            assert "Discard unsaved profile changes" in session.text(), session.text()
            session.send(b"n")
            assert "F6 Copy" in session.text(), session.text()
            session.send(b"\x11")
            assert "Discard unsaved profile changes and quit" in session.text(), session.text()
            session.send(b"n")
            session.send(CTRL_T); session.send(b"r"); session.send(b"y", .3)
            assert "F5 Copy" in session.text(), session.text()
            # Hiding hints does not disable any of the physical bindings.
            session.preferences(); session.row(4); session.send(ENTER); session.row(4); session.send(ENTER)
            for i in range(4):
                session.send(ENTER)
                if i != 3: session.send(DOWN)
            session.send(F4)
            assert "F5 Copy" not in session.text(), session.text()
            session.send(b"\x1c"); session.send(b"\x1bOC"); session.send(b"\x1bOC")
            assert "Copy" in session.text() and "F5" not in session.text(), session.text()
            session.send(ESC); session.send(F1)
            assert "Copy" in session.text() and "F5" not in session.text(), session.text()
            session.send(ESC); session.send(CTRL_T); session.send(b"r")
            assert "(y/n)" not in session.text(), session.text()
            session.send(b"y", .3)
            session.quit()
        finally: session.close()
    print("Preferences TUI: color preview/Cancel, capture/Apply, menu/Help/F-bar, Copy, Save/restart/manual reload: passed")

if __name__ == "__main__": main()
