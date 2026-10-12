/*
FIRST_PERSON_LEGS.C

Your own legs in first person (display.first_person_legs, HALO_FIRST_PERSON_LEGS;
the Vita's settings panel: Graphics > Display > First-person legs): the
player's own body, which the game animates as everyone else sees it but does
not draw for its own player, drawn from the waist down. Look down while
standing, running, strafing or crouching and the legs move as the body's
animation moves them.

Only the drawing is changed: a copy of the body's pose, all but its hips and
legs folded into a lid over the waist, the whole set back a little behind
the eye so the camera never looks out through the hips. The body itself, its
hit boxes and markers, are the game's as ever. The legs are drawn in the
world with the world's camera, so they keep their place and size at any
field of view.

From OpenCE (OpenCommunityEdition/OpenCE, CC0): d36c15c3 "Your own legs in
first person, seen looking down (display.first_person_legs)" and f735ca15
"First-person legs: one mesh kept, its waist forgotten on a new map" (#243).
Here:
- Drawn only for the window's own local player's unit (split screen: each
  player's own body in their own view), in the director's first person, on
  foot, alive, not zoomed, not in a cinematic; the unit's lit and drawn as
  any object (render_objects.c), so the Vita's renderer (d3d8_gxm.c) draws
  it as it draws every other model.
- The camera and its frustum are the window's (render.camera, render.frustum)
  rather than the rasterizer's copy of them; the bottom plane by its name.
- The setting is read again when the Vita's settings panel changes it
  (halo_settings_generation), so the row is live.
- A pose kept per local player, so two views of a frame keep their own.
- HALO_FIRST_PERSON_LEGS_LOG=1: a line each time the legs start or stop
  being drawn, and why; the test command @legs, the frames they were drawn
  in (the harness test, port/vita/tests/run_first_person_legs_test.sh).
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "models/model_definitions.h"
#include "units/units.h"
#include "game/players.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "render/render.h"
#include "tag_files/tag_files.h"

#include "object_mesh.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
extern volatile unsigned long halo_settings_generation;
void platform_log(const char *format, ...);
/* port/linux/src (the host's clock, microseconds) */
unsigned long long vita_host_time_us(void);

enum
{
	FIRST_PERSON_LEGS_MAXIMUM_NODES = 64,
	FIRST_PERSON_LEGS_MAXIMUM_RIM = 64,
	FIRST_PERSON_LEGS_LOCAL_PLAYERS = 4,
	/* (render_cameras.c's _render_frustum_plane_bottom) */
	FIRST_PERSON_LEGS_BOTTOM_PLANE = 2,
};

/* how far the body is set back behind the eye at least, world units (about
12 cm), and at most (about 1 m) */
#define FIRST_PERSON_LEGS_SET_BACK 0.04f
#define FIRST_PERSON_LEGS_SET_BACK_MAXIMUM 0.33f
/* how far out of the view the waist's rim is kept (world units: 3 cm) */
#define FIRST_PERSON_LEGS_OUT_OF_VIEW 0.01f
/* the point the upper body folds into: the middle of the waist's rim, a
little above it (world units: about 3 cm), so the skin that joined hips to
chest closes the waist as a low lid, flush with the belt (the body's model is
a hollow shell: folded higher, it stood up as a cone before the eye; lower,
it sank into a funnel) */
#define FIRST_PERSON_LEGS_LID_UP 0.01f

/* why the legs are not drawn this frame (the log's words) */
enum
{
	_first_person_legs_drawn,
	_first_person_legs_hidden_off,
	_first_person_legs_hidden_not_first_person,
	_first_person_legs_hidden_cinematic,
	_first_person_legs_hidden_vehicle,
	_first_person_legs_hidden_dead,
	_first_person_legs_hidden_zoomed,
	_first_person_legs_hidden_no_legs,
	NUMBER_OF_FIRST_PERSON_LEGS_STATES
};

