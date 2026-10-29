/*
FILESHARE_FRIENDS.C

The game's side of File Share with friends (port/linux/src/p2p_fileshare.h;
the design: docs/fileshare-friends.md), from the main loop
(friends_game.c) every frame:

- This machine's list for its friends: its map variants (forge_store.c,
  every map's) and its player's own game variants (the game's own 27 are
  never shared), each one marked shared read through the game's own loader
  and validator - forge_store_load (forge_file_decode) and encoded again by
  forge_file_encode; playlist_profile_get_from_path (its checksum) and the
  game variant checks below - and hashed; read again when the p2p side
  wants it (marks changed, a friend asked and it is old, the menus asked),
  while friends are online.
- Jobs: a file read for a friend (a download of a shared file, or the
  player's send), through the same loader and checks; a file received
  (whole, its SHA-256 the signed offer's) checked by the validator -
  forge_file_decode for a map variant, the game variant checks for a game
  variant - then saved by the game's own writers: forge_store_save, under a
  name made here from the file's hash (forge_file_name_received), never
  the sender's; playlist_profile_new and playlist_profile_save
  (menu_functions_gametype_save_received). A file that fails is never
  written.
- A friend's offer ("send to friend"): asked at the menus, out of any level
  and lobby, as a friend's invite is (the overlay's question in every menu
  style): Accept downloads it, Decline tells the friend.

(debug, not in release builds) HALO_TEST_FILESHARE: the tests' steps of
this side (fileshare_friends_test_parse below; the p2p side reads its own
of the same variable); HALO_TEST_FILESHARE_ANSWER=accept or decline answers
an offer's question at once.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "game/players.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "tag_files/files.h"
#include <xtl.h>
#include "halo_port_limits.h"

#include "../src/p2p_friends.h"
#include "../src/p2p_fileshare.h"
#include "../src/system_link_shortcut.h"
#include "../src/forge_store.h"
#include "../src/lang.h"
#include "forge_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
int platform_question_asking(void);
/* menu_functions.c's: a game variant received saved as a new game type of
the player's (its name, else with a number after it) */
boolean menu_functions_gametype_save_received(struct game_variant const *variant, char *saved, int saved_size,
	char *why, int why_size);

typedef char fileshare_game_variant_size_check[sizeof(struct game_variant) == P2P_FILESHARE_GAME_VARIANT_BYTES ? 1 : -1];

enum
{
	/* the game variants of the player's own listed at most */
	FILESHARE_GAME_VARIANTS_MAXIMUM = 32,
	FILESHARE_TEST_BROWSES = 4,
};

static struct
{
	/* the list (built here, given to the p2p side) */
	struct p2p_fileshare_local local[P2P_FILESHARE_LOCAL_MAXIMUM];
	struct forge_store_entry forge_entries[FORGE_STORE_MAXIMUM_LISTED];
	struct forge_file_layout layout;
	unsigned char bytes[P2P_FILESHARE_MAXIMUM_BYTES + 1];
	/* the offer asked about, by its id ("" none) */
	char asking[P2P_FILESHARE_ID_SIZE];
	char asking_name[P2P_FRIEND_NAME_SIZE];
	char asking_title[P2P_FILESHARE_TITLE_SIZE];
	/* (the tests' steps) */
	boolean test_parsed;
	boolean test_share_all, test_shared, test_corrupt;
	char test_make_gametype[16];
	boolean test_made;
	long test_browse_at[FILESHARE_TEST_BROWSES];
	int test_browse_count, test_browses_done;
	int test_download;
	int test_downloaded;
	long test_send_at;
	int test_send_kind;
	boolean test_sent;
	unsigned long test_started;
	int test_last_state;
	unsigned long test_download_time;
} fileshare_friends;

/* ---------- game variants */

/* a game variant's name as ASCII (players.h's letters with marks plain,
'?' for the rest) */
static void fileshare_variant_name(char *text, int size, wchar_t const *wide)
{
	int index;

	for (index = 0; index < size - 1 && index < 12 && wide[index]; index++)
		text[index] = player_name_character_ascii(wide[index]);
	text[index] = 0;
}

