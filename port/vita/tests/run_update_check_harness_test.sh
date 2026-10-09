#!/bin/bash
# The update check in the game (port/linux/src/update_notify.c), on the
# Linux harness (a build of this tree: build/linux/halo), against
# update_test_server.py on 127.0.0.1 standing in for GitHub
# (HALO_UPDATE_URL); the settings panel's Check for updates pressed by the
# test command @update (HALO_TEST_COMMANDS) at the main menu:
#
#   idle      no press: no request, no update line at all
#   newer     a newer release: halo.log says so, the main menu's corner
#             shows it, update_check.txt keeps it; a second press 5 s later
#             is answered from it - one request in all, with the fixed
#             User-Agent HaloCEVita/<version> and no cookie
#   same      this build's own version: up to date, nothing on the menu
#   older     an older release (1.0.3): up to date
#   cached    update_check.txt from 10 s before: answered from it, no request
#   stale     update_check.txt from 2 minutes before: asked again
#   stable    HALO_UPDATE_CHANNEL=stable on a pre-release build: logged once,
#             Experimental used
#   offline   GitHub's own URL inside a network namespace with no network
#             (unshare -rn): "no internet connection", nothing sent
#
#   run_update_check_harness_test.sh [CASE...]   (default: all)
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder
#   HALO_TEST_OUT    where the runs go (deleted after a pass unless
#                    HALO_TEST_KEEP=1)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_update_check_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-idle newer same older cached stale stable offline}
version=$(sed -n 's/^#define HALO_VITA_VERSION "\(.*\)"$/\1/p' "$root/port/vita/include/vita_version.h")
status=0
fail() { echo "FAIL ($1): $2"; status=1; }
server_pid=

