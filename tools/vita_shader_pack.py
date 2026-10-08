#!/usr/bin/env python3
"""Make the Vita's shipped shader programs (port/vita/app0/shaders.pak).

The Vita's renderer makes its GPU programs as Cg from the game's combiner
and vertex program states and compiles them on the device with SceShaccCg,
which takes 0.6-1.5 s a program on the hardware: every first visit to an
area froze for that long. The pack holds those programs compiled ahead, so
the device compiles only what it misses (port/vita/host/vita_gxm.c reads it
whole at start-up and looks programs up by the hash of their Cg).

How the programs are gathered:

1. Collect the sources: run the Linux build with the Vita's device
   (configure.py --linux-d3d gxm-null) through the levels with
   HALO_SHADER_COLLECT=<directory>; each program's Cg is written there as
   <hash>.vp.cg / <hash>.fp.cg, the hash being the one the Vita keys its
   programs by (the Cg is the same text on every platform). The campaign:
   init.txt's level with HALO_SHADER_TOUR=5 and HALO_TEST_INPUT=bot:<n>;
   the multiplayer maps: HALO_NETWORK_TEST=local:<map>:<variant> (a host
   alone never starts its game) with the tour, which then walks the
   starting locations; the menus: no init.txt, HALO_TEST_PAD.
2. Compile them with the device's own compiler: copy the directory to
   ux0:data/haloce-vita/ on Vita3K (whose libshacccg.suprx is the device's
   module) and start the Vita build once with
   HALO_SHADER_PRECOMPILE=ux0:data/haloce-vita/<directory>: every source is
   compiled into the memory card's cache, ux0:data/haloce-vita/shaders.
   HALO_SHADER_COLLECT on that run adds the renderer's built-in programs.
   At start-up the game's heap is still small, so one run compiles them all
   (265 in 32 s on Vita3K, the compiler's heap at ~12 MB); later in a game
   SceShaccCg runs out of heap after 50-110 compiles (so the programs the
   precompiling run itself then draws, some with texture stages missing, are
   not sources to add). Any program a clean-cache run still compiles in the
   background ("compiled in the background" in the log) is a source to
   collect and add.
3. Pack them: vita_shader_pack.py --sources <directory> --programs
   <the cache directory> [--output port/vita/app0/shaders.pak].

The cache's files and the pack carry the compile id and the generator id of
the build that made them (port/vita/include/vita_shader_cache.h has the
formats). The Vita uses a pack only if its compile id is its own; every
program in it is bound to its Cg's hash and to a checksum of its bytes.
The build (tools/vita_build.py) puts in the VPK a copy of the pack stamped
with the build's generator id (--stamp <pack> --generator-header <header>
--output <copy>), made again whenever that id changes, so the pack in a VPK
always names its eboot's generator; the device matches programs by their
Cg's hash anyway. When the pack in the tree was made for another generator
(the Cg generators changed since), the stamp says so at build time: collect
the sources again and check them (--check <pack> --sources <directory>: how
many of them the pack has), or make the pack again.
--repack <old pack> --compile-id <hex> rewrites a pack of the first format
("HCEVSHP1", raw programs) in this one, its generator id left 0 (unknown):
only for programs compiled with the compiler settings that compile id names
(the build logs "gxm: shader ids: compile <hex>, generator <hex>").
"""

import argparse
import re
import struct
import sys
from pathlib import Path

SOURCE = re.compile(r"([0-9a-f]{16})\.(vp|fp)\.cg")
CACHE_MAGIC = b"HCEVSHC2"
PACK_MAGIC = b"HCEVSHP2"


def source_hash(text: bytes, fragment: bool) -> int:
    """vita_shader_cache.c's vshc_source_hash: FNV-1a 64 over the Cg, the
    basis xored with the kind"""
    value = 14695981039346656037 ^ int(fragment)
    for byte in text:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def checksum(data: bytes) -> int:
    """vshc_checksum: FNV-1a 32"""
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def gxp_whole(data: bytes) -> bool:
    return len(data) >= 16 and data[:4] == b"GXP\0" and struct.unpack_from("<I", data, 8)[0] == len(data)


def read_cache_file(path: Path, value: int):
    """(compile id, generator id, program) of a cache file, checked as the Vita does"""
    data = path.read_bytes()
    if len(data) < 40 or data[:8] != CACHE_MAGIC:
        sys.exit(f"{path}: not a cache file of this format (an older build's? run the build again on a clean cache)")
    compile_id, generator_id, hash_value, size, crc = struct.unpack_from("<QQQII", data, 8)
    program = data[40:]
    if hash_value != value or size != len(program) or checksum(program) != crc or not gxp_whole(program):
        sys.exit(f"{path}: damaged, or not the program of its name")
    return compile_id, generator_id, program


def read_old_pack(path: Path):
    """{hash: program} of a pack of the first format"""
    data = path.read_bytes()
    if data[:8] != b"HCEVSHP1":
        sys.exit(f"{path}: not a pack of the first format")
    count = struct.unpack_from("<I", data, 8)[0]
    programs = {}
    for index in range(count):
        value, offset, size = struct.unpack_from("<QII", data, 16 + 16 * index)
        program = data[offset:offset + size]
        if not gxp_whole(program):
            sys.exit(f"{path}: program {value:016x} is not whole")
        programs[value] = program
    return programs


def write_pack(output: Path, programs, compile_id: int, generator_id: int) -> int:
    header = struct.pack("<8sIIQQ", PACK_MAGIC, len(programs), 0, compile_id, generator_id)
    offset = len(header) + 24 * len(programs)
    index = b""
    blobs = b""
    for value, data in sorted(programs.items()):
        offset_aligned = (offset + 15) & ~15
        blobs += b"\0" * (offset_aligned - offset) + data
        index += struct.pack("<QIIII", value, offset_aligned, len(data), checksum(data), 0)
        offset = offset_aligned + len(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + index + blobs)
    return len(header) + len(index) + len(blobs)


