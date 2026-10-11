/*
CROSSPLAY_GUARD.H

The client's guardrails on the crossplay wire (docs/crossplay.md 6, task
T11): what a Vita with Crossplay on (network_wire.h) makes of a PC game
(OpenCE, ChupathingyCE, DamnationCE) before it joins, from the server
browser's listing or a LAN advertisement, and again on the host's settings
record. Pure: no game, no network; the browsers (menu_functions.c, the
Vita's settings panel), the network test and the game's client
(port/linux/game/network_crossplay_guard.c) share it.

- A co-op game (no game engine: the campaign) is allowed (the owner, Oct
  11 2026): badged Experimental, and joining it asks once, as a large game
  does. A client runs no AI and no scripts: every index a PC co-op host
  sends is one of this build's capacities, the same as OpenCE's for what a
  client holds (objects 8192, players and machines 128, device groups 1024,
  the AI units it drives 1056 on the crossplay wire: network_actors.c);
  docs/crossplay.md 6.2 (gap G14). A co-op game with more than
  CROSSPLAY_GUARD_COOP_PLAYERS players says so in its question.
- A large game (more than 16 players in it, or a maximum above 16) is
  allowed (the owner, Oct 11 2026): badged Experimental, and joining it
  asks once ("Join anyway?"). Never refused by its player count.
- Split screen (PS TV): one local player in a crossplay game until it is
  measured (docs/crossplay.md 8).

Everything a PC machine says is untrusted: counts are taken as any int,
names as any bytes.
*/

#ifndef __CROSSPLAY_GUARD_H
#define __CROSSPLAY_GUARD_H

/* more players than this (in the game, or as its maximum) make a PC game
large: what a Vita host takes, and what the Vita was measured at */
#define CROSSPLAY_GUARD_LARGE_PLAYERS 16
/* players a Vita expects in a co-op game: its own co-op hosts take 2, 3
and 4 experimental (network.coop_players); a PC co-op game with more says so
when it asks (a PC co-op host takes 16 by default, and extra enemies for
each player past the first) */
#define CROSSPLAY_GUARD_COOP_PLAYERS 4
/* local players a Vita plays in a crossplay game (split screen) */
#define CROSSPLAY_GUARD_LOCAL_PLAYERS 1
/* how long a "yes" to a large or co-op game, or a "no", is remembered for it (the
browser's answer reaching the lobby; the network test's joins again) */
#define CROSSPLAY_GUARD_ANSWER_MILLISECONDS 120000UL

enum crossplay_guard_verdict
{
	CROSSPLAY_GUARD_OK,
	/* joined, after asking once: Experimental */
	CROSSPLAY_GUARD_LARGE,
	/* a co-op game: joined, after asking once, Experimental (whatever its
	players: the host's AI and its extra enemies are simulated here too) */
	CROSSPLAY_GUARD_COOP,
};

/* the verdict on a PC game: co-op (nonzero: no game engine, or a campaign
level), its players and maximum (any values) */
int crossplay_guard_judge(int coop, int players, int maximum);
/* whether those counts make it large */
int crossplay_guard_large(int players, int maximum);
/* whether the game is badged Experimental and asked about once (any
verdict but CROSSPLAY_GUARD_OK) */
int crossplay_guard_experimental(int coop, int players, int maximum);
/* whether a map (a tag path, levels\test\<name>\<name>, or a listing's bare
name) is one of the Xbox's thirteen multiplayer levels: a game on one is
never co-op, whatever its engine says (a PC host between games, or a
network test's, can send no engine for a moment). Case aside; size bounds
the name read */
int crossplay_guard_stock_multiplayer_map(const char *map, int size);

/* the player's answer about the large or co-op game of this name (the listing's,
the advertisement's or the record's: compared on its letters and digits,
case aside, at most 15), at now (milliseconds): remembered for
CROSSPLAY_GUARD_ANSWER_MILLISECONDS. A newer answer replaces it */
void crossplay_guard_answer(const char *name, int yes, unsigned long now);
/* the answer remembered for the game of this name: 1 yes, 0 no, -1 none
(none for an empty name) */
int crossplay_guard_answered(const char *name, unsigned long now);
/* everything remembered forgotten */
void crossplay_guard_forget(void);

/* the answer for automated runs, HALO_CROSSPLAY_LARGE_ANSWER (yes or no):
1, 0, or -1 when it is not set (the player is asked) */
int crossplay_guard_test_answer(void);

/* the browser's badge for a game's host (short, translated): on the
crossplay wire "PC" or "PC SERVER" (a dedicated server's), on the legacy
wire "VITA" or "SERVER" */
const char *crossplay_guard_badge(int crossplay, int dedicated);

/* the texts for the player (translated: lang.h) */
const char *crossplay_guard_large_title(void);
const char *crossplay_guard_large_question(void);
/* the question's title and text for a verdict asked about (LARGE or COOP;
players: the game's now, which a co-op question names past
CROSSPLAY_GUARD_COOP_PLAYERS) */
const char *crossplay_guard_title(int verdict);
const char *crossplay_guard_question(int verdict, int players);
/* what the browsers put after a PC game's badge for its verdict
(translated): " CO-OP EXPERIMENTAL", " EXPERIMENTAL" or "" */
const char *crossplay_guard_badge_suffix(int verdict);

#endif /* __CROSSPLAY_GUARD_H */
