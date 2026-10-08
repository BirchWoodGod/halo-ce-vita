#!/usr/bin/env python3
"""Builds the dedicated server's release packages (port/linux/DEDICATED_SERVER.md,
"Download a release build"): one tarball per architecture, as the GitHub
workflow does (.github/workflows/dedicated-server.yml), into dist/:

    python3 tools/package_server.py --version 1.1.0-beta.2
    python3 tools/package_server.py --arch x86 --test-data ~/halo-data

    dist/halo-ce-vita-server-<version>-linux-x86.tar.gz    (32-bit x86 PCs and VPSes)
    dist/halo-ce-vita-server-<version>-linux-armhf.tar.gz  (Raspberry Pi, 32-bit ARM)
    dist/SHA256SUMS

A server built on a new distribution needs that distribution's glibc (one
built on Arch Linux asked for 2.43, and Ubuntu 24.04 has 2.39, Debian 12
2.36). These builds are made against Debian 12 ("bookworm") instead: the
script fetches Debian's own glibc and libgcc packages for the architecture
(pinned, checked against their SHA-256) into a sysroot, builds SDL3 there as
a static, headless library (no video, sound, input or D-Bus: the server
needs only its threads and clock), and compiles the server with clang
against that sysroot. The result needs glibc 2.36 or newer (Debian 12+,
Ubuntu 22.04+, Raspberry Pi OS 12+) and no SDL: its only libraries are
glibc's. The script checks that (no GLIBC_ symbol version above 2.36, no
library outside glibc) and fails the build if it is not so.

The tree is built from a copy of its tracked files (under --work), so the
checkout's own build.ninja and build/ are left alone. What a package holds:
halo-server, README-SERVER.md and DEDICATED_SERVER.md, init.txt.example
and examples/, systemd/ (the units, and the extras for several servers on
one machine), the licences (GPL-3.0 and those of the code linked in, SDL3's
among them) and SHA256SUMS. No game data: a server's owner copies the maps
of their own Xbox game.

Checks (--no-test skips them): each package's server shows its options
(-help) and starts in an empty folder, in Debian 12's userland: the
sysroot run as the root of a bubblewrap sandbox (bwrap), with no network,
or a debian:bookworm container (docker or podman) when bwrap is missing.
The ARM build runs the same way under qemu-arm (on PATH, or Debian 12's
qemu-user-static with --fetch-qemu). --test-data DIR (a folder holding the
game's maps/) also lets each server load its menu and host a game.

Needs: python3, clang and lld (any recent version; the release builds use
Arch Linux's), cmake, ninja, readelf (binutils), and the network for the
first run (the downloads are cached in --work).
"""

import argparse
import gzip
import hashlib
import io
import lzma
import os
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Dict, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent

# glibc and libgcc of Debian 12 (bookworm), the oldest system the packages
# are for: what the server is compiled and linked against. Pinned by
# SHA-256, fetched from Debian's mirror and, once a point release has
# replaced them there, from snapshot.debian.org.
DEBIAN_MIRRORS = (
    "https://deb.debian.org/debian/",
    "https://snapshot.debian.org/archive/debian/20261008T000000Z/",
)
DEBIAN_PACKAGES: Dict[str, List[Tuple[str, str]]] = {
    "i386": [
        ("pool/main/g/glibc/libc6_2.36-9+deb12u14_i386.deb",
         "76b12e06be66ec3fc2c1791d7d8cf34c2b828ed210e3913feeeb8edc9688f820"),
        ("pool/main/g/glibc/libc6-dev_2.36-9+deb12u14_i386.deb",
         "c02c0adacabf53b8f34364940c7c4fe5aedaf921765e915742cc31ffc7a9d1ad"),
        ("pool/main/l/linux/linux-libc-dev_6.1.176-1_i386.deb",
         "94b68e46c4df2da33b443c0f1f3ec74691e71ff54b053f0a6198d7b62d159c5e"),
        ("pool/main/g/gcc-12/libgcc-12-dev_12.2.0-14+deb12u1_i386.deb",
         "d25bdc547dc760f2c48aa808dcd0c4f3b5bd124daa3b47186ee6070ea57d85f1"),
        ("pool/main/g/gcc-12/libgcc-s1_12.2.0-14+deb12u1_i386.deb",
         "56b95a550b342418f6c4765d5c9b08df2c7167134767e44f1f1bc82ced363ece"),
    ],
    "armhf": [
        ("pool/main/g/glibc/libc6_2.36-9+deb12u14_armhf.deb",
         "758c68b92654747025b48476a45c315bf16df1321224abdd9be672bfd120be45"),
        ("pool/main/g/glibc/libc6-dev_2.36-9+deb12u14_armhf.deb",
         "ca9486dc7f2dfb25c61c0c93d561c01f8abdf9866bd4c157f0ccd518295bbbb7"),
        ("pool/main/l/linux/linux-libc-dev_6.1.176-1_armhf.deb",
         "5641c2f8c252bacc210abb9456deb4344357ccf32ca5821cb8ffe3a9a193a1fb"),
        ("pool/main/g/gcc-12/libgcc-12-dev_12.2.0-14+deb12u1_armhf.deb",
         "f4af3b766ae50185d8c94933bfb369266aeb4b9288916d2c9789bf0283f3df5a"),
        ("pool/main/g/gcc-12/libgcc-s1_12.2.0-14+deb12u1_armhf.deb",
         "f58562bf01efd6112b914182147ee149e0d4c90869046e31844da8803669eeb3"),
    ],
}
# (--fetch-qemu: Debian 12's qemu-arm, a static program, to run the ARM
# build's checks on an x86-64 machine)
QEMU_PACKAGE = ("pool/main/q/qemu/qemu-user-static_7.2+dfsg-7+deb12u18+b3_amd64.deb",
                "c3e3ba2bd87f8c5b9a5da5ef21b5a3b82d7c63b89dd448d9ddaa4eabc5b6e402")

