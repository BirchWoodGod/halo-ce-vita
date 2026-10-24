/*
MCC_CONTENT_TEST.C

Halo CE content: MCC files, without any MCC file (run_mcc_content_test.sh):
the names that are MCC's own maps (port/linux/game/mcc_content.c) and the
halo1 folder of Steam's libraries (port/linux/src/mcc_paths.c), on made-up
libraryfolders.vdf texts, malformed and cut short ones among them.
*/

#include "mcc_content.h"
#include "mcc_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static void check(int passed, char const *text, int line)
{
	checks++;
	if (!passed)
	{
		failures++;
		printf("FAIL line %d: %s\n", line, text);
	}
}

static void test_names(void)
{
	char stem[MCC_CONTENT_STEM_BYTES];
	char path[64];
	int index, campaign = 0, multiplayer = 0;

	CHECK(mcc_content_name_stem("mcc_bloodgulch", stem) == _mcc_content_multiplayer && !strcmp(stem, "bloodgulch"));
	CHECK(mcc_content_name_stem("levels\\test\\mcc_bloodgulch\\mcc_bloodgulch", stem) == _mcc_content_multiplayer &&
		!strcmp(stem, "bloodgulch"));
	CHECK(mcc_content_name_stem("MCC_BloodGulch", stem) == _mcc_content_multiplayer && !strcmp(stem, "bloodgulch"));
	CHECK(mcc_content_name_stem("mods\\mcc_a10", stem) == _mcc_content_campaign && !strcmp(stem, "a10"));
	CHECK(mcc_content_name_stem("mcc_timberland", stem) == _mcc_content_multiplayer);
	/* (not MCC's: another prefix, no prefix, a name no stock map has, a
	stock name with more after it, the ui map) */
	CHECK(mcc_content_name_stem("bloodgulch", stem) == _mcc_content_none && !stem[0]);
	CHECK(mcc_content_name_stem("levels\\test\\bloodgulch\\bloodgulch", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mods\\ruby_a10", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mcc_mygulch", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mcc_bloodgulch2", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mcc_ui", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mcc_", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mc", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("", stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem(NULL, stem) == _mcc_content_none);
	CHECK(mcc_content_name_stem("mcc_bloodgulch\\..", stem) == _mcc_content_none);

	CHECK(mcc_content_map_path("bloodgulch", path, sizeof(path)) && !strcmp(path, "d:\\mcc\\halo1\\maps\\bloodgulch.map"));
	CHECK(mcc_content_map_path("D40", path, sizeof(path)) && !strcmp(path, "d:\\mcc\\halo1\\maps\\d40.map"));
	CHECK(!mcc_content_map_path("../x", path, sizeof(path)));
	CHECK(!mcc_content_map_path("mygulch", path, sizeof(path)));
	CHECK(!mcc_content_map_path("bloodgulch", path, 12));

	CHECK(!strcmp(mcc_content_title("a10"), "The Pillar of Autumn") && !strcmp(mcc_content_title("carousel"), "Derelict"));
	CHECK(mcc_content_title("mygulch") == NULL);
	CHECK(mcc_content_picture_frame("a10") == 0 && mcc_content_picture_frame("d40") == 9);
	CHECK(mcc_content_picture_frame("beavercreek") == 0 && mcc_content_picture_frame("bloodgulch") == 9 &&
		mcc_content_picture_frame("longest") == 12 && mcc_content_picture_frame("icefields") == -1);
	for (index = 0; mcc_content_campaign_stem(index); index++)
	{
		CHECK(mcc_content_campaign_index(mcc_content_campaign_stem(index)) == index);
		CHECK(mcc_content_stem_kind(mcc_content_campaign_stem(index)) == _mcc_content_campaign);
		campaign++;
	}
	for (index = 0; mcc_content_multiplayer_stem(index); index++)
	{
		CHECK(mcc_content_stem_kind(mcc_content_multiplayer_stem(index)) == _mcc_content_multiplayer);
		CHECK(strlen(MCC_CONTENT_NAME_PREFIX) + strlen(mcc_content_multiplayer_stem(index)) < MCC_CONTENT_NAME_BYTES);
		multiplayer++;
	}
	/* (MCC's halo1\maps: ten levels, the Xbox's thirteen and Halo PC's six) */
	CHECK(campaign == MCC_CONTENT_CAMPAIGN_LEVELS && multiplayer == 19);
	CHECK(mcc_content_campaign_index("bloodgulch") == -1 && mcc_content_campaign_stem(-1) == NULL);
}

static void test_library_folders(void)
{
	static char const modern[] =
		"\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/me/.local/share/Steam\"\n"
		"\t\t\"label\"\t\t\"\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"976730\"\t\t\"52342342\"\n\t\t}\n\t}\n"
		"\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n"
		"\t\"2\"\n\t{\n\t\t\"path\"\t\t\"/home/me/.local/share/Steam\"\n\t}\n}\n";
	static char const old[] =
		"\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1234\"\n\t\"ContentStatsID\"\t\t\"-42\"\n"
		"\t\"1\"\t\t\"E:\\\\Games\\\\Steam\"\n}\n";
	char folders[MCC_PATHS_MAXIMUM_LIBRARIES][MCC_PATHS_BYTES];
	char halo1[MCC_PATHS_BYTES + 96];
	char *big;
	unsigned long length;
	int count;

	count = mcc_paths_library_folders(modern, sizeof(modern) - 1, folders, MCC_PATHS_MAXIMUM_LIBRARIES);
	CHECK(count == 2 && !strcmp(folders[0], "/home/me/.local/share/Steam") && !strcmp(folders[1], "D:\\SteamLibrary"));
	count = mcc_paths_library_folders(old, sizeof(old) - 1, folders, MCC_PATHS_MAXIMUM_LIBRARIES);
	CHECK(count == 1 && !strcmp(folders[0], "E:\\Games\\Steam"));
	/* (the app's numbered keys with numbers for values are not paths) */
	CHECK(mcc_paths_library_folders("\"1\" \"12345\"", (unsigned long)strlen("\"1\" \"12345\""), folders, 4) == 0);
	CHECK(mcc_paths_library_folders(NULL, 0, folders, 4) == 0);
	CHECK(mcc_paths_library_folders(modern, sizeof(modern) - 1, folders, 0) == 0);
	CHECK(mcc_paths_library_folders(modern, sizeof(modern) - 1, folders, 1) == 1);
	/* (every truncation of the file: no read past it, what was whole kept) */
	for (length = 0; length < sizeof(modern) - 1; length++)
	{
		char *copy = malloc(length ? length : 1);

		memcpy(copy, modern, length);
		count = mcc_paths_library_folders(copy, length, folders, MCC_PATHS_MAXIMUM_LIBRARIES);
		CHECK(count >= 0 && count <= 2);
		free(copy);
	}
	/* (a path longer than kept: left out, the file read on) */
	big = malloc(4096);
	memset(big, 'x', 4096);
	memcpy(big, "\"path\" \"/", 9);
	memcpy(big + 2000, "\" \"path\" \"/ok\"", 14);
	count = mcc_paths_library_folders(big, 2014, folders, MCC_PATHS_MAXIMUM_LIBRARIES);
	CHECK(count == 1 && !strcmp(folders[0], "/ok"));
	free(big);
	/* (a newline inside a quoted value: a broken file, nothing of it) */
	CHECK(mcc_paths_library_folders("\"path\" \"/a\nb\"", (unsigned long)strlen("\"path\" \"/a\nb\""), folders, 4) == 0);

	CHECK(mcc_paths_mcc("/home/me/.local/share/Steam/", halo1, sizeof(halo1)) &&
		!strcmp(halo1, "/home/me/.local/share/Steam/steamapps/common/Halo The Master Chief Collection"));
	CHECK(mcc_paths_mcc("D:\\SteamLibrary", halo1, sizeof(halo1)) &&
		!strcmp(halo1, "D:\\SteamLibrary/steamapps/common/Halo The Master Chief Collection"));
	CHECK(!mcc_paths_mcc("", halo1, sizeof(halo1)) && !mcc_paths_mcc("/x", halo1, 10));
}

static void test_loaders(void)
{
	/* (a cache header's first bytes: 'head' little-endian, its version) */
	static unsigned char const halo1[8] = { 'd', 'a', 'e', 'h', 13, 0, 0, 0 };
	static unsigned char const xbox[8] = { 'd', 'a', 'e', 'h', 5, 0, 0, 0 };
	static unsigned char const custom_edition[8] = { 'd', 'a', 'e', 'h', 0x61, 0x02, 0, 0 };
	static unsigned char const other[8] = { 'B', 'L', 'A', 'M', 13, 0, 0, 0 };
	struct content_library_game const *game;
	int index, game_index;

	CHECK(content_library_loader_for_header(halo1, sizeof(halo1), &game_index) == _content_library_loader_halo1 &&
		game_index == 0 && !strcmp(content_library_game_get(game_index)->folder, "halo1"));
	CHECK(content_library_loader_for_header(xbox, sizeof(xbox), &game_index) == _content_library_loader_none &&
		game_index == -1);
	CHECK(content_library_loader_for_header(custom_edition, sizeof(custom_edition), NULL) == _content_library_loader_none);
	CHECK(content_library_loader_for_header(other, sizeof(other), &game_index) == _content_library_loader_none &&
		game_index == -1);
	CHECK(content_library_loader_for_header(halo1, 7, &game_index) == _content_library_loader_none);
	CHECK(content_library_loader_for_header(NULL, 8, NULL) == _content_library_loader_none);
	/* (every game once, its folder a plain name; those without a loader
	known by no header) */
	for (index = 0; (game = content_library_game_get(index)) != NULL; index++)
	{
		CHECK(game->folder && game->folder[0] && !strpbrk(game->folder, "\\/.:") && game->title);
		CHECK(game->loader == _content_library_loader_none || game->cache_version);
	}
	CHECK(index >= 6 && content_library_game_get(-1) == NULL);
}

/* the menus' map: which of the Xbox's ui.map and MCC's is read, and the bare
name "ui" */
static void test_menu_source(void)
{
	char const *reason = NULL;
	char path[64];
	char stem[MCC_CONTENT_STEM_BYTES];

	/* (no setting: the Xbox's when it is there, else MCC's, else the Xbox's error) */
	CHECK(mcc_content_menu_source_choose(NULL, 1, 1, &reason) == _mcc_content_menu_source_xbox && reason);
	CHECK(mcc_content_menu_source_choose("", 1, 0, NULL) == _mcc_content_menu_source_xbox);
	CHECK(mcc_content_menu_source_choose("auto", 0, 1, &reason) == _mcc_content_menu_source_mcc && reason);
	CHECK(mcc_content_menu_source_choose("AUTO", 1, 1, NULL) == _mcc_content_menu_source_xbox);
	CHECK(mcc_content_menu_source_choose("", 0, 0, &reason) == _mcc_content_menu_source_xbox && reason);
	/* (xbox: never MCC's; mcc: MCC's when it is there, even with the Xbox's) */
	CHECK(mcc_content_menu_source_choose("xbox", 0, 1, NULL) == _mcc_content_menu_source_xbox);
	CHECK(mcc_content_menu_source_choose("xbox", 1, 1, NULL) == _mcc_content_menu_source_xbox);
	CHECK(mcc_content_menu_source_choose("mcc", 1, 1, NULL) == _mcc_content_menu_source_mcc);
	CHECK(mcc_content_menu_source_choose("MCC", 0, 1, NULL) == _mcc_content_menu_source_mcc);
	CHECK(mcc_content_menu_source_choose("mcc", 1, 0, &reason) == _mcc_content_menu_source_xbox && reason);
	CHECK(mcc_content_menu_source_choose("mcc", 0, 0, NULL) == _mcc_content_menu_source_xbox);
	/* (a setting that is nothing of these is auto) */
	CHECK(mcc_content_menu_source_choose("what", 0, 1, NULL) == _mcc_content_menu_source_mcc);
	CHECK(mcc_content_menu_source_choose("what", 1, 1, NULL) == _mcc_content_menu_source_xbox);
	/* (the bare name) */
	CHECK(mcc_content_name_is_menu("ui") && mcc_content_name_is_menu("UI") && mcc_content_name_is_menu("d:\\maps\\ui"));
	CHECK(!mcc_content_name_is_menu("ui2") && !mcc_content_name_is_menu("mcc_ui") && !mcc_content_name_is_menu("") &&
		!mcc_content_name_is_menu(NULL) && !mcc_content_name_is_menu("levels\\ui\\ui2"));
	CHECK(mcc_content_map_path("ui", path, sizeof(path)) && !strcmp(path, "d:\\mcc\\halo1\\maps\\ui.map"));
	CHECK(!mcc_content_map_path("ui", path, 10));
	/* (the stem lookups do not know it: only the cache loader's own check does) */
	CHECK(mcc_content_name_stem("ui", stem) == _mcc_content_none && !stem[0]);
	CHECK(mcc_content_stem_kind("ui") == _mcc_content_none);
}

int main(void)
{
	test_names();
	test_menu_source();
	test_loaders();
	test_library_folders();
	printf("mcc_content_test: %d/%d checks passed\n", checks - failures, checks);
	return failures != 0;
}
