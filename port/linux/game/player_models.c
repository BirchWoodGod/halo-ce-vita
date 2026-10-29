/*
PLAYER_MODELS.C

Player models from Halo Custom Edition maps (player_models.h, which says
what they are for).

The scan. Every Custom Edition multiplayer map of the maps folder (not one
the mod manager turned off) is looked at without being loaded
(scene_pack_ce_multiplayer_unit: its tag index and its globals' multiplayer
information, a few reads within its tag data): its multiplayer unit, when it
is a biped held by the map itself whose tag or model is not the stock
cyborg's (characters\cyborg_mp\cyborg_mp, characters\cyborg\cyborg), is a
model; a protected map's names say nothing ("<protected>"), so its biped is
listed whatever it is. One of more nodes than the renderer skins at once is
left out (a pack cannot draw it: scene_pack_ce.c). Each model is a scene
pack of its own, made by recipe here rather than read from app0:packs:

	pack pm_<hash of the map's name and identity>
	cemap <map> <size> <checksum>
	unit multiplayer

built the first time it is shown (CUSTOMIZATION's preview) or played, from
the player's map and the maps folder's resource maps, checked as any pack
is. Its name in the lists: the biped's tag's last part as words ("spartan_mp":
"Spartan"), and the map's ("Spartan (Extinction)"); a protected map's, the
map's alone ("Covenant V Marines Beta 5").

The choice. A profile's choice is kept in t:\player_models.txt (the save
folder's t, beside the packs; blam.sav is the Xbox's profile, whose layout
has no room for it), one line a profile - its folder's name, the model's
map, size and checksum, so that a choice keeps to its map whatever else the
maps folder holds - written whole by way of a temporary file. A line whose
map is gone, or another version of it, chooses the stock Spartan.

The game. As a map's tags load (cache_files.c): in a local game (the
game's connection local: never a network game, hosted or joined) of a
multiplayer scenario, the first local player's profile's model's pack is
loaded beside the map (scene_packs_load_now: after Forge's), and the player
spawns as its biped (players.c player_spawn: a game engine's game, which a
multiplayer scenario's always is), in the profile's colour. Bots, other
local players and every network game keep the map's own unit. The campaign
is left out: its maps have no tag cache to spare for a pack (Forge design,
"The memory budget") and its scripts and cutscenes name the cyborg.
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "tag_files/files.h"
#include "cache/cache_files.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "saved games/saved_game_files.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include "scene_pack.h"
#include "scene_pack_ce.h"
#include "scene_packs.h"
#include "player_models.h"
#include "../src/posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
int config_boolean(const char *name);

/* ---------- constants */

#define PLAYER_MODELS_FILE "t:\\player_models.txt"
#define PLAYER_MODELS_TEMPORARY "t:\\player_models.tmp"
#define STOCK_BIPED "characters\\cyborg_mp\\cyborg_mp"
#define STOCK_MODEL "characters\\cyborg\\cyborg"

enum
{
	/* the most profiles the file keeps choices for */
	MAXIMUM_CHOICES = 64,
	PROFILE_KEY_SIZE = 64,
	/* the most map files of the maps folder looked at */
	MAXIMUM_LISTED_MAPS = 512,
	/* the most nodes a pack's model may have (scene_pack_ce.c) */
	MAXIMUM_PACK_NODES = 43,
	/* a multiplayer scenario (scenario_definitions.h) */
	SCENARIO_TYPE_MULTIPLAYER = 1,
};

/* ---------- structures */

struct player_model_choice
{
	char profile[PROFILE_KEY_SIZE];
	char map[32];
	unsigned long file_size;
	unsigned long checksum;
};

/* ---------- globals */

static struct
{
	boolean scanned;
	short count;
	struct player_model models[MAXIMUM_PLAYER_MODELS];
	/* the choices file, as read (once) and written */
	boolean choices_read;
	short choice_count;
	struct player_model_choice choices[MAXIMUM_CHOICES];
	/* the pack loaded with the map now, for the first local player (its
	first tag, the biped: NONE when there is none) */
	long unit_tag_index;
	/* (the settings panel's thread) the choice it made, taken at the top of
	a frame: -2 none, else its index (0 the stock Spartan) */
	int volatile panel_request;
	/* the panel's list (a copy, under the lock) and the choice it shows */
	int volatile lock;
	short panel_count;
	char panel_names[MAXIMUM_PLAYER_MODELS + 1][PLAYER_MODEL_NAME_SIZE];
	int volatile panel_chosen;
} player_models = { FALSE, 0, { { { 0 } } }, FALSE, 0, { { { 0 } } }, NONE, -2, 0, 0, { { 0 } }, 0 };

