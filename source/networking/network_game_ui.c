/*
NETWORK_GAME_UI.C

symbols in this file:
0011AE30 0060:
	_network_game_get_random_player_name (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "game/players.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

char const *config_string(char const *name);

/* ---------- globals */

/* ---------- public code */

wchar_t const *network_game_get_random_player_name(
	void)
{
	wchar_t const *player_name = L"";
	long string_list_index = tag_loaded('ustr', "ui\\random_player_names");

	if (string_list_index != NONE)
	{
		struct string_list *string_list = unicode_string_list_definition_get(string_list_index);

		if (string_list)
		{
			short string_index = seed_random_range(
				get_global_local_random_seed_address(),
				0,
				string_list->strings.count - 1);
			player_name = unicode_string_list_get_string(string_list_index, string_index);
		}
	}

	return player_name;
}

/* port: the name a local player goes by in a network game. Its profile's,
if it has one; a profile without one (the Xbox's default profiles, which
the multiplayer profile list offers) goes by this machine's user's name,
network.player_name (the Vita's user name, vita_settings.c), kept to what
player_name_clean leaves (UTF-8 read as UTF-16, cut to the name's length);
else "Player", as the host names a player whose name is empty. The profile
itself is left as it is (nothing saves the name into it). TRUE if the name
is the profile's or the user's, FALSE for "Player". */
boolean network_game_local_player_name(
	wchar_t const *profile_name,
	wchar_t *name,
	long count)
{
	char const *user_name;
	long length = 0;

	match_assert("c:\\halo\\SOURCE\\networking\\network_game_ui.c", __LINE__, name && count > 1);

	if (profile_name && profile_name[0])
	{
		ustrncpy(name, profile_name, count - 1);
		name[count - 1] = 0;
		return TRUE;
	}

	user_name = config_string("network.player_name");
	while (user_name && *user_name && length < count - 1)
	{
		unsigned char lead = (unsigned char)*user_name++;
		unsigned long character = lead;
		long more = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;

		if (more)
			character = lead & (0x3F >> more);
		while (more-- > 0 && ((unsigned char)*user_name & 0xC0) == 0x80)
			character = (character << 6) | ((unsigned char)*user_name++ & 0x3F);
		/* (one a name cannot hold, past the 16-bit range or a stray byte:
		player_name_clean leaves "?" out of the ban command's count) */
		name[length++] = (wchar_t)(character < 0x10000 && (lead < 0x80 || lead >= 0xC0) ? character : '?');
	}
	name[length] = 0;
	if (length && player_name_clean(name, count))
		return TRUE;

	ustrncpy(name, L"Player", count - 1);
	name[count - 1] = 0;
	return FALSE;
}

/* ---------- private code */
