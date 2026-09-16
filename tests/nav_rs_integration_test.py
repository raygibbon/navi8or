#!/usr/bin/env python3
"""PTY smoke test for the experimental Rust two-pane core."""
import os
from pathlib import Path
import pty
import sys
import tempfile
import time

from resize_test import DOWN, drain, resize, stop_nav, wait_for_exit, write
from terminal_screen import TerminalScreen


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="nav-rs-") as temporary:
        root = Path(temporary)
        left = root / "left"
        right = root / "right"
        child = left / "child"
        child.mkdir(parents=True)
        right.mkdir()
        sample = left / "sample.txt"
        sample.write_text("legacy C Viewer handoff\n")
        (right / "other.txt").write_text("right pane\n")

        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm-256color"
            os.execv(executable, [executable, str(left), str(right)])

        exited = False
        try:
            resize(fd, 100, 30)
            time.sleep(0.25)
            screen = TerminalScreen(100, 30)
            screen.feed(drain(fd))
            initial = screen.text()
            for expected in ("Local Filesystem", "sample.txt", "other.txt", "F10", "Quit"):
                assert expected in initial, (expected, initial)

            left_title_style = screen.cells[1][1][1]
            right_title_style = screen.cells[1][52][1]
            assert left_title_style != right_title_style
            write(fd, b"\t", screen=screen)
            assert screen.cells[1][1][1] == right_title_style
            assert screen.cells[1][52][1] == left_title_style
            write(fd, b"\t", screen=screen)

            write(fd, DOWN, screen=screen)
            write(fd, b"\r", screen=screen)
            assert str(child) in screen.text(), screen.text()
            write(fd, b"\x7f", screen=screen)
            assert str(left) in screen.text(), screen.text()

            write(fd, DOWN, screen=screen)
            write(fd, DOWN, screen=screen)
            viewer_output = write(fd, b"\x1bOR", delay=0.4)
            viewer = TerminalScreen(100, 30)
            viewer.feed(viewer_output)
            assert "legacy C Viewer handoff" in viewer.text(), viewer.text()
            assert "Close" in viewer.text(), viewer.text()

            returned_output = write(fd, b"\x1b", delay=0.4)
            returned = TerminalScreen(100, 30)
            returned.feed(returned_output)
            assert "Local Filesystem" in returned.text(), returned.text()
            assert "Viewer closed" in returned.text(), returned.text()

            write(fd, b"\x1b[15~", delay=0.4, screen=returned)
            assert "Copy complete" in returned.text(), returned.text()
            assert (right / "sample.txt").read_bytes() == sample.read_bytes()
            assert returned.text().count("sample.txt") >= 2, returned.text()

            resize(fd, 60, 15)
            time.sleep(0.15)
            resized = TerminalScreen(60, 15)
            resized.feed(drain(fd))
            assert "Local Filesystem" in resized.text(), resized.text()

            write(fd, b"\x1b[21~")
            wait_for_exit(pid, "nav-rs F10")
            exited = True
        finally:
            stop_nav(pid, fd, exited)

    viewer_during_copy(executable)
    print("nav-rs navigation, C Viewer handoff, background copy, and exit: passed")


def viewer_during_copy(executable):
    with tempfile.TemporaryDirectory(prefix="nav-rs-viewer-copy-") as temporary:
        root = Path(temporary)
        left = root / "left"
        right = root / "right"
        left.mkdir()
        right.mkdir()
        source = left / "large.txt"
        source.write_bytes(b"viewer during background copy\n" * 200000)

        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm-256color"
            os.execv(executable, [executable, str(left), str(right)])

        exited = False
        try:
            resize(fd, 100, 30)
            time.sleep(0.2)
            screen = TerminalScreen(100, 30)
            screen.feed(drain(fd))
            write(fd, DOWN, screen=screen)
            viewer_output = write(fd, b"\x1b[15~\x1bOR", delay=0.5)
            viewer = TerminalScreen(100, 30)
            viewer.feed(viewer_output)
            assert "viewer during background copy" in viewer.text(), viewer.text()
            assert (right / "large.txt").read_bytes() == source.read_bytes()

            returned_output = write(fd, b"\x1b", delay=0.4)
            returned = TerminalScreen(100, 30)
            returned.feed(returned_output)
            assert "Copy complete" in returned.text(), returned.text()
            assert returned.text().count("large.txt") >= 2, returned.text()
            write(fd, b"\x1b[21~")
            wait_for_exit(pid, "nav-rs Viewer during copy")
            exited = True
        finally:
            stop_nav(pid, fd, exited)


if __name__ == "__main__":
    main()
