# SPDX-License-Identifier: GPL-3.0-only
"""The cabinet reader and the LZX decoder against the test's own
compressor (fakes.py), and the Custom Edition installer check on a fake
installer."""

import os
import random
import struct

import pytest

import fakes
import halo_ce_vita_installer as hcv


def sample(size, seed=1):
    """Compressible bytes with every byte value (255 takes the decoder's
    long codes), repeats near and far, and E8 bytes."""
    rng = random.Random(seed)
    words = [bytes(rng.randrange(256) for _ in range(rng.randrange(3, 12))) for _ in range(300)]
    out = bytearray()
    while len(out) < size:
        choice = rng.random()
        if choice < 0.6:
            out += rng.choice(words)
        elif choice < 0.7:
            out += bytes([0xE8]) + struct.pack("<i", rng.randrange(-5000, 300000))
        elif choice < 0.8 and len(out) > 40000:
            start = rng.randrange(len(out) - 30000, len(out) - 10)
            out += out[start:start + rng.randrange(3, 300)]
        else:
            out += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 20)))
    return bytes(out[:size])


def decompress(frames, window_bits, size):
    return b"".join(hcv.lzx_decompress(b"".join(frames), window_bits, size))


@pytest.mark.parametrize("window_bits", [15, 16, 21])
def test_lzx_round_trip(window_bits):
    data = sample(150000, seed=window_bits)
    frames = fakes.lzx_compress(data, window_bits)
    assert decompress(frames, window_bits, len(data)) == data


def test_lzx_e8_translation_round_trip():
    data = sample(100000, seed=99)
    assert data.count(0xE8) > 100
    frames = fakes.lzx_compress(data, 16, e8_size=12000000)
    assert decompress(frames, 16, len(data)) == data


def test_lzx_block_kinds_one_at_a_time():
    data = sample(70001, seed=4)
    for kind in (1, 2, 3):
        frames = fakes.lzx_compress(data, 17, blocks=[(kind, len(data))])
        assert decompress(frames, 17, len(data)) == data


def test_e8_translate_by_hand():
    frame = bytearray(64)
    frame[20] = 0xE8
    struct.pack_into("<i", frame, 21, 50)          # absolute 50 at 20 -> relative 30
    frame[30] = 0xE8
    struct.pack_into("<i", frame, 31, -10)         # -10 >= -30 -> -10 + size
    frame[40] = 0xE8
    struct.pack_into("<i", frame, 41, 1000)        # >= size: left alone
    frame[56] = 0xE8                               # within the last 10 bytes: left alone
    struct.pack_into("<i", frame, 57, 50)
    hcv._e8_translate(frame, 0, 1000)
    assert struct.unpack_from("<i", frame, 21)[0] == 30
    assert struct.unpack_from("<i", frame, 31)[0] == 990
    assert struct.unpack_from("<i", frame, 41)[0] == 1000
    assert struct.unpack_from("<i", frame, 57)[0] == 50


