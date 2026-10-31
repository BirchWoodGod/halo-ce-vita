# SPDX-License-Identifier: GPL-3.0-only
"""The Setup wizard: the pages of each choice (no display needed), greyed
entries that cannot be picked, the PC build's archive read as untrusted, a real
PC install (the record, repair's check, uninstall, the shortcuts, the Windows
parts against a stand-in registry), and the window walked page by page with
Automatic and Manual, on the Vita and on PC, Cancel, a failure and Back, repair
and uninstall. Every game file here is made up (fakes)."""

import io
import json
import os
import socket
import stat
import subprocess
import sys
import tarfile
import threading
import types
import zipfile

import pytest

import fakes
import halo_ce_vita_installer as hcv
from test_window import all_widgets, pump, root, stage  # noqa: F401 (root: the fixture)

MCC_PARTS = ("steamapps", "common", hcv.MCC_FOLDER_NAME)


# -- the pages of each choice

def test_flow_of_each_choice():
    wizard = ("welcome", "license", "platform", "game_files", "destination", "ready", "installing", "finish")
    assert hcv.install_flow("vita") == wizard
    assert hcv.install_flow("pc") == wizard
    assert hcv.install_flow("pc", installed=True) == wizard[:1] + ("maintenance",) + wizard[1:]


def test_flow_of_repair_and_uninstall():
    end = ("ready", "installing", "finish")
    start = ("welcome", "maintenance")
    assert hcv.install_flow("pc", action="uninstall", installed=True) == start + end
    assert hcv.install_flow("pc", action="repair", installed=True, need_game=False, need_build=False) == start + end
    assert hcv.install_flow("pc", action="repair", installed=True, need_game=True, need_build=False) == \
        start + ("game_files",) + end
    assert hcv.install_flow("pc", action="repair", installed=True, need_game=False, need_build=True) == \
        start + ("destination",) + end
    with pytest.raises(hcv.InstallerError):
        hcv.install_flow("pc", action="format")


def test_every_flow_page_is_drawn_by_a_method():
    for page in hcv.InstallerWindow.PAGES:
        assert hasattr(hcv.InstallerWindow, "page_" + page)
    for platform in ("vita", "pc"):
        for action in hcv.ACTIONS:
            assert set(hcv.install_flow(platform, action=action, installed=True)) <= set(hcv.InstallerWindow.PAGES)


def test_greyed_platforms_cannot_be_picked():
    greyed = [choice for choice in hcv.PLATFORMS if not choice.available]
    assert [choice.name for choice in greyed] == ["Android", "Nintendo Switch", "Other consoles"]
    assert [choice.key for choice in hcv.PLATFORMS if choice.available] == ["vita", "pc"]
    for choice in greyed:
        with pytest.raises(hcv.InstallerError, match="not ready"):
            hcv.pick(hcv.PLATFORMS, choice.key)
        with pytest.raises(hcv.InstallerError):
            hcv.install_flow(choice.key)
    with pytest.raises(hcv.InstallerError):
        hcv.install_flow("dreamcast")


def test_a_second_game_adds_a_page_and_nothing_else():
    two = hcv.GAMES + (hcv.Choice("halo-2", "Halo 2"),)
    plain = hcv.install_flow("vita")
    assert hcv.install_flow("vita", two) == plain[:2] + ("game",) + plain[2:]


def test_run_tasks_maps_everything_onto_one_bar():
    seen = []
    started = []

    def first(step):
        step("a.map", 50, 100)
        step("a.map", 100, 100)

    def second(step):
        step("b.map", 1, 2)
    tasks = [hcv.InstallTask("one", 300, first), hcv.InstallTask("two", 100, second)]
    hcv.run_tasks(tasks, lambda message, done, total: seen.append((message, done, total)), started.append)
    assert started == ["one", "two"]
    assert [item for item in seen if item[0]] == [("a.map", 150, 400), ("a.map", 300, 400), ("b.map", 350, 400)]
    assert seen[-1] == ("", 400, 400)
    assert [done for _, done, _ in seen] == sorted(done for _, done, _ in seen)  # it never goes back
    # a Cancel pressed once the last task is done comes too late to count


def test_a_cancel_after_the_last_task_changes_nothing():
    state = []

    def report(message, done, total):
        if state:
            raise hcv.Cancelled()
    hcv.run_tasks([hcv.InstallTask("one", 1, lambda step: state.append("done"))], report)
    assert state == ["done"]


# -- finding a PC build

def pc_build(folder, kind="linux"):
    """A stand-in for an unpacked PC release."""
    program = {"linux": "halo", "windows": "halo.exe"}[kind]
    data = folder / "build" / kind
    data.mkdir(parents=True)
    (data / program).write_bytes(b"\x7fELF")
    (folder / "halo.sh").write_text("#!/bin/sh\n")
    return data


def test_pc_data_folder(tmp_path, monkeypatch):
    release = tmp_path / "release"
    data = pc_build(release)
    assert hcv.pc_data_folder(str(release)) == str(data)  # the unpacked folder
    assert hcv.pc_data_folder(str(data)) == str(data)  # the executable's folder
    assert hcv.pc_data_folder(str(tmp_path)) is None
    assert hcv.pc_data_folder(str(tmp_path / "missing")) is None
    assert hcv.pc_data_folder("") is None
    # both builds in one folder: this system's first
    other = pc_build(tmp_path / "both", "windows")
    (tmp_path / "both" / "build" / "linux").mkdir()
    (tmp_path / "both" / "build" / "linux" / "halo").write_bytes(b"\x7fELF")
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    assert hcv.pc_data_folder(str(tmp_path / "both")) == str(other)
    monkeypatch.setattr(hcv.sys, "platform", "linux")
    assert hcv.pc_data_folder(str(tmp_path / "both")).endswith(os.path.join("build", "linux"))


def test_find_pc_builds_in_downloads(tmp_path):
    downloads = tmp_path / "home" / "Downloads"
    downloads.mkdir()
    data = pc_build(downloads / "halo-ce-vita-1.1.2-pc-linux")
    pc_build(downloads / "something-else")  # not a release's name
    assert hcv.find_pc_builds() == [str(data)]


# -- the copy into the PC build

def staged(tmp_path):
    out = tmp_path / "out"
    stage(str(out), xbox=True, pc=True, movies=2)
    fakes.make_mcc(str(tmp_path / "mcc"))
    hcv.install_mcc_files(str(tmp_path / "mcc"), str(out), log=lambda text: None)
    return out


def test_copy_to_pc_build_lays_out_as_the_game_reads(tmp_path):
    out = staged(tmp_path)
    data = pc_build(tmp_path / "release")
    lines = []
    result = hcv.copy_to_pc_build(str(out), str(tmp_path / "release"), log=lines.append)
    assert result["skipped"] == 0 and result["copied"] == len(hcv.pc_uploads(str(out)))
    assert (data / "default.xbe").read_bytes() == (out / "default.xbe").read_bytes()
    assert (data / "maps" / "a10.map").exists() and (data / "maps" / "bitmaps.map").exists()
    assert (data / "mcc" / "halo1" / "maps" / "a10.map").exists()
    assert (data / "mcc" / "halo1" / "maps" / "custom_edition" / "loc.map").exists()
    assert (data / "mcc" / "halo1" / "sound" / "pc" / "sounds_adpcm.fsb").exists()
    assert not (data / "movies").exists()  # the Vita's
    assert not [name for _, _, names in os.walk(str(data)) for name in names if name.endswith(".part")]
    # again: kept; with replace: copied; a wrong size: copied
    assert hcv.copy_to_pc_build(str(out), str(data), log=lines.append)["copied"] == 0
    assert hcv.copy_to_pc_build(str(out), str(data), replace=True, log=lines.append)["skipped"] == 0
    (data / "default.xbe").write_bytes(b"short")
    assert hcv.copy_to_pc_build(str(out), str(data), log=lines.append)["copied"] == 1
    assert (data / "default.xbe").stat().st_size == (out / "default.xbe").stat().st_size


