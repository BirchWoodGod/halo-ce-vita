#!/bin/bash
# The crossplay version table's fetch and keeping in the game
# (port/linux/src/netversion_fetch.c, netversion_table.c, network_wire.c), on
# the Linux harness (a build of this tree configured with --linux-net-vita
# and --netversions-test-key: build/linux/halo), against
# netversions_test_server.py on 127.0.0.1 standing in for the
# "crossplay-table" release on GitHub (HALO_TEST_NETVERSIONS_URL), each run's
# kept table in a folder of its own (HALO_TEST_NETVERSIONS_FOLDER), tables
# signed by tools/release/sign_netversions.py with NETVERSIONS_TEST_KEY (a
# throwaway key, never the owner's). In a network namespace of its own
# (unshare -rn: loopback and a dummy LAN, nothing outside).
#
#   none        no table published (404): the built-in list, nothing kept
#   good        a table (join 40 by 39's rules, refuse 50): taken, kept as
#               served, the wire joins 29, 32, 38, 39 and 40; two requests,
#               the fixed User-Agent, no cookie
#   kept        the kept table taken at start; the same version fetched: not
#               newer, kept as it was
#   newer       a newer one fetched: taken, kept in its place
#   older       an older one fetched (a replay): not taken, the kept one stays
#   badsig      a table changed after signing: not taken, nothing kept
#   otherkey    a table signed with another key: not taken
#   garbage     bytes that are no table and no signature: not taken
#   unknownwire join 41 by 41's rules (a newer build's) beside join 40: taken,
#               41 not joined, 40 joined
#   big         a 100000-byte answer: refused by its size
#   chunked     no Content-Length: refused
#   redirect    the files behind a redirect on this machine (as GitHub's
#               release downloads redirect to its storage): taken
#   elsewhere   a redirect to another host: refused, nothing kept
#   keptbad     a kept table changed on the card: not taken at start (the
#               built-in list), then the fetched one taken
#   off         network.crossplay_updates false: no request; the kept table
#               still taken
#   offline     internet play off (network.online false): no request
#   autooff     the PC's update.auto false: no request
#   daily       HALO_TEST_NETVERSIONS_INTERVAL=8 in place of a day: asked
#               again while running, the table taken once
#
#   run_netversion_fetch_harness_test.sh [CASE...]   (default: all)
#   NETVERSIONS_TEST_KEY  the test key's private file (make_update_key.py)
#                         whose public half the harness was configured with
#   HALO_TEST_VITA   the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA   a folder with the game's maps folder
#   HALO_TEST_OUT    where the runs go (deleted after a pass unless
#                    HALO_TEST_KEEP=1)
#   NETVERSIONS_PARALLEL  runs at once (6)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
if [ "${NETVERSIONS_NETNS_INSIDE:-}" != 1 ]; then
	export NETVERSIONS_NETNS_INSIDE=1
	exec unshare -rn bash -c 'ip link set lo up; ip link add d0 type dummy 2>/dev/null &&
		ip addr add 192.168.77.2/24 brd + dev d0 && ip link set d0 up; exec "$@"' netns "$0" "$@"
fi
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_netversion_fetch_test.$$}
key=${NETVERSIONS_TEST_KEY:-}
. "$here/test_out.sh"
cases=${*:-none good kept newer older badsig otherkey garbage unknownwire big chunked redirect elsewhere keptbad off offline autooff daily}
parallel=${NETVERSIONS_PARALLEL:-6}
[ -n "$key" ] && [ -r "$key" ] || { echo "SKIP: wants NETVERSIONS_TEST_KEY (a test key: make_update_key.py)"; exit 0; }
public=$(sed -n 's/^# public key: //p' "$key")
grep -aq "$public" "$binary" ||
	{ echo "SKIP: $binary was not configured with this key (configure.py --netversions-test-key $public)"; exit 0; }
