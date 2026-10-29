# OpenCE network version 39: a clean-room specification

Written Oct 27 2026 for the 39 work (worktree wt-xpv39). This is the "dirty
room" output: the observable wire format and behaviour a peer needs to play
with OpenCE at network version 39 (origin/main 337b05cf). It was written by
reading every commit in OpenCE 7fb941b0..337b05cf (7fb941b0 is the commit
that defines 38; 31 commits) and the source at 337b05cf. It holds facts,
numbers, layouts and rules only. **No OpenCE code, comments or prose are
reproduced here, and none may be copied into this repository.** OpenCE is
AGPL-3.0-or-later since 4b09168e; implementers on our side (GPL-3.0-only)
work from this document and our own code, not from OpenCE's sources.

Names such as `_distributed_message_*` appear only where needed to refer to
a wire constant by the name both projects' format records use.

Notation as in `docs/opence-net32-spec.md`: "host", "client", "stream" (the
reliable per-client TCP connection), "datagram" (UDP), offsets in bytes,
integers little-endian unless stated. "Ours" means this tree as of ba0943eb
(client joins 29, 32, 38; hosts announce 32; `docs/crossplay.md` 10.9, 10.11).

Status tags: **[match]** ours already behaves this way; **[differs]** ours
differs and should change; **[n/a]** nothing to do for interop, noted so
nobody re-investigates it; **[optional]** compatible either way.

---

## 0. Summary of 38 -> 39

Version 39 is a small raise. It marks a *dedicated server* (a host with no
player of its own, `halo --server`) in the LAN advertisement and the
internet listing. Two later changes ride on 39 with no further raise: a new
client-to-host message (kind 92) and a new rejection code (7). Everything
else a dedicated server does goes over messages that 38 already has.

| Change | Kind | Ours today |
|---|---|---|
| Version number 38 -> 39; acceptance is still an exact match plus the distributed flag; the host still never checks a client's version (1) | negotiation | **[differs]**: 39 is not in our accepted list |
| Advertisement flags byte: new bit 0x04 "dedicated server" (2.1) | wire, informational | **[differs]** cosmetically: our client takes 0x04 to mean a ChupathingyCE (Delta) host and tags the game that way |
| Listing flags byte: new bit 0x20 "dedicated server" (2.2) | wire, informational | **[match]**: our crossplay listing reader masks 0x20 off; the game reads and joins as before |
| Distributed kind 92, client build report, client to host, stream, 756 bytes, optional (3) | wire: new kind (no raise) | host: **[match]** (dropped as an unknown kind, silently); client: **[optional]** (not sent; OpenCE hosts show "no build reported") |
| Rejection code 7 "player name refused" by a dedicated server's word filter (4) | wire: new value (no raise) | **[differs]**: our code 7 means "Vitas only", so our client would show the wrong reason |
| Dedicated host has no player of its own; its lobby, countdown, rotation, notices and shutdown use existing messages (5) | host behaviour | expected **[match]**; needs a runtime check (T3-T6) |
| Text chat on a public dedicated server (6) | host/client rule, kinds 87-89 unchanged | **[match]** (our client follows the host's chat config in every game) |
| Master servers: HTTPS list of signed listings; dedicated servers are by default *not* on the MQTT brokers (7) | new discovery path | **[differs]**: our browser reads the brokers only, so public v39 dedicated servers don't show up there (invite and LAN still work) |
| `network.client_timeout_seconds` (5-300 s, default 15), local to each machine (8) | timing, local | **[n/a]** (nothing on the wire changes) |

**Answer to "does a client that accepts 39 and plays exactly 38's rules
play correctly?": yes.** Nothing in simulation, the settings record (13,136
bytes, gametype options 0x2C, unchanged), the join request, the join
accept, tick windows, keep-alives or any 38 message changed. Our 33-36 rule
switches test `version >= n`, so adding 39 to the accepted list plays 38's
rules. Three fixes are needed to be *correct* rather than just playable:
the 0x04 tag (2.1), the meaning of code 7 (4), and the runtime checks for
a host with no player (5).

