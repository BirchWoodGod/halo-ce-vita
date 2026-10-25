/*
NETWORK_WIRE.H

Which network wire a game speaks (docs/crossplay.md): one wire a game,
chosen per game (the owner, Oct 19 2026: no switch). The server browsers
list the Vitas' games and OpenCE's (OpenCE, ChupathingyCE, DamnationCE hosts
and PC servers) side by side, each tagged, and joining one speaks whichever
wire it plays; a Vita that hosts chooses who can join (network.host_crossplay,
"Who can join": Vitas, or Vitas and PC).

- The legacy Vita wire (network version 18), exactly as betas 1-4 speak it:
  hcev signalling (message version 3, relays after the addresses), listing
  format 2, the Vita join token, the 13,092-byte settings record, the Vita
  advertisement flag and the betas' simulation. Vita to Vita.
- The crossplay wire: OpenCE's as at the pinned OpenCE 8102e711 (build-206,
  network version 29, as ChupathingyCE 7d6d8f24 (0.7.5b) announces it): hceu
  signalling (message version 4, a WebRTC block of kind none, no relays),
  listing format 1, OpenCE's advertisement flags (0x02 a game in progress),
  its constant join token, its 13,120-byte record and the simulation fixes it
  made without a version raise. PC hosts and servers, and a Vita's game hosted
  for Vitas and PC (its advertisement and listing marked as a Vita's:
  NETWORK_WIRE_ADVERTISED_VITA_HOST_FLAG). None of this build's extensions
  (map share, relays, the ping table, friends' cards on the tunnel) go to its
  machines.

The game's wire is chosen as the game begins: by a client from the game it
joins (its advertisement's version and flags, or the listing, code record,
friend's presence or party it came from), by a host from network.who_can_join
(everyone: the crossplay wire) as its server is made. It holds for that game: nothing switches under it.
With no game it is the legacy wire. Ad hoc play and the dedicated server are
always on the legacy wire (no PC reaches ad hoc; the servers stay off the
crossplay wire), and so is a build that does not play as a Vita.

The versions a crossplay client joins, and the one a crossplay host
announces, are a list built in (a signed table widens it later:
docs/crossplay.md 5.2, T10): exactly the pinned OpenCE's. Not 26 or 27
beside it: 28 plays a script's switch_bsp, the flashlight and dynamic lights
and a second weapon picked up while firing otherwise than they do, with no
message changed to tell, so a client of one with a host of the other plays
on wrong rather than being refused (docs/crossplay.md 5.1). Not 28 either:
29 names a Custom Edition map and says which version it is otherwise (the
map's own name, and its hash's first 32 bits as the record's map version,
where 28 sent a hashed name and the header checksum), so the same record
means another map to each (crossplay_maps.h).

Since Oct 19 2026 (docs/crossplay.md 10.9, docs/opence-net32-spec.md) the
crossplay wire has two sets of rules, chosen per game by the version the
game's host announces: 29 (ChupathingyCE, DamnationCE when it moves, OpenCE
build-206) and 32 (OpenCE 6ec42856 and after). 32 carries an elevator's
riders by the same tick's movement on every machine and corrects a rider
otherwise (30), drops datagrams 15 s from their sender's latest tick and
answers a client's datagrams at their source port (31), and has the host
send its mouse aim assist policy (32: kind 90). A client joins exactly those
two and plays the host's; a host for everyone (Who can join: EVERYONE, a
Vita's or the PC build's) announces 32, OpenCE's current. Not 30 or 31: no released build announces them, and each
plays part of 32's rules alone, with nothing in a message to tell. One game
plays one version's rules from its start to its end.
*/

#ifndef __NETWORK_WIRE_H
#define __NETWORK_WIRE_H

/* the crossplay wire's network versions: the one a host for everyone
(network_wire_host_audience) announces, OpenCE's since 31ba6b37 (6ec42856, Oct 19 2026); and the
older one its clients still join, OpenCE's at 8102e711 (build-206),
ChupathingyCE's since a501871f (0.7.5a) */
#define HALO_PORT_CROSSPLAY_NETWORK_VERSION 32
#define HALO_PORT_CROSSPLAY_OLDER_NETWORK_VERSION 29
/* ... and the newest one its clients join, OpenCE's and ChupathingyCE's
servers' since Oct 23 2026 (network_wire_v33_rules ...) */
#define HALO_PORT_CROSSPLAY_NEWEST_NETWORK_VERSION 38