/* ---------- private code */

static void take(void)
{
	while (__atomic_exchange_n(&player_models.lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void give(void)
{
	__atomic_store_n(&player_models.lock, 0, __ATOMIC_RELEASE);
}

static unsigned long fnv32(unsigned long hash, void const *data, unsigned long size)
{
	unsigned char const *bytes = data;
	unsigned long index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
	}
	return hash;
}

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

static boolean same_name(char const *a, char const *b)
{
	while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b))
		a++, b++;
	return !*a && !*b;
}

/* a file name as words ("Covenant_V_Marines_Beta_5": "Covenant V Marines Beta
5"), each word's first letter a capital */
static void words_make(char const *text, char *out, int size)
{
	boolean word_start = TRUE;
	int length = 0;

	for (; *text && length < size - 1; text++)
	{
		char character = *text == '_' || *text == '-' ? ' ' : *text;

		if ((unsigned char)character < 0x20 || (unsigned char)character >= 0x7F)
			continue;
		if (word_start && character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		word_start = character == ' ';
		out[length++] = character;
	}
	out[length] = 0;
}

/* the name a model is shown by */
static void model_name_make(struct player_model *model, char const *biped)
{
	char map_words[40];
	char biped_words[40];
	char last[64];
	char const *slash = strrchr(biped, '\\');
	int length;

	words_make(model->map, map_words, sizeof(map_words));
	if (!strcmp(biped, "<protected>") || !biped[0])
	{
		snprintf(model->name, sizeof(model->name), "%s", map_words);
		return;
	}
	snprintf(last, sizeof(last), "%s", slash ? slash + 1 : biped);
	/* ("spartan_mp": "Spartan") */
	length = (int)strlen(last);
	if (length > 3 && same_name(last + length - 3, "_mp"))
		last[length - 3] = 0;
	words_make(last, biped_words, sizeof(biped_words));
	snprintf(model->name, sizeof(model->name), "%s (%s)", biped_words, map_words);
}

/* a map the mod manager turned off (HALO_MAPS_DISABLED, commas between) */
static boolean map_disabled(char const *name)
{
	char const *list = getenv("HALO_MAPS_DISABLED");

	while (list && *list)
	{
		int index = 0;

		while (name[index] && list[index] && list[index] != ',' && lower((unsigned char)name[index]) ==
			lower((unsigned char)list[index]))
		{
			index++;
		}
		if (!name[index] && (!list[index] || list[index] == ','))
			return TRUE;
		while (*list && *list != ',')
			list++;
		if (*list == ',')
			list++;
	}
	return FALSE;
}

/* the model's recipe registered with the packs */
static void model_register(struct player_model const *model)
{
	char recipe[512];

	snprintf(recipe, sizeof(recipe),
		"# %s: the multiplayer biped of the player's Halo Custom Edition map %s.map (player_models.c)\n"
		"pack %s\ncemap %s %lu %08lx\nunit multiplayer\n", model->name, model->map, model->pack, model->map,
		model->file_size, model->checksum);
	if (!scene_packs_recipe_register(model->pack, recipe))
		platform_log("player models: %s: its recipe cannot be kept", model->pack);
}

static void panel_list_update(void)
{
	short index;

	take();
	snprintf(player_models.panel_names[0], PLAYER_MODEL_NAME_SIZE, "Spartan");
	for (index = 0; index < player_models.count; index++)
		snprintf(player_models.panel_names[index + 1], PLAYER_MODEL_NAME_SIZE, "%s", player_models.models[index].name);
	player_models.panel_count = (short)(player_models.count + 1);
	give();
}

/* the first local player's profile: the one in use, else the one player 1
used last (the Halo 3 main menu's nameplate's: a game started from a lobby,
or from the settings panel, before any profile was chosen for it) */
static long first_player_profile(void)
{
	long profile_index = player_ui_get_active_player_profile_index(0);

	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	/* (none chosen yet, and the memory card holds one profile: that one) */
	if (profile_index == NONE)
	{
		long indices[8];
		word count = NUMBEROF(indices);

		player_profiles_enumerate_available_to_local_player_index(0, &count, indices, FALSE);
		if (count == 1)
			profile_index = indices[0];
	}
	return profile_index;
}

/* ---------- the choices file */

/* a profile's key: its folder's name */
static boolean profile_key(long profile_index, char *key, int size)
{
	char path[256];
	char *end, *start;

	key[0] = 0;
	if (profile_index == NONE || !saved_game_file_get_path_to_enclosing_directory(profile_index, path))
		return FALSE;
	end = path + strlen(path);
	while (end > path && (end[-1] == '\\' || end[-1] == '/'))
		*--end = 0;
	start = end;
	while (start > path && start[-1] != '\\' && start[-1] != '/')
		start--;
	if (!*start || strlen(start) >= (size_t)size)
		return FALSE;
	snprintf(key, (size_t)size, "%s", start);
	return TRUE;
}

/* a name of the file's: printable, no spaces or tabs */
static boolean field_valid(char const *text, int size)
{
	int length = 0;

	for (; text[length]; length++)
	{
		if (length >= size - 1 || (unsigned char)text[length] <= 0x20 || (unsigned char)text[length] >= 0x7F)
			return FALSE;
	}
	return length > 0;
}

/* the file read (once; anyone's file: each line checked, a bad one left out) */
static void choices_read(void)
{
	FILE *file;
	char line[256];

	if (player_models.choices_read)
		return;
	player_models.choices_read = TRUE;
	player_models.choice_count = 0;
	file = fopen(PLAYER_MODELS_FILE, "rb");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file) && player_models.choice_count < MAXIMUM_CHOICES)
	{
		struct player_model_choice *choice = &player_models.choices[player_models.choice_count];
		char profile[PROFILE_KEY_SIZE + 1], map[33], size_text[16], checksum_text[16], extra[2];

		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, "%64s %32s %15s %15s %1s", profile, map, size_text, checksum_text, extra) != 4 ||
			!field_valid(profile, sizeof(choice->profile)) || !field_valid(map, sizeof(choice->map)) ||
			strspn(size_text, "0123456789") != strlen(size_text) || strlen(size_text) > 10 ||
			strlen(checksum_text) != 8 || strspn(checksum_text, "0123456789abcdefABCDEF") != 8)
		{
			continue;
		}
		snprintf(choice->profile, sizeof(choice->profile), "%s", profile);
		snprintf(choice->map, sizeof(choice->map), "%s", map);
		choice->file_size = strtoul(size_text, NULL, 10);
		choice->checksum = strtoul(checksum_text, NULL, 16);
		player_models.choice_count++;
	}
	fclose(file);
}