# SDL3 as the server on a VPS was first built (a 32-bit, headless SDL)
SDL_VERSION = "3.2.24"
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz"
SDL_SHA256 = "81cc0fc17e5bf2c1754eeca9af9c47a76789ac5efdd165b3b91cbbe4b90bfb76"
# everything SDL could look for on the build machine, off: the server uses
# SDL's threads, timers and events, with the dummy video and audio drivers
SDL_CMAKE_OPTIONS = [
    "-DSDL_STATIC=ON", "-DSDL_SHARED=OFF", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF",
    "-DSDL_INSTALL_DOCS=OFF", "-DSDL_UNIX_CONSOLE_BUILD=ON", "-DSDL_DEPS_SHARED=OFF",
    "-DSDL_X11=OFF", "-DSDL_WAYLAND=OFF", "-DSDL_KMSDRM=OFF", "-DSDL_OFFSCREEN=OFF",
    "-DSDL_OPENGL=OFF", "-DSDL_OPENGLES=OFF", "-DSDL_VULKAN=OFF", "-DSDL_GPU=OFF", "-DSDL_RENDER_GPU=OFF",
    "-DSDL_RENDER_VULKAN=OFF", "-DSDL_CAMERA=OFF",
    "-DSDL_PULSEAUDIO=OFF", "-DSDL_PIPEWIRE=OFF", "-DSDL_ALSA=OFF", "-DSDL_JACK=OFF", "-DSDL_SNDIO=OFF",
    "-DSDL_OSS=OFF", "-DSDL_HIDAPI=OFF", "-DSDL_HIDAPI_LIBUSB=OFF", "-DSDL_DBUS=OFF", "-DSDL_IBUS=OFF",
    "-DSDL_LIBUDEV=OFF", "-DSDL_LIBURING=OFF", "-DSDL_ASAN=OFF",
]

# the newest glibc symbol version the packages may need: Debian 12's
GLIBC_MAX = (2, 36)
# the only libraries the server may need: glibc's (and libgcc's, which every
# glibc system has)
ALLOWED_NEEDED = re.compile(r"^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|"
                            r"libgcc_s\.so\.1|ld-linux\.so\.2|ld-linux-armhf\.so\.3)$")


class Arch:
    def __init__(self, name: str, debian: str, triple: str, multiarch: str, description: str,
                 interpreter: str, qemu: Optional[str]):
        self.name = name                # the package's name for it
        self.debian = debian            # Debian's architecture
        self.triple = triple            # clang's target
        self.multiarch = multiarch      # Debian's library folder
        self.description = description
        self.interpreter = interpreter  # the dynamic loader the program names
        self.qemu = qemu                # what runs it on an x86-64 machine


ARCHES = {
    "x86": Arch("x86", "i386", "i686-linux-gnu", "i386-linux-gnu",
                "32-bit x86 (i686), for x86-64 PCs and VPSes with the 32-bit glibc",
                "/lib/ld-linux.so.2", None),
    "armhf": Arch("armhf", "armhf", "arm-linux-gnueabihf", "arm-linux-gnueabihf",
                  "32-bit ARM (armhf, ARMv8 tuned for the Raspberry Pi 4), for a Raspberry Pi",
                  "/lib/ld-linux-armhf.so.3", "qemu-arm"),
}


def log(text: str) -> None:
    print(text, flush=True)