[ -n "$out" ] || exit 2
mkdir -p "$out"
cleanup() { [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null; }
trap cleanup EXIT
rm -f "$out/port" "$out/requests"
python3 "$here/update_test_server.py" "$out/port" "$out/requests" & server_pid=$!
for i in $(seq 50); do [ -s "$out/port" ] && break; sleep 0.1; done
port=$(cat "$out/port")
base="http://127.0.0.1:$port"

run() { # NAME SECONDS COMMANDS [VAR=value...]
	local name=$1 seconds=$2 commands=$3
	shift 3
	rm -rf "${out:?}/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	[ -n "${cache_text:-}" ] && printf '%s' "$cache_text" > "$out/$name/data/update_check.txt"
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 "HALO_TEST_COMMANDS=$commands" "$@" ${wrapper:-} timeout -k 5 $((seconds + 90)) "$binary" \
		> "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

requests() { # PATH-PREFIX: the requests the server had for it
	[ -f "$out/requests" ] || { echo 0; return; }
	grep -c "^$1" "$out/requests" || true
}

common() { # NAME
	local log=$out/$1/run.log
	[ "$(cat "$out/$1/exit")" = 0 ] || fail "$1" "exit $(cat "$out/$1/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$1" "an assertion, exception or halt"
	grep -aq "test command at tick .*: @update" "$log" || [ "$1" = idle ] || fail "$1" "@update did not run"
	echo "--- $1"
	grep -a "update:" "$log" | sed 's/^/    /'
}

for case in $cases; do
	cache_text=
	wrapper=
	case $case in
	idle)
		run idle 25 "" "HALO_UPDATE_URL=$base/releases/v9.9.0/0"
		common idle
		[ "$(requests /releases/v9.9.0/)" = 0 ] || fail idle "a request without a press"
		grep -aq "update:" "$out/idle/run.log" && fail idle "an update line without a press"
		;;
	newer)
		run newer 40 "300:@update;450:@update" "HALO_UPDATE_URL=$base/releases/v9.9.9-beta.1/1"
		common newer
		log=$out/newer/run.log
		grep -aq "update: 9.9.9-beta.1 is available (this is $version; Update channel experimental): github.com/BirchWoodGod/halo-ce-vita/releases" "$log" ||
			fail newer "halo.log does not say 9.9.9-beta.1 is available"
		grep -aq 'update: the main menu shows "Update: 9.9.9-beta.1"' "$log" ||
			fail newer "the main menu does not show it"
		grep -aq "update: 9.9.9-beta.1 is available .*the look of [0-9]* s ago" "$log" ||
			fail newer "the second press was not answered from the cache"
		[ "$(requests /releases/v9.9.9-beta.1/)" = 1 ] || fail newer "$(requests /releases/v9.9.9-beta.1/) requests, not 1"
		grep "^/releases/v9.9.9-beta.1/" "$out/requests" | grep -qv " HaloCEVita/$version$" &&
			fail newer "a request with another User-Agent or a cookie"
		grep -q "^latest=9.9.9-beta.1$" "$out/newer/data/update_check.txt" 2>/dev/null ||
			fail newer "update_check.txt does not keep it"
		;;
	same)
		run same 30 "300:@update" "HALO_UPDATE_URL=$base/releases/v$version/1"
		common same
		grep -aq "update: up to date ($version; Update channel experimental)" "$out/same/run.log" ||
			fail same "not up to date"
		grep -aq "the main menu shows" "$out/same/run.log" && fail same "the main menu shows an update"
		;;
	older)
		run older 30 "300:@update" "HALO_UPDATE_URL=$base/releases/v1.0.3/0"
		common older
		grep -aq "update: up to date ($version" "$out/older/run.log" || fail older "not up to date"
		grep -aq "the main menu shows" "$out/older/run.log" && fail older "the main menu shows an update"
		;;
	cached)
		cache_text=$(printf 'checked=%s\nchannel=experimental\nresult=ok\nlatest=9.9.9-beta.2\n' $(($(date +%s) - 10)))
		run cached 30 "300:@update" "HALO_UPDATE_URL=$base/releases/v9.9.9-beta.3/1"
		common cached
		grep -aq "update: 9.9.9-beta.2 is available .*the look of [0-9]* s ago" "$out/cached/run.log" ||
			fail cached "not answered from update_check.txt"
		[ "$(requests /releases/v9.9.9-beta.3/)" = 0 ] || fail cached "GitHub asked within a minute of the last look"
		;;
	stale)
		cache_text=$(printf 'checked=%s\nchannel=experimental\nresult=ok\nlatest=9.9.9-beta.2\n' $(($(date +%s) - 120)))
		run stale 30 "300:@update" "HALO_UPDATE_URL=$base/releases/v9.9.9-beta.4/1"
		common stale
		grep -aq "update: 9.9.9-beta.4 is available" "$out/stale/run.log" || fail stale "not asked again"
		[ "$(requests /releases/v9.9.9-beta.4/)" = 1 ] || fail stale "$(requests /releases/v9.9.9-beta.4/) requests, not 1"
		;;
	stable)
		run stable 30 "300:@update" "HALO_UPDATE_URL=$base/releases/v9.9.9-beta.5/1" HALO_UPDATE_CHANNEL=stable
		common stable
		case $version in
		*-*)
			[ "$(grep -ac "update: the Update channel stable is not for a pre-release build ($version): experimental" "$out/stable/run.log")" = 1 ] ||
				fail stable "Stable on a pre-release build not refused once"
			grep -aq "update: 9.9.9-beta.5 is available (this is $version; Update channel experimental)" "$out/stable/run.log" ||
				fail stable "Experimental not used" ;;
		*)
			grep -aq "update: no release for the Update channel stable\|update: up to date ($version; Update channel stable)" "$out/stable/run.log" ||
				fail stable "a release build on Stable took a pre-release" ;;
		esac
		;;
	offline)
		if ! unshare -rn true 2>/dev/null; then
			echo "SKIP offline: no unshare -rn"
			continue
		fi
		wrapper="unshare -rn"
		run offline 30 "300:@update"
		common offline
		grep -aq "update: could not check: no internet connection" "$out/offline/run.log" ||
			fail offline "not 'no internet connection'"
		;;
	*)
		echo "usage: $0 [idle|newer|same|older|cached|stale|stable|offline]..." >&2; exit 2 ;;
	esac
done
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
