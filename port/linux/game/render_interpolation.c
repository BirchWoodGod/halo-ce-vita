/*
RENDER_INTERPOLATION.C

Frames between the game's 30 Hz ticks, for the native ports (port/linux,
port/android, port/windows; see port/linux/README.md, "Frame rate"), and
on the Vita the settings panel's "Frame interpolation".

The game simulates in 30 Hz ticks and originally drew one frame per tick.
With interpolation the ports draw more frames than that, and every frame
shows the world between the last two ticks: after each tick the camera,
every object's node matrices and the first-person weapon's pose are kept,
and a frame blends the previous and the latest by how far the game clock has
run into the next tick. That puts what is drawn one tick (33 ms) behind the
simulation, the usual price of interpolation. (A Catmull-Rom spline would
also need the tick after the pair it spans: two ticks behind.)

Rotations are blended as quaternions (normalised lerp, taking the shorter
way round), positions and scales linearly. Anything that moves further than
a tick of motion plausibly allows (teleports, respawns, camera cuts) snaps
instead of sweeping across the world, and so does a pose whose nodes moved
too far for one tick: two snapshots of different poses (a model swapped,
another weapon's skeleton, a pose left from seconds before) blended node by
node stretch vertices across the screen (OpenCE's e4461981, "Snap poses
that changed too much in a tick").

Particles, contrails and other effects already move every frame
(game_frame), so they need nothing here.

An object the distributed netcode moves to where the host has it
(port/linux/game/network_objects.c) is drawn gliding there over a few ticks
rather than jumping: its snapshots move with it, and the difference is drawn
on top of it, whole until the next tick and fading from then on (no more than
a few world units of it: further is a jump). A local player's view glides so
with their unit, and with what it rides (OpenCE's 0d548798).

The snapshots are the threaded tick's poses (below): with the tick on a
thread of its own (the Vita's), a frame is drawn while the next tick runs,
so nothing the frame reads may be what that tick writes. OpenCE's own
snapshots were taken by the tick itself into the records the frame blended
(the frame read a record the running tick was rewriting, and the tick's own
reads of node matrices got the frame's blend while a frame was drawn): the
Vita kept interpolation off. Here the tick thread copies the poses after its
update into a buffer of three the render is not reading, the join publishes
it, and a frame blends the two latest published, as they stood after their
ticks. Inert scenery is left out of the copies, as Bruno Santana's build
leaves it out of the interpolation: it never moves (render_tick_poses_
capture). Whether the frames between ticks are drawn at all is main.c's
(the pacing: two frames a tick while both fit, else one).
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/players.h"
#include "render/render_cameras.h"
#include "structures/structures.h"
#include "structures/structure_bsp_definitions.h"
#include "render_epoch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* ---------- constants */

#define MAXIMUM_INTERPOLATED_NODES 64
/* the most ticks two blended snapshots may be apart (a frame that ran two
ticks to catch up blends across both; more is a pause or a hitch) */
#define MAXIMUM_INTERPOLATED_SPAN 4

/* world units (10 feet each) a node may move in one tick before it snaps:
well beyond any vehicle, short of any teleport */
#define OBJECT_SNAP_DISTANCE 10.0f
/* ... a node may move in the object's root node's frame in one tick before
the pose is taken for a new one, not blended to: further than any limb or
part of a model moves in 33 ms (37 m/s), short of the game changing a pose
at once (an actor waking from dormancy, a model swapped), which blended
sweeps the vertices through poses it never had. (In the root's frame, so
that the whole object turning moves nothing: measured in the world, a fast
turn swung the head and weapon far enough to snap, and characters moved
like robots.) */
#define NODE_SNAP_DISTANCE 0.4f
/* ... a first-person node may move relative to the camera */
#define FIRST_PERSON_SNAP_DISTANCE 0.25f
/* a correction's difference left drawn after each tick (of 1) */
#define CORRECTION_DECAY 0.6f
/* ... and small enough to be none */
#define CORRECTION_NEGLIGIBLE 0.001f
/* ... or so large that it is no glide but a jump (corrections come one on
another: their sum is drawn): past the largest a client's own unit or
vehicle is corrected by (3 and 4 world units), short of a snap */
#define CORRECTION_MAXIMUM 8.0f
/* a camera cut: a jump or turn no player or scripted camera makes in 33 ms */
#define CAMERA_CUT_DISTANCE 3.0f
#define CAMERA_CUT_COSINE 0.5f

/* ---------- structures */

struct interpolation_quaternion
{
	real i, j, k, w;
};

/* a node's rotation: whether its basis is one a quaternion can hold, and
that quaternion */
struct interpolation_rotation
{
	struct interpolation_quaternion quaternion;
	boolean is_rotation;
};

/* a correction: what is drawn on top of where it is, fading, and the
offsets since the last tick, drawn whole until the next */
struct interpolation_correction
{
	real_vector3d correction;
	real_vector3d pending;
};

struct interpolated_camera
{
	long tick;
	long previous_tick;
	boolean valid;
	boolean has_previous;
	struct observer_result previous;
	struct observer_result latest;
	struct observer_result blended;
};

struct interpolated_first_person
{
	long tick;
	/* the ticks between the previous pose and the latest */
	long span;
	short node_count;
	boolean has_previous;
	real_matrix4x3 previous[MAXIMUM_INTERPOLATED_NODES];
	real_matrix4x3 latest[MAXIMUM_INTERPOLATED_NODES];
};

/* an object as this frame draws it (the render's, by absolute index) */
struct interpolated_object
{
	long frame;
	long object_index;
	/* its nodes in blend_matrices (else the latest snapshot's), from */
	boolean blended;
	long first_matrix;
	real_point3d center;
	real radius;
};

/* ---------- globals */

static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
static struct interpolated_first_person interpolated_first_person[MAXIMUM_LOCAL_PLAYERS];
/* the ticks run (the tick thread's count: the snapshots' times) */
static long interpolation_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
/* the fraction of a tick the clock had run past the latest snapshot, and
the blend between the two snapshots it puts the frame at */
static real interpolation_fraction = 1.0f;
static real interpolation_blend = 1.0f;
/* the frame's snapshots (the latest's tick, its game time; the ticks back
to the previous, 0: none to blend from) */
static long interpolation_drawn_tick;
static long interpolation_drawn_game_time;
static long interpolation_span;
/* the tick the last frame drew (render_interpolation_frame_drew_new_tick) */
static long interpolation_last_drawn_tick = NONE;
static boolean interpolation_new_tick;
/* the map the cameras' and the first-person snapshots are of */
static unsigned long interpolation_snapshots_generation;
/* the first-person weapon's blend: with every frame interpolated, the same
tick and fraction; with only the weapon (HALO_INTERPOLATE_FIRST_PERSON),
the ticks the render has drawn and the fraction past them, both read on
the render's side (the running tick changes interpolation_tick and the
clock's leftover while the frame is drawn) */
static boolean first_person_rendering;
static long first_person_tick;
static real first_person_fraction = 1.0f;

/* the render's blends, by absolute index */
static struct interpolated_object *interpolated_objects;
static real_matrix4x3 *blend_matrices;
static long blend_capacity;

/* the netcode's corrections (the tick's side: render_interpolation_correct_
object, render_interpolation_tick), by absolute index, and the local
players' views'; each with the offsets moved since the last capture
(shift), which the capture hands the render with the poses */
static struct object_correction
{
	long object_index;
	struct interpolation_correction drawn;
	real_vector3d shift;
} *object_corrections;
static struct
{
	boolean valid;
	struct interpolation_correction drawn;
	real_vector3d shift;
} camera_corrections[MAXIMUM_LOCAL_PLAYERS];

short game_time_get_elapsed(void);

static real_matrix4x3 *tick_pose_node_matrices(long object_index);
static void tick_pose_frame_begin(void);
static void tick_pose_frame_end(void);
static struct tick_pose_buffer *tick_pose_latest(void);
static struct tick_pose_buffer *tick_pose_previous(void);
static long tick_pose_buffer_tick(struct tick_pose_buffer const *buffer, long *game_time);
static boolean tick_pose_buffer_camera(struct tick_pose_buffer const *buffer, short local_player_index,
	struct interpolation_correction *correction, real_vector3d *shift);

/* ---------- blending */

