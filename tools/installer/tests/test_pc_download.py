# SPDX-License-Identifier: GPL-3.0-only
"""The PC build downloaded by Setup and checked as the game checks its updates:
a release signed by tools/release/sign_update.py with a throwaway key made
here (never the owner's), served by a stand-in for GitHub. Every refusal leaves
nothing installed, and Launch starts only a checked download."""

import hashlib
import io
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

import pytest

import halo_ce_vita_installer as hcv
from test_flow import advance, complaints, fake_steam, tar_release, to_game_files, wait_for_finish, wizard
from test_flow import RELEASE, checks, choose_package, package_next_to_the_tool, texts
from test_window import root  # noqa: F401 (the fixture)

REPOSITORY = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TOOL = os.path.join(REPOSITORY, "tools", "release", "sign_update.py")
VERSION = "1.1.3"
LATEST = hcv.RELEASE_DOWNLOADS + "/latest/download/"
TOP = "halo-ce-vita-%s-pc-linux" % VERSION
ARCHIVE = TOP + ".tar.gz"


@pytest.fixture
def key(tmp_path, monkeypatch):
    """A throwaway key: its private file in the test's folder, its public half
    what the tool trusts here."""
    seed = os.urandom(32)
    path = tmp_path / "throwaway.key"
    path.write_text("# a test's key\n%s\n" % seed.hex())
    path.chmod(0o600)
    monkeypatch.setattr(hcv, "RELEASE_PUBLIC_KEY", hcv.ed25519.public_key(seed).hex())
    return path, seed


class Answer(io.BytesIO):
    pass


class Release:
    """What GitHub serves: url -> bytes, a list of the urls asked."""

    def __init__(self, monkeypatch):
        self.files = {}
        self.asked = []
        monkeypatch.setattr(hcv, "open_download", self.open)

    def open(self, url):
        self.asked.append(url)
        hcv.check_download_url(url)
        if url not in self.files:
            raise urllib.error.HTTPError(url, 404, "Not Found", {}, None)
        return Answer(self.files[url])


def sign(tmp_path, key_path, archive, version=VERSION, option="--pc-linux"):
    out = tmp_path / "signed"
    subprocess.run([sys.executable, TOOL, "--key", str(key_path), option, str(archive), "--version", version,
                    "--out", str(out)], check=True, capture_output=True, text=True)
    return out


@pytest.fixture
def release(tmp_path, key, monkeypatch):
    """A signed release of VERSION, as the owner makes one, on the stand-in."""
    monkeypatch.setattr(sys, "platform", "linux")
    archive = tar_release(tmp_path / ARCHIVE, top=TOP)
    out = sign(tmp_path, key[0], archive)
    served = Release(monkeypatch)
    served.files[LATEST + hcv.PC_MANIFEST] = (out / hcv.PC_MANIFEST).read_bytes()
    served.files[LATEST + hcv.PC_SIGNATURE] = (out / hcv.PC_SIGNATURE).read_bytes()
    served.files[hcv.RELEASE_DOWNLOADS + "/download/v%s/%s" % (VERSION, ARCHIVE)] = (out / ARCHIVE).read_bytes()
    served.archive_url = hcv.RELEASE_DOWNLOADS + "/download/v%s/%s" % (VERSION, ARCHIVE)
    served.out = out
    return served


def fetch(tmp_path, installed=""):
    folder = tmp_path / "download"
    folder.mkdir(exist_ok=True)
    return hcv.download_pc_build(str(folder), log=lambda text: None, installed=installed), folder


# -- the check

def test_the_public_key_is_the_release_key():
    header = open(os.path.join(REPOSITORY, "port/vita/include/vita_update_key.h"), encoding="utf-8").read()
    assert re.search(r'#define HALO_RELEASE_PUBLIC_KEY "([0-9a-f]{64})"', header).group(1) == hcv.RELEASE_PUBLIC_KEY


def test_a_signed_release_is_downloaded(tmp_path, release):
    package, folder = fetch(tmp_path)
    assert package.verified and package.version == VERSION
    assert os.path.basename(package.path) == ARCHIVE
    assert sorted(os.listdir(str(folder))) == [ARCHIVE]


def test_the_signature_is_read_before_the_archive_is_asked(tmp_path, release):
    release.files[LATEST + hcv.PC_MANIFEST] += b"x"
    with pytest.raises(hcv.InstallerError, match="signature"):
        fetch(tmp_path)
    assert release.archive_url not in release.asked


