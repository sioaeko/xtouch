"""Bambu login/setup protocol, referenced from ha-bambulab (MIT).

Reference: 0e027ff135a6d9265cb756d3e246747954c76722, bambu_cloud.py.
Passwords are used for one request only. Tokens live in the helper's memory.
"""
from __future__ import annotations

import base64
import json
import re
import threading
import time

import requests


class LoginError(Exception):
    def __init__(self, code: str, http_status: int = 400):
        super().__init__(code)
        self.code = code
        self.http_status = http_status


MODELS = {"C12": "P1S", "P1S": "P1S", "C11": "P1P", "P1P": "P1P",
          "BL-P001": "X1C", "X1 Carbon": "X1C", "X1C": "X1C",
          "3DPrinter-X1-Carbon": "X1C", "X1": "X1", "3DPrinter-X1": "X1"}
HEADERS = {
    "User-Agent": "bambu_network_agent/01.09.05.01",
    "X-BBL-Client-Name": "OrcaSlicer",
    "X-BBL-Client-Type": "slicer",
    "X-BBL-Client-Version": "01.09.05.51",
    "X-BBL-Language": "en-US",
    "X-BBL-OS-Type": "linux",
    "X-BBL-OS-Version": "6.2.0",
    "X-BBL-Agent-Version": "01.09.05.01",
    "X-BBL-Executable-info": "{}",
    "X-BBL-Agent-OS-Type": "linux",
    "Accept": "application/json",
    "Content-Type": "application/json",
}


