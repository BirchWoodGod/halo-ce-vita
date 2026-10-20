#!/bin/bash
# The file layer and the map copy never depend on thread-local storage
# (port/linux/src/xbox_files.c, source/cache/cache_files_decompress_windows.c),
# on the Linux harness built as the Vita's __thread variables were through
# insider.17: ONE for the whole game (vitasdk's emulated TLS keeps a value per
# thread only when pthread_cancel is linked: port/vita/host/vita_main.c's
# tls_check). Then the map copy's thread made itself the background one for
# every thread, and once a copy was asked to stop - the Halo 3 multiplayer
# lobby hosting Battle Creek while the menus' copy of another map ran - every
# thread's reads ended at once with 0 bytes: Ben's whole beavercreek.map "cut
# short (0 bytes)", game types that "could not be read", the Forge packs'
# builds stopping at 0%, Blood Gulch and the main menu "damaged", the game
# stuck (Oct 20 2026).
#
#   hostcopy  the harness built with -D__thread= (a copy of this tree, its
#             own build folder); ui.map copied to the cache partition first
#             at the disk's speed, then at a memory card's (HALO_IO_THROTTLE_*):
#             MULTIPLAYER on SYSTEM LINK hosts Slayer on Battle Creek as the
#             lobby opens, the copy of the map before it stopped for Battle
#             Creek's: Battle Creek precached, never "could not be precached";
#             back, FORGE: the lobby's packs build past 0% (their reads not ended);
#             the game's network in a namespace of its own (unshare -rn), so
#             other harnesses' ports never meet it
#   vitalink  (when this tree has a Vita build, build/vita/halo.elf) the
#             Vita's executable links pthread_cancel: its __thread variables
#             are each thread's
#
#   run_shared_tls_test.sh [CASE...]   (default: hostcopy vitalink)
#   HALO_TEST_SHARED_TLS_VITA  a harness already built with -D__thread= (no
#                              build: the test's copy of the tree is skipped)
#   HALO_TEST_DATA_MENUS       a folder with the game's maps folder (the Xbox
#                              maps and Halo PC's bitmaps.map and loc.map)
#   HALO_TEST_PROFILE          a profile's blam.sav (the lobby hosts with one;
#                              default ../vita-save-backups/20261017-halo-profile)
#   HALO_TEST_LINUX_CC         the compiler the copy is built with (default:
#                              configure.py's)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
profile=${HALO_TEST_PROFILE:-$root/../vita-save-backups/20261017-halo-profile/blam-unlocked.sav}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_shared_tls_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
cases=${*:-hostcopy vitalink}
status=0
fail() { echo "FAIL: $1"; status=1; }
mkdir -p "$out"

# the harness with __thread a plain global (clang: the keyword hidden by a macro)
shared_tls_harness() {
	local tree="$out/tree" cc
	if [ -n "${HALO_TEST_SHARED_TLS_VITA:-}" ]; then
		binary=$(readlink -f "$HALO_TEST_SHARED_TLS_VITA")
		return 0
	fi
	cc=${HALO_TEST_LINUX_CC:-}
	if [ -z "$cc" ]; then
		cc=$(sed -n 's/^linux_cc = //p' "$root/build.ninja" 2>/dev/null | head -1)
		[ -n "$cc" ] || cc=clang
	fi
	mkdir -p "$tree"
	cat > "$out/cc-shared-tls" <<EOF
#!/bin/sh
exec $cc -D__thread= -Wno-keyword-macro "\$@"
EOF
	chmod +x "$out/cc-shared-tls"
	(cd "$root" && git ls-files -z --cached --others --exclude-standard) |
		(cd "$root" && xargs -0 cp --parents -a -t "$tree" 2>/dev/null)
	(cd "$tree" && python3 configure.py --lto off --pgo off --linux-cc "$out/cc-shared-tls" > "$out/configure.log" 2>&1 &&
		nice ninja -j "$(nproc)" build/linux/halo > "$out/build.log" 2>&1) || {
		fail "the shared-TLS harness did not build ($out/build.log)"
		return 1
	}
	binary=$tree/build/linux/halo
}

