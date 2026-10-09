/*
CUSTOM_EDITION_CACHE.H

What the native builds' cache file loader (source/cache/cache_files.c and
cache_files_windows.c) does with Halo Custom Edition caches, OpenSauce ".yelo"
caches among them. By default it finds them, says what they are, and refuses
them. With HALO_CUSTOM_EDITION set it loads and runs them instead, which is
experimental: their tags are laid out for Halo PC, and custom_edition_cache.c
and custom_edition_geometry.c convert what they know to differ
(docs/custom_edition_caches.md). The format itself is read by
cache_file_formats.c.
*/

#ifndef __CUSTOM_EDITION_CACHE_H
#define __CUSTOM_EDITION_CACHE_H

/* ---------- constants */

/* A Custom Edition campaign map's level name (one of a solo scenario,
played alone or as network co-op) is this and its file's name:
custom_maps\firefight-airlock for firefight-airlock.map in the maps folder,
which the cache file loader finds by the last part, as it finds every map.
None of the game's own levels is named so, so nothing takes such a map for
one of the campaign's (saved games, the profile's progress, the next level:
main_get_solo_level_from_name). From OpenCE's custom_edition_cache.h (CC0,
ce41b41d), whose maps all have such names; here only the campaign maps, the
multiplayer ones keeping levels\test\<name>\<name> (custom_edition_maps.c),
which 1.0.3's players know. */
#define CUSTOM_EDITION_LEVEL_NAME_PREFIX "custom_maps\\"

/* ---------- structures */

struct cache_file_tag_header;
struct custom_edition_load_report;
struct scenario_object_datum;
struct structure_bsp;

/* ---------- prototypes/CUSTOM_EDITION_CACHE.C */

/* When `header` (CACHE_FILE_HEADER_BYTES bytes, with `build` its build
string field) is a Custom Edition cache header, logs what it is, asserts when
`fatal` as cache_file_header_verify does, and returns TRUE. */
boolean custom_edition_cache_refuse(
	void const *header,
	char const *build,
	char const *scenario_name,
	boolean fatal);

/* When the ".map" file `path` names does not exist but an OpenSauce ".yelo"
file of the same name does, makes `path` (`path_size` characters) name that
instead. */
void opensauce_cache_path_find(
	char *path,
	long path_size);

/* TRUE when Custom Edition maps may run (HALO_CUSTOM_EDITION) and the map
`map_name` names is a Custom Edition cache whose resource maps are present:
it is then read in place, never copied to the cache partition. */
boolean custom_edition_cache_playable(
	char const *map_name);
/* The same, for a map of a multiplayer scenario (the multiplayer menus,
custom_edition_maps.c). */
boolean custom_edition_cache_multiplayer(
	char const *map_name);
/* The same, for a Custom Edition map of a solo scenario: a campaign map,
played alone or as network co-op (the campaign's level list,
custom_edition_maps.c). */
boolean custom_edition_cache_campaign(
	char const *map_name);
/* Whether `level_name` is a Custom Edition campaign map's
(CUSTOM_EDITION_LEVEL_NAME_PREFIX and a file name map sharing would take,
map_share_name_valid): no file is looked at. */
boolean custom_edition_level_name(
	char const *level_name);
/* TRUE when the map `map_name` names is a Custom Edition cache, whether or
not Custom Edition maps may run (the multiplayer join check: a joiner with
PC maps off cannot load one, network_client_manager.c). */
boolean custom_edition_cache_is_custom_edition(
	char const *map_name);
/* TRUE when the map `map_name` names is a Custom Edition cache that takes
tags from resource maps (bitmaps.map, sounds.map, loc.map, or an OpenSauce
mod set's) not in the maps folder, which are then named in `missing`
(`missing_size` characters, ", " between them): it cannot be loaded. A
quick look at its tag index (cache_file_formats.c,
custom_edition_cache_resource_maps_used). */
boolean custom_edition_cache_missing_resource_maps(
	char const *map_name,
	char *missing,
	long missing_size);
/* TRUE when the map `map_name` names is an Xbox cache of a multiplayer
scenario that the cache partition can take (named inside as its file is,
and no longer than the Xbox's multiplayer maps): a modded or newly built
Xbox map for the level list (custom_edition_maps.c); it needs no setting. */
boolean custom_edition_cache_xbox_multiplayer(
	char const *map_name);
