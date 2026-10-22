# netns_dedicated_compat.sh: run_netns_online_test.sh's dedicatedcompat mode
# (sourced by it, inside its dedicated servers' case: run_copy, side, console,
# $code, $sl, $sd and fail are its). README_dedicated_compat.md says how to run it.
#
# (Any game type: HALO_TEST_COMPAT_CYCLE's first game's decides the checks,
# Infection's or another's - a team game's teams: README_dedicated_compat.md.)
#
# The question it answers: can the PUBLIC 1.1.0 build (and the insider.21
# build beside it) join and play the dedicated server's Infection with the
# bots filling it (the s8 server)? The server (HALO_TEST_SERVER, this tree's
# halo-server) runs Infection (HALO_TEST_COMPAT_CYCLE, "prisoner infection"),
# sv_maxplayers HALO_TEST_BOTS_MAX (16), sv_bots_fill 1; one 1.1.0 client
# (HALO_TEST_COMPAT_V110, player Old110) joins by the code, one insider.21
# client (HALO_TEST_COMPAT_NEW, player Ins21) 10 s later, both scripted
# (bot input, shooting now and then) until a round or more is over. Then
# netns_compat_check.py reads every log and prints one line a check.

compat_v110=${HALO_TEST_COMPAT_V110:-$root/../wt-v110client/build/linux/halo}
compat_new=${HALO_TEST_COMPAT_NEW:-$root/../wt-i21client/build/linux/halo}
compat_most=${HALO_TEST_BOTS_MAX:-16}

# dedicated_compat_init FILE: the server's init.txt
dedicated_compat_init() {
	local cycle=${HALO_TEST_COMPAT_CYCLE:-prisoner infection} entry
	{
		echo "# the netns test's s8 stand-in (run_netns_online_test.sh dedicatedcompat)"
		echo "sv_name \"Netns Compat\""
		echo "sv_maxplayers $compat_most"
		echo "sv_public 1"
		echo "sv_bots_fill 1"
		# (HALO_TEST_COMPAT_CYCLE: games separated by ';', "map gametype" each)
		echo "$cycle" | tr ';' '\n' | while read -r entry; do
			[ -n "$entry" ] && echo "sv_mapcycle_add $entry"
		done
		# (HALO_TEST_COMPAT_EXTRA: more commands, separated by ';': an official
		# server's own, "sv_starting_weapons generic;sv_bots_skill easy")
		echo "${HALO_TEST_COMPAT_EXTRA:-}" | tr ';' '\n' | while read -r entry; do
			[ -n "$entry" ] && echo "$entry"
		done
		echo "sv_timelimit ${HALO_TEST_COMPAT_MINUTES:-3}"
		echo "sv_start_delay 5"
		echo "sv_postgame 5"
		echo "sv_end_empty 20"
		echo "sv_port 2302"
	} > "$1"
}

dedicated_compat_run() {
	local v110_pid new_pid status_pid v110_rc new_rc server_rc
	for binary in "$compat_v110" "$compat_new"; do
		[ -x "$binary" ] || { echo "dedicatedcompat: no client build at $binary (HALO_TEST_COMPAT_V110, HALO_TEST_COMPAT_NEW)"; exit 2; }
	done
	echo "dedicatedcompat: 1.1.0 client $compat_v110"
	echo "dedicatedcompat: insider client $compat_new"
	echo "dedicatedcompat: server $server"
	# (the server's sv_status every 5 s, its sv_items every 20 s)
	(
		for i in $(seq 1 $(((seconds - 20) / 5))); do
			sleep 5
			echo sv_status >&"$console_fd" 2>/dev/null || exit 0
			[ $((i % 4)) = 0 ] && { echo sv_items >&"$console_fd" 2>/dev/null || exit 0; }
		done
	) & status_pid=$!; pids="$pids $status_pid"
	run_copy v110 "$join_machine" "$compat_v110" "$cpu_b" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_NET_PLAYER_NAME=Old110 HALO_NET_LOBBY_NAME=Old110 HALO_EXIT_AFTER=$((seconds - 10)) HALO_TEST_INPUT=bot:2 \
		HALO_NETWORK_TEST_SHOOT=2
	v110_pid=$last_pid
	sleep 10
	side d2 10.10.22 192.168.22
	run_copy insider "$d2_machine" "$compat_new" "$cpu_c" HALO_NET_ONLINE=true HALO_NETWORK_TEST=join-code:$code \
		HALO_NET_PLAYER_NAME=Ins21 HALO_NET_LOBBY_NAME=Ins21 HALO_EXIT_AFTER=$((seconds - 20)) HALO_TEST_INPUT=bot:3 \
		HALO_NETWORK_TEST_SHOOT=2
	new_pid=$last_pid
	sleep 60
	console sv_players
	console sv_status
	wait "$v110_pid" 2>/dev/null; v110_rc=$?
	wait "$new_pid" 2>/dev/null; new_rc=$?
	kill "$status_pid" 2>/dev/null; wait "$status_pid" 2>/dev/null
	kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null; server_rc=$?
	echo "--- server (joins, bots, game rules, the round)"
	grep -aE "server: (the next game|the game|player #[0-9]+ \S+ (joined|left))|game rules net|infection: (the round|.* is a zombie|.* infected )" "$sl" |
		grep -av "trace:" | head -50
	echo "--- the 1.1.0 client's own lines"
	grep -aE "network test: (joining|the public|player [0-9]+ is named)|system link: |XLaunchNewImage|lost the connection" \
		"$out/v110/run.log" | head -24
	echo "--- the insider client's infection lines"
	grep -aE "infection:|game rules net:" "$out/insider/run.log" | grep -av "trace:" | head -24
	echo "--- checks"
	# (the cycle's first game's type: Infection's checks, or another's - a
	# team game's teams)
	local first_type teams=
	first_type=$(echo "${HALO_TEST_COMPAT_CYCLE:-prisoner infection}" | cut -d';' -f1 | awk '{print $2}')
	case $first_type in team_*|ctf|btb) teams=--teams ;; esac
	python3 "$here/netns_compat_check.py" --server-log "$sl" --server-debug "$sd" --server-rc "$server_rc" \
		--most "$compat_most" --gametype "$first_type" $teams \
		--client "v110:Old110:$out/v110:$v110_rc" --client "insider:Ins21:$out/insider:$new_rc" \
		$([ "${HALO_TEST_COMPAT_LENIENT:-0}" = 1 ] && echo --lenient) ||
		fail "the compatibility checks failed (the [FAIL] lines above)"
}
