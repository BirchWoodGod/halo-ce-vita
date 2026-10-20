#!/bin/bash
# A profile's save stopped dead at any step never loses the profile
# (insider.15, Oct 2026: a tester's profile came back "damaged", the defaults,
# after a crash; the Xbox wrote blam.sav in place). saved_game_files.c writes
# blam.sav.tmp, reads it back, moves a good blam.sav to blam.sav.bak, then
# names the .tmp blam.sav; the memory units looked at, a profile whose write
# stopped is made good again (saved_game_file_recover).
#
# With HALO_TEST_PROFILE's profile (never in the repository; skipped without),
# Menu style Halo 3: CUSTOMIZATION, the profile, COLOR right (WHITE: colour 0;
# the profile's is RANDOM, -1), Circle saves - the save stopped dead
# (HALO_TEST_PROFILE_WRITE_CRASH, a debug build's _exit) after each step:
#
#   tmp     the new file written and checked: the next start keeps blam.sav
#           (the colour before), the .tmp dropped
#   bak     blam.sav moved to .bak: the next start takes the whole .tmp
#           ("profile restored from its unfinished save", colour 0)
#   rename  done: colour 0, the .bak the file before
#
# then, with no write: blam.sav of 0 bytes, cut short or of garbage beside a
# good .bak (a .tmp cut short too) - "profile restored from backup", never
# damaged; and no .bak: still damaged, as before (h3damaged). Each next start
# opens CUSTOMIZATION's editor on the profile and must show it whole.
#
#   run_profile_safety_test.sh
#   HALO_TEST_VITA         the Linux build (debug: the crash knob is not in --release)
#   HALO_TEST_DATA_MENUS   the maps with Halo PC's bitmaps.map and loc.map
#   HALO_TEST_PROFILE      a profile's blam.sav (its SaveMeta.xbx beside it)
#   HALO_TEST_OUT          the output folder (kept on failure or HALO_TEST_KEEP=1)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/profile_safety_test.XXXXXX")}
. "$here/test_out.sh"
test_out_begin "$out"
if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
	echo "SKIP: no HALO_TEST_PROFILE"; exit 0
fi
if [ ! -r "$menus_data/maps/bitmaps.map" ]; then
	echo "SKIP: no Halo PC bitmaps.map in $menus_data/maps (HALO_TEST_DATA_MENUS)"; exit 0
fi
mkdir -p "$out"
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
status=0
fail() { echo "FAIL (profile safety): $*"; status=1; }
id=869079979327

run() { # NAME SAVE SECONDS PAD [VAR=value...]
	local name=$1 save=$2 seconds=$3 pad=$4
	shift 4
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data"
	ln -sfn "$(cd "$menus_data" && pwd)/maps" "$out/$name/data/maps"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 HALO_MENU_STYLE=halo3 HALO_SCENE_PACKS=0 "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 90)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}
# (a save folder with the test profile)
fresh_save() { # DIR
	rm -rf "$1"
	mkdir -p "$1/u/UDATA/$id"
	cp "$HALO_TEST_PROFILE" "$1/u/UDATA/$id/blam.sav"
	cp "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" "$1/u/UDATA/$id/"
}
d="down:150:600"
open_editor="wait:150:3000 $d $d $d $d $d a wait:150:3000 a wait:150:4000"
# (the editor's first line of the profile: "editing <name>: color <c>, ...")
editing() { grep -a "menus: h3 customize: editing" "$out/$1/run.log" | head -1 | sed 's/.*editing //'; }
# a next start on that save: the profile whole, in colour WANT
check_start() { # NAME SAVE WANT_COLOR [restored|backup|none]
	local name=$1 save=$2 want=$3 how=$4 log
	run "$name" "$save" 20 "$open_editor"
	log=$out/$name/run.log
	echo "$name: $(editing "$name")"
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name: exit $(cat "$out/$name/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name: an assertion, exception or halt"
	grep -aq "menus: h3 customize: editing birch: color $want," "$log" ||
		fail "$name: the editor did not open on birch in colour $want ($(editing "$name"))"
	grep -aq "is damaged" "$log" && fail "$name: the profile was damaged"
	grep -aq "checksum failed on player profile file" "$out/$name/data/debug.txt" && fail "$name: the profile's checksum failed"
	case $how in
	restored) grep -aq "profile restored from its unfinished save" "$log" || fail "$name: not restored from the .tmp" ;;
	backup) grep -aq "profile restored from backup" "$log" || fail "$name: not restored from the .bak" ;;
	none) grep -aq "profile restored" "$log" && fail "$name: restored, with nothing to restore" ;;
	esac
	[ -e "$save/u/UDATA/$id/blam.sav.tmp" ] && fail "$name: blam.sav.tmp left behind"
	[ "$(stat -c %s "$save/u/UDATA/$id/blam.sav" 2>/dev/null)" = 512 ] || fail "$name: blam.sav not 512 bytes"
}