def test_copy_to_pc_build_makes_the_maps_folder(tmp_path):
    """MCC's files alone: the game takes the folder for its data root by its
    maps folder (xbox_files.c has_maps), and only then reads mcc/ there."""
    out = tmp_path / "out"
    fakes.make_mcc(str(tmp_path / "mcc"))
    hcv.install_mcc_files(str(tmp_path / "mcc"), str(out), log=lambda text: None)
    data = pc_build(tmp_path / "release")
    hcv.copy_to_pc_build(str(out), str(data), log=lambda text: None)
    assert (data / "maps").is_dir() and not os.listdir(str(data / "maps"))


def test_copy_to_pc_build_refuses(tmp_path):
    data = pc_build(tmp_path / "release")
    with pytest.raises(hcv.InstallerError, match="No PC build"):
        hcv.copy_to_pc_build(str(tmp_path), str(tmp_path / "nothing"))
    with pytest.raises(hcv.InstallerError, match="Nothing to copy"):
        hcv.copy_to_pc_build(str(tmp_path / "empty"), str(data))
    # the output folder is the data folder: nothing to do, nothing harmed
    stage(str(data), xbox=True)
    result = hcv.copy_to_pc_build(str(data), str(data), log=lambda text: None)
    assert result["copied"] == 0 and (data / "default.xbe").exists()


@pytest.mark.skipif(sys.platform == "win32", reason="symbolic links need rights on Windows")
def test_copy_to_pc_build_does_not_write_through_a_link(tmp_path):
    out = staged(tmp_path)
    data = pc_build(tmp_path / "release")
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    os.symlink(str(elsewhere), str(data / "maps"))
    with pytest.raises(hcv.InstallerError, match="link"):
        hcv.copy_to_pc_build(str(out), str(data), log=lambda text: None)
    assert not os.listdir(str(elsewhere))


def test_a_failed_copy_leaves_no_partial_file(tmp_path, monkeypatch):
    out = staged(tmp_path)
    data = pc_build(tmp_path / "release")

    def stop(message, done, total):
        raise hcv.Cancelled()
    with pytest.raises(hcv.Cancelled):
        hcv.copy_to_pc_build(str(out), str(data), progress=stop, log=lambda text: None)
    assert not [name for _, _, names in os.walk(str(data)) for name in names if name.endswith(".part")]


def test_command_line_pc_copy(tmp_path, capsys):
    out = staged(tmp_path)
    data = pc_build(tmp_path / "release")
    assert hcv.main(["--out", str(out), "pc-copy", str(tmp_path / "release")]) == 0
    assert (data / "mcc" / "halo1" / "maps" / "a10.map").exists()
    assert hcv.main(["--out", str(out), "pc-copy", str(tmp_path / "nothing")]) == 1
    assert hcv.main(["--out", str(out), "pc-copy"]) == 1  # none to be found


# -- a stand-in Steam

def fake_steam(tmp_path, monkeypatch):
    """A stand-in Steam, its libraryfolders.vdf naming a library elsewhere
    with a stand-in MCC, in the test's own home (conftest.py)."""
    home = tmp_path / "home"
    library = tmp_path / "second drive"
    mcc = library.joinpath(*MCC_PARTS)
    fakes.make_mcc(str(mcc))
    if sys.platform == "win32":
        steam = home / "Program Files" / "Steam"
        monkeypatch.setenv("ProgramFiles", str(home / "Program Files"))
    elif sys.platform == "darwin":
        steam = home / "Library" / "Application Support" / "Steam"
    else:
        steam = home / ".local" / "share" / "Steam"
    steamapps = steam / "steamapps"
    steamapps.mkdir(parents=True)
    (steamapps / "libraryfolders.vdf").write_text(
        '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n' % str(library).replace("\\", "\\\\"))
    return mcc


def tree(folder):
    """Every file under `folder` with its size and time."""
    found = {}
    for directory, _, names in os.walk(str(folder)):
        for name in names:
            status = os.stat(os.path.join(directory, name))
            found[os.path.join(directory, name)] = (status.st_size, status.st_mtime_ns)
    return found


# -- the PC build's archive

RELEASE = "halo-ce-vita-1.1.2-pc-linux"
RELEASE_FILES = {"halo.sh": b"#!/bin/sh\n", "README.txt": b"read me", "build/linux/halo": b"\x7fELF program",
                 "build/linux/brokers.txt": b"broker", "lib/libSDL3.so.0": b"sdl",
                 "port/vita/app0/menus/main.xml": b"<menu/>", "licenses/LICENSE.txt": b"GPL"}


def tar_release(path, files=None, top=RELEASE):
    with tarfile.open(str(path), "w:gz") as archive:
        for name, data in (files or RELEASE_FILES).items():
            info = tarfile.TarInfo("%s/%s" % (top, name) if top else name)
            info.size = len(data)
            info.mode = 0o755 if name in ("halo.sh", "build/linux/halo") else 0o644
            archive.addfile(info, io.BytesIO(data))
    return str(path)


