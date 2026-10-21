#!/bin/bash
# The flashlight under the scene's light cap (HALO_MAX_SCENE_LIGHTS, object
# lights.c lights_preprocess_scene). Ben 10, insider.18, c10 on Performance (4
# lights): the flashlight was on but lit the world only looking some ways -
# the cap kept the first lights the visibility walk found, and the flashlight
# came after others in many views. On the harness, c10 after its opening
# cinematic, the cap at 1 (so that one light in view before it is enough):
# the flashlight on (white), the view turned round in six steps of five
# presses, HALO_LIGHTS_LOG=1's lines once a second. Pass: once it is on,
# every line names the flashlight submitted, in every direction - with other
# lights in view before it as well as alone - and the other lights stay at
# the cap (only the flashlight is past it).
#
#   run_flashlight_test.sh
#   HALO_TEST_VITA   the Linux harness (build/linux/halo; HALO_LIGHTS_LOG and
#                    the test pad are not in release builds)
#   HALO_TEST_DATA   a folder with the maps (c10.map, ui.map; ../triage/menus/data)
#   HALO_TEST_OUT    where the logs go (deleted after a pass unless HALO_TEST_KEEP=1)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/flashlight_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
grep -aq "HALO_LIGHTS_LOG" "$binary" 2>/dev/null || { echo "SKIP: $binary has no HALO_LIGHTS_LOG"; exit 0; }
[ -f "$data/maps/c10.map" ] && [ -f "$data/maps/ui.map" ] || { echo "no maps (c10, ui) in $data/maps (HALO_TEST_DATA)"; exit 2; }
mkdir -p "$out/data" "$out/save"
trap 'test_out_done $?' EXIT
ln -sfn "$(cd "$data" && pwd)/maps" "$out/data/maps"
turn() { local p="" i; for i in 1 2 3 4 5; do p="$p rleft:100:100"; done; echo "$p wait:150:3000"; }
pad="level wait:150:85000 white:200:3000 $(turn) $(turn) $(turn) $(turn) $(turn) $(turn)"
(cd "$out" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/data" \
	HALO_SAVE_ROOT="$out/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=150 HALO_FULLSCREEN=0 \
	HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_UPDATE_CHECK=false \
	HALO_NET_ONLINE=false "HALO_TEST_COMMANDS=M30:map_name levels\\c10\\c10" "HALO_TEST_PAD=$pad" \
	HALO_LIGHTS_LOG=1 HALO_MAX_SCENE_LIGHTS=1 timeout -k 5 300 "$binary" > run.log 2>&1)
code=$?
python3 -I - "$out/run.log" "$code" <<'EOF'
import re, sys
lines = open(sys.argv[1], 'rb').read().decode('latin-1').splitlines()
code = int(sys.argv[2])
on = False
seconds = submitted = missing = crowded = over = 0
view = None
for line in lines:
    m = re.search(r'lights: (\d+) in view, (\d+) submitted \(cap (\d+)\)', line)
    if m:
        view = tuple(int(x) for x in m.groups())
        continue
    m = re.search(r'lights:\s+(\d+): characters.cyborg.flashlight_cyborg .* (NOT submitted|submitted)$', line)
    if m:
        on = True
        seconds += 1
        if m.group(2) == 'submitted':
            submitted += 1
        else:
            missing += 1
        if int(m.group(1)) > 0:
            crowded += 1
        if view and view[1] > view[2] + 1:
            over += 1
print('the flashlight on for %d s: submitted in %d, missing in %d; with lights in view before it in %d; '
      'past the cap by more than the flashlight in %d' % (seconds, submitted, missing, crowded, over))
ok = code == 0 and seconds >= 20 and missing == 0 and crowded >= 3 and over == 0
print('PASS' if ok else 'FAIL (exit %d)' % code)
sys.exit(0 if ok else 1)
EOF