/* the game variant checks a received one must pass before it is saved:
its name 1 to 11 printable characters, terminated; its flags none (a
sender's are cleared: the game's own flag and its default's number); its
engine one of the five; and game_engine_variant_cleanup (what every network
game applies to a host's variant) changing nothing. TRUE, or FALSE with why */
static boolean fileshare_game_variant_valid(struct game_variant const *variant, char *why, int why_size)
{
	struct game_variant cleaned;
	int index;

	for (index = 0; index < 12 && variant->human_readable_game_description[index]; index++)
	{
		unsigned long character = (unsigned short)variant->human_readable_game_description[index];

		if (character < 0x20 || (character >= 0x7F && character < 0xA0) || (character >= 0xD800 && character < 0xE000) ||
			character >= 0xFFFE)
		{
			snprintf(why, (size_t)why_size, "%s", T("its name has characters no name has"));
			return FALSE;
		}
	}
	if (index == 0 || index >= 12)
	{
		snprintf(why, (size_t)why_size, "%s", T("its name is empty or too long"));
		return FALSE;
	}
	if (variant->flags != 0)
	{
		snprintf(why, (size_t)why_size, "%s", T("its flags are not a shared game variant's"));
		return FALSE;
	}
	if (variant->game_engine_index < 1 || variant->game_engine_index > 5)
	{
		snprintf(why, (size_t)why_size, "%s", T("its game type is none of the five"));
		return FALSE;
	}
	cleaned = *variant;
	game_engine_variant_cleanup(&cleaned);
	if (memcmp(&cleaned, variant, sizeof(cleaned)))
	{
		snprintf(why, (size_t)why_size, "%s", T("its settings are out of range"));
		return FALSE;
	}
	return TRUE;
}

/* the player's own game variants (not the game's): their profile indices,
how many */
static int fileshare_game_variants(long *indices, int maximum)
{
	long all[64];
	word count = (word)NUMBEROF(all);
	int index, found = 0;

	playlist_profiles_enumerate_available_to_local_player_index(0, &count, all);
	for (index = 0; index < (int)count && index < (int)NUMBEROF(all) && found < maximum; index++)
	{
		if (all[index] == NONE || !TEST_FLAG(all[index], _saved_game_file_index_valid_bit) ||
			TEST_FLAG(all[index], _saved_game_file_index_read_only_bit))
		{
			continue;
		}
		indices[found++] = all[index];
	}
	return found;
}

/* a game variant's saved game folder's name (the last part of its path) */
static boolean fileshare_game_variant_folder(long profile_index, char *folder, int size, char *path, int path_size)
{
	char full[MAXIMUM_FILENAME_LENGTH + 1];
	char const *last;
	int index;

	if (!saved_game_file_get_path_to_enclosing_directory(profile_index, full))
		return FALSE;
	last = strrchr(full, '\\');
	if (!last)
		last = strrchr(full, '/');
	last = last ? last + 1 : full;
	if (!last[0] || (int)strlen(last) >= size)
		return FALSE;
	for (index = 0; last[index]; index++)
		if (!((last[index] >= 'a' && last[index] <= 'z') || (last[index] >= 'A' && last[index] <= 'Z') ||
			(last[index] >= '0' && last[index] <= '9') || last[index] == '_' || last[index] == '-'))
		{
			return FALSE;
		}
	snprintf(folder, (size_t)size, "%s", last);
	if (path)
	{
		snprintf(path, (size_t)path_size, "%s\\blam.lst", full);
		path[path_size - 1] = 0;
	}
	return TRUE;
}

/* a game variant of the player's read (by its folder) and checked, its
flags cleared as it is shared: TRUE with it and its name */
static boolean fileshare_game_variant_read(char const *folder, struct game_variant *variant, char *title, int title_size,
	char *why, int why_size)
{
	long indices[FILESHARE_GAME_VARIANTS_MAXIMUM];
	int count = fileshare_game_variants(indices, FILESHARE_GAME_VARIANTS_MAXIMUM), index;

	for (index = 0; index < count; index++)
	{
		char each[P2P_FILESHARE_FILE_SIZE], path[MAXIMUM_FILENAME_LENGTH + 1];

		if (!fileshare_game_variant_folder(indices[index], each, sizeof(each), path, sizeof(path)) || strcmp(each, folder))
			continue;
		if (!playlist_profile_get_from_path(path, variant))
		{
			snprintf(why, (size_t)why_size, "%s", T("it is damaged (its checksum)"));
			return FALSE;
		}
		/* (shared as a player's own: not the game's, no default's number) */
		variant->flags = 0;
		if (!fileshare_game_variant_valid(variant, why, why_size))
			return FALSE;
		fileshare_variant_name(title, title_size, variant->human_readable_game_description);
		return TRUE;
	}
	snprintf(why, (size_t)why_size, "%s", T("it isn't there any more"));
	return FALSE;
}

