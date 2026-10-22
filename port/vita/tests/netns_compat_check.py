#!/usr/bin/env python3
"""netns_compat_check.py: the checks of run_netns_online_test.sh's
dedicatedcompat mode (netns_dedicated_compat.sh), from the logs alone.

The server: its run.log (sv_status every 5 s, the joins, game rules net,
infection's round and turns) and debug.txt. Each client (--client
NAME:PLAYER:FOLDER:EXIT): its run.log, whose "network test: tick" lines (one
a second, the same in 1.1.0 and the insider builds) give every player's team
(t), weapons (hex definition:rounds) and this machine's own player (local).

One line a check, [PASS], [FAIL], [WARN] (said, not failed) or [NOT RUN] (the
run never got there: a FAIL unless --lenient); exit 1 on any FAIL.

--gametype (the cycle's first game's, "infection" by default): any other
game type skips Infection's round, turns and skull, and checks instead that
the server's game ended, each client saw its end, the two clients agree on
every player's team and (a team game) see each player on the team the
server put them on, and the server kept its teams within one of each other
once settled ("teams:" in its sv_status).
"""
import argparse
import collections
import os
import re
import sys

ZOMBIE_TEAM = 0  # infection.c's infection_side_team: zombies t0, humans t1
HUMAN_TEAM = 1

results = []


def check(state, name, detail=""):
    results.append((state, name, detail))
    print("[%s] %s%s" % (state, name, (": " + detail) if detail else ""), flush=True)


def ok(cond, name, detail=""):
    check("PASS" if cond else "FAIL", name, detail)
    return cond


def read(path):
    try:
        with open(path, errors="replace") as f:
            return f.read().splitlines()
    except OSError:
        return []


TICK = re.compile(r"network test: tick (\d+)(.*?) \| items .*? \| (playing|game over) to -?\d+ .*?\| local (-?\d+) camera")
SEGMENT = re.compile(r"(?= player \d+: )")
TAIL = re.compile(r" s-?\d+ k\d+ d\d+ f\d+ t(\d+) m(-?\d+)\s*$")
WEAPONS = re.compile(r" thr-?[\d.]+((?: [0-9a-f]+:-?\d+)*) s-?\d+ k\d+")
NAMED = re.compile(r"network test: player (\d+) is named (.*)$")


def parse_ticks(lines):
    """[(tick, state, local, {number: (team, alive, weapons tuple)}, names)]"""
    names, ticks = {}, []
    for line in lines:
        m = NAMED.search(line)
        if m:
            names[m.group(1)] = m.group(2).strip()
            continue
        m = TICK.search(line)
        if not m:
            continue
        players = {}
        for segment in SEGMENT.split(m.group(2))[1:]:
            number = segment.split(":", 1)[0].split()[1]
            tail = TAIL.search(segment)
            if not tail:
                continue
            alive = ": dead" not in segment.split(" s", 1)[0]
            weapons = ()
            w = WEAPONS.search(segment)
            if alive and w:
                weapons = tuple(x.split(":")[0] for x in w.group(1).split())
            players[number] = (int(tail.group(1)), alive, weapons)
        ticks.append((int(m.group(1)), m.group(3), m.group(4), players, dict(names)))
    return ticks


CRASH = re.compile(r"egmentation fault|SIGSEGV|SIGABRT|SIGBUS|EXCEPTION halt|halt_and_catch_fire|stack trace|"
                   r"fatal signal|AddressSanitizer|runtime error:", re.I)
ASSERT = re.compile(r"assert", re.I)
ASSERT_BENIGN = re.compile(r"assertion skipped \(torn read\?\)|assertions", re.I)


