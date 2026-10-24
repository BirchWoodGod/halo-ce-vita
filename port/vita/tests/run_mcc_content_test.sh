#!/bin/bash
# The Halo content folder (paths.content, HALO_CONTENT_ROOT: the player's own
# Halo: The Master Chief Collection folder, halo1 in it; no switch:
# port/linux/game/mcc_content.c, docs/mcc_cea_maps.md).
#
# 1. Without any MCC file, always: mcc_content_test.c (MCC's maps' names,
#    Steam's libraryfolders.vdf, its truncations, the loaders' table by a
#    map file's header), under AddressSanitizer
#    and undefined-behaviour trapping when the compiler has them.
# 2. With the player's own MCC halo1 folder (HALO_TEST_MCC_ROOT, default
#    Steam's; SKIP without one) and the Xbox maps (HALO_TEST_DATA), on the
#    Linux harness: MCC's maps linked read only into a content folder of the
#    test's (<out>/mcc/halo1/maps, never copied), which HALO_CONTENT_ROOT
#    names:
#      campaign  each of HALO_TEST_MCC_LEVELS (b30 d40) started as the campaign
#                list starts it (mods\mcc_<level>), its cinematics skipped,
#                played HALO_TEST_MCC_SECONDS (60): it must load from MCC's
#                folder, say its sounds are silent, tick on, and log no
#                assertion or exception
#      won       mcc_b30 won goes on to mcc_b40, MCC's next level
#      mp        mcc_bloodgulch, an offline game with HALO_TEST_MCC_BOTS (6)
#                bots (the network test's local: game, in a namespace with no
#                network), HALO_TEST_MCC_MP_SECONDS (60): the bots must score
#      xbox      no content folder (HALO_CONTENT_ROOT a folder without halo1):
#                the Xbox maps alone, mods\mcc_b30 does not load, no MCC map
#                is listed
#      shadow    a file of the maps folder named mcc_bloodgulch.map (an Xbox
#                map) is never listed nor played for MCC's name
#   HALO_TEST_SHOTS: a folder for the runs' frames (every 60 frames; the GL
#   build's).
#
#   run_mcc_content_test.sh [unit|harness]   (default both)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
mcc=${HALO_TEST_MCC_ROOT:-$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1}
levels=${HALO_TEST_MCC_LEVELS:-b30 d40}
seconds=${HALO_TEST_MCC_SECONDS:-60}
mp_seconds=${HALO_TEST_MCC_MP_SECONDS:-60}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_mcc_content_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
status=0
fail() { echo "FAIL: $1"; status=1; }
parts=${*:-unit harness}

case " $parts " in *" unit "*)
	cc=${CC:-gcc}
	sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
	echo 'int main(void){return 0;}' | $cc $sanitize -x c - -o "$out/probe" 2>/dev/null || sanitize=
	$cc -g -O1 -Wall -Wextra -Werror $sanitize -I"$root/port/linux/game" -I"$root/port/linux/src" \
		"$here/mcc_content_test.c" "$root/port/linux/game/mcc_content.c" "$root/port/linux/src/mcc_paths.c" \
		-o "$out/mcc_content_test" || fail "mcc_content_test did not build"
	[ -x "$out/mcc_content_test" ] && { "$out/mcc_content_test" || fail "mcc_content_test"; }
	;;
esac