/* ---------- map variants */

/* a map variant read through Forge's loader (its validator) and encoded
again: its size (0: why) */
static int fileshare_map_variant_read(char const *map, char const *file, char *title, int title_size, char *why,
	int why_size)
{
	int result, size;

	if (!forge_store_load(map, file, &fileshare_friends.layout, why, why_size))
		return 0;
	size = forge_file_encode(&fileshare_friends.layout, fileshare_friends.bytes, P2P_FILESHARE_MAXIMUM_BYTES, &result);
	if (size <= 0)
	{
		snprintf(why, (size_t)why_size, "%s", T("it can't be written as a file"));
		return 0;
	}
	snprintf(title, (size_t)title_size, "%s", fileshare_friends.layout.title);
	return size;
}

/* ---------- the list */

static void fileshare_friends_list(void)
{
	struct p2p_fileshare_local *local = fileshare_friends.local;
	int count = 0, index, forge_count, hashed = 0;

	forge_count = forge_store_list(NULL, fileshare_friends.forge_entries, FORGE_STORE_MAXIMUM_LISTED);
	for (index = 0; index < forge_count && count < P2P_FILESHARE_LOCAL_MAXIMUM; index++)
	{
		struct forge_store_entry const *entry = &fileshare_friends.forge_entries[index];
		struct p2p_fileshare_local *each = &local[count++];

		memset(each, 0, sizeof(*each));
		each->kind = P2P_FILESHARE_KIND_MAP_VARIANT;
		snprintf(each->map, sizeof(each->map), "%s", entry->map_name);
		snprintf(each->file, sizeof(each->file), "%s", entry->file_name);
		snprintf(each->title, sizeof(each->title), "%s", entry->title[0] ? entry->title : entry->file_name);
		each->size = (int)entry->size;
		each->damaged = entry->damaged;
	}
	{
		long indices[FILESHARE_GAME_VARIANTS_MAXIMUM];
		int variants = fileshare_game_variants(indices, FILESHARE_GAME_VARIANTS_MAXIMUM);

		for (index = 0; index < variants && count < P2P_FILESHARE_LOCAL_MAXIMUM; index++)
		{
			struct p2p_fileshare_local *each = &local[count];
			wchar_t name[MAX_GAMENAME];

			memset(each, 0, sizeof(*each));
			if (!fileshare_game_variant_folder(indices[index], each->file, sizeof(each->file), NULL, 0))
				continue;
			each->kind = P2P_FILESHARE_KIND_GAME_VARIANT;
			each->size = P2P_FILESHARE_GAME_VARIANT_BYTES;
			if (playlist_profile_get_display_name(indices[index], name))
				fileshare_variant_name(each->title, sizeof(each->title), name);
			count++;
		}
	}
	/* (the shared ones read, checked and hashed: only those that pass are
	listed to friends) */
	for (index = 0; index < count; index++)
	{
		struct p2p_fileshare_local *each = &local[index];
		char why[FORGE_STORE_WHY_SIZE], title[P2P_FILESHARE_TITLE_SIZE];
		int size = 0;

		if (each->damaged || !p2p_fileshare_marked(each->kind, each->map, each->file))
			continue;
		if (each->kind == P2P_FILESHARE_KIND_MAP_VARIANT)
			size = fileshare_map_variant_read(each->map, each->file, title, sizeof(title), why, sizeof(why));
		else
		{
			struct game_variant variant;

			if (fileshare_game_variant_read(each->file, &variant, title, sizeof(title), why, sizeof(why)))
			{
				memcpy(fileshare_friends.bytes, &variant, sizeof(variant));
				size = (int)sizeof(variant);
			}
		}
		if (size <= 0)
		{
			each->damaged = TRUE;
			platform_log("fileshare: %s %s shared but not listed: %s", p2p_fileshare_kind_text(each->kind), each->file,
				why);
			continue;
		}
		each->size = size;
		snprintf(each->title, sizeof(each->title), "%s", title);
		p2p_fileshare_digest(fileshare_friends.bytes, size, each->digest);
		each->hashed = TRUE;
		hashed++;
	}
	p2p_fileshare_set_local(local, count);
	platform_log("fileshare: this machine's list: %d files, %d of them shared", count, hashed);
}

