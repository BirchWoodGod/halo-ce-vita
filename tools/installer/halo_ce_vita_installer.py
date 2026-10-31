#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
HALO_CE_VITA_INSTALLER.PY

The install helper for Halo: Combat Evolved: it gathers the player's own game
files, in the layout README.md's Install section gives, and copies them to a
PS Vita over VitaShell's FTP mode or into a PC build's data folder.

    python3 halo_ce_vita_installer.py            the window (tkinter)
    python3 halo_ce_vita_installer.py --help     the command-line steps

The window is a standard Setup wizard: Welcome, License, Where to Install
(PLATFORMS; those not ready yet are listed greyed), Game Files (Automatic:
everything from the player's own Halo: The Master Chief Collection, found as
the game finds it; Manual: the player's files, chosen on that one page),
Destination, Ready to Install, Installing (one progress bar, everything in
order), Finish. install_flow() gives the pages of each choice; a game choice
(GAMES) shows up as a page of its own once there are two games. Run again over
a PC install it offers repair or uninstall.

The sources, each one optional:

- Game files: the maps folder and default.xbe out of the player's Xbox disc
  image (an XISO or a full disc image: read here, by a small XDVDFS reader,
  nothing to install) or an already unpacked folder; or, the other way, the
  Halo CE content of the player's own MCC (Steam), copied as the game reads
  it into mcc/.
- Movies (Vita only, optional: the game runs without them): the disc's Bink
  movies converted by ffmpeg to the H.264 MP4s the Vita plays
  (movies/<name>.mp4).
- Halo PC files: bitmaps.map, sounds.map and loc.map, which online play and
  the Custom Edition maps need, from Halo MCC on Steam
  (halo1/maps/custom_edition), the Halo Custom Edition installer (any file
  name: it is recognised by what is in it, and its cabinet is unpacked here,
  LZX included) or a folder that has them.

What Setup does with them:

- On a Vita: everything gathered, over VitaShell's FTP (port 1337), into
  ux0:data/haloce-vita/. A copy that stops (the Vita slept) goes on from the
  files already there. Then the VPK to ux0:data/, to install with VitaShell.
- On PC: a real install, for the current user (no admin needed): the PC
  build's archive unpacked into the install folder, the game files (without
  the movies) in the folder of its executable, build/linux or build/windows,
  where the game looks for maps/ and mcc/. Shortcuts; on Windows an
  uninstaller and an Apps & Features entry under HKCU; on Linux a .desktop
  file.

No game data comes with this tool: everything is read from the player's own
files. Every name read from a disc image or a cabinet is checked before a
file is written (no "..", no absolute paths, nothing outside the output
folder), and the files are checked once written (sizes, headers).

Python 3.8 or later, no other packages (tkinter for the window, in ttk's
native theme: 'vista' on Windows).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import queue
import random
import re
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zipfile
import zlib
from array import array
from dataclasses import dataclass
from typing import Callable, Dict, Iterator, List, Optional, Sequence, Tuple

VERSION = "1.1.2"
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

# README.md's movie conversion: H.264 Constrained Baseline (x264's baseline
# profile: no B-frames, no CABAC), 640 wide. Every choice must stay so: the
# Vita plays High profile's B-frames as white frames. "better" is the same
# profile at a higher quality (larger files, slower), its bit rate capped
# for level 3.1.
FFMPEG_MOVIE_ARGS = {
    "baseline": ["-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1", "-bf", "0",
                 "-pix_fmt", "yuv420p", "-vf", "scale=640:-2", "-c:a", "aac", "-b:a", "128k",
                 "-movflags", "+faststart"],
    "better": ["-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1", "-bf", "0", "-preset", "slow",
               "-crf", "18", "-maxrate", "6M", "-bufsize", "12M", "-pix_fmt", "yuv420p", "-vf", "scale=640:-2",
               "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart"],
}
# (the old High profile choice's name: now the Vita-safe "better")
MOVIE_QUALITY_ALIASES = {"high": "better", "standard": "baseline"}

Progress = Callable[[str, int, int], None]


@dataclass(frozen=True)
class Choice:
    """An entry of a choice page: its key, its name, one line about it, and
    whether it can be picked yet (one that cannot is listed greyed)."""
    key: str
    name: str
    note: str = ""
    available: bool = True


# One entry until there are more: Halo 2 joins GAMES in 1.2.0 (with its
# sources: game_tasks and the Game Files page are where a game's differ), and
# a console in PLATFORMS is greyed until there is a build of it.
GAMES = (Choice("halo-ce", "Halo: Combat Evolved"),)
PLATFORMS = (
    Choice("vita", "PS Vita", "Setup sends the files over VitaShell's FTP. Needs HENkaku/Enso, VitaShell and the "
                              "shader compiler ur0:data/libshacccg.suprx (ShaRKF00D extracts it)."),
    Choice("pc", "PC", "Setup installs the PC build on this computer."),
    Choice("android", "Android", available=False),
    Choice("switch", "Nintendo Switch", available=False),
    Choice("other", "Other consoles", available=False),
)
MODES = (
    Choice("automatic", "Automatic", "Everything comes from your own Halo: The Master Chief Collection."),
    Choice("manual", "Manual", "Pick your disc image, the Halo Custom Edition files and the movies yourself."),
)
ACTIONS = ("install", "repair", "uninstall")


def pick(choices: Sequence[Choice], key: str) -> Choice:
    """The entry `key` of a choice list; one that is greyed cannot be picked."""
    for choice in choices:
        if choice.key == key:
            if not choice.available:
                raise InstallerError("%s is not ready yet." % choice.name)
            return choice
    raise InstallerError("Unknown choice: %s" % key)


def install_flow(platform: str, games: Optional[Sequence[Choice]] = None, action: str = "install",
                 installed: bool = False, need_game: bool = True, need_build: bool = True) -> Tuple[str, ...]:
    """The wizard's pages, in order, for these choices. Over an install
    (`installed`) it asks what to do first; a repair shows only the pages it
    has a use for: the game files if some are damaged (`need_game`), the
    destination if program files are and no PC build is at hand
    (`need_build`). An uninstall asks nothing more."""
    pick(PLATFORMS, platform)
    if action not in ACTIONS:
        raise InstallerError("Unknown action: %s" % action)
    end = ["ready", "installing", "finish"]
    if action == "uninstall":
        return tuple(["welcome", "maintenance"] + end)
    if action == "repair":
        return tuple(["welcome", "maintenance"] + ["game_files"] * need_game + ["destination"] * need_build + end)
    first = ["welcome"] + ["maintenance"] * installed + ["license"]
    game = ["game"] if len(GAMES if games is None else games) > 1 else []
    return tuple(first + game + ["platform", "game_files", "destination"] + end)


def _no_progress(message: str, done: int, total: int) -> None:
    pass


class InstallerError(Exception):
    """A problem to show the player as it is."""


@dataclass
class InstallTask:
    """One piece of Installing's work: what the page says, its share of the
    one progress bar, and the work, given a Progress for its own share."""
    label: str
    weight: int
    run: Callable[[Progress], object]


