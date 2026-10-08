# SPDX-License-Identifier: GPL-3.0-only
"""The window: every page drawn, a step run through it (the Xbox files from
a generated image), the fields read on the window's thread. Skipped where
there is no display or no tkinter."""


import pytest

import fakes
import halo_ce_vita_installer as hcv

tkinter = pytest.importorskip("tkinter")


@pytest.fixture
def root():
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
    # a step without its file: told, nothing started
    window.show(3)
    window.pc_mode.set("installer")
    window.installer.set("")
    window.start(window.do_pc_files)
    assert errors and "Choose the installer" in errors[-1]
    # the address field
    window.host.set("ftp://192.168.1.20:1337/")
    assert window._host_port() == ("192.168.1.20", 1337)
    window.host.set("rm -rf /")
    with pytest.raises(hcv.InstallerError):
        window._host_port()
