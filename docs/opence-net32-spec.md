# OpenCE network version 32: a clean-room specification

Written Oct 19 2026 for branch feat/opence-net32. This is the "dirty room"
output: a description of observable wire format and simulation behaviour
that a peer must have to play with OpenCE at network version 32
(origin/main 6ec42856). It is written from reading OpenCE's history between
its version 29 commit (3e0ba551; released as build-206, 8102e711) and
6ec42856, and it holds facts, numbers, layouts and rules only. **No OpenCE
code, comments or prose are reproduced here, and none may be copied into
this repository.** OpenCE relicensed from CC0 to AGPL-3.0-or-later at
4b09168e (Oct 19 2026); everything that raised the version past 29 came in
after that point. Implementers of the Vita side (GPL-3.0-only) work from
this document and from our own code, not from OpenCE's sources.

Names such as `_distributed_message_*` appear only where they are needed to
refer to a wire constant by the name both projects' format records use.

Notation: "tick" is the 30 Hz game tick. "Host" is the machine running the
game server; "client" any other machine in the game. "Stream" is the
reliable per-client TCP connection the game already uses (the one the
Bungie messages and the "reliable" distributed messages travel on);
"datagram" is the UDP path. Offsets are bytes from the start of an entry.
Integers are little-endian unless stated, as for every v29 distributed
message. "Ours" means the crossplay wire of wt-net32 as of 406d5abd2
(announcing 29).

Status tags: **[match]** ours already behaves this way; **[differs]** ours
differs and must change for 32; **[n/a]** nothing to do for interop, noted
so nobody re-investigates it; **[optional]** compatible either way, a
choice for us.

---

## 0. Summary of 29 -> 32