static char const *const first_person_legs_state_names[NUMBER_OF_FIRST_PERSON_LEGS_STATES] =
{
	"drawn", "off", "not first person", "cinematic", "in a vehicle", "dead or hidden", "zoomed", "no legs found",
};

static real_matrix4x3 first_person_legs_matrices[FIRST_PERSON_LEGS_LOCAL_PLAYERS][FIRST_PERSON_LEGS_MAXIMUM_NODES];
/* the frames (a window each) the legs were drawn in, since the start (the
test command @legs says it: run_first_person_legs_test.sh) */
static volatile unsigned long first_person_legs_drawn_count;
/* how far the last pose drawn was set back, and the most since the start */
static real first_person_legs_set_back_last;
static real first_person_legs_set_back_most;

unsigned long first_person_legs_drawn_frames(
	real *set_back_last,
	real *set_back_most)
{
	*set_back_last = first_person_legs_set_back_last;
	*set_back_most = first_person_legs_set_back_most;
	return first_person_legs_drawn_count;
}

/* render_objects.c: the legs' pose went to render_model */
void first_person_legs_drawn(
	void)
{
	first_person_legs_drawn_count++;
}

/* ---------- the setting */

static boolean first_person_legs_enabled(
	void)
{
	static int enabled = -1;
	static unsigned long settings_seen;

	if (enabled < 0)
	{
		settings_seen = halo_settings_generation;
		enabled = config_boolean("display.first_person_legs") != 0;
	}
	else if (settings_seen != halo_settings_generation)
	{
		/* (the Vita's settings panel sets the environment variable, as
		port_config.c reads it: "0", "false", "no" and "off" are false) */
		char const *setting = getenv("HALO_FIRST_PERSON_LEGS");

		settings_seen = halo_settings_generation;
		if (setting)
		{
			enabled = *setting && strcmp(setting, "0") && strcasecmp(setting, "false") &&
				strcasecmp(setting, "no") && strcasecmp(setting, "off");
		}
	}
	return enabled != 0;
}

/* ---------- the log */

static int first_person_legs_log_enabled(
	void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_FIRST_PERSON_LEGS_LOG");

		enabled = setting && atoi(setting) != 0;
	}
	return enabled;
}

static struct
{
	short state;
	unsigned long frames;
} first_person_legs_log[FIRST_PERSON_LEGS_LOCAL_PLAYERS];

/* (HALO_FIRST_PERSON_LEGS_LOG) a line when a local player's legs change
from drawn to hidden or back, or hidden for another reason, with how many
frames the last state lasted */
static void first_person_legs_note(
	short local_player_index,
	short state,
	long object_index,
	real set_back)
{
	if (!first_person_legs_log_enabled() || !VALID_INDEX(local_player_index, FIRST_PERSON_LEGS_LOCAL_PLAYERS))
		return;
	if (first_person_legs_log[local_player_index].frames == 0 || first_person_legs_log[local_player_index].state != state)
	{
		struct object_datum *object = object_try_and_get(object_index);

		platform_log("first-person legs: player %d %s (unit %08lx %s, set back %.3f, mesh %ld bytes; %lu frames %s before)",
			(int)local_player_index, first_person_legs_state_names[state], (unsigned long)object_index,
			object ? tag_name_strip_path(tag_get_name(object->definition_index)) : "none", (double)set_back,
			object_mesh_memory(), first_person_legs_log[local_player_index].frames,
			first_person_legs_state_names[first_person_legs_log[local_player_index].state]);
		first_person_legs_log[local_player_index].state = state;
		first_person_legs_log[local_player_index].frames = 0;
	}
	first_person_legs_log[local_player_index].frames++;
}

/* ---------- which body */

