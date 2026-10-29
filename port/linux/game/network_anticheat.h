/*
NETWORK_ANTICHEAT.H

The host's answer to a modified client (port/linux/NETCODE.md, "A modified
client"): what the distributed netcode's checks refuse of a client machine
is counted, weighed by how surely only a modified client sends it, and a
machine whose count runs past the host's limit is dropped and kept out of
its games for a while; the host's own KICK and BAN (the PLAYERS screen, the
settings panel's Players page, the console) go through here too.
*/

#ifndef __NETWORK_ANTICHEAT_H
#define __NETWORK_ANTICHEAT_H
#pragma once

/* ---------- constants */

/* what a client machine sent that the host refused (network_anticheat.c
weighs each: network.anticheat_weights) */
enum
{
	/* a hit report no honest client makes: of another machine's player,
	numbers that are not numbers, damage harder than its weapon deals, a
	part its target does not have, a shape the game never gives it */
	_anticheat_hit_forged = 0,
	/* ... damage of a weapon the player has not held in ten seconds */
	_anticheat_hit_weapon,
	/* ... the point it hit behind a wall from everywhere the shooter was
	(network_damage.c's line of sight) */
	_anticheat_hit_wall,
	/* ... fired from a weapon the host has had empty for a while (infinite
	ammunition, no reload) */
	_anticheat_hit_empty,
	/* ... a part of the target far from where the shot hit (a headshot
	claimed of a shot at the legs) */
	_anticheat_hit_part,
	/* ... refused for what a client on a poor link can send too: out of
	reach of where the host had the shooter, the target far from where the
	host had it, too old, a grenade's throw the host did not see */
	_anticheat_hit_refused,
	/* ... more than the weapon fires */
	_anticheat_hit_rate,
	/* a player's own moves refused for many seconds on end (a teleport, a
	speed hack, flying: the host's copy stays where it was) */
	_anticheat_move,
	/* ... through the map's walls or floor */
	_anticheat_move_wall,
	/* a player's vehicle's moves refused for many seconds on end */
	_anticheat_vehicle_move,
	/* network Forge's requests malformed or replayed */
	_anticheat_forge,
	NUMBER_OF_ANTICHEAT_KINDS
};

/* the host's own: kick (may join again at once), ban for the session (kept
out of this host's games while it runs) and ban (and in bans.txt) */
enum
{
	_anticheat_drop_kick = 0,
	_anticheat_drop_session_ban,
	_anticheat_drop_ban,
};

/* ---------- prototypes */

/* (the host) a client machine's refused message, weighed (amount: how many
of it, 1 for one); verified: it came over the machine's stream, or a
datagram internet play's tunnel authenticated (a LAN's datagram, which any
machine on it can send as from another's address, is counted and logged
but never drops a machine) */
void network_anticheat_violation(long machine_index, short kind, boolean verified, real amount);
/* ... whether a machine's datagrams are its own (internet play's tunnel
seals each to its peer) */
boolean network_anticheat_datagram_verified(long machine_index);
/* (the host) after each tick */
void network_anticheat_tick(void);
/* a game's start: the counts begin again (the session's bans stay) */
void network_anticheat_new_game(void);
/* (network_server_manager.c) whether a machine joining is kept out: dropped
by the host's limit or banned for the session within the time it stays out
(its real address, an internet play peer's endpoint, or its hardware id) */
boolean network_anticheat_refused(unsigned long address, char const *hardware_id);
/* (network_server_manager.c) one person's address, for the joins' limits:
an internet play peer's real address (its endpoint, which a new run of its
game - a new identity, a new stand-in - keeps), any other address as it is
(security review 1.1.1-beta.1 F-11) */
unsigned long network_anticheat_person_address(unsigned long address);
/* the players the machines of one person's address may have in a game of
maximum_players: half of them, a machine's four at least */
long network_anticheat_address_player_limit(long maximum_players);
/* whether a machine of an address may have `adding` players more: the
game's remote machines given (count of them: their addresses, as connected,
and their players, a machine with none counting as one: its first player
comes once it is in), the machine itself not among them; adding: its own
players with the one it asks for */
boolean network_anticheat_address_room(unsigned long address, unsigned long const *addresses, long const *players,
	long count, long maximum_players, long adding);
/* (the host's KICK and BAN) a client machine dropped (_anticheat_drop_*):
every machine told, the action logged; FALSE if it is not a client in the
game */
boolean network_anticheat_drop(long machine_index, short how, char const *why);
/* the session's bans let go (the settings panel's Clear session bans) */
void network_anticheat_clear_session_bans(void);
short network_anticheat_session_ban_count(void);
/* what was counted this game, for the tests and the log: the violations of
each kind, the machines dropped by the limit */
void network_anticheat_statistics(long *counts, long *kicks);
/* whether the host drops machines by the limit (network.anticheat) */
boolean network_anticheat_enabled(void);

#endif
