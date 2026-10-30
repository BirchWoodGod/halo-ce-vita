# SPDX-License-Identifier: GPL-3.0-only
"""The window's new front: the platform, Automatic or Manual, the copy. The
pages of each choice (no display needed), greyed entries that cannot be
picked, Automatic finding a stand-in MCC in a stand-in Steam library, Manual's
sources, the copy into a stand-in PC build, the game page that is ready for
a second game. Every game file here is made up (fakes)."""

import os
import sys

import pytest

import fakes
import halo_ce_vita_installer as hcv
from test_window import all_widgets, pump, root, stage  # noqa: F401 (root: the fixture)

MCC_PARTS = ("steamapps", "common", hcv.MCC_FOLDER_NAME)


# -- the pages of each choice

def test_flow_of_each_choice():
    vita_copy = ("copy_vita", "vpk", "done")
    assert hcv.install_flow("vita", "automatic") == ("platform", "mode", "automatic") + vita_copy
    assert hcv.install_flow("vita", "manual") == ("platform", "mode", "game_files", "movies", "pc_files") + vita_copy
    assert hcv.install_flow("pc", "automatic") == ("platform", "mode", "automatic", "copy_pc", "done")
    # the PC build plays no movies
    assert hcv.install_flow("pc", "manual") == ("platform", "mode", "game_files", "pc_files", "copy_pc", "done")


def test_every_flow_page_is_drawn_by_a_method():
    for platform in ("vita", "pc"):
        for mode in ("automatic", "manual"):
            for page in hcv.install_flow(platform, mode):
                assert hasattr(hcv.InstallerWindow, "page_" + page) and page in hcv.PAGE_PICTURES
                assert page in hcv.InstallerWindow.PAGES


def test_greyed_platforms_cannot_be_picked():
    greyed = [choice for choice in hcv.PLATFORMS if not choice.available]
    assert greyed and [choice.key for choice in hcv.PLATFORMS if choice.available] == ["vita", "pc"]
    for choice in greyed:
        with pytest.raises(hcv.InstallerError, match="not ready"):
            hcv.pick(hcv.PLATFORMS, choice.key)
        with pytest.raises(hcv.InstallerError):
            hcv.install_flow(choice.key, "manual")
    with pytest.raises(hcv.InstallerError):
        hcv.install_flow("dreamcast", "manual")
    with pytest.raises(hcv.InstallerError):
        hcv.install_flow("vita", "guess")


def test_a_second_game_adds_a_page_and_nothing_else():
    two = hcv.GAMES + (hcv.Choice("halo-2", "Halo 2"),)
    assert hcv.install_flow("vita", "manual", two) == ("game",) + hcv.install_flow("vita", "manual")


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


# -- the window

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


def radios(window):
    return {widget.cget("text"): widget for widget in all_widgets(window.body)
            if widget.winfo_class() == "TRadiobutton"}


def texts(window):
    return [widget.cget("text") for widget in all_widgets(window.body) if widget.winfo_class() == "TLabel"]


def buttons(window):
    return {widget.cget("text"): widget for widget in all_widgets(window.body) if widget.winfo_class() == "TButton"}


def finish(window, root):
    assert pump(root, lambda: not window.worker.is_alive() and window.messages.empty())
    pump(root, lambda: False, 0.3)