/* ---------- jobs */

static void fileshare_friends_load(struct p2p_fileshare_job const *job)
{
	char why[FORGE_STORE_WHY_SIZE], title[P2P_FILESHARE_TITLE_SIZE];
	int size = 0;

	why[0] = 0;
	title[0] = 0;
	if (job->kind == P2P_FILESHARE_KIND_MAP_VARIANT)
		size = fileshare_map_variant_read(job->map, job->file, title, sizeof(title), why, sizeof(why));
	else if (job->kind == P2P_FILESHARE_KIND_GAME_VARIANT)
	{
		struct game_variant variant;

		if (fileshare_game_variant_read(job->file, &variant, title, sizeof(title), why, sizeof(why)))
		{
			memcpy(fileshare_friends.bytes, &variant, sizeof(variant));
			size = (int)sizeof(variant);
		}
	}
#ifndef HALO_RELEASE
	/* (the tests' damaged file: one byte of its payload flipped, its CRC
	left - the receiver's validator must refuse it) */
	if (size > FORGE_FILE_HEADER_SIZE + 8 && fileshare_friends.test_corrupt && !job->has_digest)
	{
		fileshare_friends.bytes[size - 8] ^= 0x5A;
		platform_log("fileshare: test: the file sent damaged (a byte of its payload flipped)");
	}
#endif
	p2p_fileshare_job_loaded(job->serial, size > 0 ? fileshare_friends.bytes : NULL, size, title, why);
}

static void fileshare_friends_save(struct p2p_fileshare_job const *job)
{
	char why[FORGE_STORE_WHY_SIZE], saved[FORGE_FILE_NAME_SIZE];

	why[0] = 0;
	if (job->kind == P2P_FILESHARE_KIND_MAP_VARIANT)
	{
		char reason[96];
		int result = forge_file_decode(fileshare_friends.bytes, job->size, NULL, &fileshare_friends.layout, reason,
			sizeof(reason));

		if (result != _forge_file_ok)
		{
			platform_log("fileshare: \"%s\" from %s refused by the validator: %s", job->title, job->friend_name, reason);
			p2p_fileshare_job_saved(job->serial, FALSE, T("it isn't a valid map variant"));
			return;
		}
		{
			char name[FORGE_FILE_NAME_SIZE];
			unsigned int hash = (unsigned int)job->digest[0] << 24 | (unsigned int)job->digest[1] << 16 |
				(unsigned int)job->digest[2] << 8 | job->digest[3];

			forge_file_name_received(hash, name, sizeof(name));
			if (!forge_store_save(&fileshare_friends.layout, name, saved, sizeof(saved), why, sizeof(why)))
			{
				p2p_fileshare_job_saved(job->serial, FALSE, why);
				return;
			}
		}
		platform_log("fileshare: \"%s\" (%s) from %s passed the validator: saved as %s/%s", fileshare_friends.layout.title,
			fileshare_friends.layout.map_name, job->friend_name, fileshare_friends.layout.map_name, saved);
		p2p_fileshare_job_saved(job->serial, TRUE, fileshare_friends.layout.title);
		p2p_fileshare_refresh();
		return;
	}
	if (job->kind == P2P_FILESHARE_KIND_GAME_VARIANT && job->size == P2P_FILESHARE_GAME_VARIANT_BYTES)
	{
		struct game_variant variant;
		char name[48];

		memcpy(&variant, fileshare_friends.bytes, sizeof(variant));
		if (!fileshare_game_variant_valid(&variant, why, sizeof(why)))
		{
			platform_log("fileshare: game variant \"%s\" from %s refused by the checks: %s", job->title, job->friend_name,
				why);
			p2p_fileshare_job_saved(job->serial, FALSE, why);
			return;
		}
		if (!menu_functions_gametype_save_received(&variant, name, sizeof(name), why, sizeof(why)))
		{
			p2p_fileshare_job_saved(job->serial, FALSE, why);
			return;
		}
		platform_log("fileshare: game variant from %s passed the checks: saved as \"%s\"", job->friend_name, name);
		p2p_fileshare_job_saved(job->serial, TRUE, name);
		p2p_fileshare_refresh();
		return;
	}
	p2p_fileshare_job_saved(job->serial, FALSE, T("it isn't a variant"));
}

/* ---------- a friend's offer: the question */