@pytest.mark.parametrize("damage", [
    lambda text: text.replace(b"version ", b"version 9"),
    lambda text: text[:-2] + b"0\n",
    lambda text: text + b"\n",
    lambda text: b"",
])
def test_a_changed_manifest_is_refused(tmp_path, release, damage):
    release.files[LATEST + hcv.PC_MANIFEST] = damage(release.files[LATEST + hcv.PC_MANIFEST])
    with pytest.raises(hcv.InstallerError, match="signature"):
        fetch(tmp_path)


@pytest.mark.parametrize("signature", [b"", b"zz", b"00" * 64, b"\xff" * 129])
def test_a_bad_signature_file_is_refused(tmp_path, release, signature):
    release.files[LATEST + hcv.PC_SIGNATURE] = signature
    with pytest.raises(hcv.InstallerError):
        fetch(tmp_path)


def test_another_key_is_refused(tmp_path, release):
    text = release.files[LATEST + hcv.PC_MANIFEST]
    release.files[LATEST + hcv.PC_SIGNATURE] = hcv.ed25519.sign(os.urandom(32), text).hex().encode()
    with pytest.raises(hcv.InstallerError, match="signature"):
        fetch(tmp_path)


def signed(seed, text):
    return text, hcv.ed25519.sign(seed, text).hex().encode()


def row(file=ARCHIVE, size=100, system="linux"):
    return "%s %s %d %s\n" % (system, file, size, "a" * 128)


@pytest.mark.parametrize("text,word", [
    ("halo-ce-vita-update 1\nversion 1.1.3\n" + row(), "form"),  # (the Vita's magic)
    ("halo-ce-vita-update-pc 1\nversion 1.1.3-insider.2\n" + row(), "form"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n", "form"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n" + row() + row(), "twice"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n" + row(system="windows", file="x"), "no PC build"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n" + row(file="../evil.tar.gz"), "form"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n" + row(file="halo.sh"), "another file"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.3\n" + row(size=(1 << 30) + 1), "too large"),
    ("halo-ce-vita-update-pc 1\nversion 1.1.1\n" + row(file="halo-ce-vita-1.1.1-pc-linux.tar.gz"), "older"),
])
def test_a_signed_manifest_of_the_wrong_form_is_refused(key, text, word):
    with pytest.raises(hcv.InstallerError, match=word):
        hcv.verify_pc_manifest(*signed(key[1], text.encode()), "linux")


def test_the_vita_manifest_tool_reads_no_pc_manifest(tmp_path, release):
    sys.path.insert(0, os.path.dirname(TOOL))
    import update_signing as common

    text = release.files[LATEST + hcv.PC_MANIFEST].decode()
    assert common.parse_manifest(text) is None
    assert common.parse_pc_manifest(text)[0] == VERSION


def test_a_step_down_is_refused(tmp_path, release):
    with pytest.raises(hcv.InstallerError, match="older"):
        fetch(tmp_path, installed="1.1.4")
    assert release.archive_url not in release.asked
    fetch(tmp_path, installed=VERSION)  # (a repair: the same version)


def test_version_order():
    order = ["1.1.2-beta.1", "1.1.2-beta.2", "1.1.2-rc.1", "1.1.2", "1.1.10"]
    assert sorted(order, key=hcv.version_order) == order