def read_pack(path: Path):
    """(header, {hash: (offset, size)}) of a pack of this format, its index checked"""
    data = path.read_bytes()
    if len(data) < 32 or data[:8] != PACK_MAGIC:
        sys.exit(f"{path}: not a pack of this format")
    count = struct.unpack_from("<I", data, 8)[0]
    entries = {}
    for index in range(count):
        value, offset, size, crc, _ = struct.unpack_from("<QIIII", data, 32 + 24 * index)
        if offset + size > len(data) or checksum(data[offset:offset + size]) != crc:
            sys.exit(f"{path}: program {value:016x} is damaged")
        entries[value] = (offset, size)
    return data, entries


def header_generator_id(path: Path) -> int:
    """VSHC_GENERATOR_ID from the header tools/vita_shader_generator_id.py wrote"""
    match = re.search(r"#define VSHC_GENERATOR_ID 0x([0-9a-f]{16})ULL", path.read_text())
    if not match:
        sys.exit(f"{path}: no VSHC_GENERATOR_ID")
    return int(match.group(1), 16)


def stamp(pack: Path, generator_id: int, output: Path) -> None:
    data, entries = read_pack(pack)
    made_for = struct.unpack_from("<Q", data, 24)[0]
    if made_for != generator_id:
        print(f"note: {pack} was made for Cg generator {made_for:016x}, this build's is {generator_id:016x}: "
              "programs whose Cg changed are compiled on the device; collect the sources again and check them "
              "(tools/vita_shader_pack.py --check), or make the pack again", file=sys.stderr)
    stamped = data[:24] + struct.pack("<Q", generator_id) + data[32:]
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_bytes() != stamped:
        output.write_bytes(stamped)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sources", type=Path, action="append",
                        help="a directory of collected <hash>.vp.cg / <hash>.fp.cg (repeatable)")
    parser.add_argument("--programs", type=Path,
                        help="the compiled programs, <hash>.gxp (the Vita's shader cache)")
    parser.add_argument("--repack", type=Path, help="a pack of the first format to rewrite")
    parser.add_argument("--compile-id", help="with --repack: the compile id its programs were made with (hex)")
    parser.add_argument("--output", type=Path, default=Path("port/vita/app0/shaders.pak"))
    parser.add_argument("--stamp", type=Path, help="a pack to copy to --output with this build's generator id")
    parser.add_argument("--generator-header", type=Path,
                        help="with --stamp: the build's vita_shader_generator_id.h (default: the id of the tree)")
    parser.add_argument("--check", type=Path, help="a pack to check against --sources (how many it has)")
    args = parser.parse_args()

    if args.stamp:
        if args.generator_header:
            generator_id = header_generator_id(args.generator_header)
        else:
            from vita_shader_generator_id import generator_id as tree_generator_id
            generator_id = tree_generator_id()
        stamp(args.stamp, generator_id, args.output)
        return
    if args.check:
        _, entries = read_pack(args.check)
        if not args.sources:
            sys.exit("--check needs --sources")
        have, lacking = 0, []
        for directory in args.sources:
            for path in sorted(directory.iterdir()):
                match = SOURCE.fullmatch(path.name)
                if not match:
                    continue
                if source_hash(path.read_bytes(), match.group(2) == "fp") != int(match.group(1), 16):
                    sys.exit(f"{path}: its hash is not its name")
                if int(match.group(1), 16) in entries:
                    have += 1
                else:
                    lacking.append(path.name)
        print(f"{args.check}: {have} of {have + len(lacking)} sources in the pack"
              + (f"; lacking: {' '.join(lacking)}" if lacking else ""))
        sys.exit(1 if lacking else 0)

    if args.repack:
        if not args.compile_id:
            sys.exit("--repack needs --compile-id")
        programs = read_old_pack(args.repack)
        size = write_pack(args.output, programs, int(args.compile_id, 16), 0)
        print(f"{args.output}: {len(programs)} programs rewritten, {size} bytes")
        return
    if not args.sources or not args.programs:
        sys.exit("--sources and --programs (or --repack)")
    hashes = {}
    for directory in args.sources:
        for path in sorted(directory.iterdir()):
            match = SOURCE.fullmatch(path.name)
            if not match:
                continue
            value = int(match.group(1), 16)
            if source_hash(path.read_bytes(), match.group(2) == "fp") != value:
                sys.exit(f"{path}: its hash is not its name (edited, or written by an older build?)")
            hashes[value] = path
    programs = {}
    ids = set()
    missing = []
    for value in sorted(hashes):
        path = args.programs / f"{value:016x}.gxp"
        if not path.is_file():
            missing.append(hashes[value].name)
            continue
        compile_id, generator_id, program = read_cache_file(path, value)
        ids.add((compile_id, generator_id))
        programs[value] = program
    if len(ids) > 1:
        sys.exit(f"the programs were made by {len(ids)} different builds: clean the cache and compile them again")
    if missing:
        print(f"{len(missing)} sources have no compiled program (left out): {' '.join(missing[:8])}"
              + (" ..." if len(missing) > 8 else ""), file=sys.stderr)
    compile_id, generator_id = ids.pop() if ids else (0, 0)
    size = write_pack(args.output, programs, compile_id, generator_id)
    vertex = sum(1 for value in programs if hashes[value].name.endswith(".vp.cg"))
    print(f"{args.output}: {len(programs)} programs ({vertex} vertex, {len(programs) - vertex} fragment), "
          f"{size} bytes, compile id {compile_id:016x}, generator id {generator_id:016x}")


if __name__ == "__main__":
    main()