def test_damaged_lzx_stream_is_an_error_not_a_crash():
    data = sample(60000, seed=8)
    stream = bytearray(b"".join(fakes.lzx_compress(data, 16)))
    rng = random.Random(5)
    for _ in range(20):
        damaged = bytearray(stream)
        for _ in range(10):
            damaged[rng.randrange(len(damaged))] ^= 1 << rng.randrange(8)
        try:
            out = b"".join(hcv.lzx_decompress(bytes(damaged), 16, len(data)))
        except hcv.InstallerError:
            continue
        assert len(out) == len(data)
    with pytest.raises(hcv.InstallerError):
        b"".join(hcv.lzx_decompress(bytes(stream[:len(stream) // 3]), 16, len(data)))


def test_checksum_matches_the_plain_definition():
    rng = random.Random(3)
    for size in (0, 1, 2, 3, 4, 5, 7, 8, 1000, 32771):
        data = bytes(rng.randrange(256) for _ in range(size))
        assert hcv.cab_checksum(data, 0x1234) == fakes.reference_checksum(data, 0x1234)


def test_cabinet_folders_stored_mszip_lzx(tmp_path):
    files = {
        "a\\stored.bin": sample(40000, 1),
        "b\\zipped.bin": sample(90000, 2),
        "c\\first.bin": sample(30000, 3),
        "c\\second.bin": sample(80000, 4),
    }
    cabinet = fakes.make_cab([("stored", [("a\\stored.bin", files["a\\stored.bin"])]),
                              ("mszip", [("b\\zipped.bin", files["b\\zipped.bin"])]),
                              ("lzx", [("c\\first.bin", files["c\\first.bin"]),
                                       ("c\\second.bin", files["c\\second.bin"])])], window_bits=16)
    path = tmp_path / "test.cab"
    path.write_bytes(cabinet)
    assert hcv.find_cabinets(str(path)) == [0]
    cab = hcv.Cabinet(str(path))
    wanted = {name: str(tmp_path / ("out%d" % index)) for index, name in enumerate(files)}
    cab.extract(wanted)
    for name, data in files.items():
        with open(wanted[name], "rb") as file:
            assert file.read() == data
    # a block's checksum wrong: refused
    damaged = bytearray(cabinet)
    damaged[-100] ^= 0xFF
    path.write_bytes(bytes(damaged))
    with pytest.raises(hcv.InstallerError, match="checksum|damaged"):
        hcv.Cabinet(str(path)).extract({"c\\second.bin": str(tmp_path / "x")})
    assert not os.path.exists(str(tmp_path / "x"))


def test_fake_installer_any_name(tmp_path, monkeypatch):
    program, maps = fakes.fake_installer()
    path = tmp_path / "some download (1).exe"
    path.write_bytes(program)
    check = hcv.check_ce_installer(str(path))
    assert check.problem is None and check.known is None
    assert check.cabinet.base > 0
    out = tmp_path / "out"
    written = hcv.extract_pc_maps_from_installer(str(path), str(out), log=lambda message: None)
    assert len(written) == 3
    for name, data in maps.items():
        assert (out / "maps" / name).read_bytes() == data
        assert hcv.resource_map_ok(str(out / "maps" / name), name)
    # only the three, under the tool's own names
    assert sorted(os.listdir(out / "maps")) == ["bitmaps.map", "loc.map", "sounds.map"]


def test_known_installer_hash(tmp_path, monkeypatch):
    program, maps = fakes.fake_installer()
    path = tmp_path / "halocesetup_en_1.00.exe"
    path.write_bytes(program)
    digest = hcv.file_sha256(str(path))
    monkeypatch.setattr(hcv, "KNOWN_CE_INSTALLERS", {digest: "the test's installer"})
    import hashlib
    monkeypatch.setattr(hcv, "KNOWN_CE_RESOURCE_MAPS",
                        {digest: {name: hashlib.sha256(data).hexdigest() for name, data in maps.items()}})
    assert hcv.check_ce_installer(str(path)).known == "the test's installer"
    hcv.extract_pc_maps_from_installer(str(path), str(tmp_path / "out"), log=lambda message: None)
    # a map that does not come out as the known hash says: refused
    monkeypatch.setattr(hcv, "KNOWN_CE_RESOURCE_MAPS", {digest: {"loc.map": "0" * 64}})
    with pytest.raises(hcv.InstallerError, match="damaged"):
        hcv.extract_pc_maps_from_installer(str(path), str(tmp_path / "out2"), log=lambda message: None)


def test_not_the_installer(tmp_path):
    other = tmp_path / "halocesetup_en_1.00.exe"
    other.write_bytes(fakes.make_pe([("ICONS", "MAIN", b"\0" * 1000)]))
    check = hcv.check_ce_installer(str(other))
    assert "isn't the Halo Custom Edition installer" in check.problem
    with pytest.raises(hcv.InstallerError, match="isn't the Halo Custom Edition installer"):
        hcv.extract_pc_maps_from_installer(str(other), str(tmp_path / "out"))
    text = tmp_path / "notes.exe"
    text.write_bytes(b"hello" * 1000)
    assert "isn't" in hcv.check_ce_installer(str(text)).problem
    # a cabinet without the maps
    cab = fakes.make_cab([("stored", [("setup.ini", b"x" * 100)])])
    program = tmp_path / "other.exe"
    program.write_bytes(fakes.make_pe([("CABFILE", "CAB1.CAB", cab)]))
    assert "isn't" in hcv.check_ce_installer(str(program)).problem


def test_cabinet_names_never_become_paths(tmp_path):
    """A cabinet with ..\\ names and the maps: only the three are written, at
    the tool's own paths."""
    program, maps = fakes.fake_installer(extra_files=[("..\\..\\evil.txt", b"evil"), ("/etc/evil", b"evil")])
    path = tmp_path / "setup.exe"
    path.write_bytes(program)
    out = tmp_path / "deep" / "out"
    hcv.extract_pc_maps_from_installer(str(path), str(out), log=lambda message: None)
    found = sorted(os.path.relpath(os.path.join(folder, name), tmp_path).replace(os.sep, "/")
                   for folder, _, names in os.walk(tmp_path) for name in names)
    assert found == ["deep/out/maps/bitmaps.map", "deep/out/maps/loc.map", "deep/out/maps/sounds.map",
                     "setup.exe"]


def test_truncated_installer(tmp_path):
    program, _ = fakes.fake_installer()
    path = tmp_path / "cut.exe"
    path.write_bytes(program[:len(program) // 2])
    check = hcv.check_ce_installer(str(path))
    assert check.problem