static real lerp(real a, real b, real t)
{
	return a + (b - a) * t;
}

static void point_lerp(real_point3d const *a, real_point3d const *b, real t, real_point3d *result)
{
	result->x = lerp(a->x, b->x, t);
	result->y = lerp(a->y, b->y, t);
	result->z = lerp(a->z, b->z, t);
}

static real vector_length(real_vector3d const *v)
{
	return (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k);
}

static void vector_nlerp(real_vector3d const *a, real_vector3d const *b, real t, real_vector3d *result)
{
	real length;

	result->i = lerp(a->i, b->i, t);
	result->j = lerp(a->j, b->j, t);
	result->k = lerp(a->k, b->k, t);
	length = vector_length(result);
	if (length > 1e-6f)
	{
		result->i /= length;
		result->j /= length;
		result->k /= length;
	}
	else
	{
		*result = *b;
	}
}

/* an orthonormal right-handed basis, which a quaternion can represent */
static boolean basis_is_rotation(real_matrix4x3 const *matrix)
{
	real_vector3d cross;
	real determinant;

	if (fabs(vector_length(&matrix->forward) - 1.0f) > 1e-2f ||
		fabs(vector_length(&matrix->left) - 1.0f) > 1e-2f ||
		fabs(vector_length(&matrix->up) - 1.0f) > 1e-2f)
	{
		return FALSE;
	}
	cross.i = matrix->forward.j * matrix->left.k - matrix->forward.k * matrix->left.j;
	cross.j = matrix->forward.k * matrix->left.i - matrix->forward.i * matrix->left.k;
	cross.k = matrix->forward.i * matrix->left.j - matrix->forward.j * matrix->left.i;
	determinant = cross.i * matrix->up.i + cross.j * matrix->up.j + cross.k * matrix->up.k;
	return determinant > 0.5f;
}

/* the basis vectors are the matrix's columns (x forward, y left, z up) */
static void quaternion_from_basis(real_matrix4x3 const *matrix, struct interpolation_quaternion *q)
{
	real m00 = matrix->forward.i, m10 = matrix->forward.j, m20 = matrix->forward.k;
	real m01 = matrix->left.i, m11 = matrix->left.j, m21 = matrix->left.k;
	real m02 = matrix->up.i, m12 = matrix->up.j, m22 = matrix->up.k;
	real trace = m00 + m11 + m22;
	real s;

	if (trace > 0.0f)
	{
		s = 0.5f / (real)sqrt(trace + 1.0f);
		q->w = 0.25f / s;
		q->i = (m21 - m12) * s;
		q->j = (m02 - m20) * s;
		q->k = (m10 - m01) * s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		s = 2.0f * (real)sqrt(1.0f + m00 - m11 - m22);
		q->w = (m21 - m12) / s;
		q->i = 0.25f * s;
		q->j = (m01 + m10) / s;
		q->k = (m02 + m20) / s;
	}
	else if (m11 > m22)
	{
		s = 2.0f * (real)sqrt(1.0f + m11 - m00 - m22);
		q->w = (m02 - m20) / s;
		q->i = (m01 + m10) / s;
		q->j = 0.25f * s;
		q->k = (m12 + m21) / s;
	}
	else
	{
		s = 2.0f * (real)sqrt(1.0f + m22 - m00 - m11);
		q->w = (m10 - m01) / s;
		q->i = (m02 + m20) / s;
		q->j = (m12 + m21) / s;
		q->k = 0.25f * s;
	}
}

static void basis_from_quaternion(struct interpolation_quaternion const *q, real_matrix4x3 *matrix)
{
	real ii = q->i * q->i, jj = q->j * q->j, kk = q->k * q->k;
	real ij = q->i * q->j, ik = q->i * q->k, jk = q->j * q->k;
	real wi = q->w * q->i, wj = q->w * q->j, wk = q->w * q->k;

	matrix->forward.i = 1.0f - 2.0f * (jj + kk);
	matrix->forward.j = 2.0f * (ij + wk);
	matrix->forward.k = 2.0f * (ik - wj);
	matrix->left.i = 2.0f * (ij - wk);
	matrix->left.j = 1.0f - 2.0f * (ii + kk);
	matrix->left.k = 2.0f * (jk + wi);
	matrix->up.i = 2.0f * (ik + wj);
	matrix->up.j = 2.0f * (jk - wi);
	matrix->up.k = 1.0f - 2.0f * (ii + jj);
}

static void rotation_from_matrix(real_matrix4x3 const *matrix, struct interpolation_rotation *rotation)
{
	rotation->is_rotation = basis_is_rotation(matrix);
	if (rotation->is_rotation)
		quaternion_from_basis(matrix, &rotation->quaternion);
}

/* a matrix a fraction t of the way from a to b, their rotations found */
static void matrix_blend_rotations(
	real_matrix4x3 const *a,
	real_matrix4x3 const *b,
	struct interpolation_rotation const *rotation_a,
	struct interpolation_rotation const *rotation_b,
	real t,
	real_matrix4x3 *result)
{
	result->scale = lerp(a->scale, b->scale, t);
	point_lerp(&a->position, &b->position, t, &result->position);
	if (rotation_a->is_rotation && rotation_b->is_rotation)
	{
		struct interpolation_quaternion qa = rotation_a->quaternion, qb = rotation_b->quaternion, q;
		real length;

		/* q and -q are the same rotation: take the shorter way round */
		if (qa.i * qb.i + qa.j * qb.j + qa.k * qb.k + qa.w * qb.w < 0.0f)
		{
			qb.i = -qb.i;
			qb.j = -qb.j;
			qb.k = -qb.k;
			qb.w = -qb.w;
		}
		q.i = lerp(qa.i, qb.i, t);
		q.j = lerp(qa.j, qb.j, t);
		q.k = lerp(qa.k, qb.k, t);
		q.w = lerp(qa.w, qb.w, t);
		length = (real)sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.w * q.w);
		if (length > 1e-6f)
		{
			q.i /= length;
			q.j /= length;
			q.k /= length;
			q.w /= length;
			basis_from_quaternion(&q, result);
			return;
		}
	}
	/* a basis a quaternion cannot hold (scaled or mirrored): blend it as is */
	result->forward.i = lerp(a->forward.i, b->forward.i, t);
	result->forward.j = lerp(a->forward.j, b->forward.j, t);
	result->forward.k = lerp(a->forward.k, b->forward.k, t);
	result->left.i = lerp(a->left.i, b->left.i, t);
	result->left.j = lerp(a->left.j, b->left.j, t);
	result->left.k = lerp(a->left.k, b->left.k, t);
	result->up.i = lerp(a->up.i, b->up.i, t);
	result->up.j = lerp(a->up.j, b->up.j, t);
	result->up.k = lerp(a->up.k, b->up.k, t);
}

static real distance_squared(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return x * x + y * y + z * z;
}

/* ---------- corrections (OpenCE's 0d548798, kept by the tick and handed
to the render with the poses) */

/* the vector's parts' sum, large enough to be a correction (so written that
one not a number is none) */
static boolean correction_significant(real_vector3d const *correction)
{
	return fabs(correction->i) + fabs(correction->j) + fabs(correction->k) >= CORRECTION_NEGLIGIBLE;
}

/* a correction a tick on: what was drawn fades, what came since is drawn
whole from now on, fading from the next */
static void correction_advance(struct interpolation_correction *correction)
{
	correction->correction.i = correction->correction.i * CORRECTION_DECAY + correction->pending.i;
	correction->correction.j = correction->correction.j * CORRECTION_DECAY + correction->pending.j;
	correction->correction.k = correction->correction.k * CORRECTION_DECAY + correction->pending.k;
	correction->pending = *global_zero_vector3d;
	if (!correction_significant(&correction->correction))
		correction->correction = *global_zero_vector3d;
}

/* a correction as drawn this frame: fading through the tick as it does tick
to tick, and those since the tick whole */
static boolean correction_drawn(struct interpolation_correction const *correction, real_vector3d *drawn)
{
	real fade = lerp(1.0f, CORRECTION_DECAY, interpolation_fraction);

	if (!correction_significant(&correction->correction) && !correction_significant(&correction->pending))
		return FALSE;
	drawn->i = correction->correction.i * fade + correction->pending.i;
	drawn->j = correction->correction.j * fade + correction->pending.j;
	drawn->k = correction->correction.k * fade + correction->pending.k;
	return TRUE;
}

