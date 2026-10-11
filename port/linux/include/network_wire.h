/*
NETWORK_WIRE.H

Which network wire this machine speaks (docs/crossplay.md): one switch,
network.crossplay (HALO_CROSSPLAY; the Vita's Multiplayer tab, Crossplay),
Off as shipped.

- Off: the legacy Vita wire (network version 18), exactly as betas 1-4 speak
  it: Vitas play only Vitas (halo_port_limits.h), hcev signalling (message
  version 3, relays after the addresses), listing format 2, the Vita join
  token, the 13,092-byte settings record, and the betas' simulation.
- On: OpenCE's wire as at the pinned OpenCE 6399ed9d (network version 26):
  hceu signalling (message version 4, a WebRTC block of kind none, no
  relays), listing format 1, OpenCE's advertisement flags (0x02 a game in
  progress), its constant join token, its 13,120-byte record and the
  simulation fixes it made without a version raise. This machine only joins
  then (PC hosts and servers: OpenCE, ChupathingyCE, DamnationCE): it never
  hosts a game others can join (network_wire_hosting_refused), and sends
  none of its own extensions (map share, short codes, relays, the ping
  table) to the PC machines.

Read once, at the network's start, and kept for the run: a change takes a
restart (the Vita's panel says so), so that no game or signalling session
ever sees the wire change under it. Ad hoc play and the dedicated server
are always on the legacy wire (no PC reaches ad hoc; the servers stay off
the crossplay wire).

The versions a crossplay client joins are a list built in (a signed table
widens it later: docs/crossplay.md 5.2, T10): exactly the pinned OpenCE's.
*/

#ifndef __NETWORK_WIRE_H
#define __NETWORK_WIRE_H

/* the crossplay wire's network version: OpenCE's at 6399ed9d */
#define HALO_PORT_CROSSPLAY_NETWORK_VERSION 26

/* nonzero on the crossplay wire */
int network_wire_crossplay(void);
/* the network version this machine's wire speaks: HALO_PORT_NETWORK_VERSION
(18), or HALO_PORT_CROSSPLAY_NETWORK_VERSION */
unsigned int network_wire_version(void);
/* whether a host announcing the version can be joined on this machine's
wire: legacy, exactly 18; crossplay, the accepted list (26) */
int network_wire_version_accepted(unsigned int version);
/* nonzero (and why, static text for the player, in why) when this machine
must not host a game others can join: on the crossplay wire */
int network_wire_hosting_refused(const char **why);

#endif /* __NETWORK_WIRE_H */
