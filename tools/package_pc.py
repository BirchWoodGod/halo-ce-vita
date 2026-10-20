#!/usr/bin/env python3
"""Packs the PC build (Linux or Windows) as a release: a folder and its archive

    python3 tools/package_pc.py linux      # dist/halo-ce-vita-<version>-pc-linux.tar.gz
    python3 tools/package_pc.py windows    # dist/halo-ce-vita-<version>-pc-windows.zip

from what `ninja linux` or `ninja windows` built. The release's files are
named (tools/release/README.md, "Release files"):

    halo-ce-vita-<version>-vita.vpk          the Vita's
    halo-ce-vita-<version>-pc-windows.zip    this script, windows
    halo-ce-vita-<version>-pc-linux.tar.gz   this script, linux

The folder keeps the tree's layout, as the game looks for its files there
(port/linux/src/menu_files.c: the menus at <exe>/../../port/vita/app0/menus,
the packs beside them; config.toml, brokers.txt, brokers_crossplay.txt and
relays.txt beside the executable):

    halo.sh (Linux) or halo.bat (Windows) / README.txt / licenses/
    build/linux/halo (or build/windows/halo.exe and SDL3.dll)
    build/<platform>/brokers.txt, brokers_crossplay.txt, relays.txt
    port/vita/app0/menus, lang, packs, shaders.pak, brokers*.txt, relays.txt
    lib/libSDL3.so.0 (Linux; the launcher points LD_LIBRARY_PATH at it)

The PC build plays with the Vitas only when it was configured with
--linux-net-vita (HALO_NET_AS_VITA); a build without it is refused unless
--any-wire is given. The version is the one the build was configured with
(configure.py --vita-version, which the build says it is), else
port/vita/include/vita_version.h's; --version names the files otherwise.
The executable must say the version it is packed as (a build configured
anew and not built again is refused).
"""

import argparse
import os
import re
import shutil
import sys
import tarfile
import zipfile
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent
APP0 = ROOT / "port/vita/app0"
# what goes into port/vita/app0, and what goes beside the executable too
APP0_ITEMS = ("menus", "lang", "packs", "shaders.pak", "brokers.txt", "brokers_crossplay.txt", "relays.txt")
BESIDE_EXECUTABLE = ("brokers.txt", "brokers_crossplay.txt", "relays.txt")
# the notices the binaries' libraries ask for (as tools/ci_build.py's dist/)
LICENSES = {
    "LICENSE.txt": "LICENSE",
    "extract-xiso-LICENSE.txt": "port/third_party/extract-xiso/LICENSE.TXT",
    "mbedtls-LICENSE.txt": "port/third_party/mbedtls/LICENSE",
    "libmspack-COPYING.LIB.txt": "port/third_party/libmspack/COPYING.LIB",
    "miniupnpc-LICENSE.txt": "port/third_party/miniupnpc/LICENSE",
    "tremor-COPYING.txt": "port/third_party/tremor/COPYING",
    "libogg-COPYING.txt": "port/third_party/libogg/COPYING",
}
# where a 32-bit libSDL3.so.0 is looked for (Linux), in this order
SDL_CANDIDATES = (ROOT.parent / "sdl3-i686/lib", ROOT.parent.parent / "sdl3-i686/lib", Path("/usr/lib32"),
                  Path("/usr/lib/i386-linux-gnu"))

LAUNCHER = """#!/bin/sh
# Halo CE Vita {version} (PC build) - start it from anywhere: ./halo.sh
# It needs your Xbox game data (the folder with maps/): put it in this
# folder, or the game asks for an Xbox disc image (.iso/.xiso) the first time.
here=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$here/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
cd "$here" && exec "$here/build/linux/halo" "$@"
"""

# (Windows) started from the package's folder, as halo.sh is, so that the
# game data can sit there too (the data root is also looked for in the
# current folder: port/linux/README.md "Start the game")
BATCH = """@echo off
rem Halo CE Vita {version} (PC build) - double-click this file to play.
rem It needs your Xbox game data (the folder with maps\\): put it in this
rem folder or in build\\windows, or the game asks for an Xbox disc image
rem (.iso/.xiso) the first time.
cd /d "%~dp0"
"%~dp0build\\windows\\halo.exe" %*
"""

