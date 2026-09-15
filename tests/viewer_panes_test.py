"""One application loop, independent pane Viewers and contextual focus."""
import os
import re
import sys
import tempfile
import time
from resize_test import spawn_nav, write, resize, drain, wait_for_exit, stop_nav
from terminal_screen import TerminalScreen

F3 = b"\x1bOR"
FULL = b"\x1b[18~"
NEXT_LINK = b"\x1b[19~"
OPEN_LINK = b"\x1b[20~"
FIND_NEXT = b"\x1b[15~"
DOWN = b"\x1bOB"
ESC = b"\x1b"


def run(nav, remapped):
    with tempfile.TemporaryDirectory(prefix="nav-pane-viewers-") as root:
        left, right, config = [os.path.join(root, p) for p in ("left", "right", "nav")]
        for path in (left, right, config):
            os.mkdir(path)
        for directory, name in ((left, "LEFT"), (right, "RIGHT")):
            with open(os.path.join(directory, name + ".txt"), "w") as stream:
                for line in range(100):
                    stream.write(f"{name} LINE {line:03d}" +
                                 (f" MATCH {name}" if line in (10, 60, 90) else "") +
                                 (" https://example.com/resource" if line == 0 else "") + "\n")
        with open(os.path.join(config, "nav.toml"), "w") as stream:
            stream.write('[keys.viewer.commands]\n"viewer.toggle_fullscreen"="F7"\n'
                         '"viewer.next_link"="F8"\n"viewer.open_link"="F9"\n')
            stream.write('[keys.global]\n"F12"="app.quit"\n')
            if remapped:
                stream.write('[keys]\npane_switch="Ctrl+P S"\n')
        switch = b"\x10s" if remapped else b"\t"
        screen = TerminalScreen(160, 32)
        pid, fd = spawn_nav(nav, left, right, root, screen, columns=160, rows=32)
        exited = False
        try:
            def send(data, delay=0.15):
                write(fd, data, delay, screen=screen)

            def expect(text):
                assert text in screen.text(), screen.text()

            def pane_text(index):
                x = 0 if index == 0 else screen.columns // 2 + 1
                end = screen.columns // 2 if index == 0 else screen.columns
                return "\n".join(row[x:end] for row in screen.text().splitlines())

            def position(index):
                match = re.search(r"Ln (\d+)/100", pane_text(index))
                assert match, screen.text()
                return int(match.group(1))

            def geometry(columns, rows):
                nonlocal screen
                screen = TerminalScreen(columns, rows)
                resize(fd, columns, rows); time.sleep(0.15); screen.feed(drain(fd))

            def search(name):
                send(b"\x13" + f"MATCH {name}".encode() + b"\r")

            baseline_fds = len(os.listdir(f"/proc/{pid}/fd"))
            send(DOWN); send(F3)
            expect("LEFT LINE 000"); expect("RIGHT.txt")
            assert "Search" in screen.text().splitlines()[0]
            assert "F5 Next" in screen.text().splitlines()[-1]
            send(NEXT_LINK)
            send(switch)
            assert "Repositories" in screen.text().splitlines()[0], screen.text()
            assert "F5 Copy" in screen.text().splitlines()[-1]
            expect("LEFT LINE 000")
            send(DOWN)  # Files context, not Viewer movement.
            send(switch)
            assert position(0) == 1
            send(OPEN_LINK)
            expect("Open Link"); expect("Open in Browser"); send(ESC)
            search("LEFT"); assert position(0) == 11
            send(switch); send(F3)
            expect("RIGHT LINE 000")
            assert position(0) == 11 and position(1) == 1
            search("RIGHT"); assert position(1) == 11
            send(switch); send(FIND_NEXT)
            assert position(0) == 61 and position(1) == 11
            send(switch); send(FIND_NEXT)
            assert position(0) == 61 and position(1) == 61
            # Remapped switch replaces Tab, including while Viewer is active.
            if remapped:
                send(b"\t"); send(DOWN)
                assert position(0) == 61 and position(1) == 62
                send(b"\x1bOA")
            for active in (1, 0):
                if active == 0:
                    send(switch)
                before = position(active)
                send(FULL)
                other = "LEFT LINE" if active == 1 else "RIGHT LINE"
                assert other not in screen.text(), screen.text()
                send(switch)  # Invisible panes cannot acquire focus.
                send(DOWN)
                assert re.search(fr"Ln {before + 1}/100", screen.text()), screen.text()
                send(FULL)
                assert position(active) == before + 1
                assert position(1 - active) == (61 if active == 1 else 62)
                geometry(60, 15); expect("LEFT.txt"); expect("RIGHT.txt")
                geometry(160, 32)
                assert position(active) == before + 1
            send(ESC)  # Close Left only; Right Viewer remains.
            expect("LEFT.txt"); assert "Ln" not in pane_text(0)
            assert position(1) == 62
            send(switch); send(ESC)
            expect("LEFT.txt"); expect("RIGHT.txt")
            assert "Ln" not in pane_text(0) and "Ln" not in pane_text(1)
            # Repeated lifetime cycles, each including two live sources.
            for _ in range(3):
                send(F3); send(switch); send(F3)
                assert position(0) == 1 and position(1) == 1
                send(ESC); send(switch); send(ESC)
                assert len(os.listdir(f"/proc/{pid}/fd")) == baseline_fds
            # Leaving both live on Quit exercises application teardown.
            send(F3); send(switch); send(F3)
            send(b"\x1b[24~")  # Application Quit cleans both live Viewers.
            wait_for_exit(pid, "independent pane Viewers"); exited = True
        finally:
            stop_nav(pid, fd, exited)


if __name__ == "__main__":
    for remapped in (False, True):
        run(os.path.abspath(sys.argv[1]), remapped)
    print("Independent pane Viewers: focus/context, search/link state, two sources, fullscreen/resize, close/teardown and remapped switch: passed")
