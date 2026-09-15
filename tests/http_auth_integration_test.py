"""Live HTTPS/API and keyboard-only challenge workflows, with synthetic secrets."""
import base64
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
from preferences_integration_test import Session, ENTER, ESC
from resize_test import DOWN
from location_integration_test import replace_field, location, confirm_download

BASIC = 'Basic ' + base64.b64encode(b'fixture-user:fixture-pass').decode()
F9 = b'\x1b[20~'
F11 = b'\x1b[23~'
F7 = b'\x1b[18~'

class Handler(http.server.BaseHTTPRequestHandler):
    requests = []
    origin = ''
    other_origin = ''

    def log_message(self, *args): pass

    def respond(self, head):
        authorization = self.headers.get('Authorization')
        type(self).requests.append((self.command, self.path, authorization))
        if self.path.startswith('/files/redirect-'):
            self.send_response(302)
            self.send_header('Location', '/files/once-target' if 'once' in self.path else
                             self.other_origin + '/sink' if 'host' in self.path else '/sink')
            self.send_header('Content-Length', '0'); self.end_headers(); return
        if self.path.startswith('/forbidden/'):
            self.send_response(403); self.send_header('Content-Length', '0'); self.end_headers(); return
        protected = self.path.startswith(('/files/', '/known/', '/logs/', '/bearer/', '/newrepo/', '/download-only/'))
        if self.path.startswith('/download-only/') and head: protected = False
        bearer = self.path.startswith('/bearer/')
        expected = 'Bearer fixture-pass' if bearer else BASIC
        if protected and authorization != expected:
            self.send_response(401)
            self.send_header('WWW-Authenticate', 'Bearer realm="fixture"' if bearer else 'Basic realm="fixture"')
            self.send_header('Content-Length', '0'); self.end_headers(); return
        body = (f'ORIGIN RESOURCE\n{self.origin}/logs/job.log\n{self.origin}/logs/other.log\n'
                f'{self.origin}/unrelated/public.log\n{self.origin}/download-only/file.log\n'
                f'{self.origin}/known/job.log\n').encode() if self.path == '/files/origin.log' else b'AUTHENTICATED LOG\nsecond line\n'
        start, end = 0, len(body) - 1
        if self.headers.get('Range'):
            first, last = self.headers['Range'][6:].split('-')
            start = int(first); end = min(int(last), end) if last else end
            self.send_response(206); self.send_header('Content-Range', f'bytes {start}-{end}/{len(body)}')
        else: self.send_response(200)
        self.send_header('Content-Length', str(end - start + 1)); self.end_headers()
        if not head: self.wfile.write(body[start:end + 1])

    def do_GET(self): self.respond(False)
    def do_HEAD(self): self.respond(True)

def prepare(driver, root, origin):
    config = root / 'config' / 'nav'; config.mkdir(parents=True)
    (config / 'nav.toml').write_text(
        '[network]\nproxy_mode="none"\n[menu]\nremember_position=false\n'
        '[keys.viewer.commands]\n"viewer.open_link"="F9"\n"viewer.back"="F11"\n'
        '"viewer.toggle_fullscreen"="F7"\n')
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'), TERM='xterm-256color',
               NO_PROXY='localhost,127.0.0.1', no_proxy='localhost,127.0.0.1')
    subprocess.run([driver, 'seed', str(config), origin], env=env, check=True)
    return config, env

def choose(session, credential=1, scope=0):
    assert 'Authentication Required' in session.text(), session.text()
    session.send(ENTER)
    assert 'Vault Credential' in session.text(), session.text()
    session.send(b'\x1bOH')
    for _ in range(credential): session.send(DOWN)
    session.send(ENTER)
    session.send(ENTER)
    assert 'Credential Scope' in session.text(), session.text()
    for _ in range(scope): session.send(DOWN)
    session.send(ENTER); session.send(ENTER, .5)