def test_a_changed_archive_is_refused_and_thrown_away(tmp_path, release):
    data = bytearray(release.files[release.archive_url])
    data[len(data) // 2] ^= 1
    release.files[release.archive_url] = bytes(data)
    with pytest.raises(hcv.InstallerError, match="signed"):
        fetch(tmp_path)
    assert os.listdir(str(tmp_path / "download")) == []


def test_a_cut_archive_is_refused(tmp_path, release):
    release.files[release.archive_url] = release.files[release.archive_url][:-1]
    with pytest.raises(hcv.InstallerError, match="signed"):
        fetch(tmp_path)


def test_a_longer_archive_stops_the_download(tmp_path, release):
    release.files[release.archive_url] += b"x" * (2 << 20)
    with pytest.raises(hcv.InstallerError, match="longer"):
        fetch(tmp_path)


def test_a_release_without_the_manifest_says_so(tmp_path, release):
    del release.files[LATEST + hcv.PC_MANIFEST]
    with pytest.raises(hcv.InstallerError, match="no signed PC build"):
        fetch(tmp_path)


@pytest.mark.parametrize("url", [
    "http://github.com/x", "https://evil.example/x", "https://github.com.evil.example/x", "ftp://github.com/x",
    "file:///etc/passwd", "https://github.com:8443/x", "https://user@github.com/x", "https://[bad/x",
])
def test_only_https_to_github_is_asked(url):
    with pytest.raises(hcv.InstallerError):
        hcv.check_download_url(url)


def test_github_storage_is_allowed():
    hcv.check_download_url("https://release-assets.githubusercontent.com/x")
    hcv.check_download_url("https://objects.githubusercontent.com/x")


def test_a_redirect_elsewhere_is_refused():
    handler = hcv._GitHubRedirects()
    request = urllib.request.Request("https://github.com/x")
    with pytest.raises(hcv.InstallerError):
        handler.redirect_request(request, None, 302, "Found", {}, "https://evil.example/x")
    with pytest.raises(hcv.InstallerError):
        handler.redirect_request(request, None, 302, "Found", {}, "http://github.com/x")


# -- the signing tool

def test_the_tool_refuses_what_is_no_pc_build(tmp_path, key):
    other = tmp_path / "other.tar.gz"
    tar_release(other, {"build/windows/halo.exe": b"MZ"})
    result = subprocess.run([sys.executable, TOOL, "--key", str(key[0]), "--pc-linux", str(other), "--version",
                             VERSION, "--out", str(tmp_path / "o")], capture_output=True, text=True)
    assert result.returncode != 0 and "no build/linux/halo" in result.stderr
    assert not (tmp_path / "o").exists()


def test_the_tool_names_the_file_and_signs_both_systems(tmp_path, key):
    archive = tar_release(tmp_path / "whatever.tar.gz", top=TOP)
    out = sign(tmp_path, key[0], archive)
    text = (out / hcv.PC_MANIFEST).read_text()
    assert text.startswith("halo-ce-vita-update-pc 1\nversion %s\nlinux %s " % (VERSION, ARCHIVE))
    with open(archive, "rb") as file:
        data = file.read()
    assert hashlib.sha512(data).hexdigest() in text and (out / ARCHIVE).read_bytes() == data


def test_verify_update_checks_the_pc_files(tmp_path, release, key):
    verify = os.path.join(REPOSITORY, "tools", "release", "verify_update.py")
    command = [sys.executable, verify, "--pc", "--dir", str(release.out), "--version", VERSION, "--public",
               hcv.RELEASE_PUBLIC_KEY]
    assert subprocess.run(command, capture_output=True, text=True).returncode == 0
    with open(str(release.out / ARCHIVE), "ab") as file:
        file.write(b"x")
    assert subprocess.run(command, capture_output=True, text=True).returncode != 0


# -- the command line

def test_command_line_download(tmp_path, release, capsys):
    folder = tmp_path / "Halo CE"
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder), "--download"]) in (0, None)
    assert (folder / "build" / "linux" / "halo").exists()
    assert hcv.read_install(str(folder))["verified"] is True
    assert hcv.read_install(str(folder))["version"] == VERSION


def test_command_line_refuses_a_changed_download(tmp_path, release, capsys):
    release.files[release.archive_url] = release.files[release.archive_url][:-1]
    folder = tmp_path / "Halo CE"
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder), "--download"]) not in (0, None)
    assert not (folder / "build").exists()


def test_a_package_given_is_warned_about_and_not_verified_unless_it_is_the_signed_file(tmp_path, release, capsys):
    folder = tmp_path / "Halo CE"
    archive = tmp_path / ARCHIVE
    # (the very bytes the release signed: as good as the download)
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder), "--package", str(archive)]) in (0, None)
    assert "not signature-checked" in capsys.readouterr().out
    record = hcv.read_install(str(folder))
    assert record["verified"] is True and record["version"] == VERSION
    # (another file by that name: installed, as the user asked, and never verified)
    release.files[LATEST + hcv.PC_MANIFEST], keep = b"", release.files[LATEST + hcv.PC_MANIFEST]
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder), "--package", str(archive)]) in (0, None)
    assert hcv.read_install(str(folder))["verified"] is False


def test_with_nothing_given_the_command_line_downloads(tmp_path, release, monkeypatch):
    """A well named archive beside Setup is never picked by itself."""
    here = tmp_path / "Downloads"
    here.mkdir()
    fake = tar_release(here / "halo-ce-vita-99.0.0-pc-linux.tar.gz", top="halo-ce-vita-99.0.0-pc-linux")
    monkeypatch.setattr(hcv, "_tool_folder", lambda: str(here))
    monkeypatch.chdir(str(here))
    folder = tmp_path / "Halo CE"
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder)]) in (0, None)
    record = hcv.read_install(str(folder))
    assert record["verified"] is True and record["version"] == VERSION
    assert os.path.exists(fake)