def test_platform_page_comes_first_with_greyed_entries(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    root.update()
    assert window.page == "platform" and window.platform.get() == "vita"
    shown = radios(window)
    assert [text for text in shown] == [choice.name for choice in hcv.PLATFORMS]
    for choice in hcv.PLATFORMS:
        assert shown[choice.name].instate(["disabled"]) is (not choice.available)
    # clicking or invoking a greyed one does nothing
    for choice in hcv.PLATFORMS[2:]:
        shown[choice.name].invoke()
        shown[choice.name].event_generate("<Button-1>")
        root.update()
        assert window.platform.get() == "vita"
    assert any(text == "Not yet" for text in texts(window))
    assert "No game data comes with this tool" in " ".join(texts(window))


def test_choosing_pc_changes_the_sidebar(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert window.mode.get() == "manual"  # no MCC here
    assert list(window.step_rows) == list(hcv.install_flow("vita", "manual"))
    assert window.step_rows["platform"][2].cget("text") == "PS Vita"
    radios(window)["PC"].invoke()
    root.update()
    assert list(window.step_rows) == list(hcv.install_flow("pc", "manual"))
    assert window.step_rows["platform"][2].cget("text") == "PC"
    assert "movies" not in window.step_rows and "vpk" not in window.step_rows
    assert window.step_rows["pc_files"][2].cget("text") == "Optional"
    radios(window)["PS Vita"].invoke()
    root.update()
    assert "vpk" in window.step_rows and window.step_rows["pc_files"][2].cget("text") == "Required"


def test_walking_every_flow_with_next_and_back(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    for platform in ("vita", "pc"):
        for mode in ("automatic", "manual"):
            window.show("platform")
            window.platform.set(platform)
            window.mode.set(mode)
            flow = window.flow()
            seen = [window.page]
            while window.page != "done":
                window.next_button.invoke()
                root.update()
                seen.append(window.page)
            assert tuple(seen) == flow
            assert window.step_caption.cget("text") == "FINISHED"
            assert window.next_button.cget("text") == "Close"
            window.back_button.invoke()
            assert window.page == flow[-2]
            assert window.step_caption.cget("text") == "STEP %d OF %d" % (len(flow) - 1, len(flow) - 1)
    assert window.step_state("platform") == "done"  # moved past: made


def test_skip_this_step_stays(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.go("game_files")
    assert window.next_button.cget("text") == "Skip this step"
    window.next_button.invoke()
    assert window.page == "movies" and window.step_state("game_files") == "skipped"
    assert window.step_rows["game_files"][2].cget("text") == "Skipped"


def test_automatic_finds_mcc_in_a_steam_library(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    errors = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: errors.append(text))
    mcc = fake_steam(tmp_path, monkeypatch)
    out = tmp_path / "out"
    before = tree(mcc)
    window = hcv.InstallerWindow(root, str(out))
    assert window.mcc_root.get() == str(mcc) and window.mode.get() == "automatic"
    window.next_button.invoke()
    assert window.page == "mode"
    root.update()
    assert any(text.startswith("✓ Found: ") and str(mcc) in text for text in texts(window))
    window.next_button.invoke()
    assert window.page == "automatic"
    root.update()
    assert any(text.startswith("What is taken: ") for text in texts(window))
    assert window.next_button.cget("text") == "Skip this step"
    buttons(window)["Get everything from MCC"].invoke()
    finish(window, root)
    assert not errors
    assert hcv.mcc_staged(str(out)) and hcv.pc_files_status(str(out)).complete
    assert not (out / "movies").exists()
    assert window.step_state_done("automatic") and "MCC files ready" in window.notice_text()
    assert window.next_button.cget("text") == "Next"
    assert tree(mcc) == before  # only read
    assert not (out / "mcc" / "halo3").exists()  # and only Halo CE's files taken
    window.go("done")
    rows = {what: (done, how) for done, what, how in window.done_summary()}
    assert rows["MCC files"][0] and rows["Halo PC files"][0]


def test_automatic_without_mcc_says_so_and_offers_manual(root, tmp_path, monkeypatch):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.mode.set("automatic")
    window.go("mode")
    root.update()
    assert any("was not found on this computer" in text and "Manual" in text for text in texts(window))
    window.go("automatic")
    root.update()
    assert any(text.startswith("⚠") and "not found in your Steam libraries" in text for text in texts(window))
    window.start(window.do_mcc)
    assert "Choose MCC's folder" in window.notice_text() and not window.busy()
    # MCC appears (its drive plugged in): Look again
    mcc = fake_steam(tmp_path, monkeypatch)
    buttons(window)["Look again"].invoke()
    root.update()
    assert window.mcc_root.get() == str(mcc)
    assert any(text.startswith("What is taken: ") for text in texts(window))
    # or Manual instead
    window.mcc_root.set("")
    window.show("automatic")
    buttons(window)["Use Manual instead"].invoke()
    root.update()
    assert window.mode.get() == "manual" and window.page == "game_files"
    assert "automatic" not in window.step_rows and "movies" in window.step_rows
    window.back_button.invoke()
    assert window.page == "mode"


def test_automatic_with_a_folder_that_is_not_mcc(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.mode.set("automatic")
    window.mcc_root.set(str(tmp_path))
    window.go("automatic")
    root.update()
    assert any(text.startswith("⚠ No Halo: The Master Chief Collection") for text in texts(window))
    # MCC without Anniversary's maps
    broken = tmp_path / "broken"
    files = fakes.make_mcc(str(broken))
    os.remove(str(broken / "halo1" / "maps" / "bitmaps.map"))
    assert files
    window.mcc_root.set(str(broken))
    window.show("automatic")
    root.update()
    assert any(text.startswith("⚠ This MCC lacks") for text in texts(window))
    window.start(window.do_mcc)
    finish_errors = []
    from tkinter import messagebox
    original = messagebox.showerror
    messagebox.showerror = lambda title, text, **options: finish_errors.append(text)
    try:
        finish(window, root)
    finally:
        messagebox.showerror = original
    assert finish_errors and "lacks" in finish_errors[0]


def test_manual_paths_end_to_end_to_the_pc_build(root, tmp_path, monkeypatch):
    """Manual on PC: the disc image and the Custom Edition folder chosen by
    hand, then copied into the PC build."""
    from tkinter import messagebox
    errors = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: errors.append(text))
    monkeypatch.setattr(messagebox, "askyesno", lambda *arguments, **options: pytest.fail("asked"))
    image = tmp_path / "halo.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree())
    ce = tmp_path / "ce" / "maps"
    ce.mkdir(parents=True)
    for name, kind in hcv.PC_RESOURCE_MAPS:
        (ce / name).write_bytes(fakes.fake_resource_map(kind))
    release = tmp_path / "release"
    data = pc_build(release)
    out = tmp_path / "out"
    window = hcv.InstallerWindow(root, str(out))
    radios(window)["PC"].invoke()
    window.next_button.invoke()
    assert window.page == "mode" and window.mode.get() == "manual"
    root.update()
    window.next_button.invoke()
    assert window.page == "game_files"
    window.xbox_source.set(str(image))
    window.start(window.do_game_files)
    finish(window, root)
    window.next_button.invoke()
    assert window.page == "pc_files"  # no movies on PC
    window.pc_mode.set("folder")
    window.pc_folder.set(str(ce))
    window.start(window.do_pc_files)
    finish(window, root)
    window.next_button.invoke()
    assert window.page == "copy_pc"
    root.update()
    assert any(text.startswith("To copy: ") for text in texts(window))
    # no build chosen, or a wrong one: told on the page
    window.pc_build.set("")
    window.start(window.do_copy_pc)
    assert "Choose your PC build" in window.notice_text() and not window.busy()
    window.pc_build.set(str(tmp_path))
    window.start(window.do_copy_pc)
    assert "No PC build" in window.notice_text() and not window.busy()
    window.pc_build.set(str(release))
    window.show("copy_pc")
    root.update()
    window.start(window.do_copy_pc)
    finish(window, root)
    assert not errors
    assert (data / "default.xbe").exists() and (data / "maps" / "a10.map").exists()
    assert (data / "maps" / "loc.map").exists()
    window.next_button.invoke()
    assert window.page == "done"
    rows = {what: (done, how) for done, what, how in window.done_summary()}
    assert rows["Copied to the PC build"][0] and "copied" in rows["Copied to the PC build"][1]
    assert "Movies" not in rows and "The VPK" not in rows and "Copied to the Vita" not in rows
    assert "Next, on the PC" in texts(window)


def test_manual_on_the_vita_keeps_its_pages(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert list(window.step_rows)[2:] == ["game_files", "movies", "pc_files", "copy_vita", "vpk", "done"]
    window.show("movies")
    root.update()
    assert any(text == "ffmpeg" for text in texts(window))
    window.show("done")
    root.update()
    assert "Next, on the Vita" in texts(window)


def test_pc_files_are_optional_on_pc(root, tmp_path):
    out = tmp_path / "out"
    stage(str(out), xbox=True)
    window = hcv.InstallerWindow(root, str(out))
    window.platform.set("pc")
    window.show("copy_pc")
    root.update()
    assert any(text.startswith("Optional on PC") for text in texts(window))
    assert not any(text.startswith("⚠ Required") for text in texts(window))
    window.show("done")
    rows = {what: (done, how) for done, what, how in window.done_summary()}
    assert rows["Halo PC files"] == (False, "not there (optional on PC)")


def test_a_look_change_keeps_the_flow(root, tmp_path, monkeypatch):
    """The window built again (as the look check box does) keeps the choices."""
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.platform.set("pc")
    window.go("mode")
    window.set_look("plain")
    root.update()
    assert window.page == "mode" and list(window.step_rows) == list(hcv.install_flow("pc", "manual"))
    window.mode.set("automatic")
    root.update()
    assert "automatic" in window.step_rows


def test_a_second_game_shows_its_page(root, tmp_path, monkeypatch):
    """What 1.2.0 needs: an entry in GAMES, and the page is there, first."""
    monkeypatch.setattr(hcv, "GAMES", hcv.GAMES + (hcv.Choice("halo-2", "Halo 2"),))
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    root.update()
    assert window.page == "game" and list(window.step_rows)[0] == "game"
    assert list(radios(window)) == ["Halo: Combat Evolved", "Halo 2"]
    window.next_button.invoke()
    assert window.page == "platform"
    assert window.step_caption.cget("text") == "STEP 2 OF %d" % (len(window.flow()) - 1)
