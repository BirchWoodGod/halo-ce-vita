/*
CACHE_FILE_FORMATS.H

Recognition, validation and loading of the Halo 1 map files that this build
did not ship with: Halo Custom Edition caches (version 609), the OpenSauce
extension of their header (".yelo" maps), the Custom Edition resource
maps bitmaps.map, sounds.map and loc.map, and (port) Halo: CE Anniversary
caches (MCC's version 13, docs/mcc_cea_maps.md), which are Custom Edition
caches linked elsewhere, with their structure BSPs' vertices outside them. docs/custom_edition_caches.md
defines what each milestone (recognize, load, run) means, which of them this
module reaches, and the evidence behind every layout used here.

The code is standalone C with fixed-width types: the same file is compiled
into the native game builds and into the host tool
port/tools/cache_file_report.c, which the tests drive
(tools/test_cache_file_formats.py). Map files are untrusted input: every
field is read little-endian from a bounds-checked buffer, never through a
structure laid over file data, and every offset, count and size is checked
against the file or buffer it refers to before it is used.
*/

#ifndef __CACHE_FILE_FORMATS_H
#define __CACHE_FILE_FORMATS_H

/* ---------- headers */

#include <stddef.h>
#include <stdint.h>

/* ---------- constants */

#define CACHE_FILE_HEADER_BYTES 0x800
/* the name, build and mod name fields: at most 31 characters and a NUL */
#define CACHE_FILE_STRING_BYTES 32

#define CACHE_FILE_VERSION_XBOX 5
#define CACHE_FILE_VERSION_CUSTOM_EDITION 609
/* (port) Halo: CE Anniversary, MCC's Halo 1 (Invader's CACHE_FILE_MCC_CEA) */
#define CACHE_FILE_VERSION_MCC_CEA 13

/* Halo PC and Custom Edition keep tag data at a fixed address, as the Xbox
does at 0x803A6000: 0x40440000, above a 0x440000-byte game state at
0x40000000, with 23 MB of room, or 1.5 times that for caches built with
OpenSauce's memory upgrades (OpenSauce cache_constants.hpp,
blam_memory_upgrades.hpp). Every map examined puts its structure BSPs at the
top of that room. */
#define CUSTOM_EDITION_TAG_CACHE_ADDRESS 0x40440000UL
#define CUSTOM_EDITION_TAG_CACHE_BYTES 0x01700000UL
#define CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED 0x02280000UL
#define CUSTOM_EDITION_CACHE_FILE_MAXIMUM_BYTES 0x18000000UL
#define CUSTOM_EDITION_CACHE_FILE_MAXIMUM_BYTES_UPGRADED 0x24000000UL

/* (port) A Halo: CE Anniversary cache links its tag data to 0x50000000, with
64 MB of room above it, and its structure BSPs to the top of that room
(Invader's map.hpp and build_workload.cpp; every one of Ruby's Rebalanced
Halo: CE's ten maps). Its tag data and each structure BSP keep the layouts
of a Custom Edition cache's, but for the BSP's header: where Custom Edition
has (none of) the Xbox's vertex buffer arrays, the CEA header says where in
the file the BSP's vertices are, which Custom Edition keeps in the BSP.
This build gives such a map a tag cache of its tag data and, after it, room
for its largest structure BSP, which every structure BSP is loaded into
(MCC_CEA_STRUCTURE_BSP_ALIGNMENT apart from the tag data), and reads the
vertices from the file as the BSP loads. The file is at most
MCC_CEA_CACHE_FILE_MAXIMUM_BYTES long (the combined offset space's room for
a map, custom_edition_cache.c), and the tag cache at most
MCC_CEA_TAG_CACHE_MAXIMUM_BYTES: OpenSauce's memory upgrades' 34.5 MB, what
this build gives the largest Custom Edition map. */
#define MCC_CEA_TAG_CACHE_ADDRESS 0x50000000UL
#define MCC_CEA_TAG_SPACE_BYTES 0x04000000UL
#define MCC_CEA_TAG_CACHE_MAXIMUM_BYTES CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED
#define MCC_CEA_CACHE_FILE_MAXIMUM_BYTES 0x40000000UL
#define MCC_CEA_STRUCTURE_BSP_ALIGNMENT 0x10000UL
/* the CEA header's flags (Invader's CacheFileHeaderCEAFlags): it may take
bitmaps and sounds from MCC's bitmaps.map and sounds.map (this build loads
only the tags it holds itself: a tag marked as held elsewhere refuses it),
and it is classic only */
#define MCC_CEA_HEADER_FLAGS_DEFINED 0x7
/* the scenario's structure BSP references (MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO) */
#define CACHE_FILE_MAXIMUM_STRUCTURE_BSPS 32

