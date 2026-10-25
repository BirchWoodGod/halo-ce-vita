# Crossplay plan: Vita and PC (OpenCE, ChupathingyCE, DamnationCE)

Status: plan, with T1, T2, T8 and the client side of T4 and T5 built
(sections 10.1 and 10.2), and PC co-op joined as Experimental (section 6.2,
10.4). Written Oct 11 2026 against
next-1.1 50ae7395 (network version 18), next-1.1.1 367b4534 (network
version 19, unreleased), OpenCE 6399ed9d (network version 26),
ChupathingyCE 8cb79900 (0.7.2e, network version 26, accepts 11 to 30) and
DamnationCE 7be58b2b (0.3.40, network version 26, accepts 26 only).
Target: 1.1.0-beta.5 for the first phase.

**Update, Oct 14 2026: the crossplay wire is network version 29** (section
10.6): OpenCE 8102e711 (build-206 and later) and ChupathingyCE 7d6d8f24
(0.7.5b; 29 since 0.7.5a) announce it. The Vita joins exactly 29; hosts of
26 to 28 (DamnationCE 7be58b2b among them) are refused with a message
saying the host runs an older version. (Oct 13 it became 28: section 10.5.)

**Update, Oct 19 2026 (evening): network versions 29 and 32, chosen per
game** (section 10.9). The crossplay wire joins hosts of 29 (ChupathingyCE,
OpenCE build-206) and of 32 (OpenCE 6ec42856 and later), each game played by
its host's version's rules; a Vita hosting for Vitas and PC announces 32.

