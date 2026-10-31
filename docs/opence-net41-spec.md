# OpenCE network versions 40 and 41: a clean-room specification

Written Oct 31 2026 for the 41 work (worktree wt-net41, branch
fix/crossplay-net41). This is the "dirty room" output: the observable wire
format and behaviour a peer needs to play with OpenCE at network version 41
(origin/main da8d4556, Oct 30, build-335). It was written by reading every
commit in OpenCE 337b05cf..da8d4556 (200 commits: 125 plus 75 merges; section
9 accounts for each) and the source at da8d4556. It holds facts, numbers,
layouts and rules only. **No OpenCE code, comments or prose are reproduced
here, and none may be copied into this repository.** OpenCE is
AGPL-3.0-or-later since 4b09168e; implementers on our side (GPL-3.0-only)
work from this document and our own code, not from OpenCE's sources.

Names such as `_distributed_message_*` and text constants such as
`OPCEPASS` appear only where they are wire constants.

Notation as in `docs/opence-net32-spec.md` and `docs/opence-net39-spec.md`:
"host", "client", "stream" (the reliable per-client TCP connection),
"datagram" (UDP), offsets in bytes, integers little-endian unless stated,
ILP32 sizes. "Ours" means this tree at 7b0e2ee26 (from
integration/1.1.2-insider.1): our client joins 29, 32, 38, 39; our hosts
announce 32.

Status tags: **[match]** ours already behaves this way; **[differs]** ours
differs and must change; **[n/a]** nothing to do for interop, noted so nobody
re-investigates it; **[optional]** compatible either way, a choice for us.

Nothing here was built or run. Facts come from source and history. Claims
about ours come from reading this tree.

---

## 0. Summary

Version 40 enlarges the join request. Version 41 changes the two vote
messages, adds map-vote messages, one rejection code and one co-op flag.
Neither changes simulation, the settings record's size, the listing, the
advertisement, signalling or the tunnel.

| Ver | Change | Kind | Client must | Ours today |
|---|---|---|---|---|
| 40 | Join request grows from 0x70 to 0x70 + 234 bytes (pass 170, proof 64, all zero for none) (2.1) | wire, **every 40+ host, player hosts too** | **send the larger message to a host of 40 or more** | **[differs]**: fixed at 0x70 |
| 40 | Rejection codes 8-12: a dedicated server's VPN policy (2.2) | wire, new values | accept, explain | **[differs]**: falls to a generic failure; safe |
| 40 | Discord pass and proof, VPN door (2.3-2.5) | host rule | nothing, unless the machine's address is listed | **[optional]**; recommended: not built (7.1) |
| 41 | Kinds 78/79 entries change (3.1) | wire, layout | nothing if not voting | **[match]**: dropped as unknown, no vote UI |
| 41 | Kinds 94/95, dedicated map vote, in the lobby too (3.2) | wire, new | nothing if not voting | **[match]**: dropped |
| 41 | Rejection code 13, address full (3.3) | wire, new value | accept, explain | **[differs]**: generic failure |
| 41 | Co-op flags bit 2 (value 0x04): host is a dedicated server (3.4) | wire, new bit in the record | **accept** | **[differs]**: **record refused**, so no dedicated co-op game can be joined |
| no raise, in 39 from build-323 | Further option byte 10: Force Team Spawns (4.2) | wire, a former reserved byte | **accept 0 or 1** | **[differs]**: **record refused** when 1, **also on 39 hosts today** |
| no raise, in 39 from build-323 | King state gains 20 bytes in a Multi-Hill game (4.3) | wire, conditional size | **accept** | **[differs]**: every state refused, so no score, no end of game by state, **also on 39 hosts today** |
| no raise, in 39 from build-323 | CTF Kills Score in the variant (4.4) | variant bytes | nothing for play | **[optional]**: display only |
| no raise, in 39 from build-323 | Kind 93, client's fire origin (4.1) | wire, optional | nothing | **[match]** |
| no raise, in 41 from build-334 | Player bodies turn with the aim in the air and throwing (4.5) | simulation, one-sided | nothing for interop | **[optional]** |
| no change | Kind 92 build report stays optional (5.3) | | nothing | **[match]** |

**A client that accepts 41 and otherwise plays 39's rules plays correctly
once four things are done:** the join size, the co-op bit, Force Team
Spawns' byte, and the King state's size. Codes 8-13 need words. The rest can
stay dropped.

**Moving target.** 41 kept moving after cf9fad60 with no raise (4.5, section
9). The message format record at da8d4556 is identical to cf9fad60's. Pin
da8d4556.

---

## 1. Version negotiation (rules unchanged, new numbers)

1.1 The constant is 41. It is announced in the advertisement (u16 LE at
reserved offset 0) and the listing (u16 **BE** after the format byte). The
advertisement's size and fields and the listing's format are unchanged from
39. A client joins a LAN or invite game only when the advertised version
equals its own exactly, with the distributed bit set. Hosts still never read
a client's version, and nothing in the join request states it.

1.2 **Which build is which** (harness note: take the version from here, never
from the tree).

| OpenCE | Network version |
|---|---|
| 337b05cf, build-323, build-326, build-329 | 39 |
| 475ae67f | 40 |
| cf9fad60, build-334 (c507cb26), build-335 (da8d4556) | 41 |

Local tags show no release at 40. Builds 330-333 are not tagged in the local
clone, so 40 may or may not have shipped. The window was under a day.