enum cache_file_format
{
	_cache_file_format_unrecognized,
	/* version 5: the format this build runs, when its build string is
	01.01.14.2342 (cache_file_header_verify decides that, not this module) */
	_cache_file_format_xbox_cache,
	/* a cache header with a version this module does not know */
	_cache_file_format_other_cache,
	_cache_file_format_custom_edition_cache,
	_cache_file_format_resource_map,
	/* (port) version 13, Halo: CE Anniversary (MCC): loaded as a Custom
	Edition cache only where the caller says it may be (a mod's campaign,
	offline: custom_edition_cache.c); every check of the Custom Edition
	format leaves it out */
	_cache_file_format_mcc_cea_cache,

	NUMBER_OF_CACHE_FILE_FORMATS
};

/* the values stored in a resource map's first field (OpenSauce
data_file.hpp, data_file_reference_type) */
enum resource_map_type
{
	_resource_map_none,
	_resource_map_bitmaps,
	_resource_map_sounds,
	_resource_map_locale,

	NUMBER_OF_RESOURCE_MAP_TYPES
};

/* the flags of the OpenSauce header (OpenSauce
cache_files_structures_yelo.hpp, s_cache_header_yelo::s_flags) */
enum opensauce_cache_flag
{
	_opensauce_cache_uses_memory_upgrades_bit,
	_opensauce_cache_uses_mod_data_files_bit,
	_opensauce_cache_is_protected_bit,
	_opensauce_cache_uses_game_state_upgrades_bit,
	_opensauce_cache_has_compression_parameters_bit,

	NUMBER_OF_OPENSAUCE_CACHE_FLAGS
};

/* findings that do not stop a load but must be reported */
enum custom_edition_warning
{
	/* the header checksum is not the CRC-32 of the data it covers
	(OpenSauce cache_files_yelo.cpp, CalculateChecksumFromMemoryMap) */
	_custom_edition_warning_checksum_mismatch_bit,
	/* bytes after the end the header declares that nothing refers to */
	_custom_edition_warning_trailing_data_bit,
	/* the map holds OpenSauce's project_yellow ('yelo') or
	project_yellow_globals ('gelo') tags */
	_custom_edition_warning_opensauce_tags_bit,
	/* the scenario's group was renamed to 'prot' (a "protected" map) and
	is given back */
	_custom_edition_warning_protected_bit,

	NUMBER_OF_CUSTOM_EDITION_WARNINGS
};

enum cache_file_status
{
	_cache_file_status_ok,

	/* reading and memory */
	_cache_file_status_read_failed,
	_cache_file_status_out_of_memory,
	_cache_file_status_file_too_small,
	_cache_file_status_file_too_large,

	/* the cache file header */
	_cache_file_status_bad_signatures,
	_cache_file_status_unsupported_version,
	_cache_file_status_unterminated_string,
	_cache_file_status_bad_file_length,
	_cache_file_status_compressed_cache,
	_cache_file_status_bad_tag_data_range,

	/* the OpenSauce header */
	_cache_file_status_bad_opensauce_header,
	_cache_file_status_unknown_opensauce_flags,
	_cache_file_status_bad_opensauce_definitions_range,

	/* the tag index */
	_cache_file_status_bad_tag_index_signature,
	_cache_file_status_bad_tag_instances_range,
	_cache_file_status_bad_tag_handle,
	_cache_file_status_bad_tag_name,
	_cache_file_status_bad_tag_address,
	_cache_file_status_bad_scenario_tag,
	_cache_file_status_bad_model_data_range,
	_cache_file_status_unexpected_external_tag,
	_cache_file_status_bad_tag_layout,

	/* structure BSPs */
	_cache_file_status_bad_structure_bsp_block,
	_cache_file_status_bad_structure_bsp_range,
	_cache_file_status_bad_structure_bsp_header,
	_cache_file_status_bad_structure_bsp_geometry,

	/* model geometry */
	_cache_file_status_bad_model_part,

	/* conversion */
	_cache_file_status_bad_shader_type,
	_cache_file_status_bad_script_nodes,