/* a correction added (offset): all of it dropped when the sum is too large
to glide (or not a number) */
static void correction_add(struct interpolation_correction *correction, real_vector3d const *offset)
{
	real i, j, k;

	correction->pending.i += offset->i;
	correction->pending.j += offset->j;
	correction->pending.k += offset->k;
	i = correction->correction.i + correction->pending.i;
	j = correction->correction.j + correction->pending.j;
	k = correction->correction.k + correction->pending.k;
	if (!(i * i + j * j + k * k <= CORRECTION_MAXIMUM * CORRECTION_MAXIMUM))
	{
		correction->correction = *global_zero_vector3d;
		correction->pending = *global_zero_vector3d;
	}
}

/* the object (and what it carries) moved by the netcode from where it was,
offset from where it is now: drawn from there, gliding (the tick's side) */
void render_interpolation_correct_object(long object_index, real_vector3d const *offset)
{
	struct object_datum *object;
	long child_index;
	long absolute_index;

	/* (so written that an offset not a number is none) */
	if (!halo_interpolation_enabled() || object_index == NONE ||
		!(offset->i * offset->i + offset->j * offset->j + offset->k * offset->k <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE))
	{
		return;
	}
	if (!object_corrections)
	{
		long index;

		object_corrections = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*object_corrections));
		if (!object_corrections)
			return;
		for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
			object_corrections[index].object_index = NONE;
	}
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < MAXIMUM_OBJECTS_PER_MAP)
	{
		struct object_correction *record = &object_corrections[absolute_index];

		if (record->object_index != object_index)
		{
			memset(record, 0, sizeof(*record));
			record->object_index = object_index;
		}
		/* (the snapshots where it would have been: the render moves the
		previous by the shift; the difference drawn) */
		record->shift.i += offset->i;
		record->shift.j += offset->j;
		record->shift.k += offset->k;
		correction_add(&record->drawn, offset);
	}
	/* a local player's unit (by itself, or with what it rides): its
	first-person view, which the observer poses from it, glides with it */
	{
		short local_player_index;

		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			if (player_control_get_unit_index(local_player_index) != object_index)
				continue;
			camera_corrections[local_player_index].valid = TRUE;
			camera_corrections[local_player_index].shift.i += offset->i;
			camera_corrections[local_player_index].shift.j += offset->j;
			camera_corrections[local_player_index].shift.k += offset->k;
			correction_add(&camera_corrections[local_player_index].drawn, offset);
		}
	}
	object = object_get(object_index);
	for (child_index = object->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		render_interpolation_correct_object(child_index, offset);
	}
}

/* ---------- ticks */

/* A map change must not blend against snapshots of the previous map.
Keep the Vita's tick-pose publication and render-epoch locking intact. */
void render_interpolation_reset(void)
{
	long index;

	if (object_corrections)
		for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
			object_corrections[index].object_index = NONE;
	memset(camera_corrections, 0, sizeof(camera_corrections));
	memset(interpolated_cameras, 0, sizeof(interpolated_cameras));
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		interpolated_first_person[index].node_count = 0;
		interpolated_first_person[index].has_previous = FALSE;
	}
}

/* after each tick (game_time.c, on the tick's thread): the corrections a
tick on; the poses are copied once the update is done
(render_tick_poses_capture) */
void render_interpolation_tick(void)
{
	long index;

	interpolation_tick++;
	if (!halo_interpolation_enabled())
		return;
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
		if (camera_corrections[index].valid)
			correction_advance(&camera_corrections[index].drawn);
	if (!object_corrections)
		return;
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		struct object_correction *record = &object_corrections[index];

		if (record->object_index == NONE)
			continue;
		correction_advance(&record->drawn);
		if (!correction_significant(&record->drawn.correction) && !correction_significant(&record->drawn.pending) &&
			!correction_significant(&record->shift))
		{
			record->object_index = NONE;
		}
	}
}

/* ---------- frames */

/* HALO_INTERPOLATE_FIRST_PERSON=1: without interpolation, the first-person
weapon and hands alone are blended between the last two ticks drawn. The
Vita draws a frame only when a tick has run, 20-30 a second, so a frame
moves the weapon's animation on by one tick or by two, unevenly - "on
twos" (#13). The blend costs a few microseconds a frame (a matrix inverse,
two products and a quaternion blend per node); the weapon's animation is
drawn up to a tick behind, the camera and the world are not. With frame
interpolation on, the weapon is blended with the rest instead, once. */
int halo_first_person_interpolation_enabled(void)
{
	static int enabled = -1;
	static unsigned long settings_seen;
	extern volatile unsigned long halo_settings_generation;

	/* (read again when the settings panel changes something) */
	if (enabled < 0 || settings_seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_INTERPOLATE_FIRST_PERSON");

		settings_seen = halo_settings_generation;
		enabled = setting && atoi(setting) != 0;
	}
	return enabled;
}

void render_interpolation_frame_begin(void)
{
	struct tick_pose_buffer *latest = tick_pose_latest();

	interpolation_frame++;
	interpolation_rendering = halo_interpolation_enabled() && latest;
	if (interpolation_snapshots_generation != halo_map_generation)
	{
		short index;

		/* (a revert or a saved game: no camera or pose of before it) */
		interpolation_snapshots_generation = halo_map_generation;
		memset(interpolated_cameras, 0, sizeof(interpolated_cameras));
		for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
			interpolated_first_person[index].has_previous = FALSE;
	}
	if (interpolation_rendering)
	{
		struct tick_pose_buffer *previous = tick_pose_previous();
		long previous_tick = previous ? tick_pose_buffer_tick(previous, NULL) : NONE;

		interpolation_drawn_tick = tick_pose_buffer_tick(latest, &interpolation_drawn_game_time);
		interpolation_span = previous_tick != NONE ? interpolation_drawn_tick - previous_tick : 0;
		if (interpolation_span < 1 || interpolation_span > MAXIMUM_INTERPOLATED_SPAN)
			interpolation_span = 0;
		/* (the finished update's, with the tick on its thread: the running
		one rewrites the clock's leftover) */
		interpolation_fraction = halo_render_tick_fraction_get();
		if (!(interpolation_fraction >= 0.0f))
			interpolation_fraction = 0.0f;
		if (interpolation_fraction > 1.0f)
			interpolation_fraction = 1.0f;
		{
			/* (debug) HALO_INTERPOLATION_LATEST=1: every frame draws the
			latest tick, as without interpolation, through the same
			snapshots (the tick hash's check that they change nothing of the
			simulation: triage/interp-status.md) */
			static int latest_only = -1;

			if (latest_only < 0)
				latest_only = getenv("HALO_INTERPOLATION_LATEST") && atoi(getenv("HALO_INTERPOLATION_LATEST"));
			if (latest_only)
				interpolation_fraction = 1.0f;
		}
		/* (the latest snapshot is span ticks after the previous, the clock
		a fraction of a tick past it, and the frame a tick behind it) */
		interpolation_blend = interpolation_span ?
			((real)(interpolation_span - 1) + interpolation_fraction) / (real)interpolation_span : 1.0f;
		interpolation_new_tick = interpolation_drawn_tick != interpolation_last_drawn_tick;
		interpolation_last_drawn_tick = interpolation_drawn_tick;
	}
	else
	{
		interpolation_fraction = game_time_get_tick_fraction();
		interpolation_new_tick = TRUE;
		interpolation_last_drawn_tick = NONE;
	}
	first_person_rendering = interpolation_rendering || halo_first_person_interpolation_enabled();
	if (interpolation_rendering)
	{
		first_person_tick = interpolation_drawn_tick;
		first_person_fraction = interpolation_fraction;
	}
	else if (first_person_rendering)
	{
		short elapsed = game_time_get_elapsed();

		first_person_tick += elapsed > 0 ? elapsed : 0;
		first_person_fraction = halo_render_tick_fraction_get();
	}
	tick_pose_frame_begin();
}

void render_interpolation_frame_end(void)
{
	interpolation_rendering = FALSE;
	first_person_rendering = FALSE;
	tick_pose_frame_end();
}

real render_interpolation_fraction(void)
{
	return interpolation_rendering ? interpolation_fraction : 1.0f;
}