def test_an_unchecked_install_is_no_floor(tmp_path, release):
    """A file called 99.0.0, installed, does not keep the genuine release out."""
    assert hcv.installed_version({"version": "99.0.0", "verified": False}) == ""
    assert hcv.installed_version({"version": "99.0.0"}) == ""
    assert hcv.installed_version({"version": "99.0.0", "verified": "yes"}) == ""
    assert hcv.installed_version({"version": "1.1.3", "verified": True}) == "1.1.3"
    folder = tmp_path / "Halo CE"
    fake = tar_release(tmp_path / "halo-ce-vita-99.0.0-pc-linux.tar.gz", top="halo-ce-vita-99.0.0-pc-linux")
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder), "--package", str(fake)]) in (0, None)
    assert hcv.read_install(str(folder))["verified"] is False
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder)]) in (0, None)
    record = hcv.read_install(str(folder))
    assert record["verified"] is True and record["version"] == VERSION


def test_a_verified_install_is_a_floor(tmp_path, release):
    folder = tmp_path / "Halo CE"
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder)]) in (0, None)
    record = hcv.read_install(str(folder))
    record["version"] = "9.9.9"
    (folder / hcv.INSTALL_RECORD).write_text(hcv.json.dumps(record))
    assert hcv.main(["--out", str(tmp_path / "out"), "pc-install", str(folder)]) not in (0, None)


# -- the wizard

def empty_folder(tmp_path, monkeypatch):
    """No archive beside the test: the wizard offers its download."""
    here = tmp_path / "elsewhere"
    here.mkdir()
    monkeypatch.chdir(str(here))


def to_ready(window, root, tmp_path):
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    advance(window, root, "destination", "ready")