grep -aq "halo-ce-vita-netversions 1" "$binary" || { echo "FAIL: $binary has no crossplay version table"; exit 1; }
test_out_begin "$out"
version=$(grep -aoE 'Halo CE Vita [0-9A-Za-z.-]+ \(PC\) starting' "$binary" | head -1 | sed 's/^Halo CE Vita \(.*\) (PC) starting$/\1/')
[ -n "$version" ] || version=$(sed -n 's/^#define HALO_VITA_VERSION "\(.*\)"$/\1/p' "$root/port/vita/include/vita_version.h")
status=0
fail() { echo "FAIL ($1): $2"; status=1; }
server_pid=

mkdir -p "$out/served" "$out/keys"
cleanup() { [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null; }
trap 'cleanup; test_out_done $status' EXIT
python3 "$root/tools/release/make_update_key.py" --private "$out/keys/other.key" > /dev/null
sign() { # FOLDER ARGUMENTS...: a table made or changed in FOLDER with the test key
	local folder=$1
	shift
	mkdir -p "$folder"
	python3 "$root/tools/release/sign_netversions.py" --test --key "$key" --table "$folder/netversions.txt" "$@" \
		>> "$out/sign.log" 2>&1 || { echo "BROKEN: sign_netversions.py $*"; exit 2; }
}
# the tables served (version 5: join 40 by 39's rules, refuse 50; 6: and
# join 45 by 38's; 7: join 41 by 41's)
sign "$out/served/v5" --join 40:39 --version 4
sign "$out/served/v5" --refuse 50 "OpenCE 50 changed the record"
cp -r "$out/served/v5" "$out/served/v6"
sign "$out/served/v6" --join 45:38
cp -r "$out/served/v5" "$out/served/v7"
sign "$out/served/v7" --join 41:41 --allow-newer-rules --version 7
mkdir -p "$out/served/badsig" "$out/served/otherkey" "$out/served/garbage" "$out/served/none"
cp "$out/served/v5/"* "$out/served/badsig/"
sed -i 's/rules 39/rules 38/' "$out/served/badsig/netversions.txt"
python3 "$root/tools/release/sign_netversions.py" --test --key "$out/keys/other.key" \
	--table "$out/served/otherkey/netversions.txt" --join 40:39 --version 9 >> "$out/sign.log" 2>&1
head -c 300 /dev/urandom > "$out/served/garbage/netversions.txt"
head -c 129 /dev/urandom > "$out/served/garbage/netversions.txt.sig"
grep -q "^version 5$" "$out/served/v5/netversions.txt" && grep -q "^version 6$" "$out/served/v6/netversions.txt" &&
	grep -q "^version 7$" "$out/served/v7/netversions.txt" || { echo "BROKEN: the served tables"; exit 2; }

rm -f "$out/port" "$out/requests"
python3 "$here/netversions_test_server.py" "$out/served" "$out/port" "$out/requests" & server_pid=$!
for i in $(seq 50); do [ -s "$out/port" ] && break; sleep 0.1; done
port=$(cat "$out/port")
base="http://127.0.0.1:$port"

run() { # NAME SECONDS URL-PATH KEPT-FROM [VAR=value...]: the game at its main menu
	local name=$1 seconds=$2 path=$3 kept=$4
	shift 4
	rm -rf "${out:?}/$name"
	mkdir -p "$out/$name/data" "$out/$name/save" "$out/$name/kept" "$out/$name/home"
	ln -sfn "$(cd "$data" && pwd)/maps" "$out/$name/data/maps"
	[ -n "$kept" ] && cp "$kept"/netversions.txt* "$out/$name/kept/"
	cp "$binary" "$out/$name/halo"
	(cd "$out/$name" && exec env -u DISPLAY -u WAYLAND_DISPLAY HOME="$out/$name/home" SDL_AUDIODRIVER=dummy \
		SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 \
		HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 \
		HALO_UPDATE_AUTO=true HALO_UPDATE_CHECK=false HALO_NET_ONLINE=true HALO_NET_ALLOW_UPNP=false \
		HALO_DISCORD_APPLICATION= HALO_CRASH_REPORTS=no HALO_NET_JOIN_FROM_CLIPBOARD=false \
		HALO_TEST_NETVERSIONS_URL="$base/$path" HALO_TEST_NETVERSIONS_FOLDER="$out/$name/kept" "$@" \
		timeout -k 5 $((seconds + 90)) "$out/$name/halo" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

requests() { # PATH-PREFIX: the requests the server had for it
	[ -f "$out/requests" ] || { echo 0; return; }
	grep -c "^/$1" "$out/requests" || true
}
has() { grep -aq -- "$2" "$out/$1/run.log" || fail "$1" "${3:-no line: $2}"; }
hasnt() { grep -aq -- "$2" "$out/$1/run.log" && fail "$1" "${3:-a line: $2}"; }
kept_is() { # NAME FROM: the kept table is FROM's, byte for byte
	cmp -s "$out/$1/kept/netversions.txt" "$out/served/$2/netversions.txt" &&
		cmp -s "$out/$1/kept/netversions.txt.sig" "$out/served/$2/netversions.txt.sig" ||
		fail "$1" "the kept table is not $2's"
}
nothing_kept() { [ -e "$out/$1/kept/netversions.txt" ] || [ -e "$out/$1/kept/netversions.txt.sig" ] &&
	fail "$1" "a table kept"; ls "$out/$1/kept" | grep -q "\.new$" && fail "$1" "a download left behind"; return 0; }
builtin="the crossplay wire joins 29, 32, 38 and 39"

start_case() { # CASE: its run, in the background
	case $1 in
	none) run none 25 files/none "" ;;
	good) run good 25 files/v5 "" ;;
	kept) run kept 25 files/v5 "$out/served/v5" ;;
	newer) run newer 25 files/v6 "$out/served/v5" ;;
	older) run older 25 files/v5 "$out/served/v6" ;;
	badsig) run badsig 25 files/badsig "" ;;
	otherkey) run otherkey 25 files/otherkey "" ;;
	garbage) run garbage 25 files/garbage "" ;;
	unknownwire) run unknownwire 25 files/v7 "" ;;
	big) run big 25 big/v5 "" ;;
	chunked) run chunked 25 chunked/v5 "" ;;
	redirect) run redirect 25 redirect/v5 "" ;;
	elsewhere) run elsewhere 25 elsewhere/v5 "" ;;
	keptbad) mkdir -p "$out/keptbad-seed" && cp "$out/served/v5/"* "$out/keptbad-seed/" &&
		sed -i 's/^version 5$/version 9/' "$out/keptbad-seed/netversions.txt" && run keptbad 25 files/v6 "$out/keptbad-seed" ;;
	off) run off 25 files/v6 "$out/served/v5" HALO_NET_CROSSPLAY_UPDATES=false ;;
	offline) run offline 25 files/v6 "" HALO_NET_ONLINE=false ;;
	autooff) run autooff 25 files/v6 "" HALO_UPDATE_AUTO=false ;;
	daily) run daily 40 files/v5 "" HALO_TEST_NETVERSIONS_INTERVAL=8 ;;
	*) echo "unknown case $1"; status=1 ;;
	esac
}
pids=
for case in $cases; do
	start_case "$case" & pids="$pids $!"
	if [ "$(echo $pids | wc -w)" -ge "$parallel" ]; then
		for p in $pids; do wait "$p"; done
		pids=
	fi