1.3 **Faster raises from now on.** `server.auto_update` defaulted to `off`
at 39 and defaults to `install` at 41 (`check` in the Docker image), with a
check every 30 minutes. A server already on 41 installs a new version by
itself and restarts after its current game, ended at 30 minutes at the
latest. So future raises reach default-config 41 servers within hours, not
days. Servers still on 39 keep their old default (off).

1.4 **Ours.** Add 40 and 41 to the crossplay list. Keep 29, 32, 38, 39. Hosts
keep announcing 32: an OpenCE 41 client can't join our hosts and this work
doesn't change that. **[differs]**

---

## 2. Version 40

### 2.1 The join request

`message_client_join_game_request`, Bungie message group, packet class client
pregame.

| Offset | Size | Field | At 40 |
|---|---|---|---|
| 0x00 | 64 | machine name | unchanged (16 UTF-16 units) |
| 0x40 | 16 | join token | unchanged |
| 0x50 | 32 | hardware id, hex | unchanged |
| 0x70 | 170 | pass | new; all zero for none |
| 0x11A | 64 | proof | new; all zero for none |

Total structure 346 (0x15A) bytes. The field table gains one more bytes
field of 234. Message version and packet type number are unchanged.

**A client with no pass sends all 234 bytes zero. That is valid everywhere.**
A host calls a pass "shown" when any of the 170 bytes is non-zero.

**Size rules on the receiver.** Decoding reads the table's fields and fails
only if the message runs short. It doesn't check for leftover bytes.
- A 40+ host given 0x70 bytes: decoding overflows, the host logs a decode
  failure and drops the connection. **No rejection code is sent**, so an old
  client sees a join that just dies.
- A pre-40 host given 346 bytes would take the first 0x70 and ignore the
  rest. That is an observation from the decoder, not a promise. Don't rely
  on it.

**Ours [differs]:**
- Send 346 bytes only when the chosen game's version is 40 or more.
- **Keep 0x70 for 29, 32, 38, 39 and for the legacy wire.** Betas, Vitas and
  older PC clients still join our hosts with 0x70, and our host decodes
  through the same message table. If the shared definition simply grows, all
  of them overflow and are refused silently. This is the main trap.
- The version must be chosen before the request goes out. It is on the
  list path (`network_wire_choose_version`, as the token is made). Check the
  invite and code paths.
- The new bytes must come from the existing zero-fill, never from
  uninitialised stack.

### 2.2 Rejection codes (carried in the existing machine-rejected message)

| Code | Name | When | Our text should say |
|---|---|---|---|
| 0-6 | as before | | as before |
| 7 | name refused | 39's | as before: on the crossplay wire from a PC host |
| 8 | vpn link required | listed address, policy `link`, no pass shown | this server doesn't allow VPN or datacenter connections |
| 9 | vpn blocked | listed address and policy `block`; or no netlist held with `server.vpn_fail = closed` | the same |
| 10 | vpn link invalid | pass shown does not check | a Discord link could not be checked (never expected from us: we show none) |
| 11 | vpn link too new | the account is younger than `server.link_min_age_days` | as 10 |
| 12 | vpn link in use | the account is already in the game | as 10 |
| 13 | address full (41) | 3.3 | too many players from your address |

Pre-40 OpenCE builds show these as "game closed".

**Ours [differs]:** `network_game_client_rejected_by_game` switches on the
code with no table, so unknown codes are safe: reason "<unknown>", then the
generic failed-to-join error. `NUMBER_OF_SERVER_REJECTION_CODES` is 8 and
nothing bounds on it. Add fixed strings (Spanish too), read on the crossplay
wire from a non-Vita host only. Our legacy-wire 7 keeps its meaning. **No
retry on any code**; a host must not be able to loop the client.

### 2.3 The door (host logic: which machines ever see 8-12)

Applies **only to a dedicated server**. Player hosts let everyone in.

Order inside the accept step:
1. If a pass is shown, check it (2.5).
2. Bans: address, hardware id, or the verified Discord id; then machines kept
   out by a vote.
3. Door: skip if `server.vpn_policy = off`, if the real address is 0, or if
   it isn't globally routable (unless `server.vpn_check_lan`).
4. "Listed" means in the union of the netlists of the masters the server
   publishes to, plus `vpn_block.txt`, less `vpn_allow.txt`.
5. Not listed: let in, with no pass and nothing else asked.
6. If no list is held: `server.vpn_fail` `open` (default) lets in, `closed`
   gives 9.
7. Listed: `block` gives 9; `link` (default) gives 8 with no pass, 10 for a
   pass that fails, 11 below `server.link_min_age_days` (default 365), 12
   for a second seat with `server.link_one_seat` (default on).
8. Then `server.max_machines_per_address` (3.3).

