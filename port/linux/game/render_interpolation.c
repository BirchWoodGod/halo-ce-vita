/*
RENDER_INTERPOLATION.C

Frames between the game's 30 Hz ticks, for the native ports (port/linux,
port/android, port/windows; see port/linux/README.md, "Frame rate").

The game simulates in 30 Hz ticks and originally drew one frame per tick.
The ports draw at the display's refresh rate instead, and every frame shows
the world between the last two ticks: after each tick the camera, every
object's node matrices and the first-person weapon's pose are kept, and a
frame blends the previous and the latest by how far the game clock has run
into the next tick. That puts what is drawn one tick (33 ms) behind the
simulation, the usual price of interpolation. (A Catmull-Rom spline would
also need the tick after the pair it spans: two ticks behind.)

Rotations are blended as quaternions (normalised lerp, taking the shorter
way round), positions and scales linearly. Anything that moves further than
a tick of motion plausibly allows (teleports, respawns, camera cuts) snaps
instead of sweeping across the world.

Particles, contrails and other effects already move every frame
(game_frame), so they need nothing here.

An object the distributed netcode moves to where the host has it
(port/linux/game/network_objects.c) is drawn gliding there over a few ticks
rather than jumping: its snapshots move with it, and the difference is drawn
on top of it, fading each tick.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "camera/observer.h"
#include "game/players.h"
#include "render/render_cameras.h"
#include "render_epoch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

/* ---------- constants */

#define MAXIMUM_INTERPOLATED_OBJECTS (MAXIMUM_OBJECTS_PER_MAP * 5)
#define MAXIMUM_INTERPOLATED_NODES 64

/* world units (10 feet each) a node may move in one tick before it snaps:
well beyond any vehicle, short of any teleport */
#define OBJECT_SNAP_DISTANCE 10.0f
/* a correction's difference left drawn after each tick (of 1) */
#define CORRECTION_DECAY 0.6f
/* ... and small enough to be none */
#define CORRECTION_NEGLIGIBLE 0.001f
/* a camera cut: a jump or turn no player or scripted camera makes in 33 ms */
#define CAMERA_CUT_DISTANCE 3.0f
#define CAMERA_CUT_COSINE 0.5f

/* ---------- structures */

struct interpolation_quaternion
{
	real i, j, k, w;
};

struct interpolated_object
{
	long object_index; /* NONE when unused */
	long tick; /* the tick of the latest snapshot */
	short node_count;
	short node_capacity;
	boolean has_previous;
	byte latest; /* which snapshot is the latest */
	long blended_frame;
	/* where it is drawn from where it is: a correction fading */
	real_vector3d correction;
	/* [0] and [1]: the two snapshots, [2]: the blend drawn this frame */
	real_matrix4x3 *nodes;
};

struct interpolated_camera
{
	long tick;
	boolean valid;
	boolean has_previous;
	struct observer_result previous;
	struct observer_result latest;
	struct observer_result blended;
};

struct interpolated_first_person
{
	long tick;
	short node_count;
	boolean has_previous;
	real_matrix4x3 previous[MAXIMUM_INTERPOLATED_NODES];
	real_matrix4x3 latest[MAXIMUM_INTERPOLATED_NODES];
};

/* ---------- globals */

static struct interpolated_object *interpolated_objects;
static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
static struct interpolated_first_person interpolated_first_person[MAXIMUM_LOCAL_PLAYERS];
static long interpolation_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;

static real_matrix4x3 *tick_pose_node_matrices(long object_index);
static void tick_pose_frame_begin(void);
static void tick_pose_frame_end(void);

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

