/*
SCENE_PACK.H

Scene packs (scene_pack.c): a few objects of another map - their tags, and
their bitmaps' pixels and sounds' samples - built on the player's machine
from the player's own maps into one file, then loaded beside whatever map
is loaded. Option B of the Forge design ("Palette packs, built on the Vita,
loaded with the map": docs/superpowers/specs/2026-10-18-forge-design.md).
Nothing of a map is shipped: a pack is made from a recipe (our own text: a
pack's name, the maps it may be made from, the tags it holds, the groups and
tags it leaves out), on the machine that uses it.

This is the pack itself, with no game state: its recipe, its builder, and
the checks and moves a pack's tags go through before anything reads them.
scene_packs.c puts a pack into the running game (the tag cache, the tag
table, the reads of its pixels and samples); the tools test this alone
(tools/scene_pack_test.c).

A pack file (little-endian):
  the header (SCENE_PACK_HEADER_SIZE bytes): signature, version, sizes and
    offsets, the recipe's hash and the source map's, the source map's name,
    a CRC of the header and one of each section;
  the tags (header.tags_size bytes at header.tags_offset): laid out as a
    map's tag data - a tag header (its scenario none), the tag table, the
    tags' names, the models' vertex and index buffers' headers, the tags
    (each one's root, blocks and data together), the buffers' vertices and
    indices - every pointer linked to SCENE_PACK_LINK_BASE, every tag index
    the pack's own (its first header.root_count tags are the recipe's, in
    its order), every bitmap's pixels and sound's samples at a file offset
    from SCENE_PACK_FILE_BASE;
  the data (header.data_size bytes at header.data_offset): those pixels and
    samples.

A pack is anyone's file: it is checked before anything reads it, as
strictly as a map is (scene_pack_open, scene_pack_load): bounded counts and
sizes, every section in the file, the file's size and CRCs; then its tags
moved with every pointer followed only into the pack and every reference
checked against its own table (tag_relocate.c), then checked by the tag
validator against their groups' schemas with the pack as the only region
and its data as the only file (tag_validate.c), which a pack must pass with
no correction at all. A pack that fails anything is refused (and made
again by the game: scene_packs.c), never trusted in part.
*/

#ifndef __SCENE_PACK_H
#define __SCENE_PACK_H

#include "cseries.h"

/* ---------- constants */

enum
{
	SCENE_PACK_VERSION = 1,
	/* (the builder's own: a pack of another builder's is made again) */
	SCENE_PACK_BUILDER_VERSION = 2,
	SCENE_PACK_HEADER_SIZE = 128,

	/* a recipe's most lines' worth */
	SCENE_PACK_MAXIMUM_RECIPE_BYTES = 8 * 1024,
	SCENE_PACK_MAXIMUM_MAPS = 16,
	SCENE_PACK_MAXIMUM_ROOTS = 16,
	SCENE_PACK_MAXIMUM_STRIP_GROUPS = 32,
	SCENE_PACK_MAXIMUM_STRIP_TAGS = 32,
	SCENE_PACK_MAXIMUM_TUNES = 4,
	/* a pack's (and its name's, a map's name's, a tag path's) most */
	SCENE_PACK_NAME_SIZE = 32,
	SCENE_PACK_TAG_PATH_SIZE = 256,
	SCENE_PACK_MAXIMUM_TAGS = 2048,
	SCENE_PACK_MAXIMUM_TAG_BYTES = 8 * 1024 * 1024,
	SCENE_PACK_MAXIMUM_DATA_BYTES = 64 * 1024 * 1024,
	SCENE_PACK_MAXIMUM_BUFFERS = 8192,

	/* a reason's room (scene_pack_build, scene_pack_open...) */
	SCENE_PACK_WHY_SIZE = 160,
};

/* where a pack's tags are linked (they are moved to wherever they are
read), and where its pixels' and samples' file offsets count from: past any
map's file (and Custom Edition's combined offsets, 0x40000000 and
0x60000000: custom_edition_cache.c) */
#define SCENE_PACK_LINK_BASE 0x5C000000UL
#define SCENE_PACK_FILE_BASE 0x70000000UL