---

## 1. Version negotiation (unchanged rules, new number)

1.1 The network version constant is 39. A v39 machine announces 39 in the
advertisement (reserved bytes 0..1, u16 LE) and the listing (u16 **BE**
after the format byte). Its client joins a LAN or invite game only when the
advertised version equals 39 *and* the distributed bit 0x01 is set. If not,
it refuses with the same three messages as before (equal version without
0x01, host newer, host older). Hosts never check a joining client's
version, and nothing in the join request carries it.

1.2 Browser (brokers, MQTT): unchanged. Listings of exactly 39 are shown.
Live listings of a newer version are counted (by key hash) to tell the
player that newer games exist. Older ones are dropped.
Browser (master servers, section 7): listings of another version are
*shown greyed and unjoinable* and sorted last. That is new, but it applies
only to master-listed games.

1.3 **Ours.** Add 39 to the crossplay accepted list (`network_wire.h`). It
plays 38's rules. Keep 38: ChupathingyCE servers and older OpenCE builds
still announce 38. A Vita host announcing 39 would need the host side of
33-38 first (we only have their client side today), so hosting is out of
scope here. **[differs]**

---

## 2. The dedicated-server flags

2.1 **Advertisement.** The flags byte (reserved offset 2) gains bit **0x04**,
set on every LAN answer from a host running in dedicated-server mode. It is
never set by a player's host. Full byte at 39: 0x01 distributed, 0x02 game
in progress (loading, playing or postgame), 0x04 dedicated. Offset 3 keeps
v37's game kind. OpenCE's client shows the flag in its lobby list and its
`--probe` JSON ("dedicated"). Nothing else reads it, and joining is
unaffected.

Conflict: ChupathingyCE also sets 0x04 (its "Delta" bit), and our
`crossplay_guard_platform_of_advertisement` maps 0x04 to
CROSSPLAY_PLATFORM_CHUPA. Against an OpenCE v39 dedicated server our client
would tag the game as ChupathingyCE's. That is a label only (no rule
depends on it). Suggested: when the version is 39 or higher, read 0x04 as
"dedicated" (tag SERVER / OpenCE). For ChupathingyCE's own 38 servers, keep
the existing reading. **[differs]** (cosmetic)

2.2 **Listing** ("HL", format 1, label `hceu-lobby-1`, unchanged layout):
the flags byte at offset 5 gains bit **0x20** (32), dedicated. Flags at 39:
1 open, 2 in progress, 4 teams, 8 closed (tombstone), 16 password, 32
dedicated. The stamp (8 bytes: kind, then 7 zero) and the signature are
unchanged. Sizes stay 140..268 bytes. A v39 browser does **not** trust this
bit for its DEDICATED tab or for chat (any host can set it). A game counts
as dedicated only when a master server's entry says so (section 7). A
broker-only listing is shown as a player's game whatever its flag.

Our crossplay reader keeps only 1/2/4/8/16/64/128 and drops 32, so the
listing parses, verifies and joins as before. ChupathingyCE marks dedicated
with 128. Our format-2 (Vita) listing uses 32 for "PC map", but that is a
different format and label, so there's no clash. **[match]**; showing
"dedicated" from 0x20 is **[optional]** and only cosmetic.

---

## 3. Kind 92: the client's build report (client to host, optional)

3.1 **Layout.** `_distributed_message_client_build` = 92. Total size is
exactly 756 bytes: the 8-byte distributed header plus one 748-byte entry.

| Offset | Size | Field | Content |
|---|---|---|---|
| 0 | 2 | message header | packet type 2 (data), size 756, flags 0 |
| 2 | 1 | kind | 92 |
| 3 | 1 | count | 1 |
| 4 | 4 | game tick | the sender's tick (s32) |
| 8 | 12 | number | build number as decimal text ("282"; "0" for an unnumbered build) |
| 20 | 16 | flavor | "release", "debug" or "local" |
| 36 | 144 | repository | GitHub "owner/name" the build was made from, or empty |
| 180 | 64 | version | the label the game shows, e.g. "OpenCE Linux \| build 282 (release)" |
| 244 | 512 | mods | enabled mods in load order, one per line, "folder version" (version omitted if none), lines separated by 0x0A; a line that would overflow is dropped whole |