/* a matrix a fraction t of the way from a to b */
static void matrix_blend(real_matrix4x3 const *a, real_matrix4x3 const *b, real t, real_matrix4x3 *result)
{
	result->scale = lerp(a->scale, b->scale, t);
	point_lerp(&a->position, &b->position, t, &result->position);
	if (basis_is_rotation(a) && basis_is_rotation(b))
	{
		struct interpolation_quaternion qa, qb, q;
		real length;

		quaternion_from_basis(a, &qa);
		quaternion_from_basis(b, &qb);
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

/* ---------- ticks */

void render_interpolation_tick(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long previous_tick = interpolation_tick++;

	if (!halo_interpolation_enabled())
		return;
	if (!interpolated_objects)
	{
		long index;

		interpolated_objects = calloc(MAXIMUM_INTERPOLATED_OBJECTS, sizeof(*interpolated_objects));
		if (!interpolated_objects)
			return;
		for (index = 0; index < MAXIMUM_INTERPOLATED_OBJECTS; index++)
			interpolated_objects[index].object_index = NONE;
	}

	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		struct interpolated_object *record;
		short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
		boolean continuing;

		if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
			continue;
		record = &interpolated_objects[absolute_index];
		if (node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
		{
			record->object_index = NONE;
			continue;
		}
		if (record->node_capacity < node_count)
		{
			real_matrix4x3 *nodes = realloc(record->nodes, 3 * node_count * sizeof(real_matrix4x3));

			if (!nodes)
			{
				record->object_index = NONE;
				continue;
			}
			record->nodes = nodes;
			record->node_capacity = node_count;
			record->object_index = NONE; /* the old snapshots moved */
		}
		continuing = record->object_index == iterator.index &&
			record->node_count == node_count &&
			record->tick == previous_tick;
		if (continuing)
		{
			record->latest ^= 1;
			record->correction.i *= CORRECTION_DECAY;
			record->correction.j *= CORRECTION_DECAY;
			record->correction.k *= CORRECTION_DECAY;
			if (fabs(record->correction.i) + fabs(record->correction.j) + fabs(record->correction.k) < CORRECTION_NEGLIGIBLE)
				record->correction = *global_zero_vector3d;
		}
		else
		{
			record->correction = *global_zero_vector3d;
		}
		memcpy(
			record->nodes + record->latest * record->node_capacity,
			object_get_node_matrices(iterator.index),
			node_count * sizeof(real_matrix4x3));
		record->object_index = iterator.index;
		record->node_count = node_count;
		record->tick = interpolation_tick;
		record->has_previous = continuing;
		record->blended_frame = NONE;
	}
}

/* ---------- frames */

void render_interpolation_frame_begin(void)
{
	interpolation_rendering = halo_interpolation_enabled();
	interpolation_frame++;
	interpolation_fraction = game_time_get_tick_fraction();
	tick_pose_frame_begin();
}

void render_interpolation_frame_end(void)
{
	interpolation_rendering = FALSE;
	tick_pose_frame_end();
}

real render_interpolation_fraction(void)
{
	return interpolation_rendering ? interpolation_fraction : 1.0f;
}

real_matrix4x3 *render_interpolation_object_node_matrices(long object_index)
{
	struct interpolated_object *record;
	long absolute_index;

	if (!interpolation_rendering)
		return tick_pose_node_matrices(object_index);
	if (!interpolated_objects || object_index == NONE)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return NULL;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != object_index || record->tick != interpolation_tick || !record->has_previous)
		return NULL;
	if (record->blended_frame != interpolation_frame)
	{
		real_matrix4x3 const *previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
		real_matrix4x3 const *latest = record->nodes + record->latest * record->node_capacity;
		real_matrix4x3 *blended = record->nodes + 2 * record->node_capacity;
		short node_index;

		if (distance_squared(&previous[0].position, &latest[0].position) >
			OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE)
		{
			memcpy(blended, latest, record->node_count * sizeof(real_matrix4x3));
		}
		else
		{
			for (node_index = 0; node_index < record->node_count; node_index++)
				matrix_blend(&previous[node_index], &latest[node_index], interpolation_fraction, &blended[node_index]);
		}
		/* (a correction fading through the tick as it does tick to tick) */
		if (record->correction.i != 0.0f || record->correction.j != 0.0f || record->correction.k != 0.0f)
		{
			real fade = lerp(1.0f, CORRECTION_DECAY, interpolation_fraction);

			for (node_index = 0; node_index < record->node_count; node_index++)
			{
				blended[node_index].position.x += record->correction.i * fade;
				blended[node_index].position.y += record->correction.j * fade;
				blended[node_index].position.z += record->correction.k * fade;
			}
		}
		record->blended_frame = interpolation_frame;
	}
	return record->nodes + 2 * record->node_capacity;
}

/* ---------- corrections */

/* the object (and what it carries) moved by the netcode from where it was,
offset from where it is now: drawn from there, gliding */
void render_interpolation_correct_object(long object_index, real_vector3d const *offset)
{
	struct object_datum *object;
	long child_index;
	long absolute_index;

	if (!interpolated_objects || object_index == NONE ||
		offset->i * offset->i + offset->j * offset->j + offset->k * offset->k > OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE)
	{
		return;
	}
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < MAXIMUM_INTERPOLATED_OBJECTS &&
		interpolated_objects[absolute_index].object_index == object_index)
	{
		struct interpolated_object *record = &interpolated_objects[absolute_index];
		short snapshot;
		short node_index;

		/* the snapshots where it would have been, the difference drawn */
		for (snapshot = 0; snapshot < 2; snapshot++)
		{
			real_matrix4x3 *nodes = record->nodes + snapshot * record->node_capacity;

			for (node_index = 0; node_index < record->node_count; node_index++)
			{
				nodes[node_index].position.x -= offset->i;
				nodes[node_index].position.y -= offset->j;
				nodes[node_index].position.z -= offset->k;
			}
		}
		record->correction.i += offset->i;
		record->correction.j += offset->j;
		record->correction.k += offset->k;
		record->blended_frame = NONE;
	}
	object = object_get(object_index);
	for (child_index = object->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		render_interpolation_correct_object(child_index, offset);
	}
}

