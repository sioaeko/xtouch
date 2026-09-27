import base64
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from bambu_login import BambuLogin, LoginError

spec = importlib.util.spec_from_file_location("setup_server", Path(__file__).resolve().parent.parent / "scripts/setup-server.py")
server_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server_module)


def token(username="u_123"):
    payload = base64.urlsafe_b64encode(json.dumps({"username": username}).encode()).decode().rstrip("=")
    return "header." + payload + ".signature"


class Response:
    def __init__(self, data=None, status=200, cookies=None, text=""):
        self.data, self.status_code, self.cookies, self.text = data, status, cookies or {}, text

    def json(self):
        return self.data


class Http:
    def __init__(self, *responses):
        self.responses, self.calls, self.closed = list(responses), [], False

    def request(self, method, url, **kwargs):
        self.calls.append((method, url, kwargs))
        if not self.responses:
            raise AssertionError("unexpected upstream request")
        return self.responses.pop(0)

    def close(self):
        self.closed = True


class CloudTests(unittest.TestCase):
    def test_password_login_and_device_selection_do_not_export_lan_secrets(self):
        http = Http(Response({"accessToken": token()}), Response({"devices": [
            {"dev_id": "01P00A123456789", "name": "My P1S", "dev_model_name": "C12", "online": True, "dev_access_code": "secret00"},
            {"dev_id": "TESTH2", "dev_product_name": "H2D", "name": "Unsupported"}]}))
        client = BambuLogin("test@example.com", "Global", http)
        self.assertEqual(client.login("test-password"), "authenticated")
        self.assertNotIn("password", client.__dict__)
        self.assertFalse(http.calls[0][2]["allow_redirects"])
        devices = client.list_devices()
        self.assertNotIn("dev_access_code", json.dumps(devices))
        self.assertEqual(devices[0]["model"], "P1S")
        self.assertFalse(devices[1]["supported"])
        self.assertEqual(client.mqtt_config(devices[0]["serial"])["username"], "u_123")
        with self.assertRaisesRegex(LoginError, "unsupported_printer"):
            client.mqtt_config("TESTH2")
        self.assertNotIn("Authorization", http.calls[0][2]["headers"])
        self.assertEqual(http.calls[1][2]["headers"]["Authorization"], "Bearer " + token())

    def test_email_code_login_and_retries(self):
        http = Http(Response({"loginType": "verifyCode"}), Response({"code": 2}, 400),
                    Response({"code": 1}, 400), Response({"accessToken": token()}))
        client = BambuLogin("test@example.com", "Global", http)
        self.assertEqual(client.login("password"), "email_code")
        for expected in ("code_incorrect", "code_expired"):
            with self.assertRaisesRegex(LoginError, expected):
                client.verify("123456")
            self.assertEqual(client.stage, "email_code")
        self.assertEqual(client.verify("123456"), "authenticated")
        self.assertEqual(http.calls[-1][2]["json"], {"account": "test@example.com", "code": "123456"})

    def test_two_factor_csrf_cookie_and_token_cookie(self):
        http = Http(Response({"loginType": "tfa", "tfaKey": "key"}),
                    Response(status=204, cookies={"bbl_csrf_token": "csrf"}),
                    Response(cookies={"token": token()}))
        client = BambuLogin("test@example.com", "Global", http)
        self.assertEqual(client.login("password"), "two_factor")
        self.assertEqual(client.verify("123456"), "authenticated")
        self.assertEqual(http.calls[-1][1], "https://bambulab.com/api/sign-in/tfa")
        self.assertEqual(http.calls[-1][2]["headers"]["x-bbl-csrf-token"], "csrf")
        self.assertEqual(http.calls[-1][2]["json"], {"tfaKey": "key", "tfaCode": "123456"})

    def test_csrf_rejection_falls_back_to_email_code(self):
        http = Http(Response({"loginType": "tfa", "tfaKey": "key"}),
                    Response(status=204, cookies={"bbl_csrf_token": "csrf"}),
                    Response(status=403, text="missing_cookie"), Response({"message": "success"}))
        client = BambuLogin("test@example.com", "Global", http)
        client.login("password")
        self.assertEqual(client.verify("123456"), "email_code")
        self.assertTrue(http.calls[-1][1].endswith("/sendemail/code"))
        with self.assertRaisesRegex(LoginError, "code_rate_limited"):
            client.request_code()

    def test_china_opaque_token_uses_preference_uid(self):
        http = Http(Response({"accessToken": "opaque-token"}), Response({"uid": 789}))
        client = BambuLogin("test@example.com", "China", http)
        client.login("password")
        self.assertEqual(client.username, "u_789")
        self.assertTrue(all(call[1].startswith("https://api.bambulab.cn/") for call in http.calls))

    def test_cloudflare_and_long_tokens_fail_without_raw_response(self):
        for response, expected in [(Response(status=403, text="cloudflare private-account@example.com"), "cloudflare_blocked"),
                                   (Response({"accessToken": "x" * 2048}), "token_too_large")]:
            with self.subTest(expected=expected):
                client = BambuLogin("test@example.com", "Global", Http(response))
                with self.assertRaisesRegex(LoginError, expected) as error:
                    client.login("password")
                self.assertNotIn("private-account", str(error.exception))

    def test_missing_password_and_unknown_region_never_make_requests(self):
        with self.assertRaisesRegex(LoginError, "invalid_region"):
            BambuLogin("test@example.com", "https://attacker.invalid")
        http = Http()
        client = BambuLogin("test@example.com", "Global", http)
        with self.assertRaisesRegex(LoginError, "password_required"):
            client.login("")
        self.assertEqual(http.calls, [])


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.state = server_module.SetupState(lambda email, region: BambuLogin(email, region, Http(Response({"accessToken": token()}))))
        self.server = server_module.make_server(0, self.state)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = "http://127.0.0.1:" + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, path, data=None, headers=None):
        outgoing = {"Content-Type": "application/json"}
        outgoing.update(headers or {})
        request = Request(self.base + path, data=json.dumps(data).encode() if data is not None else None, headers=outgoing)
        try:
            response = urlopen(request, timeout=3)
        except HTTPError as error:
            response = error
        with response:
            return response.status, json.load(response)

    def test_helper_session_reuse_has_no_tokens_or_passwords_in_bootstrap(self):
        status, bootstrap = self.request("/api/bootstrap")
        self.assertEqual(status, 200)
        status, login = self.request("/api/login", {"email": "test@example.com", "password": "password", "region": "Global"},
                                     {"X-XTouch-Setup": bootstrap["csrf"], "Origin": self.base})
        self.assertEqual(status, 200)
        self.assertEqual(login["stage"], "authenticated")
        _, reused = self.request("/api/bootstrap")
        self.assertEqual(reused["accounts"][0]["email"], "test@example.com")
        self.assertNotIn(token(), json.dumps(reused))
        self.assertNotIn("password", json.dumps(reused))

    def test_cross_origin_rebinding_and_missing_nonce_are_rejected(self):
        for headers in ({"Origin": "https://untrusted.invalid", "X-XTouch-Setup": self.state.csrf},
                        {"Host": "untrusted.invalid", "X-XTouch-Setup": self.state.csrf},
                        {}, {"X-XTouch-Setup": "wrong"}):
            status, _ = self.request("/api/login", {"email": "test@example.com", "password": "password"}, headers)
            self.assertEqual(status, 403)
        self.assertEqual(self.state.sessions, {})

    def test_expired_session_clears_token(self):
        login = self.state.action("/api/login", {"email": "test@example.com", "password": "password", "region": "Global"})
        client = self.state.lookup(login["session_id"])
        client.updated_at = time.monotonic() - 3601
        self.assertEqual(self.state.accounts(), [])
        self.assertIsNone(client.token)
        self.assertTrue(client.http.closed)


if __name__ == "__main__":
    unittest.main()