/* ---------- structures */

struct scene_pack_recipe_tag
{
	unsigned long group_tag;
	char path[SCENE_PACK_TAG_PATH_SIZE];
};

/* a Custom Edition map a recipe may be made from (cemap): its file's name
in the maps folder, without ".map", and its identity - the file's size and
its header's checksum field - which must be the file's */
struct scene_pack_recipe_custom_edition_map
{
	char name[SCENE_PACK_NAME_SIZE];
	unsigned long file_size;
	unsigned long checksum;
};

/* (a recipe's tune line) a tuning of the builder's and the recipe's tag it
changes */
struct scene_pack_recipe_tune
{
	char tuning[SCENE_PACK_NAME_SIZE];
	struct scene_pack_recipe_tag tag;
};

/* a recipe (scene_pack_recipe_parse): its text is

	# a comment
	pack <name>                  the pack's file name (letters, digits, _ -)
	map <name> [<name>...]       the maps it may be made from, the first one
	                             of them in the maps folder used
	cemap <name> <size> <checksum>
	                             (instead of map lines) a Halo Custom Edition
	                             map it may be made from, <name>.map in the
	                             maps folder, of that size in bytes (decimal)
	                             and header checksum (8 hex digits): the
	                             first of them there, of its identity, is
	                             used, with the maps folder's bitmaps.map,
	                             sounds.map and loc.map
	tag <group> <path>           a tag it holds, with all it needs (in order:
	                             the pack's first tags)
	unit multiplayer             a tag it holds: the source map's
	                             multiplayer unit (its globals' player
	                             biped), whatever its name (a protected
	                             map's have none)
	strip <group>                a group left out: references to its tags
	                             name none
	strip <group> <path>         a tag left out, the same way
	tune <tuning> <group> <path> one of the recipe's tags (a tag line's)
	                             changed by one of the builder's own
	                             tunings (scene_pack.c: "flyable_pelican",
	                             the campaign's Pelican made one players
	                             fly) once the pack is built; a tag that is
	                             not what the tuning expects is left as it
	                             is (said in the log)

groups as four characters ("bipd", "snd!"), paths as the tags' own names
("characters\cyborg_mp\cyborg_mp"). A root of the multiplayer unit has the
group 'unit' and an empty path. */
struct scene_pack_recipe
{
	char name[SCENE_PACK_NAME_SIZE];
	short map_count;
	short root_count;
	short strip_group_count;
	short strip_tag_count;
	char maps[SCENE_PACK_MAXIMUM_MAPS][SCENE_PACK_NAME_SIZE];
	struct scene_pack_recipe_tag roots[SCENE_PACK_MAXIMUM_ROOTS];
	unsigned long strip_groups[SCENE_PACK_MAXIMUM_STRIP_GROUPS];
	struct scene_pack_recipe_tag strip_tags[SCENE_PACK_MAXIMUM_STRIP_TAGS];
	/* of what the recipe says, as parsed (not its comments or spacing),
	and the builder's version: two machines with the same recipe have the
	same hash */
	unsigned long long hash;
	/* (after the hash: tools/scene_pack_test.c knows the fields before it)
	the Custom Edition maps it may be made from (cemap lines, which a
	recipe has instead of map lines) */
	short custom_edition_map_count;
	struct scene_pack_recipe_custom_edition_map custom_edition_maps[SCENE_PACK_MAXIMUM_MAPS];
	/* (tune lines) the tunings of its tags */
	short tune_count;
	struct scene_pack_recipe_tune tunes[SCENE_PACK_MAXIMUM_TUNES];
};

/* what a pack file's header says, once checked (scene_pack_open) */
struct scene_pack_info
{
	unsigned long file_size;
	unsigned long tags_offset;
	unsigned long tags_size;
	unsigned long data_offset;
	unsigned long data_size;
	long tag_count;
	long root_count;
	unsigned long tags_crc;
	unsigned long data_crc;
	unsigned long builder_version;
	unsigned long long recipe_hash;
	unsigned long long source_hash;
	char source_map[SCENE_PACK_NAME_SIZE];
};