| Ver | What changed | Kind | Ours today |
|---|---|---|---|
| 30 | Elevator riders are carried by the elevator's movement of the *same* tick, after the elevator's node transforms are recomputed, on every machine (the Xbox code carried them by the previous tick's movement). Client-side: a height error against the host's echo of the client's own prediction is ignored while standing on the same elevator, but only within +/-4 world units. Host-side: "riding an elevator" now also covers an elevator at rest whose floor the unit is touching this tick. | simulation (carry: all machines must agree); local correction rules | **[differs]** we carry one tick late and have neither the host nor the client elevator rule (not even v29's host rule, OpenCE 8dad0c75) |
| 30 (shipped in 32's tree) | Player input entry byte 2 becomes "this player declines co-op friendly fire". Host-side per-player friendly-fire choice plus a 70 s idle override in co-op. | wire meaning of a former pad byte | **[match]** on the wire (we send 0, which is the default meaning); **[differs]** in host behaviour (we ignore the choice) |
| 31 | Pregame keep-alive is a 32-bit field whose two bytes past the original 16-bit value are zero. No other byte of any Bungie message moves. | wire (cosmetic) | **[match]** (87caa6192 sends zeros) |
| 31 | Every machine drops a distributed message that arrives in a datagram stamped more than 450 ticks (15 s) from the latest tick already taken from that sender; a stream message is always taken and re-bases the sender. | receive check | **[differs]** slightly: our window is 600 ticks (20 s) |
| 31 | Host takes client_ready (9), client_identity (18) and hit_reports (11) only over that client's stream. | receive check | **[match]** |
| 31 | Host sends each client's per-tick datagrams to the source address *and port* that client's datagrams arrive from; answers a LAN game search from a non-standard port directly. | addressing | **[differs]** we always send to the standard client port and drop the source port |
| 32 | New distributed message kind 90: the host's mouse aim assist policy, sent reliably over the stream. A v32 client defaults to "blocked" until it arrives. | wire: new kind | **[differs]** we neither send nor take it |
| (no raise) | Text chat, kinds 87/88/89 (host-relayed lines, chat settings). | additive | **[n/a]** dropping them is the defined behaviour of a build without chat |
| (no raise) | Server-browser ping probes (an ordinary signalling join that is closed after a few tunnel pings); live listings accepted only within one hour of the browser's clock. | host tolerance / listing time | check at runtime (section 9) |

The rest of the 329 commits between 8102e711 and 6ec42856 are local
(menus, bots in split screen only, latency meter, web build, map-share
throughput, co-op display fixes, decompilation reconciliation) and change
nothing another machine sees. Section 10 lists the ones checked and why
they are not wire changes.

---

## 1. Version negotiation and refusal

1.1 **The number.** A v32 machine announces 32 and joins only 32.

- Advertisement (LAN answer to a game search, and the same record over the
  tunnel): the game advertisement's reserved bytes carry the network
  version as an unsigned 16-bit **little-endian** value at reserved offset
  0..1, then a flags byte at offset 2 (0x01 distributed netcode, 0x02 game
  in progress; ChupathingyCE uses 0x04; we use 0x40 for a Vita host).
  Unchanged since 29. A v32 client joins only if the version equals 32
  *and* 0x01 is set. Otherwise it refuses with one of three messages:
  equal version without 0x01 (old lockstep host), host newer, host older.
- Server browser listing ("HL", format 1, label hceu-lobby-1): the version
  is an unsigned 16-bit **big-endian** value right after the format byte.
  Unchanged layout. A v32 browser shows only listings whose version equals
  32. Since fb8a0e63 it additionally *counts* (by the listing key's hash,
  unverified, not shown) live, non-closed listings with a version greater
  than its own, to tell the player that newer games exist. Older versions
  are silently dropped.
- The host never checks a joining client's version. Nothing in the join
  request carries it. This is unchanged in all known PC builds.

1.2 **What we must do.** The crossplay wire of a game becomes "32" as a
whole: announce 32 in both places, accept exactly 32, and play the 30, 31
and 32 rules below. Do not accept 29 beside 32 on one wire: 30's elevator
carry changes simulation without a message telling the peers, and a 29
client would also silently drop kind 90. If ChupathingyCE (or another fork)
stays at 29 while OpenCE is at 32, per-game wires (network_wire.h) would
need two crossplay variants ("29 rules" and "32 rules") chosen from the
advertised version, rather than one accepted list. **[differs]**
(HALO_PORT_CROSSPLAY_NETWORK_VERSION is 29; `pcversion` shows a 32 host
refused as newer.)

1.3 **What a 29 peer gets wrong against 32**, for the release notes and the
refusal tests: it is refused both ways by version (good); if the check were
bypassed it would carry elevator riders a tick late (riders drift from the
host's copy and get corrected every tick an elevator moves, worst at
reversals), lack the host's at-rest elevator rule (clients' players dip
through the floor when a lift stops or reverses), ignore kind 90, and
accept stale datagrams for 5 s longer.

---

## 2. Version 30: elevator rider carry (simulation, all machines)

2.1 **Background, as in the Xbox code (ours today).** A machine device
flagged as an elevator has an "elevator node" (an index into its model's
nodes). During the machine's own device update, which runs inside that
object's per-tick update *before* the object's node transforms are
recomputed for the tick, the game took the difference between the
elevator node's world position as it stood (last tick's transforms) and a
position it had saved at the end of the previous carry, and translated
every rider by it. The effect is that riders are moved by the movement the
elevator made one tick earlier, and at a reversal they move the wrong way
for one tick.

2.2 **Rule at 30.** For every object update of a machine whose definition
has the elevator flag and a valid elevator node (the node index is within
the object's node count):

1. *Before* the object's type-specific update (the device update that
   advances the machine's position/phase for this tick), record P_before,
   the elevator node's current world-space position (the translation part
   of that node's transform as it stands from the previous recompute or
   any change applied since).
2. Run the object's update as usual: device update, collision model update,
   then, unless the object is flagged not to recompute its node transforms,
   recompute the node transforms.
3. Only if the node transforms were recomputed in step 2: let P_after be the
   elevator node's world position now, and D = P_after - P_before
   (component-wise, single precision).