def zip_release(path, files=None, top="halo-ce-vita-1.1.2-pc-windows"):
    if files is None:
        files = {"halo.bat": b"@echo off\r\n", "build/windows/halo.exe": b"MZ program",
                 "build/windows/SDL3.dll": b"MZ sdl", "port/vita/app0/menus/main.xml": b"<menu/>"}
    with zipfile.ZipFile(str(path), "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr("%s/%s" % (top, name) if top else name, data)
    return str(path)


def test_read_a_package(tmp_path):
    package = hcv.read_pc_package(tar_release(tmp_path / (RELEASE + ".tar.gz")))
    assert package.kind == "tar" and package.version == "1.1.2"
    assert {item.relative for item in package.files} == set(RELEASE_FILES)
    assert package.size == sum(len(data) for data in RELEASE_FILES.values())
    # no top folder is fine too, and the version then comes from the file's name
    package = hcv.read_pc_package(tar_release(tmp_path / "halo-ce-vita-1.1.3-beta.1-pc-linux.tar.gz", top=""))
    assert package.version == "1.1.3-beta.1"


def test_a_package_for_the_other_system_is_refused(tmp_path, monkeypatch):
    with pytest.raises(hcv.InstallerError, match="not the PC build for this system"):
        hcv.read_pc_package(zip_release(tmp_path / "windows.zip"))
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    assert hcv.read_pc_package(zip_release(tmp_path / "windows.zip")).kind == "zip"
    with pytest.raises(hcv.InstallerError, match="not the PC build for this system"):
        hcv.read_pc_package(tar_release(tmp_path / "linux.tar.gz"))


def test_not_a_package(tmp_path):
    with pytest.raises(hcv.InstallerError, match="does not exist"):
        hcv.read_pc_package(str(tmp_path / "missing.zip"))
    with pytest.raises(hcv.InstallerError, match="does not exist"):
        hcv.read_pc_package("")
    other = tmp_path / "other.bin"
    other.write_bytes(b"not an archive" * 100)
    with pytest.raises(hcv.InstallerError, match="not a PC build archive"):
        hcv.read_pc_package(str(other))


@pytest.mark.parametrize("bad", ["../evil.txt", "build/../../evil.txt", "build/con.txt", "bad:name.txt"])
def test_unsafe_names_in_a_package_are_refused(tmp_path, bad):
    files = dict(RELEASE_FILES)
    files[bad] = b"evil"
    with pytest.raises(hcv.InstallerError, match="unsafe file name"):
        hcv.read_pc_package(tar_release(tmp_path / "bad.tar.gz", files, top=""))
    assert not (tmp_path / "evil.txt").exists()


def test_absolute_names_in_a_package_are_refused(tmp_path):
    path = tmp_path / "bad.tar.gz"
    tar_release(path, top="")
    with tarfile.open(str(path), "r:gz") as old:
        members = [(info, old.extractfile(info).read()) for info in old.getmembers()]
    with tarfile.open(str(path), "w:gz") as archive:
        for info, data in members:
            archive.addfile(info, io.BytesIO(data))
        info = tarfile.TarInfo("/tmp/evil")
        info.size = 4
        archive.addfile(info, io.BytesIO(b"evil"))
    with pytest.raises(hcv.InstallerError, match="unsafe file name"):
        hcv.read_pc_package(str(path))


@pytest.mark.parametrize("kind", [tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE, tarfile.CHRTYPE])
def test_links_and_devices_in_a_package_are_refused(tmp_path, kind):
    path = tmp_path / "bad.tar.gz"
    with tarfile.open(str(path), "w:gz") as archive:
        for name, data in RELEASE_FILES.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        info = tarfile.TarInfo("lib/link")
        info.type = kind
        info.linkname = "/etc/passwd"
        archive.addfile(info)
    with pytest.raises(hcv.InstallerError, match="link or a device"):
        hcv.read_pc_package(str(path))


def test_a_link_in_a_zip_is_refused(tmp_path, monkeypatch):
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    path = zip_release(tmp_path / "bad.zip")
    with zipfile.ZipFile(path, "a") as archive:
        info = zipfile.ZipInfo("halo-ce-vita-1.1.2-pc-windows/link")
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        archive.writestr(info, "/etc/passwd")
    with pytest.raises(hcv.InstallerError, match="link or a device"):
        hcv.read_pc_package(path)


def test_doubles_strangers_and_size_are_refused(tmp_path, monkeypatch):
    with pytest.raises(hcv.InstallerError, match="twice"):
        hcv.read_pc_package(tar_release(tmp_path / "a.tar.gz", dict(RELEASE_FILES, **{"HALO.SH": b"x"}), top=""))
    path = tmp_path / "b.tar.gz"
    with tarfile.open(str(path), "w:gz") as archive:
        for name in ("top/build/linux/halo", "elsewhere/file"):
            info = tarfile.TarInfo(name)
            archive.addfile(info, io.BytesIO(b""))
    with pytest.raises(hcv.InstallerError):
        hcv.read_pc_package(str(path))
    monkeypatch.setattr(hcv, "PACKAGE_ENTRIES_MAXIMUM", 2)
    with pytest.raises(hcv.InstallerError, match="too many"):
        hcv.read_pc_package(tar_release(tmp_path / "c.tar.gz"))
    monkeypatch.setattr(hcv, "PACKAGE_ENTRIES_MAXIMUM", 100)
    monkeypatch.setattr(hcv, "PACKAGE_SIZE_MAXIMUM", 4)
    with pytest.raises(hcv.InstallerError, match="too large"):
        hcv.read_pc_package(tar_release(tmp_path / "d.tar.gz"))


def test_a_damaged_package_stops_and_leaves_no_partial_file(tmp_path, monkeypatch):
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    path = tmp_path / "damaged.zip"
    big = {"build/windows/halo.exe": b"MZ" + os.urandom(4096)}
    with zipfile.ZipFile(str(path), "w", zipfile.ZIP_STORED) as archive:
        for name, data in big.items():
            archive.writestr(name, data)
    data = bytearray(path.read_bytes())
    data[2000] ^= 0xff  # (inside the stored file: its CRC no longer matches)
    path.write_bytes(bytes(data))
    package = hcv.read_pc_package(str(path))
    folder = tmp_path / "install"
    with pytest.raises(hcv.InstallerError, match="damaged"):
        hcv.install_pc_package(package, str(folder), log=lambda text: None)
    assert not [name for _, _, names in os.walk(str(folder)) for name in names]


def test_install_keeps_the_layout_and_the_programs_mode(tmp_path):
    package = hcv.read_pc_package(tar_release(tmp_path / (RELEASE + ".tar.gz")))
    folder = tmp_path / "Halo CE"
    bars = []
    written = hcv.install_pc_package(package, str(folder), lambda m, done, total: bars.append(done), lambda text: None)
    assert written == {name: len(data) for name, data in RELEASE_FILES.items()}
    assert (folder / "build" / "linux" / "halo").read_bytes() == b"\x7fELF program"
    assert os.access(str(folder / "halo.sh"), os.X_OK) and os.access(str(folder / "build" / "linux" / "halo"), os.X_OK)
    assert not os.access(str(folder / "README.txt"), os.X_OK)
    assert bars == sorted(bars) and bars[-1] == package.size
    assert hcv.pc_data_folder(str(folder)) == str(folder / "build" / "linux")


@pytest.mark.skipif(sys.platform == "win32", reason="symbolic links need rights on Windows")
def test_install_does_not_write_through_a_link(tmp_path):
    package = hcv.read_pc_package(tar_release(tmp_path / (RELEASE + ".tar.gz")))
    folder = tmp_path / "install"
    elsewhere = tmp_path / "elsewhere"
    folder.mkdir()
    elsewhere.mkdir()
    os.symlink(str(elsewhere), str(folder / "build"))
    with pytest.raises(hcv.InstallerError, match="link"):
        hcv.install_pc_package(package, str(folder), log=lambda text: None)
    assert not os.listdir(str(elsewhere))


def test_find_pc_packages_next_to_the_tool_only(tmp_path, monkeypatch):
    here = tmp_path / "here"
    here.mkdir()
    monkeypatch.setattr(hcv, "_tool_folder", lambda: str(here))
    monkeypatch.chdir(str(here))
    downloads = tmp_path / "home" / "Downloads"
    downloads.mkdir()
    tar_release(downloads / "halo-ce-vita-9.9.9-pc-linux.tar.gz")  # (may be anyone's: not picked by itself)
    assert hcv.find_pc_packages() == []
    old = tar_release(here / "halo-ce-vita-1.1.2-pc-linux.tar.gz")
    new = tar_release(here / "halo-ce-vita-1.1.10-pc-linux.tar.gz")
    (here / "halo-ce-vita-1.1.2-pc-windows.zip").write_bytes(b"not for this system")
    assert hcv.find_pc_packages() == [new, old]


def test_default_install_folders(monkeypatch, tmp_path):
    assert hcv.default_install_folder() == os.path.join(str(tmp_path / "home"), ".local", "share", "halo-ce")
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "Local"))
    assert hcv.default_install_folder() == os.path.join(str(tmp_path / "Local"), "Programs", "Halo CE")


def test_install_folder_must_be_a_full_path():
    for bad in ("", "   ", "relative/folder", "/bad\nfolder"):
        with pytest.raises(hcv.InstallerError):
            hcv.check_install_folder(bad)
    assert hcv.check_install_folder(" /some/folder/ ") == "/some/folder"


# -- a PC install, Linux

def installed(tmp_path, desktop=True, start_menu=True, game=True):
    """The stand-in release installed, with game files, as the wizard does."""
    package = hcv.read_pc_package(tar_release(tmp_path / (RELEASE + ".tar.gz")))
    folder = tmp_path / "Halo CE"
    files = hcv.install_pc_package(package, str(folder), log=lambda text: None)
    if game:
        stage(str(folder / "build" / "linux"), xbox=True, pc=True)
    warnings = hcv.finish_pc_install(str(folder), package.version, files, desktop, start_menu, lambda text: None)
    assert warnings == []
    return folder


def test_finish_writes_the_record_and_remembers(tmp_path):
    folder = installed(tmp_path)
    record = hcv.read_install(str(folder))
    assert record["version"] == "1.1.2" and record["desktop"] is True
    assert set(RELEASE_FILES) < set(record["files"])
    assert "build/linux/default.xbe" in record["game_files"]
    assert "build/linux/maps/a10.map" in record["game_files"]
    assert hcv.check_install(str(folder)) == ([], [])
    assert hcv.find_existing_install() == str(folder)


