"""Keyboard-first pane Viewer and explicit link actions through a real PTY."""
import http.server
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from resize_test import spawn_nav, write, resize, drain, wait_for_exit, stop_nav
from terminal_screen import TerminalScreen

F3 = b"\x1bOR"
FULL = b"\x1b[18~"
NEXT = b"\x1b[19~"
OPEN = b"\x1b[20~"
BACK = b"\x1b[23~"
PREV = b"\x1b[24~"
DOWN = b"\x1bOB"


class Handler(http.server.BaseHTTPRequestHandler):
    requests = []
    port = 0

    def log_message(self, *args):
        pass

    def body(self):
        if self.path.startswith("/binary"):
            return b"\0" * 8192
        if self.path.startswith("/second"):
            return b"SECOND RESOURCE\n"
        return (f"REMOTE RESOURCE\nhttp://127.0.0.1:{self.port}/second.html\n" +
                "".join(f"remote line {i:03d}\n" for i in range(100))).encode()

    def respond(self, head):
        type(self).requests.append((self.command, self.path, self.headers.get("Authorization")))
        body = self.body()
        start, end = 0, len(body) - 1
        if self.headers.get("Range"):
            start_text, end_text = self.headers["Range"][6:].split("-")
            start = int(start_text)
            end = min(int(end_text), end) if end_text else end
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(body)}")
        else:
            self.send_response(200)
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()
        if not head:
            self.wfile.write(body[start:end + 1])

    def do_GET(self):
        self.respond(False)

    def do_HEAD(self):
        self.respond(True)


def workflow(nav, root, port, pane):
    left, right = [os.path.join(root, name) for name in ("left", "right")]
    for path in (left, right):
        os.mkdir(path)
    origin = left if pane == 0 else right
    opposite = right if pane == 0 else left
    url = f"http://127.0.0.1:{port}/remote.bin?q=x&n=2"
    with open(os.path.join(origin, "000-origin.log"), "w") as stream:
        stream.write(f"Origin resource\nLog: ({url}).\nhttp://127.0.0.1:{port}/binary.dat\n"
                     "https://example.com/report.html\nBearer do-not-forward-secret\n")
        stream.writelines(f"origin line {i:03d}\n" for i in range(100))
    with open(os.path.join(opposite, "context-marker.txt"), "w") as stream:
        stream.write("opposite pane context\n")
    config = os.path.join(root, "nav")
    os.mkdir(config)
    with open(os.path.join(config, "nav.toml"), "w") as stream:
        stream.write('[network]\nproxy_mode = "none"\n[keys.viewer.commands]\n'
                     '"viewer.toggle_fullscreen" = "F7"\n"viewer.next_link" = "F8"\n'
                     '"viewer.open_link" = "F9"\n"viewer.back" = "F11"\n'
                     '"viewer.previous_link" = "F12"\n"resource.download" = "Ctrl+D"\n')
    screen = TerminalScreen(140, 30)
    pid, fd = spawn_nav(nav, left, right, root, screen, columns=140)
    exited = False
    try:
        def send(data, delay=0.16):
            write(fd, data, delay, screen=screen)

        def expect(text):
            assert text in screen.text(), screen.text()

        def geometry(columns, rows):
            nonlocal screen
            screen = TerminalScreen(columns, rows)
            resize(fd, columns, rows)
            time.sleep(0.15)
            screen.feed(drain(fd))

        if pane:
            send(b"\t")
        send(DOWN)
        before = screen.text()
        count = len(Handler.requests)
        send(F3)
        expect("Origin resource")
        expect("context-marker.txt")
        lines = screen.text().splitlines()
        divider = 70
        origin_rows = [row for row in lines if "Origin resource" in row]
        assert origin_rows and (origin_rows[0].find("Origin resource") < divider) == (pane == 0)
        send(DOWN)
        send(FULL)
        expect("Origin resource")
        assert "context-marker.txt" not in screen.text()
        assert "Ln 2/" in screen.text(), screen.text()
        geometry(12, 5); expect("Terminal")
        geometry(120, 25)
        expect("Origin resource")
        assert "Ln 2/" in screen.text()
        send(FULL)
        expect("context-marker.txt")
        geometry(12, 5); expect("Terminal")
        geometry(160, 32)
        expect("context-marker.txt")
        assert len(Handler.requests) == count, "fullscreen/resize reopened the resource"
        send(b"\x1b[6~")
        position = re.search(r"Ln \d+/\d+ Col \d+", screen.text()).group()
        top = screen.text().splitlines()[2][0 if pane == 0 else 81:][:20]
        send(FULL)
        assert position in screen.text()
        assert screen.text().splitlines()[2][:20] == top
        geometry(130, 28)
        assert position in screen.text()
        send(FULL)
        assert position in screen.text()
        geometry(160, 32)
        send(b"\x1bOH"); send(DOWN)
        # Next Link can select the URL on the current line without a mouse.
        send(NEXT)
        send(OPEN)
        for action in ("View", "Download", "Open in Browser", "Copy URL", "Cancel"):
            expect(action)
        send(b"\x1b")
        send(OPEN)
        for _ in range(3):
            send(DOWN)
        send(b"\r")
        expect("URL copied")
        with open(os.environ["NAV_CLIPBOARD_LOG"], encoding="utf-8") as stream:
            assert stream.read() == url
        send(OPEN)
        send(DOWN); send(DOWN); send(b"\r")
        expect("URL sent to default browser")
        with open(os.environ["NAV_BROWSER_LOG"], encoding="utf-8") as stream:
            assert stream.read() == url
        send(OPEN); send(b"\r", 0.4)
        expect("REMOTE RESOURCE")
        expect("context-marker.txt")
        send(b"\x04"); expect("Download / Save Copy"); expect(origin)
        send(b"\r\x01remote-copy.log\r\r", 0.4)
        expect("Download complete"); send(b"\x1b")
        with open(os.path.join(origin, "remote-copy.log"), "rb") as stream:
            assert stream.read().startswith(b"REMOTE RESOURCE")
        # Content type/extension deliberately say binary: explicit View wins.
        request_count = len(Handler.requests)
        send(FULL); send(FULL)
        assert len(Handler.requests) == request_count
        send(NEXT); send(OPEN); send(b"\r", 0.4)
        expect("SECOND RESOURCE")
        send(b"\t"); expect("context-marker.txt"); send(b"\t")
        expect("SECOND RESOURCE")
        send(BACK); expect("remote.bin"); expect("/second.html")
        send(BACK); expect("Origin resource")
        # Binary failure does not replace the current resource/history.
        send(NEXT); send(OPEN); send(b"\r", 0.4)
        expect("does not appear to be text")
        send(b"\x1b"); expect("Origin resource")
        send(PREV)
        send(OPEN); send(DOWN); send(b"\r")
        expect("Download / Save Copy")
        expect(origin)
        # Change filename and destination using the normal download form.
        send(b"\x01" + opposite.encode())
        send(b"\r")
        send(b"\x01copy.txt\r\r", 0.4)
        expect("Download complete")
        send(b"\x1b")
        with open(os.path.join(opposite, "copy.txt"), "rb") as stream:
            assert stream.read().startswith(b"REMOTE RESOURCE")
        # Current-resource Save Copy uses the same form/path, and cancellation
        # leaves both panes' state alone.
        send(b"\x04"); expect("Download / Save Copy"); expect(origin); send(b"\x1b")
        send(b"\x1b")
        expect("000-origin.log")
        assert "Origin resource" not in screen.text()
        expect("context-marker.txt")
        send(F3); expect("Origin resource"); send(b"\x1b")
        # No pane reload when a direct URL Viewer closes.
        send(b"\x0c")
        send(b"\x01" + url.encode() + b"\r", 0.4)
        expect("REMOTE RESOURCE"); expect("context-marker.txt")
        send(b"\x1b")
        expect("000-origin.log")
        send(b"\x1b[21~")
        wait_for_exit(pid, "pane Viewer workflow")
        exited = True
    finally:
        stop_nav(pid, fd, exited)