4. If D is exactly zero in all three components, carry nothing.
5. Otherwise find the bipeds touching the machine's bounding sphere (the
   game's standard sphere query over bipeds, centred on the machine's
   bounding-sphere centre with its radius, starting from the machine's
   location; up to 2048 results), and for each biped whose remembered
   elevator (see 2.4) is this machine, translate it (the game's standard
   object translation, which also moves what is attached to it) to its
   current position plus D. The order is the query's order.
6. Save P_after as the machine's stored elevator position (kept for parity;
   the carry no longer reads it).

The old carry inside the device update is removed: riders are moved once per
tick, by D. Machines that are not elevators, or whose node index is invalid,
carry nothing. Vehicles and other non-biped objects are never carried (as
before). OpenCE applies this in single player too; for interop only network
games matter, and our legacy (Vita-to-Vita, version 18) wire must keep the
Xbox carry, gated like `network_wire_v28_rules()`.

2.3 **What D includes.** Because P_before is read immediately before the
tick's device update and P_after immediately after the recompute, any
change to the machine's position or phase since the last recompute is part
of D: its own motion this tick, and an externally applied correction (a
co-op client applying the host's device state, which sets the device's
position before the object update runs). A correction that is applied and
recomputed outside the object update (not via the next object update) would
not be carried; OpenCE relies on the former path.

2.4 **Remembered elevator (unchanged, needed to state 2.2 and 3).** Each
tick a biped's physics update reports the elevator it is standing on, if
any. If it reports one, the biped remembers that machine and sets a
countdown to 60. If it reports none, the countdown decrements by one each
tick while above zero, and once it is zero the remembered elevator is
cleared. So a biped stays "on" an elevator for up to 60 ticks after leaving
its floor (a jump), and the countdown equals 60 exactly on ticks where the
physics found it on the elevator.

2.5 **Edge cases.**
- Riders are found by sphere test plus the remembered-elevator match; a
  biped in the sphere that remembers another elevator is not carried.
- Object update order is the game's normal order; whether a rider updated
  before or after its elevator within the tick is not changed by 30.
- A machine whose node transforms are frozen that tick carries nothing that
  tick, and P_before on the next tick is the stale transform, so the next
  recompute carries the whole accumulated difference.
- Floating point: D is computed in single precision from the node
  transforms; nothing is rounded or quantised for the network.

**Ours: [differs].** `source/devices/device_machines.c` still carries
inside the machine update with the saved position (the Xbox order).

---

## 3. Version 30: elevator rules in the distributed netcode (local, but needed)

These do not change any message; they are what each side does with the
positions it already exchanges. Without them co-op riders fall through or
rubber-band on lifts against an OpenCE peer.

3.1 **"Supported by an elevator" (both sides).** A unit is supported by
elevator E this tick when: it is a biped; it is not airborne; it has a
support surface; its elevator countdown (2.4) is exactly 60; and its
remembered elevator E exists and is a machine. Otherwise it is supported by
nothing.

3.2 **Host: taking a client's on-foot prediction.** When the host takes a
client's predicted position for that client's player's unit on foot, and
the host's copy of that unit is *riding an elevator*, the host keeps its own
copy's Z and takes only the predicted X and Y (the rest of the existing
acceptance logic: tolerance 3.5 units, speed bounds, anchors, is unchanged
and runs on that adjusted position). "Riding an elevator" at 30 means: the
unit is a biped with a remembered elevator that exists and is of type
*machine*, and either that machine's device position velocity is nonzero
(it is moving), or the unit is supported by an elevator per 3.1 (at rest
with the unit on its floor). The remembered elevator alone, after the unit
has left a stopped elevator, is not riding it.

At 29 (OpenCE 8dad0c75, CC0, before the relicence) the rule existed with
only the "moving" condition and any device type; 30 adds the at-rest case
and requires a machine. **Ours: [differs]**: we have neither form; we take
the full predicted XYZ.