	/* resource maps and the tags they hold */
	_cache_file_status_bad_resource_map_header,
	_cache_file_status_wrong_resource_map_type,
	_cache_file_status_bad_resource_item,
	_cache_file_status_missing_resource_map,
	_cache_file_status_missing_resource_item,
	_cache_file_status_bad_resource_layout,
	_cache_file_status_bad_resource_data_range,
	_cache_file_status_tag_cache_overflow,

	/* (port) Halo: CE Anniversary caches */
	_cache_file_status_bad_mcc_cea_header,
	_cache_file_status_bad_object_names,
	_cache_file_status_script_parameters,

	NUMBER_OF_CACHE_FILE_STATUSES
};

/* ---------- structures */

/* Reads `size` bytes at `offset` into `buffer`, returning nonzero only when
every byte was read. */
typedef int (*cache_file_read_proc)(
	void *context,
	uint32_t offset,
	uint32_t size,
	void *buffer);

struct cache_file_source
{
	void *context;
	cache_file_read_proc read;
	uint32_t size;
};

/* the OpenSauce header at offset 0x70 of a Custom Edition cache header */
struct opensauce_cache_header
{
	int16_t version;
	uint16_t flags;
	uint8_t project_yellow_version;
	uint8_t project_yellow_globals_version;
	float memory_upgrade_amount;
	/* the zlib-compressed tag definitions of the OpenSauce editing kit,
	appended after the cache data */
	uint32_t definitions_size;
	uint32_t definitions_decompressed_size;
	uint32_t definitions_offset;
	char definitions_build[CACHE_FILE_STRING_BYTES];
	char mod_name[CACHE_FILE_STRING_BYTES];
	int16_t build_stage;
	uint32_t build_revision;
	int64_t build_timestamp;
	char build_string[CACHE_FILE_STRING_BYTES];
	uint8_t tools_version_major;
	uint8_t tools_version_minor;
	uint16_t tools_version_build;
	uint8_t minimum_version_major;
	uint8_t minimum_version_minor;
	uint16_t minimum_version_build;
	/* OpenSauce resource storage: compression parameters, tag symbols,
	string ids, tag string to string id tables */
	uint32_t resource_offsets[4];
};

struct cache_file_identity
{
	enum cache_file_format format;
	uint32_t file_size;

	/* cache files */
	int32_t version;
	uint32_t file_length;
	uint32_t compressed_file_length;
	uint32_t tag_data_offset;
	uint32_t tag_data_size;
	int16_t scenario_type;
	uint32_t checksum;
	char name[CACHE_FILE_STRING_BYTES];
	char build[CACHE_FILE_STRING_BYTES];
	int has_opensauce_header;
	struct opensauce_cache_header opensauce;
	/* (port) the room above CUSTOM_EDITION_TAG_CACHE_ADDRESS a Custom
	Edition cache without OpenSauce's memory upgrades puts its structure
	BSPs at the top of, when that is more than Custom Edition's 23 MB and no
	more than the upgrades' 1.5 times it: a map built with a larger tag
	space (Invader's --tag-space), as port/tools/mcc_map_convert.py builds
	Halo: CE Anniversary maps; 0 otherwise (cache_file_identify) */
	uint32_t structure_bsp_tag_cache_bytes;

	/* (port) a Halo: CE Anniversary cache's header flags
	(MCC_CEA_HEADER_FLAGS_DEFINED), and the tag cache it is given: its tag
	data and, MCC_CEA_STRUCTURE_BSP_ALIGNMENT apart, its largest structure
	BSP (structure_bsp_tag_cache_bytes then holds the whole) */
	uint16_t mcc_cea_flags;
	uint32_t mcc_cea_structure_bsp_offset;

	/* resource maps */
	enum resource_map_type resource_map_type;
	int32_t resource_item_count;
};

/* (port) A structure BSP of a Halo: CE Anniversary cache as the map links
it: where it is in the file, its size, the address its pointers are linked
to, and where in the file its vertices are (its header's two CEA fields).
The load moves its reference to the tag cache's room for structure BSPs
(the report's mcc_cea_structure_bsp_address): the game reads it there, and
custom_edition_cache.c moves its pointers from linked_address there and
reads its vertices from the file. */
struct mcc_cea_structure_bsp
{
	uint32_t file_offset;
	uint32_t size;
	uint32_t linked_address;
	uint32_t vertices_offset;
	uint32_t vertices_size;
};

