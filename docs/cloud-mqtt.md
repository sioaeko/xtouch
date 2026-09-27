# Bambu Cloud MQTT on XTouch CYD

This revision targets P1S Cloud on the original ESP32 2.8-inch CYD. The existing
local MQTT option remains available. Use [the English quickstart](p1s-cyd-quickstart.md)
for the packaged firmware and installation steps.

## Configuration

Install the PC helper dependencies with `python -m pip install -r scripts/setup-requirements.txt`,
then run `python scripts/setup-server.py --open` (or `start-setup.cmd` on Windows).
Use Python 3.10+ and Chrome/Edge at `http://127.0.0.1:8767`.
Connect the CYD over USB, enter its Wi-Fi settings, sign in with a Bambu email/password,
complete any email or authenticator challenge, select a printer, and save.
Social accounts without a Bambu password are not supported.
The helper also works with the USB protocol in firmware `0.9.213-cyd.2`.

The login flow follows ha-bambulab's login, send-email-code, CSRF/TFA, preference,
and bound-device endpoints. Passwords are sent only to the regional Bambu HTTPS API,
through the loopback helper; they are not stored on disk or sent to the ESP32.
Authenticated accounts can be reused by email while the helper runs. Sessions stay
in memory, expire after one hour without use, and disappear when the helper exits.
This does not read Home Assistant's saved accounts. The helper binds only to loopback,
checks Host/Origin and a per-run request nonce, and does not log request credentials.

Select **LAN manual connection** to enter a printer's IP, access code, serial and model
without Cloud login. **Manual Cloud token** remains available for existing credentials.
The built-in setup AP at `192.168.4.1` supports these two manual methods; email login
requires the PC helper. A plain `python -m http.server` does not provide the login API.

Configuration is stored in internal SPIFFS. The USB browser form and
`scripts/provision-usb.py` can update it. An SD card is optional and its configuration
is imported only if internal configuration is absent. The configuration format is:

```json
{
  "ssid": "your-2.4ghz-wifi",
  "pwd": "your-wifi-password",
  "mqtt": {
    "mode": "cloud",
    "region": "Global",
    "username": "u_123456789",
    "authToken": "your-bambu-cloud-auth-token",
    "serialNumber": "01P00A123456789",
    "printerModel": "P1S"
  }
}
```

The helper obtains these credentials after login. It reads a JWT's `username` claim
or obtains `u_<uid>` from the authenticated preference endpoint for opaque tokens.
Manual setup also accepts the `username` and `auth_token` already used by ha-bambulab.
No token refresh or web login takes place on the ESP32; sign in again and save when
the token expires. Tokens stay in the device's internal flash; they are not returned
by the status endpoint or saved in browser storage. The PC is not needed after setup.

The bundled extension accepts these credentials directly, without a Bambu web
tab. Reading credentials from a signed-in Bambu tab is optional. Downloads are
named `xtouch.json` and contain a single nested `mqtt` object. WiFi whitespace is
preserved. An IP is needed only for HTTP provisioning. Browser storage contains
SSID, device/model and region preferences, never the token or WiFi password.

`region: China` selects `cn.mqtt.bambulab.com`; other regions select
`us.mqtt.bambulab.com`. Port defaults to 8883. Cloud hosts are restricted to
these two TLS-validated names. A HA `host` containing a printer LAN address is
ignored in Cloud mode. P1S/P1P/X1/X1C names map to the legacy model IDs used by
the UI; this build's hardware verification target is P1S.

Internal loading, HTTP, and USB provisioning share normalization/validation. Both accept
legacy flat `cloud-*` fields, HA-style `auth_token`, `serial`, `device_type`, and
`local_mqtt`, plus the original local nested form. Nested fields take precedence
over flat fields; explicit `mode` takes precedence over inference. Credentials
and serials that exceed the firmware's buffers are rejected rather than truncated.

Local example:

```json
{
  "ssid": "your-2.4ghz-wifi",
  "pwd": "your-wifi-password",
  "mqtt": {
    "mode": "local",
    "host": "192.168.0.18",
    "accessCode": "12345678",
    "serialNumber": "01P00A123456789",
    "printerModel": "P1S"
  }
}
```

Internal file priority is `/xtouch.json`, then `/xtouch.json.bak` if the primary is absent, then `/provisioning.json`. Missing WiFi settings or a 20-second connection failure opens a WPA2 setup AP with a fresh password displayed on the screen. USB provisioning stays available during setup and touch calibration. SD import/update runs before display initialization, and releases its SPI host before the LCD starts.

## Connection and recovery

The client uses a CYD MAC-based client ID and a 30-second keepalive. It subscribes
to `device/<serial>/report` and publishes to `device/<serial>/request`, requesting
`get_version` and `pushall` at connect. It enters the main UI only after receiving
a printer status report. After 60 seconds without status it requests `start` and
`pushall`; after another 60 seconds it reconnects. Failures use 1 to 30 seconds of backoff.