3.3 **Client: correcting its own player.** The client already keeps, per
local player, a ring of the last 64 ticks of (tick, unit, position) and,
when the host's unit state for that player carries the "predicted" flag
and the low 16 bits of the client tick it echoes, compares the host's
position with the one it recorded for that tick. At 30 each record also
holds the elevator the unit was supported by on that tick (3.1), recorded
only for a unit on foot (not seated). Then, with error = host position -
recorded position:

1. If the recorded elevator is not none, and the unit is supported by the
   *same* elevator now, and the horizontal error satisfies
   ex^2 + ey^2 <= 3.0^2, and -4.0 <= ez <= 4.0 (inclusive), then set ez = 0.
2. Continue as before: if |error| <= 3.0, do nothing; otherwise move the
   unit by the error from where it is now and shift every recorded
   position of that unit by the same amount.

The 4.0 bound is the platform's travel over a round trip; a larger
vertical error (teleport, fall, platform out of step) is corrected
normally, as are jumps (not supported), a different elevator, and states
without the predicted flag. An intermediate OpenCE commit (1b038802) had
step 1 without the +/-4 bound; 6ec42856 has the bound. **Ours: [differs]**
(no step 1, no elevator in the record).

---

## 4. Version 30 (as merged into 32): co-op friendly-fire choice in the input

4.1 **Wire.** In each entry of `_distributed_message_player_inputs` (kind
14, client to host, datagrams), the entry layout is unchanged in size; byte
offset 2, formerly padding, becomes an unsigned 8-bit boolean:
1 = this player declines friendly fire with other players in co-op; 0 = no
objection (the default). Byte 3 stays padding (zero). Layout for reference:
offset 0 player absolute index (u8), 1 the client's loaded structure BSP
(u8), 2 the new byte, 3 pad, 4 client tick (s32), 8 latest host tick heard
(s32), then the player action record and the four-entry button history, as
at 29.

A sender writes 1 only when a co-op game is active and the player turned
the option off; in every other game it writes 0. A receiver reads it only in
co-op (on the host).

4.2 **Host behaviour (co-op only).** The host keeps per player (by absolute
index, reset when a different player takes the slot): the player's choice
(from the input byte for remote players, from its own setting for its local
players, default "accepts"), the last input's button bits, and an idle
counter. Each host tick, for each player with an action that tick: if the
button bits differ from the previous tick's, or either throttle axis is
nonzero, or the primary trigger is nonzero, the idle counter resets to 0;
otherwise it increments (saturating just above 2100 ticks). When one player
damages another (attacker and victim both players, and different):
damage is suppressed entirely if not (victim idle > 2100 ticks, i.e. 70 s)
and not (both accept). If damage is allowed by that test, the host's normal
co-op friendly-fire setting from the game settings applies as before.
Player-on-AI and AI-on-player damage are unaffected. PvP games are
unaffected.

**Ours:** wire **[match]** (our entries are zeroed, so we always send 0,
which is valid); host behaviour **[differs]** (only visible when PC
players host-joined co-op on our Vita host, which is experimental).

---

## 5. Version 31: Bungie message table and the pregame keep-alive

5.1 The game's message table historically paired three server messages
with their neighbours' field lists. Version 31 rewrote the table so each
message has its own list, chosen so that **no encoded byte changes**:
begin game encodes one 16-bit value (the low half of its 32-bit field),
pregame graceful exit encodes one 32-bit value, and the pregame keep-alive
encodes one 32-bit value. The keep-alive's structure becomes 32 bits wide;
the two bytes beyond its original 16-bit payload, which earlier builds
filled from uninitialised memory, are now always zero.

5.2 Receivers must ignore the keep-alive's content. Senders should zero
it. **Ours: [match]**: our encoder sends a zero-padded copy (87caa6192),
and our decoder no longer writes past the 2-byte structure.

---

## 6. Version 31: receive checks and addressing

