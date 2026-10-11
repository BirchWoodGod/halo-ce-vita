/*
NETWORK_GAME_RECORD.H

The game settings record (struct network_game, network_game_manager.h) as
each wire sends it. In memory this build keeps OpenCE's record (its network
version 26, 13,120 bytes at 128 machines and players): the Xbox game's,
co-op's switches in the byte the Xbox left as padding (cooperative_flags,
OpenCE's version 19), and the gametype's PC options before the local data
(struct game_variant_options, OpenCE's version 11; the PC vehicle set of
its version 24 among them). Two wires carry it (docs/crossplay.md):

- the legacy Vita wire (network version 18, betas 1-4): the record as those
  builds have it, 13,092 bytes, with neither: the padding byte sent as 0,
  the local data where the options begin. A record received on it gets the
  options its variant implies (game_variant_options_default), which are the
  Xbox game's play: what the betas play. What this wire sends is byte for
  byte what the betas send (port/vita/tests/run_wire_contract_test.sh's L1).
- the crossplay wire (OpenCE's): the record as it is. A record received on
  it is a stranger's (a PC host's): its options and co-op switches must be
  ones this game can play by, else the record is refused
  (game_variant_options_valid).

Which wire: network.crossplay (HALO_CROSSPLAY), the one switch
(network_game_record_format_current). The record's own checks (players,
names, map) stay network_game_client_game_settings_updated's.

Kept apart from the game (it needs only its headers) so that a fuzz target
and the record test build it on their own (port/vita/tests/net_fuzz_variant_options.c,
wire_contract_record.c).
*/

#ifndef __NETWORK_GAME_RECORD_H
#define __NETWORK_GAME_RECORD_H

struct network_game;
struct game_variant;
struct game_variant_options;

enum network_game_record_format
{
	_network_game_record_legacy = 0,
	_network_game_record_crossplay,
	NUMBER_OF_NETWORK_GAME_RECORD_FORMATS
};

/* the wire's: crossplay when network.crossplay is on */
short network_game_record_format_current(void);
/* the record's size on the wire: 13,092 or 13,120 bytes */
long network_game_record_size(short format);
/* the record as the wire sends it, into buffer (at least
network_game_record_size bytes); FALSE for a format there is not */
boolean network_game_record_encode(
	short format,
	struct network_game const *game,
	void *buffer,
	long buffer_size);
/* the record a host sent on the wire (size bytes) into game; FALSE, and
why (static text), when it is not one this game can play */
boolean network_game_record_decode(
	short format,
	void const *buffer,
	long size,
	struct network_game *game,
	char const **why);
/* whether co-op's switches are ones this game knows */
boolean network_game_cooperative_flags_valid(
	byte flags);

#endif /* __NETWORK_GAME_RECORD_H */
