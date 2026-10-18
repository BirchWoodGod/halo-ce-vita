/*
SCENE_PACKS.H

Scene packs in the running game (scene_packs.c; the pack itself, its
builder and checks: scene_pack.c). A pack is wanted by its recipe's name
(app0:packs/<name>.txt); the first time, and whenever its recipe or its
source map changes, it is built on a thread of its own from the player's
maps, into t:\packs\<name>.pack; then, at the top of a frame, it is loaded
beside the loaded map: its tags read into the tag cache after the map's
(before the structure bsp, which loads at the top: the room between them),
checked as any pack is, given the game's tag indices after the map's (and
the menus' tags), its models' buffers registered, its pixels and samples
read from its file when the texture and sound caches want them (file
offsets of its own: scene_packs_read). It stays until the map goes.

Forge's use (the design's option B), as the menus' scenes use it:
  scene_packs_want(name)       wanted with this map: built (on a thread)
                               and loaded when it can be; its state
  scene_packs_load_now(name)   the same, at once (built here if it must
                               be): before a map's objects are placed
  scene_packs_tag(name, group, path)  a tag of a loaded pack, the game's
                               index (NONE if it is not loaded)
  scene_packs_roots(name, ...) the recipe's tags (the pack's objects), the
                               game's indices, in the recipe's order
Nothing here touches an object.
*/

#ifndef __SCENE_PACKS_H
#define __SCENE_PACKS_H

#include "cseries.h"

/* ---------- constants */

enum
{
	_scene_pack_state_none = 0,
	/* being checked or built (a thread's) */
	_scene_pack_state_building,
	/* built, to be loaded at the top of the next frame */
	_scene_pack_state_ready,
	_scene_pack_state_loaded,
	/* no recipe, no source map, a build or a load failed: not tried again
	until the map changes */
	_scene_pack_state_failed,
	NUMBER_OF_SCENE_PACK_STATES,

	MAXIMUM_LOADED_SCENE_PACKS = 6,
	/* the packs a map may want (built, or loaded: at most
	MAXIMUM_LOADED_SCENE_PACKS of them) */
	MAXIMUM_SCENE_PACK_SLOTS = 10,
};

/* ---------- prototypes/SCENE_PACKS.C */

/* the pack of that recipe wanted with the loaded map (an Xbox map: none
with a Custom Edition one): its state now */
short scene_packs_want(
	char const *name);

/* (Forge's lobby) the pack of that recipe built, on the thread, if it is
not built already, but not loaded with the map loaded now (the menus'):
its state (ready once built). Wanted afterwards (scene_packs_want), it is
loaded as any */
short scene_packs_prebuild(
	char const *name);

/* a pack's state with this map, how far its build is (0 to 100), and why it
failed (why may be NULL): _scene_pack_state_none if it was not wanted */
short scene_packs_status(
	char const *name,
	long *progress,
	char *why,
	long why_size);

/* the loaded pack whose tag a tag index is (its recipe's name), NULL for
one of the map's */
char const *scene_packs_owner(
	long tag_index);

/* a recipe's hash (app0:packs/<name>.txt, as parsed), 0 if it has none */
unsigned long long scene_packs_recipe_hash(
	char const *name);

/* whether every tag of a recipe's own (its "tag" lines) is a tag of the
loaded map already (its pack would bring none of its objects) */
boolean scene_packs_recipe_covered(
	char const *name);

/* the same, loaded before this returns if it can be at all (built now if
it must be, which takes a while): TRUE if it is loaded */
boolean scene_packs_load_now(
	char const *name);

/* a loaded pack's tag (group, path), as the game's tag index, or NONE */
long scene_packs_tag(
	char const *name,
	unsigned long group_tag,
	char const *path);

/* a loaded pack's recipe tags (up to most), as the game's tag indices:
how many */
short scene_packs_roots(
	char const *name,
	long *tag_indices,
	short most);

/* a recipe the game made (player_models.c: a Custom Edition map's player
biped), wanted by its name as one of app0:packs (which it takes the place
of while it is registered): FALSE if it cannot be kept */
boolean scene_packs_recipe_register(
	char const *name,
	char const *text);

/* whether a recipe's source map is in the maps folder (of the identity it
gives): FALSE, and why (if not NULL), if it is not, or there is no such
recipe */
boolean scene_packs_recipe_available(
	char const *name,
	char *why,
	long why_size);

/* (main.c, the top of a frame) packs built meanwhile loaded */
void scene_packs_update(
	void);

/* (cache_files.c, scenario_tags_unload) the map's packs gone with it,
before its tags */
void scene_packs_unloaded(
	void);

/* (cache_files.c) the tag cache's bytes after the map's tags that the
packs hold (a structure bsp loads past them) */
unsigned long scene_packs_reserved_bytes(
	void);

/* (cache_files_windows.c) a read of a pack's pixels or samples (a file
offset of the packs'): served from its file, at once. FALSE if the offset is
not the packs' */
boolean scene_packs_read(
	long offset,
	long size,
	void *buffer);

/* a console command that starts with "pack" (hs.c): what follows the word
("want <name>", "load <name>", "list"). TRUE if it was understood */
boolean scene_packs_command(
	char const *arguments);

/* the packs' log line of a state */
char const *scene_packs_state_name(
	short state);

#endif