6.1 **Tick window for datagrams.** For each sender (on the host: each
client machine; on a client: the host) keep L, the latest tick taken from
that sender, initially none; reset to none when a new game starts and when
a machine slot is newly loaded. For every distributed message that passed
the kind/size/direction checks (messages diverted earlier, such as voice
and kind 90, are exempt), with tick t from its header:

1. If L is none: L = t, accept.
2. d = |t - L| computed without overflow.
3. If d > 450 (15 s x 30): if the message came in a datagram, drop it (log
   at most once a second); if it came over the stream, set L = t, forget
   the per-kind "newest seen" ticks used to drop stale unreliable messages
   from that sender, and accept.
4. Otherwise accept and set L = max(L, t).

On a client, "came over the stream" means the host's message was read from
the reliable connection rather than the UDP socket. After this check a
client advances its notion of the host's latest tick (echoed in its inputs)
only if t is newer.

**Ours: [differs]** in the constant only: 600 ticks (20 s). We also clear
the client's host-time on a stream re-base, which is compatible. Use 450.

6.2 **Stream-only client kinds.** The host discards client_ready (9),
client_identity (18) and hit_reports (11) unless they arrive over that
client's stream; a compatible client must therefore send all three
reliably. **Ours: [match]** (both the check and the sending).

6.3 **Datagram source port and return address.** The host remembers, per
client machine slot, the full source address *and port* of the most recent
distributed datagram it accepted from that machine, together with which
stream connection it belonged to. When sending that client its per-tick
datagrams, it uses the remembered address and port if the remembered
connection is the client's current one and the remembered IP equals the
connection's IP; otherwise it falls back to the connection's IP and the
standard client port. When matching an incoming datagram to a machine by IP,
the host prefers a joined remote machine at that IP over its own local
machine (two game copies on one computer share an IP). A client binds the
standard client port, or any free port if another process holds it.

**Ours: [differs]**: our unreliable queue keeps only the source IP (port
reported as 0), and the host always sends to the standard client port.
Interop impact is limited to a PC client that had to bind another port (a
second copy on one PC, some NAT setups on a LAN): it would receive no
per-tick state from our host. Over the tunnel each machine has its own
virtual address and this does not arise.

6.4 **LAN game search from another port.** A host answers a broadcast
game search with its broadcast advertisement at most once per
advertisement interval; additionally, if the search came from a port other
than the standard client port, it unicasts the advertisement back to the
searcher's address and port every time. **Ours: [n/a]** for joining PC
hosts; **[optional]** for our host (only helps a second PC copy find us).

6.5 Host clearing joins queued as a game ended (94209aaa): host-local
bookkeeping, no wire change. **[n/a]**.

---

## 7. Version 32: the host's aim assist policy (kind 90)

7.1 **Message.** Distributed message kind 90
(`_distributed_message_aim_assist_policy`). Total size exactly 12 bytes:

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 2 | message header | packet type 2 (data), size 12, flags 0, encoded as every distributed message's header |
| 2 | 1 | kind | 90 |
| 3 | 1 | count | 1 |
| 4 | 4 | game tick | the sender's current tick (not checked by the receiver) |
| 8 | 2 | block_mouse (u16 LE) | 0 = allowed, 1 = blocked |
| 10 | 2 | reserved (u16) | 0 |

(The pull request first used 87, which the text chat took; the final
number is 90. Nothing must treat 87 as aim assist.)

7.2 **Sender: the host only, over each client's stream.**
- When a client's client_ready (kind 9) is handled, the host sends that
  client the current policy (also on a repeated client_ready).
- Once per main-loop iteration while a game is in progress and the main
  menu is not up, the host compares the policy with the last value it
  broadcast this game; if different (including the first time in a game,
  when nothing has been broadcast), it sends the policy to all clients
  reliably and remembers it. The "last broadcast" resets at each new game.
- Never sent in a datagram, never sent by a client.