case " $parts " in *" harness "*)
	if [ ! -x "$binary" ]; then
		echo "SKIP (harness): no harness at $binary (HALO_TEST_VITA)"
	elif [ ! -e "$data/maps/ui.map" ]; then
		echo "SKIP (harness): no Xbox maps in $data/maps (HALO_TEST_DATA)"
	elif [ ! -r "$mcc/maps/bitmaps.map" ]; then
		echo "SKIP (harness): no MCC halo1 folder at $mcc (HALO_TEST_MCC_ROOT)"
	else
		# (MCC's maps, linked read only, never copied)
		mkdir -p "$out/mcc/halo1/maps"
		for file in "$mcc"/maps/*.map; do
			ln -sfn "$file" "$out/mcc/halo1/maps/"
		done
		# run NAME SECONDS [VAR=value...]: the harness, the data folder's
		# maps the Xbox's, init.txt and the commands the case's
		run() {
			local name=$1 run_seconds=$2 dir="$out/$1"
			shift 2
			rm -rf "$dir"
			mkdir -p "$dir/data/maps" "$dir/save"
			for file in "$(cd "$data" && pwd)"/maps/*.map; do
				ln -sfn "$file" "$dir/data/maps/"
			done
			[ -n "${init_line:-}" ] && printf 'game_difficulty_set hard\n%s\n' "$init_line" > "$dir/data/init.txt"
			[ -n "${extra_map:-}" ] && ln -sfn "$extra_map" "$dir/data/maps/$extra_map_name"
			local shots=()
			if [ -n "${HALO_TEST_SHOTS:-}" ]; then
				mkdir -p "$HALO_TEST_SHOTS/$name"
				shots=("HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$name" HALO_SCREENSHOT_EVERY=60)
			fi
			(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up 2> /dev/null; exec "$@"' sh \
				env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
				HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$run_seconds" \
				HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
				HALO_CONTENT_ROOT="${content_root:-$out/mcc}" HALO_FRAME_TIMING=300 "${shots[@]}" "$@" \
				timeout -k 5 $((run_seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
			local code=$?
			[ "$code" = 0 ] || fail "$name: the harness exited with $code ($dir/run.log)"
			if cat "$dir/run.log" "$dir/data/debug.txt" 2>/dev/null | grep -av "render assertion skipped" |
				grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire"; then
				fail "$name: an assertion or exception ($dir)"
			fi
		}
		ticks_of() { grep -a "frame-timing: game tick" "$1" | tail -1 | sed -E 's/.*game tick ([0-9]+).*/\1/'; }
		campaign_commands='30:(set cheat_deathless_player true);60:@skip;300:@skip;600:@skip;900:@skip;1200:@skip'

		for level in $levels; do
			init_line="map_name mods\\mcc_$level" run "mcc_$level" "$seconds" \
				HALO_TICK_THREAD=0 "HALO_TEST_COMMANDS=$campaign_commands"
			log="$out/mcc_$level/run.log"
			debug="$out/mcc_$level/data/debug.txt"
			ticks=$(ticks_of "$log")
			if ! grep -aq "memory: loaded mods\\\\mcc_$level" "$log" ||
				! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\$level.map'" "$debug"; then
				fail "mcc_$level was not loaded from MCC's folder ($out/mcc_$level)"
			elif ! grep -aq "mcc: '$level' is MCC's own campaign map: its sounds are in MCC's FMOD banks" "$debug"; then
				fail "mcc_$level: its silent sounds were not said"
			elif [ "${ticks:-0}" -lt $((seconds * 20)) ]; then
				fail "mcc_$level reached tick ${ticks:-0} in $seconds s"
			else
				echo "mcc_$level: loaded from MCC's folder, tick $ticks in $seconds s;" \
					"$(grep -a 'objects: ' "$log" | tail -1 | sed 's/.*objects: \([0-9]*\) in the game.*/\1 objects/')"
			fi
		done

		# won: on to MCC's next level
		init_line='map_name mods\mcc_b30' run won 40 HALO_TICK_THREAD=0 'HALO_TEST_COMMANDS=M300:(game_won);'
		if grep -aq "memory: loaded mods\\\\mcc_b40" "$out/won/run.log"; then
			echo "won mcc_b30: on to mcc_b40"
		else
			fail "mcc_b30 won did not go on to mcc_b40 ($out/won)"
		fi

		# mp: offline with bots on MCC's Blood Gulch
		init_line= run mp "$mp_seconds" HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_NETWORK_TEST_SCORE=500 \
			HALO_TEST_INPUT=bot:1 HALO_BOTS="${HALO_TEST_MCC_BOTS:-6}"
		log="$out/mp/run.log"
		# (the kills of every player, the network test's last tick line's kN)
		kills=$(grep -a "network test: tick" "$log" | tail -1 | grep -aoE " k[0-9]+" | tr -d ' k' | awk '{s+=$1} END {print s+0}')
		if ! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/mp/data/debug.txt"; then
			fail "mp: MCC's bloodgulch was not loaded ($out/mp)"
		elif ! grep -aq "network test: starting the game" "$log"; then
			fail "mp: the game did not start ($out/mp)"
		elif [ "${kills:-0}" -lt 1 ]; then
			fail "mp: no kill in $mp_seconds s ($out/mp)"
		else
			echo "mp: mcc_bloodgulch, $(grep -ac 'network test: player [0-9]* is named' "$log") players, kills ${kills:-0};" \
				"$(grep -a 'mcc: [0-9]* multiplayer maps' "$out/mp/data/debug.txt" | tail -1 | sed 's/.*mcc: //')"
		fi

		# xbox: no content folder, the Xbox maps alone
		mkdir -p "$out/none"
		content_root="$out/none" init_line='map_name mods\mcc_b30' run xbox 25 HALO_TICK_THREAD=0
		if grep -aq "memory: loaded mods" "$out/xbox/run.log" || grep -aq "^.*mcc: [0-9]* .* maps" "$out/xbox/data/debug.txt" ||
			! grep -aq "content: the Halo content folder (paths.content): .*(no halo1/maps there)" "$out/xbox/run.log"; then
			fail "no content folder: an MCC map was listed or loaded ($out/xbox)"
		else
			echo "no content folder: the Xbox maps alone, mods\\mcc_b30 not loaded"
		fi

		# shadow: the maps folder's mcc_bloodgulch.map (an Xbox map) is not
		# MCC's Blood Gulch, nor listed
		extra_map="$(cd "$data" && pwd)/maps/bloodgulch.map" extra_map_name=mcc_bloodgulch.map init_line= \
			run shadow 40 HALO_TICK_THREAD=1 \
			HALO_NETWORK_TEST="local:mcc_bloodgulch:slayer" HALO_NETWORK_TEST_START=5 HALO_TEST_INPUT=bot:1 HALO_BOTS=1
		if ! grep -aq "the maps folder's 'mcc_bloodgulch' is not listed" "$out/shadow/data/debug.txt" ||
			! grep -aq "custom edition: loading 'd:\\\\mcc\\\\halo1\\\\maps\\\\bloodgulch.map'" "$out/shadow/data/debug.txt"; then
			fail "shadow: the maps folder's mcc_bloodgulch.map was listed or played ($out/shadow)"
		else
			echo "shadow: the maps folder's mcc_bloodgulch.map left out; MCC's played"
		fi
	fi
	;;
esac

[ "$status" = 0 ] && echo "PASS"
test_out_done "$status"
exit "$status"