struct resource_map_item
{
	uint32_t data_offset;
	uint32_t size;
	char const *name;
};

/* an open resource map: its index and names, read once */
struct resource_map
{
	struct cache_file_source *source;
	enum resource_map_type type;
	int32_t item_count;
	struct resource_map_item *items;
	char *names;
	/* (port) the items in the order of their data's offsets, made when a
	Halo: CE Anniversary map's pixels or samples are first looked for in
	them (by offset: resource_map_item_at); freed with the rest */
	struct resource_map_item *items_by_offset;
};

struct custom_edition_load_report
{
	struct cache_file_identity identity;
	enum cache_file_status status;
	/* where the first fatal problem was found: a tag index or NONE (-1), and
	a file offset or runtime address as the status describes */
	int32_t problem_tag_index;
	uint32_t problem_location;

	uint32_t tag_cache_bytes;
	int32_t tag_count;
	int32_t scenario_tag_index;
	/* the model vertex and strip data, which no tag holds: its offset in the
	file, where its strips start within it, and its size */
	uint32_t model_data_offset;
	uint32_t model_index_data_offset;
	uint32_t model_data_bytes;
	/* bytes of tag data read from the map, then of tags read from resource
	maps and placed after it */
	uint32_t tag_data_bytes;
	uint32_t resource_tag_bytes;
	int32_t resource_tag_counts[NUMBER_OF_RESOURCE_MAP_TYPES];
	int32_t structure_bsp_count;
	uint32_t largest_structure_bsp_bytes;
	uint32_t lowest_structure_bsp_address;
	int32_t structure_bsp_materials_checked;
	int32_t bitmap_data_ranges_checked;
	int32_t sound_sample_ranges_checked;
	/* (port) a Halo: CE Anniversary map's sound permutations whose samples
	are in a sounds.map the map was not given (MCC's own, which are FMOD
	banks), made silent */
	int32_t mcc_cea_sound_permutations_silent;
	int32_t relocated_pointer_count;
	uint32_t computed_checksum;
	uint32_t trailing_bytes;
	uint32_t warnings;
	/* (custom_edition_load_hooks' defer_model_checksum: the checksum before
	the model data, and the CRC-32 from 0 of the tag data, until
	custom_edition_cache_checksum_finish takes the model data's) */
	int32_t checksum_deferred;
	uint32_t checksum_before_model_data;
	uint32_t tag_data_crc;
	/* (port) a Halo: CE Anniversary cache's structure BSPs
	(structure_bsp_count of them), and where they are all loaded (the
	header checksum is not taken: MCC's covers other bytes) */
	uint32_t mcc_cea_structure_bsp_address;
	struct mcc_cea_structure_bsp mcc_cea_structure_bsps[CACHE_FILE_MAXIMUM_STRUCTURE_BSPS];
};

/* (port) what mcc_cea_cache_fix changed */
struct mcc_cea_fix_report
{
	/* the scenario's script syntax nodes, MCC's array of 32767 with holes,
	packed into this build's array of 19001: how many there are */
	int32_t script_nodes_packed;
	/* object names past this build's 512 that the scripts, AI
	conversations or command lists name, moved into the places of names
	nothing names; and the names left past 512, which name nothing now */
	int32_t object_names_moved;
	int32_t object_names_dropped;
	/* DXT bitmaps not marked compressed, marked so, their mipmaps cut to
	those their pixels hold whole */
	int32_t bitmaps_marked_compressed;
	/* bitmaps whose flags MCC's tools set and Custom Edition's never do
	(CEA's environment flag) were cleared */
	int32_t bitmaps_flags_cleared;
	/* the tag of the problem, when there was one, else NONE (-1) */
	int32_t problem_tag_index;
};

/* (port) The game's loader (custom_edition_cache.c) reads the map named
`source` through `read_crc`, which reads as source->read does and gives the
CRC-32 from 0 (cache_file_crc32_update) of the bytes read - on a thread of
its own, the CRC with it - and with `defer_model_checksum` leaves the model
data's part of the checksum to custom_edition_cache_checksum_finish, from
its own reads of the model data. NULL: none (the tools). */
struct custom_edition_load_hooks
{
	struct cache_file_source *source;
	int (*read_crc)(void *context, uint32_t offset, uint32_t size, void *buffer, uint32_t *crc);
	int defer_model_checksum;
};

void custom_edition_cache_load_hooks(
	struct custom_edition_load_hooks const *hooks);