Each field is NUL-terminated within its size. The rest of the field is zero
(the sender zero-fills the message). Characters are printable ASCII
0x20-0x7E. Only "mods" may also contain 0x0A. Trailing spaces and line
feeds are trimmed.

3.2 **Sender.** Only a client sends it, reliably over its stream, once per
game. It first goes out on the client's per-tick update right after the
client's first client_ready (kind 9) of that game, in the same place where
the client's Discord identity (kind 18) is sent. The "sent" flag resets at
each new game. It is never sent in a datagram and never sent by a host.

3.3 **Receiver: host.** The host accepts it only when it came from a client
machine index (not from the host), over that client's stream (a datagram
copy is dropped, like kinds 9, 11 and 18), while a game is in progress,
with count >= 1 and total size >= 8 + count x 748. It reads the first entry
only, forces each field's terminator, and keeps only printable characters
(plus 0x0A in mods). The kept copy is replaced by any later copy and
cleared when the machine slot is reloaded or a new game starts. The host
logs it once and shows it in the dedicated server's `/api/players`
("client": build/flavor/version/repository/mods, or null). It also passes
the generic tick-window check, like any other message.

3.4 **Not required.** No host refuses, kicks, delays or treats differently a
client that never sends it. An older host (38, or ours) drops it as an
unknown kind. That is the defined compatible behaviour, and it is why the
version was not raised again.

3.5 **Ours.** Host: kind 92 is at or above our NUMBER_OF_DISTRIBUTED_MESSAGES
(78) and is not intercepted earlier (87-91 are), so the generic check
returns silently. 756 bytes is within our reliable-message limit (0x1000).
**[match]**. Client: **[optional]**. Sending it would let OpenCE server
operators see our build. If we do, keep "mods" empty unless we decide
otherwise. The repository field is public text, so send it only if the
repo is public.

---

## 4. Rejection code 7: name refused

4.1 The join refusal codes gain one value. 0 version too old, 1 too new,
2 bad join token, 3 bad password, 4 full, 5 closed, 6 blacklisted machine,
**7 player name refused**. It is carried in the existing machine-rejected
message, in the existing field.

4.2 **When.** Only a dedicated server with `server.chat_filter = "block"`
sends it, and only when the joining machine's name (the join request's
machine name) matches the filter's word list. For the check, characters
outside 0x20-0x7E are read as spaces, so our platform mark in the name's
last character (0x56xx) is ignored. A v39 client shows "The host refused:
your player name". A pre-7 OpenCE build shows its generic refusal.

4.3 **Ours: [differs].** Our `_rejection_code_vitas_only` is also 7, and our
client shows the "Vitas only" text for it. Suggested: on the crossplay wire
with a non-Vita host, read 7 as "the host refused your player name". On the
legacy wire, keep "Vitas only". (Our host sends 7 only on the legacy wire,
where OpenCE never is.)

---

## 5. What a joining client sees from a dedicated host

All of this is host-side logic. The client receives only messages that 38
already defines.

5.1 **No host player.** The host machine is in the game as machine index 0
but owns **no player**. The game's player list, player count and
advertisement count hold only the joined machines' players. The lobby can
sit with 0 players. A client must not assume machine 0 (or player 0) is
the host's, or that the host has any player for display, spectating, team
balance, scoreboard or "host name" purposes. The host's own action queue
is allowed to hold zero actions per tick. The relayed actions and unit
states a client receives simply have no host-player entries.

