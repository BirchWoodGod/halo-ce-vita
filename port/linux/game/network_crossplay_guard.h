/*
NETWORK_CROSSPLAY_GUARD.H

The game client's side of the crossplay guardrails (docs/crossplay.md 6,
T11; the rules are port/linux/src/crossplay_guard.h's). In a PC host's game
on the crossplay wire (network_wire.h; a Vita's game on that wire is none of
this but the split screen rule):

- a PC co-op game (no game engine, or a campaign level) and a large PC
  game (more than 16 players in it, or a maximum above 16) are joined,
  Experimental, and the player asked once whether to play it anyway: in
  the server browser before the join, else in the game's lobby when its
  settings record arrives; "no" leaves it (co-op since Oct 11 2026: its
  indices all fit this build, docs/crossplay.md 6.2);
- split screen (PS TV): one local player in a crossplay game; another is
  not added, and told why.

Nothing here applies on the legacy wire, nor to a game this machine's own
server runs (split screen, bots).
*/

#ifndef __NETWORK_CROSSPLAY_GUARD_H
#define __NETWORK_CROSSPLAY_GUARD_H

struct network_game;

/* a PC host's LAN advertisement, of a version this machine joins: FALSE
refuses it (the player told, if tell). Nothing is refused by it now (a
co-op or large game is logged, and asked about in its lobby); kept as the
place a refusal would go */
boolean network_crossplay_guard_advertisement(
	wchar_t const *game_name,
	char const *map_name,
	long map_name_size,
	short engine_type,
	long players,
	long maximum,
	boolean tell);
/* whether a game on this map (map_name_size bytes at most) with this game
engine is co-op: a campaign level whatever it says, or no game engine (as
OpenCE lists and hosts co-op) on a level that is not one of the Xbox's
multiplayer ones */
boolean network_crossplay_guard_coop_game(
	char const *map_name,
	long map_name_size,
	long engine_type);
/* whether the player said no to this large PC game a moment ago (the
automatic joins, network_game_client_join_first_available_game, leave it
alone: said once, if tell) */
boolean network_crossplay_guard_declined(
	wchar_t const *game_name,
	boolean tell);
/* the host's settings record as the client takes it (client: whose
game): FALSE refuses it, the player told, and the game is to be left */
boolean network_crossplay_guard_record(
	void const *client,
	struct network_game const *game);
/* each frame of the client: FALSE when the player said no to the large
game it is in (the game is to be left) */
boolean network_crossplay_guard_update(
	void const *client);
/* the client reset (a new search or join): its game's answers forgotten
(the browser's stay theirs: crossplay_guard.h) */
void network_crossplay_guard_reset(
	void);
/* whether the local player (controller local_player_index) may be added
to the game (machine_index: this machine's; requested: the controllers
whose players were asked for, requested_count of them): FALSE, told once */
boolean network_crossplay_guard_local_player(
	struct network_game const *game,
	short machine_index,
	short local_player_index,
	boolean const *requested,
	short requested_count);

#endif /* __NETWORK_CROSSPLAY_GUARD_H */
