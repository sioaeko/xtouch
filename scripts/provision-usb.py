"""Configure XTouch over USB without an SD card. Never prints credentials."""
import argparse
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", required=True)
parser.add_argument("--config", type=Path)
parser.add_argument("--validate-only", action="store_true")
parser.add_argument("--setup", action="store_true", help="Open the device's setup access point")
args = parser.parse_args()
if args.config:
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    body = json.dumps(config, ensure_ascii=False, separators=(",", ":"))
    if len(body.encode("utf-8")) > 8192:
        parser.error("configuration is too large")
    command = ("XTOUCH VALIDATE " if args.validate_only else "XTOUCH PROVISION ") + body
elif args.setup:
    command = "XTOUCH SETUP"
else:
    command = "XTOUCH STATUS"

with serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=5) as port:
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    port.reset_input_buffer()
    port.write((command + "\n").encode("utf-8"))
    deadline = time.monotonic() + 25
    buffer = bytearray()
    while time.monotonic() < deadline:
        buffer.extend(port.read(port.in_waiting or 1))
        while b"\n" in buffer:
            line, _, buffer = buffer.partition(b"\n")
            if line.startswith(b"XTOUCH_RESULT "):
                response = json.loads(line[14:])
                # Ephemeral AP password is shown only by explicit --setup/status users.
                print(json.dumps(response, ensure_ascii=False, indent=2))
                raise SystemExit(0 if response.get("status") == "ok" else 1)
    raise SystemExit("No response from XTouch. Close other serial monitors and check the firmware version.")
