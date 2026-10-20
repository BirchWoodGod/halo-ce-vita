# Network Forge's cases of run_netns_online_test.sh (port/linux/game/
# forge_net.inc), sourced by it: its run_copy, fail, $out, $vita, $cpu_*,
# $host_machine, $lan and forge_lan_machine. Every machine on the host's LAN
# (System Link); the host hosts a Forge game of Blood Gulch (the network
# test's host:bloodgulch:forge, its packs the Covenant vehicles and AI), the
# joiners join it by the System Link search, and each machine's monitor
# edits by the console (HALO_TEST_COMMANDS: forge.c's commands, the edits of
# the controller's buttons).
#   forge      two machines: the host places a Warthog and a boulder; the
#              joiner sees them (the Warthog the netcode's, the boulder its
#              own copy), places a teleporter base, holds it while the host's
#              grab is refused, the host takes it once let go and the
#              joiner's grab is refused; the joiner moves the host's Warthog,
#              deletes the boulder and undoes it (its own history, through
#              the host), places an Elite (its actor the host's: the joiner
#              sees its unit), sends the host a volley of malformed,
#              replayed and too many requests (dropped and counted, the host
#              plays on); the host destroys the Warthog and starts a NEW
#              ROUND (the layout whole again on both), saves the map variant,
#              the joiner saves its own copy; both monitors seen by the
#              other; screenshots at the Vita's picture (HALO_TEST_SHOTS)
#   forge3     three: a third machine joins the game in progress and is
#              given the layout (read through the map variant validator), sees
#              what the others placed, places one the others see, and leaves
#              holding it (the host lets it go); screenshots
#   forgeold   a machine that does not play network Forge - HALO_TEST_VITA_JOINER
#              (an older build, insider.15 by default when it is there) and
#              this build with HALO_FORGE_NET=0 - is refused from the Forge
#              game, then plays a Slayer game of the same host's build
#   forgeleave the host leaves its Forge game (the main menu): the joiner is
#              told, back at the menus
forge_shots() { # NAME: the copy's screenshots at the Vita's picture, in HALO_TEST_SHOTS/mpforge-shots
	[ -n "${HALO_TEST_SHOTS:-}" ] || return 0
	mkdir -p "$HALO_TEST_SHOTS/$1"
	echo "HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1"
}

# the host's Forge game, with HALO_TEST_COMMANDS commands
forge_host() { # forge_host COMMANDS [ENV...]
	local commands=$1
	shift
	run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=host:bloodgulch:forge \
		HALO_NETWORK_TEST_START=20 HALO_TEST_FORGE_PACKS=covenant_vehicles,covenant_ai "HALO_TEST_COMMANDS=$commands" "$@"
	host_pid=$last_pid
}

forge_check_clean() { # no assertion, exception or crash in the copies' logs
	local log
	for log in "$@"; do
		[ -f "$log" ] || continue
		grep -av "assertion skipped (torn read?)" "$log" | grep -aqiE "assert|exception|halt_and_catch_fire|segmentation" &&
			fail "$(basename "$(dirname "$log")"): an assertion, exception or crash"
	done
}

# the last "forge net" state of a log: its objects' lines
forge_state() { # LOG TICK: the lines of the state logged at that test command's tick
	sed -n "/test command at tick $2: forge net\$/,/test command at tick/p" "$1" | grep -a "forge net: #"
}

