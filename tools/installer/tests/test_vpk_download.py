# SPDX-License-Identifier: GPL-3.0-only
"""The Vita's VPK: Setup's signed download is the default, a VPK next to Setup
is never picked up by itself, and one the user chooses is sent after the
"not signature-checked" warning (and counts as checked only when it is, byte
for byte, the file the release signed)."""

import os
import sys
import zipfile

import pytest

import halo_ce_vita_installer as hcv
from test_flow import advance, complaints, fake_steam, to_game_files, wait_for_finish, wizard
from test_pc_download import LATEST, REPOSITORY, Release, key  # noqa: F401 (key: the fixture)
from test_window import root  # noqa: F401 (the fixture)

sys.path.insert(0, os.path.join(REPOSITORY, "tools", "release"))
import update_signing as common  # noqa: E402

VERSION = "1.1.3"
NAME = "halo-ce-vita-%s-vita.vpk" % VERSION
URL = hcv.RELEASE_DOWNLOADS + "/download/v%s/%s" % (VERSION, NAME)


def make_vpk(path, extra=b""):
    with zipfile.ZipFile(str(path), "w") as archive:
        archive.writestr("eboot.bin", b"x" + extra)
        archive.writestr("sce_sys/param.sfo", b"x")
    return path


@pytest.fixture
def release(tmp_path, key, monkeypatch):
    """A release signed by sign_update.py with the throwaway key, on the stand-in."""
    vpk = make_vpk(tmp_path / "built.vpk")
    size, sha512 = common.file_digest(vpk)
    text = common.manifest_text(VERSION, "HALOCEVTA", size, sha512).encode("ascii")
    out = tmp_path / "signed"
    out.mkdir()
    (out / NAME).write_bytes(vpk.read_bytes())
    (out / hcv.VITA_MANIFEST).write_bytes(text)
    (out / hcv.VITA_SIGNATURE).write_text(common.ed25519.sign(key[1], text).hex() + "\n")
    served = Release(monkeypatch)
    served.files[LATEST + hcv.VITA_MANIFEST] = (out / hcv.VITA_MANIFEST).read_bytes()
    served.files[LATEST + hcv.VITA_SIGNATURE] = (out / hcv.VITA_SIGNATURE).read_bytes()
    served.files[URL] = (out / NAME).read_bytes()
    served.signed = vpk
    return served


def download(tmp_path):
    folder = tmp_path / "download"
    folder.mkdir(exist_ok=True)
    return hcv.download_vpk(str(folder), log=lambda text: None), folder


def test_a_signed_vpk_is_downloaded(tmp_path, release):
    path, folder = download(tmp_path)
    assert os.path.basename(path) == NAME and sorted(os.listdir(str(folder))) == [NAME]
    assert open(path, "rb").read() == release.signed.read_bytes()


def test_the_signature_is_read_before_the_vpk_is_asked(tmp_path, release):
    release.files[LATEST + hcv.VITA_MANIFEST] += b"x"
    with pytest.raises(hcv.InstallerError, match="signature"):
        download(tmp_path)
    assert URL not in release.asked


def test_another_key_is_refused(tmp_path, release, monkeypatch):
    monkeypatch.setattr(hcv, "RELEASE_PUBLIC_KEY", hcv.ed25519.public_key(os.urandom(32)).hex())
    with pytest.raises(hcv.InstallerError, match="signature"):
        download(tmp_path)


def test_a_changed_vpk_is_refused_and_thrown_away(tmp_path, release):
    release.files[URL] = release.files[URL][:-1] + b"!"
    with pytest.raises(hcv.InstallerError, match="not the file"):
        download(tmp_path)
    assert os.listdir(str(tmp_path / "download")) == []


def test_a_longer_vpk_stops_the_download(tmp_path, release):
    release.files[URL] += b"x"
    with pytest.raises(hcv.InstallerError, match="longer"):
        download(tmp_path)
    assert os.listdir(str(tmp_path / "download")) == []


def test_a_release_without_a_manifest_says_so(tmp_path, release):
    del release.files[LATEST + hcv.VITA_MANIFEST]
    with pytest.raises(hcv.InstallerError):
        download(tmp_path)


def test_a_step_down_is_refused(tmp_path, release, monkeypatch):
    monkeypatch.setattr(hcv, "VERSION", "1.2.0")
    with pytest.raises(hcv.InstallerError, match="older"):
        download(tmp_path)


def test_the_file_may_be_the_old_name_only_as_halo_vpk(release):
    text = release.files[LATEST + hcv.VITA_MANIFEST].decode()
    other = text.replace("file " + NAME, "file evil.vpk").encode()
    with pytest.raises(hcv.InstallerError, match="signature"):  # (unsigned change)
        hcv.verify_vita_manifest(other, release.files[LATEST + hcv.VITA_SIGNATURE])


