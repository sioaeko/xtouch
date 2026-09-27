"""Run hardware-independent firmware regression tests with the real ArduinoJson."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = root / ".pio" / "native-tests"
build.mkdir(parents=True, exist_ok=True)
includes = [root / "src", root / "tests/native/stubs",
            root / ".pio/libdeps/esp32dev/ArduinoJson/src"]
if not (includes[-1] / "ArduinoJson.h").exists():
    sys.exit("Run pio run -e esp32dev first to install ArduinoJson.")
sources = [root / "tests/native/core_tests.cpp", root / "src/xtouch/autogrowstream.cpp"]
binary = build / ("core_tests.exe" if os.name == "nt" else "core_tests")
compiler = shutil.which("g++") or shutil.which("clang++")
if compiler:
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", *["-I" + str(p) for p in includes],
                    *map(str, sources), "-o", str(binary)], cwd=build, check=True)
elif os.name == "nt":
    locator = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installs = json.loads(subprocess.check_output([str(locator), "-all", "-products", "*", "-format", "json", "-utf8"], encoding="utf-8"))
    for install in installs:
        setup = Path(install["installationPath"]) / "VC/Auxiliary/Build/vcvars64.bat"
        if setup.exists():
            break
    else:
        sys.exit("Install a C++ compiler (MSVC, GCC, or Clang) to run native tests.")
    args = ["cl", "/nologo", "/EHsc", "/std:c++14", "/W4", "/D_CRT_SECURE_NO_WARNINGS",
            *["/I" + str(p) for p in includes], *map(str, sources), "/Fe:" + str(binary)]
    command = f'call "{setup}" >nul && ' + subprocess.list2cmdline(args)
    subprocess.run(command, shell=True, cwd=build, check=True)
else:
    sys.exit("Install GCC or Clang to run native tests.")
subprocess.run([str(binary)], cwd=root, check=True)
