#!/bin/bash
# Ragdolls (display.ragdolls, HALO_RAGDOLLS; the Vita's settings panel:
# Graphics > Detail > Ragdolls; port/linux/game/ragdolls.c): a dead body
# falls limp, a ragdoll made from its model's own nodes, simulated on the
# tick's thread, its pose drawn in place of the game's.
#
# On the Linux harness (gxm-null: the Vita's renderer) with the Vita's
# settings and the tick on its own thread, characters killed by the test
# commands "@ragdoll boom N R" (a frag grenade's explosion at the feet of
# each of the N living characters nearest the player within R world units)
# and "@ragdoll kill N R" (killed outright); "@ragdoll stats" logs what was
# made, slept, let go and what it cost, "@ragdoll check" whether any
# particle is in solid structure or not a number, and how far a ragdoll is
# from its body; HALO_RAGDOLLS_LOG=1 a line for each start, sleep and
# release:
#
#   b30         the beach's fight: grenades and kills; ragdolls made from
#               marines' and grunts' models, an explosion's thrown; four
#               awake at most, twelve kept at most; every one asleep (at
#               rest) 25 s later, none in solid structure, none not a number
#   b30-cap     one awake at most (HALO_RAGDOLLS_ACTIVE=1): never two
#   b30-revert  a checkpoint saved, bodies made ragdolls, the checkpoint
#               reverted: every ragdoll let go at once ("game state
#               replaced"), new ones made after it
#   b30-map     ragdolls, then another map (a10): every one let go, none
#               kept there
#   b30-off     the setting off: no ragdoll, no line
#   b30-live    off at the start, set on (@set, as the settings panel), the
#               kills make ragdolls; set off: every one let go
#   a10         the Pillar of Autumn: crewmen killed (the grunts of the
#               next structure, not loaded, get none: their bodies are in
#               solid structure)
#   bloodgulch  multiplayer: the player killed (@kill): its body a ragdoll
#
# Each run must exit by itself with no assertion or exception.
#
#   run_ragdolls_test.sh [CASE...]
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CPUS          the processors the runs are held to (taskset; default all)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ragdolls_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-b30 b30-cap b30-revert b30-map b30-off b30-live a10 bloodgulch}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
for map in a10 b30 bloodgulch; do
	if [ ! -e "$data/maps/$map.map" ]; then
		echo "no $map.map in $data/maps (HALO_TEST_DATA)"
		exit 2
	fi
done
pin=()
[ -n "${HALO_TEST_CPUS:-}" ] && pin=(taskset -c "$HALO_TEST_CPUS")

# run NAME SECONDS INIT COMMANDS [ENV...]: the harness on the map init.txt
# loads (INIT, printf's %b), with the Vita's settings (as triage's run.sh),
# the test commands run
run() {
	local name=$1 seconds=$2 init=$3 commands=$4
	shift 4
	local dir="$out/$name" file
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	printf '%b\n' "$init" > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_INTERPOLATION=false HALO_INTERPOLATE_FIRST_PERSON=1 HALO_STATIC_SCENERY=1 HALO_SCENERY_UPDATE_DIVISOR=4 \
		HALO_MIN_OBJECT_PIXELS=8 HALO_MODEL_LOD_SCALE=0.5 HALO_LIGHTING_REFRESH_DIVISOR=3 \
		HALO_SOUND_OBSTRUCTION_TICKS=3 HALO_RAGDOLLS_LOG=1 HALO_TEST_COMMANDS="$commands" "$@" \
		"${pin[@]}" timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	# (the save folder fills with the game state: only the log is kept)
	rm -rf "$dir/save"
}

