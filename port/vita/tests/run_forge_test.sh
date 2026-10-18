#!/bin/bash
# Forge's sandbox, a SPIKE (port/linux/game/forge.c; the design:
# docs/superpowers/specs/2026-10-18-forge-design.md): the console's "forge"
# commands, through HALO_TEST_COMMANDS, in a loaded level on the Linux
# harness (the desktop GL build, for its screenshots):
#
#   bloodgulch  monitor mode on: the flying camera moves by the monitor's
#               controls ("forge stick", "forge press"); a Warthog spawned,
#               turned, carried and dropped (it falls); a rocket launcher
#               and a teleporter spawned; the teleporter grabbed, moved and
#               deleted; the palette stepped and spawned from with the
#               controller's buttons; monitor mode off by its button (the
#               player back, at the camera)
#   b30         the beach: AI characters spawned (a Hunter, a marine, an
#               Elite with a sword: each given a free actor, ai_attach_free)
#               and a Ghost; they fight
#   off         no "forge" command: no "forge:" line at all
#
# Each must exit by itself, with no assertion or exception. The screenshots
# (HALO_SCREENSHOT_DIR, each run's shots folder) are kept with
# HALO_TEST_KEEP=1.
#
#   run_forge_test.sh [CASE...]      (default: bloodgulch b30 off)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA        a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test
#                         made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_forge_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-bloodgulch b30 off}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi

# run NAME SECONDS INIT COMMANDS: the harness on the map init.txt loads
run() {
	local name=$1 seconds=$2 init=$3 commands=$4
	local dir="$out/$name" file
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save" "$dir/shots"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy HALO_DATA_ROOT="$dir/data" HALO_SAVE_ROOT="$dir/save" \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
		HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_SCREENSHOT_DIR="$dir/shots" HALO_TEST_COMMANDS="$commands" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

checked() {
	local log="$out/$1/run.log" code
	code=$(cat "$out/$1/exit" 2>/dev/null)
	if [ "$code" != 0 ]; then
		fail "$1: the harness exited with $code ($log)"
		return 1
	fi
	if grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
		fail "$1: an assertion or exception ($log)"
		return 1
	fi
	return 0
}

# expect NAME TEXT PATTERN: a line of the log matches PATTERN (grep -E)
expect() {
	if grep -aqE "$3" "$out/$1/run.log"; then
		echo "  ok: $1: $2"
	else
		fail "$1: $2 ($out/$1/run.log)"
	fi
}

bloodgulch='game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch'
b30='map_name levels\\b30\\b30'

for c in $cases; do
	case $c in
	bloodgulch)
		run "$c" 40 "$bloodgulch" "L280:@shot 00-player;L300:forge on;L301:forge where;L305:forge list;L310:@shot 01-monitor;L320:forge stick 0 1 0 0 30;L325:forge press a 20;L360:forge where;L362:@shot 02-flown;L380:forge spawn warthog;L382:forge where;L385:@shot 03-spawned-warthog;L400:forge turn 90;L402:forge where;L405:@shot 04-turned;L410:forge stick 0 0 0.5 0 20;L440:forge where;L442:@shot 05-carried;L450:forge drop;L500:forge where;L502:@shot 06-dropped;L510:forge spawn rocket launcher;L515:forge drop;L520:forge stick 0 0 -0.5 0 20;L545:forge spawn teleporter_base;L548:forge drop;L560:@shot 07-more;L570:forge where;L575:forge grab;L580:forge stick 0 0 0 0.4 15;L600:forge where;L602:@shot 08-grabbed-moved;L610:forge drop;L620:forge delete;L625:forge where;L628:@shot 09-deleted;L640:forge press x;L650:forge drop;L660:forge press black;L670:forge press x;L680:forge press rt;L690:forge where;L700:forge press rs;L705:@shot 10-player-again;L710:forge where" & ;;
	b30)
		run "$c" 80 "$b30" "L60:@skip;L205:(set cheat_deathless_player true);L1200:@shot 00-b30-player;L1210:forge on;L1220:forge camera -63 39 3.2 -50 -22;L1240:@shot 01-b30-monitor;L1250:forge spawn hunter;L1252:forge distance 4;L1258:@shot 02-b30-hunter-held;L1260:forge drop;L1262:forge stick 0 0 0.25 0 12;L1280:forge spawn marine_armored assault rifle major;L1282:forge distance 3.5;L1285:forge drop;L1287:forge stick 0 0 -0.5 0 12;L1300:forge spawn ghost;L1302:forge distance 4;L1303:forge turn 90;L1305:forge drop;L1307:forge stick 0 0 0.25 0 12;L1320:forge spawn elite commander;L1322:forge distance 5;L1325:forge drop;L1340:@shot 03-b30-spawned;L1420:@shot 04-b30-ai-1;L1520:@shot 05-b30-ai-2;L1620:@shot 06-b30-ai-3;L1630:forge where;L1700:forge off;L1705:@shot 07-b30-player" & ;;
	off)
		run "$c" 25 "$bloodgulch" "L300:@shot 00-off" & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
done
wait

for c in $cases; do
	checked "$c" || continue
	case $c in
	bloodgulch)
		expect "$c" "monitor mode on, the palette built" "forge: Monitor mode: [1-9][0-9]* objects in the palette"
		# (the camera starts where the player's eyes are; flown forward and up)
		first=$(grep -a "forge: camera at" "$out/$c/run.log" | head -1 | awk '{print $5, $6, $7}')
		flown=$(grep -a "forge: camera at" "$out/$c/run.log" | sed -n 2p | awk '{print $5, $6, $7}')
		if [ -n "$first" ] && [ "$first" != "$flown" ]; then
			echo "  ok: $c: the camera flew from $first to $flown"
		else
			fail "$c: the camera did not move ($first, $flown)"
		fi
		expect "$c" "a Warthog spawned and held" "forge: Spawned warthog \\(vehicles\\)"
		expect "$c" "turned 90 degrees" "forge: turned warthog by 90 degrees"
		expect "$c" "dropped" "forge: Dropped warthog at"
		expect "$c" "a weapon and scenery spawned" "forge: Spawned teleporter_base \\(scenery\\)"
		expect "$c" "the object under the crosshair grabbed" "forge: Grabbed teleporter_base"
		expect "$c" "deleted" "forge: Deleted teleporter_base"
		expect "$c" "the controller's buttons: the palette stepped, spawned from" "forge: scenery: .*\\([0-9]+ of [0-9]+\\)"
		expect "$c" "monitor mode off by its button" "forge: Player mode \\([0-9]+ spawned, 1 deleted\\)" ;;
	b30)
		expect "$c" "a Hunter given a free actor" "forge: hunter given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "a marine given a free actor" "forge: marine_armored given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "an Elite given a free actor" "forge: elite special given a free actor \\([0-9a-f]{8}\\)"
		expect "$c" "a Ghost spawned" "forge: Spawned ghost \\(vehicles\\)"
		if grep -aq "given a free actor (ffffffff)" "$out/$c/run.log"; then
			fail "$c: a character got no actor"
		fi ;;
	off)
		if grep -aq "forge:" "$out/$c/run.log"; then
			fail "$c: a forge line with no forge command"
		else
			echo "  ok: $c: no forge line"
		fi ;;
	esac
done

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
