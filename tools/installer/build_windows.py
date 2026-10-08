#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
BUILD_WINDOWS.PY

Builds HaloCEVitaInstaller.exe: the install helper as one Windows program,
with ffmpeg inside it for the movies. Run on Windows (the GitHub Actions
workflow .github/workflows/windows-installer.yml does), from any folder:

    python -m pip install -r tools/installer/requirements-build.txt
    python tools/installer/build_windows.py            -> tools/installer/dist/HaloCEVitaInstaller.exe
    python tools/installer/build_windows.py --no-ffmpeg   (a smaller program, no movie step without ffmpeg)

ffmpeg is downloaded from one fixed address and checked against its
SHA-256 before anything is taken out of it: gyan.dev's "essentials" build
(GPL v3; its LICENSE and README.txt, which says where its source is, go into
the program next to it). Only bin/ffmpeg.exe, LICENSE and README.txt are
taken out, under fixed names. Nothing downloaded is committed: build/ and
dist/ are ignored.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
FFMPEG_FOLDER = os.path.join(BUILD, "ffmpeg")

# FFmpeg 9.0.2, gyan.dev's Windows build, as GyanD/codexffmpeg publishes it
FFMPEG_URL = "https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip"
FFMPEG_SHA256 = "60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba"
FFMPEG_MEMBERS = {
    "ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe": "ffmpeg.exe",
    "ffmpeg-9.0.2-essentials_build/LICENSE": "LICENSE",
    "ffmpeg-9.0.2-essentials_build/README.txt": "README.txt",
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_ffmpeg():
    os.makedirs(BUILD, exist_ok=True)
    archive = os.path.join(BUILD, "ffmpeg-download.zip")
    if not os.path.isfile(archive) or sha256(archive) != FFMPEG_SHA256:
        print("downloading", FFMPEG_URL, flush=True)
        partial = archive + ".part"
        with urllib.request.urlopen(FFMPEG_URL, timeout=120) as response, open(partial, "wb") as file:
            shutil.copyfileobj(response, file, 1 << 20)
        os.replace(partial, archive)
    found = sha256(archive)
    if found != FFMPEG_SHA256:
        os.remove(archive)
        sys.exit("ffmpeg download has SHA-256 %s, not the pinned %s: refusing it" % (found, FFMPEG_SHA256))
    shutil.rmtree(FFMPEG_FOLDER, ignore_errors=True)
    os.makedirs(FFMPEG_FOLDER)
    with zipfile.ZipFile(archive) as zipped:
        for member, name in FFMPEG_MEMBERS.items():
            with zipped.open(member) as source, open(os.path.join(FFMPEG_FOLDER, name), "wb") as target:
                shutil.copyfileobj(source, target, 1 << 20)
    print("ffmpeg ready in", FFMPEG_FOLDER, flush=True)


def main():
    parser = argparse.ArgumentParser(description="Build HaloCEVitaInstaller.exe with PyInstaller.")
    parser.add_argument("--no-ffmpeg", action="store_true", help="leave ffmpeg out")
    arguments = parser.parse_args()
    if arguments.no_ffmpeg:
        shutil.rmtree(FFMPEG_FOLDER, ignore_errors=True)
    else:
        fetch_ffmpeg()
    environment = dict(os.environ, HCV_FFMPEG_DIR="" if arguments.no_ffmpeg else FFMPEG_FOLDER)
    subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
                    "--distpath", os.path.join(HERE, "dist"), "--workpath", os.path.join(BUILD, "pyinstaller"),
                    os.path.join(HERE, "halo_ce_vita_installer.spec")], check=True, env=environment)
    program = os.path.join(HERE, "dist", "HaloCEVitaInstaller.exe" if sys.platform == "win32"
                           else "HaloCEVitaInstaller")
    print("built", program, "SHA-256", sha256(program))


if __name__ == "__main__":
    main()