/* (main.c's count of frames) the frame drew a tick no frame drew before:
FALSE for one drawn between ticks, from the same snapshots as the last */
boolean render_interpolation_frame_drew_new_tick(void)
{
	return interpolation_new_tick;
}

/* the tick the frame draws (the first-person weapon's sway, once a tick) */
long render_interpolation_drawn_tick(void)
{
	return interpolation_rendering ? interpolation_drawn_tick : game_time_get();
}

/* ---------- camera */

struct observer_result const *render_interpolation_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	struct interpolated_camera *camera;
	struct tick_pose_buffer *latest;
	long span;
	real t;

	if (!interpolation_rendering || !observer ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
	{
		return observer;
	}
	camera = &interpolated_cameras[local_player_index];
	latest = tick_pose_latest();
	/* the observer as it stood after each tick (the first frame drawn
	after the tick: observer_update places it before the next tick starts,
	from the tick the frame draws) */
	if (!camera->valid || camera->tick != interpolation_drawn_tick)
	{
		struct interpolation_correction correction;
		real_vector3d shift;

		camera->has_previous = camera->valid && interpolation_drawn_tick > camera->tick &&
			interpolation_drawn_tick - camera->tick <= MAXIMUM_INTERPOLATED_SPAN;
		camera->previous = camera->latest;
		camera->previous_tick = camera->tick;
		/* (moved by the netcode since: where it would have been) */
		if (tick_pose_buffer_camera(latest, local_player_index, &correction, &shift))
		{
			camera->previous.position.x -= shift.i;
			camera->previous.position.y -= shift.j;
			camera->previous.position.z -= shift.k;
		}
		camera->latest = *observer;
		camera->tick = interpolation_drawn_tick;
		camera->valid = TRUE;
	}
	span = camera->tick - camera->previous_tick;
	/* (so written that a position or direction not a number cuts) */
	if (!camera->has_previous || span < 1 ||
		!(distance_squared(&camera->previous.position, &camera->latest.position) <=
			CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE * (real)(span * span)) ||
		!(camera->previous.forward.i * camera->latest.forward.i +
			camera->previous.forward.j * camera->latest.forward.j +
			camera->previous.forward.k * camera->latest.forward.k >= CAMERA_CUT_COSINE))
	{
		return observer;
	}
	t = ((real)(span - 1) + interpolation_fraction) / (real)span;

	camera->blended = camera->latest;
	point_lerp(&camera->previous.position, &camera->latest.position, t, &camera->blended.position);
	{
		struct interpolation_correction correction;
		real_vector3d shift, drawn;

		if (tick_pose_buffer_camera(latest, local_player_index, &correction, &shift) &&
			correction_drawn(&correction, &drawn))
		{
			camera->blended.position.x += drawn.i;
			camera->blended.position.y += drawn.j;
			camera->blended.position.z += drawn.k;
		}
	}
	vector_nlerp(&camera->previous.forward, &camera->latest.forward, t, &camera->blended.forward);
	vector_nlerp(&camera->previous.up, &camera->latest.up, t, &camera->blended.up);
	{
		/* keep up perpendicular to forward */
		real_vector3d *forward = &camera->blended.forward;
		real_vector3d *up = &camera->blended.up;
		real along = up->i * forward->i + up->j * forward->j + up->k * forward->k;
		real length;

		up->i -= forward->i * along;
		up->j -= forward->j * along;
		up->k -= forward->k * along;
		length = vector_length(up);
		if (length > 1e-6f)
		{
			up->i /= length;
			up->j /= length;
			up->k /= length;
		}
		else
		{
			*up = camera->latest.up;
		}
	}
	camera->blended.field_of_view = lerp(camera->previous.field_of_view, camera->latest.field_of_view, t);
	return &camera->blended;
}

/* ---------- first-person weapon */

/* The first-person weapon and hands are posed in world space from the drawn
camera each frame, from animation state that changes once a tick: blend the
pose relative to the camera. */
void render_interpolation_first_person(
	short local_player_index,
	real_matrix4x3 *node_matrices,
	short node_count,
	struct render_camera const *camera)
{
	struct interpolated_first_person *first_person;
	real_matrix4x3 camera_matrix;
	real_matrix4x3 inverse_camera;
	struct interpolation_rotation previous_rotations[MAXIMUM_INTERPOLATED_NODES];
	struct interpolation_rotation latest_rotations[MAXIMUM_INTERPOLATED_NODES];
	short node_index;
	real t;

	if (!first_person_rendering ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
	{
		return;
	}
	first_person = &interpolated_first_person[local_player_index];
	matrix4x3_from_point_and_vectors(&camera_matrix, &camera->position, &camera->forward, &camera->up);
	matrix4x3_inverse(&camera_matrix, &inverse_camera);
	if (first_person->tick != first_person_tick)
	{
		/* the pose drawn last, at the end of the previous tick drawn (more
		than one tick back when a frame ran several; a long gap - a pause,
		a load, the view away from first person - starts afresh) */
		first_person->span = first_person_tick - first_person->tick;
		first_person->has_previous = first_person->node_count == node_count &&
			first_person->span > 0 && first_person->span <= MAXIMUM_INTERPOLATED_SPAN;
		memcpy(first_person->previous, first_person->latest, sizeof(first_person->previous));
		first_person->tick = first_person_tick;
	}
	for (node_index = 0; node_index < node_count; node_index++)
		matrix4x3_multiply(&inverse_camera, &node_matrices[node_index], &first_person->latest[node_index]);
	first_person->node_count = node_count;
	if (!first_person->has_previous)
		return;
	/* the pose a tick before the clock: the previous pose is span ticks
	behind the latest, the clock a fraction of a tick ahead of it (at the
	latest, the pose as it is, not through a quaternion and back) */
	t = ((real)(first_person->span - 1) + first_person_fraction) / (real)first_person->span;
	if (!(t < 1.0f))
		return;
	/* a node that jumped further in the camera's frame than its ticks
	allow: the last pose was another weapon's skeleton (of as many nodes),
	not this one moving (OpenCE's e4461981; so written that a position not
	a number snaps) */
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 const *previous = &first_person->previous[node_index];
		real_matrix4x3 const *latest = &first_person->latest[node_index];
		real allowed = FIRST_PERSON_SNAP_DISTANCE * (real)first_person->span;

		if (!(distance_squared(&previous->position, &latest->position) <= allowed * allowed))
			return;
		rotation_from_matrix(previous, &previous_rotations[node_index]);
		rotation_from_matrix(latest, &latest_rotations[node_index]);
	}
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 blended;

		matrix_blend_rotations(
			&first_person->previous[node_index],
			&first_person->latest[node_index],
			&previous_rotations[node_index],
			&latest_rotations[node_index],
			t,
			&blended);
		matrix4x3_multiply(&camera_matrix, &blended, &node_matrices[node_index]);
	}
}

/* ---------- time */

/* game time for animated shaders, continuous between ticks: the time of
the frame drawn (a tick behind the simulation, like the objects) */
real render_interpolation_game_time_sec(long ticks)
{
	real time;

	if (!interpolation_rendering)
		return (real)ticks * (1.0f / TICKS_PER_SECOND);
	/* (the snapshot's, not the running tick's) */
	time = ((real)interpolation_drawn_game_time - 1.0f + interpolation_fraction) * (1.0f / TICKS_PER_SECOND);
	return time > 0.0f ? time : 0.0f;
}

/* ---------- the threaded tick's poses

With the tick on a thread of its own (port/linux/game/tick_thread.c) a frame
is drawn while the next tick runs. The camera is placed before that tick
starts (observer_update), but without interpolation the render read each
object's node matrices where the tick rewrites them: an object the tick had
already moved when the render reached it was drawn a tick ahead of the
camera, and one it was moving at that moment half old and half new. Where
the camera follows a moving object - a vehicle being driven, a Pelican in a
cinematic - which of those the render saw changed from frame to frame with
the threads' timing, and the object shook and flickered against a world
that stood still (on the Vita; the emulator's timing hid it).

Instead, after its game_time_update the tick thread copies every object's
node matrices into one of three buffers, and the join publishes that buffer.
The next frame's render, drawn while the following tick runs, reads the
published buffer, which that tick does not write (it fills another), so
every object is drawn as it stood when the camera was placed. The tick's
own reads, and the frames drawn with no tick running, use the live
matrices as before.

With frame interpolation, every frame draws from the published buffer and
the one published before it with an earlier tick (the third is the one the
tick fills), blended (render_interpolation_object_node_matrices); an update
that ran no tick copies nothing then (its poses are the published ones).

HALO_TICK_POSES=0 turns this off (and frame interpolation with it);
HALO_TICK_POSES_STATS=1 logs, every 300 frames, how many objects drawn from
the poses the tick had already changed (each one a draw that would have
been out of step) and what the copies cost. */

