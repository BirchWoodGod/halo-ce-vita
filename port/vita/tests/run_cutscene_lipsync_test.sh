#!/bin/bash
# Cutscene lip sync with the sound manager's work rate (the owner's Vita, Oct
# 10 2026: "during the cutscene the NPC mouths aren't moving"). A speaking
# unit's mouth is the mouth data of the permutation its channel plays (30
# samples a second), read at the channel's estimated time once a frame
# (update_channels), and unit_update closes it by 0.1 every tick. With
# HALO_SOUND_MANAGER_DIVISOR=n (the Vita's profiles: 2 since 1.1.0-beta.1) the
# work ran every nth frame, the estimate moved by one frame's time in n
# (channel_get_state): the mouths ran at half the speech's pace or less and
# were set every other frame only. Now the skipped frames' sound time goes to
# the next one that does the work and the mouths are set every frame
# (sound_manager.c, channel_set_mouth_aperture).
#
# The Linux harness (gxm-null), fixed ticks (one a frame), the sound on (SDL's
# dummy driver), the tick on its own thread as the Vita's, a10's opening
# cinematic (Keyes and Cortana on the bridge, the sergeant, the marines) to
# tick 3400, with HALO_MOUTH_LOG=1 ("mouth: unit <index> sample <n> of
# <count> aperture <a>", each mouth set), at work rates 1 (the Xbox's), 2 and
# 3. Each speech line is a run of sets for one unit and one permutation (its
# mouth data's count):
#
# 1. rate 1: at least five lines of Keyes's unit (the first to speak), their
#    mouths open (an aperture of 0.5 or more) and read to the end of their
#    mouth data (within 3 samples) as the speech ends;
# 2. rates 2 and 3: the same lines, in the same order, each set on as many
#    frames as at rate 1 (one less allowed) and read as far (within 2
#    samples: a sound starts on a frame that does the work, up to n - 1
#    frames later).
#
#   run_cutscene_lipsync_test.sh
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_OUT    where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_cutscene_lipsync_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/a10.map" ]; then
	echo "no a10.map in $data/maps (HALO_TEST_DATA)"
	exit 2
fi

# run NAME DIVISOR: a10's opening at that sound work rate, the mouths logged
run() {
	local name=$1 divisor=$2
	local dir="$out/$name"
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	printf 'map_name levels\\a10\\a10\n' > "$dir/data/init.txt"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=0 HALO_EXIT_AFTER=240 HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_TICK_THREAD=1 HALO_FIXED_TICK=1 HALO_FIXED_DT=1 HALO_UPDATE_AUTO=false \
		HALO_NET_ONLINE=false HALO_INTERPOLATION=false HALO_SOUND_MANAGER_DIVISOR="$divisor" HALO_MOUTH_LOG=1 \
		HALO_TEST_COMMANDS='3400:@exit' \
		timeout -k 5 420 "$binary" > "$dir/run.log" 2>&1)
	local code=$?
	rm -rf "$dir/save"
	[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
	grep -av "render assertion skipped" "$dir/run.log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$name: an assertion or exception ($dir/run.log)"
	grep -aq "test command at tick 3400: @exit" "$dir/run.log" || fail "$name: the cinematic did not reach tick 3400 ($dir/run.log)"
}

# lines NAME: each speech line, one a line: "<unit> <count> <sets> <highest sample> <largest aperture>"
lines() {
	grep -a "mouth: unit" "$out/$1/run.log" | sed 's/.*mouth: unit \([0-9a-f]*\) sample \(-*[0-9]*\) of \([0-9]*\) aperture \([0-9.]*\).*/\1 \2 \3 \4/' |
		awk '{
			key = $1 " " $3
			if (key != last || $2 < previous) {
				if (last != "") print last, sets, highest, largest
				sets = 0; highest = -1; largest = 0
			}
			last = key; previous = $2; sets++
			if ($2 > highest) highest = $2
			if ($4 > largest) largest = $4
		}
		END { if (last != "") print last, sets, highest, largest }'
}

for divisor in 1 2 3; do
	run "rate$divisor" "$divisor"
done

# 1. the Xbox's rate
lines rate1 > "$out/rate1.lines"
speaker=$(head -1 "$out/rate1.lines" | cut -d' ' -f1)
keyes_lines=$(awk -v unit="$speaker" '$1 == unit' "$out/rate1.lines" | wc -l)
if [ -z "$speaker" ] || [ "$keyes_lines" -lt 5 ]; then
	fail "rate 1: ${keyes_lines:-0} speech lines of the first speaker (${speaker:-none}), not 5 or more ($out/rate1/run.log)"
else
	awk -v unit="$speaker" '$1 == unit && ($5 < 0.5 || $4 < $2 - 3) { bad = 1 } END { exit bad }' "$out/rate1.lines" &&
		echo "  ok: rate 1: $keyes_lines lines of $speaker's, each mouth open and read to the end of its data ($(wc -l < "$out/rate1.lines") lines in all)" ||
		fail "rate 1: a line of $speaker's with its mouth shut or not read to its end ($out/rate1.lines: unit count sets highest largest)"
fi

# 2. every nth frame's work: the same mouths
for divisor in 2 3; do
	lines "rate$divisor" > "$out/rate$divisor.lines"
	if ! awk '{ print $1, $2 }' "$out/rate1.lines" | cmp -s - <(awk '{ print $1, $2 }' "$out/rate$divisor.lines"); then
		fail "rate $divisor: not the speech lines of rate 1 ($out/rate1.lines, $out/rate$divisor.lines)"
		continue
	fi
	if paste -d' ' "$out/rate1.lines" "$out/rate$divisor.lines" |
		awk '$8 < $3 - 1 || $9 < $4 - 2 { print "  line " NR ": unit " $1 ", " $2 " samples: set on " $8 " frames, read to " $9 " (rate 1: " $3 ", " $4 ")"; bad = 1 } END { exit bad }'; then
		echo "  ok: rate $divisor: the $(wc -l < "$out/rate$divisor.lines") lines' mouths set every frame and read to their ends, as at rate 1"
	else
		fail "rate $divisor: mouths set on fewer frames or read less far than at rate 1"
	fi
done

echo "logs: $out"
if [ "$status" = 0 ]; then
	echo "PASS"
fi
test_out_done $status
exit $status