**Update, Oct 19 2026: no switch; each game speaks its own wire; PC players
join Vita hosts** (section 10.7, the owner's decisions). The browsers list
the Vitas' games and OpenCE's together, every row tagged (VITA, VITA+PC,
OPENCE, CHUPA, PC); joining a game speaks the wire it plays; a Vita that
hosts chooses who can join (Vitas, or Vitas and PC on OpenCE's wire). Phase
3's T15 and T16 are built; section 7.4 is the host side's threat review.
OpenCE has moved on to network version 32 (6ec42856): not accepted, section
10.8.

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
| G6 | `map.version` in the game record | Legacy wire: our map-share identity (checksum ^ size·0x9E3779B1, or a CRC-32), CE multiplayer maps named `levels\test\<n>\<n>` (`custom_edition_maps.c`, `map_share_protocol.c`). Crossplay: **closed for our client (T3, section 6.1)** | Raw CE header checksum; CE maps named `custom_maps\<name>` (v22, 4e8ed2f1; 9d309f8d) | Meaning: each side refuses the other's CE maps | 1 (CE maps), stock maps unaffected |
| G7 | v24 PC vehicle set | Missing | New enum value in `game_variant_options.vehicle_set` | Meaning (rides G1) | 1 |
| G8 | v19 co-op player collisions | `__padding10C` | `cooperative_flags` bit 0 in byte 0x10C | Meaning; harmless if ignored | 2: **closed** (carried, checked and played: 6.2) |
| G9 | v10 pings (distributed 19) | Missing on 1.1. 1.1.1 has the same id and entry, but rejects values over 9999 other than 0xFFFF, while OpenCE sends up to 0xFFFE. | Every 2 s, unreliable | Additive | 1 (port 1.1.1's, relax the check) |
| G10 | Votekick (distributed 78/79) | Missing | 3b20ef87, no version raise | Additive | 2 |
| G11 | Voice (distributed 80/81/82) | 1.1.1 uses **78/79** (a collision with votekick), Opus 16 kHz, 16 kbps, 4 talkers, play only | Opus 48 kHz, 8–64 kbps chosen by the host, keyed frames, in the lobby too, proximity modes, 8 talkers | Collision, then format | 4 (renumber before 1.1.1 ships) |
| G12 | Our Bungie-group messages 35–37 (map share) and 38–39 (chat, 1.1.1) | Ours only | None. Unknown packet types are logged and dropped (`network_server_message_handler.c:1355`, `network_client_message_handler.c:704`) [V]. A future OpenCE type 35+ would collide. | Additive today; collision risk later | 1 (gate behind capabilities) |
| G13 | Our tunnel type 6 (client ping table) | Ours only | Types 1–5 | Additive [I] (verify the drop path in OpenCE `p2p.c`) | 1 (gate) |
| G14 | Capacity (`halo_port_capacity.h`) | Actors 256, props 768, swarms 32, object lists (Xbox size), listed objects 1024, game state 0xFC0000 | 1024, 8192, 128, 1024, 8192, 0x13C0000. Network actors 1056 vs our 288. | Index ranges in **co-op** | **Closed** (6.2): those pools are the host's only; a client's AI unit table is 1056 on the crossplay wire |
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
| **Crossplay wire** | everyone else | OpenCE's: hceu, format 1, 13,120-byte record, constant token, the version the table says to announce (32 for a Vita's host since Oct 19, 29 and 32 joined, each by its rules: 10.9; 29 since Oct 14: section 10.6) |

**Hosts** speak one wire per game, chosen by the host's "Who can join"
(network.who_can_join since Oct 20 2026, network.host_crossplay before:
10.7, 10.10):
- Vita (Vitas only) and Vita PC (Vitas and this port's PC build): legacy;
- Everyone (what "Vitas and PC" was): crossplay.

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

**Floor.** The table is built in as the floor: announce 32, accept exactly 29
and 32 (not 30 or 31), each game by its host's version's rules (since Oct 19
2026: 10.9; 29 and 29..29 before that) for
the crossplay wire (26..26 until Oct 13, 28..28 until Oct 14: sections 10.5,
10.6).

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
| players (owner, Oct 11) | **allow**, marked **Experimental** past 16 | Not a refusal: OpenCE's quick-setup hosts allow 128 slots, and our tree holds 128 machines (`HALO_PORT_MAXIMUM_NETWORK_MACHINES`), so it is a frame-rate question, not a limit. With Crossplay On, the browser badges a game with more than 16 players in it, or a maximum above 16, "Experimental", and joining one asks once ("Large PC game: more than 16 players can make the Vita lag. Join anyway?"). Judged on current players and the maximum, from the listing and again on the settings record. |
| co-op (owner, Oct 11) | **allow**, marked **Experimental**, asked once | Built (section 6.2): a PC co-op game is badged CO-OP EXPERIMENTAL and joining it asks once ("PC co-op game: experimental on the Vita. The host's AI and extra enemies can make it lag. Join anyway?"; past 4 players it says so). Every index such a host sends fits this build (G14 closed). The other direction (PC clients in a Vita's co-op) is experimental since Oct 19 2026 (10.7). |
| map | must be present locally and pass `custom_edition_maps` limits (23 MB tag cache, file ≤ 0x18000000, no compressed CE maps, resource maps from the player) | Built (T3, section 6.1). OpenCE has no map download, so a PC host's custom map needs the player's own copy; a missing one is refused with a message naming it, and map share is never asked of a PC host. |
| game in progress (flag 0x02) | allowed | Late join costs a precache. Our texture-streaming work applies. |
| split screen in a PC game (PS TV) | 1 local player in crossplay games until measured | |

### 6.1 A PC host's map (T3, built Oct 11 2026)

With Crossplay on, our client takes a PC host's map as OpenCE names it
since network version 22 (`port/linux/game/crossplay_maps.c`,
`custom_edition_maps_crossplay_check`; the legacy check is untouched):

| The host names | map.version | Our client plays it when |
|---|---|---|
| `levels\test\<n>\<n>` (a multiplayer level) | 0 | its `<n>.map` is the Xbox map, whole (NTSC 2276 or PAL 2342) |
| another level path (a modded Xbox map) | 0 | its `<n>.map` is an Xbox multiplayer cache |
| `custom_maps\<n>` (a CE map) | since v29 (section 10.6): its version, the first 32 bits of the map file's hash (never 0; 0: nothing compared) | its `<n>.map`, or else its `<n>_<8 hex digits of the version>.map`, is a CE cache of that version, its resource maps are there and PC maps is on. (22 to 28 sent the raw header checksum; 27 and 28 a hashed name: section 10.5) |
| `maps_md\<n>.md`, `maps_pc\<n>.pc` (ChupathingyCE's HaloMD and Halo PC retail, cache v7) | checksum | never |
| one of the game's own maps with a version, or a name refused | | never |

**The stock-map rule.** OpenCE, ChupathingyCE and DamnationCE run the Xbox
game's own caches: their "stock" multiplayer maps are the Xbox maps, NTSC
01.10.12.2276 or PAL 01.01.14.2342 (the same build list as ours), named by
their Xbox level paths with version 0. They join as on the legacy wire:
never compared (regions play together), our file checked to be the Xbox map.
Halo PC's versions of those levels are other maps with other tags: a PC host
plays them as Custom Edition maps (`custom_maps\bloodgulch`), compared by
checksum like any other; our client with the Xbox `bloodgulch.map` is told
the host plays the PC map. They can't be made equivalent: objects are named
by tag index.

Names are untrusted: at most 63 characters, printable, no `/`, `:` or
`..`, a file name of at most 25 characters that map sharing would take (or
an Xbox level's), no device or resource map name, nothing under
`custom_maps\` but the file. Only a name that passed is looked for, and
only as a file of the maps folder. Each refusal names the map (es.txt has
the messages) and offers no download.


As built (T11, Oct 11 2026; section 10.3): `port/linux/src/crossplay_guard.c`
holds the rules and texts, `port/linux/game/network_crossplay_guard.c` the
game client's hooks. A co-op game is one with no game engine (how OpenCE
lists and hosts co-op) or on a campaign level: joined as Experimental since
section 6.2.

**Our hosts with PC clients** (phase 3, T15, built Oct 19 2026: 10.7, 7.4):
- The 16-player cap stays (`network_game_server_port_maximum_players`),
  checked with 16 players at a Vita's pace (`pccap`).
- The Vita host picks only maps and settings it can run. PC clients can't
  change them.
- PC clients' input and send rates are bounded by the existing per-machine
  budgets (`network_server_manager.c`'s message milliseconds), the tunnel's
  new caps on a crossplay Vita host (1000 packets a second a peer, 8 peers an
  address, 24 peers) and OpenCE's speed-hack check.

**Memory.**
- No change from crossplay as such.
- PC players' extra traffic is small: about 3.9 KB per tick at 128 players
  [D], about 0.5 KB at 16 [I].
- Voice decode is the one new memory and CPU cost. Decode at 16 kHz output and
  at most 4 talkers on the Vita, even if the host relays 8 (section 8).

**Official servers.** They are x86, so any PC load is fine there. Keep their
16-player cap and stock maps while Vitas join them. Big Team stays at 16.

### 6.2 PC co-op (G14, built Oct 11 2026)

The owner's decision: a Vita with Crossplay on joins a PC host's network
co-op game, badged Experimental and asked about once, as a large game is.
Branch feat/xp-coop.

**The wire is already the same** [V]. Co-op's distributed kinds have the
same numbers on both sides (64 actor states, 65 structure BSP, 66
presentation, 68 device groups, 69 object names, 70 skip vote, 71 events, 72
object transforms, 73 actor damage, 74 object looks, 75 damage animations,
76 screen effect, 77 device states), and every `struct distributed_coop_*`
and `distributed_actor_state` is the same field for field (diffed). Of
OpenCE's 18 co-op commits (8194c5a0 to bad4bf45) all were in but the game
half of 62fa7e13 (PLAYER COLLISIONS, ported now) and 140f0298 (a follow
camera kept valid, client-local; ours has its own fix). Our differences
from upstream's co-op code are host-side (the object census, actor states'
send periods, no object states for AI bipeds that actor states place) or
client-local (the pace hold, traces); an OpenCE host sends AI bipeds' object
states too, which our client ignores while their actor states are fresh.
The join is the same: co-op is a campaign level with no game engine; the
difficulty is the record's (checked: 0 to 3); OpenCE's advertisement has no
co-op bit (G2), so a co-op game is known by its engine 0 on a level that is
not one of the Xbox's multiplayer ones. `cooperative_flags` (G8) was already
carried and checked (only bit 0 known); its PLAYER COLLISIONS off now lets
the players' bipeds pass through each other on our client as on the host
(`network_coop_player_collisions`, bipeds.c, collisions.c). OpenCE allows up
to 16 players in co-op by default (Server Setup, up to its maximum), with
its extra enemies on (`per_player`, 50% of a squad more per player past the
first, at most 8 times the squad).

**What G14 really is** [V]. A distributed client runs no AI (game.c runs
`ai_update` only off clients; actors.c places no actor and no encounter on a
client) and no scripts in co-op (game.c skips `hs_update` on a co-op
client). So the actor, prop and swarm pools, the script object lists and
the game state they make (0xFC0000 against OpenCE's 0x13C0000) are filled
only by a host: a PC host's numbers never index them on our client. What a
host's numbers do index on a client:

| Index | Ours | OpenCE | Checked |
|---|---|---|---|
| object (datum index) | 8192 objects, 0x800000 bytes | the same | `distributed_object_index_valid`, tracked objects = objects per map |
| player, machine | 128, 128 | the same | the record's checks, `distributed_player` |
| device group | 1024 (devices.c) | the same | `device_group_find`: below the scenario's count and 1024 |
| object name, BSP, cutscene flag, title, HUD message, marker, breakable surface | the map's own | the same map | each against the scenario, model or BSP block's count |
| tag (sound, effect, animation) | the map's own | the same map | `distributed_tag_of_group`, `distributed_graph_animation` |
| AI units a client drives (actor states) | **288** (actors + 32) | 1056 (OpenCE's version 13) | the one real gap: now **1056 on the crossplay wire** |

A full AI table never corrupted anything: the unit's actor state was
dropped, its object states still placed it (an OpenCE host sends them for
every unit), and nothing drove its aim, fire and animations. The table now
holds OpenCE's 1024 + 32 on the crossplay wire (about 150 KB of static
data, outside the game state, so saves are untouched; the legacy wire keeps
288), found by the unit's absolute object index through a slot map
(`port/linux/game/network_actor_table.c`) rather than a walk of a thousand
entries for every state. A unit past it is still dropped gracefully, logged
once a game ("the host drives more AI units than this client tracks") and
counted; a state refused for its values is logged once with the field and
counted. The wire contract test now judges G14 this way (objects equal,
object memory at least the host's, the client's AI table at least the
host's actors + 32) and passes. Approach: (b) for the one table that a
client fills from a host, (a) for everything else, which already dropped
out-of-range entries without a crash and needed no change.

**Measured** (crosscoopcensus: OpenCE co-op hosts, our client's census each
second; the bots do not progress through a level, so the census covers each
level's start and every BSP the host switched through on its console):

| Level | Extra enemies | Players | Objects | Units | Living AI | AI units tracked (most) |
|---|---|---|---|---|---|---|
| a50 | per player (OpenCE's default) | 2 / 4 | 1176 / 1061 | 45 / 60 | 25 / 32 | 22 / 30 |
| b30 | per player | 2 / 4 | 1078 / 1156 | 95 / 129 | 82 / 110 | 31 / 31 |
| b40 | per player | 2 / 4 | 580 / 594 | 66 / 73 | 58 / 63 | 9 / 14 |
| c10 | per player | 2 / 4 | 963 / 995 | 29 / 31 | 13 / 13 | 7 / 7 |
| c20 | per player | 2 / 4 | 639 / 657 | 42 / 46 | 29 / 29 | 29 / 29 |
| c40 | per player | 2 / 4 | 541 / 574 | 39 / 47 | 11 / 11 | 8 / 8 |
| d20 | per player | 2 / 4 | 925 / 979 | 48 / 73 | 23 / 45 | 6 / 21 |
| d40 | per player | 2 / 4 | 722 / 778 | 63 / 82 | 35 / 30 | 8 / 0 |
| a50 | 32 times | 2 / 4 | 1563 / 1582 | 317 / 322 | 297 / 297 | **294 / 294** |
| b30 | 32 times | 2 / 4 | 2496 / 2231 | 808 / 812 | 794 / 794 | **651 / 651** |
| b40 | 32 times | 2 / 4 | 890 / 884 | 227 / 228 | 210 / 210 | 161 / 161 |
| c20 | 32 times | 2 / 4 | 1481 / 1488 | 898 / 898 | 886 / 886 | **897 / 897** |
| d20 | 32 times | 2 / 4 | 1028 / 1545 | 333 / 707 | 308 / 677 | 192 / 192 |
| d40 | 32 times | 4 | 1157 | 305 | 246 | 0 |

No AI unit was ever untracked and no actor state refused. With OpenCE's
defaults every level stays far inside every capacity, the old table of 288
included (31 at most). With its extra enemies at their maximum (32 times)
the host drives up to 897 AI units at once on c20 and 651 on b30: past the
old 288, inside the new 1056 (the host's own actor pool, 1024, bounds it).
Objects peaked at 2496 of 8192. a10 (its cryo tube waits for the host to
finish its look training, which the bots don't) and a30 (players spawn
after its long opening) were left at their starts.

The stress run found two things, both fixed: our checked (debug) builds
halted when the lights' cluster reference lists filled ("couldn't add to
reference list cluster light reference": our port's two append-at-the-end
adds asserted where OpenCE's and the Xbox's own add drop the datum; release
builds, the Vita's, already dropped it), and the overflow warning was
written once per datum per tick (20,000 debug.txt lines a minute), now 8
times a run. Our client's lights overflow those lists where OpenCE's
clients don't (our lights' port work keeps more clusters): at 32 times
some lights drop out of clusters on our client; at OpenCE's defaults the
lists never filled.

**Policy** (the owner's): allowed, Experimental, asked once: "PC co-op game:
experimental on the Vita. The host's AI and extra enemies can make it lag.
Join anyway?" (past 4 players: "PC co-op game with more than 4 players:
experimental on the Vita. More players and the host's extra enemies can
make it lag. Join anyway?"). Asked in either browser before the join and
remembered for its lobby, else in the lobby when the record comes; "no"
leaves it, and System Link's automatic join doesn't come back to it. Badges:
"PC CO-OP EXPERIMENTAL" (PC menus), "COOP EXP" (the panel's rows), "PC co-op:
experimental on the Vita" (its line in words). Large co-op (more than the
Vita's own 4) is allowed with the same single question, which says so: the
census shows no capacity limit reached at 4 players with extra enemies, so
it is a frame-rate question, as large multiplayer games are. The question is
asked once a game, on the players when it is asked. Kept: a Vita never hosts
on the crossplay wire; one local player in a crossplay game; the record's
values checked as before.

**Limits and risks.**
- CPU, not memory, is the Vita's limit: OpenCE's extra enemies and more
  players mean more units to simulate on every machine. Unmeasured on a
  Vita (owner-run hardware check).
- OpenCE's network test cannot turn PLAYER COLLISIONS off (only Server
  Setup does), so the collisions-off path is ported from upstream and
  reviewed, not exercised against a PC host in the harness.
- The bots do not play through a level: scripted encounters later in a
  level (the AI a level places as players advance) were reached only as far
  as BSP switches and level starts go; the stress run's multiplied extra
  enemies stand in for a level's peak.

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
| Out-of-range indices in a valid-sized struct (player, machine, actor, object, BSP, vehicle set, loadout) | distributed handlers, settings record | Bounds-check against **our** capacities, not OpenCE's (G14). A PC host may legally send indices our pools don't have. Co-op's audited (section 6.2): every one checked against what this build holds; T14 for the rest. |
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

### 7.4 PC clients on a Vita host: the host side's threat review (Oct 19 2026)

A Vita that hosts "for Vitas and PC" (network.host_crossplay, section 10.7)
receives OpenCE-family clients, and strangers' modified builds of them. The
Vita line was never a boundary (a GPL "Vita" builds on any PC), so every
handler below already faced hostile clients on the legacy wire; what is new
is the volume, OpenCE's message formats on our host, and the host announcing
OpenCE's version. Every message a PC client can send, in the order it meets
them:

| Stage | Message | Bounds and checks | Change made here |
|---|---|---|---|
| Signalling (MQTT) | JOIN, message version 4, sealed with the invite's key | at most 4 addresses (6 bytes each); then OpenCE's WebRTC block, read by its own rules (`skip_webrtc`: kind 0-2, a 32-byte certificate hash, ICE text of 4-32 and 22-64 ice-chars or none, at most 2 mDNS names of a ".local" form under 64 bytes with a nonzero port); then nothing or exactly the 24-byte proof. Anything else, a version 3 JOIN, or relays where the block goes: dropped unanswered. Unproven requests: answers from a bucket (32, one each 50 ms), key work from another (32, one each 50 ms), 256 askers remembered; a session only from a proof whose host nonce is this period's or the last's, each request once (1024 remembered) | the host's v4 parser and ACCEPT (exactly OpenCE's: addresses, kind none, tag); fuzz: net_fuzz_signal's host checks (kinds, trailing bytes, versions), p2p_signal_fuzz on both wires |
| Signalling | the listing's query (hceu/3/lobby/q) | a republish at most each 5 s, jittered | the host listens on its own wire's query alone |
| Tunnel (UDP) | sealed packets, types 1-5 | X25519 per session, ChaCha20-Poly1305, 64-packet replay window, KCP streams (4 a peer) | new caps on a Vita hosting for Vitas and PC: 1000 packets a second a peer (the rest dropped), 8 peers an address, 24 peers reached or reaching at once (the game takes 16) |
| Tunnel | type 6 (ping table), type 7 (friends' cards) | ours alone | never read from a client (6), neither sent nor read on the crossplay wire (7) |
| Game, client to host | join request | constant OpenCE token compared; hardware id kept as hex only; machine name cleaned; a full lobby refused; bans | none needed |
| Game | add/remove player, settings and player settings requests, start, exit, precached, loaded, switch to pregame | the field tables bound every size; a message of a bad size drops its machine; unknown types logged and dropped; each machine 10 ms of messages a frame, all 50 ms | none needed |
| Game | map download (35, ours) | | ignored on the crossplay wire: a type 35 from a PC client is not ours to answer, and our clients drop a host's 36/37 there |
| Distributed, client to host | only player prediction, player inputs, vehicle prediction, hit reports, client identity, client ready, co-op skip vote | readiness, identity and hits only over the client's stream; the sender must own the player (`distributed_machine_has_player`); object indices checked (8192 on both sides); a tick near the sender's latest; stale and fast-clock messages dropped; identity sanitized; hit reports through `distributed_report_valid` | none needed |
| Distributed | votekick 78/79, voice 80-82, OpenCE's map share 83-86, text chat 87-89 | beyond NUMBER_OF_DISTRIBUTED_MESSAGES: dropped | none needed |
| Distributed | aim assist policy 90 (OpenCE 32) | a host never takes one (from a client: ignored, logged once a second); a client takes it only from the host, over its stream, in a game of 32 under way, 12 bytes, count 1, value 0 or 1, reserved 0, before every other check (10.9) | new, with the twin's kind-90 mutation |
| Distributed, client to host | player inputs (32's friendly-fire byte 2) | the byte is not read (co-op for PC players is experimental: 10.9); the buttons are now held to the unit's 15 control flags (a twin's bit 15 halted a debug host: 10.9) | the mask |

**G14 on the host's side.** A client sends player and machine indices (128
on both sides) and object indices (8192 on both); it runs no AI, so no
actor, prop, swarm, encounter or script index comes from it, and OpenCE's
larger pools (actors 1024 against our 256) never index ours. The co-op skip
vote carries a machine index alone, bounded. No gap.

**Versions.** A join carries no network version (G15, true in all four
projects): the host cannot check a client's. Our host announces exactly the
built-in host version (32 since Oct 19 2026: 10.9) in its advertisement and
listing; OpenCE and DamnationCE clients join exactly their own (checked:
OpenCE of 28 and of 29 refuse our 32 host, `pcversion`). ChupathingyCE
clients accept 11..30, so they do not join a 32 host at all; an older
ChupathingyCE (26-28) could join a host of 29 (only a test build hosts one)
and play some things otherwise: no memory risk (every input is checked as
above), a residual simulation risk, named here. T10's signed table is not built: the built-in list is the floor,
so an unknown or unsigned version is refused, never accepted.

**Passwords.** Found by this review: our listings sealed the token with
Argon2id of four passes, OpenCE's with three, so no password ever opened
across. The crossplay wire now uses OpenCE's three (checked byte for byte in
p2p_lobby_test); the legacy wire keeps four. A friend's private game with a
password sends its sealed token to friends only, opened with the same key.

**A modified PC client (1.1.1-beta.1).** A PC player can change anything
in their game's memory. The host decides what matters and refuses what it
can check, and since 1.1.1 it counts what it refuses and drops a machine
that keeps sending what only a modified game sends (forged hits, hits
through walls or of an empty weapon, moves refused for seconds on end),
keeping it out of its games for ten minutes by its real address and
hardware id; the host's KICK and BAN are in its menus
(`port/linux/game/network_anticheat.c`; `port/linux/NETCODE.md`, "A
modified client"; `docs/security-review-1.1.1-beta.1.md`). The same checks
apply to a PC client and a Vita.

**Leaks and identity.** As 7.2: a PC peer sees the Vita's address (no relay
on the crossplay wire); the help says so (Who can join). Hardware ids are
self-reported. Friends' messages never ride the tunnel to PC peers.

**Fuzzing.** `crossfuzzhost` (netns, not in `all`): an AddressSanitizer +
UBSan build of ours (`HALO_NET_FUZZ_TWIN`, now twinning on the host side
too: client messages, datagrams) hosting for Vitas and PC, OpenCE clients on
a LAN and over the internet, one message in two twinned: 2048 and 1024
twins, no sanitizer memory error, no crash, the host to its game's end;
UBSan only the lines known before (units.c, render_sprite.c, ikcp.c,
thread_win32.c at start-up). The unit fuzz targets: signal (host checks),
lobby (both wires), friends.

**Not covered.** ChupathingyCE's Delta (port 5160): nothing listens there; a
browser (WebRTC) player cannot reach a Vita host (its block is stepped past,
the tunnel stays UDP); hardware run (T17).

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
| T3 Map identity v22 | Client side (owner, Oct 11: we never host on the crossplay wire): take a PC host's `custom_maps\<name>` and raw CE checksum, find the local map, refuse what is missing or unloadable naming it, never map share toward a PC host (section 6.1). Done Oct 11 (feat/xp-map-identity) | 3–4 | `run_crossplay_maps_test.sh` (unit + stand-in host), `crossplay_maps` fuzz target, netns `crossmaps` (XFAIL until T4); `badmap`/`mapmid`/`mapmidmixed`/`code` with Crossplay off |
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
| T11 Client guardrails | Refusals of section 6, browser badges (PC / Vita / dedicated), clear messages | 3–4 | netns `crossrefuse`: co-op refused, a second local player refused, missing map named (T3's text); `crossbig`: an OpenCE host of 128 joined after asking once (done Oct 11, 10.3) |
| T12 Browser for PC games | List hceu games (OpenCE/Chupa/Damnation hosts and servers), passwords (Argon2id cost on Vita: measure), invite link entry; optionally halo.milenko.org's list (**owner decision**: a third-party service; ask its operator first) | 3–4 | netns `browseropence` |
| T13 Votekick | Port OpenCE 3b20ef87 (78/79) | 3 | OpenCE harness test `votekick_count.c` ported; netns mixed vote |
| T14 Security pass 2 | Port missing ChupathingyCE map-hardening items; index audit for co-op messages; a security review of the branch | 3–4 | Map fuzz under ASan with CE maps from the owner's MCC folder (not in repo) |

### Phase 3: PC joins Vita hosts, about 8–10 agent-days

| Task | What | Days | Test |
|---|---|---|---|
| T15 Vita host on the crossplay wire | Host announces the table's version; PC clients' budgets; 16 cap; co-op with PC clients after the actor index audit. **Built Oct 19 2026** (10.7, 7.4) | 3–4 | netns `pcjoinsvita` (HALO_TICK_SLOWDOWN 19), `pccap` (8 OpenCE + 7 of ours, a 17th refused), `crossfuzzhost` |
| T16 Invites both ways | Our hosts produce `halo://join/<64 hex>` links (shown in the panel for PC friends); our short codes stay ours-only. **Built Oct 19 2026** (the Halo 3 lobby and the panel show the link; codes carry the wire) | 2 | netns `pcjoinsvita`'s invite pair: OpenCE joins by our link through both NATs, no relay |
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
| `crossrefuse` | OpenCE host: a second local player / missing map (co-op until 6.2) | our client refuses with the right message |
| `crosscoop` | OpenCE, ChupathingyCE, DamnationCE co-op hosts on b30; OpenCE with 4 players; answering no | asked once, plays beside the host, has its AI, takes its checkpoint and BSP switch, nothing untracked or refused (10.4) |
| `crossbig` | OpenCE host of 128 (more than 16: Experimental) | our client asks once, then plays; answering no, it plays none of it |
| `crossversion` | an OpenCE build one version ahead, table not widened, then widened (signed test key) | refused, then plays |
| `crossfuzz` | ASan build of ours, `debug.network_corrupt` a third of datagrams on the PC peer | no ASan report, game continues |
| `crossvoice` (phase 4) | OpenCE + ours with voice test tones (`HALO_VOICE_TEST`) | frames heard both ways, at most 4 decoded on ours |

**ChupathingyCE's harness as a cross-check.** ChupathingyCE's
`tools/crossplay_test.py` (CC0) uses loopback aliases instead of
namespaces. Its pass criteria list (join, spawn, kills, damage, score, end,
rejoin, stability) is worth copying into our checks, with credit.

### 10.1 The harness as built (T8, Oct 11 2026)

**Scripts** (in `port/vita/tests/`):
- `build_pc_crossplay.sh PROJECT CHECKOUT COMMIT [OUT]` builds `opence`,
  `chupa` (with `server-x86`) or `damnation` from a pinned commit into a
  new `~/halo-test-out/xppc-PROJECT.XXXX`, with `clang-sdl32`. It writes
  `BUILD_INFO` (full commit, compiler, binaries' sha256). No patches were
  needed for OpenCE 6399ed9d (build-185), ChupathingyCE 8cb79900 or
  DamnationCE 7be58b2b.
- `run_netns_crossplay_test.sh MODE` (`XPPC_BUILDS=<the builds' folder>`)
  runs the modes. `all` runs every one and prints a summary.
- `crossplay_check.py` holds the steps, ported from ChupathingyCE's
  `crossplay_test.py` with credit.

Our client runs with `HALO_CROSSPLAY=1`. The modes cover only our client
joining PC hosts and servers: `dedicatedopence` and `pcjoinsvita` were
dropped, since our servers and Vita hosts stay off the crossplay wire.
(Since Oct 19 2026, 10.7: no switch, our client runs with no setting, and
`pcjoinsvita` is back with `pccap`, `browserboth`, `crosspassword`,
`pcversion`, `crossfuzzhost` and `crosscoophost`.)

**No leaks.** The namespaces have no route out. On top of that, each router
drops and counts packets bound anywhere else, and names resolve only
through a test DNS that answers none. Each PC copy gets its own
`brokers.txt`, and the game list, Delta table fetch, UPnP, updates and
Discord are off. A run fails on any leak. None were seen; every client
spoke MQTT 5. With `XPPC_MQTT311=1` the broker refuses MQTT 5: OpenCE falls
back to 3.1.1 and `pcself` still passes.

**Results on f94d8961 (network version 18).** The PC-only modes pass. Our
client's modes are expected failures: they report XFAIL (exit 0) at their
first failing stage, next to an OpenCE-client control that passes.

| Mode | Result | Where our client stops |
|---|---|---|
| `pcself` | PASS | (OpenCE against OpenCE: LAN; internet through the server browser) |
| `chupaopence` | PASS | (both ways on a LAN; OpenCE host, ChupathingyCE client by invite) |
| `opencelan` | XFAIL | version: "not joining a host of network version 26 (this machine's is 18)" |
| `opencehost` | XFAIL | browser: discover (the host lists on `hceu`, our client reads `hcev`); invite: signal (our client signals on `hcev` and never reaches the host) |
| `chupahost` (LAN) | XFAIL | discover: "ignoring a host that is not a Vita" |
| `chupaserver` | XFAIL | LAN: version; internet: discover (`hceu` listing) |
| `damnserver` | XFAIL | LAN: version; internet by invite: signal |
| `crossrefuse` | XFAIL | no right refusal message yet (T11); refused for the network version instead |
| `crossmaps` (T3) | XFAIL | version (OpenCE hosts on Blood Gulch and on `custom_maps\pcgulch`, had and lacked; the OpenCE controls pass). The map check itself runs today against our build standing in for a PC host: `run_crossplay_maps_test.sh` |
| `crossmixed`, `crossversion` | SKIP | wait for T4 and T10 |

**Findings for the wire tasks:**
- **G2 seen live.** Our client reads OpenCE's 0x02 "in progress" flag as
  the Vita flag. A PC host in its lobby is ignored as "not a Vita". Once its
  game is under way, the host passes that check and is refused on the
  version instead.
- **Default maximum players.** An OpenCE host made by fast setup (as its
  network test hosts) takes 128 players (`HALO_PORT_MAXIMUM_NETWORK_PLAYERS`).
  T11's "refuse > 16 maximum" would refuse every such game, so it may need
  to look at current players, or at what the host set.
- **DamnationCE's server** is listed only on halo.milenko.org, not in the
  MQTT lobby. Without that list (owner question 6), a Vita reaches it only
  by its invite link.
- **ChupathingyCE's network test** can't browse or list a player's game,
  and its host logs only the start of its invite. That leaves its player
  hosts LAN-only in the harness; its server covers the internet.
- **OpenCE build-185 host crash.** The host crashed twice when a client
  joined its postgame while the network test was moving players (with
  ChupathingyCE's score of 4). Score 10 and a second client avoid it.

**Hardware.** Hardware checks come last and are owner-run: one Vita on an
official crossplay server with a PC player, then a Vita joining a PC host.
Ask before any deploy or quit on a Vita.

### 10.2 The wire switch as built (T4 + T5 client side, Oct 11 2026)

Branch feat/xp-wire-switch. One switch, as the owner decided: no per-game
wire choice and no Vita host on the crossplay wire.

- `port/linux/include/network_wire.h`, `port/linux/src/network_wire.c`:
  `network.crossplay` read once, at the network's start, and kept for the
  run (the panel's Crossplay row now applies after a restart). Ad hoc and
  the dedicated server are always legacy. The crossplay wire speaks 26 and
  joins the versions of a compiled-in list (26 alone) until T10's table
  (28 alone since Oct 13, 29 alone since Oct 14: sections 10.5, 10.6).
- **Off** changes no byte: the legacy wire's topics, message version 3,
  relays, listing format 2, token XOR, flags and the 13,092-byte record are
  as before, and the four OpenCE behaviour fixes ported in c4835524
  (1f87b4ff, 22fe5628, 792711ca, ab3c388c) apply only on the crossplay
  wire, so a beta.4 machine and a beta.5 one agree.
- **On**: OpenCE's advertisement flags (the Vita and co-op bits are not
  read), the constant join token, `hceu` signalling with message version 4
  and a WebRTC block of kind none (a host's block is checked and skipped:
  the tunnel stays plain UDP), no relays, listing format 1 under
  `hceu-lobby-1`. Hosting is refused on every path (Create Game, fast
  setup, co-op, the panel), with the player told to turn Crossplay off.
  Our extensions stay silent toward PC machines (T6-lite): map share's
  messages are never sent, codes are refused with a message, relays are
  off, and the ping table is neither sent nor read.

**Results (netns, our client with `HALO_CROSSPLAY=1`):** opencelan,
opencehost (browser and invite link), chupahost, chupaserver and damnserver
pass every step, so they are now acceptance tests (`XPPC_EXPECTED_FAIL`
defaults to `crossrefuse`, which waits for T11). The new `crossmixed` mode
checks that the two worlds stay apart. The wire contract test closes G2,
G3, G4, G5, G12, G13 and G15; still open: G6 (T3), G9, G10, G11, G14.

### 10.3 The client guardrails as built (T11, Oct 11 2026)

Branch feat/xp-guardrails. With Crossplay on (and only then):

- **PC co-op games are refused** (G14; superseded the same day by 10.4:
  joined as Experimental), with "This is a PC co-op game.
  Co-op games on PC hosts can't be joined with Crossplay on: they run more
  AI than the Vita can hold." Checked in both server browsers (the PC
  menus', `menu_functions.c`, and the settings panel's), on a LAN
  advertisement before any join (`network_game_client_advertised_game_compatible`)
  and again on the settings record (the game left). OpenCE's listing has
  no co-op bit: a format-1 listing with game engine 0 is co-op, shown
  without a difficulty it never sent.
- **Large PC games are allowed** (more than 16 players in the game, or a
  maximum above 16): the browsers badge them EXPERIMENTAL, and joining one
  asks once: "Large PC game: more than 16 players can make the Vita lag.
  Join anyway?" The browser asks before the join and remembers the answer
  by the game's name for two minutes, so the lobby doesn't ask again. A game
  joined from System Link is asked about in its lobby when the settings
  record arrives, and "no" leaves it. Player counts never refuse a game.
  `HALO_CROSSPLAY_LARGE_ANSWER=yes|no` answers for automated runs.
- **Badges** (crossplay wire only; the legacy wire's rows are unchanged,
  all of them Vitas'): "PC" or "PC SERVER" (OpenCE's listing has
  ChupathingyCE's dedicated bit), then " EXPERIMENTAL" or " CO-OP". The PC
  menus put them before the name, `[PC SERVER EXPERIMENTAL] XpServer`. The
  panel's rows end in PC/SRV and EXP/COOP, and the chosen game gets a line
  in words. A "VITA" badge exists (`crossplay_guard_badge`) but never shows:
  with one switch, a listing never mixes the two worlds.
- **Join with a code** is hidden on the panel while the running wire is
  the crossplay one (codes join Vita games).
- **Split screen** (PS TV): one local player in a crossplay game. Another
  controller's join isn't sent, and that controller is told once.
  `HALO_NETWORK_TEST_SECOND_PLAYER` tests it.
- Missing, too-big and unsupported maps are T3's (feat/xp-map-identity).
  `crossrefuse` checks its refusal once the build has T3's text.

**Results (netns, our client with `HALO_CROSSPLAY=1`):** `crossrefuse`
PASS (co-op refused before any join; a second local player refused on
every copy, the game played every step); `crossbig` PASS (asked exactly
once on a LAN and from the browser, the listing badged EXPERIMENTAL, every
step passed; answering no: left, no tick played). `XPPC_EXPECTED_FAIL` is
now empty. The strict suite (`XPPC_STRICT=1 all`) passes every mode but
crossversion (SKIP, T10), with no leaks. It found one bug, now fixed: a
ChupathingyCE host's settings record between games names Blood Gulch with
no game engine for a moment, so no engine on one of the Xbox's multiplayer
levels is not co-op.

### 10.4 PC co-op as built (Oct 11 2026)

Branch feat/xp-coop (section 6.2). With Crossplay on:

- **PC co-op games are joined, Experimental**, after asking once (the
  large-game mechanism: `crossplay_guard_question`, the answer remembered by
  the game's name for its lobby, `HALO_CROSSPLAY_LARGE_ANSWER` for automated
  runs). Badges: "PC CO-OP EXPERIMENTAL", the panel's "COOP EXP" and "PC
  co-op: experimental on the Vita". The refusal text and its paths are gone;
  the second local player and the map refusals stay.
- **The client's AI unit table** holds 1056 on the crossplay wire
  (`network_actor_table.c`, found by absolute object index; 288 on the
  legacy wire), its overflow logged once and counted, refused actor states
  logged with the field. The network test's co-op line logs the census.
- **PLAYER COLLISIONS off** (`cooperative_flags` bit 0) is played
  (`network_coop_player_collisions`, OpenCE 62fa7e13's game half).
- **Fixes found by the new tests**: a full cluster reference list drops the
  datum (warned 8 times a run) instead of halting a checked build, and no
  NULL transparent group's references are written through; a nav point
  with no screen type yet draws no arrow (was index -1), the host's nav
  point offset pinned; no division by 0 local players in effects' particle
  counts.

**Tests:**
- `crosscoop` (in `all`): OpenCE, ChupathingyCE and DamnationCE co-op hosts
  on b30, our client asked once, 166-193 s beside the host's player, the
  host's AI units driven (at most 31-44 tracked at once), the host's
  checkpoint taken (`game_save` on its telnet console) and its
  `switch_bsp 1` followed (121-145 s on it), nothing untracked or refused;
  four players (ours and two OpenCE clients): all four alive 174-186 s;
  answering no: left, no co-op tick played. Control: an OpenCE client.
- `crosscoopcensus` (not in `all`): the measurements of 6.2.
- `crosscoopfuzz` (not in `all`; an ASan + UBSan `HALO_NET_FUZZ_TWIN` build
  of ours): one in two of the host's AI and co-op messages (kinds 16 and
  64-77, `HALO_NET_FUZZ_KINDS`) twinned on each of the three PC builds' co-op
  hosts, four players on OpenCE, and one in three of every message: about
  30,000 twins, no AddressSanitizer error, no crash; UBSan only the lines
  known before (start-up, the null renderer, render_sprite.c, units.c). Its
  first run found the nav point index; the ASan stress census (32 times,
  b30 and c20) the effects division.
- `actor_table` network fuzz target (run_net_fuzz_test.sh): the table
  against a plain list, with ASan and UBSan, kept cases and libFuzzer.
- The wire contract test passes G14 (and G8's text says the switch is
  played).

**Gates** (feat/xp-coop): the strict crossplay suite (`XPPC_STRICT=1 all`)
passes every mode but crossversion (SKIP, T10) and crossmaps (SKIP there for
want of a CE map; run on its own with pcgulch and the Custom Edition
resource maps: PASS), no leaks; Crossplay off: netns code, coop, lobby,
menus, dedicated and swatmixed (beta binaries) PASS, the wire contract's
legacy baseline unchanged; settings test (381 checks a configuration),
p2p_lobby (175), crossplay_guard (28), browser list, network wire, port
config, players' machines, lang (552 of 552 translated) and the network
fuzz targets (kept cases, then 60 s of libFuzzer each, actor_table
included) PASS; a10 and b30 150 s threaded single player exit 0; the Vita
build compiles (eboot and VPK).

**Not checked here:** the collisions-off path against a PC host (OpenCE's
network test leaves it on; only its Server Setup turns it off); a real
Vita's frame rate in a PC co-op game (owner-run).

### 10.5 Network version 28 (Oct 13 2026)

(Superseded by 10.6 the next day for the version and the map names: the
rules ported here stay.)

Branch feat/xp-v28. Current OpenCE and ChupathingyCE hosts announce 28; our
client joined only 26, so it could join none of them.

**What changed between 26 and 28** (OpenCE 6399ed9d..b1ef7f7e, 56 commits,
and ChupathingyCE 8cb79900..35fc7e8b, 79 commits, read in full) [V]:

| Upstream | What | Wire or simulation? | Here |
|---|---|---|---|
| OpenCE a2aad18f (v27; Chupa c1805623) | Custom Edition map sharing: distributed kinds 83-86 (request, offer, sources, progress), handled in the lobby too; map files in `custom_maps` renamed `<name>_<8 hex>.map` (BLAKE2b of the file's 256 KiB pieces), so a host's level name is `custom_maps\<name>_<8 hex>`; a TCP swarm between players on port 5153; signalling may ask another player for a session (`p2p_signal_join_player`, same message format and version 4) | wire: new kinds and the name | kinds dropped as unknown (past our 78; wire contract S6), our own map share messages 36/37 from a host dropped on the crossplay wire (S7); the hashed name played (below); nothing listens on 5153 and our client never answers a player's session request, so a PC player downloading takes the map from the host and the other PCs |
| OpenCE 67f22142, 71ceb363, baab0780, 5a7c655a (v28 item 1) | a machine still downloading the map is left out of the start and its players held out until it has it, then added as late joiners | the host's | nothing: our client never hosts there and never downloads from a PC host; the late adds are the join-in-progress path it already plays |
| OpenCE c3365b1c, b71bd6a0 (v28 item 2; Chupa 2bc7d41a, 645c875a) | a multiplayer script's `switch_bsp` keeps the game's first BSP; later ones to another copy (sky-cycling maps) were a level reload every couple of seconds | simulation: every machine runs the scripts | ported, `network_wire_v28_rules` (hs.c) |
| OpenCE 417de7e2 (v28 item 3; Chupa f05544ff) | dynamic lights and the flashlight stay on past the Xbox's player and vehicle limits | simulation (units' integrated lights are in the object states since v25) | ported, `network_wire_v28_rules` (game_engine.c) |
| OpenCE 2598dbe8 (v28 item 4; Chupa 96436597) | a second weapon picked up into a one-weapon loadout while firing is readied; an overshield's drain no longer flashes the shield meter | simulation; HUD | ported, `network_wire_v28_rules` (units.c; hud_unit.c, our own HUD kept with it) |
| OpenCE e791cf9a | a body the host moves is no longer drawn floating or in a wall on clients | client-local, no message | ported, crossplay wire only (network_objects.c) |
| Chupa 2ec77029 | a client deleting a CTF flag on the host's word no longer halts a debug build | debug assert | ported, both wires (release builds check nothing, so nothing changes there) |
| OpenCE 0b4a0356, d6df9448, 9633a7c3, 2059b6ce, 0b2c6759 | co-op respawn beside the group, co-op teleports spread by the host, co-op grav lifts, the lobby countdown, late join checks | the host's (co-op clients run no scripts here: game.c) | not needed for our client |
| the rest (rasterizer, text, menus, web, Windows, config, STUN retry) | | local | not ported here |

Chupathingy's other new work (map downloads from community repositories,
blocked names, carnage reports, settings) is its own and changes nothing a
Vita client sees. OpenCE's recorded wire format (`network_format.txt`,
c6a2f900) says the same: from 26 to 28 items were only added.

**The decision: exactly 28, no range.** A machine of 26 or 27 and one of 28
disagree on the three simulation items above with no message to tell them
apart (a 27 host reloads a sky-cycling map that 28 clients keep), so a mixed
game plays on wrong rather than being refused. ChupathingyCE draws the same
line: it gives older wires no row for 28 (wire `chupa-28a`). Accepting
26-28 would need two sets of rules in the simulation, chosen by the host's
version, tested against hosts that are being replaced; one version keeps
one simulation, one test matrix and nothing a stranger's version number can
switch. DamnationCE 7be58b2b is still 26: its servers are refused until it
moves (its workflow follows OpenCE's raises). The table (5.2) can widen
later, only after the suite passes against the exact build.

**Gating.** `network_wire_v28_rules()` (network_wire.h) is on with Crossplay
on, whose hosts are all 28, and off on the legacy wire, ad hoc and the
dedicated server: Vita-to-Vita games play as betas 1-4 (network_wire_test
checks each). The body fix is gated on the crossplay wire as the earlier
upstream client fixes are.

**Refusals.** A PC host of an older version: "This PC game's host runs an
older version of OpenCE or ChupathingyCE (network version 26). Crossplay
joins hosts of network version 28, the current OpenCE and ChupathingyCE.
Ask the host to update, or join another game." A newer one is told to
update the game. The server browser never lists another version's game.

**The hashed name** (`crossplay_maps.c`, `custom_edition_cache.c`). A name
`custom_maps\<base>_<8 lowercase hex>` is taken when the base passes every
rule a whole name does (at most 25 characters, map sharing's file name rules,
no device or resource map name, no separator, drive or `..`): 34 characters
at most. Digits in uppercase, or fewer than 8, are no hash, so the whole is
a plain name held to 25, as OpenCE has it. Only a name that passed is looked
for, as a file of the maps folder: `<base>_<digits>.map` (an OpenCE player's
copy), else `<base>.map`; the loader takes `<base>.map` for the hashed name
only when it is a Custom Edition cache whose hash begins with the digits.
The file found must have the host's header checksum and the name's hash,
else it is refused as another version ("Your pcgulch.map isn't the same
version as the PC host's"); missing, it is refused naming `<base>`. The hash
is BLAKE2b over 64 KiB reads (no piece buffer), once per file and size for
the run, logged with its time (`crossplay: <path> hashed: <digits>, <bytes>
in <ms>`). On the Vita that is a few seconds for a 50 MB map in the lobby,
once: measure on hardware.

**Tests** (the harness: `--linux-d3d gxm-null --lto off --pgo off
--linux-net-vita`, clang-sdl32-reloc; PC builds by build_pc_crossplay.sh:
OpenCE b1ef7f7e, ChupathingyCE 35fc7e8b, and for the refusals OpenCE
6399ed9d, ChupathingyCE 8cb79900 and DamnationCE 7be58b2b, all 26):

| Test | Result |
|---|---|
| network_wire_test (on, off, ad hoc, release, dedicated) | PASS: 28 joined; 26, 27, 29, 30 refused; v28 rules on with Crossplay only |
| wire contract against OpenCE b1ef7f7e | PASS: baseline S1-S4, S6 (kinds 83-86 dropped), S7 (36/37 dropped), L1/L2 legacy record byte for byte; G1-G5, G7, G8, G12-G15 closed (G15 announces 28) |
| crossplay_maps_test (ASan, UBSan) | PASS, 158 checks: hashed names, hostile ones, the hash against hashlib vectors and a real map (pcgulch: `69cd51eb`) |
| net fuzz crossplay_maps, lobby | PASS: kept cases, then 90 s and 60 s of libFuzzer (one case found and kept: a 24-character base, fixed in the test's bound) |
| run_crossplay_maps_test.sh, stand-in host | PASS: stock, ce, cemissing, cediffer, cexbox, cehash, cehashfile, cehashwrong (same checksum, other digits: refused by the hash), cehashmissing |
| crossplay record test | PASS |
| netns `opencelan`, `opencehost` (browser and invite), `crosstls` | PASS (OpenCE b1ef7f7e) |
| netns `chupahost`, `chupaserver` (LAN and internet) | PASS (ChupathingyCE 35fc7e8b) |
| netns `crossctf` | PASS: OpenCE and ChupathingyCE CTF games played to their end, scoreboards alike (the network test's CTF game lasts seconds) |
| netns `crosscoop` | PASS: OpenCE and ChupathingyCE b30 co-op, asked once, 168-194 s beside the host, its checkpoint and BSP switch, 0 untracked; 4 players 190 s; answering no: left |
| netns `crossmaps` | PASS: the OpenCE host renamed its link `pcgulch_69cd51eb.map` and named the game so; our client played from `pcgulch.map` and from `pcgulch_69cd51eb.map` (hashed in about 120 ms on x86), refused another version and a missing map by name |
| netns `crossbig`, `crossrefuse`, `crossmixed` | PASS |
| netns `crossold` | PASS: OpenCE 6399ed9d and ChupathingyCE 8cb79900 (26) on a LAN refused with the older-version message; over the internet "dropped 3 of another version", never listed |
| netns `damnserver` | SKIP: DamnationCE 7be58b2b is 26 (crossold's refusal); its co-op game left out of crosscoop |
| netns Crossplay off: `lan`, `code`, `lobby`, `coop` | PASS |
| netns Crossplay off: `dedicated`, `swatmixed` (beta.2 binaries) | PASS (`ninja linux-server`) |
| a10, b30 150 s threaded single player (gxm-null) | exit 0, no assertion or fault |
| Vita release build (`--lto off --pgo off --portable --release`, `ninja vita`) | compiles: eboot.bin and halo.vpk (at d8791745) |

**Not checked here:** a real Vita (hashing time on the memory card, a game
against a PC host on hardware); a sky-cycling multiplayer map (none in the
test data: the rule is OpenCE's and ChupathingyCE's line for line); a PC
player downloading a map from the host while our client is in its game.

### 10.6 Network version 29 (Oct 14 2026)

Branch feat/xp-v29. OpenCE released build-206 (8102e711) and ChupathingyCE
0.7.5b (7d6d8f24), both announcing 29; our 28 client refused them all.

**What changed between 28 and 29** (OpenCE b1ef7f7e..8102e711, 5 commits,
and ChupathingyCE 35fc7e8b..7d6d8f24, 50 commits, read in full) [V]:

| Upstream | What | Wire or simulation? | Here |
|---|---|---|---|
| OpenCE 3e0ba551 (29; Chupa a501871f) | a Custom Edition map's level name is its own again (`custom_maps\hugeass`) and the record's map version is the first 32 bits of the map file's hash (0 made 1), where 22-28 sent the header checksum and 27-28 a hashed name; maps are no longer renamed, a download beside another version is saved `<name>_<8 hex>.map` | wire: what the level name and map version mean | ported (below) |
| OpenCE 52e33a39, 6d64abdb | map sharing and Custom Edition loading hardened (tag names in tag data, lightmap vertices, game globals without camera/player/first-person blocks refused, snprintf for long tag names), p2p virtual addresses not reused across machines | local | not ported here (our loader has its own validation; a candidate for later) |
| OpenCE 37ded328 | harness tests for clang 23 | none | |
| Chupa 33f2e7e9, a122e2d2 | Delta: its own hosts advertise a wire id; a presumed-additive next number | Chupa's own extension (reserved bytes after OpenCE's) | nothing: our client reads OpenCE's version and flags only |
| Chupa 92aaf638 | co-op: a client gives an AI unit a grenade type from the host's inventory, so its throws are seen | client-local display | not ported (co-op only, not wire; a later candidate) |
| Chupa 2e7c88b5, abc42998, 0cbcac04, fd9c9183 and the rest | co-op respawns, checkpoint timing (the host's), field of view and match timer, HUD sound, menus, Android, settings | host-side or local | nothing |

No simulation rule changed: 29 keeps 28's (`network_wire_v28_rules`).

**The decision: exactly 29.** A 28 and a 29 machine read the same record
as different maps (a hashed name and a checksum against a plain name and a
hash version), so a 28 host is refused like 26 and 27, with the
older-version message. ChupathingyCE's client takes both meanings; we keep
one meaning and one test matrix on release day.

**Map identity** (`crossplay_maps.c`, `custom_edition_cache.c`). The name is
held to the same rules as before (at most 25 characters, map sharing's file
name rules, no device or resource map name, no separator, drive or `..`);
a name ending in what looks like a hash is just a name. With the host's
version (nonzero) our client takes `<name>.map`, else
`<name>_<8 lowercase hex digits of the version>.map`, whichever is a Custom
Edition cache whose hash is that version; another version is never played
in its place. The cache loader asks the client's game record for the
version of the level it loads (`network_game_client_crossplay_map_version`),
so a player with both versions loads the host's. With version 0 (a host that
could not hash its map) `<name>.map` plays, nothing compared, as OpenCE's
clients do. A file that is another version is refused as such; none, by
name. Hashes are cached per file for the run (x86: 46 MB in about 120 ms;
measure on hardware).

**Tests** (OpenCE 8102e711, ChupathingyCE 7d6d8f24 from build_pc_crossplay.sh;
`crossold` against the 28 builds of 10.5):

| Test | Result |
|---|---|
| network_wire_test (on, off, ad hoc, release, dedicated), p2p_lobby, lang | PASS: 29 joined; 26, 27, 28, 30, 31 refused |
| wire contract against OpenCE 8102e711 | PASS: baseline S1-S4, S6, S7, L1/L2; G15 announces 29 |
| crossplay_maps_test (ASan, UBSan), fuzz crossplay_maps (60 s) | PASS: versions (0 as 1), another version's file name, the hash against hashlib and a real map (pcgulch: version `69cd51eb`) |
| run_crossplay_maps_test.sh, stand-in host | PASS: stock, ce, cemissing, cediffer, cexbox, cever, cebeside, ceverwrong (the first cever run found the lobby precaching before the record held the version: fixed, 1e12bb2c) |
| `run_netns_crossplay_test.sh all` | PASS: pcself, chupaopence, opencelan, opencehost, crosstls, chupahost, chupaserver, crossctf, crossmaps, crossmixed, crossrefuse, crossbig, crosscoop, crossold; SKIP damnserver (DamnationCE 7be58b2b is 26; its co-op game left out of crosscoop) and crossversion (T10) |
| crossmaps detail | the OpenCE host named `custom_maps\pcgulch (copy 0x69CD51EB)`; our client found `pcgulch.map`, only `pcgulch_69cd51eb.map`, and `pcgulch_69cd51eb.map` beside another version's `pcgulch.map`, each of version `69cd51eb`; another version and none refused by name |
| crossold detail | OpenCE b1ef7f7e and ChupathingyCE 35fc7e8b (28) on a LAN refused with the older-version message; over the internet dropped for their version, never listed |
| Crossplay off: `code`, `lan`, `coop`, `dedicated` | PASS |
| a10, b30 150 s threaded single player | exit 0, no assertion |
| Vita release build (`--lto off --pgo off --portable --release`, `ninja vita`) | compiles: eboot.bin, halo.vpk (93c3dedd) |

### 10.7 Per-game wires and PC players on Vita hosts (Oct 19 2026)

Branch feat/crossplay-auto. The owner's decisions:

1. **No switch.** network.crossplay is retired. Each game speaks one wire,
   chosen as it begins (`network_wire.h`): a client takes the joined game's
   (its advertisement's version and flags; the listing's topics; a code's
   record, version 3 with the wire; a friend's presence or the party's
   leader; an invite link's host asked on both wires, the one it answers on
   kept), a host the one network.host_crossplay says. With no game, ad hoc,
   the dedicated server and builds that do not play as a Vita: the legacy
   wire. Signalling and the browsers read both wires: the brokers of both
   files, each once (up to 6), both wires' slots and queries.
2. **Tags.** Every row says what hosts the game (`crossplay_guard.h`):
   VITA (the legacy wire's, or a Vita's game on the crossplay wire: its
   advertisement's 0x40 and its listing's 64, bits PC builds ignore), +PC
   when PC players can join it, OPENCE (a crossplay advertisement with no
   other mark), CHUPA (its Delta bit 0x04; its dedicated server's listing
   bit 128), DAMNATION (kept for when the wire tells it: today nothing does,
   and its 7be58b2b is refused for its version), PC (a listing that says
   nothing more); then SERVER, EXPERIMENTAL or CO-OP EXPERIMENTAL for a PC
   game. In the Halo 3 browser (its VITA/PC art, the rest before the name),
   OpenCE's server browser, the System Link list, the Halo 3 LAN browser,
   the settings panel's public games, friends' activity lines and the
   pregame lobby ("Listed as: name (VITA+PC)", "Hosted on PC (OpenCE)").
   Browser shows (network.browser_filter): all (as shipped), Vita, PC; the
   retired switch's On becomes PC games, Off all. Every browser lists the
   Vitas' games first, then the PCs' (the owner, Oct 19 2026): the Halo 3
   and Halo CE browsers and the LAN browser under a header row for each
   group with its count (VITA GAMES (n), PC GAMES (n); never chosen, stepped
   over), the Xbox's System Link list and the settings panel's list in that
   order; within a group the sort is Triangle's (players as shipped: the
   most first, then games not failed, games taking players, the name; no
   ping). The D-pad's left and right step the filter on those browsers (All
   games, Vita games, PC games), kept as the panel's Browser shows is (the
   Vita's settings.txt). `run_menus_test.sh h3groups`, `browser_list_test`.
3. **PC joins Vita (T15, T16).** Who can join (the settings panel's Vita and
   PC page, and a WHO CAN JOIN row on both Create Game screens): Vitas (the
   legacy wire), or Vitas and PC (the crossplay wire). Such a game is listed
   as OpenCE's browsers read it (format 1, hceu-lobby-1, version 29), its
   name beginning "[VITA] " for PC players; keeps the Vitas' 16; names its
   Custom Edition maps as OpenCE does (`custom_maps\<name>`, the hash
   version); never serves map share; its join link (halo://join/..., as
   OpenCE's) shows in the Halo 3 lobby and the settings panel for PC
   friends; its code stays ours (version 3 record). Builds before this
   cannot join it (its token, record and wire are OpenCE's): they are told
   to update, or see no game for the code. Co-op hosted for Vitas and PC is
   experimental: `crosscoophost`.

Security: section 7.4.

**Tests** (OpenCE 8102e711 and ChupathingyCE 7d6d8f24 of 10.6; OpenCE
b1ef7f7e of 28 and 6ec42856 of 32 for the versions; the harness build
`--linux-net-vita --linux-d3d gxm-null`):

| Test | Result |
|---|---|
| `pcjoinsvita`: our host (HALO_TICK_SLOWDOWN 19) for Vitas and PC; OpenCE clients on its LAN, from OpenCE's browser, by our host's join link through both NATs | PASS, every step of crossplay_check (join, spawn, kills, damage, score, end, rejoin, stability) |
| `pccap`: the same host, 8 OpenCE clients and 7 of ours on one LAN, a 17th OpenCE | PASS: 16 players, the 17th joins nothing |
| `browserboth`: an OpenCE host, our Vita host for Vitas, ours for Vitas and PC, all public; our clients, no setting changed | PASS: rows `[PC EXPERIMENTAL] <OpenCE's name>`, `[VITA] XpVitaHost`, `[VITA+PC] XpVitaPc`; one client joins each from the same list and plays on its wire; Browser shows Vita games lists the two Vitas' alone; on one LAN the System Link list `[OPENCE] ..., [VITA+PC] ..., [VITA] ...` |
| `crossmixed` (rewritten): our host for Vitas, for Vitas and PC (LAN and browser), an OpenCE host, each joined by the same client | PASS |
| `crosspassword`: our host for Vitas and PC with a password | PASS: a wrong password refused, the right one plays; OpenCE's browser leaves the locked game alone |
| `pcversion` | PASS: OpenCE of 28 and of 32 do not join our 29 host; our client refuses an OpenCE host of 32 on a LAN ("newer version") and drops its listing online |
| `crossfuzzhost` (ASan, twins on the host) | PASS (7.4) |
| `crosscoophost` | NOT VALIDATED: the host never leaves the lobby because its campaign map's precache (a10, b30) never finishes. The same happens with the legacy control (`XPPC_COOPHOST_WIRE=legacy`), the online suite's `coop` mode, and the base build 02dbe9382, so it is not this work, but co-op hosted for Vitas and PC has not been played end to end. It stays experimental and is on the hardware test list |
| unit: network_wire_test (four builds), p2p_lobby_test, signal and lobby fuzz targets, p2p_friends_test, vita_settings_test, lang | PASS |

### 10.8 Upstream OpenCE at network version 32 (6ec42856, Oct 19 2026): supported since 10.9

OpenCE's origin/main announces 32 (329 commits past 8102e711). Per 5.2 the
crossplay wire does not widen until the suite passes against that build and
the changes are ported; our builds keep joining and announcing 29, and tell
the player of a 32 host that it is newer (`pcversion`). The raises:

| Version | OpenCE | What | Wire or simulation |
|---|---|---|---|
| 30 | 607da502 | an elevator's riders carried by the same tick's movement on every machine; a client's vertical error kept only within 4 units | simulation |
| 31 | 167d67ec, e595f89c | the keep-alive's field table (a long; its two stray bytes zero), the datagram tick window recorded (`DISTRIBUTED_TIME_WINDOW_TICKS`), hits, identity and readiness only over a client's stream, a datagram's source port kept | wire table; checks (ours has the stream-only rule and a tick window already) |
| 32 | 31ba6b37 | the host's aim assist policy, a distributed message (90) a client must not ignore | wire: a new kind |

Done the same day, clean-room from docs/opence-net32-spec.md: section 10.9.


### 10.9 Network versions 29 and 32, per game (Oct 19 2026)

Branch feat/opence-net32. OpenCE relicensed to AGPL-3.0-or-later at 4b09168e,
and everything that raised its version past 29 came after that point, so
this was built **clean-room**: from `docs/opence-net32-spec.md` (facts and
rules written down by another agent, no code) and our own code. No OpenCE
source at or after 4b09168e was opened, read, grepped or diffed; OpenCE
6ec42856 ran only as a black box (its binary, built by build_pc_crossplay.sh
with no patch). The harness takes its version from the spec, by its commit,
never from its tree (`pc_version`).

**Versions, per game** (`network_wire.h`). The crossplay wire joins exactly
29 and 32 (`HALO_PORT_CROSSPLAY_OLDER_NETWORK_VERSION`,
`HALO_PORT_CROSSPLAY_NETWORK_VERSION`); a client plays the version its
host's advertisement announces (`network_wire_choose_version`), a Vita
hosting for Vitas and PC announces and plays 32 (`network_wire_host_version`;
test builds alone take `HALO_TEST_CROSSPLAY_HOST_VERSION=29`). Not 30 or 31
(never released; each plays part of 32's rules alone), nothing newer. The
rules are switched by `network_wire_v30_rules`, `_v31_rules`, `_v32_rules`:
off on the legacy wire and in a game of 29, which play as before. Refusals:
an older host is told the version to look for (29 for 26-28, 32 for 30-31),
a newer one is told to update; the log names both joined versions
(`crossplay: this machine joins 29 and 32`).

**A choice for the owner.** A Vita hosting for Vitas and PC (Who can join:
Everyone since 10.10) announces 32, so
current OpenCE (and DamnationCE once it follows) joins it, and ChupathingyCE
(29; its clients accept 11..30) and OpenCE build-206 do not. Hosting 29
instead would trade one for the other. A "PC version" choice in Who can join
would serve both; it is not built (the lobby menus are feat/live-lobby's).

**What each version brings** (the spec's sections in brackets):

| | What | Here |
|---|---|---|
| 30 (2) | an elevator's riders carried by the movement of the same tick, after its nodes are recomputed, on every machine | `machine_elevator_carry_begin/end` around a machine's update in `object_update` (objects.c, device_machines.c); the Xbox carry in `machine_update` skipped in a game of 32 |
| 30 (3.2) | the host takes a riding client player's prediction across, not up (riding: a machine elevator that moves, or under the unit at rest) | `distributed_take_prediction`, `distributed_riding_elevator` |
| 30 (3.3) | a client's own player on the same elevator as at the echoed tick: a height error within ±4 units (and within 3 across) left | `distributed_correct_own_unit`, the elevator recorded with each own position |
| 30 (4) | input byte 2: the player declines co-op friendly fire | **not implemented**: the byte is never read and ours always sends 0 (valid). On a Vita's co-op host for PC (experimental) a PC player who turned friendly fire off can still be hurt by others |
| 31 (6.1) | datagrams 15 s (450 ticks) from the sender's latest dropped | `DISTRIBUTED_TIME_WINDOW_TICKS_V31`; 600 elsewhere |
| 31 (6.3, 6.4) | the host answers a client's datagrams at their source port; a LAN search from another port answered there | the datagram queue keeps the port (network_connection.c); `network_game_server_note_datagram_source` / `_datagram_destination`; the search answer at the searcher's port (at most 8 an interval) |
| 31 (5, 6.2) | keep-alive's zero bytes; ready, identity and hits only over the stream | already so |
| 32 (7) | kind 90, the host's mouse aim assist policy | client: taken and logged (no mouse with aim assist here: the desktop mouse never gets it, the Vita's gyro is no mouse), blocked until it comes; host: sent at each ready, as the game starts and on change, network.block_mouse_aim_assist (default false: allowed; the settings panel's Vita and PC page has it as PC mouse aim assist, Their choice or Off, since Oct 20 2026) |
| (9.1) | OpenCE browsers' ping sessions | a Vita's crossplay host whose 24 tunnels are full lets one that has carried no game packet for 15 s make way for a proven new player (never on an unproven request) |
| (9.2) | listings within an hour of the browser's clock | the host stamps `time(NULL)` (UTC on Linux; the Vita's not checked: hardware) |
| (9.4) | a host deleting a flag or ball | our client deleted one without halting only in objects.c; weapons.c's own check halted a debug client at every OpenCE 32 CTF join (it removes its own flags then). Now skipped on a distributed client, the flag's goal markers cleared, logged |

**Found by the tests:** the weapons.c flag assert above; a fuzzed input's
control flag bit 15 reaching `unit_control` (a debug host's halt; release
builds tested nothing): every peer's buttons are now held to the unit's 15
flags (player_queues_new.c); OpenCE 6ec42856 itself faults (segfault, eip 0)
once a network test's game is over, its own host and clients alike, its own
control games too: crossplay_check notes that for the PC copies the harness
names (`XPPC_POSTGAME_FAULT_KINDS`, set for that commit) instead of failing
stability, never for ours; its host logs only the start of its invite link,
so `opencehost`'s invite join is not tested against it; OpenCE-to-OpenCE over
the internet stalled once in three runs after its browser's ping session
("reached already", then nothing: `pcself`), not ours.

**Tests** (OpenCE 6ec42856 of 32, ChupathingyCE 7d6d8f24 and OpenCE 8102e711
of 29, OpenCE b1ef7f7e and ChupathingyCE 35fc7e8b of 28; our harness build
`--linux-net-vita --linux-d3d gxm-null`, the fuzz one with ASan + UBSan and
`HALO_NET_FUZZ_TWIN`):

| Test | Result |
|---|---|
| `net32` pol-opence-block / -allow | PASS: our client took blocked (3 copies) and allowed from OpenCE hosts, played every step |
| `net32` pol-ours-opence | PASS: our host sent the policy at 3 readies, to every client as the game started (allowed) and at the change (blocked, `HALO_TEST_AIM_ASSIST_TOGGLE`); on the wire (a sniffer on the host's LAN port) 12-byte kind 90, count 1, reserved 0: block 0 four times, block 1 twice; the OpenCE clients played every step |
| `net32` pol-ours-ours | PASS: allowed then blocked taken; the forged copies (in a datagram, 13 bytes, count 2, value 2, reserved 1: `HALO_TEST_AIM_ASSIST_FORGE`) each refused 5 times, every step played |
| `net32` port-ours-opence | PASS: OpenCE clients with the client port held (another program on 5151) found our host (the search answered at their port), were answered at the ports their datagrams came from and played every step |
| `net32` ctfdel-opence-ours | PASS: an OpenCE CTF host's `object_destroy_all`: our copies deleted the flags on the host's word (at joining and at the command) with no assertion, and played on |
| `run_elevator_test.sh` (T4, our build alone, c10's lift) | PASS: the same tick's carry kept the rider on the platform all 1119 moving ticks (four legs and a reversal), its height over the elevator node constant (spread 0.0000); the Xbox carry for comparison: 0.49 spread, 97 ticks airborne, lost at the reversal |
| `net29` | PASS: our client on an OpenCE 29 host, our host announcing 29 (test build) with OpenCE 29 clients, both by 29's rules, every step |
| `pcversion` | PASS: OpenCE of 28 and of 29 do not join our 32 host; a host of 33 (our stand-in, `HALO_TEST_ANNOUNCE_VERSION`) refused as newer on a LAN, its listing dropped online |
| `opencelan`, `opencehost` (browser), `crossmixed`, `crossrefuse`, `pcjoinsvita`, `browserboth`, `crossold`, `chupahost`, `crosscoop` | PASS (OpenCE of 32; ChupathingyCE's games of 29, with an OpenCE 29 control) |
| `crossmaps` (pcgulch, Custom Edition resources) | PASS |
| `crossfuzzhost` | PASS: 2048 and 1024 twins (now also kind 90 and ticks moved to the windows' edges), no sanitizer memory error, no crash; its first runs found the control-flag halt above |
| `crosscoop` b30 on an OpenCE 32 host | PASS, BSP switch followed (124-146 s on it); the particle assertion seen at a BSP switch with 29 builds did not appear in these three games |
| `crossctf` | ChupathingyCE PASS; OpenCE 32: played to its end, the scoreboards alike, but the kills step fails: its CTF game lasts seconds and both its deaths fell while no client of ours was in it (before our join, after the scheduled leave). The control passed |
| `pcself` | PASS in one run; one stall in OpenCE-to-OpenCE internet play (above) |
| unit: network_wire_test (four builds), p2p_lobby, p2p_friends, lang, crossplay_guard, vita_settings, network fuzz targets' kept cases; wire contract against 8102e711 | PASS |
| Vita release build (`--lto off --pgo off --portable --release`, `ninja vita`) | compiles (eboot.bin, halo.vpk) |

**The iOS port and current OpenCE PCs (Oct 20 2026, insider.18).** SnailBBQ's
Halo-CE-IOS is OpenCE build-244 at network version 32, the same as OpenCE
6ec42856 (build-244) on PC, so it is joined and joins as that build is: a
Vita's client joins its games (PC GAMES), and it joins a Vita's game hosted
for Everyone (which announces 32). Whether a listing says its host is an iOS
device was looked at from the outside only: OpenCE 6ec42856's listings, as
its binary published them to the harness's broker
(`MQTT_TEST_BROKER_DUMP=lobby/s/`), carry version 32, OpenCE's flags, the
game's name, map and gametype, and an 8-byte stamp of zeros; nothing in them
names a platform. No iOS listing could be seen offline, so the browsers tag
an iOS host's game as any OpenCE PC's (no OPENCE (iOS) tag). If the iOS
port's listings ever carry a mark, the stamp's first byte is where ours are
(10.10), read only for a Vita's game.

**Not verified here:** T5/T6 (a lift in a network game against OpenCE, the ±4
bound injected): no stock multiplayer map has an elevator machine, and a
campaign lift needs scripted riders on both machines; the carry itself is
T4's, the netcode rules are by the spec. T8's exact 449/451 boundary (the
window is a constant; the twins moved ticks around both windows' edges with
no fault). T7 (friendly fire: not implemented). T13 (ten minutes of browser
probes) and T14 (listing clock under faketime: not installed). The kind-90
start-of-game broadcast reaching a client still loading (spec 12.1): the
copy sent at its ready is what each client got. Hardware: the Vita's clock
(9.2) and a game against an OpenCE 32 host.


### 10.10 This port's PC build and three-way Who can join (Oct 20 2026)

The PC build (Linux, Windows: `--linux-net-vita`, `tools/package_pc.py`)
speaks the legacy Vita wire, so before this its games looked like a Vita's
everywhere. It now says what it is, in fields every older build ignores, so
no network version is raised (`network_wire.h`):

| Where | How | Older builds |
|---|---|---|
| Online listing (format 2, and a Vita's format 1) | the stamp's first byte (signed with the rest): 0x01 the host is the PC build, 0x02 the game takes Vitas only | read past the stamp (ours since beta.1, OpenCE's and ChupathingyCE's alike) |
| System Link advertisement, legacy wire | `reserved[2]` bits 0x08 (PC build host) and 0x10 (Vitas only) | read 0x01, 0x02, 0x04 only |
| ... crossplay wire (a Vita's game) | bit 0x80 (PC build host), beside 0x40 (4.1 reserves both for us) | PC builds test their own bits |
| Join request | the machine name's last character, 0x5600 + platform + 1 | cleared by `player_name_clean` before use |
| Friends' presence, invites, party follow | the game-flags byte's 0x08 (sender is the PC build), 0x10 (its game's host is), 0x20 (Vitas only) | read 0x01, 0x02, 0x04 only |

A build that says nothing is taken for a Vita. Browsers tag the PC build's
games **VITA (PC)** (`crossplay_guard_host_tag`), group and filter them
with the PCs' (Browser shows PC games: OpenCE's and the PC build's), and
friends and party lists put "(PC)" after its players.

**Who can join** has three choices (the owner): **Vita** (Vitas only, a
Vita's default), **Vita PC** (Vitas and the PC build, on the legacy wire;
the PC build's default, and the least it offers: a PC host is no Vita, so
its menus have only Vita PC and Everyone, `strings_pc`, and a stored "vita"
reads as Vita PC; the dedicated server's default too), **Everyone** (also
OpenCE's PC players, the crossplay wire). An older settings.txt's
`HALO_NET_HOST_CROSSPLAY=false/true` loads as Vita / Everyone, and
`network.host_crossplay = true` in config.toml as Everyone.

A host for Vita refuses a joiner whose join request says it is the PC build
(rejection code `_rejection_code_vitas_only`; the joiner is told "This game
is for PS Vitas only"; an older client shows its generic refusal), and its
listing and advertisement say Vitas only, which the PC build's browser marks
**VITA VITAS ONLY**. **This is best effort, not a security line:** every mark
is what the other machine says, and a modified PC build can say it is a
Vita. Nothing else relies on it: the host's validation of what any client
sends is the same for a Vita and a PC.

| Test | Result |
|---|---|
| unit: network_wire_test (five builds: platform, marks, audiences, refusal, the retired setting), crossplay_guard_test, p2p_lobby_test (the stamp), p2p_friends_test (the presence bits), vita_settings_test (the row, its migration), lang | PASS |
| `run_netns_online_test.sh vitapc`: a PC build's public game and a Vita's; a Vita's browser, and an older build's (HALO_TEST_VITA_OLD: 717e788e4) | PASS: rows `[VITA (PC)] "PcBuildHost"`, `[VITA] "VitaHost"`; the Vita joins the PC build's game; the older build lists both as `[VITA]` and joins it too |
| `run_netns_online_test.sh vitaonly`: a Vita host for Vita refuses the PC build, takes a Vita; for Vita PC takes both | PASS: the PC build's row `[VITA VITAS ONLY] "VitaOnly"`, refused with `_rejection_code_vitas_only` and told "This game is for PS Vitas only"; the Vita in; for Vita PC both in |
| `run_menus_test.sh h3vitapc h3createwho`: the tags and groups; WHO CAN JOIN's choices on a Vita and on the PC build | PASS (and h3create, h3mp, h3browser, h3lan, h3groups, h3oddrows, browserrows as before) |
| `run_netns_online_test.sh code lan lobby`, `run_netns_crossplay_test.sh browserboth` | code, lan, lobby PASS; browserboth's Vita rows as before (`[VITA]`, `[VITA+PC]`, online and on the LAN), its OpenCE half not run: the OpenCE builds at hand announce 26 and 32, which the crossplay wire refuses |

### 10.11 Network version 38 joined (Oct 24 2026)

Branch fix/browser-pc-versions. On Oct 24 the public brokers held 74-87
hosts our browser dropped: v38 about 40-50 (ChupathingyCE's [C] servers,
which announce 38 by a local legacy table, and OpenCE's current builds:
7fb941b0 defines 38), v26 about 15 (DamnationCE, old OpenCE), and v11 and
v21-v36 one-offs (". SWAT pistols" lists itself under 14 versions). The PC
tab showed 5-6 games. Our client now joins 29, 32 and 38 (not 33-37, which
no release announces); our hosts still announce 32.

What 33-38 change, as ChupathingyCE (GPL, 2d2ad1d8 and 251bba5c; its
port/linux/include/delta.h DELTA_LEGACY_VERSIONS rows 33-38 and
port/linux/game/network_rules.h) follows OpenCE's raises; OpenCE's own
source past 4b09168e (AGPL) was not read. The code here is ported from
ChupathingyCE's:

| Version | Change | Here |
|---|---|---|
| 33 | co-op byte bit 1 (players' hits spare their AI allies), the host's rule | taken and dropped (network_game_record.c); bit 2 and up still refused |
| 34 | shield-only scenery and devices are static targets: every machine runs their shields, a client reports no damage one deals, a host's event on a copy made during play finds the nearest within 2 units (+ an area effect's cutoff) | network_damage.c, damage.c (network_wire_v34_rules) |
| 35 | gametype options 0x2C bytes (16 further before the local data), record 13,136 bytes; OpenCE's SWAT, Infection, Gun Game, Hardcore | network_game_record_decode_version: the size by the host's version; a mode past Gun Game, Hardcore past 1 or a non-zero reserved byte refused; a game of one of those types refused with the player told (this build plays none of them) |
| 36 | message 91, the host's shield effects (12 bytes, value 0-5) | taken only from the host, over its stream, exactly as laid out; drawn as stock (network_distributed.c) |
| 37 | a listed game's kind in the listing's stamp and the advertisement's fourth reserved byte | the browser greys a game of an OpenCE game type, saying which |
| 38 | message 91's value 6 (each machine's own look) | accepted, drawn as stock |

Tested (run_netns_crossplay_test.sh, our Vita-path debug build, no tick
thread): opencelan and opencehost against OpenCE 7fb941b0 (38) passed join,
settings, play, spawn, kills, client damage, score, end of game and rejoin
(the host, a hand-linked build, crashed after its game ended, its own
control too); chupaserver against ChupathingyCE d7fafc81's server announcing
38 (XPPC_PC_LEGACY_TABLE) passed every step on a LAN. Unit: network_wire_test,
wire_contract_record (v38_*), net_fuzz variant_options (38's record).

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
