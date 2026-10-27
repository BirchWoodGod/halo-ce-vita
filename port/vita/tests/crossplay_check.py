#!/usr/bin/env python3
"""The checks of run_netns_crossplay_test.sh: one game between a host and its
clients, read from every copy's log, step by step, and where it stopped.

The steps and their pass rules are ChupathingyCE's (tools/crossplay_test.py,
CC0, by Milenko: https://github.com/ChupathingyCE/chupathingyce), whose
network test log lines (port/linux/game/network_test.c) are the same in
OpenCE, ChupathingyCE, DamnationCE and our tree: every second each machine
logs every player's position, health, score, kills, deaths, team and machine.
Ours adds the stages before a game (discover, version, join, settings) and a
diagnosis: the log lines that say why a copy never got further.

    crossplay_check.py direction NAME --host DIR --client DIR [--client DIR ...]
        [--score N] [--rejoin] [--json FILE]
    crossplay_check.py server NAME --host DIR --client DIR [--client DIR ...] [--json FILE]

DIR is a copy's folder: run.log (its output) and data/debug.txt. With
--rejoin the --clients after the first are the copy that joined game 1 in
progress and stayed, and the one that came back after the first left.
Exit status 0 every step passed, 1 a step failed (the first failing stage
is printed as "stage: NAME"), 2 bad arguments.
"""

import argparse
import json
import os
import re
import statistics
import sys

TICKS_PER_SECOND = 30
DEATH_SEEN_TICKS = 2 * TICKS_PER_SECOND
POSITION_TOLERANCE = 2.0

TICK_LINE = re.compile(r"network test: tick (\d+)(.*?) \| items .*? \| (playing|game over) to (-?\d+) \|.*? \| "
    r"hits (\d+) dealt (\d+) rejected (\d+) replayed (\d+) \| local (-?\d+)")
PLAYER = re.compile(r" player (\d+): (dead|\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) h(-?[\d.]+)/(-?[\d.]+).*?) "
    r"s(-?\d+) k(-?\d+) d(-?\d+) f(-?\d+) t(-?\d+) m(-?\d+)(?= player \d+:|$)")
TROUBLE_LINES = re.compile(r"(Segmentation fault|SIGSEGV|SIGABRT|Aborted|stack trace|crashed|AddressSanitizer)", re.I)

# the stages, in order, and the log lines that name each one's failure
# (any copy's run.log or debug.txt); the first stage a game failed is where
# the report points, with these lines as its evidence
STAGES = ["discover", "signal", "version", "join", "settings", "play", "spawn", "kills", "client_damage", "score",
    "end_of_game", "rejoin", "stability"]
DIAGNOSES = [
    ("discover", re.compile(r"ignoring a host that is not a Vita|ignoring a Vita's host|"
        r"other side of the Vita-only line|Vita-only", re.I)),
    ("discover", re.compile(r"no game has code|never listed|the public games list is empty", re.I)),
    ("version", re.compile(r"not joining a host of network version|newer version of the network|"
        r"older version of the network|plays network version|does not speak Delta|"
        r"another network version|network version \d+ .*(refus|ignor)", re.I)),
    ("join", re.compile(r"refus|rejected the join|join token|game is full|could not join|cannot join|"
        r"join .*failed|failed to join|lost the connection|connection (to the host )?(was )?(lost|closed)", re.I)),
    ("settings", re.compile(r"different game layout|game settings .*(size|layout)|settings_update", re.I)),
    ("messages", re.compile(r"unknown (packet|message) type|unknown message|unknown distributed|"
        r"bad (message|packet)|malformed", re.I)),
]


def read_lines(folder, name, end=None):
    """the file's lines; with end, (those before byte end, those after)"""
    path = os.path.join(folder, name)
    if not os.path.exists(path):
        return [] if end is None else ([], [])
    with open(path, "rb") as file:
        data = file.read()
    if end is None:
        return data.decode("utf-8", "replace").splitlines()
    return (data[:end].decode("utf-8", "replace").splitlines(), data[end:].decode("utf-8", "replace").splitlines())


