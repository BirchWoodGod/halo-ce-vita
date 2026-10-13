"""Tests for port/tools/mcc_map_convert.py's own parsing: the parts that read
the mod's files themselves (a map's bitmap headers, a scenario tag's script
source, the mod's pictures) and the script rewriting. Invader's part needs
Invader and a mod, and is run by port/vita/tests/run_mcc_maps_test.sh.
Everything here is made up in memory: no game data."""
import struct
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/tools"))
import mcc_map_convert as convert  # noqa: E402

BASE = 0x50000000


def mcc_map(bitmaps):
    """A cache version 13 map's header and tag data, with one bitmap tag of
    `bitmaps`: (format, flags, width, height, mipmaps, pixels size)."""
    tag_data = bytearray(0x40)
    instances = len(tag_data)
    tag_data += bytes(32)
    group = len(tag_data)
    tag_data += bytes(0x6C)
    elements = len(tag_data)
    for bitmap_format, flags, width, height, mipmaps, size in bitmaps:
        element = bytearray(0x30)
        struct.pack_into("<4sHHHHHH", element, 0, b"mtib", width, height, 1, 0, bitmap_format, flags)
        struct.pack_into("<H", element, 0x14, mipmaps)
        struct.pack_into("<I", element, 0x1C, size)
        tag_data += element
    struct.pack_into("<IIII", tag_data, 0, BASE + instances, 0, 0, 1)
    struct.pack_into("<4sIIIIII", tag_data, instances, b"mtib", 0, 0, 0, 0, BASE + group, 0)
    struct.pack_into("<iI", tag_data, group + 0x60, len(bitmaps), BASE + elements)
    header = bytearray(0x800)
    struct.pack_into("<4sIIIII", header, 0, b"daeh", 13, 0x800 + len(tag_data), 0, 0x800, len(tag_data))
    return bytes(header + tag_data)


def test_unmarked_dxt_bitmaps_are_found_and_their_whole_mipmaps_counted(tmp_path):
    path = tmp_path / "test.map"
    # the HUD wraith icon: 1024x512 DXT1, its pixels measured uncompressed
    # without whole blocks (0x55555), which hold mipmaps 0-7 whole
    path.write_bytes(mcc_map([(14, 0x81, 1024, 512, 10, 0x55555), (14, 0x83, 64, 64, 6, 0x800), (11, 0x80, 64, 64, 0, 0x4000)]))
    found = convert.unmarked_dxt_bitmaps(str(path))
    assert len(found) == 1 and found[0][1] == 7
    copy = tmp_path / "copy.map"
    convert.mark_dxt_bitmaps_compressed(str(path), found, str(copy))
    assert convert.unmarked_dxt_bitmaps(str(copy)) == []
    data = copy.read_bytes()
    assert struct.unpack_from("<H", data, found[0][0] + 0x14)[0] == 7


@pytest.mark.parametrize("damage", ["signature", "instances", "bitmaps"])
def test_a_damaged_map_is_refused_not_read_past(tmp_path, damage):
    data = bytearray(mcc_map([(14, 0x81, 64, 64, 0, 0x800)]))
    if damage == "signature":
        data[0:4] = b"head"
    elif damage == "instances":
        struct.pack_into("<I", data, 0x800, 0x7FFFFFF0)
    else:
        struct.pack_into("<iI", data, 0x800 + 0x60 + 0x60, 1000, BASE)
    path = tmp_path / "test.map"
    path.write_bytes(bytes(data))
    with pytest.raises(convert.ConversionError):
        convert.unmarked_dxt_bitmaps(str(path))


def test_dxt_bytes_counts_whole_blocks():
    assert convert.dxt_bytes(4, 4, 1, 0, 4, 0) == 8
    assert convert.dxt_bytes(8, 8, 1, 3, 8, 0) == 64 + 16 * 3
    assert convert.dxt_bytes(4, 4, 1, 0, 4, 2) == 48