def run_tasks(tasks: Sequence[InstallTask], report: Progress, started: Callable[[str], None] = lambda label: None) -> None:
    """Everything in order, each task's progress mapped onto the one bar."""
    total = max(sum(task.weight for task in tasks), 1)
    finished = 0
    for task in tasks:
        def step(message: str, done: int, whole: int, base: int = finished, weight: int = task.weight) -> None:
            report(message, base + (min(weight * done // whole, weight) if whole else 0), total)
        started(task.label)
        report("", finished, total)
        task.run(step)
        finished += task.weight
    try:
        report("", total, total)
    except Cancelled:  # (too late: everything is done)
        pass


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
# the Xbox game files


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


def _xbox_files(source: XboxSource, source_path: str):
    """The maps and default.xbe of the player's game, or why they will not do."""
    maps = [item for item in source.files_in("maps") if item[0].lower().endswith(".map")]
    xbe = source.file("default.xbe")
    names = {item[0].lower() for item in maps}
    missing = [name for name in XBOX_REQUIRED_MAPS if name not in names]
    if xbe is None:
        raise InstallerError("No default.xbe in %s: is it the Xbox Halo disc?" % source_path)
    if missing:
        raise InstallerError("The maps folder of %s lacks %s: is it the Xbox Halo disc? "
                             "(The PC version's maps do not work.)" % (source_path, ", ".join(missing)))
    return maps, xbe, names


def check_xbox_files(source_path: str) -> int:
    """Whether this is the Xbox game, before anything is copied: its files'
    size, or InstallerError."""
    source = XboxSource.open(source_path)
    try:
        maps, xbe, _ = _xbox_files(source, source_path)
        return sum(item[1] for item in maps) + xbe[1]
    finally:
        source.close()


def extract_xbox_files(source_path: str, staging: str, progress: Progress = _no_progress,
                       log: Callable[[str], None] = print) -> Dict[str, int]:
    """`staging`/maps/*.map and `staging`/default.xbe from the
    player's disc image or folder. Returns {name: size} written."""
    source = XboxSource.open(source_path)
    try:
        maps, xbe, names = _xbox_files(source, source_path)
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
# Halo PC's resource maps


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
    # (and MCC's folders found otherwise: the registry, the Xbox app's)
    for root in find_mcc_folders():
        folder = pc_maps_folder(root)
        if folder and os.path.normcase(os.path.realpath(folder)) not in \
                [os.path.normcase(os.path.realpath(seen)) for seen in found]:
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


def find_ce_install_folders() -> List[str]:
    """Halo Custom Edition installs' maps folders holding the three resource
    maps: the folder its installer recorded (Windows registry: Microsoft
    Games\\Halo CE, "EXE Path"), then the usual places."""
    folders = []
    if sys.platform == "win32":
        try:
            import winreg
            for hive, key in ((winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Halo CE"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Microsoft Games\Halo CE"),
                              (winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Microsoft Games\Halo CE")):
                try:
                    with winreg.OpenKey(hive, key) as handle:
                        folders.append(winreg.QueryValueEx(handle, "EXE Path")[0])
                except OSError:
                    pass
        except ImportError:
            pass
        for variable in ("ProgramFiles(x86)", "ProgramFiles"):
            if os.environ.get(variable):
                folders.append(os.path.join(os.environ[variable], "Microsoft Games", "Halo Custom Edition"))
    found = []
    for folder in folders:
        maps = pc_maps_folder(folder) if folder and os.path.isdir(folder) else None
        if maps and maps not in found:
            found.append(maps)
    return found


def find_ce_installers() -> List[str]:
    """halocesetup*.exe in the player's Downloads, next to this tool or in
    the current folder (any other name is chosen by hand: Choose file...)."""
    places = [os.path.join(os.path.expanduser("~"), "Downloads"),
              os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__)),
              os.getcwd()]
    found = []
    for place in places:
        try:
            names = sorted(os.listdir(place))
        except OSError:
            continue
        for name in names:
            full = os.path.join(place, name)
            if (name.lower().startswith("halocesetup") and name.lower().endswith(".exe") and
                    os.path.isfile(full) and full not in found):
                found.append(full)
    return found


# ---------------------------------------------------------------------------
# the player's own Halo: The Master Chief Collection
#
# What the game reads from the Halo content folder (docs/mcc_cea_maps.md,
# port/linux/game/mcc_content.c, mcc_sound.c), the Vita's
# ux0:data/haloce-vita/mcc: halo1/maps (the campaign, the multiplayer maps,
# MCC's own bitmaps.map, ui.map for the menus when there is no Xbox ui.map),
# halo1/maps/custom_edition (the Halo PC files), and for the classic sound
# effects halo1/sound/pc/sounds_adpcm.fsb (88 MB) with its list
# lst/sounds_adpcm.lst.bin (the other lists only count what stays silent).
# Nothing else of MCC (dialogue and music banks, the other games) is needed.

STAGING_MCC = "mcc"  # -> ux0:data/haloce-vita/mcc, the game's Halo content folder there
# Halo PC's resource maps in MCC: the game takes them from here when the maps
# folder has none (port/linux/src/xbox_files.c, platform_halo_pc_resource_map)
MCC_CE_PARTS = ("halo1", "maps", "custom_edition")
MCC_STEAM_APP_ID = "976730"
MCC_FOLDER_NAME = "Halo The Master Chief Collection"
MCC_CAMPAIGN_MAPS = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")
MCC_MULTIPLAYER_MAPS = ("beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
                        "carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest", "dangercanyon",
                        "deathisland", "gephyrophobia", "icefields", "infinity", "timberland")
MCC_SOUND_PARTS = ("halo1", "sound", "pc")
MCC_SOUND_BANK = "sounds_adpcm.fsb"
MCC_SOUND_LISTS = ("sounds_adpcm.lst.bin", "sounds_dialogs.lst.bin", "sounds_music.lst.bin",
                   "sounds_stream.lst.bin", "sounds_debug.lst.bin")
MCC_FILE_MAXIMUM = 2 << 30


@dataclass
class MccFile:
    source: str
    parts: Tuple[str, ...]  # where in the output folder (and under ux0:data/haloce-vita)
    size: int
    kind: str  # "map", "resource", "bank", "list"


def _cache_map_ok(path: str) -> bool:
    """A Halo cache file of MCC's: 'head' (as it lies, "daeh") and version 13."""
    try:
        with open(path, "rb") as file:
            header = file.read(8)
    except OSError:
        return False
    return len(header) == 8 and header[:4] == b"daeh" and struct.unpack_from("<I", header, 4)[0] == 13


def _mcc_file_ok(item: MccFile, path: Optional[str] = None) -> bool:
    path = path or item.source
    name = item.parts[-1]
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as file:
            head = file.read(8)
    except OSError:
        return False
    if size == 0 or size > MCC_FILE_MAXIMUM:
        return False
    if item.kind == "map":
        return _cache_map_ok(path) if name != "bitmaps.map" else (len(head) >= 4 and head[:4] == b"\1\0\0\0")
    if item.kind == "resource":
        return resource_map_ok(path, name)
    if item.kind == "bank":
        return head[:4] == b"FSB5"
    if item.kind == "list":
        return len(head) >= 4 and struct.unpack_from("<I", head, 0)[0] == 1
    return False


def mcc_root_of(path: str) -> Optional[str]:
    """MCC's folder (the one holding halo1) from what the player chose: that
    folder, its halo1, halo1/maps or halo1/maps/custom_edition, a Steam
    library, its steamapps or steamapps/common."""
    if not path or not os.path.isdir(path):
        return None
    candidates = [path]
    base = os.path.basename(os.path.normpath(path)).lower()
    parent = os.path.dirname(os.path.normpath(path))
    if base == "halo1":
        candidates.append(parent)
    elif base == "maps":
        candidates.append(os.path.dirname(parent))
    elif base == "custom_edition":
        candidates.append(os.path.dirname(os.path.dirname(parent)))
    for root in (path, os.path.join(path, "steamapps"), os.path.join(path, "steamapps", "common"),
                 os.path.join(path, "common")):
        child = _find_child(root, MCC_FOLDER_NAME)
        if child:
            candidates.append(child)
    for candidate in candidates:
        halo1 = _find_child(candidate, "halo1")
        if halo1 and _find_child(_find_child(halo1, "maps"), "a10.map"):
            return candidate
    return None


def find_mcc_folders() -> List[str]:
    """MCC's folders on this computer: in each Steam library
    (libraryfolders.vdf, the registry), where Steam's uninstall entry says it
    is, and the Xbox app's XboxGames folders."""
    candidates = [os.path.join(library, "steamapps", "common", MCC_FOLDER_NAME) for library in steam_library_folders()]
    if sys.platform == "win32":
        try:
            import winreg
            key = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App " + MCC_STEAM_APP_ID
            for hive, view in ((winreg.HKEY_LOCAL_MACHINE, getattr(winreg, "KEY_WOW64_64KEY", 0)),
                               (winreg.HKEY_LOCAL_MACHINE, getattr(winreg, "KEY_WOW64_32KEY", 0))):
                try:
                    with winreg.OpenKey(hive, key, 0, winreg.KEY_READ | view) as handle:
                        candidates.append(winreg.QueryValueEx(handle, "InstallLocation")[0])
                except OSError:
                    pass
        except ImportError:
            pass
        for letter in "CDEFGHIJ":
            candidates.append("%s:\\XboxGames\\%s\\Content" % (letter, MCC_FOLDER_NAME))
    found = []
    for candidate in candidates:
        root = mcc_root_of(candidate) if candidate and os.path.isdir(candidate) else None
        if root and os.path.normcase(os.path.realpath(root)) not in \
                [os.path.normcase(os.path.realpath(seen)) for seen in found]:
            found.append(root)
    return found


@dataclass
class MccPlan:
    root: str
    files: List[MccFile]
    missing: List[str]  # required files not there (relative to MCC's folder)
    notes: List[str]  # optional ones not there, and what that means

    @property
    def size(self) -> int:
        return sum(item.size for item in self.files)

    def needed(self, staging: str) -> int:
        """The bytes still to copy into `staging` (files there at their size
        are kept)."""
        needed = 0
        for item in self.files:
            path = os.path.join(staging, *item.parts)
            if not (os.path.isfile(path) and os.path.getsize(path) == item.size):
                needed += item.size
        return needed


def mcc_plan(path: str) -> MccPlan:
    """What would be copied from MCC's folder `path` (see above), and what of
    it is missing."""
    root = mcc_root_of(path)
    if root is None:
        raise InstallerError("No Halo: The Master Chief Collection in %s: choose MCC's folder (Steam: right-click "
                             "the game, Manage, Browse local files), the one that has halo1." % path)
    files: List[MccFile] = []
    missing: List[str] = []
    notes: List[str] = []

    def take(folder_parts, name, parts, kind, required, note=""):
        folder = root
        for part in folder_parts:
            folder = _find_child(folder, part)
        source = _find_child(folder, name)
        label = "/".join(folder_parts + (name,))
        if source and os.path.isfile(source) and not os.path.islink(source):
            files.append(MccFile(source, parts, os.path.getsize(source), kind))
        elif required:
            missing.append(label)
        elif note:
            notes.append("%s is not there: %s" % (label, note))

    maps = (STAGING_MCC, "halo1", "maps")
    for stem in MCC_CAMPAIGN_MAPS:
        take(("halo1", "maps"), stem + ".map", maps + (stem + ".map",), "map", True)
    take(("halo1", "maps"), "bitmaps.map", maps + ("bitmaps.map",), "map", True)
    for stem in MCC_MULTIPLAYER_MAPS:
        take(("halo1", "maps"), stem + ".map", maps + (stem + ".map",), "map", False,
             "that multiplayer map is left out")
    take(("halo1", "maps"), "ui.map", maps + ("ui.map",), "map", False,
         "the menus need the Xbox ui.map then")
    for name, _ in PC_RESOURCE_MAPS:
        take(MCC_CE_PARTS, name, (STAGING_MCC,) + MCC_CE_PARTS + (name,), "resource", False,
             "the Halo Custom Edition files come from the Halo PC files step then")
    sound = (STAGING_MCC,) + MCC_SOUND_PARTS
    take(MCC_SOUND_PARTS, MCC_SOUND_BANK, sound + (MCC_SOUND_BANK,), "bank", False,
         "MCC's maps play without sound effects")
    for name in MCC_SOUND_LISTS:
        take(MCC_SOUND_PARTS + ("lst",), name, sound + ("lst", name), "list", False,
             "MCC's maps play without sound effects" if name == MCC_SOUND_LISTS[0] else "")
    return MccPlan(root, files, missing, notes)


def install_mcc_files(path: str, staging: str, progress: Progress = _no_progress,
                      log: Callable[[str], None] = print) -> MccPlan:
    """The Halo CE content of the player's own MCC
    copied into `staging`/mcc/ as the Vita has it (ux0:data/haloce-vita/mcc).
    Nothing is copied unless every required file is there; each file is
    checked (its header) before and after."""
    plan = mcc_plan(path)
    if plan.missing:
        raise InstallerError("MCC's folder %s lacks %s. Is Halo: Combat Evolved Anniversary installed in MCC "
                             "(Steam: the game's Properties, DLC)?" % (
                                 plan.root, ", ".join(plan.missing)))
    for item in plan.files:
        if not _mcc_file_ok(item):
            raise InstallerError("%s is not the file this build reads (its header does not match): is the MCC "
                                 "folder damaged? Steam's Verify integrity of game files mends it." % item.source)
    for note in plan.notes:
        log("Note: " + note)
    os.makedirs(staging, exist_ok=True)
    total = plan.size
    done = 0
    for item in plan.files:
        folder = staging
        for part in item.parts[:-1]:
            folder = safe_join(folder, part)
            os.makedirs(folder, exist_ok=True)
        destination = safe_join(folder, item.parts[-1])
        if os.path.isfile(destination) and os.path.getsize(destination) == item.size:
            log("%s already there" % "/".join(item.parts))
        else:
            partial = destination + ".part"
            try:
                with open(item.source, "rb") as reader, open(partial, "wb") as writer:
                    _copy_range(reader, writer, 0, item.size, progress, item.parts[-1], done, total)
            except BaseException:
                if os.path.exists(partial):
                    os.remove(partial)
                raise
            if os.path.getsize(partial) != item.size or not _mcc_file_ok(item, partial):
                os.remove(partial)
                raise InstallerError("%s came out damaged" % item.parts[-1])
            os.replace(partial, destination)
            log("%s (%s)" % ("/".join(item.parts), human_size(item.size)))
        done += item.size
        progress(item.parts[-1], done, total)
    return plan


def mcc_staged(staging: str) -> bool:
    """Whether the output folder has MCC's Halo CE content (its campaign and
    bitmaps.map)."""
    maps = os.path.join(staging, STAGING_MCC, "halo1", "maps")
    return all(os.path.isfile(os.path.join(maps, name))
               for name in [stem + ".map" for stem in MCC_CAMPAIGN_MAPS] + ["bitmaps.map"])


# ---------------------------------------------------------------------------
# the required Halo Custom Edition files: where they are in the output folder


@dataclass
class PcFilesStatus:
    found: Dict[str, str]  # name -> where (the output folder's maps, or MCC's custom_edition)
    missing: List[str]

    @property
    def complete(self) -> bool:
        return not self.missing


def pc_files_status(staging: str) -> PcFilesStatus:
    """Which of bitmaps.map, sounds.map and loc.map the output folder has, in
    maps/ or in MCC's halo1/maps/custom_edition/ (each header checked)."""
    found = {}
    missing = []
    for name, _ in PC_RESOURCE_MAPS:
        for where, parts in (("maps", (STAGING_MAPS,)), ("MCC's custom_edition", (STAGING_MCC,) + MCC_CE_PARTS)):
            path = os.path.join(staging, *parts, name)
            if os.path.isfile(path) and resource_map_ok(path, name):
                found[name] = where
                break
        else:
            missing.append(name)
    return PcFilesStatus(found, missing)


def pc_files_help(missing: Sequence[str]) -> str:
    """What is missing and where to get it, for the player."""
    return ("Required Halo Custom Edition files missing: %s. Get them from one of: your Halo: The Master Chief "
            "Collection on Steam (its halo1/maps/custom_edition folder), the free Halo Custom Edition installer "
            "(halocesetup_en_1.00.exe, %s), or a Halo Custom Edition install's maps folder. The Halo PC files "
            "step takes them from any of these." % (", ".join(missing), CE_INSTALLER_URL))


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
    """The three resource maps out of the installer's cabinet into
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
# the movies


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
    """Every bink/*.bik converted to `staging`/movies/<name>.mp4 as
    README.md's Movies section does (H.264 Constrained Baseline, no B-frames,
    640 wide, AAC). `quality` is "baseline" or "better" (both Vita-safe)."""
    ffmpeg_path = find_ffmpeg(ffmpeg)
    if not ffmpeg_path:
        raise InstallerError("ffmpeg was not found. The Windows program brings its own; otherwise install "
                             "ffmpeg (https://ffmpeg.org) or choose ffmpeg's program.")
    quality = MOVIE_QUALITY_ALIASES.get(quality, quality)
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
# VitaShell's FTP


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
    and any custom maps put there),
    movies/*.mp4, and MCC's files under mcc/ (halo1/maps, its custom_edition,
    halo1/sound/pc's bank and lists)."""
    items = []
    xbe = os.path.join(staging, STAGING_XBE)
    if os.path.isfile(xbe):
        items.append(UploadItem(xbe, VITA_GAME_FOLDER, STAGING_XBE, os.path.getsize(xbe)))
    mcc_maps = STAGING_MCC + "/halo1/maps"
    mcc_sound = STAGING_MCC + "/" + "/".join(MCC_SOUND_PARTS[:])
    for folder, suffixes in ((STAGING_MAPS, (".map", ".yelo")),
                             (STAGING_MOVIES, (".mp4",)),
                             (mcc_maps, (".map",)), (mcc_maps + "/custom_edition", (".map",)),
                             (mcc_sound, (".fsb",)), (mcc_sound + "/lst", (".lst.bin",))):
        directory = os.path.join(staging, *folder.split("/"))
        if not os.path.isdir(directory) or os.path.islink(directory):
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
    """The VPK to ux0:data/ (VitaShell installs it: the Vita cannot
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
# the PC build's data folder
#
# The game's data root (port/linux/src/xbox_files.c): the folder of its
# executable when that has a maps folder, the same layout as the Vita's
# ux0:data/haloce-vita. An unpacked release keeps the executable in
# build/linux or build/windows (tools/package_pc.py).


PC_BUILDS = (("windows", "halo.exe"), ("linux", "halo"))


def _pc_builds_here_first() -> Sequence[Tuple[str, str]]:
    return PC_BUILDS if sys.platform == "win32" else PC_BUILDS[::-1]


def pc_data_folder(path: str) -> Optional[str]:
    """The PC build's data folder from what the player chose: the unpacked
    release (the build for this system first) or the executable's folder."""
    if not path or not os.path.isdir(path):
        return None
    builds = _pc_builds_here_first()
    for name, program in builds:
        folder = os.path.join(path, "build", name)
        if os.path.isfile(os.path.join(folder, program)):
            return folder
    if any(os.path.isfile(os.path.join(path, program)) for _, program in builds):
        return path
    return None


def find_pc_builds() -> List[str]:
    """Unpacked PC builds next to this tool, in the current folder and in the
    user's usual folders (halo-ce-vita-<version>-pc-...)."""
    home = os.path.expanduser("~")
    places = [os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__)),
              os.getcwd()] + [os.path.join(home, name) for name in ("Downloads", "Desktop", "Documents")]
    found: List[str] = []
    for place in places:
        candidates = [place]
        try:
            candidates += [os.path.join(place, name) for name in sorted(os.listdir(place))
                           if name.lower().startswith("halo-ce-vita") and "-pc-" in name.lower()]
        except OSError:
            continue
        for candidate in candidates:
            folder = pc_data_folder(candidate)
            if folder and folder not in found:
                found.append(folder)
    return found


def pc_uploads(staging: str) -> List[UploadItem]:
    """What goes to a PC build: what was gathered, without the movies."""
    return [item for item in staged_uploads(staging)
            if item.folder != VITA_GAME_FOLDER + "/" + STAGING_MOVIES]


def copy_to_pc_build(staging: str, build: str, replace: bool = False, progress: Progress = _no_progress,
                     log: Callable[[str], None] = print) -> Dict[str, int]:
    """The gathered files into the PC build's data folder, laid out as in the
    output folder. Each is written beside its place, checked by its size and
    then moved in; files there at the right size are kept unless `replace`.
    The maps folder is made even when empty: the game takes the folder for
    its data root by it, and only then reads mcc/ there."""
    folder = pc_data_folder(build)
    if folder is None:
        raise InstallerError("No PC build in %s: choose the folder you unpacked "
                             "halo-ce-vita-<version>-pc-... into, or the one with the game's program." % build)
    items = pc_uploads(staging)
    if not items:
        raise InstallerError("Nothing to copy in %s: gather the files first." % staging)
    if os.path.realpath(staging) == os.path.realpath(folder):
        log("The output folder is the PC build's data folder: the files are already there")
        return {"copied": 0, "skipped": len(items)}

    def target(item: UploadItem) -> Tuple[Sequence[str], str]:
        parts = [part for part in item.folder[len(VITA_GAME_FOLDER):].split("/") if part]
        directory = folder
        for part in parts:
            directory = safe_join(directory, part)
        return parts, safe_join(directory, item.name)

    def kept(item: UploadItem, path: str) -> bool:
        return not replace and os.path.isfile(path) and os.path.getsize(path) == item.size

    places = [(item,) + target(item) for item in items]
    _check_space(folder, sum(item.size for item, _, path in places if not kept(item, path)))
    os.makedirs(safe_join(folder, STAGING_MAPS), exist_ok=True)
    total = sum(item.size for item in items)
    done = copied = skipped = 0
    for item, parts, path in places:
        label = "/".join(parts + [item.name])
        if kept(item, path):
            log("%s already there" % label)
            skipped += 1
        else:
            directory = folder
            for part in parts:
                directory = safe_join(directory, part)
                os.makedirs(directory, exist_ok=True)
            partial = path + ".part"
            try:
                with open(item.local, "rb") as reader, open(partial, "wb") as writer:
                    _copy_range(reader, writer, 0, item.size, progress, item.name, done, total)
                if os.path.getsize(partial) != item.size:
                    raise InstallerError("%s came out damaged" % label)
                os.replace(partial, path)
            except BaseException:
                if os.path.exists(partial):
                    os.remove(partial)
                raise
            log("%s (%s)" % (label, human_size(item.size)))
            copied += 1
        done += item.size
        progress(item.name, done, total)
    return {"copied": copied, "skipped": skipped}


# ---------------------------------------------------------------------------
# a PC install
#
# What Setup does on PC, as any installer does: the PC build's archive (next
# to the tool, or chosen) unpacked into the install folder, the game files
# gathered straight into its data folder, the shortcuts, on Windows an
# uninstaller and an Apps & Features entry under HKCU (no admin needed), and
# a record in the folder of everything written, which is how repair finds
# what is damaged and uninstall removes those files and no others.
#
# The release's signed manifest covers the VPK alone (tools/release/README.md:
# "The PC files are not signed"), so nothing here downloads a build: the tool
# has no way to verify one. The player gets it from the releases page, and
# its archive is read as untrusted all the same.


APP_NAME = "Halo CE"
PUBLISHER = "Halo CE for PS Vita (fan project)"
PROJECT_URL = "https://github.com/BirchWoodGod/halo-ce-vita"
INSTALL_RECORD = "halo-ce-install.json"
INSTALL_RECORD_MAXIMUM = 8 << 20
UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\HaloCE"
SHORTCUT_LINK = "Halo CE.lnk"  # Windows
SHORTCUT_ENTRY = "halo-ce.desktop"  # Linux
PACKAGE_ICON = "halo-ce.png"
PACKAGE_ENTRIES_MAXIMUM = 20000
PACKAGE_SIZE_MAXIMUM = 2 << 30
_PACKAGE_NAME = re.compile(r"halo-ce-vita-([0-9][0-9A-Za-z.\-]*)-pc-(?:windows|linux)")


def default_install_folder() -> str:
    """%LOCALAPPDATA%\\Programs\\Halo CE on Windows (no admin needed),
    ~/.local/share/halo-ce elsewhere."""
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Local")
        return os.path.join(base, "Programs", APP_NAME)
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, "halo-ce")


def check_install_folder(path: str) -> str:
    """The install folder as an absolute path: nothing that a shortcut could
    not hold."""
    path = os.path.expanduser((path or "").strip())
    if not path or not os.path.isabs(path) or any(ord(character) < 32 for character in path):
        raise InstallerError("Choose a full folder path to install in.")
    return os.path.normpath(path)


def pc_package_suffix() -> str:
    return "-pc-windows.zip" if sys.platform == "win32" else "-pc-linux.tar.gz"


def _tool_folder() -> str:
    return os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))


def find_pc_packages() -> List[str]:
    """This system's PC build archives next to the tool and in the current
    folder (not Downloads: what is picked by itself may be run), the newest
    version first."""
    found: List[str] = []
    for place in (_tool_folder(), os.getcwd()):
        try:
            names = os.listdir(place)
        except OSError:
            continue
        for name in names:
            full = os.path.join(place, name)
            if name.startswith("halo-ce-vita-") and name.endswith(pc_package_suffix()) and \
                    os.path.isfile(full) and full not in found:
                found.append(full)
    return sorted(found, key=lambda path: [int(number) for number in re.findall(r"\d+", os.path.basename(path))],
                  reverse=True)


@dataclass
class PackageFile:
    name: str  # as the archive names it
    parts: Tuple[str, ...]  # its place in the install folder
    size: int
    executable: bool = False

    @property
    def relative(self) -> str:
        return "/".join(self.parts)


@dataclass
class PcPackage:
    path: str
    kind: str  # "zip" or "tar"
    version: str
    files: List[PackageFile]

    @property
    def size(self) -> int:
        return sum(item.size for item in self.files)


def _archive_entries(path: str) -> Tuple[str, List[Tuple[str, int, bool, bool]]]:
    """The archive's kind and its files: (name, size, is a plain file or
    folder, executable). Folders are left out."""
    entries = []
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                mode = info.external_attr >> 16
                if info.flag_bits & 1:
                    raise InstallerError("%s is password protected: it is not a release's PC build." % path)
                if not info.is_dir():
                    entries.append((info.filename, info.file_size, not stat.S_ISLNK(mode), False))
        return "zip", entries
    if tarfile.is_tarfile(path):
        with tarfile.open(path, "r:*") as archive:
            for info in archive:
                if len(entries) > PACKAGE_ENTRIES_MAXIMUM:
                    break
                if not info.isdir():
                    entries.append((info.name, info.size, info.isreg(), bool(info.mode & 0o100)))
        return "tar", entries
    raise InstallerError("%s is not a PC build archive (.zip or .tar.gz)." % path)


def read_pc_package(path: str) -> PcPackage:
    """What the PC build's archive holds, checked before a byte of it is
    written: this system's build is in it, one top folder at most, every name
    safe (check_name), plain files only (no links, devices or doubles), sizes
    within bounds. CRCs are checked as it is unpacked."""
    if not path or not os.path.isfile(path):
        raise InstallerError("%s does not exist." % (path or "The PC build"))
    platform, program = _pc_builds_here_first()[0]
    wanted = "build/%s/%s" % (platform, program)
    try:
        kind, entries = _archive_entries(path)
    except (OSError, zipfile.BadZipFile, tarfile.TarError, EOFError, zlib.error) as error:
        raise InstallerError("%s cannot be read: %s" % (path, error))
    if len(entries) > PACKAGE_ENTRIES_MAXIMUM:
        raise InstallerError("%s holds too many files to be a PC build." % path)
    names = {name for name, _, _, _ in entries}
    prefix = ""
    if wanted not in names:
        tops = {name.split("/")[0] for name in names}
        prefix = tops.pop() + "/" if len(tops) == 1 else ""
        if not prefix or prefix + wanted not in names:
            raise InstallerError("%s has no %s: it is not the PC build for this system (%s). Download "
                                 "halo-ce-vita-<version>%s from %s" % (path, wanted, platform, pc_package_suffix(),
                                                                       RELEASES_URL))
    files: List[PackageFile] = []
    seen = set()
    for name, size, plain, executable in entries:
        if not plain:
            raise InstallerError("%s holds a link or a device (%s): it is not a release's PC build." % (path, name))
        if not name.startswith(prefix):
            raise InstallerError("%s has files outside its folder (%s)." % (path, name))
        parts = tuple(check_name(part) for part in name[len(prefix):].split("/"))
        key = "/".join(parts).lower()
        if key in seen:
            raise InstallerError("%s names %s twice." % (path, name))
        seen.add(key)
        files.append(PackageFile(name, parts, size, executable))
    if sum(item.size for item in files) > PACKAGE_SIZE_MAXIMUM:
        raise InstallerError("%s is too large to be a PC build." % path)
    found = _PACKAGE_NAME.search(prefix or os.path.basename(path)) or _PACKAGE_NAME.search(os.path.basename(path))
    return PcPackage(path, kind, found.group(1) if found else VERSION, files)


def _package_readers(package: PcPackage):
    """(the open archive, a function that opens one of its files)."""
    if package.kind == "zip":
        archive = zipfile.ZipFile(package.path)
        return archive, archive.open
    archive = tarfile.open(package.path, "r:*")
    return archive, archive.extractfile


def install_pc_package(package: PcPackage, folder: str, progress: Progress = _no_progress,
                       log: Callable[[str], None] = print) -> Dict[str, int]:
    """The archive unpacked into `folder`, the release's layout kept: each
    file written beside its place, checked by its size (and CRC) and moved
    in. Returns {relative path: size}."""
    os.makedirs(folder, exist_ok=True)
    log("SHA-256 of %s: %s" % (os.path.basename(package.path), file_sha256(package.path)))
    done = 0
    written: Dict[str, int] = {}
    try:
        archive, opener = _package_readers(package)
        with archive:
            for item in package.files:
                directory = folder
                for part in item.parts[:-1]:
                    directory = safe_join(directory, part)
                    os.makedirs(directory, exist_ok=True)
                path = safe_join(directory, item.parts[-1])
                partial = path + ".part"
                count = 0
                try:
                    with opener(item.name) as reader, open(partial, "wb") as writer:
                        for chunk in iter(lambda: reader.read(1 << 20), b""):
                            count += len(chunk)
                            if count > item.size:
                                raise InstallerError("%s is larger than its archive says." % item.name)
                            writer.write(chunk)
                            progress(item.parts[-1], done + count, package.size)
                    if count != item.size:
                        raise InstallerError("%s came out damaged." % item.name)
                    os.replace(partial, path)
                except BaseException:
                    if os.path.exists(partial):
                        os.remove(partial)
                    raise
                if item.executable:
                    os.chmod(path, 0o755)
                done += item.size
                written[item.relative] = item.size
                progress(item.parts[-1], done, package.size)
    except (zipfile.BadZipFile, tarfile.TarError, EOFError, zlib.error) as error:
        raise InstallerError("%s is damaged: %s. Download it again." % (package.path, error))
    except OSError as error:
        raise InstallerError("Cannot write the PC build into %s: %s. Is Halo CE running?" % (folder, error))
    log("PC build %s (%d files, %s) in %s" % (package.version, len(written), human_size(package.size), folder))
    return written


def read_install(folder: str) -> Optional[dict]:
    """The record of the install in `folder`, or None (none, or damaged)."""
    path = os.path.join(folder, INSTALL_RECORD)
    try:
        if os.path.getsize(path) > INSTALL_RECORD_MAXIMUM:
            return None
        with open(path, "r", encoding="utf-8") as file:
            record = json.load(file)
    except (OSError, ValueError):
        return None
    good = isinstance(record, dict) and all(isinstance(record.get(key), dict) for key in ("files", "game_files"))
    return record if good else None


def find_existing_install() -> Optional[str]:
    """Where Halo CE is installed for this user, if it is: where Apps &
    Features says on Windows, where Setup last put it, the default folder."""
    places = []
    if sys.platform == "win32":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY) as key:
                places.append(winreg.QueryValueEx(key, "InstallLocation")[0])
        except (ImportError, OSError):
            pass
    remembered = load_settings().get("install_folder")
    if isinstance(remembered, str):
        places.append(remembered)
    places.append(default_install_folder())
    return next((place for place in places if read_install(place) is not None), None)


def _recorded_path(folder: str, relative: str) -> str:
    """A path of the record, which is only a file in a folder the user can
    write: inside `folder`, checked as any other name."""
    return safe_join(folder, *str(relative).split("/"))


def check_install(folder: str) -> Tuple[List[str], List[str]]:
    """What is missing or the wrong size in the install: (program files,
    game files)."""
    record = read_install(folder)
    if record is None:
        raise InstallerError("No Halo CE install in %s." % folder)

    def damaged(files: dict) -> List[str]:
        found = []
        for relative, size in files.items():
            try:
                path = _recorded_path(folder, relative)
                good = os.path.isfile(path) and os.path.getsize(path) == size
            except InstallerError:
                good = False
            if not good:
                found.append(relative)
        return found
    return damaged(record["files"]), damaged(record["game_files"])


def installed_game_files(folder: str) -> Dict[str, int]:
    """The game files in the install's data folder: {relative path: size}."""
    data = pc_data_folder(folder)
    if data is None:
        return {}
    return {os.path.relpath(item.local, folder).replace(os.sep, "/"): item.size for item in pc_uploads(data)}


def _desktop_value(text: str) -> str:
    return text.replace("\\", "\\\\")


def _desktop_exec(path: str) -> str:
    """`path` as Exec= takes it: quoted, with what the quotes leave special
    escaped (the Desktop Entry specification's two levels)."""
    return _desktop_value('"%s"' % re.sub(r'(["`$\\])', r"\\\1", path)).replace("%", "%%")


def desktop_entry(folder: str, program: str, icon: str) -> str:
    return ("[Desktop Entry]\nType=Application\nName=%s\nComment=Halo: Combat Evolved on your own game files\n"
            "Exec=%s\nPath=%s\nIcon=%s\nTerminal=false\nCategories=Game;\n"
            % (APP_NAME, _desktop_exec(program), _desktop_value(folder), _desktop_value(icon)))


# One line for PowerShell's WScript.Shell: what varies comes in through the
# environment, never in the command's text, so no folder name is read as code.
SHORTCUT_SCRIPT = ("[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
                   "$path = Join-Path ([Environment]::GetFolderPath($env:HCV_PLACE)) '" + SHORTCUT_LINK + "'; "
                   "$link = (New-Object -ComObject WScript.Shell).CreateShortcut($path); "
                   "$link.TargetPath = $env:HCV_TARGET; $link.WorkingDirectory = $env:HCV_FOLDER; "
                   "$link.IconLocation = $env:HCV_TARGET; $link.Save(); Write-Output $path")


def _windows_shortcut(place: str, target: str, folder: str) -> str:
    """A shortcut in the Desktop or Programs (Start menu) folder, wherever
    Windows keeps them (OneDrive moves the desktop). Returns its path."""
    system = os.environ.get("SystemRoot", r"C:\Windows")
    shell = os.path.join(system, "System32", "WindowsPowerShell", "v1.0", "powershell.exe")
    result = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-Command", SHORTCUT_SCRIPT],
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
                            env=dict(os.environ, HCV_PLACE=place, HCV_TARGET=target, HCV_FOLDER=folder),
                            **_hidden_window())
    path = result.stdout.decode("utf-8", "replace").strip().lstrip("\ufeff")
    if result.returncode != 0 or not path:
        raise OSError(result.stderr.decode("utf-8", "replace").strip() or "PowerShell failed")
    return path


def make_shortcuts(folder: str, desktop: bool, start_menu: bool) -> Tuple[List[str], List[str]]:
    """The desktop shortcut and the Start menu or applications menu entry.
    Returns (their paths, warnings): one that fails does not stop the
    install."""
    made: List[str] = []
    warnings: List[str] = []
    data = pc_data_folder(folder) or folder
    if sys.platform == "win32":
        target = os.path.join(data, "halo.exe")
        for wanted, place, name in ((desktop, "Desktop", "desktop shortcut"), (start_menu, "Programs", "Start menu entry")):
            if wanted:
                try:
                    made.append(_windows_shortcut(place, target, folder))
                except (OSError, subprocess.SubprocessError) as error:
                    warnings.append("The %s could not be made: %s" % (name, error))
        return made, warnings
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    launcher = os.path.join(folder, "halo.sh")
    entry = desktop_entry(folder, launcher, os.path.join(folder, PACKAGE_ICON))
    places = [(start_menu, os.path.join(base, "applications"), "applications menu entry"),
              (desktop, os.path.join(os.path.expanduser("~"), "Desktop"), "desktop shortcut")]
    for wanted, place, name in places:
        if wanted:
            try:
                os.makedirs(place, exist_ok=True)
                path = os.path.join(place, SHORTCUT_ENTRY)
                with open(path, "w", encoding="utf-8") as file:
                    file.write(entry)
                os.chmod(path, 0o755)
                made.append(path)
            except OSError as error:
                warnings.append("The %s could not be made: %s" % (name, error))
    return made, warnings


def remove_shortcuts(paths: Sequence[str]) -> None:
    """The shortcuts Setup made, by exact name: a damaged record can name no
    other file."""
    for path in paths:
        if isinstance(path, str) and os.path.basename(path) in (SHORTCUT_LINK, SHORTCUT_ENTRY):
            try:
                if os.path.isfile(path) and not os.path.islink(path):
                    os.remove(path)
            except OSError:
                pass


def write_uninstaller(folder: str) -> Tuple[str, List[str]]:
    """A copy of this tool in the install folder: (its name there, the command
    that runs it)."""
    frozen = getattr(sys, "frozen", False)
    name = "uninstall.exe" if frozen else "uninstall.py"
    path = safe_join(folder, name)
    source = sys.executable if frozen else os.path.abspath(__file__)
    if os.path.realpath(source) != os.path.realpath(path):
        shutil.copy2(source, path)
    return name, ([path, "--uninstall"] if frozen else [sys.executable, path, "--uninstall"])


def register_windows(folder: str, version: str, command: Sequence[str], program: str, size: int) -> None:
    """The Apps & Features entry, the current user's own: no admin needed."""
    import winreg
    line = subprocess.list2cmdline(list(command))
    texts = {"DisplayName": APP_NAME, "DisplayVersion": version, "Publisher": PUBLISHER, "InstallLocation": folder,
             "UninstallString": line, "DisplayIcon": program, "URLInfoAbout": PROJECT_URL}
    numbers = {"EstimatedSize": min(size // 1024, 0x7fffffff), "NoModify": 1, "NoRepair": 1}
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY) as key:
        for name, value in texts.items():
            winreg.SetValueEx(key, name, 0, winreg.REG_SZ, value)
        for name, value in numbers.items():
            winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, value)


def unregister_windows() -> None:
    import winreg
    try:
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY)
    except OSError:
        pass


def finish_pc_install(folder: str, version: str, files: Dict[str, int], desktop: bool, start_menu: bool,
                      log: Callable[[str], None] = print) -> List[str]:
    """What makes the files an install: the maps folder the game takes the
    folder by (as copy_to_pc_build), the shortcuts, the uninstaller and the
    Apps & Features entry, and the record. Returns warnings."""
    data = pc_data_folder(folder)
    if data is None:
        raise InstallerError("No PC build in %s." % folder)
    os.makedirs(safe_join(data, STAGING_MAPS), exist_ok=True)
    files = dict(files)
    old = read_install(folder) or {}
    old_shortcuts = old.get("shortcuts")
    if isinstance(old_shortcuts, list):
        remove_shortcuts(old_shortcuts)
    warnings: List[str] = []
    uninstaller = ""
    if sys.platform != "win32":
        icon = safe_join(folder, PACKAGE_ICON)
        with open(icon, "wb") as file:
            file.write(icon_png(128))
        files[PACKAGE_ICON] = os.path.getsize(icon)
    game_files = installed_game_files(folder)
    shortcuts, failed = make_shortcuts(folder, desktop, start_menu)
    warnings += failed
    record = {"format": 1, "name": APP_NAME, "version": version, "files": files, "game_files": game_files,
              "desktop": desktop, "start_menu": start_menu, "shortcuts": shortcuts}
    if sys.platform == "win32":
        uninstaller, command = write_uninstaller(folder)
        record["uninstaller"] = uninstaller
        try:
            register_windows(folder, version, command, os.path.join(data, "halo.exe"),
                             sum(files.values()) + sum(game_files.values()))
        except (OSError, ImportError) as error:
            warnings.append("The Apps & Features entry could not be made: %s" % error)
    partial = safe_join(folder, INSTALL_RECORD + ".part")
    with open(partial, "w", encoding="utf-8") as file:
        json.dump(record, file, indent=1, sort_keys=True)
    os.replace(partial, safe_join(folder, INSTALL_RECORD))
    settings = load_settings()
    settings["install_folder"] = folder
    save_settings(settings)
    for warning in warnings:
        log("Warning: " + warning)
    log("Installed in %s" % folder)
    return warnings


def _remove_empty_folders(folder: str) -> None:
    """Empty folders under `folder`, then `folder` if it is empty too."""
    for directory, _, _ in os.walk(folder, topdown=False):
        try:
            os.rmdir(directory)
        except OSError:
            pass


def uninstall_pc(folder: str, progress: Progress = _no_progress, log: Callable[[str], None] = print) -> Dict[str, int]:
    """Everything the record names, and no other file: what the game wrote
    later (saved games, config.toml) stays, and the folder with it. Returns
    counts: removed, kept (what is left in the folder)."""
    record = read_install(folder)
    if record is None:
        raise InstallerError("No Halo CE install in %s." % folder)
    names = list(record["files"]) + list(record["game_files"])
    if isinstance(record.get("uninstaller"), str):
        names.append(record["uninstaller"])
    removed = 0
    for index, relative in enumerate(names):
        try:
            path = _recorded_path(folder, relative)
            if os.path.isfile(path):
                os.remove(path)
                removed += 1
        except InstallerError as error:
            log("Left alone: %s" % error)
        except OSError as error:
            log("Could not remove %s: %s" % (relative, error))
        progress(os.path.basename(str(relative)), index + 1, len(names))
    if isinstance(record.get("shortcuts"), list):
        remove_shortcuts(record["shortcuts"])
    if sys.platform == "win32":
        unregister_windows()
    os.remove(os.path.join(folder, INSTALL_RECORD))
    settings = load_settings()
    if settings.pop("install_folder", None) is not None:
        save_settings(settings)
    _remove_empty_folders(folder)
    kept = sum(len(found) for _, _, found in os.walk(folder))
    log("Removed %d files from %s (%d left)" % (removed, folder, kept))
    return {"removed": removed, "kept": kept}


def snapshot_folder(folder: str) -> set:
    """What is in `folder` before an install, to take it back to that."""
    found = set()
    for directory, folders, names in os.walk(folder):
        for name in folders + names:
            found.add(os.path.relpath(os.path.join(directory, name), folder))
    return found


def roll_back_install(folder: str, before: set) -> None:
    """An install that did not finish: everything it added, and nothing that
    was there before it."""
    for directory, _, names in os.walk(folder, topdown=False):
        for name in names:
            path = os.path.join(directory, name)
            if os.path.relpath(path, folder) not in before:
                try:
                    os.remove(path)
                except OSError:
                    pass
        relative = os.path.relpath(directory, folder)
        if relative != "." and relative not in before:
            try:
                os.rmdir(directory)
            except OSError:
                pass
    if not before:
        try:
            os.rmdir(folder)
        except OSError:
            pass


def launch_pc(folder: str) -> None:
    """The game, as its shortcut starts it: never through a shell, and only
    the install's own program."""
    data = pc_data_folder(folder)
    if data is None:
        raise InstallerError("No PC build in %s." % folder)
    launcher = os.path.join(folder, "halo.sh")
    program = os.path.join(data, "halo.exe") if sys.platform == "win32" else \
        launcher if os.path.isfile(launcher) else os.path.join(data, "halo")
    subprocess.Popen([program], cwd=folder, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)


# The uninstaller is this one-file program, in the folder it empties, and a
# running program cannot delete itself on Windows: it runs again from a copy
# in the temporary folder, and that copy is deleted once it has closed.
TEMPORARY_UNINSTALLER = "halo-ce-uninstall-"


def uninstall_from_outside(folder: str) -> bool:
    """When this is the install's own uninstall.exe: started again from a
    temporary copy. True if so (this one then closes)."""
    if sys.platform != "win32" or not getattr(sys, "frozen", False):
        return False
    if os.path.dirname(os.path.realpath(sys.executable)) != os.path.realpath(folder):
        return False
    temporary = tempfile.mkdtemp(prefix=TEMPORARY_UNINSTALLER)
    copy = os.path.join(temporary, "uninstall.exe")
    shutil.copy2(sys.executable, copy)
    subprocess.Popen([copy, "--uninstall", folder], cwd=temporary)
    return True


def delete_temporary_uninstaller() -> None:
    """The temporary copy's own folder, a moment after it closes. The one
    command this tool gives a shell: fixed text, the folder (made by
    uninstall_from_outside, by its name) coming in through the environment."""
    folder = os.path.dirname(os.path.realpath(sys.executable))
    if sys.platform != "win32" or not getattr(sys, "frozen", False) or \
            not os.path.basename(folder).startswith(TEMPORARY_UNINSTALLER):
        return
    shell = os.environ.get("ComSpec") or os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "cmd.exe")
    subprocess.Popen('"%s" /c ping -n 4 127.0.0.1 >nul & rmdir /s /q "%%HCV_DELETE%%"' % shell, cwd=tempfile.gettempdir(),
                     env=dict(os.environ, HCV_DELETE=folder), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, **_hidden_window())


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
    """What this copy of the tool can do: tkinter for the window, the
    license's text (the Windows program must bring it), ffmpeg for the movies
    (the Windows program brings its own)."""
    lines = ["halo_ce_vita_installer %s, Python %s on %s" % (VERSION, sys.version.split()[0], sys.platform)]
    good = True
    try:
        import tkinter  # noqa: F401
        lines.append("tkinter: ok")
    except ImportError as error:
        lines.append("tkinter: missing (%s)" % error)
        good = False
    whole = "TERMS AND CONDITIONS" in license_text()
    lines.append("license text: %s" % ("ok" if whole else "only the short notice"))
    good = good and (whole or not getattr(sys, "frozen", False))
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
        description="Halo CE Setup: installs Halo: Combat Evolved on a PS Vita or a PC. With no command, the "
                    "Setup window opens. Every step reads your own files; no game data comes with this tool.")
    parser.add_argument("--version", action="version", version=VERSION)
    parser.add_argument("--out", default=default_output_folder(),
                        help="the folder the files are gathered in (default: %(default)s)")
    parser.add_argument("--uninstall", nargs="?", const="", metavar="FOLDER",
                        help="open the window to uninstall the PC install (what Apps & Features runs)")
    commands = parser.add_subparsers(dest="command")
    step = commands.add_parser("xbox", help="manual: maps and default.xbe from your Xbox disc image or folder")
    step.add_argument("source", help="the Xbox disc image (.iso / .xiso) or the unpacked game folder")
    step = commands.add_parser("mcc", help="automatic: MCC's Halo CE content from your own Halo: The Master Chief "
                                           "Collection (Steam)")
    step.add_argument("source", nargs="?", help="MCC's folder (default: found in your Steam libraries)")
    step = commands.add_parser("movies", help="manual, Vita only: the disc's movies converted to MP4 with ffmpeg")
    step.add_argument("source", help="the Xbox disc image or the unpacked game folder")
    step.add_argument("--ffmpeg", help="ffmpeg's program (default: the bundled one, or on the PATH)")
    step.add_argument("--quality", choices=sorted(FFMPEG_MOVIE_ARGS) + sorted(MOVIE_QUALITY_ALIASES),
                      default="baseline", help="baseline (as README.md) or better (the same Vita-safe profile at "
                                               "a higher quality: larger, slower); high is better's old name")
    step = commands.add_parser("pc-files", help="manual: Halo Custom Edition's bitmaps.map, sounds.map, loc.map "
                                                "(required on the Vita)")
    which = step.add_mutually_exclusive_group(required=True)
    which.add_argument("--mcc", nargs="?", const="", metavar="FOLDER",
                       help="from Halo MCC on Steam (found by itself, or give its custom_edition folder)")
    which.add_argument("--installer", metavar="EXE", help="from the Halo Custom Edition installer (any name)")
    which.add_argument("--folder", help="from a folder that has them (a Custom Edition install's maps)")
    commands.add_parser("check", help="check the output folder for the required Halo Custom Edition files "
                                      "(bitmaps.map, sounds.map, loc.map)")
    step = commands.add_parser("check-installer", help="tell whether a file is the Halo Custom Edition installer")
    step.add_argument("exe")
    step = commands.add_parser("upload", help="copy the gathered files to the Vita (VitaShell FTP)")
    step.add_argument("host", help="the Vita's address, as VitaShell's FTP screen shows it")
    step.add_argument("--port", type=int, default=FTP_PORT)
    step.add_argument("--replace", action="store_true", help="copy files already on the Vita again")
    step = commands.add_parser("vpk", help="copy the VPK to ux0:data/ (install it with VitaShell)")
    step.add_argument("host")
    step.add_argument("vpk", nargs="?", help="the VPK (default: one next to this tool)")
    step.add_argument("--port", type=int, default=FTP_PORT)
    step = commands.add_parser("pc-copy", help="copy the gathered files into a PC build's data folder")
    step.add_argument("build", nargs="?", help="the unpacked PC build (default: one found next to this tool or in "
                                               "your Downloads, Desktop or Documents)")
    step.add_argument("--replace", action="store_true", help="copy files already there again")
    step = commands.add_parser("pc-install", help="install the PC build: the archive, the gathered files, shortcuts "
                                                  "and, on Windows, the uninstaller and Apps & Features entry")
    step.add_argument("folder", nargs="?", help="the install folder (default: %s)" % default_install_folder())
    step.add_argument("--package", help="the PC build's archive (default: one next to this tool)")
    step.add_argument("--desktop-shortcut", action="store_true", help="make a desktop shortcut")
    step.add_argument("--start-menu", action="store_true", help="make a Start menu or applications menu entry")
    step = commands.add_parser("pc-uninstall", help="remove a PC install: the files it wrote and its shortcuts")
    step.add_argument("folder", nargs="?", help="the install folder (default: the install found)")
    step = commands.add_parser("selftest", help="check this copy of the tool (the window, ffmpeg)")
    step.add_argument("--report", help="write the results to this file too")
    arguments = parser.parse_args(argv)
    if arguments.command == "selftest":
        return self_test(arguments.report)

    if arguments.uninstall is not None:
        folder = arguments.uninstall or find_existing_install()
        if not folder or read_install(folder) is None:
            _console_log("No Halo CE install found: give its folder (--uninstall FOLDER).")
            return 1
        if uninstall_from_outside(folder):
            return 0
        result = run_window(arguments.out, folder)
        delete_temporary_uninstaller()
        return result
    if not arguments.command:
        return run_window(arguments.out)
    progress = _ConsoleProgress()
    out = arguments.out
    try:
        if arguments.command == "xbox":
            _check_space(out, xbox_files_size(arguments.source))
            written = extract_xbox_files(arguments.source, out, progress, _console_log)
            _console_log("Xbox files ready in %s (%d files)" % (out, len(written)))
        elif arguments.command == "mcc":
            folders = [arguments.source] if arguments.source else find_mcc_folders()
            if not folders:
                raise InstallerError("Halo: The Master Chief Collection was not found in your Steam libraries: "
                                     "give its folder (mcc FOLDER)")
            plan = mcc_plan(folders[0])
            _check_space(out, plan.needed(out))
            install_mcc_files(folders[0], out, progress, _console_log)
            _console_log("MCC files ready in %s (%d files, %s)" % (out, len(plan.files), human_size(plan.size)))
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
                folders = [pc_maps_folder(arguments.mcc) or arguments.mcc] if arguments.mcc else \
                    find_mcc_custom_edition()
                if not folders:
                    raise InstallerError("Halo MCC's halo1/maps/custom_edition was not found: give its folder "
                                         "(--mcc FOLDER)")
                copy_pc_maps_from_folder(folders[0], out, progress, _console_log)
        elif arguments.command == "check":
            status = pc_files_status(out)
            if not status.complete:
                _console_log(pc_files_help(status.missing))
                return 1
            _console_log("bitmaps.map, sounds.map and loc.map: in place (%s)"
                         % " and ".join(sorted(set(status.found.values()))))
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
            status = pc_files_status(out)
            if not status.complete:
                _console_log("Warning: " + pc_files_help(status.missing))
            _console_log("Copying %d files (%s) to %s:%d. Keep the Vita awake." %
                         (len(items), human_size(sum(item.size for item in items)), arguments.host, arguments.port))
            result = upload_files(arguments.host, items, arguments.port, arguments.replace, progress, _console_log)
            _console_log("Done: %(copied)d copied, %(skipped)d already there" % result)
        elif arguments.command == "vpk":
            vpk = arguments.vpk or (find_local_vpks() or [None])[0]
            if not vpk:
                raise InstallerError("No VPK given and none next to this tool. Download halo-ce-vita-<version>-vita.vpk from " +
                                     RELEASES_URL)
            name = upload_vpk(arguments.host, vpk, arguments.port, progress, _console_log)
            _console_log(VPK_INSTRUCTIONS.format(name=name))
        elif arguments.command == "pc-copy":
            build = arguments.build or (find_pc_builds() or [None])[0]
            if not build:
                raise InstallerError("No PC build found: give its folder (pc-copy FOLDER)")
            result = copy_to_pc_build(out, build, arguments.replace, progress, _console_log)
            _console_log("Done: %(copied)d copied, %(skipped)d already there" % result)
        elif arguments.command == "pc-install":
            package_path = arguments.package or (find_pc_packages() or [None])[0]
            if not package_path:
                raise InstallerError("No PC build next to this tool: give it (--package). Download "
                                     "halo-ce-vita-<version>%s from %s" % (pc_package_suffix(), RELEASES_URL))
            folder = check_install_folder(arguments.folder or default_install_folder())
            package = read_pc_package(package_path)
            files = install_pc_package(package, folder, progress, _console_log)
            if pc_uploads(out):
                copy_to_pc_build(out, folder, False, progress, _console_log)
            else:
                _console_log("No game files gathered in %s: the game will ask for them." % out)
            finish_pc_install(folder, package.version, files, arguments.desktop_shortcut, arguments.start_menu,
                              _console_log)
        elif arguments.command == "pc-uninstall":
            folder = arguments.folder or find_existing_install()
            if not folder:
                raise InstallerError("No Halo CE install found: give its folder (pc-uninstall FOLDER)")
            result = uninstall_pc(check_install_folder(folder), progress, _console_log)
            _console_log("Done: %(removed)d files removed, %(kept)d left in the folder" % result)
    except InstallerError as error:
        _console_log("Error: %s" % error)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


# ---------------------------------------------------------------------------
# the window: a standard Setup wizard
#
# Next, next, finish, with what every Windows installer has: < Back, Next >
# and Cancel at the bottom right, a picture on the first and last pages and a
# white band on the others, one progress bar. ttk's native theme ('vista' on
# Windows), nothing styled but the white of those bands.


def _windows_dpi_awareness() -> None:
    """Sharp text on scaled displays: Windows would otherwise draw the
    window at 96 DPI and stretch it. Also names the process, so the taskbar
    shows the window's own icon, not Python's."""
    if sys.platform != "win32":
        return
    import ctypes
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)  # system DPI aware
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("HaloCE.Setup")
    except (AttributeError, OSError):
        pass


SETTINGS_FILE = "installer-settings.json"
LOG_FILE = "setup-log.txt"


def settings_path() -> str:
    """Setup's settings (where it installed): a small JSON file in the
    user's application data folder."""
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Roaming")
        folder = os.path.join(base, "HaloCEVita")
    elif sys.platform == "darwin":
        folder = os.path.join(os.path.expanduser("~"), "Library", "Application Support", "HaloCEVita")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
        folder = os.path.join(base, "halo-ce-vita")
    return os.path.join(folder, SETTINGS_FILE)


def load_settings() -> dict:
    """The saved settings, or none (a missing or damaged file is no
    settings)."""
    try:
        with open(settings_path(), "r", encoding="utf-8") as file:
            settings = json.load(file)
        return settings if isinstance(settings, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(settings: dict) -> bool:
    """Writes the settings (to a temporary file, then renamed over the
    old one). A folder that cannot be written only means they are not kept."""
    path = settings_path()
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        partial = path + ".part"
        with open(partial, "w", encoding="utf-8") as file:
            json.dump(settings, file, indent=1, sort_keys=True)
        os.replace(partial, path)
        return True
    except OSError:
        return False


def license_text() -> str:
    """The GPL's text: inside the Windows program, or the repository's."""
    here = os.path.dirname(os.path.abspath(__file__))
    for path in (os.path.join(getattr(sys, "_MEIPASS", here), "LICENSE"), os.path.join(here, "..", "..", "LICENSE")):
        try:
            with open(path, "r", encoding="utf-8") as file:
                return file.read()
        except OSError:
            pass
    return "Halo CE for PS Vita is free software under the GNU General Public License, version 3 only.\n\n" \
           "The license's full text: https://www.gnu.org/licenses/gpl-3.0.txt\n"


# The window's icon, drawn here (no image files): a blue rounded square with
# a white arrow going down into a tray. Generic on purpose: no game artwork.
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def _icon_sample(u: float, v: float):
    margin, radius = 0.04, 0.21
    dx = max(abs(u - 0.5) - (0.5 - margin - radius), 0.0)
    dy = max(abs(v - 0.5) - (0.5 - margin - radius), 0.0)
    if dx * dx + dy * dy > radius * radius:
        return None
    white = (255, 255, 255)
    if 0.44 <= u <= 0.56 and 0.17 <= v <= 0.46:  # the arrow's shaft
        return white
    if 0.40 <= v <= 0.65 and abs(u - 0.5) <= (0.65 - v) / 0.25 * 0.21:  # its head
        return white
    if 0.73 <= v <= 0.82 and 0.21 <= u <= 0.79:  # the tray
        return white
    if 0.57 <= v <= 0.82 and (0.21 <= u <= 0.30 or 0.70 <= u <= 0.79):
        return white
    top, bottom = (0x3a, 0x96, 0xdd), (0x00, 0x5f, 0xb8)
    return tuple(round(a + (b - a) * v) for a, b in zip(top, bottom))


def _png(width: int, height: int, rgba: bytes) -> bytes:
    row = width * 4
    raw = b"".join(b"\0" + rgba[y * row:(y + 1) * row] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def icon_png(size: int) -> bytes:
    """The icon as a PNG of size x size pixels (each pixel the average of
    a grid of samples, so the edges are smooth)."""
    grid = 4 if size <= 64 else 2
    out = bytearray()
    for y in range(size):
        for x in range(size):
            red = green = blue = alpha = 0
            for j in range(grid):
                v = (y + (j + 0.5) / grid) / size
                for i in range(grid):
                    colour = _icon_sample((x + (i + 0.5) / grid) / size, v)
                    if colour:
                        red += colour[0]
                        green += colour[1]
                        blue += colour[2]
                        alpha += 1
            if alpha:
                out += bytes((red // alpha, green // alpha, blue // alpha, 255 * alpha // (grid * grid)))
            else:
                out += b"\0\0\0\0"
    return _png(size, size, bytes(out))


def write_icon(path: str, sizes: Sequence[int] = ICON_SIZES) -> None:
    """The icon as a Windows .ico (PNG images inside, every size Windows
    asks for): the program's icon in the Windows build."""
    images = [icon_png(size) for size in sizes]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = b""
    for size, image in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    with open(path, "wb") as file:
        file.write(header + entries + b"".join(images))


def sidebar_png(width: int, height: int) -> bytes:
    """The picture on the first and last pages, drawn here: a night sky over
    a dark horizon's curve, a few stars. No artwork of anyone's."""
    stars = random.Random(11)
    bright = {}
    for _ in range(width * height // 900):
        bright[stars.randrange(width), stars.randrange(height * 7 // 10)] = stars.randrange(120, 255)
    centre_x, centre_y = width * 0.15, height * 1.55
    radius = height * 0.85
    out = bytearray()
    for y in range(height):
        share = y / max(height - 1, 1)
        sky = (int(7 + 16 * share), int(12 + 58 * share), int(34 + 96 * share))
        for x in range(width):
            star = bright.get((x, y))
            distance = ((x - centre_x) ** 2 + (y - centre_y) ** 2) ** 0.5 - radius
            if distance < 0:
                glow = max(0.0, 1.0 + distance / (height * 0.05))
                out += bytes((int(6 + 40 * glow), int(16 + 96 * glow), int(26 + 120 * glow), 255))
            elif distance < height * 0.03:
                glow = 1.0 - distance / (height * 0.03)
                out += bytes((int(sky[0] + 60 * glow), int(sky[1] + 110 * glow), int(sky[2] + 110 * glow), 255))
            elif star:
                out += bytes((star, star, min(star + 20, 255), 255))
            else:
                out += bytes(sky + (255,))
    return _png(width, height, bytes(out))


def run_window(out: str, uninstall: Optional[str] = None) -> int:
    try:
        import tkinter
        from tkinter import ttk  # noqa: F401 (the window's widgets)
    except ImportError:
        _console_log("tkinter is not available: use the command-line steps (--help), or install Python's "
                     "tkinter (python3-tk).")
        return 1
    _windows_dpi_awareness()
    try:
        root = tkinter.Tk()
    except tkinter.TclError as error:
        _console_log("Cannot open a window (%s): use the command-line steps (--help)." % error)
        return 1
    InstallerWindow(root, out, uninstall)
    root.mainloop()
    return 0


def _native_theme(style) -> str:
    """Windows' own look, or the system's nearest."""
    names = style.theme_names()
    for name in ("vista", "aqua", "clam"):
        if name in names:
            style.theme_use(name)
            return name
    return style.theme_use()


class InstallerWindow:
    WIDTH, HEIGHT = 620, 480  # at 100%
    SIDEBAR = 164
    EXTERIOR = ("welcome", "finish")
    # each page's title and the line below it
    PAGES = {
        "welcome": ("Welcome to Halo CE Setup", ""),
        "maintenance": ("Halo CE Is Already Installed", "Choose what Setup should do."),
        "license": ("License Agreement", "Please read the license before you install Halo CE."),
        "game": ("Choose a Game", "Which game do you want to install?"),
        "platform": ("Where to Install", "Choose where you will play Halo CE."),
        "game_files": ("Game Files", "Choose where Setup gets the game files from."),
        "destination": ("Destination", ""),
        "ready": ("Ready to Install", "Setup is ready to install Halo CE."),
        "installing": ("Installing", "Please wait while Setup installs Halo CE."),
        "finish": ("Setup completed", ""),
    }
    NOTICE = ("This is an unofficial fan project, not affiliated with or endorsed by Microsoft or Bungie. No game "
              "data comes with it: Setup uses your own copy of the game.")

    def __init__(self, root, out: str, uninstall: Optional[str] = None):
        import tkinter
        from tkinter import ttk
        self.tk = tkinter
        self.ttk = ttk
        self.root = root
        root.title("%s Setup" % APP_NAME)
        self.messages: "queue.Queue" = queue.Queue()
        self.worker: Optional[threading.Thread] = None
        self.tester: Optional[threading.Thread] = None
        self.cancel = threading.Event()
        self.page = ""
        self.outcome = ""  # at the end: done, failed or cancelled
        self.reason = ""
        self.warnings: List[str] = []
        self.log_lines: List[str] = []
        self.left_behind = 0
        self.direct = uninstall is not None  # started as the uninstaller: straight to it
        self.existing = uninstall or find_existing_install()
        self.damaged: Tuple[List[str], List[str]] = ([], [])
        self.package_cache: Dict[str, PcPackage] = {}
        self.game_bytes = 0
        self.rollback: Optional[set] = None

        var = tkinter.StringVar
        self.action = var(value="uninstall" if self.direct else "repair" if self.existing else "install")
        self.accepted = var(value="no")
        self.game = var(value=GAMES[0].key)
        self.platform = var(value=PLATFORMS[0].key)
        self.mcc_root = var(value=(find_mcc_folders() or [""])[0])
        self.mcc_found = bool(self.mcc_root.get())
        self.mode = var(value="automatic" if self.mcc_found else "manual")
        self.xbox_source = var()
        self.ce_source = var(value=next(iter(find_mcc_custom_edition() + find_ce_installers() +
                                             find_ce_install_folders()), ""))
        self.quality = var(value="baseline")
        self.movies = tkinter.BooleanVar(value=bool(find_ffmpeg()))
        self.host = var()
        self.vpk = var(value=(find_local_vpks() or [""])[0])
        self.out = var(value=out)
        self.install_folder = var(value=self.existing or default_install_folder())
        self.package = var(value=(find_pc_packages() or [""])[0])
        self.package_found = bool(self.package.get())
        self.desktop_shortcut = tkinter.BooleanVar(value=True)
        self.start_menu = tkinter.BooleanVar(value=True)
        self.launch = tkinter.BooleanVar(value=True)
        self.space = var()
        self.test_result = var()
        self.current_task = var(value="Preparing...")
        self.current_file = var()
        self.percent = var()

        self.style = ttk.Style(root)
        self.theme = _native_theme(self.style)
        self.scale = max(1.0, root.winfo_fpixels("1i") / 96.0)
        self._make_fonts()
        self._images: Dict[str, object] = {}
        try:
            self._icons = [tkinter.PhotoImage(data=icon_png(size), format="png") for size in (64, 32, 16)]
            root.iconphoto(True, *self._icons)
        except tkinter.TclError:
            pass
        self.width, self.height = self.px(self.WIDTH), self.px(self.HEIGHT)
        root.geometry("%dx%d" % (self.width, self.height))
        root.resizable(False, False)
        self._build()
        root.protocol("WM_DELETE_WINDOW", self.ask_cancel)
        root.bind("<Return>", lambda event: self.press(self.next_button))
        root.bind("<Escape>", lambda event: self.press(self.cancel_button))
        self.accepted.trace_add("write", lambda *ignored: self.update_buttons())
        self.mode.trace_add("write", lambda *ignored: self.update_fields())
        self.install_folder.trace_add("write", lambda *ignored: self.update_space())
        self.package.trace_add("write", lambda *ignored: self.update_space())
        self.show(self.flow()[0])
        root.after(100, self.poll)

    def flow(self) -> Tuple[str, ...]:
        """The pages of the choices made so far."""
        if self.direct:
            return ("ready", "installing", "finish")
        return install_flow(self.platform.get(), action=self.action.get(), installed=bool(self.existing),
                            need_game=bool(self.damaged[1]),
                            need_build=bool(self.damaged[0]) and not self.package_found)

    @property
    def on_pc(self) -> bool:
        return self.platform.get() == "pc" or self.action.get() != "install"

    def verb(self) -> str:
        return {"repair": "Repair", "uninstall": "Uninstall"}.get(self.action.get(), "Install")

    # -- the shell of the pages
    def px(self, value: float) -> int:
        return int(round(value * self.scale))

    def _make_fonts(self):
        from tkinter import font as tkfont
        base = tkfont.nametofont("TkDefaultFont", root=self.root).actual()
        self.font_bold = tkfont.Font(root=self.root, family=base["family"], size=base["size"], weight="bold")
        self.font_large = tkfont.Font(root=self.root, family=base["family"], size=int(base["size"] * 1.5), weight="bold")
        white = "#ffffff"
        self.style.configure("White.TFrame", background=white)
        self.style.configure("White.TLabel", background=white)
        self.style.configure("White.TCheckbutton", background=white)
        self.style.map("White.TCheckbutton", background=[("active", white), ("disabled", white)])

    def _build(self):
        ttk, px = self.ttk, self.px
        self.shell = ttk.Frame(self.root)
        self.separator = ttk.Separator(self.root)
        self.separator.pack(side="top", fill="x")
        bar = self.button_bar = ttk.Frame(self.root, padding=(px(12), px(10)))
        bar.pack(side="bottom", fill="x")
        self.cancel_button = ttk.Button(bar, text="Cancel", width=10, command=self.ask_cancel)
        self.cancel_button.pack(side="right")
        self.next_button = ttk.Button(bar, text="Next >", width=10, default="active", command=self.go_next)
        self.next_button.pack(side="right", padx=(0, px(10)))
        self.back_button = ttk.Button(bar, text="< Back", width=10, command=self.go_back)
        self.back_button.pack(side="right")
        self.shell.pack(side="top", fill="both", expand=True, before=self.separator)

    def image(self, name: str, make):
        if name not in self._images:
            try:
                self._images[name] = self.tk.PhotoImage(master=self.root, data=make(), format="png")
            except self.tk.TclError:
                self._images[name] = None
        return self._images[name]

    def show(self, page: str):
        self.page = page
        ttk, px = self.ttk, self.px
        for child in self.shell.winfo_children():
            child.destroy()
        title, line = self.PAGES[page]
        title, line = self.words_of(page, title, line)
        if page in self.EXTERIOR:
            self.root.update_idletasks()
            height = self.height - self.button_bar.winfo_reqheight() - self.separator.winfo_reqheight()
            picture = self.image("sidebar", lambda: sidebar_png(px(self.SIDEBAR), height))
            if picture is not None:
                self.sidebar = self.tk.Label(self.shell, image=picture, borderwidth=0, highlightthickness=0)
                self.sidebar.pack(side="left", fill="y")
            self.body = ttk.Frame(self.shell, style="White.TFrame", padding=(px(20), px(20)))
            self.body.pack(side="left", fill="both", expand=True)
            self.wrap = self.width - px(self.SIDEBAR + 48)
            self.white = True
            self.title_label = ttk.Label(self.body, text=title, style="White.TLabel", font=self.font_large,
                                         wraplength=self.wrap, justify="left")
            self.title_label.pack(anchor="w", pady=(0, px(14)))
        else:
            band = ttk.Frame(self.shell, style="White.TFrame", padding=(px(16), px(10)))
            band.pack(side="top", fill="x")
            icon = self.image("icon", lambda: icon_png(px(40)))
            if icon is not None:
                ttk.Label(band, image=icon, style="White.TLabel").pack(side="right")
            self.title_label = ttk.Label(band, text=title, style="White.TLabel", font=self.font_bold)
            self.title_label.pack(anchor="w")
            self.subtitle_label = ttk.Label(band, text=line, style="White.TLabel")
            self.subtitle_label.pack(anchor="w", padx=(px(14), 0), pady=(px(2), 0))
            ttk.Separator(self.shell).pack(side="top", fill="x")
            self.body = ttk.Frame(self.shell, padding=(px(24), px(12)))
            self.body.pack(side="top", fill="both", expand=True)
            self.wrap = self.width - px(56)
            self.white = False
        getattr(self, "page_" + page)()
        self.update_buttons()
        self.next_button.focus_set()

    def words_of(self, page: str, title: str, line: str) -> Tuple[str, str]:
        """The titles that follow the choices."""
        action = self.action.get()
        if page == "destination":
            line = "Choose the folder to install Halo CE in." if self.on_pc else "Tell Setup where your PS Vita is."
        if page == "ready" and action != "install":
            title, line = "Ready to %s" % self.verb(), "Setup is ready to %s Halo CE." % action
        if page == "installing" and action == "repair":
            title, line = "Repairing", "Please wait while Setup repairs Halo CE."
        if page == "installing" and action == "uninstall":
            title, line = "Uninstalling", "Please wait while Setup removes Halo CE."
        if page == "finish":
            done = {"repair": "Repair completed", "uninstall": "Uninstall completed"}.get(action, title)
            title = {"failed": "Setup was not completed", "cancelled": "Setup was cancelled"}.get(self.outcome, done)
        return title, line

    def update_buttons(self):
        page = self.page
        if not page:
            return
        flow = self.flow()
        running = page == "installing"
        ended = page == "finish"
        next_on = not running and (page != "license" or self.accepted.get() == "yes")
        back_on = not running and (page in flow and flow.index(page) > 0 if not ended else self.outcome != "done")
        self.next_button.configure(text="Finish" if ended else self.verb() if page == "ready" else "Next >",
                                   state="normal" if next_on else "disabled")
        self.back_button.configure(state="normal" if back_on else "disabled")
        self.cancel_button.configure(state="disabled" if ended or self.cancel.is_set() else "normal")

    def press(self, button):
        if button.instate(["!disabled"]):
            button.invoke()

    def complain(self, text: str):
        from tkinter import messagebox
        messagebox.showerror("%s Setup" % APP_NAME, text, parent=self.root)

    def text(self, text: str, parent=None, pady=(0, 0), indent: int = 0, **options):
        """A paragraph, wrapped to the page."""
        parent = parent or self.body
        style = "White.TLabel" if self.white else "TLabel"
        label = self.ttk.Label(parent, text=text, style=style, justify="left",
                               wraplength=self.wrap - self.px(indent), **options)
        label.pack(anchor="w", pady=pady, padx=(self.px(indent), 0))
        return label

    def radio(self, text: str, value: str, variable, available: bool = True, note: str = "", pady: int = 0):
        button = self.ttk.Radiobutton(self.body, text=text, value=value, variable=variable)
        button.pack(anchor="w", pady=(self.px(pady), 0))
        if not available:
            button.state(["disabled"])
        if note:
            self.text(note, indent=20, pady=(0, self.px(6)))
        return button

    def field(self, label: str, variable, browse=None, more=None, indent: int = 0, pady=(0, 8), state: str = "normal"):
        """A label over an entry and its Browse button. Returns the widgets
        to turn on and off."""
        ttk, px = self.ttk, self.px
        if label:
            self.text(label, indent=indent)
        row = ttk.Frame(self.body)
        row.pack(fill="x", padx=(px(indent), 0), pady=(px(2), px(pady[1])))
        entry = ttk.Entry(row, textvariable=variable, state=state)
        entry.pack(side="left", fill="x", expand=True)
        widgets = [entry]
        for text, command in ((("Browse...", browse),) if browse else ()) + tuple(more or ()):
            button = ttk.Button(row, text=text, command=command, state=state)
            button.pack(side="left", padx=(px(6), 0))
            widgets.append(button)
        return widgets

    def browse_file(self, variable, filetypes=None):
        from tkinter import filedialog
        chosen = filedialog.askopenfilename(parent=self.root, filetypes=filetypes or [("All files", "*")])
        if chosen:
            variable.set(chosen)

    def browse_folder(self, variable):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.root)
        if chosen:
            variable.set(chosen)

    def link(self, text: str, url: str, indent: int = 0):
        import webbrowser
        label = self.text(text, indent=indent, foreground="#0066cc", cursor="hand2")
        label.bind("<Button-1>", lambda event: webbrowser.open(url))
        return label

    # -- moving through the pages
    def go_next(self):
        page = self.page
        check = getattr(self, "leave_" + page, None)
        if check is not None and not check():
            return
        if page == "ready":
            self.begin()
        elif page == "finish":
            self.finish()
        else:
            flow = self.flow()
            self.show(flow[flow.index(page) + 1])

    def go_back(self):
        flow = self.flow()
        if self.page == "finish":  # after a failure: try again from the summary
            self.outcome = ""
            self.show("ready")
        else:
            self.show(flow[max(flow.index(self.page) - 1, 0)])

    def ask_cancel(self):
        """Cancel asks first, and the window's close button is Cancel."""
        from tkinter import messagebox
        if self.page == "finish":
            self.root.destroy()
            return
        if self.cancel.is_set():
            return
        if not messagebox.askyesno("%s Setup" % APP_NAME, "Are you sure you want to cancel %s Setup?" % APP_NAME,
                                   icon="warning", parent=self.root):
            return
        if self.busy():
            self.cancel.set()
            self.current_task.set("Cancelling...")
            self.update_buttons()
        else:
            self.root.destroy()

    def finish(self):
        if self.outcome == "done" and self.action.get() != "uninstall" and self.on_pc and self.launch.get():
            try:
                launch_pc(self.installed_folder())
            except (OSError, InstallerError) as error:
                self.complain("Halo CE could not be started: %s" % error)
        self.root.destroy()

    def installed_folder(self) -> str:
        return check_install_folder(self.install_folder.get())

    # -- the pages
    def page_welcome(self):
        self.text("This will install Halo: Combat Evolved %s on your PS Vita or PC, from your own game files." % VERSION,
                  pady=(0, self.px(12)))
        self.text(self.NOTICE, pady=(0, self.px(12)))
        self.text("Click Next to continue, or Cancel to exit Setup.")

    def page_maintenance(self):
        record = read_install(self.existing) or {}
        self.text("Version %s is installed in:" % record.get("version", "unknown"))
        self.text(self.existing, indent=14, pady=(0, self.px(10)))
        self.radio("Repair", "repair", self.action, note="Put back any file that is missing or damaged. Your saved "
                                                         "games are kept.")
        self.radio("Uninstall", "uninstall", self.action, note="Remove Halo CE from this computer. Your saved games "
                                                               "are kept.")
        self.radio("Install again, or install on a PS Vita", "install", self.action,
                   note="Go through Setup from the start.")

    def leave_maintenance(self):
        self.damaged = ([], [])
        if self.action.get() == "install":
            return True
        self.install_folder.set(self.existing)
        self.package_found = bool(find_pc_packages())
        record = read_install(self.existing) or {}
        self.desktop_shortcut.set(record.get("desktop") is not False)
        self.start_menu.set(record.get("start_menu") is not False)
        if self.action.get() == "repair":
            self.damaged = check_install(self.existing)
        return True

    def page_license(self):
        ttk, px = self.ttk, self.px
        self.text("Halo CE for PS Vita is free software under the GNU General Public License, version 3. No game "
                  "data is included: you need your own copy of the game.", pady=(0, px(6)))
        box = ttk.Frame(self.body)
        box.pack(fill="both", expand=True)
        text = self.tk.Text(box, wrap="word", height=8, width=10, font="TkDefaultFont", relief="sunken", borderwidth=1)
        scroll = ttk.Scrollbar(box, command=text.yview)
        text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        text.pack(side="left", fill="both", expand=True)
        text.insert("1.0", license_text())
        text.configure(state="disabled")
        self.license_box = text
        self.radio("I accept the terms of the license", "yes", self.accepted, pady=6)
        self.radio("I do not accept the terms of the license", "no", self.accepted)

    def page_game(self):
        for choice in GAMES:
            self.radio(choice.name, choice.key, self.game, choice.available, choice.note)

    def page_platform(self):
        for choice in PLATFORMS:
            self.radio(choice.name if choice.available else "%s (not available yet)" % choice.name, choice.key,
                       self.platform, choice.available, choice.note, pady=2)

    def page_game_files(self):
        px = self.px
        mcc = self.mcc_root.get()
        self.radio(MODES[0].name, "automatic", self.mode, pady=0)
        if self.mcc_found:
            self.text("Everything comes from your own Halo: The Master Chief Collection, found here:", indent=20,
                      pady=(0, px(2)))
        else:
            self.text("Halo: The Master Chief Collection was not found on this computer, so Manual is selected. If "
                      "you have it somewhere else, choose its folder.", indent=20, pady=(0, px(2)))
        automatic = self.field("MCC folder:" if not mcc else "", self.mcc_root, indent=20, pady=(0, 4),
                               browse=lambda: self.browse_folder(self.mcc_root))
        self.radio(MODES[1].name, "manual", self.mode, pady=2)
        self.text("Pick your own files.", indent=20, pady=(0, px(2)))
        manual = self.field("Xbox disc image (.iso, .xiso) or game folder:", self.xbox_source, indent=20, pady=(0, 4),
                            browse=lambda: self.browse_file(
                                self.xbox_source, [("Xbox disc images", "*.iso *.xiso"), ("All files", "*")]),
                            more=(("Folder...", lambda: self.browse_folder(self.xbox_source)),))
        ce = "Halo Custom Edition installer or folder%s:" % (" (optional)" if self.on_pc else "")
        manual += self.field(ce, self.ce_source, indent=20, pady=(0, 4),
                             browse=lambda: self.browse_file(self.ce_source, [("Programs", "*.exe"),
                                                                              ("All files", "*")]),
                             more=(("Folder...", lambda: self.browse_folder(self.ce_source)),))
        if not self.on_pc:
            ffmpeg = find_ffmpeg()
            row = self.ttk.Frame(self.body)
            row.pack(fill="x", padx=(px(20), 0))
            movies = self.ttk.Checkbutton(row, text="Convert the disc's movies (takes a few minutes)" if ffmpeg else
                                          "Movies need ffmpeg, which was not found: skipped", variable=self.movies)
            movies.pack(side="left")
            quality = self.ttk.Combobox(row, state="readonly", width=16, values=("Standard quality", "Better quality"))
            quality.current(0 if self.quality.get() == "baseline" else 1)
            quality.bind("<<ComboboxSelected>>", lambda event: self.quality.set(
                "baseline" if quality.current() == 0 else "better"))
            quality.pack(side="right")
            manual += [movies, quality] if ffmpeg else []
            if not ffmpeg:
                self.movies.set(False)
                movies.state(["disabled"])
                quality.state(["disabled"])
        self.mode_widgets = {"automatic": automatic, "manual": manual}
        self.update_fields()

    def update_fields(self):
        """The fields of the way not chosen are greyed."""
        if self.page != "game_files":
            return
        for mode, widgets in self.mode_widgets.items():
            for widget in widgets:
                widget.state(["!disabled" if mode == self.mode.get() else "disabled"])
                if widget.winfo_class() == "TCombobox" and mode == self.mode.get():
                    widget.state(["readonly"])

    def staged_ready(self) -> bool:
        """An earlier run gathered the game on the Vita's path: it can go on
        from there, with no source chosen again."""
        try:
            summary = self._staged()
        except OSError:
            return False
        return summary["xbe"] and summary["xbox_maps"] or bool(summary["mcc"])

    def _staged(self) -> dict:
        out = self.out.get()
        items = staged_uploads(out)
        names = {item.name.lower() for item in items if item.folder == VITA_GAME_FOLDER + "/" + STAGING_MAPS}
        return {"xbe": any(item.name == STAGING_XBE for item in items),
                "xbox_maps": all(name in names for name in XBOX_REQUIRED_MAPS), "mcc": mcc_staged(out)}

    def pc_files_ready(self) -> bool:
        try:
            return pc_files_status(self.out.get()).complete
        except OSError:
            return False

    def leave_game_files(self) -> bool:
        try:
            self.game_bytes = self.check_automatic() if self.mode.get() == "automatic" else self.check_manual()
        except InstallerError as error:
            self.complain(str(error))
            return False
        except OSError as error:
            self.complain("That file cannot be read: %s" % error)
            return False
        return True

    def check_automatic(self) -> int:
        folder = self.mcc_root.get().strip()
        if not folder:
            raise InstallerError("Choose MCC's folder (Steam: right-click the game, Manage, Browse local files), "
                                 "or use Manual.")
        plan = mcc_plan(folder)
        if plan.missing:
            raise InstallerError("This MCC lacks %s. Is Halo: Combat Evolved Anniversary installed in it?"
                                 % ", ".join(plan.missing[:3]))
        resources = sum(1 for item in plan.files if item.kind == "resource")
        if resources < len(PC_RESOURCE_MAPS) and not self.on_pc and not self.pc_files_ready():
            raise InstallerError("This MCC lacks the Halo Custom Edition files the Vita needs. Use Manual.")
        return plan.size

    def check_manual(self) -> int:
        source = self.xbox_source.get().strip()
        ce = self.ce_source.get().strip()
        size = 0
        if source:
            size = check_xbox_files(source)
        elif self.on_pc or not self.staged_ready():
            raise InstallerError("Choose your Xbox disc image (.iso) or the unpacked game folder.")
        if ce:
            if not os.path.exists(ce):
                raise InstallerError("%s does not exist." % ce)
            if os.path.isdir(ce) and not pc_maps_folder(ce):
                raise InstallerError("No bitmaps.map, sounds.map and loc.map in %s." % ce)
            size += 200 << 20
        elif not self.on_pc and not self.pc_files_ready():
            raise InstallerError(pc_files_help(list(name for name, _ in PC_RESOURCE_MAPS)))
        return size + ((600 << 20) if self.will_convert() else 0)

    def will_convert(self) -> bool:
        return not self.on_pc and self.mode.get() == "manual" and self.movies.get() and \
            bool(self.xbox_source.get().strip())

    def page_destination(self):
        if self.on_pc:
            self.destination_pc()
        else:
            self.destination_vita()

    def destination_vita(self):
        ttk, px = self.ttk, self.px
        self.text("On the Vita, open VitaShell and press SELECT. It shows an address like ftp://192.168.1.20:1337: "
                  "type it here.", pady=(0, px(8)))
        self.text("Vita's address:")
        row = ttk.Frame(self.body)
        row.pack(fill="x", pady=(px(2), 0))
        ttk.Entry(row, textvariable=self.host).pack(side="left", fill="x", expand=True)
        self.test_button = ttk.Button(row, text="Test connection", command=self.test_connection)
        self.test_button.pack(side="left", padx=(px(6), 0))
        self.test_result.set("")
        self.text("", textvariable=self.test_result, pady=(px(2), px(6)))
        self.field("Game package (VPK):", self.vpk, pady=(0, 2),
                   browse=lambda: self.browse_file(self.vpk, [("VPK", "*.vpk"), ("All files", "*")]))
        if not self.vpk.get():
            self.text("None found next to Setup. Leave it empty if the game is already on the Vita.", pady=(0, px(2)))
            self.link("Download halo-ce-vita-<version>-vita.vpk from the releases page", RELEASES_URL)
        self.ttk.Frame(self.body, height=px(6)).pack()
        self.field("Folder for the files on their way to the Vita (about 2.5 GB):", self.out, pady=(0, 2),
                   browse=lambda: self.browse_folder(self.out))

    def destination_pc(self):
        ttk, px = self.ttk, self.px
        repairing = self.action.get() == "repair"
        self.text("Setup will install Halo CE in this folder. No administrator rights are needed.",
                  pady=(0, px(8)))
        self.field("Install folder:", self.install_folder, browse=lambda: self.browse_folder(self.install_folder),
                   state="disabled" if repairing else "normal")
        if self.package_found:
            self.text("PC build: %s" % os.path.basename(self.package.get()), pady=(0, px(8)))
        else:
            kind = [("PC build", "*" + os.path.splitext(pc_package_suffix())[1]), ("All files", "*")]
            self.field("PC build (halo-ce-vita-<version>%s):" % pc_package_suffix(), self.package, pady=(0, 2),
                       browse=lambda: self.browse_file(self.package, kind))
            self.link("Download it from the releases page", RELEASES_URL)
            self.ttk.Frame(self.body, height=px(6)).pack()
        self.text("", textvariable=self.space, pady=(0, px(8)))
        self.update_space()
        if not repairing:
            ttk.Checkbutton(self.body, text="Create a desktop shortcut", variable=self.desktop_shortcut).pack(anchor="w")
            ttk.Checkbutton(self.body, text="Create a Start menu entry" if sys.platform == "win32" else
                            "Create an applications menu entry", variable=self.start_menu).pack(anchor="w")

    def package_info(self, path: str) -> PcPackage:
        path = path.strip()
        if path not in self.package_cache:
            self.package_cache[path] = read_pc_package(path)
        return self.package_cache[path]

    def needed_bytes(self) -> int:
        needed = self.game_bytes
        if self.package.get().strip():
            try:
                needed += self.package_info(self.package.get()).size
            except InstallerError:
                pass
        return needed

    def free_bytes(self, path: str) -> int:
        path = os.path.abspath(os.path.expanduser(path or "."))
        while not os.path.isdir(path) and os.path.dirname(path) != path:
            path = os.path.dirname(path)
        try:
            return shutil.disk_usage(path).free
        except OSError:
            return 0

    def update_space(self):
        if self.page != "destination" or not self.on_pc:
            return
        self.space.set("Space needed: %s     Space free: %s" % (
            human_size(self.needed_bytes()), human_size(self.free_bytes(self.install_folder.get()))))

    def _host_port(self) -> Tuple[str, int]:
        text = self.host.get().strip()
        found = re.match(r"^(?:ftp://)?([A-Za-z0-9.\-]+)(?::(\d+))?/?$", text)
        if not found:
            raise InstallerError("Type the Vita's address as VitaShell shows it (like 192.168.1.20).")
        return found.group(1), int(found.group(2) or FTP_PORT)

    def test_connection(self):
        if self.tester and self.tester.is_alive():
            return
        try:
            host, port = self._host_port()
        except InstallerError as error:
            self.test_result.set(str(error))
            return
        self.test_result.set("Connecting...")
        self.test_button.state(["disabled"])

        def run():
            client = VitaFtp(host, port, 5.0)
            try:
                client.connect()
                self.messages.put(("tested", "Connected to the Vita at %s:%d." % (host, port)))
            except FtpError as error:
                self.messages.put(("tested", str(error)))
            finally:
                client.close()
        self.tester = threading.Thread(target=run, daemon=True)
        self.tester.start()

    def leave_destination(self) -> bool:
        return self.leave_destination_pc() if self.on_pc else self.leave_destination_vita()

    def leave_destination_vita(self) -> bool:
        try:
            self._host_port()
            if self.vpk.get().strip():
                check_vpk(self.vpk.get().strip())
            if not self.out.get().strip():
                raise InstallerError("Choose a folder for the files on their way to the Vita.")
            needed, free = self.game_bytes, self.free_bytes(self.out.get())
            if needed > free:
                raise InstallerError("Not enough free space for the files: %s needed, %s free."
                                     % (human_size(needed), human_size(free)))
        except InstallerError as error:
            self.complain(str(error))
            return False
        return True

    def leave_destination_pc(self) -> bool:
        from tkinter import messagebox
        try:
            folder = self.installed_folder()
            self.package_info(self.package.get())
            needed, free = self.needed_bytes(), self.free_bytes(folder)
            if needed > free:
                raise InstallerError("Not enough free space in %s: %s needed, %s free."
                                     % (folder, human_size(needed), human_size(free)))
        except InstallerError as error:
            self.complain(str(error))
            return False
        if self.action.get() == "install" and os.path.isdir(folder) and os.listdir(folder):
            question = "Halo CE is already installed in %s. Install over it?" if read_install(folder) is not None \
                else "%s already has files in it. Install there anyway?"
            return messagebox.askyesno("%s Setup" % APP_NAME, question % folder, parent=self.root)
        return True

    def page_ready(self):
        ttk = self.ttk
        action = self.action.get()
        self.text("Click %s to %s." % (self.verb(), "begin" if action == "install" else "go on"),
                  pady=(0, self.px(6)))
        box = ttk.Frame(self.body)
        box.pack(fill="both", expand=True)
        text = self.tk.Text(box, wrap="char", height=8, width=10, font="TkDefaultFont", relief="sunken", borderwidth=1)
        scroll = ttk.Scrollbar(box, command=text.yview)
        text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        text.pack(side="left", fill="both", expand=True)
        text.insert("1.0", "\n".join(self.summary()))
        text.configure(state="disabled")
        self.summary_box = text

    def source_lines(self) -> List[str]:
        if self.mode.get() == "automatic":
            return ["Game files: from your MCC", "    %s" % self.mcc_root.get().strip()]
        lines = []
        source = self.xbox_source.get().strip()
        ce = self.ce_source.get().strip()
        lines += ["Game files: your Xbox game", "    %s" % source] if source else \
            ["Game files: already gathered in %s" % self.out.get()]
        if ce:
            lines += ["Halo Custom Edition files:", "    %s" % ce]
        elif self.on_pc:
            lines.append("Halo Custom Edition files: left out")
        if not self.on_pc:
            lines.append("Movies: %s" % ("converted from the disc (%s quality)" % (
                "better" if self.quality.get() == "better" else "standard") if self.will_convert() else "left out"))
        return lines

    def summary(self) -> List[str]:
        """The Ready page's plain list of the choices."""
        action = self.action.get()
        if action == "uninstall":
            return ["Remove Halo CE from:", "    %s" % self.existing, "",
                    "The game and its game files are removed, with the shortcuts.",
                    "Your saved games and settings are kept."]
        if action == "repair":
            program, game = self.damaged
            lines = ["Repair Halo CE in:", "    %s" % self.existing, ""]
            if not program and not game:
                return lines + ["Every file is there and the right size. The shortcuts are made again."]
            if program:
                lines.append("Program files to put back: %d" % len(program))
                lines.append("    From: %s" % self.package.get())
            if game:
                lines += ["Game files to put back: %d" % len(game)] + self.source_lines()
            return lines
        if self.on_pc:
            shortcuts = [name for wanted, name in ((self.desktop_shortcut.get(), "desktop"),
                                                   (self.start_menu.get(), "Start menu" if sys.platform == "win32"
                                                    else "applications menu")) if wanted]
            return ["Install on: PC", "Install folder:", "    %s" % self.install_folder.get(),
                    "PC build:", "    %s" % self.package.get()] + self.source_lines() + [
                "Space needed: %s" % human_size(self.needed_bytes()),
                "Shortcuts: %s" % (", ".join(shortcuts) or "none")]
        host, port = self._host_port()
        return ["Install on: PS Vita at %s:%d, in %s" % (host, port, VITA_GAME_FOLDER)] + self.source_lines() + [
            "Game package: %s" % (os.path.basename(self.vpk.get()) if self.vpk.get().strip() else "none chosen"),
            "Files gathered in:", "    %s" % self.out.get()]

    def page_installing(self):
        ttk, px = self.ttk, self.px
        self.text("", textvariable=self.current_task, pady=(px(8), px(4)))
        self.bar = ttk.Progressbar(self.body, mode="determinate", maximum=1000)
        self.bar.pack(fill="x", pady=(px(4), px(4)))
        row = ttk.Frame(self.body)
        row.pack(fill="x")
        ttk.Label(row, textvariable=self.current_file).pack(side="left")
        ttk.Label(row, textvariable=self.percent).pack(side="right")
        if not self.on_pc:
            self.text("Keep the Vita awake while it copies: touch its screen now and then, or turn off auto-standby "
                      "in its power settings.", pady=(px(16), 0))

    def page_finish(self):
        px = self.px
        action = self.action.get()
        if self.outcome != "done":
            if self.reason:
                self.text(self.reason, pady=(0, px(10)))
            if self.rollback is not None:
                self.text("What Setup had copied was removed again.", pady=(0, px(10)))
            elif not self.on_pc:
                self.text("Files already copied are kept, so a second try goes on from where this one stopped.",
                          pady=(0, px(10)))
            self.text("Click Back to try again, or Finish to close Setup.")
            return
        for warning in self.warnings:
            self.text(warning, pady=(0, px(8)))
        if action == "uninstall":
            self.text("Halo CE was removed from this computer.", pady=(0, px(8)))
            if self.left_behind:
                self.text("%s still has %d files the game wrote there. They were left alone: delete the folder if "
                          "you do not want them." % (self.existing, self.left_behind), pady=(0, px(8)))
            self.text("Click Finish to close Setup.")
        elif self.on_pc:
            self.text("Halo CE was %s in:" % ("repaired" if action == "repair" else "installed"))
            self.text(self.install_folder.get(), pady=(0, px(8)))
            self.text("The first load of each level takes a while: the game writes a cache file for it.",
                      pady=(0, px(10)))
            self.ttk.Checkbutton(self.body, text="Launch Halo CE", variable=self.launch,
                                 style="White.TCheckbutton").pack(anchor="w", pady=(0, px(10)))
            self.text("Click Finish to close Setup.")
        else:
            self.finish_vita()

    def finish_vita(self):
        px = self.px
        self.text("The files are on your PS Vita. What is left, on the Vita:", pady=(0, px(6)))
        vpk = os.path.basename(self.vpk.get().strip())
        steps = ["In VitaShell, press Circle to leave FTP mode, open ux0:, then data, press Cross on %s and confirm. "
                 "The bubble is called Halo CE." % vpk if vpk else
                 "Install the VPK in VitaShell, if the game is not on the Vita yet.",
                 "Check that ur0:data/libshacccg.suprx is there (ShaRKF00D extracts it).",
                 "Start Halo CE from its bubble."]
        for number, step in enumerate(steps, 1):
            self.text("%d.  %s" % (number, step), pady=(0, px(4)))
        self.text("You can delete %s now." % self.out.get(), pady=(px(6), 0))

    # -- the work: everything is read from the window here, on its thread
    # (tkinter is not thread-safe), and runs in order on a thread of its own
    def game_tasks(self, out: str) -> List[InstallTask]:
        """Gathering the game files into `out`, from the Game Files page's
        choice."""
        log = self.log
        if self.mode.get() == "automatic":
            folder = self.mcc_root.get().strip()

            def from_mcc(step):
                _check_space(out, mcc_plan(folder).needed(out))
                install_mcc_files(folder, out, step, log)
            return [InstallTask("Copying the game files from MCC", max(self.game_bytes, 1), from_mcc)]
        tasks = []
        source = self.xbox_source.get().strip()
        ce = self.ce_source.get().strip()
        if source:
            def from_disc(step):
                _check_space(out, xbox_files_size(source))
                extract_xbox_files(source, out, step, log)
            tasks.append(InstallTask("Copying the game files from your Xbox game", 1 << 30, from_disc))
        if ce:
            def from_ce(step):
                _check_space(out, 200 << 20)
                os.makedirs(out, exist_ok=True)
                if os.path.isdir(ce):
                    copy_pc_maps_from_folder(ce, out, step, log)
                else:
                    extract_pc_maps_from_installer(ce, out, step, log)
            tasks.append(InstallTask("Getting the Halo Custom Edition files", 200 << 20, from_ce))
        if self.will_convert():
            quality = self.quality.get()

            def movies(step):
                _check_space(out, 600 << 20)
                convert_movies(source, out, None, quality, step, log, self.cancel)
            tasks.append(InstallTask("Converting the movies", 1 << 30, movies))
        return tasks

    def vita_tasks(self) -> List[InstallTask]:
        out = self.out.get()
        host, port = self._host_port()
        vpk = self.vpk.get().strip()
        tasks = self.game_tasks(out)

        def send(step):
            items = staged_uploads(out)
            status = pc_files_status(out)
            if not items:
                raise InstallerError("Nothing to copy in %s." % out)
            if not status.complete:
                raise InstallerError(pc_files_help(status.missing))
            log_line = "Copying %d files (%s) to %s:%d"
            self.log(log_line % (len(items), human_size(sum(item.size for item in items)), host, port))
            upload_files(host, items, port, False, step, self.log, self.cancel)
        tasks.append(InstallTask("Sending the files to the Vita", max(self.game_bytes, 1 << 30), send))
        if vpk:
            tasks.append(InstallTask("Sending the game package to the Vita", 64 << 20,
                                     lambda step: upload_vpk(host, vpk, port, step, self.log, self.cancel)))
        return tasks

    def pc_tasks(self) -> List[InstallTask]:
        folder = self.installed_folder()
        action = self.action.get()
        record = read_install(folder) or {}
        files = dict(record.get("files", {})) if isinstance(record.get("files"), dict) else {}
        version = record.get("version", VERSION) if isinstance(record.get("version"), str) else VERSION
        desktop, start_menu = self.desktop_shortcut.get(), self.start_menu.get()
        program, game = self.damaged
        unpack = action == "install" or bool(program)
        gather = action == "install" or bool(game)
        platform = _pc_builds_here_first()[0][0]
        tasks = []
        if unpack:
            package = self.package_info(self.package.get())
            version = package.version

            def unpacked(step):
                files.clear()
                files.update(install_pc_package(package, folder, step, self.log))
            tasks.append(InstallTask("Copying the program files", max(package.size, 1), unpacked))
        if gather:
            data = pc_data_folder(folder) or os.path.join(folder, "build", platform)
            tasks += self.game_tasks(data)

        def finished(step):
            self.warnings = finish_pc_install(folder, version, files, desktop, start_menu, self.log)
        tasks.append(InstallTask("Creating the shortcuts", 1 << 20, finished))
        return tasks

    def uninstall_tasks(self) -> List[InstallTask]:
        folder = self.existing

        def remove(step):
            self.left_behind = uninstall_pc(folder, step, self.log)["kept"]
        return [InstallTask("Removing Halo CE", 1, remove)]

    def begin(self):
        """The Install button: the plan, then the Installing page."""
        action = self.action.get()
        try:
            tasks = self.uninstall_tasks() if action == "uninstall" else self.pc_tasks() if self.on_pc \
                else self.vita_tasks()
        except InstallerError as error:
            self.complain(str(error))
            return
        self.rollback = None
        folder = ""
        if self.on_pc and action == "install":
            folder = self.installed_folder()
            if read_install(folder) is None:  # (a new install: taken back if it stops)
                self.rollback = snapshot_folder(folder)
        self.cancel.clear()
        self.outcome = ""
        self.reason = ""
        self.warnings = []
        self.current_task.set("Preparing...")
        self.current_file.set("")
        self.percent.set("")
        self.show("installing")

        def run():
            try:
                run_tasks(tasks, self.report, lambda label: self.messages.put(("task", label)))
                self.messages.put(("end", "done", ""))
            except Cancelled:
                self.roll_back(folder)
                self.messages.put(("end", "cancelled", ""))
            except InstallerError as error:
                self.log("ERROR: %s" % error)
                self.roll_back(folder)
                self.messages.put(("end", "failed", str(error)))
            except Exception as error:  # shown, not lost in a thread
                self.log("ERROR: %r" % (error,))
                self.roll_back(folder)
                self.messages.put(("end", "failed", "Unexpected problem: %r" % (error,)))

        self.worker = threading.Thread(target=run, daemon=True)
        self.worker.start()

    def roll_back(self, folder: str):
        if self.rollback is not None:
            roll_back_install(folder, self.rollback)

    def busy(self) -> bool:
        return bool(self.worker and self.worker.is_alive())

    def log(self, message: str):
        self.log_lines.append(message)

    def save_log(self):
        """What Setup did, for a report when it went wrong."""
        path = os.path.join(os.path.dirname(settings_path()), LOG_FILE)
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as file:
                file.write("Halo CE Setup %s\n%s\n" % (VERSION, "\n".join(self.log_lines)))
        except OSError:
            pass

    def report(self, message: str, done: int, total: int):
        if self.cancel.is_set():
            raise Cancelled()
        self.messages.put(("progress", message, done, total))

    def poll(self):
        try:
            while True:
                message = self.messages.get_nowait()
                if message[0] == "task":
                    if not self.cancel.is_set():
                        self.current_task.set(message[1] + "...")
                elif message[0] == "progress":
                    _, text, done, total = message
                    if self.page == "installing":
                        self.bar["value"] = 1000 * done // total if total else 0
                    self.current_file.set(text)
                    self.percent.set("%d%%" % (100 * done // total if total else 100))
                elif message[0] == "tested":
                    self.test_result.set(message[1])
                    if self.page == "destination" and not self.on_pc and self.test_button.winfo_exists():
                        self.test_button.state(["!disabled"])
                elif message[0] == "end":
                    self.outcome, self.reason = message[1], message[2]
                    self.cancel.clear()
                    self.save_log()
                    self.show("finish")
        except queue.Empty:
            pass
        self.root.after(100, self.poll)


if __name__ == "__main__":
    sys.exit(main())
