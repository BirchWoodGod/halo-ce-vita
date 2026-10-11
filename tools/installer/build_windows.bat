@echo off
rem Builds tools\installer\dist\HaloCEVitaInstaller.exe (see build_windows.py).
cd /d "%~dp0"
python -m pip install -r requirements-build.txt || exit /b 1
python build_windows.py %* || exit /b 1