# run NAME SECONDS PAD [VAR=value...]: the harness in a network of its own
run() {
	local name=$1 seconds=$2 pad=$3 dir="$out/$1"
	shift 3
	rm -rf "$dir"
	mkdir -p "$dir/data"
	ln -sfn "$(cd "$data" && pwd)/maps" "$dir/data/maps"
	(cd "$dir" && exec unshare -rn sh -c 'ip link set lo up && ip link add wifi0 type dummy &&
		ip addr add 10.98.0.1/24 dev wifi0 && ip link set wifi0 up && exec "$@"' sh \
		env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" HALO_SAVE_ROOT="$out/save" \
		HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 HALO_HIDDEN_WINDOW=1 \
		HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_UI_LOG=1 \
		HALO_MENU_STYLE=halo3 HALO_MENUS_FOLDER="$root/port/vita/app0/menus/" "HALO_TEST_PAD=$pad" "$@" \
		timeout -k 5 $((seconds + 120)) "$binary" > "$dir/run.log" 2>&1)
	echo $? > "$dir/exit"
}

for case in $cases; do
	echo "--- $case"
	case $case in
	hostcopy)
		if [ ! -r "$data/maps/beavercreek.map" ] || [ ! -r "$data/maps/bitmaps.map" ]; then
			echo "SKIP: no maps in $data/maps (HALO_TEST_DATA_MENUS)"; continue
		fi
		if [ ! -r "$profile" ]; then
			echo "SKIP: no profile ($profile, HALO_TEST_PROFILE)"; continue
		fi
		if ! unshare -rn true 2> /dev/null; then
			echo "SKIP: no unprivileged network namespaces (unshare -rn)"; continue
		fi
		shared_tls_harness || continue
		rm -rf "$out/save"
		mkdir -p "$out/save/u/UDATA/869079979327"
		cp "$profile" "$out/save/u/UDATA/869079979327/blam.sav"
		[ -r "$(dirname "$profile")/SaveMeta.xbx" ] && cp "$(dirname "$profile")/SaveMeta.xbx" "$out/save/u/UDATA/869079979327/"
		run hostcopy-fill 12 "wait:150:1000"
		run hostcopy 50 "wait:150:3000 down:150:700 a wait:150:15000 b wait:150:3000 down:150:700 a wait:150:12000" \
			HALO_IO_THROTTLE_KBPS=10000 HALO_IO_THROTTLE_WRITE_KBPS=750 HALO_TEST_SLOW_IO=20000:all
		log=$out/hostcopy/run.log
		[ "$(cat "$out/hostcopy/exit")" = 0 ] || fail "hostcopy: the harness exited with $(cat "$out/hostcopy/exit") ($log)"
		grep -aq "menus: h3 live lobby: open on SYSTEM LINK (Slayer on Battle Creek)" "$log" ||
			fail "hostcopy: the lobby did not host Battle Creek ($log)"
		grep -aq "starting precaching of map 'beavercreek'" "$out/hostcopy/data/debug.txt" ||
			fail "hostcopy: Battle Creek's copy never began"
		if grep -aq "could not be precached\|could not be read\|is cut short" "$log"; then
			fail "hostcopy: a read ended for another thread's stopped copy: $(grep -a "could not be precached\|could not be read\|is cut short" "$log" | head -2)"
		else
			echo "  ok: hostcopy: Battle Creek precached while the copy before it stopped (no read of another thread ended)"
		fi
		grep -aq "forge: packs: .* wanted for the lobby" "$log" || fail "hostcopy: the Forge lobby asked for no pack"
		grep -aqE "forge lobby note at [0-9]+ ms: Building .*\.\.\. [1-9][0-9]?%" "$log" &&
			echo "  ok: hostcopy: the Forge lobby's packs build (their reads go on)" ||
			fail "hostcopy: the Forge lobby's packs never got past 0%"
		;;
	vitalink)
		elf=$root/build/vita/halo.elf
		nm=${VITASDK:-$HOME/vitasdk}/bin/arm-vita-eabi-nm
		if [ ! -r "$elf" ] || [ ! -x "$nm" ]; then
			echo "SKIP: no Vita build ($elf) or no $nm"; continue
		fi
		if "$nm" "$elf" | grep -q " T pthread_cancel$"; then
			echo "  ok: vitalink: pthread_cancel linked: emulated TLS is each thread's"
		else
			fail "vitalink: pthread_cancel not linked ($("$nm" "$elf" | grep pthread_cancel)): every __thread variable is one for the game"
		fi
		;;
	*)
		fail "no case $case"
		;;
	esac
done

[ "$status" = 0 ] && echo "PASS" || echo "logs in $out"
test_out_done $status
exit $status
