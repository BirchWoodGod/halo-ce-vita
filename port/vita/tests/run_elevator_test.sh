#!/bin/bash
# An elevator's riders (docs/opence-net32-spec.md 2, docs/crossplay.md 10.9):
# OpenCE's network version 30 carries them by the movement their elevator
# made the same tick, after its nodes are recomputed (objects.c,
# device_machines.c: machine_elevator_carry_begin/end), where the Xbox's
# carry, which the legacy wire and crossplay games of 29 keep, moved them by
# the movement of the tick before. Our harness build alone on c10, whose
# second elevator machine (343 Guilty Spark's lift) takes a rider: 3 s after
# the opening cinematic the player is put on its platform, which goes to its
# other end, back half way and turned round (a reversal in its stride), and
# down to its end again (HALO_TEST_ELEVATOR_RIDE=2), every tick logged. Once
# with each carry (HALO_TEST_ELEVATOR_CARRY=same, =xbox).
#
# Pass: with the same tick's carry the rider stands on the elevator every
# tick it moves, never airborne, its height over the elevator node the same
# to 0.001 units all the while (four legs, the reversal too); the Xbox's
# carry is logged for comparison (it lifts the rider off the platform and
# loses it at the reversal: not failed on, it is the old behaviour).
#
#   run_elevator_test.sh
#
#   HALO_TEST_VITA   the Linux harness build (build/linux/halo; not a release
#                    build: the knobs are tests' only)
#   HALO_TEST_DATA   a folder with the maps (c10.map, ui.map; ../data2276)
#   HALO_TEST_OUT    where the logs go (deleted after a pass unless
#                    HALO_TEST_KEEP=1)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=${HALO_TEST_VITA:-$root/build/linux/halo}
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/elevator_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
grep -aq "HALO_TEST_ELEVATOR_RIDE" "$binary" 2>/dev/null ||
	{ echo "SKIP: $binary has no HALO_TEST_ELEVATOR_RIDE (a release build, or none)"; exit 0; }
[ -f "$data/maps/c10.map" ] && [ -f "$data/maps/ui.map" ] || { echo "no maps (c10, ui) in $data/maps (HALO_TEST_DATA)"; exit 2; }
mkdir -p "$out"
trap 'test_out_done $?' EXIT

run() { # run CARRY
	local dir=$out/$1
	mkdir -p "$dir/data" "$dir/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	cp "$binary" "$dir/halo"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=80 HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_UPDATE_CHECK=false \
		HALO_NET_ONLINE=false HALO_DISCORD_APPLICATION= "HALO_TEST_COMMANDS=M30:map_name levels\\c10\\c10" \
		HALO_TEST_ELEVATOR_RIDE=2 HALO_TEST_ELEVATOR_CARRY="$1" \
		timeout -k 5 200 ./halo > run.log 2>&1)
}
run same & run xbox & wait

status=0
for carry in same xbox; do
	result=$(python3 -I - "$out/$carry/data/debug.txt" <<'EOF'
import re, sys
rows = []
with open(sys.argv[1], "rb") as f:
    for line in f.read().decode("utf-8", "replace").splitlines():
        m = re.search(r"elevator test: tick (\d+) leg (\d+) node (\S+) rider (\S+) over (\S+) velocity (\S+) "
            r"on (\d) ticks (\d+) airborne (\d)", line)
        if m:
            rows.append((int(m[2]), float(m[5]), float(m[6]), int(m[7]), int(m[9])))
moving = [r for r in rows if r[2] != 0.0]
legs = sorted({r[0] for r in moving})
on = [r for r in moving if r[3] and not r[4]]
spread = max(r[1] for r in on) - min(r[1] for r in on) if on else -1
print("%d %d %d %.4f %s" % (len(moving), len(on), len(legs), spread, ",".join(map(str, legs))))
EOF
)
	set -- $result
	reversed=$(grep -ac "elevator test: tick [0-9]* reversed" "$out/$carry/data/debug.txt" 2>/dev/null)
	echo "$carry carry: ${1:-0} ticks moving, ${2:-0} on it and grounded, legs ${5:-none}, reversed ${reversed:-0}," \
		"height over the node varied ${4:-?} units"
	if [ "$carry" = same ]; then
		if [ "${1:-0}" -lt 300 ] || [ "${2:-0}" != "${1:-x}" ] || [ "${3:-0}" -lt 4 ] || [ "${reversed:-0}" = 0 ] ||
			! awk -v s="${4:--1}" 'BEGIN { exit !(s >= 0 && s <= 0.001) }'; then
			status=1
		fi
	fi
done
[ $status = 0 ] && echo "PASS (elevator carry: the same tick's)" || echo "FAIL (elevator carry: the same tick's)"
exit $status
