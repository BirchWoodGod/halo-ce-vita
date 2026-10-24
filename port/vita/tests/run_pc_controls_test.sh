#!/bin/bash
# The PC builds' keyboard and mouse in play (port/linux/src/xinput_sdl.c, the
# bindings of port/linux/include/halo_key_bindings.h, Halo MCC's defaults):
# a local Team Slayer game on Blood Gulch with a bot, Menu style Halo 3, each
# default key and mouse button held in turn ("@hold NAME MS", HALO_TEST_COMMANDS:
# pushed into SDL's queue as the real ones arrive), and the game's own actions
# logged as it takes them (HALO_TEST_ACTION_LOG: player_control.c), so each key
# is checked against the action it must fire - and E against reloading (the
# action alone, nothing to act on), X and Left Shift on foot against anything.
# Then a game whose config.toml rebinds the grenade to V: V throws, G does not.
# Each run must exit by itself with no assertion or exception.
#
#   run_pc_controls_test.sh [defaults|rebound]
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA_MENUS  a folder with the game's maps folder (bloodgulch.map)
#   HALO_TEST_PROFILE     a blam.sav (needed: the game is played with it)
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_pc_controls_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-defaults rebound}
status=0
fail() { echo "FAIL: $1"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -r "$data/maps/bloodgulch.map" ]; then
	echo "no bloodgulch.map in $data/maps (HALO_TEST_DATA_MENUS)"
	exit 2
fi
if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
	echo "no HALO_TEST_PROFILE (a blam.sav)"
	exit 2
fi
mkdir -p "$out"
trap 'test_out_done $status' EXIT

# the keys held, in order, and the action each must fire ("-": none of the
# game's actions)
checks=(
	"W|move_forward" "S|move_backward" "A|strafe_left" "D|strafe_right"
	"Space|jump" "Left Ctrl|crouch" "C|crouch"
	"Mouse Left|fire" "Mouse Right|zoom" "Z|zoom"
	"G|throw_grenade" "F|melee" "Mouse 4|melee"
	"R|reload" "E|action (nothing to act on: no reload)"
	"Tab|switch_weapon" "Wheel Up|switch_weapon" "Wheel Down|switch_weapon"
	"H|switch_grenade" "Mouse Middle|switch_grenade"
	"Q|flashlight"
	"X|-" "Left Shift|-"
)

# run NAME SECONDS COMMANDS [VAR=value...]
run() {
	local name=$1 seconds=$2 commands=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data" "$dir/save/u/UDATA/869079979327"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	cp "$HALO_TEST_PROFILE" "$dir/save/u/UDATA/869079979327/blam.sav"
	[ -r "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" ] &&
		cp "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" "$dir/save/u/UDATA/869079979327/"
	[ -n "${config_text:-}" ] && printf '%s' "$config_text" > "$dir/config.toml"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_MENUS_FOLDER="$root/port/vita/app0/menus/" HALO_UI_LOG=1 HALO_TEST_COMMANDS="$commands" \
		HALO_TEST_CONFIG="$dir/config.toml" HALO_TEST_ACTION_LOG=1 HALO_MENU_STYLE=halo3 HALO_NETWORK_TEST=local:bloodgulch:team_slayer \
		HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1 \
		"$@" timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
	rm -rf "$dir/save"
}

ended() { # NAME
	local log="$out/$1/run.log"
	[ "$(cat "$out/$1/exit")" = 0 ] || fail "$1: the harness exited with $(cat "$out/$1/exit") ($log)"
	grep -av "render assertion skipped" "$log" | grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" &&
		fail "$1: an assertion or exception ($log)"
}

# fired LOG KEY: the actions logged after the key's hold and before the next one
fired() {
	awk -v key="test hold: $2 for" '
		index($0, "test hold: ") { if (on) exit; if (index($0, key)) { on = 1; next } }
		on && index($0, "action: ") { sub(/.*action: /, ""); print }' "$1"
}

for case in $cases; do
	case $case in
	defaults)
		# (the level from tick 150, five seconds in; a key every 1.5 s, held 300 ms)
		commands="" tick=150
		for check in "${checks[@]}"; do
			commands="$commands;L$tick:@hold ${check%%|*} 300"
			tick=$((tick + 45))
		done
		# (Backspace: the scores held; Escape: the pause menu, and again to close it)
		commands="$commands;L$tick:@hold Backspace 1500;L$((tick + 90)):@hold Escape 200;L$((tick + 150)):@hold Escape 200"
		config_text="" run defaults 60 "${commands#;}"
		ended defaults
		log="$out/defaults/run.log"
		grep -aq "action: move_forward" "$log" || fail "defaults: the game never ran the keys (no action logged: $log)"
		for check in "${checks[@]}"; do
			key=${check%%|*} want=${check#*|}
			got=$(fired "$log" "$key" | sort -u | tr '\n' ',' | sed 's/,$//')
			if [ "$want" = - ]; then
				[ -z "$got" ] && echo "PASS: $key fires nothing on foot" || fail "$key fired $got"
			elif fired "$log" "$key" | grep -qxF "$want"; then
				echo "PASS: $key -> $want${got:+ ($got)}"
			else
				fail "$key should fire $want, fired: ${got:-nothing}"
			fi
		done
		fired "$log" "E" | grep -qx "reload" && fail "E reloaded (the action key never reloads)"
		fired "$log" "R" | grep -q "^action" && fail "R acted (the reload key only reloads)"
		sed -n '/test hold: Backspace/,$p' "$log" | grep -aq "scoreboard\|ui: screen pc.h3.scoreboard" &&
			echo "PASS: Backspace shows the scores" || fail "Backspace did not show the scores"
		sed -n '/test hold: Escape/,$p' "$log" | grep -aq "ui: screen pc.h3.mppause.screen" &&
			echo "PASS: Escape pauses" || fail "Escape did not open the pause menu" ;;
	rebound)
		# (the player's V for the grenade; two of an earlier version's defaults,
		# which become MCC's: Z and the middle button zoomed, X switched grenades)
		config_text=$'[controls.keyboard]\nthrow_grenade = "V"\nzoom = "Z, Mouse Middle"\nswitch_grenade = "X"\n'
		run rebound 40 "L150:@hold G 300;L195:@hold V 300;L240:@hold X 300;L285:@hold H 300" HALO_MENU_STYLE=halo3
		ended rebound
		log="$out/rebound/run.log"
		fired "$log" V | grep -qx throw_grenade && echo "PASS: rebound: V throws" || fail "rebound: V did not throw"
		[ -z "$(fired "$log" G)" ] && echo "PASS: rebound: G does nothing" || fail "rebound: G still fired $(fired "$log" G)"
		grep -aq 'throw_grenade = "V"' "$out/rebound/config.toml" && echo "PASS: rebound: the player's binding kept" ||
			fail "rebound: config.toml lost the player's binding"
		grep -aq 'zoom = "Mouse Right, Z"' "$out/rebound/config.toml" &&
			grep -aq 'switch_grenade = "H, Mouse Middle"' "$out/rebound/config.toml" &&
			echo "PASS: rebound: an earlier version's defaults migrated to MCC's" ||
			fail "rebound: the earlier defaults were not migrated"
		[ -z "$(fired "$log" X)" ] && fired "$log" H | grep -qx switch_grenade &&
			echo "PASS: rebound: H switches grenades, X no longer" || fail "rebound: X/H after the migration" ;;
	*)
		echo "unknown case $case"; status=1 ;;
	esac
done
[ $status = 0 ] && echo "PASS: the keyboard and mouse in play"
exit $status
