# SPDX-License-Identifier: GPL-3.0-only
"""The required Halo Custom Edition files (bitmaps.map, sounds.map,
loc.map): where the output folder has them (maps/, or MCC's
custom_edition, which the game reads when the maps folder has none), what
the player is told when they are missing, and the places they are found by
themselves (a Custom Edition install through Windows' registry, mocked; an
installer in Downloads)."""

import os
import sys

import pytest

import fakes
import halo_ce_vita_installer as hcv
from test_mcc import FakeWinreg


def put_pc_maps(folder, kinds=None):
    os.makedirs(str(folder), exist_ok=True)
    for name, kind in hcv.PC_RESOURCE_MAPS:
        (folder / name).write_bytes(fakes.fake_resource_map((kinds or {}).get(name, kind)))


def test_nothing_there(tmp_path):
    status = hcv.pc_files_status(str(tmp_path / "out"))
    assert not status.complete and status.missing == ["bitmaps.map", "sounds.map", "loc.map"]
    text = hcv.pc_files_help(status.missing)
    for words in ("bitmaps.map, sounds.map, loc.map", "custom_edition", "halocesetup_en_1.00.exe",
                  hcv.CE_INSTALLER_URL, "Step 3"):
        assert words in text


def test_in_the_maps_folder(tmp_path):
    out = tmp_path / "out"
    put_pc_maps(out / "maps")
    status = hcv.pc_files_status(str(out))
    assert status.complete and set(status.found.values()) == {"maps"}


def test_from_mcc_alone(tmp_path):
    """An MCC-only install: custom_edition's copies count (the game takes
    them when the maps folder has none); MCC's own halo1/maps/bitmaps.map
    never does."""
    mcc = tmp_path / "MCC"
    fakes.make_mcc(str(mcc))
    out = tmp_path / "out"
    hcv.install_mcc_files(str(mcc), str(out), log=lambda message: None)
    status = hcv.pc_files_status(str(out))
    assert status.complete and set(status.found.values()) == {"MCC's custom_edition"}
    # MCC without its custom_edition: missing, even with its own bitmaps.map
    out2 = tmp_path / "out2"
    fakes.make_mcc(str(tmp_path / "MCC2"), custom_edition=False)
    hcv.install_mcc_files(str(tmp_path / "MCC2"), str(out2), log=lambda message: None)
    assert (out2 / "mcc" / "halo1" / "maps" / "bitmaps.map").exists()
    assert hcv.pc_files_status(str(out2)).missing == ["bitmaps.map", "sounds.map", "loc.map"]


def test_damaged_or_wrong_files_are_missing(tmp_path):
    out = tmp_path / "out"
    put_pc_maps(out / "maps", {"sounds.map": 1})            # a bitmaps.map named sounds.map
    (out / "maps" / "loc.map").write_bytes(b"\0" * 64)      # no header
    assert hcv.pc_files_status(str(out)).missing == ["sounds.map", "loc.map"]
    # one place each is fine
    put_pc_maps(out / "mcc" / "halo1" / "maps" / "custom_edition")
    status = hcv.pc_files_status(str(out))
    assert status.complete
    assert status.found == {"bitmaps.map": "maps", "sounds.map": "MCC's custom_edition",
                            "loc.map": "MCC's custom_edition"}


def test_check_command(tmp_path, capsys):
    out = tmp_path / "out"
    assert hcv.main(["--out", str(out), "check"]) == 1
    assert "Required Halo Custom Edition files missing" in capsys.readouterr().out
    put_pc_maps(out / "maps")
    assert hcv.main(["--out", str(out), "check"]) == 0
    assert "in place" in capsys.readouterr().out


def test_pc_files_from_an_mcc_folder(tmp_path):
    """pc-files --mcc takes MCC's folder too, not only its custom_edition."""
    mcc = tmp_path / "MCC"
    fakes.make_mcc(str(mcc))
    out = tmp_path / "out"
    assert hcv.main(["--out", str(out), "pc-files", "--mcc", str(mcc)]) == 0
    assert sorted(os.listdir(out / "maps")) == ["bitmaps.map", "loc.map", "sounds.map"]
    assert (out / "maps" / "bitmaps.map").read_bytes() == \
        (mcc / "halo1" / "maps" / "custom_edition" / "bitmaps.map").read_bytes()


def test_custom_edition_install_from_the_registry(tmp_path, monkeypatch):
    install = tmp_path / "Program Files (x86)" / "Microsoft Games" / "Halo Custom Edition"
    put_pc_maps(install / "maps")
    key = r"SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Halo CE"
    monkeypatch.setitem(sys.modules, "winreg",
                        FakeWinreg({(FakeWinreg.HKEY_LOCAL_MACHINE, key): {"EXE Path": str(install)}}))
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.delenv("ProgramFiles(x86)", raising=False)
    monkeypatch.delenv("ProgramFiles", raising=False)
    assert hcv.find_ce_install_folders() == [str(install / "maps")]
    # and the usual place without the registry
    monkeypatch.setitem(sys.modules, "winreg", FakeWinreg({}))
    monkeypatch.setenv("ProgramFiles(x86)", str(tmp_path / "Program Files (x86)"))
    assert hcv.find_ce_install_folders() == [str(install / "maps")]


def test_mcc_custom_edition_from_the_registry(tmp_path, monkeypatch):
    mcc = tmp_path / "D" / "Games" / hcv.MCC_FOLDER_NAME
    fakes.make_mcc(str(mcc))
    monkeypatch.setattr(hcv, "steam_library_folders", lambda: [])
    monkeypatch.setattr(hcv, "find_mcc_folders", lambda: [str(mcc)])
    assert hcv.find_mcc_custom_edition() == [str(mcc / "halo1" / "maps" / "custom_edition")]


def test_installers_in_downloads(tmp_path, monkeypatch):
    home = tmp_path / "home"
    (home / "Downloads").mkdir(parents=True)
    (home / "Downloads" / "halocesetup_en_1.00.exe").write_bytes(b"MZ")
    (home / "Downloads" / "HALOCESETUP (1).EXE").write_bytes(b"MZ")
    (home / "Downloads" / "other.exe").write_bytes(b"MZ")
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("USERPROFILE", str(home))
    monkeypatch.chdir(str(tmp_path))
    found = [os.path.basename(path) for path in hcv.find_ce_installers()]
    assert found == ["HALOCESETUP (1).EXE", "halocesetup_en_1.00.exe"]
