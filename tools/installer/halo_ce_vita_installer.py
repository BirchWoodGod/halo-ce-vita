#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
HALO_CE_VITA_INSTALLER.PY

The install helper for Halo: Combat Evolved for the PS Vita: it gathers the
player's own game files, in the layout README.md's Install section gives,
and copies them to the Vita over VitaShell's FTP mode.

    python3 halo_ce_vita_installer.py            the window (tkinter)
    python3 halo_ce_vita_installer.py --help     the command-line steps

The steps, each one optional:

1. Xbox game files: the maps folder and default.xbe out of the player's Xbox
   disc image (an XISO or a full disc image: read here, by a small XDVDFS
   reader, nothing to install) or an already unpacked folder.
2. Movies (optional: the game runs without them): the disc's Bink movies
   converted by ffmpeg to the H.264 MP4s the Vita plays (movies/<name>.mp4).
3. Halo PC files: bitmaps.map, sounds.map and loc.map, which online play and
   the Custom Edition maps need, from Halo MCC on Steam
   (halo1/maps/custom_edition), the Halo Custom Edition installer (any file
   name: it is recognised by what is in it, and its cabinet is unpacked here,
   LZX included) or a folder that has them.
4. Copy to the Vita: everything gathered, over VitaShell's FTP (port 1337),
   into ux0:data/haloce-vita/. A copy that stops (the Vita slept) goes on
   from the files already there.
5. The VPK copied to ux0:data/, to install with VitaShell.

No game data comes with this tool: everything is read from the player's own
files. Every name read from a disc image or a cabinet is checked before a
file is written (no "..", no absolute paths, nothing outside the output
folder), and the files are checked once written (sizes, headers).

Python 3.8 or later, no other packages (tkinter for the window).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import queue
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib
from array import array
from dataclasses import dataclass
from typing import Callable, Dict, Iterator, List, Optional, Sequence, Tuple

VERSION = "1.0"
RELEASES_URL = "https://github.com/BirchWoodGod/halo-ce-vita/releases/latest"
CE_INSTALLER_URL = "https://www.halomaps.org/hce/detail.cfm?fid=410"

# where the game looks for its files on the Vita (README.md, Install)
VITA_GAME_FOLDER = "ux0:data/haloce-vita"
VITA_VPK_FOLDER = "ux0:data"
FTP_PORT = 1337

# the folder the steps gather the files into mirrors VITA_GAME_FOLDER
STAGING_MAPS = "maps"
STAGING_MOVIES = "movies"
STAGING_XBE = "default.xbe"

# Halo PC's resource maps: (name, resource map type in the header)
PC_RESOURCE_MAPS = (("bitmaps.map", 1), ("sounds.map", 2), ("loc.map", 3))
PC_RESOURCE_MAP_MAXIMUM = {"bitmaps.map": 512 << 20, "sounds.map": 256 << 20, "loc.map": 16 << 20}

# halocesetup_en_1.00.exe, the English Halo Custom Edition installer 1.00
KNOWN_CE_INSTALLERS = {
    "011b03b61634561d7c1b6090e7229fe0a2df40de2c2a14b45ac8f0455d5d990f":
        "Halo Custom Edition installer 1.00 (English)",
}

# what the known installers' resource maps hash to once unpacked (checked by
# libmspack's decoder too: tools/ce_installer_extract.c)
KNOWN_CE_RESOURCE_MAPS = {
    "011b03b61634561d7c1b6090e7229fe0a2df40de2c2a14b45ac8f0455d5d990f": {
        "bitmaps.map": "317658e0ebd4aa189bac68e994fe0b515928f44d17b62ba676d8850aa329dd3d",
        "sounds.map": "81658a8d9b127b2771558a35e664ec3f1a2b370f271fe957426d945001ceb5d2",
        "loc.map": "1321e17b5a86be84b1930b6eb2e7fdfe9285275d5d54cbe3d4a3635e2bf04041",
    },
}

# the Xbox campaign's maps and the menu: a maps folder without them is not
# the Xbox game's
XBOX_REQUIRED_MAPS = ("ui.map", "a10.map", "a30.map", "a50.map", "b30.map", "b40.map",
                      "c10.map", "c20.map", "c40.map", "d20.map", "d40.map")