README = """Halo CE Vita {version} - PC build ({system})
https://github.com/BirchWoodGod/halo-ce-vita

The PC build of the Vita port. It plays online with the Vitas: your games
show as VITA (PC) in their browsers.

Start: {start}
{needs}
Game data: no game files are included. Put the folder with maps/ from your
own Xbox disc image in this folder, or start the game and pick the disc
image (.iso/.xiso) when it asks: it copies maps/ next to the game. The Halo
PC (Custom Edition) files are optional: they give the PC menus their
pictures and Custom Edition maps (README.md, "Halo PC files").

Controls: keyboard and mouse, or any gamepad.
  W A S D / mouse            move / aim
  left click / right click   fire / grenade (G too)
  Space or Enter             jump, accept
  F, Backspace, mouse 4      melee, back
  E or R                     action, reload
  Tab or mouse wheel         switch weapon
  Q / X                      flashlight / switch grenade
  Left Ctrl or C             crouch
  Z or middle click          zoom
  Escape / F1                pause / scoreboard
  F11 / F12                  fullscreen / free the mouse
In the menus the mouse moves a pointer: left click picks, right click goes
back. The full table is in README.md on GitHub (PC section).

Button icons: the prompts and menus show the keys you use (the keyboard's
icons) and switch to your pad's buttons when you pick one up
(display.button_icons in config.toml: auto, keyboard, xbox or playstation).

Settings: config.toml beside the game ({exe_folder}). The game writes it
the first time with every setting and its help.

Saves and the map cache: {saves}.

For Halo PC's own maps and the PC community's servers, see OpenCE:
https://github.com/OpenCommunityEdition/OpenCE

GPL-3.0-only (licenses/LICENSE.txt); the other notices are in licenses/.
"""


def fail(message: str) -> None:
    print(f"package_pc: {message}", file=sys.stderr)
    sys.exit(1)


def source_version() -> str:
    header = (ROOT / "port/vita/include/vita_version.h").read_text()
    return re.search(r'#define HALO_VITA_VERSION "([^"]+)"', header).group(1)


def configured_version() -> Optional[str]:
    """the version build.ninja compiles the builds as (configure.py
    --vita-version: tools/linux_build.py's version_defines), or None for the
    header's"""
    ninja = ROOT / "build.ninja"
    if not ninja.is_file():
        return None
    versions = set(re.findall(r'-DHALO_VITA_VERSION=\\"([^"\\]+)\\"', ninja.read_text(errors="replace")))
    if len(versions) > 1:
        fail("build.ninja names more than one version: " + ", ".join(sorted(versions)))
    return versions.pop() if versions else None


def plays_as_vita(platform: str) -> bool:
    """whether build.ninja compiles the PC build with HALO_NET_AS_VITA"""
    ninja = ROOT / "build.ninja"
    if not ninja.is_file():
        return False
    text = ninja.read_text(errors="replace")
    # (linux_build.py and windows_build.py put it in their abi variable)
    return "-DHALO_NET_AS_VITA=1" in text and f"build/{platform}/" in text


def find_sdl(option: str) -> Path:
    candidates = [Path(option)] if option else list(SDL_CANDIDATES)
    for folder in candidates:
        library = folder / "libSDL3.so.0" if folder.is_dir() else folder
        if library.is_file():
            return library
    fail("no 32-bit libSDL3.so.0 found (give its folder with --sdl): " + ", ".join(str(c) for c in candidates))
    raise SystemExit(1)


