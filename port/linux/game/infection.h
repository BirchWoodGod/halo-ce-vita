/*
INFECTION.H

Infection (infection.c): the port's own calls past game_engine.h's (the
bots', the settings', the lobby's check).
*/

#ifndef HALO_INFECTION_H
#define HALO_INFECTION_H

#include "game/game_engine.h"
#include "math/real_math.h"

/* this machine's settings for the built-in game type (infection.*) */
void infection_settings_configured(
	struct infection_settings *settings);

/* whether a zombie has shields (the settings') */
boolean infection_zombie_shields(
	void);

/* bots.c: a zombie bot's hunt, the nearest human's place (FALSE: not a
zombie, or no human) */
boolean infection_bot_hunt(
	long player_index,
	real_point3d const *position,
	real_point3d *target);

#endif
