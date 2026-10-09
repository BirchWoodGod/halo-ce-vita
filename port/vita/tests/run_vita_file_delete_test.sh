#!/bin/bash
# Deleting files as on the Vita, on the Linux harness (any build of this
# tree), with chmod refused (EINVAL) as the Vita's newlib refuses it (a
# 32-bit LD_PRELOAD built here), every press scripted (HALO_TEST_PAD), the
# screens named in the log (HALO_UI_LOG). Two starts on one save folder:
#
#   create   a first start: New Campaign makes a profile (saved as the
#            Xbox saves one, u/UDATA/<id>/blam.sav) and the default
#            playlists are written
#   delete   the next start: the default playlists are deleted and written
#            again (file_delete: before the fix every one failed with error
#            0x57, as on the owner's Vitas), then Settings opens the
#            profiles, X asks to delete the profile and A deletes it: its
#            folder is gone, and nothing about it failed
#
# Each must log no assertion or exception, and exit by itself.
#
#   run_vita_file_delete_test.sh
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them; the harness copy is removed)
#   HALO_TEST_CC     a compiler for the 32-bit preload (default clang)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_file_delete_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
rm -rf "$out/data" "$out/save" "$out/bin"
mkdir -p "$out/data" "$out/save" "$out/bin"
ln -sfn "$(cd "$data" && pwd)/maps" "$out/data/maps"
cp "$binary" "$out/bin/halo"
cat > "$out/bin/nochmod.c" <<'EOF'
/* the Vita's chmod: refused */
#include <errno.h>
#include <sys/types.h>
int chmod(const char *path, mode_t mode) { (void)path; (void)mode; errno = EINVAL; return -1; }
EOF
if ! ${HALO_TEST_CC:-clang} -m32 -shared -fPIC -O1 "$out/bin/nochmod.c" -o "$out/bin/nochmod.so"; then
	echo "cannot build the 32-bit chmod preload (HALO_TEST_CC)"
	exit 2
fi

run() { # NAME SECONDS PAD
	local name=$1 seconds=$2 pad=$3
	(cd "$out" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/data" \
		HALO_SAVE_ROOT="$out/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 "HALO_TEST_PAD=$pad" LD_PRELOAD="$out/bin/nochmod.so" \
		timeout -k 5 $((seconds + 90)) "$out/bin/halo" > "$out/$name.log" 2>&1)
	echo $? > "$out/$name.exit"
	cp "$out/data/debug.txt" "$out/$name.debug.txt" 2>/dev/null
	rm -f "$out/data/debug.txt"
}

check() { # NAME
	local name=$1
	echo "--- $name"
	grep -aE "ui: screen" "$out/$name.log" | head -12
	[ "$(cat "$out/$name.exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name.exit")"
	grep -aqiE "assert|exception|halt" "$out/$name.log" && fail "$name" "an assertion, exception or halt"
	if grep -aq "file_delete(" "$out/$name.debug.txt"; then
		fail "$name" "$(grep -ac 'file_delete(' "$out/$name.debug.txt") file_delete errors, the first: $(grep -a -m1 'file_delete(' "$out/$name.debug.txt")"
	fi
}

profiles() { find "$out/save/u/UDATA" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l; }

# (the presses start a second after the main menu is up; New Campaign has
# the focus with no profile; A makes one, A again saves it)
run create 30 "wait:150:8000 a:150:3000 a:150:3000"
check create
[ "$(profiles)" = 1 ] || fail create "$(profiles) profiles saved, not 1"
[ "$(find "$out/save/z/saved/playlists/default_playlist" -name blam.lst | wc -l)" = 26 ] ||
	fail create "not the 26 default playlists"

# (Load Campaign has the focus with a profile: down, down is Settings, which
# opens the profiles; X asks to delete the one chosen, A says yes)
run delete 35 "wait:150:8000 down:150:1500 down:150:1500 a:150:3000 x:150:3000 a:150:3000"
check delete
grep -aq "ui: screen ui.shell.error.confirm_delete_profile_modal" "$out/delete.log" ||
	fail delete "the profile's delete was not asked"
[ "$(profiles)" = 0 ] || fail delete "the profile is still there: $(find "$out/save/u/UDATA" -mindepth 1 -maxdepth 1)"
grep -aqE "player_profile_delete\(\) failed|XDeleteSaveGame\(\) failed|remove_nth_entry_in_mapfile\(\) failed" \
	"$out/delete.debug.txt" && fail delete "the profile's delete failed: $(grep -a -m1 'failed' "$out/delete.debug.txt")"
[ "$(find "$out/save/z/saved/playlists/default_playlist" -name blam.lst | wc -l)" = 26 ] ||
	fail delete "not the 26 default playlists, written again"

rm -rf "$out/bin"
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