/* Completes the checksum of a load that deferred the model data's part,
from the CRC-32 from 0 of all of the model data (report->model_data_bytes
from report->model_data_offset): computed_checksum and the mismatch warning
as custom_edition_cache_load would have set them. */
void custom_edition_cache_checksum_finish(
	struct custom_edition_load_report *report,
	uint32_t model_data_crc);

/* CRC-32 as the checksum takes it (reflected 0x04C11DB7, no inversions):
`crc` updated with `size` bytes, and `crc` as `length` zero bytes would
leave it - so the CRC of A then B, A's from some start and B's from 0, is
cache_file_crc32_shift(a, length of B) ^ b */
uint32_t cache_file_crc32_update(
	uint32_t crc,
	void const *bytes,
	uint32_t size);
uint32_t cache_file_crc32_shift(
	uint32_t crc,
	uint32_t length);

/* Where Halo PC keeps what a texture holds in other channels than this
build reads it from (docs/custom_edition_caches.md). The renderer samples a
texture in the order the game names for it (port/linux/src/xbox_textures.c,
halo_custom_edition_texels_channels). */
enum custom_edition_channel_order
{
	/* as this build reads them */
	_custom_edition_channels_xbox,
	/* a model shader's multipurpose masks: the auxiliary (detail) mask,
	self-illumination, specular and color change in red, green, blue and
	alpha, which this build's model shaders read from alpha, green, red and
	blue */
	_custom_edition_channels_multipurpose,
	/* a HUD meter: its shape in color and the order it fills in alpha,
	where this build's meters take the fill order from color and discard
	what has no alpha */
	_custom_edition_channels_hud_meter,

	NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS
};

/* Halo PC behaviours some Custom Edition maps were made around, where Chimera
fixes Halo PC to draw as the Xbox does (Chimera's map_hacks.hpp, by
SnowyMouse; from DamnationCE, CC0). This build draws as the Xbox does, so a
map listed as relying on one (custom_edition_behaviours.inc, by name and tag
data checksum) is drawn as Halo PC drew it where this build can. */
enum custom_edition_behaviour
{
	_custom_edition_behaviour_gearbox_chicago_multiply,
	_custom_edition_behaviour_gearbox_meters,
	/* HUD multitexture overlays' blend functions in Halo PC's order
	(hud_draw.c) */
	_custom_edition_behaviour_gearbox_multitexture_blend_modes,
	_custom_edition_behaviour_alternate_bump_attenuation,
	_custom_edition_behaviour_gearbox_bump_attenuation,
	/* model shaders' detail after reflection flag means the opposite */
	_custom_edition_behaviour_invert_detail_after_reflection,
	_custom_edition_behaviour_embedded_lua,
	/* the HUD digits are at the Xbox's size, not twice it */
	_custom_edition_behaviour_hud_number_scale,
	/* bitmaps' half HUD scale flags were set by mistake */
	_custom_edition_behaviour_disable_bitmap_hud_scale_flags,
	_custom_edition_behaviour_old_widescreen_fix,
	_custom_edition_behaviour_gearbox_shader_environment_types,
	/* HUD multitexture overlays are not drawn (hud_draw.c) */
	_custom_edition_behaviour_block_multitexture_overlays,

	NUMBER_OF_CUSTOM_EDITION_BEHAVIOURS
};

/* A permutation's compression as custom_edition_cache_convert leaves it for
the sound cache (custom_edition_sounds.c) when its sound is a 44 kHz mono
one made 22 kHz: its samples (16-bit PCM, or Xbox ADPCM) taken at half
their rate. Halo PC has no such compression, and a map's permutation in
one makes its sound unplayable (sound_prepare). */
#define CUSTOM_EDITION_PERMUTATION_HALVED 0x100
#define CUSTOM_EDITION_PERMUTATION_PCM_HALVED (CUSTOM_EDITION_PERMUTATION_HALVED | 0)
#define CUSTOM_EDITION_PERMUTATION_XBOX_ADPCM_HALVED (CUSTOM_EDITION_PERMUTATION_HALVED | 1)