5.2 **Lobby and start.** The countdown begins when at least
`server.min_players` (default 1, range 1-16) players are in. A machine
count rule still applies. The countdown is `server.lobby_seconds` (default
15, range 0-300) and uses the normal countdown messages. A team game may
start with every player on one team (`server.team_start = "one"`, the
default; "both" waits for one player on each team; "skip" plays the next
non-team entry). The host doesn't wait for a host player to precache. Its
own map load is still required. Max players is `server.max_players`
(default 16, range 2-127), so our >16 experimental guard can apply.

5.3 **Rotation.** After a game the scores show for `server.postgame_seconds`
(default 12). At the start of the postgame the host broadcasts a notice
(kind 17) of the form "Next game in N s: <map>, <gametype>". Then it goes
back to the pregame lobby with the next entry's map and gametype, the way a
player host does after its postgame. A game with no players ends
(`server.empty_seconds`, default 30; at once when the last player leaves),
as does one with no score change for `server.idle_minutes` (default 0,
meaning never). Co-op entries (`<level>:coop`) are possible.

5.4 **Notices and moderation.** `say`/`tell`, the MOTD and moderation
messages are notices, kind 17 (text up to 159 characters plus NUL; our
limit is the same 160). The MOTD (up to 127 characters) goes once to each
joining machine about 5 s after its player appears. Kick and ban use the
existing machine drop and notices. A timed ban is enforced only by
`bans.txt` (rejoining is refused with code 6 while it lasts). Mute uses the
existing chat-mute path. There are no votes beyond the existing votekick.
**[match]** (we handle kind 17 the same way).

5.5 **Shutdown.** On SIGTERM/SIGINT, `sv_stop`, or a drain, a game in
progress goes to its postgame. The host then sends the existing
graceful-shutdown message to every machine before it exits. A draining
server's advertisement reports the game as closed, and joins are refused
as "closed" (code 5).

5.6 **Our client: expected [match], unverified.** Everything above is 38's
message set. The open risk is code of ours that expects the host to have a
player (5.1). Check it at runtime (T3, T4).

---

## 6. Text chat (kinds 87-89 unchanged)

A dedicated server's chat follows its `network.text_chat` setting ("on"
by default, "links", or "off") even while it is listed. A player's listed
game still has chat forced off. The chat config (kind 89) is sent exactly
as before. A v39 client honours a host's chat in a game it joined from the
browser only if a master server listed that game as dedicated. In every
other browser-joined game it ignores chat. With `server.chat_filter`, the
host either stars filtered words in relayed lines ("censor") or refuses the
line with a notice to the sender ("block"). Line formats are unchanged.
**Ours: [match]** (our client follows the host's config in every game and
filters locally).

---

## 7. Master servers (new discovery path, no game-protocol change)

7.1 A public dedicated server (`server.public` true, `network.online`,
`network.public_lobby`) publishes its normal signed listing (section 2.2)
to HTTPS master servers. It is **not** published to the MQTT brokers unless
`server.list_on_brokers = true` (default false). Joining a master-listed
game uses the listing's invite token over the usual brokers and tunnel, so
the join path is unchanged.

7.2 **What a browser reads:** `GET <master>/v1/servers` (plain HTTP
allowed, because the body is signed). The JSON is `{"protocol":1,
"master":{"name","key_id","time"},"servers":[...]}`, up to 512 entries,
dedicated first. Each entry has the listing fields plus "publisher"
(id, name, since), "region", "build", "variant", "dedicated", "reachable",
"updated", and "listing" (base64 of the exact signed listing bytes). The
response header `Signature: ed25519=<base64>` is the master key's signature
over the exact (decompressed) body. `Signature-Key` is the master's key id:
the first 8 bytes of SHA-256 of its public key, as 16 lowercase hex digits.

A client must:
- verify the signature against the key pinned for that master;
- require protocol 1, a matching key id, and `master.time` within 300 s of
  its own clock;
- decode and verify each entry's listing, skipping bad entries;
- display the *listing's* name, map and counts (not the entry's);
- merge duplicates by id or listing key;
- never ping listed servers.

The built-in master, used when `masters.txt` lists none, is
`https://master.opencommunityedition.com` with key
`ed25519:89f4eda71229f11f3538679f49ef7fb0c2b407b380093f7f4fe02105b92225aa`.
Up to 4 masters are used.