def test_linux_desktop_files(tmp_path):
    folder = installed(tmp_path)
    home = tmp_path / "home"
    entry = home / ".local" / "share" / "applications" / "halo-ce.desktop"
    text = entry.read_text()
    assert "Name=Halo CE\n" in text and "Type=Application\n" in text
    assert 'Exec="%s/halo.sh"\n' % folder in text and "Path=%s\n" % folder in text
    assert (home / "Desktop" / "halo-ce.desktop").read_text() == text
    assert (folder / hcv.PACKAGE_ICON).read_bytes()[:4] == b"\x89PNG"


def test_desktop_entry_quoting():
    text = hcv.desktop_entry('/a "b"/$HOME/100%', '/a "b"/$HOME/100%/halo.sh', "/i")
    assert 'Exec="/a \\\\"b\\\\"/\\\\$HOME/100%%/halo.sh"\n' in text


def test_checkboxes_decide_the_shortcuts(tmp_path):
    installed(tmp_path, desktop=False, start_menu=False)
    home = tmp_path / "home"
    assert not (home / ".local" / "share" / "applications").exists() and not (home / "Desktop").exists()


def test_check_install_finds_damage(tmp_path):
    folder = installed(tmp_path)
    (folder / "README.txt").unlink()
    (folder / "build" / "linux" / "brokers.txt").write_bytes(b"other size")
    (folder / "build" / "linux" / "maps" / "a10.map").unlink()
    program, game = hcv.check_install(str(folder))
    assert sorted(program) == ["README.txt", "build/linux/brokers.txt"]
    assert game == ["build/linux/maps/a10.map"]


def test_uninstall_removes_what_it_wrote_and_nothing_else(tmp_path):
    folder = installed(tmp_path)
    home = tmp_path / "home"
    (folder / "build" / "linux" / "config.toml").write_text("the player's own")
    result = hcv.uninstall_pc(str(folder), log=lambda text: None)
    assert result["kept"] == 1 and result["removed"] > len(RELEASE_FILES)
    assert sorted(path.name for path in folder.rglob("*") if path.is_file()) == ["config.toml"]
    assert not (home / ".local" / "share" / "applications" / "halo-ce.desktop").exists()
    assert not (home / "Desktop" / "halo-ce.desktop").exists()
    assert hcv.find_existing_install() is None and "install_folder" not in hcv.load_settings()


def test_uninstall_empties_the_folder_away(tmp_path):
    folder = installed(tmp_path)
    assert hcv.uninstall_pc(str(folder), log=lambda text: None)["kept"] == 0
    assert not folder.exists()


def test_a_tampered_record_cannot_reach_outside(tmp_path):
    folder = installed(tmp_path)
    outside = tmp_path / "precious.txt"
    outside.write_text("keep me")
    other = tmp_path / "home" / "other.lnk"
    other.write_text("keep me")
    path = folder / hcv.INSTALL_RECORD
    record = json.loads(path.read_text())
    record["files"]["../precious.txt"] = 7
    record["files"][str(outside)] = 7
    record["game_files"]["build/../../precious.txt"] = 7
    record["shortcuts"] = [str(outside), str(other)]
    record["uninstaller"] = "../precious.txt"
    path.write_text(json.dumps(record))
    assert sorted(hcv.check_install(str(folder))[0]) == sorted(["../precious.txt", str(outside)])
    hcv.uninstall_pc(str(folder), log=lambda text: None)
    assert outside.read_text() == "keep me" and other.read_text() == "keep me"


def test_a_damaged_record_is_no_install(tmp_path):
    folder = tmp_path / "folder"
    folder.mkdir()
    assert hcv.read_install(str(folder)) is None
    (folder / hcv.INSTALL_RECORD).write_text("{not json")
    assert hcv.read_install(str(folder)) is None
    (folder / hcv.INSTALL_RECORD).write_text('{"files": []}')
    assert hcv.read_install(str(folder)) is None
    with pytest.raises(hcv.InstallerError):
        hcv.uninstall_pc(str(folder))
    assert (folder / hcv.INSTALL_RECORD).exists()


def test_roll_back_keeps_what_was_there(tmp_path):
    folder = tmp_path / "folder"
    (folder / "mine").mkdir(parents=True)
    (folder / "mine" / "file.txt").write_text("mine")
    before = hcv.snapshot_folder(str(folder))
    (folder / "mine" / "new.txt").write_text("new")
    (folder / "build" / "linux").mkdir(parents=True)
    (folder / "build" / "linux" / "halo").write_text("new")
    hcv.roll_back_install(str(folder), before)
    assert hcv.snapshot_folder(str(folder)) == before
    fresh = tmp_path / "fresh"
    fresh.mkdir()
    (fresh / "file").write_text("new")
    hcv.roll_back_install(str(fresh), set())
    assert not fresh.exists()


def test_launch_runs_the_install_own_program_with_no_shell(tmp_path, monkeypatch):
    folder = installed(tmp_path)
    runs = []
    monkeypatch.setattr(hcv.subprocess, "Popen", lambda *a, **k: runs.append((a, k)))
    hcv.launch_pc(str(folder))
    assert runs[0][0] == ([str(folder / "halo.sh")],) and runs[0][1]["cwd"] == str(folder)
    assert not runs[0][1].get("shell")
    with pytest.raises(hcv.InstallerError):
        hcv.launch_pc(str(tmp_path / "nothing"))


# -- a PC install, Windows (its parts against stand-ins: this runs anywhere)

class WritingWinreg(types.ModuleType):
    HKEY_CURRENT_USER = 1
    REG_SZ, REG_DWORD = 1, 4

    def __init__(self):
        super().__init__("winreg")
        self.keys = {}

    def _handle(self, values):
        class Handle:
            def __enter__(handle):
                return values

            def __exit__(handle, *exception):
                return False
        return Handle()

    def CreateKey(self, hive, key):
        return self._handle(self.keys.setdefault((hive, key), {}))

    def OpenKey(self, hive, key, *rest):
        if (hive, key) not in self.keys:
            raise OSError("no key")
        return self._handle(self.keys[(hive, key)])

    def SetValueEx(self, values, name, reserved, kind, value):
        values[name] = value

    def QueryValueEx(self, values, name):
        return values[name], 1

    def DeleteKey(self, hive, key):
        del self.keys[(hive, key)]


@pytest.fixture
def windows(tmp_path, monkeypatch):
    """The Windows paths of the code, with a stand-in registry and
    PowerShell. Returns the registry and the PowerShell calls."""
    registry = WritingWinreg()
    calls = []
    monkeypatch.setitem(sys.modules, "winreg", registry)
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "Local"))
    monkeypatch.setenv("SystemRoot", "C:\\Windows")
    places = {"Desktop": tmp_path / "Desktop", "Programs": tmp_path / "Programs"}

    def run(arguments, **options):
        calls.append((arguments, options))
        place = places[options["env"]["HCV_PLACE"]]
        place.mkdir(exist_ok=True)
        link = place / hcv.SHORTCUT_LINK
        link.write_text("a shortcut")
        return subprocess.CompletedProcess(arguments, 0, (str(link) + "\r\n").encode("utf-8"), b"")
    monkeypatch.setattr(hcv.subprocess, "run", run)
    return registry, calls


def windows_install(tmp_path, folder=None):
    package = hcv.read_pc_package(zip_release(tmp_path / "halo-ce-vita-1.1.2-pc-windows.zip"))
    folder = folder or tmp_path / "It's a &%PATH%$(evil) folder"
    files = hcv.install_pc_package(package, str(folder), log=lambda text: None)
    stage(str(folder / "build" / "windows"), xbox=True)
    return folder, hcv.finish_pc_install(str(folder), package.version, files, True, True, lambda text: None)