# (COLOR right: WHITE, colour 0; Circle saves)
save_pad="$open_editor $d right:150:800 b wait:150:4000"
for stage in tmp bak rename; do
	save=$out/save_$stage
	fresh_save "$save"
	run "crash_$stage" "$save" 25 "$save_pad" HALO_TEST_PROFILE_WRITE_CRASH=$stage
	log=$out/crash_$stage/run.log
	echo "--- crash after $stage: exit $(cat "$out/crash_$stage/exit"); files: $(LC_ALL=C ls "$save/u/UDATA/$id" | tr '\n' ' ')"
	[ "$(cat "$out/crash_$stage/exit")" = 86 ] || fail "crash_$stage: the save did not stop dead after $stage (exit $(cat "$out/crash_$stage/exit"))"
	grep -aq "profile write: stopped dead after its $stage step" "$log" || fail "crash_$stage: no save stopped after $stage"
	files=$(LC_ALL=C ls "$save/u/UDATA/$id" | tr '\n' ' ')
	case $stage in
	tmp) [ "$files" = "SaveMeta.xbx blam.sav blam.sav.tmp " ] || fail "crash_tmp: files $files"
		cmp -s "$HALO_TEST_PROFILE" "$save/u/UDATA/$id/blam.sav" || fail "crash_tmp: blam.sav changed before its .tmp was named"
		check_start "after_$stage" "$save" -1 none ;;
	bak) [ "$files" = "SaveMeta.xbx blam.sav.bak blam.sav.tmp " ] || fail "crash_bak: files $files"
		check_start "after_$stage" "$save" 0 restored ;;
	rename) [ "$files" = "SaveMeta.xbx blam.sav blam.sav.bak " ] || fail "crash_rename: files $files"
		check_start "after_$stage" "$save" 0 none ;;
	esac
	cmp -s "$HALO_TEST_PROFILE" "$save/u/UDATA/$id/blam.sav.bak" 2>/dev/null || [ $stage = tmp ] ||
		fail "after_$stage: blam.sav.bak is not the profile before the save"
done

# (the save in full, no crash: colour 0, the .bak the file before, no .tmp)
save=$out/save_whole
fresh_save "$save"
run whole "$save" 25 "$save_pad"
echo "--- a whole save: files $(LC_ALL=C ls "$save/u/UDATA/$id" | tr '\n' ' ')"
grep -aq "menus: h3 customize: saved birch: color 0," "$out/whole/run.log" || fail "whole: the save was not made"
cmp -s "$HALO_TEST_PROFILE" "$save/u/UDATA/$id/blam.sav.bak" || fail "whole: blam.sav.bak is not the file before"
check_start after_whole "$save" 0 none

# (a damaged blam.sav beside a good .bak, as a write cut by a power cut in place left it)
for damage in empty short garbage tmpshort; do
	save=$out/save_$damage
	fresh_save "$save"
	mv "$save/u/UDATA/$id/blam.sav" "$save/u/UDATA/$id/blam.sav.bak"
	case $damage in
	empty) : > "$save/u/UDATA/$id/blam.sav" ;;
	short) head -c 100 "$HALO_TEST_PROFILE" > "$save/u/UDATA/$id/blam.sav" ;;
	garbage) head -c 512 /dev/urandom > "$save/u/UDATA/$id/blam.sav" ;;
	tmpshort) head -c 300 "$HALO_TEST_PROFILE" > "$save/u/UDATA/$id/blam.sav.tmp" ;;
	esac
	echo "--- $damage"
	check_start "damaged_$damage" "$save" -1 backup
	cmp -s "$HALO_TEST_PROFILE" "$save/u/UDATA/$id/blam.sav" || fail "damaged_$damage: blam.sav not the .bak's copy"
done
# (no good copy at all: damaged, as before)
save=$out/save_nobackup
fresh_save "$save"
: > "$save/u/UDATA/$id/blam.sav"
run nobackup "$save" 20 "wait:150:3000 a wait:150:3000"
echo "--- no backup: $(grep -a "menus: h3 profiles: .*damaged" "$out/nobackup/run.log" | head -1)"
grep -aq "(damaged)" "$out/nobackup/run.log" || fail "nobackup: a 0-byte blam.sav with no backup was not damaged"
grep -aq "profile restored" "$out/nobackup/run.log" && fail "nobackup: restored from nothing"

[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