def main():
    nav, browser_test = map(os.path.abspath, sys.argv[1:])
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    Handler.port = server.server_port
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    saved_environment = os.environ.copy()
    try:
        with tempfile.TemporaryDirectory(prefix="nav-viewer-workflow-") as root:
            helpers = os.path.join(root, "helpers")
            os.mkdir(helpers)
            os.environ["PATH"] = helpers + os.pathsep + os.environ["PATH"]
            os.environ["WAYLAND_DISPLAY"] = "nav-test"
            os.environ["NAV_BROWSER_LOG"] = os.path.join(root, "browser.txt")
            os.environ["NAV_CLIPBOARD_LOG"] = os.path.join(root, "clipboard.txt")
            for name, contents in {
                "xdg-open": 'import os,sys\nassert len(sys.argv)==2\nopen(os.environ["NAV_BROWSER_LOG"],"w",encoding="utf-8").write(sys.argv[1])\n',
                "wl-copy": 'import os,sys\nopen(os.environ["NAV_CLIPBOARD_LOG"],"w",encoding="utf-8").write(sys.stdin.read())\n',
            }.items():
                path = os.path.join(helpers, name)
                with open(path, "w") as stream:
                    stream.write("#!/usr/bin/python3\n" + contents)
                os.chmod(path, 0o700)
            # Shell-looking characters remain one literal argv, never code.
            literal = "https://example.com/a?x=$(false)&y=2"
            subprocess.run([browser_test, literal], check=True)
            time.sleep(0.1)
            with open(os.environ["NAV_BROWSER_LOG"]) as stream:
                assert stream.read() == literal
            for pane in (0, 1):
                directory = os.path.join(root, str(pane)); os.mkdir(directory)
                workflow(nav, directory, server.server_port, pane)
    finally:
        os.environ.clear(); os.environ.update(saved_environment)
        server.shutdown(); server.server_close(); thread.join()
    assert all(auth is None for _, _, auth in Handler.requests)
    print("Pane Viewer, fullscreen/resize, configured links/actions/history, binary guard and shared transfers: ok")


if __name__ == "__main__":
    main()
