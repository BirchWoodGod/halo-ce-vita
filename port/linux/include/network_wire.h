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
friend's presence or party it came from), by a host from network.host_crossplay
as its server is made. It holds for that game: nothing switches under it.
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
means another map to each (crossplay_maps.h). Nor 30 to 32 (OpenCE
6ec42856, Oct 19 2026: docs/crossplay.md 10.7) until the suite passes
against them.
*/

#ifndef __NETWORK_WIRE_H
#define __NETWORK_WIRE_H

/* the crossplay wire's network version: OpenCE's at 8102e711 (build-206
and after), ChupathingyCE's since a501871f (0.7.5a) */
#define HALO_PORT_CROSSPLAY_NETWORK_VERSION 29

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
/* the network version of a wire: HALO_PORT_NETWORK_VERSION (18), or the
crossplay wire's (the one a host announces) */
unsigned int network_wire_version_of(int crossplay);
/* ... of this machine's game's wire */
unsigned int network_wire_version(void);
/* whether a host announcing the version can be joined on that wire:
legacy, exactly 18; crossplay, the accepted list (29) */
int network_wire_version_accepted_on(int crossplay, unsigned int version);
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

/* hosting: whether the next game this machine hosts takes PC players too
(network.host_crossplay, "Who can join": Vitas and PC), on the crossplay
wire; 0 where that wire is not available (above) */
int network_wire_host_crossplay(void);
/* (automated tests only, never in release builds) HALO_TEST_CROSSPLAY_HOST=1:
this machine hosts on the crossplay wire as a stand-in for a PC host (its
advertisement and listing not marked a Vita's: run_crossplay_record_test.sh,
run_crossplay_maps_test.sh) */
int network_wire_host_pc_stand_in(void);

/* the server browsers' filter (network.browser_filter, "Browser shows"):
every game, the Vitas' only, or PC hosts' only. network.crossplay (the
retired switch, Oct 19 2026) set and the filter left at its default shows
PC games only, as the switch did */
enum
{
	NETWORK_WIRE_FILTER_ALL,
	NETWORK_WIRE_FILTER_VITA,
	NETWORK_WIRE_FILTER_PC,
};
int network_wire_browser_filter(void);
/* whether the browsers show a game of a Vita's host (nonzero) or a PC's */
int network_wire_browser_shows(int vita_host);

#endif /* __NETWORK_WIRE_H */
