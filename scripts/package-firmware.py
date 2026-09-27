"""Package the CYD firmware locally; never flash a device or publish a release."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = root / ".pio/build/esp32dev"
version = json.loads((root / "version.json").read_text())["version"]
destination = root / "dist" / f"xtouch-cyd-p1s-cloud-{version}"
destination.mkdir(parents=True, exist_ok=True)
cores = [Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio"))), root / ".pio/core"]
for core in cores:
    esptool = core / "packages/tool-esptoolpy/esptool.py"
    boot_app = core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    if esptool.exists() and boot_app.exists():
        break
else:
    sys.exit("PlatformIO ESP32 tools missing. Build with pio run -e esp32dev first.")

for name in ["firmware.bin", "bootloader.bin", "partitions.bin"]:
    shutil.copy2(build / name, destination / name)
shutil.copy2(boot_app, destination / "boot_app0.bin")
subprocess.run([sys.executable, str(esptool), "--chip", "esp32", "merge_bin", "-o",
                str(destination / "xtouch-cyd-full.bin"), "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
                "0x1000", str(destination / "bootloader.bin"),
                "0x8000", str(destination / "partitions.bin"),
                "0xe000", str(destination / "boot_app0.bin"),
                "0x10000", str(destination / "firmware.bin")], check=True)
shutil.copytree(root / "xtouch28", destination / "xtouch28", dirs_exist_ok=True)
shutil.copytree(root / "setup", destination / "setup", dirs_exist_ok=True)
(destination / "scripts").mkdir(exist_ok=True)
for name in ["provision-usb.py", "setup-server.py", "bambu_login.py", "setup-requirements.txt"]:
    shutil.copy2(root / "scripts" / name, destination / "scripts" / name)
shutil.copy2(root / "start-setup.cmd", destination / "start-setup.cmd")
shutil.copy2(root / "docs/examples/xtouch-p1s-cloud.example.json", destination)
shutil.copy2(root / "docs/p1s-cyd-quickstart.ko.md", destination / "README.ko.md")
shutil.copy2(root / "docs/p1s-cyd-quickstart.md", destination / "README.md")
for name in ["LICENSE.md", "LICENSE-3RD-PARTY.md", "gpl-3.0.txt"]:
    shutil.copy2(root / name, destination / name)
files = sorted(p for p in destination.rglob("*") if p.is_file() and p.name != "SHA256SUMS.txt")
hashes = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(destination).as_posix()}" for p in files]
(destination / "SHA256SUMS.txt").write_text("\n".join(hashes) + "\n", encoding="utf-8")
archive = shutil.make_archive(str(destination), "zip", root_dir=destination)
print("Package:", archive)
print("SHA-256:", hashlib.sha256(Path(archive).read_bytes()).hexdigest())
