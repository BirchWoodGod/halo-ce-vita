/*
SCENE_PACK_CE.H

Scene packs built from Halo Custom Edition maps (scene_pack_ce.c), for the
pack builder (scene_pack.c) alone: the source map found by its identity,
its tags loaded, converted and checked as the game's Custom Edition loader
does (cache_file_formats.c, tag_relocate.c, tag_validate.c), and what the
Xbox's pack format holds made of what Custom Edition holds otherwise, as the
pack is built: a gbxmodel's parts this build's model parts, their vertices
compressed and strips copied from the map's model data; a bitmap's pixels
laid out as the Xbox's (swizzled, cube maps face by face, padded), the
channels Halo PC keeps elsewhere put where this build reads them; a sound
permutation's Ogg Vorbis or 16-bit PCM samples made Xbox ADPCM. The pack
that comes of it is an Xbox pack: the game loads and checks it as any
(scene_packs.c, scene_pack_load), with no Custom Edition code of its own.
*/

#ifndef __SCENE_PACK_CE_H
#define __SCENE_PACK_CE_H

#include "cseries.h"
#include "scene_pack.h"

#include <stdio.h>

struct scene_pack_recipe;
struct scene_pack_ce;

/* the Custom Edition builder's version (a pack it made is made again when
it changes: the recipe's hash takes it) */
char const *scene_pack_custom_edition_builder_version(
	void);

/* the first of the recipe's Custom Edition maps in maps_directory of the
identity the recipe gives (its size and header checksum), with the
resource maps it needs there: its name, and its identity as packs record it
(its header, its size and the resource maps' sizes). FALSE, and why, if
there is none */
boolean scene_pack_ce_source_check(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash,
	char *why,
	long why_size);

/* the map named map_name of maps_directory (scene_pack_ce_source_check's)
opened, its tags loaded into scratch (scratch_size bytes) when they fit
there, else into the heap, converted, moved where they are and checked as
the game's loader checks a Custom Edition map's: NULL, and why, if it
cannot be used. *cancel (if not NULL) becoming non-zero stops it; progress
(if not NULL) goes from 0 to 30 */
struct scene_pack_ce *scene_pack_ce_open(
	char const *maps_directory,
	char const *map_name,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	long volatile *progress,
	char *why,
	long why_size);

void scene_pack_ce_close(
	struct scene_pack_ce *ce);

/* its tags (where they are: their pointers point there) and their size */
unsigned char *scene_pack_ce_tags(
	struct scene_pack_ce *ce,
	unsigned long *size);

/* the name a tag of it goes by in a pack: its own, or one made for it when
it has none (a protected map's are "<protected>") */
char const *scene_pack_ce_tag_name(
	struct scene_pack_ce *ce,
	long source_index,
	char const *name);

/* (the closure) the gbxmodel at model_address (the tag source_index) made
this build's model in place, its parts repacked: FALSE, and why, if its
parts name what it has not, or it has more nodes than this build skins at
once */
boolean scene_pack_ce_model_convert(
	struct scene_pack_ce *ce,
	unsigned long model_address,
	long source_index,
	char *why,
	long why_size);

/* a part of a model scene_pack_ce_model_convert made (at part_address): its
compressed vertices and its strip's indices, read from the map's model data
and checked, in the heap (the caller's): FALSE, and why, if they cannot be */
boolean scene_pack_ce_part_buffers(
	struct scene_pack_ce *ce,
	unsigned long part_address,
	unsigned char **vertices,
	unsigned long *vertex_bytes,
	unsigned char **indices,
	unsigned long *index_bytes,
	char *why,
	long why_size);

/* the pack's tags of the groups that say which bitmaps Halo PC keeps in
other channels (model shaders, unit and weapon HUD interfaces): group and
root, count of them; a bitmap is then written in this build's channels
(scene_pack_ce_bitmap_write) */
void scene_pack_ce_channels(
	struct scene_pack_ce *ce,
	unsigned long const *groups,
	unsigned long const *roots,
	long count);

/* a bitmap of the pack (bitmap: its bitmap_data, as copied into the pack,
of the bitmap group source_index, whose root is group_root) written to data
as the Xbox lays its pixels out, at *data_size (which grows by what was
written), its fields made what the written pixels are (offset from the
data's start, size, format, flags, levels). FALSE, and why, if it cannot be */
boolean scene_pack_ce_bitmap_write(
	struct scene_pack_ce *ce,
	void *bitmap,
	long source_index,
	void const *group_root,
	FILE *data,
	unsigned long *data_size,
	unsigned long *data_crc,
	char *why,
	long why_size);

/* a sound permutation of the pack (permutation: its sound_permutation, as
copied into the pack; sound: its sound_definition's) written to data as Xbox
ADPCM, at *data_size, its fields made what was written (compression, size,
offset from the data's start). FALSE, and why, if it cannot be */
boolean scene_pack_ce_permutation_write(
	struct scene_pack_ce *ce,
	void *permutation,
	void const *sound,
	FILE *data,
	unsigned long *data_size,
	unsigned long *data_crc,
	char *why,
	long why_size);

/* a Custom Edition map's multiplayer unit, as its tag index says it, read
from its file without loading the map (a few reads, each within its tag
data: the player models' scan, player_models.c) */
struct scene_pack_ce_unit
{
	unsigned long file_size;
	unsigned long checksum;
	short scenario_type;
	unsigned long group_tag;
	long tag_index;
	/* its tag's name ("<protected>" for a protected map's), its model's,
	and the model's nodes */
	char name[SCENE_PACK_TAG_PATH_SIZE];
	char model_name[SCENE_PACK_TAG_PATH_SIZE];
	long model_nodes;
};

/* the map named map_name of maps_directory: its multiplayer unit (TRUE),
or why it has none */
boolean scene_pack_ce_multiplayer_unit(
	char const *maps_directory,
	char const *map_name,
	struct scene_pack_ce_unit *unit,
	char *why,
	long why_size);

/* (tools/scene_pack_test.c's fuzzing of damaged sources) called with every
read of the source map, its offset and bytes, which it may change; NULL
(the game): none */
extern void (*scene_pack_ce_read_hook)(unsigned long offset, void *buffer, unsigned long size);

#endif
