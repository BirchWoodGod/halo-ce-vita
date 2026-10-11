"""Tests for reading the PNG pictures of a Halo: The Master Chief Collection
mod's levels (port/linux/game/png_files.c), which the campaign's level list
shows (custom_edition_mods.c, custom_edition_maps.c).

The pictures are made here (no mod's are stored) with every one of PNG's
row filters, and the report tool port/tools/png_file_report.c is built with
the port's zlib (port/third_party/zlib, its names prefixed z_ as the game
builds it), warnings as errors, undefined behaviour trapping and, where the
compiler has it, AddressSanitizer: the bitmap each picture becomes is
compared pixel for pixel, the kinds not read are refused, and every
truncation and seeded random corruptions of a valid file must end in a
clean verdict.
"""
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "port/linux/game/png_files.c"
TOOL = ROOT / "port/tools/png_file_report.c"
ZLIB = ROOT / "port/third_party/zlib"
ZLIB_SOURCES = ["adler32", "crc32", "inffast", "inflate", "inftrees", "zutil"]

STRICT_FLAGS = ["-Wall", "-Wextra", "-Wpedantic", "-Werror"]
UB_TRAP_FLAGS = ["-fsanitize=undefined", "-fsanitize-trap=undefined"]


def find_clang():
    for candidate in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