def stop_offsets(folder):
    """(run.log, debug.txt) sizes as the harness stopped a PC build's copy
    (run_netns_crossplay_test.sh mark_stop), else None: ours are held to
    their whole logs"""
    try:
        with open(os.path.join(folder, "kind")) as file:
            if file.read().strip() == "ours":
                return None
        with open(os.path.join(folder, "stop_offsets")) as file:
            out, debug = file.read().split()
        return int(out), int(debug)
    except (OSError, ValueError):
        return None


class Copy:
    def __init__(self, folder):
        self.folder = folder
        self.name = os.path.basename(os.path.normpath(folder))
        self.out = read_lines(folder, "run.log")
        self.debug = read_lines(folder, os.path.join("data", "debug.txt"))
        # (a PC build's lines after the harness stopped it: its faults at
        # being stopped are noted, not a crash in the game)
        self.after_stop = []
        offsets = stop_offsets(folder)
        if offsets:
            before_out, after_out = read_lines(folder, "run.log", offsets[0])
            before_debug, after_debug = read_lines(folder, os.path.join("data", "debug.txt"), offsets[1])
            self.after_stop = after_out + after_debug
            self.stability_lines = before_out + before_debug
        else:
            self.stability_lines = self.out + self.debug
        # (a PC build known to fault once its game is over, its own control
        # games too, named by the harness: XPPC_POSTGAME_FAULT_KINDS, the
        # copies' kinds; OpenCE 6ec42856, docs/crossplay.md 10.9): its trouble
        # after its first game's end is noted, not a crash in the game
        self.after_game = []
        kinds = os.environ.get("XPPC_POSTGAME_FAULT_KINDS", "").split()
        try:
            with open(os.path.join(folder, "kind")) as file:
                kind = file.read().strip()
        except OSError:
            kind = ""
        if kind and kind != "ours" and kind in kinds:
            over = next((i for i, line in enumerate(self.out) if "network test: tick" in line and "| game over to" in line),
                None)
            if over is not None:
                late = [line.strip() for line in self.out[over:] if TROUBLE_LINES.search(line)]
                self.after_game = late
                self.stability_lines = [line for line in self.stability_lines
                    if not (TROUBLE_LINES.search(line) and any(l and l in line for l in late))]
        self.games, self.scripted = read_games(self.out)


class Game:
    def __init__(self):
        self.ticks = []
        self.host_game = None

    def first(self):
        return self.ticks[0][0]

    def last(self):
        return self.ticks[-1][0]

    def over(self):
        return any(not entry[2] for entry in self.ticks)

    def end(self):
        return next((entry[1] for entry in self.ticks if not entry[2]), None)

    def both(self):
        return sum(1 for entry in self.ticks if len(entry[1]) >= 2)


def read_games(lines):
    games, scripted, last_tick = [], 0, None
    for line in lines:
        if "network test: the first player kills the last" in line:
            scripted += 1
            continue
        match = TICK_LINE.search(line)
        if not match:
            continue
        # (no game engine: the main menu's level, which a copy that never
        # joined logs too)
        if int(match.group(4)) == 0:
            continue
        tick = int(match.group(1))
        if last_tick is None or tick < last_tick:
            games.append(Game())
        last_tick = tick
        players = {}
        for player in PLAYER.finditer(match.group(2)):
            state = {"alive": player.group(2) != "dead", "score": int(player.group(8)), "kills": int(player.group(9)),
                "deaths": int(player.group(10)), "team": int(player.group(12)), "machine": int(player.group(13))}
            if state["alive"]:
                state["position"] = (float(player.group(3)), float(player.group(4)), float(player.group(5)))
                state["health"] = float(player.group(6))
            players[int(player.group(1))] = state
        counters = {"hits": int(match.group(5)), "dealt": int(match.group(6)), "rejected": int(match.group(7))}
        games[-1].ticks.append((tick, players, match.group(3) == "playing", int(match.group(4)), counters))
    return games, scripted