7.3 **Receiver: a client.** Accept only if all hold: it came from the host
(not from a client machine index), this machine is a network client, a game
is in progress and the main menu is not up, it was read from the stream
(not a datagram), the total size is exactly 12, the header's packet type is
2, kind 90, count 1, block_mouse is 0 or 1, reserved is 0. Then set the
local policy to block_mouse. Anything else is silently ignored. It is
handled before the generic distributed checks (no tick window, no stale
check, no per-kind length table). At each new game the client resets its
policy to **blocked** until a valid message arrives.

7.4 **Semantics.** On the host, the policy is its own setting (default
allowed). On a client, the received value (default blocked). While the
policy is "blocked", a game is in progress and the menu is not up, a player
who is currently aiming with the mouse gets no optional view magnetism
(slow-down and drag toward targets) even if their own preference enables
it. "Aiming with the mouse" means the first local player (gamepad port 0)
whose mouse moved more recently than the right stick (or whose stick has
never aimed), and, on touch devices, not more recently aimed by touch.
Controller and touch aiming keep their usual magnetism. The player's saved
preference is not changed. Bullet auto-aim (projectile aim correction) is
never affected. Since OpenCE's own default for mouse magnetism is off, the
policy only changes anything for PC players who opted in.

7.5 **Ours: [differs].** Client: we have no mouse aiming on the Vita, so
"honouring" the policy is a no-op, but we must accept the message (not
count it as malformed, not desync) and should log it for tests. Host: we
must send it, or every v32 PC client on our Vita host runs "blocked" for the
whole game. Recommended default: send 0 (allowed), matching OpenCE's host
default; a host option ("Block mouse aim assist") is optional and belongs
in the settings panel per the overlay rule.

---

## 8. Text chat, kinds 87-89 (no version raise; optional)

Added before the relicence and without a version raise: builds without chat
drop the three kinds as unknown, which is the defined compatible behaviour.
For completeness, the observable format:

- Kind 87 (line up), client to host, stream, count 1, one 68-byte entry:
  u8 sender absolute player index (must be one of that client machine's
  players), u8 flags (bit 0 team only, bit 1 vehicle only, bit 2 notice),
  2 pad bytes, 64 bytes of NUL-terminated text (at most 63 characters,
  printable ASCII; '|' replaced by '?'). Host rate limit per machine: burst
  of 5, then one per 1.5 s; excess dropped; counter reset when a machine
  leaves its slot.
- Kind 88 (line down), host to client, stream, same entry: the host's
  relayed line (sender index as found, channel bits) or a notice (bit 2,
  text only). Clients take lines only over the host's stream.
- Kind 89 (chat config), host to all clients, stream, count 1, 4-byte
  entry: u8 flags (bit 0 chat allowed, bit 1 links allowed), 3 pad bytes;
  sent when it changes and every 3 s. A client that never receives it has
  chat off. Hosts force chat off while listed in the server browser, and a
  client joined from the browser ignores chat whatever it is told.

**Ours: [n/a]**: we drop 87-89 on the crossplay wire and send none, so PC
players on our host see chat off. Our own 1.1.1 chat (Bungie group 38/39)
stays off the crossplay wire.

---

## 9. Other behaviour a compatible peer must tolerate (no version raise)

9.1 **Server-browser ping probes (2759fc6d).** A v32 browser, for each
listed host without a password (up to 4 at once, each again every 2
minutes and on refresh), sends an ordinary signalling join request using
the listing's invite token, but listens for the answer on a topic of its
own; it brings the tunnel up, sends tunnel pings, keeps the best of three,
and closes the tunnel. The game layer on the browser side never sees the
peer. A host sees an ordinary join request, a session, pings, and a close,
with no game connection. **Ours:** a Vita host listed for Vitas and PC will
receive these; it must answer like any join and must not count them against
player slots or trip join-flood limits meant for real joins. Runtime check
(section 11, T12).