/* the window's local player's own unit, seen from inside it: its legs drawn
(on foot, alive, not hidden, not zoomed, the director's first person, no
cinematic) */
static short first_person_legs_state(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	long player_index = local_player_get_player_index(render.local_player_index);
	struct unit_datum *unit;

	if (!first_person_legs_enabled())
		return _first_person_legs_hidden_off;
	if (!object || object->object.type != _object_type_biped || player_index == NONE ||
		player_get(player_index)->unit_index != object_index ||
		director_get_perspective(render.local_player_index) != _director_perspective_first_person)
	{
		return _first_person_legs_hidden_not_first_person;
	}
	if (cinematic_in_progress())
		return _first_person_legs_hidden_cinematic;
	if (object->object.parent_object_index != NONE)
		return _first_person_legs_hidden_vehicle;
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
		TEST_FLAG(object->object.flags, _object_invisible_bit))
	{
		return _first_person_legs_hidden_dead;
	}
	unit = (struct unit_datum *)object;
	if (unit->unit.current_zoom_level != NONE || unit->unit.desired_zoom_level != NONE)
		return _first_person_legs_hidden_zoomed;
	return _first_person_legs_drawn;
}

boolean first_person_legs_wanted(
	long object_index)
{
	short state = first_person_legs_state(object_index);

	/* (off: nothing noted, nothing changed) */
	if (state != _first_person_legs_drawn && state != _first_person_legs_hidden_off)
		first_person_legs_note(render.local_player_index, state, object_index, 0.f);
	return state == _first_person_legs_drawn;
}

/* whether a node's name has part in it, whatever its case (the game's
characters name theirs "bip01 l thigh", "Bip01 Pelvis") */
static boolean first_person_legs_name_has(
	char const *name,
	char const *part)
{
	char lower[TAG_STRING_LENGTH + 1];
	short index;

	for (index = 0; index < TAG_STRING_LENGTH && name[index]; index++)
		lower[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] - 'A' + 'a' : name[index]);
	lower[index] = 0;
	return strstr(lower, part) != NULL;
}

