/*
RAGDOLLS.H

Ragdolls (ragdolls.c; display.ragdolls, HALO_RAGDOLLS): a dead body's pose
made by a small physics simulation of its own skeleton, drawn in place of
the game's. Only the picture: nothing here is game state, nothing goes over
the network or into a saved game.
*/

#ifndef __RAGDOLLS_H
#define __RAGDOLLS_H

#include "cseries.h"
#include "real_math.h"

struct damage_data;

/* ---------- the tick's side (the tick's thread, or the game's thread with
the tick joined) */

/* units.c's unit_died, not feigned: a body that may go limp */
void ragdolls_unit_died(long unit_index);
/* damage.c's object_damage_aftermath: the blow, for the impulse of a death
that follows it, or to wake a ragdoll it strikes */
void ragdolls_note_damage(long object_index, struct damage_data const *damage);
/* game_time.c, after each game tick: the ragdolls simulated a tick */
void ragdolls_tick(void);
/* objects_initialize_for_new_map: none left (a new map's objects are not
the old map's) */
void ragdolls_reset(void);

/* ---------- the render's poses (render_interpolation.c) */

/* the tick's thread, copying the poses the render draws: the object has a
ragdoll's pose (copied even while the game keeps it inactive) */
boolean ragdolls_object_has_pose(long object_index);
/* ... the ragdoll's pose over the game's copy of node_count matrices, and
the sphere around it; FALSE: none */
boolean ragdolls_pose_override(long object_index, real_matrix4x3 *matrices, short node_count,
	real_point3d *center, real *radius);
/* the render with no tick running (no thread writes the ragdolls then):
the pose, or NULL */
real_matrix4x3 *ragdolls_live_node_matrices(long object_index);
boolean ragdolls_live_bounding_sphere(long object_index, real_point3d *center, real *radius);

/* ---------- tests (main.c's "@ragdoll ...", at the top of the main loop,
the tick joined) */
void ragdolls_test_command(char const *arguments);

#endif // __RAGDOLLS_H