# stats NAME TICK FIELD: a number from the "ragdolls stats:" line logged by
# the @ragdoll stats at that tick (FIELD: kept, awake, most_kept, most_awake,
# started, asleep, released, replaced, map, gone, setting, busy, ms, most_ms,
# vectors, nan, copied, read, steps, start_ms)
stats() {
	awk -v tick="$2" -v field="$3" '
		$0 ~ "test command at tick " tick ": @ragdoll stats$" { want = 1; next }
		want && /ragdolls stats:/ {
			line = $0
			sub(/.*ragdolls stats: /, "", line)
			n = split(line, words, /[ ,;()]+/)
			# (the numbers in their order on the line)
			count = 0
			for (i = 1; i <= n; i++)
				if (words[i] ~ /^[0-9.]+$/)
					numbers[++count] = words[i]
			split("kept awake most_kept most_awake cap started not_made asleep woken released replaced map gone alive setting busy ms most_ms vectors points fixed reverts nan copied read steps start_ms", names, " ")
			for (i = 1; i <= 27; i++)
				if (names[i] == field)
					print numbers[i]
			exit
		}
	' "$out/$1/run.log"
}

# check NAME TICK FIELD: from the "ragdolls check:" line of the @ragdoll
# check at that tick (kept, awake, particles, solid, nan, farthest, wrong,
# hinges)
check() {
	awk -v tick="$2" -v field="$3" '
		$0 ~ "test command at tick " tick ": @ragdoll check$" { want = 1; next }
		want && /ragdolls check: [0-9]+ kept/ {
			line = $0
			sub(/.*ragdolls check: /, "", line)
			n = split(line, words, /[ ,;()]+/)
			count = 0
			for (i = 1; i <= n; i++)
				if (words[i] ~ /^[0-9.]+$/)
					numbers[++count] = words[i]
			split("kept awake particles solid nan farthest wrong hinges", names, " ")
			for (i = 1; i <= 8; i++)
				if (names[i] == field)
					print numbers[i]
			exit
		}
	' "$out/$1/run.log"
}

# checked NAME: the run exited by itself, with no assertion or exception
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

# expect NAME TEXT CONDITION: CONDITION (a shell test) passed, or TEXT fails
expect() {
	local name=$1 text=$2
	shift 2
	if "$@"; then
		echo "  ok: $name: $text"
	else
		fail "$name: $text ($out/$name/run.log)"
	fi
}

gt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 > b + 0) }'; }
le() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 <= b + 0) }'; }

a10='map_name levels\\a10\\a10'
b30='map_name levels\\b30\\b30'
bloodgulch='game_variant slayer\nmap_name levels\\test\\bloodgulch\\bloodgulch'
# (b30: the insertion skipped; the Pelican lands and the marines and the
# beach's Covenant fight from about tick 900; the player deathless)
b30_setup='L60:@skip;L205:(set cheat_deathless_player true)'
# (a10: the opening skipped, the player taken out of the cryotube, the
# crewmen about it)
a10_out='L200:@skip;L250:(unit_exit_vehicle (player0))'
# (b30's fight: grenades at six, then six more killed outright; settled by
# tick 2400)
b30_fight='L1500:@ragdoll boom 6 40;L1505:@ragdoll stats;L1560:@ragdoll kill 6 40;L1565:@ragdoll stats;L1700:@ragdoll check;L2400:@ragdoll stats;L2400:@ragdoll check'

started=()
for c in $cases; do
	case $c in
	b30) run "$c" 95 "$b30" "$b30_setup;$b30_fight" & ;;
	b30-cap) run "$c" 75 "$b30" "$b30_setup;L1500:@ragdoll boom 6 40;L1560:@ragdoll kill 6 40;L1900:@ragdoll stats;L1900:@ragdoll check" \
		HALO_RAGDOLLS_ACTIVE=1 & ;;
	b30-revert)
		run "$c" 85 "$b30" "$b30_setup;L1400:game_save_totally_unsafe;L1500:@ragdoll kill 4 40;L1600:@ragdoll stats;L1650:game_revert;L1700:@ragdoll stats;L1700:@ragdoll check;L1760:@ragdoll boom 4 40;L1900:@ragdoll stats;L1900:@ragdoll check" & ;;
	b30-map)
		# (the game's time starts again on the next map: M runs there)
		run "$c" 85 "$b30" "$b30_setup;L1500:@ragdoll kill 4 40;L1600:@ragdoll stats;L1610:(map_name levels\\a10\\a10);M200:@ragdoll stats" & ;;
	b30-off) run "$c" 70 "$b30" "$b30_setup;L1500:@ragdoll boom 4 40;L1560:@ragdoll kill 4 40;L1800:@ragdoll stats" HALO_RAGDOLLS=0 & ;;
	b30-live)
		run "$c" 80 "$b30" "$b30_setup;L1400:@ragdoll kill 2 40;L1450:@ragdoll stats;L1460:@set HALO_RAGDOLLS 1;L1500:@ragdoll kill 4 40;L1600:@ragdoll stats;L1700:@set HALO_RAGDOLLS 0;L1720:@ragdoll stats;L1730:@ragdoll kill 2 40;L1800:@ragdoll stats" \
			HALO_RAGDOLLS=0 & ;;
	a10) run "$c" 60 "$a10" "$a10_out;L600:@ragdoll kill 4 30;L630:@ragdoll stats;L1300:@ragdoll stats;L1300:@ragdoll check" & ;;
	bloodgulch) run "$c" 50 "$bloodgulch" "L300:@kill 0;L310:@ragdoll stats;L700:@ragdoll stats;L700:@ragdoll check" & ;;
	*) echo "unknown case $c"; exit 2 ;;
	esac
	started+=("$c")