def test_windows_install_registers_for_the_current_user(tmp_path, windows):
    registry, _ = windows
    folder, warnings = windows_install(tmp_path)
    assert warnings == []
    values = registry.keys[(registry.HKEY_CURRENT_USER, hcv.UNINSTALL_KEY)]
    assert list(registry.keys) == [(1, hcv.UNINSTALL_KEY)]  # HKCU and nothing else: no admin needed
    assert values["DisplayName"] == "Halo CE" and values["DisplayVersion"] == "1.1.2"
    assert values["InstallLocation"] == str(folder) and values["NoModify"] == values["NoRepair"] == 1
    assert values["DisplayIcon"] == os.path.join(str(folder / "build" / "windows"), "halo.exe")
    assert values["UninstallString"].endswith("--uninstall") and values["EstimatedSize"] >= 0
    assert (folder / "uninstall.py").read_bytes() == open(hcv.__file__, "rb").read()
    assert hcv.read_install(str(folder))["uninstaller"] == "uninstall.py"
    assert hcv.find_existing_install() == str(folder)


def test_windows_uninstaller_from_the_one_file_program(tmp_path, windows, monkeypatch):
    program = tmp_path / "HaloCEVitaInstaller.exe"
    program.write_bytes(b"MZ the tool")
    monkeypatch.setattr(hcv.sys, "frozen", True, raising=False)
    monkeypatch.setattr(hcv.sys, "executable", str(program))
    folder, _ = windows_install(tmp_path, tmp_path / "Halo CE")
    assert (folder / "uninstall.exe").read_bytes() == b"MZ the tool"
    registry, _ = windows
    line = registry.keys[(1, hcv.UNINSTALL_KEY)]["UninstallString"]
    assert line == subprocess.list2cmdline([str(folder / "uninstall.exe"), "--uninstall"])


def test_windows_shortcuts_take_no_path_as_code(tmp_path, windows):
    _, calls = windows
    folder, _ = windows_install(tmp_path)
    assert len(calls) == 2
    for arguments, options in calls:
        assert arguments[0] == os.path.join("C:\\Windows", "System32", "WindowsPowerShell", "v1.0", "powershell.exe")
        assert not any(str(folder) in argument for argument in arguments)  # only through the environment
        assert "shell" not in options
        assert options["env"]["HCV_FOLDER"] == str(folder)
        assert options["env"]["HCV_TARGET"] == os.path.join(str(folder / "build" / "windows"), "halo.exe")
    assert [options["env"]["HCV_PLACE"] for _, options in calls] == ["Desktop", "Programs"]
    assert len(hcv.read_install(str(folder))["shortcuts"]) == 2


def test_windows_shortcut_failure_does_not_stop_the_install(tmp_path, windows, monkeypatch):
    def fail(arguments, **options):
        return subprocess.CompletedProcess(arguments, 1, b"", b"no COM here")
    monkeypatch.setattr(hcv.subprocess, "run", fail)
    folder, warnings = windows_install(tmp_path)
    assert len(warnings) == 2 and "desktop shortcut" in warnings[0] and "no COM here" in warnings[0]
    assert hcv.read_install(str(folder))["shortcuts"] == []


def test_windows_uninstall(tmp_path, windows):
    registry, _ = windows
    folder, _ = windows_install(tmp_path)
    assert (tmp_path / "Desktop" / hcv.SHORTCUT_LINK).exists()
    assert hcv.uninstall_pc(str(folder), log=lambda text: None)["kept"] == 0
    assert not folder.exists() and not registry.keys
    assert not (tmp_path / "Desktop" / hcv.SHORTCUT_LINK).exists()
    assert not (tmp_path / "Programs" / hcv.SHORTCUT_LINK).exists()


def test_the_uninstaller_leaves_the_folder_it_empties(tmp_path, monkeypatch):
    folder = tmp_path / "Halo CE"
    folder.mkdir()
    program = folder / "uninstall.exe"
    program.write_bytes(b"MZ")
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.setattr(hcv.sys, "frozen", True, raising=False)
    monkeypatch.setattr(hcv.sys, "executable", str(program))
    monkeypatch.setattr(hcv.tempfile, "tempdir", str(tmp_path))
    runs = []
    monkeypatch.setattr(hcv.subprocess, "Popen", lambda *a, **k: runs.append((a, k)))
    assert hcv.uninstall_from_outside(str(folder)) is True
    copy = runs[0][0][0][0]
    assert os.path.dirname(copy) != str(folder) and runs[0][0][0][1:] == ["--uninstall", str(folder)]
    # the copy: stays, and is deleted after it closes, its path never in the command's text
    monkeypatch.setattr(hcv.sys, "executable", copy)
    assert hcv.uninstall_from_outside(str(folder)) is False
    monkeypatch.setattr(hcv, "_hidden_window", lambda: {})
    hcv.delete_temporary_uninstaller()
    command, options = runs[-1][0][0], runs[-1][1]
    assert os.path.dirname(copy) not in command and options["env"]["HCV_DELETE"] == os.path.dirname(copy)
    # any other program deletes nothing
    runs.clear()
    monkeypatch.setattr(hcv.sys, "executable", str(tmp_path / "somewhere" / "tool.exe"))
    hcv.delete_temporary_uninstaller()
    assert not runs


# -- the command line

def test_command_line_pc_install_and_uninstall(tmp_path, capsys):
    out = staged(tmp_path)
    package = tar_release(tmp_path / (RELEASE + ".tar.gz"))
    folder = tmp_path / "Halo CE"
    assert hcv.main(["--out", str(out), "pc-install", str(folder), "--package", package, "--start-menu"]) == 0
    assert (folder / "build" / "linux" / "halo").exists()
    assert (folder / "build" / "linux" / "mcc" / "halo1" / "maps" / "a10.map").exists()
    assert (tmp_path / "home" / ".local" / "share" / "applications" / "halo-ce.desktop").exists()
    assert not (tmp_path / "home" / "Desktop").exists()
    assert hcv.main(["--out", str(out), "pc-uninstall", str(folder)]) == 0
    assert not folder.exists()
    assert hcv.main(["--out", str(out), "pc-uninstall"]) == 1
    assert hcv.main(["--out", str(out), "pc-install", str(folder)]) == 1  # no package
    assert "No PC build" in capsys.readouterr().out


def test_command_line_uninstall_window_needs_an_install(tmp_path, capsys):
    assert hcv.main(["--uninstall", str(tmp_path)]) == 1
    assert "No Halo CE install" in capsys.readouterr().out


def test_help_lists_the_new_commands(capsys):
    with pytest.raises(SystemExit):
        hcv.main(["--help"])
    text = capsys.readouterr().out
    for command in ("xbox", "mcc", "movies", "pc-files", "check", "upload", "vpk", "pc-copy", "pc-install",
                    "pc-uninstall", "selftest", "--uninstall"):
        assert command in text


# -- the window

def radios(window):
    return {widget.cget("text"): widget for widget in all_widgets(window.body)
            if widget.winfo_class() == "TRadiobutton"}


def texts(window):
    labels = all_widgets(window.body) + [window.title_label]
    return [widget.cget("text") for widget in labels if widget.winfo_class() == "TLabel"]


def buttons(window):
    return {widget.cget("text"): widget for widget in all_widgets(window.body) if widget.winfo_class() == "TButton"}


def checks(window):
    return {widget.cget("text"): widget for widget in all_widgets(window.body) if widget.winfo_class() == "TCheckbutton"}


def wizard(root, tmp_path):
    return hcv.InstallerWindow(root, str(tmp_path / "out"))


def advance(window, root, *pages):
    """Next, once for each page named, arriving on it each time."""
    for page in pages:
        window.next_button.invoke()
        root.update()
        assert window.page == page


def to_game_files(window, root, platform="pc"):
    advance(window, root, "license")
    window.accepted.set("yes")
    advance(window, root, "platform")
    window.platform.set(platform)
    advance(window, root, "game_files")


def complaints(monkeypatch):
    from tkinter import messagebox
    found = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: found.append(text))
    return found


def wait_for_finish(window, root):
    assert pump(root, lambda: window.page == "finish")
    root.update()


def package_next_to_the_tool(tmp_path, monkeypatch):
    here = tmp_path / "tool"
    here.mkdir(exist_ok=True)
    monkeypatch.setattr(hcv, "_tool_folder", lambda: str(here))
    monkeypatch.chdir(str(here))
    return tar_release(here / (RELEASE + ".tar.gz"))


