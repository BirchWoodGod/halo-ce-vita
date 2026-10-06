#!/usr/bin/env python3
"""The Vita's shader generator id (port/vita/include/vita_shader_cache.h).

A 64-bit FNV-1a hash of the sources that write the Cg the Vita compiles,
taken at build time (tools/vita_build.py runs this into a header that
port/vita/host/vita_gxm.c includes). The memory card's shader cache keeps
the id of the build that wrote it and is emptied when a build with another
id starts (issue #28), so no hand-bumped number has to remember it.

    vita_shader_generator_id.py --header <out.h>   the header
    vita_shader_generator_id.py                    the id, in hex
"""

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# the Cg generators and the uniform layout they write against
SOURCES = (
    "port/vita/platform/nv2a_psh_cg.c",
    "port/vita/platform/nv2a_vsh_cg.c",
    "port/vita/include/vita_xgpu.h",
)


def generator_id(root: Path = ROOT) -> int:
    value = 14695981039346656037
    for name in SOURCES:
        for byte in name.encode() + b"\0" + (root / name).read_bytes().replace(b"\r\n", b"\n") + b"\0":
            value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    value = generator_id()
    if not args.header:
        print(f"{value:016x}")
        return
    text = ("/* made by tools/vita_shader_generator_id.py: a hash of " + ", ".join(SOURCES) + " */\n"
            f"#define VSHC_GENERATOR_ID 0x{value:016x}ULL\n")
    args.header.parent.mkdir(parents=True, exist_ok=True)
    if not args.header.is_file() or args.header.read_text() != text:
        args.header.write_text(text)


if __name__ == "__main__":
    main()
