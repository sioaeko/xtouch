@echo off
cd /d "%~dp0"
python scripts\setup-server.py --open
if errorlevel 1 (
  echo.
  echo Install Python requirements with:
  echo python -m pip install -r scripts\setup-requirements.txt
  pause
)
