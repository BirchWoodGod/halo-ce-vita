/*
SCENE_PACK_BLOCKS.H

Forge's building blocks (scene_pack_blocks.c): blocks, walls, platforms,
bridges, columns, ramps and a corner that the game makes itself, as a scene
pack's tags, when a recipe has a "blocks" line (scene_pack.h). Nothing of
them is shipped and nothing is another map's but their textures: each
block's scenery, model (its geometry made here) and collision model (its
bsp made here, so that players, vehicles and shots collide with it) are our
own, and their two shaders are model shaders of our own whose bitmaps are
the source map's (the source map's environment shaders' base and detail
maps, chosen by their names: a metal panel for walls, a floor plate for
floors, the same on every machine).

The generator reads the source map's tag data (an Xbox map's, as the
builder has it inflated: anyone's file) only to choose those bitmaps, with
every read bounded; what it makes is laid out as a map's tags at addresses
of its own (SCENE_PACK_BLOCKS_ADDRESS), which the builder takes as more of
the source's tags (scene_pack.c), so that they are copied, linked and
checked as any pack's are (no correction allowed). The same map makes the
same bytes on every machine: the shapes are exact (sizes in quarters of a
world unit, the slopes' normals constants), and every choice is in the
source's order.
*/

#ifndef __SCENE_PACK_BLOCKS_H
#define __SCENE_PACK_BLOCKS_H

#include "cseries.h"

/* ---------- constants */

enum
{
	/* the generator's version (in the hash of a recipe with "blocks": a
	generator changed is a pack made again) */
	SCENE_PACK_BLOCKS_VERSION = 1,
	SCENE_PACK_MAXIMUM_BLOCKS = 32,
};

/* where the made tags are (as source addresses: past any map's tag cache) */
#define SCENE_PACK_BLOCKS_ADDRESS 0x90000000UL
/* the blocks' tags' paths begin so */
#define SCENE_PACK_BLOCKS_PATH_PREFIX "forge\\blocks\\"

/* ---------- structures */

/* a block of the set: its tag path (its scenery's; its model and collision
model have the same path), its size in world units (along x, y and z; its
origin at its middle), its kind */
struct scene_pack_block_info
{
	char const *path;
	real size[3];
	short kind;
};

enum
{
	_scene_pack_block_box = 0,
	_scene_pack_block_ramp,
	_scene_pack_block_corner,
	NUMBER_OF_SCENE_PACK_BLOCK_KINDS
};

/* the source map's tag data as the builder has it */
struct scene_pack_blocks_source
{
	unsigned char const *tags;
	unsigned long tags_size;
	/* the tag data's address (the Xbox's), its instances' and their count */
	unsigned long address;
	unsigned long instances;
	long count;
	/* (its name in the maps folder: its own level's shaders are its look) */
	char const *map;
};

struct scene_pack_blocks;

/* ---------- prototypes/SCENE_PACK_BLOCKS.C */

/* the set: how many, and each */
short scene_pack_blocks_count(
	void);

struct scene_pack_block_info const *scene_pack_blocks_info(
	short index);

/* the block whose tag path that is (case aside), or NONE */
short scene_pack_blocks_find(
	char const *path);

/* the blocks' tags made from the source map (its bitmaps chosen): NULL,
and why, if they cannot be (no bitmap the blocks can take, no memory) */
struct scene_pack_blocks *scene_pack_blocks_make(
	struct scene_pack_blocks_source const *source,
	char *why,
	long why_size);

void scene_pack_blocks_free(
	struct scene_pack_blocks *blocks);

/* how many tags it made (the source's tags count on from source->count:
the made tag i is the source's tag source->count + i) */
long scene_pack_blocks_tag_count(
	struct scene_pack_blocks const *blocks);

/* a made tag's instance (32 bytes, as a map's), or NULL */
unsigned char *scene_pack_blocks_instance(
	struct scene_pack_blocks const *blocks,
	long index);

/* the made tags' size bytes at address (theirs), or NULL if they are not
there */
unsigned char *scene_pack_blocks_at(
	struct scene_pack_blocks const *blocks,
	unsigned long address,
	unsigned long size);

/* the made tag that is block's scenery (an index as instance's) */
long scene_pack_blocks_scenery(
	struct scene_pack_blocks const *blocks,
	short block);

/* a made model part's (at part_address) vertices and strip, copies the
caller frees: FALSE if it is not one of the made parts */
boolean scene_pack_blocks_part_buffers(
	struct scene_pack_blocks const *blocks,
	unsigned long part_address,
	unsigned char **vertices,
	unsigned long *vertex_bytes,
	unsigned char **indices,
	unsigned long *index_bytes);

/* the bitmaps it chose (their source tag names, for the log): the walls'
and the floors' */
void scene_pack_blocks_materials(
	struct scene_pack_blocks const *blocks,
	char const **walls,
	char const **floors);

#endif
