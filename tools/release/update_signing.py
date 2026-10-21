"""What the release tools share (make_update_key.py, sign_update.py,
verify_update.py): the private key file's form, the manifest's text (as
port/vita/host/vita_update_verify.c reads it), the signature file's, and
reading a VPK's param.sfo.

The private key file is text: comment lines (#) and one line of 64
hexadecimal digits, the Ed25519 seed. It belongs on the owner's machine (an
encrypted disk or a USB stick kept offline), never in git, never in CI.
"""

import hashlib
import os
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ed25519_rfc8032 as ed25519  # noqa: E402

MAGIC = "halo-ce-vita-update 1"
MANIFEST = "halo-update.txt"
SIGNATURE = "halo-update.txt.sig"
# the release's VPK: halo-ce-vita-<version>-vita.vpk (vpk_name), beside the
# PC builds' halo-ce-vita-<version>-pc-windows.zip and -pc-linux.tar.gz
# (tools/package_pc.py); halo.vpk, its name until 1.1.1, which the builds
# before 1.1.1 download alone (LEGACY_VPK; README.md, "Release files")
LEGACY_VPK = "halo.vpk"
VPK = LEGACY_VPK
VPK_MAXIMUM = 64 * 1024 * 1024
TAG = re.compile(r"\d{1,3}\.\d{1,3}\.\d{1,3}(-(beta|rc)\.\d{1,3})?")
TITLE = re.compile(r"[A-Z0-9]{9}")
REPOSITORY = Path(__file__).resolve().parents[2]
KEY_HEADER = REPOSITORY / "port/vita/include/vita_update_key.h"
VERSION_HEADER = REPOSITORY / "port/vita/include/vita_version.h"
PLACEHOLDER = "0" * 64


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def inside_git(path):
    """whether path's folder is in a git working tree"""
    folder = Path(path).resolve().parent
    while not folder.exists():
        folder = folder.parent
    try:
        result = subprocess.run(["git", "-C", str(folder), "rev-parse", "--is-inside-work-tree"],
                                capture_output=True, text=True, check=False)
    except OSError:
        return False
    return result.returncode == 0 and result.stdout.strip() == "true"


def read_private_key(path):
    """the seed (32 bytes) of a private key file"""
    try:
        text = Path(path).read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        fail(f"cannot read the private key {path}: {error}")
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")]
    if len(lines) != 1 or not re.fullmatch(r"[0-9a-fA-F]{64}", lines[0]):
        fail(f"{path} is not an update key file (one line of 64 hexadecimal digits)")
    if os.name == "posix" and os.stat(path).st_mode & 0o077:
        print(f"warning: {path} can be read by other users: chmod 600 it", file=sys.stderr)
    return bytes.fromhex(lines[0])


def source_public_key():
    """the public key vita_update_key.h holds (hex), or None"""
    try:
        text = KEY_HEADER.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r'#define HALO_(?:RELEASE|UPDATE)_PUBLIC_KEY "([0-9a-fA-F]{64})"', text)
    return match.group(1).lower() if match else None


def source_version():
    try:
        text = VERSION_HEADER.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r'#define HALO_VITA_VERSION "([^"]+)"', text)
    return match.group(1) if match else None


def sfo_title(data):
    """the TITLE_ID of a param.sfo's bytes, or None"""
    if len(data) < 20 or data[:4] != b"\0PSF":
        return None
    _, _, key_table, data_table, count = struct.unpack_from("<4sIIII", data, 0)
    for index in range(count):
        offset = 20 + index * 16
        if offset + 16 > len(data):
            return None
        key_offset, _, length, _, data_offset = struct.unpack_from("<HHIII", data, offset)
        start = key_table + key_offset
        end = data.find(b"\0", start)
        if data[start:end] == b"TITLE_ID":
            value = data[data_table + data_offset:data_table + data_offset + length]
            return value.split(b"\0", 1)[0].decode("ascii", "replace")
    return None


def vpk_title(path):
    """the TITLE_ID in a VPK's sce_sys/param.sfo; fails when it is no VPK"""
    try:
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            if "eboot.bin" not in names or "sce_sys/param.sfo" not in names:
                fail(f"{path} has no eboot.bin or sce_sys/param.sfo: not a VPK")
            return sfo_title(archive.read("sce_sys/param.sfo"))
    except zipfile.BadZipFile:
        fail(f"{path} is not a zip file (a VPK)")


def file_digest(path):
    digest = hashlib.sha512()
    size = 0
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
            size += len(block)
    return size, digest.hexdigest()


def vpk_name(version):
    """the release's VPK for a version (port/vita/host/vita_update_verify.c's
    update_asset_vpk_name)"""
    return f"halo-ce-vita-{version}-vita.vpk"


def manifest_text(version, title, size, sha512, file=None):
    return (f"{MAGIC}\nversion {version}\ntitle {title}\nfile {file or vpk_name(version)}\nsize {size}\n"
            f"sha512 {sha512}\n")


def parse_manifest(text):
    """the manifest's fields (dict), as strictly as the game reads it, or None"""
    pattern = (r"halo-ce-vita-update 1\nversion (?P<version>[0-9a-z.-]{1,63})\ntitle (?P<title>[A-Z0-9]{9})\n"
               r"file (?P<file>[0-9a-z._-]{1,95})\nsize (?P<size>[1-9][0-9]{0,8}|0)\nsha512 (?P<sha512>[0-9a-f]{128})\n")
    match = re.fullmatch(pattern, text)
    return match.groupdict() if match else None