forge_case() {
	local hl=$out/host/run.log jl=$out/joiner/run.log hl2 jl2 host_cmds join_cmds join2_cmds
	case $mode in
	forge)
		host_cmds="L150:forge camera 60 -100 1.6 130 0;L160:forge spawn warthog;L165:forge distance 8;L170:forge drop"
		host_cmds="$host_cmds;L180:forge spawn boulder;L185:forge distance 6;L190:forge drop;L300:forge net"
		host_cmds="$host_cmds;L430:forge grab #3;L520:forge grab #3;L530:forge distance 5;L540:forge drop;L560:forge net"
		host_cmds="$host_cmds;L760:forge ai;L900:forge net;L920:forge destroy #1;L960:forge round;L1020:forge net"
		host_cmds="$host_cmds;L1040:forge camera 62 -102 1.6 128 6;L1060:@shot forge-host-sees-joiner;L1080:forge save Net Test"
		join_cmds="L250:forge net;L260:forge camera 60 -100 1.6 150 0;L270:forge spawn teleporter base;L280:forge distance 6"
		join_cmds="$join_cmds;L290:forge drop;L380:forge grab #3;L390:forge distance 7;L480:forge drop;L520:forge grab #3"
		join_cmds="$join_cmds;L600:forge grab #1;L610:forge distance 10;L620:forge drop;L640:forge delete #2;L650:forge undo"
		join_cmds="$join_cmds;L660:forge net;L700:forge spawn elite major;L705:forge distance 6;L710:forge drop;L800:forge net"
		join_cmds="$join_cmds;L840:forge net junk 60;L1030:forge net;L1040:forge camera 49 -90 3 322 -4"
		join_cmds="$join_cmds;L1062:@shot forge-joiner-sees-host;L1090:forge save Joiner Copy"
		forge_host "$host_cmds" $(forge_shots mpforge-shots/forge-host)
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join "HALO_TEST_COMMANDS=$join_cmds" \
			$(forge_shots mpforge-shots/forge-joiner)
		join_pid=$last_pid
		wait $join_pid $host_pid 2>/dev/null
		forge_check_clean "$hl" "$jl"
		echo "--- host"; grep -aE "forge net: (machine|refused|new round)|forge: .*(is holding|saved)" "$hl" | head -40
		echo "--- joiner"; grep -aE "forge net: (the host|holding|asked)|forge: (Undone|.*is holding|map variant)" "$jl" | head -40
		# (the handshake: the joiner plays it, given the game)
		grep -aq "forge net: machine 1 plays the Forge game" "$hl" || fail "the joiner did not answer the Forge hello"
		grep -aq "forge net: machine 1 given the Forge game" "$hl" || fail "the joiner was not given the Forge game"
		grep -aq "forge net: the host's layout read: .* (89 bytes, checked)\|forge net: the host's layout read: .*checked)" "$jl" ||
			fail "the joiner did not read the host's layout through the validator"
		# (the host's objects on the joiner: the Warthog at the host's index, the boulder its own copy)
		forge_state "$jl" 250 | grep -aq "#1 warthog networked object [0-9a-f]\{8\} " ||
			fail "the joiner did not have the host's Warthog (the netcode's)"
		forge_state "$jl" 250 | grep -aq "#2 boulder own copy object [0-9a-f]* " ||
			fail "the joiner did not have its own copy of the host's boulder"
		# (the joiner's object on the host)
		grep -aq "forge net: machine 1 spawned teleporter_base (#3)" "$hl" || fail "the joiner's teleporter base was not placed by the host"
		forge_state "$hl" 300 | grep -aq "#3 teleporter_base" || fail "the host did not have the joiner's teleporter base"
		# (conflicting grabs: the first wins, the other refused, both ways)
		sed -n '/test command at tick 430: forge grab #3/,+3p' "$hl" | grep -aq "forge: Player 2 is holding it" ||
			fail "the host's grab of the joiner's held object was not refused"
		# (the same tick's grabs: the host's own first, the joiner's refused by the host)
		grep -aq "forge net: refused machine 1's grab: held by another" "$hl" ||
			fail "the host did not refuse the joiner's grab of the object it held"
		grep -aq "forge: Player is holding it" "$jl" || fail "the joiner was not told the host held it"
		# (the joiner moved the host's Warthog, deleted the boulder and undid it, its own history)
		grep -aq "forge net: machine 1 dropped warthog (#1)" "$hl" || fail "the joiner did not move the host's Warthog"
		grep -aq "forge net: machine 1 deleted #2" "$hl" || fail "the joiner's delete was not made by the host"
		grep -aq "forge: Undone: Delete Boulder" "$jl" || fail "the joiner's undo was not made"
		forge_state "$jl" 660 | grep -aq "#2 boulder own copy" || fail "the boulder was not back on the joiner after the undo"
		forge_state "$hl" 900 | grep -aq "#2 boulder" || fail "the boulder was not back on the host after the undo"
		# (the joiner's Elite: the host's actor, the joiner sees its unit)
		grep -aq "forge net: machine 1 spawned elite major" "$hl" || fail "the joiner's Elite was not placed by the host"
		grep -aqE "forge: elite.* given a (free actor|swarm)" "$hl" || fail "the joiner's Elite has no actor on the host"
		forge_state "$jl" 800 | grep -aq "elite major.* networked object [0-9a-f]\{8\} " ||
			fail "the joiner did not see the Elite's unit"
		# (the junk: dropped and counted, the host played on)
		grep -aqE "forge net: machine 1: [1-9][0-9]* malformed, [1-9][0-9]* replayed, [1-9][0-9]* rate limited dropped" "$hl" ||
			fail "the host did not drop and count the joiner's junk"
		grep -aq "test command at tick 1020: forge net" "$hl" || fail "the host did not play on after the junk"
		# (NEW ROUND: the Warthog destroyed, whole again on both)
		grep -aq "forge net: new round 1: the layout's [0-9]* objects placed again" "$hl" || fail "the host's new round was not made"
		grep -aq "forge net: the host started round 1" "$jl" || fail "the joiner was not told of the new round"
		forge_state "$jl" 1030 | grep -aq "warthog networked object [0-9a-f]\{8\} " ||
			fail "the joiner did not have the Warthog after the new round"
		# (the monitors: each sees the other's)
		grep -aq "forge net: player 1 (Player 2): monitor" "$hl" || fail "the host did not see the joiner's monitor"
		grep -aq "forge net: player 0 (Player): monitor" "$jl" || fail "the joiner did not see the host's monitor"
		# (saving: the host's map variant, the joiner's own copy)
		grep -aq 'forge: map variant "Net Test" saved as bloodgulch/' "$hl" || fail "the host did not save its map variant"
		grep -aq 'forge: map variant "Joiner Copy" saved as bloodgulch/' "$jl" || fail "the joiner did not save its copy"
		ls "$out"/host/save/forge/bloodgulch/*.hmv "$out"/joiner/save/forge/bloodgulch/*.hmv 2>/dev/null
		local saved_host saved_join
		saved_host=$(sed -n 's/.*forge: map variant "Net Test" saved as .*, \([0-9]*\) objects/\1/p' "$hl" | tail -1)
		saved_join=$(sed -n 's/.*forge: map variant "Joiner Copy" saved as .*, \([0-9]*\) objects/\1/p' "$jl" | tail -1)
		echo "saved: the host's $saved_host objects, the joiner's copy $saved_join"
		[ -n "$saved_host" ] && [ "$saved_host" = "$saved_join" ] || fail "the joiner's copy did not hold the host's objects"
		;;
	forge3)
		forge_lan_machine lan2 192.168.1.4
		hl2=$out/joiner2/run.log
		host_cmds="L150:forge camera 60 -100 1.6 130 0;L160:forge spawn warthog;L165:forge distance 8;L170:forge drop"
		host_cmds="$host_cmds;L180:forge spawn boulder;L185:forge distance 6;L190:forge drop;L1500:forge net;L1700:forge net"
		host_cmds="$host_cmds;L1720:forge camera 54 -110 3 80 -5;L1745:@shot forge3-host"
		join_cmds="L250:forge camera 60 -100 1.6 150 0;L270:forge spawn teleporter base;L280:forge distance 6;L290:forge drop"
		join_cmds="$join_cmds;L1500:forge net;L1700:forge net;L1720:forge camera 50 -90 3 315 -6;L1745:@shot forge3-joiner"
		join2_cmds="L1350:forge net;L1370:forge camera 58 -96 2 200 0;L1390:forge spawn shade;L1400:forge distance 7"
		join2_cmds="$join2_cmds;L1450:forge net;L1500:forge camera 66 -104 3 150 -5;L1560:@shot forge3-late-joiner;L1620:@menu"
		forge_host "$host_cmds" $(forge_shots mpforge-shots/forge3-host)
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join "HALO_TEST_COMMANDS=$join_cmds" \
			$(forge_shots mpforge-shots/forge3-joiner)
		join_pid=$last_pid
		# (the third joins the game in progress, a minute in)
		sleep "${HALO_TEST_LATE_JOIN:-55}"
		run_copy joiner2 "$lan2" "$vita" "$cpu_c" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join \
			"HALO_TEST_COMMANDS=$join2_cmds" $(forge_shots mpforge-shots/forge3-late-joiner)
		join2_pid=$last_pid
		wait $join_pid $join2_pid $host_pid 2>/dev/null
		forge_check_clean "$hl" "$jl" "$hl2"
		echo "--- host"; grep -aE "forge net: (machine|refused)" "$hl" | head -30
		echo "--- late joiner"; grep -aE "forge net: (the host|holding|asked|#)" "$hl2" | head -30
		grep -aq "forge net: machine 2 plays the Forge game" "$hl" || fail "the late joiner did not answer the Forge hello"
		grep -aqE "forge net: the host's layout read: [3-9] objects, .*checked\)" "$hl2" ||
			fail "the late joiner was not given the layout (checked) with the others' objects"
		grep -aqE "forge net: the host's Forge game had: [3-9] objects \([3-9] linked\)" "$hl2" ||
			fail "the late joiner did not link the layout's objects"
		forge_state "$hl2" 1350 | grep -aq "#1 warthog networked object [0-9a-f]\{8\} " ||
			fail "the late joiner did not have the host's Warthog"
		forge_state "$hl2" 1350 | grep -aq "teleporter_base own copy object" ||
			fail "the late joiner did not have the first joiner's teleporter base"
		grep -aq "forge net: machine 2 spawned c gun turret" "$hl" || fail "the late joiner's Shade was not placed by the host"
		forge_state "$jl" 1700 | grep -aq "c gun turret networked" || fail "the first joiner did not see the late joiner's Shade"
		grep -aq "forge net: machine 2 left the Forge game" "$hl" || fail "the host did not see the late joiner leave"
		sed -n '/machine 2 left the Forge game/,+2p' "$hl" | grep -aq "forge: dropped Shade" ||
			fail "the host did not let go of what the late joiner held as it left"
		local log
		for log in "$hl" "$jl"; do
			sed -n "/test command at tick 1500: forge net\$/,/test command at tick/p" "$log" | grep -aq "forge net: player 2 (.*): monitor" ||
				fail "$(basename "$(dirname "$log")") did not see the late joiner's monitor"
		done
		;;
	forgeold)
		forge_lan_machine lan2 192.168.1.4
		local old=${HALO_TEST_VITA_JOINER:-$root/../wt-insider15/build/linux/halo}
		forge_host "" HALO_EXIT_AFTER=70
		sleep 5
		if [ -x "$old" ]; then
			echo "an older build: $old"
			run_copy joiner "$lan" "$old" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=60
			join_pid=$last_pid
		fi
		run_copy joiner2 "$lan2" "$vita" "$cpu_c" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_FORGE_NET=0 HALO_EXIT_AFTER=60
		join2_pid=$last_pid
		wait ${join_pid:-} $join2_pid $host_pid 2>/dev/null
		forge_check_clean "$hl" "$out/joiner2/run.log"
		echo "--- host"; grep -aE "forge net: (machine|refused)" "$hl" | head
		if [ -x "$old" ]; then
			grep -aqE "forge net: machine [0-9]+ did not answer the Forge hello" "$hl" || fail "the older build was not refused"
			grep -aq "unable to join game: reason= #0/_rejection_code_version_too_old" "$out/joiner/data/debug.txt" ||
				fail "the older build was not told it failed to join"
		fi
		[ "$(grep -ac "did not answer the Forge hello in" "$hl")" -ge "$([ -x "$old" ] && echo 2 || echo 1)" ] ||
			fail "the machine without network Forge was not refused"
		grep -aq "unable to join game: reason= #0/_rejection_code_version_too_old" "$out/joiner2/data/debug.txt" ||
			fail "the machine without network Forge was not told it failed to join"
		grep -a "network test: tick" "$out/joiner2/run.log" | grep -aq "| playing" && fail "the refused machine played the Forge game"
		# (then a Slayer game of this build's host: both play it)
		mv "$out/host" "$out/host-forge"; mv "$out/joiner2" "$out/joiner2-forge"
		[ -d "$out/joiner" ] && mv "$out/joiner" "$out/joiner-forge"
		run_copy host "$host_machine" "$vita" "$cpu_a" HALO_NET_ONLINE=false HALO_NETWORK_TEST=host:bloodgulch:slayer \
			HALO_NETWORK_TEST_START=20 HALO_EXIT_AFTER=75; host_pid=$last_pid
		sleep 5
		if [ -x "$old" ]; then
			run_copy joiner "$lan" "$old" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_EXIT_AFTER=70
			join_pid=$last_pid
		fi
		run_copy joiner2 "$lan2" "$vita" "$cpu_c" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join HALO_FORGE_NET=0 HALO_EXIT_AFTER=70
		join2_pid=$last_pid
		wait ${join_pid:-} $join2_pid $host_pid 2>/dev/null
		local played
		for side in joiner joiner2; do
			[ -f "$out/$side/run.log" ] || continue
			played=$(grep -a "network test: tick" "$out/$side/run.log" | grep -a "| playing" | grep -aEc "player [0-9]+:.* player [0-9]+:")
			echo "$side's seconds playing the Slayer game with others: $played"
			[ "$played" -ge 15 ] || fail "$side did not play the Slayer game ($played s)"
		done
		grep -aq "forge net:" "$out/host/run.log" && fail "the Slayer game's host spoke network Forge"
		;;
	forgeleave)
		host_cmds="L150:forge camera 60 -100 1.6 130 0;L160:forge spawn warthog;L165:forge distance 8;L170:forge drop;L600:@menu"
		join_cmds="L300:forge net"
		forge_host "$host_cmds"
		sleep 5
		run_copy joiner "$lan" "$vita" "$cpu_b" HALO_NET_ONLINE=false HALO_NETWORK_TEST=join "HALO_TEST_COMMANDS=$join_cmds"
		join_pid=$last_pid
		wait $join_pid $host_pid 2>/dev/null
		forge_check_clean "$hl" "$jl"
		grep -aE "forge net|host|menus|error" "$jl" | tail -12
		forge_state "$jl" 300 | grep -aq "#1 warthog networked" || fail "the joiner did not have the host's Warthog"
		grep -aq "menus: the host's game closed: said for System Link" "$jl" || fail "the joiner was not told the host's game closed"
		sed -n '/the host.s game closed/,$p' "$jl" | grep -aq "system link: menus" || fail "the joiner was not back at the menus"
		grep -aq "forge net: the host's Forge game left" "$jl" || fail "the joiner's Forge session did not end with the host's game"
		;;
	esac
}
