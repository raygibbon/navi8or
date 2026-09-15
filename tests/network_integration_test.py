#!/usr/bin/env python3
"""Failing environment proxy: live TUI Apply/Cancel/Save/restart and HTTP API."""
import os
import base64
import hashlib
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http_integration_test import DirectoryHandler, QuietThreadingHTTPServer
from preferences_integration_test import Session, ENTER, ESC, F2, F4
from resize_test import DOWN, UP, END, resize, drain
from terminal_screen import TerminalScreen

def main():
    client, nav = map(lambda s: str(Path(s).resolve()), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="nav-network-") as temporary:
        root = Path(temporary); cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-keyout", str(key), "-out", str(cert), "-subj", "/CN=localhost"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        server = QuietThreadingHTTPServer(("127.0.0.1", 0), DirectoryHandler)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); tls.load_cert_chain(cert, key)
        server.socket = tls.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        url = f"https://localhost:{server.server_port}/"
        DirectoryHandler.fixtures["/root/proxy-range.txt"] = b"proxy body\n"
        environment = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", https_proxy="http://127.0.0.1:1",
                           NO_PROXY="", no_proxy="", XDG_CONFIG_HOME=str(root / "config"), TERM="xterm-256color")
        try:
            subprocess.run([client, url], env=environment, check=True, timeout=40)
            for mode in ("basic", "bearer"):
                authorization = ("Basic " + base64.b64encode(b"network-user:network-pass").decode()
                                 if mode == "basic" else "Bearer network-pass")
                DirectoryHandler.expected_authorization = authorization
                DirectoryHandler.requests = []
                subprocess.run([client, url, mode], env=environment, check=True, timeout=40)
                expected = hashlib.sha256(authorization.encode()).hexdigest()
                assert DirectoryHandler.requests and all(r["authorization_sha256"] == expected for r in DirectoryHandler.requests)
            DirectoryHandler.expected_authorization = None
            left, right = root / "left", root / "right"; left.mkdir(); right.mkdir()
            (left / "local.txt").write_text("keep local pane\n")
            config = root / "config" / "nav"; config.mkdir(parents=True)
            operational = config / "nav.toml"
            operational.write_text('# retained comment\n[menu]\nremember_position=false\n')
            (config / "repositories.toml").write_text(
                f'[[repositories]]\nname="Proxy fixture"\nurl="{url}root/"\ntls_verify=false\n')
            session = Session(nav, left, right, environment)
            def open_repository():
                session.send(b"\x12o"); session.send(ENTER, .6)
            def network():
                session.preferences(); session.send(END); session.send(ENTER)
                assert "Proxy Mode" in session.text(), session.text()
            def choose(n, cancel=False, space=False):
                session.send(b" " if space else ENTER)
                assert "No Proxy" in session.text(), session.text()
                session.send(END if n else b"\x1bOH")
                session.send(ESC if cancel else ENTER)
            try:
                count = len(DirectoryHandler.requests); open_repository()
                assert "local.txt" in session.text() and "Loading repository" not in session.text(), session.text()
                assert len(DirectoryHandler.requests) == count, "System ignored failing proxy"
                network(); choose(1, cancel=True, space=True)
                assert "[ System ▼ ]" in session.text() and "Preferences *" not in session.text(), session.text()
                choose(1); session.send(DOWN); session.send(b"\x1bOC"); session.send(b"\x1bOC"); session.send(ENTER) # Cancel action button
                assert "Discard unsaved Preferences" in session.text(), session.text()
                session.send(b"y")
                assert 'proxy_mode' not in operational.read_text()
                network(); choose(1); session.send(F4)
                open_repository(); assert "fallback-small.txt" in session.text(), session.text()
                assert 'proxy_mode' not in operational.read_text(), "Apply unexpectedly saved"
                network(); choose(0); session.send(F4)
                count = len(DirectoryHandler.requests)
                session.send(b"\x12r", .6) # refresh existing HTTP provider
                assert len(DirectoryHandler.requests) == count, "System ignored failing proxy after direct success"
                network(); choose(1)
                # Small terminal keeps category navigation, picker and actions accessible.
                resize(session.fd, 60, 16); session.screen = TerminalScreen(60, 16); time.sleep(.2); session.screen.feed(drain(session.fd))
                choose(0, cancel=True)
                assert "[ No Proxy ▼ ]" in session.text(), session.text()
                session.send(F2, .5)
                assert 'proxy_mode = "none"' in operational.read_text(), session.text()
                assert '# retained comment' in operational.read_text()
                assert not (config / "profiles").exists(), "Network-only Save created a UI profile"
                session.quit()
            finally: session.close()
            session = Session(nav, left, right, environment)
            try:
                open_repository(); assert "fallback-small.txt" in session.text(), session.text()
                network(); assert "[ No Proxy ▼ ]" in session.text(), session.text()
                session.send(ESC); session.send(ESC) # unchanged: no discard prompt
                assert "Discard" not in session.text(), session.text()
                session.preferences(); session.send(ENTER); session.row(4) # General -> action row
                session.send(b"\x1bOC"); session.send(b"\x1bOC"); session.send(ENTER)
                assert "Preferences" not in session.text(), session.text()
                session.quit()
            finally: session.close()
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=2)
    print("Network TUI: picker/Space/Esc, Cancel, Apply, System restored, Save/restart, 60x16: passed")

if __name__ == "__main__": main()
