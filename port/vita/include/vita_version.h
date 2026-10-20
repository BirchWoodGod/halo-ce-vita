/*
The release this build is: the one place its version is set.

HALO_VITA_VERSION is the release's name, as its tag says without the v
(1.1.0-beta.1, 1.1.0): halo.log's first line has it (vita_main.c) and
the settings panel shows it (Controls > Advanced; vita_settings.c), so a
report says which build it came from.

HALO_VITA_APP_VER is the APP_VER of param.sfo, which the LiveArea and the
system show: "XX.YY", two digits each side, so a release's x.y.z is
"0x.yz" (1.0.3 was 01.03, 1.1.0 is 01.10) and its betas carry the
version they lead to (the 1.0.3 betas were 01.03, the 1.1.1 betas 01.11).
tools/vita_build.py reads it from here.

configure.py --vita-version sets HALO_VITA_VERSION for one build (an
insider build's, a candidate's), the Vita's and the PC builds' alike
(tools/linux_build.py's version_defines); tools/package_pc.py names the PC
files for it.
*/
#ifndef VITA_VERSION_H
#define VITA_VERSION_H

/* (a test may build as another version: run_vita_settings_test.sh) */
#ifndef HALO_VITA_VERSION
#define HALO_VITA_VERSION "1.1.1-beta.1"
#endif
#define HALO_VITA_APP_VER "01.11"

/* the version as a build shows it (halo.log, the main menu, the window):
a PC build's (Linux, Windows) with " (PC)" after it, "1.1.1-beta.1 (PC)",
so a report says which it came from; the release's files are named
halo-ce-vita-<version>-vita.vpk and -pc-windows.zip, -pc-linux.tar.gz
(tools/package_pc.py, tools/release/README.md) */
#if defined(HALO_VITA) || defined(__vita__)
#define HALO_VITA_VERSION_SHOWN HALO_VITA_VERSION
#else
#define HALO_VITA_VERSION_SHOWN HALO_VITA_VERSION " (PC)"
#endif

#endif