def main():
    driver, nav = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    with tempfile.TemporaryDirectory(prefix='nav-http-auth-') as temporary:
        root = Path(temporary); cert, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-keyout', str(key), '-out', str(cert), '-subj', '/CN=localhost'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); tls.load_cert_chain(cert, key)
        servers = [http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) for _ in range(2)]
        for server in servers:
            server.socket = tls.wrap_socket(server.socket, server_side=True)
            threading.Thread(target=server.serve_forever, daemon=True).start()
        Handler.origin = origin = f'https://localhost:{servers[0].server_port}'
        Handler.other_origin = f'https://127.0.0.1:{servers[1].server_port}'
        try:
            api = root / 'api'; api.mkdir(); (api / 'nav.toml').write_text('[network]\nproxy_mode="none"\n')
            env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'api-config'), NO_PROXY='localhost,127.0.0.1', no_proxy='localhost,127.0.0.1')
            subprocess.run([driver, 'check', str(api), origin], env=env, check=True, timeout=60)
            assert not any(path == '/sink' for _, path, _ in Handler.requests), 'Out-of-scope redirect was followed'
            assert not any(path == '/files/once-target' for _, path, _ in Handler.requests), 'Use once broadened to a different URL'
            tui = root / 'tui'; tui.mkdir(); config, env = prepare(driver, tui, origin)
            helpers = tui / 'helpers'; helpers.mkdir(); browser_log = tui / 'browser-argv'
            helper = helpers / 'xdg-open'
            helper.write_text('#!/usr/bin/python3\nimport os,sys\nopen(os.environ["AUTH_BROWSER_LOG"],"w").write("\\n".join(sys.argv[1:]))\n')
            helper.chmod(0o700)
            env.update(PATH=str(helpers) + os.pathsep + env.get('PATH', ''), AUTH_BROWSER_LOG=str(browser_log))
            left, right = tui / 'left', tui / 'right'; left.mkdir(); right.mkdir()
            (left / 'local.log').write_text('local marker\n'); (right / 'opposite-marker.txt').write_text('opposite context\n')
            before_config = (config / 'repositories.toml').read_bytes()
            session = Session(nav, left, right, env)
            try:
                # Unlock within the original operation, without visiting Vault menus.
                location(session, origin + '/files/origin.log')
                assert 'Vault locked' in session.text(), session.text()
                session.send(ENTER); session.send(b'wrong-master'); session.send(ENTER, .6)
                assert 'Unable to unlock vault' in session.text(), session.text()
                session.send(ESC); session.send(ENTER); session.send(b'auth-test-master'); session.send(ENTER, .6)
                choose(session)
                assert 'ORIGIN RESOURCE' in session.text() and 'opposite-marker.txt' in session.text(), session.text()
                session.send(DOWN); count = len(Handler.requests)
                session.send(F9); session.send(DOWN); session.send(DOWN); session.send(ENTER, .5)
                assert browser_log.read_text() == origin + '/logs/job.log'
                assert len(Handler.requests) == count and 'Authentication Required' not in session.text()
                session.send(F7)
                # Anonymous request outside Repo A: wrong credential then manual selection of B.
                count = len(Handler.requests); session.send(F9); session.send(ENTER, .5)
                assert Handler.requests[count:] and all(auth is None for _, _, auth in Handler.requests[count:])
                session.send(ESC)
                assert 'ORIGIN RESOURCE' in session.text() and 'Authentication Required' not in session.text() and 'Viewer Error' not in session.text(), session.text()
                session.send(F9); session.send(ENTER, .5)
                choose(session, credential=0)
                assert 'authentication failed' in session.text(), session.text()
                assert 'already attempted' in session.text(), session.text()
                choose(session, credential=0)
                assert 'already attempted. Retry it manually?' in session.text(), session.text()
                session.send(b'y', .5)
                assert 'Authentication Required' in session.text(), session.text()
                choose(session)
                assert 'AUTHENTICATED LOG' in session.text() and 'opposite-marker.txt' not in session.text(), session.text()
                session.send(F7)
                assert 'AUTHENTICATED LOG' in session.text() and 'opposite-marker.txt' in session.text(), session.text()
                assert (config / 'repositories.toml').read_bytes() == before_config
                session.send(F11); assert 'ORIGIN RESOURCE' in session.text(), session.text()
                # Use once does not carry into the next linked resource or future operation.
                session.send(DOWN); session.send(F9); session.send(ENTER, .5)
                assert 'Authentication Required' in session.text(), session.text()
                choose(session, scope=1)
                assert 'Remember URL root:' in session.text() and '/logs/' in session.text(), session.text()
                replace_field(session, 'https://elsewhere.invalid/logs/'); session.send(ENTER)
                assert 'Scope must be a valid same-origin parent URL' in session.text(), session.text()
                assert (config / 'repositories.toml').read_bytes() == before_config
                session.send(ESC); session.send(ENTER)
                session.send(ENTER); assert 'Save credential scope' in session.text(), session.text()
                session.send(b'y', .5)
                assert 'AUTHENTICATED LOG' in session.text(), session.text()
                saved = (config / 'repositories.toml').read_text()
                assert '[[http_auth_scopes]]' in saved and origin + '/logs/' in saved
                assert all(secret not in saved for secret in ('fixture-pass', 'fixture-wrong', 'auth-test-master'))
                session.send(F11); session.send(F9); session.send(ENTER, .5)
                assert 'AUTHENTICATED LOG' in session.text() and 'Authentication Required' not in session.text(), session.text()
                session.send(F11); session.send(DOWN); session.send(DOWN)
                session.send(F9); session.send(DOWN); session.send(ENTER)
                assert 'Download / Save Copy' in session.text(), session.text()
                replace_field(session, str(left)); session.send(ENTER)
                replace_field(session, 'viewer-download.log'); session.send(ENTER); session.send(ENTER, .5)
                choose(session)
                assert 'Download complete' in session.text(), session.text()
                assert (left / 'viewer-download.log').read_bytes().startswith(b'AUTHENTICATED LOG')
                session.send(ESC)
                assert 'ORIGIN RESOURCE' in session.text() and 'opposite-marker.txt' in session.text(), session.text()
                session.send(DOWN); session.send(F9); session.send(ENTER, .5)
                assert 'AUTHENTICATED LOG' in session.text() and 'Authentication Required' not in session.text(), session.text()
                session.send(F11); assert 'ORIGIN RESOURCE' in session.text(), session.text()
                session.send(ESC)
                # A GET-only challenge during Download retries without losing destination.
                location(session, origin + '/download-only/file.log', download=True)
                assert 'Download / Save Copy' in session.text(), session.text()
                replace_field(session, str(left)); session.send(ENTER)
                replace_field(session, 'get-challenge.log'); session.send(ENTER); session.send(ENTER, .5)
                choose(session)
                assert 'Download complete' in session.text(), session.text()
                assert (left / 'get-challenge.log').read_bytes().startswith(b'AUTHENTICATED LOG')
                session.send(ESC)
                location(session, origin + '/known/job.log')
                assert 'AUTHENTICATED LOG' in session.text() and 'Authentication Required' not in session.text(), session.text()
                session.send(ESC)
                count = len(Handler.requests); location(session, origin + '/unrelated/public.log')
                assert all(auth is None for _, _, auth in Handler.requests[count:])
                session.send(ESC)
                location(session, origin + '/forbidden/job.log')
                assert '403' in session.text() and 'Authentication Required' not in session.text(), session.text()
                # Ctrl+L download retains destination across challenge and retries the transfer.
                location(session, origin + '/newrepo/download.log', download=True)
                # Ctrl+L probes before showing the destination form.
                choose(session)
                confirm_download(session, left, 'downloaded.log')
                assert (left / 'downloaded.log').read_bytes().startswith(b'AUTHENTICATED LOG')
                # Bearer-only challenge filters Basic identifiers out.
                location(session, origin + '/bearer/job.log')
                session.send(ENTER)
                assert 'Work Bearer' in session.text() and 'Work HTTP' not in session.text(), session.text()
                session.send(ENTER); session.send(ENTER); session.send(ENTER); session.send(ENTER, .5)
                assert 'AUTHENTICATED LOG' in session.text(), session.text()
                session.send(ESC)
                # Add Repository uses the normal prepopulated form, then retries original URL.
                location(session, origin + '/newrepo/job.log'); choose(session, scope=2)
                assert 'Name:' in session.text(), session.text()
                replace_field(session, 'Added logs'); session.send(ENTER); session.send(ENTER)
                assert 'Repository Credential' in session.text(), session.text()
                session.send(ENTER)  # keep prepopulated reference
                for _ in range(5): session.send(ENTER)
                assert 'TLS verification disabled' in session.text(), session.text()
                session.send(b'y', .5)
                assert 'AUTHENTICATED LOG' in session.text() and 'Authentication Required' not in session.text(), session.text()
                assert 'Added logs' in (config / 'repositories.toml').read_text()
                session.send(ESC)
                session.quit()
            finally: session.close()
            # Restart: mappings survive, but the vault must still be unlocked normally.
            session = Session(nav, left, right, env)
            try:
                location(session, origin + '/logs/restart.log')
                assert 'Vault locked' in session.text(), session.text()
                session.send(ENTER); session.send(b'auth-test-master'); session.send(ENTER, .6); choose(session)
                assert 'AUTHENTICATED LOG' in session.text(), session.text()
                session.send(ESC)
                session.quit()
            finally: session.close()
        finally:
            for server in servers: server.shutdown(); server.server_close()
    print('HTTP auth PTY: locked vault, wrong credential, use once, remembered root, Repo precedence, Ctrl+L, download, Bearer, 403 and Add Repository passed')

if __name__ == '__main__': main()
