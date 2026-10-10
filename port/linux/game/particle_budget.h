/*
PARTICLE_BUDGET.H

(port) The particle budget, the settings panel's Particles row
(HALO_PARTICLE_BUDGET, config.toml's graphics.particles): fewer of the
effects' particles made in a fight, Full as the Xbox: particle_budget.c
*/

#ifndef __PARTICLE_BUDGET_H
#define __PARTICLE_BUDGET_H
#pragma once

#include "cseries/cseries.h"
#include "math/real_math.h"

/* ---------- constants */

enum particle_budget_level
{
	/* every particle, as the Xbox (the default) */
	_particle_budget_full = 0,
	_particle_budget_reduced,
	_particle_budget_low,
	NUMBER_OF_PARTICLE_BUDGET_LEVELS
};

/* ---------- globals */

/* set by effect_generate_particles around each particle_new it asks for
from an entry the budget thins (effects.c): particle_new then shortens the
particle's life and may decline it (particles.c); FALSE for every other
particle_new (a breakable surface's shards) */
extern boolean particle_budget_applies;

/* ---------- prototypes */

/* the level chosen now (read again when the settings panel changes it) */
short particle_budget_level(void);
/* the particles an effect's event makes of an entry whose count is `count`
(the map's, after the split screen's share), at the budget's level */
short particle_budget_count(short count);
/* a particle's life span (after the split screen's share) at the budget's
level */
real particle_budget_lifespan(real lifespan);
/* whether a thinned particle at `position`, `maximum_radius` across at its
largest (half its diameter), whose definition draws it only over
`lod_cutoff` pixels, is made: not when the budget's particles are all live,
nor when no view drew one lately could see it at that size */
boolean particle_budget_admits(real_point3d const *position, real maximum_radius, real lod_cutoff);
/* (render_particles.c) the view a local player's particles were drawn from:
the camera's position and forward and the pixels a unit spans at a depth of
one */
void particle_budget_note_view(short local_player_index, real_point3d const *position,
	real_vector3d const *forward, real pixels_per_unit);

#endif // __PARTICLE_BUDGET_H