#define TICK_POSE_BUFFERS 3

struct tick_pose_entry
{
	long object_index;
	long first_matrix;
	short node_count;
	/* the render found its nodes' rotations (interpolation) */
	boolean rotations_valid;
	/* the buffer's capture this entry is from: older ones are stale */
	unsigned long capture;
	/* the bounding sphere then (the detail level, the shadow, the sorting
	and fog centroid are taken from it) */
	real_point3d center;
	real radius;
	/* (interpolation) the netcode's correction then, and the offsets it
	moved the object by since the capture before */
	struct interpolation_correction correction;
	real_vector3d shift;
};

/* a cluster partition's object lists as the tick left them: per cluster,
its objects from first[cluster] on, each run ended by NONE */
struct tick_cluster_list
{
	long *first;
	long first_capacity;
	long *datums;
	long datum_capacity;
	short cluster_count;
	boolean valid;
};

struct tick_pose_buffer
{
	/* by absolute object index */
	struct tick_pose_entry *entries;
	real_matrix4x3 *matrices;
	/* (interpolation) the matrices' rotations, found by the render */
	struct interpolation_rotation *rotations;
	long matrix_capacity;
	unsigned long capture;
	unsigned long map_generation;
	/* the ticks run before the capture (interpolation_tick) and the game
	time then */
	long tick;
	long game_time;
	/* (interpolation) the local players' views' corrections */
	struct
	{
		boolean valid;
		struct interpolation_correction correction;
		real_vector3d shift;
	} cameras[MAXIMUM_LOCAL_PLAYERS];
};

/* the cluster lists, copied with every update */
struct tick_list_buffer
{
	unsigned long capture;
	unsigned long map_generation;
	struct tick_cluster_list cluster_lists[_tick_cluster_list_count];
};

static struct tick_pose_buffer tick_pose_buffers[TICK_POSE_BUFFERS];
/* the buffer the render reads (-1: none yet), the one published before it
with an earlier tick (interpolation's other snapshot; -1: none), and the
one the tick thread filled since the last join (-1: none) */
static int tick_pose_published = -1, tick_pose_previous_index = -1, tick_pose_captured = -1;
static struct tick_list_buffer tick_list_buffers[2];
static int tick_list_published = -1, tick_list_captured = -1;
/* a tick was started after the camera was placed and is not yet joined */
static boolean tick_pose_tick_in_flight;
static boolean tick_pose_rendering;
/* the frame finds its objects in the published cluster lists */
static boolean tick_cluster_lists_rendering;

static struct
{
	int wanted;
	unsigned long frames, lookups, moved;
	unsigned long captures, captured_objects, captured_nodes;
	unsigned long long capture_us;
	/* (interpolation) objects blended and snapped, and their time */
	unsigned long blended, snapped;
	unsigned long long blend_us;
} tick_pose_stats = { -1 };

unsigned long long vita_host_time_us(void) __attribute__((weak));

static boolean tick_poses_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_POSES");
		const char *stats = getenv("HALO_TICK_POSES_STATS");

		enabled = !setting || atoi(setting) != 0;
		tick_pose_stats.wanted = stats && atoi(stats) != 0;
	}
	return enabled;
}

static unsigned long long tick_pose_now_us(void)
{
	return vita_host_time_us ? vita_host_time_us() : 0;
}

static long tick_pose_buffer_tick(struct tick_pose_buffer const *buffer, long *game_time)
{
	if (game_time)
		*game_time = buffer->game_time;
	return buffer->tick;
}

/* the local player's view's correction as the buffer's tick left it, and
the offsets it was moved by since the capture before: FALSE when none */
static boolean tick_pose_buffer_camera(struct tick_pose_buffer const *buffer, short local_player_index,
	struct interpolation_correction *correction, real_vector3d *shift)
{
	if (!buffer || local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!buffer->cameras[local_player_index].valid)
	{
		return FALSE;
	}
	*correction = buffer->cameras[local_player_index].correction;
	*shift = buffer->cameras[local_player_index].shift;
	return TRUE;
}

/* (interpolation) the latest snapshot the render draws, and the one before
it; NULL when there is none for this map */
static struct tick_pose_buffer *tick_pose_latest(void)
{
	if (!tick_poses_enabled() || tick_pose_published < 0 ||
		tick_pose_buffers[tick_pose_published].map_generation != halo_map_generation)
	{
		return NULL;
	}
	return &tick_pose_buffers[tick_pose_published];
}

static struct tick_pose_buffer *tick_pose_previous(void)
{
	if (!tick_pose_latest() || tick_pose_previous_index < 0 ||
		tick_pose_buffers[tick_pose_previous_index].map_generation != halo_map_generation)
	{
		return NULL;
	}
	return &tick_pose_buffers[tick_pose_previous_index];
}

/* ---------- the threaded tick's cluster lists

The render finds the objects to draw by walking each visible cluster's list
of objects (structure_visibility_find_objects). Those lists are the tick's:
an object that moves, turns or animates its bounds is taken out of every
cluster's list (object_disconnect_from_map), its node matrices recomputed,
and put back (object_reconnect_to_map) - every tick, for every walking
biped, driven vehicle, animating device. A render walking a list while the
running tick has an object out of it does not find the object, and it is
not drawn that frame: the one-frame flicker of the Vita's animated models
(the cryo pods and alert lights of a10, the Warthog, NPCs, with
HALO_FLICKER_LOG=1 hundreds of them in the a10 opening alone), more often
the more there is moving. Interpolation off and the threaded tick on (the
Vita's defaults) leave nothing to hide it.

So the tick thread copies the object lists of every cluster with the poses
(after game_time_update, when it is alone with them) and the render, while
the next tick runs, finds its objects in the copy: the lists as they stood
when the camera was placed, which the objects' poses are from too. An
object the running tick deletes is still there for the render (render_
epoch.h's tombstones) and one it creates appears next frame, as before. */

static boolean tick_cluster_list_room(long **array, long *capacity, long wanted)
{
	long grown;
	long *larger;

	if (wanted <= *capacity)
		return TRUE;
	grown = *capacity ? *capacity * 2 : 1024;
	if (grown < wanted)
		grown = wanted;
	/* (the C library's realloc: see render_tick_poses_capture) */
	larger = (realloc)(*array, grown * sizeof(long));
	if (!larger)
		return FALSE;
	*array = larger;
	*capacity = grown;
	return TRUE;
}

/* (object_lights.c) */
extern struct cluster_partition light_cluster_partition;
extern struct data_array *light_data;

/* the array each list's datums are in */
static struct data_array *tick_cluster_list_data(int which)
{
	return which == _tick_cluster_list_light ? light_data : object_header_data;
}

static void tick_cluster_lists_capture(struct tick_list_buffer *buffer)
{
	/* (the lights too: an object that moves - the player with the
	flashlight - takes its lights out of their clusters and back every tick
	(objects.c object_connect_lights), and a render that missed the
	flashlight in its walk (lights_preprocess_scene) drew the frame without
	it: on the Vita the flashlight's pool and what it lit went black on some
	frames) */
	static struct cluster_partition *const partitions[_tick_cluster_list_count] = {
		&collideable_object_cluster_partition,
		&noncollideable_object_cluster_partition,
		&light_cluster_partition,
	};
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	short cluster_count = structure_bsp ? (short)structure_bsp->clusters.count : 0;
	int which;

	for (which = 0; which < _tick_cluster_list_count; which++)
	{
		struct tick_cluster_list *list = &buffer->cluster_lists[which];
		struct cluster_partition *partition = partitions[which];
		long used = 0;
		short cluster_index;

		list->valid = FALSE;
		if (!cluster_count || !partition->data_reference_data || !partition->data_reference_data->valid ||
			!tick_cluster_list_room(&list->first, &list->first_capacity, cluster_count))
		{
			continue;
		}
		for (cluster_index = 0; cluster_index < cluster_count; cluster_index++)
		{
			long reference, datum_index;

			list->first[cluster_index] = used;
			for (datum_index = cluster_partition_get_first_datum(partition, &reference, cluster_index);
				datum_index != NONE;
				datum_index = cluster_partition_get_next_datum(partition, &reference))
			{
				if (!tick_cluster_list_room(&list->datums, &list->datum_capacity, used + 2))
					break;
				list->datums[used++] = datum_index;
			}
			if (!tick_cluster_list_room(&list->datums, &list->datum_capacity, used + 1))
				break;
			list->datums[used++] = NONE;
		}
		if (cluster_index == cluster_count)
		{
			list->cluster_count = cluster_count;
			list->valid = TRUE;
		}
	}
}