/* what custom_edition_cache_convert changed */
struct custom_edition_conversion_report
{
	/* shaders whose type this build numbers differently, and transparent
	chicago extended shaders made transparent chicago ones */
	int32_t shaders_retyped;
	int32_t chicago_extended_shaders;
	/* shaders whose type was not their group's, given their group's */
	int32_t shaders_mistyped;
	/* bitmaps given their own tag and the state of a bitmap not yet drawn */
	int32_t bitmaps_prepared;
	/* (port) uncompressed 2D bitmaps of other dimensions than powers of two,
	without mipmaps, not marked linear (sprites of Halo: CE Anniversary
	maps rebuilt by Invader, which marks only interface bitmaps' so), made
	linear: the layout of every unswizzled bitmap, which this build draws
	only so at those dimensions */
	int32_t bitmaps_made_linear;
	/* 1 when the scenario's script syntax nodes, upgraded by OpenSauce, were
	made this build's number */
	int32_t script_nodes_reduced;
	/* animation graph object overlays that named an animation the graph
	does not have, made to name none */
	int32_t animation_overlays_disabled;
	/* weapon exported functions of Halo PC's "primary/secondary firing on",
	made primary/secondary firing */
	int32_t weapon_functions_converted;
	/* sounds in a compression this build cannot decode (Halo PC's IMA
	ADPCM, or permutations of another compression than their sound's), made
	unplayable */
	int32_t sounds_undecodable;
	/* Ogg Vorbis sounds made Xbox ADPCM ones, which the sound cache decodes
	as it loads them (custom_edition_sounds.c) */
	int32_t sounds_ogg_vorbis;
	/* (port) their permutations, the streams' bytes and the Xbox ADPCM
	bytes the sound cache gives them (0 for one the loader could not size,
	which the sound cache measures as it loads it): what the decoding costs
	and what a sound cache file holds (custom_edition_sounds.c) */
	int32_t sound_permutations_ogg_vorbis;
	int32_t sound_permutations_ogg_vorbis_unsized;
	uint32_t sound_ogg_vorbis_bytes;
	uint32_t sound_ogg_vorbis_adpcm_bytes;
	/* 16-bit PCM permutations of Xbox ADPCM sounds, which the sound cache
	encodes to Xbox ADPCM as it loads them (custom_edition_sounds.c) */
	int32_t sound_permutations_pcm;
	/* permutations whose size is not whole frames (16-bit PCM) or blocks
	(Xbox ADPCM), muted */
	int32_t sound_permutations_muted;
	/* 44 kHz mono sounds of Xbox ADPCM or 16-bit PCM, which the game plays
	at 22 kHz only (it refused them), made 22 kHz ones whose permutations
	the sound cache takes at half their rate as it loads them
	(custom_edition_sounds.c), and those permutations */
	int32_t sounds_halved;
	int32_t sound_permutations_halved;
	/* HUD element placements with Halo PC's high resolution scale, whose
	scale was halved */
	int32_t hud_placements_rescaled;
	/* placements with it that keep their scale and the flag: crosshair and
	overlay items, which Halo PC does not halve by their own flag, and
	numbers, whose digits hud_draw_numbers halves */
	int32_t hud_placements_kept;
	/* (port) 1 when the HUD is Halo: CE Anniversary's, laid out by MCC on
	a canvas twice the Xbox's 640x480 (its hud_globals remaps HUD bitmaps
	to MCC's remastered ones: custom_edition_cache_convert), and the
	placements, waypoint and damage indicator margins and digit metrics
	brought to the Xbox's canvas; hud_draw.c draws what no placement scales
	by it (custom_edition_cache_hud_mcc_canvas) */
	int32_t hud_mcc_canvas;
	int32_t hud_mcc_placements;
	/* 1 when the multiplayer hint that a key shows the score was made to
	name the Xbox button */
	int32_t score_hint_converted;
	/* widget event handlers numbered as the port's own menu functions,
	made to fail as Halo PC's own do, and game data inputs of Halo PC's own
	functions, made to run none; and 1 when the multiplayer pause menu was
	made the Xbox's resume and quit */
	int32_t widget_functions_cleared;
	int32_t pause_menu_trimmed;
	/* 1 when the campaign pause menu's SAVE LAST CHECKPOINT was taken out
	(its QUIT saves the checkpoint) */
	int32_t solo_pause_save_removed;
	/* the Halo PC behaviours the map relies on (enum
	custom_edition_behaviour, flags) */
	uint32_t behaviours;
	/* the tag of the problem, when there was one, else NONE (-1) */
	int32_t problem_tag_index;
};

/* ---------- prototypes */

char const *cache_file_status_describe(
	enum cache_file_status status);
char const *cache_file_format_describe(
	enum cache_file_format format);
