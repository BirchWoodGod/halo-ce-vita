# SPDX-License-Identifier: GPL-3.0-only
"""Step 1 on generated Xbox disc images (XDVDFS) and folders, and the name
and path checks every file written goes through."""

import os
import struct

import pytest

import fakes
import halo_ce_vita_installer as hcv


@pytest.mark.parametrize("name", ["..", ".", "", "a/b", "a\\b", "C:evil", "/etc", "CON", "nul.txt", "x\r\nDELE y",
                                  "tab\tname", "trailing.", " lead", "a" * 200, "what?", "pipe|"])
def test_unsafe_names(name):
    with pytest.raises(hcv.InstallerError):
        hcv.check_name(name)


@pytest.mark.parametrize("name", ["[h3]_sandtrap.map", "a10.map", "Blood Gulch (v2).map", "intro.mp4"])
def test_safe_names(name):
    assert hcv.check_name(name) == name


def test_safe_join_stays_inside(tmp_path):
    root = tmp_path / "out"
    root.mkdir()
    assert hcv.safe_join(str(root), "maps", "a10.map").startswith(os.path.realpath(str(root)))
    outside = tmp_path / "elsewhere"
    outside.mkdir()
    try:
        os.symlink(str(outside), str(root / "maps"), target_is_directory=True)
    except (OSError, NotImplementedError):
        pytest.skip("no symbolic links here")
    with pytest.raises(hcv.InstallerError, match="link"):
        hcv.safe_join(str(root), "maps", "a10.map")


@pytest.mark.parametrize("base", [0, 0x18300000])
def test_xiso_extract(tmp_path, base):
    image = tmp_path / "Halo (USA).iso"
    fakes.make_xiso(str(image), fakes.xbox_tree(), base=base)
    with hcv.XisoImage(str(image)) as xiso:
        assert xiso.base == base
        names = ["/".join(entry.path) for entry in xiso.walk()]
        assert "maps/a10.map" in names and "bink/intro.bik" in names and "default.xbe" in names
        assert xiso.find("MAPS", "UI.MAP").size == len(fakes.xbox_tree()["maps"]["ui.map"])
    out = tmp_path / "out"
    written = hcv.extract_xbox_files(str(image), str(out), log=lambda message: None)
    assert written["default.xbe"] == 304
    tree = fakes.xbox_tree()
    for name, data in tree["maps"].items():
        assert (out / "maps" / name).read_bytes() == data
    assert (out / "default.xbe").read_bytes() == tree["default.xbe"]
    assert not (out / "bink").exists()
    # again: the files already there are kept
    log = []
    hcv.extract_xbox_files(str(image), str(out), log=log.append)
    assert all("already there" in line for line in log)


def test_unpacked_folder(tmp_path):
    folder = tmp_path / "Halo"
    (folder / "Maps").mkdir(parents=True)
    tree = fakes.xbox_tree()
    for name, data in tree["maps"].items():
        (folder / "Maps" / name.upper()).write_bytes(data)
    (folder / "default.xbe").write_bytes(tree["default.xbe"])
    out = tmp_path / "out"
    hcv.extract_xbox_files(str(folder), str(out), log=lambda message: None)
    assert (out / "maps" / "a10.map").read_bytes() == tree["maps"]["a10.map"]
    # choosing the maps folder itself works too
    out2 = tmp_path / "out2"
    hcv.extract_xbox_files(str(folder / "Maps"), str(out2), log=lambda message: None)
    assert (out2 / "default.xbe").exists()


def test_not_the_xbox_game(tmp_path):
    image = tmp_path / "other.iso"
    fakes.make_xiso(str(image), {"default.xbe": b"XBEH", "maps": {"bloodgulch.map": b"x"}})
    with pytest.raises(hcv.InstallerError, match="lacks"):
        hcv.extract_xbox_files(str(image), str(tmp_path / "out"))
    plain = tmp_path / "plain.iso"
    plain.write_bytes(b"\0" * 200000)
    with pytest.raises(hcv.InstallerError, match="not an Xbox disc image"):
        hcv.extract_xbox_files(str(plain), str(tmp_path / "out"))


@pytest.mark.parametrize("evil", ["..", "a/../../x.map", "\\..\\x.map", "C:x.map", "con.map", "../.map"])
def test_hostile_names_in_the_image(tmp_path, evil):
    image = tmp_path / "evil.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree(extra_maps={evil: b"evil"}))
    out = tmp_path / "deep" / "out"
    if evil.endswith(".map"):
        with pytest.raises(hcv.InstallerError, match="unsafe"):
            hcv.extract_xbox_files(str(image), str(out), log=lambda message: None)
    else:
        # not a map: never taken
        hcv.extract_xbox_files(str(image), str(out), log=lambda message: None)
    for folder, _, names in os.walk(str(tmp_path)):
        for name in names:
            full = os.path.join(folder, name)
            assert full == str(image) or full.startswith(str(out) + os.sep)