/* the file written whole (a temporary file, then put in its place) */
static boolean choices_write(void)
{
	FILE *file = fopen(PLAYER_MODELS_TEMPORARY, "wb");
	boolean written;
	short index;

	if (!file)
		return FALSE;
	written = fprintf(file, "# Halo CE Vita: each profile's player model (CUSTOMIZATION's MODEL): the profile's folder,\n"
		"# the Halo Custom Edition map its model is from, the map's size and checksum\n") > 0;
	for (index = 0; index < player_models.choice_count && written; index++)
	{
		struct player_model_choice const *choice = &player_models.choices[index];

		written = fprintf(file, "%s %s %lu %08lx\n", choice->profile, choice->map, choice->file_size, choice->checksum) > 0;
	}
	if (fclose(file))
		written = FALSE;
	if (!written)
	{
		remove(PLAYER_MODELS_TEMPORARY);
		return FALSE;
	}
	remove(PLAYER_MODELS_FILE);
	if (rename(PLAYER_MODELS_TEMPORARY, PLAYER_MODELS_FILE))
	{
		remove(PLAYER_MODELS_TEMPORARY);
		return FALSE;
	}
	return TRUE;
}

/* ---------- public code */

boolean player_models_enabled(
	void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_PLAYER_MODELS");

		if (setting && *setting)
			enabled = strcmp(setting, "0") && _stricmp(setting, "false");
		else
			enabled = config_boolean("menus.player_models") ? 1 : 0;
		platform_log("player models: %s", enabled ? "on (menus.player_models, HALO_PLAYER_MODELS)" :
			"off (1.1.1: every player the Spartan; menus.player_models or HALO_PLAYER_MODELS=1 turns them on)");
	}
	return enabled ? TRUE : FALSE;
}

