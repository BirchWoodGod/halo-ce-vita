/*
FORGE_BLOCKS.H

Forge's building blocks in the game (forge_blocks.c): the pack of them made
from the Forge map itself (scene_pack_blocks.c makes their tags, textured
with the map's own bitmaps; scene_packs.c builds and loads the pack), the
browser's BLOCKS category and names, and the grid snap that lines blocks up
edge to edge. A block is an ordinary scenery object of its tag path
(forge\blocks\<name>): placed, moved, saved in a map variant and counted in
the budget as any other.
*/

#ifndef __FORGE_BLOCKS_H
#define __FORGE_BLOCKS_H

#include "cseries.h"
#include "real_math.h"

/* ---------- prototypes/FORGE_BLOCKS.C */

/* (a Forge game's map loaded, before its other packs) the blocks' pack of
the loaded map (its scenario's path) built if it must be and loaded: TRUE if
the blocks are there */
boolean forge_blocks_load(
	char const *scenario);

/* (the Forge lobby, its map chosen: forge_map_short's name) the blocks'
pack of that map built in the background, if it is not built already */
void forge_blocks_prebuild(
	char const *map_name);

/* the block a tag of the loaded map is (scene_pack_blocks.h's index), or
NONE for any other tag */
short forge_blocks_of_tag(
	long tag_index);

/* a block's name in the browser, translated */
void forge_blocks_name(
	short block,
	char *name,
	int size);

/* (Forge's grid snap, on) the place of a block held, its corner on the
grid (grid world units) along each world axis as it is turned (forward,
up), so that blocks of any size side by side or stacked meet edge to edge;
nothing for an object that is not a block */
void forge_blocks_snap(
	long object_index,
	real grid,
	real_point3d *position,
	real_vector3d const *forward,
	real_vector3d const *up);

/* (tests) the console's "forge blocks <what>" (forge_blocks.c): TRUE if it
was understood */
boolean forge_blocks_command(
	char const *arguments);

#endif
