# Crossplay plan: Vita and PC (OpenCE, ChupathingyCE, DamnationCE)

Status: plan only, no game code changed. Written Oct 11 2026 against
next-1.1 50ae7395 (network version 18), next-1.1.1 367b4534 (network
version 19, unreleased), OpenCE 6399ed9d (network version 26),
ChupathingyCE 8cb79900 (0.7.2e, network version 26, accepts 11 to 30) and
DamnationCE 7be58b2b (0.3.40, network version 26, accepts 26 only).
Target: 1.1.0-beta.5 for the first phase.

Note on the brief: since it was written, OpenCE moved to **network version 26**
("Internet play with browsers", 6dc423fd, Oct 11). ChupathingyCE (PR #187) and
DamnationCE have moved to 26 as well. OpenCE raised its version eight times
between Oct 5 and Oct 11. That pace drives most of this plan.

Each statement below is tagged:
- [V] checked in code or git;
- [D] taken from the project's own docs or commit messages;
- [I] inference.

Upstream paths are relative to each project's root.

---

## 1. Summary and recommendation

**Recommendation: option A, done as convergence, not a rebase.**

Make OpenCE's wire the protocol of our builds. We already run OpenCE's
netcode: everything through upstream version 21 is in, and so are parts of 23
and 25. So the job is to close a short, known list of wire gaps (section 3),
not to rebuild the netcode. Our own extras (map sharing, short codes,
relays, ping table, chat, Vita voice tuning) move behind a capability
handshake. They degrade silently when the peer is a stock PC build, as
ChupathingyCE's Delta does.

Version handling uses a small **signed table** fetched from our repository.
It lets a released Vita build follow OpenCE's raises without a new VPK, and it
can switch crossplay off remotely. This follows ChupathingyCE's design. It
uses our own key and wire names, not theirs.

**Phases** (each a releasable step):

| Phase | What players get | Who is at risk | Target |
|---|---|---|---|
| 1. Meet on official servers | Vitas (beta.5+) and PC players (OpenCE, ChupathingyCE, DamnationCE) play together on our Linode dedicated servers. They appear in OpenCE's in-game server browser. | Only our x86 servers host. Maps, caps and rules are ours. | 1.1.0-beta.5 (crossplay as a setting) |
| 2. Vita joins PC games | The Vita's browser lists PC hosts and PC dedicated servers through OpenCE's shared MQTT lobby. Guardrails on the Vita refuse games it can't run. | The Vita client meets untrusted PC hosts and heavy settings. | beta.6 / 1.1.0 |
| 3. PC joins Vita hosts | A Vita's peer-to-peer games are joinable from PC browsers and invite links. | The Vita host, with PC clients, under the 16-player cap. | 1.1.x |
| 4. Follow upstream | Signed-table raises after each OpenCE build passes our netns crossplay suite. Chat, voice and split screen ride the crossplay wire as extensions. | Ongoing | 1.1.1 onward |

**Why phase 1 before "Vita joins PC".** Both directions need the same wire
work: the settings record, flags, signalling and listing. Phase 1 exercises all
of it on the one host we control, with no Vita CPU risk and no untrusted
host. It also fills the official servers.

**Effort.** About 55 to 70 agent-days in total; section 9 has the
breakdown.
- Phase 1 is about 30 agent-days, or about 2 to 2.5 weeks of wall time with
  the 3-agent limit.
- Phases 2 to 4 are about 25 to 40 agent-days more.

**Biggest risks:**
1. OpenCE's raise cadence. It accepts exactly one version and raises it often,
   and some raises change formats. Mitigation: the signed table (with a kill
   switch), plus a crossplay test gate before each widening.
2. Vita performance in PC-sized games. Mitigation: refuse at join (section 6).
3. Security of the wider exposure (section 7). Note that the Vita-only line
   was never a security boundary: our GPL source builds a "Vita" on any PC
   (`--linux-net-vita`).
4. Keeping betas 1 to 4 playing during the move (section 5).

**Decision needed from the owner.** On Oct 4 the owner approved "Vitas play
only Vitas" (94f56ced) because a Vita can't keep up with a PC host. This plan
reverses that only where the Vita isn't the weak host:
- phase 1: our servers host;
- phase 2: client-side guardrails;
- phase 3: the 16-player cap stays and the Vita host sets the rules.

The owner should confirm the reversal and choose crossplay's default for
beta.5. My suggestion: an "Online with PC players" setting, off by default
in beta.5 and on in 1.1.0.

---

## 2. Where our netcode comes from

**The fork point.**
- Our first commit df004b15 ("Halo CE for the PS Vita", Oct 1) is a squashed
  snapshot of OpenCE from about 045d67f86 (Sep 30), at OpenCE network version
  4 [V]. It shares no git ancestor with any upstream, so everything since has
  been ported by hand or cherry-picked.
- The snapshot predates OpenCE's removal of the lockstep netcode, and 1.0.x
  Vitas defaulted to lockstep [V].
- PR #7 (6665b0e6 / c44a91c4, "Universal protocol 9 networking for Vita")
  imported OpenCE's distributed netcode, tunnel and signalling as of
  fd3d62f9 (network version 9) [V].
- Versions 12 to 17 came as cherry-picks. Our "Network version 16" (826df828)
  is OpenCE's b525bf85 (by xshxdex98), not ours [V].
- Our **18** (ccc5187c) is our own raise. It covers OpenCE's 18 and 21 content
  plus our own changes [V].
- Our **19** (next-1.1.1) is our own: chat, voice and pings [V].

**Releases and their network versions** [V]:

| Release | Network version |
|---|---|
| 1.0.0 – 1.0.3 | 4 (lockstep default) |
| 1.1.0-beta.1 – beta.3, next-1.1 (beta.4 queue) | 18 |
| next-1.1.1 (unreleased) | 19 |

Within version 18, betas 1 to 4 play each other through additive fields. The
netns modes pingsmixed, mapmidmixed and swatmixed test this.

**ChupathingyCE** forks OpenCE.
- Its last real merge is OpenCE build-147 (version 23, d9d27ab4).
- Its 24 and 25 raised only the number: it lacks OpenCE's v25 light bits,
  votekick and voice [V].
- It hosts announcing 26 and joins hosts announcing 11 to 30. That is a plain
  range check: it has no version-conditional encoding anywhere [V]
  (`network_client_manager.c:3146-3200`, `delta.h:164-188`).

**DamnationCE** pulls OpenCE tags every 15 minutes.
- Its merge base with OpenCE is 19f1bd9f (after v26) [V].
- Its wire matches OpenCE 26 [I]. Its integrated-light bits ride unused
  unit-flag bits.
- It has a range check, currently 26..26, which its workflow moves with each
  OpenCE raise [V].
- It runs dedicated servers (`server/`, `HALO_DEDICATED=<playlist>`, up to
  128-player instances) and an HTTP list at halo.milenko.org. That list
  service is run by Milenko and closed source [D].

---

## 3. Wire comparison: ours (18) against OpenCE (26)

### 3.1 What is already the same [V]

- **Bungie message group 0–34:** layouts and field tables. Both carry the
  hardware id in the join request (OpenCE v9).
- **The 2-byte pregame keep-alive overrun:** fixed on both sides. Ours in
  87caa6192, ChupathingyCE in ee32bdad, with the wire unchanged.
- **Distributed netcode:** header `{message_header; byte type; byte count;
  long game_time}`, kinds 1–18 and 64–77, raw little-endian ILP32 structs,
  30 Hz tick, the distance tiers. Our ARM Vita and our i686 Linux builds
  already share this layout, so struct layout across ARM and x86 is a solved
  problem.
- **Tunnel:** magic 0x69, X25519 + ChaCha20-Poly1305, 64-packet replay window,
  KCP streams, STUN, UPnP, virtual 100.64/10 addresses, packet types 1–5.
- **Join token:** the constant "message in a bottle". Both trees build the game
  with `DEBUG`, so the per-build mixing is compiled out.
- **The listing's dedicated flag:** 128 in both ours and ChupathingyCE's
  listing format (a coincidence).
- **Object capacity:** 8192 objects per map on both sides. Objects are named
  by datum index, so this is the capacity that must match for multiplayer.

### 3.2 Gaps, in the order they block

| # | Item | Ours (18) | OpenCE (26) | Kind | Phase |
|---|---|---|---|---|---|
| G1 | Game settings record | 13,092 bytes. Local data at PLAYERS_END+0xA, no `game_variant_options`. Our client drops any settings piece whose total size isn't its own (`network_client_message_handler.c:975`). | 13,120 bytes. `struct game_variant_options` (0x1C: time limit, friendly fire, vehicle respawn, team balance, radar, vehicle set, counts, loadout, no map weapons) at +0xA (OpenCE v11, c9ee319a). | **Format, blocking** | 1 |
| G2 | Advertisement flags (`reserved[2]`) | 0x01 distributed, **0x02 Vita**, 0x04 co-op, difficulty in byte 3 | 0x01 distributed, **0x02 in progress**. ChupathingyCE uses **0x04 Delta**. | Meaning clash | 1 |
| G3 | Join token | XOR "Vita-only game!" on Vita builds (`network_server_manager.c:3539-3550`) | Constant | Blocking by design | 1 |
| G4 | Signalling (MQTT) | Topic prefix and key label `hcev` (the label goes into the session key HMAC). MESSAGE_VERSION 3. Our optional relays block sits right after the address list. | `hceu`, MESSAGE_VERSION 4: a WebRTC block follows the address list. OpenCE's JOIN parser accepts no trailing bytes other than the 32-byte proof, and ACCEPT must match exactly (`p2p_signal.c:1181-1186, 1347`). | **Format, blocking** | 1 |
| G5 | Server browser listing | "HL" **format 2**, `hcev/3/lobby/...`, label `hcev-lobby-1`, version must equal ours (`p2p_lobby.c:556, 1228`) | Format 1, `hceu/3/lobby/...`, `hceu-lobby-1`, version must equal 26 | Format | 1 |
| G6 | `map.version` in the game record | Our map-share identity (checksum ^ size·0x9E3779B1, or a CRC-32). CE multiplayer maps keep `levels\test\<n>\<n>` names (`custom_edition_maps.c:584-591`, `map_share_protocol.c:803-823`). | Raw CE header checksum; CE maps named `custom_maps\<name>` (v22, 4e8ed2f1; 9d309f8d) | Meaning: each side refuses the other's CE maps | 1 (CE maps), stock maps unaffected |
| G7 | v24 PC vehicle set | Missing | New enum value in `game_variant_options.vehicle_set` | Meaning (rides G1) | 1 |
| G8 | v19 co-op player collisions | `__padding10C` | `cooperative_flags` bit 0 in byte 0x10C | Meaning; harmless if ignored | 2 |
| G9 | v10 pings (distributed 19) | Missing on 1.1. 1.1.1 has the same id and entry, but rejects values over 9999 other than 0xFFFF, while OpenCE sends up to 0xFFFE. | Every 2 s, unreliable | Additive | 1 (port 1.1.1's, relax the check) |
| G10 | Votekick (distributed 78/79) | Missing | 3b20ef87, no version raise | Additive | 2 |
| G11 | Voice (distributed 80/81/82) | 1.1.1 uses **78/79** (a collision with votekick), Opus 16 kHz, 16 kbps, 4 talkers, play only | Opus 48 kHz, 8–64 kbps chosen by the host, keyed frames, in the lobby too, proximity modes, 8 talkers | Collision, then format | 4 (renumber before 1.1.1 ships) |
| G12 | Our Bungie-group messages 35–37 (map share) and 38–39 (chat, 1.1.1) | Ours only | None. Unknown packet types are logged and dropped (`network_server_message_handler.c:1355`, `network_client_message_handler.c:704`) [V]. A future OpenCE type 35+ would collide. | Additive today; collision risk later | 1 (gate behind capabilities) |
| G13 | Our tunnel type 6 (client ping table) | Ours only | Types 1–5 | Additive [I] (verify the drop path in OpenCE `p2p.c`) | 1 (gate) |
| G14 | Capacity (`halo_port_capacity.h`) | Actors 256, props 768, swarms 32, object lists (Xbox size), listed objects 1024, game state 0xFC0000 | 1024, 8192, 128, 1024, 8192, 0x13C0000. Network actors 1056 vs our 288. | Index ranges in **co-op** | Co-op crossplay refused until checked (section 6) |
| G15 | Version check | Exact match on the client, from the advertisement. The host never checks (true in all four projects). | Exact match on the client (DamnationCE uses a range that is currently 26..26; ChupathingyCE 11..30) | Policy | 1 + table in 2 |
| G16 | WebRTC (browser players) | None | DTLS/SCTP data channels, ICE-lite | Not planned on the Vita. Announce kind "none" as ChupathingyCE does, so browser players can't join us and we can't join them. | — |

**Semantic raises we must match before announcing 26** [D]:
- 21: killing blows sent reliably, bodies at rest sent three times, host-only
  co-op BSP switches. Already ours.
- 22: CE map naming (G6).
- 23: CE tag blocks kept whole. Already ours (360ffe22).
- 24: G7.
- 25: integrated lights. Already ours (9d671835).

OpenCE behaviour fixes without a format change also matter for agreement on
simulation: grenades, shock waves, teleporters, elevators (1f87b4ff, 792711ca,
22fe5628, 8dad0c75, ab3c388c). Task T2 audits these.

### 3.3 Transport, crypto, discovery: what each side has

| | Ours | OpenCE | ChupathingyCE | DamnationCE |
|---|---|---|---|---|
| LAN | TCP stream + UDP, 5150/5151, broadcast (also an ad hoc bridge on Vita, beacon "HCEV" port 2306) | same, no ad hoc | same | same; remembers the source port |
| Internet | tunnel + KCP, STUN, UPnP, **our relay** (port/relay, magic 0x6A, Linode :47320) | tunnel + KCP, no relay; WebRTC for browsers | same as OpenCE, no WebRTC | same as OpenCE |
| Join | short codes ABCD-EFGH (ours), invite file | `halo://join/<64 hex>`, `https://halocombatevolved.com/?join=`, Discord, clipboard | + site list | + site list, `HALO_PROBE` |
| Browser | signed MQTT listing format 2 (hcev) | signed MQTT listing format 1 (hceu), Argon2id/XChaCha passwords | MQTT format 1 + HTTP list halo.milenko.org | same as ChupathingyCE |
| Brokers | emqx, hivemq, mosquitto | **opence.milenko.org:1883** + the same three | same as OpenCE | same as OpenCE |
| Map download | **ours** (35–37, SHA-256, resume, 256 MiB cap) | none (missing-map message only) | none (BLAKE2b identity over Delta) | none |
| Dedicated server | ours (Linode, 7 servers) | none | `server/` (control panel, TLS, Argon2id, TOTP) | `server/` (playlists, Docker) |
| Text chat | 1.1.1 (38/39) | none | capability bit reserved, not built | none |
| Voice | 1.1.1 (78/79) | 80–82 | none | from OpenCE |
| Votekick | none | 78/79 | none | from OpenCE |
| Player cap | 16 on Vita hosts and our servers (`network_server_manager.c:4571-4586`) | 128 | 128 | 128 (dedicated defaults to 12, 32 or 128) |

---

## 4. Options

### A. Adopt OpenCE's protocol, our extras as optional extensions (recommended)

**What it means here.**
- Converge on the OpenCE 26 wire (gaps G1–G7, G9, G15) in our tree, not a
  rebase onto OpenCE's tree. A rebase would throw away our Vita work, the
  hardening, the relay, map share and the dedicated server. It would bring
  little the convergence doesn't: the distributed netcode is already
  OpenCE's.
- Our extras go behind a **capability handshake** (section 4.1).
- A signed table sets which versions we announce and accept.

**Effort:** about 55 to 70 agent-days over four phases.

**Risk:** medium.
- Wire work is well bounded and testable on the netns harness against real
  PC builds.
- The ongoing cost is following upstream.

**Pros:**
- Direct peer-to-peer and server play, no extra hop.
- One protocol to harden.
- Stock PC builds need no change.
- ChupathingyCE shows the approach works (`tools/crossplay_test.py`).

**Cons:**
- Every non-additive OpenCE raise needs a Vita release before crossplay
  resumes. The table's kill switch falls back to Vita-only meanwhile.

### B. Bridge or translator on our relay or servers

**Rejected for game traffic:**
- Game traffic is end-to-end encrypted between peers (X25519 per session). A
  bridge must be a party to both sides, a deliberate man in the middle that
  sees every game in plaintext. That is a poor fit for a security-first owner.
- It adds a hop (Chicago) to every crossplay game.
- It uses Linode transfer (4 TB/month) and is a single point of failure.
- It only translates bytes. It can't make two different simulations agree:
  v21–v25 changed behaviour, not just formats, so a translated game would
  still drift.
- It only helps games that go through it. PC games hosted peer to peer stay
  unreachable unless the Vita routes through the bridge to them.

**Effort and risk:** about 30 to 40 agent-days for the translator, and high
operational risk.

**Where a bridge idea is useful:** discovery only.
- Our dedicated servers can publish both listing formats.
- Vita clients can read OpenCE's lobby directly.
- Neither needs a translator.

### C. One direction first

Either direction needs the same wire, because OpenCE hosts and clients both
check exact formats. So C is a matter of phase order and policy, not a
cheaper protocol. This plan takes C's idea as its phasing: servers first,
then Vita joins PC, then PC joins Vita.

**"Only Vita joins PC" without letting PC join Vita hosts** is possible
indefinitely: phase 3 is just a host-side setting. That's a fallback if
Vita-host performance with PC players disappoints.

### 4.1 The capability handshake ("Vita extensions")

**Message.** One distributed message kind, sent reliably by each joiner to
the host, and by the host back, right after joining:

| Field | Size |
|---|---|
| magic `XV` | 2 |
| major | 1 |
| capability bits | 32-bit |
| build id | variable |
| platform | 1 (Vita, PS TV, Linux server, PC build of ours) |

- Take a kind far from OpenCE's growth, for example 0xF0. OpenCE,
  ChupathingyCE and DamnationCE drop unknown kinds [D: OpenCE NETCODE.md
  "builds without votes drop both kinds as unknown"; ChupathingyCE
  `network_distributed.c:3479`].
- The handshake rides the existing stream, so it needs no extra port. This
  differs from Delta's port 5160, which would need its own tunnel and relay
  path on the Vita.

**Capability bits:**

| Bit | Capability |
|---|---|
| 0 | map_share (35–37) |
| 1 | ping_table (tunnel type 6) |
| 2 | relays |
| 3 | chat (38/39) |
| 4 | vita_voice_profile |
| 5 | coop_vita_flags (the co-op flag and difficulty that left the advertisement) |
| 6 | vita_limits (the host promises Vita-safe settings) |

**Rule:** send an extension's messages only to peers that announced it.
- Silence for 4 s means none.
- Room-wide features, such as chat in a room with PC players, show a hint
  that PC players don't see chat.
- Any extension fails closed, meaning stock behaviour.

**Pre-join information** (co-op difficulty, "Vita host", "Vita-safe") goes in
the listing name or gametype text, or in new advertisement bits.
- Reserve bits 0x40 and 0x80 of `reserved[2]` and record them in NETCODE.md.
- Leave 0x02 and 0x04 to upstream.

**Map share across kinds** stays among our builds only: hosts serve it to
peers with bit 0. PC players get OpenCE's missing-map message naming the
map.

**Credit:** the design follows ChupathingyCE's Delta (CC0), credited in the
commit and in NETCODE.md.

---

## 5. Versions and the transition from betas 1–4

### 5.1 What we announce and accept

**Two wires live in beta.5 builds,** chosen per game:

| Wire | Spoken with | Announced as |
|---|---|---|
| **Legacy Vita wire, "v18"** | betas 1–4 | Exactly today's: hcev topics, listing format 2, 13,092-byte record, Vita token XOR, flags 0x02/0x04 |
| **Crossplay wire** | everyone else | OpenCE's: hceu, format 1, 13,120-byte record, constant token, the version the table says to announce (26 today) |

**Hosts** speak one wire per game, chosen by the crossplay setting:
- off: legacy;
- on: crossplay.

Dedicated servers choose in their config (`sv_crossplay 0/1`). The Linode can
run both kinds side by side during the move.

**Clients** pick the wire from the advertisement or listing:
- version 18 with the Vita flag, or a format-2 listing: legacy;
- a version inside the table's accepted range: crossplay.

**Legacy wire translation** in our code (task T4):
- The settings record is translated at the encode/decode boundary. On a
  legacy game, variant options get their defaults and aren't sent.
- The token XOR applies on legacy only.
- The flags are mapped.
- v19, v22 and v24 behaviour switches off.

**Result:**
- Betas 1–4 keep playing each other unchanged.
- Beta.5 plays with them on legacy games.
- Beta.5 plays with PC on crossplay games.
- A beta 1–4 that meets a crossplay host sees the existing "update" message.
  The in-app updater (feat/vita-updater, merged) makes updating easy.

**Sunset:** retire the legacy wire in 1.2, once update-check telemetry or
server logs show few 18s. Releasing 1.1.1 as **19** would add a third Vita
wire. Instead, 1.1.1's chat, voice and split screen ship as extensions on the
crossplay wire (section 8), and the "19" number is never released.

### 5.2 The signed crossplay table (phase 2, used by servers from phase 1)

**Floor.** The table is built in as the floor: announce 26, accept 26..26 for
the crossplay wire.

**Fetch:**
- From `https://raw.githubusercontent.com/<our repo>/crossplay-table/crossplay.json`
  plus `.sig`.
- At start, then every 4 h (30 min after a failure).
- Cached in the save root.
- Through the updater's HTTPS code. The signature, not TLS, is the trust
  anchor.
- The Linode stays UDP-only, so nothing new opens in its firewall.

**Format:**

```
{ "xv_crossplay": 1, "serial": N, "issued": "...",
  "wires": { "<wire id>": {announce, minimum, maximum, follows} },
  "crossplay_disabled": bool,
  "limits": {max_join_players, refuse_coop, ...} }
```

Wire ids such as `xv-26a` change whenever our own crossplay wire changes.

**Signature:**
- Ed25519 (Monocypher is already in-tree for listings) over the exact bytes.
- Primary and recovery public keys built in.
- Private keys held offline by the owner, never in the repo or CI unless the
  owner chooses otherwise.

**Rules:**
- Size cap 16 KB.
- Signature checked before parsing; strict parser; serials only go forward.
- Rows may only widen the built-in range, never narrow it. The exception is
  `crossplay_disabled`, which turns the crossplay wire off (legacy stays).
- A fuzz target covers the parser.

**Following OpenCE (phase 4):** a script builds each new OpenCE, ChupathingyCE
and DamnationCE tag in netns and runs the crossplay suite (section 10). On a
pass, the owner signs a table that widens `maximum` and moves `announce`. A
failure means a Vita release is needed; meanwhile the table can disable
crossplay or keep announcing the last good version.

**The additive gamble.** ChupathingyCE admits it: "a raise that doesn't
[leave messages as they are] will play wrong rather than refuse the join." We
only widen after our suite passes against that exact build, and never
speculatively. ChupathingyCE's speculative maximum of 30 is a choice we
should not copy.

**Announcing vs accepting:**
- Our hosts must announce exactly OpenCE's current number, because OpenCE
  clients accept only that.
- Our clients accept a range, as ChupathingyCE's do.
- ChupathingyCE clients accept 11..30, so they join our 26 hosts already [I].
- DamnationCE clients follow OpenCE.

---

## 6. Vita constraints and what to refuse

**CPU.**
- The Vita is CPU-bound. Every machine simulates the whole game (distributed
  model), so a client's cost grows with players and vehicles much as a host's
  does.
- Our cap is 16 players on Vita hosts, tested with `HALO_TICK_SLOWDOWN=19`
  in netns mode `many`.
- PC games run up to 128 players (DamnationCE runs a 128-player Blood Gulch).
- All four projects tick at 30 Hz; there's no high-tick-rate setting to
  refuse. Frame interpolation is local.

**Client guardrails** (phase 2, task T11). The checks run before joining,
from the listing or advertisement, and again on the settings record:

| Check | Default | Notes |
|---|---|---|
| maximum players | refuse > 16 | A table `limits.max_join_players` may raise it. The setting shows "PC game too big for the Vita". |
| current players | refuse > 16 | Covers games whose maximum is lowered later |
| co-op | refuse the crossplay wire's co-op hosts | G14: a PC co-op host drives up to 1056 actors; ours hold 288. The other direction (PC client, Vita co-op host) is safe in principle and can open in phase 3 after an index audit. |
| map | must be present locally and pass `custom_edition_maps` limits (23 MB tag cache, file ≤ 0x18000000, no compressed CE maps, resource maps from the player) | OpenCE has no map download, so a PC host's custom map needs the player's own copy. The v22 missing-map message names it. |
| game in progress (flag 0x02) | allowed | Late join costs a precache. Our texture-streaming work applies. |
| split screen in a PC game (PS TV) | 1 local player in crossplay games until measured | |

**Our hosts with PC clients** (phase 3, task T12):
- The 16-player cap stays (`network_game_server_port_maximum_players`).
- The Vita host picks only maps and settings it can run. PC clients can't
  change them.
- PC clients' input and send rates are bounded by the existing per-machine
  budgets (`network_server_manager.c:577-578`) and OpenCE's speed-hack check.

**Memory.**
- No change from crossplay as such.
- PC players' extra traffic is small: about 3.9 KB per tick at 128 players
  [D], about 0.5 KB at 16 [I].
- Voice decode is the one new memory and CPU cost. Decode at 16 kHz output and
  at most 4 talkers on the Vita, even if the host relays 8 (section 8).

**Official servers.** They are x86, so any PC load is fine there. Keep their
16-player cap and stock maps while Vitas join them. Big Team stays at 16.

---

## 7. Security (the owner's first concern)

### 7.1 What changes in exposure

Strangers can already join public Vita games, and the Vita line never stopped
a PC from posing as a Vita: our source is GPL and builds `--linux-net-vita`.
What crossplay changes:
- **Volume:** the PC player base, including modified clients built from CC0
  source.
- **New input paths:**
  - the variant-options record;
  - signalling v4's WebRTC block (parsed only to skip it);
  - listing format 1;
  - votekick and voice messages;
  - the capability handshake;
  - the signed table;
  - PC-sized settings.

### 7.2 Threat model

| Threat | Where it hits | Required handling |
|---|---|---|
| Malformed or oversized messages from a PC host or client | every decoder | Field tables bound sizes. Fuzz every new parser under ASan/UBSan (`run_net_fuzz_test.sh` gains targets: variant options, listing v1, signalling v4, votekick, voice 80–82, capabilities, table). |
| Out-of-range indices in a valid-sized struct (player, machine, actor, object, BSP, vehicle set, loadout) | distributed handlers, settings record | Bounds-check against **our** capacities, not OpenCE's (G14). A PC host may legally send indices our pools don't have. Audit task T14 with a test per index. |
| Settings that crash or starve the Vita | settings record, map | Section 6 refusals, enforced on receipt as well as in the browser |
| Map-share abuse | our 35–37 | Already: host serves only its current custom map, name whitelist, header validation, SHA-256 before rename, 256 MiB cap, rate limits. New: never request a map from a peer that didn't announce map_share; keep "download only from code/Wi-Fi games unless Anyone". |
| Hostile custom maps on a PC host | map loader | Our validator and script allowlist (from OpenCE's map validator). Port ChupathingyCE's map-hardening items we lack (see 7.3). |
| Cheats (speed, aimbot, god mode via host rules) | host | OpenCE v9 speed-hack kick and bans (ours); clients enforce the host's rules (v8, ours). Port votekick (G10) for mixed lobbies. Hardware ids are self-reported (they deter, they don't prove). |
| Flooding | signalling, tunnel, host | Existing budgets (signalling 8-publish bucket, 256 askers; tunnel 2000 pkt/s on servers; 6 joins/min/address on servers). Recheck them against 128-slot PC clients joining 16-slot games. |
| Spoofed datagrams | client | OpenCE's rule (datagrams attributed by source only; game messages in datagrams ignored) plus ours (spoofed virtual sources dropped, c87c7aff). |
| Forged or rolled-back version table | table fetch | Ed25519 with two keys, serial monotonic, widen-only, size cap, kill switch. The private key is offline. |
| Chat or voice abuse from PC players | 1.1.1 | Chat stays ours-only (PC builds have none). Voice from PC players follows our defaults: off in public games, per-player mute, at most 4 decoded. |
| Leaking addresses | signalling | Same as today: addresses go only to proven joiners. The relay hides the Vita's address only between our builds; PC peers see it. Note this in the setting's help. |

### 7.3 Hardening already in place, and what to add

**Ours:**
- feat/net-hardening (603352887): spoofed sources, keep-alive overrun, ad hoc
  slot expiry, code-record match, host name cleaning, seal-check budget, and
  OpenCE's receive-side rounds (76ae3cd8, c5f2990f, 55ecc9b4, 0f8988bb).
- `run_net_fuzz_test.sh`: p2p, signal, messages and map_share; 1.1.1 adds
  chat, voice and ping.
- `relay_fuzz.c`.
- The map validator and script allowlist (a3a79995).
- `debug.network_corrupt`.

**OpenCE:**
- The seven netcode reviews (e7d1ed3a … 71ae1f47).
- Hardening rounds 761210d4, 76addf66, 54abd344.
- tag_validate and hostile-map fixes (d7efd393, c3657ae9).
- Map names that are device names refused (5aad51ac).
- `p2p_lobby_check.c`, harness tests for votekick and voice routing.
- Its map fuzzers are skipped in CI without game data.

**ChupathingyCE:**
- 0.7.1d "security update" (5c25f556): the map-hardening branch d5317b38,
  with bounded strings, stb_vorbis refusals, the downloaded-maps script
  allow-list, game_speed bounds, widgets kept from profile/settings/quit, GL
  upload bounds and libFuzzer map fuzzers.
- `test_network_messages.py`, which compares every message's bytes with
  upstream's under ASan.
- Delta parser fuzzers (`delta_test.c`, `delta_fuzz.c`) in CI.

**To add (task T14):**
1. A wire-contract test like ChupathingyCE's `test_network_messages.py`: encode
   every message and compare the bytes against a pinned OpenCE checkout's
   table. Also compare enum values, struct offsets and the advertisement
   layout. This catches the next G1/G11.
2. Fuzz targets for every new parser listed in 7.2.
3. The index audit against our capacities (G14).
4. A diff of ChupathingyCE's map-hardening commits against ours, porting what
   applies (several started from OpenCE's c3657ae9, which we have).
5. ASan builds of our Vita-path Linux build in the netns crossplay modes,
   playing a full game against real OpenCE and ChupathingyCE builds with
   `debug.network_corrupt` at a third of datagrams.
6. A security review (`/security-review` or a Claude Security scan) of the
   branch before beta.5.

---

## 8. Chat, voice and PS TV split screen (the v19 work)

**Do not release network version 19.** Re-target next-1.1.1's network
features onto the crossplay wire before 1.1.1 ships:

**Voice:**
- Renumber off 78/79 (votekick's).
- Prefer OpenCE's 80/81/82 format, so Vita and PC players hear each other.
  The Vita side would:
  - request a low rate in `voice_config` (16 kbps);
  - encode at 16 kHz bandwidth inside Opus (decoders accept any encoder
    rate);
  - decode at 16 kHz output;
  - decode at most 4 talkers, even if the host relays 8;
  - keep our defaults (push to talk on Select+L, off in public games).
- Effort: about 5 to 7 days, replacing 1.1.1's message layer and keeping its
  audio path.
- If CPU on the Vita rules out OpenCE's format, keep ours as the
  `vita_voice_profile` extension (only between our builds) on kinds 0xF1/0xF2.

**Chat (38/39):**
- PC builds have no text chat.
- Keep ours as the `chat` extension, sent only to peers that announced it.
  Move it off the Bungie group numbers to distributed kinds in our range so a
  future OpenCE type 38 can't collide.
- In mixed rooms, show "PC players don't see chat".

**PS TV split screen:**
- No wire change: up to 4 local players per machine on every side.
- In crossplay games, start with 1 local player until it's measured
  (section 6).

**Pings (distributed 19):** take 1.1.1's version on the crossplay wire, with
its accept range relaxed to OpenCE's (up to 0xFFFE).

---

## 9. Task breakdown

Each task is a few days for one agent and testable on the Linux harness. The
order follows the phases; tasks within a phase run in parallel up to the
3-agent limit.

**Common to every task:**
- The OpenCE checkout is pinned in `~/halo-test-out/<dir>`.
- Netns modes go in `port/vita/tests/run_netns_online_test.sh`.
- All 24 gates must pass before merge.
- Credit OpenCE or ChupathingyCE in commit messages where code or design
  comes from them.

### Phase 1: meet on official servers (beta.5), about 30 agent-days

| Task | What | Days | Test |
|---|---|---|---|
| T1 Wire contract test | Tool comparing our message tables, enum values, struct offsets (`network_game`, advertisement, listing) and encoded bytes against a pinned OpenCE checkout; reports each gap of section 3.2 | 2–3 | Fails today on G1/G2/G5/G11; passes as tasks land |
| T2 Settings record v11 + v24 + v19 | Add `game_variant_options` to `network_game` (13,120 bytes), the PC gametype options' behaviour (vehicle set incl. v24, loadouts, radar, friendly fire, respawn) with receive-side validation; `cooperative_flags`; audit the upstream behaviour fixes listed in 3.2 | 4–5 | T1 passes for the record; net_fuzz target for variant options; host/join on the harness with each option |
| T3 Map identity v22 | Host sends the raw CE checksum in `map.version` and `custom_maps\<name>` for CE multiplayer maps on the crossplay wire; map share keeps its own identity inside its own messages (answer carries checksum and size) | 3–4 | netns `badmap`/`mapmid`; a CE map game between our two builds, with and without a download |
| T4 Two wires | The legacy v18 wire and the crossplay wire behind one runtime switch: record translation, token XOR, flags (0x40/0x80 reserved for ours), version announce/accept, prefix/label, listing format; the crossplay setting and `sv_crossplay` | 4–6 | netns `crossmixed`: beta.4 (v18) host + beta.5 joiner, and the reverse, on legacy; beta.5 pair on crossplay; existing `*mixed` modes still pass |
| T5 Signalling v4 + listing v1 | MESSAGE_VERSION 4 with WebRTC kind "none"; relays moved out of JOIN/ACCEPT for crossplay (a separate sealed message, or only on legacy); hceu topics and label; publish format-1 listings (and format 2 on legacy) | 4–5 | `run_p2p_lobby_test.sh` + signal fuzz; netns `code`/`lobby`/`relay` on both wires |
| T6 Capability handshake | The XV distributed message, peer capability table, gating of map share, ping table, relays; pre-join bits | 3–4 | Unit test + fuzz; netns with an OpenCE peer shows no extension traffic (log greps) |
| T7 Crossplay server build | Dedicated server on the crossplay wire, listing on hceu, 16 cap, stock maps; Linode rollout plan (owner approves; restart only empty servers) | 2–3 | netns `dedicatedopence`: an OpenCE client and a Vita-path client play a full game on our server |
| T8 Real PC builds in netns | Build scripts for pinned OpenCE, ChupathingyCE and DamnationCE Linux builds (32-bit, `HALO_NULL_RENDERER`, SDL dummy), drivers using their env knobs (`HALO_NETWORK_TEST`, `HALO_NET_BROKERS_FILE`, `HALO_NET_STUN`, `HALO_NET_ADDRESS`); new modes (section 10) | 4–5 | The modes themselves |
| T9 Security pass 1 | Fuzz targets for T2/T5/T6 parsers, the index audit for multiplayer messages, ASan netns runs | 3–4 | `run_net_fuzz_test.sh` with new targets; ASan modes clean |

### Phase 2: Vita joins PC games, about 15–20 agent-days

| Task | What | Days | Test |
|---|---|---|---|
| T10 Signed table | Fetch, cache, verify (Ed25519, two keys), widen-only, kill switch; key generation and signing script for the owner (offline); `HALO_CROSSPLAY_TABLE` override for tests | 4–5 | Unit + fuzz; netns serves a signed table from the test broker host over HTTP (local, no internet) |
| T11 Client guardrails | Refusals of section 6, browser badges (PC / Vita / dedicated), clear messages | 3–4 | netns: an OpenCE host with 32 maximum is refused; co-op refused; missing map named |
| T12 Browser for PC games | List hceu games (OpenCE/Chupa/Damnation hosts and servers), passwords (Argon2id cost on Vita: measure), invite link entry; optionally halo.milenko.org's list (**owner decision**: a third-party service; ask its operator first) | 3–4 | netns `browseropence` |
| T13 Votekick | Port OpenCE 3b20ef87 (78/79) | 3 | OpenCE harness test `votekick_count.c` ported; netns mixed vote |
| T14 Security pass 2 | Port missing ChupathingyCE map-hardening items; index audit for co-op messages; a security review of the branch | 3–4 | Map fuzz under ASan with CE maps from the owner's MCC folder (not in repo) |

### Phase 3: PC joins Vita hosts, about 8–10 agent-days

| Task | What | Days | Test |
|---|---|---|---|
| T15 Vita host on the crossplay wire | Host announces the table's version; PC clients' budgets; 16 cap; co-op with PC clients after the actor index audit | 3–4 | netns `pcjoinsvita` with `HALO_TICK_SLOWDOWN=19` on our host, 8 OpenCE bots + 7 of ours |
| T16 Invites both ways | Our hosts produce `halo://join/<64 hex>` links (shown in the panel for PC friends); our short codes stay ours-only | 2 | netns: OpenCE joins by link through NAT, no relay |
| T17 Hardware run | Owner-run test on a Vita with a PC friend (hands off the Vitas otherwise) | — | Checklist |

### Phase 4: follow upstream, chat/voice/split on crossplay (1.1.1), about 12–18 agent-days

| Task | What | Days |
|---|---|---|
| T18 Crossplay gate script | Builds a new upstream tag in netns, runs the suite, writes a result the owner signs into the table | 3 |
| T19 Voice onto 80–82 | Section 8 | 5–7 |
| T20 Chat as extension | Move 38/39 to the XV range, mixed-room hint | 2–3 |
| T21 Split screen in crossplay | Measure, then lift the 1-local limit if it holds | 2 |
| T22 Retire the legacy wire (1.2) | Remove T4's legacy half once telemetry allows | 2 |

---

## 10. Test plan with real PC builds

**Builds.** Build in `~/halo-test-out/<dir>/` from pinned commits:
- OpenCE: `configure.py --release --pgo=off --lto=off`, `ninja linux`.
- ChupathingyCE: the same, plus `ninja server-x86`.
- DamnationCE: the same, plus its server.

All are 32-bit and need lib32 SDL3; our `clang-sdl32` toolchain via
`--linux-cc`. Run them headless with `HALO_NULL_RENDERER=1`,
`SDL_VIDEO_DRIVER=dummy`, `SDL_AUDIO_DRIVER=dummy` and `HALO_NO_AUDIO=1`.
Their test env knobs match ours (`HALO_NETWORK_TEST=host:<map>:<variant>` or
`join`, `HALO_TEST_INPUT=bot:N`, `HALO_NETWORK_TEST_START/KILL/SHOOT/SCORE`)
[V, from OpenCE's `port_config.c` and ChupathingyCE's `crossplay_test.py`].

**Network.** Our harness's namespaces, `mqtt_test_broker.py` (check MQTT 5
support; OpenCE falls back to 3.1.1) and `stun_test_server.py`.
- Point PC builds at them with `HALO_NET_BROKERS_FILE` and `HALO_NET_STUN`.
- Set `HALO_NET_ONLINE=true` only inside netns.
- Never contact real brokers.
- Never use DISPLAY :0.

**New modes:**

| Mode | Setup | Pass |
|---|---|---|
| `opencelan` | OpenCE host + our Vita-path client on one LAN bridge, system link | join, spawn, kills both ways, score, game end, rejoin; no "unknown message" lines on either side beyond expected |
| `opencehost` | OpenCE host behind NAT A, our client behind NAT B, join from the listing | as above + latency median in range |
| `chupahost` / `damnserver` | ChupathingyCE host; DamnationCE dedicated server (`HALO_DEDICATED`) | as above |
| `dedicatedopence` | our crossplay server, one OpenCE client + one of ours + bots | full game, both scoreboards agree (tick hash on our side), map rotation |
| `pcjoinsvita` | our Vita-path host with `HALO_TICK_SLOWDOWN=19`, OpenCE clients | 16 cap holds, the 17th is refused, host frame budget logged |
| `crossmixed` | beta.4 (v18) binaries (`HALO_TEST_VITA_OLD`) + beta.5 + OpenCE | legacy and crossplay games each work; beta.4 on a crossplay host gets the update message |
| `crossrefuse` | OpenCE host with 32 maximum / co-op / missing map | our client refuses with the right message |
| `crossversion` | an OpenCE build one version ahead, table not widened, then widened (signed test key) | refused, then plays |
| `crossfuzz` | ASan build of ours, `debug.network_corrupt` a third of datagrams on the PC peer | no ASan report, game continues |
| `crossvoice` (phase 4) | OpenCE + ours with voice test tones (`HALO_VOICE_TEST`) | frames heard both ways, at most 4 decoded on ours |

**ChupathingyCE's harness as a cross-check.** ChupathingyCE's
`tools/crossplay_test.py` (CC0) uses loopback aliases instead of
namespaces. Its pass criteria list (join, spawn, kills, damage, score, end,
rejoin, stability) is worth copying into our checks, with credit.

**Hardware.** Hardware checks come last and are owner-run: one Vita on an
official crossplay server with a PC player, then a Vita joining a PC host.
Ask before any deploy or quit on a Vita.

---

## 11. Licensing and credit

**Licences:**
- OpenCE, ChupathingyCE and DamnationCE are CC0 1.0 (`LICENSE.md`, the same
  text in all three). CC0 code can go into our GPL-3.0-only tree. No
  attribution is legally required.
- Third-party libraries keep their licences: Monocypher (BSD-2 or CC0, already
  ours), Opus (BSD, already ours in 1.1.1), KCP (MIT, ours), Mbed TLS (only
  if we ever take WebRTC; we won't).

**Credit** (the owner's rule, and courtesy):
- **README credits:** OpenCE (MrBruh and contributors) for the protocol,
  votekick and voice; ChupathingyCE (Milenko) for the Delta design, the
  signed version table and the crossplay test approach; DamnationCE
  (xshxdex98) for its upstream fixes already in our tree.
- **Commit messages:** name the upstream commit hashes ported.
- **Upstream credit lists:** keep OpenCE's AUTHORS.md art credits where its
  art is used (the PC menus already are).

**Direction of flow.** Our code is GPL-3.0-only, so it can't flow back into
CC0 upstreams unless its authors offer it under CC0. That's an owner decision
if upstream ever wants our map share, relay or chat.

**Contact.** No contact with other projects is planned here. Two items need
the owner to decide whether to reach out:
- listing on halo.milenko.org;
- telling upstream about reserved advertisement bits and our distributed
  kind range.

---

## 12. Open questions for the owner

1. Approve reversing "Vitas play only Vitas" along these phases?
2. Crossplay default in beta.5 (suggested: off, as a setting) and in 1.1.0
   (suggested: on)?
3. Run legacy and crossplay official servers side by side during the move?
   Which servers go crossplay first? (Suggested: s1 Slayer and s5 Team Slayer;
   restart only empty servers.)
4. Hold the 1.1.1 network work (chat, voice, split screen) until it sits on
   the crossplay wire, so version 19 never ships?
5. Who holds the table-signing keys, and where (suggested: owner, offline,
   with a recovery key stored separately)?
6. Should the Vita read halo.milenko.org's list (a third-party service, ask
   first) or only OpenCE's MQTT lobby (no third party beyond public brokers)?
   Should we add `opence.milenko.org:1883` to our broker list, as upstream
   has it first?