/* a Vita's game on the crossplay wire says it is a Vita's: this bit in its
advertisement's flags (HALO_PORT_ADVERTISED_FLAGS_OFFSET; OpenCE's 0x01 and
0x02 are the port's, ChupathingyCE's 0x04 its Delta; docs/crossplay.md 4.1
reserves 0x40 and 0x80 for ours), and _listing_vita_host in its listing's
(p2p_lobby.c). PC builds ignore both */
#define NETWORK_WIRE_ADVERTISED_VITA_HOST_FLAG 0x40

/* nonzero when the game this machine hosts or joins (the one being joined
too) is on the crossplay wire; 0 with none */
int network_wire_crossplay(void);
/* the wire of the game this machine is about to host or join, and what
hosts it (crossplay_guard.h's CROSSPLAY_PLATFORM_*: 0 a Vita - a legacy
game's always; a crossplay one's when its advertisement or listing says so,
or this machine hosts it - else the PC build its advertisement tells); why,
for the log. Ad hoc play, the dedicated server and builds that do not play
as a Vita stay on the legacy wire whatever is asked */
void network_wire_choose(int crossplay, int platform, const char *why);
/* ... a client's, by the network version the game's host announces: the
legacy wire's (18), or one the crossplay wire joins, whose rules the game
then plays (network_wire_v30_rules ...); any other is the legacy wire (never
a version this build does not play: the join is refused before this) */
void network_wire_choose_version(unsigned int version, int platform, const char *why);
/* whether the host of this machine's game is a Vita (see above; a legacy
game's always), and what it is (CROSSPLAY_PLATFORM_*) */
int network_wire_host_vita(void);
int network_wire_host_platform(void);
/* back to the legacy wire, with no game (the connection switched live, at
the menus with no network game: network_connection.c) */
void network_wire_reset(void);
/* whether this build speaks the crossplay wire at all: one that plays as a
Vita, not the dedicated server, not in ad hoc play */
int network_wire_crossplay_available(void);
/* the network version of a wire as this machine hosts it:
HALO_PORT_NETWORK_VERSION (18), or the one a crossplay host announces
(network_wire_host_version) */
unsigned int network_wire_version_of(int crossplay);
/* the version this machine's advertisement and listing say for a game it
hosts on that wire: network_wire_version_of's; in test builds alone
HALO_TEST_ANNOUNCE_VERSION=n says n on the crossplay wire whatever it plays
(a stand-in for a PC host of a version no build has yet: pcversion) */
unsigned int network_wire_announced_version(int crossplay);
/* the version a host for everyone announces and plays:
HALO_PORT_CROSSPLAY_NETWORK_VERSION (32); in test builds alone
HALO_TEST_CROSSPLAY_HOST_VERSION may name another the wire joins (29) */
unsigned int network_wire_host_version(void);
/* ... of this machine's game: 18 on the legacy wire, the game's crossplay
version (the host's, joined or hosted) on the crossplay wire */
unsigned int network_wire_version(void);
/* whether a host announcing the version can be joined on that wire:
legacy, exactly 18; crossplay, the accepted list (29, 32 and 38) */
int network_wire_version_accepted_on(int crossplay, unsigned int version);
/* the crossplay versions joined, for a log line or a message ("29, 32 and 38") */
void network_wire_crossplay_versions_text(char *text, int size);
/* the crossplay version a player should look for instead of a host's
version not joined: the lowest joined above it, else the highest joined
(an older host: 26-28 -> 29, 30-31 -> 32; a newer one: 32) */
unsigned int network_wire_crossplay_version_near(unsigned int version);
/* ... on this machine's game's wire */
int network_wire_version_accepted(unsigned int version);
/* which wire a host announcing this version plays (its listing or
advertisement): 0 the legacy wire, 1 the crossplay wire, -1 neither (a
version this build joins on no wire: refused, the player told which) */
int network_wire_of_version(unsigned int version);
/* nonzero when this machine plays network version 28's rules (OpenCE
5a7c655a, build-201; ChupathingyCE 6f5064f6), which 29 keeps: the
crossplay wire, whose hosts are all of 29 (network_wire_version_accepted).
They change no message, so every machine of a game must agree on them: a
script's switch_bsp keeps a multiplayer game's first BSP (hs.c), dynamic
lights and the flashlight stay on past the Xbox's player and vehicle limits
(game_engine.c), a second weapon picked up while firing is readied
(units.c). The legacy wire plays the betas' (0). */
int network_wire_v28_rules(void);
/* nonzero when this machine's game plays OpenCE's network version 30's
rules, which 32 keeps: a crossplay game of 32 (docs/opence-net32-spec.md 2-4).
An elevator's riders carried by the movement of the same tick, after its
nodes are recomputed (objects.c, device_machines.c: every machine must
agree), a rider's height left to its client (the host takes a riding
client player's prediction across, not up: network_distributed.c) and a
client's own player on an elevator left within 4 units up or down. 0 on the
legacy wire and in a crossplay game of 29: the Xbox's carry, a tick late */
int network_wire_v30_rules(void);
/* ... version 31's (spec 5-6): a datagram 15 s (450 ticks) from its
sender's latest tick dropped (20 s otherwise), and the host's datagrams to a
client sent to the address and port its datagrams come from */
int network_wire_v31_rules(void);
/* ... version 32's (spec 7): the host's mouse aim assist policy (distributed
kind 90) sent by a host, taken by a client */
int network_wire_v32_rules(void);
/* ... versions 33 to 36's, which 38 keeps (a crossplay game of 38; Oct 24
2026, ported from ChupathingyCE 2d2ad1d8 and 251bba5c, GPL, which follow
OpenCE's raises; its port/linux/include/delta.h DELTA_LEGACY_VERSIONS rows
33-38 and port/linux/game/network_rules.h say what each changed):
- 33: co-op's second switch (bit 1 of the settings' co-op byte: the
  players' hits spare their AI allies), the host's rule alone: a client
  takes it and does nothing with it;
- 34: scenery and devices that are a shield alone are static targets too:
  every machine runs their shields, a client reports no damage one of them
  deals (a tripmine's going off: the host deals it), and a host's event on
  a copy of one made during play finds the nearest here (network_damage.c);
- 35: the gametype's options 0x2C bytes, the settings record 16 bytes
  longer (13,136: network_game_record.c), OpenCE's own game types (SWAT,
  Infection, Gun Game, Hardcore) in them: a game of one is refused, as this
  build does not play them;
- 36: the host's shield effects, a new message (kind 91), taken checked and
  drawn as stock (network_distributed.c);
- 37: a listed game's kind in a byte that was zero (the listing's stamp, the
  advertisement's fourth reserved byte): read for the browser alone;
- 38: the shield effects' seventh value (6). */
int network_wire_v33_rules(void);
int network_wire_v34_rules(void);
int network_wire_v35_rules(void);
int network_wire_v36_rules(void);