def crash_lines(lines):
    bad = [l for l in lines if CRASH.search(l)]
    bad += [l for l in lines if ASSERT.search(l) and not ASSERT_BENIGN.search(l)]
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-log", required=True)
    ap.add_argument("--server-debug", required=True)
    ap.add_argument("--server-rc", type=int, default=0)
    ap.add_argument("--most", type=int, default=16)
    ap.add_argument("--client", action="append", required=True, help="NAME:PLAYER:FOLDER:EXIT")
    ap.add_argument("--lenient", action="store_true", help="a check the run never reached is a warning")
    ap.add_argument("--gametype", default="infection")
    ap.add_argument("--teams", action="store_true", help="a team game (not Infection)")
    args = ap.parse_args()
    not_run = "WARN" if args.lenient else "NOT RUN"

    sl = read(args.server_log)
    sd = read(args.server_debug)
    clients = []
    for spec in args.client:
        name, player, folder, rc = spec.split(":", 3)
        clients.append({"name": name, "player": player, "folder": folder, "rc": int(rc or 0),
                        "log": read(os.path.join(folder, "run.log")),
                        "debug": read(os.path.join(folder, "data", "debug.txt"))})
        clients[-1]["ticks"] = parse_ticks(clients[-1]["log"])
    v110 = clients[0]

    print("== the server")
    # ---- joins, refusals
    for c in clients:
        joined = any(re.search(r"server: player #\d+ %s joined" % re.escape(c["player"]), l) for l in sl)
        ok(joined, "%s (%s) joined the server's game" % (c["player"], c["name"]))
    refusals = [l for l in sl + sd if re.search(
        r"refused from the Infection game|game is full|its address has its share|client connection refused|"
        r"game rules net: .*refused|joins too often", l)]
    ok(not refusals, "no one refused", refusals[0].strip() if refusals else "")
    rules = [l.split("game rules net: ", 1)[1] for l in sl if "game rules net: machine" in l]
    for line in rules[:6]:
        check("INFO", "game rules net", line)
    infection = args.gametype == "infection"
    winner = None
    turned = []
    if not infection:
        started = any(re.search(r"server: the game starts: %s on " % re.escape(args.gametype), l) for l in sl)
        ok(started, "the server's %s game started" % args.gametype)
        ended = [l for l in sl if re.search(r"server: the game ends \(", l)]
        ok(bool(ended), "the server's game ended", ended[0].split("server: ", 1)[1].strip() if ended else "never")
    # ---- the round
    began = infection and any("infection: the round begins" in l for l in sl)
    if infection:
        ok(began, "the server's Infection round began")
        over = [re.search(r"infection: the round is over \((.*?)\)", l) for l in sl]
        over = [m.group(1) for m in over if m]
        if over:
            winner = "humans" if "humans win" in over[0] else "zombies" if "zombies win" in over[0] else None
        ok(winner is not None, "the server's round ended with a winner", over[0] if over else "never ended")
        for l in sl:
            m = re.search(r"infection: (.+?) is a zombie \((.*?)\)", l)
            if m:
                turned.append((m.group(1), m.group(2)))
        check("INFO", "players turned on the server", "%d (%s)" % (len(turned), ", ".join("%s: %s" % t for t in turned[:8])))
    # ---- the teams: within one of each other once settled (sv_status's "teams:")
    if args.teams:
        sizes = []
        for l in sl:
            m = re.search(r"server: teams: red (\d+)\+(\d+), blue (\d+)\+(\d+)", l)
            if m:
                p0, b0, p1, b1 = map(int, m.groups())
                sizes.append((p0 + b0, p1 + b1, p0, p1))
        played = [z for z in sizes if z[2] + z[3] > 0 and z[0] + z[1] >= args.most - 1]
        uneven = [z for z in played if abs(z[0] - z[1]) > 1]
        ok(len(played) >= 5 and len(uneven) <= max(1, len(played) // 10), "the server's teams within one of each other",
           "%d sv_status with people in and the game filled, %d uneven: %s" % (len(played), len(uneven),
                                                                                sorted(set(played))[:6]))
    # ---- the bots: once settled, people + bots = most - 1 (a free slot)
    status = []
    for l in sl:
        m = re.search(r"server: \S+ on \S+, (\d+) of (\d+) players \((\d+) bots", l)
        if m:
            status.append((int(m.group(1)), int(m.group(3))))
    with_people = [s for s in status if s[0] > s[1]]
    short = [s for s in with_people if s[0] == args.most - 1]
    full = [s for s in with_people if s[0] >= args.most and s[1] > 0]
    detail = "%d sv_status with people in, %d at %d of %d; counts (players, bots): %s" % (
        len(with_people), len(short), args.most - 1, args.most, sorted(set(with_people)))
    ok(len(with_people) >= 10 and len(short) >= 0.7 * len(with_people), "the bots keep the game at most-1", detail)
    ok(not full, "a slot always free (never full with bots)", str(full[:3]) if full else "")
    bots_most = max([s[1] for s in with_people], default=0)
    ok(bots_most >= args.most - 1 - len(clients), "the bots filled the game",
       "most bots seen %d (want %d with %d people)" % (bots_most, args.most - 1 - len(clients), len(clients)))
    # ---- crashes
    bad = crash_lines(sl) + [l for l in sd if CRASH.search(l)]
    ok(not bad, "the server: no crash or assertion", bad[0].strip()[:160] if bad else "")
    # (the test stops it with TERM; timeout -k kills it 5 s on: 137, slow to stop, said, not failed)
    if args.server_rc in (0, 143):
        check("PASS", "the server stopped cleanly", "exit %d" % args.server_rc)
    else:
        check("WARN", "the server stopped", "exit %d%s" % (args.server_rc,
              " (still running 5 s after TERM: killed)" if args.server_rc == 137 else ""))

    # ---- each client
    for c in clients:
        print("== %s (%s)" % (c["name"], c["player"]))
        ticks = c["ticks"]
        bad = crash_lines(c["log"]) + [l for l in c["debug"] if CRASH.search(l)]
        ok(not bad, "%s: no crash or assertion" % c["name"], bad[0].strip()[:160] if bad else "")
        ok(c["rc"] == 0, "%s exited cleanly" % c["name"], "exit %d" % c["rc"])
        dash = [l for l in c["log"] if "XLaunchNewImage" in l]
        ok(not dash, "%s: never at the dashboard" % c["name"])
        lost = [l for l in c["log"] if "lost the connection" in l]
        ok(not lost, "%s: never lost the connection" % c["name"], lost[0].strip()[:120] if lost else "")
        problems = [l for l in c["log"] if "connection problem" in l]
        if problems:
            check("WARN", "%s: %d connection problem lines" % (c["name"], len(problems)), problems[0].strip()[:120])
        playing = [t for t in ticks if t[1] == "playing" and len(t[3]) >= 2]
        ok(len(playing) >= 40, "%s played with others" % c["name"], "%d s playing with another player" % len(playing))
        seen = max([len(t[3]) for t in ticks], default=0)
        ok(seen >= args.most - 1, "%s sees the players and bots" % c["name"],
           "most players in one second %d (the game's %d)" % (seen, args.most - 1))
        first = next((i for i, t in enumerate(ticks) if t[1] == "playing"), None)
        ended = first is not None and any(t[1] == "game over" for t in ticks[first:])
        ok(ended, "%s saw the game end" % c["name"])
        here = [re.search(r"infection: game over here: the (\w+) win", l) for l in c["log"]]
        here = [m.group(1) for m in here if m]
        if not infection:
            pass
        elif here:
            ok(here[0] == winner, "%s's winner is the server's" % c["name"], "%s here, %s on the server" % (here[0], winner))
        elif c is not v110:
            ok(False, "%s logged Infection's end (infection: game over here)" % c["name"])
        # (its own player: the name it has)
        own = None
        for t in ticks:
            if t[2] != "-1" and t[4].get(t[2]) == c["player"]:
                own = t[2]
        c["own"] = own
        ok(own is not None, "%s knows its own player" % c["name"], "player %s" % own)

    if not infection:
        return finish(clients, sl, args)
    # ---- the zombies' skull: the weapon the zombies hold alone, as either client sees them
    print("== teams and the skull")
    held = collections.Counter()
    for c in clients:
        hands = collections.Counter()
        for _, state, _, players, _ in c["ticks"]:
            if state == "playing":
                for number, (team, alive, weapons) in players.items():
                    if team == ZOMBIE_TEAM and alive:
                        hands[len(weapons)] += 1
        check("INFO", "%s: the zombies' hands as it sees them" % c["name"], "%d zombie-seconds alive: %s" % (
            sum(hands.values()), ", ".join("%d with %d weapons" % (n, k) for k, n in sorted(hands.items())) or "none"))
        teams = collections.Counter(team for t in c["ticks"] if t[1] == "playing" for team, _, _ in t[3].values())
        mine = collections.Counter(t[3][t[2]][0] for t in c["ticks"] if t[1] == "playing" and t[2] in t[3])
        check("INFO", "%s: the teams it sees" % c["name"], "all players' seconds t0 %d, t1 %d; its own player's t0 %d, t1 %d" % (
            teams[0], teams[1], mine[0], mine[1]))
    for c in clients:
        for _, state, _, players, _ in c["ticks"]:
            if state != "playing":
                continue
            for number, (team, alive, weapons) in players.items():
                if number != c.get("own") and team == ZOMBIE_TEAM and alive and len(weapons) == 1:
                    held[weapons[0]] += 1
    skull = held.most_common(1)[0][0] if held else None
    ok(skull is not None, "the zombies hold one weapon (the skull) on the clients",
       "definition %s (%s)" % (skull, ", ".join("%s x%d" % kv for kv in held.most_common(3))))

    # ---- the 1.1.0 client's own player: turned, its team switched on its side, the skull alone
    v110_turn = [why for name, why in turned if name == v110["player"]]
    own = v110.get("own")
    if not v110_turn:
        check(not_run, "the 1.1.0 player was turned this run",
              "Old110 never became a zombie on the server: its team change and skull not exercised (run again)")
    else:
        killed = "killed by a zombie" in v110_turn
        check("PASS" if killed else not_run, "the 1.1.0 player was killed by a zombie",
              "turned: %s" % ", ".join(v110_turn))
        series = [(t[0], t[3][own][0], t[3][own][1], t[3][own][2]) for t in v110["ticks"]
                  if t[1] == "playing" and own in t[3]]
        switch = None
        for (a, b) in zip(series, series[1:]):
            if a[1] == HUMAN_TEAM and b[1] == ZOMBIE_TEAM:
                switch = b[0]
                break
        first_zombie = next((s[0] for s in series if s[1] == ZOMBIE_TEAM), None)
        ok(first_zombie is not None, "the 1.1.0 client has its own player on the zombies' team",
           "first t0 at tick %s; human->zombie switch seen at tick %s" % (first_zombie, switch))
        if killed:
            ok(switch is not None, "the 1.1.0 client saw its own team switch (human to zombie)",
               "at tick %s" % switch)
        # (its weapons once a zombie, 3 s on: the skull alone; a zombie by its
        # own team, or else by holding the skull, its team aside)
        since = first_zombie if first_zombie is not None else 1 << 40
        zombie_alive = [s for s in series if s[1] == ZOMBIE_TEAM and s[2] and s[0] >= since + 90]
        if not zombie_alive and skull is not None:
            zombie_alive = [s for s in series if s[2] and skull in s[3]]
        right = [s for s in zombie_alive if skull is not None and s[3] == (skull,)]
        wrong = collections.Counter(s[3] for s in zombie_alive if s[3] != (skull,))
        if not zombie_alive:
            check(not_run, "the 1.1.0 zombie's weapons", "never alive as a zombie 3 s after turning")
        else:
            ok(len(right) >= 3 and len(right) >= 0.9 * len(zombie_alive), "the 1.1.0 zombie holds only the skull",
               "%d of %d s alive as a zombie with the skull alone; otherwise %s" % (
                   len(right), len(zombie_alive), dict(wrong.most_common(3)) or "never"))
        # (the skull with its own team still the humans': the team change lost on its side)
        human = [s for s in series if s[1] == HUMAN_TEAM and s[2] and s[3]]
        if human and skull:
            skulled = [s for s in human if skull in s[3]]
            ok(len(skulled) <= 2, "the 1.1.0 player's team is the zombies' whenever it holds the skull",
               "%d s with the skull while its own team said human (t1)" % len(skulled))

    # ---- every player the server turned, a zombie on each client
    turned_names = sorted(set(name for name, _ in turned))
    for c in clients:
        named_zombie = set()
        known = set()
        for _, state, _, players, names in c["ticks"]:
            for number, (team, _, _) in players.items():
                name = names.get(number)
                if name:
                    known.add(name)
                    if team == ZOMBIE_TEAM and state == "playing":
                        named_zombie.add(name)
        expected = [n for n in turned_names if n in known]
        missing = [n for n in expected if n not in named_zombie]
        if not expected:
            check(not_run, "%s: the server's turns on its side" % c["name"], "no turned player known by name")
            continue
        ok(len(missing) <= max(1, len(expected) // 10), "%s: the server's turns on its side" % c["name"],
           "%d of %d turned players seen as zombies%s" % (
               len(expected) - len(missing), len(expected), (": missing " + ", ".join(missing[:5])) if missing else ""))

    return finish(clients, sl, args)


def games_of(ticks):
    """each second's game: a new game when the tick count starts again"""
    games, game, last = [], 0, None
    for t in ticks:
        if last is not None and t[0] + 300 < last:
            game += 1
        last = t[0]
        games.append(game)
    return games


def quitters(ticks):
    """the players of each game dead for 20 s or more in a row: those who left
    (Halo keeps a player who quits on the scores, always dead)"""
    gone, run = set(), collections.Counter()
    for game, t in zip(games_of(ticks), ticks):
        for number, (team, alive, _) in t[3].items():
            key = (game, number)
            run[key] = 0 if alive else run[key] + 1
            if run[key] >= 20:
                gone.add(key)
    return gone


def agree_teams(clients):
    a, b = clients[0], clients[1]
    bt = {(g, t[0]): t for g, t in zip(games_of(b["ticks"]), b["ticks"]) if t[1] == "playing"}
    keys = sorted(bt)
    samples = agree = 0
    import bisect
    for game, (tick, state, _, players, _) in zip(games_of(a["ticks"]), a["ticks"]):
        if state != "playing" or not keys:
            continue
        i = bisect.bisect_left(keys, (game, tick))
        near = min((k for k in keys[max(0, i - 1):i + 1]), key=lambda k: (k[0] != game, abs(k[1] - tick)))
        if near[0] != game or abs(near[1] - tick) > 15:
            continue
        other = bt[near][3]
        for number, (team, _, _) in players.items():
            if number in other:
                samples += 1
                agree += other[number][0] == team
    return agree, samples


def finish(clients, sl, args):
    print("== the teams on the clients")
    agree, samples = agree_teams(clients)
    strict = args.gametype != "infection"
    ok(samples >= 100 and agree >= (0.98 if strict else 0.9) * samples, "the clients agree on the teams (no desync)",
       "%d of %d player-seconds agree" % (agree, samples))
    if args.teams:
        # (the people's teams - which never change in a game - as the server
        # put them, sv_players's, on each client; then the team sizes each
        # client sees, bots and people, within one of each other)
        server_team = {}
        for l in sl:
            m = re.search(r"server: #\d+\s+(\S+)\s+team (\d+)\s+machine", l)
            if m:
                server_team[m.group(1)] = int(m.group(2))
        people = [c["player"] for c in clients]
        for c in clients:
            # (each person's team in each game as it sees it: one a game; its
            # first game's the server's, sv_players's, which ran in it)
            seen = {}
            for game, (_, state, _, players, names) in zip(games_of(c["ticks"]), c["ticks"]):
                for number, (team, _, _) in players.items():
                    name = names.get(number)
                    if name in people and state == "playing":
                        seen.setdefault((name, game), set()).add(team)
            first = {n: seen.get((n, 0)) for n in people}
            wrong = [n for n in people if n in server_team and first[n] != {server_team[n]}]
            split = [k for k, v in seen.items() if len(v) > 1]
            ok(all(first[n] for n in people) and not wrong and not split,
               "%s sees the people on the server's teams" % c["name"],
               "first game %s; the server's %s; a team change in a game: %s" % (
                   {n: sorted(first[n] or ()) for n in people}, {n: server_team.get(n) for n in people}, split or "none"))
            gone = quitters(c["ticks"])
            full = even = 0
            for game, t in zip(games_of(c["ticks"]), c["ticks"]):
                if t[1] != "playing":
                    continue
                here = {n: v for n, v in t[3].items() if (game, n) not in gone}
                if len(here) < args.most - 1:
                    continue
                full += 1
                even += abs(sum(1 for v in here.values() if v[0] == 0) - sum(1 for v in here.values() if v[0] == 1)) <= 1
            ok(full >= 20 and even >= 0.9 * full, "%s sees even teams" % c["name"],
               "%d of %d full seconds within one (players who left not counted)" % (even, full))
    fails = [r for r in results if r[0] in ("FAIL", "NOT RUN")]
    print("== %s: %d checks, %d failed%s" % ("PASS" if not fails else "FAIL", len(results), len(fails),
                                            "".join("\n   - %s" % r[1] for r in fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