The real address is the tunnel peer's endpoint. The check runs on the server
against a list it holds. **No player address is sent to a master.** The
netlist (signed, IPv4 ranges of VPN providers, Tor exits and datacenter or
hosting networks, including Cloudflare's network by default) is a
server-to-master matter. **A client never fetches or parses it. [n/a]**

**Consequences for us:**
- A Vita on a home connection is not listed: it joins with an empty pass.
- A Vita behind a VPN, a Tor exit, WARP or a datacenter address gets 8 on
  default servers and can't get in without a pass.
- Nothing of ours may route crossplay through our Linode relay: Linode is
  a datacenter. Today relays are off on the crossplay wire. **[match]**
- The sources are VPN, hosting and Tor lists. Mobile and CGNAT networks are
  not a stated target, but the list is the master's policy and can change.
  Not verified on real networks.

### 2.4 Pass and proof (format, for the record; not needed to join)

**Pass, 170 bytes, integers in network byte order:**

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | ASCII `OPCEPASS` |
| 8 | 1 | format, 1 |
| 9 | 1 | flags: 1 in the master's Discord server |
| 10 | 8 | master key id, raw (first 8 bytes of SHA-256 of its public key) |
| 18 | 32 | player public key (Ed25519) |
| 50 | 8 | Discord account id |
| 58 | 32 | Discord username, ASCII, zero padded |
| 90 | 8 | issued, Unix time |
| 98 | 8 | expires, Unix time, 72 h after issue |
| 106 | 64 | master's Ed25519 signature over ASCII `opence-pass-1` then bytes 0..105 |

**Proof, 64 bytes:** the player key's Ed25519 signature over ASCII
`opence-pass-proof-1` (19 bytes), then SHA-256 of the pass, then the 32-byte
binding.

**Binding:** HMAC-SHA-256, key = the tunnel session's 32-byte secret, message
= ASCII `opence-link-binding-1` (21 bytes). **On a LAN connection it is 32
zero bytes.**

**Client behaviour in OpenCE.** A key made on first use; a pass fetched from
the master over HTTP(S), signed-in by the key after a browser Discord OAuth
link. `network.link_share` is `when_needed` (default), `always` or `never`.
On code 8 with no pass attached, the client joins again at once with a pass,
preferring a master that lists the game. On code 10 it tries the next held
pass. At most masters + 1 tries, up to 4 masters. **It doesn't check that the
host is dedicated or listed: for an invite or LAN join it offers each pass
held in turn.** See 7.1.

### 2.5 Host's check of a pass

Format reads; master key pinned and one the server publishes to; signature
good; issued no more than 300 s ahead; expires no more than 300 s behind;
proof good for this connection's binding; then, at the door, age and seat. A
verified account replaces whatever the client said in kind 18. A server that
publishes nowhere takes no pass. **[n/a]**

---

## 3. Version 41

### 3.1 Votes, kinds 78 and 79 (entry layouts changed)

Both keep their numbers and sizes. Header as every distributed message.

Kind 78 (`_distributed_message_votekick`), client to host, stream, total 12:

| Offset (entry) | 39 and 40 | 41 |
|---|---|---|
| 0 | player index | vote kind: 1 kick, 2 end the game |
| 1 | pad | player index (kick only) |
| 2 | pad | choice: 1 yes (starts a vote when none runs), 2 no |
| 3 | pad | pad |

Kind 79 (`_distributed_message_votekick_status`), host to client, total 16.
**It goes in the host's per-tick datagram batch, not the stream.** Sent every
second while a vote runs and as it changes.

| Offset (entry) | 39 and 40 | 41 |
|---|---|---|
| 0 | player index | vote kind, 0 none |
| 1 | votes | player index, 0xFF none |
| 2 | needed | yes votes |
| 3 | seconds left | no votes |
| 4 | flags | yes votes needed |
| 5 | pad | seconds left |
| 6 | pad | flags: bit 0 voted yes, bit 1 may vote, bit 2 voted no |
| 7 | pad | pad |

Rules, host side **[n/a]**: 45 s a vote, 30 s between; yes votes needed
scale with eligible players: more than half up to 8, 40% with at least 5 up
to 32, 30% with at least 13 beyond, or a fixed percentage
(`network.votekick_threshold`); at least 2; more yes than no. A client's
requests: one a second. Starter cooldown 5 min, target protection 10 min.
Kick bans address and hardware id for `network.votekick_ban_minutes`. End
game needs 30 s played and a game a minute old, and ends the game normally
(`network.vote_end_game`, default on). Votes count per real address, then
per hardware id.

The typed words `!endgame`, `!yes`, `!no` are local to a 41 client. They are
never sent as chat. If ours types them they go out as ordinary lines.

**Ours [match]:** 78/79 are at or above our NUMBER_OF_DISTRIBUTED_MESSAGES
(78) and return silently. Our players can't vote and count as eligible
voters who didn't. They can still be voted out. Host notices about votes
arrive as kind 17 and show. Supporting votes is **[optional]** (7.3).

### 3.2 The map vote, kinds 94 and 95 (dedicated server, off by default)

`server.map_vote` is off by default. After a game with players, the server
offers `server.map_vote_choices` (4, range 2-8) rotation entries for
`server.map_vote_seconds` (20, range 5-120), ending early when every machine
has voted.

Kind 94 (`_distributed_message_map_vote`), host to each client, **stream
only, handled in the lobby too**, one per client as "chosen" differs, every
second and on change, and once with the result. Total **496** bytes, always
full size, count 1, tick 0 outside a game.

| Offset (after the 8-byte header) | Size | Field |
|---|---|---|
| 0 | 1 | round, one more each vote |
| 1 | 1 | count of choices |
| 2 | 1 | seconds left |
| 3 | 1 | flags: bit 0 over |
| 4 | 1 | this machine's choice, 0xFF none |
| 5 | 1 | winner, 0xFF none |
| 6 | 2 | pad |
| 8 | 8 x 60 | choices: map name 32, gametype 24, votes 1, pad 3 |

Kind 95 (`_distributed_message_map_vote_choice`), client to host, stream
only, in the lobby too, total 12: round 1, choice 1, pad 2. Host takes one
per 250 ms per machine, may change until the end, one count per machine.

A client forgets a vote not heard for 3 s. The winner is played next through
the normal lobby messages.

**Ours [match]:** in the lobby `network_distributed_handle_message` returns
at its "no game in progress" check. In a game it returns at the kind bound.
496 bytes is within the 0x1000 reliable limit. A client that doesn't vote
just waits out the timer. Our lobby shows the next map as usual. Supporting
it is **[optional]** (7.3).

### 3.3 Rejection code 13

`server.max_machines_per_address` (default 0, none). A dedicated server
refuses a machine when that many from its real address are in the game.
Relevant to two Vitas on one home connection, only where an operator sets
it. **[differs]** for words only (2.2).

### 3.4 Co-op flags (record offset 0x10C)

Bit 0 player collisions off, bit 1 (value 0x02) no ally friendly fire (33),
**bit 2 (value 0x04): the host is a dedicated server (41)**. Set on every
co-op game on a dedicated server.

Effect on a client: it doesn't show the "waiting for the host at the zone"
hint. A dedicated server has no player. It tells players by notice how many
more must cross, or when the cooldown ends.

Host rules **[n/a]**: `server.coop_bsp_switch` `majority` (default) needs more
than half of the living players who are not away in zones to the same BSP,
then a cooldown of 15 s. A player left on a zone by a teleport is not killed
there. Notices carry all of this.

**Ours [differs], blocking:** `network_game_record_decode_version` refuses
any bit it doesn't know ("co-op switches this game does not know"). Accept
0x04 for version 41 or more, 40 not included. Keep refusing higher bits.
Suppress our host-wait hint when set, if our client shows one
(`players_coop_show_host_wait`). It has no other effect on a client.

### 3.5 Host behaviour a joining client meets (existing messages)

- **AFK kick:** `server.afk_kick_minutes` default **5**, warning a minute
  before as a notice. Counts only in a game, not dead, not in a cinematic.
  Any button change, held stick or trigger, or view turn resets it. Then a
  kick; rejoining is allowed. With `server.afk_kick_when_full`, kicks only
  when full, after 3 min. **[match]** on the wire. Players should know.
- **Co-op shield effects:** a 41 client ignores kind 91 in co-op. Ours always
  draws stock. **[match]**
- **Update restart:** "This server updates after this game" as a notice,
  game ended normally. **[match]**
- **Team wipe:** host's `network.coop_team_wipe` (`auto`): public games
  respawn at checkpoint positions, private ones revert. Co-op messages
  unchanged. **[match]** expected; see 12.
- **Lobby:** a dedicated server's START NOW from a client takes 5 s off a
  running countdown and starts none. Clients send what they sent. **[match]**

---

## 4. Changes with no version raise

### 4.1 Kind 93, fire origins (39 from build-323), client to host

`_distributed_message_fire_origins`, datagram, optional, for VR-style builds.
Entry 8 bytes: player index 1, pad 1, offset 3 x s16 in 1/4096 world unit. Up
to 4 entries. A client whose shots leave from the camera never sends it.
Hosts without it drop it. **[match]**: not sent, and a client drops it if it
ever arrives.

### 4.2 Force Team Spawns (39 from build-323)

`game_variant_options` stays 0x2C bytes; the record stays 13,136 bytes.

| Offset in the options | Further byte (ours) | Field |
|---|---|---|
| 0x1C | 0 | mode |
| 0x1D | 1 | hardcore |
| 0x1E-0x23 | 2-7 | Infection's six |
| 0x24 | 8 | Gun Game order |
| 0x25 | 9 | Gun Game melee demotes |
| **0x26** | **10** | **force team spawns, boolean; was reserved** |
| 0x27-0x2B | 11-15 | reserved, zero |

Only the host picks spawns. A client has nothing to do with it.

**Ours [differs], blocking, and a bug in shipped 1.1.1:**
`FURTHER_OPTION_RESERVED` is 10, so a record with this on is refused as
"reserved gametype options not zero" on 39 as well. Allow 0 or 1 at byte 10
for version 39 or more, and keep 11-15 zero. Accepted then ignored.

The option is in the menus. No server rotation rule or gametype-file key for
it was found, so expect it on player hosts more than servers.

### 4.3 Multi-Hill (39 from build-323)

Variant: King's variant gains byte `multi_hill` at offset 1 in the union (0,
or 2-5 hills at once). Scoring is the host's.