void player_models_scan(
	boolean again)
{
	char host_path[1024];
	char entry[256];
	char (*names)[32];
	long name_count = 0, name_index;
	void *directory;
	short count = 0;

	if (!player_models_enabled())
	{
		/* (off: no maps folder looked through, no model, no pack) */
		player_models.scanned = TRUE;
		player_models.count = 0;
		return;
	}
	if (player_models.scanned && !again)
		return;
	player_models.scanned = TRUE;
	platform_translate_path(cache_files_map_directory(), host_path, sizeof(host_path));
	/* (the folder's map names listed first, then each looked at: nothing
	is read while the folder is listed) */
	names = malloc(MAXIMUM_LISTED_MAPS * sizeof(*names));
	directory = names ? posix_directory_open(host_path) : NULL;
	if (!directory)
		platform_log("player models: %s (%s) cannot be listed", cache_files_map_directory(), host_path);
	while (directory && name_count < MAXIMUM_LISTED_MAPS && posix_directory_next(directory, entry, sizeof(entry)))
	{
		char const *dot = strrchr(entry, '.');

		/* (<name>.map, the name a map's: no folder, no other dots) */
		if (!dot || !same_name(dot, ".map") || dot == entry || (unsigned long)(dot - entry) >= sizeof(names[0]))
			continue;
		memcpy(names[name_count], entry, (size_t)(dot - entry));
		names[name_count][dot - entry] = 0;
		if (strchr(names[name_count], '.') || strchr(names[name_count], '/') || strchr(names[name_count], '\\') ||
			same_name(names[name_count], "bitmaps") || same_name(names[name_count], "sounds") ||
			same_name(names[name_count], "loc") || map_disabled(names[name_count]))
		{
			continue;
		}
		name_count++;
	}
	if (directory)
		posix_directory_close(directory);
	for (name_index = 0; name_index < name_count && count < MAXIMUM_PLAYER_MODELS; name_index++)
	{
		struct scene_pack_ce_unit unit;
		struct player_model *model = &player_models.models[count];
		char const *name = names[name_index];
		char why[SCENE_PACK_WHY_SIZE];
		unsigned long hash;
		unsigned char identity[8];
		boolean stock;

		/* (a folder named as a map - Oct 21 2026, a player's maps folder
		held a map's folder of development files: never opened as one) */
		{
			char path[1100];
			struct posix_file_information information;

			if (snprintf(path, sizeof(path), "%s/%s.map", host_path, name) >= (int)sizeof(path) ||
				posix_stat(path, &information) != 0 || (information.flags & _posix_file_is_directory))
			{
				platform_log("player models: %s.map: a folder, or not there, not a map file: left out", name);
				continue;
			}
		}
		/* (an Xbox map is no Custom Edition one: refused at once) */
		if (!scene_pack_ce_multiplayer_unit(cache_files_map_directory(), name, &unit, why, sizeof(why)))
		{
			if (strcmp(why, "it is not a Halo Custom Edition map"))
				platform_log("player models: %s.map: %s", name, why);
			continue;
		}
		if (unit.scenario_type != SCENARIO_TYPE_MULTIPLAYER || unit.group_tag != 'bipd')
			continue;
		stock = same_name(unit.name, STOCK_BIPED) && same_name(unit.model_name, STOCK_MODEL);
		if (stock)
		{
			platform_log("player models: %s.map: its multiplayer biped is the stock cyborg", name);
			continue;
		}
		if (unit.model_nodes < 1 || unit.model_nodes > MAXIMUM_PACK_NODES)
		{
			platform_log("player models: %s.map: its biped %s has a model of %ld nodes, more than a pack draws: left out",
				name, unit.name, unit.model_nodes);
			continue;
		}
		memset(model, 0, sizeof(*model));
		snprintf(model->map, sizeof(model->map), "%s", name);
		snprintf(model->biped, sizeof(model->biped), "%s", unit.name);
		model->file_size = unit.file_size;
		model->checksum = unit.checksum;
		hash = fnv32(2166136261UL, name, (unsigned long)strlen(name));
		identity[0] = (unsigned char)model->file_size;
		identity[1] = (unsigned char)(model->file_size >> 8);
		identity[2] = (unsigned char)(model->file_size >> 16);
		identity[3] = (unsigned char)(model->file_size >> 24);
		identity[4] = (unsigned char)model->checksum;
		identity[5] = (unsigned char)(model->checksum >> 8);
		identity[6] = (unsigned char)(model->checksum >> 16);
		identity[7] = (unsigned char)(model->checksum >> 24);
		hash = fnv32(hash, identity, sizeof(identity));
		snprintf(model->pack, sizeof(model->pack), "pm_%08lx", hash);
		model_name_make(model, unit.name);
		model_register(model);
		platform_log("player models: %d: %s - %s of %s.map (%ld nodes), pack %s", count, model->name, unit.name, name,
			unit.model_nodes, model->pack);
		count++;
	}
	if (names)
		free(names);
	player_models.count = count;
	platform_log("player models: %d found in the maps folder's Custom Edition maps", count);
	panel_list_update();
}