done
wait

for c in "${started[@]}"; do
	checked "$c" || continue
	log="$out/$c/run.log"
	case $c in
	b30)
		s1=$(stats "$c" 1565 started) a1=$(stats "$c" 1565 most_awake) k1=$(stats "$c" 1565 most_kept)
		expect "$c" "ragdolls made: $s1 by tick 1565 (grenades, then kills)" gt "${s1:-0}" 5
		expect "$c" "marines' and grunts' bodies, an explosion's thrown" \
			sh -c "grep -aq 'ragdolls: .*(marine.*started' '$log' && grep -aq 'ragdolls: .*(grunt) started' '$log' && grep -aq 'started in slot .*an explosion' '$log'"
		expect "$c" "four awake at most ($a1), twelve kept at most ($k1)" sh -c "[ '${a1:-9}' -le 4 ] && [ '${k1:-99}' -le 12 ]"
		expect "$c" "awake ones simulated: $(stats "$c" 1565 busy) busy ticks, $(stats "$c" 1565 vectors) collision vectors" \
			gt "$(stats "$c" 1565 vectors)" 100
		awake=$(stats "$c" 2400 awake) kept=$(stats "$c" 2400 kept)
		expect "$c" "settled: by tick 2400 $kept kept, $awake awake; some asleep at rest" \
			sh -c "[ '${awake:-9}' = 0 ] && [ '${kept:-0}' -gt 0 ] && grep -aq 'asleep: at rest' '$log'"
		for t in 1700 2400; do
			expect "$c" "tick $t: none in solid structure ($(check "$c" $t solid) of $(check "$c" $t particles) particles), none not a number ($(check "$c" $t nan)), within 150 m of its body (farthest $(check "$c" $t farthest)), no knee or elbow bent the wrong way ($(check "$c" $t wrong) of $(check "$c" $t hinges))" \
				sh -c "[ '$(check "$c" $t solid)' = 0 ] && [ '$(check "$c" $t nan)' = 0 ] && awk 'BEGIN { exit !($(check "$c" $t farthest) + 0 <= 50) }' && [ '$(check "$c" $t wrong)' = 0 ] && [ '$(check "$c" $t hinges)' -gt 0 ]"
		done
		expect "$c" "never not a number ($(stats "$c" 2400 nan))" test "$(stats "$c" 2400 nan)" = 0
		expect "$c" "drawn: $(stats "$c" 2400 copied) poses copied with the tick's for the render" gt "$(stats "$c" 2400 copied)" 100
		echo "  cost: $(stats "$c" 2400 busy) busy ticks, $(stats "$c" 2400 ms) ms a tick, at most $(stats "$c" 2400 most_ms) ms; $(stats "$c" 2400 steps) ragdoll steps, $(stats "$c" 2400 start_ms) ms a start (this machine)" ;;
	b30-cap)
		expect "$c" "one awake at most ($(stats "$c" 1900 most_awake)), $(stats "$c" 1900 started) made" \
			sh -c "[ '$(stats "$c" 1900 most_awake)' = 1 ] && [ '$(stats "$c" 1900 started)' -gt 1 ]"
		expect "$c" "none in solid, none not a number" sh -c "[ '$(check "$c" 1900 solid)' = 0 ] && [ '$(check "$c" 1900 nan)' = 0 ]" ;;
	b30-revert)
		expect "$c" "made before the revert: $(stats "$c" 1600 kept) kept" gt "$(stats "$c" 1600 kept)" 0
		# (the revert takes the game back to tick 1401: the commands of
		# ticks 1700 and on run some 300 ticks after it, the fight going
		# on meanwhile)
		k=$(stats "$c" 1600 kept) m=$(stats "$c" 1700 map)
		n=$(awk '/game state: reverted/ { want = 1; next } want && /ragdolls: .* let go: the game state replaced/ { n++ } want && !/let go: the game state replaced/ && /ragdolls: / { exit } END { print n + 0 }' "$log")
		# (those kept at tick 1600 and any made until the revert at 1650)
		expect "$c" "the revert lets every one go at once: $n lines after it, $m let go with the game state, $k kept at tick 1600" \
			sh -c "[ '$n' = '${m:-0}' ] && [ '${m:-0}' -ge '$k' ] && [ '$k' -gt 0 ]"
		expect "$c" "made again after it: $(stats "$c" 1700 started) -> $(stats "$c" 1900 started), none in solid" \
			sh -c "[ '$(stats "$c" 1900 started)' -gt '$(stats "$c" 1700 started)' ] && [ '$(check "$c" 1900 solid)' = 0 ]" ;;
	b30-map)
		expect "$c" "made on b30: $(stats "$c" 1600 kept) kept" gt "$(stats "$c" 1600 kept)" 0
		expect "$c" "a10 loaded: none kept ($(stats "$c" 200 kept)), the old ones let go" \
			sh -c "grep -aq 'test command at tick 200: @ragdoll stats' '$log' && [ '$(stats "$c" 200 kept)' = 0 ]" ;;
	b30-off)
		expect "$c" "off: none made ($(stats "$c" 1800 started)), no line" \
			sh -c "[ '$(stats "$c" 1800 started)' = 0 ] && ! grep -aq 'ragdolls: .* started' '$log'" ;;
	b30-live)
		expect "$c" "off at first: none ($(stats "$c" 1450 started))" test "$(stats "$c" 1450 started)" = 0
		expect "$c" "set on: made ($(stats "$c" 1600 started))" gt "$(stats "$c" 1600 started)" 0
		expect "$c" "set off: every one let go ($(stats "$c" 1720 kept) kept, $(stats "$c" 1720 setting) by the setting), none made after ($(stats "$c" 1720 started) -> $(stats "$c" 1800 started))" \
			sh -c "[ '$(stats "$c" 1720 kept)' = 0 ] && [ '$(stats "$c" 1720 setting)' -gt 0 ] && [ '$(stats "$c" 1800 started)' = '$(stats "$c" 1720 started)' ]" ;;
	a10)
		expect "$c" "crewmen made ragdolls ($(stats "$c" 630 started))" \
			sh -c "[ '$(stats "$c" 630 started)' -gt 0 ] && grep -aq 'ragdolls: .*(crewman.*started' '$log'"
		expect "$c" "settled ($(stats "$c" 1300 awake) awake), none in solid, none not a number, no hinge bent the wrong way ($(check "$c" 1300 wrong) of $(check "$c" 1300 hinges))" \
			sh -c "[ '$(stats "$c" 1300 awake)' = 0 ] && [ '$(check "$c" 1300 solid)' = 0 ] && [ '$(check "$c" 1300 nan)' = 0 ] && [ '$(check "$c" 1300 wrong)' = 0 ]" ;;
	bloodgulch)
		expect "$c" "the player's body a ragdoll ($(stats "$c" 310 started))" gt "$(stats "$c" 310 started)" 0
		expect "$c" "none in solid, none not a number" sh -c "[ '$(check "$c" 700 solid)' = 0 ] && [ '$(check "$c" 700 nan)' = 0 ]" ;;
	esac
done

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
