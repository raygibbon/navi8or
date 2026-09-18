#!/usr/bin/env python3
"""PTY smoke test for responsive Rust HTTP listing and download."""
import http.server
import os
from pathlib import Path
import pty
import sys
import tempfile
import threading
import time

from resize_test import DOWN, drain, resize, stop_nav, wait_for_exit, write
from terminal_screen import TerminalScreen


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/repo/sub/":
            time.sleep(0.8)
            body = b"<html><a href='../'>../</a><a href='nested.txt'>nested.txt</a></html>"
        elif self.path == "/repo/":
            body = b"<html><a href='bad/'>bad/</a><a href='sub/'>sub/</a></html>"
        elif self.path == "/repo/sub/nested.txt":
            body = b"REMOTE COPY CONTENT\n"
        else:
            self.send_error(404)
            return
        try:
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_HEAD(self):
        if self.path != "/repo/sub/nested.txt":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", "20")
        self.end_headers()


def until(fd, screen, expected, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        screen.feed(drain(fd))
        if expected in screen.text():
            return
        time.sleep(0.04)
    raise AssertionError((expected, screen.text()))


def main():
    executable = str(Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="nav-rs-http-") as temporary:
            destination = Path(temporary)
            url = f"http://127.0.0.1:{server.server_port}/repo/"
            pid, fd = pty.fork()
            if pid == 0:
                os.environ["TERM"] = "xterm-256color"
                os.environ["NO_PROXY"] = "127.0.0.1"
                os.execv(executable, [executable, url, str(destination)])
            exited = False
            try:
                resize(fd, 100, 30)
                screen = TerminalScreen(100, 30)
                until(fd, screen, "sub/")
                write(fd, DOWN, screen=screen)
                write(fd, DOWN, screen=screen)
                write(fd, b"\r", screen=screen)
                assert "Loading directory" in screen.text(), screen.text()
                write(fd, b"\t", screen=screen)
                resize(fd, 90, 25)
                screen = TerminalScreen(90, 25)
                until(fd, screen, "Local Filesystem")
                assert screen.text().splitlines()[2].split("│")[1].strip() == "/", screen.text()
                until(fd, screen, "nested.txt")
                write(fd, b"\t", screen=screen)
                write(fd, DOWN, screen=screen)
                write(fd, b"\x1b[15~", delay=0.35, screen=screen)
                assert (destination / "nested.txt").read_bytes() == b"REMOTE COPY CONTENT\n"

                # Return to root and cancel another slow listing. Its late
                # result must not replace the authoritative root location.
                write(fd, b"\x7f", delay=0.25, screen=screen)
                until(fd, screen, "sub/")
                write(fd, DOWN, screen=screen)
                write(fd, DOWN, screen=screen)
                write(fd, b"\r", screen=screen)
                write(fd, b"\x1b", screen=screen)
                time.sleep(1.0)
                screen.feed(drain(fd))
                assert screen.text().splitlines()[2].split("│")[1].strip() == "/", screen.text()
                # A newer refresh must win over a delayed navigation result.
                write(fd, b"\r", screen=screen)
                write(fd, b"\x0er", screen=screen)  # Ctrl+N, then R
                time.sleep(1.0)
                screen.feed(drain(fd))
                assert screen.text().splitlines()[2].split("│")[1].strip() == "/", screen.text()
                write(fd, b"\x1b[A", screen=screen)  # Select bad/
                write(fd, b"\r", delay=0.2, screen=screen)
                until(fd, screen, "Directory load failed")
                assert screen.text().splitlines()[2].split("│")[1].strip() == "/", screen.text()
                write(fd, b"\x1b[21~")
                wait_for_exit(pid, "nav-rs HTTP F10")
                exited = True
            finally:
                stop_nav(pid, fd, exited)
    finally:
        server.shutdown()
        server.server_close()
    print("nav-rs HTTP navigation, responsive UI, cancellation and download: passed")


if __name__ == "__main__":
    main()