def test_a_chosen_vpk_matches_only_the_signed_file(tmp_path, release):
    logs = []
    assert hcv.match_signed_vpk(str(release.signed), logs.append) is True
    other = make_vpk(tmp_path / "other.vpk", b"y")
    assert hcv.match_signed_vpk(str(other), logs.append) is False
    assert "not signature-checked" in logs[-1]
    del release.files[LATEST + hcv.VITA_MANIFEST]
    assert hcv.match_signed_vpk(str(release.signed), logs.append) is False  # (the network or the list gone)


# -- the command line

@pytest.fixture
def sent(monkeypatch):
    sent = []
    monkeypatch.setattr(hcv, "upload_vpk", lambda host, vpk, *a, **k: sent.append(open(vpk, "rb").read()) or
                        os.path.basename(vpk))
    return sent


def test_the_command_line_downloads_by_default_and_ignores_a_vpk_beside_it(tmp_path, release, monkeypatch, sent):
    here = tmp_path / "Downloads"
    here.mkdir()
    fake = make_vpk(here / "halo-ce-vita-99.0.0-vita.vpk", b"evil")
    monkeypatch.setattr(hcv, "_tool_folder", lambda: str(here))
    monkeypatch.chdir(str(here))
    assert hcv.main(["--out", str(tmp_path / "out"), "vpk", "192.168.1.20"]) in (0, None)
    assert sent == [release.signed.read_bytes()] and fake.exists()


def test_the_command_line_warns_about_a_vpk_given(tmp_path, release, sent, capsys):
    other = make_vpk(tmp_path / "mine.vpk", b"y")
    assert hcv.main(["--out", str(tmp_path / "out"), "vpk", "192.168.1.20", str(other)]) in (0, None)
    text = capsys.readouterr().out
    assert "mine.vpk is not signature-checked" in text and sent == [other.read_bytes()]


def test_the_command_line_refuses_a_changed_download(tmp_path, release, sent, capsys):
    release.files[URL] = release.files[URL][:-1] + b"!"
    assert hcv.main(["--out", str(tmp_path / "out"), "vpk", "192.168.1.20"]) not in (0, None)
    assert sent == []


def test_nothing_is_found_by_itself():
    assert not hasattr(hcv, "find_local_vpks")


# -- the window

def to_destination(window, root, tmp_path, monkeypatch):
    fake_steam(tmp_path, monkeypatch)
    to_game_files(window, root, "vita")
    advance(window, root, "destination")
    window.host.set("192.168.1.20")


def test_a_vpk_beside_setup_is_not_chosen_and_the_download_is_on(root, tmp_path, monkeypatch):
    here = tmp_path / "tool"
    here.mkdir()
    make_vpk(here / "halo-ce-vita-99.0.0-vita.vpk")
    monkeypatch.setattr(hcv, "_tool_folder", lambda: str(here))
    monkeypatch.chdir(str(here))
    window = wizard(root, tmp_path)
    assert window.vpk.get() == "" and window.download_vpk_box.get() is True


def test_the_window_downloads_the_signed_vpk(root, tmp_path, monkeypatch, release):
    errors = complaints(monkeypatch)
    uploaded = []
    monkeypatch.setattr(hcv, "upload_files", lambda host, items, *a, **k: uploaded.append(
        [(item.name, open(item.source, "rb").read() if item.name.endswith(".vpk") else None) for item in items]) or {})
    window = wizard(root, tmp_path)
    to_destination(window, root, tmp_path, monkeypatch)
    window.vpk.set(str(make_vpk(tmp_path / "typed.vpk", b"z")))  # (ignored while the download is on)
    advance(window, root, "ready")
    assert "downloaded from the project's GitHub releases" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert uploaded[-1] == [(NAME, release.signed.read_bytes())]
    assert URL in release.asked


def test_a_chosen_vpk_asks_first_and_no_stays(root, tmp_path, monkeypatch, release):
    from tkinter import messagebox
    window = wizard(root, tmp_path)
    to_destination(window, root, tmp_path, monkeypatch)
    window.download_vpk_box.set(False)
    window.vpk.set(str(make_vpk(tmp_path / "mine.vpk", b"y")))
    asked = {}
    monkeypatch.setattr(messagebox, "askyesno", lambda title, text, **given: asked.update(given, text=text) or False)
    window.next_button.invoke()
    root.update()
    assert window.page == "destination"
    assert "mine.vpk is not signature-checked" in asked["text"] and asked["default"] == "no"
    monkeypatch.setattr(messagebox, "askyesno", lambda *a, **k: True)
    window.next_button.invoke()
    root.update()
    assert window.page == "ready"
    assert "mine.vpk (not signature-checked)" in window.summary_box.get("1.0", "end")


def test_no_download_and_no_file_sends_nothing(root, tmp_path, monkeypatch, release):
    errors = complaints(monkeypatch)
    uploaded = []
    monkeypatch.setattr(hcv, "upload_files", lambda host, items, *a, **k: uploaded.append([i.name for i in items]) or {})
    window = wizard(root, tmp_path)
    to_destination(window, root, tmp_path, monkeypatch)
    window.download_vpk_box.set(False)
    advance(window, root, "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert all(not name.endswith(".vpk") for names in uploaded for name in names) and URL not in release.asked