/* a node of the legs: the hips, thighs, calves, feet and toes */
static boolean first_person_legs_node(
	char const *name)
{
	static char const *const parts[] = { "pelvis", "thigh", "calf", "foot", "toe", "leg" };
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (first_person_legs_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* which of a model's nodes are the legs', and its pelvis and thighs, kept
for the model they were found for (by name: read once a model, not every
frame; a new map forgets them, first_person_legs_reset) */
static struct
{
	long model_index;
	short pelvis;
	short thighs;
	boolean legs[FIRST_PERSON_LEGS_MAXIMUM_NODES];
} first_person_legs_nodes = { NONE };

static void first_person_legs_nodes_find(
	long model_index,
	struct model const *model)
{
	short count = (short)MIN(model->nodes.count, FIRST_PERSON_LEGS_MAXIMUM_NODES);
	short index;

	if (first_person_legs_nodes.model_index == model_index)
		return;
	first_person_legs_nodes.model_index = model_index;
	first_person_legs_nodes.pelvis = NONE;
	first_person_legs_nodes.thighs = 0;
	memset(first_person_legs_nodes.legs, 0, sizeof(first_person_legs_nodes.legs));
	for (index = 0; index < count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		if (first_person_legs_nodes.pelvis == NONE && first_person_legs_name_has(node->name, "pelvis"))
			first_person_legs_nodes.pelvis = index;
		if (first_person_legs_name_has(node->name, "thigh"))
			first_person_legs_nodes.thighs++;
		first_person_legs_nodes.legs[index] = first_person_legs_node(node->name);
	}
}

/* ---------- the waist */

/* the middle of the waist's rim in the model's space: the hips' vertices
on triangles that run up into the body above them (FALSE: none), kept for
the mesh it was found in (a new map forgets it: first_person_legs_reset) */
static real_point3d first_person_legs_rim[FIRST_PERSON_LEGS_MAXIMUM_RIM];
static short first_person_legs_rim_count = 0;
static long first_person_legs_waist_key = NONE;
static long first_person_legs_waist_model = NONE;
static boolean first_person_legs_waist_found = FALSE;
static real_point3d first_person_legs_waist_middle;

/* a new map (objects_initialize_for_new_map): the waist found, and the mesh
it was found in, forgotten (the new map's models have indices of their own) */
void first_person_legs_reset(
	void)
{
	first_person_legs_waist_key = NONE;
	first_person_legs_waist_model = NONE;
	first_person_legs_rim_count = 0;
	first_person_legs_nodes.model_index = NONE;
	memset(first_person_legs_log, 0, sizeof(first_person_legs_log));
	object_mesh_reset();
}

static boolean first_person_legs_waist(
	struct object_mesh const *mesh,
	struct model const *model,
	short pelvis,
	real_point3d *middle)
{
	real_point3d sum = { 0.f, 0.f, 0.f };
	long found = 0;
	long index;

	if (mesh->key == first_person_legs_waist_key && mesh->model_index == first_person_legs_waist_model)
	{
		*middle = first_person_legs_waist_middle;
		return first_person_legs_waist_found;
	}
	first_person_legs_rim_count = 0;
	for (index = 0; index < mesh->triangle_count; index++)
	{
		boolean hips = FALSE;
		boolean above = FALSE;
		short corner;

		for (corner = 0; corner < 3; corner++)
		{
			struct object_mesh_vertex const *vertex = &mesh->vertices[mesh->triangles[index][corner]];
			short n;

			if (vertex->nodes[0] == pelvis && (vertex->nodes[1] == pelvis || vertex->weight >= 32767))
				hips = TRUE;
			for (n = 0; n < 2; n++)
			{
				if (vertex->nodes[n] < MIN(model->nodes.count, FIRST_PERSON_LEGS_MAXIMUM_NODES) &&
					(n == 0 || vertex->weight < 32767) && !first_person_legs_nodes.legs[vertex->nodes[n]])
				{
					above = TRUE;
				}
			}
		}
		if (!hips || !above)
			continue;
		for (corner = 0; corner < 3; corner++)
		{
			struct object_mesh_vertex const *vertex = &mesh->vertices[mesh->triangles[index][corner]];

			if (vertex->nodes[0] == pelvis && (vertex->nodes[1] == pelvis || vertex->weight >= 32767))
			{
				sum.x += vertex->position.x;
				sum.y += vertex->position.y;
				sum.z += vertex->position.z;
				found++;
				if (first_person_legs_rim_count < FIRST_PERSON_LEGS_MAXIMUM_RIM)
					first_person_legs_rim[first_person_legs_rim_count++] = vertex->position;
			}
		}
	}
	first_person_legs_waist_key = mesh->key;
	first_person_legs_waist_model = mesh->model_index;
	first_person_legs_waist_found = found > 0;
	if (found > 0)
	{
		first_person_legs_waist_middle.x = sum.x / found;
		first_person_legs_waist_middle.y = sum.y / found;
		first_person_legs_waist_middle.z = sum.z / found + FIRST_PERSON_LEGS_LID_UP;
	}
	*middle = first_person_legs_waist_middle;
	return first_person_legs_waist_found;
}

/* ---------- the pose */

/* the pose to draw the legs with: the body's own (as it is drawn now), all
but the legs folded into the hips, the whole set back behind the eye; NULL:
none (a body without named legs) */
real_matrix4x3 const *first_person_legs_node_matrices(
	long object_index,
	real_matrix4x3 const *nodes)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition *definition;
	struct model const *model;
	real_matrix4x3 *matrices;
	long count;
	short pelvis;
	short index;
	real_point3d fold;
	real_vector3d back;
	real_matrix4x3 skin;
	real set_back;

	if (!object || !nodes || !VALID_INDEX(render.local_player_index, FIRST_PERSON_LEGS_LOCAL_PLAYERS))
		goto none;
	matrices = first_person_legs_matrices[render.local_player_index];
	definition = object_definition_get(object->definition_index);
	if (!definition || definition->object.model.index == NONE)
		goto none;
	model = model_definition_get(definition->object.model.index);
	count = object->object.node_matrices.size / (long)sizeof(real_matrix4x3);
	if (!model || count <= 0 || count > FIRST_PERSON_LEGS_MAXIMUM_NODES || model->nodes.count < count)
		goto none;

	first_person_legs_nodes_find(definition->object.model.index, model);
	pelvis = first_person_legs_nodes.pelvis;
	if (pelvis == NONE || first_person_legs_nodes.thighs == 0)
		goto none;
	/* (the waist's middle as the hips are posed now) */
	{
		unsigned long long started = first_person_legs_log_enabled() ? vita_host_time_us() : 0;
		long waist_key = first_person_legs_waist_key;
		struct object_mesh const *mesh = object_mesh_get(object_index);
		real_point3d middle;
		boolean found = mesh && pelvis < mesh->node_count && first_person_legs_waist(mesh, model, pelvis, &middle);

		/* (the mesh read and the waist found again: how long that took,
		once a model) */
		if (started && first_person_legs_waist_key != waist_key)
		{
			platform_log("first-person legs: %s's waist %s in %.2f ms (%ld vertices, %ld triangles, a rim of %d)",
				tag_name_strip_path(tag_get_name(definition->object.model.index)), found ? "found" : "not found",
				(double)(vita_host_time_us() - started) / 1000.0, mesh ? mesh->vertex_count : 0L,
				mesh ? mesh->triangle_count : 0L, (int)first_person_legs_rim_count);
		}
		if (!found)
			goto none;
		matrix4x3_multiply(&nodes[pelvis], &mesh->inverse[pelvis], &skin);
		matrix4x3_transform_point(&skin, &middle, &fold);
	}

	/* (set back away from the way the eye looks, level) */
	back.i = -render.camera.forward.i;
	back.j = -render.camera.forward.j;
	back.k = 0.f;
	if (normalize3d(&back) <= 0.f)
		back = *global_zero_vector3d;

	/* (as far back as keeps the waist's rim out of the bottom of the view,
	as Halo 2 keeps its first person body's open top out of it: looking
	ahead, the least; looking down, the legs stand behind the eye, the
	thighs, knees and feet seen as they swing forward) */
	set_back = FIRST_PERSON_LEGS_SET_BACK;
	{
		real_plane3d const *bottom = &render.frustum.world_planes[FIRST_PERSON_LEGS_BOTTOM_PLANE];
		real toward = dot_product3d(&bottom->n, &back);
		short corner;

		if (toward > 0.05f)
		{
			for (corner = 0; corner < first_person_legs_rim_count; corner++)
			{
				real_point3d p;
				real distance;

				matrix4x3_transform_point(&skin, &first_person_legs_rim[corner], &p);
				distance = plane3d_distance_to_point(bottom, &p) + toward * FIRST_PERSON_LEGS_SET_BACK;
				if (distance < FIRST_PERSON_LEGS_OUT_OF_VIEW)
				{
					real needed = FIRST_PERSON_LEGS_SET_BACK + (FIRST_PERSON_LEGS_OUT_OF_VIEW - distance) / toward;

					if (needed > set_back)
						set_back = needed;
				}
			}
		}
		if (set_back > FIRST_PERSON_LEGS_SET_BACK_MAXIMUM)
			set_back = FIRST_PERSON_LEGS_SET_BACK_MAXIMUM;
	}

	for (index = 0; index < count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);
		real_matrix4x3 *matrix = &matrices[index];

		*matrix = nodes[index];
		/* (the root kept: it is the body's frame, not a part of it) */
		if (node->parent_node_index != NONE && !first_person_legs_nodes.legs[index])
		{
			matrix->position = fold;
			matrix->scale = 0.001f;
		}
		matrix->position.x += back.i * set_back;
		matrix->position.y += back.j * set_back;
		matrix->position.z += back.k * set_back;
	}
	first_person_legs_note(render.local_player_index, _first_person_legs_drawn, object_index, set_back);
	first_person_legs_set_back_last = set_back;
	if (set_back > first_person_legs_set_back_most)
		first_person_legs_set_back_most = set_back;
	return matrices;

none:
	first_person_legs_note(render.local_player_index, _first_person_legs_hidden_no_legs, object_index, 0.f);
	return NULL;
}
