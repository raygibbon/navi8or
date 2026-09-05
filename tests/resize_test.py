#!/usr/bin/env python3
"""PTY resize smoke test for persistent and transient NAV controls."""
import fcntl
import os
import pty
import signal
import struct
import sys
import tempfile
import time
import termios

GEOMETRIES = [(120, 40), (80, 25), (60, 15), (140, 45), (40, 10),
              (12, 5), (100, 30)]

def resize(fd, columns, rows):
    fcntl.ioctl(fd, termios.TIOCSWINSZ,
                struct.pack("HHHH", rows, columns, 0, 0))

def write(fd, data, delay=0.12):
    os.write(fd, data)
    time.sleep(delay)
    drain(fd)

def drain(fd):
    os.set_blocking(fd, False)
    try:
        while True:
            if not os.read(fd, 65536):
                break
    except (BlockingIOError, OSError):
        pass
    finally:
        os.set_blocking(fd, True)

def alive(pid):
    child, status = os.waitpid(pid, os.WNOHANG)
    if child:
        raise RuntimeError(
            f"NAV exited during resize: {os.waitstatus_to_exitcode(status)}")

def resize_series(pid, fd):
    for columns, rows in GEOMETRIES:
        resize(fd, columns, rows)
        time.sleep(0.05)
        drain(fd)
        alive(pid)

def run_state(executable, left, right, name, enter, leave=b"\x1b",
              last_enter_delay=0.12):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(executable, [executable, left, right])
    exited = False
    try:
        resize(fd, 100, 30)
        time.sleep(0.2)
        for index, keys in enumerate(enter):
            delay = last_enter_delay if index == len(enter) - 1 else 0.12
            write(fd, keys, delay)
        resize_series(pid, fd)
        if leave:
            if isinstance(leave, list):
                for keys in leave:
                    write(fd, keys)
            else:
                write(fd, leave)
        write(fd, b"\x1b[21~")
        deadline = time.time() + 4
        while time.time() < deadline:
            child, status = os.waitpid(pid, os.WNOHANG)
            if child:
                exited = True
                if os.waitstatus_to_exitcode(status) != 0:
                    raise RuntimeError(f"{name}: NAV returned an error")
                print(f"resize {name}: ok")
                return
            time.sleep(0.05)
        raise RuntimeError(f"{name}: NAV did not exit")
    finally:
        if not exited:
            try:
                os.kill(pid, signal.SIGTERM)
                os.waitpid(pid, 0)
            except ProcessLookupError:
                pass
        os.close(fd)

def main(executable):
    with tempfile.TemporaryDirectory(prefix="nav-resize-") as root:
        left, right = os.path.join(root, "left"), os.path.join(root, "right")
        os.mkdir(left)
        os.mkdir(right)
        with open(os.path.join(left, "sample.txt"), "w", encoding="utf-8") as stream:
            stream.write("alpha\ntimeout\nomega\n")
        with open(os.path.join(left, "large.bin"), "wb") as stream:
            stream.truncate(64 * 1024 * 1024)

        run_state(executable, left, right, "commander", [], leave=b"")
        run_state(executable, left, right, "menu",
                  [b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"\x1b[1;5D"],
                  leave=[b"\x1c", b"\x1b[1;5C", b"\x1b"])
        run_state(executable, left, right, "menu accelerator",
                  [b"\x1c", b"v"], leave=b"")
        run_state(executable, left, right, "menu enter",
                  [b"\x1c", b"\r"], leave=b"")
        run_state(executable, left, right, "help",
                  [b"\x1bOP", b"\x1b[6~", b"\x1b[F", b"\x1b[H"])
        run_state(executable, left, right, "filter",
                  [b"/", b"no-match", b"\x1b[D", b"\x1b[D", b"X"],
                  leave=[b"\x1b[D", b"\x1b[3~", b"\x1b[H", b"\x1b[F", b"\x1b"])
        run_state(executable, left, right, "mkdir", [b"\x1b[18~"])
        run_state(executable, left, right, "copy progress",
                  [b"\x1b[B", b"\x1b[15~", b"\r"], leave=b"",
                  last_enter_delay=0)
        run_state(executable, left, right, "move", [b"\x1b[B", b"\x1b[17~"])
        run_state(executable, left, right, "delete", [b"\x1b[B", b"\x1b[19~"])
        run_state(executable, left, right, "viewer",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR"])
        run_state(executable, left, right, "find",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR", b"/"] ,
                  leave=[b"\x1b", b"\x1b"])
    return 0

if __name__ == "__main__":
    raise SystemExit(main(os.path.abspath(sys.argv[1])))
