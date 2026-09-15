#!/usr/bin/env python3
"""CLI selection and real command dispatch through a Linux PTY."""
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import time
from resize_test import resize, drain, write, alive, wait_for_exit, stop_nav, DOWN
from terminal_screen import TerminalScreen


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="nav-input-") as temporary:
        root = Path(temporary)
        left, right = root / "left", root / "right"
        left.mkdir(); right.mkdir()
        (left / "sample.txt").write_text("command-driven viewer\n")
        config = root / "custom.toml"
        config.write_text('[keys.global]\n"F12" = "app.quit"\n[keys.panel]\n"F5" = "file.view"\n[keys.viewer]\n"F5" = "search.previous"\n"F6" = "search.next"\n')
        automatic = root / "automatic"
        environment = dict(os.environ, XDG_CONFIG_HOME=str(automatic), TERM="xterm-256color")
        malformed = root / "malformed.toml"
        malformed.write_text("[profile\n")
        for args in (["-i"], ["-i", "missing.toml"], ["-i", str(malformed)], ["--unknown"]):
            result = subprocess.run([executable, *args], cwd=root, env=environment,
                                    capture_output=True, timeout=5)
            assert result.returncode == 2, (args, result.stderr)
            if len(args) == 2:
                assert args[1].encode() in result.stderr, result.stderr
        assert (automatic / "nav" / "nav.toml").exists(), "Normal config layer was not discovered"
        # Both TDX option spellings select the same file; paths resolve from cwd.
        for options in (["-i", "custom.toml"], ["-icustom.toml"], ["-i", str(config)]):
            pid, fd = pty.fork()
            if pid == 0:
                os.chdir(root)
                os.execve(executable, [executable, *options, str(left), str(right)], environment)
            exited = False
            screen = TerminalScreen(120, 30)
            try:
                resize(fd, 120, 30); time.sleep(0.3); screen.feed(drain(fd))
                assert "F5 View" in screen.text(), screen.text()
                write(fd, DOWN, screen=screen)
                write(fd, b"\x1b[15~", screen=screen)  # Remapped F5 must open viewer, not copy.
                assert "command-driven viewer" in screen.text(), screen.text()
                assert "File" in screen.text().splitlines()[0] and "Search" in screen.text().splitlines()[0]
                assert "sample.txt" in screen.text().splitlines()[1]
                assert "F10 Close" in screen.text(), screen.text()
                assert "F5 Prev" in screen.text() and "F6 Next" in screen.text(), screen.text()
                write(fd, b"\x1b[21~", screen=screen)
                alive(pid)
                write(fd, b"/", screen=screen)
                write(fd, b"\x11", screen=screen)  # Ctrl+Q cancels the prompt, not the app.
                alive(pid)
                write(fd, b"\x06", screen=screen)  # Generic Ctrl+F prefix.
                assert "Escape cancels" in screen.text(), screen.text()
                write(fd, b"\x1b", screen=screen)
                write(fd, b"\x1c", screen=screen)
                write(fd, b"\x1b[1;5C", screen=screen)
                write(fd, b"\x1b[1;5C", screen=screen)
                assert "Ctrl+F C" in screen.text(), screen.text()
                write(fd, b"\x1b", screen=screen)
                if not (right / "sample.txt").exists():
                    write(fd, b"\x06c", screen=screen)
                    write(fd, b"\r", delay=0.4, screen=screen)
                    assert (right / "sample.txt").read_bytes() == (left / "sample.txt").read_bytes()
                if options[0] == "-icustom.toml":
                    write(fd, b"\x1b[15~", screen=screen)
                    assert "command-driven viewer" in screen.text()
                    write(fd, b"\x1b[24~")  # Global app.quit from viewer workspace.
                else:
                    write(fd, b"\x11")
                wait_for_exit(pid, "configured input")
                exited = True
            finally:
                stop_nav(pid, fd, exited)
        assert (automatic / "nav" / "nav.toml").exists()
        config.write_text('[keys.panel]\n"F5" = "unknown.command"\n')
        result = subprocess.run([executable, "-i", str(config)], env=environment,
                                capture_output=True, timeout=5)
        assert result.returncode == 2 and b"invalid binding" in result.stderr
        # Exact palette paths also work through CLI, with profile chrome applied.
        config.write_text('[profile]\nname="Sparse UI"\n[layout]\nshow_menu=false\nshow_status=false\nshow_function_bar=false\n[keys]\nquit="Ctrl+Q"\n')
        pid, fd = pty.fork()
        if pid == 0:
            os.execve(executable, [executable, "-i", str(config), str(left), str(right)], environment)
        exited = False
        try:
            resize(fd, 120, 30); time.sleep(0.3)
            screen = TerminalScreen(120, 30); screen.feed(drain(fd))
            assert "F5 Copy" not in screen.text(), screen.text()
            assert "Command" not in screen.text().splitlines()[0], screen.text()
            write(fd, b"\x11")
            wait_for_exit(pid, "sparse profile quit"); exited = True
        finally:
            stop_nav(pid, fd, exited)
    print("CLI -i, configurable dispatch, modal cancellation, prefix copy, and menu labels: passed")


if __name__ == "__main__":
    main()
