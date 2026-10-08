# libmspack

Stuart Caie's library of Microsoft compression formats, in C, under the GNU
Lesser General Public License, version 2.1 (see `COPYING.LIB`; its authors
in `AUTHORS`). The LGPL 2.1 lets its code be used under the GPL instead
(its section 3), so it goes into this GPL-3.0-only program as the FSF's
license list says: compatible.

Upstream: https://github.com/kyz/libmspack, commit
55d501976171397ccd5d5a7a1ca7da065b1d9a06 (July 2026: release 0.11alpha, the
one cabextract 1.11 ships, and the fixes after it for UBSan and salvage
mode). Only the Microsoft Cabinet reader and its decompressors are copied,
unchanged, from its `libmspack/mspack/` directory: `cabd.c`, `cab.h`,
`lzxd.c`, `lzx.h`, `mszipd.c`, `mszip.h`, `qtmd.c`, `qtm.h`, `system.c`,
`system.h`, `macros.h`, `readbits.h`, `readhuff.h` and `mspack.h`, with
`COPYING.LIB` and `AUTHORS` from `libmspack/`. Its `config.h` is replaced by
`-DHAVE_INTTYPES_H=1` (`tools/linux_build.py`'s `LIBMSPACK_FLAGS`).

The game reads Halo Custom Edition's bitmaps.map, sounds.map and loc.map
out of the player's own Custom Edition installer with it
(`port/linux/src/posix_ce_installer.c`, on the Vita
`port/vita/host/vita_ce_installer.c`, on a PC `tools/ce_installer_extract.c`):
the installer's resources hold a cabinet of LZX-compressed folders. It
builds into the Linux and Vita builds, with the host's C library.
`port/vita/tests/run_ce_installer_test.sh` fuzzes it.