def disc(tmp_path):
    image = tmp_path / "halo.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree())
    return image


def ce_folder(tmp_path):
    ce = tmp_path / "ce" / "maps"
    ce.mkdir(parents=True)
    for name, kind in hcv.PC_RESOURCE_MAPS:
        (ce / name).write_bytes(fakes.fake_resource_map(kind))
    return ce


def test_welcome_is_a_setup_wizard_welcome(root, tmp_path):
    window = wizard(root, tmp_path)
    root.update()
    assert window.page == "welcome" and root.title() == "Halo CE Setup"
    assert window.title_label.cget("text") == "Welcome to Halo CE Setup"
    assert any("fan project" in text and "No game data" in text for text in texts(window))
    assert window.sidebar.winfo_ismapped()
    assert [window.back_button.cget("text"), window.next_button.cget("text"), window.cancel_button.cget("text")] == \
        ["< Back", "Next >", "Cancel"]
    assert window.back_button.instate(["disabled"]) and window.next_button.instate(["!disabled"])
    # bottom right, in that order
    xs = [button.winfo_rootx() for button in (window.back_button, window.next_button, window.cancel_button)]
    assert xs == sorted(xs) and xs[0] > window.width // 3
    assert not hasattr(window, "step_rows")  # no step list


def test_the_license_gates_next(root, tmp_path):
    window = wizard(root, tmp_path)
    advance(window, root, "license")
    text = window.license_box.get("1.0", "end")
    assert "GNU GENERAL PUBLIC LICENSE" in text and "Version 3" in text
    assert window.license_box.cget("state") == "disabled"
    assert any("No game data is included" in line for line in texts(window))
    assert window.next_button.instate(["disabled"])
    window.next_button.invoke()
    assert window.page == "license"
    radios(window)["I accept the terms of the license"].invoke()
    root.update()
    assert window.next_button.instate(["!disabled"])
    radios(window)["I do not accept the terms of the license"].invoke()
    root.update()
    assert window.next_button.instate(["disabled"])
    window.accepted.set("yes")
    advance(window, root, "platform")


def test_greyed_entries_on_the_platform_page(root, tmp_path):
    window = wizard(root, tmp_path)
    window.show("platform")
    root.update()
    shown = radios(window)
    assert shown["PS Vita"].instate(["!disabled"]) and shown["PC"].instate(["!disabled"])
    for name in ("Android", "Nintendo Switch", "Other consoles"):
        button = shown["%s (not available yet)" % name]
        assert button.instate(["disabled"])
        button.invoke()
        button.event_generate("<Button-1>")
        root.update()
        assert window.platform.get() == "vita"


def test_game_files_with_mcc_found_is_automatic_on_one_page(root, tmp_path, monkeypatch):
    mcc = fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    assert window.mode.get() == "automatic" and window.mcc_root.get() == str(mcc)
    window.show("game_files")
    root.update()
    assert any("found here" in text for text in texts(window))
    assert not any("not found" in text for text in texts(window))
    entries = [widget for widget in all_widgets(window.body) if widget.winfo_class() == "TEntry"]
    assert len(entries) == 3  # MCC, the disc, Custom Edition: all on this one page
    assert sum(entry.instate(["disabled"]) for entry in entries) == 2
    window.mode.set("manual")
    root.update()
    assert entries[0].instate(["disabled"]) and entries[1].instate(["!disabled"])
    assert "Convert the disc's movies (takes a few minutes)" in checks(window) or \
        "Movies need ffmpeg, which was not found: skipped" in checks(window)


def test_game_files_without_mcc_is_manual_and_says_why(root, tmp_path):
    window = wizard(root, tmp_path)
    assert window.mode.get() == "manual"
    window.show("game_files")
    root.update()
    assert any("was not found on this computer, so Manual is selected" in text for text in texts(window))


