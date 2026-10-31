# SPDX-License-Identifier: GPL-3.0-only
"""The window's parts that are not the wizard's flow (test_flow.py has that):
ttk's native theme and nothing custom, the generated pictures, the icon, the
settings file. Skipped where there is no display or no tkinter (the icon's and
pictures' tests need neither)."""

import os
import struct
import sys
import zlib

import pytest

import fakes
import halo_ce_vita_installer as hcv


def tk_module():
    return pytest.importorskip("tkinter", exc_type=ImportError)


@pytest.fixture
def root():
    tkinter = tk_module()
    try:
        window = tkinter.Tk()
    except tkinter.TclError as error:
        pytest.skip("no display: %s" % error)
    yield window
    try:
        window.destroy()
    except tkinter.TclError:  # (Finish closed it)
        pass


def pump(root, until, seconds=20.0):
    import time
    deadline = time.time() + seconds
    while time.time() < deadline:
        root.update()
        if until():
            return True
        time.sleep(0.02)
    return False


def stage(folder, xbox=True, pc=False, movies=0):
    maps = os.path.join(folder, "maps")
    os.makedirs(maps, exist_ok=True)
    if xbox:
        with open(os.path.join(folder, "default.xbe"), "wb") as file:
            file.write(b"XBEH" + bytes(64))
        for name in hcv.XBOX_REQUIRED_MAPS:
            with open(os.path.join(maps, name), "wb") as file:
                file.write(bytes(64))
    if pc:
        for name, kind in hcv.PC_RESOURCE_MAPS:
            with open(os.path.join(maps, name), "wb") as file:
                file.write(fakes.fake_resource_map(kind))
    if movies:
        os.makedirs(os.path.join(folder, "movies"), exist_ok=True)
        for index in range(movies):
            with open(os.path.join(folder, "movies", "movie%d.mp4" % index), "wb") as file:
                file.write(bytes(64))


def all_widgets(widget):
    found = []
    for child in widget.winfo_children():
        found.append(child)
        found.extend(all_widgets(child))
    return found


def test_the_native_theme_and_nothing_of_the_old_looks(root, tmp_path):
    from tkinter import ttk
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert window.theme == ttk.Style(root).theme_use() and window.theme in ("vista", "aqua", "clam")
    for name in ("PALETTES", "THEMES", "PAGE_PICTURES", "indicator_png", "_sv_ttk_module", "_set_palette"):
        assert not hasattr(hcv, name)


def test_the_window_has_a_fixed_size_and_every_page_fits(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert root.resizable() == (False, False)
    window.host.set("192.168.1.20")
    window.existing = str(tmp_path)  # (so the maintenance page is one of them)
    for platform in ("vita", "pc"):
        window.platform.set(platform)
        for mode in ("automatic", "manual"):
            window.mode.set(mode)
            for page in window.PAGES:
                window.show(page)
                root.update()
                assert window.shell.winfo_reqheight() <= window.shell.winfo_height(), page
                assert window.shell.winfo_reqwidth() <= window.shell.winfo_width(), page
                bottom = window.shell.winfo_rooty() + window.shell.winfo_height()
                right = window.shell.winfo_rootx() + window.shell.winfo_width()
                for widget in all_widgets(window.body):
                    if widget.winfo_ismapped():
                        assert widget.winfo_rooty() + widget.winfo_height() <= bottom, page
                        assert widget.winfo_rootx() + widget.winfo_width() <= right, page


def test_damaged_or_unwritable_settings(root, tmp_path, own_settings, monkeypatch):
    own_settings.parent.mkdir(parents=True)
    own_settings.write_text("{not json")
    assert hcv.load_settings() == {}
    own_settings.write_text("[1, 2]")
    assert hcv.load_settings() == {}
    hcv.InstallerWindow(root, str(tmp_path / "out"))
    blocked = tmp_path / "a file"
    blocked.write_text("")
    monkeypatch.setattr(hcv, "settings_path", lambda: str(blocked / "settings.json"))
    assert hcv.save_settings({"install_folder": "/somewhere"}) is False
    assert hcv.load_settings() == {}


def test_settings_path_is_in_the_users_app_data(monkeypatch, tmp_path, real_settings_path):
    monkeypatch.setattr(hcv, "settings_path", real_settings_path)
    if sys.platform == "win32":
        monkeypatch.setenv("APPDATA", str(tmp_path))
        assert hcv.settings_path() == str(tmp_path / "HaloCEVita" / hcv.SETTINGS_FILE)
    elif sys.platform == "darwin":
        assert hcv.settings_path().endswith(os.path.join("Application Support", "HaloCEVita", hcv.SETTINGS_FILE))
        return
    else:
        monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
        assert hcv.settings_path() == str(tmp_path / "halo-ce-vita" / hcv.SETTINGS_FILE)
    assert hcv.save_settings({"install_folder": "/somewhere"})
    assert hcv.load_settings() == {"install_folder": "/somewhere"}


def read_png(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    width, height = struct.unpack(">II", data[16:24])
    position, idat = 8, b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        if kind == b"IDAT":
            idat += data[position + 8:position + 8 + length]
        position += 12 + length
    raw = zlib.decompress(idat)
    assert len(raw) == height * (width * 4 + 1)
    return width, height, raw


def test_icon_png():
    width, height, raw = read_png(hcv.icon_png(32))
    assert (width, height) == (32, 32)
    pixel = lambda x, y: raw[y * (32 * 4 + 1) + 1 + x * 4:][:4]
    assert pixel(0, 0)[3] == 0  # the corner: transparent
    assert pixel(16, 8) == b"\xff\xff\xff\xff"  # the arrow: white
    red, green, blue, alpha = pixel(4, 16)
    assert alpha == 255 and blue > red  # the square: blue


def test_sidebar_png():
    width, height, raw = read_png(hcv.sidebar_png(164, 420))
    assert (width, height) == (164, 420)
    assert all(raw[y * (164 * 4 + 1) + 4] == 255 for y in range(0, 420, 50))  # opaque
    assert hcv.sidebar_png(164, 420) == hcv.sidebar_png(164, 420)  # always the same picture


def test_icon_file(tmp_path):
    path = tmp_path / "icon.ico"
    hcv.write_icon(str(path), sizes=(16, 32, 256))
    data = path.read_bytes()
    reserved, kind, count = struct.unpack("<HHH", data[:6])
    assert (reserved, kind, count) == (0, 1, 3)
    for index, size in enumerate((16, 32, 256)):
        width, height, _, _, planes, bits, length, offset = struct.unpack(
            "<BBBBHHII", data[6 + 16 * index:22 + 16 * index])
        assert (width, height, planes, bits) == (size % 256, size % 256, 1, 32)
        assert read_png(data[offset:offset + length])[:2] == (size, size)
