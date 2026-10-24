# SPDX-License-Identifier: GPL-3.0-only
"""Step 1's other way: the Halo CE content of the player's own Halo: The
Master Chief Collection, found (Steam's libraries, Windows' registry, mocked
here), copied as the game reads it (ux0:data/haloce-vita/mcc/...), and the
Halo 2 test builds' files only when asked. Every MCC file here is made up
(fakes.make_mcc)."""

import os
import sys
import types

import pytest

import fakes
import halo_ce_vita_installer as hcv

MCC_PARTS = ("steamapps", "common", hcv.MCC_FOLDER_NAME)


def mcc_folder(tmp_path, **options):
    root = tmp_path.joinpath("SteamLibrary", *MCC_PARTS)
    files = fakes.make_mcc(str(root), **options)
    return root, files


def relative_files(folder):
    found = []
    for directory, _, names in os.walk(str(folder)):
        for name in names:
            found.append(os.path.relpath(os.path.join(directory, name), str(folder)).replace(os.sep, "/"))
    return sorted(found)


def test_mcc_root_from_any_pick(tmp_path):
    root, _ = mcc_folder(tmp_path)
    library = tmp_path / "SteamLibrary"
    for pick in (root, root / "halo1", root / "halo1" / "maps", root / "halo1" / "maps" / "custom_edition",
                 library, library / "steamapps", library / "steamapps" / "common"):
        assert hcv.mcc_root_of(str(pick)) == str(root), pick
    assert hcv.mcc_root_of(str(tmp_path)) is None
    assert hcv.mcc_root_of(str(tmp_path / "missing")) is None


def test_find_mcc_in_steam_libraries(tmp_path, monkeypatch):
    root, _ = mcc_folder(tmp_path)
    monkeypatch.setattr(hcv, "steam_library_folders", lambda: [str(tmp_path / "nothing"),
                                                               str(tmp_path / "SteamLibrary")])
    assert hcv.find_mcc_folders() == [str(root)]


class FakeWinreg(types.ModuleType):
    """winreg with the keys given: {(hive, key): {value: data}}."""
    HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE = 1, 2
    KEY_READ, KEY_WOW64_64KEY, KEY_WOW64_32KEY = 0x20019, 0x100, 0x200

    def __init__(self, keys):
        super().__init__("winreg")
        self.keys = keys

    def OpenKey(self, hive, key, *rest):
        if (hive, key) not in self.keys:
            raise OSError("no key")
        values = self.keys[(hive, key)]

        class Handle:
            def __enter__(handle):
                return values

            def __exit__(handle, *exception):
                return False
        return Handle()

    def QueryValueEx(self, values, name):
        if name not in values:
            raise OSError("no value")
        return values[name], 1


def test_find_mcc_from_the_registry(tmp_path, monkeypatch):
    """On Windows: Steam's uninstall entry for MCC (app 976730) names its
    folder, even in a library libraryfolders.vdf no longer lists."""
    root, _ = mcc_folder(tmp_path)
    key = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 976730"
    monkeypatch.setitem(sys.modules, "winreg",
                        FakeWinreg({(FakeWinreg.HKEY_LOCAL_MACHINE, key): {"InstallLocation": str(root)}}))
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.setattr(hcv, "steam_library_folders", lambda: [])
    assert hcv.find_mcc_folders() == [str(root)]