static int fileshare_friends_test_answer(void)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_TEST_FILESHARE_ANSWER");

	if (setting && !strcmp(setting, "accept"))
		return 1;
	if (setting && !strcmp(setting, "decline"))
		return 0;
#endif
	return -1;
}

static void fileshare_friends_answer(int accept)
{
	int outcome = p2p_fileshare_answer(fileshare_friends.asking, accept);

	platform_log("fileshare: %s's \"%s\" %s: %s", fileshare_friends.asking_name, fileshare_friends.asking_title,
		accept ? "accepted" : "declined", p2p_fileshare_outcome_text(outcome));
	fileshare_friends.asking[0] = 0;
}

static void fileshare_friends_offers(boolean main_menu_loaded, boolean other_question)
{
	int state = __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
	struct p2p_fileshare_offer offer;

	if (fileshare_friends.asking[0])
	{
		int answer = platform_question_answer();

		/* (left for a level or a lobby meanwhile: the question withdrawn, the
		offer declined - it was not answered) */
		if (!main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS)
		{
			platform_ask_question(NULL, NULL);
			fileshare_friends_answer(0);
			return;
		}
		if (answer >= 0)
		{
			platform_ask_question(NULL, NULL);
			fileshare_friends_answer(answer == 1);
		}
		return;
	}
	if (other_question || !main_menu_loaded || state != SYSTEM_LINK_STATE_MENUS || platform_question_asking())
		return;
	if (!p2p_fileshare_take_offer(&offer))
		return;
	snprintf(fileshare_friends.asking, sizeof(fileshare_friends.asking), "%s", offer.id);
	snprintf(fileshare_friends.asking_name, sizeof(fileshare_friends.asking_name), "%s", offer.friend_name);
	snprintf(fileshare_friends.asking_title, sizeof(fileshare_friends.asking_title), "%s", offer.title);
	if (fileshare_friends_test_answer() >= 0)
	{
		platform_log("fileshare: %s offered you \"%s\" (the test answers)", offer.friend_name, offer.title);
		fileshare_friends_answer(fileshare_friends_test_answer());
		return;
	}
	{
		char text[320];

		if (offer.kind == P2P_FILESHARE_KIND_MAP_VARIANT)
			snprintf(text, sizeof(text), T("%s wants to send you a map variant:\n\"%s\" (%s, %d KB)\n\nAccept it? It is "
				"checked before it's saved to your File Share."), offer.friend_name, offer.title, offer.map,
				(offer.size + 1023) / 1024);
		else
			snprintf(text, sizeof(text), T("%s wants to send you a game variant:\n\"%s\"\n\nAccept it? It is checked "
				"before it's saved to your game types."), offer.friend_name, offer.title);
		platform_ask_question(T("FILE FROM A FRIEND"), text);
	}
}

/* whether an offer's question is up (friends_game.c asks nothing else then) */
boolean fileshare_friends_asking(void)
{
	return fileshare_friends.asking[0] != 0;
}

/* ---------- the tests' steps (debug) */

#ifndef HALO_RELEASE
/* HALO_TEST_FILESHARE (this side's): share-all (every file of the list marked
shared once it is read), make-gametype=NAME (a game type of the player's,
Slayer's rules, made once at the menus), browse-at=S[:S...] (S seconds after
friends went online, the first friend online browsed), download=first|all
(each file listed downloaded, the question answered yes), send-at=S[:game]
(the first map variant - or game variant - sent to the first friend
online), corrupt (a file sent damaged: the receiver's validator must refuse
it) */
static void fileshare_friends_test_parse(void)
{
	char const *setting = getenv("HALO_TEST_FILESHARE");
	char copy[512];
	char *step, *save_pointer = NULL;

	fileshare_friends.test_parsed = TRUE;
	fileshare_friends.test_send_at = -1;
	if (!setting || !setting[0])
		return;
	snprintf(copy, sizeof(copy), "%s", setting);
	for (step = strtok_r(copy, ",", &save_pointer); step; step = strtok_r(NULL, ",", &save_pointer))
	{
		if (!strcmp(step, "share-all"))
			fileshare_friends.test_share_all = TRUE;
		else if (!strcmp(step, "corrupt"))
			fileshare_friends.test_corrupt = TRUE;
		else if (!strncmp(step, "make-gametype=", 14))
			snprintf(fileshare_friends.test_make_gametype, sizeof(fileshare_friends.test_make_gametype), "%s", step + 14);
		else if (!strncmp(step, "browse-at=", 10))
		{
			char *at = step + 10;

			while (at && *at && fileshare_friends.test_browse_count < FILESHARE_TEST_BROWSES)
			{
				fileshare_friends.test_browse_at[fileshare_friends.test_browse_count++] = atol(at);
				at = strchr(at, ':');
				at = at ? at + 1 : NULL;
			}
		}
		else if (!strcmp(step, "download=first"))
			fileshare_friends.test_download = 1;
		else if (!strcmp(step, "download=all"))
			fileshare_friends.test_download = 2;
		else if (!strncmp(step, "send-at=", 8))
		{
			fileshare_friends.test_send_at = atol(step + 8);
			fileshare_friends.test_send_kind = strstr(step, ":game") ? P2P_FILESHARE_KIND_GAME_VARIANT :
				P2P_FILESHARE_KIND_MAP_VARIANT;
		}
	}
	platform_log("fileshare: test steps: %s", setting);
}