/* hosting: whether the next game this machine hosts takes OpenCE's PC
players too (network_wire_host_audience: everyone), on the crossplay wire;
0 where that wire is not available (above) */
int network_wire_host_crossplay(void);
/* (automated tests only, never in release builds) HALO_TEST_CROSSPLAY_HOST=1:
this machine hosts on the crossplay wire as a stand-in for a PC host (its
advertisement and listing not marked a Vita's: run_crossplay_record_test.sh,
run_crossplay_maps_test.sh) */
int network_wire_host_pc_stand_in(void);

/* ---------- this project's platforms (docs/crossplay.md 4.3)

A build that plays as a Vita is a Vita (HALO_VITA) or the project's PC build
(Linux, Windows: HALO_NET_AS_VITA, tools/package_pc.py), which speaks the
Vita wire and plays with the Vitas. The PC build says so where older builds
ignore it, with no network version raised: the listing's stamp
(NETWORK_WIRE_STAMP_*, p2p_lobby.c), the System Link advertisement's flags
(NETWORK_WIRE_ADVERTISED_*), the join request (the last character of its
machine name, NETWORK_WIRE_JOIN_PLATFORM_MARK) and a friend's presence
(p2p_friends.c). The browsers then tag its games VITA (PC). A build that
says nothing is taken for a Vita, as before. All of it is what the other
machine says: best effort (a modified client can lie), never a security
line. The dedicated server is a Vita here (its games are tagged SERVER). */
enum
{
	NETWORK_PLATFORM_VITA,
	NETWORK_PLATFORM_VITA_PC,
};
/* this build's: a Vita, or the PC build. (automated tests only, never in
release builds) HALO_TEST_PLATFORM=vita or pc makes a Linux build stand in
for either */
int network_wire_this_platform(void);