7.3 Masters probe a publishing server's tunnel UDP port with a 35-byte
"OPCE-PROBE" datagram, which the server answers with a signed 103-byte ack.
That matters only if *we* publish to masters. **[n/a]**

7.4 **Ours: [differs]** for discovery only. Our browser won't list public
v39 dedicated servers unless their operators turn on
`server.list_on_brokers`. Invites and LAN work. Reading masters on the Vita
needs HTTPS, JSON and Ed25519 (we already have Monocypher). It's
**[optional]** and an owner decision (third-party service).

---

## 8. Other changes checked: no wire effect

- `network.client_timeout_seconds` (default 15, range 5-300) replaces the
  fixed 15 s silence timeout, the stream write-stall timeout and the host's
  "network lost" grace. Each machine reads its own value, and nothing is
  exchanged. An operator who sets 5 s drops a client after 5 s of silence.
  **[n/a]**
- `network.public_address` puts an operator-given address first in the
  host's tunnel candidates. The candidate format is unchanged. **[n/a]**
- Bans keyed by address, real address or hardware id; timed bans
  (`until=` in `bans.txt`). Host-local. **[n/a]**
- Browser default ALL GAMES, sorting, filters, join-progress words, the
  `--probe` CLI, a web admin page/RCON/HTTP API, Docker/Helm/panel files,
  release signing, frame pacing, shader cache, controller layouts, the
  Gun Game plasma fix (host-side gametype logic; the result reaches clients
  through normal object replication). **[n/a]**
- `struct` hashes in OpenCE's format record: the only change 38 -> 39 is
  the added `distributed_client_build`. Settings record, advertisement,
  join request/accept and every other message are unchanged.

**Last commit that changes the wire:** 09123e997c51a4a6b40fd89176fe4f95f2fc83e6
(rejection code 7). Later commits (5388f2f4, the controller-layouts merge
3c7ed45e, frame pacing, shader cache) are local. Pin
**337b05cfdf203e7c3e3d8af0b1b37894c0b5b057** (origin/main, same wire as
09123e99). Earlier wire points: 815d3a0c (39 and flags), d0acb3bd (kind 92).

---

## 9. Running OpenCE v39 for black-box tests

Build 337b05cf like before (`port/vita/tests/build_pc_crossplay.sh opence
<checkout> 337b05cf`). The Linux binary includes the server mode. It needs
`maps/` with `ui.map` and the rotation's maps; otherwise it exits with code 2.

**Dedicated server.**
- Start: `./halo --server` (or `halo-server`, `HALO_SERVER=1`, or
  `server.enabled = true`). Use `--config <path>` or `HALO_CONFIG_FILE`
  for the settings file. `./halo --version` prints the build and network
  version.
- First start: the server writes the `server.*` settings into config.toml.
  If no web password exists it makes one (stdout only).
- Ready line on stdout: "server: ready (tunnel <port>/udp, ...)". The invite
  link is printed on stdout only.
- Rotation: `HALO_SERVER_ROTATION="bloodgulch:ctf, prisoner:slayer+score=25"`
  (entries `map[:gametype[+rule=value]]`). Gametype names include slayer,
  team_slayer, ctf, king, oddball, race, and OpenCE's swat/infection/
  gun_game/hardcore (we refuse those).
- Useful settings for tests:
  - `HALO_SERVER_MIN_PLAYERS=1`
  - `HALO_SERVER_LOBBY_SECONDS=5`
  - `HALO_SERVER_POSTGAME_SECONDS=5`
  - `HALO_SERVER_EMPTY_SECONDS=30`
  - `HALO_SERVER_TEAM_START=one|both|skip`
  - `HALO_SERVER_MAX_PLAYERS`
  - `HALO_SERVER_MOTD="..."`
  - `HALO_SERVER_PASSWORD` (browser only; an invite bypasses it)
  - `HALO_NET_TEXT_CHAT=on|links|off`
  - `HALO_SERVER_CHAT_FILTER=off|censor|block` (with `_SLURS/_SWEARING/_INSULTS = off|mild|strong|severe`; slurs default mild), which triggers code 7
  - `HALO_NET_CLIENT_TIMEOUT_SECONDS`
