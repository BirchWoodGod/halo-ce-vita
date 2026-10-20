/*
FORGE_BLOCKS.C

Forge's building blocks in the game (forge_blocks.h). The pack is the
loaded map's own, blocks_<map> (its recipe made here: "pack blocks_<map>",
"map <map>", "blocks"), so each map's blocks wear that map's bitmaps and
every machine with the same map makes the same pack (scene_pack_blocks.c:
byte for byte, a multiplayer Forge game's objects named by their tag paths
alike on all of them). It is loaded before Forge's other packs, so that its
tags have the same indices wherever the same packs are chosen. The tag paths
are the same on every map: a map variant names its blocks by them, and they
are found again in the map's own pack when it loads.
*/

/* ---------- headers */

#include "cseries.h"
#include "real_math.h"
#include "camera/observer.h"
#include "game/players.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "tag_files/tag_groups.h"

#include "forge_blocks.h"
#include "scene_pack.h"
#include "scene_pack_blocks.h"
#include "scene_packs.h"
#include "../src/lang.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);

/* ---------- constants */

enum
{
	/* "blocks_" and a map's name: within a pack's name */
	FORGE_BLOCKS_MAP_NAME_SIZE = SCENE_PACK_NAME_SIZE - 8,
};

/* ---------- globals */

/* the blocks' names in the browser (scene_pack_blocks.c's order) */
static struct
{
	char const *leaf;
	char const *name;
} const forge_block_names[] =
{
	{ "block_small", N_("Small Block") },
	{ "block", N_("Block") },
	{ "block_large", N_("Large Block") },
	{ "wall_small", N_("Small Wall") },
	{ "wall", N_("Wall") },
	{ "wall_large", N_("Large Wall") },
	{ "platform_small", N_("Small Platform") },
	{ "platform", N_("Platform") },
	{ "platform_large", N_("Large Platform") },
	{ "bridge", N_("Bridge") },
	{ "bridge_long", N_("Long Bridge") },
	{ "column", N_("Column") },
	{ "column_tall", N_("Tall Column") },
	{ "ramp_small", N_("Small Ramp") },
	{ "ramp", N_("Ramp") },
	{ "ramp_shallow", N_("Vehicle Ramp") },
	{ "corner", N_("Corner") },
};

/* ---------- private code */

/* a map's short name ([a-z0-9_], from a scenario's path or a lobby's) made
the pack's name and recipe: FALSE if it cannot be one */
static boolean forge_blocks_recipe(
	char const *map,
	char *name,
	int name_size)
{
	char short_name[FORGE_BLOCKS_MAP_NAME_SIZE];
	char const *slash = map ? strrchr(map, '\\') : NULL;
	char recipe[160];
	int index;

	if (!map)
		return FALSE;
	snprintf(short_name, sizeof(short_name), "%s", slash ? slash + 1 : map);
	if (!short_name[0] || strlen(slash ? slash + 1 : map) >= sizeof(short_name))
		return FALSE;
	for (index = 0; short_name[index]; index++)
	{
		char character = short_name[index];

		if (character >= 'A' && character <= 'Z')
			short_name[index] = character = (char)(character - 'A' + 'a');
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_'))
			return FALSE;
	}
	snprintf(name, (size_t)name_size, "blocks_%s", short_name);
	snprintf(recipe, sizeof(recipe), "# Forge's building blocks (forge_blocks.c)\npack %s\nmap %s\nblocks\n", name,
		short_name);
	return scene_packs_recipe_register(name, recipe);
}

/* ---------- public code */

boolean forge_blocks_load(
	char const *scenario)
{
	char name[SCENE_PACK_NAME_SIZE];
	char why[SCENE_PACK_WHY_SIZE];
	long progress;

	if (!forge_blocks_recipe(scenario, name, sizeof(name)))
	{
		platform_log("forge: blocks: not on this map");
		return FALSE;
	}
	if (!scene_packs_recipe_available(name, why, sizeof(why)))
	{
		platform_log("forge: blocks: %s unavailable: %s", name, why);
		return FALSE;
	}
	if (scene_packs_load_now(name))
	{
		platform_log("forge: blocks: %s loaded (%lu KB of the tag cache held by packs now)", name,
			scene_packs_reserved_bytes() / 1024);
		return TRUE;
	}
	scene_packs_status(name, &progress, why, sizeof(why));
	platform_log("forge: blocks: %s left out: %s", name, why[0] ? why : "it could not be loaded");
	return FALSE;
}

void forge_blocks_prebuild(
	char const *map_name)
{
	/* (the lobby's map asked for once, as it is chosen: the lobby asks
	every frame, and whether the map is there reads its file) */
	static char asked[FORGE_BLOCKS_MAP_NAME_SIZE];
	char name[SCENE_PACK_NAME_SIZE];

	if (!map_name || !*map_name || strlen(map_name) >= sizeof(asked) || !strcmp(asked, map_name))
		return;
	snprintf(asked, sizeof(asked), "%s", map_name);
	if (!forge_blocks_recipe(map_name, name, sizeof(name)) || !scene_packs_recipe_available(name, NULL, 0) ||
		scene_packs_status(name, NULL, NULL, 0) != _scene_pack_state_none)
	{
		return;
	}
	platform_log("forge: blocks: %s wanted for the lobby: %s", name, scene_packs_state_name(scene_packs_prebuild(name)));
}