def test_game_files_leaves_nothing_unchecked(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    window = wizard(root, tmp_path)
    to_game_files(window, root, "vita")
    window.next_button.invoke()
    assert window.page == "game_files" and "Choose your Xbox disc image" in errors[-1]
    # a file that is no disc image
    wrong = tmp_path / "wrong.iso"
    wrong.write_bytes(b"nothing" * 10000)
    window.xbox_source.set(str(wrong))
    window.next_button.invoke()
    assert window.page == "game_files" and len(errors) == 2
    # the disc, no Custom Edition files: the Vita needs them
    window.xbox_source.set(str(disc(tmp_path)))
    window.next_button.invoke()
    assert window.page == "game_files" and "Required Halo Custom Edition files missing" in errors[-1]
    window.ce_source.set(str(tmp_path / "nothing"))
    window.next_button.invoke()
    assert "does not exist" in errors[-1]
    window.ce_source.set(str(tmp_path))
    window.next_button.invoke()
    assert "No bitmaps.map" in errors[-1]
    window.ce_source.set(str(ce_folder(tmp_path)))
    advance(window, root, "destination")
    # on PC they are optional
    window.show("game_files")
    window.platform.set("pc")
    window.ce_source.set("")
    advance(window, root, "destination")
    # automatic with no folder, or a broken MCC
    window.show("game_files")
    window.mode.set("automatic")
    window.mcc_root.set("")
    window.next_button.invoke()
    assert "Choose MCC's folder" in errors[-1]
    broken = tmp_path / "broken"
    fakes.make_mcc(str(broken))
    os.remove(str(broken / "halo1" / "maps" / "bitmaps.map"))
    window.mcc_root.set(str(broken))
    window.next_button.invoke()
    assert "lacks" in errors[-1] and window.page == "game_files"


def test_destination_on_the_vita_needs_an_address(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    window = wizard(root, tmp_path)
    window.platform.set("vita")
    window.show("destination")
    root.update()
    assert "Test connection" in buttons(window)
    assert any("ftp://192.168.1.20:1337" in text for text in texts(window))
    window.next_button.invoke()
    assert window.page == "destination" and "Type the Vita's address" in errors[-1]
    window.host.set("ftp://192.168.1.20:1337/")
    assert window._host_port() == ("192.168.1.20", 1337)
    window.host.set("rm -rf /")
    window.next_button.invoke()
    assert window.page == "destination"
    window.host.set("192.168.1.20")
    assert window._host_port() == ("192.168.1.20", hcv.FTP_PORT)
    bad = tmp_path / "bad.vpk"
    bad.write_bytes(b"not a zip")
    window.vpk.set(str(bad))
    window.next_button.invoke()
    assert "not a VPK" in errors[-1]
    window.vpk.set("")
    advance(window, root, "ready")
    assert "none chosen" in window.summary_box.get("1.0", "end")


class Greeter:
    """A port that answers as VitaShell's FTP does when asked in."""

    def __init__(self):
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        client, _ = self.server.accept()
        client.sendall(b"220 FTPServer ready.\r\n")
        for line in client.makefile("rb"):
            client.sendall(b"200 OK\r\n" if line.startswith(b"TYPE") else b"230 in\r\n")


def test_test_connection(root, tmp_path):
    window = wizard(root, tmp_path)
    window.platform.set("vita")
    window.show("destination")
    window.host.set("not an address!")
    buttons(window)["Test connection"].invoke()
    assert "Type the Vita's address" in window.test_result.get()
    greeter = Greeter()
    window.host.set("127.0.0.1:%d" % greeter.port)
    buttons(window)["Test connection"].invoke()
    assert pump(root, lambda: window.test_result.get().startswith("Connected to the Vita"), 5)
    assert buttons(window)["Test connection"].instate(["!disabled"])
    greeter.server.close()
    closed = socket.socket()
    closed.bind(("127.0.0.1", 0))
    port = closed.getsockname()[1]
    closed.close()
    window.host.set("127.0.0.1:%d" % port)
    buttons(window)["Test connection"].invoke()
    assert pump(root, lambda: window.test_result.get().startswith("Cannot connect"), 5)


def test_destination_on_pc(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    window = wizard(root, tmp_path)
    window.platform.set("pc")
    window.game_bytes = 1 << 30
    window.show("destination")
    root.update()
    assert window.install_folder.get() == hcv.default_install_folder()
    assert list(checks(window)) == ["Create a desktop shortcut", "Create an applications menu entry"]
    assert window.desktop_shortcut.get() and window.start_menu.get()
    assert "Browse..." in buttons(window)
    assert window.space.get().startswith("Space needed: 1.0 GB") and "Space free: " in window.space.get()
    # no build found: chosen here
    window.next_button.invoke()
    assert window.page == "destination" and "does not exist" in errors[-1]
    package = tar_release(tmp_path / (RELEASE + ".tar.gz"))
    window.package.set(package)
    root.update()
    assert "Space needed: 1.0 GB" in window.space.get()
    window.install_folder.set("relative")
    window.next_button.invoke()
    assert "full folder path" in errors[-1]
    window.install_folder.set(str(tmp_path / "Halo CE"))
    monkeypatch.setattr(window, "free_bytes", lambda path: 1024)
    window.next_button.invoke()
    assert "Not enough free space" in errors[-1] and window.page == "destination"


def test_a_folder_with_files_asks_first(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    answers = []
    monkeypatch.setattr(messagebox, "askyesno", lambda title, text, **options: answers.append(text) or False)
    complaints(monkeypatch)
    window = wizard(root, tmp_path)
    window.platform.set("pc")
    window.show("destination")
    window.package.set(tar_release(tmp_path / (RELEASE + ".tar.gz")))
    folder = tmp_path / "busy"
    folder.mkdir()
    (folder / "file").write_text("")
    window.install_folder.set(str(folder))
    window.next_button.invoke()
    assert window.page == "destination" and "already has files" in answers[0]


def test_ready_is_a_plain_summary_with_an_install_button(root, tmp_path, monkeypatch):
    package_next_to_the_tool(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    to_game_files(window, root, "pc")
    advance(window, root, "destination", "ready")
    assert window.next_button.cget("text") == "Install"
    assert window.title_label.cget("text") == "Ready to Install"
    text = window.summary_box.get("1.0", "end")
    for line in ("Install on: PC", window.install_folder.get(), "from your MCC", "Shortcuts: desktop"):
        assert line in text
    assert window.summary_box.cget("state") == "disabled"
    window.back_button.invoke()
    assert window.page == "destination" and window.next_button.cget("text") == "Next >"


def test_automatic_on_pc_end_to_end(root, tmp_path, monkeypatch):
    """Next through every page, one Install, and a real install on the Finish
    page."""
    errors = complaints(monkeypatch)
    package_next_to_the_tool(tmp_path, monkeypatch)
    mcc = fake_steam(tmp_path, monkeypatch)
    before = tree(mcc)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    advance(window, root, "destination", "ready")
    assert not any(text.startswith("PC build (") for text in texts(window))
    window.next_button.invoke()
    root.update()
    assert window.page == "installing"
    assert window.next_button.instate(["disabled"]) and window.back_button.instate(["disabled"])
    assert window.cancel_button.instate(["!disabled"])
    # one bar, and no button of its own
    bars = [widget for widget in all_widgets(window.body) if widget.winfo_class() == "TProgressbar"]
    assert len(bars) == 1 and not buttons(window)
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done"
    assert window.title_label.cget("text") == "Setup completed"
    assert list(checks(window)) == ["Launch Halo CE"] and window.launch.get()
    assert window.next_button.cget("text") == "Finish"
    assert window.back_button.instate(["disabled"]) and window.cancel_button.instate(["disabled"])
    folder = tmp_path / "Halo CE"
    data = folder / "build" / "linux"
    assert (data / "halo").exists() and (data / "maps").is_dir()
    assert (data / "mcc" / "halo1" / "maps" / "a10.map").exists()
    assert hcv.check_install(str(folder)) == ([], [])
    assert tree(mcc) == before  # only read
    assert not list((tmp_path / "out").glob("*")) if (tmp_path / "out").exists() else True  # no second copy
    runs = []
    monkeypatch.setattr(hcv, "launch_pc", runs.append)
    window.next_button.invoke()
    assert runs == [str(folder)]


def test_launch_can_be_unticked(root, tmp_path, monkeypatch):
    window = wizard(root, tmp_path)
    window.platform.set("pc")
    window.outcome = "done"
    window.show("finish")
    window.launch.set(False)
    monkeypatch.setattr(hcv, "launch_pc", lambda folder: pytest.fail("started"))
    window.next_button.invoke()


def test_manual_on_pc_end_to_end(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    package_next_to_the_tool(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    assert not [text for text in checks(window) if "movies" in text]  # the PC build plays none
    window.xbox_source.set(str(disc(tmp_path)))
    window.ce_source.set(str(ce_folder(tmp_path)))
    advance(window, root, "destination", "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    data = tmp_path / "Halo CE" / "build" / "linux"
    assert (data / "default.xbe").exists() and (data / "maps" / "a10.map").exists()
    assert (data / "maps" / "loc.map").exists() and not (data / "movies").exists()


def test_vita_end_to_end(root, tmp_path, monkeypatch):
    """Everything in one run: gathered, sent, the VPK after them; Finish says
    what is left on the Vita."""
    errors = complaints(monkeypatch)
    vpk = tmp_path / "halo-ce-vita-1.1.2-vita.vpk"
    with zipfile.ZipFile(str(vpk), "w") as archive:
        archive.writestr("eboot.bin", b"x")
        archive.writestr("sce_sys/param.sfo", b"x")
    sent = []

    def upload(host, items, port=0, replace=False, progress=None, log=None, cancel=None, **options):
        sent.append((host, port, [item.name for item in items]))
        for index, item in enumerate(items):
            progress(item.name, index + 1, len(items))
        return {"copied": len(items), "skipped": 0, "retries": 0}
    monkeypatch.setattr(hcv, "upload_files", upload)
    mcc = fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    assert window.mcc_root.get() == str(mcc)
    to_game_files(window, root, "vita")
    advance(window, root, "destination")
    window.host.set("ftp://192.168.1.20:1337")
    window.vpk.set(str(vpk))
    advance(window, root, "ready")
    assert "PS Vita at 192.168.1.20:1337" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert [entry[:2] for entry in sent] == [("192.168.1.20", 1337)] * 2
    assert "a10.map" in sent[0][2] and sent[1][2] == [vpk.name]
    assert hcv.mcc_staged(str(tmp_path / "out"))
    words = " ".join(texts(window))
    assert "press Cross on %s" % vpk.name in words and "VitaShell" in words
    assert "Launch Halo CE" not in checks(window)


def test_the_vita_goes_on_from_files_already_gathered(root, tmp_path, monkeypatch):
    complaints(monkeypatch)
    calls = []
    monkeypatch.setattr(hcv, "upload_files", lambda host, items, *a, **k: calls.append(items) or {})
    out = tmp_path / "out"
    stage(str(out), xbox=True, pc=True)
    window = hcv.InstallerWindow(root, str(out))
    window.mode.set("manual")
    to_game_files(window, root, "vita")
    advance(window, root, "destination")
    window.host.set("192.168.1.20")
    advance(window, root, "ready")
    assert "already gathered" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert window.outcome == "done" and len(calls) == 1


def test_a_failure_then_back_tries_again(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    mcc = fake_steam(tmp_path, monkeypatch)
    calls = []

    def upload(*arguments, **options):
        calls.append(arguments)
        if len(calls) == 1:
            raise hcv.FtpError("the Vita went to sleep")
        return {}
    monkeypatch.setattr(hcv, "upload_files", upload)
    window = wizard(root, tmp_path)
    assert window.mcc_root.get() == str(mcc)
    to_game_files(window, root, "vita")
    advance(window, root, "destination")
    window.host.set("192.168.1.20")
    advance(window, root, "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert window.outcome == "failed" and window.title_label.cget("text") == "Setup was not completed"
    assert any("the Vita went to sleep" in text for text in texts(window))
    assert window.back_button.instate(["!disabled"]) and not errors
    log = (tmp_path / "settings" / hcv.LOG_FILE).read_text()
    assert "the Vita went to sleep" in log
    window.back_button.invoke()
    root.update()
    assert window.page == "ready"
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert window.outcome == "done" and len(calls) == 2
    assert window.back_button.instate(["disabled"])


def test_cancel_asks_first(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    asked = []
    answer = [False]
    monkeypatch.setattr(messagebox, "askyesno", lambda title, text, **options: asked.append(text) or answer[0])
    closed = []
    window = wizard(root, tmp_path)
    monkeypatch.setattr(root, "destroy", lambda: closed.append(True))
    window.cancel_button.invoke()
    assert asked == ["Are you sure you want to cancel Halo CE Setup?"] and not closed
    answer[0] = True
    window.cancel_button.invoke()
    assert closed == [True]
    monkeypatch.undo()


def test_the_close_button_and_keys_are_the_buttons(root, tmp_path, monkeypatch):
    window = wizard(root, tmp_path)
    assert root.protocol("WM_DELETE_WINDOW")
    pressed = []
    monkeypatch.setattr(window, "ask_cancel", lambda: pressed.append("cancel"))
    monkeypatch.setattr(window, "go_next", lambda: pressed.append("next"))
    window.next_button.configure(command=window.go_next)
    window.cancel_button.configure(command=window.ask_cancel)
    root.focus_force()
    root.update()
    window.press(window.next_button)
    window.press(window.cancel_button)
    window.next_button.state(["disabled"])
    window.press(window.next_button)
    assert pressed == ["next", "cancel"]


def test_cancel_while_installing_takes_a_new_install_back(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    monkeypatch.setattr(messagebox, "askyesno", lambda *a, **k: True)
    errors = complaints(monkeypatch)
    package_next_to_the_tool(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    folder = tmp_path / "Halo CE"
    started = threading.Event()
    release = threading.Event()
    real = hcv.install_mcc_files

    def slow(path, staging, progress, log):
        progress("a10.map", 1, 2)
        started.set()
        release.wait(10)
        return real(path, staging, progress, log)
    monkeypatch.setattr(hcv, "install_mcc_files", slow)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(folder))
    to_game_files(window, root, "pc")
    advance(window, root, "destination", "ready")
    window.next_button.invoke()
    assert pump(root, started.is_set)
    assert (folder / "build" / "linux" / "halo").exists()  # the build is in
    assert pump(root, lambda: window.current_file.get() == "a10.map", 5)
    window.cancel_button.invoke()
    root.update()
    assert window.cancel_button.instate(["disabled"]) and window.current_task.get() == "Cancelling..."
    release.set()
    wait_for_finish(window, root)
    assert window.outcome == "cancelled" and window.title_label.cget("text") == "Setup was cancelled"
    assert not folder.exists() and not errors  # and gone again
    assert hcv.find_existing_install() is None


def test_a_failed_new_install_is_taken_back(root, tmp_path, monkeypatch):
    complaints(monkeypatch)
    package_next_to_the_tool(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    monkeypatch.setattr(hcv, "install_mcc_files", lambda *a: (_ for _ in ()).throw(hcv.InstallerError("no room")))
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    advance(window, root, "destination", "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert window.outcome == "failed" and not (tmp_path / "Halo CE").exists()


def existing_install(tmp_path, monkeypatch):
    package_next_to_the_tool(tmp_path, monkeypatch)
    return installed(tmp_path)


def test_run_again_offers_repair_or_uninstall(root, tmp_path, monkeypatch):
    folder = existing_install(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    assert window.flow()[:2] == ("welcome", "maintenance") and window.action.get() == "repair"
    advance(window, root, "maintenance")
    assert any(str(folder) == text for text in texts(window))
    assert list(radios(window)) == ["Repair", "Uninstall", "Install again, or install on a PS Vita"]
    # nothing damaged: straight to Ready
    advance(window, root, "ready")
    assert window.next_button.cget("text") == "Repair"
    assert "Every file is there" in window.summary_box.get("1.0", "end")
    window.back_button.invoke()
    # or the whole wizard
    window.action.set("install")
    advance(window, root, "license")


def test_repair_puts_back_what_is_damaged(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    folder = existing_install(tmp_path, monkeypatch)
    (folder / "README.txt").unlink()
    (folder / "build" / "linux" / "halo").write_bytes(b"broken")
    (folder / "build" / "linux" / "config.toml").write_text("kept")
    window = wizard(root, tmp_path)
    advance(window, root, "maintenance", "ready")  # the build is next to the tool, the game files are fine
    assert "Program files to put back: 2" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert window.title_label.cget("text") == "Repair completed"
    assert (folder / "README.txt").exists() and (folder / "build" / "linux" / "halo").read_bytes() == b"\x7fELF program"
    assert (folder / "build" / "linux" / "config.toml").read_text() == "kept"
    assert hcv.check_install(str(folder)) == ([], [])
    assert (tmp_path / "home" / "Desktop" / "halo-ce.desktop").exists()


def test_repair_asks_for_what_it_needs(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    folder = existing_install(tmp_path, monkeypatch)
    (folder / "build" / "linux" / "maps" / "a10.map").unlink()
    (folder / "README.txt").unlink()
    (tmp_path / "tool" / (RELEASE + ".tar.gz")).rename(tmp_path / (RELEASE + ".tar.gz"))
    window = wizard(root, tmp_path)
    advance(window, root, "maintenance", "game_files")
    window.mode.set("manual")
    window.xbox_source.set(str(disc(tmp_path)))
    advance(window, root, "destination")
    entries = [widget for widget in all_widgets(window.body) if widget.winfo_class() == "TEntry"]
    assert entries[0].instate(["disabled"])  # the folder is the install's
    window.package.set(str(tmp_path / (RELEASE + ".tar.gz")))
    advance(window, root, "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert hcv.check_install(str(folder)) == ([], [])


def test_uninstall_from_the_wizard(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    folder = existing_install(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    advance(window, root, "maintenance")
    window.action.set("uninstall")
    advance(window, root, "ready")
    assert window.next_button.cget("text") == "Uninstall"
    assert "saved games and settings are kept" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert window.title_label.cget("text") == "Uninstall completed" and not folder.exists()
    assert not checks(window)
    monkeypatch.setattr(hcv, "launch_pc", lambda folder: pytest.fail("started"))
    window.next_button.invoke()


def test_started_as_the_uninstaller(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    folder = existing_install(tmp_path, monkeypatch)
    (folder / "build" / "linux" / "config.toml").write_text("the player's")
    window = hcv.InstallerWindow(root, str(tmp_path / "out"), str(folder))
    assert window.page == "ready" and window.back_button.instate(["disabled"])
    assert window.next_button.cget("text") == "Uninstall"
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert any("still has 1 files" in text for text in texts(window))
    assert (folder / "build" / "linux" / "config.toml").exists()


def test_a_second_game_shows_its_page(root, tmp_path, monkeypatch):
    """What 1.2.0 needs: an entry in GAMES, and the page is there."""
    monkeypatch.setattr(hcv, "GAMES", hcv.GAMES + (hcv.Choice("halo-2", "Halo 2"),))
    window = wizard(root, tmp_path)
    advance(window, root, "license")
    window.accepted.set("yes")
    advance(window, root, "game")
    assert list(radios(window)) == ["Halo: Combat Evolved", "Halo 2"]
    advance(window, root, "platform")


def test_nothing_of_the_old_flow_is_left():
    source = open(hcv.__file__, encoding="utf-8").read()
    for word in ("Skip this step", "step_rows", "Get everything", "Required", "pictures/", "sv_ttk"):
        assert word not in source.replace("Required Halo Custom Edition files", "")