class Seconds:
    def __init__(self, game):
        self.ticks = [entry[0] for entry in game.ticks]
        self.players = [entry[1] for entry in game.ticks]

    def near(self, tick, within=TICKS_PER_SECOND // 2):
        import bisect
        index = bisect.bisect_left(self.ticks, tick)
        best = None
        for candidate in (index - 1, index):
            if 0 <= candidate < len(self.ticks) and abs(self.ticks[candidate] - tick) <= within and \
                    (best is None or abs(self.ticks[candidate] - tick) < abs(self.ticks[best] - tick)):
                best = candidate
        return (self.ticks[best], self.players[best]) if best is not None else (None, None)


def tally(players):
    return {index: (state["kills"], state["deaths"]) for index, state in players.items()}


def match_games(host_games, client_games):
    floor = 0
    for game in client_games:
        best, best_score = None, 0
        for number in range(floor, len(host_games)):
            seconds = Seconds(host_games[number])
            score = sum(1 for entry in game.ticks
                if (lambda p: p is not None and tally(p) == tally(entry[1]))(seconds.near(entry[0])[1]))
            if score > best_score:
                best, best_score = number, score
        game.host_game = best
        if best is not None:
            floor = best


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def step(passed, detail):
    return {"pass": bool(passed), "detail": detail}


def joined(copy):
    return any("network test: joining" in line for line in copy.out)


def diagnose(copies):
    """the lines that name a failure, by stage, from every copy (at most a
    few of each)"""
    found = {}
    for copy in copies:
        seen = set()
        for line in copy.out + copy.debug:
            for stage, pattern in DIAGNOSES:
                if pattern.search(line):
                    text = re.sub(r"^\S+ \S+\s+", "", line.strip())[:200]
                    key = (stage, text)
                    if key in seen:
                        continue
                    seen.add(key)
                    found.setdefault(stage, [])
                    if len(found[stage]) < 6:
                        found[stage].append(f"{copy.name}: {text}")
    return found


def stability(copies):
    trouble = {}
    stopped = []
    for copy in copies:
        lines = [line.strip() for line in copy.stability_lines if TROUBLE_LINES.search(line)][:3]
        if lines:
            trouble[copy.name] = lines
        if any(TROUBLE_LINES.search(line) for line in copy.after_stop):
            stopped.append(copy.name)
        if copy.after_game:
            stopped.append(copy.name + " (after its game: " + copy.after_game[0][:80] + ")")
    note = "; PC copies that faulted only as the harness stopped them or after their game: " + ", ".join(stopped) \
        if stopped else ""
    return step(not trouble, "no copy crashed" + note if not trouble else trouble)


def check_direction(host, clients, score_to_win, rejoin):
    steps = {}
    for copy in clients:
        match_games(host.games, copy.games)
    first = clients[0]
    pairs = [(copy, game, host.games[game.host_game]) for copy in clients for game in copy.games
        if game.host_game is not None]

    # before a game: found the host (and its version), joined, got the settings
    diag = diagnose([host] + clients)
    # (found: it joined a game it saw, or an invite or listing led it to ask)
    found = joined(first) or any(game.both() for game in first.games) or \
        any("Internet play: joining" in line for line in first.out)
    heard = found or "version" in diag
    steps["discover"] = step(heard, "the client found the host" if found else
        "the client heard the host (and refused it: below)" if heard else "the client never found the host")
    # (a client in the game: one it logged with another player in it; a
    # client that never joined may play a game of its own)
    in_game = any(game.both() for game in first.games)
    # (internet play: an invite opened, or a listing joined, reaches the host
    # through the brokers' signalling, then the tunnel connects)
    asked = any("Internet play: joining" in line for line in first.out)
    connected = any(re.search(r"Internet play: connected to host|Internet play: joining .*, reached already", line)
                    for line in first.out)
    steps["signal"] = step(not asked or connected, "no internet play" if not asked else
        "the tunnel to the host connected" if connected else
        "the client asked to join through the brokers and never reached the host")
    steps["version"] = step(in_game or "version" not in diag,
        "no version refusal" if in_game or "version" not in diag else "the client refused the host's network version")
    played = bool(host.games) and any(game.both() for _, game, _ in pairs) and any(g.both() for g in host.games)
    steps["join"] = step(in_game, "joined: the client logged the game" if in_game else
        "the client tried to join but never got into the game" if found else "never joined")
    steps["settings"] = step("settings" not in diag, "settings record taken" if "settings" not in diag else
        "the client dropped the host's settings record")
    steps["play"] = step(played, f"the host logged {sum(len(g.ticks) for g in host.games)} s of play, the clients "
        f"{sum(len(g.ticks) for c in clients for g in c.games)} s" if played else
        "no game with both players on both machines")
    later = ["spawn", "kills", "client_damage", "score", "end_of_game"] + (["rejoin"] if rejoin else [])
    if not played:
        for name in later:
            steps[name] = step(False, "not reached: no game played together")
        steps["stability"] = stability([host] + clients)
        return steps, diag

    # (a client that joined a game in progress logs its seconds at other ticks
    # than the host, a few ticks off: the host's position there is taken
    # between its seconds on either side, where the player was alive at both;
    # ChupathingyCE's loopback copies log at the same ticks)
    gaps, alive_both = [], False
    for _, game, host_game in pairs:
        host_ticks = host_game.ticks
        import bisect
        keys = [entry[0] for entry in host_ticks]
        for tick, players, *_ in game.ticks:
            at = bisect.bisect_left(keys, tick)
            if at < len(keys) and keys[at] == tick:
                before = after = host_ticks[at]
            elif 0 < at < len(keys) and keys[at] - keys[at - 1] <= TICKS_PER_SECOND + 2:
                before, after = host_ticks[at - 1], host_ticks[at]
            else:
                continue
            share = 0.0 if after[0] == before[0] else (tick - before[0]) / (after[0] - before[0])
            other = {}
            for index in set(before[1]) & set(after[1]):
                one, two = before[1][index], after[1][index]
                if one["alive"] and two["alive"] and one["deaths"] == two["deaths"]:
                    other[index] = {"alive": True, "position": tuple(a + (b - a) * share for a, b in
                        zip(one["position"], two["position"]))}
                elif not one["alive"] and not two["alive"]:
                    other[index] = {"alive": False}
            for index, state in players.items():
                seen = other.get(index)
                if state["alive"] and seen and seen["alive"]:
                    gaps.append(distance(state["position"], seen["position"]))
            if len(players) >= 2 and len(other) >= 2 and all(s["alive"] for s in players.values()) and \
                    all(s["alive"] for s in other.values()):
                alive_both = True
    if gaps:
        gaps.sort()
        median = statistics.median(gaps)
        detail = (f"both players alive on both machines: {alive_both}; positions compared at {len(gaps)} "
            f"player-seconds, median gap {median:.3f}, largest {gaps[-1]:.3f} world units")
    else:
        median, detail = None, "no second logged by both machines with a living player"
    steps["spawn"] = step(alive_both and median is not None and median <= POSITION_TOLERANCE, detail)

    events = []
    for number, game in enumerate(host.games):
        before = {}
        for tick, players, *_ in game.ticks:
            for index, state in players.items():
                if index in before and state["deaths"] > before[index]:
                    events.append((number, tick, index, state["deaths"]))
                before[index] = state["deaths"]
    seen, missed, standing, unwatched = 0, [], [], 0
    for number, tick, victim, count in events:
        watching = [(copy, game) for copy, game, _ in pairs if game.host_game == number and
            game.first() <= tick - TICKS_PER_SECOND and game.last() >= tick + DEATH_SEEN_TICKS]
        if not watching:
            unwatched += 1
            continue
        seconds = Seconds(host.games[number])
        for copy, game in watching:
            counted = next((entry[0] for entry in game.ticks if entry[0] >= tick - DEATH_SEEN_TICKS and
                victim in entry[1] and entry[1][victim]["deaths"] >= count), None)
            body = next((entry[0] for entry in game.ticks if tick - DEATH_SEEN_TICKS <= entry[0] <= tick +
                DEATH_SEEN_TICKS and victim in entry[1] and not entry[1][victim]["alive"]), None)
            if counted is not None and counted <= tick + DEATH_SEEN_TICKS and body is not None:
                seen += 1
            else:
                missed.append((copy.name, number + 1, tick, victim))
            for entry in game.ticks:
                if tick + TICKS_PER_SECOND <= entry[0] <= tick + 2 * TICKS_PER_SECOND:
                    host_state = seconds.near(entry[0])[1] or {}
                    # (and still dead on the host a second on: a client
                    # alive within a sample of the host's respawn - its
                    # clock in step with the host's, or a tick ahead, the
                    # death seen up to a sample late - is no body standing)
                    host_later = seconds.near(entry[0] + TICKS_PER_SECOND)[1] or {}
                    mine = entry[1].get(victim)
                    if mine and mine["alive"] and victim in host_state and not host_state[victim]["alive"] and \
                            victim in host_later and not host_later[victim]["alive"]:
                        standing.append((copy.name, number + 1, entry[0], victim))
    steps["kills"] = step(seen and not missed and not standing,
        f"{len(events)} deaths on the host ({host.scripted} scripted; {unwatched} with no client there); {seen} seen "
        f"on a client within 2 s" + (f"; missed {missed[:5]}" if missed else "") +
        (f"; bodies standing on a client {standing[:5]}" if standing else ""))

    dealt = max((entry[4]["dealt"] for game in host.games for entry in game.ticks), default=0)
    sent = sum(max((entry[4]["hits"] for game in c.games for entry in game.ticks), default=0) for c in clients)
    steps["client_damage"] = step(dealt > 0, f"the clients reported {sent} hits; the host dealt {dealt}")

    def table(players):
        return {index: (state["score"], state["kills"], state["deaths"]) for index, state in (players or {}).items()}

    host_end = host.games[0].end()
    client_end = next((game.end() for _, game, _ in pairs if game.host_game == 0 and game.end()), None)
    reached = max((value[0] for value in table(host_end).values()), default=0) >= score_to_win
    steps["score"] = step(host_end is not None and table(host_end) == table(client_end) and reached,
        f"end of game 1 (player: score, kills, deaths): host {table(host_end) or 'never ended'}, client "
        f"{table(client_end) or 'never ended'}; to win {score_to_win}")
    second = len(host.games) >= 2 and any(entry[2] for entry in host.games[1].ticks)
    steps["end_of_game"] = step(host_end is not None and client_end is not None,
        f"game 1 over on the host: {host_end is not None}, on a client: {client_end is not None}; "
        f"a second game on the host: {second}")
    if rejoin:
        # (ChupathingyCE's: a second client joins game 1 in progress and stays,
        # so that the first leaving does not end it; the first leaves, and a
        # new copy comes back into it)
        parts, passed = [], True
        for copy in clients[1:]:
            in_game = bool(copy.games) and copy.games[0].host_game == 0
            both = sum(game.both() for game in copy.games)
            death = any(not state["alive"] or state["deaths"] > 0 for game in copy.games for entry in game.ticks
                for state in entry[1].values())
            passed = passed and joined(copy) and in_game and both >= 5 and death
            parts.append(f"{copy.name}: joined {joined(copy)}, into game 1 {in_game}, {both} s with the others, "
                f"a death seen {death}")
        left = bool(first.games) and first.games[0].host_game == 0 and \
            first.games[-1].last() < host.games[0].last() - 3 * TICKS_PER_SECOND
        steps["rejoin"] = step(passed and left, f"the first client left game 1: {left}; " + "; ".join(parts))
    steps["stability"] = stability([host] + clients)
    return steps, diag


def check_server(server, clients):
    """a dedicated server with two clients: both play, and see the same game
    (ChupathingyCE's check_server: the server runs no scripted kills)"""
    steps = {}
    diag = diagnose([server] + clients)
    first = clients[0]
    # (the client under test is the last; the first, an OpenCE client, is the
    # other player: with the one under test out, it plays alone)
    subject = clients[-1]
    found = joined(subject) or any(game.both() for game in subject.games) or \
        any("Internet play: joining" in line for line in subject.out)
    steps["discover"] = step(found or "version" in diag, f"{subject.name} found the server" if found else
        f"{subject.name} heard the server (and refused it: below)" if "version" in diag else
        f"{subject.name} never found the server")
    asked = any("Internet play: joining" in line for line in subject.out)
    # (or "reached already": OpenCE's browser of 39 pings a listed game, its
    # tunnel up before the join asks: docs/opence-net39-spec.md, crossplay.md
    # 10.12)
    connected = any("Internet play: connected to host" in line or
                    re.search(r"Internet play: joining .*, reached already", line) for line in subject.out)
    steps["signal"] = step(not asked or connected, "no internet play" if not asked else
        "the tunnel to the server connected" if connected else
        f"{subject.name} asked to join through the brokers and never reached the server")
    in_game = any(game.both() for game in subject.games)
    steps["version"] = step(in_game or "version" not in diag, "no version refusal" if in_game or "version" not in diag
        else f"{subject.name} refused the server's network version")
    steps["join"] = step(in_game, ", ".join(
        f"{c.name}: {'in the game' if any(g.both() for g in c.games) else 'tried, never in the game' if joined(c) else 'never joined'}"
        for c in clients))
    steps["settings"] = step("settings" not in diag, "settings record taken" if "settings" not in diag else
        "a client dropped the server's settings record")
    played = {c.name: sum(game.both() for game in c.games) for c in clients}
    steps["play"] = step(all(value >= 10 for value in played.values()),
        ", ".join(f"{name}: {value} s with both players" for name, value in played.items()))
    if len(clients) >= 2 and steps["play"]["pass"]:
        ticks = {entry[0]: entry[1] for game in clients[0].games for entry in game.ticks}
        gaps, agree, differ = [], 0, 0
        for game in clients[1].games:
            for tick, players, *_ in game.ticks:
                other = ticks.get(tick)
                if not other or len(other) != len(players) or len(players) < 2:
                    continue
                for index, state in players.items():
                    seen = other.get(index)
                    if state["alive"] and seen and seen["alive"]:
                        gaps.append(distance(state["position"], seen["position"]))
                if tally(players) == tally(other):
                    agree += 1
                else:
                    differ += 1
        median = statistics.median(gaps) if gaps else None
        steps["spawn"] = step(bool(gaps) and median <= POSITION_TOLERANCE,
            f"positions compared at {len(gaps)} player-seconds, median gap {median:.3f}" if gaps else
            "no second logged by both clients")
        steps["score"] = step(agree > 0 and differ <= 2, f"kills and deaths the same at {agree} seconds, "
            f"different at {differ} (two allowed)")
    elif len(clients) >= 2:
        steps["spawn"] = step(False, "not reached")
        steps["score"] = step(False, "not reached")
    steps["stability"] = stability([server] + clients)
    return steps, diag


def report(name, steps, diag, json_path):
    failed = [stage for stage in STAGES if stage in steps and not steps[stage]["pass"]]
    for stage in STAGES:
        if stage in steps:
            print(f"  {name} {stage}: {'pass' if steps[stage]['pass'] else 'FAIL'}: {steps[stage]['detail']}")
    first = failed[0] if failed else None
    if first:
        print(f"  {name} stage: {first}")
        for stage, lines in diag.items():
            for line in lines:
                print(f"  {name} evidence ({stage}): {line}")
    else:
        print(f"  {name}: every step passed")
    if json_path:
        with open(json_path, "w") as file:
            json.dump({"name": name, "pass": not failed, "stage": first, "steps": steps, "evidence": diag}, file,
                indent=2)
    return 0 if not failed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("kind", choices=["direction", "server"])
    parser.add_argument("name")
    parser.add_argument("--host", required=True)
    parser.add_argument("--client", action="append", required=True)
    parser.add_argument("--score", type=int, default=4)
    parser.add_argument("--rejoin", action="store_true")
    parser.add_argument("--json")
    args = parser.parse_args()
    host = Copy(args.host)
    clients = [Copy(folder) for folder in args.client]
    if args.kind == "server":
        steps, diag = check_server(host, clients)
    else:
        steps, diag = check_direction(host, clients, args.score, args.rejoin)
    return report(args.name, steps, diag, args.json)


if __name__ == "__main__":
    sys.exit(main())