# README.md's movie conversion (the first command: Baseline, 640 wide), and
# the High profile one it gives as the better-quality choice
FFMPEG_MOVIE_ARGS = {
    "baseline": ["-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1", "-pix_fmt", "yuv420p",
                 "-vf", "scale=640:-2", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart"],
    "high": ["-c:v", "libx264", "-profile:v", "high", "-level", "4.0", "-crf", "20", "-pix_fmt", "yuv420p",
             "-vf", "scale=640:-2", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart"],
}

Progress = Callable[[str, int, int], None]


def _no_progress(message: str, done: int, total: int) -> None:
    pass


class InstallerError(Exception):
    """A problem to show the player as it is."""


# ---------------------------------------------------------------------------
# names and paths read from the player's files


_WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL"} | {"COM%d" % i for i in range(1, 10)} | \
    {"LPT%d" % i for i in range(1, 10)}
_BAD_NAME_CHARACTERS = set('<>:"/\\|?*')


def check_name(name: str) -> str:
    """`name` when it is safe as one file or folder name on Windows, Linux,
    macOS and the Vita's FTP; InstallerError otherwise."""
    if not isinstance(name, str) or not name or len(name) > 128:
        raise InstallerError("unsafe file name %r" % (name,))
    if name in (".", "..") or name != name.strip() or name.endswith("."):
        raise InstallerError("unsafe file name %r" % (name,))
    for character in name:
        if ord(character) < 32 or ord(character) == 127 or character in _BAD_NAME_CHARACTERS:
            raise InstallerError("unsafe file name %r" % (name,))
    if name.split(".")[0].upper() in _WINDOWS_RESERVED:
        raise InstallerError("unsafe file name %r" % (name,))
    return name


def safe_join(root: str, *names: str) -> str:
    """`root`/`names`..., each name checked (check_name), the result inside
    `root` once resolved, and no part of it a symbolic link."""
    path = os.path.realpath(root)
    for name in names:
        path = os.path.join(path, check_name(name))
        if os.path.islink(path):
            raise InstallerError("refusing to write through a link: %s" % path)
    real_root = os.path.realpath(root)
    real = os.path.realpath(path)
    try:
        inside = os.path.commonpath([real_root, real]) == real_root and real != real_root
    except ValueError:  # another drive
        inside = False
    if not inside:
        raise InstallerError("path escapes the output folder: %s" % path)
    return path


def human_size(size: float) -> str:
    for unit in ("bytes", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return ("%d %s" % (size, unit)) if unit == "bytes" else ("%.1f %s" % (size, unit))
        size /= 1024.0
    return "%d" % size


def _copy_range(source, destination, offset: int, size: int, progress: Progress, label: str,
                done_before: int = 0, total: int = 0) -> None:
    source.seek(offset)
    left = size
    while left:
        chunk = source.read(min(left, 1 << 20))
        if not chunk:
            raise InstallerError("%s: the image ends early" % label)
        destination.write(chunk)
        left -= len(chunk)
        progress(label, done_before + size - left, total or size)


# ---------------------------------------------------------------------------
# Xbox disc images (XDVDFS)

XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
XDVDFS_SECTOR = 2048
# where the game partition starts: an XISO (extract-xiso, the usual
# backups), then full disc images of the three disc kinds
XDVDFS_PARTITIONS = (0, 0x18300000, 0x0FD90000, 0x02080000)
XDVDFS_DIRECTORY_MAXIMUM = 4 << 20
XDVDFS_ENTRIES_MAXIMUM = 100000
XDVDFS_DEPTH_MAXIMUM = 32
XDVDFS_ATTRIBUTE_DIRECTORY = 0x10


@dataclass
class XisoEntry:
    path: Tuple[str, ...]
    sector: int
    size: int
    directory: bool


class XisoImage:
    """A read-only Xbox disc image: the XDVDFS volume at sector 32 of the
    game partition, its directories binary trees of entries."""

    def __init__(self, path: str):
        self.path = path
        self.file = open(path, "rb")
        try:
            self.file.seek(0, os.SEEK_END)
            self.size = self.file.tell()
            for base in XDVDFS_PARTITIONS:
                header = self._read(base + 32 * XDVDFS_SECTOR, XDVDFS_SECTOR)
                if header and header[:20] == XDVDFS_MAGIC and header[0x7EC:0x800] == XDVDFS_MAGIC:
                    self.base = base
                    self.root_sector, self.root_size = struct.unpack_from("<II", header, 20)
                    break
            else:
                raise InstallerError("%s is not an Xbox disc image (no XDVDFS volume found)"
                                     % os.path.basename(path))
        except BaseException:
            self.file.close()
            raise

    def close(self) -> None:
        self.file.close()

    def __enter__(self):
        return self

    def __exit__(self, *exception):
        self.close()

    def _read(self, offset: int, size: int) -> Optional[bytes]:
        if offset < 0 or size < 0 or offset + size > self.size:
            return None
        self.file.seek(offset)
        data = self.file.read(size)
        return data if len(data) == size else None

    def data_offset(self, sector: int, size: int) -> int:
        offset = self.base + sector * XDVDFS_SECTOR
        if offset + size > self.size:
            raise InstallerError("the disc image is cut short (a file lies past its end): %s" % self.path)
        return offset

    def list_directory(self, sector: int, size: int, path: Tuple[str, ...] = ()) -> List[XisoEntry]:
        if size == 0:
            return []
        if size > XDVDFS_DIRECTORY_MAXIMUM:
            raise InstallerError("a directory of the disc image is too large")
        table = self._read(self.base + sector * XDVDFS_SECTOR, size)
        if table is None:
            raise InstallerError("a directory of the disc image lies past its end")
        entries = []
        seen = set()
        stack = [0]
        while stack:
            offset = stack.pop()
            if offset in seen:
                continue
            seen.add(offset)
            if len(seen) > XDVDFS_ENTRIES_MAXIMUM:
                raise InstallerError("a directory of the disc image has too many entries")
            if offset + 14 > size:
                raise InstallerError("a directory entry of the disc image is out of bounds")
            left, right, entry_sector, entry_size, attributes, name_length = \
                struct.unpack_from("<HHIIBB", table, offset)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            if name_length == 0 or offset + 14 + name_length > size:
                raise InstallerError("a directory entry of the disc image is out of bounds")
            raw_name = table[offset + 14:offset + 14 + name_length]
            name = raw_name.decode("latin-1")
            entries.append(XisoEntry(path + (name,), entry_sector, entry_size,
                                     bool(attributes & XDVDFS_ATTRIBUTE_DIRECTORY)))
            if left:
                stack.append(left * 4)
            if right:
                stack.append(right * 4)
        entries.sort(key=lambda entry: entry.path[-1].lower())
        return entries

    def root(self) -> List[XisoEntry]:
        return self.list_directory(self.root_sector, self.root_size)

    def walk(self) -> Iterator[XisoEntry]:
        """Every entry, parents before children."""
        stack = [(self.root_sector, self.root_size, ())]
        visited = set()
        while stack:
            sector, size, path = stack.pop()
            if len(path) > XDVDFS_DEPTH_MAXIMUM or sector in visited and size:
                continue
            visited.add(sector)
            for entry in self.list_directory(sector, size, path):
                yield entry
                if entry.directory:
                    stack.append((entry.sector, entry.size, entry.path))

    def find(self, *path: str) -> Optional[XisoEntry]:
        """The entry at `path` (names compared without case, as the Xbox
        does), or None."""
        sector, size = self.root_sector, self.root_size
        found = None
        for depth, name in enumerate(path):
            found = None
            for entry in self.list_directory(sector, size, tuple(path[:depth])):
                if entry.path[-1].lower() == name.lower():
                    found = entry
                    break
            if found is None:
                return None
            if depth < len(path) - 1:
                if not found.directory:
                    return None
                sector, size = found.sector, found.size
        return found

    def extract(self, entry: XisoEntry, destination: str, progress: Progress = _no_progress,
                done_before: int = 0, total: int = 0) -> None:
        if entry.directory:
            raise InstallerError("%s is a folder" % "/".join(entry.path))
        offset = self.data_offset(entry.sector, entry.size)
        partial = destination + ".part"
        with open(partial, "wb") as output:
            _copy_range(self.file, output, offset, entry.size, progress, entry.path[-1], done_before, total)
        if os.path.getsize(partial) != entry.size:
            raise InstallerError("%s: wrong size after copying" % entry.path[-1])
        os.replace(partial, destination)


# ---------------------------------------------------------------------------
# step 1: the Xbox game files


def _is_xbe(path: str) -> bool:
    try:
        with open(path, "rb") as file:
            return file.read(4) == b"XBEH"
    except OSError:
        return False


@dataclass
class XboxSource:
    """The player's Xbox game: a disc image or an unpacked folder."""
    path: str
    image: Optional[XisoImage] = None
    folder: Optional[str] = None

    @staticmethod
    def open(path: str) -> "XboxSource":
        if os.path.isdir(path):
            # the folder itself, or its parent when the maps folder was chosen
            folder = path
            if not _find_child(folder, "default.xbe") and os.path.basename(path).lower() == "maps":
                folder = os.path.dirname(path)
            return XboxSource(path, folder=folder)
        if os.path.isfile(path):
            return XboxSource(path, image=XisoImage(path))
        raise InstallerError("%s does not exist" % path)

    def close(self) -> None:
        if self.image:
            self.image.close()

    # (name, size, reader) for the files in a folder of the game
    def files_in(self, folder: str) -> List[Tuple[str, int, object]]:
        if self.image:
            entry = self.image.find(folder)
            if entry is None or not entry.directory:
                return []
            return [(item.path[-1], item.size, item)
                    for item in self.image.list_directory(entry.sector, entry.size, entry.path)
                    if not item.directory]
        directory = _find_child(self.folder, folder)
        if not directory or not os.path.isdir(directory):
            return []
        result = []
        for name in sorted(os.listdir(directory)):
            full = os.path.join(directory, name)
            if os.path.isfile(full) and not os.path.islink(full):
                result.append((name, os.path.getsize(full), full))
        return result

    def file(self, name: str) -> Optional[Tuple[str, int, object]]:
        if self.image:
            entry = self.image.find(name)
            if entry is None or entry.directory:
                return None
            return (entry.path[-1], entry.size, entry)
        full = _find_child(self.folder, name)
        if full and os.path.isfile(full):
            return (os.path.basename(full), os.path.getsize(full), full)
        return None

    def copy_out(self, item: Tuple[str, int, object], destination: str, progress: Progress,
                 done_before: int, total: int) -> None:
        name, size, reader = item
        if isinstance(reader, XisoEntry):
            self.image.extract(reader, destination, progress, done_before, total)
            return
        partial = destination + ".part"
        with open(reader, "rb") as source, open(partial, "wb") as output:
            _copy_range(source, output, 0, size, progress, name, done_before, total)
        os.replace(partial, destination)


def _find_child(folder: Optional[str], name: str) -> Optional[str]:
    """`folder`/`name`, the name compared without case (Xbox backups come in
    every case)."""
    if not folder or not os.path.isdir(folder):
        return None
    exact = os.path.join(folder, name)
    if os.path.exists(exact):
        return exact
    for child in os.listdir(folder):
        if child.lower() == name.lower():
            return os.path.join(folder, child)
    return None


def extract_xbox_files(source_path: str, staging: str, progress: Progress = _no_progress,
                       log: Callable[[str], None] = print) -> Dict[str, int]:
    """Step 1: `staging`/maps/*.map and `staging`/default.xbe from the
    player's disc image or folder. Returns {name: size} written."""
    source = XboxSource.open(source_path)
    try:
        maps = [item for item in source.files_in("maps") if item[0].lower().endswith(".map")]
        xbe = source.file("default.xbe")
        names = {item[0].lower() for item in maps}
        missing = [name for name in XBOX_REQUIRED_MAPS if name not in names]
        if xbe is None:
            raise InstallerError("No default.xbe in %s: is it the Xbox Halo disc?" % source_path)
        if missing:
            raise InstallerError("The maps folder of %s lacks %s: is it the Xbox Halo disc? "
                                 "(The PC version's maps do not work.)" % (source_path, ", ".join(missing)))
        for name in ("bitmaps.map", "sounds.map"):
            if name in names:
                log("Note: %s has %s, which the Xbox game does not: is this the PC version?"
                    % (source_path, name))
        os.makedirs(staging, exist_ok=True)
        maps_folder = safe_join(staging, STAGING_MAPS)
        os.makedirs(maps_folder, exist_ok=True)
        total = sum(item[1] for item in maps) + xbe[1]
        done = 0
        written = {}
        for item in maps + [xbe]:
            name = check_name(item[0])
            if item is xbe:
                destination = safe_join(staging, STAGING_XBE)
            else:
                destination = safe_join(staging, STAGING_MAPS, name.lower())
            if os.path.isfile(destination) and os.path.getsize(destination) == item[1]:
                log("%s already there" % name)
            else:
                source.copy_out(item, destination, progress, done, total)
                log("%s (%s)" % (name, human_size(item[1])))
            done += item[1]
            progress(name, done, total)
            written[name.lower()] = item[1]
        if not _is_xbe(safe_join(staging, STAGING_XBE)):
            raise InstallerError("default.xbe is not an Xbox program (no XBEH header)")
        return written
    finally:
        source.close()


# ---------------------------------------------------------------------------
# step 3: Halo PC's resource maps


def resource_map_ok(path: str, name: str) -> bool:
    """The resource map header (port/linux/game/cache_file_formats.c's
    resource_map_header_identify): its type matching the name, the names
    and index tables within the file."""
    expected = dict(PC_RESOURCE_MAPS).get(name)
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as file:
            header = file.read(16)
    except OSError:
        return False
    if expected is None or len(header) < 16 or size > PC_RESOURCE_MAP_MAXIMUM[name]:
        return False
    kind, names_offset, index_offset, count = struct.unpack("<IIIi", header)
    return (kind == expected and names_offset >= 16 and index_offset >= names_offset and
            count >= 0 and index_offset <= size and count <= (size - index_offset) // 12)


def steam_library_folders() -> List[str]:
    """The Steam library folders this computer has, from Steam's
    libraryfolders.vdf (and the usual places)."""
    steam_roots = []
    if sys.platform == "win32":
        try:
            import winreg
            for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam")):
                try:
                    with winreg.OpenKey(hive, key) as handle:
                        for value in ("SteamPath", "InstallPath"):
                            try:
                                steam_roots.append(winreg.QueryValueEx(handle, value)[0])
                            except OSError:
                                pass
                except OSError:
                    pass
        except ImportError:
            pass
        for variable in ("ProgramFiles(x86)", "ProgramFiles"):
            if os.environ.get(variable):
                steam_roots.append(os.path.join(os.environ[variable], "Steam"))
    elif sys.platform == "darwin":
        steam_roots.append(os.path.expanduser("~/Library/Application Support/Steam"))
    else:
        steam_roots += [os.path.expanduser(path) for path in (
            "~/.steam/steam", "~/.steam/root", "~/.local/share/Steam",
            "~/.var/app/com.valvesoftware.Steam/.local/share/Steam",
            "~/snap/steam/common/.local/share/Steam")]
    libraries = []
    for root in steam_roots:
        root = os.path.normpath(root)
        if os.path.isdir(root):
            libraries.append(root)
            libraries += parse_library_folders(os.path.join(root, "steamapps", "libraryfolders.vdf"))
            libraries += parse_library_folders(os.path.join(root, "config", "libraryfolders.vdf"))
    if sys.platform == "win32":
        # libraries on other drives Steam no longer lists
        for letter in "CDEFGHIJ":
            for name in ("SteamLibrary", "Steam", r"Program Files (x86)\Steam", r"Games\Steam"):
                candidate = "%s:\\%s" % (letter, name)
                if os.path.isdir(os.path.join(candidate, "steamapps")):
                    libraries.append(candidate)
    unique = []
    for library in libraries:
        key = os.path.normcase(os.path.realpath(library))
        if key not in [os.path.normcase(os.path.realpath(seen)) for seen in unique]:
            unique.append(library)
    return unique


def parse_library_folders(vdf_path: str) -> List[str]:
    """The "path" values in a Steam libraryfolders.vdf (both the old and new
    layouts)."""
    try:
        with open(vdf_path, "r", encoding="utf-8", errors="replace") as file:
            text = file.read(1 << 20)
    except OSError:
        return []
    paths = []
    for key, value in re.findall(r'"([^"]*)"\s+"((?:[^"\\]|\\.)*)"', text):
        value = value.replace("\\\\", "\\")
        if key == "path" or (key.isdigit() and (":" in value or value.startswith("/"))):
            paths.append(value)
    return paths


MCC_CUSTOM_EDITION = ("steamapps", "common", "Halo The Master Chief Collection", "halo1", "maps",
                      "custom_edition")


def find_mcc_custom_edition() -> List[str]:
    """MCC's halo1/maps/custom_edition folders that hold the three resource
    maps."""
    found = []
    for library in steam_library_folders():
        folder = os.path.join(library, *MCC_CUSTOM_EDITION)
        if all(os.path.isfile(os.path.join(folder, name)) for name, _ in PC_RESOURCE_MAPS):
            found.append(folder)
    return found


def pc_maps_folder(path: str) -> Optional[str]:
    """The folder with the three resource maps: `path` itself, its maps
    folder (a Custom Edition install), or MCC's halo1/maps/custom_edition
    under it."""
    for candidate in (path, os.path.join(path, "maps"), os.path.join(path, "custom_edition"),
                      os.path.join(path, "halo1", "maps", "custom_edition"),
                      os.path.join(path, *MCC_CUSTOM_EDITION[2:])):
        if all(_find_child(candidate, name) for name, _ in PC_RESOURCE_MAPS):
            return candidate
    return None


def copy_pc_maps_from_folder(folder: str, staging: str, progress: Progress = _no_progress,
                             log: Callable[[str], None] = print) -> List[str]:
    found = pc_maps_folder(folder)
    if not found:
        raise InstallerError("No bitmaps.map, sounds.map and loc.map in %s. With Halo MCC, choose "
                             "halo1/maps/custom_edition (not halo1/maps)." % folder)
    maps_folder = safe_join(staging, STAGING_MAPS)
    os.makedirs(maps_folder, exist_ok=True)
    sources = [(name, _find_child(found, name)) for name, _ in PC_RESOURCE_MAPS]
    for name, source in sources:
        if not resource_map_ok(source, name):
            raise InstallerError("%s is not a Halo PC %s (its header does not match). MCC's own maps in "
                                 "halo1/maps do not work: use halo1/maps/custom_edition." % (source, name))
    total = sum(os.path.getsize(source) for _, source in sources)
    done = 0
    written = []
    for name, source in sources:
        destination = safe_join(staging, STAGING_MAPS, name)
        size = os.path.getsize(source)
        partial = destination + ".part"
        with open(source, "rb") as reader, open(partial, "wb") as writer:
            _copy_range(reader, writer, 0, size, progress, name, done, total)
        os.replace(partial, destination)
        done += size
        written.append(destination)
        log("%s (%s) from %s" % (name, human_size(size), found))
    return written


# ---------------------------------------------------------------------------
# Microsoft cabinets in the Halo Custom Edition installer


class CabinetError(InstallerError):
    pass


CAB_SIGNATURE = b"MSCF\0\0\0\0"
CAB_FILES_MAXIMUM = 10000
CAB_FOLDERS_MAXIMUM = 1000
CAB_BLOCK_MAXIMUM = 32768 + 6144
CAB_INSTALLER_MAXIMUM = 1 << 30


@dataclass
class CabFolder:
    index: int
    data_offset: int
    blocks: int
    compression: int


@dataclass
class CabFile:
    name: str
    size: int
    offset: int
    folder: int


class Cabinet:
    """A Microsoft cabinet at `base` in the file `path` (a .cab, or one
    inside an installer), its files read from their folders: stored,
    MSZIP or LZX."""

    def __init__(self, path: str, base: int = 0):
        self.path = path
        self.base = base
        with open(path, "rb") as file:
            file.seek(0, os.SEEK_END)
            file_size = file.tell()
            file.seek(base)
            header = file.read(36)
            if len(header) < 36 or header[:8] != CAB_SIGNATURE:
                raise CabinetError("no cabinet at offset %d" % base)
            (_, cabinet_size, _, files_offset, _, minor, major, folder_count, file_count, flags,
             _, _) = struct.unpack_from("<IIIIIBBHHHHH", header, 4)
            if major != 1 or cabinet_size < 36 or base + cabinet_size > file_size:
                raise CabinetError("the cabinet's header does not fit the file")
            if flags & 3:
                raise CabinetError("a cabinet split over several files is not supported")
            if folder_count > CAB_FOLDERS_MAXIMUM or file_count > CAB_FILES_MAXIMUM:
                raise CabinetError("the cabinet has too many entries")
            self.size = cabinet_size
            offset = 36
            self.folder_reserve = self.data_reserve = 0
            if flags & 4:
                header_reserve, self.folder_reserve, self.data_reserve = \
                    struct.unpack("<HBB", self._read(file, offset, 4))
                offset += 4 + header_reserve
            self.folders = []
            for index in range(folder_count):
                data_offset, blocks, compression = struct.unpack("<IHH", self._read(file, offset, 8))
                if data_offset >= cabinet_size:
                    raise CabinetError("a cabinet folder lies outside the cabinet")
                self.folders.append(CabFolder(index, data_offset, blocks, compression))
                offset += 8 + self.folder_reserve
            self.files = []
            offset = files_offset
            for _ in range(file_count):
                size, folder_offset, folder, _, _, _ = struct.unpack("<IIHHHH", self._read(file, offset, 16))
                raw = self._read(file, offset + 16, min(257, cabinet_size - offset - 16))
                end = raw.find(b"\0")
                if end <= 0:
                    raise CabinetError("a cabinet file name is not terminated")
                name = raw[:end].decode("latin-1")
                offset += 16 + end + 1
                if folder < folder_count:
                    self.files.append(CabFile(name, size, folder_offset, folder))

    def _read(self, file, offset: int, size: int) -> bytes:
        if offset < 0 or offset + size > self.size:
            raise CabinetError("the cabinet's directory lies outside the cabinet")
        file.seek(self.base + offset)
        data = file.read(size)
        if len(data) != size:
            raise CabinetError("the cabinet is cut short")
        return data

    def find(self, name: str) -> Optional[CabFile]:
        """The file whose name (any case, either slash) is `name`."""
        wanted = name.replace("\\", "/").lower()
        for entry in self.files:
            if entry.name.replace("\\", "/").lower() == wanted:
                return entry
        return None

    def _blocks(self, folder: CabFolder, uncompressed_needed: int) -> Iterator[Tuple[bytes, int]]:
        """(compressed data, uncompressed size) of the folder's data blocks,
        each checksum checked, until `uncompressed_needed` bytes are
        covered."""
        with open(self.path, "rb") as file:
            offset = folder.data_offset
            covered = 0
            for _ in range(folder.blocks):
                if covered >= uncompressed_needed:
                    return
                header = self._read(file, offset, 8)
                checksum, compressed_size, uncompressed_size = struct.unpack("<IHH", header)
                offset += 8 + self.data_reserve
                if compressed_size > CAB_BLOCK_MAXIMUM or uncompressed_size > 32768:
                    raise CabinetError("a cabinet data block is too large")
                data = self._read(file, offset, compressed_size)
                offset += compressed_size
                if checksum and cab_checksum(header[4:8], cab_checksum(data, 0)) != checksum:
                    raise CabinetError("a cabinet data block's checksum is wrong: the file is damaged "
                                       "(download it again)")
                covered += uncompressed_size
                yield data, uncompressed_size
            if covered < uncompressed_needed:
                raise CabinetError("the cabinet folder ends early")

    def folder_stream(self, folder: CabFolder, needed: int) -> Iterator[bytes]:
        """The folder's uncompressed bytes, in pieces, up to `needed` (or a
        little more)."""
        kind = folder.compression & 0x0F
        if kind == 0:
            for data, size in self._blocks(folder, needed):
                if len(data) != size:
                    raise CabinetError("a stored cabinet block has the wrong size")
                yield data
        elif kind == 1:
            history = b""
            for data, size in self._blocks(folder, needed):
                if data[:2] != b"CK":
                    raise CabinetError("an MSZIP block lacks its signature")
                inflater = zlib.decompressobj(-15, zdict=history) if history else zlib.decompressobj(-15)
                try:
                    out = inflater.decompress(data[2:], size)
                except zlib.error as error:
                    raise CabinetError("an MSZIP block does not decompress: %s" % error)
                if len(out) != size:
                    raise CabinetError("an MSZIP block has the wrong size")
                history = (history + out)[-32768:]
                yield out
        elif kind == 3:
            window_bits = (folder.compression >> 8) & 0x1F
            if not 15 <= window_bits <= 21:
                raise CabinetError("unsupported LZX window (%d bits)" % window_bits)
            blocks = list(self._blocks(folder, needed))
            sizes = [size for _, size in blocks]
            if any(size != 32768 for size in sizes[:-1]):
                raise CabinetError("an LZX folder's blocks are not whole frames")
            compressed = b"".join(data for data, _ in blocks)
            del blocks
            yield from lzx_decompress(compressed, window_bits, sum(sizes))
        else:
            raise CabinetError("unsupported cabinet compression (%d)" % kind)

    def extract(self, wanted: Dict[str, str], progress: Progress = _no_progress) -> None:
        """Writes the cabinet's files named by `wanted`'s keys to the paths
        that are its values: each written to <path>.part, its size checked,
        then renamed. The paths are the caller's: the cabinet's names are
        only compared."""
        entries = []
        for name, destination in wanted.items():
            entry = self.find(name)
            if entry is None:
                raise CabinetError("the cabinet has no %s" % name)
            entries.append((entry, destination))
        total = sum(entry.size for entry, _ in entries)
        done = 0
        for folder in self.folders:
            jobs = sorted([job for job in entries if job[0].folder == folder.index], key=lambda job: job[0].offset)
            if not jobs:
                continue
            needed = max(entry.offset + entry.size for entry, _ in jobs)
            position = 0
            outputs = []
            try:
                for entry, destination in jobs:
                    outputs.append([entry, destination, open(destination + ".part", "wb"), 0])
                for piece in self.folder_stream(folder, needed):
                    start, end = position, position + len(piece)
                    for output in outputs:
                        entry = output[0]
                        low, high = max(start, entry.offset), min(end, entry.offset + entry.size)
                        if low < high:
                            output[2].write(piece[low - start:high - start])
                            output[3] += high - low
                            done += high - low
                    position = end
                    progress(os.path.basename(jobs[0][1]) if len(jobs) == 1 else "cabinet folder %d" % folder.index,
                             done, total)
                    if position >= needed:
                        break
            finally:
                for output in outputs:
                    output[2].close()
            for entry, destination, _, written in outputs:
                if written != entry.size or os.path.getsize(destination + ".part") != entry.size:
                    raise CabinetError("%s came out at the wrong size" % entry.name)
                os.replace(destination + ".part", destination)


def cab_checksum(data: bytes, seed: int) -> int:
    """The cabinet data block checksum (libmspack's cabd_checksum): the XOR
    of the little-endian 32-bit words, the last 1-3 bytes taken big-endian."""
    whole = len(data) & ~3
    value = int.from_bytes(data[:whole], "little")
    bits = whole * 8
    while bits > 32:
        half = ((bits // 32 + 1) // 2) * 32
        value = (value & ((1 << half) - 1)) ^ (value >> half)
        bits = half
    tail = 0
    for byte in data[whole:]:
        tail = (tail << 8) | byte
    return (seed ^ value ^ tail) & 0xFFFFFFFF


def find_cabinets(path: str) -> List[int]:
    """Offsets of the cabinets in `path`: a .cab, or a Windows program's
    resources (the installer's .rsrc/CABFILE), else any MSCF signature with
    a header that fits."""
    with open(path, "rb") as file:
        file.seek(0, os.SEEK_END)
        size = file.tell()
        if size > CAB_INSTALLER_MAXIMUM:
            raise InstallerError("%s is too large to be the Halo Custom Edition installer" % path)
        file.seek(0)
        start = file.read(8)
        if start == CAB_SIGNATURE:
            return [0]
        found = []
        if start[:2] == b"MZ":
            found = _pe_resource_cabinets(file, size)
        if not found:
            found = _scan_cabinets(file, size)
        return found


def _cabinet_header_fits(file, size: int, offset: int) -> bool:
    file.seek(offset)
    header = file.read(36)
    if len(header) < 36 or header[:8] != CAB_SIGNATURE:
        return False
    cabinet_size, = struct.unpack_from("<I", header, 8)
    files_offset, = struct.unpack_from("<I", header, 16)
    major = header[25]
    return major == 1 and 36 <= files_offset < cabinet_size and offset + cabinet_size <= size


def _pe_resource_cabinets(file, size: int) -> List[int]:
    """The resource data entries of a PE file that start with a cabinet (a
    bounded walk of the resource tree: three levels, 4096 entries)."""
    def read(offset, length):
        if offset < 0 or offset + length > size:
            return None
        file.seek(offset)
        data = file.read(length)
        return data if len(data) == length else None

    dos = read(0, 64)
    if not dos:
        return []
    pe_offset, = struct.unpack_from("<I", dos, 0x3C)
    pe = read(pe_offset, 24)
    if not pe or pe[:4] != b"PE\0\0":
        return []
    section_count, = struct.unpack_from("<H", pe, 6)
    optional_size, = struct.unpack_from("<H", pe, 20)
    optional = read(pe_offset + 24, optional_size)
    if not optional or section_count > 96:
        return []
    magic, = struct.unpack_from("<H", optional, 0)
    directories = {0x10B: 96, 0x20B: 112}.get(magic)
    if directories is None or optional_size < directories + 3 * 8:
        return []
    resource_rva, resource_size = struct.unpack_from("<II", optional, directories + 2 * 8)
    sections = []
    table = read(pe_offset + 24 + optional_size, 40 * section_count)
    if table is None:
        return []
    for index in range(section_count):
        virtual_size, address, raw_size, raw_offset = struct.unpack_from("<IIII", table, index * 40 + 8)
        sections.append((address, max(virtual_size, raw_size), raw_offset, raw_size))

    def rva_to_offset(rva, length):
        for address, _, raw_offset, raw_size in sections:
            if address <= rva and rva - address + length <= raw_size:
                return raw_offset + rva - address
        return None

    resource_offset = rva_to_offset(resource_rva, 16) if resource_rva else None
    if resource_offset is None:
        return []
    found = []
    seen = 0
    stack = [(0, 0)]
    while stack:
        directory, depth = stack.pop()
        header = read(resource_offset + directory, 16)
        if header is None or directory + 16 > resource_size:
            continue
        named, numbered = struct.unpack_from("<HH", header, 12)
        for index in range(named + numbered):
            seen += 1
            if seen > 4096:
                return found
            entry = read(resource_offset + directory + 16 + index * 8, 8)
            if entry is None:
                break
            _, target = struct.unpack("<II", entry)
            if target & 0x80000000:
                if depth < 2:
                    stack.append((target & 0x7FFFFFFF, depth + 1))
                continue
            leaf = read(resource_offset + target, 16)
            if leaf is None:
                continue
            data_rva, data_size = struct.unpack_from("<II", leaf, 0)
            data_offset = rva_to_offset(data_rva, min(data_size, 36))
            if data_offset is not None and data_size >= 36 and _cabinet_header_fits(file, size, data_offset):
                found.append(data_offset)
    return sorted(found)


def _scan_cabinets(file, size: int) -> List[int]:
    found = []
    file.seek(0)
    position = 0
    carry = b""
    while position < size and len(found) < 8:
        chunk = file.read(4 << 20)
        if not chunk:
            break
        data = carry + chunk
        base = position - len(carry)
        index = data.find(CAB_SIGNATURE)
        while index != -1:
            here = file.tell()
            if _cabinet_header_fits(file, size, base + index):
                found.append(base + index)
            file.seek(here)
            index = data.find(CAB_SIGNATURE, index + 1)
        position += len(chunk)
        carry = data[-7:]
    return found


@dataclass
class InstallerCheck:
    path: str
    sha256: str
    known: Optional[str]
    cabinet: Optional[Cabinet]
    problem: Optional[str]


def file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(4 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_ce_installer(path: str, progress: Progress = _no_progress) -> InstallerCheck:
    """Whether `path` (whatever it is called) is the Halo Custom Edition
    installer: its SHA-256 among the known ones, or a cabinet inside it with
    maps/bitmaps.map, sounds.map and loc.map."""
    digest = hashlib.sha256()
    size = os.path.getsize(path)
    done = 0
    with open(path, "rb") as file:
        while True:
            chunk = file.read(4 << 20)
            if not chunk:
                break
            digest.update(chunk)
            done += len(chunk)
            progress("checking the installer", done, size)
    sha256 = digest.hexdigest()
    known = KNOWN_CE_INSTALLERS.get(sha256)
    problem = None
    cabinet = None
    try:
        for offset in find_cabinets(path):
            candidate = Cabinet(path, offset)
            if all(candidate.find("maps/" + name) for name, _ in PC_RESOURCE_MAPS):
                cabinet = candidate
                break
        if cabinet is None:
            problem = ("This isn't the Halo Custom Edition installer: no cabinet with maps/bitmaps.map, "
                       "sounds.map and loc.map in it.")
    except InstallerError as error:
        problem = "This isn't the Halo Custom Edition installer (%s)." % error
    return InstallerCheck(path, sha256, known, cabinet, problem)


def extract_pc_maps_from_installer(path: str, staging: str, progress: Progress = _no_progress,
                                   log: Callable[[str], None] = print) -> List[str]:
    """Step 3b: the three resource maps out of the installer's cabinet into
    `staging`/maps/, each checked (size, header)."""
    check = check_ce_installer(path, progress)
    if check.problem:
        raise InstallerError(check.problem)
    log("Recognised: %s" % (check.known or "a Halo Custom Edition installer (its cabinet has the maps)"))
    for name, _ in PC_RESOURCE_MAPS:
        entry = check.cabinet.find("maps/" + name)
        if entry.size > PC_RESOURCE_MAP_MAXIMUM[name] or entry.offset > 1 << 30:
            raise InstallerError("This isn't the Halo Custom Edition installer (its %s is too large)." % name)
    maps_folder = safe_join(staging, STAGING_MAPS)
    os.makedirs(maps_folder, exist_ok=True)
    wanted = {"maps/" + name: safe_join(staging, STAGING_MAPS, name) for name, _ in PC_RESOURCE_MAPS}
    started = time.time()
    check.cabinet.extract(wanted, progress)
    expected = KNOWN_CE_RESOURCE_MAPS.get(check.sha256, {})
    for name, _ in PC_RESOURCE_MAPS:
        destination = safe_join(staging, STAGING_MAPS, name)
        damaged = not resource_map_ok(destination, name)
        if not damaged and name in expected:
            damaged = file_sha256(destination) != expected[name]
        if damaged:
            os.remove(destination)
            raise InstallerError("%s came out of the installer damaged" % name)
        log("%s (%s)%s" % (name, human_size(os.path.getsize(destination)), ", checked" if name in expected else ""))
    log("Unpacked in %d s" % (time.time() - started))
    return list(wanted.values())


# ---------------------------------------------------------------------------
# LZX (the cabinet kind: Microsoft's LZX with 32 KB frames and the E8
# translation), written from the format's description; libmspack's lzxd.c
# (port/third_party/libmspack) is the reference it was checked against


class LzxError(CabinetError):
    pass


_LZX_EXTRA = []
_LZX_BASE = []


def _lzx_tables() -> None:
    step = 0
    for index in range(0, 52, 2):
        _LZX_EXTRA.extend((step, step))
        if index and step < 17:
            step += 1
    position = 0
    for index in range(52):
        _LZX_BASE.append(position)
        position += 1 << _LZX_EXTRA[index]


_lzx_tables()
_LZX_POSITION_SLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}
_MASK64 = (1 << 64) - 1


def _huffman_table(lengths: Sequence[int], table_bits: int, may_be_empty: bool = False):
    """A canonical Huffman decoding table: entries symbol << 5 | length for
    codes up to `table_bits` long, -1 where a longer code starts (looked up
    in the second dict, keyed length << 16 | code), -2 where no code is."""
    by_length = [[] for _ in range(17)]
    for symbol, length in enumerate(lengths):
        if length:
            if length > 16:
                raise LzxError("a Huffman code is too long")
            by_length[length].append(symbol)
    table = [-2] * (1 << table_bits)
    longer = {}
    code = 0
    used = 0
    for length in range(1, 17):
        for symbol in by_length[length]:
            if code >= 1 << length:
                raise LzxError("over-subscribed Huffman table")
            if length <= table_bits:
                shift = table_bits - length
                table[code << shift:(code + 1) << shift] = [symbol << 5 | length] * (1 << shift)
            else:
                table[code >> (length - table_bits)] = -1
                longer[length << 16 | code] = symbol
            code += 1
            used += 1
        code <<= 1
    if not used and not may_be_empty:
        raise LzxError("empty Huffman table")
    return table, longer


class _LzxBits:
    """The LZX bit stream: 16-bit little-endian words, each read from its
    top bit. `words` holds them in pairs, so a 32-bit value is two words in
    reading order."""

    def __init__(self, data: bytes):
        padded = bytes(data) + b"\0" * ((-len(data)) % 4 + 16)
        swapped = bytearray(len(padded))
        swapped[0::4] = padded[2::4]
        swapped[1::4] = padded[3::4]
        swapped[2::4] = padded[0::4]
        swapped[3::4] = padded[1::4]
        words = array("I")
        if words.itemsize != 4:
            words = array("L")
        words.frombytes(bytes(swapped))
        if sys.byteorder == "big":
            words.byteswap()
        self.words = words
        self.raw = padded
        self.length = len(data)
        self.index = 0
        self.buffer = 0
        self.count = 0

    def bits(self, count: int) -> int:
        if self.count < 17:
            self.buffer = ((self.buffer << 32) | self.words[self.index]) & _MASK64
            self.index += 1
            self.count += 32
        self.count -= count
        return (self.buffer >> self.count) & ((1 << count) - 1)

    def symbol(self, table, longer, table_bits: int) -> int:
        if self.count < 17:
            self.buffer = ((self.buffer << 32) | self.words[self.index]) & _MASK64
            self.index += 1
            self.count += 32
        peek = (self.buffer >> (self.count - 16)) & 0xFFFF
        entry = table[peek >> (16 - table_bits)]
        if entry >= 0:
            self.count -= entry & 31
            return entry >> 5
        symbol, length = _long_symbol(peek, entry, longer, table_bits)
        self.count -= length
        return symbol

    def bit_position(self) -> int:
        return self.index * 32 - self.count

    def seek_byte(self, position: int) -> None:
        if position & 1 or position > self.length:
            raise LzxError("bad LZX stream position")
        self.index = position >> 2
        if position & 2:
            self.buffer = self.words[self.index] & 0xFFFF
            self.count = 16
            self.index += 1
        else:
            self.buffer = 0
            self.count = 0

    def align16(self) -> None:
        self.count -= self.count & 15


def _long_symbol(peek: int, entry: int, longer, table_bits: int) -> Tuple[int, int]:
    if entry == -1:
        for length in range(table_bits + 1, 17):
            symbol = longer.get(length << 16 | (peek >> (16 - length)))
            if symbol is not None:
                return symbol, length
    raise LzxError("invalid Huffman code in the LZX stream")


def _lzx_read_lengths(bits: _LzxBits, lengths: List[int], first: int, last: int) -> None:
    pretree = _huffman_table([bits.bits(4) for _ in range(20)], 6)
    index = first
    while index < last:
        code = bits.symbol(pretree[0], pretree[1], 6)
        if code == 17:
            run = bits.bits(4) + 4
            value = None
        elif code == 18:
            run = bits.bits(5) + 20
            value = None
        elif code == 19:
            run = bits.bits(1) + 4
            code = bits.symbol(pretree[0], pretree[1], 6)
            if code > 16:
                raise LzxError("bad LZX length code")
            value = (lengths[index] - code) % 17
        else:
            run = 1
            value = (lengths[index] - code) % 17
        if index + run > last:
            raise LzxError("LZX lengths overrun their table")
        for _ in range(run):
            lengths[index] = 0 if value is None else value
            index += 1


def _e8_translate(frame: bytearray, position: int, file_size: int) -> None:
    end = len(frame) - 10
    index = frame.find(0xE8, 0, end)
    while index != -1:
        current = position + index
        absolute = int.from_bytes(frame[index + 1:index + 5], "little", signed=True)
        if -current <= absolute < file_size:
            relative = absolute - current if absolute >= 0 else absolute + file_size
            frame[index + 1:index + 5] = (relative & 0xFFFFFFFF).to_bytes(4, "little")
        index = frame.find(0xE8, index + 5, end)


def lzx_decompress(data: bytes, window_bits: int, output_size: int) -> Iterator[bytes]:
    """The LZX stream `data` (a cabinet folder's data blocks joined),
    yielding its output 32 KB frame by frame, `output_size` bytes in all."""
    try:
        yield from _lzx_frames(data, window_bits, output_size)
    except (IndexError, struct.error):
        raise LzxError("the LZX stream ends early or is damaged")


def _lzx_frames(data: bytes, window_bits: int, output_size: int) -> Iterator[bytes]:
    window_size = 1 << window_bits
    window = bytearray(window_size)
    main_count = 256 + _LZX_POSITION_SLOTS[window_bits] * 8
    main_lengths = [0] * main_count
    length_lengths = [0] * 249
    extra_bits = _LZX_EXTRA
    position_base = _LZX_BASE
    bits = _LzxBits(data)
    words = bits.words
    R0 = R1 = R2 = 1
    position = 0
    block_type = 0
    block_length = 0
    remaining = 0
    raw_position = 0
    main_table = main_long = length_table = length_long = aligned_table = aligned_long = None
    intel_started = False
    intel_size = 0
    output = 0
    frame_number = 0

    if bits.bits(1):
        high = bits.bits(16)
        intel_size = (high << 16) | bits.bits(16)
        if intel_size >= 1 << 31:
            intel_size -= 1 << 32

    while output < output_size:
        frame_size = min(32768, output_size - output)
        frame_start = position
        frame_end = position + frame_size
        while position < frame_end:
            if remaining == 0:
                if block_type == 3:
                    bits.seek_byte(raw_position + (block_length & 1))
                block_type = bits.bits(3)
                high = bits.bits(16)
                block_length = remaining = (high << 8) | bits.bits(8)
                if block_type == 2:
                    aligned_table, aligned_long = _huffman_table([bits.bits(3) for _ in range(8)], 7)
                if block_type in (1, 2):
                    _lzx_read_lengths(bits, main_lengths, 0, 256)
                    _lzx_read_lengths(bits, main_lengths, 256, main_count)
                    main_table, main_long = _huffman_table(main_lengths, 12)
                    if main_lengths[0xE8]:
                        intel_started = True
                    _lzx_read_lengths(bits, length_lengths, 0, 249)
                    length_table, length_long = _huffman_table(length_lengths, 12, may_be_empty=True)
                elif block_type == 3:
                    intel_started = True
                    start = (bits.bit_position() // 16 + 1) * 2
                    if start + 12 > bits.length:
                        raise LzxError("the LZX stream ends early")
                    R0, R1, R2 = struct.unpack_from("<III", bits.raw, start)
                    raw_position = start + 12
                else:
                    raise LzxError("bad LZX block type %d" % block_type)
                if remaining == 0:
                    raise LzxError("empty LZX block")
            run = min(remaining, frame_end - position)
            remaining -= run
            if block_type == 3:
                if raw_position + run > bits.length:
                    raise LzxError("the LZX stream ends early")
                window[position:position + run] = bits.raw[raw_position:raw_position + run]
                raw_position += run
                position += run
                continue

            # verbatim and aligned blocks: the hot loop, on locals
            aligned = block_type == 2
            buffer, count, index = bits.buffer, bits.count, bits.index
            end = position + run
            while position < end:
                if count < 17:
                    buffer = ((buffer << 32) | words[index]) & _MASK64
                    index += 1
                    count += 32
                entry = main_table[(buffer >> (count - 12)) & 0xFFF]
                if entry >= 0:
                    count -= entry & 31
                    symbol = entry >> 5
                else:
                    symbol, length = _long_symbol((buffer >> (count - 16)) & 0xFFFF, entry, main_long, 12)
                    count -= length
                if symbol < 256:
                    window[position] = symbol
                    position += 1
                    continue
                symbol -= 256
                match_length = symbol & 7
                if match_length == 7:
                    if count < 17:
                        buffer = ((buffer << 32) | words[index]) & _MASK64
                        index += 1
                        count += 32
                    entry = length_table[(buffer >> (count - 12)) & 0xFFF]
                    if entry >= 0:
                        count -= entry & 31
                        match_length += entry >> 5
                    else:
                        extra, length = _long_symbol((buffer >> (count - 16)) & 0xFFFF, entry, length_long, 12)
                        count -= length
                        match_length += extra
                match_length += 2
                slot = symbol >> 3
                if slot > 2:
                    if slot == 3:
                        offset = 1
                    else:
                        extra = extra_bits[slot]
                        offset = position_base[slot] - 2
                        if count < 17:
                            buffer = ((buffer << 32) | words[index]) & _MASK64
                            index += 1
                            count += 32
                        if aligned and extra >= 3:
                            if extra > 3:
                                count -= extra - 3
                                offset += ((buffer >> count) & ((1 << (extra - 3)) - 1)) << 3
                                if count < 17:
                                    buffer = ((buffer << 32) | words[index]) & _MASK64
                                    index += 1
                                    count += 32
                            entry = aligned_table[(buffer >> (count - 7)) & 0x7F]
                            if entry < 0:
                                raise LzxError("invalid aligned offset code")
                            count -= entry & 31
                            offset += entry >> 5
                        else:
                            count -= extra
                            offset += (buffer >> count) & ((1 << extra) - 1)
                    R2 = R1
                    R1 = R0
                    R0 = offset
                elif slot == 0:
                    offset = R0
                elif slot == 1:
                    offset = R1
                    R1 = R0
                    R0 = offset
                else:
                    offset = R2
                    R2 = R0
                    R0 = offset
                if position + match_length > end:
                    raise LzxError("an LZX match runs past its block or frame")
                if offset <= 0 or offset > window_size:
                    raise LzxError("an LZX match reaches outside the window")
                source = position - offset
                if source >= 0:
                    if offset >= match_length:
                        window[position:position + match_length] = window[source:source + match_length]
                    else:
                        pattern = window[source:position]
                        window[position:position + match_length] = \
                            (pattern * (match_length // offset + 1))[:match_length]
                else:
                    source += window_size
                    for step in range(match_length):
                        window[position + step] = window[source]
                        source += 1
                        if source == window_size:
                            source = 0
                position += match_length
            bits.buffer, bits.count, bits.index = buffer, count, index

        if block_type != 3:
            bits.align16()
        frame = bytearray(window[frame_start:frame_end])
        if intel_started and intel_size and frame_number < 32768 and frame_size > 10:
            _e8_translate(frame, output, intel_size)
        yield bytes(frame)
        output += frame_size
        frame_number += 1
        if position == window_size:
            position = 0


# ---------------------------------------------------------------------------
# step 2: the movies


class Cancelled(InstallerError):
    def __init__(self):
        super().__init__("Stopped.")


def find_ffmpeg(explicit: Optional[str] = None) -> Optional[str]:
    """ffmpeg: the one given, the one bundled into the Windows program, one
    next to this tool, or one on the PATH."""
    executable = "ffmpeg.exe" if sys.platform == "win32" else "ffmpeg"
    candidates = []
    if explicit:
        candidates.append(explicit)
    bundle = getattr(sys, "_MEIPASS", None)
    if bundle:
        candidates.append(os.path.join(bundle, "ffmpeg", executable))
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
    candidates += [os.path.join(here, executable), os.path.join(here, "ffmpeg", executable),
                   os.path.join(here, "ffmpeg", "bin", executable)]
    on_path = shutil.which("ffmpeg")
    if on_path:
        candidates.append(on_path)
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def _hidden_window() -> dict:
    if sys.platform == "win32":
        return {"creationflags": 0x08000000}  # CREATE_NO_WINDOW
    return {}


def bink_movies(source_path: str) -> List[Tuple[str, int, object]]:
    """The disc's movies: bink/*.bik in the image or folder."""
    source = XboxSource.open(source_path)
    try:
        return [item for item in source.files_in("bink") if item[0].lower().endswith(".bik")]
    finally:
        source.close()


_DURATION = re.compile(r"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)")


def convert_movies(source_path: str, staging: str, ffmpeg: Optional[str] = None, quality: str = "baseline",
                   progress: Progress = _no_progress, log: Callable[[str], None] = print,
                   cancel: Optional[threading.Event] = None) -> List[str]:
    """Step 2: every bink/*.bik converted to `staging`/movies/<name>.mp4 as
    README.md's Movies section does (H.264, 640 wide, AAC)."""
    ffmpeg_path = find_ffmpeg(ffmpeg)
    if not ffmpeg_path:
        raise InstallerError("ffmpeg was not found. The Windows program brings its own; otherwise install "
                             "ffmpeg (https://ffmpeg.org) or choose ffmpeg's program.")
    if quality not in FFMPEG_MOVIE_ARGS:
        raise InstallerError("unknown movie quality %r" % quality)
    source = XboxSource.open(source_path)
    written = []
    try:
        movies = [item for item in source.files_in("bink") if item[0].lower().endswith(".bik")]
        if not movies:
            raise InstallerError("No bink folder with .bik movies in %s" % source_path)
        movies_folder = safe_join(staging, STAGING_MOVIES)
        os.makedirs(movies_folder, exist_ok=True)
        work = tempfile.mkdtemp(prefix="movies-", dir=staging)
        try:
            for number, item in enumerate(movies):
                name = check_name(item[0])
                base = name[:-4].lower()
                destination = safe_join(staging, STAGING_MOVIES, base + ".mp4")
                label = "%s (%d of %d)" % (base + ".mp4", number + 1, len(movies))
                if isinstance(item[2], XisoEntry):
                    bik = os.path.join(work, "movie.bik")
                    source.copy_out(item, bik, lambda m, d, t: progress("reading " + label, d, t), 0, item[1])
                else:
                    bik = item[2]
                _run_ffmpeg(ffmpeg_path, bik, destination, FFMPEG_MOVIE_ARGS[quality], label, progress, cancel)
                if isinstance(item[2], XisoEntry):
                    os.remove(bik)
                log("%s (%s)" % (base + ".mp4", human_size(os.path.getsize(destination))))
                written.append(destination)
        finally:
            shutil.rmtree(work, ignore_errors=True)
    finally:
        source.close()
    return written


def _run_ffmpeg(ffmpeg: str, source: str, destination: str, arguments: List[str], label: str,
                progress: Progress, cancel: Optional[threading.Event]) -> None:
    partial = destination + ".part"
    command = [ffmpeg, "-hide_banner", "-nostdin", "-y", "-i", source] + arguments + \
        ["-f", "mp4", "-progress", "pipe:1", "-nostats", partial]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, **_hidden_window())
    duration = 0
    tail = []
    try:
        for raw in process.stdout:
            line = raw.decode("utf-8", "replace").strip()
            tail = (tail + [line])[-12:]
            if cancel is not None and cancel.is_set():
                process.kill()
                raise Cancelled()
            found = _DURATION.search(line)
            if found and not duration:
                hours, minutes, seconds = found.groups()
                duration = int((int(hours) * 3600 + int(minutes) * 60 + float(seconds)) * 1000000)
            elif line.startswith("out_time_us=") or line.startswith("out_time_ms="):
                try:
                    value = int(line.split("=", 1)[1])
                except ValueError:
                    continue
                progress(label, min(value, duration or value), duration or max(value, 1))
        code = process.wait()
    except BaseException:
        if os.path.exists(partial):
            process.kill()
            process.wait()
            os.remove(partial)
        raise
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    if code != 0 or not os.path.isfile(partial) or os.path.getsize(partial) == 0:
        if os.path.exists(partial):
            os.remove(partial)
        raise InstallerError("ffmpeg could not convert %s:\n%s" % (os.path.basename(source), "\n".join(tail)))
    os.replace(partial, destination)


# ---------------------------------------------------------------------------
# step 4 and 5: VitaShell's FTP


class FtpError(InstallerError):
    pass


class FtpLost(FtpError):
    """The connection dropped (the Vita slept, VitaShell left FTP mode, the
    Wi-Fi went): worth trying again."""


@dataclass
class RemoteEntry:
    name: str
    directory: bool
    size: int


def parse_list_line(line: str) -> Optional[RemoteEntry]:
    """One line of a Unix-style LIST, as VitaShell sends:
    `-rw-r--r-- 1 vita vita 123 Oct  8 00:31 [h3]_sandtrap.map`."""
    fields = line.split(None, 8)
    if len(fields) < 9 or not fields[0] or fields[0][0] not in "-dl":
        return None
    try:
        size = int(fields[4])
    except ValueError:
        return None
    name = fields[8]
    if name in (".", ".."):
        return None
    return RemoteEntry(name, fields[0][0] == "d", size)


def vita_path(*parts: str) -> str:
    """`ux0:data/haloce-vita` + names -> VitaShell's absolute FTP path,
    /ux0:/data/haloce-vita/..."""
    device, _, rest = parts[0].partition(":")
    pieces = [piece for piece in rest.split("/") if piece] + [check_name(part) for part in parts[1:]]
    return "/" + device + ":/" + "/".join(pieces)


class VitaFtp:
    """A small FTP client for VitaShell: passive mode, LIST parsed here
    (VitaShell has no NLST), stray transfer replies skipped, every upload
    checked by its size in a fresh LIST. Commands are built from checked
    names only (no line breaks reach the control connection)."""

    def __init__(self, host: str, port: int = FTP_PORT, timeout: float = 30.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.buffer = b""
        self.address = None

    # -- the control connection
    def connect(self) -> None:
        self.close()
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as error:
            raise FtpLost("Cannot connect to %s:%d (%s). Is VitaShell's FTP mode on (SELECT) and the "
                          "address right?" % (self.host, self.port, error))
        self.address = self.sock.getpeername()[0]
        self.buffer = b""
        code, text = self._reply()
        if code != 220:
            raise FtpError("unexpected greeting: %d %s" % (code, text))
        code, _ = self.command("USER vita", ok=(230, 331))
        if code == 331:
            self.command("PASS vita", ok=(230, 202))
        try:
            self.command("TYPE I", ok=(200,))
        except FtpError:
            pass

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def _line(self) -> str:
        while b"\n" not in self.buffer:
            try:
                chunk = self.sock.recv(4096)
            except OSError as error:
                raise FtpLost("lost the connection to the Vita (%s)" % error)
            if not chunk:
                raise FtpLost("the Vita closed the connection")
            self.buffer += chunk
            if len(self.buffer) > 65536:
                raise FtpError("an FTP reply is too long")
        line, _, self.buffer = self.buffer.partition(b"\n")
        return line.rstrip(b"\r").decode("utf-8", "replace")

    def _reply(self) -> Tuple[int, str]:
        line = self._line()
        if len(line) < 3 or not line[:3].isdigit():
            raise FtpError("unexpected FTP reply %r" % line[:80])
        code = int(line[:3])
        text = [line[4:]]
        if line[3:4] == "-":
            while True:
                more = self._line()
                text.append(more)
                if more[:3] == line[:3] and more[3:4] == " ":
                    break
        return code, "\n".join(text)

    def _drain(self) -> None:
        """Replies nobody waits for (a late 226 after a transfer), read and
        dropped so they are not taken for the next command's."""
        import select
        while True:
            if b"\n" not in self.buffer:
                readable, _, _ = select.select([self.sock], [], [], 0)
                if not readable:
                    return
            try:
                self._reply()
            except FtpError:
                return

    def command(self, text: str, ok: Sequence[int] = (), skip_stray: bool = True) -> Tuple[int, str]:
        if self.sock is None:
            raise FtpLost("not connected")
        if "\r" in text or "\n" in text or "\0" in text:
            raise FtpError("refusing an FTP command with a line break")
        self._drain()
        try:
            self.sock.sendall(text.encode("utf-8") + b"\r\n")
        except OSError as error:
            raise FtpLost("lost the connection to the Vita (%s)" % error)
        while True:
            code, reply = self._reply()
            # a transfer's late reply arriving now
            if skip_stray and code in (125, 150, 226) and code not in ok:
                continue
            break
        if ok and code not in ok:
            raise FtpError("%s: %d %s" % (text.split(" ")[0], code, reply))
        return code, reply

    def _data_connection(self) -> socket.socket:
        code, reply = self.command("PASV", ok=(227,))
        found = re.search(r"(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)", reply)
        if not found:
            raise FtpError("unexpected PASV reply %r" % reply)
        numbers = [int(value) for value in found.groups()]
        port = numbers[4] * 256 + numbers[5]
        # the Vita's own address, whatever the reply says
        try:
            return socket.create_connection((self.address, port), timeout=self.timeout)
        except OSError as error:
            raise FtpLost("cannot open the data connection (%s)" % error)

    # -- folders
    def exists_directory(self, path: str) -> bool:
        code, _ = self.command("CWD " + path)
        return code == 250

    def list_directory(self, path: str) -> Optional[Dict[str, RemoteEntry]]:
        """The folder's entries by lower-case name, or None when it does not
        exist."""
        if not self.exists_directory(path):
            return None
        data = self._data_connection()
        try:
            code, reply = self.command("LIST", ok=(125, 150, 226, 250))
            chunks = []
            received = 0
            while True:
                try:
                    chunk = data.recv(65536)
                except OSError as error:
                    raise FtpLost("the folder listing stopped (%s)" % error)
                if not chunk:
                    break
                chunks.append(chunk)
                received += len(chunk)
                if received > 16 << 20:
                    raise FtpError("the Vita's folder listing is too long")
        finally:
            data.close()
        if code in (125, 150):
            self._final_reply()
        entries = {}
        for line in b"".join(chunks).decode("utf-8", "replace").splitlines():
            entry = parse_list_line(line)
            if entry:
                entries[entry.name.lower()] = entry
        return entries

    def _final_reply(self) -> None:
        """A transfer's closing reply (226), waited for a little; one that
        comes late is dropped by _drain."""
        self.sock.settimeout(min(self.timeout, 10.0))
        try:
            if b"\n" in self.buffer or self._wait_readable(10.0):
                self._reply()
        except FtpError:
            pass
        finally:
            if self.sock is not None:
                self.sock.settimeout(self.timeout)

    def _wait_readable(self, seconds: float) -> bool:
        import select
        readable, _, _ = select.select([self.sock], [], [], seconds)
        return bool(readable)

    def make_directories(self, path: str) -> None:
        """Every folder of `path` (/ux0:/data/a/b) that is missing."""
        device, _, rest = path.lstrip("/").partition("/")
        current = "/" + device
        for piece in [piece for piece in rest.split("/") if piece]:
            current = current + "/" + piece
            if not self.exists_directory(current):
                self.command("MKD " + current)
                if not self.exists_directory(current):
                    raise FtpError("could not make the folder %s on the Vita" % current)

    # -- files
    def upload(self, local: str, directory: str, name: str, progress: Progress = _no_progress,
               cancel: Optional[threading.Event] = None, done_before: int = 0, total: int = 0) -> None:
        check_name(name)
        size = os.path.getsize(local)
        data = self._data_connection()
        sent = 0
        try:
            self.command("STOR %s/%s" % (directory, name), ok=(125, 150))
            with open(local, "rb") as file:
                while True:
                    if cancel is not None and cancel.is_set():
                        raise Cancelled()
                    chunk = file.read(256 * 1024)
                    if not chunk:
                        break
                    try:
                        data.sendall(chunk)
                    except OSError as error:
                        raise FtpLost("the copy of %s stopped (%s)" % (name, error))
                    sent += len(chunk)
                    progress(name, done_before + sent, total or size)
            try:
                data.shutdown(socket.SHUT_WR)
            except OSError:
                pass
        finally:
            data.close()
        self._final_reply()

    def remote_size(self, directory: str, name: str) -> Optional[int]:
        entries = self.list_directory(directory)
        if not entries:
            return None
        entry = entries.get(name.lower())
        return None if entry is None or entry.directory else entry.size


@dataclass
class UploadItem:
    local: str
    folder: str  # Vita folder: ux0:data/haloce-vita[/maps|/movies]
    name: str
    size: int


def staged_uploads(staging: str) -> List[UploadItem]:
    """What the steps gathered in `staging`, with where each goes on the
    Vita (README.md's layout): default.xbe, maps/*.map (the game's, Halo PC's
    and any custom maps put there), movies/*.mp4."""
    items = []
    xbe = os.path.join(staging, STAGING_XBE)
    if os.path.isfile(xbe):
        items.append(UploadItem(xbe, VITA_GAME_FOLDER, STAGING_XBE, os.path.getsize(xbe)))
    for folder, suffixes in ((STAGING_MAPS, (".map", ".yelo")), (STAGING_MOVIES, (".mp4",))):
        directory = os.path.join(staging, folder)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            full = os.path.join(directory, name)
            if not name.lower().endswith(suffixes) or not os.path.isfile(full) or os.path.islink(full):
                continue
            try:
                check_name(name)
            except InstallerError:
                continue
            items.append(UploadItem(full, VITA_GAME_FOLDER + "/" + folder, name, os.path.getsize(full)))
    return items


def upload_files(host: str, items: List[UploadItem], port: int = FTP_PORT, replace: bool = False,
                 progress: Progress = _no_progress, log: Callable[[str], None] = print,
                 cancel: Optional[threading.Event] = None, attempts: int = 6, retry_wait: float = 3.0,
                 timeout: float = 30.0) -> Dict[str, int]:
    """Steps 4 and 5: `items` copied to the Vita over VitaShell's FTP. Files
    already there at the right size are left (unless `replace`); a dropped
    connection is reconnected and the copy goes on; each file is checked by
    its size afterwards. Returns counts: copied, skipped."""
    client = VitaFtp(host, port, timeout)
    total = sum(item.size for item in items)
    done = 0
    copied = skipped = 0
    listings: Dict[str, Optional[Dict[str, RemoteEntry]]] = {}
    failures = 0
    copied_now = set()

    def connect():
        client.connect()
        listings.clear()

    def listing(folder: str) -> Dict[str, RemoteEntry]:
        if folder not in listings:
            path = vita_path(folder)
            entries = client.list_directory(path)
            if entries is None:
                client.make_directories(path)
                entries = client.list_directory(path) or {}
            listings[folder] = entries
        return listings[folder]

    try:
        connect()
        for item in items:
            attempt = 0
            while True:
                if cancel is not None and cancel.is_set():
                    raise Cancelled()
                try:
                    remote = listing(item.folder).get(item.name.lower())
                    if (remote and not remote.directory and remote.size == item.size and
                            not (replace and item.local not in copied_now)):
                        skipped += 1
                        log("%s: already on the Vita" % item.name)
                        break
                    client.upload(item.local, vita_path(item.folder), item.name, progress, cancel, done, total)
                    listings.pop(item.folder, None)
                    remote = listing(item.folder).get(item.name.lower())
                    if remote is None or remote.size != item.size:
                        raise FtpLost("%s: the Vita has %s bytes, not %d" %
                                      (item.name, "no" if remote is None else remote.size, item.size))
                    copied_now.add(item.local)
                    copied += 1
                    log("%s: copied (%s)" % (item.name, human_size(item.size)))
                    break
                except Cancelled:
                    raise
                except FtpError as error:
                    attempt += 1
                    failures += 1
                    if attempt >= attempts:
                        raise FtpError("%s could not be copied: %s" % (item.name, error))
                    log("%s - trying again (%d of %d). Keep the Vita awake with VitaShell's FTP on."
                        % (error, attempt, attempts - 1))
                    deadline = time.time() + retry_wait * attempt
                    while time.time() < deadline:
                        if cancel is not None and cancel.is_set():
                            raise Cancelled()
                        time.sleep(0.2)
                    try:
                        connect()
                    except FtpError as again:
                        log(str(again))
            done += item.size
            progress(item.name, done, total)
    finally:
        client.close()
    return {"copied": copied, "skipped": skipped, "retries": failures}


def check_vpk(path: str) -> None:
    """A VPK is a zip with eboot.bin and sce_sys/param.sfo."""
    if not zipfile.is_zipfile(path):
        raise InstallerError("%s is not a VPK (not a zip file)" % path)
    with zipfile.ZipFile(path) as archive:
        names = {name.lower() for name in archive.namelist()}
    if "eboot.bin" not in names or "sce_sys/param.sfo" not in names:
        raise InstallerError("%s is not a Vita VPK (no eboot.bin and sce_sys/param.sfo)" % path)


def find_local_vpks() -> List[str]:
    """VPKs next to this tool (and in the current folder)."""
    places = [os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__)),
              os.getcwd()]
    found = []
    for place in places:
        try:
            for name in sorted(os.listdir(place)):
                full = os.path.join(place, name)
                if name.lower().endswith(".vpk") and os.path.isfile(full) and full not in found:
                    found.append(full)
        except OSError:
            pass
    return found


def upload_vpk(host: str, vpk: str, port: int = FTP_PORT, progress: Progress = _no_progress,
               log: Callable[[str], None] = print, cancel: Optional[threading.Event] = None) -> str:
    """Step 5: the VPK to ux0:data/ (VitaShell installs it: the Vita cannot
    be told to from here). Returns its name there."""
    check_vpk(vpk)
    name = os.path.basename(vpk)
    try:
        check_name(name)
    except InstallerError:
        name = "halo.vpk"
    item = UploadItem(vpk, VITA_VPK_FOLDER, name, os.path.getsize(vpk))
    upload_files(host, [item], port, replace=True, progress=progress, log=log, cancel=cancel)
    return name


VPK_INSTRUCTIONS = (
    "On the Vita, in VitaShell: leave FTP mode (Circle), open ux0:, then the data folder, "
    "press Cross on {name} and confirm the install. The bubble is called Halo CE. "
    "This tool cannot install it for you: only VitaShell on the Vita can.")


# ---------------------------------------------------------------------------
# the command line


def default_output_folder() -> str:
    documents = os.path.join(os.path.expanduser("~"), "Documents")
    base = documents if os.path.isdir(documents) else os.path.expanduser("~")
    return os.path.join(base, "Halo CE Vita files")


def _console_log(message: str) -> None:
    if sys.stdout is not None:
        print(message, flush=True)


class _ConsoleProgress:
    def __init__(self):
        self.last = 0.0

    def __call__(self, message: str, done: int, total: int) -> None:
        now = time.time()
        if sys.stderr is None or (now - self.last < 0.5 and done < total):
            return
        self.last = now
        percent = 100 * done // total if total else 100
        sys.stderr.write("\r  %-40s %3d%%" % (message[:40], percent))
        if done >= total:
            sys.stderr.write("\n")
        sys.stderr.flush()


def _check_space(folder: str, needed: int) -> None:
    os.makedirs(folder, exist_ok=True)
    free = shutil.disk_usage(folder).free
    if free < needed:
        raise InstallerError("Not enough free space in %s: %s needed, %s free"
                             % (folder, human_size(needed), human_size(free)))


def xbox_files_size(source_path: str) -> int:
    source = XboxSource.open(source_path)
    try:
        return sum(item[1] for item in source.files_in("maps")) + ((source.file("default.xbe") or (0, 0))[1])
    finally:
        source.close()


def self_test(report: Optional[str] = None) -> int:
    """What this copy of the tool can do: tkinter for the window, ffmpeg
    for the movies (the Windows program brings its own)."""
    lines = ["halo_ce_vita_installer %s, Python %s on %s" % (VERSION, sys.version.split()[0], sys.platform)]
    good = True
    try:
        import tkinter  # noqa: F401
        lines.append("tkinter: ok")
    except ImportError as error:
        lines.append("tkinter: missing (%s)" % error)
        good = False
    ffmpeg = find_ffmpeg()
    if ffmpeg:
        try:
            result = subprocess.run([ffmpeg, "-hide_banner", "-version"], stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
                                    **_hidden_window())
            first = result.stdout.decode("utf-8", "replace").splitlines()[:1]
            lines.append("ffmpeg: %s (%s)" % (ffmpeg, first[0] if first else "no output"))
            good = good and result.returncode == 0
        except (OSError, subprocess.SubprocessError) as error:
            lines.append("ffmpeg: %s does not run (%s)" % (ffmpeg, error))
            good = False
    else:
        lines.append("ffmpeg: not found")
        good = False
    lines.append("self test: %s" % ("ok" if good else "PROBLEMS"))
    for line in lines:
        _console_log(line)
    if report:
        with open(report, "w", encoding="utf-8") as file:
            file.write("\n".join(lines) + "\n")
    return 0 if good else 1


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="halo_ce_vita_installer",
        description="Install helper for Halo: Combat Evolved for the PS Vita. With no command, the window "
                    "opens. Every step reads your own files; no game data comes with this tool.")
    parser.add_argument("--version", action="version", version=VERSION)
    parser.add_argument("--out", default=default_output_folder(),
                        help="the folder the files are gathered in (default: %(default)s)")
    commands = parser.add_subparsers(dest="command")
    step = commands.add_parser("xbox", help="step 1: maps and default.xbe from your Xbox disc image or folder")
    step.add_argument("source", help="the Xbox disc image (.iso / .xiso) or the unpacked game folder")
    step = commands.add_parser("movies", help="step 2: the disc's movies converted to MP4 with ffmpeg")
    step.add_argument("source", help="the Xbox disc image or the unpacked game folder")
    step.add_argument("--ffmpeg", help="ffmpeg's program (default: the bundled one, or on the PATH)")
    step.add_argument("--quality", choices=sorted(FFMPEG_MOVIE_ARGS), default="baseline")
    step = commands.add_parser("pc-files", help="step 3: Halo PC's bitmaps.map, sounds.map, loc.map")
    which = step.add_mutually_exclusive_group(required=True)
    which.add_argument("--mcc", nargs="?", const="", metavar="FOLDER",
                       help="from Halo MCC on Steam (found by itself, or give its custom_edition folder)")
    which.add_argument("--installer", metavar="EXE", help="from the Halo Custom Edition installer (any name)")
    which.add_argument("--folder", help="from a folder that has them (a Custom Edition install's maps)")
    step = commands.add_parser("check-installer", help="tell whether a file is the Halo Custom Edition installer")
    step.add_argument("exe")
    step = commands.add_parser("upload", help="step 4: copy the gathered files to the Vita (VitaShell FTP)")
    step.add_argument("host", help="the Vita's address, as VitaShell's FTP screen shows it")
    step.add_argument("--port", type=int, default=FTP_PORT)
    step.add_argument("--replace", action="store_true", help="copy files already on the Vita again")
    step = commands.add_parser("vpk", help="step 5: copy the VPK to ux0:data/ (install it with VitaShell)")
    step.add_argument("host")
    step.add_argument("vpk", nargs="?", help="the VPK (default: one next to this tool)")
    step.add_argument("--port", type=int, default=FTP_PORT)
    step = commands.add_parser("selftest", help="check this copy of the tool (the window, ffmpeg)")
    step.add_argument("--report", help="write the results to this file too")
    arguments = parser.parse_args(argv)
    if arguments.command == "selftest":
        return self_test(arguments.report)

    if not arguments.command:
        return run_window(arguments.out)
    progress = _ConsoleProgress()
    out = arguments.out
    try:
        if arguments.command == "xbox":
            _check_space(out, xbox_files_size(arguments.source))
            written = extract_xbox_files(arguments.source, out, progress, _console_log)
            _console_log("Xbox files ready in %s (%d files)" % (out, len(written)))
        elif arguments.command == "movies":
            os.makedirs(out, exist_ok=True)
            convert_movies(arguments.source, out, arguments.ffmpeg, arguments.quality, progress, _console_log)
        elif arguments.command == "pc-files":
            os.makedirs(out, exist_ok=True)
            if arguments.installer:
                extract_pc_maps_from_installer(arguments.installer, out, progress, _console_log)
            elif arguments.folder:
                copy_pc_maps_from_folder(arguments.folder, out, progress, _console_log)
            else:
                folders = [arguments.mcc] if arguments.mcc else find_mcc_custom_edition()
                if not folders:
                    raise InstallerError("Halo MCC's halo1/maps/custom_edition was not found: give its folder "
                                         "(--mcc FOLDER)")
                copy_pc_maps_from_folder(folders[0], out, progress, _console_log)
        elif arguments.command == "check-installer":
            check = check_ce_installer(arguments.exe, progress)
            _console_log("SHA-256 %s" % check.sha256)
            if check.problem:
                _console_log(check.problem)
                return 1
            _console_log("Yes: %s" % (check.known or "a Halo Custom Edition installer (not one of the known "
                                                     "files, but its cabinet has the maps)"))
        elif arguments.command == "upload":
            items = staged_uploads(out)
            if not items:
                raise InstallerError("Nothing to copy in %s: run the other steps first" % out)
            _console_log("Copying %d files (%s) to %s:%d. Keep the Vita awake." %
                         (len(items), human_size(sum(item.size for item in items)), arguments.host, arguments.port))
            result = upload_files(arguments.host, items, arguments.port, arguments.replace, progress, _console_log)
            _console_log("Done: %(copied)d copied, %(skipped)d already there" % result)
        elif arguments.command == "vpk":
            vpk = arguments.vpk or (find_local_vpks() or [None])[0]
            if not vpk:
                raise InstallerError("No VPK given and none next to this tool. Download halo.vpk from " +
                                     RELEASES_URL)
            name = upload_vpk(arguments.host, vpk, arguments.port, progress, _console_log)
            _console_log(VPK_INSTRUCTIONS.format(name=name))
    except InstallerError as error:
        _console_log("Error: %s" % error)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


# ---------------------------------------------------------------------------
# the window


def run_window(out: str) -> int:
    try:
        import tkinter
        from tkinter import ttk  # noqa: F401 (the window's widgets)
    except ImportError:
        _console_log("tkinter is not available: use the command-line steps (--help), or install Python's "
                     "tkinter (python3-tk).")
        return 1
    try:
        root = tkinter.Tk()
    except tkinter.TclError as error:
        _console_log("Cannot open a window (%s): use the command-line steps (--help)." % error)
        return 1
    InstallerWindow(root, out)
    root.mainloop()
    return 0


class InstallerWindow:
    STEPS = ("Welcome", "1. Xbox game files", "2. Movies", "3. Halo PC files", "4. Copy to the Vita",
             "5. Install the VPK", "Done")

    def __init__(self, root, out: str):
        import tkinter
        from tkinter import ttk
        self.tk = tkinter
        self.ttk = ttk
        self.root = root
        root.title("Halo CE for PS Vita - Install helper")
        root.minsize(820, 660)
        self.messages: "queue.Queue" = queue.Queue()
        self.worker: Optional[threading.Thread] = None
        self.cancel = threading.Event()
        self.page = 0

        self.out = tkinter.StringVar(value=out)
        self.xbox_source = tkinter.StringVar()
        self.movie_source = tkinter.StringVar()
        self.ffmpeg = tkinter.StringVar(value=find_ffmpeg() or "")
        self.quality = tkinter.StringVar(value="baseline")
        self.pc_mode = tkinter.StringVar(value="mcc")
        self.mcc_folder = tkinter.StringVar(value=(find_mcc_custom_edition() or [""])[0])
        self.installer = tkinter.StringVar()
        self.pc_folder = tkinter.StringVar()
        self.host = tkinter.StringVar()
        self.port = tkinter.StringVar(value=str(FTP_PORT))
        self.replace = tkinter.BooleanVar(value=False)
        self.vpk = tkinter.StringVar(value=(find_local_vpks() or [""])[0])
        self.status = tkinter.StringVar(value="")

        outer = ttk.Frame(root, padding=10)
        outer.pack(fill="both", expand=True)
        sidebar = ttk.Frame(outer)
        sidebar.pack(side="left", fill="y", padx=(0, 12))
        self.step_labels = []
        for name in self.STEPS:
            label = ttk.Label(sidebar, text=name, padding=(6, 4))
            label.pack(anchor="w", fill="x")
            self.step_labels.append(label)
        main = ttk.Frame(outer)
        main.pack(side="left", fill="both", expand=True)
        # (packed from the bottom up, so the buttons stay on screen)
        buttons = ttk.Frame(main)
        buttons.pack(side="bottom", fill="x", pady=(8, 0))
        log_frame = ttk.Frame(main)
        log_frame.pack(side="bottom", fill="x", pady=(6, 0))
        self.log_text = tkinter.Text(log_frame, height=6, wrap="word", state="disabled")
        scroll = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        bar = ttk.Frame(main)
        bar.pack(side="bottom", fill="x", pady=(8, 0))
        self.progress = ttk.Progressbar(bar, mode="determinate", maximum=1000)
        self.progress.pack(fill="x")
        ttk.Label(bar, textvariable=self.status).pack(anchor="w")
        self.body = ttk.Frame(main)
        self.body.pack(side="top", fill="both", expand=True)
        self.back_button = ttk.Button(buttons, text="< Back", command=lambda: self.show(self.page - 1))
        self.back_button.pack(side="left")
        self.stop_button = ttk.Button(buttons, text="Stop", command=self.cancel.set, state="disabled")
        self.stop_button.pack(side="left", padx=8)
        self.next_button = ttk.Button(buttons, text="Next (skip) >", command=lambda: self.show(self.page + 1))
        self.next_button.pack(side="right")
        self.show(0)
        root.after(100, self.poll)

    # -- helpers
    def clear(self):
        for child in self.body.winfo_children():
            child.destroy()

    def heading(self, text: str, explanation: str):
        ttk = self.ttk
        ttk.Label(self.body, text=text, font=("TkDefaultFont", 14, "bold")).pack(anchor="w")
        ttk.Label(self.body, text=explanation, wraplength=520, justify="left").pack(anchor="w", pady=(4, 10))

    def path_row(self, label: str, variable, browse_file=None, browse_folder=None, filetypes=None):
        ttk = self.ttk
        from tkinter import filedialog
        row = ttk.Frame(self.body)
        row.pack(fill="x", pady=3)
        ttk.Label(row, text=label, width=16).pack(side="left")
        ttk.Entry(row, textvariable=variable).pack(side="left", fill="x", expand=True)
        if browse_file:
            ttk.Button(row, text="File...", command=lambda: variable.set(
                filedialog.askopenfilename(filetypes=filetypes or [("All files", "*")]) or variable.get())
            ).pack(side="left", padx=(4, 0))
        if browse_folder:
            ttk.Button(row, text="Folder...", command=lambda: variable.set(
                filedialog.askdirectory() or variable.get())).pack(side="left", padx=(4, 0))
        return row

    def action(self, text: str, prepare):
        button = self.ttk.Button(self.body, text=text, command=lambda: self.start(prepare))
        button.pack(anchor="w", pady=(10, 0))
        return button

    def link(self, text: str, url: str):
        import webbrowser
        label = self.ttk.Label(self.body, text=text, foreground="#1a5fb4", cursor="hand2")
        label.pack(anchor="w")
        label.bind("<Button-1>", lambda event: webbrowser.open(url))

    def log(self, message: str):
        self.messages.put(("log", message))

    def report(self, message: str, done: int, total: int):
        if self.cancel.is_set():
            raise Cancelled()
        self.messages.put(("progress", message, done, total))

    # -- the worker: `prepare` reads the window's fields (here, on the
    # window's thread: tkinter is not thread-safe) and returns the work,
    # which runs on a thread of its own and reports through the queue
    def start(self, prepare):
        if self.worker and self.worker.is_alive():
            return
        try:
            function = prepare()
        except InstallerError as error:
            from tkinter import messagebox
            messagebox.showerror("Halo CE for PS Vita", str(error), parent=self.root)
            return
        self.cancel.clear()
        self.stop_button.configure(state="normal")
        self.next_button.configure(state="disabled")
        self.back_button.configure(state="disabled")
        self.progress["value"] = 0

        def run():
            try:
                result = function()
                self.messages.put(("done", result))
            except Cancelled:
                self.messages.put(("error", "Stopped."))
            except InstallerError as error:
                self.messages.put(("error", str(error)))
            except Exception as error:  # shown, not lost in a thread
                self.messages.put(("error", "Unexpected problem: %r" % (error,)))

        self.worker = threading.Thread(target=run, daemon=True)
        self.worker.start()

    def poll(self):
        try:
            while True:
                message = self.messages.get_nowait()
                if message[0] == "log":
                    self.append_log(message[1])
                elif message[0] == "progress":
                    _, text, done, total = message
                    self.progress["value"] = 1000 * done // total if total else 0
                    self.status.set("%s - %d%%" % (text, 100 * done // total if total else 100))
                elif message[0] in ("done", "error"):
                    self.stop_button.configure(state="disabled")
                    self.next_button.configure(state="normal")
                    self.back_button.configure(state="normal")
                    if message[0] == "error":
                        self.status.set("Problem - see below")
                        self.append_log("ERROR: " + message[1])
                        from tkinter import messagebox
                        messagebox.showerror("Halo CE for PS Vita", message[1], parent=self.root)
                    else:
                        self.progress["value"] = 1000
                        self.status.set("Done")
                        if isinstance(message[1], str) and message[1]:
                            self.append_log(message[1])
                        self.show(self.page)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def append_log(self, text: str):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    # -- the pages
    def show(self, page: int):
        page = max(0, min(page, len(self.STEPS) - 1))
        self.page = page
        for index, label in enumerate(self.step_labels):
            label.configure(font=("TkDefaultFont", 10, "bold" if index == page else "normal"))
        self.clear()
        self.back_button.configure(state="normal" if page else "disabled")
        self.next_button.configure(text="Next (skip) >" if 0 < page < len(self.STEPS) - 1 else "Next >",
                                   state="normal" if page < len(self.STEPS) - 1 else "disabled")
        getattr(self, "page_%d" % page)()

    def page_0(self):
        self.heading("Halo CE for PS Vita - install helper",
                     "This gathers your own game files in the layout the Vita needs and copies them to the "
                     "Vita over VitaShell's FTP. No game data comes with it: you need your own Xbox copy of "
                     "Halo: Combat Evolved. Every step can be skipped (Next). The files are gathered in "
                     "the folder below first (about 2.5 GB with everything).")
        self.path_row("Output folder", self.out, browse_folder=True)
        self.ttk.Label(self.body, wraplength=520, justify="left", text=(
            "Also needed on the Vita: HENkaku/Enso, VitaShell, and the shader compiler "
            "ur0:data/libshacccg.suprx (ShaRKF00D extracts it). See README.md.")).pack(anchor="w", pady=(10, 0))
        self.link("The official downloads (releases)", RELEASES_URL)

    def page_1(self):
        self.heading("1. Xbox game files",
                     "Choose your Xbox Halo disc image (.iso / .xiso, a full disc image works too) or an "
                     "already unpacked game folder. The maps folder and default.xbe are taken out of it. "
                     "It must be the Xbox version: the PC version's maps do not work.")
        self.path_row("Disc image/folder", self.xbox_source, browse_file=True, browse_folder=True,
                      filetypes=[("Xbox disc images", "*.iso *.xiso"), ("All files", "*")])
        self.action("Get the Xbox files", self.do_xbox)
        self.show_staged()

    def do_xbox(self):
        source = self.xbox_source.get().strip()
        if not source:
            raise InstallerError("Choose your Xbox disc image or game folder first.")
        out = self.out.get()
        if not self.movie_source.get():
            self.movie_source.set(source)

        def work():
            _check_space(out, xbox_files_size(source))
            extract_xbox_files(source, out, self.report, self.log)
            return "Xbox files ready in %s" % out
        return work

    def page_2(self):
        self.heading("2. Movies",
                     "The disc's movies (Bink) converted to the MP4s the Vita plays, with ffmpeg: the intro, "
                     "the attract videos and the credits. They are optional: without them the game skips "
                     "each movie and the main menu comes up ready to play. This takes a few minutes; Next "
                     "skips it.")
        if not self.movie_source.get() and self.xbox_source.get():
            self.movie_source.set(self.xbox_source.get())
        self.path_row("Disc image/folder", self.movie_source, browse_file=True, browse_folder=True,
                      filetypes=[("Xbox disc images", "*.iso *.xiso"), ("All files", "*")])
        self.path_row("ffmpeg", self.ffmpeg, browse_file=True)
        if not self.ffmpeg.get():
            self.ttk.Label(self.body, text="ffmpeg was not found: install it from ffmpeg.org, or choose its "
                                           "program.", foreground="#a51d2d").pack(anchor="w")
        row = self.ttk.Frame(self.body)
        row.pack(anchor="w", pady=(6, 0))
        self.ttk.Radiobutton(row, text="Standard (H.264 Baseline, as README.md)", value="baseline",
                             variable=self.quality).pack(anchor="w")
        self.ttk.Radiobutton(row, text="Better quality (H.264 High profile, same size)", value="high",
                             variable=self.quality).pack(anchor="w")
        self.action("Convert the movies", self.do_movies)
        self.show_staged()

    def do_movies(self):
        source = self.movie_source.get().strip()
        if not source:
            raise InstallerError("Choose your Xbox disc image or game folder first.")
        out, ffmpeg, quality = self.out.get(), self.ffmpeg.get() or None, self.quality.get()

        def work():
            _check_space(out, 600 << 20)
            convert_movies(source, out, ffmpeg, quality, self.report, self.log, self.cancel)
            return "Movies ready"
        return work

    def page_3(self):
        self.heading("3. Halo PC files (for online play)",
                     "Online play and the Custom Edition maps need Halo PC's bitmaps.map, sounds.map and "
                     "loc.map, from your own copy. Pick one way:")
        ttk = self.ttk
        ttk.Radiobutton(self.body, text="Halo: The Master Chief Collection on Steam", value="mcc",
                        variable=self.pc_mode).pack(anchor="w")
        self.path_row("  custom_edition", self.mcc_folder, browse_folder=True)
        if not self.mcc_folder.get():
            ttk.Label(self.body, text="  (not found by itself: choose halo1/maps/custom_edition in MCC's "
                                      "folder)").pack(anchor="w")
        ttk.Radiobutton(self.body, text="The Halo Custom Edition installer (any file name)", value="installer",
                        variable=self.pc_mode).pack(anchor="w", pady=(8, 0))
        self.path_row("  Installer", self.installer, browse_file=True,
                      filetypes=[("Programs", "*.exe"), ("All files", "*")])
        self.link("  Where to get it: halomaps.org (Halo Custom Edition)", CE_INSTALLER_URL)
        ttk.Radiobutton(self.body, text="A folder that has them (a Custom Edition install's maps)",
                        value="folder", variable=self.pc_mode).pack(anchor="w", pady=(8, 0))
        self.path_row("  Folder", self.pc_folder, browse_folder=True)
        self.action("Get the Halo PC files", self.do_pc_files)
        self.show_staged()

    def do_pc_files(self):
        out = self.out.get()
        mode = self.pc_mode.get()
        installer = self.installer.get()
        folder = self.mcc_folder.get() if mode == "mcc" else self.pc_folder.get()
        if mode == "installer" and not installer:
            raise InstallerError("Choose the installer first.")
        if mode != "installer" and not folder:
            raise InstallerError("Choose the folder first.")

        def work():
            _check_space(out, 200 << 20)
            if mode == "installer":
                extract_pc_maps_from_installer(installer, out, self.report, self.log)
            else:
                copy_pc_maps_from_folder(folder, out, self.report, self.log)
            return "Halo PC files ready"
        return work

    def page_4(self):
        self.heading("4. Copy to the Vita",
                     "On the Vita, open VitaShell and press SELECT: it shows FTP mode with an address like "
                     "ftp://192.168.1.20:1337. Type that address below. Keep the Vita awake while it copies "
                     "(touch the screen now and then, or turn off auto-standby): if it sleeps the copy stops; "
                     "press Copy again and it goes on from where it stopped.")
        row = self.ttk.Frame(self.body)
        row.pack(fill="x", pady=3)
        self.ttk.Label(row, text="Vita address", width=16).pack(side="left")
        self.ttk.Entry(row, textvariable=self.host, width=20).pack(side="left")
        self.ttk.Label(row, text="  port").pack(side="left")
        self.ttk.Entry(row, textvariable=self.port, width=6).pack(side="left")
        self.ttk.Checkbutton(self.body, text="Copy files already on the Vita again", variable=self.replace
                             ).pack(anchor="w", pady=(6, 0))
        items = staged_uploads(self.out.get())
        self.ttk.Label(self.body, wraplength=520, justify="left", text=(
            "To copy into %s: %d files, %s. (Custom maps put in %s are copied too.)" % (
                VITA_GAME_FOLDER, len(items), human_size(sum(item.size for item in items)),
                os.path.join(self.out.get(), STAGING_MAPS)))).pack(anchor="w", pady=(6, 0))
        self.action("Copy to the Vita", self.do_upload)

    def _host_port(self) -> Tuple[str, int]:
        text = self.host.get().strip()
        found = re.match(r"^(?:ftp://)?([A-Za-z0-9.\-]+)(?::(\d+))?/?$", text)
        if not found:
            raise InstallerError("Type the Vita's address as VitaShell shows it (like 192.168.1.20).")
        port = int(found.group(2) or self.port.get() or FTP_PORT)
        return found.group(1), port

    def do_upload(self):
        host, port = self._host_port()
        items = staged_uploads(self.out.get())
        replace = self.replace.get()
        if not items:
            raise InstallerError("Nothing to copy yet: do the steps before this one (or check the output "
                                 "folder on the first page).")

        def work():
            self.log("Copying %d files (%s) to %s:%d" % (len(items), human_size(sum(i.size for i in items)),
                                                         host, port))
            result = upload_files(host, items, port, replace, self.report, self.log, self.cancel)
            return "On the Vita: %(copied)d copied, %(skipped)d were already there" % result
        return work

    def page_5(self):
        self.heading("5. Install the VPK",
                     "Download halo.vpk from the official releases (or put it next to this program), then "
                     "copy it to the Vita's ux0:data/ here. VitaShell installs it: this program cannot.")
        self.link("Releases: download halo.vpk", RELEASES_URL)
        self.path_row("VPK", self.vpk, browse_file=True, filetypes=[("VPK", "*.vpk"), ("All files", "*")])
        row = self.ttk.Frame(self.body)
        row.pack(fill="x", pady=3)
        self.ttk.Label(row, text="Vita address", width=16).pack(side="left")
        self.ttk.Entry(row, textvariable=self.host, width=20).pack(side="left")
        self.action("Copy the VPK to the Vita", self.do_vpk)
        self.ttk.Label(self.body, wraplength=520, justify="left", text=VPK_INSTRUCTIONS.format(
            name=os.path.basename(self.vpk.get()) or "halo.vpk")).pack(anchor="w", pady=(10, 0))

    def do_vpk(self):
        vpk = self.vpk.get()
        if not vpk:
            raise InstallerError("Choose the VPK first.")
        host, port = self._host_port()

        def work():
            name = upload_vpk(host, vpk, port, self.report, self.log, self.cancel)
            return VPK_INSTRUCTIONS.format(name=name)
        return work

    def page_6(self):
        self.heading("Done",
                     "Start Halo CE from its bubble. The first load of each level takes a while: the game "
                     "writes a cache file for it. Stuck on the loading picture while the music plays? The "
                     "shader compiler ur0:data/libshacccg.suprx is missing (ShaRKF00D). Online play also "
                     "needs Connection: Online in the settings panel (Select + Start).")
        self.show_staged()

    def show_staged(self):
        items = staged_uploads(self.out.get())
        maps = [item for item in items if item.folder.endswith("/maps")]
        movies = [item for item in items if item.folder.endswith("/movies")]
        pc = [name for name, _ in PC_RESOURCE_MAPS if any(item.name.lower() == name for item in maps)]
        xbe = any(item.name == STAGING_XBE for item in items)
        self.ttk.Label(self.body, wraplength=520, justify="left", foreground="#555", text=(
            "Gathered so far in %s: %s, %d maps (%s), %d movies." % (
                self.out.get(), "default.xbe" if xbe else "no default.xbe", len(maps),
                "with the Halo PC files" if len(pc) == 3 else "no Halo PC files", len(movies)))
        ).pack(anchor="w", pady=(14, 0))


if __name__ == "__main__":
    sys.exit(main())
