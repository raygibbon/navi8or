#!/usr/bin/env python3
"""Deterministic anonymous HTTP/HTTPS directory-listing integration test."""
import http.server
import hashlib
import base64
import errno
import fcntl
import os
import pty
import select
import struct
import ssl
import subprocess
import sys
import tempfile
import threading
import termios
import time
import urllib.parse
from terminal_screen import TerminalScreen


PTY_SCREENS = {}


class DirectoryHandler(http.server.BaseHTTPRequestHandler):
    requests = []
    fixtures = {}
    uploads = {}
    directories = set()
    upload_records = []
    expected_authorization = None
    root_redirect = None
    protocol_version = "HTTP/1.1"

    @staticmethod
    def payload(path):
        uploaded = DirectoryHandler.uploads.get(path)
        if uploaded is not None:
            return uploaded["body"]
        if path in DirectoryHandler.fixtures:
            return DirectoryHandler.fixtures[path]
        if path == "/root/fallback-small.txt":
            return b"small fallback text\nsecond line\n"
        if path == "/root/fallback-large.txt":
            return b"x" * (8 * 1024 * 1024)
        if path == "/root/binary.bin":
            return bytes((index * 31) & 255 for index in range(5 * 1024 * 1024))
        if path == "/root/unknown.bin":
            return bytes((index * 17) & 255 for index in range(384 * 1024))
        if path == "/root/interrupted.bin":
            return bytes((index * 13) & 255 for index in range(512 * 1024))
        return None

    def record(self, served=0, status=None):
        authorization = self.headers.get("Authorization")
        type(self).requests.append({
            "method": self.command,
            "path": self.path,
            "range": self.headers.get("Range"),
            "served": served,
            "status": status,
            "destination": self.headers.get("Destination"),
            # Retain proof of isolation without retaining a reusable secret.
            "authorization_sha256": hashlib.sha256(
                authorization.encode()).hexdigest() if authorization else None,
        })

    def authorize(self):
        expected = type(self).expected_authorization
        if expected is None or self.headers.get("Authorization") == expected:
            return True
        self.record(status=401)
        self.send_response(401)
        self.send_header("WWW-Authenticate", "Basic realm=\"Navi8or test\"")
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def do_HEAD(self):
        if not self.authorize():
            return
        if self.path == "/upload/":
            self.record()
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path in DirectoryHandler.uploads:
            uploaded = DirectoryHandler.uploads[self.path]
            self.record()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(uploaded["size"]))
            self.end_headers()
            return
        payload = self.payload(self.path)
        self.record()
        if payload is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        if self.path != "/root/unknown.bin":
            self.send_header("Content-Length", str(len(payload)))
        self.end_headers()

    def do_GET(self):
        if not self.authorize():
            return
        if self.path == "/root/" and type(self).root_redirect:
            self.record(status=302)
            self.send_response(302)
            self.send_header("Location", type(self).root_redirect)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/upload/":
            links = []
            for path in sorted(DirectoryHandler.directories):
                leaf = path[len("/upload/"):]
                if "/" not in leaf.rstrip("/"):
                    links.append(f"<a href='{leaf}'>{leaf}</a>")
            for path in sorted(DirectoryHandler.uploads):
                leaf = path[len("/upload/"):]
                if "/" not in leaf:
                    links.append(f"<a href='{leaf}'>{leaf}</a>")
            body = ("<html>" + "".join(links) + "</html>").encode()
        elif self.path == "/root/":
            body = (b"<html><a href='../'>../</a><a href='sub/'>sub/</a>"
                    b"<a href='space%20name.txt'>space name.txt</a>"
                    b"<a href='large.txt'>large.txt</a>"
                    b"<a href='fallback-small.txt'>fallback-small.txt</a>"
                    b"<a href='fallback-large.txt'>fallback-large.txt</a>"
                    b"<a href='binary.bin'>binary.bin</a>"
                    b"<a href='unknown.bin'>unknown.bin</a>"
                    b"<a href='interrupted.bin'>interrupted.bin</a>"
                    b"<a href='long-line.txt'>long-line.txt</a>"
                    b"<a href='aligned.txt'>aligned.txt</a>"
                    b"<a href='partial.txt'>partial.txt</a>"
                    b"<a href='utf8-boundary.txt'>utf8-boundary.txt</a>"
                    b"<a href='line-limit-minus.txt'>line-limit-minus.txt</a>"
                    b"<a href='line-limit.txt'>line-limit.txt</a>"
                    b"<a href='line-limit-plus.txt'>line-limit-plus.txt</a>"
                    b"<a href='?C=N;O=D'>sort</a>"
                    b"<a href='../../escape/'>escape</a>"
                    b"<a href='https://evil.example/'>external</a></html>")
        elif self.path == "/root/sub/":
            body = b"<html><a href='../'>../</a><a href='nested.txt'>nested.txt</a></html>"
        elif self.path in ("/root/space%20name.txt", "/root/sub/nested.txt"):
            body = b"FILE BODY MUST NOT BE FETCHED"
        else:
            body = self.payload(self.path)
            if body is None:
                self.record()
                self.send_error(404)
                return
            requested_range = self.headers.get("Range")
            supports_range = self.path not in (
                "/root/fallback-small.txt", "/root/fallback-large.txt",
                "/root/unknown.bin", "/root/interrupted.bin")
            if requested_range and supports_range:
                units, limits = requested_range.split("=", 1)
                start_text, end_text = limits.split("-", 1)
                if units != "bytes":
                    self.record()
                    self.send_error(416)
                    return
                start = int(start_text)
                if start >= len(body):
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{len(body)}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    self.record()
                    return
                end = min(int(end_text), len(body) - 1)
                chunk = body[start:end + 1]
                self.send_response(206)
                self.send_header("Content-Range",
                                 f"bytes {start}-{end}/{len(body)}")
                self.send_header("Content-Length", str(len(chunk)))
                self.end_headers()
                self.wfile.write(chunk)
                self.record(len(chunk))
                return
            if self.path == "/root/interrupted.bin":
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                partial = body[:len(body) // 2]
                self.wfile.write(partial)
                self.wfile.flush()
                self.record(len(partial))
                self.connection.shutdown(1)
                self.connection.close()
                return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        if self.path == "/root/unknown.bin":
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        served = 0
        try:
            for offset in range(0, len(body), 16384):
                chunk = body[offset:offset + 16384]
                if self.path == "/root/unknown.bin":
                    self.wfile.write(f"{len(chunk):x}\r\n".encode())
                    self.wfile.write(chunk)
                    self.wfile.write(b"\r\n")
                else:
                    self.wfile.write(chunk)
                served += len(chunk)
            if self.path == "/root/unknown.bin":
                self.wfile.write(b"0\r\n\r\n")
        except (BrokenPipeError, ConnectionResetError):
            pass
        self.record(served)

    def read_upload_body(self):
        digest = hashlib.sha256()
        retained = bytearray()
        received = 0
        chunked = self.headers.get("Transfer-Encoding", "").lower() == "chunked"
        if chunked:
            while True:
                size_line = self.rfile.readline(128)
                size = int(size_line.split(b";", 1)[0], 16)
                if not size:
                    self.rfile.readline()
                    break
                chunk = self.rfile.read(size)
                self.rfile.read(2)
                digest.update(chunk)
                received += len(chunk)
                if len(retained) <= 1024 * 1024:
                    retained.extend(chunk)
        else:
            remaining = int(self.headers.get("Content-Length", "0"))
            while remaining:
                chunk = self.rfile.read(min(65536, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                digest.update(chunk)
                received += len(chunk)
                if len(retained) <= 1024 * 1024:
                    retained.extend(chunk)
        body = bytes(retained) if received <= 1024 * 1024 else None
        return received, digest.hexdigest(), body, chunked

    def do_PUT(self):
        if not self.authorize():
            return
        if not self.path.startswith("/upload/"):
            self.send_response(403)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        leaf = self.path[len("/upload/"):]
        status_by_name = {"forbidden.bin": 403, "unsupported.bin": 405,
                          "too-large.bin": 413}
        if leaf in status_by_name:
            self.send_response(status_by_name[leaf])
            self.send_header("Content-Length", "0")
            self.end_headers()
            self.upload_records.append({"path": self.path,
                                        "status": status_by_name[leaf],
                                        "received": 0})
            return
        if leaf == "interrupted-upload.bin":
            self.rfile.read(32768)
            self.upload_records.append({"path": self.path, "status": 0,
                                        "received": 32768})
            self.connection.shutdown(2)
            self.connection.close()
            return
        if self.headers.get("If-None-Match") == "*" and \
                self.path in DirectoryHandler.uploads:
            self.send_response(412)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self.upload_records.append({"path": self.path, "status": 412,
                                        "received": 0})
            return
        received, digest, body, chunked = self.read_upload_body()
        DirectoryHandler.uploads[self.path] = {
            "body": body, "size": received, "sha256": digest,
        }
        self.upload_records.append({
            "path": self.path, "status": 201, "received": received,
            "sha256": digest, "chunked": chunked,
        })
        self.send_response(201)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_MKCOL(self):
        if not self.authorize():
            return
        if not self.path.startswith("/upload/") or self.path == "/upload/":
            self.record(status=403)
            self.send_response(403)
        else:
            path = self.path.rstrip("/") + "/"
            parent = path.rstrip("/").rsplit("/", 1)[0] + "/"
            if path in DirectoryHandler.directories:
                status = 405
            elif parent != "/upload/" and parent not in DirectoryHandler.directories:
                status = 409
            else:
                DirectoryHandler.directories.add(path)
                status = 201
            self.record(status=status)
            self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_DELETE(self):
        if not self.authorize():
            return
        path = self.path
        directory = path.rstrip("/") + "/"
        if path == "/upload/" or not path.startswith("/upload/"):
            status = 403
        elif path in DirectoryHandler.uploads:
            del DirectoryHandler.uploads[path]
            status = 204
        elif directory in DirectoryHandler.directories:
            nonempty = any(item.startswith(directory)
                           for item in DirectoryHandler.uploads) or any(
                               item != directory and item.startswith(directory)
                               for item in DirectoryHandler.directories)
            if nonempty:
                status = 409
            else:
                DirectoryHandler.directories.remove(directory)
                status = 204
        else:
            status = 404
        self.record(status=status)
        self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_MOVE(self):
        if not self.authorize():
            return
        destination = self.headers.get("Destination", "")
        parsed = urllib.parse.urlsplit(destination)
        target = parsed.path
        source_directory = self.path.rstrip("/") + "/"
        target_directory = target.rstrip("/") + "/"
        if (parsed.netloc != self.headers.get("Host") or
                not self.path.startswith("/upload/") or
                not target.startswith("/upload/") or
                self.path == "/upload/" or target == "/upload/"):
            status = 403
        elif target in DirectoryHandler.uploads or \
                target_directory in DirectoryHandler.directories:
            status = 412
        elif self.path in DirectoryHandler.uploads:
            DirectoryHandler.uploads[target] = DirectoryHandler.uploads.pop(self.path)
            status = 201
        elif source_directory in DirectoryHandler.directories:
            moved_uploads = {
                target_directory + path[len(source_directory):]: value
                for path, value in DirectoryHandler.uploads.items()
                if path.startswith(source_directory)
            }
            for path in list(DirectoryHandler.uploads):
                if path.startswith(source_directory):
                    del DirectoryHandler.uploads[path]
            DirectoryHandler.uploads.update(moved_uploads)
            moved_directories = {
                target_directory + path[len(source_directory):]
                for path in DirectoryHandler.directories
                if path.startswith(source_directory)
            }
            DirectoryHandler.directories = {
                path for path in DirectoryHandler.directories
                if not path.startswith(source_directory)
            }
            DirectoryHandler.directories.update(moved_directories)
            status = 201
        else:
            status = 404
        self.record(status=status)
        self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, _format, *_args):
        pass


class QuietThreadingHTTPServer(http.server.ThreadingHTTPServer):
    def handle_error(self, _request, _client_address):
        # Interrupted-transfer fixtures deliberately terminate TLS/HTTP early.
        pass


def read_pty(fd, size=65536):
    try:
        data = os.read(fd, size)
        screen = PTY_SCREENS.get(fd)
        if data and screen is not None:
            screen.feed(data)
        return data
    except OSError as error:
        # Linux PTY masters may return EIO after slave close; treat that as EOF.
        if error.errno == errno.EIO:
            return b""
        raise


def pane_text(screen, pane):
    """Return only Commander listing cells, excluding prompts/status/filter rows."""
    divider = screen.columns // 2
    start = 0 if pane == 0 else divider + 1
    end = divider if pane == 0 else screen.columns
    return "\n".join(
        "".join(cell[0] for cell in row[start:end])
        for row in screen.cells[3:screen.rows - 3]
    )


def assert_keybar_state(screen, available=(), disabled=()):
    row = screen.cells[-1]
    line = "".join(cell[0] for cell in row)
    reference = line.find("F1")
    if reference < 0:
        raise RuntimeError(f"keybar F1 is missing: {line!r}")
    key_style = row[reference][1]
    label_style = row[reference + 2][1]
    for token in available:
        column = line.find(token)
        if column < 0 or row[column][1] != key_style or \
                row[column + len(token)][1] != label_style:
            raise RuntimeError(f"available {token} has inconsistent keybar style")
    for token in disabled:
        column = line.find(token)
        if column < 0 or row[column][1] == key_style or \
                row[column][1] != row[column + len(token)][1]:
            raise RuntimeError(f"disabled {token} has inconsistent keybar style")


def drain(fd):
    output = bytearray()
    os.set_blocking(fd, False)
    try:
        while True:
            data = read_pty(fd)
            if not data:
                break
            output.extend(data)
    except BlockingIOError:
        pass
    finally:
        os.set_blocking(fd, True)
    return bytes(output)


def read_until(fd, needle=None, timeout=0.25, quiet=0.025):
    output = bytearray()
    deadline = time.monotonic() + timeout
    quiet_deadline = None
    while time.monotonic() < deadline:
        wait = min(0.025, deadline - time.monotonic())
        ready, _, _ = select.select([fd], [], [], max(0, wait))
        if ready:
            data = read_pty(fd)
            if not data:
                break
            output.extend(data)
            quiet_deadline = time.monotonic() + quiet
            if needle and needle in output:
                break
        elif output and quiet_deadline and time.monotonic() >= quiet_deadline:
            break
    return bytes(output)


def send(fd, keys, delay=None):
    os.write(fd, keys)
    if delay is not None:
        time.sleep(delay)
        return drain(fd)
    return read_until(fd)


def run_nav_open(nav, url):
    with tempfile.TemporaryDirectory(prefix="navi8or-http-ui-") as temp:
        config = os.path.join(temp, "config", "nav")
        left = os.path.join(temp, "left")
        right = os.path.join(temp, "right")
        os.makedirs(config)
        os.mkdir(left)
        os.mkdir(right)
        with open(os.path.join(right, "right-unchanged.txt"), "w",
                  encoding="utf-8") as stream:
            stream.write("right")
        with open(os.path.join(config, "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[menu]\nremember_position = false\n[panels]\nview = \"full\"\n")
        with open(os.path.join(config, "repositories.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[[repositories]]\nname = \"HTTP UI Test\"\n"
                         f"url = \"{url}\"\ntls_verify = true\n")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm-256color"
            os.environ["XDG_CONFIG_HOME"] = os.path.join(temp, "config")
            os.execv(nav, [nav, left, right])
        exited = False
        try:
            screen = TerminalScreen(120, 30)
            PTY_SCREENS[fd] = screen
            fcntl.ioctl(fd, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 30, 120, 0, 0))
            initial = read_until(fd, b"right-unchanged.txt", 1.0)
            if b"right-unchanged.txt" not in initial:
                raise RuntimeError("inactive local pane fixture was not displayed")
            send(fd, b"\x1c")
            for _ in range(3):
                send(fd, b"\x1b[1;5C")
            send(fd, b"\r")
            opened = send(fd, b"\r", 0.6)
            if "HTTP UI T" not in screen.text() or "Repository" not in screen.text():
                raise RuntimeError(
                    f"Open Repository did not replace only the active pane: {opened[-2000:]!r}")
            mutation_count = sum(request["method"] in ("MKCOL", "DELETE", "MOVE")
                                 for request in DirectoryHandler.requests)
            disabled_mkdir = send(fd, b"\x1b[18~", 0.3)
            if sum(request["method"] in ("MKCOL", "DELETE", "MOVE")
                   for request in DirectoryHandler.requests) != mutation_count or \
                    b"supported" not in disabled_mkdir:
                raise RuntimeError("mkdir=false did not gate Commander F7")
            send(fd, b"\x1bOB")  # select a valid remote entry, not '..'
            disabled_delete = send(fd, b"\x1b[19~", 0.3)
            disabled_rename = send(fd, b"\x1b[17~", 0.3)
            if sum(request["method"] in ("MKCOL", "DELETE", "MOVE")
                   for request in DirectoryHandler.requests) != mutation_count:
                raise RuntimeError("disabled mutation reached the HTTP server")
            if b"supported" not in disabled_delete or \
                    b"supported" not in disabled_rename:
                raise RuntimeError("disabled F8/F6 status was not capability-specific")
            send(fd, b"\x1bOH")
            send(fd, b"\x1c")
            send(fd, b"\x1b[1;5C")
            send(fd, b"\x1bOB")
            send(fd, b"\x1bOB")
            send(fd, b"\r")  # Full -> Brief through the provider-neutral View menu
            send(fd, b"\x1bOB")
            child = send(fd, b"\r", 0.4)
            if b"sub/" not in child or b"nested.txt" not in child:
                raise RuntimeError(f"Enter did not navigate the HTTP pane: {child[-2000:]!r}")
            root_requests = sum(request["path"] == "/root/"
                                for request in DirectoryHandler.requests)
            send(fd, b"\x7f", 0.4)
            if sum(request["path"] == "/root/"
                   for request in DirectoryHandler.requests) <= root_requests:
                raise RuntimeError("Backspace did not return to repository root")
            root_requests = sum(request["path"] == "/root/"
                                for request in DirectoryHandler.requests)
            send(fd, b"\x12r", 0.4)
            if sum(request["path"] == "/root/"
                   for request in DirectoryHandler.requests) <= root_requests:
                raise RuntimeError("Ctrl+R R did not refresh the HTTP pane")

            send(fd, b"/")
            send(fd, b"large.txt")
            send(fd, b"\r")
            send(fd, b"\x1bOB")  # skip fallback-large.txt substring match
            viewed = send(fd, b"\x1bOR", 0.8)
            if "00000000" not in screen.text():
                raise RuntimeError(
                    f"F3 did not open the provider-backed remote Viewer: "
                    f"{DirectoryHandler.requests[-5:]!r} {viewed[-1000:]!r}")
            send(fd, b"\x1b")
            viewed = send(fd, b"\r", 0.8)
            if "00000000" not in screen.text():
                raise RuntimeError(
                    f"Enter did not reuse the remote Viewer path: {viewed[-2000:]!r}")
            send(fd, b"\x1b")

            send(fd, b"/")
            send(fd, b"\x7f" * len("large.txt"))
            send(fd, b"binary.bin")
            send(fd, b"\r")
            send(fd, b"\x1b[15~")
            copied = send(fd, b"\r", 2.0)
            copied_path = os.path.join(right, "binary.bin")
            if not os.path.isfile(copied_path):
                raise RuntimeError(
                    f"F5 did not copy HTTP to the opposite local pane: "
                    f"exists={os.path.isfile(copied_path)} "
                    f"requests={DirectoryHandler.requests[-6:]!r} "
                    f"screen={copied[-1500:]!r}")
            with open(copied_path, "rb") as stream:
                copied_body = stream.read()
            if copied_body != DirectoryHandler.payload("/root/binary.bin"):
                raise RuntimeError("F5 HTTP-to-Local output did not match")

            send(fd, b"\t")
            downloads = sum(request["method"] == "GET" and
                            request["path"] == "/root/binary.bin"
                            for request in DirectoryHandler.requests)
            disabled = send(fd, b"\x1b[15~", 0.4)
            if sum(
                    request["method"] == "GET" and
                    request["path"] == "/root/binary.bin"
                    for request in DirectoryHandler.requests) != downloads:
                raise RuntimeError("Local-to-HTTP F5 was not capability-disabled")
            # The status row may be rendered in separate terminal spans.
            if "Destination is" not in screen.text() or \
                    "read-only" not in screen.text():
                raise RuntimeError(
                    f"read-only F5 did not report the capability error: "
                    f"{disabled[-1500:]!r}")
            send(fd, b"\t")

            send(fd, b"\t")
            send(fd, b"\x15")  # move the HTTP provider into the active right pane
            send(fd, b"\x1c")
            send(fd, b"\r")  # File -> Open Location
            send(fd, b"\x7f")  # replace the remote display path '/'
            send(fd, left.encode())
            local = send(fd, b"\r", 0.4)
            if b"Local" not in local:
                raise RuntimeError("Open Location did not switch HTTP pane back to local")
            send(fd, b"\x1b[21~")
            child_pid, status = os.waitpid(pid, 0)
            if child_pid != pid or os.waitstatus_to_exitcode(status) != 0:
                raise RuntimeError("Navi8or HTTP pane lifecycle failed")
            exited = True
        finally:
            PTY_SCREENS.pop(fd, None)
            if not exited:
                try:
                    os.kill(pid, 15)
                    os.waitpid(pid, 0)
                except ProcessLookupError:
                    pass
            os.close(fd)


def run_nav_upload(nav, url):
    with tempfile.TemporaryDirectory(prefix="navi8or-http-upload-ui-") as temp:
        config = os.path.join(temp, "config", "nav")
        left = os.path.join(temp, "left")
        right = os.path.join(temp, "right")
        os.makedirs(config)
        os.mkdir(left)
        os.mkdir(right)
        source_path = os.path.join(left, "upload-ui.txt")
        with open(source_path, "w", encoding="utf-8") as stream:
            stream.write("uploaded through Commander F5\n")
        with open(os.path.join(config, "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[menu]\nremember_position = false\n[panels]\nview = \"full\"\n")
        with open(os.path.join(config, "repositories.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[[repositories]]\nname = \"Writable UI Test\"\n"
                         f"url = \"{url}\"\ntls_verify = true\nwritable = true\n"
                         "mkdir = true\ndelete = true\nrename = true\n")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm-256color"
            os.environ["XDG_CONFIG_HOME"] = os.path.join(temp, "config")
            os.execv(nav, [nav, left, right])
        exited = False
        try:
            screen = TerminalScreen(120, 30)
            PTY_SCREENS[fd] = screen
            fcntl.ioctl(fd, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 30, 120, 0, 0))
            read_until(fd, b"Local Filesystem", 1.0)
            send(fd, b"\t")  # open repository in the right pane
            send(fd, b"\x1c")
            for _ in range(3):
                send(fd, b"\x1b[1;5C")
            send(fd, b"\r")
            opened = send(fd, b"\r", 0.5)
            if "Writable UI" not in screen.text():
                raise RuntimeError("writable repository did not open in right pane")
            # The empty repository has only '..' selected: creation is valid,
            # while edit/delete require a real entry.
            assert_keybar_state(screen, available=("F1", "F2", "F7", "F10"),
                                disabled=("F4", "F8"))

            # Exercise F7/F8 through the real Commander path while HTTP is the
            # active pane. Creation and deletion must both refresh the pane.
            before_mutations = len(DirectoryHandler.requests)
            send(fd, b"\x1b[18~")
            send(fd, b"test folder")
            created = send(fd, b"\r", 0.6)
            if not any(request["method"] == "MKCOL" and
                       request["path"] == "/upload/test%20folder"
                       for request in DirectoryHandler.requests[before_mutations:]):
                raise RuntimeError("Commander F7 did not issue the expected MKCOL")
            if "test folder" not in pane_text(screen, 1):
                raise RuntimeError("HTTP pane did not refresh after MKCOL")
            send(fd, b"/")
            send(fd, b"test folder")
            send(fd, b"\r")
            before_delete = len(DirectoryHandler.requests)
            send(fd, b"\x1b[19~")
            deleted = send(fd, b"y", 0.6)
            if not any(request["method"] == "DELETE" and
                       request["path"] == "/upload/test%20folder/"
                       for request in DirectoryHandler.requests[before_delete:]):
                raise RuntimeError("Commander F8 did not issue the expected DELETE")
            if "test folder" in pane_text(screen, 1):
                raise RuntimeError("HTTP pane did not refresh after DELETE")
            send(fd, b"/")
            send(fd, b"\x7f" * len("test folder"))
            send(fd, b"\r")
            send(fd, b"\t")  # local source active
            send(fd, b"\x1bOB")  # skip '..'
            assert_keybar_state(screen,
                                available=("F1", "F2", "F3", "F4", "F5",
                                           "F7", "F8", "F10"),
                                disabled=("F6",))
            send(fd, b"\x1b[15~")
            uploaded = send(fd, b"\r", 1.0)
            if "/upload/upload-ui.txt" not in DirectoryHandler.uploads or \
                    DirectoryHandler.uploads["/upload/upload-ui.txt"]["body"] != \
                    b"uploaded through Commander F5\n":
                raise RuntimeError(f"Commander F5 upload failed: {uploaded[-1500:]!r}")
            if "upload-ui.txt" not in pane_text(screen, 1):
                raise RuntimeError("HTTP destination pane did not refresh after upload")
            send(fd, b"\t")
            send(fd, b"/")
            send(fd, b"upload-ui.txt")
            send(fd, b"\r")
            before_oversized_move = len(DirectoryHandler.requests)
            send(fd, b"\x1b[17~")
            send(fd, b"\x7f" * len("upload-ui.txt"))
            oversized_name = b"x" * 256
            send(fd, oversized_name, 2.0)
            send(fd, b"\r", 0.3)
            if any(request["method"] == "MOVE" for request in
                   DirectoryHandler.requests[before_oversized_move:]):
                raise RuntimeError("oversized HTTP rename issued MOVE")
            before_move = len(DirectoryHandler.requests)
            send(fd, b"\x1b[17~")
            send(fd, b"\x7f" * len("upload-ui.txt"))
            send(fd, b"renamed-ui.txt")
            renamed = send(fd, b"\r", 0.6)
            moves = [request for request in
                     DirectoryHandler.requests[before_move:]
                     if request["method"] == "MOVE"]
            if not moves or moves[-1]["path"] != "/upload/upload-ui.txt" or \
                    not moves[-1]["destination"].endswith(
                        "/upload/renamed-ui.txt"):
                raise RuntimeError(f"Commander F6 MOVE was incorrect: {moves!r}")
            if "/upload/upload-ui.txt" in DirectoryHandler.uploads or \
                    "/upload/renamed-ui.txt" not in DirectoryHandler.uploads:
                raise RuntimeError("Commander F6 did not rename the remote file")
            if "renamed-ui.txt" not in pane_text(screen, 1) or \
                    "upload-ui.txt" in pane_text(screen, 1):
                raise RuntimeError("HTTP pane did not refresh after MOVE")
            send(fd, b"/")
            send(fd, b"\x7f" * len("upload-ui.txt"))
            send(fd, b"renamed-ui.txt")
            send(fd, b"\r")
            viewed = send(fd, b"\x1bOR", 0.6)
            if "uploaded through Commander F5" not in screen.text():
                raise RuntimeError("uploaded file did not reopen through remote F3")
            send(fd, b"\x1b")
            send(fd, b"\x12r")
            assert_keybar_state(screen, available=("F7", "F8"), disabled=("F4",))
            send(fd, b"\x15")
            assert_keybar_state(screen, available=("F4", "F7", "F8"))
            send(fd, b"\x1b[21~")
            child_pid, status = os.waitpid(pid, 0)
            if child_pid != pid or os.waitstatus_to_exitcode(status) != 0:
                raise RuntimeError("writable HTTP pane lifecycle failed")
            exited = True
        finally:
            PTY_SCREENS.pop(fd, None)
            if not exited:
                try:
                    os.kill(pid, 15)
                    os.waitpid(pid, 0)
                except ProcessLookupError:
                    pass
            os.close(fd)


def run_server(executable, tls, verify, expect_success, certificate=None,
               key=None, nav=None, large_upload=False, auth_mode="none",
               client_secret=""):
    DirectoryHandler.requests = []
    DirectoryHandler.uploads = {}
    DirectoryHandler.directories = set()
    DirectoryHandler.upload_records = []
    DirectoryHandler.root_redirect = None
    if auth_mode == "basic":
        encoded = base64.b64encode(b"navi8or:testpass").decode()
        DirectoryHandler.expected_authorization = f"Basic {encoded}"
    elif auth_mode == "bearer":
        DirectoryHandler.expected_authorization = "Bearer navi8or-test-token-12345"
    else:
        DirectoryHandler.expected_authorization = None
    server = QuietThreadingHTTPServer(("127.0.0.1", 0), DirectoryHandler)
    if tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    scheme = "https" if tls else "http"
    url = f"{scheme}://localhost:{server.server_port}/root/"
    try:
        command = [executable, url, "1" if verify else "0",
                        "1" if expect_success else "0",
                        "1" if large_upload else "0"]
        if auth_mode != "none":
            command.extend([auth_mode, client_secret])
        subprocess.run(command, check=True,
                       timeout=120 if large_upload else 60)
        if expect_success:
            if DirectoryHandler.expected_authorization:
                expected_hash = hashlib.sha256(
                    DirectoryHandler.expected_authorization.encode()).hexdigest()
                if any(request["authorization_sha256"] != expected_hash
                       for request in DirectoryHandler.requests):
                    raise RuntimeError(
                        "credential was absent or crossed repository boundaries")
            large_ranges = [request for request in DirectoryHandler.requests
                            if request["path"] == "/root/large.txt"]
            if not large_ranges or large_ranges[0]["range"] != "bytes=0-65535" or \
                    large_ranges[0]["served"] != 65536:
                raise RuntimeError(f"remote Viewer was not a bounded Range read: {large_ranges!r}")
            if any(request["served"] > 65536 for request in large_ranges):
                raise RuntimeError("remote Viewer issued an oversized block request")
            if sum(request["served"] for request in large_ranges) >= 10 * 1024 * 1024:
                raise RuntimeError("large Viewer navigation transferred too much data")
            known_downloads = [request for request in DirectoryHandler.requests
                               if request["path"] == "/root/binary.bin" and
                               request["method"] == "GET"]
            if len(known_downloads) != 1 or known_downloads[0]["range"] or \
                    known_downloads[0]["served"] != 5 * 1024 * 1024:
                raise RuntimeError("known-size F5 did not stream the complete object")
            unknown_downloads = [request for request in DirectoryHandler.requests
                                 if request["path"] == "/root/unknown.bin" and
                                 request["method"] == "GET"]
            if len(unknown_downloads) != 1 or \
                    unknown_downloads[0]["served"] != 384 * 1024:
                raise RuntimeError("unknown-size F5 did not stream the complete object")
            fallback = [request for request in DirectoryHandler.requests
                        if request["path"] == "/root/fallback-large.txt"]
            if not fallback or fallback[0]["served"] >= 8 * 1024 * 1024:
                raise RuntimeError("non-Range Viewer fallback consumed the whole object")
            records = DirectoryHandler.upload_records
            required = {"/upload/zero.bin", "/upload/tiny.txt",
                        "/upload/space%20%23%20%25%20%C3%BC%20%28x%29.txt",
                        "/upload/unknown.bin", "/upload/interrupted-upload.bin",
                        "/upload/forbidden.bin", "/upload/unsupported.bin",
                        "/upload/too-large.bin"}
            if not required.issubset({record["path"] for record in records}):
                raise RuntimeError(f"missing HTTP PUT coverage: {records!r}")
            unknown_upload = next(record for record in records
                                  if record["path"] == "/upload/unknown.bin")
            if not unknown_upload["chunked"] or unknown_upload["received"] != 24:
                raise RuntimeError("unknown-size upload was not streamed chunked")
            special = DirectoryHandler.uploads[
                "/upload/space%20%23%20%25%20%C3%BC%20%28x%29.txt"]
            if special["body"] != b"special\n":
                raise RuntimeError("special-filename upload did not round-trip")
            renamed_special = "/upload/renamed%20%23%20%25%20%C3%BC%20%28x%29.txt"
            if renamed_special not in DirectoryHandler.uploads or \
                    DirectoryHandler.uploads[renamed_special]["body"] != b"second upload\n" or \
                    "/upload/rename-old.txt" in DirectoryHandler.uploads:
                raise RuntimeError("special-filename MOVE did not preserve contents")
            if "/upload/nonempty-renamed/child.txt" not in DirectoryHandler.uploads or \
                    "/upload/nonempty/child.txt" in DirectoryHandler.uploads:
                raise RuntimeError("directory MOVE did not preserve its child")
            tiny_records = [record for record in records
                            if record["path"] == "/upload/tiny.txt"]
            if [record["status"] for record in tiny_records] != [201, 412, 201] or \
                    DirectoryHandler.uploads["/upload/tiny.txt"]["body"] != b"second upload\n":
                raise RuntimeError("conditional upload overwrite semantics failed")
            if large_upload:
                large_record = next(record for record in records
                                    if record["path"] == "/upload/large.bin")
                digest = hashlib.sha256()
                for _ in range(100 * 1024 * 1024 // 65536):
                    digest.update(b"\0" * 65536)
                if large_record["received"] != 100 * 1024 * 1024 or \
                        large_record["sha256"] != digest.hexdigest():
                    raise RuntimeError("100 MiB streamed upload hash mismatch")
        if nav:
            run_nav_open(nav, url)
            run_nav_upload(nav, url.replace("/root/", "/upload/"))
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    if expect_success:
        if any(("space%20name.txt" in request["path"] or
                "nested.txt" in request["path"])
               for request in DirectoryHandler.requests):
            raise RuntimeError("browsing fetched a file body")
        if not any(request["path"] == "/root/sub/"
                   for request in DirectoryHandler.requests):
            raise RuntimeError("remote child directory was not fetched")


def run_stream_redirect_tests(executable, certificate=None, key=None,
                              auth_mode="none", secret=""):
    class StreamHandler(DirectoryHandler):
        requests = []
        expected_authorization = None

        def do_GET(self):
            if not self.authorize():
                return
            path = self.path
            targets = {
                "/root/absolute": origin_url + "/root/direct",
                "/root/relative": "sub/../direct",
                "/root/outside": "/external",
                "/root/cross": sink_url + "/root/direct",
                "/root/traversal": "../external",
                "/root/encoded": "/root/%2e%2e/external",
                "/root/encoded-slash": "/root/%2e%2e%2fexternal",
                "/root/chain": "relative-outside",
                "/root/relative-outside": "/external",
            }
            status = 302
            target = targets.get(path)
            if path.startswith("/root/status"):
                status = int(path.removeprefix("/root/status"))
                target = "direct"
            if path.startswith("/root/limit/"):
                remaining = int(path.rsplit("/", 1)[1])
                if remaining:
                    target = str(remaining - 1)
            if not target:
                status = {"/root/missing": 404, "/root/denied": 403,
                          "/root/unauthorized": 401}.get(path, 200)
            body = (b"R" if target else b"S") * (128 * 1024)
            self.record(served=len(body), status=status)
            self.send_response(status)
            if target:
                self.send_header("Location", target)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    if auth_mode == "basic":
        encoded = base64.b64encode(f"navi8or:{secret}".encode()).decode()
        StreamHandler.expected_authorization = f"Basic {encoded}"
    elif auth_mode == "bearer":
        StreamHandler.expected_authorization = f"Bearer {secret}"
    sink_handler = type("StreamSink", (StreamHandler,), {
        "requests": [], "expected_authorization": None,
    })
    origin = QuietThreadingHTTPServer(("127.0.0.1", 0), StreamHandler)
    sink = QuietThreadingHTTPServer(("127.0.0.1", 0), sink_handler)
    servers = (origin, sink)
    if certificate:
        for server in servers:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certificate, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
    scheme = "https" if certificate else "http"
    origin_url = f"{scheme}://localhost:{origin.server_port}"
    sink_url = f"{scheme}://localhost:{sink.server_port}"
    threads = [threading.Thread(target=server.serve_forever, daemon=True)
               for server in servers]
    for thread in threads:
        thread.start()
    try:
        subprocess.run([executable, origin_url + "/root/", "0", "1", "0",
                        auth_mode, secret, "stream-redirects"], check=True,
                       timeout=60)
        assert not sink_handler.requests, "stream redirect reached another origin"
        paths = [request["path"] for request in StreamHandler.requests]
        assert "/external" not in paths
        assert not any("%" in path for path in paths)
        assert paths.count("/root/limit/0") == 1
        assert paths.count("/root/limit/1") == 2
        if StreamHandler.expected_authorization:
            expected = hashlib.sha256(
                StreamHandler.expected_authorization.encode()).hexdigest()
            assert all(request["authorization_sha256"] == expected
                       for request in StreamHandler.requests)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)


def run_cross_origin_redirect_test(executable, certificate, key):
    sink_handler = type("RedirectSinkHandler", (DirectoryHandler,), {
        "requests": [], "expected_authorization": None, "root_redirect": None,
    })
    sink = QuietThreadingHTTPServer(("127.0.0.1", 0), sink_handler)
    origin = QuietThreadingHTTPServer(("127.0.0.1", 0), DirectoryHandler)
    for server in (sink, origin):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    encoded = base64.b64encode(b"navi8or:testpass").decode()
    DirectoryHandler.requests = []
    DirectoryHandler.expected_authorization = f"Basic {encoded}"
    DirectoryHandler.root_redirect = (
        f"https://127.0.0.1:{sink.server_port}/root/")
    threads = [threading.Thread(target=server.serve_forever, daemon=True)
               for server in (sink, origin)]
    for thread in threads:
        thread.start()
    try:
        url = f"https://localhost:{origin.server_port}/root/"
        subprocess.run([executable, url, "0", "1", "0", "basic",
                        "testpass", "redirect"], check=True,
                       timeout=30)
        if not sink_handler.requests:
            raise RuntimeError("cross-origin redirect did not reach sink")
        if any(request["authorization_sha256"] is not None
               for request in sink_handler.requests):
            raise RuntimeError("credential leaked across redirect origin")
    finally:
        DirectoryHandler.root_redirect = None
        for server in (origin, sink):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)


def run_cross_repository_test(executable, certificate, key):
    encoded = base64.b64encode(b"navi8or:testpass").decode()
    basic_handler = type("BasicRepositoryHandler", (DirectoryHandler,), {
        "requests": [], "expected_authorization": f"Basic {encoded}",
        "root_redirect": None,
    })
    bearer_handler = type("BearerRepositoryHandler", (DirectoryHandler,), {
        "requests": [],
        "expected_authorization": "Bearer navi8or-test-token-12345",
        "root_redirect": None,
    })
    servers = [QuietThreadingHTTPServer(("127.0.0.1", 0), handler)
               for handler in (basic_handler, bearer_handler)]
    for server in servers:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threads = [threading.Thread(target=server.serve_forever, daemon=True)
               for server in servers]
    for thread in threads:
        thread.start()
    try:
        basic_url = f"https://localhost:{servers[0].server_port}/root/"
        bearer_url = f"https://localhost:{servers[1].server_port}/root/"
        subprocess.run([executable, basic_url, "0", "1", "0", "cross",
                        "testpass", bearer_url,
                        "navi8or-test-token-12345"], check=True, timeout=30)
        basic_hash = hashlib.sha256(f"Basic {encoded}".encode()).hexdigest()
        bearer_hash = hashlib.sha256(
            b"Bearer navi8or-test-token-12345").hexdigest()
        if not basic_handler.requests or not bearer_handler.requests:
            raise RuntimeError("cross-repository credential probes were absent")
        if any(request["authorization_sha256"] != basic_hash
               for request in basic_handler.requests) or any(
                   request["authorization_sha256"] != bearer_hash
                   for request in bearer_handler.requests):
            raise RuntimeError("credentials crossed repository boundaries")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)


def main():
    if len(sys.argv) != 3:
        return 2
    executable = os.path.abspath(sys.argv[1])
    nav = os.path.abspath(sys.argv[2])
    run_stream_redirect_tests(executable)
    line_count = 50 * 1024 * 1024 // 64
    large = bytearray(50 * 1024 * 1024)
    for index in range(line_count):
        text = f"line {index:08d} ".encode()
        line = text + b"x" * (63 - len(text)) + b"\n"
        start = index * 64
        large[start:start + 64] = line
    marker_offset = 5 * 1024 * 1024
    marker_line = marker_offset // 64
    marker = f"line {marker_line:08d} MARKER-5MB".encode()
    marker_start = marker_line * 64
    large[marker_start:marker_start + 63] = marker.ljust(63, b"x")
    DirectoryHandler.fixtures = {
        "/root/large.txt": bytes(large),
        "/root/long-line.txt": b"L" * (2 * 65536 + 17) + b"\nlast-no-newline",
        "/root/aligned.txt": (b"aligned line\n" * 6000)[:65536],
        "/root/partial.txt": (b"partial line\n" * 6000)[:65536 + 123],
        "/root/utf8-boundary.txt": b"u" * 65535 + "€".encode() + b"\n",
        "/root/line-limit-minus.txt": b"m" * (8 * 1024 * 1024 - 1),
        "/root/line-limit.txt": b"e" * (8 * 1024 * 1024),
        "/root/line-limit-plus.txt": b"p" * (8 * 1024 * 1024 + 1),
    }
    run_server(executable, False, True, True, nav=nav, large_upload=True)
    with tempfile.TemporaryDirectory(prefix="navi8or-http-test-") as temp:
        certificate = os.path.join(temp, "cert.pem")
        key = os.path.join(temp, "key.pem")
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-keyout", key, "-out", certificate,
            "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        run_stream_redirect_tests(executable, certificate, key,
                                  "basic", "testpass")
        run_stream_redirect_tests(executable, certificate, key,
                                  "bearer", "navi8or-test-token-12345")
        run_server(executable, True, False, True, certificate, key)
        run_server(executable, True, False, True, certificate, key,
                   auth_mode="basic", client_secret="testpass")
        run_server(executable, True, False, True, certificate, key,
                   auth_mode="bearer",
                   client_secret="navi8or-test-token-12345")
        run_server(executable, True, False, False, certificate, key,
                   auth_mode="basic", client_secret="wrong-password")
        run_server(executable, True, False, False, certificate, key,
                   auth_mode="bearer", client_secret="wrong-token")
        run_cross_repository_test(executable, certificate, key)
        run_cross_origin_redirect_test(executable, certificate, key)
        run_server(executable, True, True, False, certificate, key)
    print("http provider integration: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