/* the render: does this frame find its objects in the lists as the tick
left them? */
boolean render_tick_cluster_lists_active(int which)
{
	return tick_cluster_lists_rendering && !halo_epoch_on_mutator() &&
		which >= 0 && which < _tick_cluster_list_count &&
		tick_list_buffers[tick_list_published].cluster_lists[which].valid;
}

/* the walk of a cluster's objects in those lists (*iterator: the position) */
long render_tick_cluster_list_next(int which, long *iterator)
{
	struct tick_cluster_list *list = &tick_list_buffers[tick_list_published].cluster_lists[which];

	while (*iterator != NONE)
	{
		long datum_index = list->datums[*iterator];

		if (datum_index == NONE)
		{
			*iterator = NONE;
			break;
		}
		(*iterator)++;
		/* (a datum deleted on this thread since, between the frames - an
		object a script or cheat deleted, a light's transition that ended
		(lights_preprocess_scene) - whose slot may hold another) */
		if (datum_try_and_get(tick_cluster_list_data(which), datum_index))
			return datum_index;
	}
	return NONE;
}

long render_tick_cluster_list_first(int which, long *iterator, short cluster_index)
{
	struct tick_cluster_list *list = &tick_list_buffers[tick_list_published].cluster_lists[which];

	*iterator = cluster_index >= 0 && cluster_index < list->cluster_count ? list->first[cluster_index] : NONE;
	return render_tick_cluster_list_next(which, iterator);
}

/* The other direction: the clusters an object is in, as the tick left it.
An object's dynamic lights are searched in its clusters (object_lights.c
lights_prepare_for_object_dynamic), and the live walk of its cluster
references (object_get_first_cluster) finds none while the running tick has
it out of the map to move it: that frame the object was drawn without the
flashlight or any other point light, the next with it. Made on the render
thread, once per published capture, from the two object lists above. */
static struct
{
	unsigned long capture;
	int buffer;
	/* by absolute object index: the datum index then, its clusters from
	first[] on, count[] of them */
	long owner[MAXIMUM_OBJECTS_PER_MAP];
	long first[MAXIMUM_OBJECTS_PER_MAP];
	short count[MAXIMUM_OBJECTS_PER_MAP];
	short *clusters;
	long cluster_capacity;
	boolean valid;
} tick_object_clusters = { 0, -1 };

static void tick_object_clusters_build(struct tick_list_buffer *buffer)
{
	long total = 0, used = 0;
	int which;
	long index;

	tick_object_clusters.valid = FALSE;
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		tick_object_clusters.owner[index] = NONE;
		tick_object_clusters.count[index] = 0;
	}
	for (which = _tick_cluster_list_collideable; which <= _tick_cluster_list_noncollideable; which++)
	{
		struct tick_cluster_list *list = &buffer->cluster_lists[which];
		short cluster_index;

		if (!list->valid)
			return;
		for (cluster_index = 0; cluster_index < list->cluster_count; cluster_index++)
		{
			long entry;

			for (entry = list->first[cluster_index]; list->datums[entry] != NONE; entry++)
			{
				long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(list->datums[entry]);

				if (absolute_index >= 0 && absolute_index < MAXIMUM_OBJECTS_PER_MAP)
				{
					tick_object_clusters.owner[absolute_index] = list->datums[entry];
					tick_object_clusters.count[absolute_index]++;
					total++;
				}
			}
		}
	}
	if (total > tick_object_clusters.cluster_capacity)
	{
		long capacity = total * 2 > 4096 ? total * 2 : 4096;
		/* (the C library's realloc: see render_tick_poses_capture) */
		short *larger = (realloc)(tick_object_clusters.clusters, capacity * sizeof(short));

		if (!larger)
			return;
		tick_object_clusters.clusters = larger;
		tick_object_clusters.cluster_capacity = capacity;
	}
	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
	{
		tick_object_clusters.first[index] = used;
		used += tick_object_clusters.count[index];
		tick_object_clusters.count[index] = 0;
	}
	for (which = _tick_cluster_list_collideable; which <= _tick_cluster_list_noncollideable; which++)
	{
		struct tick_cluster_list *list = &buffer->cluster_lists[which];
		short cluster_index;

		for (cluster_index = 0; cluster_index < list->cluster_count; cluster_index++)
		{
			long entry;

			for (entry = list->first[cluster_index]; list->datums[entry] != NONE; entry++)
			{
				long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(list->datums[entry]);

				if (absolute_index >= 0 && absolute_index < MAXIMUM_OBJECTS_PER_MAP &&
					tick_object_clusters.owner[absolute_index] == list->datums[entry])
				{
					tick_object_clusters.clusters[tick_object_clusters.first[absolute_index] +
						tick_object_clusters.count[absolute_index]++] = cluster_index;
				}
			}
		}
	}
	tick_object_clusters.valid = TRUE;
}

/* the render: the clusters the object (its ultimate parent, as
object_get_first_cluster) was in when the tick drawn was captured; FALSE
when the frame does not use the captured lists or the object was not in
them (created since): the caller walks the live references then */
boolean render_tick_object_clusters(long object_index, short const **clusters, short *count)
{
	struct tick_list_buffer *buffer;
	long absolute_index;

	if (!render_tick_cluster_lists_active(_tick_cluster_list_collideable) ||
		!render_tick_cluster_lists_active(_tick_cluster_list_noncollideable))
	{
		return FALSE;
	}
	buffer = &tick_list_buffers[tick_list_published];
	if (tick_object_clusters.buffer != tick_list_published || tick_object_clusters.capture != buffer->capture)
	{
		tick_object_clusters.buffer = tick_list_published;
		tick_object_clusters.capture = buffer->capture;
		tick_object_clusters_build(buffer);
	}
	if (!tick_object_clusters.valid)
		return FALSE;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP ||
		tick_object_clusters.owner[absolute_index] != object_index)
	{
		return FALSE;
	}
	*clusters = tick_object_clusters.clusters + tick_object_clusters.first[absolute_index];
	*count = tick_object_clusters.count[absolute_index];
	return TRUE;
}