char const *resource_map_type_describe(
	enum resource_map_type type);

/* Classifies a file from its first bytes and checks every header field that
can be checked against the file size alone. */
enum cache_file_status cache_file_identify(
	struct cache_file_source *source,
	struct cache_file_identity *identity);

/* The format of a cache header already in memory (CACHE_FILE_HEADER_BYTES
bytes), from its signatures and version alone, and whether it carries an
OpenSauce header: for callers that hold a header but not its file. */
enum cache_file_format cache_file_header_format(
	void const *header,
	int *has_opensauce_header);

/* The room for tag data a Custom Edition cache needs at
CUSTOM_EDITION_TAG_CACHE_ADDRESS: 23 MB, or 1.5 times that with memory
upgrades, or what its structure BSPs say it was built for, up to that
(structure_bsp_tag_cache_bytes); for a Halo: CE Anniversary cache, its tag
data and its largest structure BSP. */
uint32_t custom_edition_tag_cache_bytes(
	struct cache_file_identity const *identity);

/* The resource maps a Custom Edition cache (`map`, identified as
`identity`) takes tags from: a bit (1 << resource_map_type) for each kind
held by one of its tags marked as in a resource map, which
custom_edition_cache_load reads from there. A quick look at the tag index
alone (a few KB read at a time, before the map is loaded): pixels and
samples a tag of the map's own keeps in a resource map are not looked for.
0 when it takes none, or its tag index cannot be read. */
uint32_t custom_edition_cache_resource_maps_used(
	struct cache_file_source *map,
	struct cache_file_identity const *identity);

enum cache_file_status resource_map_open(
	struct cache_file_source *source,
	enum resource_map_type expected_type,
	struct resource_map *map);
void resource_map_close(
	struct resource_map *map);

/* Loads a Custom Edition cache into `tag_cache`, which stands for the
`tag_cache_bytes` bytes at CUSTOM_EDITION_TAG_CACHE_ADDRESS and must be at
least custom_edition_tag_cache_bytes() long. With `mcc_cea_allowed`, a Halo:
CE Anniversary cache is loaded too, at MCC_CEA_TAG_CACHE_ADDRESS (its
linked address, custom_edition_cache_linked_address), with its structure
BSP references moved to the room after its tag data (struct
mcc_cea_structure_bsp); without, it is refused as an unknown version. The map's tag data is copied to
the start, the tags kept in resource maps are placed after it with their
addresses resolved, and every structure BSP, bitmap and sound sample range is
checked. `resource_maps` is indexed by resource_map_type; entries may be NULL
when the map needs none of that type. The structure BSPs themselves are not
loaded: the game loads one at a time when it switches to it. Returns the
first fatal problem; the report describes the map either way. */
enum cache_file_status custom_edition_cache_load(
	struct cache_file_source *map,
	struct resource_map *const resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES],
	uint8_t *tag_cache,
	uint32_t tag_cache_bytes,
	int mcc_cea_allowed,
	struct custom_edition_load_report *report);

/* (port) The same for a Custom Edition cache's tags alone (a scene pack's
source: port/linux/game/scene_pack_ce.c), into `buffer` of `buffer_bytes`,
which stands for the start of the tag cache at
CUSTOM_EDITION_TAG_CACHE_ADDRESS and needs room for the tag data and the
tags kept in resource maps (placed after it, below the lowest structure BSP
and within the buffer): every check above but the structure BSPs' own (each
reference's tag, file range and place in the tag cache are checked; the BSP
is not read) and the header checksum (not taken). A Halo: CE Anniversary
cache is refused. */
enum cache_file_status custom_edition_cache_load_tags(
	struct cache_file_source *map,
	struct resource_map *const resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES],
	uint8_t *buffer,
	uint32_t buffer_bytes,
	struct custom_edition_load_report *report);

/* (port) Makes the tags of a Halo: CE Anniversary cache that
custom_edition_cache_load filled (`loaded_bytes` of `tag_cache` in use)
what this build takes where MCC has more room or other values, as
port/tools/mcc_map_convert.py makes them for Invader: the scenario's script
syntax nodes packed into this build's array (more than it holds refuse the
map), the object names past this build's 512 that are named by the scripts,
AI conversations or command lists moved into the places of names nothing
names (more than 512 such names refuse the map), the rest past 512 dropped
(what names them names nothing), bitmaps' flags that only MCC sets cleared,
and DXT bitmaps not marked compressed marked so, their mipmaps cut to those
their pixels hold whole. Indices out
of range, enumerators past their values, a sixth weapon type and MCC's own
script functions are made harmless as any map's are (tag_validate.c,
custom_edition_scripts.c). Returns the first problem. */
enum cache_file_status mcc_cea_cache_fix(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	struct mcc_cea_fix_report *report);

