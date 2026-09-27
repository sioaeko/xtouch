# XTouch CYD installation and setup

This unofficial fork targets the original **ESP32-2432S028R** with a 2.8-inch ILI9341 display and XPT2046 resistive touch. Do not flash it onto an ESP32-C3/S3 or a 5-inch XTouch Pro board.

## 1. Install the firmware

Download and extract this fork's release ZIP. Use a USB data cable and identify the CYD's CH340 serial port. Close serial monitors and browser setup tabs before flashing. Replace `COM_PORT` below with the actual port, such as `COM7`, or the corresponding device path on Linux/macOS.

Install Python 3.10+ and esptool:

```sh
python -m pip install "esptool==4.5.1"
```

For a **first installation**, flash the full image:

```sh
python -m esptool --chip esp32 --port COM_PORT --baud 460800 write_flash 0x0 xtouch-cyd-full.bin
```

The full image overwrites existing boot, partition and NVS data. Back up an existing device before using it.

For an **update from this fork with a verified matching partition layout**, write only the application to preserve settings:

```sh
python -m esptool --chip esp32 --port COM_PORT --baud 460800 write_flash 0x10000 firmware.bin
```

Do not assume that an unrelated firmware uses this partition layout. If automatic download mode fails, hold BOOT, press and release RST, then release BOOT and retry. After flashing, release BOOT and press RST if the application does not start.

## 2. Configure with a Bambu Lab account

No SD card is required. From the extracted package:

```sh
python -m pip install -r scripts/setup-requirements.txt
python scripts/setup-server.py --open
```

On Windows, `start-setup.cmd` also launches the helper. Open **http://127.0.0.1:8767** in Chrome or Edge. The current setup UI uses Korean labels:

1. Choose **USB 보드 연결** (Connect USB board), select the CYD's CH340 port, and wait for the firmware version. A connected cable alone does not establish the browser connection.
2. Enter the board's **2.4 GHz Wi-Fi** name and password.
3. Choose **Bambu Lab 계정 로그인** (Bambu Lab account login), select the account region, and enter the account email and password.
4. Choose **Bambu Lab 로그인** (Log in). Complete email verification or authenticator-based two-factor authentication if requested. Passwordless social accounts are not supported.
5. Select the P1S under **내 프린터** (My printers), then choose **보드에 저장하고 재시작** (Save to board and restart).
6. On first boot, tap the two calibration crosses on the CYD. Calibration and connection settings are stored in internal flash.

The PC helper can be closed after setup. Home Assistant does not need to run; the board connects directly to Bambu Cloud.

A CH340 board may reboot when the browser opens its serial port. Allow up to 35 seconds for the helper to obtain a response. For a port-open error, disconnect other serial monitors or setup tabs. Do not open multiple clients against the same port.

## Manual LAN and Wi-Fi setup

The PC helper also offers **LAN 수동 연결** (Manual LAN connection) and **Cloud 토큰 직접 입력 (고급)** (Manual Cloud token, advanced). LAN configuration requires the printer IP, access code, serial number and model. Both devices must be able to communicate on the local network.

If settings are missing or Wi-Fi fails for 20 seconds, the board creates an **XTouch-Setup-XXXX** network. Connect using the temporary password displayed on the screen and open **http://192.168.4.1**. This page accepts manual LAN or Cloud credentials. Account email/password login is available through the PC helper only. After saving, reconnect your phone to its normal Wi-Fi.

Wi-Fi passwords and Cloud tokens are stored on the board. Account passwords are not stored there. PC login sessions remain in memory, expire after an hour of inactivity, and disappear when the helper exits. Do not share configuration exports or flash backups.

## Connection and recovery

The board connects to Wi-Fi, synchronizes its clock over NTP, and connects to Cloud MQTT. NTP uses UDP 123; MQTT uses TCP 8883. The home screen appears after the first printer status report. The printer must be online in the selected account.

Cloud tokens are not automatically refreshed. If authentication fails, sign in again using the PC helper and save new settings. Upstream online OTA is disabled; use this fork's firmware for updates.

At 115200 baud, `XTOUCH STATUS` reports connection and printer status without returning saved credentials. Light diagnostics include `light_requests`, `light_target`, `light_published` and `light_result`. `sent` confirms transmission; `state_confirmed` confirms a report matching the requested state. The USB-only `XTOUCH LIGHT TOGGLE` command performs a real light action through the same command handler as the touchscreen.

## Verification and limits

Version **0.9.213-cyd.6** was installed on a CYD with its settings preserved. P1S firmware **01.09.00.00** reported Cloud telemetry and confirmed light off/on commands; the user also confirmed physical light changes from the touchscreen. Noticeable Cloud-control delay remains possible. The earlier intermittent failure's cause was not established.

Native tests cover configuration parsing, command envelopes, AMS updates and chamber-light reports. Other actuators, filament changes and other printer models have not been comprehensively verified. The filament screen supports external-spool loading and unloading the active spool, not arbitrary AMS tray selection.
