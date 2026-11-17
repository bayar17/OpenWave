"""Local QEMU integration fixtures; observations never contain cookie values.

Run on the host, then browse http://10.0.2.2:8897/ in the test VM.
GET /observations returns cookie names and request outcomes for assertions.
"""
import json
import struct
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PAYLOAD = b"Ocean authenticated native download\n" * 1024
def png_chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


PNG = (b'\x89PNG\r\n\x1a\n'
       + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 24, 24, 8, 2, 0, 0, 0))
       + png_chunk(b'IDAT', zlib.compress((b'\0' + b'\0\xb8\xd1' * 24) * 24))
       + png_chunk(b'IEND', b''))
OBSERVATIONS = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        names = sorted(part.split("=", 1)[0].strip() for part in self.headers.get("Cookie", "").split(";") if "=" in part)
        if self.path == "/observations":
            self.reply(json.dumps(OBSERVATIONS).encode(), "application/json")
            return
        OBSERVATIONS.append({"path": self.path, "cookies": names})
        if self.path in ("/set", "/private-set"):
            self.send_response(200)
            prefix = "private_" if self.path == "/private-set" else ""
            for cookie in (f"{prefix}persistent=fixture; Max-Age=3600; Path=/; SameSite=Lax",
                           f"{prefix}session=fixture; Path=/", f"{prefix}http_only=fixture; Max-Age=3600; Path=/; HttpOnly",
                           f"{prefix}scoped=fixture; Max-Age=3600; Path=/scoped",
                           f"{prefix}expired=fixture; Max-Age=0; Path=/",
                           f"{prefix}secure=fixture; Max-Age=3600; Path=/; Secure",
                           f"{prefix}wrong_domain=fixture; Max-Age=3600; Path=/; Domain=unrelated.example"):
                self.send_header("Set-Cookie", cookie)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(self.page())
        elif self.path == "/auth":
            if "persistent" not in names or "http_only" not in names:
                self.reply(b"Authentication required", "text/plain", 403)
            else:
                self.attachment(PAYLOAD, "report.bin")
        elif self.path == "/file":
            self.attachment(PAYLOAD, "report.bin")
        elif self.path == "/unknown":
            self.attachment(PAYLOAD, "unknown.bin", known=False)
        elif self.path in ("/slow", "/broken"):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", 'attachment; filename="slow.bin"')
            self.send_header("Content-Length", str(4 * 1024 * 1024))
            self.end_headers()
            try:
                for _ in range(64 if self.path == "/slow" else 2):
                    self.wfile.write(b"x" * 65536); self.wfile.flush(); time.sleep(.15)
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
        elif self.path == "/error":
            self.reply(b"Server failure", "text/plain", 500)
        elif self.path == "/picture.png":
            self.reply(PNG, "image/png")
        elif self.path == "/js":
            # Report only names visible to JavaScript; HttpOnly must be absent.
            self.reply(self.page(b"<script>fetch('/js-observed/' + document.cookie.split(';').map(c=>c.split('=')[0].trim()).join(','))</script>"), "text/html")
        else:
            self.reply(self.page(), "text/html")

    def page(self, extra=b""):
        return (b"<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
                b"<title>Ocean Download Fixture</title><style>body{font:24px sans-serif}a{display:block;padding:12px}</style>"
                b"<h1>Native transfer fixture</h1><a href='/auth'>Authenticated file</a>"
                b"<a href='/file'>Ordinary file</a><a href='/unknown'>Unknown length</a>"
                b"<a href='/slow'>Slow transfer</a><a href='/broken'>Interrupted transfer</a>"
                b"<img style='width:200px;height:100px' src='/picture.png'><p>Archive marker: Ocean-archive-fixture</p>" + extra)

    def reply(self, body, mime, code=200):
        self.send_response(code); self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

    def attachment(self, body, name, known=True):
        self.send_response(200); self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition", f'attachment; filename="{name}"')
        if known: self.send_header("Content-Length", str(len(body)))
        self.end_headers(); self.wfile.write(body); self.close_connection = True


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8897), Handler).serve_forever()