/* ---------- camera */

struct observer_result const *render_interpolation_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	struct interpolated_camera *camera;
	real t = interpolation_fraction;

	if (!interpolation_rendering || !observer ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
	{
		return observer;
	}
	camera = &interpolated_cameras[local_player_index];
	/* the observer as it stood after each tick (the first frame drawn
	after the tick) */
	if (!camera->valid || camera->tick != interpolation_tick)
	{
		camera->has_previous = camera->valid;
		camera->previous = camera->latest;
		camera->latest = *observer;
		camera->tick = interpolation_tick;
		camera->valid = TRUE;
	}
	if (!camera->has_previous ||
		distance_squared(&camera->previous.position, &camera->latest.position) >
			CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE ||
		camera->previous.forward.i * camera->latest.forward.i +
			camera->previous.forward.j * camera->latest.forward.j +
			camera->previous.forward.k * camera->latest.forward.k < CAMERA_CUT_COSINE)
	{
		return observer;
	}

	camera->blended = camera->latest;
	point_lerp(&camera->previous.position, &camera->latest.position, t, &camera->blended.position);
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
	short node_index;

	if (!interpolation_rendering ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
	{
		return;
	}
	first_person = &interpolated_first_person[local_player_index];
	matrix4x3_from_point_and_vectors(&camera_matrix, &camera->position, &camera->forward, &camera->up);
	matrix4x3_inverse(&camera_matrix, &inverse_camera);
	if (first_person->tick != interpolation_tick)
	{
		/* the pose drawn last, at the end of the previous tick */
		first_person->has_previous = first_person->node_count == node_count;
		memcpy(first_person->previous, first_person->latest, sizeof(first_person->previous));
		first_person->tick = interpolation_tick;
	}
	for (node_index = 0; node_index < node_count; node_index++)
		matrix4x3_multiply(&inverse_camera, &node_matrices[node_index], &first_person->latest[node_index]);
	first_person->node_count = node_count;
	if (!first_person->has_previous)
		return;
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 blended;

		matrix_blend(
			&first_person->previous[node_index],
			&first_person->latest[node_index],
			interpolation_fraction,
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
	time = ((real)ticks - 1.0f + interpolation_fraction) * (1.0f / TICKS_PER_SECOND);
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
node matrices into one of two buffers, and the join publishes that buffer.
The next frame's render, drawn while the following tick runs, reads the
published buffer, which that tick does not write (it fills the other), so
every object is drawn as it stood when the camera was placed. The tick's
own reads, and the frames drawn with no tick running, use the live
matrices as before.

HALO_TICK_POSES=0 turns this off; HALO_TICK_POSES_STATS=1 logs, every 300
frames, how many objects drawn from the poses the tick had already changed
(each one a draw that would have been out of step) and what the copies
cost. */

struct tick_pose_entry
{
	long object_index;
	long first_matrix;
	short node_count;
	/* the buffer's capture this entry is from: older ones are stale */
	unsigned long capture;
	/* the bounding sphere then (the detail level, the shadow, the sorting
	and fog centroid are taken from it) */
	real_point3d center;
	real radius;
};

struct tick_pose_buffer
{
	/* by absolute object index */
	struct tick_pose_entry *entries;
	real_matrix4x3 *matrices;
	long matrix_capacity;
	unsigned long capture;
	unsigned long map_generation;
};

static struct tick_pose_buffer tick_pose_buffers[2];
/* the buffer the render reads (-1: none yet), and the one the tick
thread filled since the last join (-1: none) */
static int tick_pose_published = -1, tick_pose_captured = -1;
/* a tick was started after the camera was placed and is not yet joined */
static boolean tick_pose_tick_in_flight;
static boolean tick_pose_rendering;

static struct
{
	int wanted;
	unsigned long frames, lookups, moved;
	unsigned long captures, captured_objects, captured_nodes;
	unsigned long long capture_us;
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

/* the tick thread, after game_time_update: the objects as the tick left
them, into the buffer the render is not reading */
void render_tick_poses_capture(void)
{
	struct tick_pose_buffer *buffer;
	struct object_iterator iterator;
	struct object_datum *object;
	unsigned long long before;
	long used = 0;
	int which;

	if (!tick_poses_enabled() || halo_interpolation_enabled())
		return;
	before = tick_pose_stats.wanted ? tick_pose_now_us() : 0;
	which = tick_pose_published == 0 ? 1 : 0;
	buffer = &tick_pose_buffers[which];
	if (!buffer->entries)
	{
		buffer->entries = calloc(MAXIMUM_OBJECTS_PER_MAP, sizeof(*buffer->entries));
		if (!buffer->entries)
			return;
	}
	buffer->capture++;
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
		the objects and nodes of a level, kept out of the copy) */
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

			if (capacity < used + node_count)
				capacity = used + node_count;
			/* (the C library's realloc, not cseries.h's tracked one, whose
			list the main thread may be changing) */
			matrices = (realloc)(buffer->matrices, capacity * sizeof(real_matrix4x3));
			if (!matrices)
				break; /* (the objects left out are drawn live) */
			buffer->matrices = matrices;
			buffer->matrix_capacity = capacity;
		}
		memcpy(buffer->matrices + used,
			object_header_block_get(iterator.index, &object->object.node_matrices),
			node_count * sizeof(real_matrix4x3));
		entry = &buffer->entries[absolute_index];
		entry->object_index = iterator.index;
		entry->first_matrix = used;
		entry->node_count = node_count;
		entry->capture = buffer->capture;
		entry->center = object->object.bounding_sphere_center;
		entry->radius = object->object.bounding_sphere_radius;
		used += node_count;
		if (tick_pose_stats.wanted)
			tick_pose_stats.captured_objects++;
	}
	buffer->map_generation = halo_map_generation;
	tick_pose_captured = which;
	if (tick_pose_stats.wanted)
	{
		tick_pose_stats.captures++;
		tick_pose_stats.captured_nodes += used;
		tick_pose_stats.capture_us += tick_pose_now_us() - before;
	}
}

/* the main thread, as a tick starts (after the camera was placed) */
void render_tick_poses_tick_started(void)
{
	tick_pose_tick_in_flight = TRUE;
}

/* the main thread, at the join: the finished tick's poses are the ones the
next frame draws */
void render_tick_poses_publish(void)
{
	tick_pose_tick_in_flight = FALSE;
	if (tick_pose_captured >= 0)
	{
		tick_pose_published = tick_pose_captured;
		tick_pose_captured = -1;
	}
}

static void tick_pose_frame_begin(void)
{
	/* (a game state replaced since the capture - a new map, a revert, a
	saved game - leaves the poses meaningless) */
	tick_pose_rendering = tick_poses_enabled() && !interpolation_rendering && tick_pose_tick_in_flight &&
		tick_pose_published >= 0 && tick_pose_buffers[tick_pose_published].map_generation == halo_map_generation;
}

static void tick_pose_frame_end(void)
{
	tick_pose_rendering = FALSE;
	if (tick_pose_stats.wanted > 0 && ++tick_pose_stats.frames % 300 == 0)
	{
		unsigned long captures = tick_pose_stats.captures ? tick_pose_stats.captures : 1;

		platform_log("tick poses: %lu lookups, %lu of them moved by the tick since (drawn from the poses); "
			"%lu captures, %lu objects / %lu nodes each, %llu us each",
			tick_pose_stats.lookups, tick_pose_stats.moved, tick_pose_stats.captures,
			tick_pose_stats.captured_objects / captures, tick_pose_stats.captured_nodes / captures,
			tick_pose_stats.capture_us / captures);
		tick_pose_stats.lookups = tick_pose_stats.moved = 0;
		tick_pose_stats.captures = tick_pose_stats.captured_objects = tick_pose_stats.captured_nodes = 0;
		tick_pose_stats.capture_us = 0;
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

/* the object's bounding sphere as its pose has it (render_objects.c); FALSE
when it is drawn live */
boolean render_tick_pose_bounding_sphere(long object_index, real_point3d *center, real *radius)
{
	struct tick_pose_entry *entry = tick_pose_entry_get(object_index);

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