- Console commands on stdin or in `init.txt`: `sv_status`, `sv_players`,
  `say`, `tell`, `kick`, `ban <p> 30m <reason>`, `unban`, `mute`, `sv_map`,
  `sv_map_next`, `sv_end_game`, `sv_rotation_set`, `sv_set`, `sv_stop
  [now|game|empty]`, `quit`.
- SIGTERM: postgame plus graceful shutdown. SIGHUP: reload.
- Web admin: `server.web_port` (default 8080 on 127.0.0.1); set 0 to turn it off.
- **LAN only:** `HALO_NET_ONLINE=false`. The LAN advertisement (UDP
  5150/5151, TCP 5150/5153 as usual) carries 0x04. `./halo --probe` on the
  LAN prints the first game as JSON, including "dedicated" and
  "network_version". Use it to check both their host and ours.
- **Internet, private:** `HALO_SERVER_PUBLIC=false`. Join by invite. The
  tunnel UDP port defaults to 2302 for a server (`HALO_NET_TUNNEL_PORT`).
- **Internet, on the brokers (so our browser sees it):**
  `HALO_SERVER_PUBLIC=true HALO_NET_PUBLIC_LOBBY=true
  HALO_SERVER_LIST_ON_BROKERS=true`. The listing has flag 0x20.
- **Masters:** for offline netns tests set `HALO_SERVER_MASTER_PUBLISH=false`
  (otherwise it tries the official master over HTTPS). A real master
  listing needs a `--keygen` publisher key approved by a master.

**Offline tests, and other details (checked at 337b05cf).**
- **Master servers.** A client (or a server) contacts the master servers
  only while its server browser is open, which in the test harness means
  `HALO_NETWORK_TEST=browse`. A plain LAN `join` never fetches them. To
  turn the fetch off but keep the brokers, set `HALO_NET_LISTINGS=brokers`
  (`network.listings`: `both` default, `masters`, `brokers`).
  `HALO_NET_ONLINE=false` also stops it, together with all internet play.
  Two settings do *not* stop it:
  - An empty or comments-only `masters.txt` falls back to the built-in
    official master.
  - `network.browser_source` only filters what the browser shows.
- **What the dedicated server contacts by default:**
  - The master publish (HTTPS). Off with `HALO_SERVER_MASTER_PUBLISH=false`
    or `HALO_SERVER_PUBLIC=false`.
  - The MQTT brokers (`brokers.txt`) and the STUN servers
    (`network.stun_servers`, default stun.l.google.com:19302 and
    stun.cloudflare.com:3478). Both are part of internet play: off with
    `HALO_NET_ONLINE=false`, which also skips binding the tunnel port.
  - Release updates: `HALO_SERVER_AUTO_UPDATE` defaults to `off`. The
    game's own updater (`update.auto` / `HALO_UPDATE_AUTO`, default true)
    acts only in builds CI numbered. A locally built binary (build number
    0) never checks, and network-test runs skip it too.
  - Release signing is a CI step only and contacts nothing at run time.
- **Environment variable names.** `HALO_SERVER_WEB_PORT` (0 = no web port)
  and `HALO_SERVER_NAME` are correct.
- **Rotation.** `HALO_SERVER_ROTATION` splits entries on `,`, `;` or a
  newline, with or without spaces, so
  `bloodgulch:slayer+score=10,bloodgulch:team_slayer` is two entries.
- **Ending a game.** Rotation rules give no time limit shorter than 60 s:
  `time` (`time_limit`) counts in **minutes**, 0-1440, so `+time=1` is 60 s.
  `server.idle_minutes` is also in minutes. There is no delayed command.
  Send `sv_end_game` (scores, then the next entry) or `sv_map_next` (no
  scores) on stdin at the moment you want.