/* the listing's stamp (its first byte; older builds read none of it) */
#define NETWORK_WIRE_STAMP_PC_HOST 0x01
#define NETWORK_WIRE_STAMP_VITAS_ONLY 0x02
/* the System Link advertisement's flags on the legacy wire (bits older
builds ignore; HALO_PORT_ADVERTISED_* has 0x01, 0x02 and 0x04): the host is
the PC build; its game takes Vitas only */
#define NETWORK_WIRE_ADVERTISED_PC_HOST_FLAG 0x08
#define NETWORK_WIRE_ADVERTISED_VITAS_ONLY_FLAG 0x10
/* ... on the crossplay wire, past NETWORK_WIRE_ADVERTISED_VITA_HOST_FLAG
(docs/crossplay.md 4.1 reserves 0x40 and 0x80 for ours): the Vita host is
the PC build */
#define NETWORK_WIRE_ADVERTISED_CROSSPLAY_PC_HOST_FLAG 0x80
/* the join request's mark: its machine name's last character (a name of
at most 31; older hosts clear it, player_name_clean) is this with the
joiner's NETWORK_PLATFORM_* + 1 in its low byte */
#define NETWORK_WIRE_JOIN_PLATFORM_MARK 0x5600
#define NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN (-1)
/* the mark for this build (put in the name's last character), and the
platform a mark tells (NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN: none, an older
build's, which is taken for a Vita) */
unsigned short network_wire_join_platform_mark(void);
int network_wire_join_platform_of_mark(unsigned short mark);

/* ---------- who can join (network.who_can_join, "Who can join"; the owner,
Oct 20 2026) */
enum
{
	/* VITA, Vitas only (the Vita's default): the legacy wire; the PC build is
	refused at the join ("This game is for PS Vitas only") */
	NETWORK_AUDIENCE_VITAS,
	/* VITA PC, PS Vitas and Halo CE Vita on PC (the PC build's default
	and least: a PC
	host cannot host for Vitas only; the dedicated server's): the legacy
	wire, the PC build joins */
	NETWORK_AUDIENCE_VITA_PC,
	/* EVERYONE: also OpenCE players on PC, on the crossplay wire (what
	"Vitas and PC" was) */
	NETWORK_AUDIENCE_EVERYONE,
	NUMBER_OF_NETWORK_AUDIENCES,
};
/* the setting's values (network.who_can_join, HALO_NET_WHO_CAN_JOIN):
"vita", "vita_pc", "everyone" (the menus' VITA, VITA PC, EVERYONE; the
PC build offers no VITA: its menus' spinners strings_pc, the Vita's
panel alone has all three) */
const char *network_wire_audience_value(int audience);
/* a value read: NETWORK_AUDIENCE_*, or -1 for none. The retired
network.host_crossplay's (HALO_NET_HOST_CROSSPLAY) "true" and "1" read as
everyone, "false" and "0" as Vitas only */
int network_wire_audience_parse(const char *value);
/* who can join the next game this machine hosts: the setting (else the
retired one's, else this build's default), what this build can host (the
PC build: not Vitas only; no crossplay wire, as in ad hoc play: not
everyone) */
int network_wire_host_audience(void);
/* the default: Vitas only on a Vita, Vitas and Halo CE Vita PC on the PC
build and the dedicated server */
int network_wire_audience_default(void);
/* the audiences this build offers when it hosts: 3 on a Vita (first
NETWORK_AUDIENCE_VITAS), 2 on the PC build (from NETWORK_AUDIENCE_VITA_PC) */
int network_wire_audience_first(void);
/* whether the game this machine hosts refuses a joiner of this platform
(NETWORK_PLATFORM_*, or NETWORK_WIRE_JOIN_PLATFORM_UNKNOWN: an older
build's, taken for a Vita): the PC build, by a host for Vitas only, on the
legacy wire */
int network_wire_host_refuses(int platform);

/* the server browsers' filter (network.browser_filter, "Browser shows"):
every game, the Vitas' only, or PC hosts' only (OpenCE's, and the PC
build's: VITA (PC)). network.crossplay (the retired switch, Oct 19 2026) set
and the filter left at its default shows PC games only, as the switch did */
enum
{
	NETWORK_WIRE_FILTER_ALL,
	NETWORK_WIRE_FILTER_VITA,
	NETWORK_WIRE_FILTER_PC,
};
int network_wire_browser_filter(void);
/* whether the browsers show a game of a Vita's host (nonzero: a Vita, not
the PC build) or a PC's */
int network_wire_browser_shows(int vita_host);

#endif /* __NETWORK_WIRE_H */
