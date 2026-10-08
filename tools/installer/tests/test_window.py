# SPDX-License-Identifier: GPL-3.0-only
"""The window: every page drawn, a step run through it (the Xbox files from
a generated image), the fields read on the window's thread, the sidebar's
step states, the details, the Done page, the classic look by default, the
Modern look (light and dark) and its setting kept, HCV_THEME, the pictures
and the window without them. Skipped where there is no display or no
tkinter (the icon's tests need neither)."""

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
    window.destroy()


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
        for name, _ in hcv.PC_RESOURCE_MAPS:
            with open(os.path.join(maps, name), "wb") as file:
                file.write(bytes(64))
    if movies:
        os.makedirs(os.path.join(folder, "movies"), exist_ok=True)
        for index in range(movies):
            with open(os.path.join(folder, "movies", "movie%d.mp4" % index), "wb") as file:
                file.write(bytes(64))


def test_pages_and_a_step(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    errors = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: errors.append(text))
    out = tmp_path / "out"
    window = hcv.InstallerWindow(root, str(out))
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        assert window.body.winfo_children()
        assert window.title_label.cget("text") == window.PAGES[page][2]
    image = tmp_path / "halo.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree())
    window.show(1)
    window.xbox_source.set(str(image))
    window.start(window.do_xbox)
    assert pump(root, lambda: not window.worker.is_alive() and window.messages.empty())
    pump(root, lambda: False, 0.3)
    assert not errors
    assert (out / "maps" / "a10.map").exists() and (out / "default.xbe").exists()
    assert window.movie_source.get() == str(image)
    # done: the sidebar's tick, the step's line, Next back as the main button
    assert window.step_state(1) == "current" and 1 in window.done_steps
    assert "Xbox files ready" in window.notice_text()
    assert window.next_button.cget("text") == "Next"
    # a step without its file: told on the page, nothing started, no dialog
    window.show(3)
    window.pc_mode.set("installer")
    window.installer.set("")
    window.start(window.do_pc_files)
    assert "Choose the installer" in window.notice_text()
    assert not window.busy() and not errors
    # the address field
    window.host.set("ftp://192.168.1.20:1337/")
    assert window._host_port() == ("192.168.1.20", 1337)
    window.host.set("rm -rf /")
    with pytest.raises(hcv.InstallerError):
        window._host_port()


def test_a_failed_step_is_shown(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    errors = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: errors.append(text))
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.show(1)
    window.xbox_source.set(str(tmp_path / "missing.iso"))
    window.start(window.do_xbox)
    assert pump(root, lambda: not window.worker.is_alive() and window.messages.empty())
    pump(root, lambda: False, 0.3)
    assert errors and 1 not in window.done_steps
    assert window.notice_text().startswith("⚠")
    assert window.details_shown  # the log opened to show what happened


def test_sidebar_states(root, tmp_path):
    out = tmp_path / "out"
    window = hcv.InstallerWindow(root, str(out))
    assert window.step_state(0) == "current" and window.step_state(1) == "todo"
    assert window.next_button.cget("text") == "Get started"
    window.go(1)
    assert window.next_button.cget("text") == "Skip this step"
    window.go(2)  # moving on past a step not done: skipped
    assert window.step_state(1) == "skipped"
    assert window.step_rows[1][2].cget("text") == "Skipped"
    window.go(1)  # going back skips nothing
    assert window.step_state(2) == "todo"
    # files already in the output folder count as done (an earlier run)
    stage(str(out), xbox=True, pc=True)
    window.show(4)
    assert window.step_state(1) == "done" and window.step_state(3) == "done"
    assert window.step_state(2) == "todo"
    assert window.step_rows[3][2].cget("text") == "Done"
    # the sidebar moves too
    window.step_rows[5][1].event_generate("<Button-1>")
    root.update()
    assert window.page == 5 and window.step_state(4) == "skipped"