short forge_blocks_of_tag(
	long tag_index)
{
	char const *name;
	char const *owner;

	if (tag_index == NONE || tag_get_group_tag(tag_index) != 'scen' || !(name = tag_get_name(tag_index)) ||
		strncmp(name, SCENE_PACK_BLOCKS_PATH_PREFIX, sizeof(SCENE_PACK_BLOCKS_PATH_PREFIX) - 1) ||
		!(owner = scene_packs_owner(tag_index)) || strncmp(owner, "blocks_", 7))
	{
		return NONE;
	}
	return scene_pack_blocks_find(name);
}

void forge_blocks_name(
	short block,
	char *name,
	int size)
{
	struct scene_pack_block_info const *info = scene_pack_blocks_info(block);
	char const *leaf = info ? info->path + sizeof(SCENE_PACK_BLOCKS_PATH_PREFIX) - 1 : "";
	short index;

	for (index = 0; index < (short)NUMBEROF(forge_block_names); index++)
	{
		if (!strcmp(forge_block_names[index].leaf, leaf))
		{
			snprintf(name, (size_t)size, "%s", T(forge_block_names[index].name));
			return;
		}
	}
	snprintf(name, (size_t)size, "%s", leaf);
}

void forge_blocks_snap(
	long object_index,
	real grid,
	real_point3d *position,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct scene_pack_block_info const *info;
	real_vector3d left;
	short axis;

	if (!object || grid <= 0.0f ||
		!(info = scene_pack_blocks_info(forge_blocks_of_tag(object->definition_index))))
	{
		return;
	}
	cross_product3d(up, forward, &left);
	for (axis = 0; axis < 3; axis++)
	{
		/* (the block's half extent along the world's axis as it is turned:
		its corner, not its middle, on the grid) */
		real half = 0.5f * (info->size[0] * (real)fabs(forward->n[axis]) + info->size[1] * (real)fabs(left.n[axis]) +
			info->size[2] * (real)fabs(up->n[axis]));
		real corner = (position->n[axis] - half) / grid;
		real snapped = corner >= 0.0f ? (real)(long)(corner + 0.5f) : -(real)(long)(-corner + 0.5f);

		position->n[axis] = snapped * grid + half;
	}
}

/* (tests) the console's "forge blocks ...": "ray" - a projectile's line
(collision_test_line, the projectiles' flags) 50 world units along the
first local player's view (the monitor's or the Spartan's), and what it
meets; "projectiles" - each projectile in the world and where */
boolean forge_blocks_command(
	char const *arguments)
{
	if (!strcmp(arguments, "ray"))
	{
		struct observer_result const *camera = observer_get_camera(0);
		long player_index = local_player_get_player_index(0);
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
		struct collision_result collision;
		real_point3d end;

		if (!camera)
			return FALSE;
		end.x = camera->position.x + camera->forward.i * 50.0f;
		end.y = camera->position.y + camera->forward.j * 50.0f;
		end.z = camera->position.z + camera->forward.k * 50.0f;
		if (!collision_test_line(_collision_test_for_projectiles_flags, &camera->position, &end, unit_index, &collision))
		{
			platform_log("forge: blocks: ray from %.2f %.2f %.2f meets nothing", camera->position.x, camera->position.y,
				camera->position.z);
			return TRUE;
		}
		platform_log("forge: blocks: ray from %.2f %.2f %.2f meets %s %s at %.3f %.3f %.3f, its plane %.3f %.3f %.3f, "
			"material %d", camera->position.x, camera->position.y, camera->position.z,
			collision.type == _collision_result_object ? "the object" : collision.type == _collision_result_structure ?
				"the structure" : "something else",
			collision.type == _collision_result_object && object_try_and_get(collision.object_index) ?
				tag_get_name(object_get(collision.object_index)->definition_index) : "",
			collision.point.x, collision.point.y, collision.point.z, collision.plane.n.i, collision.plane.n.j,
			collision.plane.n.k, collision.material_type);
		return TRUE;
	}
	if (!strcmp(arguments, "projectiles"))
	{
		struct object_iterator iterator;
		struct object_datum *object;
		short count = 0;

		object_iterator_new(&iterator, _object_mask_projectile, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			platform_log("forge: blocks: projectile %s at %.3f %.3f %.3f", tag_get_name(object->definition_index),
				object->object.position.x, object->object.position.y, object->object.position.z);
			count++;
		}
		platform_log("forge: blocks: %d projectiles", count);
		return TRUE;
	}
	return FALSE;
}