def run(command: Sequence, cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None,
        quiet_log: Optional[Path] = None) -> None:
    printable = " ".join(str(part) for part in command)
    log(f"+ {printable}" + (f"  (> {quiet_log})" if quiet_log else ""))
    if quiet_log:
        with open(quiet_log, "w") as output:
            result = subprocess.run([str(part) for part in command], cwd=cwd, env=env, stdout=output,
                                    stderr=subprocess.STDOUT)
        if result.returncode != 0:
            sys.stdout.write(quiet_log.read_text(errors="replace")[-6000:])
            raise SystemExit(f"failed ({result.returncode}): {printable}")
    else:
        subprocess.run([str(part) for part in command], cwd=cwd, env=env, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(urls: Sequence[str], expected: str, destination: Path) -> Path:
    """a file, from the first of urls that has it, checked against its
    SHA-256 (and kept: a later run reuses it)"""
    if destination.exists() and sha256(destination) == expected:
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    errors = []
    for url in urls:
        partial = destination.with_name(destination.name + ".part")
        try:
            log(f"fetching {url}")
            request = urllib.request.Request(url, headers={"User-Agent": "halo-ce-vita-package-server"})
            with urllib.request.urlopen(request, timeout=120) as response, open(partial, "wb") as output:
                shutil.copyfileobj(response, output)
        except OSError as error:
            errors.append(f"{url}: {error}")
            partial.unlink(missing_ok=True)
            continue
        actual = sha256(partial)
        if actual != expected:
            errors.append(f"{url}: SHA-256 {actual}, expected {expected}")
            partial.unlink()
            continue
        partial.rename(destination)
        return destination
    raise SystemExit("cannot fetch " + destination.name + ":\n  " + "\n  ".join(errors))


def deb_data(deb: Path) -> tarfile.TarFile:
    """a Debian package's data.tar (the ar archive's member)"""
    blob = deb.read_bytes()
    if not blob.startswith(b"!<arch>\n"):
        raise SystemExit(f"{deb}: not a Debian package")
    offset = 8
    while offset + 60 <= len(blob):
        header = blob[offset:offset + 60]
        name = header[:16].decode().strip().rstrip("/")
        size = int(header[48:58].decode().strip())
        body = blob[offset + 60:offset + 60 + size]
        offset += 60 + size + (size & 1)
        if name.startswith("data.tar"):
            if name.endswith(".xz"):
                body = lzma.decompress(body)
            elif name.endswith(".gz"):
                body = gzip.decompress(body)
            elif not name.endswith(".tar"):
                raise SystemExit(f"{deb}: {name} is not compressed in a way this script reads")
            return tarfile.open(fileobj=io.BytesIO(body))
    raise SystemExit(f"{deb}: no data.tar")


def extract_deb(deb: Path, root: Path) -> None:
    """a Debian package's files under root, as dpkg would put them under /;
    absolute symbolic links become relative ones, so they stay in root"""
    with deb_data(deb) as archive:
        for member in archive.getmembers():
            name = PurePosixPath(member.name.lstrip("./").lstrip("/"))
            if not name.parts or ".." in name.parts:
                continue
            target = root / name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink() or target.exists():
                target.unlink()
            if member.issym():
                link = member.linkname
                if link.startswith("/"):
                    link = os.path.relpath(link.lstrip("/"), str(name.parent))
                target.symlink_to(link)
            elif member.islnk():
                source = root / PurePosixPath(member.linkname.lstrip("./").lstrip("/"))
                shutil.copy2(source, target)
            elif member.isfile():
                with archive.extractfile(member) as stream, open(target, "wb") as output:
                    shutil.copyfileobj(stream, output)
                os.chmod(target, member.mode & 0o755)


def sysroot(arch: Arch, work: Path) -> Path:
    """Debian 12's glibc, kernel headers and libgcc for arch, in a folder"""
    root = work / f"sysroot-{arch.debian}"
    stamp = root / ".packages"
    wanted = "\n".join(sha for _, sha in DEBIAN_PACKAGES[arch.debian])
    if stamp.exists() and stamp.read_text() == wanted:
        return root
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    for path, expected in DEBIAN_PACKAGES[arch.debian]:
        deb = fetch([mirror + path for mirror in DEBIAN_MIRRORS], expected,
                    work / "downloads" / PurePosixPath(path).name)
        extract_deb(deb, root)
    # (bookworm's libc6 is in /lib, its -dev half in /usr/lib: clang looks in both)
    stamp.write_text(wanted)
    return root


def sdl_target_flags(arch: Arch) -> List[str]:
    """SDL's target: the game's ABI (tools/linux_build.py, tools/linux_armhf_cc.sh),
    for ARM as ARMv7 (SDL 3.2 takes an ARMv8 target in 32-bit mode for
    AArch64, whose NEON functions 32-bit ARM lacks; the server does not
    use SDL's NEON code anyway)"""
    if arch.name == "x86":
        return [f"--target={arch.triple}", "-m32", "-march=x86-64"]
    return [f"--target={arch.triple}", "-march=armv7-a", "-mfpu=neon", "-mfloat-abi=hard", "-mthumb"]


def build_sdl(arch: Arch, work: Path, root: Path, clang: str) -> Path:
    """SDL3, static and headless, built against the sysroot"""
    prefix = work / f"sdl3-{arch.name}"
    stamp = prefix / ".built"
    key = f"{SDL_VERSION} {' '.join(SDL_CMAKE_OPTIONS)} {' '.join(sdl_target_flags(arch))} {clang}"
    if stamp.exists() and stamp.read_text() == key:
        return prefix
    archive = fetch([SDL_URL], SDL_SHA256, work / "downloads" / f"SDL3-{SDL_VERSION}.tar.gz")
    source = work / f"SDL3-{SDL_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(work, filter="data")
    build = work / f"sdl3-build-{arch.name}"
    if build.exists():
        shutil.rmtree(build)
    if prefix.exists():
        shutil.rmtree(prefix)
    build.mkdir(parents=True)
    flags = " ".join(sdl_target_flags(arch))
    toolchain = build / "toolchain.cmake"
    toolchain.write_text(f"""set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR {"i686" if arch.name == "x86" else "armv7l"})
set(CMAKE_SYSROOT "{root}")
set(CMAKE_C_COMPILER "{clang}")
set(CMAKE_C_COMPILER_TARGET {arch.triple})
set(CMAKE_CXX_COMPILER "{clang}++")
set(CMAKE_CXX_COMPILER_TARGET {arch.triple})
set(CMAKE_C_FLAGS_INIT "{flags}")
set(CMAKE_CXX_FLAGS_INIT "{flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
""")
    env = dict(os.environ)
    # (nothing of the build machine's: no pkg-config file is found)
    empty = build / "no-pkgconfig"
    empty.mkdir()
    env["PKG_CONFIG_LIBDIR"] = str(empty)
    env["PKG_CONFIG_PATH"] = ""
    env["PKG_CONFIG_SYSROOT_DIR"] = str(root)
    run(["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
         *SDL_CMAKE_OPTIONS], env=env, quiet_log=build / "configure.log")
    run(["cmake", "--build", build], env=env, quiet_log=build / "build.log")
    run(["cmake", "--install", build], env=env, quiet_log=build / "install.log")
    shutil.copy2(source / "LICENSE.txt", prefix / "LICENSE.txt")
    stamp.write_text(key)
    return prefix


def copy_tree(work: Path, arch: Arch) -> Path:
    """the tracked files of the checkout (as they are in it), in a folder of
    their own to configure and build in"""
    tree = work / f"src-{arch.name}"
    files = subprocess.run(["git", "-C", ROOT, "ls-files", "-z", "--cached"], check=True,
                           capture_output=True).stdout.decode().split("\0")
    wanted = set()
    for name in files:
        if not name:
            continue
        source = ROOT / name
        if not source.exists() and not source.is_symlink():
            continue  # (deleted in the checkout)
        wanted.add(name)
        target = tree / name
        if target.exists() and not target.is_symlink() and not source.is_symlink():
            source_stat = source.stat()
            target_stat = target.stat()
            if source_stat.st_size == target_stat.st_size and source_stat.st_mtime_ns == target_stat.st_mtime_ns:
                continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink() or target.exists():
            target.unlink()
        if source.is_symlink():
            target.symlink_to(os.readlink(source))
        else:
            shutil.copy2(source, target)
    # (a file the checkout no longer has goes from the copy too, except the
    # build's own)
    for path in sorted(tree.rglob("*"), reverse=True):
        relative = path.relative_to(tree).as_posix()
        if relative.split("/")[0] in ("build", ".ninja_log", ".ninja_deps", "build.ninja", "objdiff.json",
                                      "compile_commands.json"):
            continue
        if (path.is_file() or path.is_symlink()) and relative not in wanted:
            path.unlink()
    return tree


def build_server(arch: Arch, work: Path, root: Path, sdl: Path, clang: str, jobs: Optional[int]) -> Path:
    """build/linux/halo-server, built in a copy of the tree against the sysroot"""
    tree = copy_tree(work, arch)
    wrapper = work / f"cc-{arch.name}"
    if arch.name == "x86":
        # (the game's commands already name i686 and -m32)
        wrapper.write_text(f"""#!/bin/sh
# the dedicated server's x86 release build (tools/package_server.py): the
# game's clang command against Debian 12's glibc and a static, headless SDL3
exec "{clang}" --sysroot="{root}" -I"{sdl}/include" -L"{sdl}/lib" -fuse-ld=lld -Qunused-arguments "$@"
""")
    else:
        wrapper.write_text(f"""#!/bin/sh
# the dedicated server's armhf release build (tools/package_server.py):
# tools/linux_armhf_cc.sh against Debian 12's glibc and a static, headless SDL3
ARMHF_SYSROOT="{root}" SDL3_ARMHF="{sdl}" CLANG="{clang}" exec "{tree}/tools/linux_armhf_cc.sh" "$@"
""")
    wrapper.chmod(0o755)
    configure = [sys.executable, "configure.py", "--linux-cc", wrapper, "--portable", "--release",
                 "--lto", "off", "--pgo", "off"]
    launcher = os.environ.get("CI_COMPILER_LAUNCHER")
    if launcher:
        configure += ["--compiler-launcher", launcher]
    run(configure, cwd=tree, quiet_log=work / f"configure-{arch.name}.log")
    ninja = ["ninja", "linux-server"] + ([f"-j{jobs}"] if jobs else [])
    run(ninja, cwd=tree, quiet_log=work / f"build-{arch.name}.log")
    return tree / "build" / "linux" / "halo-server"


def readelf(binary: Path, option: str) -> str:
    return subprocess.run(["readelf", "-W", option, binary], check=True, capture_output=True,
                          text=True).stdout


def check_binary(arch: Arch, binary: Path) -> Dict[str, object]:
    """what the program needs from the system it runs on: its libraries and
    glibc's symbol versions (fails above GLIBC_MAX or outside glibc)"""
    header = readelf(binary, "-h")
    machine = "Intel 80386" if arch.name == "x86" else "ARM"
    if machine not in header or "ELF32" not in header:
        raise SystemExit(f"{binary}: not a 32-bit {machine} program")
    dynamic = readelf(binary, "-d")
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dynamic)
    stray = [library for library in needed if not ALLOWED_NEEDED.match(library)]
    if stray:
        raise SystemExit(f"{binary}: needs libraries beyond glibc's: {', '.join(stray)}")
    if re.search(r"\((RPATH|RUNPATH)\)", dynamic):
        raise SystemExit(f"{binary}: has an rpath, which a release build should not need")
    interpreter = re.search(r"Requesting program interpreter: ([^\]]+)\]", readelf(binary, "-l"))
    if not interpreter or interpreter.group(1) != arch.interpreter:
        raise SystemExit(f"{binary}: its dynamic loader is {interpreter and interpreter.group(1)}, "
                         f"expected {arch.interpreter}")
    versions = sorted({tuple(int(part) for part in match.split("."))
                       for match in re.findall(r"Name: GLIBC_([0-9.]+)", readelf(binary, "-V"))})
    if not versions:
        raise SystemExit(f"{binary}: no glibc symbol versions found")
    if versions[-1] > GLIBC_MAX:
        raise SystemExit(f"{binary}: needs GLIBC_{'.'.join(map(str, versions[-1]))}, newer than "
                         f"{'.'.join(map(str, GLIBC_MAX))} (Debian 12)")
    newest = ".".join(map(str, versions[-1]))
    log(f"{arch.name}: needs {', '.join(needed)}; newest glibc symbol version GLIBC_{newest}; "
        f"loader {arch.interpreter}")
    return {"needed": needed, "glibc": newest}


