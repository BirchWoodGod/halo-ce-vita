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
    assert rewritten.startswith("(if (not true) (sleep 1))")


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