done
for p in $pids; do wait "$p"; done

for case in $cases; do
	log=$out/$case/run.log
	[ -f "$log" ] || { fail "$case" "no run"; continue; }
	[ "$(cat "$out/$case/exit")" = 0 ] || fail "$case" "exit $(cat "$out/$case/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$case" "an assertion, exception or halt"
	echo "--- $case"
	grep -a "netversions:" "$log" | sed 's/^[0-9.]* [0-9:]*  //' | sed 's/^/    /'
	case $case in
	none)
		has none "no crossplay version table kept: the built-in list; $builtin"
		has none "could not fetch the table: the server answered 404"
		nothing_kept none ;;
	good)
		has good "the fetched table taken: version 5, 2 rows; the crossplay wire joins 29, 32, 38, 39 and 40"
		has good "netversions:   join 40 by 39's rules"
		has good "netversions:   refuse 50: OpenCE 50 changed the record"
		kept_is good v5
		[ "$(requests files/v5/netversions.txt)" -ge 1 ] && [ "$(requests files/v5/netversions.txt.sig)" -ge 1 ] ||
			fail good "the table and its signature not both asked"
		grep "^/files/v5/" "$out/requests" | grep -v " HaloCEVita/$version$" | grep -q . &&
			fail good "a request with another User-Agent or a cookie" ;;
	kept)
		has kept "the kept table .*/kept/netversions.txt taken: version 5, 2 rows; the crossplay wire joins 29, 32, 38, 39 and 40"
		has kept "the fetched table is not taken: version 5 is not newer than 5"
		kept_is kept v5 ;;
	newer)
		has newer "the kept table .* taken: version 5"
		has newer "the fetched table taken: version 6, 3 rows; the crossplay wire joins 29, 32, 38, 39, 40 and 45"
		kept_is newer v6 ;;
	older)
		has older "the kept table .* taken: version 6"
		has older "the fetched table is not taken: version 5 is not newer than 6"
		kept_is older v6 ;;
	badsig|otherkey)
		has $case "the fetched table is not taken: the signature is not the owner's key's"
		hasnt $case "table taken"
		nothing_kept $case ;;
	garbage)
		has garbage "the fetched table is not taken: "
		hasnt garbage "table taken"
		nothing_kept garbage ;;
	unknownwire)
		has unknownwire "the fetched table taken: version 7, 3 rows; the crossplay wire joins 29, 32, 38, 39 and 40$"
		has unknownwire "join 41 by 41's rules: not rules this build has (a newer build's), not joined"
		kept_is unknownwire v7 ;;
	big)
		has big "could not fetch the table: the file is too large (100000 bytes)"
		nothing_kept big ;;
	chunked)
		has chunked "could not fetch the table: a chunked download"
		nothing_kept chunked ;;
	redirect)
		has redirect "the fetched table taken: version 5"
		[ "$(requests files/v5/)" -ge 2 ] || fail redirect "the redirect not followed to this machine's files"
		kept_is redirect v5 ;;
	elsewhere)
		has elsewhere "could not fetch the table: a redirect to a place not allowed (evil.example)"
		nothing_kept elsewhere ;;
	keptbad)
		has keptbad "the kept table .* not taken: the signature is not the owner's key's"
		has keptbad "the built-in list; $builtin"
		has keptbad "the fetched table taken: version 6"
		kept_is keptbad v6 ;;
	off)
		has off "the kept table .* taken: version 5"
		has off "not fetching the table: network.crossplay_updates is off"
		hasnt off "fetching the crossplay version table" ;;
	offline)
		has offline "not fetching the table: internet play is off"
		hasnt offline "fetching the crossplay version table" ;;
	autooff)
		has autooff "not fetching the table: update.auto is off"
		hasnt autooff "fetching the crossplay version table" ;;
	daily)
		[ "$(grep -ac "netversions: fetching the crossplay version table" "$log")" -ge 2 ] ||
			fail daily "not asked again while running"
		[ "$(grep -ac "the fetched table taken" "$log")" = 1 ] || fail daily "the table not taken exactly once"
		has daily "the fetched table is not taken: version 5 is not newer than 5" ;;
	esac
done
# (the cases that must ask nothing: off, offline, autooff ran against v6,
# which only newer, keptbad asked for)
case " $cases " in *" off "*|*" offline "*|*" autooff "*)
	expected=0
	case " $cases " in *" newer "*) expected=$((expected + 2)) ;; esac
	case " $cases " in *" keptbad "*) expected=$((expected + 2)) ;; esac
	[ "$(requests files/v6/)" -le "$expected" ] || fail off "$(requests files/v6/) requests for v6, not at most $expected" ;;
esac
[ $status = 0 ] && echo "PASS crossplay version table fetch (${cases})" || echo "FAIL crossplay version table fetch"
exit $status
