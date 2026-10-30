# -*- mode: python ; coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-only
# PyInstaller: the install helper as one windowed program,
# HaloCEVitaInstaller.exe. build_windows.py runs it (and fetches ffmpeg into
# build/ffmpeg first, passed in HCV_FFMPEG_DIR); the program finds ffmpeg in
# its bundle's ffmpeg folder. The project's LICENSE goes in, for the License
# page. The icon is drawn by the tool itself (write_icon) into build/.
import os
import sys

here = os.path.abspath(SPECPATH)
ffmpeg_folder = os.environ.get("HCV_FFMPEG_DIR", os.path.join(here, "build", "ffmpeg"))
datas = []
if ffmpeg_folder and os.path.isdir(ffmpeg_folder):
    for name in ("ffmpeg.exe", "ffmpeg", "LICENSE", "README.txt"):
        path = os.path.join(ffmpeg_folder, name)
        if os.path.isfile(path):
            datas.append((path, "ffmpeg"))

# the License page's text: required in the Windows program (its selftest says
# so)
datas.append((os.path.join(here, "..", "..", "LICENSE"), "."))

sys.path.insert(0, here)
import halo_ce_vita_installer  # noqa: E402

icon = os.path.join(here, "build", "HaloCEVitaInstaller.ico")
os.makedirs(os.path.dirname(icon), exist_ok=True)
halo_ce_vita_installer.write_icon(icon)

a = Analysis(
    [os.path.join(here, "halo_ce_vita_installer.py")],
    pathex=[here],
    binaries=[],
    datas=datas,
    hiddenimports=[],
    hookspath=[],
    runtime_hooks=[],
    excludes=["pytest", "pyftpdlib", "unittest", "pydoc", "xmlrpc"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="HaloCEVitaInstaller",
    icon=icon,
    debug=False,
    strip=False,
    upx=False,
    console=False,
    runtime_tmpdir=None,
)