**Game state message (kind 7) for King:** `postgame_state` (4), then the
King state as before, then, **only when more than one hill is live**:

| Size | Field |
|---|---|
| 4 | extra hill count, 1 to 4 |
| 16 | 4 x s32 hill ids; unused zero |

Total 20 extra bytes. A single-hill game sends exactly the old size. A 41
reader takes the old size or old + 20 and nothing else, and refuses a count
of 5 or more (a count of 0 passes).

**Ours [differs], and a bug in shipped 1.1.1:**
`game_engine_king_read_network_state` refuses any other size, so
`game_engine_read_network_state` returns early on every state. Scores never
update and the game's end isn't taken from that path. Required: accept old +
20, read the first hill as today, check the count. Drawing the extra hills is
**[optional]**: the host's scores stay right either way, and a Vita player
just can't see hills 2-5. A host's hill id is only a search key for netgame
flags, cut to s16. An id with no flags gives 0 points, which must stay
harmless.

Found in the menus only, as 4.2.

### 4.4 CTF Kills Score (39 from build-323)

`ctf_variant`'s four booleans and `single_flag_time` (offset 4) are followed
by s32 `points_per_capture` (offset 8) and s32 `kills_score` (offset 12). The
variant union stays 0x18 bytes and `game_variant` 0x68. When on: kill +1 for the
team, team kill -1, capture worth `points_per_capture` (range 1-100, default
25 if not positive), score to win = captures x points. All scoring is the
host's. CTF's network state is unchanged.

