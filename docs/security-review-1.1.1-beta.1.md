# Security review before 1.1.1-beta.1

October 20 2026, branch `sec/anticheat-hardening` (from
`integration/1.1.1-insider.17`, 717e788e4). The PC build goes public with
1.1.1-beta.1, so PC players, whose game memory anyone can change, will be in
the Vitas' games. Two parts:

- **A. A modified client.** What the host already enforced against a client
  that sends anything at all, what was added (the host's anti-cheat, its
  KICK and BAN), with thresholds and tests (`port/linux/NETCODE.md`, "A
  modified client").
- **B. A review with fresh eyes** of every online-facing change since
  v1.1.0 (`git diff e1bafe52..HEAD`: `port/linux/src/p2p*`, `p2p_party.inc`,
  `p2p_friends.c`, `port/linux/game/network*`, `forge_net*`, `map_share.c`,
  `friends_game.c`, `network_connection.c`, the crossplay guard,
  `network_wire.c`, the join paths in `menu_functions.c` and
  `source/networking`): memory safety, authentication, privacy, denial of
  service, paths and crypto.

Severity: **Critical** (remote code execution, a host's or another
player's machine taken over, an account or friend identity stolen),
**High** (a game or machine ruined at will by a stranger, a privacy promise
broken for everyone), **Medium** (needs a position - a friend, a party
member, a host - or gives an unfair edge), **Low** (hardening, little
effect), **Info** (as designed, stated).

No Critical or High finding was left open: the one High (the host had no
answer to a client that keeps sending what only a modified game sends, and
no host-side line of sight, ammunition or noclip check) is fixed in part A.

## A. A modified client

### What the host already enforced (v1.1.0 and the insiders)

| Class | Enforced before this branch | Where |
|---|---|---|
| Damage dealt | The host deals all damage; a client reports its hits, checked: its own player, a weapon held in the last 10 s (or a grenade the host saw thrown, or the vehicle driven), the damage's shape, its scale (1, an airborne melee 1.5), numbers finite, node/region/material the target's, made at a host tick no later than now and no more than 3 s old, the shooter within the projectile's reach of where the host had them, the target where the host had it when the shooter saw it, no more hits than the weapon fires (a 3 s bucket at twice its rate) | `network_damage.c` `distributed_report_valid` |
| Health, shields, invulnerability | The host's alone; the client's unit state carries none the host reads | `network_distributed.c` `distributed_apply_state` |
| Movement | A client's own player taken within 3.5 wu of the host's copy, no further from an anchor than the player moves (twice run and jump speed, or what the host's ticks threw it), no higher than a jump, falling as gravity has it; its velocity clamped | `distributed_take_prediction`, `distributed_on_foot_bound` |
| Vehicles | The same for a driven vehicle: twice its tag's top speed, below a thrown thing's ceiling unless it flies | `network_objects.c` `network_objects_apply_vehicle_predictions` |
| Speed hack | A client's clock 10% fast and 0.5 s ahead: its moves refused; for 10 s over its stream: dropped, banned (bans.txt) | `distributed_note_client_clock` |
| Input | Sticks and trigger pinned to a controller's, NaN zeroed; a client's input only for its own players | `distributed_handle_inputs` |
| Fire rate, grenades, melee range | The hit bucket; throws the host's game saw; a melee blow from where the host had the striker | `distributed_report_paid`, `distributed_grenade_throw`, `distributed_player_was_near` |
| Score, kills, flags, oddball, hills, pickups, spawns, powerups | The host's game alone; the clients apply its state | NETCODE.md stage 2 |
| Team change, name, colour | The lobby closed once the game starts; a machine's player only of its own slot, a team of the game's; names cleaned to drawable text and numbered when another's ("Player 2"): a friend's name cannot be taken while they are in the game | `network_server_message_handler.c` |
| Floods | Datagrams stamped far from the sender's tick dropped; reliable kinds (hits, ready, identity, Forge) only over the stream; game-settings broadcasts paced | `network_distributed.c`, `network_server_message_handler.c` |
| Forge (38, 40-43) | Every field checked, sequence window, rate buckets, host's palette, the world, not a player's unit | `forge_net_protocol.c`, `forge_net.inc` |
| Kick and ban | Console `kick`/`ban` by name (ban: bans.txt by real address and hardware id) | `network_server_manager.c` |

### What was added

| Check | Threshold (default) | Weight | Where |
|---|---|---|---|
| Hit through a wall: the point a shot hit seen from the shooter's eyes (camera offset on the host's history, and at the report's tick 0.3 wu higher for a player who stood to fire) at one of 5 ticks spread from now back to the report's, through the map's structure as a shot meets it (front faces, not two-sided, invisible or breakable), stopping 0.5 wu before the point; at most 6 rays a host tick, one such shot looked at whole a tick (past it the shot is taken); the first unseen shot in 5 s is dealt and counted, another within 5 s refused | 0.5 wu, 6 rays/tick, 5 s | 6 | `distributed_point_seen` |
| Hit from an empty weapon: the host's copy has had no rounds (loaded or not) or no battery for longer than 2 s + the shooter's round trip + the projectile's flight | 2 s + RTT + flight | 4 | `distributed_weapon_armed`, `distributed_player_deals` |
| Headshot spoofing: the node a hit names of a biped within 0.75 wu of where it hit (the host's node moved by where the shooter saw the target) | 0.75 wu | 6 | `distributed_part_near` |
| Noclip: a player's move from the host's copy to the client's word crossing the map's walls or floor (bounding-sphere centre) is not taken; counted once a second at most and never toward a run of refused moves; after 3 s of nothing else it is taken (an honest client parted from the host's copy across a wall is never held or dropped; a walk through a wall stalls 3 s each time) | any crossing, 3 s | 1/s | `distributed_move_through_wall`, `distributed_take_prediction` |
| Refusals classified and counted: forged hit 25, weapon not held 10, poor-link refusals 0, rate 0.25, Forge requests malformed or replayed 2 (once a tick: a burst weighs as one) | - | as listed | `distributed_report_valid` (returns the kind), `forge_net_host_request` |
| Moves refused for 5 s on end (teleport, flying, speed): each further second | 5 s | 10/s | `distributed_note_move` (players and vehicles) |
| Response: the score falls 4/s; past 100 the machine is dropped, every machine told, a line in cheaters.txt, kept out 10 min by real address (internet peer's endpoint), stand-in and hardware id; only stream messages or internet play's sealed datagrams count toward a drop (a LAN's datagrams logged, never acted on) | 100, 4/s, 10 min | - | `network_anticheat.c` |
| Tunables: `network.anticheat`, `network.anticheat_kick_score`, `network.anticheat_decay`, `network.anticheat_ban_minutes`, `network.anticheat_weights` (env `HALO_NET_ANTICHEAT*`) | - | - | `port_config.c` |
| Host's KICK and BAN: PLAYERS screen (halo3/players.xml, the host's alone); settings panel Multiplayer > Vita and PC > Players (Cross kick, Triangle ban for the session, Square ban for good in bans.txt, Anti-cheat on/off, Clear bans). A ban keeps the machine out by real address, stand-in and hardware id, and blocks the player's friend identity when their machine gives its friend card (at once if it gave it before) | - | - | `menu_functions.c`, `vita_settings.c`, `network_anticheat_drop`, `network_anticheat_panel_update` |
| bans.txt capped at its last 512 lines, cheaters.txt at 2000 | 512 / 2000 | - | `distributed_trim_player_records` |

Not caught (stated in NETCODE.md): aimbots and wall hacks that only show
or steer (a player's input); a headshot whose made-up point is near the
head; hits through geometry the map's collision does not have.

### Tests

All on the Linux build (`--linux-net-vita`, debug), two copies in network
namespaces behind NATs, internet play through the test broker
(`run_netns_online_test.sh`):

| Test | What | Result |
|---|---|---|
| `anticheat` (`HALO_NETWORK_TEST_CHEAT=damage,fly`) | hits ten times as hard as the weapon | dropped 3 s into the game (5 forged hits: 25, 71, 117 of 100); cheaters.txt line; its three joins again refused; a new run of its game on the same machine (a new internet identity and stand-in) refused three times: "kept out by the host"; PASS (again with the final code) |
| `anticheat` `HALO_TEST_CHEAT=fly` | its player held 4 wu in the air | 22 seconds of refused moves (5 s free, then 10 a second, falling 4): dropped 34 s into the game; kept out; PASS |
| `anticheat` `HALO_TEST_CHEAT=wall` | hits on the other player wherever they are, ten a second | dropped every run, 7-40 s into the game as the players wander; with the final code 30 through terrain counted, dropped 19 s in (74 hits in the open dealt, 31 refused); PASS |
| `anticheat` `HALO_TEST_CHEAT=ammo` | a weapon kept full on the client, its trigger pressed at the weapon's rate, its hits landed when it sees the other player; the host keeping its copies of the clients' weapons dry (`HALO_NETWORK_TEST_DRAIN`, test builds), as a client's that fired them all are | Oct 21 2026 (`sec/review-followups`, 0e70f6586): dropped 14 s into the game for an empty weapon's hits (cheaters.txt `reason=dropped: hit of an empty weapon`), kept out on its rejoins and new run: PASS. The same build with the check turned off (not committed): 212 hits dealt, no drop: FAIL. (Before, not exercised: the starting weapon took no rounds a shot on the host's copy and the cheat held the trigger) |
| `anticheatlag` (netem `delay 150ms 50ms loss 2%` on both NATs: a 300 ms round trip, 100 ms of jitter each way, 2% lost each way) | two honest players aiming and firing all game (real shots, reloads) and landing a hit every 3 s (only when they see each other, as a player could) after the host brings them together (the host teleporting the joiner's player each time), 161 s of play | final code: **no machine dropped**; 42 of the joiner's hits dealt, 0 refused; one shot the host's lagged view saw no line to was counted (6 points, decayed) and dealt, as the first in five seconds is. Runs on the way: 3 hits refused as of an empty weapon (a starting weapon whose trigger takes no rounds: the rule now reads each trigger); 1-6 refused as through a wall (the test's scripted hits then landed through hills, wall hacks themselves: the scripted shooter now fires only when it sees its target; and one honest one in 45 with the sight test alone: hence the first unseen shot in five seconds dealt). No run ever dropped anyone |
| `network_anticheat_test.c` (`run_network_anticheat_test.sh`, plain and ASan/UBSan) | scores, decay, drops, weights, what never drops, keep-out and its expiry, KICK/BAN/Clear bans | PASS |
| `vita_settings_test.c` | the Players page | PASS (English and Spanish release) |
| `p2p_friends_test.c`; `run_p2p_friends_test.sh fuzz` (ASan/UBSan: 200,000 inputs, then 900,000 more before a 15-minute limit) | F-04, F-07; every message, card, party message and file | PASS (both new tests fail without the fixes); no sanitizer error |
| `run_net_fuzz_test.sh` (all targets; 60 s of libFuzzer each for p2p, messages, forge, map_share) | the decoders a client reaches | PASS |
| `run_lang_test.sh`, `check_port_names.py`, `p2p_lobby`, `network_wire`, `crossplay_guard`, `map_share`, `players_machine` | | PASS |
| `run_port_config_test.sh` | | fails to link before this branch too (posix_files.c needs `platform_translate_path` since c49f533b3); not this branch's |
| Vita `configure.py --lto off --pgo off --portable --release && ninja vita` | | builds |
| Linux `linux-server` (the dedicated server) | | builds |

Also (no regressions, and the checks quiet with PC clients):

| Test | Result |
|---|---|
| `run_netns_crossplay_test.sh pcjoinsvita` (`XPPC_BUILDS`: OpenCE v29 clients on our host for Vitas and PC at a Vita's pace, LAN, server browser and join link) | PASS: every game played to its end; the PC clients' hits dealt 38 of 37 reported, 20 of 21, 24 of 24; no machine dropped |
| `run_netns_online_test.sh forge` (the joiner's volley of 60 malformed, replayed and too many Forge requests) | PASS: dropped and counted, weighed once a tick (2 points), the joiner plays on; before that change the volley (50 malformed or replayed at 2 each) dropped the joiner |
| `run_netns_online_test.sh spooflan` (a LAN machine sending datagrams as from each copy) | PASS (its datagrams logged and dropped; no one dropped) |
| `run_netns_online_test.sh code` (code join, play, leave, join again, map change) | PASS |

## B. Findings

| # | Severity | Where | Finding | Status |
|---|---|---|---|---|
| F-01 | High | `port/linux/game/network_damage.c` `distributed_report_valid`, `network_distributed.c` `distributed_take_prediction` | The host refused a modified client's forged hits and impossible moves but never acted on them: a cheater could send them for the whole game at no cost, and there was no host check for a shot through a wall, from an empty weapon, a spoofed headshot part, or a walk through walls (noclip). | Fixed (part A): `network_anticheat.c`, the four checks; tests `anticheat`, `anticheatlag` |
| F-02 | Medium | `source/networking/network_server_manager.c:2275` (`network_game_server_kicked_addresses`) | The session's keep-out list is matched by the connection's address, which for an internet play peer is its stand-in (100.64.0.0/10) derived from its per-run identifier: a machine dropped for cheating came back with a new run of its game. (bans.txt, by real address and hardware id, still held for the console's `ban` and the speed hack's drop.) | Fixed: `network_anticheat_refused` (real endpoint, stand-in, hardware id) for the anti-cheat limit's drops and the session bans; test `anticheat` (a new run on the dropped machine refused) |
| F-03 | Low | `port/linux/game/network_distributed.c:4300` (`distributed_write_player_record`) | cheaters.txt and bans.txt grew without bound (a machine dropped again and again under new identities fills the storage, and every join reads bans.txt whole). | Fixed: kept to their last 2000 and 512 lines |
| F-04 | Medium | `port/linux/src/p2p_friends.c:2336` (`request_heard`), `:2646` (`pair_heard`) | Appearing offline leaked: a friend who sent a friend request to the code of a machine appearing offline got an ACCEPT and a presence back (the "friend who lost this machine" path), and a removed friend's message got a REMOVE reply: both say the machine is on. | Fixed: neither answered while appearing offline; test in `p2p_friends_test.c` (fails before the fix) |
| F-05 | Medium | `port/linux/src/p2p_friends.c:3115` (`card_heard`) | A game's host relays its joiners' friend cards; a malicious host can answer an ADD FRIEND for another player with its own card (the signature covers the asker's nonce, nothing ties the key to the player chosen). The asker then sends a request to the host's code, and if the host accepts, a friend appears under whatever name the host gave it. | Partly fixed (Oct 21 2026, `sec/review-followups`): an ask's nonce now starts with a mark (`card_bound_mark`), and a machine of this build answers a marked ask with a signature over the answering and asking machines too (label `hcev-friends-2 card`); the asker checks it against the machine it asked, so a host can no longer present one player's real card as another player's; a card that another player of the game gave, or the asker's own, is refused. Same packet sizes: hosts and machines of insider.18-20 relay and answer as before (an older machine's card, unbound, is still taken: logged). Not closable: a host may answer for a player with a card of its own making (it relays every packet; the asker has no other path to the player), so the friends list and requests keep showing each friend's code. Test: `test_card_binding` in `p2p_friends_test.c` (fails before) |
| F-06 | Low | `p2p_friends.c:2336` (`request_heard`) | A stranger who knows a friend code can fill its incoming requests (fresh keys, 10 at once then one per 2 s) until the list is full; honest requests are then dropped until the owner declines some. | Mitigated (Oct 21 2026, `sec/review-followups`): a request's budget is now its sender's - a key known here (a request waiting, a friend, the key of a code this machine asked) has its own, strangers share theirs - spent after the envelope's symmetric open and before the signature's work; the inbox may hold 12 strangers' and 8 known keys' requests of the 64 queued, so a flood crowds out neither the friends' messages nor a known key's request (and two who both enter each other's codes become friends through one); when the list is full, a request its sender has not sent again for longer than a sender waits (6 min) makes room, so a list filled once and left takes requests again; a request not heard for 15 days goes. Still inherent: a sender that keeps sending from 16 or more fresh keys can keep the list full while it does. Test: `test_request_floods` (fails before) |
| F-07 | Medium | `port/linux/src/p2p_party.inc:1061` (`party_follow_precheck`) | After a party's leader goes silent, the longest-standing member leads; that member may be a stranger to the others (a friend of the leader gone), yet a FOLLOW from them joined every member's machine into the host of their choosing by itself (the member's address to that host; its maps offered). | Fixed: a leader who is not this machine's friend is not followed by itself ("could not follow"); test in `p2p_friends_test.c` (fails before the fix) |
| F-08 | Medium | `port/linux/src/p2p_crypto.c:700`, `p2p_lobby.c` (sealed token) | A password-protected public game's listing carries its invite token sealed with the password's key (Argon2id, 4 MiB, 3-4 passes): anyone can try passwords offline against it; a short password falls. | Open (as designed: OpenCE's parameters, which crossplay must match). Advise long passwords in the help |
| F-09 | Low | `port/linux/src/p2p_signal.c:2830` (`broker_update_friends`) | `p2p_friends_outgoing` copied a queued message into the caller's buffer before the caller checked its size; safe only because every message (about 494 bytes) is smaller than the buffer. | Fixed: a declared room (`P2P_FRIENDS_PAYLOAD_ROOM`) and a compile-time check |
| F-10 | Low | `p2p_friends.c` `seen_check` | A stranger's request replays are kept in a ring of 64 keys: one captured off a broker can be replayed after 64 others' (a declined one is muted 7 days). | Fixed (Oct 21 2026, `sec/review-followups`): the newest epoch and counter of each key this machine declined, removed or blocked (128, the oldest going) and of each request waiting are kept in `marks.dat` (a new file: builds before it ignore it, and it does not change `friends.dat`); a request no newer is dropped as a replay, after any number of others and after a restart; a friend's request must be newer than the newest message of theirs (not merely inside the replay window); accepting a request starts the friend's record at its epoch and counter. A friend's or stranger's new run (a newer epoch) and a request sent again (a newer counter) are heard as before; duplicates through the four brokers stay counted as replays. Test: `test_request_replays` (fails before) |
| F-11 | Low | `source/networking/network_game_manager.c:373` | One machine adds up to 4 players (its controller indices) and an address 2 waiting connections; several machines of one person can still fill a public lobby. | Fixed (Oct 21 2026, `sec/review-followups`): the host counts one person by address - an internet play peer's real address (its endpoint), not its stand-in, which each run of the game (a new identity) changes. The machines of one address may have no more than half the game's players (4 at least: a machine's own); a machine past that is refused as the game being full, and a machine's player past it refused; the waiting connections (2 an address) and the players added in a game in progress (8 an address a game) are counted by that address too, so new identities from one home no longer hold the slots. A machine joining again from its own address is not counted twice. Not done: hardware ids are self-reported and shared by copies on one PC, so they are not a limit. Tests: `network_anticheat_test.c` (the rule), `run_netns_online_test.sh dedicatedfill` (five machines behind one NAT join an 8-player server: before, all five played; now four play, the fifth is refused) |
| F-12 | Info | `p2p_friends.c` presence | A joinable game's invite token goes to every friend in presence (a bearer token: a friend can pass it on); a password's token sealed. | As designed |
| F-13 | Info | `docs/crossplay.md` 7.2, 7.4 | A Vita hosting for Vitas and PC shows its address to PC peers (no relay on that wire); hardware ids and Discord identities are self-reported. | As designed, stated in the help |
| F-14 | Info | `forge_net.inc` `forge_net_request_object` | In a network Forge game any player can move or delete any networked object but a player's unit, a vehicle someone drives included. | As designed (Forge is friends'; never listed) |
| F-15 | Info | `p2p_signal.c` (friends' topics), `p2p_lobby.c` (listings) | Appearing offline still subscribes the inbox and mailbox topics (hashed: a broker's operator sees a subscriber, no client does); a public listing names its lobby's players to anyone browsing, as server browsers do. | As designed |

### Status of F-01 to F-11 (October 21 2026, `sec/review-followups`)

| # | Status | Commit |
|---|---|---|
| F-01 | Fixed: the four host checks and the response (part A). Each check now proven by a test that fails without it: forged damage, flying, walls (`anticheat` `damage,fly`, `fly`, `wall`), and the empty weapon (`anticheat` `ammo`: PASS with the check, FAIL with it off) | c79a2ab7a (merged in aaf8fa796, insider.20); empty weapon test 0e70f6586 |
| F-02 | Fixed: drops and session bans kept out by real endpoint, stand-in and hardware id | c79a2ab7a |
| F-03 | Fixed: cheaters.txt and bans.txt capped | c79a2ab7a |
| F-04 | Fixed: appearing offline answers no request or REMOVE | 141d1b7ac |
| F-05 | Partly fixed: a card bound to the machine asked (insider.18-20 machines' unbound cards still taken, logged). Not closable: a host may answer with a card of its own making; the friend code stays shown | 66ad00d7f |
| F-06 | Mitigated: per-sender request budgets, a share of the inbox for known keys, stale requests make room. Still inherent: 16+ fresh keys sending all the time keep the list full while they do | 8e0a4d143 |
| F-07 | Fixed: a party follows no stranger into a game | 141d1b7ac |
| F-08 | Open, as designed: OpenCE's Argon2id parameters, which crossplay must match; long passwords advised in the help | - |
| F-09 | Fixed: a declared payload room and a compile-time check | 141d1b7ac |
| F-10 | Fixed: replays dropped however old (`marks.dat`, a new file older builds ignore) | 33ecf6621 |
| F-11 | Fixed: joins limited per person's address (an internet peer's real endpoint; a relayed peer's own offered address) | e641ac50a |

Compatibility checks of these commits (Linux `--linux-net-vita` debug build of the branch, network namespaces, the test broker; no real service): no packet size or format changed; F-05's mark rides in the ask's nonce and older machines relay and answer as before; F-10's `marks.dat` is a new file, `friends.dat` unchanged; F-11 lets one home's 2-4 Vitas in, counts relayed peers by their own address, not the relay's. Runs: `run_p2p_friends_test.sh` (plain and ASan/UBSan) PASS; `run_p2p_friends_test.sh fuzz 300000` PASS; `run_network_anticheat_test.sh` (plain and ASan/UBSan) PASS; `run_net_fuzz_test.sh` with `NET_FUZZ_SECONDS=45` (every target, ASan/UBSan, libFuzzer) PASS; `run_netns_online_test.sh` lobby, code, friends PASS, h3online and party PASS (each failed once in a run of five modes, two crossplay modes and the fuzzers at once - menus' timing under load - and passed run again); `anticheat` PASS with `damage,fly` (dropped 4 s in, forged hits), `wall` (31 s in, through a wall) and `ammo` (14 s in, an empty weapon's hits), `anticheatlag` PASS (no one dropped, 43 of the joiner's hits dealt, 0 refused, 160 s of play); `run_netns_crossplay_test.sh` net32 (OpenCE build-244, 6ec42856) and net29 (8102e711) PASS (once failed beside the five online modes: kills seen late on the OpenCE client; passed run again with less load). Vita `ninja vita` (`--lto off --pgo off`) builds.

Looked at and found sound: the friends' and party's envelopes (every
length, count and text checked; signatures before work; replay windows;
budgets on what strangers can send: requests, boxes, unopened pair
messages), the friend cards' relay (sender corrected by the host, paced),
the crossplay JOIN/ACCEPT parsing (`skip_webrtc`), the code records of
version 3, the server browser's listings on both wires (signed, sanitized),
the ping table and card tunnel packets (bounded, host-only), the Forge
lobby message (type 38) and requests (40), the map share request (35:
refused on the crossplay wire), the held joins (one slot a machine), the
crossplay host's peer limits (24 joiners, 8 an address, 1000 packets a
second), the network switch's teardown (threads joined before sockets
close), the mod manager's received-map names (`mod_manager_file_name_safe`).

## Fuzzing

The host-side fuzzers for what a client can send: the game's message
decoder (every client packet type, `messages`), Forge requests and lobby
answers (`forge`), map share requests (`map_share`), internet play's
tunnel, its card and ping packets (`p2p`), friends' and the party's
messages and cards (`p2p_friends_fuzz.c`), signalling and listings
(`signal`, `lobby`), and live: `run_netns_online_test.sh hostfuzz` (an
ASan + UBSan `HALO_NET_FUZZ_TWIN` host on the Vitas' wire twinning one in
two of a Vita client's messages and datagrams - predictions, inputs, hit
reports, readiness, identity, the anti-cheat's counting - with its drops
off) beside `run_netns_crossplay_test.sh crossfuzzhost` (OpenCE clients).

Results:

| Fuzzer | Result |
|---|---|
| `run_net_fuzz_test.sh` (all targets' checks and kept cases; libFuzzer 60 s each for p2p, messages, forge, map_share, 90 s each for signal, lobby) | PASS, no sanitizer error |
| `run_p2p_friends_test.sh fuzz` | 1.1 million inputs, no sanitizer error |
| `run_netns_crossplay_test.sh crossfuzzhost` with this branch's ASan + UBSan `HALO_NET_FUZZ_TWIN` build as the host (OpenCE v29 clients on a LAN and online; their joins, inputs, predictions, hit reports - through the anti-cheat's checks and counting - and batches) | PASS: 1024 + 1024 twins, AddressSanitizer errors 0, crashes 0; UBSan only the lines known before (units.c, render_sprite.c, thread_win32.c) |
| `run_netns_online_test.sh hostfuzz` (the same on the Vitas' wire with a Vita client) | PASS (Oct 21 2026, `sec/review-followups`, ASan + UBSan `HALO_NET_FUZZ_TWIN` build of the branch, the build `--lto off --pgo off --linux-net-vita`): 3072 twins, AddressSanitizer errors 0, crashes 0, the joiner 161 s in the host's game; reached: the joiner's stream messages (its join, players, readiness 19, game updates 24, hit reports d11) and 3052 datagrams. The October 20 failure (the ASan host leaving its own lobby) did not happen here, in three runs. The mode now checks what the twins reached (`net_fuzz_twin.c` logs its tally by direction and message type) |
| `run_netns_online_test.sh hostfuzzforge` (new: a network Forge game; the joiner places, moves, deletes, undoes, and sends three volleys of 60 malformed, replayed and too many requests) | PASS: 3072 twins, no AddressSanitizer error, no crash; reached the Forge answer (38) and 422 Forge requests (d40), 158 s played |
| `run_netns_online_test.sh hostfuzzmap` (new: the host's game on a Custom Edition map the joiner lacks, `HALO_TEST_CE_MAP`, the Halo PC resource maps of `HALO_TEST_DATA_MENUS`) | PASS: 2048 twins, no AddressSanitizer error, no crash; 1084 map sharing messages (35) twinned while the joiner downloaded the 98 MB map, then played 103 s |
| UBSan lines in these runs (none memory errors) | units.c, render_sprite.c, thread_win32.c (as before); the offsetof idiom in `port/third_party/kcp/ikcp.c` and `tomlc17.c` (a null pointer's member address); `source/objects/damage.c:1307` (a region's damage stored in a byte past 255: the original game's arithmetic). Friend cards ride the tunnel, not the game's messages: covered offline by `p2p_friends_fuzz.c` |

## CPU

Measured on this x86 machine (the Linux debug build; the CPU-side code is
the gxm-null harness's: that build changes the renderer only):
`HALO_NETWORK_TEST_RAY_BENCH=1` on Blood Gulch, where the host's first
player stands, and `HALO_TICK_PROFILE=1`'s net-tick line through the
`anticheatlag` run.

| Cost | x86 | Vita (estimated, 25-60x) |
|---|---|---|
| A move's wall test: one short ray (0.2 wu) | 0.22 us | 6-13 us |
| A shot's line of sight: one ray (20 wu) | 0.51 us | 13-31 us |
| net-tick `anticheat_move_wall`, one client moving | < 0.001 ms/tick (0.3-0.6 calls) | < 0.03 ms/tick |
| net-tick `anticheat_hit_sight`, 39 hits in 161 s | < 0.001 ms/tick | < 0.01 ms/tick |

Per host tick on a Vita: the move test, one ray per client player moving
more than 0.01 wu (standing or turning: none): 0.02-0.04 ms in the usual
2-4 player game, 0.09-0.2 ms with 15 clients all moving; a shot's line of
sight, one ray per honest hit report (a busy 16-player game: 1-3 a tick,
0.01-0.09 ms), never more than 6 rays a tick whatever is sent (0.08-0.19
ms); the counting, a few array writes per refusal. So about 0.02-0.06 ms a
tick in a usual Vita game, 0.1-0.3 ms in the busiest honest 16-player
moment, and at most 0.17-0.39 ms in a full game with a cheater spending
the sight budget every tick - the top of that only at the slow end of the
estimate. The factor is the null harness's (the Pi 4 ran the same game
code about 5x the Vita, this x86 5-12x the Pi); not measured on a Vita
(none touched): `HALO_TICK_PROFILE=1` there prints the same two net-tick
entries, and the sight budget (`LINE_OF_SIGHT_RAYS_PER_TICK`,
network_damage.c) is where to trim if a full game shows more.
