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
   programs by (the Cg is the same text on every platform).
2. Compile them with the device's own compiler: copy the directory to
   ux0:data/haloce-vita/ on Vita3K (whose libshacccg.suprx is the device's
   module) and start the Vita build once with
   HALO_SHADER_PRECOMPILE=ux0:data/haloce-vita/<directory>: every source is
   compiled into the memory card's cache, ux0:data/haloce-vita/shaders.
   HALO_SHADER_COLLECT on that run adds the renderer's built-in programs.
   At start-up the game's heap is still small, so one run compiles them all
   (265 in 32 s on Vita3K, the compiler's heap at ~12 MB); later in a game
   SceShaccCg runs out of heap after 50-110 compiles. Any program a clean-cache
   run still compiles in the background ("compiled in the background" in the
   log) is a source to collect and add.
3. Pack them: vita_shader_pack.py --sources <directory> --programs
   <the cache directory> [--output port/vita/app0/shaders.pak].

The format: "HCEVSHP1", the count (u32), 4 bytes of zero, then per program
its hash (u64), offset and size (u32 each), sorted by hash, then the
programs, each 16-byte aligned. Little endian.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

SOURCE = re.compile(r"([0-9a-f]{16})\.(vp|fp)\.cg")


def source_hash(text: bytes, fragment: bool) -> int:
    """vita_gxm.c's source_hash: FNV-1a 64 over the Cg, the basis xored with
    the kind"""
    value = 14695981039346656037 ^ int(fragment)
    for byte in text:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sources", type=Path, action="append", required=True,
                        help="a directory of collected <hash>.vp.cg / <hash>.fp.cg (repeatable)")
    parser.add_argument("--programs", type=Path, required=True,
                        help="the compiled programs, <hash>.gxp (the Vita's shader cache)")
    parser.add_argument("--output", type=Path, default=Path("port/vita/app0/shaders.pak"))
    args = parser.parse_args()

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
    programs = []
    missing = []
    for value in sorted(hashes):
        path = args.programs / f"{value:016x}.gxp"
        if not path.is_file():
            missing.append(hashes[value].name)
            continue
        data = path.read_bytes()
        if data[:4] != b"GXP\0" or struct.unpack_from("<I", data, 8)[0] != len(data):
            sys.exit(f"{path}: not a whole GXP program")
        programs.append((value, data))
    if missing:
        print(f"{len(missing)} sources have no compiled program (left out): {' '.join(missing[:8])}"
              + (" ..." if len(missing) > 8 else ""), file=sys.stderr)
    header = struct.pack("<8sII", b"HCEVSHP1", len(programs), 0)
    offset = len(header) + 16 * len(programs)
    index = b""
    blobs = b""
    for value, data in programs:
        offset_aligned = (offset + 15) & ~15
        blobs += b"\0" * (offset_aligned - offset) + data
        index += struct.pack("<QII", value, offset_aligned, len(data))
        offset = offset_aligned + len(data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + index + blobs)
    vertex = sum(1 for value, _ in programs if hashes[value].name.endswith(".vp.cg"))
    print(f"{args.output}: {len(programs)} programs ({vertex} vertex, {len(programs) - vertex} fragment), "
          f"{len(header) + len(index) + len(blobs)} bytes")


if __name__ == "__main__":
    main()
