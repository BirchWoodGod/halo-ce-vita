# SPDX-License-Identifier: GPL-3.0-only
"""Steps 4 and 5 against a local VitaShell-like FTP server (fakes.FakeVita:
pyftpdlib on 127.0.0.1, VitaShell's LIST lines, no NLST, no SIZE)."""

import os
import zipfile

import pytest

pytest.importorskip("pyftpdlib")

import fakes  # noqa: E402
import halo_ce_vita_installer as hcv  # noqa: E402


def staging_with_files(folder, big=0):
    (folder / "maps").mkdir(parents=True)
    (folder / "movies").mkdir()
    (folder / "default.xbe").write_bytes(b"XBEH" + b"\0" * 1000)
    (folder / "maps" / "a10.map").write_bytes(os.urandom(300000))
    (folder / "maps" / "[h3]_sandtrap.map").write_bytes(b"custom" * 5000)
    (folder / "maps" / "loc.map").write_bytes(fakes.fake_resource_map(3))
    (folder / "maps" / "a30.map.part").write_bytes(b"half")       # never copied
    (folder / "movies" / "intro.mp4").write_bytes(os.urandom(big or 50000))
    return hcv.staged_uploads(str(folder))


def test_parse_list_line():
    entry = hcv.parse_list_line("-rw-r--r-- 1 vita vita 123456 Oct  8 00:31 [h3]_sandtrap map.map")
    assert entry.name == "[h3]_sandtrap map.map" and entry.size == 123456 and not entry.directory
    assert hcv.parse_list_line("drwxr-xr-x 1 vita vita 0 Jan 01 2020 maps").directory
    assert hcv.parse_list_line("total 3") is None
    assert hcv.parse_list_line("garbage") is None


def test_vita_path():
    assert hcv.vita_path("ux0:data/haloce-vita") == "/ux0:/data/haloce-vita"
    assert hcv.vita_path("ux0:data/haloce-vita/maps") == "/ux0:/data/haloce-vita/maps"
    with pytest.raises(hcv.InstallerError):
        hcv.vita_path("ux0:data", "../x")


def test_staged_uploads_layout(tmp_path):
    items = staging_with_files(tmp_path / "out")
    places = sorted((item.folder, item.name) for item in items)
    assert places == [("ux0:data/haloce-vita", "default.xbe"),
                      ("ux0:data/haloce-vita/maps", "[h3]_sandtrap.map"),
                      ("ux0:data/haloce-vita/maps", "a10.map"),
                      ("ux0:data/haloce-vita/maps", "loc.map"),
                      ("ux0:data/haloce-vita/movies", "intro.mp4")]


def test_upload_then_skip(tmp_path):
    items = staging_with_files(tmp_path / "out")
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        log = []
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=log.append, timeout=5)
        assert result["copied"] == 5 and result["skipped"] == 0
        for item in items:
            relative = item.folder.split(":", 1)[1].split("/")
            with open(vita.path(*relative, item.name), "rb") as remote, open(item.local, "rb") as local:
                assert remote.read() == local.read()
        assert "NLST" not in vita.commands and "SIZE" not in vita.commands
        # a second run: everything already there
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=log.append, timeout=5)
        assert result == {"copied": 0, "skipped": 5, "retries": 0}
        # one file changed size on the Vita (an older copy): copied again
        with open(vita.path("data", "haloce-vita", "maps", "a10.map"), "wb") as file:
            file.write(b"old")
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=log.append, timeout=5)
        assert result["copied"] == 1 and result["skipped"] == 4
        # --replace: all again
        result = hcv.upload_files("127.0.0.1", items, vita.port, replace=True, log=log.append, timeout=5)
        assert result["copied"] == 5


def test_dropped_connection_resumes(tmp_path):
    items = staging_with_files(tmp_path / "out", big=4 * 1024 * 1024)
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        vita.drop["intro.mp4"] = 1024 * 1024
        log = []
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=log.append, retry_wait=0.1, timeout=5)
        assert vita.dropped == ["intro.mp4"]
        assert result["copied"] == 5 and result["retries"] >= 1
        assert any("trying again" in line for line in log)
        assert os.path.getsize(vita.path("data", "haloce-vita", "movies", "intro.mp4")) == 4 * 1024 * 1024


def test_stray_replies(tmp_path):
    items = staging_with_files(tmp_path / "out")
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        vita.double_226 = True
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=lambda message: None, timeout=5)
        assert result == {"copied": 5, "skipped": 0, "retries": 0}
        result = hcv.upload_files("127.0.0.1", items, vita.port, log=lambda message: None, timeout=5)
        assert result == {"copied": 0, "skipped": 5, "retries": 0}


def test_nothing_listening(tmp_path):
    items = staging_with_files(tmp_path / "out")
    import socket
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    with pytest.raises(hcv.FtpError, match="Cannot connect"):
        hcv.upload_files("127.0.0.1", items, port, attempts=2, retry_wait=0.01, timeout=2)


def test_cancel(tmp_path):
    import threading
    items = staging_with_files(tmp_path / "out")
    cancel = threading.Event()
    cancel.set()
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        with pytest.raises(hcv.Cancelled):
            hcv.upload_files("127.0.0.1", items, vita.port, cancel=cancel, timeout=5)


def test_vpk_upload(tmp_path):
    vpk = tmp_path / "halo.vpk"
    with zipfile.ZipFile(str(vpk), "w") as archive:
        archive.writestr("eboot.bin", b"\0" * 100)
        archive.writestr("sce_sys/param.sfo", b"\0PSF")
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        name = hcv.upload_vpk("127.0.0.1", str(vpk), vita.port, log=lambda message: None)
        assert name == "halo.vpk"
        assert open(vita.path("data", "halo.vpk"), "rb").read() == vpk.read_bytes()
    not_vpk = tmp_path / "x.vpk"
    not_vpk.write_bytes(b"nope")
    with pytest.raises(hcv.InstallerError, match="not a VPK"):
        hcv.upload_vpk("127.0.0.1", str(not_vpk), 1)


def test_command_line_upload(tmp_path):
    staging_with_files(tmp_path / "out")
    with fakes.FakeVita(str(tmp_path / "vita")) as vita:
        assert hcv.main(["--out", str(tmp_path / "out"), "upload", "127.0.0.1", "--port", str(vita.port)]) == 0
        assert os.path.isfile(vita.path("data", "haloce-vita", "maps", "[h3]_sandtrap.map"))
