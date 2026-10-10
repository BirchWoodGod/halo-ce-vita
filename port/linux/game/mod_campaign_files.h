/*
MOD_CAMPAIGN_FILES.H

(port) Reading of the campaign files of a Halo: The Master Chief Collection
mod (a Steam Workshop folder): its CampaignInfo.json, which names its
campaign and lists its levels' files in the order they are played, and
each level's campaign/<level>.json, which names the level's map
("ScenarioFile") and gives its title and description. Only those fields are
read; their text is kept as printable ASCII (anything else is a '?'), cut
to the room given. docs/mcc_cea_maps.md says where the files go.

The code is standalone C with fixed-width types: the same file is compiled
into the native game builds (custom_edition_mods.c) and into the host tool
port/tools/mod_campaign_report.c, which the tests drive
(tools/test_mod_campaign_files.py). A mod's files are untrusted input: the
text is read a byte at a time within its size, nesting is bounded, and
nothing is kept but what fits.
*/

#ifndef __MOD_CAMPAIGN_FILES_H
#define __MOD_CAMPAIGN_FILES_H

/* ---------- headers */

#include <stdint.h>

/* ---------- constants */

/* the largest file read: the mods' are under a kilobyte */
#define MOD_CAMPAIGN_FILE_MAXIMUM_BYTES 0x10000
/* the levels a campaign lists that are kept (the stock campaign has ten) */
#define MOD_CAMPAIGN_MAXIMUM_LEVELS 32
/* each text's room, its terminator included */
#define MOD_CAMPAIGN_TITLE_BYTES 64
#define MOD_CAMPAIGN_DESCRIPTION_BYTES 256
#define MOD_CAMPAIGN_FILE_NAME_BYTES 64
#define MOD_CAMPAIGN_SCENARIO_FILE_BYTES 32

enum mod_campaign_file_status
{
	_mod_campaign_file_status_ok,
	/* empty, too large, or not a JSON object */
	_mod_campaign_file_status_not_json,
	/* nested deeper than anything the mods write */
	_mod_campaign_file_status_too_deep,
	/* text that ends inside a value, or a character no JSON has there */
	_mod_campaign_file_status_malformed,

	NUMBER_OF_MOD_CAMPAIGN_FILE_STATUSES
};

/* ---------- structures */

/* CampaignInfo.json: "Title": { "Neutral": ... }, and "CampaignMaps", the
levels' files ("campaign/a10.json") in the order they are played */
struct mod_campaign_info
{
	char title[MOD_CAMPAIGN_TITLE_BYTES];
	int16_t level_count;
	char levels[MOD_CAMPAIGN_MAXIMUM_LEVELS][MOD_CAMPAIGN_FILE_NAME_BYTES];
};

/* campaign/<level>.json: "ScenarioFile" (the map's file name, without
".map"), "Title" and "Description" ({ "Neutral": ... }), and its pictures'
files in the mod's folder ("Images": { "Thumbnail": ..., "Large": ... }) */
struct mod_campaign_level
{
	char scenario_file[MOD_CAMPAIGN_SCENARIO_FILE_BYTES];
	char title[MOD_CAMPAIGN_TITLE_BYTES];
	char description[MOD_CAMPAIGN_DESCRIPTION_BYTES];
	char thumbnail[MOD_CAMPAIGN_FILE_NAME_BYTES];
	char large_image[MOD_CAMPAIGN_FILE_NAME_BYTES];
};

/* ---------- prototypes */

char const *mod_campaign_file_status_describe(
	enum mod_campaign_file_status status);

/* Reads the `size` bytes at `text` as a CampaignInfo.json into `info`
(cleared first: what the file lacks stays empty). */
enum mod_campaign_file_status mod_campaign_info_read(
	uint8_t const *text,
	uint32_t size,
	struct mod_campaign_info *info);

/* Reads the `size` bytes at `text` as a level's campaign file into `level`
(cleared first). */
enum mod_campaign_file_status mod_campaign_level_read(
	uint8_t const *text,
	uint32_t size,
	struct mod_campaign_level *level);

#endif