9.2 **Live listing age (c9f3d07e).** A browser drops a live listing whose
Unix time differs from its own clock by more than 3600 s (retained copies:
600 s, as before). **Ours:** our host must stamp current UTC Unix time; a
Vita with a wrong clock or a local-time/UTC mix-up beyond an hour vanishes
from PC browsers. Runtime check (T11).

9.3 **Tunnel candidate filtering (c827e29b).** Hosts drop offered addresses
in 0.0.0.0/8, multicast, reserved, broadcast, or with port 0. Ours must not
offer only such addresses. **[match]** expected; not verified.

9.4 **Host deletes a CTF flag or oddball (956387d8).** A host may tell its
clients to delete a flag or ball (release builds skip the assertion that
forbids it). A client must follow: take it out of the carrier's hands, clear
the game type's goal markers at it (CTF: the team's flag goal and its
carrier goal; oddball: the team's ball goal), and not assert. **Ours:** check
that our client's delete path does not hit the flag-delete assertions in a
release Vita build. Runtime check (T13).

9.5 **Map download start (62379e56, 05d7fd38).** A v32 host starting while
a client still downloads holds that machine's players and lets them in as
late joiners afterwards. **[n/a]**: we never use OpenCE's map share on the
crossplay wire.

9.6 **Fallen host bipeds (40c1db8a)** and **unsteered vehicle carry
(e5e97c05, 7cf24ae5)**: client-local display and cleanup; nothing sent
changes. **[n/a]** for interop.

---

## 10. Commits checked and found not to affect the wire

Bots (local split screen only, never in joinable games), latency meter
(uses existing data; nothing new sent), split-screen start rules,
streamer-mode name filter, map-share throughput limits, co-op extra
pickups/vehicles (host-side spawns, replicated through existing object
messages), co-op AI grenade type on clients (5e1cb4ce), Running Riot
announcement fix, Team Oddball variant initialisation, Discord/broker/STUN
robustness, NAT advice text, web build, test hooks and `net_compare.py`
drift logging, and the decompilation reconciliation commits in bipeds.c and
the transport (byte-identical by their own gates). The listing, signalling
(message version 4, hceu topics), tunnel (magic 0x69, packet types) and
advertisement formats are unchanged between 8102e711 and 6ec42856.

---

## 11. Test scenarios (netns harness, OpenCE pinned at 6ec42856)

Build OpenCE 6ec42856 with `build_pc_crossplay.sh opence <checkout>
6ec42856` (the pinned checkout under `~/halo-test-out/crossplay-D5yr/OpenCE`
is at 6399ed9d; fetch 6ec42856 from the local opence-contrib clone into it
or a copy, do not modify opence-contrib). Keep 8102e711 (29) as the "old"
build (`XPPC_OLD_BUILDS`). Our build: `--linux-net-vita --linux-d3d
gxm-null`. LAN game traffic is unencrypted, so `tcpdump` on the namespace
bridge can check bytes directly.