/* the poses into a buffer neither published nor interpolation's previous */
static void tick_poses_capture(int which)
{
	struct tick_pose_buffer *buffer = &tick_pose_buffers[which];
	struct object_iterator iterator;
	struct object_datum *object;
	boolean corrections = halo_interpolation_enabled();
	long used = 0;
	short local_player_index;

	if (!buffer->entries)
	{
		buffer->entries = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*buffer->entries));
		if (!buffer->entries)
			return;
	}
	buffer->capture++;
	buffer->tick = interpolation_tick;
	buffer->game_time = game_time_get();
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		buffer->cameras[local_player_index].valid = corrections && camera_corrections[local_player_index].valid;
		if (buffer->cameras[local_player_index].valid)
		{
			buffer->cameras[local_player_index].correction = camera_corrections[local_player_index].drawn;
			buffer->cameras[local_player_index].shift = camera_corrections[local_player_index].shift;
			camera_corrections[local_player_index].shift = *global_zero_vector3d;
		}
	}
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
		struct object_header_datum *header = object_header_get(iterator.index);
		struct tick_pose_entry *entry;

		if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP || node_count <= 0)
			continue;
		/* (what the tick does not move is left to the live matrices, which
		then hold still: an inactive object, which it does not update, and
		static scenery, whose recompute writes the matrices it had - half
		the objects and nodes of a level, kept out of the copy, and out of
		interpolation, as in Bruno Santana's build) */
		if (object->object.parent_object_index == NONE && !TEST_FLAG(header->flags, _object_header_active_bit))
			continue;
		if (header->type == _object_type_scenery && object->object.animation.animation_graph_index == NONE &&
			object->object.parent_object_index == NONE && object->object.first_child_object_index == NONE)
		{
			continue;
		}
		if (used + node_count > buffer->matrix_capacity)
		{
			long capacity = buffer->matrix_capacity ? buffer->matrix_capacity * 2 : 4096;
			real_matrix4x3 *matrices;
			struct interpolation_rotation *rotations;

			if (capacity < used + node_count)
				capacity = used + node_count;
			/* (the C library's realloc, not cseries.h's tracked one, whose
			list the main thread may be changing) */
			matrices = (realloc)(buffer->matrices, capacity * sizeof(real_matrix4x3));
			if (!matrices)
				break; /* (the objects left out are drawn live) */
			buffer->matrices = matrices;
			rotations = (realloc)(buffer->rotations, capacity * sizeof(struct interpolation_rotation));
			if (!rotations)
				break;
			buffer->rotations = rotations;
			buffer->matrix_capacity = capacity;
		}
		memcpy(buffer->matrices + used,
			object_header_block_get(iterator.index, &object->object.node_matrices),
			node_count * sizeof(real_matrix4x3));
		entry = &buffer->entries[absolute_index];
		entry->object_index = iterator.index;
		entry->first_matrix = used;
		entry->node_count = node_count;
		entry->rotations_valid = FALSE;
		entry->capture = buffer->capture;
		entry->center = object->object.bounding_sphere_center;
		entry->radius = object->object.bounding_sphere_radius;
		if (corrections && object_corrections && object_corrections[absolute_index].object_index == iterator.index)
		{
			entry->correction = object_corrections[absolute_index].drawn;
			entry->shift = object_corrections[absolute_index].shift;
			object_corrections[absolute_index].shift = *global_zero_vector3d;
		}
		else
		{
			memset(&entry->correction, 0, sizeof(entry->correction));
			entry->shift = *global_zero_vector3d;
		}
		used += node_count;
		if (tick_pose_stats.wanted)
			tick_pose_stats.captured_objects++;
	}
	buffer->map_generation = halo_map_generation;
	tick_pose_captured = which;
	if (tick_pose_stats.wanted)
		tick_pose_stats.captured_nodes += used;
}

/* the tick thread, after game_time_update (or the game's thread after it,
with the tick on that thread and interpolation on): the objects as the tick
left them, into a buffer the render is not reading */
void render_tick_poses_capture(void)
{
	unsigned long long before;
	int which;

	if (!tick_poses_enabled())
		return;
	before = tick_pose_stats.wanted ? tick_pose_now_us() : 0;
	which = tick_list_published == 0 ? 1 : 0;
	tick_list_buffers[which].capture++;
	tick_cluster_lists_capture(&tick_list_buffers[which]);
	tick_list_buffers[which].map_generation = halo_map_generation;
	tick_list_captured = which;
	/* (interpolated frames draw the published poses until a tick runs: an
	update that ran none has nothing new to copy) */
	if (halo_interpolation_enabled() && tick_pose_published >= 0 &&
		tick_pose_buffers[tick_pose_published].tick == interpolation_tick &&
		tick_pose_buffers[tick_pose_published].map_generation == halo_map_generation)
	{
		return;
	}
	for (which = 0; which < TICK_POSE_BUFFERS; which++)
		if (which != tick_pose_published && which != tick_pose_previous_index)
			break;
	tick_poses_capture(which);
	if (tick_pose_stats.wanted)
	{
		tick_pose_stats.captures++;
		tick_pose_stats.capture_us += tick_pose_now_us() - before;
	}
}

/* the main thread, as a tick starts (after the camera was placed) */
void render_tick_poses_tick_started(void)
{
	tick_pose_tick_in_flight = TRUE;
}

/* the main thread, at the join: the finished tick's poses are the ones the
next frame draws, and the poses drawn until now, if of an earlier tick,
interpolation's previous */
void render_tick_poses_publish(void)
{
	tick_pose_tick_in_flight = FALSE;
	if (tick_list_captured >= 0)
	{
		tick_list_published = tick_list_captured;
		tick_list_captured = -1;
	}
	if (tick_pose_captured >= 0)
	{
		struct tick_pose_buffer *captured = &tick_pose_buffers[tick_pose_captured];

		if (tick_pose_published < 0 || tick_pose_buffers[tick_pose_published].map_generation != captured->map_generation)
			tick_pose_previous_index = -1;
		else if (tick_pose_buffers[tick_pose_published].tick < captured->tick)
			tick_pose_previous_index = tick_pose_published;
		tick_pose_published = tick_pose_captured;
		tick_pose_captured = -1;
	}
}

static void tick_pose_frame_begin(void)
{
	/* (a game state replaced since the capture - a new map, a revert, a
	saved game - leaves the poses meaningless) */
	boolean published = tick_poses_enabled() && tick_pose_tick_in_flight && tick_pose_published >= 0 &&
		tick_pose_buffers[tick_pose_published].map_generation == halo_map_generation;
	boolean lists_published = tick_poses_enabled() && tick_pose_tick_in_flight && tick_list_published >= 0 &&
		tick_list_buffers[tick_list_published].map_generation == halo_map_generation;

	static int lists_wanted = -1;

	if (lists_wanted < 0)
	{
		/* HALO_TICK_CLUSTER_LISTS=0: the render walks the running tick's
		lists, as before */
		const char *setting = getenv("HALO_TICK_CLUSTER_LISTS");

		lists_wanted = !setting || atoi(setting) != 0;
	}
	tick_pose_rendering = published && !interpolation_rendering;
	tick_cluster_lists_rendering = lists_published && lists_wanted;
}

static void tick_pose_frame_end(void)
{
	tick_pose_rendering = FALSE;
	tick_cluster_lists_rendering = FALSE;
	if (tick_pose_stats.wanted > 0 && ++tick_pose_stats.frames % 300 == 0)
	{
		unsigned long captures = tick_pose_stats.captures ? tick_pose_stats.captures : 1;

		platform_log("tick poses: %lu lookups, %lu of them moved by the tick since (drawn from the poses); "
			"%lu captures, %lu objects / %lu nodes each, %llu us each; interpolation: %lu objects blended, "
			"%lu snapped, %llu us",
			tick_pose_stats.lookups, tick_pose_stats.moved, tick_pose_stats.captures,
			tick_pose_stats.captured_objects / captures, tick_pose_stats.captured_nodes / captures,
			tick_pose_stats.capture_us / captures, tick_pose_stats.blended, tick_pose_stats.snapped,
			tick_pose_stats.blend_us);
		tick_pose_stats.lookups = tick_pose_stats.moved = 0;
		tick_pose_stats.captures = tick_pose_stats.captured_objects = tick_pose_stats.captured_nodes = 0;
		tick_pose_stats.capture_us = 0;
		tick_pose_stats.blended = tick_pose_stats.snapped = 0;
		tick_pose_stats.blend_us = 0;
	}
}

/* the object's entry in the poses the render draws, NULL when it is drawn
live (no tick running, the tick's own reads, an object not kept) */
static struct tick_pose_entry *tick_pose_entry_get(long object_index)
{
	struct tick_pose_buffer *buffer;
	struct tick_pose_entry *entry;
	long absolute_index;

	if (!tick_pose_rendering || object_index == NONE || halo_epoch_on_mutator())
		return NULL;
	buffer = &tick_pose_buffers[tick_pose_published];
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	entry = &buffer->entries[absolute_index];
	if (entry->capture != buffer->capture || entry->object_index != object_index)
		return NULL;
	return entry;
}

/* ---------- interpolated objects */