class BambuLogin:
    def __init__(self, email: str, region: str, http=None):
        if not isinstance(email, str) or not re.fullmatch(r"[^\s@]{1,128}@[^\s@]{1,128}", email):
            raise LoginError("invalid_email")
        if region not in ("Global", "China"):
            raise LoginError("invalid_region")
        self.email, self.region = email, region
        suffix = "cn" if region == "China" else "com"
        self.api = f"https://api.bambulab.{suffix}"
        self.website = f"https://bambulab.{suffix}"
        self.http = http or requests.Session()
        self.stage = "password"
        self.tfa_key = None
        self.token = None
        self.username = None
        self.devices = []
        self.updated_at = time.monotonic()
        self.code_requested_at = 0.0
        self.lock = threading.Lock()

    def close(self):
        self.token = self.username = self.tfa_key = None
        self.devices = []
        self.http.close()

    def _request(self, method, path, payload=None, *, website=False, auth=False, headers=None):
        outgoing = dict(HEADERS)
        if auth:
            if not self.token:
                raise LoginError("login_required", 401)
            outgoing["Authorization"] = f"Bearer {self.token}"
        outgoing.update(headers or {})
        try:
            response = self.http.request(method, (self.website if website else self.api) + path,
                                         json=payload, headers=outgoing, timeout=20,
                                         allow_redirects=False)
        except requests.RequestException:
            raise LoginError("cloud_unreachable", 502) from None
        # Never return upstream response text: it may contain account details.
        if response.status_code in (403, 429):
            if "missing_cookie" in response.text:
                raise LoginError("csrf_required", 502)
            if "cloudflare" in response.text.lower():
                raise LoginError("cloudflare_blocked", 502)
        if response.status_code == 429:
            raise LoginError("rate_limited", 429)
        if response.status_code == 401:
            raise LoginError("login_rejected", 401)
        if response.status_code >= 500:
            raise LoginError("cloud_unreachable", 502)
        if response.status_code >= 300 and response.status_code != 400:
            raise LoginError("login_rejected", 400)
        return response

    @staticmethod
    def _json(response):
        try:
            data = response.json()
        except (ValueError, TypeError):
            raise LoginError("invalid_cloud_response", 502) from None
        if not isinstance(data, dict):
            raise LoginError("invalid_cloud_response", 502)
        return data

    def _accept_token(self, token):
        if not isinstance(token, str) or not token:
            raise LoginError("invalid_cloud_response", 502)
        if len(token.encode("utf-8")) > 2047:
            raise LoginError("token_too_large")
        self.token = token
        username = None
        try:
            payload = token.split(".")[1]
            claims = json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))
            username = claims.get("username")
        except (ValueError, IndexError, TypeError, AttributeError):
            pass
        if not isinstance(username, str) or not username:
            response = self._request("GET", "/v1/design-user-service/my/preference", auth=True)
            data = self._json(response)
            uid = data.get("uid")
            if isinstance(uid, (str, int)) and str(uid):
                username = "u_" + str(uid)
        if not isinstance(username, str) or not re.fullmatch(r"u_[A-Za-z0-9_-]{1,61}", username):
            self.token = None
            raise LoginError("username_unavailable", 502)
        self.username = username
        self.tfa_key = None
        self.stage = "authenticated"

    def _login_result(self, response, code_login=False):
        data = self._json(response)
        if response.status_code == 400:
            if code_login and data.get("code") == 1:
                raise LoginError("code_expired")
            if code_login and data.get("code") == 2:
                raise LoginError("code_incorrect")
            raise LoginError("login_rejected")
        token = data.get("accessToken")
        if token:
            self._accept_token(token)
        elif data.get("loginType") == "verifyCode":
            self.stage = "email_code"
        elif data.get("loginType") == "tfa" and isinstance(data.get("tfaKey"), str):
            self.tfa_key = data["tfaKey"]
            self.stage = "two_factor"
        else:
            raise LoginError("invalid_cloud_response", 502)
        return self.stage

    def login(self, password):
        if not isinstance(password, str) or not password or len(password) > 1024:
            raise LoginError("password_required")
        # Deliberately do not keep a password attribute or log request payloads.
        response = self._request("POST", "/v1/user-service/user/login",
                                 {"account": self.email, "password": password, "apiError": ""})
        return self._login_result(response)

    def request_code(self):
        if self.stage not in ("email_code", "two_factor"):
            raise LoginError("code_not_expected")
        if self.code_requested_at and time.monotonic() - self.code_requested_at < 60:
            raise LoginError("code_rate_limited", 429)
        response = self._request("POST", "/v1/user-service/user/sendemail/code",
                                 {"email": self.email, "type": "codeLogin"})
        if response.status_code == 400:
            raise LoginError("code_request_failed")
        self.code_requested_at = time.monotonic()
        self.stage = "email_code"
        return self.stage

    def verify(self, code):
        if not isinstance(code, str) or not re.fullmatch(r"[0-9]{4,10}", code.strip()):
            raise LoginError("invalid_code")
        code = code.strip()
        if self.stage == "email_code":
            response = self._request("POST", "/v1/user-service/user/login", {"account": self.email, "code": code})
            return self._login_result(response, code_login=True)
        if self.stage != "two_factor":
            raise LoginError("code_not_expected")
        try:
            csrf = self._request("GET", "/api/csrf", website=True)
            csrf_token = csrf.cookies.get("bbl_csrf_token")
            if csrf.status_code != 204 or not csrf_token:
                raise LoginError("csrf_required")
            response = self._request("POST", "/api/sign-in/tfa", website=True,
                                     payload={"tfaKey": self.tfa_key, "tfaCode": code},
                                     headers={"x-bbl-csrf-token": csrf_token,
                                              "Cookie": f"bbl_csrf_token={csrf_token}"})
            if response.status_code == 400:
                raise LoginError("code_incorrect")
            self._accept_token(response.cookies.get("token"))
            return self.stage
        except LoginError as error:
            if error.code != "csrf_required":
                raise
            # Match HA's code-login fallback when the website rejects CSRF.
            return self.request_code()

    def list_devices(self):
        response = self._request("GET", "/v1/iot-service/api/user/bind", auth=True)
        data = self._json(response)
        if response.status_code != 200 or not isinstance(data.get("devices"), list):
            raise LoginError("device_list_failed", 502)
        devices = []
        for item in data["devices"]:
            if not isinstance(item, dict):
                continue
            serial = item.get("dev_id")
            if not isinstance(serial, str) or not re.fullmatch(r"[A-Za-z0-9]{1,15}", serial):
                continue
            model = MODELS.get(item.get("dev_model_name")) or MODELS.get(item.get("dev_product_name"))
            devices.append({"serial": serial, "name": str(item.get("name") or serial)[:120],
                            "model": model or str(item.get("dev_product_name") or "Unknown")[:40],
                            "supported": bool(model), "online": item.get("online") is True})
        self.devices = devices
        return devices  # No LAN access codes or other upstream private fields.

    def mqtt_config(self, serial):
        if self.stage != "authenticated":
            raise LoginError("login_required", 401)
        device = next((item for item in self.devices if item["serial"] == serial), None)
        if not device or not device["supported"]:
            raise LoginError("unsupported_printer")
        return {"mode": "cloud", "region": self.region, "username": self.username,
                "authToken": self.token, "serialNumber": device["serial"], "printerModel": device["model"]}