short player_models_count(
	void)
{
	return player_models.count;
}

boolean player_models_get(
	short index,
	struct player_model *model)
{
	if (index < 0 || index >= player_models.count)
		return FALSE;
	*model = player_models.models[index];
	return TRUE;
}

char const *player_models_pack(
	short model_index)
{
	return model_index >= 0 && model_index < player_models.count ? player_models.models[model_index].pack : NULL;
}

short player_models_profile_choice(
	long profile_index)
{
	char key[PROFILE_KEY_SIZE];
	short index, model;

	/* (off: a saved choice ignored, its file left as it is) */
	if (!player_models_enabled() || !profile_key(profile_index, key, sizeof(key)))
		return NONE;
	choices_read();
	for (index = 0; index < player_models.choice_count; index++)
	{
		struct player_model_choice const *choice = &player_models.choices[index];

		if (strcmp(choice->profile, key))
			continue;
		for (model = 0; model < player_models.count; model++)
		{
			struct player_model const *found = &player_models.models[model];

			if (same_name(found->map, choice->map) && found->file_size == choice->file_size &&
				found->checksum == choice->checksum)
			{
				return model;
			}
		}
		return NONE;
	}
	return NONE;
}

boolean player_models_profile_choose(
	long profile_index,
	short model_index)
{
	char key[PROFILE_KEY_SIZE];
	short index;

	/* (off: nothing written) */
	if (!player_models_enabled() || !profile_key(profile_index, key, sizeof(key)))
		return FALSE;
	choices_read();
	for (index = 0; index < player_models.choice_count && strcmp(player_models.choices[index].profile, key); index++)
		;
	if (model_index < 0 || model_index >= player_models.count)
	{
		/* (the stock Spartan: the profile's line gone) */
		if (index < player_models.choice_count)
		{
			player_models.choices[index] = player_models.choices[--player_models.choice_count];
		}
	}
	else
	{
		struct player_model const *model = &player_models.models[model_index];
		struct player_model_choice *choice;

		if (index == player_models.choice_count)
		{
			if (player_models.choice_count >= MAXIMUM_CHOICES)
				return FALSE;
			player_models.choice_count++;
		}
		choice = &player_models.choices[index];
		snprintf(choice->profile, sizeof(choice->profile), "%s", key);
		snprintf(choice->map, sizeof(choice->map), "%s", model->map);
		choice->file_size = model->file_size;
		choice->checksum = model->checksum;
	}
	if (!choices_write())
	{
		platform_log("player models: the choice of %s cannot be written", key);
		return FALSE;
	}
	platform_log("player models: %s chose %s", key, model_index >= 0 && model_index < player_models.count ?
		player_models.models[model_index].name : "the Spartan");
	if (profile_index == first_player_profile())
		player_models.panel_chosen = model_index >= 0 && model_index < player_models.count ? model_index + 1 : 0;
	return TRUE;
}

