"""Offline curl/provider and real TUI location/download tests."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from preferences_integration_test import Session, ENTER, ESC
from resize_test import DOWN

BODY = b"direct URL log content\nsecond line\n"
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *args): pass
    def respond(self, head=False):
        if self.path == "/directory":
            self.send_response(301); self.send_header("Location", "/directory/"); self.send_header("Content-Length", "0"); self.end_headers(); return
        if self.path.startswith("/auth/"):
            expected = "specific" if self.path.startswith("/auth/nested/") else "broad"
            if self.headers.get("Authorization") != "Bearer " + expected:
                self.send_response(401); self.send_header("Content-Length", "0"); self.end_headers(); return
        elif self.path.startswith("/401") or self.path.startswith("/403"):
            self.send_response(int(self.path[1:4])); self.send_header("Content-Length", "0"); self.end_headers(); return
        if not head and self.path.startswith("/get401"):
            self.send_response(401); self.send_header("Content-Length", "0"); self.end_headers(); return
        chunked = self.path.startswith("/chunked/")
        large = self.path.startswith(("/large/", "/broken/", "/slow/", "/stalled/"))
        body = b"<pre><a href='file.log'>file.log</a> 14-Sep-2026 18:45 34\n</pre>" if self.path == "/directory/" else BODY
        self.send_response(200)
        self.send_header("Content-Type", "text/html" if self.path == "/directory/" else "text/plain")
        if chunked: self.send_header("Transfer-Encoding", "chunked")
        else: self.send_header("Content-Length", str(64 * 1024 * 1024 if large else len(body)))
        self.end_headers()
        if head: return
        try:
            if chunked:
                for part in (BODY[:8], BODY[8:]): self.wfile.write(f"{len(part):x}\r\n".encode() + part + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
            elif self.path.startswith("/stalled/"):
                self.wfile.write(b"x" * 65536); self.wfile.flush(); time.sleep(5); self.close_connection = True
            elif large:
                for _ in range(2 if self.path.startswith("/broken/") else 1024):
                    self.wfile.write(b"x" * 65536)
                    if self.path.startswith("/slow/"): time.sleep(.01)
                if self.path.startswith("/broken/"): self.close_connection = True
            else: self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError): pass
    def do_HEAD(self): self.respond(True)
    def do_GET(self): self.respond()

def replace_field(session, value):
    session.send(b"\x1bOH") # Home
    session.send(b"\x1bOF") # End
    # Current field content is selected only by explicit deletions.
    for _ in range(180): session.send(b"\x7f", 0)
    session.send(value.encode(), .3)

def location(session, value, download=False):
    session.send(b"\x0c")
    assert "Enter URL / Location" in session.text(), session.text()
    replace_field(session, value)
    if download: session.send(b"\t"); session.send(b"\x1bOC")
    session.send(ENTER, .5)

def confirm_download(session, directory, filename):
    assert "Download / Save Copy" in session.text(), session.text()
    replace_field(session, str(directory)); session.send(ENTER)
    replace_field(session, filename); session.send(ENTER); session.send(ENTER, .6)
    assert "Download complete" in session.text(), session.text()
    session.send(ESC)

def main():
    driver, executable = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
    def run_case(mode, url, target, base, env):
        arguments = [driver, mode, url, str(target), str(base)]
        if driver.endswith(".exe"):
            arguments = ["wine", driver, mode, url, "Z:" + str(target).replace("/", "\\"), "Z:" + str(base).replace("/", "\\")]
            env = dict(env, WINEDEBUG="-all")
        subprocess.run(arguments, env=env, check=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_port}"
    environment = dict(os.environ, NO_PROXY="127.0.0.1,localhost", no_proxy="127.0.0.1,localhost")
    with tempfile.TemporaryDirectory(prefix="nav-location-") as temp:
        root = Path(temp); left = root / "left"; other = root / "other"; right = root / "right"
        for p in (left, other, right): p.mkdir()
        for index, (mode, path) in enumerate([
            ("directory", "/directory/"), ("redirect-directory", "/directory"), ("file", "/directory/file.log"),
            ("file", "/directory/file.log?download=1"), ("file", "/chunked/file.log"),
            ("large", "/large/file.bin"), ("cancel", "/large/file.bin"), ("stall-cancel", "/stalled/file.bin"),
            ("failure", "/broken/file.bin"), ("failure", "/401"), ("failure", "/403"), ("failure", "/get401")]):
            run_case(mode, origin + path, root / f"copy{index}", left, environment)
        protected = root / "protected.log"; protected.write_text("keep")
        run_case("overwrite-failure", origin + "/broken/file.bin", protected, left, environment)
        conflict = root / "conflict.log"; Path(str(conflict) + ".part").write_text("keep")
        run_case("partial-conflict", origin + "/directory/file.log", conflict, left, environment)
        # Exercise proxy routing: a deliberately unreachable proxy is respected,
        # and explicit NO_PROXY restores access to the same origin.
        proxied = dict(environment, http_proxy="http://127.0.0.1:1", HTTP_PROXY="http://127.0.0.1:1", NO_PROXY="", no_proxy="")
        run_case("failure", origin + "/directory/file.log", root / "proxy-failure", left, proxied)
        run_case("file", origin + "/directory/file.log", root / "proxy-bypass", left, dict(proxied, NO_PROXY="127.0.0.1", no_proxy="127.0.0.1"))
        certificate, key = root / "cert.pem", root / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost", "-keyout", str(key), "-out", str(certificate)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        secure = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(certificate, key)
        secure.socket = context.wrap_socket(secure.socket, server_side=True)
        threading.Thread(target=secure.serve_forever, daemon=True).start()
        try:
            for n, path in enumerate(("/auth/nested/file.log", "/auth/file.log", "/auth/nestedness/file.log")):
                auth_dir = root / f"auth-dir{n}"; auth_dir.mkdir()
                run_case("auth", f"https://127.0.0.1:{secure.server_port}" + path, root / f"auth-copy{n}", auth_dir, environment)
        finally: secure.shutdown(); secure.server_close()
        profile = root / "profile.toml"
        profile.write_text('[profile]\nname="URL test"\n[keys.viewer.commands]\ndownload="Alt+D"\n')
        env = dict(environment, TERM="xterm-256color", XDG_CONFIG_HOME=str(root / "config"))
        session = Session(executable, left, right, env, profile)
        try:
            location(session, str(other)); assert str(other) in session.text(), session.text()
            location(session, str(left))
            location(session, origin + "/directory/"); assert "file.log" in session.text(), session.text()
            location(session, str(left))
            location(session, origin + "/directory"); assert "file.log" in session.text(), session.text()
            location(session, str(left))
            location(session, origin + "/401"); assert "authentication" in session.text(), session.text()
            location(session, origin + "/directory/file.log")
            assert "direct URL log content" in session.text(), session.text()
            session.send(b"\x1bd")
            assert str(left) in session.text(), session.text()
            confirm_download(session, other, "renamed.log")
            assert (other / "renamed.log").read_bytes() == BODY
            session.send(ESC)
            location(session, origin + "/chunked/file.log", True)
            assert str(left) in session.text(), session.text()
            confirm_download(session, left, "direct.log")
            assert (left / "direct.log").read_bytes() == BODY
            location(session, origin + "/slow/file.bin", True)
            session.send(ENTER); replace_field(session, "cancelled.bin"); session.send(ENTER); session.send(ENTER, .15)
            session.send(ESC, .8)
            deadline = time.monotonic() + 5
            while "Download cancelled" not in session.text() and time.monotonic() < deadline:
                session.send(b"", .2)
            assert "Download cancelled" in session.text(), session.text()
            assert not (left / "cancelled.bin").exists() and not (left / "cancelled.bin.part").exists()
            session.send(ESC)
            # File-menu accelerator invokes the same command and same dialog.
            session.send(b"\x1c"); session.send(b"o")
            assert "Enter URL / Location" in session.text(), session.text()
            session.send(ESC); session.quit()
        finally: session.close()
    server.shutdown(); server.server_close()
    print("URL resolution, auth, streaming/partial/cancel/proxy and actual TUI workflows passed")
if __name__ == "__main__": main()
