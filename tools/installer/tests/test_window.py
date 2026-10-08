# SPDX-License-Identifier: GPL-3.0-only
"""The window: every page drawn, a step run through it (the Xbox files from
a generated image), the fields read on the window's thread, the sidebar's
step states, the details, the Done page, the light and dark themes and the
plain-Tk window without them. Skipped where there is no display or no
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


def test_plain_tk_without_the_theme(root, tmp_path, monkeypatch):
    monkeypatch.setitem(sys.modules, "sv_ttk", None)  # as if not installed
    assert hcv._sv_ttk_module() is None
    window = hcv.InstallerWindow(root, str(tmp_path / "out"))
    assert window.sv_ttk is None and window.theme == "plain"
    for page in range(len(window.STEPS)):
        window.show(page)
        root.update()
        assert window.body.winfo_children()
    monkeypatch.delitem(sys.modules, "sv_ttk")
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
    root.update()
    assert not window.scrollbar.winfo_ismapped()
    root.minsize(1, 1)
    root.geometry("900x520")
    pump(root, lambda: window.scrollbar.winfo_ismapped(), 3)
    assert window.scrollbar.winfo_ismapped()
    root.geometry("1000x900")
    pump(root, lambda: not window.scrollbar.winfo_ismapped(), 3)
    assert not window.scrollbar.winfo_ismapped()