/* the entry of the object in the buffer, NULL when the buffer has none */
static struct tick_pose_entry *tick_pose_buffer_entry(struct tick_pose_buffer *buffer, long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct tick_pose_entry *entry;

	if (!buffer || !buffer->entries || absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	entry = &buffer->entries[absolute_index];
	if (entry->capture != buffer->capture || entry->object_index != object_index)
		return NULL;
	return entry;
}

static void tick_pose_entry_rotations(struct tick_pose_buffer *buffer, struct tick_pose_entry *entry)
{
	short node_index;

	if (entry->rotations_valid)
		return;
	for (node_index = 0; node_index < entry->node_count; node_index++)
	{
		rotation_from_matrix(&buffer->matrices[entry->first_matrix + node_index],
			&buffer->rotations[entry->first_matrix + node_index]);
	}
	entry->rotations_valid = TRUE;
}

/* the object as this frame draws it, between its two snapshots: NULL when
the latest has none of it (drawn live: an object the tick does not move) */
static struct interpolated_object *interpolated_object_get(long object_index)
{
	struct tick_pose_buffer *latest = tick_pose_latest();
	struct tick_pose_buffer *previous = interpolation_span ? tick_pose_previous() : NULL;
	struct tick_pose_entry *latest_entry, *previous_entry;
	struct interpolated_object *drawn;
	long absolute_index;
	real_matrix4x3 const *latest_nodes;
	real_matrix4x3 *blended;
	real_vector3d correction;
	unsigned long long before;
	boolean snap;
	short node_index;

	if (object_index == NONE || halo_epoch_on_mutator())
		return NULL;
	latest_entry = tick_pose_buffer_entry(latest, object_index);
	if (!latest_entry)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (!interpolated_objects)
	{
		interpolated_objects = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*interpolated_objects));
		if (!interpolated_objects)
			return NULL;
	}
	drawn = &interpolated_objects[absolute_index];
	if (drawn->frame == interpolation_frame && drawn->object_index == object_index)
		return drawn;
	before = tick_pose_stats.wanted ? tick_pose_now_us() : 0;
	drawn->frame = interpolation_frame;
	drawn->object_index = object_index;
	drawn->blended = FALSE;
	drawn->first_matrix = latest_entry->first_matrix;
	drawn->center = latest_entry->center;
	drawn->radius = latest_entry->radius;
	latest_nodes = latest->matrices + latest_entry->first_matrix;
	previous_entry = tick_pose_buffer_entry(previous, object_index);
	if (blend_capacity < latest->matrix_capacity)
	{
		/* (the C library's realloc: see render_tick_poses_capture) */
		real_matrix4x3 *larger = (realloc)(blend_matrices, latest->matrix_capacity * sizeof(real_matrix4x3));

		if (!larger)
			return drawn;
		blend_matrices = larger;
		blend_capacity = latest->matrix_capacity;
	}
	blended = blend_matrices + latest_entry->first_matrix;
	snap = !previous_entry || previous_entry->node_count != latest_entry->node_count || interpolation_blend >= 1.0f;
	/* (an object that stood still between the two: drawn as it is, no
	blend - most of a level's moving kinds stand still most ticks) */
	if (!snap && !correction_significant(&latest_entry->shift) &&
		!memcmp(previous->matrices + previous_entry->first_matrix, latest_nodes,
			latest_entry->node_count * sizeof(real_matrix4x3)))
	{
		snap = TRUE;
	}
	if (!snap)
	{
		real_matrix4x3 const *previous_nodes = previous->matrices + previous_entry->first_matrix;
		real_vector3d const *shift = &latest_entry->shift;
		real_point3d previous_root = previous_nodes[0].position;
		real allowed = OBJECT_SNAP_DISTANCE * (real)interpolation_span;
		real node_allowed = NODE_SNAP_DISTANCE * (real)interpolation_span;

		/* (the previous snapshot where it would have been, had the netcode
		not moved it since) */
		previous_root.x -= shift->i;
		previous_root.y -= shift->j;
		previous_root.z -= shift->k;
		/* (so written that a position not a number snaps) */
		snap = !(distance_squared(&previous_root, &latest_nodes[0].position) <= allowed * allowed);
		/* a node moved further in the root's frame than its ticks allow:
		the two snapshots are different poses, not one moving (OpenCE's
		e4461981) */
		for (node_index = 1; !snap && node_index < latest_entry->node_count; node_index++)
		{
			real_point3d previous_local, latest_local;

			matrix4x3_inverse_transform_point(&previous_nodes[0], &previous_nodes[node_index].position, &previous_local);
			matrix4x3_inverse_transform_point(&latest_nodes[0], &latest_nodes[node_index].position, &latest_local);
			snap = !(distance_squared(&previous_local, &latest_local) <= node_allowed * node_allowed);
		}
		if (!snap)
		{
			struct interpolation_rotation const *previous_rotations, *latest_rotations;

			/* (each snapshot's rotations found once, not every frame) */
			tick_pose_entry_rotations(previous, previous_entry);
			tick_pose_entry_rotations(latest, latest_entry);
			previous_rotations = previous->rotations + previous_entry->first_matrix;
			latest_rotations = latest->rotations + latest_entry->first_matrix;
			for (node_index = 0; node_index < latest_entry->node_count; node_index++)
			{
				real_matrix4x3 moved = previous_nodes[node_index];

				moved.position.x -= shift->i;
				moved.position.y -= shift->j;
				moved.position.z -= shift->k;
				matrix_blend_rotations(&moved, &latest_nodes[node_index], &previous_rotations[node_index],
					&latest_rotations[node_index], interpolation_blend, &blended[node_index]);
			}
			drawn->center.x = lerp(previous_entry->center.x - shift->i, latest_entry->center.x, interpolation_blend);
			drawn->center.y = lerp(previous_entry->center.y - shift->j, latest_entry->center.y, interpolation_blend);
			drawn->center.z = lerp(previous_entry->center.z - shift->k, latest_entry->center.z, interpolation_blend);
			drawn->blended = TRUE;
		}
		else if (tick_pose_stats.wanted)
			tick_pose_stats.snapped++;
	}
	if (correction_drawn(&latest_entry->correction, &correction))
	{
		if (!drawn->blended)
			memcpy(blended, latest_nodes, latest_entry->node_count * sizeof(real_matrix4x3));
		for (node_index = 0; node_index < latest_entry->node_count; node_index++)
		{
			blended[node_index].position.x += correction.i;
			blended[node_index].position.y += correction.j;
			blended[node_index].position.z += correction.k;
		}
		drawn->center.x += correction.i;
		drawn->center.y += correction.j;
		drawn->center.z += correction.k;
		drawn->blended = TRUE;
	}
	if (tick_pose_stats.wanted)
	{
		tick_pose_stats.blended += drawn->blended;
		tick_pose_stats.blend_us += tick_pose_now_us() - before;
	}
	return drawn;
}

/* the object's nodes as the frame draws them: the threaded tick's poses,
blended between two with interpolation; NULL: the live ones */
real_matrix4x3 *render_interpolation_object_node_matrices(long object_index)
{
	struct interpolated_object *drawn;

	if (!interpolation_rendering)
		return tick_pose_node_matrices(object_index);
	drawn = interpolated_object_get(object_index);
	if (!drawn)
		return NULL;
	return (drawn->blended ? blend_matrices : tick_pose_latest()->matrices) + drawn->first_matrix;
}

/* the object's bounding sphere as its pose has it (render_objects.c); FALSE
when it is drawn live */
boolean render_tick_pose_bounding_sphere(long object_index, real_point3d *center, real *radius)
{
	struct tick_pose_entry *entry;

	if (interpolation_rendering)
	{
		struct interpolated_object *drawn = interpolated_object_get(object_index);

		if (!drawn)
			return FALSE;
		*center = drawn->center;
		*radius = drawn->radius;
		return TRUE;
	}
	entry = tick_pose_entry_get(object_index);
	if (!entry)
		return FALSE;
	*center = entry->center;
	*radius = entry->radius;
	return TRUE;
}

static real_matrix4x3 *tick_pose_node_matrices(long object_index)
{
	struct tick_pose_buffer *buffer = &tick_pose_buffers[tick_pose_published < 0 ? 0 : tick_pose_published];
	struct tick_pose_entry *entry = tick_pose_entry_get(object_index);

	if (!entry)
		return NULL;
	if (tick_pose_stats.wanted)
	{
		/* (a racy read of the live matrices, for the count alone) */
		struct object_datum *object = object_get(object_index);

		tick_pose_stats.lookups++;
		if (memcmp(buffer->matrices + entry->first_matrix,
				object_header_block_get(object_index, &object->object.node_matrices),
				entry->node_count * sizeof(real_matrix4x3)))
		{
			tick_pose_stats.moved++;
		}
	}
	return buffer->matrices + entry->first_matrix;
}