/* what a build did (scene_pack_build) */
struct scene_pack_build_result
{
	char map[SCENE_PACK_NAME_SIZE];
	long tag_count;
	long stripped_count;
	long buffer_count;
	unsigned long tag_bytes;
	unsigned long data_bytes;
	unsigned long file_bytes;
	/* the source map's bytes inflated (or read) for it */
	unsigned long long source_bytes;
	/* how far it is, 0 to 100 (read by another thread while it builds:
	the source map read twice, each read half of it) */
	long volatile progress;
	unsigned long long microseconds;
	char why[SCENE_PACK_WHY_SIZE];
};

/* ---------- prototypes/SCENE_PACK.C */

/* a recipe's text (length bytes, at most SCENE_PACK_MAXIMUM_RECIPE_BYTES):
FALSE, and why, if it is not one */
boolean scene_pack_recipe_parse(
	char const *text,
	long length,
	struct scene_pack_recipe *recipe,
	char *why,
	long why_size);

/* (the tools) the size of a recipe, as the game's objects have it */
unsigned long scene_pack_recipe_size(
	void);

/* (the tools) a recipe's Custom Edition map's name, by index, or NULL */
char const *scene_pack_recipe_custom_edition_map(
	struct scene_pack_recipe const *recipe,
	short index);

/* whether a recipe's root is the source map's multiplayer unit (unit
multiplayer) rather than a tag of a path */
boolean scene_pack_recipe_root_is_multiplayer_unit(
	struct scene_pack_recipe_tag const *tag);

/* the source map's identity (its header, its file's size) as packs record
it, and its name in maps_directory (the first of the recipe's maps there,
which an Xbox map must be; or of its Custom Edition maps, of the identity
the recipe gives): FALSE if none of them is there, and why (why may be
NULL: "extinction.map is not in the maps folder") */
boolean scene_pack_source_check(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash,
	char *why,
	long why_size);

/* the same, without why */
boolean scene_pack_source_find(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash);

/* the pack the recipe makes, from its source map in maps_directory,
written to pack_path (by way of pack_path.tmp, renamed when whole). The
source map's tag data is read into scratch (scratch_size bytes) when it fits
there, otherwise into the heap. *cancel (if not NULL) becoming non-zero
stops it. FALSE, and why (result->why), if it was not made */
boolean scene_pack_build(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *pack_path,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *result);

/* the pack file at path: its header checked (bounds, sizes, the file's
size, the header's CRC) and what it says (info). FALSE, and why, if it is
not a pack this build reads */
boolean scene_pack_open(
	char const *path,
	struct scene_pack_info *info,
	char *why,
	long why_size);

/* the pack's tags (info->tags_size bytes) read to destination (4-aligned),
both sections' CRCs checked, the tags moved there and checked (above): FALSE,
and why, if the pack cannot be used, and nothing at destination may be */
boolean scene_pack_load(
	char const *path,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size);

/* a pack loaded at tags (scene_pack_load) given its tag indices in the
game's table: its tag i is (first_salt + i) << 16 | (first_index + i), as
are its references, tag indices and sounds' own indices; and its pixels'
and samples' file offsets moved by file_delta (several packs loaded at once
read from files of their own: scene_packs.c) */
void scene_pack_rebase(
	void *tags,
	long first_index,
	long first_salt,
	long file_delta);

/* a loaded pack's tag table (32 bytes a tag, cache_files.c's instances)
and count, and its vertex and index buffers' header (cache_files.c's struct
cache_file_tag_header) */
void *scene_pack_instances(
	void *tags,
	long *count);

/* the pack's tag of group and path (case aside), as an index into its
table, or NONE */
long scene_pack_find(
	void *tags,
	unsigned long group_tag,
	char const *path);

/* a group's four characters, terminated */
char *scene_pack_group_text(
	unsigned long group_tag,
	char text[5]);

#endif