- **Stdin.** The server polls stdin without blocking, so a pipe or FIFO
  works (no TTY needed) when `server.console` is true (the default). One
  command per line, run between frames. When stdin reaches EOF, input stops
  for the rest of the run. With `HALO_SERVER_CONSOLE_EXIT_ON_EOF=true` the
  server then also runs `quit`. So for a FIFO, keep a writer open for the
  whole test, e.g. hold it on a spare file descriptor, and don't start the
  server with `</dev/null` if you want to send commands later.

**Player host at 39.** Same as the 38 harness: `HALO_NETWORK_TEST=host:<map>[:<variant>]`
(debug.network_test). It advertises 39 without 0x04. v39's test host now
logs its full invite ("network test: invite ..."), which closes the gap
noted in crossplay.md 10.9. A v39 *client* (`HALO_NETWORK_TEST=join` or
`browse`) sends kind 92 to our host.

---

## 10. Test scenarios (netns harness, OpenCE pinned at 337b05cf)

| # | Scenario | Pass condition |
|---|---|---|
| T1 | Version: our client joins an OpenCE 39 player host (LAN and invite) and still joins 38 (ChupathingyCE, OpenCE 7fb941b0) | both join and play every step; log names 39 |
| T2 | Kind 92 at our host: an OpenCE 39 client on our host (once we host 39), or our host fed captured 756-byte copies plus datagram copies and copies of count 0, count 2, and shortened size | dropped silently; nothing counted as malformed; no kick |
| T3 | Dedicated LAN: OpenCE server (`HALO_NET_ONLINE=false`, min_players 1, lobby 5 s); our client joins alone | countdown starts with our 1 player; game plays; spawns/kills/score right; no assert from a host with no player |
| T4 | Dedicated rotation: 2 entries, postgame 5 s | the "Next game in" notice is shown; we return to the lobby and the next map/gametype starts; repeat 3 cycles |
| T5 | Team start "one": team_slayer with our client alone, then an OpenCE client joins | game starts with one team; the joiner balances; no client error |
| T6 | Dedicated shutdown and moderation: `say`, MOTD, `kick`, `ban 1m`, SIGTERM mid-game | notices show; kick drops; rejoin refused (code 6) until the ban expires; SIGTERM shows scores, then a clean disconnect |
| T7 | Code 7: `HALO_SERVER_CHAT_FILTER=block`, our player named with a slur from the default list | our client shows the name-refused text (after fix), not "Vitas only" |
| T8 | Advertisement tag: dedicated server on the LAN | our list tags it as an OpenCE server (after fix), not ChupathingyCE; a ChupathingyCE 38 host is still tagged Chupa |
| T9 | Browser: dedicated with `list_on_brokers=true` | our browser lists it (flag 0x20 ignored or shown) and joins it; with false it isn't listed but its invite joins |
| T10 | Chat: dedicated server with text_chat on, our client joined by invite and from the browser | lines in both directions; with `censor`, our line arrives starred |
| T11 | Regression: legacy wire (18), 29/32 suites, 38 opencelan/chupaserver | unchanged |

---

## 11. Ambiguities and things not verified

1. **Our client with a playerless host (5.1)** was not traced through our
   code. Runtime tests T3-T5 decide it. Look especially at our scoreboard,
   lobby and host-name display, and at any "host machine has a player"
   assumption.
2. **Kind 92 timing:** it is sent from the client's per-tick object update
   once the ready time is set, so after the client is loaded. It was not
   observed on the wire.
3. **0x04 meaning per project:** OpenCE uses it for dedicated (39+),
   ChupathingyCE for Delta (its 38 servers). If ChupathingyCE follows to 39
   it may keep 0x04 as Delta, so a version-based reading is a heuristic.
4. **Master list key and URL** are as at 337b05cf and can change.
5. `server.max_players` up to 127 interacts with our large-game guard;
   whether real servers use more than 16 is unknown.
6. Not built or run while writing this; all facts come from source and
   history reading.