# ---------- the package

def version_default() -> str:
    """the tag being built (v1.1.0-beta.2 -> 1.1.0-beta.2), else the
    release this tree is (port/vita/include/vita_version.h)"""
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/v"):
        return ref[len("refs/tags/v"):]
    header = (ROOT / "port/vita/include/vita_version.h").read_text()
    return re.search(r'#define HALO_VITA_VERSION "([^"]+)"', header).group(1)


def source_date_epoch() -> int:
    if os.environ.get("SOURCE_DATE_EPOCH", "").isdigit():
        return int(os.environ["SOURCE_DATE_EPOCH"])
    result = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%ct"], capture_output=True, text=True)
    return int(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else 0


def server_extras() -> List[Tuple[Path, str, int]]:
    """(source, path in the package, mode) of the package's files beside the
    program: its documents, examples, systemd units and licences. The units
    are what port/linux has (halo-server.service; halo-server@.service and
    tools/halo-servers for several servers on one machine, where the tree has
    them), and systemd/netns/ the network namespace set-up of the first VPS
    (port/linux/server-package/netns)."""
    package = ROOT / "port/linux/server-package"
    files: List[Tuple[Path, str, int]] = [
        (package / "README-SERVER.md", "README-SERVER.md", 0o644),
        (ROOT / "port/linux/DEDICATED_SERVER.md", "DEDICATED_SERVER.md", 0o644),
        (package / "init.txt.example", "init.txt.example", 0o644),
        (ROOT / "LICENSE", "LICENSE", 0o644),
    ]
    for example in sorted((package / "examples").glob("*.txt")):
        files.append((example, f"examples/{example.name}", 0o644))
    for unit in sorted((ROOT / "port/linux").glob("*.service")):
        files.append((unit, f"systemd/{unit.name}", 0o644))
    halo_servers = ROOT / "tools/halo-servers"
    if halo_servers.exists():
        files.append((halo_servers, "systemd/halo-servers", 0o755))
    for extra in sorted((package / "netns").iterdir()):
        files.append((extra, f"systemd/netns/{extra.name}", 0o755 if extra.suffix == ".sh" else 0o644))
    # the licences of what is linked into the program
    for folder in sorted((ROOT / "port/third_party").iterdir()):
        for notice in sorted(folder.iterdir()):
            if re.match(r"(LICEN[CS]E|COPYING|COPYRIGHT)", notice.name, re.IGNORECASE) and notice.is_file():
                stem = notice.name.split(".")[0]
                files.append((notice, f"licenses/{folder.name}-{stem}.txt", 0o644))
    return files


def write_package(arch: Arch, version: str, binary: Path, sdl: Path, dist: Path, epoch: int) -> Path:
    """the tarball: <name>/halo-server and the rest, owned by root, dated
    SOURCE_DATE_EPOCH, in a fixed order (the same input gives the same bytes)"""
    name = f"halo-ce-vita-server-{version}-linux-{arch.name}"
    stage = dist / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    entries: List[Tuple[str, Path, int]] = [("halo-server", binary, 0o755)]
    for source, path, mode in server_extras():
        entries.append((path, source, mode))
    entries.append(("licenses/SDL3-LICENSE.txt", sdl / "LICENSE.txt", 0o644))
    for path, source, mode in entries:
        target = stage / path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(mode)
    sums = "".join(f"{sha256(stage / path)}  {path}\n" for path, _, _ in sorted(entries))
    (stage / "SHA256SUMS").write_text(sums)

    tarball = dist / f"{name}.tar.gz"
    paths = sorted(p for p in stage.rglob("*"))
    with open(tarball, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch,
                                                   compresslevel=9) as compressed, \
            tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as tar:
        def add(path: Path, arcname: str) -> None:
            info = tar.gettarinfo(str(path), arcname)
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = epoch
            info.pax_headers = {}
            if info.isdir():
                info.mode = 0o755
                tar.addfile(info)
            else:
                with open(path, "rb") as stream:
                    tar.addfile(info, stream)
        add(stage, name)
        for path in paths:
            add(path, f"{name}/{path.relative_to(stage).as_posix()}")
    return tarball


# ---------- the checks

def find_qemu(arch: Arch, work: Path, fetch_it: bool) -> Optional[str]:
    for candidate in (f"{arch.qemu}-static", arch.qemu):
        if shutil.which(candidate):
            return shutil.which(candidate)
    if not fetch_it:
        return None
    target = work / "qemu" / "usr/bin/qemu-arm-static"
    if not target.exists():
        path, expected = QEMU_PACKAGE
        deb = fetch([mirror + path for mirror in DEBIAN_MIRRORS], expected,
                    work / "downloads" / PurePosixPath(path).name)
        with deb_data(deb) as archive:
            member = archive.getmember("./usr/bin/qemu-arm-static")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as stream, open(target, "wb") as output:
                shutil.copyfileobj(stream, output)
        target.chmod(0o755)
    return str(target)


class Runner:
    """runs the packaged server in Debian 12's userland: the sysroot as the
    root of a bwrap sandbox (no network), or a debian:bookworm container"""

    def __init__(self, arch: Arch, root: Path, qemu: Optional[str]):
        self.arch = arch
        self.root = root
        self.qemu = qemu
        self.engine = None
        self.containers = 0
        self.container = ""
        if shutil.which("bwrap") and subprocess.run(["bwrap", "--ro-bind", "/", "/", "true"]).returncode == 0:
            self.engine = "bwrap"
            # (the mount points, made in the sysroot, which is mounted read-only)
            for folder in ("proc", "dev", "tmp", "srv/package", "srv/server"):
                (root / folder).mkdir(parents=True, exist_ok=True)
            (root / "qemu-arm").touch()
        else:
            for engine in ("podman", "docker"):
                if shutil.which(engine):
                    self.engine = engine
                    break

    def available(self) -> Optional[str]:
        if not self.engine:
            return "neither bwrap nor docker/podman is available"
        if self.arch.qemu and not self.qemu:
            return "no qemu-arm to run an ARM program (on PATH, or --fetch-qemu)"
        return None

    def command(self, package: Path, folder: Path, arguments: List[str], maps: Optional[Path] = None) -> List[str]:
        """the command that runs the packaged server (package: its folder)
        with folder as its own, and maps (read-only) as its maps/"""
        program = ["/srv/package/halo-server", *arguments]
        if self.engine == "bwrap":
            if self.qemu:
                # (qemu-arm is a static program: it runs in Debian's root as is,
                # and finds the ARM loader there)
                program = ["/qemu-arm", *program]
            command = ["bwrap", "--ro-bind", str(self.root), "/", "--proc", "/proc", "--dev", "/dev",
                       "--tmpfs", "/tmp", "--ro-bind", str(package), "/srv/package",
                       "--bind", str(folder), "/srv/server"]
            if maps:
                command += ["--ro-bind", str(maps), "/srv/server/maps"]
            if self.qemu:
                command += ["--ro-bind", self.qemu, "/qemu-arm"]
            command += ["--chdir", "/srv/server", "--unshare-net", "--unshare-pid", "--unshare-ipc",
                        "--die-with-parent", "--clearenv", "--setenv", "HOME", "/srv/server",
                        "--setenv", "PATH", "/usr/bin:/bin"]
            return command + program
        # (a container: Debian 12 with the 32-bit glibc added, as the
        # package's README says; the network only for apt)
        image = "docker.io/library/debian:bookworm"
        setup = (f"dpkg --add-architecture {self.arch.debian} && apt-get update -qq >/dev/null && "
                 f"apt-get install -y -qq libc6:{self.arch.debian} >/dev/null && ")
        if self.arch.qemu:
            setup += "apt-get install -y -qq qemu-user-static >/dev/null && "
            program = ["qemu-arm-static", *program]
        volumes = ["-v", f"{package}:/srv/package:ro", "-v", f"{folder}:/srv/server"]
        if maps:
            volumes += ["-v", f"{maps}:/srv/server/maps:ro"]
        # (named, so that stop() ends it: the server may ignore SIGTERM while
        # it shows an error, and a container outlives its client)
        self.containers += 1
        self.container = f"halo-server-check-{os.getpid()}-{self.containers}"
        return [self.engine, "run", "--rm", "-i", "--name", self.container, *volumes, "-w", "/srv/server",
                image, "sh", "-c", setup + "exec timeout -k 5 600 " + " ".join(program)]

    def stop(self, process: subprocess.Popen) -> None:
        """stops a server started with command(): SIGTERM, then SIGKILL"""
        if self.engine != "bwrap" and process.poll() is None:
            subprocess.run([self.engine, "kill", "--signal", "TERM", self.container], capture_output=True)
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                subprocess.run([self.engine, "kill", self.container], capture_output=True)
        if process.poll() is None:
            # (bwrap: its sandbox, and every process in it, ends with it)
            process.terminate()
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def run_checks(arch: Arch, root: Path, stage: Path, work: Path, qemu: Optional[str],
               test_data: Optional[Path], seconds: int) -> List[str]:
    """-help, and a start in an empty folder (and with --test-data, a start
    with the game's maps that reaches its menu and hosts); returns what was
    checked, for the summary"""
    runner = Runner(arch, root, qemu)
    reason = runner.available()
    if reason:
        log(f"{arch.name}: not run ({reason})")
        return [f"not run: {reason}"]
    results = []
    scratch = Path(tempfile.mkdtemp(prefix=f"server-check-{arch.name}-", dir=work))
    try:
        command = runner.command(stage, scratch, ["-help"])
        log("+ " + " ".join(command))
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        if result.returncode != 0 or "halo-server [-path DIR]" not in result.stdout:
            raise SystemExit(f"{arch.name}: -help failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        log(f"{arch.name}: -help ok in Debian 12 ({runner.engine}{', qemu-arm' if arch.qemu else ''})")
        results.append(f"-help ok ({runner.engine}{', qemu-arm' if arch.qemu else ''})")

        # a start: an empty folder (the server says the maps are missing and
        # stops), or the game's maps (it loads its menu and hosts; stopped
        # after a while). No network either way, and never listed: the
        # sandbox has no network and the server is private
        empty = scratch / "empty"
        empty.mkdir()
        attempts = [("empty folder", empty, None, "ui.map is missing")]
        if test_data:
            with_maps = scratch / "with-maps"
            (with_maps / "maps").mkdir(parents=True)
            attempts.append(("game maps", with_maps, Path(test_data).resolve() / "maps",
                             "server: hosting; Vitas join with the code"))
        for label, folder, maps, expected in attempts:
            (folder / "init.txt").write_text('sv_name "package check"\nsv_public 0\n'
                                             'sv_mapcycle_add bloodgulch slayer\n')
            command = runner.command(stage, folder, [], maps)
            log("+ " + " ".join(command))
            started = time.monotonic()
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, errors="replace")
            lines: List[str] = []
            reader = threading.Thread(target=lambda: lines.extend(process.stdout), daemon=True)
            reader.start()
            # (until the server says what it should, exits, or the time is up;
            # then it is stopped as systemd would, with SIGTERM)
            while time.monotonic() - started < seconds and process.poll() is None:
                if any(expected in line for line in list(lines)):
                    break
                time.sleep(0.5)
            reached = any(expected in line for line in list(lines))
            runner.stop(process)
            reader.join(timeout=10)
            elapsed = time.monotonic() - started
            output = "".join(lines)
            (work / f"check-{arch.name}-{label.replace(' ', '-')}.log").write_text(output)
            failures = re.findall(r"(?im)^.*(segmentation fault|assertion|illegal instruction|bus error|"
                                  r"error while loading shared libraries|GLIBC_[0-9.]+' not found).*$", output)
            if failures or (process.returncode < 0 and process.returncode != -signal.SIGTERM):
                raise SystemExit(f"{arch.name}, {label}: the server failed (exit {process.returncode}):\n"
                                 + output[-4000:])
            if not reached:
                raise SystemExit(f"{arch.name}, {label}: no \"{expected}\" within {seconds} s:\n" + output[-4000:])
            summary = f"{label}: \"{expected}\" after {elapsed:.0f} s, stopped (exit {process.returncode})"
            log(f"{arch.name}: {summary}")
            results.append(summary)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--arch", choices=["x86", "armhf", "all"], default="all")
    parser.add_argument("--version", help="the release's name in the packages' (default: the tag being "
                        "built, else port/vita/include/vita_version.h's)")
    parser.add_argument("--work", type=Path, default=ROOT / "build" / "server-package",
                        help="downloads, sysroots, SDL and the build trees (default build/server-package)")
    parser.add_argument("--dist", type=Path, default=ROOT / "dist", help="where the packages go (default dist/)")
    parser.add_argument("--clang", default=os.environ.get("CLANG", "clang"), help="the clang to build with")
    parser.add_argument("-j", "--jobs", type=int, help="ninja's parallel jobs")
    parser.add_argument("--no-test", action="store_true", help="skip running the packaged servers")
    parser.add_argument("--fetch-qemu", action="store_true",
                        help="fetch Debian 12's qemu-arm to run the ARM build's checks, if none is on PATH")
    parser.add_argument("--test-data", type=Path, help="a folder with the game's maps/: the checks also start "
                        "each server with them, until it hosts")
    parser.add_argument("--test-seconds", type=int, default=60,
                        help="how long a started server runs in the checks (default 60)")
    args = parser.parse_args()

    version = args.version or version_default()
    if not re.fullmatch(r"[0-9A-Za-z.+~-]+", version):
        raise SystemExit(f"--version {version!r}: letters, digits and . + ~ - only")
    clang = shutil.which(args.clang) or args.clang
    work = args.work.resolve()
    dist = args.dist.resolve()
    work.mkdir(parents=True, exist_ok=True)
    dist.mkdir(parents=True, exist_ok=True)
    epoch = source_date_epoch()
    arches = [ARCHES[name] for name in (["x86", "armhf"] if args.arch == "all" else [args.arch])]

    report = []
    for arch in arches:
        log(f"==== {arch.name}: {arch.description}")
        root = sysroot(arch, work)
        sdl = build_sdl(arch, work, root, clang)
        built = build_server(arch, work, root, sdl, clang, args.jobs)
        # (the debugging information stays out of the package, in dist/ beside it;
        # the symbols stay in, for the game's own crash reports)
        debug = dist / f"halo-server-{version}-linux-{arch.name}.debug"
        shutil.copy2(built, debug)
        stripped = work / f"halo-server-{arch.name}"
        run(["llvm-strip" if shutil.which("llvm-strip") else "strip", "--strip-debug", "-o", stripped, built])
        needs = check_binary(arch, stripped)
        tarball = write_package(arch, version, stripped, sdl, dist, epoch)
        stage = dist / tarball.name[:-len(".tar.gz")]
        checks = [] if args.no_test else run_checks(
            arch, root, stage, work, find_qemu(arch, work, args.fetch_qemu) if arch.qemu else None,
            args.test_data, args.test_seconds)
        shutil.rmtree(stage)
        report.append((arch, tarball, needs, checks))

    # the release's checksums, of every package in dist/ (both, when the two
    # are built by separate runs)
    sums = sorted(dist.glob("halo-ce-vita-server-*.tar.gz"))
    (dist / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.name}\n" for path in sums))
    log("")
    for arch, tarball, needs, checks in report:
        log(f"{tarball.name}: {tarball.stat().st_size / 1e6:.1f} MB, sha256 {sha256(tarball)}")
        log(f"  needs {', '.join(needs['needed'])}; glibc {needs['glibc']} or newer")
        for check in checks:
            log(f"  check: {check}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