def test_mcc_mission_segment_calls_become_constants():
    source = "(if (mcc_mission_segment cine1) (sleep 1))\n    (mcc_mission_segment \"02_bridge\")\n(mcc_mission_segment)"
    rewritten, count = convert.make_mcc_calls_constant(source)
    assert count == 3
    assert "mcc_" not in rewritten
    assert rewritten.startswith("(if true (sleep 1))")


def test_the_script_source_is_read_from_the_scenario_tag(tmp_path):
    texts = [";*\n================\n    extracted.hsc, file 1 of 2\n(script static void a (print \"caf\xe9\"))\n",
             ";*\n================\n    extracted.hsc, file 2 of 2\n(global short b 0)\n"]
    blocks = b"".join(b"extracted".ljust(32, b"\0") + struct.pack(">i", len(text.encode("latin-1"))) + bytes(16)
                      for text in texts)
    tag = b"\x01\x02binary scenario fields" * 40 + blocks + b"".join(text.encode("latin-1") for text in texts) + b"\0" * 64
    path = tmp_path / "test.scenario"
    path.write_bytes(tag)
    assert convert.scenario_source(str(path)) == texts
    path.write_bytes(b"no script source" * 10)
    assert convert.scenario_source(str(path)) == []


def png(width, height, rows, color=6):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(bytes([filter_type]) + row for filter_type, row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def test_a_png_becomes_a_24_bit_bitmap(tmp_path):
    # row 0 unfiltered, row 1 "up" (each byte added to the one above)
    rows = [(0, bytes([10, 20, 30, 255, 40, 50, 60, 255])), (2, bytes([1, 1, 1, 0, 2, 2, 2, 0]))]
    path = tmp_path / "picture.png"
    path.write_bytes(png(2, 2, rows))
    width, height, pixels = convert.png_pixels(str(path))
    assert (width, height) == (2, 2)
    assert pixels == [bytes([10, 20, 30, 40, 50, 60]), bytes([11, 21, 31, 42, 52, 62])]
    bmp = tmp_path / "picture.bmp"
    convert.write_bmp(str(bmp), width, height, pixels)
    data = bmp.read_bytes()
    assert data[:2] == b"BM" and struct.unpack_from("<iiHH", data, 18) == (2, 2, 1, 24)
    # bottom-up, BGR, rows padded to 4 bytes
    assert data[54:62] == bytes([31, 21, 11, 62, 52, 42, 0, 0])


@pytest.mark.parametrize("case", ["interlaced", "16-bit", "palette", "truncated", "huge", "bad filter"])
def test_other_pngs_are_skipped(tmp_path, case):
    rows = [(0, bytes(8)), (0, bytes(8))]
    data = png(2, 2, rows)
    if case == "interlaced":
        data = data[:28] + b"\x01" + data[29:]
    elif case == "16-bit":
        data = data[:24] + b"\x10" + data[25:]
    elif case == "palette":
        data = png(2, 2, rows, color=3)
    elif case == "truncated":
        data = data[:40]
    elif case == "huge":
        data = data[:16] + struct.pack(">II", 100000, 100000) + data[24:]
    else:
        data = png(2, 2, [(7, bytes(8)), (0, bytes(8))])
    path = tmp_path / "picture.png"
    path.write_bytes(data)
    assert convert.png_pixels(str(path)) is None


def test_level_text_is_written_from_the_mods_campaign_files(tmp_path):
    mod = tmp_path / "mod"
    (mod / "campaign").mkdir(parents=True)
    (mod / "assets").mkdir()
    (mod / "assets" / "1 small.png").write_bytes(png(2, 2, [(0, bytes(8)), (0, bytes(8))]))
    (mod / "campaign" / "a10.json").write_text(
        '{"ScenarioFile": "a10", "Title": {"Neutral": "The Pillar of Autumn"}, '
        '"Description": {"Neutral": "Escape intact as Covenant forces board your ship."}, '
        '"Images": {"Thumbnail": "assets\\\\1 small.png", "Large": "..\\\\..\\\\outside.png"}}')
    out = tmp_path / "out"
    out.mkdir()
    assert convert.write_level_text(str(mod), "a10", str(out), "ruby_a10")
    assert (out / "ruby_a10.txt").read_text().splitlines() == [
        "The Pillar of Autumn", "", "Escape intact as", "Covenant forces board", "your ship."]
    assert (out / "ruby_a10.bmp").read_bytes()[:2] == b"BM"
    assert not convert.write_level_text(str(mod), "b30", str(out), "ruby_b30")


# ---------- MCC's multiplayer maps: their sounds from the Xbox maps (fake
# Invader tools: small Python scripts that do what the converter asks of
# them, no game data)

def fake_tool(folder, name, body):
    path = folder / name
    path.write_text("#!" + sys.executable + "\nimport os, sys\n" + body)
    path.chmod(0o755)
    return path


class Arguments:
    def __init__(self, invader, work, pool=None, mcc_maps=None, sound_maps=None):
        self.invader = str(invader)
        self.work = str(work)
        self.sound_pool = str(pool) if pool else None
        self.mcc_maps = mcc_maps
        self.sound_maps = sound_maps
        self.prefix = "pc_"
        self.keep_work = False


def test_a_silent_sound_is_a_tenth_of_a_second_of_22_khz_mono(tmp_path):
    path = tmp_path / "silence.wav"
    convert.write_silent_wav(str(path))
    data = path.read_bytes()
    assert data[:4] == b"RIFF" and data[8:16] == b"WAVEfmt "
    assert struct.unpack_from("<IHHIIHH", data, 16) == (16, 1, 1, 22050, 44100, 2, 16)
    assert data[36:40] == b"data" and struct.unpack_from("<I", data, 40)[0] == 2 * convert.SILENT_SOUND_FRAMES
    assert set(data[44:]) == {0} and len(data) == 44 + 2 * convert.SILENT_SOUND_FRAMES


def test_external_sounds_come_from_the_xbox_maps_else_a_stand_in_else_silence(tmp_path):
    invader = tmp_path / "invader"
    invader.mkdir()
    # (invader-sound -d DATA -t TAGS -c CLASS -F FORMAT PATH: the tag made
    # from DATA/PATH's wav, here its arguments written as the tag)
    fake_tool(invader, "invader-sound",
        "a = sys.argv[1:]\n"
        "tags = a[a.index('-t') + 1]; path = a[-1]\n"
        "assert os.path.isfile(os.path.join(a[a.index('-d') + 1], path, os.path.basename(path) + '.wav'))\n"
        "os.makedirs(os.path.dirname(os.path.join(tags, path + '.sound')), exist_ok=True)\n"
        "open(os.path.join(tags, path + '.sound'), 'w').write(' '.join(a))\n")
    pool = tmp_path / "pool"
    for name, text in (("sound/sfx/ui/teleporter_loop.sound", "xbox teleporter"),
            ("sound/sfx/weapons/fuel rod gun/explosion.sound", "xbox fuel rod explosion")):
        (pool / name).parent.mkdir(parents=True, exist_ok=True)
        (pool / name).write_text(text)
    tags = tmp_path / "tags"
    (tags / "sound/sfx/ui").mkdir(parents=True)
    (tags / "sound/sfx/ui/shield_hit.sound").write_text("already there")
    work = tmp_path / "work"
    work.mkdir()
    log = tmp_path / "log"
    sounds = ["sound/sfx/ui/teleporter_loop.sound",
        "sound/sfx/weapons/plasma_cannon/plasma_cannon_explosion.sound",
        "levels/test/timberland/sound/timberland_stream.sound",
        "sound/sfx/nowhere/unknown.sound",
        "sound/sfx/ui/shield_hit.sound"]
    counts = convert.fill_external_sounds(Arguments(invader, work, pool), str(tags), str(work), sounds, str(log))
    assert counts == (1, 1, 2)
    assert (tags / "sound/sfx/ui/teleporter_loop.sound").read_text() == "xbox teleporter"
    assert (tags / "sound/sfx/weapons/plasma_cannon/plasma_cannon_explosion.sound").read_text() == "xbox fuel rod explosion"
    stream = (tags / "levels/test/timberland/sound/timberland_stream.sound").read_text()
    assert "-c ambient_nature" in stream and "-F xbox_adpcm" in stream
    assert "-c " + convert.SILENT_SOUND_CLASS in (tags / "sound/sfx/nowhere/unknown.sound").read_text()
    assert (tags / "sound/sfx/ui/shield_hit.sound").read_text() == "already there"
    text = log.read_text()
    assert "plasma_cannon_explosion: in no Xbox map; given sound/sfx/weapons/fuel rod gun/explosion" in text
    assert "timberland_stream: in no Xbox map; silent" in text


def test_every_pc_only_stand_in_is_a_sound_the_xbox_maps_have():
    # (a stand-in is never itself one of Halo PC's own, and silence has a class)
    for path, (stand_in, sound_class) in convert.PC_ONLY_SOUNDS.items():
        assert (stand_in is None) != (sound_class is None), path
        assert stand_in not in convert.PC_ONLY_SOUNDS
        assert not path.endswith(".sound") and not (stand_in or "").endswith(".sound")


def test_the_sound_pool_is_the_xbox_maps_multiplayer_first(tmp_path):
    invader = tmp_path / "invader"
    invader.mkdir()
    fake_tool(invader, "invader-info",
        "a = sys.argv[1:]; kind = a[a.index('-T') + 1]; name = os.path.basename(a[-1])\n"
        "xbox = name in ('a10.map', 'bloodgulch.map')\n"
        "print({'engine': 'Halo: Combat Evolved (Xbox NTSC-US)' if xbox else 'Halo Custom Edition',\n"
        "    'map_type': 'Multiplayer' if name == 'bloodgulch.map' else 'Singleplayer'}[kind])\n")
    fake_tool(invader, "invader-extract",
        "a = sys.argv[1:]\n"
        "open(os.path.join(a[a.index('-t') + 1], 'order'), 'a').write(os.path.basename(a[-1]) + ' ' + a[a.index('-s') + 1] + '\\n')\n")
    maps = tmp_path / "maps"
    maps.mkdir()
    for name in ("a10.map", "bloodgulch.map", "custom.map", "bitmaps.map", "notes.txt"):
        (maps / name).write_bytes(b"x")
    work = tmp_path / "work"
    work.mkdir()
    arguments = Arguments(invader, work, sound_maps=str(maps))
    pool = convert.xbox_sound_pool(arguments, str(tmp_path / "log"))
    assert (Path(pool) / "order").read_text().splitlines() == ["bloodgulch.map *.sound", "a10.map *.sound"]
    assert convert.xbox_sound_pool(arguments, str(tmp_path / "log")) == pool
    empty = tmp_path / "empty"
    empty.mkdir()
    with pytest.raises(convert.ConversionError):
        convert.xbox_sound_pool(Arguments(invader, work, sound_maps=str(empty)), str(tmp_path / "log"))


@pytest.mark.parametrize("case", ["other kinds", "no folders", "no bitmaps.map"])
def test_a_map_of_mccs_resource_maps_is_refused_without_what_it_needs(tmp_path, case):
    invader = tmp_path / "invader"
    invader.mkdir()
    counts = {"external_tags_count": "5" if case == "other kinds" else "4", "external_bitmaps_count": "2",
        "external_sounds_count": "2"}
    fake_tool(invader, "invader-info",
        "a = sys.argv[1:]; kind = a[a.index('-T') + 1]\n"
        f"print({dict(counts, engine='Halo: Combat Evolved Anniversary (MCC)')!r}.get(kind, ''))\n")
    mcc = tmp_path / "mcc"
    mcc.mkdir()
    (mcc / "bloodgulch.map").write_bytes(b"x")
    arguments = Arguments(invader, tmp_path / "work", mcc_maps=None if case == "no folders" else str(mcc),
        sound_maps=None if case == "no folders" else str(tmp_path))
    arguments.output = str(tmp_path / "out")
    (tmp_path / "out").mkdir()
    with pytest.raises(convert.ConversionError) as refused:
        convert.convert(arguments, str(mcc / "bloodgulch.map"))
    assert {"other kinds": "other tags than bitmaps and sounds", "no folders": "--mcc-maps",
        "no bitmaps.map": "no bitmaps.map"}[case] in str(refused.value)
