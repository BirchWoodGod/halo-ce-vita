/*
CROSSPLAY_GUARD.H

The client's guardrails on the crossplay wire (docs/crossplay.md 6, task
T11): what a Vita with Crossplay on (network_wire.h) makes of a PC game
(OpenCE, ChupathingyCE, DamnationCE) before it joins, from the server
browser's listing or a LAN advertisement, and again on the host's settings
record. Pure: no game, no network; the browsers (menu_functions.c, the
Vita's settings panel), the network test and the game's client
(port/linux/game/network_crossplay_guard.c) share it.

- A co-op game (no game engine: the campaign) is refused: a PC co-op host
  can drive up to 1056 actors, this build holds 288 (gap G14).
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
/* local players a Vita plays in a crossplay game (split screen) */
#define CROSSPLAY_GUARD_LOCAL_PLAYERS 1
/* how long a "yes" to a large game, or a "no", is remembered for it (the
browser's answer reaching the lobby; the network test's joins again) */
#define CROSSPLAY_GUARD_ANSWER_MILLISECONDS 120000UL

enum crossplay_guard_verdict
{
	CROSSPLAY_GUARD_OK,
	/* joined, after asking once: Experimental */
	CROSSPLAY_GUARD_LARGE,
	/* refused */
	CROSSPLAY_GUARD_COOP,
};

/* the verdict on a PC game: co-op (nonzero: no game engine, or a campaign
level), its players and maximum (any values) */
int crossplay_guard_judge(int coop, int players, int maximum);
/* whether those counts make it large */
int crossplay_guard_large(int players, int maximum);

/* the player's answer about the large game of this name (the listing's,
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
const char *crossplay_guard_coop_refusal(void);

#endif /* __CROSSPLAY_GUARD_H */