def copy_app0(app0: Path) -> None:
    app0.mkdir(parents=True)
    for item in APP0_ITEMS:
        source = APP0 / item
        if source.is_dir():
            shutil.copytree(source, app0 / item)
        elif source.is_file():
            shutil.copy2(source, app0 / item)
        else:
            fail(f"{source.relative_to(ROOT)} is missing")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("platform", choices=["linux", "windows"])
    parser.add_argument("--version", help="the version the files are named for (default: vita_version.h's)")
    parser.add_argument("--out", default=str(ROOT / "dist"), help="the folder for the package (default: dist/)")
    parser.add_argument("--sdl", default="", help="(linux) the folder holding a 32-bit libSDL3.so.0")
    parser.add_argument("--any-wire", action="store_true",
                        help="pack a build not configured with --linux-net-vita (it plays on OpenCE's side, not with "
                        "the Vitas)")
    parser.add_argument("--no-archive", action="store_true", help="make the folder only")
    args = parser.parse_args()

    built_version = configured_version() or source_version()
    version = args.version or built_version
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?", version):
        fail(f"{version!r} is not a version (x.y.z, x.y.z-beta.n or x.y.z-beta.n-rc)")
    platform = args.platform
    executable = ROOT / ("build/linux/halo" if platform == "linux" else "build/windows/halo.exe")
    if not executable.is_file():
        fail(f"{executable.relative_to(ROOT)} is not built: ninja {platform}")
    # (the window, halo.log and the main menu say "<version> (PC)":
    # vita_version.h's HALO_VITA_VERSION_SHOWN)
    shown = f"{built_version} (PC)".encode()
    if shown not in executable.read_bytes():
        fail(f"{executable.relative_to(ROOT)} does not say {built_version} (PC): configured since it was built? "
             f"ninja {platform}")
    if version != built_version:
        print(f"package_pc: the files named {version}, the build says {built_version}", file=sys.stderr)
    if not args.any_wire and not plays_as_vita(platform):
        fail("this build does not play with the Vitas: configure.py --linux-net-vita, then ninja " + platform +
             " (or --any-wire)")

    name = f"halo-ce-vita-{version}-pc-{platform}"
    out = Path(args.out).resolve()
    folder = out / name
    if folder.exists():
        shutil.rmtree(folder)
    exe_folder = folder / "build" / platform
    exe_folder.mkdir(parents=True)

    shutil.copy2(executable, exe_folder / executable.name)
    copy_app0(folder / "port/vita/app0")
    # (the game reads these beside config.toml, which is beside the executable)
    for item in BESIDE_EXECUTABLE:
        shutil.copy2(APP0 / item, exe_folder / item)
    licenses = folder / "licenses"
    licenses.mkdir()
    for target, source in LICENSES.items():
        shutil.copy2(ROOT / source, licenses / target)

    if platform == "linux":
        lib = folder / "lib"
        lib.mkdir()
        sdl = find_sdl(args.sdl)
        # (the file itself, not a link: an archive of links to nowhere helps no one)
        shutil.copyfile(sdl.resolve(), lib / "libSDL3.so.0")
        launcher = folder / "halo.sh"
        launcher.write_text(LAUNCHER.format(version=version))
        launcher.chmod(0o755)
        (exe_folder / "halo").chmod(0o755)
        readme = README.format(version=version, system="Linux, 32-bit x86", start="./halo.sh",
                               needs="Needs the 32-bit system libraries: glibc, OpenGL (Mesa or your GPU's), "
                                     "PipeWire or PulseAudio\n(lib32-glibc, lib32-mesa, lib32-libpulse on Arch; "
                                     "libc6:i386, libgl1:i386,\nlibpulse0:i386 on Debian and Ubuntu). SDL3 is "
                                     "included in lib/.\n",
                               exe_folder="build/linux", saves="~/.local/share/halo-linux (paths.saves in config.toml)")
    else:
        sdl_dll = ROOT / "build/windows/SDL3.dll"
        if not sdl_dll.is_file():
            fail("build/windows/SDL3.dll is missing: ninja windows")
        shutil.copy2(sdl_dll, exe_folder / "SDL3.dll")
        batch = folder / "halo.bat"
        batch.write_text(BATCH.format(version=version).replace("\n", "\r\n"))
        readme = README.format(version=version, system="Windows, 32-bit x86",
                               start="double-click halo.bat (or build\\windows\\halo.exe)",
                               needs="Windows 10 or later. SDL3.dll is beside halo.exe.\n",
                               exe_folder="build\\windows", saves="%APPDATA%\\halo (paths.saves in config.toml)")
    (folder / "README.txt").write_text(readme if platform == "linux" else readme.replace("\n", "\r\n"))

    print(f"{folder}")
    if args.no_archive:
        return 0
    if platform == "linux":
        archive = out / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(folder, arcname=name)
    else:
        archive = out / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
            for path in sorted(folder.rglob("*")):
                package.write(path, Path(name) / path.relative_to(folder))
    print(f"{archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