/* What tells this machine's copy of the map `map_name` names from another
(an Xbox or Custom Edition cache): its header checksum with its length, or
the CRC-32 of the whole file when the header has none; 0 when there is no
such map. Never 0 for a map. */
unsigned long custom_edition_cache_map_identity(
	char const *map_name);
/* The file of the map `map_name` names, in the maps folder: its ".map",
else its OpenSauce ".yelo" (`path_size` characters); FALSE when there is
none. */
boolean custom_edition_cache_map_file_path(
	char const *map_name,
	char *path,
	long path_size);
/* Forgets what custom_edition_cache_map_identity worked out for the map
`map_name` names (a downloaded copy took the place of the file: map_share.c). */
void custom_edition_cache_map_identity_forget(
	char const *map_name);

/* Loads the Custom Edition map `map_name` names into its tag cache and
converts its tags for this build, copying its cache header to `header`
(CACHE_FILE_HEADER_BYTES bytes); returns its tag index header, or NULL after
logging why the map cannot be loaded. */
struct cache_file_tag_header *custom_edition_cache_tags_load(
	char const *map_name,
	void *header);

/* Gives the player's reason the map being loaded (or its structure BSP)
cannot be, when custom_edition_cache.c's own does not say it better. */
void custom_edition_cache_load_failure_note(
	char const *reason);

/* port: record that the loader refused a map as damaged or unsupported, so
main_new_map shows the player a message and returns to the menu rather than
stopping the game (cache_files.c's Xbox-cache refusals). */
void halo_map_load_refused(
	char const *map_name,
	char const *reason);
/* When the last load of the Custom Edition map `map_name` names failed,
tells the player why (platform_show_message) and returns TRUE: the caller
then leaves for the menu rather than stopping the game (also declared for
the game in halo_linux_source_fixups.h). */
boolean custom_edition_cache_load_failure_show(
	char const *map_name);
/* Reads `size` bytes at `offset` in the model data of the map being loaded
(the report's), for custom_edition_models_convert; FALSE when they are not
in it or cannot be read. */
boolean custom_edition_cache_model_data_read(
	struct custom_edition_load_report const *report,
	unsigned long offset,
	unsigned long size,
	void *buffer);
boolean custom_edition_cache_tags_loaded(
	void);
/* The tag cache the loaded map's tags are in and its size, in bytes
(cache_file_tag_cache_contains); NULL and 0 when none is loaded. */
void *custom_edition_cache_tag_cache(
	unsigned long *size);
/* A structure BSP of the loaded map was just read to `structure_bsp`: its
pointers are moved there too when the tags were (custom_edition_cache.c). */
void custom_edition_cache_structure_bsp_moved(
	void *structure_bsp,
	long size);
void custom_edition_cache_tags_unload(
	void);

/* Reads `size` bytes of the loaded map at `offset`, which counts in the
combined space of the map, its bitmaps.map and its sounds.map
(custom_edition_cache_combine_resource_offsets), for the tag `tag_index` as
cache_file_read names it (NONE, or a tag handle, or for sounds whatever the
permutation holds); bitmap pixels are converted as they arrive. FALSE when
they could not be read (the buffer then holds zeros). */
boolean custom_edition_cache_read(
	long tag_index,
	long offset,
	long size,
	void *buffer);

/* ---------- prototypes/CUSTOM_EDITION_SOUNDS.C */

struct sound_permutation;

/* Whether the sound cache loads `permutation` by decoding its Ogg Vorbis
stream (a Custom Edition map's, compression 3) rather than reading it. */
boolean custom_edition_sound_is_ogg_vorbis(
	struct sound_permutation const *permutation);
/* The bytes the sound cache holds for `permutation`: its samples' size, or
for an Ogg Vorbis one its Xbox ADPCM's (worked out from the stream's first
and last pages the first time, when the map's own word was not plausible);
0 when it cannot be played. Called by the sound cache and the channels
that play what it holds. */
long custom_edition_sound_cache_bytes(
	struct sound_permutation *permutation);
