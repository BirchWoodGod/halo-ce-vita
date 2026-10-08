# -*- mode: python ; coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-only
# PyInstaller: the install helper as one windowed program,
# HaloCEVitaInstaller.exe. build_windows.py runs it (and fetches ffmpeg into
# build/ffmpeg first, passed in HCV_FFMPEG_DIR); the program finds ffmpeg in
# its bundle's ffmpeg folder.
import os

here = os.path.abspath(SPECPATH)
ffmpeg_folder = os.environ.get("HCV_FFMPEG_DIR", os.path.join(here, "build", "ffmpeg"))
datas = []
if ffmpeg_folder and os.path.isdir(ffmpeg_folder):
    for name in ("ffmpeg.exe", "ffmpeg", "LICENSE", "README.txt"):
        path = os.path.join(ffmpeg_folder, name)
        if os.path.isfile(path):
            datas.append((path, "ffmpeg"))

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
    debug=False,
    strip=False,
    upx=False,
    console=False,
    runtime_tmpdir=None,
)