def test_steam_found_through_the_registry(tmp_path, monkeypatch):
    """Steam's own folder from the registry (SteamPath), its
    libraryfolders.vdf naming the library MCC is in."""
    steam = tmp_path / "Steam"
    (steam / "steamapps").mkdir(parents=True)
    library = tmp_path / "Games"
    root = library.joinpath(*MCC_PARTS)
    fakes.make_mcc(str(root))
    (steam / "steamapps" / "libraryfolders.vdf").write_text(
        '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n'
        % (str(steam).replace("\\", "\\\\"), str(library).replace("\\", "\\\\")))
    monkeypatch.setitem(sys.modules, "winreg", FakeWinreg(
        {(FakeWinreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"): {"SteamPath": str(steam)}}))
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    monkeypatch.delenv("ProgramFiles(x86)", raising=False)
    monkeypatch.delenv("ProgramFiles", raising=False)
    assert str(library) in hcv.steam_library_folders()
    assert hcv.find_mcc_folders() == [str(root)]


def test_install_mcc_files(tmp_path):
    root, files = mcc_folder(tmp_path)
    out = tmp_path / "out"
    log = []
    plan = hcv.install_mcc_files(str(tmp_path / "SteamLibrary"), str(out), log=log.append)
    assert plan.root == str(root)
    expected = sorted(
        ["mcc/halo1/maps/%s.map" % stem for stem in hcv.MCC_CAMPAIGN_MAPS + hcv.MCC_MULTIPLAYER_MAPS] +
        ["mcc/halo1/maps/bitmaps.map", "mcc/halo1/maps/ui.map"] +
        ["mcc/halo1/maps/custom_edition/%s" % name for name, _ in hcv.PC_RESOURCE_MAPS] +
        ["mcc/halo1/sound/pc/sounds_adpcm.fsb"] +
        ["mcc/halo1/sound/pc/lst/%s" % name for name in hcv.MCC_SOUND_LISTS])
    # exactly what the game reads: not the other banks, not the other games, nothing in maps/
    assert relative_files(out) == expected
    for relative in expected:
        assert (out / relative).read_bytes() == files[relative[len("mcc/"):]]
    assert hcv.mcc_staged(str(out))
    assert plan.needed(str(out)) == 0
    # where each goes on the Vita
    places = {(item.folder, item.name) for item in hcv.staged_uploads(str(out))}
    assert ("ux0:data/haloce-vita/mcc/halo1/maps", "a10.map") in places
    assert ("ux0:data/haloce-vita/mcc/halo1/maps/custom_edition", "loc.map") in places
    assert ("ux0:data/haloce-vita/mcc/halo1/sound/pc", "sounds_adpcm.fsb") in places
    assert ("ux0:data/haloce-vita/mcc/halo1/sound/pc/lst", "sounds_adpcm.lst.bin") in places
    assert len(places) == len(expected)
    # again: kept
    log = []
    hcv.install_mcc_files(str(root), str(out), log=log.append)
    assert all("already there" in line for line in log)


def test_install_mcc_halo2_only_when_asked(tmp_path):
    root, files = mcc_folder(tmp_path, halo2=True)
    out = tmp_path / "out"
    hcv.install_mcc_files(str(root), str(out), log=lambda message: None)
    assert not (out / "maps").exists()
    hcv.install_mcc_files(str(root), str(out), halo2=True, log=lambda message: None)
    assert sorted(os.listdir(out / "maps")) == sorted(hcv.H2_FILES)
    places = {(item.folder, item.name) for item in hcv.staged_uploads(str(out))}
    for name in hcv.H2_FILES:
        assert ("ux0:data/haloce-vita/maps", name) in places
    # (another .dat in the maps folder is not copied)
    (out / "maps" / "other.dat").write_bytes(b"x")
    assert ("ux0:data/haloce-vita/maps", "other.dat") not in {
        (item.folder, item.name) for item in hcv.staged_uploads(str(out))}


def test_install_mcc_halo2_missing(tmp_path):
    root, _ = mcc_folder(tmp_path)
    with pytest.raises(hcv.InstallerError, match="zanzibar.map"):
        hcv.install_mcc_files(str(root), str(tmp_path / "out"), halo2=True, log=lambda message: None)
    assert not (tmp_path / "out" / "mcc").exists()  # nothing copied


def test_install_mcc_refuses_a_bad_folder(tmp_path):
    with pytest.raises(hcv.InstallerError, match="No Halo: The Master Chief Collection"):
        hcv.install_mcc_files(str(tmp_path), str(tmp_path / "out"), log=lambda message: None)
    root, _ = mcc_folder(tmp_path)
    os.remove(str(root / "halo1" / "maps" / "d40.map"))
    with pytest.raises(hcv.InstallerError, match="d40.map"):
        hcv.install_mcc_files(str(root), str(tmp_path / "out"), log=lambda message: None)
    fakes.make_mcc(str(root))
    (root / "halo1" / "maps" / "b30.map").write_bytes(b"daeh" + b"\x05\0\0\0" + b"x" * 50)  # an Xbox map's version
    with pytest.raises(hcv.InstallerError, match="header"):
        hcv.install_mcc_files(str(root), str(tmp_path / "out"), log=lambda message: None)
    assert not (tmp_path / "out" / "mcc").exists()


def test_install_mcc_without_the_optional_files(tmp_path):
    root, _ = mcc_folder(tmp_path, sound=False, custom_edition=False)
    log = []
    hcv.install_mcc_files(str(root), str(tmp_path / "out"), log=log.append)
    notes = "\n".join(line for line in log if line.startswith("Note:"))
    assert "without sound effects" in notes and "step 3" in notes
    assert hcv.mcc_staged(str(tmp_path / "out"))


def test_install_mcc_stopped_leaves_no_part_file(tmp_path):
    root, _ = mcc_folder(tmp_path)
    out = tmp_path / "out"

    def stop(message, done, total):
        if done > 0:
            raise hcv.Cancelled()
    with pytest.raises(hcv.Cancelled):
        hcv.install_mcc_files(str(root), str(out), progress=stop, log=lambda message: None)
    assert not [name for name in relative_files(out) if name.endswith(".part")]


def test_mcc_command_line(tmp_path, monkeypatch):
    root, _ = mcc_folder(tmp_path, halo2=True)
    out = str(tmp_path / "out")
    monkeypatch.setattr(hcv, "find_mcc_folders", lambda: [str(root)])
    assert hcv.main(["--out", out, "mcc"]) == 0
    assert hcv.mcc_staged(out) and not os.path.exists(os.path.join(out, "maps"))
    assert hcv.main(["--out", out, "mcc", str(root), "--halo2"]) == 0
    assert sorted(os.listdir(os.path.join(out, "maps"))) == sorted(hcv.H2_FILES)
    monkeypatch.setattr(hcv, "find_mcc_folders", lambda: [])
    assert hcv.main(["--out", out, "mcc"]) == 1
