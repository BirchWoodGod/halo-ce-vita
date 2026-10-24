# SPDX-License-Identifier: GPL-3.0-only
"""Step 2 with a stand-in ffmpeg (a small Python program: no ffmpeg needed
here), and the command line's steps on generated files."""

import os
import stat
import sys

import pytest

import fakes
import halo_ce_vita_installer as hcv

FAKE_FFMPEG = r'''
import sys
arguments = sys.argv[1:]
with open(sys.argv[0] + ".log", "a") as log:
    log.write(repr(arguments) + "\n")
source = arguments[arguments.index("-i") + 1]
data = open(source, "rb").read()
if data.startswith(b"BAD"):
    sys.stdout.write("Invalid data found when processing input\n")
    sys.exit(1)
sys.stdout.write("  Duration: 00:00:02.00, start: 0.000000, bitrate: 1 kb/s\n")
sys.stdout.write("out_time_us=1000000\nprogress=continue\nout_time_us=2000000\nprogress=end\n")
open(arguments[-1], "wb").write(b"MP4" + data)
'''


@pytest.fixture
def ffmpeg(tmp_path):
    if sys.platform == "win32":
        pytest.skip("the stand-in ffmpeg is a script")
    path = tmp_path / "bin" / "ffmpeg"
    path.parent.mkdir()
    path.write_text("#!" + sys.executable + "\n" + FAKE_FFMPEG)
    path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return path


def test_convert_from_image(tmp_path, ffmpeg):
    image = tmp_path / "halo.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree())
    out = tmp_path / "out"
    progress = []
    written = hcv.convert_movies(str(image), str(out), str(ffmpeg), "high",
                                 lambda message, done, total: progress.append((done, total)),
                                 log=lambda message: None)
    assert sorted(os.path.basename(path) for path in written) == ["credits.mp4", "intro.mp4"]
    tree = fakes.xbox_tree()
    assert (out / "movies" / "intro.mp4").read_bytes() == b"MP4" + tree["bink"]["intro.bik"]
    assert (2000000, 2000000) in progress
    calls = (ffmpeg.parent / "ffmpeg.log").read_text().splitlines()
    assert len(calls) == 2
    # "high" is the old name of "better": the Vita-safe Baseline, never High profile
    assert "'-profile:v', 'baseline'" in calls[0] and "'-crf', '18'" in calls[0]
    assert "'high'" not in calls[0] and "'scale=640:-2'" in calls[0] and "'-f', 'mp4'" in calls[0]
    # the temporary copies of the movies are gone
    assert sorted(os.listdir(out)) == ["movies"]


@pytest.mark.parametrize("quality", sorted(hcv.FFMPEG_MOVIE_ARGS))
def test_every_movie_choice_is_vita_safe(quality):
    """Every choice: H.264 Constrained Baseline (x264's baseline profile), no
    B-frames, 4:2:0, at most level 3.1, 640 wide. The Vita shows High
    profile's B-frames as white frames."""
    arguments = hcv.FFMPEG_MOVIE_ARGS[quality]

    def value(flag):
        return arguments[arguments.index(flag) + 1]
    assert value("-c:v") == "libx264"
    assert value("-profile:v") == "baseline"
    assert value("-bf") == "0"
    assert value("-pix_fmt") == "yuv420p"
    assert float(value("-level")) <= 3.1
    assert value("-vf") == "scale=640:-2"
    assert not any(word in ("high", "main", "high10", "high422", "high444") for word in arguments)
    assert "-x264-params" not in arguments and "-x264opts" not in arguments


def test_movie_quality_names(tmp_path, ffmpeg):
    assert set(hcv.MOVIE_QUALITY_ALIASES.values()) <= set(hcv.FFMPEG_MOVIE_ARGS)
    with pytest.raises(hcv.InstallerError, match="unknown movie quality"):
        hcv.convert_movies(str(tmp_path), str(tmp_path / "out"), str(ffmpeg), "ultra", log=lambda message: None)


def test_convert_failure(tmp_path, ffmpeg):
    folder = tmp_path / "Halo"
    (folder / "bink").mkdir(parents=True)
    (folder / "bink" / "intro.bik").write_bytes(b"BAD movie")
    out = tmp_path / "out"
    with pytest.raises(hcv.InstallerError, match="could not convert"):
        hcv.convert_movies(str(folder), str(out), str(ffmpeg), log=lambda message: None)
    assert os.listdir(out / "movies") == []


def test_no_ffmpeg(tmp_path, monkeypatch):
    monkeypatch.setattr(hcv, "find_ffmpeg", lambda explicit=None: None)
    with pytest.raises(hcv.InstallerError, match="ffmpeg was not found"):
        hcv.convert_movies(str(tmp_path), str(tmp_path / "out"))


def test_command_line_steps(tmp_path, ffmpeg, capsys):
    image = tmp_path / "halo.iso"
    fakes.make_xiso(str(image), fakes.xbox_tree())
    program, _ = fakes.fake_installer()
    installer = tmp_path / "hce.exe"
    installer.write_bytes(program)
    out = str(tmp_path / "out")
    assert hcv.main(["--out", out, "xbox", str(image)]) == 0
    assert hcv.main(["--out", out, "movies", str(image), "--ffmpeg", str(ffmpeg)]) == 0
    assert hcv.main(["--out", out, "check-installer", str(installer)]) == 0
    assert hcv.main(["--out", out, "pc-files", "--installer", str(installer)]) == 0
    items = hcv.staged_uploads(out)
    assert len(items) == 1 + 12 + 3 + 2
    not_installer = tmp_path / "x.exe"
    not_installer.write_bytes(b"MZ" + b"\0" * 100)
    assert hcv.main(["--out", out, "check-installer", str(not_installer)]) == 1
    assert "isn't the Halo Custom Edition installer" in capsys.readouterr().out