def test_details_collapsed_by_default(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    root.update()
    assert not window.details_shown and not window.details.winfo_ismapped()
    window.append_log("hello")
    window.toggle_details()
    root.update()
    assert window.details.winfo_ismapped() and window.details_button.cget("text") == "Hide details"
    assert "hello" in window.log_text.get("1.0", "end")
    window.toggle_details()
    root.update()
    assert not window.details.winfo_ismapped()


def test_done_page_summary(root, tmp_path):
    out = tmp_path / "out"
    stage(str(out), xbox=True, pc=False, movies=3)
    window = hcv.InstallerWindow(root, str(out))
    window.results[4] = "On the Vita: 17 copied, 2 were already there"
    window.done_steps.add(4)
    window.show(6)
    rows = {what: (done, how) for done, what, how in window.done_summary()}
    assert rows["Xbox game files"] == (True, "default.xbe and %d maps" % len(hcv.XBOX_REQUIRED_MAPS))
    assert rows["Movies"] == (True, "3 movies")
    assert rows["Halo PC files"][0] is False
    assert rows["Copied to the Vita"] == (True, "17 copied, 2 were already there")
    assert rows["The VPK"] == (False, "not copied in this session")
    texts = [widget.cget("text") for widget in all_widgets(window.body) if widget.winfo_class() == "TLabel"]
    assert any("Next, on the Vita" == text for text in texts)
    assert window.next_button.cget("text") == "Close"


def all_widgets(widget):
    found = []
    for child in widget.winfo_children():
        found.append(child)
        found.extend(all_widgets(child))
    return found


def test_light_and_dark(root, tmp_path):
    pytest.importorskip("sv_ttk")
    from tkinter import ttk
    window = hcv.InstallerWindow(root, str(tmp_path / "out"), theme="dark")
    style = ttk.Style(root)
    assert style.theme_use() == "sun-valley-dark" and window.dark.get()
    root.update()
    # sv-ttk's palette does not paint over the styles' colours
    assert str(window.step_caption.cget("background")) == ""
    assert style.lookup("Sidebar.TLabel", "background") == hcv.PALETTES["dark"]["sidebar"]
    window.dark.set(False)
    window.toggle_theme()
    root.update()
    assert style.theme_use() == "sun-valley-light" and window.theme == "light"
    assert not window.follow_system
    assert str(window.step_rows[1][1].cget("background")) == ""
    assert window.log_text.cget("background") == hcv.PALETTES["light"]["log"]
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()


def test_classic_is_the_default(root, tmp_path, own_settings):
    from tkinter import ttk
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    root.update()
    assert window.theme == "classic" and window.classic and not window.modern.get()
    assert ttk.Style(root).theme_use() == "alt"  # Tk's Windows 95/98 look
    assert ttk.Style(root).lookup("Header.TFrame", "background") == hcv.PALETTES["classic"]["header"]
    assert str(window.next_button.cget("default")) == "active"  # the default button's ring
    assert not own_settings.exists()  # nothing written until the look is changed
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        assert window.body.winfo_children()
        assert window.title_label.cget("text") == window.PAGES[page][2]


def test_modern_look_is_remembered(root, tmp_path, own_settings):
    pytest.importorskip("sv_ttk")
    import json
    from tkinter import ttk
    out = str(tmp_path / "out")
    stage(out, xbox=True)
    window = hcv.InstallerWindow(root, out)
    window.show(2)
    window.movie_source.set("/some/halo.iso")
    window.append_log("a line of the log")
    window.modern.set(True)
    window.toggle_look()
    root.update()
    assert window.modern_look and ttk.Style(root).theme_use().startswith("sun-valley-")
    assert json.loads(own_settings.read_text())["look"] == "modern"
    # drawn again: same page, fields, log and sidebar states
    assert window.page == 2 and window.movie_source.get() == "/some/halo.iso"
    assert "a line of the log" in window.log_text.get("1.0", "end")
    assert window.step_state(1) == "done" and window.step_rows[1][2].cget("text") == "Done"
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
    # Dark mode is kept too
    window.dark.set(True)
    window.toggle_theme()
    assert ttk.Style(root).theme_use() == "sun-valley-dark"
    assert json.loads(own_settings.read_text()) == {"look": "modern", "dark": True}
    # the next start: the Modern look, dark
    window = hcv.InstallerWindow(root, out)
    assert window.theme == "dark" and window.modern.get() and not window.follow_system
    # and back to classic
    window.modern.set(False)
    window.toggle_look()
    root.update()
    assert window.theme == "classic" and ttk.Style(root).theme_use() == "alt"
    assert json.loads(own_settings.read_text())["look"] == "classic"
    assert hcv.InstallerWindow(root, out).theme == "classic"


def test_the_look_is_not_changed_while_a_step_runs(root, tmp_path, own_settings):
    pytest.importorskip("sv_ttk")
    import threading
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    release = threading.Event()
    window.start(lambda: (lambda: release.wait(10) and None))
    try:
        assert window.busy()
        window.modern.set(True)
        window.toggle_look()
        assert window.theme == "classic" and not window.modern.get() and not own_settings.exists()
    finally:
        release.set()
    assert pump(root, lambda: not window.busy() and window.messages.empty())


def test_hcv_theme_picks_the_look(root, tmp_path, own_settings, monkeypatch):
    pytest.importorskip("sv_ttk")
    from tkinter import ttk
    hcv.save_settings({"look": "modern", "dark": False})
    monkeypatch.setenv("HCV_THEME", "classic")
    assert hcv.InstallerWindow(root, str(tmp_path / "out")).theme == "classic"
    monkeypatch.setenv("HCV_THEME", "dark")
    assert hcv.InstallerWindow(root, str(tmp_path / "out")).theme == "dark"
    monkeypatch.setenv("HCV_THEME", "plain")
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert window.theme == "plain" and ttk.Style(root).theme_use() in ("vista", "aqua", "clam")
    monkeypatch.setenv("HCV_THEME", "something else")  # the saved look
    assert hcv.InstallerWindow(root, str(tmp_path / "out")).theme == "light"


def test_damaged_or_unwritable_settings(root, tmp_path, own_settings, monkeypatch):
    own_settings.parent.mkdir(parents=True)
    own_settings.write_text("{not json")
    assert hcv.load_settings() == {}
    own_settings.write_text("[1, 2]")
    assert hcv.load_settings() == {}
    assert hcv.InstallerWindow(root, str(tmp_path / "out")).theme == "classic"
    blocked = tmp_path / "a file"
    blocked.write_text("")
    monkeypatch.setattr(hcv, "settings_path", lambda: str(blocked / "settings.json"))
    assert hcv.save_settings({"look": "modern"}) is False
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
    assert hcv.save_settings({"look": "modern"})
    assert hcv.load_settings() == {"look": "modern"}


def test_pictures(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    zoom = max(1, int(hcv.PICTURE_ZOOM * window.scale + 1e-6))
    names = set()
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        image = window.picture(page)
        assert image is not None and (image.width(), image.height()) == (76 * zoom, 43 * zoom)
        assert window.picture_label.winfo_ismapped()
        assert str(image) in str(window.picture_label.cget("image"))
        assert window.picture_caption.cget("text") == hcv.PAGE_PICTURES[page][1]
        names.add(hcv.PAGE_PICTURES[page][0])
    assert len(names) == len(window.STEPS)  # a picture of its own for each page
    # small: they go inside the Windows program
    assert sum(os.path.getsize(hcv.picture_path(name)) for name in names) < 64 * 1024


def test_the_window_works_without_its_pictures(root, tmp_path, monkeypatch):
    from tkinter import messagebox
    errors = []
    monkeypatch.setattr(messagebox, "showerror", lambda title, text, **options: errors.append(text))
    monkeypatch.setattr(hcv, "picture_path", lambda name: str(tmp_path / "missing" / name))
    damaged = tmp_path / "damaged.png"
    damaged.write_bytes(b"\x89PNG\r\n\x1a\nnot really")
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        assert window.picture(page) is None and not window.picture_label.winfo_ismapped()
        assert window.body.winfo_children()
    monkeypatch.setattr(hcv, "picture_path", lambda name: str(damaged))
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    root.update()
    assert window.picture(0) is None and not window.picture_label.winfo_ismapped()
    assert not errors


def test_without_the_modern_theme(root, tmp_path, own_settings, monkeypatch):
    monkeypatch.setitem(sys.modules, "sv_ttk", None)  # as if not installed
    assert hcv._sv_ttk_module() is None
    hcv.save_settings({"look": "modern"})
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert window.sv_ttk is None and window.theme == "classic"
    texts = [widget.cget("text") for widget in all_widgets(window.sidebar)
             if widget.winfo_class() == "TCheckbutton"]
    assert "Modern look" not in texts
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        assert window.body.winfo_children()
    monkeypatch.setenv("HCV_THEME", "dark")
    assert hcv.InstallerWindow(root, str(tmp_path / "out")).theme == "classic"
    window = hcv.InstallerWindow(root, str(tmp_path / "out"), theme="plain")
    assert window.sv_ttk is None and window.theme == "plain"


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


def test_classic_indicators_png():
    size = 20
    width, height, raw = read_png(hcv.indicator_png("check", True, False, size))
    assert (width, height) == (size, size)
    pixel = lambda x, y: raw[y * (size * 4 + 1) + 1 + x * 4:][:4]
    assert pixel(0, 0) == b"\x80\x80\x80\xff"  # the bevel's dark grey corner
    assert pixel(size - 1, size // 2) == b"\xff\xff\xff\xff"  # its white side
    assert pixel(int(size * 0.42), int(size * 0.66)) == b"\0\0\0\xff"  # the tick
    assert pixel(size // 2, 4) == b"\xff\xff\xff\xff"  # the white box
    _, _, raw = read_png(hcv.indicator_png("radio", True, True, size))
    assert pixel(0, 0)[3] == 0  # outside the circle: transparent
    assert pixel(size // 2, size // 2)[:3] == b"\x80\x80\x80"  # a disabled one's dot


def test_classic_indicators_at_a_large_scale(root, tmp_path):
    from tkinter import ttk
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    style = ttk.Style(root)
    if window.scale < 1.2:  # (Tk's own at 100%)
        assert "Hcv.Checkbutton.indicator" not in style.element_names()
        window.scale = 1.5
        window._classic_indicators(style)
    assert "Hcv.Radiobutton.indicator" in style.element_names()
    assert "Hcv.Checkbutton.indicator" in str(style.layout("TCheckbutton"))
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()


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


def test_a_short_window_scrolls(root, tmp_path):
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    window.show(3)
    root.minsize(1, 1)
    tall, short = "%dx%d" % (window.px(1000), window.px(900)), "%dx%d" % (window.px(900), window.px(420))
    root.geometry(tall)
    pump(root, lambda: not window.scrollbar.winfo_ismapped(), 3)
    assert not window.scrollbar.winfo_ismapped()
    root.geometry(short)
    pump(root, lambda: window.scrollbar.winfo_ismapped(), 3)
    assert window.scrollbar.winfo_ismapped()
    root.geometry(tall)
    pump(root, lambda: not window.scrollbar.winfo_ismapped(), 3)
    assert not window.scrollbar.winfo_ismapped()