**Ours [optional]:** we read none of it. Team scores arrive in the state, and
a client doesn't end a game on its own score (`ctf_engine_update` guards on
the distributed client). The score limit is ours from the variant, unmultiplied,
so the limit shown, score warnings and per-player scores would be wrong. If
read, pin the two fields as above before multiplying.

An earlier game type 10 existed on a development branch only. It is mapped
to CTF on load and never appears on the wire from main. **[n/a]**

### 4.5 Player turn with the aim (41 from build-334)

A player biped now turns with the aim while airborne (69863af7), and in every
network game on all machines whatever `game.enhanced_animations` says, as does
the turn while throwing (fc247a81). Excluded: AI, airborne melee, leaps,
flying, climbing, looking without turning; only the two stock player bipeds.
Effect: a shot after a mid-air turn goes to the crosshair instead of being
clipped to the body's aiming screen. Ours keeps the Xbox rule. The host checks
reach, rate and positions, not direction, so nothing breaks: a Vita player's
mid-air shot lands as on stock, and a remote body looks different in the air
for a moment. **[optional]**; its rule would need a clean-room note of its own.

### 4.6 Other no-raise items

- **Protected Custom Edition maps (29426d53):** OpenCE's loader relaxes tag
  name and garbage-tag checks for maps stripped by map protectors, so hosts
  may play maps our validator refuses. Our client refuses these with its
  usual map message. **Don't copy this relaxation** without a security review
  (7.5). **[n/a]** for joining stock games.
- **Listing intake (c7229f33):** a browser now drops another version's and
  stale listings before queueing, queue 128 (was 64), because brokers return
  about 1,450 retained listings. No wire change. **[optional]**: same idea
  would help our browser.
- **Probe stamp (7dab2912):** fewer repeat probes of a host. Local.
- **Host notices (ee52b35b):** long notices are split into chat lines on the
  receiver. Local. Our limit stays 160.
- **Unsteered vehicles, weapon lights, function values, particles:** client
  drawing only. The tick of kind 6 is now used locally. Same bytes.

---

## 5. What each side must do, and what a 41 peer sees from an older one

### 5.1 A client joining a 41 host must

| | Rule | Ours after this work |
|---|---|---|
| Send | join request of 346 bytes, pass and proof zero | required |
| Send | everything else as at 39: no new mandatory message | **[match]** |
| Accept | co-op bit 0x04, byte 10 = 0/1, King +20, codes 8-13 | required |
| Accept (silently drop) | kinds 78, 79, 93, 94, 95, 92 | **[match]** |
| Not needed | netlist, master list, Discord pass, kind 92, votes | |

### 5.2 Older and newer peers

| Meeting | Result |
|---|---|
| Unmodified 39 or older client, 41 host | refused by the client on the version. LAN: "newer" message; broker browser: counted as newer, not shown; master browser: greyed and last. Ours: greyed, "a newer PC version" |
| Modified client sending 0x70 to a 40+ host | decode failure, connection dropped, no code |
| 40 client forced onto a 41 host | joins (same size). Its old kind 78 reads as a kind and choice 0, refused. Its old kind 79 reader misreads the status. No memory effect |
| 41 client, older host | refused by the client on the version; older listings dropped on brokers, greyed on masters |
| 41 host refusing a 39 build | can't happen in normal use; pre-40 shows "closed" |
| Client without vote support on a 41 host | plays. Counted as an eligible non-voter; sits out map votes |
| 41 client on our 32 host | doesn't join: exact match. Unchanged |

### 5.3 The build report (kind 92, d0acb3bd)

Unchanged at 41: 756 bytes, client to host, stream, once a game, **optional**.
No host refuses, delays or treats differently a client that omits it. It is
only logged and shown in the server's API. The Vita can meet it trivially by
omitting it. Recommendation: keep not sending it (39 spec 3.5).

### 5.4 What a client needs from a master

Nothing. Reading masters stays **[optional]** and an owner decision, as in 39
spec section 7. Public dedicated servers still skip the brokers unless
`server.list_on_brokers` is set, so **some 41 servers are not greyed in our
browser, they are absent**, and joining them needs an invite.

---

## 6. Which version rules, in our terms

| Rule | From | Note |
|---|---|---|
| Further byte 10 may be 1 | 39 | fix shipped behaviour |
| King state old or old + 20 | any crossplay; 39 in practice | fix shipped behaviour |
| Join request 346 | 40 | new `network_wire_v40_rules` or size by version |
| Codes 8-12 worded | crossplay wire, non-Vita host | no version test needed |
| Code 13 worded | same | |
| Co-op bit 0x04 known | 41 | |
| 78/79 layout | 41 | not read by us |