| # | Scenario | Pass condition |
|---|---|---|
| T1 | Version, both directions: our client joins an OpenCE 32 host on a LAN, from the browser and by invite; refuses OpenCE 29 with the older-version message; OpenCE 32 client joins our 32 "Vitas and PC" host; OpenCE 29 client is refused by its own check; our browser lists the 32 host, not the 29 host | all as stated; `crossplay_check` full sequence for the joins |
| T2 | Kind 90, our host: capture the stream to each OpenCE client | one 12-byte kind-90 message after each client_ready, one broadcast at game start, one more after toggling the host option mid-game; block_mouse matches the setting, reserved 0, count 1 |
| T3 | Kind 90, our client: OpenCE host with `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` 0 and 1 | our client logs the received value; a forged copy sent as a datagram from the host's address, and malformed copies (size 13, count 2, value 2, reserved 1, wrong packet type) are logged as ignored; the game plays on |
| T4 | Elevator carry, determinism: single-machine scripted ride on an elevator (co-op b40 or c40 lift; or a small CE MP map with a lift) in our build and in OpenCE, both logging rider Z and elevator node Z per tick | for each tick, rider Z - node Z is constant while supported (same-tick carry), identical between the two builds to float precision; our legacy-wire build still shows the one-tick lag |
| T5 | Elevator in a game: our host + OpenCE client, and OpenCE host + our client, co-op on a lift level with scripted riders through a full up/stop/down cycle including a reversal, at 100 and 200 ms RTT | no fall-through, client corrections on the lift no more than when standing still, positions within 1 unit between host and client logs (net_compare thresholds) |
| T6 | Elevator correction bound: inject (debug knob) a 5-unit vertical offset on the host's copy of a riding client player | our client corrects it (beyond 4); a 3-unit offset is ignored while supported |
| T7 | Friendly-fire byte: OpenCE client on our co-op host with its option off | if implemented: no player-player damage while both active; damage after the victim idles 70 s; else documented as ignored. Our client always sends byte 2 = 0 (capture) |
| T8 | Tick window: fuzz twin injects distributed datagrams stamped L+449, L+451, L-451 from a client's address, and a stream message at L+1000 | 449 accepted; 451/-451 dropped; the stream one accepted and later datagrams near the new tick accepted |
| T9 | Stream-only kinds: datagram copies of kinds 9, 11, 18 sent to our host and to OpenCE's | ignored by both |
| T10 | Return port: two OpenCE 32 clients in one namespace (the second binds another port) join our host | both receive per-tick state (players spawn and move) |
| T11 | Keep-alive bytes: capture pregame keep-alives from our host and OpenCE's | 4-byte payload, the two bytes beyond the 16-bit value zero from both |
| T12 | Chat kinds: OpenCE host sends chat lines and config to our client; our host with an OpenCE client | our client drops 87-89 silently and plays on; the OpenCE client shows chat off |
| T13 | Browser probes: an OpenCE 32 client browses while our listed host runs 10 minutes | probe sessions answered and closed, no player slot used, no join-flood refusals for real joins after, host stable |
| T14 | Listing time: run our host with its clock offset by 59 and 61 minutes (faketime) | listed in the OpenCE browser at 59, absent at 61 |
| T15 | Flag delete: OpenCE host deletes the CTF flag via its console during a game | our client (release flags) drops the flag from the carrier, no assertion, game continues |
| T16 | Regression: Vita-to-Vita legacy wire (18) and our 29 suite (`run_netns_crossplay_test.sh all` against 8102e711 where it still applies) | unchanged results |

---

## 12. Ambiguities that need runtime observation

1. **Kind 90 during client loading.** The client takes the policy only while
   a game is in progress and the menu is not up. If the host's start-of-game
   broadcast arrives while the client is still loading, it is dropped; the
   client relies on the copy sent in answer to its client_ready. Confirm
   that client_ready is always sent after the client's game counts as in
   progress (T2/T3 order).
2. **Elevator corrections applied outside the object update.** Whether our
   co-op device-state application (and OpenCE's) sets the device position so
   that the next object update carries the difference, or recomputes nodes
   at once (so the snap is not carried). Observe in T4/T5 by logging D on
   ticks with a host device-state update.
3. **Friendly-fire idle counter.** The counter is driven by the action the
   host ran for each player each tick; for remote players that is the
   relayed input. Exact behaviour when inputs are missing (packet loss, the
   host repeating the last input) needs observation in T7.
4. **ChupathingyCE and DamnationCE versions.** Whether they follow OpenCE to
   32 (and with the same 30 carry). If not, a single accepted crossplay
   version splits the PC population; see 1.2.
5. **Listing clock on hardware.** Whether a Vita's `time()` is UTC and close
   enough (T14 only covers the harness).
6. **Probe volume.** How many PC browsers probe a public Vita host at once
   in practice, and whether our join limits or the Vita's network stack mind.
7. **Mouse on our own builds.** Our Linux harness build may read a mouse;
   if it applies magnetism to it, it must apply the received policy too.