void player_models_map_loaded(
	long scenario_index)
{
	struct scenario *scenario = scenario_index != NONE ? (struct scenario *)tag_get('scnr', scenario_index) : NULL;
	short model_index;
	char const *pack;
	char why[SCENE_PACK_WHY_SIZE];

	player_models.unit_tag_index = NONE;
	if (!player_models_enabled() || !scenario || scenario->type != SCENARIO_TYPE_MULTIPLAYER || game_connection() != _game_connection_local)
		return;
	player_models_scan(FALSE);
	model_index = player_models_profile_choice(first_player_profile());
	pack = player_models_pack(model_index);
	if (!pack)
	{
		platform_log("player models: the first local player's profile (%08lx) plays as the Spartan",
			(unsigned long)first_player_profile());
		return;
	}
	if (!scene_packs_recipe_available(pack, why, sizeof(why)))
	{
		platform_log("player models: %s cannot be played: %s", player_models.models[model_index].name, why);
		return;
	}
	if (scene_packs_load_now(pack))
	{
		long roots[1];

		if (scene_packs_roots(pack, roots, 1) == 1 && roots[0] != NONE && tag_get_group_tag(roots[0]) == 'bipd')
		{
			player_models.unit_tag_index = roots[0];
			platform_log("player models: the first local player plays as %s (%s)", player_models.models[model_index].name,
				tag_get_name(roots[0]));
		}
	}
	else
	{
		scene_packs_status(pack, NULL, why, sizeof(why));
		platform_log("player models: %s was not loaded: %s", player_models.models[model_index].name, why);
	}
}

long player_models_unit(
	long player_index,
	long unit_definition_index)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

	if (!player_models_enabled() || player_models.unit_tag_index == NONE || !player || player->local_player_index != 0 ||
		game_connection() != _game_connection_local || !scene_packs_owner(player_models.unit_tag_index))
	{
		return unit_definition_index;
	}
	return player_models.unit_tag_index;
}

int player_models_panel_count(void)
{
	int count;

	take();
	count = player_models.panel_count ? player_models.panel_count : 1;
	give();
	return count;
}

const char *player_models_panel_name(int index)
{
	static char name[PLAYER_MODEL_NAME_SIZE];

	take();
	if (index <= 0 || index >= player_models.panel_count)
		snprintf(name, sizeof(name), "Spartan");
	else
		snprintf(name, sizeof(name), "%s", player_models.panel_names[index]);
	give();
	return name;
}

int player_models_panel_enabled(void)
{
	return player_models_enabled() ? 1 : 0;
}

int player_models_panel_chosen(void)
{
	return player_models.panel_chosen;
}

void player_models_panel_choose(int index)
{
	player_models.panel_chosen = index;
	player_models.panel_request = index;
}

void player_models_update(
	void)
{
	int request = player_models.panel_request;

	if (!player_models_enabled())
	{
		player_models.panel_request = -2;
		player_models.panel_chosen = 0;
		return;
	}
	if (!player_models.scanned)
	{
		player_models_scan(FALSE);
		player_models.panel_chosen = player_models_profile_choice(first_player_profile()) + 1;
	}
	if (request != -2)
	{
		player_models.panel_request = -2;
		player_models_profile_choose(first_player_profile(), (short)(request - 1));
	}
}

boolean player_models_command(
	char const *arguments)
{
	char word[16];
	int index = 0;

	word[0] = 0;
	if (sscanf(arguments, "%15s %d", word, &index) < 1)
		return FALSE;
	if (!player_models_enabled() && (!strcmp(word, "scan") || !strcmp(word, "list") || !strcmp(word, "choose")))
	{
		platform_log("player models: off (menus.player_models, HALO_PLAYER_MODELS=1)");
		return TRUE;
	}
	if (!strcmp(word, "scan"))
	{
		player_models_scan(TRUE);
		return TRUE;
	}
	if (!strcmp(word, "list"))
	{
		short model;
		long profile_index = first_player_profile();

		for (model = 0; model < player_models.count; model++)
			platform_log("player models: %d: %s (%s, %s.map)", model, player_models.models[model].name,
				player_models.models[model].pack, player_models.models[model].map);
		platform_log("player models: %d; the first local player's profile chose %d", player_models.count,
			player_models_profile_choice(profile_index));
		return TRUE;
	}
	/* "models choose <index>": the first local player's profile's choice
	(-1: the Spartan) */
	if (!strcmp(word, "choose"))
	{
		platform_log("player models: choose %d: %s", index,
			player_models_profile_choose(first_player_profile(), (short)index) ? "kept" : "not kept");
		return TRUE;
	}
	return FALSE;
}