40 can be joined at no extra cost, since we read neither vote layout. It
needs its own build to test (475ae67f). If no 40 server exists, skip it.

**Signed table.** `join 41 rules 39` must **never** be signed: 1.1.1 would
send 0x70, the host would drop the connection, and the player would get a
join that dies with no reason. See 7.7 for what the table can do.

---

## 7. Security review

### 7.1 Pass: recommended not built

Not needed for a Vita on a normal connection. If ever built:
- **Any host can harvest it.** OpenCE's client answers code 8 from any host,
  dedicated or not, listed or not. The default `when_needed` hands the signed
  pass to whoever refuses you: **Discord id and username to a stranger's
  player host.** Ours would have to show a pass only to a game a master lists
  as dedicated, after asking the player.
- **The LAN binding is 32 zero bytes**, so a pass and proof captured on a LAN
  connection replay to any other host's LAN connection for up to 72 h.
- It would add a private key in Vita storage, HTTPS to a third party (a new
  TLS root beside our GitHub pinning, and an owner decision), and a Discord
  OAuth flow the Vita's browser may not manage.

Sending zeros exposes nothing beyond today: name, token, hardware id.

### 7.2 Each new input, if only the required work is done

| Input | Size | Fields we'd trust | A hostile host can |
|---|---|---|---|
| Join request (outbound) | 346 | none | read 234 zero bytes |
| Codes 8-13 | u16 | none; switch, fixed text | show a wrong reason; never a retry, never host text |
| Co-op bit 0x04 | 1 bit | read as a boolean | hide a hint |
| Further byte 10 | 1 | 0 or 1 enforced, else refused | nothing |
| King + 20 | exact size | count 1-4 checked; ids are search keys, not indices | choose among the map's hills |
| 78, 79, 93, 94, 95 | any | not read | nothing beyond sending junk, as any kind today |

Announcing 41 gains a hostile host nothing: no record size, no field and no
parser gets larger than at 39 except King's 20 bytes.

The **requirement to stay strict:** relax only byte 10 and bit 2, each to
exact values. Keep refusing bytes 11-15 and higher co-op bits, so the next
reuse of a reserved byte is noticed rather than misread. Fuzz both.

### 7.3 If votes or map votes are built later

Recommended order: don't, until someone asks.

| Message | Limits | Unchecked or risky | What a host can do |
|---|---|---|---|
| Kind 79 | entry 8, count at least 1 | **Comes by datagram, so it can't be held to the stream.** Only as trustworthy as the source address: forgeable on a LAN, sealed through the tunnel. Fields are bytes shown as numbers. Player index needs the player check, kind needs < 3 | show a false vote box; fake counts |
| Kind 94 | exactly 496, stream only, host only, not while hosting | 32 and 24-byte text **may arrive without an end**: force the end and printable ASCII on read. Count can be up to 255 on the wire: reject over 8 before using it. Chosen and winner must be checked < count. Use map name for display only, **never for a path or a load** (OpenCE matches it against its own list for a picture) | hold a vote screen open by resending (3 s timeout); show any text; ask for nothing else |
| Kind 95, 78 (outbound) | fixed | | |

A vote screen must never block input to leave the lobby. Nothing in either
message can make a client load, download or run anything: the next map comes
through the settings path and its checks.

### 7.4 Server power over the client (no new mechanism)

A 41 server can drop us for idling, a vote, or an address limit, through the
existing drop. A passed vote keeps out the address and hardware id for up to
24 h on that server. Two Vitas behind one NAT count as one voter and share a
ban. Server logs and events hold joining machines' name, address and hardware
id, as before; refused joins are logged now too. We already say PC peers see
the Vita's address.

### 7.5 Worrying, in order

1. **Pass harvest (7.1)**, if the pass is ever built.
2. **Protected maps (4.6):** that change makes tag validation weaker to suit
   maps made to resist inspection. Those maps may be offered by hosts we join.
   Keep our validator as it is. Refusal is the right answer.
3. **Our record refusals are working as designed.** They caught a reused
   reserved byte. Don't replace them with "ignore unknown".
4. **Shared join table (2.1):** not a security bug but an outage: growing it
   for every wire locks out betas, Vitas and old PC clients with no message.
5. **Auto-update (1.3):** operators' choice, and it makes our greying recur
   fast. No direct risk to us.

### 7.6 Fuzzing

Extend `variant_options` (byte 10 at 0, 1, 2; bytes 11-15; co-op 0x04 at 39
and 41; 0x08), the King state (sizes old - 1, old, old + 19, old + 20, old +
21; counts -1, 0, 1, 4, 5; ids -1 and huge), `crossfuzzclient` against 41,
and a unit test that unknown codes through 65535 stay generic.

### 7.7 The signed table's part

A table row selects built-in rules and can't change a message size. 1.1.1 has
no 41 rules, so **no table can make 1.1.1 join 41**. Once a build has 41, the
table can do these:
- on 1.1.1: `join 41 rules 41`, a forward row, turns the grey text into
  "needs a newer Halo CE for PS Vita - update to join";
- on builds with 41: a later raise that changes nothing can be followed by
  `join 42 rules 41`, per `docs/crossplay.md` 10.13.

---

## 8. Running OpenCE 41 for black-box tests

Build as before: `port/vita/tests/build_pc_crossplay.sh opence <checkout>
da8d4556`, then write `NETWORK_VERSION` 41 beside BUILD_INFO. Everything in
39 spec section 9 still applies. New or changed:

| Setting (environment) | Default | Use |
|---|---|---|
| `HALO_SERVER_AUTO_UPDATE` | **install** | **set `off` in every test** |
| `HALO_SERVER_MASTER_PUBLISH` | true | `false` offline: no pass taken, and no netlist fetch expected (T19 checks) |
| `HALO_SERVER_VPN_POLICY` | link | off, link, block |
| `HALO_SERVER_VPN_FAIL` | open | closed gives 9 for a routable address with no list |
| `HALO_SERVER_VPN_CHECK_LAN` | false | **true is needed**: netns addresses are private and skipped otherwise |
| `HALO_SERVER_VPN_LIST` | empty | a netlist file (binary); or use `vpn_block.txt` beside `bans.txt`, one range a line |
| `HALO_SERVER_LINK_MIN_AGE_DAYS`, `HALO_SERVER_LINK_ONE_SEAT` | 365, true | |
| `HALO_SERVER_MAX_MACHINES_PER_ADDRESS` | 0 | 1 gives code 13 |
| `HALO_SERVER_AFK_KICK_MINUTES` | 5 | 0 in long tests; 1 to test the kick |
| `HALO_SERVER_AFK_KICK_WHEN_FULL`, `_FULL_MINUTES` | false, 3 | |
| `HALO_SERVER_MAP_VOTE`, `_CHOICES`, `_SECONDS` | false, 4, 20 | |
| `HALO_NET_VOTEKICK_THRESHOLD`, `_PERCENT`, `HALO_NET_VOTE_END_GAME` | dynamic, 50, true | |
| `HALO_SERVER_COOP_BSP_SWITCH`, `_COOLDOWN_SECONDS` | majority, 15 | |
| `HALO_NET_COOP_TEAM_WIPE` | auto | |
| `HALO_NET_LINK_SHARE` | when_needed | an OpenCE client's |

Console additions: `sv_vpn`, `sv_vpn_check <address>`, `sv_vpn_fetch`.
Co-op rotation entries (`<level>:coop`) start from the server alone now.

---

## 9. Every commit, 337b05cf..da8d4556

Labels are ours. "In 39" means an ancestor of 475ae67f; those shipped at 39 in
build-323 or later unless noted.

**Wire, 40:** 8bcc3233 (format of netlist, pass; docs), 475ae67f (join size,
codes 8-12, door, client pass), d8e14323 (test bots send the new size),
22f0212c (client picks the pass of a listing master).

**Wire, 41:** cf9fad60 (votes, map vote, code 13, co-op bit, AFK, update
restart, shield effects in co-op, zone majority).

**Wire or game state, no raise:** ba1c3816, 85dba477 (kind 93); 53e84ae2
(Force Team Spawns); 103460bf (Multi-Hill); 616d9e0b, 699dab76, 7d5e377e,
e9a3e653, 8e289be3 (Kills Score, in steps; game type 10 gone by the last).

**Simulation, one-sided, no raise:** 69863af7, fc247a81 (turn with the aim).

**Votes after 41, host or local, no layout change:** 2ae204a6 (D-pad; fixed
percentage; host sends "needed"), 2e56274c, 79d43054 (vote screen picture and
text).

**Host-only rules:** 5681bf19, 376d984f (START NOW and countdown), 8638476c,
22148cc0, c57402ed (co-op on dedicated, checkpoints), d1fd2e25, f48c9b9c,
2f69ac37, 2302c6c7, de2d707f, 6a20a8f1, cf83f261 (team wipe; a numbered
surface event and a raise were tried here and **taken back** by f48c9b9c, so
none remains at da8d4556), 0f2e52e8 (server image label), e9aee4c7 (server
API), caafd194 (updater file limit).

**Browser, local:** c7229f33, 7dab2912.

