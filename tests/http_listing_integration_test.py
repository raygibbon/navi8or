"""Exercise listing streaming through actual curl callbacks, without whole HTML fixtures."""
import http.server
import subprocess
import sys
import threading
import os
import tempfile
from resize_test import spawn_nav, write, wait_for_exit, stop_nav
from terminal_screen import TerminalScreen


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    gets = 0
    heads = 0

    def log_message(self, *args):
        pass

    def do_GET(self):
        type(self).gets += 1
        self.close_connection = True
        mode = self.path.split("/")[1]
        if mode in ("redirect", "outside") and not self.path.endswith("sub/"):
            self.send_response(302)
            self.send_header("Location", "sub/" if mode == "redirect" else "/elsewhere/")
            self.send_header("Content-Length", "26")
            self.end_headers()
            self.wfile.write(b"<a href='redirect-body.txt'")
            return
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def emit(chunk):
            self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
            self.wfile.flush()

        try:
            if mode == "empty":
                pass
            elif mode == "panel":
                emit(b"<pre><a href='foo.txt'>foo.txt</a> 14-Sep-2026 18:45 123\r\n"
                     b"<a href='sub/'>sub/</a> 14-Sep-2026 18:45 -\r\n</pre>")
            elif mode == "large":
                # Over 50 MiB of HTML, but only 30,000 unique entries.
                for index in range(30000):
                    tag = (f"<a data-padding='{('x' * 900)}' href='file{index:05d}.txt'>f</a>"
                           " 14-Sep-2026 18:45 18446744073709551615\r\n").encode()
                    emit(tag + tag)
            elif mode == "long":
                emit(b"<a href='foo.txt'><a data='")
                for _ in range(20):
                    emit(b"x" * 1024)
            elif mode == "oom":
                for index in range(40):
                    emit(f"<a href='file{index}.txt'>".encode())
            else:
                for chunk in (b"<a hr", b"ef=\"foo", b".txt\">", b"<a href='sub/'>",
                              b"<a href='./foo.txt'>", b"<a href='/elsewhere/outside'>",
                              b"<a href='?q=x'>", b"<a href='unfinished"):
                    emit(chunk)
            self.wfile.write(b"0\r\n\r\n")
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_HEAD(self):
        type(self).heads += 1
        self.send_response(200)
        self.send_header("Content-Length", "123")
        if self.path.endswith("file0.txt"):
            self.send_header("Last-Modified", "Mon, 14 Sep 2026 18:45:00 GMT")
        elif self.path.endswith("file1.txt"):
            self.send_header("Last-Modified", "not a date")
        self.end_headers()


def panel_metadata(nav, url):
    with tempfile.TemporaryDirectory(prefix="nav-http-metadata-") as root:
        config = os.path.join(root, "nav")
        os.mkdir(config)
        with open(os.path.join(config, "nav.toml"), "w") as stream:
            stream.write('[menu]\nremember_position = false\n[panels]\nview = "full"\n')
        with open(os.path.join(config, "repositories.toml"), "w") as stream:
            stream.write(f'[[repositories]]\nname = "Metadata Test"\nurl = "{url}"\n')
        screen = TerminalScreen(180, 30)
        pid, fd = spawn_nav(os.path.abspath(nav), root, root, root, screen, columns=180)
        exited = False
        try:
            def send(data, delay=0.15):
                write(fd, data, delay, screen=screen)
            send(b"\x1c")
            for _ in range(3):
                send(b"\x1b[1;5C")
            send(b"\r")
            send(b"\r", 0.8)
            rows = [row for row in screen.text().splitlines() if "foo.txt" in row]
            assert rows and "123 B" in rows[0] and "2026-09-14 18:45" in rows[0], screen.text()
            send(b"\x1b[21~")
            wait_for_exit(pid, "HTTP panel metadata")
            exited = True
        finally:
            stop_nav(pid, fd, exited)
    print("HTTP Commander Full panel size/date: ok")


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for mode in ("empty", "small", "large", "long", "oom", "redirect", "outside"):
            url = f"http://127.0.0.1:{server.server_port}/{mode}/"
            subprocess.run([sys.argv[1], url, mode], check=True, timeout=30)
        assert Handler.heads == 0, "directory browsing issued per-entry HEAD requests"
        assert Handler.gets == 9, f"unexpected listing GET count: {Handler.gets}"
        if len(sys.argv) > 2:
            panel_metadata(sys.argv[2], f"http://127.0.0.1:{server.server_port}/panel/")
            assert Handler.heads == 0 and Handler.gets == 10
        subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}/stat/", "stat"],
                       check=True, timeout=30)
        assert Handler.heads == 3
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print("HTTP streamed listings (>50 MiB), redirects and callback failures: ok")


if __name__ == "__main__":
    main()
