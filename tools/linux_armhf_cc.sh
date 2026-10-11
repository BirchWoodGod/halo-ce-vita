#!/bin/bash
# The Linux build retargeted to 32-bit ARM (armhf: armv7-a, hard float), for
# a Raspberry Pi: `python3 configure.py --linux-cc tools/linux_armhf_cc.sh
# --portable --release --lto off --pgo off`, then `ninja linux-server` (the
# dedicated server, port/linux/DEDICATED_SERVER.md). It stands in for clang in
# the build graph's i686 commands: the x86-only flags are dropped and the
# target is ARM's.
#
# On the Pi itself (a 32-bit Raspberry Pi OS with clang, lld and libsdl3-dev)
# nothing needs setting. To cross-compile from a PC:
#   ARM_GNU_TOOLCHAIN  Arm's GNU toolchain for arm-none-linux-gnueabihf (the
#                      folder holding bin/ and arm-none-linux-gnueabihf/libc;
#                      its glibc no newer than the Pi's)
#   SDL3_ARMHF         SDL3 built with it as a static library (the folder
#                      holding include/ and lib/libSDL3.a), linked in whole
# or, in place of Arm's toolchain (the release packages, tools/package_server.py):
#   ARMHF_SYSROOT      a Debian armhf sysroot (glibc, kernel headers, libgcc),
#                      whose glibc the program then needs at most
# and always:
#   ARMHF_CPU          the processor to tune for (default cortex-a72: the Pi 4)
#   CLANG              the clang to run (default clang)
set -e
args=()
link=1
target=arm-linux-gnueabihf
[ -n "${ARM_GNU_TOOLCHAIN:-}" ] && target=arm-none-linux-gnueabihf
for argument in "$@"; do
	case "$argument" in
	--target=i686-linux-gnu) args+=(--target=$target) ;;
	-m32 | -malign-double | -freg-struct-return | -march=native | -march=x86-64 | -mtune=native) ;;
	-c) link=0; args+=("$argument") ;;
	*) args+=("$argument") ;;
	esac
done
# (as the Vita's: the game's structures are packed as on the Xbox, so a float
# may sit at any address, which ARM's floating point loads do not take; char
# is signed, as on x86; the tag cache is relocated where the game's memory is)
extra=(-DHALO_RELOCATABLE_TAG_CACHE=1 -fsigned-char -fmax-type-align=1 -mcpu="${ARMHF_CPU:-cortex-a72}" -mfpu=neon
	-mfloat-abi=hard -mthumb -Qunused-arguments)
if [ -n "${ARM_GNU_TOOLCHAIN:-}" ]; then
	extra+=(--gcc-toolchain="$ARM_GNU_TOOLCHAIN" --sysroot="$ARM_GNU_TOOLCHAIN/arm-none-linux-gnueabihf/libc")
fi
[ -n "${ARMHF_SYSROOT:-}" ] && extra+=(--sysroot="$ARMHF_SYSROOT")
[ -n "${SDL3_ARMHF:-}" ] && extra+=(-I"$SDL3_ARMHF/include")
if [ $link = 1 ]; then
	extra+=(-fuse-ld=lld)
	[ -n "${SDL3_ARMHF:-}" ] && extra+=(-L"$SDL3_ARMHF/lib")
fi
exec "${CLANG:-clang}" "${extra[@]}" "${args[@]}"