def test_file_past_the_end(tmp_path):
    image = tmp_path / "cut.iso"
    layout = fakes.make_xiso(str(image), fakes.xbox_tree())
    sector, size = layout["maps/a10.map"]
    with open(str(image), "r+b") as file:
        file.truncate(sector * 2048 + 10)
    with pytest.raises(hcv.InstallerError):
        hcv.extract_xbox_files(str(image), str(tmp_path / "out"), log=lambda message: None)


def test_directory_loops_end(tmp_path):
    image = tmp_path / "loop.iso"
    layout = fakes.make_xiso(str(image), fakes.xbox_tree())
    sector, size = layout["maps"]
    with open(str(image), "r+b") as file:
        # the maps table's first entry's left and right point back at itself
        file.seek(sector * 2048)
        file.write(struct.pack("<HH", 0, 0))
        file.seek(sector * 2048)
        file.write(struct.pack("<HH", 0, 0))
    with hcv.XisoImage(str(image)) as xiso:
        entries = list(xiso.walk())
        assert len(entries) < 100
    # a folder entry that points at the root again
    with open(str(image), "r+b") as file:
        file.seek(32 * 2048 + 20)
        root_sector, root_size = struct.unpack("<II", file.read(8))
        file.seek(root_sector * 2048)
        table = file.read(root_size)
        at = table.index(b"\x04maps") - 13
        file.seek(root_sector * 2048 + at + 4)
        file.write(struct.pack("<II", root_sector, root_size))
    with hcv.XisoImage(str(image)) as xiso:
        assert len(list(xiso.walk())) < 1000
        assert xiso.find("maps", "maps", "maps", "default.xbe") is not None


def test_steam_library_folders_vdf(tmp_path):
    vdf = tmp_path / "libraryfolders.vdf"
    vdf.write_text('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"C:\\\\Program Files (x86)\\\\Steam"\n'
                   '\t\t"apps" { "976730" "1" }\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"D:\\\\SteamLibrary"\n\t}\n}\n')
    assert hcv.parse_library_folders(str(vdf)) == ["C:\\Program Files (x86)\\Steam", "D:\\SteamLibrary"]
    old = tmp_path / "old.vdf"
    old.write_text('"LibraryFolders"\n{\n\t"TimeNextStatsReport"\t"123"\n\t"1"\t"E:\\\\Games\\\\Steam"\n}\n')
    assert hcv.parse_library_folders(str(old)) == ["E:\\Games\\Steam"]


def test_mcc_and_pc_folders(tmp_path, monkeypatch):
    library = tmp_path / "SteamLibrary"
    folder = library.joinpath(*hcv.MCC_CUSTOM_EDITION)
    folder.mkdir(parents=True)
    for name, kind in hcv.PC_RESOURCE_MAPS:
        (folder / name).write_bytes(fakes.fake_resource_map(kind))
    # MCC's own maps in halo1/maps: not these
    (folder.parent / "bitmaps.map").write_bytes(b"\x05\0\0\0" + b"\0" * 100)
    monkeypatch.setattr(hcv, "steam_library_folders", lambda: [str(library)])
    assert hcv.find_mcc_custom_edition() == [str(folder)]
    assert hcv.pc_maps_folder(str(library / "steamapps" / "common" / "Halo The Master Chief Collection")) == \
        str(folder)
    out = tmp_path / "out"
    hcv.copy_pc_maps_from_folder(str(folder), str(out), log=lambda message: None)
    assert sorted(os.listdir(out / "maps")) == ["bitmaps.map", "loc.map", "sounds.map"]
    # the wrong maps (MCC's halo1/maps ones, say): refused
    wrong = tmp_path / "wrong"
    wrong.mkdir()
    for name, kind in hcv.PC_RESOURCE_MAPS:
        (wrong / name).write_bytes(fakes.fake_resource_map(kind + 1))
    with pytest.raises(hcv.InstallerError, match="header"):
        hcv.copy_pc_maps_from_folder(str(wrong), str(tmp_path / "out3"), log=lambda message: None)
    with pytest.raises(hcv.InstallerError, match="No bitmaps.map"):
        hcv.copy_pc_maps_from_folder(str(tmp_path / "out3"), str(tmp_path / "out4"), log=lambda message: None)
