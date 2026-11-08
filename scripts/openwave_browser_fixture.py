"""Local HTTP fixture for snapshot-only Browser action tests."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from urllib.parse import urlsplit, parse_qs

PAYLOAD = b'OpenWave browser download fixture 38\n' * 1024


class BrowserFixture:
    def __init__(self):
        self.reports = []
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                print('BROWSER_HTTP', self.command, urlsplit(self.path).path, flush=True)

            def do_GET(self):
                path = urlsplit(self.path)
                status, kind, body = 200, 'text/html; charset=utf-8', b''
                if path.path == '/page':
                    body = b'''<!doctype html><title>OpenWave Browser Actions QA</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<h1>OpenWave Browser Actions QA</h1><a href="/attachment">Download fixture</a>
<script>fetch('/report?stored='+encodeURIComponent(localStorage.getItem('ow-actions-qa')));
localStorage.setItem('ow-actions-qa','stored');</script>'''
                elif path.path == '/attachment':
                    kind, body = 'application/octet-stream', PAYLOAD
                elif path.path == '/report':
                    fixture.reports.append(parse_qs(path.query))
                    kind, body = 'text/plain', b'ok'
                else:
                    status, kind, body = 404, 'text/plain', b'not found'
                self.send_response(status)
                self.send_header('Content-Type', kind)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                if path.path == '/attachment':
                    self.send_header('Content-Disposition', 'attachment; filename="ow-browser-actions-qa.bin"')
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @property
    def url(self):
        return f'http://10.0.2.2:{self.server.server_port}/page'

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