/* Has an Ogg Vorbis permutation decoded into `destination` (its cache
block, `destination_bytes` long) on the decoding thread, which sets
`*loaded` when it is done; FALSE when it could not be asked for (the block
is silence, and loaded, at once). */
boolean custom_edition_sound_load(
	struct sound_permutation const *permutation,
	void *destination,
	long destination_bytes,
	boolean *loaded);
/* The cache block whose loaded flag is `loaded` is being deleted: its
decoding is forgotten if it waits, and cut short and waited for if it
runs. */
void custom_edition_sound_cancel(
	boolean *loaded);
/* The map is going: the waiting decodings are forgotten, the one running
waited for, and the decoder's working memory given back. */
void custom_edition_sounds_stop(
	void);

/* ---------- prototypes/CUSTOM_EDITION_BITMAPS.C */

/* Whether this build can draw every bitmap of a tag cache
custom_edition_cache_load filled (`loaded_bytes` of it in use) from
Custom Edition pixels; logs the first it cannot. */
boolean custom_edition_bitmaps_verify(
	byte *tag_cache,
	unsigned long loaded_bytes);

/* Draws the large 2D textures of a tag cache custom_edition_cache_load
filled from their second level (HALO_CE_TEXTURE_LEVEL_KB), and logs how
many it changed. */
void custom_edition_bitmaps_reduce(
	byte *tag_cache,
	unsigned long loaded_bytes);

/* Finds the bitmaps of a tag cache custom_edition_cache_load filled whose
channels Halo PC keeps elsewhere (multipurpose maps and HUD meters), which
the renderer is to sample in this build's order as their pixels arrive;
FALSE after logging why it cannot. custom_edition_bitmaps_dispose forgets
them either way. */
boolean custom_edition_reordered_bitmaps_find(
	byte *tag_cache,
	unsigned long loaded_bytes);
void custom_edition_bitmaps_dispose(
	void);

/* Called when `pixels` were read from `offset` for the tag `tag_index`
names: when those are the pixels of one of its bitmaps, lays them out as
the texture cache expects of an Xbox bitmap. */
void custom_edition_bitmap_pixels_arrived(
	byte *tag_cache,
	unsigned long loaded_bytes,
	long tag_index,
	long offset,
	void *pixels);

/* ---------- prototypes/CUSTOM_EDITION_SCRIPTS.C */

/* Gives every function call and engine global reference of the scripts of
a tag cache custom_edition_cache_load filled this build's index for the
name the script keeps; FALSE after logging why when a script uses one this
build does not have or does not keep its name. */
boolean custom_edition_scripts_convert(
	byte *tag_cache,
	unsigned long loaded_bytes);

/* ---------- prototypes/CUSTOM_EDITION_OBJECTS.C */

/* Whether a Custom Edition map is running a multiplayer game whose vehicles
are chosen by their placements' spawn flags (also declared for the game in
halo_linux_source_fixups.h). */
boolean custom_edition_vehicles_by_placement(
	void);
/* Whether the vehicle placement `placement` is placed at the start of the
running game. */
boolean custom_edition_vehicle_placement_allowed(
	struct scenario_object_datum const *placement);

/* ---------- prototypes/CUSTOM_EDITION_GEOMETRY.C */

/* Gives every model of a tag cache custom_edition_cache_load filled
(`loaded_bytes` of it in use) this build's layout and compressed geometry in
buffers of its own, made from the model data the report describes, which is
read a part at a time (custom_edition_cache_model_data_read). Returns FALSE
after logging why when a model cannot be converted: the tags are then partly
converted and must not be used. custom_edition_models_dispose releases the
buffers either way. */
boolean custom_edition_models_convert(
	byte *tag_cache,
	unsigned long loaded_bytes,
	struct custom_edition_load_report const *report);
void custom_edition_models_dispose(
	void);

/* Called by scenario_structure_bsp_load and scenario_structure_bsp_unload
for the structure BSPs of a Custom Edition map: gives its materials
compressed vertices and buffers, or releases them. The load returns FALSE
after logging why when that fails. */
boolean custom_edition_structure_bsp_load(
	struct structure_bsp *structure_bsp);
void custom_edition_structure_bsp_unload(
	void);

#endif