**Map loading, hosts' reach:** 29426d53 (protected maps), 158962f8 (its
declaration), 750c98b7 (mods' channel order).

**Exact-result speedups, nothing machines must agree on changes:** c4872c5e,
61fb747b (collision loops), 9e067c35 (fmod).

**Client drawing and feel, no bytes change:** 8dc266b9, 1a9cb3e8, 9a93f121,
552a39a4 (unsteered vehicles), fcb21ad6, 5617768a, 6cff4f97, 0ff1cad7,
74dd4671, d1c0f4ca (lights, markers, function values with the picture),
900a4d3f, ea516973 (particles), b3641162 (light list), 521c8c8d (portal NaN),
5a56e3d1 (fog), 0880bda7 (texture filter), 49c8260b (depth), b7b35728 (dead
player's cutscene camera), ee52b35b, 8b3e11f0 (notices and chat display),
b1f953dd, eef07efa (stats log), 429f7c8e (terminal), 9ba761bc (video margins).

**Menus and input, local:** 28d4308a, 2b555ed4, e8d4e49e, 0b79c292, cc37bd46,
2bf7f736, f2cc09ac, 94f3e344, 8d274d92, a6203a2b, 860c8d5a, 7d72dfca, 4c5c72a1,
eef2b70d, 37b77a56, b03baf3f, da03f89e, 1cf0b47a, ba759d5b, 49a7e163, ccfd8e5f,
0c8bca2c, 7dfda39f (debug camera), e23ea6ce, ba4a25ae (audio focus).

**HDR, Android, touch, build, tests:** b5c79f4f, f35f0be4, ca971c28, efd35cc5,
da8d4556, 88edf510, d57e605d, 62d4154e, cb643855, f7f36dab, 88bca46f, 51ba7ff8,
ab7c3c78, 8a33ec1f, e1b20b54, 77ee6193, f1894a01, 7ea0ceea, 5aff92aa, ed8366b4,
221fd822, abd3cf22, d5c103cc, a8770722, 81c1df0f, 89f84018, a8d2df5f, 9d31a792,
c507cb26, 75bf7766, a11fb95b, b3b9ed51.

**Bots' skill rules, on the machine running them, nothing new sent:**
0a83da80, 7ea7b0bf.

**Merges (75):** none adds a wire change of its own. Checked by the whole-range
diff of the format record, message tables, protocol headers, distributed
kinds, signalling, listing and tunnel code, not merge by merge.

**Last wire-changing commit:** cf9fad60. Pin
**da8d4556**, whose format record equals cf9fad60's and which has 4.5.
Earlier points: 475ae67f (40), 53e84ae2 and 103460bf (riders on 39).

---

## 10. Test scenarios (netns harness, OpenCE pinned at da8d4556)

| # | Scenario | Pass condition |
|---|---|---|
| T1 | Our client joins a 41 player host, LAN and invite | every step; log names 41 |
| T2 | Our client on a 41 dedicated server, LAN and broker-listed | both rotation games, notices shown, no host-player assumption |
| T3 | Join bytes: capture our join request to 39, 40 (if built) and 41 hosts | 0x70 to 39; 346 with 234 zero bytes to 40+ |
| T4 | Older peers on our host: legacy betas, OpenCE 39 client on a host test-built at 29/32 | still join with 0x70 |
| T5 | Regressions: 29, 32, 38, 39, `chupaserver`, `crossmixed`, `vitaonly code lan lobby` | unchanged |
| T6 | Code 8: `VPN_CHECK_LAN=true`, policy link, our address in `vpn_block.txt` | right text; one join attempt only, no retry |
| T7 | Code 9: policy block; then `VPN_FAIL=closed` with no list | right text |
| T8 | Code 13: limit 1, two copies at one address | second gets the text; first plays |
| T9 | Codes 10-12 and unknown values: stand-in host | texts; generic failure above 13 |
| T10 | Dedicated co-op: `<level>:coop` rotation, our client | record taken with 0x04; plays; BSP switch; waiting notices shown |
| T11 | Bit 0x08, or byte 11 non-zero, from a stand-in | still refused, named |
| T12 | Force Team Spawns on, stand-in at 39 and 41 | record taken, game plays |
| T13 | Multi-Hill stand-in, count 1-4 | scores, end of game, one hill drawn; counts 5 and -1 refused, no state applied |
| T14 | Map vote on, 2 cycles | no malformed count, lobby moves on after the timer, next map loads |
| T15 | OpenCE clients vote to kick our client and to end the game | status dropped; kick works; end game shows scores |
| T16 | AFK: kick at 1 minute | warning shown, dropped cleanly, rejoin works |
| T17 | `crossfuzzclient`, ASan + UBSan, against 41 player host and server | no memory error |
| T18 | Table: 1.1.1-level build with `join 41 rules 41` | greyed, says update; never attempted |
| T19 | No leaks: no packet leaves the namespaces (master, netlist, update) | none |
| T20 | `vita_abi_check`, `ninja vita` | clean |

---

## 11. Live lists

Nobody gave this spec a live server list and no public server or list was
contacted. The newest browser data held locally is a tester's 1.1.1 log of Oct
27, before 40 and 41 existed: 38 games shown, 32 hosts of other versions
greyed: v26 x13, v24 x6, v22 x5, one each of v21, v23, v25, v27, v28, v31, v36,
v11. At least one host listed itself under several versions. **So at that
date the greyed rows were mainly older than 39, not newer.** Those stay greyed
after this work. How many of today's greyed rows are 40 or 41 is unknown, and
the log line "network version N on the crossplay wire ... not yet checked"
from a current build would settle it (`docs/crossplay.md` 10.12).

Greyed is also not only a version: a row can be greyed for one of OpenCE's game
types (SWAT, Infection, Gun Game, Hardcore; `p2p_lobby.c`). Those stay greyed.

---

## 12. Ambiguities and things not verified

1. The exact client outcome of a 0x70 request to a 40+ host (connection drop
   after the decode failure) was read, not observed. T3 and a forced run show
   it.
2. Whether any server ran at 40 long enough to matter.
3. Team wipe on a client: "respawn only" on a public co-op server uses
   existing messages by reading. T10 should reach one wipe if it can.
4. Whether Force Team Spawns, Kills Score or Multi-Hill can reach a dedicated
   server through anything but the menus (no rule or file key found), and how
   to drive them against a real OpenCE host in the harness. T12 and T13 use a
   stand-in unless one is found.
5. CTF variant offsets in 4.4 are by struct reading. Our union must be checked
   for what our builds do with those bytes today.
6. Which addresses are listed in practice (mobile, CGNAT, university networks).
   The sources are public but the master's list wasn't fetched. A Vita user on
   a listed network sees code 8 and has no way in.
7. Our `players_coop_show_host_wait` text on a dedicated host when 0x04 isn't
   honoured is cosmetic: the two hints would overlap.
8. Kind 79's datagram path was read from the send call. A 41 client's receiver
   doesn't require the stream.
