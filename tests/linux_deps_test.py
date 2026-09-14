#!/usr/bin/env python3
"""Verify static curl keeps system/overridden trust and proxy behavior."""
import http.server
import os
import pathlib
import ssl
import subprocess
import sys
import tempfile
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    paths = []
    def do_GET(self):
        self.paths.append(self.path)
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')
    def log_message(self, *args):
        pass

class Server(http.server.ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        # Expected when the negative certificate test terminates the handshake.
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)

def start(server):
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server

def main():
    probe = os.path.abspath(sys.argv[1])
    subprocess.run([probe], check=True)
    env = {k: v for k, v in os.environ.items()
           if k.lower() not in ('http_proxy', 'https_proxy', 'all_proxy', 'no_proxy')
           and k not in ('SSL_CERT_FILE', 'SSL_CERT_DIR')}
    with tempfile.TemporaryDirectory() as temp:
        cert = pathlib.Path(temp, 'ca.pem')
        key = pathlib.Path(temp, 'key.key')
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-days', '1', '-keyout', str(key), '-out', str(cert),
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        server = Server(('127.0.0.1', 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        start(server)
        url = f'https://localhost:{server.server_port}/'
        try:
            rejected = subprocess.run([probe, url], env=env, capture_output=True)
            assert rejected.returncode != 0, 'untrusted TLS certificate was accepted'
            assert b'certificate' in rejected.stderr.lower(), rejected.stderr
            subprocess.run([probe, url], env=dict(env, SSL_CERT_FILE=str(cert)), check=True)
            subprocess.run(['openssl', 'rehash', temp], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run([probe, url], env=dict(env, SSL_CERT_DIR=temp), check=True)
        finally:
            server.shutdown(); server.server_close()
    proxy = start(Server(('127.0.0.1', 0), Handler))
    direct = start(Server(('127.0.0.1', 0), Handler))
    proxy_url = f'http://127.0.0.1:{proxy.server_port}'
    try:
        for variable in ('http_proxy', 'ALL_PROXY'):
            subprocess.run([probe, 'http://navi8or.invalid/proxy-check'],
                           env=dict(env, **{variable: proxy_url}), check=True)
        url = f'http://127.0.0.1:{direct.server_port}/direct-check'
        subprocess.run([probe, url], env=dict(env, http_proxy=proxy_url, NO_PROXY='127.0.0.1'),
                       check=True)
        assert Handler.paths.count('http://navi8or.invalid/proxy-check') == 2
        assert '/direct-check' in Handler.paths, Handler.paths
    finally:
        proxy.shutdown(); proxy.server_close()
        direct.shutdown(); direct.server_close()
    print('Linux dependency trust/proxy tests passed')

if __name__ == '__main__':
    main()