Cloud TLS uses DigiCert Global Root G2 with hostname validation and SNTP time
synchronization. Both regional brokers passed a desktop TLS 1.2 probe using only
this bundled root on 2026-09-25. This does not verify ESP32 operation or MQTT login.
Local printer TLS retains the original self-signed-certificate behavior.

Missing settings or rejected MQTT credentials show `Provision at <IP>`, including
a rejection after an earlier successful connection. HTTP POST `/provision` is
writable only while that recovery screen is active. It validates the complete
configuration, writes a temporary file and verifies the serialized length, keeps
the previous configuration as a backup, then schedules restart after the response.
Malformed requests and failed writes do not reboot the screen.

Status parsing uses two read-only filtered passes for regular status and AMS,
bounded string copies, and a 32 KiB receive buffer cap. Oversized/failed buffers
are dropped. These changes avoid corrupting the AMS pass or reading past strings.

This revision disables the abandoned upstream OTA channel. Use SD/USB updates.
A normal build uses the checked-in HMS error table; set `custom_refresh_errors = yes`
only when deliberately refreshing it. Legacy export to a sibling xtouch-bin
repository is opt-in (`custom_export_legacy_artifacts = yes`).

## Build and checks

```text
pio run -e esp32dev
python scripts/test-native.py
node --test tests/popup.test.cjs tests/setup.test.cjs
python -m unittest discover -s tests -p test_cloud_login.py -v
python scripts/check-cloud-tls.py
python scripts/package-firmware.py
```

Native tests use installed GCC/Clang or MSVC and the real ArduinoJson dependency.
The last script writes a ZIP under `dist/` containing the app image, full USB
image, local account setup helper, USB browser form, extension, example configuration,
quickstart, licenses and SHA-256 hashes.
The full USB image is flashed at 0x0; `firmware.bin` is the application/SD image.

The CYD display was confirmed by the user after explicitly forcing `resources/cyd_setup.h` into both app and TFT_eSPI builds. The device boots without SD and USB status/validation/overflow recovery passed. Login fixtures cover password, email code, TFA, CSRF fallback, account reuse and expired sessions. Edge browser checks exercise the email-code and printer-selection screens. On 2026-09-25, the user's real account and P1S passed Cloud MQTT subscription and status requests, and the `cyd.4` device received temperatures and entered its home screen. Full actuator verification, touch interaction, and optional SD import/update remain pending. A successful MQTT publish alone does not prove command execution.

## Controls compared with ha-bambulab

Both projects publish commands directly to the Cloud request topic when configured for Cloud. There is no reason to infer that all controls are blocked simply because a printer has newer firmware; the user's HA installation can already operate this P1S. This checkout retains light, fan, temperature, axis, extrusion, print speed, pause, resume, and stop controls using the same MQTT command families.

`cyd.5` uses `ams_change_filament` for P1 firmware starting at `01.02.99.10`, matching HA's version gate. The connected P1S reported `01.09.00.00`. External-spool loading uses target 254 and the spool profile's temperature midpoint, or 0 when unconfigured, as HA does. Unloading uses target/slot 255 and the active AMS index. Earlier P1 firmware and X1 retain their legacy command path. The CYD filament screen loads the external spool and unloads the active spool; it does not yet offer HA's arbitrary AMS tray picker.

AMS metadata is applied even when a delta omits the tray inventory array. `tray_now`, standalone `ams_status`, and external spool temperature metadata are preserved between reports. Load/unload buttons update at screen creation and on relevant reports, and follow the loaded state and printer/AMS activity. Native tests cover HA command envelopes, the firmware gate, external/AMS unload addressing, and partial status updates. These tests do not actuate the printer.

The `cyd.5` app was installed on the connected CYD on 2026-09-25 with its saved configuration preserved. USB status confirmed P1S firmware `01.09.00.00`, `native_filament_supported: true`, tray 255, load available, unload unavailable, and continued Cloud temperature/status reception. No heating, axis motion, filament change, or print control was triggered during this verification.

## Source reference

Protocol behavior was checked against `greghesp/ha-bambulab` commit
`0e027ff135a6d9265cb756d3e246747954c76722`:

- `custom_components/bambu_lab/pybambu/bambu_client.py`: Cloud credentials, TLS,
  subscription, keepalive, backoff, and initial requests.
- `custom_components/bambu_lab/pybambu/bambu_cloud.py`: regional endpoints, password/email/TFA login, CSRF handling, account UID, device listing and JWT username.
- `custom_components/bambu_lab/config_flow.py`: account reuse, authentication challenge flow and local configuration.
- `custom_components/bambu_lab/pybambu/commands.py`: state requests and light, print, G-code, and filament command envelopes.
- `custom_components/bambu_lab/pybambu/models.py`, `utils.py`, and `custom_components/bambu_lab/coordinator.py`: temperature/fan/axis controls, model/version gates, and external/AMS load and unload addressing.

The reference uses the MIT license; attribution is in LICENSE-3RD-PARTY.md.
