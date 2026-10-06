/*
NETWORK_GAME_UI.H

header included in hcex build.
*/

#ifndef __NETWORK_GAME_UI_H
#define __NETWORK_GAME_UI_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/EXAMPLE.C */

wchar_t const *network_game_get_random_player_name(
	void);

boolean network_game_local_player_name(
	wchar_t const *profile_name,
	wchar_t *name,
	long count);

/* ---------- globals */

/* ---------- public code */

#endif // __NETWORK_GAME_UI_H
