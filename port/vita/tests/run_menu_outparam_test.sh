#!/bin/bash
# The menus' calls into the port's p2p code must pass each out parameter the type it is declared
# with (insider.21, BlazeRed's crashes, Oct 22 2026): h3_pregame_update passed a boolean's address
# (one byte) to p2p_lobby_hosting_listed(int *has_password), which wrote four, over the locals beside
# it on every frame of an online host's lobby (AddressSanitizer: stack-buffer-overflow, h3liveonline).
# The game's build turns the compiler's incompatible-pointer-types errors into nothing
# (-Wno-error=incompatible-pointer-types), so this compiles the menus and p2p callers once more with
# them as errors and fails on any that names a p2p.h declaration. Needs a configured tree
# (build.ninja) and a clang that targets i686; SKIP without.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root" || exit 1
if [ ! -f build.ninja ] || ! command -v ninja > /dev/null || ! command -v clang > /dev/null; then
	echo "SKIP: no configured tree (build.ninja), ninja or clang"
	exit 0
fi
status=0
for source in port/linux/game/menu_functions.c port/linux/game/coop_menu.c; do
	object=build/linux/obj/${source%.c}.o
	cmd=$(ninja -t commands "$object" 2> /dev/null | grep -F -- " -c $source" | tail -1)
	if [ -z "$cmd" ]; then
		echo "SKIP: $source has no compile command (not in this build)"
		continue
	fi
	# (the configured compiler may be a wrapper or a sanitizer build; the checks are the same)
	cmd=${cmd#* }
	cmd="clang $cmd"
	cmd=${cmd// -w / }
	cmd=${cmd//-Wno-error=incompatible-pointer-types/}
	cmd=${cmd%% -o *}
	cmd=$(printf '%s' "$cmd" | sed 's/-MMD -MF [^ ]*//')
	out=$(eval "$cmd -fsyntax-only -Wincompatible-pointer-types" 2>&1 | grep -a "incompatible pointer types" -A4 | grep -a "p2p\.h\|p2p_lobby\|p2p_" )
	if [ -n "$out" ]; then
		echo "FAIL: $source passes a wrongly typed pointer to a p2p function:"
		echo "$out"
		status=1
	fi
done
[ $status = 0 ] && echo "PASS: the menus' calls into p2p pass the declared pointer types"
exit $status