/* Says that the pointers of the tag cache custom_edition_cache_load filled
now point to `address`, where the tag cache is, rather than to
CUSTOM_EDITION_TAG_CACHE_ADDRESS: a process that cannot have the tag cache
there moves them (port/linux/src/tag_relocate.c), and the functions below
then translate them from there. The next custom_edition_cache_load starts
from CUSTOM_EDITION_TAG_CACHE_ADDRESS again. */
void custom_edition_cache_tags_moved(
	uint32_t address);
/* the address the tags of the last cache loaded are linked to:
CUSTOM_EDITION_TAG_CACHE_ADDRESS, or a Halo: CE Anniversary cache's
MCC_CEA_TAG_CACHE_ADDRESS, for the units that move the pointers */
uint32_t custom_edition_cache_linked_address(
	void);

/* Gives the tags of a tag cache custom_edition_cache_load filled
(`loaded_bytes` of it in use) this build's layouts and values where only
their bytes need to change: every shader's type as this build numbers them,
transparent chicago extended shaders made transparent chicago shaders,
bitmaps and sound permutations in the state of ones not yet drawn or played
and naming their own tags, sounds this build cannot decode made unplayable,
animation overlays naming animations that do not exist made to name none,
and OpenSauce's upgraded script node array made this build's size when its
nodes fit; and what this build can of the Halo PC behaviours the map named
`map_name` relies on. Returns the first problem: tags already converted stay
converted. */
enum cache_file_status custom_edition_cache_convert(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	char const *map_name,
	struct custom_edition_conversion_report *report);

/* the name of a Halo PC behaviour, as Chimera's map list names it */
char const *custom_edition_behaviour_name(
	short behaviour);

/* In a tag cache custom_edition_cache_load filled (`loaded_bytes` of it in
use), the definition of the next tag of group `group_tag` after tag
`*tag_index` (NONE, -1, to start from the first): `*tag_index` becomes its
index, and NULL is returned when no tag after it has a definition of
`definition_bytes` bytes within the tag cache. */
void *custom_edition_cache_tag_next(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t group_tag,
	uint32_t definition_bytes,
	int32_t *tag_index);

/* The definition of the tag `handle` names in a tag cache
custom_edition_cache_load filled, or NULL unless that is a tag of group
`group_tag` whose definition of `definition_bytes` bytes lies within the tag
cache: any value may be asked about. */
void *custom_edition_cache_tag_get(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t handle,
	uint32_t group_tag,
	uint32_t definition_bytes);

/* The element `element_index` of `element_bytes` bytes of the tag block at
`block`, in a tag of a tag cache custom_edition_cache_load filled, or NULL
unless the block's elements lie within the tag cache: any value may be asked
about. */
void *custom_edition_cache_block_element(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	void const *block,
	int32_t element_index,
	uint32_t element_bytes);

/* The bytes the tag data field at `data` holds, in a tag of a tag cache
custom_edition_cache_load filled, with their count in `*size`, or NULL
unless there are some and they lie within the tag cache: any value may be
asked about. */
void *custom_edition_cache_data_get(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	void const *data,
	uint32_t *size);

/* The path of tag `tag_index` of a tag cache custom_edition_cache_load
filled, for messages. */
char const *custom_edition_cache_tag_name(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	int32_t tag_index);

/* Makes every tag of group `group_tag` a tag of `new_group_tag` (their parent
groups stay as they are), returning how many there were. */
int32_t custom_edition_cache_tags_regroup(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t group_tag,
	uint32_t new_group_tag);

/* In a tag cache custom_edition_cache_load filled (`loaded_bytes` of it in
use), moves the file offsets of bitmap pixels and sound samples kept in
bitmaps.map and sounds.map by `bitmaps_offset` and `sounds_offset`, and
clears the flags that said they were kept there: afterwards every offset
counts in one combined space (the map, then bitmaps.map, then sounds.map)
that a single read function can serve. */
void custom_edition_cache_combine_resource_offsets(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t bitmaps_offset,
	uint32_t sounds_offset);

#endif
