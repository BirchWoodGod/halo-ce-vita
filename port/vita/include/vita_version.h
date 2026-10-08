/*
The release this build is: the one place its version is set.

HALO_VITA_VERSION is the release's name, as its tag says without the v
(1.1.0-beta.1, 1.1.0): halo.log's first line has it (vita_main.c) and
the settings panel shows it (Controls > Advanced; vita_settings.c), so a
report says which build it came from.

HALO_VITA_APP_VER is the APP_VER of param.sfo, which the LiveArea and the
system show: "XX.YY", two digits each side, so a release's x.y.z is
"0x.yz" (1.0.3 was 01.03, 1.1.0 is 01.10) and its betas carry the
version they lead to (the 1.0.3 betas were 01.03). tools/vita_build.py
reads it from here.
*/
#ifndef VITA_VERSION_H
#define VITA_VERSION_H

#define HALO_VITA_VERSION "1.1.0-beta.2"
#define HALO_VITA_APP_VER "01.10"

#endif
