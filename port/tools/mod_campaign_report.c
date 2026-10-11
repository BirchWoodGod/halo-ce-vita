/*
MOD_CAMPAIGN_REPORT.C

Reports what the native builds read of a Halo: The Master Chief Collection
mod's campaign files (port/linux/game/mod_campaign_files.c), for the tests
(tools/test_mod_campaign_files.py).

	mod_campaign_report --info FILE      (a CampaignInfo.json)
	mod_campaign_report --level FILE     (a campaign/<level>.json)

The file gets a block of "key: value" lines; the exit status is 0 when it
was read, 1 when it was refused, 2 when it could not be opened.
*/

/* ---------- headers */

#include "mod_campaign_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- public code */

int main(
	int argument_count,
	char **arguments)
{
	/* (one more byte than the reader takes: a larger file is refused by it) */
	static uint8_t text[MOD_CAMPAIGN_FILE_MAXIMUM_BYTES + 1];
	enum mod_campaign_file_status status;
	FILE *stream;
	size_t size;

	if (argument_count != 3 || (strcmp(arguments[1], "--info") && strcmp(arguments[1], "--level")))
	{
		fprintf(stderr, "usage: mod_campaign_report --info|--level FILE\n");
		return 2;
	}
	stream = fopen(arguments[2], "rb");
	if (!stream)
	{
		return 2;
	}
	size = fread(text, 1, sizeof(text), stream);
	fclose(stream);
	if (!strcmp(arguments[1], "--info"))
	{
		struct mod_campaign_info info;
		int level_index;

		status = mod_campaign_info_read(text, (uint32_t)size, &info);
		printf("status: %s\n", mod_campaign_file_status_describe(status));
		printf("title: %s\n", info.title);
		printf("levels: %d\n", info.level_count);
		for (level_index = 0; level_index < info.level_count; level_index++)
		{
			printf("level.%d: %s\n", level_index, info.levels[level_index]);
		}
	}
	else
	{
		struct mod_campaign_level level;

		status = mod_campaign_level_read(text, (uint32_t)size, &level);
		printf("status: %s\n", mod_campaign_file_status_describe(status));
		printf("scenario_file: %s\n", level.scenario_file);
		printf("title: %s\n", level.title);
		printf("description: %s\n", level.description);
		printf("thumbnail: %s\n", level.thumbnail);
		printf("large_image: %s\n", level.large_image);
	}

	return status == _mod_campaign_file_status_ok ? 0 : 1;
}
