![XTouch](readme-assets/xtouch.png)

# XTouch CYD

**Unofficial CYD fork of XTouch with Bambu Lab Cloud account login and SD-card-free setup.**

This community-maintained fork adapts [XTouch](https://github.com/xperiments-in/xtouch) for the original ESP32 2.8-inch CYD board. Configure Wi-Fi, sign in to your Bambu Lab account using the local PC setup tool, select your printer, and save the settings directly to the board. No SD card or running Home Assistant instance is required.

Based on XTouch, with protocol references from [ha-bambulab](https://github.com/greghesp/ha-bambulab). This fork is not affiliated with or endorsed by Bambu Lab or the original XTouch authors. The original XTouch logo is retained without modification and belongs to its original owner.

## What changed

- **Bambu Lab Cloud login:** email/password login, email verification and authenticator-based two-factor authentication through a local PC helper.
- **SD-card-free setup:** USB configuration from Chrome/Edge, plus an on-device Wi-Fi setup page for manual LAN or Cloud-token configuration.
- **Persistent settings:** Wi-Fi, printer credentials, display settings and touch calibration are stored in internal flash.
- **Cloud status reception:** corrected MQTT report handling and updated broker certificates.
- **CYD display support:** explicit display wiring and touch configuration for the original 2.8-inch board.
- **P1 filament controls:** native command selection based on printer firmware, partial AMS status handling and availability checks.
- **Chamber-light controls:** identify the chamber light by name, update the button from printer reports, and expose USB diagnostics for failed requests.

## Hardware and verification

Target: **ESP32-2432S028R**, original ESP32, 2.8-inch ILI9341 display and XPT2046 resistive touch. This firmware is not intended for ESP32-C3/S3 boards or the XTouch Pro 5-inch screen.

| Area | Status |
| --- | --- |
| CYD boot, display and SD-free configuration | Verified on hardware |
| P1S Cloud login, status and temperature reports | Verified on hardware |
| P1S chamber-light control | Verified through printer reports and user-confirmed touchscreen operation |
| Filament/AMS command generation and state processing | Automated tests passed; physical filament changes not verified |
| Other controls and printer models | Not comprehensively verified |

The current release is **0.9.213-cyd.6**, tested with P1S firmware **01.09.00.00**. Cloud control can respond with a noticeable delay. The original intermittent light failure was not conclusively diagnosed; this revision corrects light-state handling and adds diagnostics.

## Install and configure

Download the firmware ZIP from [Releases](https://github.com/sioaeko/xtouch/releases) and follow the **[English installation guide](docs/p1s-cyd-quickstart.md)**. A [Korean guide](docs/p1s-cyd-quickstart.ko.md) is also available. The setup interface currently uses Korean labels; the English guide explains the main controls.

After flashing the firmware, install Python 3.10+ and run these commands from the extracted package:

```sh
python -m pip install -r scripts/setup-requirements.txt
python scripts/setup-server.py --open
```

On Windows, you can run `start-setup.cmd`. Open `http://127.0.0.1:8767` in Chrome or Edge, connect the USB board, enter your 2.4 GHz Wi-Fi details, sign in, select your printer, and save. The board then connects to Bambu Cloud independently.

**Use this fork's release files.** The original project's web installer installs upstream firmware. Upstream online OTA is disabled in this fork.

## Limitations and credentials

- Passwordless social sign-in is not supported; a Bambu account password is required.
- Cloud tokens are not automatically refreshed. If authentication expires, sign in again using the PC tool and save new settings.
- The Wi-Fi setup page supports manual credentials; email/password login requires the PC helper.
- The filament UI supports external-spool loading and unloading the active spool; it does not provide an arbitrary AMS tray picker.
- Account passwords are used for login and are not stored on the board. The PC helper keeps login sessions in memory. The board stores Wi-Fi credentials and a Cloud token; keep configuration exports and flash backups private.

See [Cloud MQTT and configuration details](docs/cloud-mqtt.md).

## Build and test

Install PlatformIO and Python, then run:

```sh
pio run -e esp32dev
python scripts/test-native.py
python -m unittest discover -s tests -p "test_*.py"
node --test tests/popup.test.cjs tests/setup.test.cjs
python scripts/package-firmware.py
```

The firmware is written to `.pio/build/esp32dev/firmware.bin`; the distributable ZIP is created in `dist/`. The package contains firmware, the PC setup helper, documentation, license notices and checksums. Source for each release is available at its matching Git tag.

## Credits and license

- [XTouch](https://github.com/xperiments-in/xtouch) by its original authors: base firmware, UI and original logo. See the [original documentation](https://github.com/xperiments-in/xtouch#readme) for upstream hardware accessories and project history.
- [ha-bambulab](https://github.com/greghesp/ha-bambulab) contributors: Cloud authentication and MQTT protocol references, including P1 filament commands.
- Other libraries and their notices are listed in [third-party licenses](LICENSE-3RD-PARTY.md).

This fork's software modifications are distributed under **GPLv3**, retaining the original copyright and license notices. See [LICENSE.md](LICENSE.md) and [the GPLv3 text](gpl-3.0.txt). The upstream license file also contains separate OEM and logo terms; the logo is not relicensed by this fork.
