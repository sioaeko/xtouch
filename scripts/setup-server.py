"""Local XTouch setup: Bambu account login and a USB browser form.

Listen only on loopback. Keep passwords out of logs and tokens only in memory.
Start with: python scripts/setup-server.py --open
"""
from __future__ import annotations

import argparse
import functools
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import threading
import time
import webbrowser

from bambu_login import BambuLogin, LoginError

ROOT = Path(__file__).resolve().parent.parent
SESSION_SECONDS = 3600


class SetupState:
    def __init__(self, factory=BambuLogin):
        self.csrf = secrets.token_urlsafe(32)
        self.sessions = {}
        self.lock = threading.RLock()
        self.factory = factory

    def prune(self):
        with self.lock:
            expired = [key for key, client in self.sessions.items()
                       if time.monotonic() - client.updated_at > SESSION_SECONDS]
            for key in expired:
                self.sessions.pop(key).close()

    def accounts(self):
        self.prune()
        with self.lock:
            return [{"id": key, "email": client.email, "region": client.region}
                    for key, client in self.sessions.items() if client.stage == "authenticated"]

    def lookup(self, key):
        self.prune()
        if not isinstance(key, str):
            raise LoginError("session_expired", 401)
        with self.lock:
            client = self.sessions.get(key)
        if client is None:
            raise LoginError("session_expired", 401)
        client.updated_at = time.monotonic()
        return client

    def action(self, path, body):
        if path == "/api/login":
            self.prune()
            email = body.get("email", "")
            if not isinstance(email, str):
                raise LoginError("invalid_email")
            client = self.factory(email.strip(), body.get("region", "Global"))
            try:
                stage = client.login(body.get("password"))
            except Exception:
                client.close()
                raise
            key = secrets.token_urlsafe(24)
            with self.lock:
                if len(self.sessions) >= 8:
                    oldest = min(self.sessions, key=lambda k: self.sessions[k].updated_at)
                    self.sessions.pop(oldest).close()
                self.sessions[key] = client
            return {"session_id": key, "stage": stage, "email": client.email}
        client = self.lookup(body.get("session_id"))
        with client.lock:
            if path == "/api/verify":
                return {"stage": client.verify(body.get("code"))}
            if path == "/api/resend":
                return {"stage": client.request_code()}
            if path == "/api/devices":
                return {"devices": client.list_devices(), "email": client.email}
            if path == "/api/config":
                return {"mqtt": client.mqtt_config(body.get("serial"))}
            if path == "/api/logout":
                with self.lock:
                    self.sessions.pop(body.get("session_id"), None)
                client.close()
                return {}
        raise LoginError("not_found", 404)


class Handler(BaseHTTPRequestHandler):
    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, **kwargs)

    def log_message(self, *_args):
        pass  # No request/body/account logging.

    def local_request(self):
        port = self.server.server_address[1]
        hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
        origin = self.headers.get("Origin")
        return (self.headers.get("Host") in hosts and
                (origin is None or origin in {"http://" + host for host in hosts}) and
                self.headers.get("Sec-Fetch-Site") != "cross-site")

    def send_body(self, status, data, content_type="application/json; charset=utf-8"):
        encoded = json.dumps(data, ensure_ascii=False).encode() if isinstance(data, dict) else data
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        if not self.local_request():
            self.send_body(403, {"error": "local_only"})
        elif self.path == "/api/bootstrap":
            self.send_body(200, {"helper": True, "csrf": self.state.csrf, "accounts": self.state.accounts()})
        elif self.path in ("/", "/index.html"):
            self.send_body(200, (ROOT / "setup/index.html").read_bytes(), "text/html; charset=utf-8")
        elif self.path == "/favicon.ico":
            self.send_body(204, b"", "image/x-icon")
        else:
            self.send_body(404, {"error": "not_found"})

    def do_POST(self):
        if not self.local_request() or not secrets.compare_digest(self.headers.get("X-XTouch-Setup", ""), self.state.csrf):
            self.send_body(403, {"error": "local_only"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if size < 1 or size > 16384 or self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise LoginError("invalid_request", 400)
            body = json.loads(self.rfile.read(size))
            if not isinstance(body, dict):
                raise LoginError("invalid_request", 400)
            if self.path not in {"/api/login", "/api/verify", "/api/resend", "/api/devices", "/api/config", "/api/logout"}:
                raise LoginError("not_found", 404)
            result = self.state.action(self.path, body)
            self.send_body(200, {"status": "ok", **result})
        except LoginError as error:
            self.send_body(error.http_status, {"error": error.code})
        except (ValueError, TypeError, UnicodeError):
            self.send_body(400, {"error": "invalid_request"})
        except Exception:
            # Do not print or return exception reprs that can contain secrets.
            self.send_body(500, {"error": "setup_failed"})


def make_server(port=8767, state=None):
    return ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, state=state or SetupState()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    server = make_server(args.port)
    print(f"XTouch account setup: http://127.0.0.1:{server.server_port}", flush=True)
    print("Account sessions last one hour and are cleared when this helper exits.", flush=True)
    if args.open:
        webbrowser.open(f"http://127.0.0.1:{server.server_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
