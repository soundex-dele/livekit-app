"""Local-only transport integration; no live credentials or external service."""
import http.server
import json
import subprocess
import sys
import threading
import time

class Handler(http.server.BaseHTTPRequestHandler):
    redirected = 0
    headers_ok = True
    grant_attempts = 0
    def log_message(self, *args):
        pass
    def do_GET(self):
        try:
            self.reply()
        except (BrokenPipeError, ConnectionResetError):
            pass
    def reply(self):
        if self.path != "/stats":
            Handler.headers_ok &= self.headers.get("X-Foundation-Request") == "1"
            if self.path == "/guest":
                Handler.headers_ok &= (self.headers.get("Authorization") is None
                                       and self.headers.get("X-AV-Resource-Credential") == "visitor-secret")
            elif self.path != "/api/v1/auth/login":
                Handler.headers_ok &= self.headers.get("Authorization") == "Bearer test-token"
            if self.path in ("/post", "/api/v1/av/rtc-grants/test-grant/credentials"):
                Handler.headers_ok &= self.headers.get("X-Organization-ID") == "org-one"
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/redirected")
            self.end_headers()
            return
        if self.path == "/redirected":
            Handler.redirected += 1
        if self.path == "/slow":
            time.sleep(7)
        value = {"ok": True}
        if self.path == "/stats":
            value = {"redirected": Handler.redirected, "headers_ok": Handler.headers_ok,
                     "grant_attempts": Handler.grant_attempts}
        if self.path == "/error":
            value = {"error": {"code": "test_rejection"}}
        if self.path == "/oversize":
            value = {"text": "x" * 100000}
        if self.path == "/post":
            value = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        status = 403 if self.path == "/error" else 200
        if self.path == "/api/v1/av/rtc-grants/test-grant/credentials":
            Handler.grant_attempts += 1
            if Handler.grant_attempts == 1:
                value, status = {"error": {"code": "media_preparing"}}, 409
            else:
                value = {"media_url": "ws://127.0.0.1", "media_token": "rtc-test-token"}
        data = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        if self.path == "/api/v1/auth/login":
            self.send_header("Set-Cookie", "pf_session=cookie-test-token; HttpOnly; SameSite=Lax")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    do_POST = do_GET

with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}"], timeout=25)
    finally:
        server.shutdown()
    sys.exit(result.returncode)