def test_the_wizard_downloads_verifies_installs_and_launches(root, tmp_path, release, monkeypatch):
    errors = complaints(monkeypatch)
    mcc = fake_steam(tmp_path, monkeypatch)
    assert mcc
    empty_folder(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    to_ready(window, root, tmp_path)
    assert window.downloading()
    assert "Downloaded from the project's GitHub releases" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    folder = tmp_path / "Halo CE"
    assert (folder / "build" / "linux" / "halo").exists()
    assert hcv.read_install(str(folder))["verified"] is True
    assert hcv.read_install(str(folder))["version"] == VERSION
    assert list(checks(window)) == ["Launch Halo CE"]
    assert not window.download_folder
    runs = []
    monkeypatch.setattr(hcv, "launch_pc", runs.append)
    window.next_button.invoke()
    assert runs == [str(folder)]


def test_a_failed_check_installs_nothing_and_offers_no_launch(root, tmp_path, release, monkeypatch):
    errors = complaints(monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    empty_folder(tmp_path, monkeypatch)
    release.files[LATEST + hcv.PC_MANIFEST] += b"x"
    window = wizard(root, tmp_path)
    to_ready(window, root, tmp_path)
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert window.outcome == "failed" and "signature" in window.reason
    assert not (tmp_path / "Halo CE").exists() or not os.listdir(str(tmp_path / "Halo CE"))
    assert "Launch Halo CE" not in checks(window)
    assert not window.download_folder
    monkeypatch.setattr(hcv, "launch_pc", lambda folder: pytest.fail("started"))
    window.finish()


def test_an_archive_beside_setup_is_not_picked(root, tmp_path, monkeypatch):
    """A site can drop a well named file into Downloads, where Setup runs."""
    package_next_to_the_tool(tmp_path, monkeypatch)
    (tmp_path / "tool" / "halo-ce-vita-99.0.0-pc-linux.tar.gz").write_bytes(b"anyone's")
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    assert window.download_build.get() and not window.package.get()
    to_ready(window, root, tmp_path)
    assert "Downloaded from the project's GitHub releases" in window.summary_box.get("1.0", "end")


def test_a_chosen_archive_is_warned_about_and_no_means_no(root, tmp_path, monkeypatch, yes_to_questions):
    from tkinter import messagebox
    package_next_to_the_tool(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    choose_package(window, tmp_path)
    window.next_button.invoke()
    root.update()
    assert window.page == "destination"
    assert any("not signature-checked" in text for text in texts(window))
    options = {}
    monkeypatch.setattr(messagebox, "askyesno", lambda title, text, **given: options.update(given, text=text) or False)
    window.next_button.invoke()
    root.update()
    assert window.page == "destination"  # no: it stays
    assert "not signature-checked" in options["text"] and options["default"] == "no"
    monkeypatch.setattr(messagebox, "askyesno", lambda *a, **k: True)
    window.next_button.invoke()
    root.update()
    assert window.page == "ready"
    assert "(not signature-checked)" in window.summary_box.get("1.0", "end")


def test_a_chosen_archive_that_is_the_signed_file_counts_as_checked(root, tmp_path, release, monkeypatch):
    errors = complaints(monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    window.download_build.set(False)
    window.package.set(str(tmp_path / ARCHIVE))
    window.next_button.invoke()
    root.update()
    window.next_button.invoke()
    root.update()
    assert window.page == "ready"
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    assert release.archive_url not in release.asked  # (compared, not downloaded)
    record = hcv.read_install(str(tmp_path / "Halo CE"))
    assert record["verified"] is True and record["version"] == VERSION
    assert list(checks(window)) == ["Launch Halo CE"]


def test_a_local_archive_installs_but_is_not_started(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    package_next_to_the_tool(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    choose_package(window, tmp_path)
    advance(window, root, "destination", "ready")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done"
    assert "Launch Halo CE" not in checks(window)
    assert any("does not start it" in text for text in texts(window))
    assert hcv.read_install(str(tmp_path / "Halo CE"))["verified"] is False
    monkeypatch.setattr(hcv, "launch_pc", lambda folder: pytest.fail("started"))
    window.next_button.invoke()


def test_the_download_can_be_turned_off_for_an_archive(root, tmp_path, monkeypatch):
    empty_folder(tmp_path, monkeypatch)
    fake_steam(tmp_path, monkeypatch)
    window = wizard(root, tmp_path)
    window.install_folder.set(str(tmp_path / "Halo CE"))
    to_game_files(window, root, "pc")
    window.next_button.invoke()
    root.update()
    assert window.page == "destination" and not any(text.startswith("PC build (") for text in texts(window))
    window.download_build.set(False)
    window.show("destination")
    root.update()
    assert any(text.startswith("PC build (") for text in texts(window))


# -- the real opener, over TLS to this machine

@pytest.fixture
def https_server(tmp_path, monkeypatch):
    """HTTPS on 127.0.0.1:443 (ns.sh's namespace lets a test bind it, else
    skipped), its certificate the only authority trusted."""
    import http.server
    import shutil
    import ssl
    import threading

    openssl = shutil.which("openssl")
    if not openssl:
        pytest.skip("no openssl")
    certificate, private = tmp_path / "cert.pem", tmp_path / "key.pem"
    subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost",
                    "-addext", "subjectAltName=DNS:localhost", "-keyout", str(private), "-out", str(certificate)],
                   check=True, capture_output=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/moved":
                self.send_response(302)
                self.send_header("Location", "https://localhost/file")
                self.end_headers()
            elif self.path == "/escape":
                self.send_response(302)
                self.send_header("Location", "http://localhost/file")
                self.end_headers()
            else:
                self.send_response(200)
                self.send_header("Content-Length", "5")
                self.end_headers()
                self.wfile.write(b"hello")

        def log_message(self, *arguments):
            pass

    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 443), Handler)
    except OSError as error:
        pytest.skip("port 443: %s" % error)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(certificate), str(private))
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    monkeypatch.setattr(hcv, "DOWNLOAD_HOSTS", ("localhost",))
    monkeypatch.setenv("SSL_CERT_FILE", str(certificate))
    yield "https://localhost"
    server.shutdown()
    server.server_close()


def test_the_real_opener_follows_a_github_redirect(https_server):
    with hcv.open_download(https_server + "/moved") as answer:
        assert answer.read() == b"hello"


def test_the_real_opener_refuses_a_redirect_to_http(https_server):
    with pytest.raises(hcv.InstallerError, match="other than GitHub"):
        hcv.open_download(https_server + "/escape")


def test_the_real_opener_refuses_a_certificate_it_does_not_trust(https_server, monkeypatch, tmp_path):
    other = tmp_path / "other.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=other",
                    "-keyout", str(tmp_path / "other.key"), "-out", str(other)], check=True, capture_output=True)
    monkeypatch.setenv("SSL_CERT_FILE", str(other))
    with pytest.raises(urllib.error.URLError):
        hcv.open_download(https_server + "/file")