/* the first friend online: its id, 0 if none */
static int fileshare_friends_test_friend(char *id, char *name)
{
	static struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	int count = p2p_friends_list(entries, P2P_FRIEND_ENTRIES_MAXIMUM), index;

	for (index = 0; index < count; index++)
		if (entries[index].kind == P2P_FRIEND_KIND_FRIEND && entries[index].online)
		{
			snprintf(id, P2P_FRIEND_ID_SIZE, "%s", entries[index].id);
			snprintf(name, P2P_FRIEND_NAME_SIZE, "%s", entries[index].name);
			return 1;
		}
	return 0;
}

static void fileshare_friends_test_steps(boolean main_menu_loaded)
{
	unsigned long now = system_milliseconds();
	char id[P2P_FRIEND_ID_SIZE], name[P2P_FRIEND_NAME_SIZE];

	if (!fileshare_friends.test_parsed)
		fileshare_friends_test_parse();
	if (fileshare_friends.test_make_gametype[0] && !fileshare_friends.test_made && main_menu_loaded)
	{
		struct game_variant variant, temporary;
		char saved[48], why[160];
		int index;

		fileshare_friends.test_made = TRUE;
		{
			long indices[FILESHARE_GAME_VARIANTS_MAXIMUM];
			int count = fileshare_game_variants(indices, FILESHARE_GAME_VARIANTS_MAXIMUM), each;

			for (each = 0; each < count; each++)
			{
				wchar_t wide[MAX_GAMENAME];
				char shown[16];

				if (playlist_profile_get_display_name(indices[each], wide))
				{
					fileshare_variant_name(shown, sizeof(shown), wide);
					if (!strcmp(shown, fileshare_friends.test_make_gametype))
						break;
				}
			}
			if (each < count)
			{
				platform_log("fileshare: test: game type \"%s\" is there", fileshare_friends.test_make_gametype);
				return;
			}
		}
		variant = *build_game_variant_slayer(&temporary);
		variant.flags = 0;
		variant.universal_variant.score_to_win = 7;
		memset(variant.human_readable_game_description, 0, sizeof(variant.human_readable_game_description));
		for (index = 0; index < 11 && fileshare_friends.test_make_gametype[index]; index++)
			variant.human_readable_game_description[index] = (wchar_t)fileshare_friends.test_make_gametype[index];
		platform_log("fileshare: test: game type \"%s\" %s", fileshare_friends.test_make_gametype,
			menu_functions_gametype_save_received(&variant, saved, sizeof(saved), why, sizeof(why)) ? "made" : why);
		p2p_fileshare_refresh();
	}
	if (!p2p_friends_wanted())
	{
		fileshare_friends.test_started = 0;
		return;
	}
	if (!fileshare_friends.test_started)
		fileshare_friends.test_started = now ? now : 1;
	if (fileshare_friends.test_share_all && !fileshare_friends.test_shared)
	{
		static struct p2p_fileshare_local entries[P2P_FILESHARE_LOCAL_MAXIMUM];
		int count = p2p_fileshare_local(entries, P2P_FILESHARE_LOCAL_MAXIMUM), index, marked = 0;

		if (count)
		{
			fileshare_friends.test_shared = TRUE;
			for (index = 0; index < count; index++)
				if (!entries[index].damaged &&
					p2p_fileshare_mark(entries[index].kind, entries[index].map, entries[index].file, 1) == P2P_FILESHARE_DONE)
				{
					marked++;
				}
			platform_log("fileshare: test: %d files marked shared", marked);
		}
	}
	if (fileshare_friends.test_browses_done < fileshare_friends.test_browse_count &&
		now - fileshare_friends.test_started >=
		(unsigned long)fileshare_friends.test_browse_at[fileshare_friends.test_browses_done] * 1000UL)
	{
		fileshare_friends.test_browses_done++;
		if (fileshare_friends_test_friend(id, name))
		{
			platform_log("fileshare: test: browsing %s: %s", name, p2p_fileshare_outcome_text(p2p_fileshare_browse(id)));
			fileshare_friends.test_downloaded = 0;
			fileshare_friends.test_last_state = -1;
		}
		else
			platform_log("fileshare: test: no friend online to browse");
	}
	{
		static struct p2p_fileshare_remote remote[P2P_FILESHARE_SHARED_MAXIMUM];
		int count = 0, state = p2p_fileshare_browse_state(name, id, remote, P2P_FILESHARE_SHARED_MAXIMUM, &count);

		if (state != fileshare_friends.test_last_state)
		{
			fileshare_friends.test_last_state = state;
			if (state == P2P_FILESHARE_BROWSE_LISTED)
			{
				int index;

				for (index = 0; index < count; index++)
					platform_log("fileshare: test: %s shares %s \"%s\" (%s, %d bytes)", name,
						p2p_fileshare_kind_text(remote[index].kind), remote[index].title, remote[index].map,
						remote[index].size);
			}
		}
		if (state == P2P_FILESHARE_BROWSE_LISTED && fileshare_friends.test_download &&
			fileshare_friends.test_downloaded < count && (fileshare_friends.test_download == 2 ||
			!fileshare_friends.test_downloaded) && !p2p_fileshare_progress(NULL) &&
			now - fileshare_friends.test_download_time >= 6000)
		{
			int outcome = p2p_fileshare_download(remote[fileshare_friends.test_downloaded].id);

			fileshare_friends.test_download_time = now;
			platform_log("fileshare: test: downloading \"%s\" (the question answered yes): %s",
				remote[fileshare_friends.test_downloaded].title, p2p_fileshare_outcome_text(outcome));
			if (outcome == P2P_FILESHARE_DONE)
				fileshare_friends.test_downloaded++;
		}
	}
	if (fileshare_friends.test_send_at >= 0 && !fileshare_friends.test_sent &&
		now - fileshare_friends.test_started >= (unsigned long)fileshare_friends.test_send_at * 1000UL)
	{
		static struct p2p_fileshare_local entries[P2P_FILESHARE_LOCAL_MAXIMUM];
		int count = p2p_fileshare_local(entries, P2P_FILESHARE_LOCAL_MAXIMUM), index;

		fileshare_friends.test_sent = TRUE;
		for (index = 0; index < count; index++)
			if (entries[index].kind == fileshare_friends.test_send_kind && !entries[index].damaged)
				break;
		if (index >= count)
			platform_log("fileshare: test: nothing of that kind to send");
		else if (!fileshare_friends_test_friend(id, name))
			platform_log("fileshare: test: no friend online to send to");
		else
			platform_log("fileshare: test: sending \"%s\" to %s: %s", entries[index].title, name,
				p2p_fileshare_outcome_text(p2p_fileshare_send(id, entries[index].kind, entries[index].map,
				entries[index].file)));
	}
}
#endif

/* ---------- each frame (friends_game.c) */

void fileshare_friends_update(boolean main_menu_loaded, boolean other_question)
{
	struct p2p_fileshare_job job;
	int jobs;

#ifndef HALO_RELEASE
	fileshare_friends_test_steps(main_menu_loaded);
#endif
	/* (this machine's list: read only while friends are online - nobody asks
	for it otherwise, and a Vita's start reads no file it does not need) */
	if (p2p_friends_wanted() && p2p_fileshare_local_wanted())
		fileshare_friends_list();
	/* (a job or two a frame: each a small file's read or write) */
	for (jobs = 0; jobs < 2; jobs++)
	{
		int type = p2p_fileshare_take_job(&job, fileshare_friends.bytes, P2P_FILESHARE_MAXIMUM_BYTES);

		if (type == P2P_FILESHARE_JOB_LOAD)
			fileshare_friends_load(&job);
		else if (type == P2P_FILESHARE_JOB_SAVE)
			fileshare_friends_save(&job);
		else
			break;
	}
	fileshare_friends_offers(main_menu_loaded, other_question);
}