@pytest.fixture(scope="session")
def report_tool(tmp_path_factory):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed to build the report tool")
    folder = tmp_path_factory.mktemp("png-file-report")
    output = folder / ("png_file_report.exe" if sys.platform == "win32" else "png_file_report")
    errors = []
    for extra in (["-fsanitize=address", "-fno-omit-frame-pointer"], []):
        objects = []
        for source in ZLIB_SOURCES:
            target = folder / f"{source}.o"
            result = subprocess.run([clang, "-O1", "-w", *extra, "-DZ_PREFIX", "-c", str(ZLIB / f"{source}.c"),
                                     "-o", str(target)], capture_output=True, text=True)
            if result.returncode:
                errors.append(result.stderr)
                break
            objects.append(str(target))
        else:
            command = [clang, "-std=c99", *STRICT_FLAGS, *UB_TRAP_FLAGS, *extra, "-O1", "-g", f"-I{MODULE.parent}",
                       f"-I{ROOT / 'source'}", str(MODULE), str(TOOL), *objects, "-o", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode == 0:
                return output
            errors.append(result.stdout + result.stderr)
    pytest.fail("could not build the report tool:\n" + "\n".join(errors))


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(width, height, channels=4, filters=None, bit_depth=8, color_type=None, interlace=0, pixels=None,
        compress=None, extra_chunks=b"", split=1):
    """A PNG of `width` by `height` (each row's filter from `filters`,
    cycling), and its pixels as (r, g, b) rows."""
    color_type = color_type if color_type is not None else (6 if channels == 4 else 2)
    filters = filters or [0, 1, 2, 3, 4]
    if pixels is None:
        pixels = [[((x * 37 + y * 11) & 0xFF, (x * 5 + y * 71) & 0xFF, (x ^ y) & 0xFF, (x + y) & 0xFF)
                   for x in range(width)] for y in range(height)]
    raw = bytearray()
    previous = bytes(width * channels)
    for y in range(height):
        row = bytes(value for x in range(width) for value in pixels[y][x][:channels])
        kind = filters[y % len(filters)]
        filtered = bytearray()
        for x in range(len(row)):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 0:
                predicted = 0
            elif kind == 1:
                predicted = left
            elif kind == 2:
                predicted = up
            elif kind == 3:
                predicted = (left + up) // 2
            else:
                estimate = left + up - corner
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
                predicted = (left, up, corner)[distances.index(min(distances))]
            filtered.append((row[x] - predicted) & 0xFF)
        raw += bytes([kind]) + filtered
        previous = row
    compressed = compress(bytes(raw)) if compress else zlib.compress(bytes(raw), 9)
    header = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace)
    pieces = [compressed[index * len(compressed) // split:(index + 1) * len(compressed) // split] for index in range(split)]
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + extra_chunks
    data += b"".join(chunk(b"IDAT", piece) for piece in pieces) + chunk(b"IEND", b"")
    return data, [[tuple(pixel[:3]) for pixel in row] for row in pixels]


def report(tool, data, folder):
    path = folder / "picture.png"
    path.write_bytes(data)
    bitmap = folder / "picture.bmp"
    if bitmap.exists():
        bitmap.unlink()
    result = subprocess.run([str(tool), str(path), str(bitmap)], capture_output=True, text=True)
    assert result.returncode in (0, 1), (result.returncode, result.stdout, result.stderr[-4000:])
    fields = dict(line.partition(": ")[::2] for line in result.stdout.splitlines())
    return result.returncode, fields, bitmap.read_bytes() if bitmap.exists() else None


def bitmap_pixels(bitmap):
    assert bitmap[:2] == b"BM"
    offset, = struct.unpack_from("<I", bitmap, 10)
    width, height, planes, bits = struct.unpack_from("<iiHH", bitmap, 18)
    assert planes == 1 and bits == 32 and height < 0
    height = -height
    return [[(bitmap[offset + (y * width + x) * 4 + 2], bitmap[offset + (y * width + x) * 4 + 1],
              bitmap[offset + (y * width + x) * 4]) for x in range(width)] for y in range(height)]


@pytest.mark.parametrize("channels", [3, 4])
def test_every_filter_is_undone(report_tool, tmp_path, channels):
    data, pixels = png(37, 23, channels=channels)
    returncode, fields, bitmap = report(report_tool, data, tmp_path)
    assert returncode == 0, fields
    assert bitmap_pixels(bitmap) == pixels


def test_image_data_in_many_chunks_and_other_chunks_between(report_tool, tmp_path):
    data, pixels = png(64, 40, extra_chunks=chunk(b"tEXt", b"Comment\0made up") + chunk(b"gAMA", bytes(4)), split=7)
    returncode, _, bitmap = report(report_tool, data, tmp_path)
    assert returncode == 0
    assert bitmap_pixels(bitmap) == pixels


def test_the_largest_picture_read(report_tool, tmp_path):
    data, pixels = png(1024, 4, channels=3)
    returncode, _, bitmap = report(report_tool, data, tmp_path)
    assert returncode == 0
    assert bitmap_pixels(bitmap) == pixels


@pytest.mark.parametrize("options,status", [
    ({"bit_depth": 16}, "unsupported"),
    ({"color_type": 3}, "unsupported"),
    ({"color_type": 0}, "unsupported"),
    ({"interlace": 1}, "unsupported"),
    ({"width": 1025}, "unsupported"),
    ({"height": 1025}, "unsupported"),
    ({"width": 0}, "unsupported"),
    ({"compress": lambda raw: zlib.compress(raw)[:-20]}, "damaged"),
    ({"compress": lambda raw: zlib.compress(raw[:-10])}, "damaged"),
    ({"compress": lambda raw: zlib.compress(raw + bytes(100))}, "damaged"),
    ({"compress": lambda raw: b"\x00" * 50}, "damaged"),
    ({"filters": [5]}, "damaged"),
])
def test_kinds_not_read_and_damage_are_refused(report_tool, tmp_path, options, status):
    width = options.pop("width", 16)
    height = options.pop("height", 8)
    if options.get("filters") == [5]:
        data, _ = png(width, height, compress=lambda raw: zlib.compress(bytes([5]) + raw[1:]))
    else:
        data, _ = png(max(width, 1), max(height, 1), **options)
        if width == 0 or width > 1024 or height > 1024:
            data = bytearray(data)
            struct.pack_into(">II", data, 16, width, height)
            data = bytes(data)
    returncode, fields, bitmap = report(report_tool, data, tmp_path)
    assert returncode == 1
    assert fields["status"].startswith({"unsupported": "not an 8-bit", "damaged": "damaged"}[status]), fields
    assert bitmap is None


def test_not_a_png_is_refused(report_tool, tmp_path):
    returncode, fields, _ = report(report_tool, b"BM" + bytes(100), tmp_path)
    assert returncode == 1 and fields["status"] == "not a PNG file"


def test_every_truncation_is_refused_cleanly(report_tool, tmp_path):
    data, _ = png(20, 10, split=3)
    for length in range(len(data)):
        returncode, _, _ = report(report_tool, data[:length], tmp_path)
        # (only the end chunk's loss leaves a whole picture)
        assert returncode == 1 or length >= len(data) - 12, length


def test_seeded_corruption_never_crashes_the_reader(report_tool, tmp_path):
    data, _ = png(30, 20, split=4)
    generator = random.Random(20261010)
    for trial in range(300):
        corrupted = bytearray(data)
        for _ in range(generator.randint(1, 6)):
            offset = generator.randrange(len(corrupted))
            if generator.random() < 0.7:
                corrupted[offset] = generator.randrange(256)
            else:
                struct.pack_into(">I", corrupted, min(offset, len(corrupted) - 4),
                                 generator.choice([0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 1024, 1025, len(corrupted)]))
        report(report_tool, bytes(corrupted), tmp_path)
