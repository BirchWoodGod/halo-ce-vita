/*
VITA_SETTINGS_TEST.C

A desktop test of the settings panel (port/vita/host/vita_settings.c,
included whole), with the calls it makes into internet play (p2p.h), ad hoc
play (vita_net.c) and the renderer (vgxm_menu_set) recorded instead: the
panel opened with SELECT+START on its first tab, its five tabs (Game,
Controls, Video, Sound, Multiplayer, as Halo Online's settings; and Dev)
switched with L and R, each tab's rows under its headings (not chosen: up
and down step over them), no page longer than a screen (the values in a
column, a help line and a line of the panel's buttons), the pages a row
opens and circle going back; the Multiplayer tab and its Play page (the
rows each Connection has, Host a game's steps, the lobby name and password
typed on the system's keyboard and kept as printable ASCII, Max players,
Visibility at once, Host co-op campaign asking the game for its Campaign
screen, a code typed with the D-pad and joined, the public lobby listed and
joined, Games on this network, an ad hoc group joined first, the game
seeing no buttons while a system dialog is up; 1.0.3's co-op choice loading
as Off), its Modded
maps page (a folder of fake maps: listed, turned off and on, deleted after
a confirmation, the map in play kept; Map downloads' three choices), its Mod
manager page (the game's look answered by the test: each entry with On or
Error, its size and kind, why it failed or what it needs on the help line,
maps and mods turned off and on and kept in settings.txt, the details, a
delete asked of the game after a question, in use, a name the game does not
take never deleted; the game's lists set through vita_settings_set), the
Dev tab behind its switch (switches saved only while on, the timing
variables, Save report's folder),
Controls' pages (Button layout and Touch zones in Xbox terms, Gyro
settings with the gyroscope's line, saved and read back), Button icons
(PlayStation at once, the panel then naming the Xbox buttons by what they
do and the guide the Vita's buttons, a remap followed by the game's icon,
kept by Reset controls), every settings
variable of 1.0.3 still a row, a settings.txt of 1.0 loading, and one saved
with a profile (1.0.3's, the 1.1 betas') loading as this build's profile
(all three Dynamic resolution, from 50%, 63% and 75%; Quality's AI
Adaptive), Custom's rows kept and env.txt's standing; then the
Language row and the panel in Spanish (test_languages: every page, line
and value fitting, every string with a Spanish entry). It runs
in a folder of its own, with ux0:data/haloce-vita made there.

Run port/vita/tests/run_vita_settings_test.sh.
*/

#include "../host/vita_settings.c"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

volatile unsigned long halo_settings_generation;
/* (the game's side of Host a game and Join a game: system_link_shortcut.c) */
volatile int halo_system_link_request;
volatile int halo_system_link_answer;
volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];
volatile int halo_pc_menus_state;
volatile int halo_text_input_state;
volatile int halo_settings_panel_request;
volatile int halo_adhoc_request;
volatile int halo_adhoc_group;
volatile int halo_network_connection = -1;
volatile int halo_network_phase;
char halo_text_input_title[HALO_TEXT_INPUT_SIZE];
char halo_text_input_text[HALO_TEXT_INPUT_SIZE];
int halo_text_input_maximum;
int halo_text_input_password;
int (*halo_test_setting_hook)(const char *variable, const char *value);
int halo_screen_restart_needed(void) { return 0; }
/* (the game's side of the Mod manager page: mod_manager.c; the test answers) */
volatile int halo_mods_request;
volatile int halo_mods_answer;
volatile int halo_mods_target_kind;
char halo_mods_target[HALO_MODS_NAME_BYTES];
struct halo_mods_inventory halo_mods_inventory;
/* (the restart offer, close_panel) the game at its main menu, idle: off but
in test_restart_offer; the restarts asked for (none of them returns here) */
static int main_menu_idle_now, restarts_asked;
int halo_main_menu_idle(void) { return main_menu_idle_now; }
int vita_host_restart_game(void) { restarts_asked++; return 0x80010013; }
/* (port/linux/src/network_wire.c's: the wire the game runs, a test's) */
static int test_crossplay_wire;
int network_wire_crossplay(void) { return test_crossplay_wire; }
/* (the map in play: never deleted) */
int halo_cache_map_in_use(const char *name) { return !strcmp(name, "inplay"); }

/* (the Forge page's: forge.c's maps - three, the last two in the maps
folder - the Forge game asked for, and forge_store.c's map variants, a
test's) */
volatile int halo_forge_request_map = -1;
char halo_forge_request_variant[HALO_FORGE_VARIANT_SIZE];
static const char *const test_forge_maps[3][2] = {
	{ "Battle Creek", "beavercreek" }, { "Blood Gulch", "bloodgulch" }, { "Chill Out", "chillout" },
};
/* (the Player model row's: player_models.c's models - none found unless a
test says so - and the profile's choice, as the game keeps it) */
static int test_model_count = 1, test_model_chosen, test_model_choice = -2;
static const char *const test_model_names[3] = { "Spartan", "Spartan (Extinction)", "Covenant V Marines Beta 5" };
int player_models_panel_count(void) { return test_model_count; }
const char *player_models_panel_name(int index) { return index >= 0 && index < 3 ? test_model_names[index] : "Spartan"; }
int player_models_panel_chosen(void) { return test_model_chosen; }
void player_models_panel_choose(int index) { test_model_choice = test_model_chosen = index; }
int forge_panel_map_count(void) { return 3; }
const char *forge_panel_map_name(int map_index) { return map_index >= 0 && map_index < 3 ? test_forge_maps[map_index][0] : ""; }
const char *forge_panel_map_short(int map_index) { return map_index >= 0 && map_index < 3 ? test_forge_maps[map_index][1] : ""; }
int forge_panel_map_available(int map_index) { return map_index == 1 || map_index == 2; }
/* (a Forge game played, with its placed characters alive and dead; what
the panel asked cleared: -1 nothing, 0 the dead, 1 all) */
static int test_forge_playing, test_forge_alive, test_forge_dead, test_forge_cleared = -1;
int forge_panel_ai_counts(int *alive, int *dead)
{
	if (alive)
		*alive = test_forge_alive;
	if (dead)
		*dead = test_forge_dead;
	return test_forge_playing;
}
void forge_panel_clear_ai(int all) { test_forge_cleared = all; }
static struct forge_store_entry test_forge_entries[4];
static int test_forge_count, test_forge_deleted;
int forge_store_list(const char *map_name, struct forge_store_entry *entries, int capacity)
{
	int index, count = 0;

	(void)map_name;
	for (index = 0; index < test_forge_count && count < capacity; index++)
		entries[count++] = test_forge_entries[index];
	return count;
}
int forge_store_delete(const char *map_name, const char *file_name)
{
	int index;

	for (index = 0; index < test_forge_count; index++)
		if (!strcmp(test_forge_entries[index].map_name, map_name) && !strcmp(test_forge_entries[index].file_name, file_name))
		{
			memmove(&test_forge_entries[index], &test_forge_entries[index + 1],
				sizeof(test_forge_entries[0]) * (size_t)(test_forge_count - index - 1));
			test_forge_count--;
			test_forge_deleted++;
			return 1;
		}
	return 0;
}

static void test_forge_entry(const char *map_name, const char *file_name, const char *title, int objects, int damaged)
{
	struct forge_store_entry *entry = &test_forge_entries[test_forge_count++];

	memset(entry, 0, sizeof(*entry));
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	snprintf(entry->file_name, sizeof(entry->file_name), "%s", file_name);
	snprintf(entry->title, sizeof(entry->title), "%s", damaged ? file_name : title);
	snprintf(entry->date, sizeof(entry->date), "2026-10-18 12:00");
	entry->objects = (short)objects;
	entry->damaged = damaged;
}
static char log_text[16384];
static int overlay_level = -1;

/* (vita_ce_installer.c's: whether a Custom Edition installer is there, and
the extraction started) */
static int installer_state, installer_starts;
int vita_ce_installer_state(char *name, int size)
{
	snprintf(name, (size_t)size, "halocesetup_en_1.00.exe");
	return installer_state;
}
int vita_ce_installer_start(void)
{
	installer_starts++;
	if (installer_state != 1)
		return -1;
	installer_state = 2;
	return 0;
}

/* (update_notify.c's: the status a test sets, the presses counted) */
static int update_status_value = UPDATE_STATUS_NONE, update_requests;
static char update_version_value[UPDATE_VERSION_SIZE];
int update_check_request(void)
{
	update_requests++;
	update_status_value = UPDATE_STATUS_CHECKING;
	return update_status_value;
}
int update_check_status(char *version, int size)
{
	if (version && size > 0)
		snprintf(version, (size_t)size, "%s", update_version_value);
	return update_status_value;
}
/* (the updater's, vita_update.c, vita_update_verify.c: whether this build
holds an update key, the presses that start a download, a downloaded VPK) */
static int test_update_key, update_starts;
static char update_ready_version[UPDATE_VERSION_SIZE];
int update_public_key(unsigned char key[32])
{
	memset(key, test_update_key, 32);
	return test_update_key;
}
int vita_update_start(void)
{
	if (update_status_value != UPDATE_STATUS_AVAILABLE)
		return -1;
	update_starts++;
	return 0;
}
int vita_update_running(void) { return 0; }
int vita_update_ready(char *version, int version_size, char *path, int path_size)
{
	snprintf(version, (size_t)version_size, "%s", update_ready_version);
	snprintf(path, (size_t)path_size, "ux0:data/haloce-vita/update/halo-%s.vpk", update_ready_version);
	return update_ready_version[0] != 0;
}

/* (vita_input.c's) */
void vita_gyro_status(char *text, int size) { snprintf(text, (size_t)size, "Gyro: yaw -999 pitch -999 roll -999 lay still"); }

int vita_host_thread_start(const char *name, void (*function)(void *), void *argument, int core)
{
	(void)name;
	(void)core;
	function(argument);
	return 0;
}

int sceRtcGetCurrentClockLocalTime(SceDateTime *time)
{
	time->year = 2026;
	time->month = 10;
	time->day = 6;
	time->hour = 15;
	time->minute = 30;
	time->second = 12;
	return 0;
}

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* ---------- what the panel calls, recorded */

static unsigned long long clock_us = 1000000;
static char menu[3072];
static int menu_selected, menu_visible;
static char joined_code[32];
static int refreshes;
static int lobby_public = -1, lobby_coop_public = -1, browsing, adhoc_connects, adhoc_mode = -1, adhoc_room = -1, adhoc_state_value;
static char lobby_name[64], lobby_password[64], joined_id[40], joined_password[64];
static int ime_opens, ime_open_now, ime_result, ime_maximum, ime_password;
static char ime_title[64], ime_initial[64], ime_typed[128];
static int hosting;

SceUInt64 sceKernelGetProcessTimeWide(void) { return clock_us; }

int sceAppUtilSystemParamGetString(unsigned int paramId, SceChar8 *buf, SceSize bufSize)
{
	(void)paramId;
	snprintf((char *)buf, bufSize, "vitauser");
	return 0;
}

/* (the Vita's system language: English US, SCE_SYSTEM_PARAM_LANG_ENGLISH_US) */
static int system_language_value = 1;

int sceAppUtilSystemParamGetInt(unsigned int paramId, int *value)
{
	if (paramId != SCE_SYSTEM_PARAM_ID_LANG)
		return -1;
	*value = system_language_value;
	return 0;
}

void vgxm_menu_set(const char *text, int selected)
{
	menu_visible = text != NULL;
	snprintf(menu, sizeof(menu), "%s", text ? text : "");
	menu_selected = selected;
}

void vgxm_overlay_enable(int enabled) { overlay_level = enabled; }
void vgxm_upscale_filter_set(int filter) { (void)filter; }
void vita_host_log(const char *line)
{
	size_t used = strlen(log_text);

	snprintf(log_text + used, sizeof(log_text) - used, "%s\n", line);
}

int p2p_join_code(const char *code)
{
	snprintf(joined_code, sizeof(joined_code), "%s", code);
	return 1;
}

/* (friends, p2p_friends.h: the test's list, and what the panel asked) */
static struct p2p_friend_entry test_friends[8];
static int test_friend_count;
static char friend_added[64], friend_acted_id[P2P_FRIEND_ID_SIZE];
static int friend_acted_kind = -1, friend_acted_action = -1, friend_act_outcome;

static void test_friend(const char *id, int kind, const char *name, const char *code, int online, int joinable,
	const char *activity)
{
	struct p2p_friend_entry *entry = &test_friends[test_friend_count++];

	memset(entry, 0, sizeof(*entry));
	snprintf(entry->id, sizeof(entry->id), "%s", id);
	entry->kind = kind;
	snprintf(entry->name, sizeof(entry->name), "%s", name);
	snprintf(entry->code, sizeof(entry->code), "%s", code);
	entry->online = online;
	entry->joinable = joinable;
	entry->invitable = online;
	entry->players = joinable ? 5 : 0;
	entry->maximum = joinable ? 16 : 0;
	snprintf(entry->activity, sizeof(entry->activity), "%s", activity);
}
int p2p_friends_code(char *code, int size)
{
	snprintf(code, (size_t)size, "KQ7M-3XPA-T9WE");
	return 1;
}
int p2p_friends_list(struct p2p_friend_entry *entries, int maximum_count)
{
	int count = test_friend_count < maximum_count ? test_friend_count : maximum_count;

	memcpy(entries, test_friends, sizeof(entries[0]) * (size_t)count);
	return count;
}
int p2p_friends_status(char *text, int size)
{
	snprintf(text, (size_t)size, "1 of 2 friends online");
	return 1;
}
int p2p_friends_add(const char *code)
{
	snprintf(friend_added, sizeof(friend_added), "%s", code);
	return P2P_FRIENDS_ADD_SENT;
}
int p2p_friends_act(const char *id, int kind, int action)
{
	snprintf(friend_acted_id, sizeof(friend_acted_id), "%s", id);
	friend_acted_kind = kind;
	friend_acted_action = action;
	return friend_act_outcome;
}
const char *p2p_friends_outcome_text(int outcome)
{
	return outcome == P2P_FRIENDS_DONE ? "Done" : outcome == P2P_FRIENDS_OFFLINE ? "Go online first" : "Not for this one";
}
const char *p2p_friends_add_text(int result)
{
	return result == P2P_FRIENDS_ADD_SENT ? "Friend request sent" : "That's not a friend code";
}

/* (the party, p2p_party.h: the test's) */
static struct p2p_party_member test_party[P2P_PARTY_MAXIMUM];
static int test_party_count, test_party_leading;
static struct p2p_party_invite test_party_invites[2];
static int test_party_invite_count;
static char party_acted[64];
int p2p_party_members(struct p2p_party_member *members, int maximum_count)
{
	int count = test_party_count < maximum_count ? test_party_count : maximum_count;

	memcpy(members, test_party, sizeof(members[0]) * (size_t)count);
	return count;
}
int p2p_party_invites(struct p2p_party_invite *invites, int maximum_count)
{
	int count = test_party_invite_count < maximum_count ? test_party_invite_count : maximum_count;

	memcpy(invites, test_party_invites, sizeof(invites[0]) * (size_t)count);
	return count;
}
int p2p_party_status(char *text, int size)
{
	snprintf(text, (size_t)size, test_party_count > 1 ? "Party: %d of 8 (Alpha leads)" : "Not in a party: invite a friend",
		test_party_count);
	return test_party_count > 1 ? test_party_count : 0;
}
int p2p_party_leading(void)
{
	return test_party_leading;
}
int p2p_party_friend_state(const char *friend_id)
{
	int index;

	for (index = 0; index < test_party_count; index++)
		if (!strcmp(test_party[index].id, friend_id))
			return 1;
	return 0;
}
int p2p_party_invite(const char *friend_id)
{
	snprintf(party_acted, sizeof(party_acted), "invite %s", friend_id);
	return P2P_PARTY_DONE;
}
int p2p_party_kick(const char *member_id)
{
	snprintf(party_acted, sizeof(party_acted), "kick %s", member_id);
	return P2P_PARTY_DONE;
}
int p2p_party_mute(const char *member_id, int muted)
{
	snprintf(party_acted, sizeof(party_acted), "%s %s", muted ? "mute" : "unmute", member_id);
	return P2P_PARTY_DONE;
}
int p2p_party_leave(void)
{
	snprintf(party_acted, sizeof(party_acted), "leave");
	return P2P_PARTY_DONE;
}
int p2p_party_answer(const char *invite_id, int accept)
{
	snprintf(party_acted, sizeof(party_acted), "%s %s", accept ? "accept" : "decline", invite_id);
	return P2P_PARTY_DONE;
}
int p2p_party_join_warning(int players, int maximum, char *text, int size)
{
	if (test_party_count < 2 || !test_party_leading || maximum - players >= test_party_count)
		return 0;
	snprintf(text, (size_t)size, "Your party has %d players but this game has %d open slots", test_party_count,
		maximum - players);
	return 1;
}
const char *p2p_party_outcome_text(int outcome)
{
	return outcome == P2P_PARTY_DONE ? "Done" : outcome == P2P_PARTY_NOT_IN_PARTY ? "Not in a party" : "Not for this one";
}
static char party_said[P2P_PARTY_CHAT_SIZE];
int p2p_party_chat_lines(struct p2p_party_chat_line *lines, int maximum_count, unsigned long after)
{
	static const char *const said[][2] = { { "Bravo", "hi" }, { "Delta #K7QX", "ready?" }, { "", "Echo joined the party" },
		{ "Bravo", "go go" } };
	int count = 0, index;

	(void)after;
	for (index = 0; index < 4 && count < maximum_count && test_party_count > 1; index++)
	{
		memset(&lines[count], 0, sizeof(lines[count]));
		lines[count].number = (unsigned long)index + 1;
		snprintf(lines[count].name, sizeof(lines[count].name), "%s", said[index][0]);
		snprintf(lines[count].text, sizeof(lines[count].text), "%s", said[index][1]);
		lines[count].notice = !said[index][0][0];
		count++;
	}
	return count;
}
const char *p2p_party_chat_preset_text(int preset)
{
	static const char *const presets[P2P_PARTY_PRESETS] = { "Ready!", "Wait for me", "Let's go", "Good game", "Join me",
		"Back in a minute", "Yes", "No" };

	return preset >= 0 && preset < P2P_PARTY_PRESETS ? presets[preset] : "";
}
int p2p_party_chat_preset(int preset)
{
	snprintf(party_said, sizeof(party_said), "preset %d", preset);
	return P2P_PARTY_DONE;
}
int p2p_party_chat_send(const char *text)
{
	snprintf(party_said, sizeof(party_said), "text %s", text);
	return P2P_PARTY_DONE;
}

int p2p_hosting_code(char *code, int size)
{
	if (!hosting)
		return 0;
	snprintf(code, (size_t)size, "QX7K-M2PA");
	return 1;
}

/* the hosted game's listing as p2p_lobby.c says it (P2P_LOBBY_HOSTING_*) */
static int hosting_listing = P2P_LOBBY_HOSTING_NONE;

int p2p_lobby_hosting_status(char *text, int size)
{
	snprintf(text, (size_t)size, "%s", hosting_listing == P2P_LOBBY_HOSTING_LISTED ? "Listed on 2 of 3 servers" :
		hosting_listing == P2P_LOBBY_HOSTING_UNREACHABLE ? "Not listed: can't reach the online game list" :
		hosting_listing == P2P_LOBBY_HOSTING_PENDING ? "Getting listed..." : "");
	return hosting_listing;
}

int p2p_lobby_browse_status(char *text, int size)
{
	snprintf(text, (size_t)size, "Looking for public games...");
	return P2P_LOBBY_BROWSE_LOOKING;
}

void p2p_lobby_set_public(int listed) { lobby_public = listed; }
void p2p_lobby_set_coop_public(int listed) { lobby_coop_public = listed; }
void p2p_lobby_browse(int on) { browsing = on; }
void p2p_lobby_set_name(const char *name) { snprintf(lobby_name, sizeof(lobby_name), "%s", name ? name : ""); }

void p2p_lobby_refresh(void) { refreshes++; }

/* (OpenCE's server browser: entries by id, with a password) */
void p2p_lobby_set_password(const char *password)
{
	snprintf(lobby_password, sizeof(lobby_password), "%s", password ? password : "");
}

int p2p_lobby_join(const char *id, const char *password)
{
	snprintf(joined_id, sizeof(joined_id), "%s", id);
	snprintf(joined_password, sizeof(joined_password), "%s", password ? password : "");
	return 1;
}

int p2p_lobby_join_state(void) { return P2P_LOBBY_JOIN_IDLE; }

/* this Vita's own game, an open one and a locked Halo PC one; with
Crossplay on, PC games (p2p_lobby.c's badges, crossplay_guard.h): a large
one, a co-op one and a server */
int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	if (test_crossplay_wire && browsing && index > 0 && index < 4)
	{
		static const char *const names[] = { "", "pc host", "pc coop", "pc server" };

		memset(entry, 0, sizeof(*entry));
		entry->compatible = 1;
		snprintf(entry->id, sizeof(entry->id), "%032d", 10 + index);
		snprintf(entry->name, sizeof(entry->name), "%s", names[index]);
		snprintf(entry->map, sizeof(entry->map), index == 2 ? "Halo" : "Blood Gulch");
		snprintf(entry->rules, sizeof(entry->rules), index == 2 ? "Co-op: Halo" : "Slayer on Blood Gulch");
		entry->players = index == 1 ? 20 : 2;
		entry->maximum = index == 1 ? 128 : 16;
		/* (p2p_lobby.c's: a co-op game is experimental too) */
		entry->experimental = index == 1 || index == 2;
		entry->coop = index == 2;
		entry->dedicated = index == 3;
		snprintf(entry->badge, sizeof(entry->badge), "%s", index == 1 ? "PC EXPERIMENTAL" : index == 2 ? "PC CO-OP" :
			"PC SERVER");
		return 1;
	}
	if (!browsing || index >= 3)
		return 0;
	memset(entry, 0, sizeof(*entry));
	entry->compatible = 1;
	entry->maximum = 16;
	if (index == 0)
	{
		snprintf(entry->id, sizeof(entry->id), "%032d", 1);
		snprintf(entry->name, sizeof(entry->name), "this vita");
		entry->players = 1;
		entry->own = 1;
	}
	else if (index == 1)
	{
		snprintf(entry->id, sizeof(entry->id), "%032d", 2);
		snprintf(entry->name, sizeof(entry->name), "desktop host");
		snprintf(entry->map, sizeof(entry->map), "Blood Gulch");
		snprintf(entry->rules, sizeof(entry->rules), "Slayer to 25 on Blood Gulch");
		snprintf(entry->players_line, sizeof(entry->players_line), "2 of 16: alpha, bravo");
		entry->players = 2;
	}
	else
	{
		snprintf(entry->id, sizeof(entry->id), "%032d", 3);
		snprintf(entry->name, sizeof(entry->name), "locked one");
		snprintf(entry->map, sizeof(entry->map), "Wizard");
		snprintf(entry->rules, sizeof(entry->rules), "CTF on Wizard (HALO PC)");
		entry->players = 1;
		entry->locked = 1;
		entry->pc_map = 1;
	}
	return 1;
}

/* (vita_ime.c's keyboard: opened, then closed with ime_result and
ime_typed once a test says so) */
int vita_ime_open(const char *title, const char *text, int maximum_length, int password)
{
	ime_opens++;
	ime_open_now = 1;
	ime_result = 0;
	snprintf(ime_title, sizeof(ime_title), "%s", title);
	snprintf(ime_initial, sizeof(ime_initial), "%s", text);
	ime_maximum = maximum_length;
	ime_password = password;
	return 0;
}

int vita_ime_poll(char *text, int size)
{
	if (!ime_open_now)
		return -1;
	if (!ime_result)
		return 0;
	ime_open_now = 0;
	if (ime_result > 0)
		snprintf(text, (size_t)size, "%s", ime_typed);
	return ime_result;
}

int p2p_status(char *text, int size)
{
	snprintf(text, (size_t)size, "ready");
	return 1;
}

/* (p2p.c's, in the language chosen) */
int p2p_status_shown(char *text, int size)
{
	snprintf(text, (size_t)size, "%s", T("ready"));
	return 1;
}

int p2p_adhoc_status(char *text, int size)
{
	snprintf(text, (size_t)size, "in the group, 1 other machine");
	return 1;
}

int vita_adhoc_connect(int mode, int room)
{
	adhoc_connects++;
	adhoc_mode = mode;
	adhoc_room = room;
	adhoc_state_value = 1;
	return 0;
}

void vita_adhoc_leave(void) { adhoc_state_value = 0; }

int vita_adhoc_state(char *text, int size)
{
	if (text && size > 0)
		snprintf(text, (size_t)size, "ad hoc: state %d", adhoc_state_value);
	return adhoc_state_value;
}

/* ---------- the pad */

/* one frame of the pad with these buttons held, 16 ms on */
static int frame(unsigned long buttons)
{
	struct vita_host_pad pad;

	memset(&pad, 0, sizeof(pad));
	pad.buttons = buttons;
	clock_us += 16667;
	return vita_settings_input(&pad);
}

/* a press and its release */
static void press(unsigned long button)
{
	frame(button);
	frame(0);
}

static void open_panel(void)
{
	int index;

	for (index = 0; index < 60; index++)
		frame(VITA_BUTTON_SELECT | VITA_BUTTON_START);
	frame(0);
}

/* the menu's line (0: the title) */
static const char *menu_line(int wanted, char *line, int size)
{
	const char *start = menu;
	int index;

	for (index = 0; index < wanted && start; index++)
	{
		start = strchr(start, '\n');
		if (start)
			start++;
	}
	if (!start)
		return "";
	snprintf(line, (size_t)size, "%.*s", (int)strcspn(start, "\n"), start);
	return line;
}

/* the file's text ("" if there is none) */
static const char *file_text(const char *path)
{
	static char text[4096];
	FILE *file = fopen(path, "r");
	size_t size = 0;

	if (file)
	{
		size = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	text[size] = 0;
	return text;
}

static void write_file(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");

	fwrite(data, 1, size, file);
	fclose(file);
}

/* a fake map: the cache header's signature, version and scenario type
(0x60: 0 solo, 1 multiplayer), then zeros */
static void write_map_of_type(const char *path, unsigned version, unsigned type, size_t size)
{
	static unsigned char bytes[300000];

	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, "daeh", 4);
	bytes[4] = version & 0xFF;
	bytes[5] = (version >> 8) & 0xFF;
	bytes[0x60] = type & 0xFF;
	write_file(path, bytes, size < sizeof(bytes) ? size : sizeof(bytes));
}

static void write_map(const char *path, unsigned version, size_t size)
{
	write_map_of_type(path, version, 1, size);
}

/* L or R until the tab named is shown */
static int to_tab(const char *name)
{
	char marked[32];
	int index;

	/* (from a page a row opened: back to its tab first) */
	if (strstr(menu, "\n\x03"))
		press(VITA_BUTTON_CIRCLE);
	snprintf(marked, sizeof(marked), "*%s", name);
	for (index = 0; index < 8 && !strstr(menu, marked); index++)
		press(VITA_BUTTON_R);
	return strstr(menu, marked) != NULL && !strstr(menu, "\n\x03");
}

/* down until the selected line starts with `label` (a row's label is the
line up to its '\x02') */
static int to_line(const char *label)
{
	char line[128];
	int index;

	for (index = 0; index < 40; index++)
	{
		if (!strncmp(menu_line(menu_selected, line, sizeof(line)), label, strlen(label)))
			return 1;
		press(VITA_BUTTON_DOWN);
	}
	return 0;
}

/* ---------- the page's shape */

/* the menu's lines: how many (the zones' diagram line not counted), the
longest row label, and the diagram's marks ("" without one) */
static int menu_lines(int *longest, char *diagram, int size)
{
	const char *at = menu;
	int count = 0;

	*longest = 0;
	diagram[0] = 0;
	while (*at)
	{
		int length = (int)strcspn(at, "\n");

		if (at[0] == '\x01')
			snprintf(diagram, (size_t)size, "%.*s", length - 1, at + 1);
		else
		{
			int label = (int)strcspn(at, "\x02\n");

			/* (the tab bar is drawn apart, not as a row of text) */
			if (at[0] != '\t')
				*longest = label > *longest ? label : *longest;
			count++;
		}
		at += length + (at[length] == '\n');
	}
	return count;
}

/* the rows of the page shown (lines with a value) */
static int menu_rows(void)
{
	const char *at = menu;
	int rows = 0;

	while ((at = strchr(at, '\x02')) != NULL)
	{
		rows++;
		at++;
	}
	return rows;
}

/* the characters of `bytes` bytes of UTF-8 text (a translation's accented
letters: two bytes, one character, as the overlay draws them) */
static int characters(const char *text, int bytes)
{
	int count = 0, index;

	for (index = 0; index < bytes && text[index]; index++)
		count += ((unsigned char)text[index] & 0xC0) != 0x80;
	return count;
}

/* every line fits: a row's label 22 characters at most and its value 24
(the value column starts 23 characters in, vita_gxm.c menu_build), the
help and the panel's buttons 70 (drawn smaller), any other line 46; in
characters, a translation's UTF-8 counted as the overlay draws it */
static int menu_fits(void)
{
	const char *at = menu;

	while (*at)
	{
		int bytes = (int)strcspn(at, "\n");
		int length = characters(at, bytes);
		int label = characters(at, (int)strcspn(at, "\x02\n"));
		int fits;

		if (at[0] == '\t' || at[0] == '\x01')
			fits = 1;
		else if (at[0] == '\x05' || at[0] == '\x06')
			fits = length - 1 <= 70;
		else if (label < length)
			fits = label <= 22 && length - label - 1 <= 24;
		else
			fits = length - (at[0] == '\x03' || at[0] == '\x04' || at[0] == '\x08' || at[0] == '!') <= 46;
		if (!fits)
		{
			printf("too long: %.*s\n", bytes, at);
			return 0;
		}
		at += bytes + (at[bytes] == '\n');
	}
	return 1;
}

/* the settings panel's lines fit its height (vita_gxm.c menu_build, the
classic panel, at 960x544: the rows from 76 pixels down, 28 a row, 22 for a
heading ('\x08'), 36 for a page's title, 14 for a gap; the chosen row's
bar, 22 pixels under its line's top, above the rule at 468 over the help
and the buttons: 392 pixels for the lines; the Halo 3 menus' panel fits its
rows to its height, so a page that fits this fits both); a screen without a
tab bar fits by its own height */
static int menu_height_fits(void)
{
	const char *at = menu;
	int y = 76, last = 0, first = 1;

	if (menu[0] != '\t')
		return 1;
	while (*at)
	{
		int bytes = (int)strcspn(at, "\n");

		if (first)
			first = 0;
		else if (at[0] == '\x01' || at[0] == '\x05' || at[0] == '\x06')
			;
		else if (at[0] == '\x03')
			y += 36;
		else if (at[0] == '\x08')
			y += 22;
		else if (!bytes)
			y += 14;
		else
		{
			last = y;
			y += 28;
		}
		at += bytes + (at[bytes] == '\n');
	}
	if (last + 22 > 468)
		printf("too many lines: the last row at %d pixels\n", last);
	return last + 22 <= 468;
}

/* the line that starts with `prefix` ("" if none) */
static const char *menu_find(const char *prefix, char *line, int size)
{
	const char *at = menu;

	while (*at)
	{
		int length = (int)strcspn(at, "\n");

		if (!strncmp(at, prefix, strlen(prefix)))
		{
			snprintf(line, (size_t)size, "%.*s", length, at);
			return line;
		}
		at += length + (at[length] == '\n');
	}
	line[0] = 0;
	return line;
}

/* a row opening a page: down to it, then cross */
static int open_page(const char *label)
{
	char title[64];

	if (!to_line(label))
		return 0;
	press(VITA_BUTTON_CROSS);
	snprintf(title, sizeof(title), "\n\x03");
	return strstr(menu, title) != NULL;
}

/* the page's headings and rows in order, "|" between them: a heading
('\x08') as "=" and its name, a row by its label (a '*' after it: a
restart's); the lines that are not rows left out. Its headings only with
`headings_only` */
static const char *outline_of(char *text, int size, int headings_only)
{
	const char *at = menu;
	int used = 0, first = 1;

	text[0] = 0;
	while (*at)
	{
		int length = (int)strcspn(at, "\n");
		int label = (int)strcspn(at, "\x02\n");

		if (used < size && at[0] == '\x08')
		{
			used += snprintf(text + used, (size_t)(size - used), "%s%s%.*s", first ? "" : "|", headings_only ? "" : "=",
				length - 1, at + 1);
			first = 0;
		}
		else if (used < size && !headings_only && label < length)
		{
			used += snprintf(text + used, (size_t)(size - used), "%s%.*s", first ? "" : "|", label, at);
			first = 0;
		}
		at += length + (at[length] == '\n');
	}
	return text;
}

/* down through the page's rows as many times as it has them, then up as
many: each press on a row (never a heading, nor a line that is not a row),
every row chosen once each way, and back on the row it started on */
static int steps_skip_headings(void)
{
	char line[128];
	int rows = menu_rows(), start = menu_selected, pass, index, good = 1;

	for (pass = 0; pass < 2; pass++)
	{
		unsigned long long chosen = 0;
		int count = 0;

		for (index = 0; index < rows; index++)
		{
			press(pass ? VITA_BUTTON_UP : VITA_BUTTON_DOWN);
			menu_line(menu_selected, line, sizeof(line));
			if (menu_selected < 1 || menu_selected > 63 || line[0] == '\x08' || !strchr(line, '\x02'))
			{
				printf("chosen: line %d, %s\n", menu_selected, line);
				good = 0;
			}
			else if (!(chosen & (1ULL << menu_selected)))
			{
				chosen |= 1ULL << menu_selected;
				count++;
			}
		}
		if (count != rows || menu_selected != start)
		{
			printf("%s: %d of %d rows chosen, on line %d (from %d)\n", pass ? "up" : "down", count, rows, menu_selected,
				start);
			good = 0;
		}
	}
	return good;
}

/* ---------- the Controls tab and its pages */

/* one frame with these buttons held and these touch zones */
static int frame_touch(unsigned long buttons, unsigned long touch)
{
	struct vita_host_pad pad;

	memset(&pad, 0, sizeof(pad));
	pad.buttons = buttons;
	pad.touch = touch;
	clock_us += 16667;
	return vita_settings_input(&pad);
}

static void test_controls_tab(void)
{
	struct vita_controls_config config;
	struct setting *channel;
	int channel_kept;
	char diagram[16], line[128];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded last: no touch or button lines in it) */
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_A"), "cross") && !strcmp(getenv("HALO_XBOX_WHITE"), "right") &&
		!strcmp(getenv("HALO_XBOX_BACK"), "select"), "a 1.0 settings.txt: touch zones Off, Xbox buttons as shipped");
	open_panel();
	to_tab("Controls");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(1, line, sizeof(line)), "\x08Look") &&
		!strncmp(menu_line(2, line, sizeof(line)), "Look sensitivity\x02", 17) &&
		!strncmp(menu_line(3, line, sizeof(line)), "Invert look\x02", 12) &&
		!strcmp(menu_line(4, line, sizeof(line)), "Stick deadzone\x02< 10% >") &&
		!strcmp(menu_line(5, line, sizeof(line)), "\x08" "Buttons") &&
		!strncmp(menu_line(6, line, sizeof(line)), "Crouch\x02", 7) &&
		!strcmp(menu_line(7, line, sizeof(line)), "Button layout\x02  As shipped  >") &&
		!strcmp(menu_line(8, line, sizeof(line)), "Reset controls\x02  >") &&
		!strcmp(menu_line(9, line, sizeof(line)), "\x08Gyro") &&
		!strncmp(menu_line(10, line, sizeof(line)), "Gyro aiming\x02", 12) &&
		!strcmp(menu_line(11, line, sizeof(line)), "Gyro settings\x02  >") &&
		!strcmp(menu_line(12, line, sizeof(line)), "\x08Touch") &&
		!strncmp(menu_line(13, line, sizeof(line)), "Rear touch guard\x02", 17) &&
		!strcmp(menu_line(14, line, sizeof(line)), "Touch zones\x02  Off  >") &&
		menu_line(15, line, sizeof(line))[0] == '\x05' && menu_line(16, line, sizeof(line))[0] == '\x06',
		"Controls: Look (sensitivity, invert, Stick deadzone 10%: the 1.0 file's), Buttons (Crouch, Button layout, "
		"Reset controls), Gyro (Gyro aiming, Gyro settings), Touch (Rear touch guard, Touch zones)");
	check(menu_selected == 2 && strstr(menu, "\n\x05Right stick turning speed\n") &&
		strstr(menu, "\n\x06L/R: tabs   Left/right: change   Circle: close"),
		"Controls opens on Look sensitivity, its first row (not the heading over it)");
	check(!strstr(menu, "\nTouch top left\x02") && !strstr(menu, "\nA\x02") && !strstr(menu, "\nGyro sensitivity\x02") &&
		!strstr(menu, "Check for updates") && !strstr(menu, "Version ") && !strstr(menu, "\nShow dev settings\x02") &&
		!strstr(menu, "\nUpdate channel\x02") && !strstr(menu, "\nButton icons\x02") && !strstr(menu, "\nAdvanced\x02"),
		"Controls: the zones, the buttons, the gyro's details on their pages; the update check, Update channel, "
		"Show dev settings and Button icons on Game (no Advanced page)");
	check(menu_rows() == 10 && count == 17 && menu_fits() && menu_height_fits(),
		"Controls: 10 rows, 4 headings (and the tab bar, help, buttons), each fits");
	check(strstr(menu, "\nRear touch guard\x02< Normal >") && !strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal"),
		"Rear touch guard: Normal as shipped (a 1.0 settings.txt has none)");
	check(!diagram[0], "Controls: no zone diagram");
	/* (the headings stepped over: down from Stick deadzone, Crouch; up from
	Gyro aiming, Reset controls; up from the first row, the last) */
	to_line("Stick deadzone");
	check(menu_selected == 4 && strstr(menu, "\n\x05Raise if the sticks drift\n"), "Stick deadzone: on the tab, its help");
	press(VITA_BUTTON_DOWN);
	check(menu_selected == 6 && !strncmp(menu_line(menu_selected, line, sizeof(line)), "Crouch\x02", 7),
		"down from Stick deadzone: Crouch (the Buttons heading stepped over)");
	to_line("Gyro aiming");
	press(VITA_BUTTON_UP);
	check(menu_selected == 8 && !strcmp(menu_line(menu_selected, line, sizeof(line)), "Reset controls\x02  >"),
		"up from Gyro aiming: Reset controls (the Gyro heading stepped over)");
	to_line("Look sensitivity");
	press(VITA_BUTTON_UP);
	check(menu_selected == 14 && !strncmp(menu_line(menu_selected, line, sizeof(line)), "Touch zones\x02", 12),
		"up from the first row: the last, Touch zones (not the Look heading)");
	press(VITA_BUTTON_DOWN);
	check(menu_selected == 2, "down from the last row: the first, Look sensitivity (not the Look heading)");
	check(strstr(menu, "\n\x06L/R: tabs   Left/right: change   Circle: close") != NULL,
		"the panel's buttons on one line: a value row's, on a tab");

	/* Touch zones: its page, the zones drawn beside */
	check(open_page("Touch zones") && strstr(menu, "\n\x03" "Controls > Touch zones\n") && !strchr(menu, '\x08'),
		"Touch zones opens its page (no headings on it)");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(strstr(menu, "\nTouch top left\x02") && strstr(menu, "\nTouch top right\x02") &&
		strstr(menu, "\nTouch left edge\x02") && strstr(menu, "\nTouch right edge\x02") &&
		strstr(menu, "\nRear touch left\x02") && strstr(menu, "\nRear touch right\x02") && menu_rows() == 6 &&
		menu_fits(), "Touch zones: the six zones");
	check(!strcmp(diagram, "s----- aaaaaa"), "Touch top left chosen: the diagram marks its zone (Off)");
	check(strstr(menu, "\n\x06L/R: tabs   Left/right: change   Circle: back") != NULL, "a page: circle goes back");
	to_line("Rear touch left");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(diagram, "----s- aaaaaa"), "Rear touch left chosen: the diagram marks its zone (Off)");
	check(frame_touch(0, 1UL << VITA_ZONE_REAR_LEFT) == 1, "panel open: a touch zone held does not reach the game");
	press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "a") && strstr(menu, "< A >") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_LEFT=a\n") && !strcmp(diagram, "----S- aaaaba"),
		"Rear touch left: A, in the environment and settings.txt; the diagram shows it set");
	for (index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "y"), "Rear touch left: Y");
	to_line("Touch top right");
	for (index = 0; index < 20; index++)
		press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "back") && strstr(menu, "< Back  ") && !strcmp(diagram, "-S--A- alaaea"),
		"Touch top right: Back, the last choice; the rear zone still marked set");
	press(VITA_BUTTON_CIRCLE);
	check(menu_visible && strstr(menu, "*Controls") && !strstr(menu, "\n\x03") &&
		!strncmp(menu_line(menu_selected, line, sizeof(line)), "Touch zones\x02  2 on  >", 21),
		"circle: back to Controls, on Touch zones, which says two are on");

	/* the rear guard, on the tab */
	to_line("Rear touch guard");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!diagram[0] && strstr(menu, "Rear pad: its edges (the grip) ignored"), "Rear touch guard chosen: its help, no zone diagram");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "strong") && strstr(menu, "< Strong") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_GUARD=strong\n"), "Rear touch guard: Strong, in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "strong"), "Rear touch guard: Strong is the last");
	for (index = 0; index < 3; index++)
		press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "off") && strstr(menu, " Off >"), "Rear touch guard: Off, the first");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "light") && vita_rear_guard_named(getenv("HALO_TOUCH_REAR_GUARD")) ==
		VITA_REAR_GUARD_LIGHT, "Rear touch guard: Light, as the touch reader takes it");

	/* Button layout: the Xbox buttons by name */
	check(open_page("Button layout") && strstr(menu, "\n\x03" "Controls > Button layout\n"), "Button layout opens its page");
	printf("%s\n--\n", menu);
	check(strstr(menu, "\nA\x02") && strstr(menu, "\nB\x02") && strstr(menu, "\nX\x02") && strstr(menu, "\nY\x02") &&
		strstr(menu, "\nBlack\x02") && strstr(menu, "\nWhite\x02") && strstr(menu, "\nLeft trigger\x02") &&
		strstr(menu, "\nRight trigger\x02") && strstr(menu, "\nLeft stick click\x02") &&
		strstr(menu, "\nRight stick click\x02") && strstr(menu, "\nBack\x02") && menu_rows() == 11 &&
		!strstr(menu, " button") && !strchr(menu, '\x08') && menu_fits() && menu_height_fits(),
		"Button layout: the eleven Xbox buttons by name (no headings), each fits");
	check(!strncmp(menu_line(2, line, sizeof(line)), "A\x02", 2) && !strstr(menu, "Button icons") &&
		vita_button_icons_playstation() == (VITA_BUTTON_ICONS_DEFAULT == 1),
		"Button layout: A first, Button icons on Game (as shipped: VITA_BUTTON_ICONS_DEFAULT)");
	to_line("White");
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!diagram[0], "a button's row: no zone diagram");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_XBOX_WHITE"), "left") && strstr(menu, "< D-pad left >") && strstr(menu, "White: flashlight"),
		"White: D-pad left; the help line says what Halo does with it");
	vita_controls_config_load(&config);
	check(config.zone_xbox[VITA_ZONE_REAR_LEFT] == VITA_XBOX_Y && config.zone_xbox[VITA_ZONE_TOP_RIGHT] == VITA_XBOX_BACK &&
		config.xbox_button[VITA_XBOX_WHITE] == VITA_BUTTON_LEFT, "the pad reads the panel's choices");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nButton layout\x02  Custom  >") != NULL, "back on Controls: Button layout says Custom");
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible && frame_touch(0, 1UL << VITA_ZONE_REAR_LEFT) == 0,
		"circle on a tab closes the panel: the touch zones reach the game");

	/* the next start: settings.txt brings them back */
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && (!strncmp(settings[index].variable, "HALO_TOUCH_", 11) ||
			!strncmp(settings[index].variable, "HALO_XBOX_", 10)))
		{
			settings[index].choice = 0;
			unsetenv(settings[index].variable);
		}
	vita_settings_load();
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "y") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "back") &&
		!strcmp(getenv("HALO_TOUCH_LEFT_EDGE"), "off") && !strcmp(getenv("HALO_XBOX_WHITE"), "left") &&
		!strcmp(getenv("HALO_XBOX_RIGHT_TRIGGER"), "r") && !strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "light"),
		"settings.txt round trip: zones, the rear guard and buttons back at start-up");

	/* the deadzone and Reset controls, on the tab (Show dev settings, on
	Game, kept) */
	open_panel();
	check(strstr(menu, "*Controls") && !strstr(menu, "\n\x03"), "the panel opens again on the tab's own page");
	to_line("Stick deadzone");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("XV_DEADZONE"), "15") && strstr(file_text(SETTINGS_FILE), "XV_DEADZONE=15\n") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Stick deadzone\x02< 15%  "),
		"Stick deadzone 15%: in the environment and settings.txt, the last");
	to_line("Reset controls");
	check(menu_selected == 8 && strstr(menu, "\n\x05" "Look, crouch, buttons, touch as shipped (gyro kept)\n") &&
		strstr(menu, "\n\x06L/R: tabs   Cross: select   Circle: close") != NULL,
		"Reset controls: under Buttons, its help; an action row's buttons: cross selects");
	/* (the Update channel, on Game, is no control: kept) */
	channel = setting_named("HALO_UPDATE_CHANNEL");
	channel_kept = channel->choice;
	channel->choice = !shipped_choice[channel - settings];
	press(VITA_BUTTON_CROSS);
	check(channel->choice != shipped_choice[channel - settings], "Reset controls keeps the Update channel");
	channel->choice = channel_kept;
	apply_value(channel);
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "off") && !strcmp(getenv("HALO_TOUCH_TOP_RIGHT"), "off") &&
		!strcmp(getenv("HALO_XBOX_WHITE"), "right") && !strcmp(getenv("XV_LOOK_SENS"), "100") &&
		!strcmp(getenv("HALO_CROUCH_TOGGLE"), "1") && !strcmp(getenv("XV_INVERT_Y"), "0") &&
		!strcmp(getenv("XV_DEADZONE"), "0") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_LEFT=off\n") &&
		strstr(file_text(SETTINGS_FILE), "XV_LOOK_SENS=100\n") && strstr(menu, "\n\x05" "Controls as shipped") &&
		!strcmp(getenv("HALO_TOUCH_REAR_GUARD"), "normal") &&
		strstr(file_text(SETTINGS_FILE), "HALO_TOUCH_REAR_GUARD=normal\n"),
		"Reset controls: zones Off, the rear guard Normal, buttons, look, crouch, deadzone as shipped, saved");
	check(!strcmp(getenv("HALO_DEV_SETTINGS"), "1") && strstr(menu, "|Dev"), "Reset controls keeps Show dev settings");
	check(strstr(menu, "\nButton layout\x02  As shipped  >") && strstr(menu, "\nTouch zones\x02  Off  >") &&
		strstr(menu, "\nStick deadzone\x02  Off >") && menu_fits() && menu_height_fits(),
		"on Controls: buttons as shipped, touch zones Off, the deadzone Off");
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible, "circle on Controls closes the panel");
}

/* ---------- gyro aiming: its row on Controls, its page */

static void test_gyro_page(void)
{
	struct vita_gyro_config config;
	char diagram[16], line[128];
	int longest, count, index;

	/* (a 1.0 settings.txt was loaded: no gyro lines in it) */
	check(!strcmp(getenv("HALO_GYRO"), "off") && !strcmp(getenv("HALO_GYRO_BUTTON"), "l") &&
		!strcmp(getenv("HALO_GYRO_SENS"), "150") && !strcmp(getenv("HALO_GYRO_INVERT_Y"), "0") &&
		!strcmp(getenv("HALO_GYRO_TURN"), "yaw") && !strcmp(getenv("HALO_GYRO_ASSIST"), "1"),
		"a 1.0 settings.txt: gyro Off, button L, 1.5x, normal, yaw, aim assist on");
	open_panel();
	to_tab("Controls");
	/* (Reset controls' notice, on the help line for 4 s, gone) */
	clock_us += 5000000;
	to_line("Gyro aiming");
	check(strstr(menu, "Turn the Vita to aim") != NULL && !strcmp(menu_line(menu_selected - 1, line, sizeof(line)), "\x08Gyro"),
		"Gyro aiming's help line; the Gyro heading over it");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "on") && strstr(file_text(SETTINGS_FILE), "HALO_GYRO=on\n"),
		"Gyro aiming On: in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO"), "hold") && strstr(menu, "< While holding  "), "Gyro aiming: While holding, the last");
	check(open_page("Gyro settings"), "Gyro settings opens its page");
	count = menu_lines(&longest, diagram, sizeof(diagram));
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(1, line, sizeof(line)), "\x03" "Controls > Gyro settings") &&
		strstr(menu_line(2, line, sizeof(line)), "Gyro button\x02") && strstr(line, "< L >") &&
		strstr(menu_line(3, line, sizeof(line)), "Gyro sensitivity\x02") && strstr(line, "< 1.5x >") &&
		strstr(menu_line(4, line, sizeof(line)), "Gyro vertical\x02") && strstr(line, "Normal") &&
		strstr(menu_line(5, line, sizeof(line)), "Gyro turning\x02") && strstr(line, "Turn (yaw)") &&
		strstr(menu_line(6, line, sizeof(line)), "Gyro aim assist\x02") && strstr(line, "On") &&
		!strncmp(menu_line(7, line, sizeof(line)), "\x04Gyro: yaw -999 pitch -999 roll -999", 36),
		"Gyro settings: button, sensitivity, vertical, turning, aim assist, the gyroscope's line");
	check(count == 10 && menu_fits() && !diagram[0],
		"Gyro settings: 10 lines (tab bar, title, 5 rows, the line, help, buttons), each fits");
	for (index = 0; index < 5; index++)
		press(VITA_BUTTON_DOWN);
	check(!strncmp(menu_line(menu_selected, line, sizeof(line)), "Gyro button", 11),
		"down skips the gyroscope's line (five downs: back to the first row)");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_BUTTON"), "r") && strstr(menu, "nothing else in play"), "Gyro button: R");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_SENS"), "250") && strstr(menu, "< 2.5x >"), "Gyro sensitivity: 2.5x");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GYRO_INVERT_Y"), "1") && !strcmp(getenv("HALO_GYRO_TURN"), "roll"),
		"Gyro vertical Inverted, turning Tilt (roll)");
	vita_gyro_config_load(&config);
	check(config.assist, "the pad reads Gyro aim assist On by default");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_GYRO_ASSIST"), "0") && strstr(menu, " Off >") &&
		strstr(menu, "On: slows and sticks on targets like the sticks"), "Gyro aim assist: Off, its help line");
	vita_gyro_config_load(&config);
	check(config.mode == VITA_GYRO_HOLD && config.button == VITA_BUTTON_R && config.sensitivity > 2.49f &&
		config.sensitivity < 2.51f && config.invert_y && config.turn == VITA_GYRO_TURN_ROLL && !config.assist,
		"the pad reads the gyro's choices");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);

	/* the next start: settings.txt brings them back */
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && !strncmp(settings[index].variable, "HALO_GYRO", 9))
		{
			settings[index].choice = 0;
			unsetenv(settings[index].variable);
		}
	vita_settings_load();
	check(!strcmp(getenv("HALO_GYRO"), "hold") && !strcmp(getenv("HALO_GYRO_BUTTON"), "r") &&
		!strcmp(getenv("HALO_GYRO_SENS"), "250") && !strcmp(getenv("HALO_GYRO_INVERT_Y"), "1") &&
		!strcmp(getenv("HALO_GYRO_TURN"), "roll") && !strcmp(getenv("HALO_GYRO_ASSIST"), "0"),
		"settings.txt round trip: the gyro's rows back at start-up");
	check(strstr(file_text(SETTINGS_FILE), "HALO_XBOX_A=") && strstr(file_text(SETTINGS_FILE), "XV_LOOK_SENS="),
		"the other rows are still saved beside them");

	/* Reset controls leaves the gyro's rows (as when they had a tab) */
	open_panel();
	to_tab("Controls");
	check(to_line("Reset controls"), "Controls: Reset controls");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(getenv("HALO_GYRO"), "hold") && !strcmp(getenv("HALO_GYRO_SENS"), "250"),
		"Reset controls leaves gyro aiming and its settings");
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the Multiplayer tab and its Play page */

/* the most players the game says its network game takes (0: none yet) */
static int status_maximum;

/* what the game's main loop says (system_link_shortcut.c), as it would */
static void game_status(int state, int machines, int games, int host)
{
	static const unsigned char address[4] = { 192, 168, 1, 23 };
	int value;

	memcpy(&value, address, sizeof(value));
	halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE] = state;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MACHINES] = machines;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_PLAYERS] = machines;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_GAMES] = games;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_HOST] = host;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_MAXIMUM] = status_maximum;
	halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS] = state == SYSTEM_LINK_STATE_STARTING ? 0 : value;
	/* (the panel redraws twice a second) */
	clock_us += 600000;
	frame(0);
}

/* the game's main loop answering the panel's request */
static void game_answers(int answer)
{
	halo_system_link_answer = answer;
	halo_system_link_request = SYSTEM_LINK_REQUEST_NONE;
	frame(0);
}

/* the keyboard closed: with this text typed (NULL: cancelled) */
static void keyboard_closes(const char *typed)
{
	snprintf(ime_typed, sizeof(ime_typed), "%s", typed ? typed : "");
	ime_result = typed ? 1 : -1;
	frame(0);
}

/* the page's rows, in order, by their labels ("|" between them); its lines
that are not rows ('\x04') counted in info */
static int rows_of(char *labels, int size, int *info)
{
	const char *at = menu;
	int rows = 0, used = 0;

	labels[0] = 0;
	*info = 0;
	while (*at)
	{
		int length = (int)strcspn(at, "\n");
		int label = (int)strcspn(at, "\x02\n");

		if (label < length && used < size)
		{
			used += snprintf(labels + used, (size_t)(size - used), "%s%.*s", rows ? "|" : "", label, at);
			rows++;
		}
		else if (at[0] == '\x04')
			(*info)++;
		at += length + (at[length] == '\n');
	}
	return rows;
}

/* ---------- the Mod manager page */

/* the game's look as the test makes it (port/linux/game/mod_manager.c's,
made up): a modded Xbox map, a corrupt file, a Custom Edition map that
needs PC maps, an MCC mod with a map that fails, a file of a name the game
does not take */
static struct halo_mods_inventory test_inventory;

static void test_entry(const char *name, const char *file, const char *title, int kind, int valid, int kilobytes,
	int modes, const char *why, const char *needs, int deletable)
{
	struct halo_mod_entry *entry = &test_inventory.entries[test_inventory.count++];

	memset(entry, 0, sizeof(*entry));
	snprintf(entry->name, sizeof(entry->name), "%s", name);
	snprintf(entry->file, sizeof(entry->file), "%s", file);
	snprintf(entry->title, sizeof(entry->title), "%s", title);
	entry->kind = kind;
	entry->valid = valid;
	entry->enabled = 1;
	entry->kilobytes = kilobytes;
	entry->modes = modes;
	entry->deletable = deletable;
	snprintf(entry->why, sizeof(entry->why), "%s", why);
	snprintf(entry->needs, sizeof(entry->needs), "%s", needs);
	test_inventory.used_kilobytes += kilobytes;
}

static void test_inventory_make(void)
{
	memset(&test_inventory, 0, sizeof(test_inventory));
	test_entry("bad name", "bad%name.map", "bad%name", HALO_MOD_KIND_UNKNOWN, 0, 1, 0,
		"Its file name has characters this game doesn't take", "", 0);
	test_entry("broken", "broken.map", "broken", HALO_MOD_KIND_UNKNOWN, 0, 69, 0, "Not a Halo map", "", 1);
	test_entry("cemap", "cemap.map", "cemap", HALO_MOD_KIND_CE_MAP, 1, 3072, HALO_MOD_MODE_MULTIPLAYER, "",
		"PC maps On (Settings > Multiplayer > Modded maps)", 1);
	test_entry("mygulch", "mygulch.map", "mygulch", HALO_MOD_KIND_XBOX_MAP, 1, 22042, HALO_MOD_MODE_MULTIPLAYER, "", "", 1);
	test_inventory.entries[test_inventory.count - 1].source = HALO_MOD_SOURCE_MAP_SHARE;
	test_entry("testmod", "TestMod", "Test Mod", HALO_MOD_KIND_MCC_MOD, 1, 6, HALO_MOD_MODE_CAMPAIGN,
		"a30.map: not a campaign map", "", 1);
	test_inventory.entries[test_inventory.count - 1].maps = 2;
	test_inventory.entries[test_inventory.count - 1].maps_valid = 1;
	test_inventory.free_kilobytes = 3 * 1024 * 1024;
}

/* the game's main loop answering the panel's request (mod_manager_update):
the request taken, `answer` given, the look's inventory handed over */
static int game_answers_mods(int answer)
{
	int request = halo_mods_request;

	if (request == HALO_MODS_REQUEST_NONE)
		return 0;
	memcpy(&halo_mods_inventory, &test_inventory, sizeof(halo_mods_inventory));
	halo_mods_answer = answer;
	halo_mods_request = HALO_MODS_REQUEST_NONE;
	frame(0);
	return request;
}

static void test_mod_manager_page(void)
{
	char line[256];
	int index;

	test_inventory_make();
	/* (none off: as the game's MOD MANAGER would set the lists) */
	vita_settings_set("HALO_MAPS_DISABLED", "");
	vita_settings_set("HALO_MODS_DISABLED", "");
	open_page("Modded maps");
	check(!strcmp(menu_line(2, line, sizeof(line)), "Mod manager\x02  >"), "Modded maps: Mod manager, its first row");
	to_line("Mod manager");
	check(strstr(menu, "\n\x05" "Custom maps and MCC mods: check, on, off, delete\n") != NULL, "Mod manager's help");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "\n\x03Multiplayer > Mod manager\n") && strstr(menu, "\n\x04" "Checking the maps and mods...\n") &&
		halo_mods_request == HALO_MODS_REQUEST_SCAN && strstr(log_text, "settings: mod manager: asked the game to look"),
		"cross: the Mod manager page, the game asked to look, the page says so meanwhile");
	check(game_answers_mods(HALO_MODS_ANSWER_SCANNED) == HALO_MODS_REQUEST_SCAN, "the game answers");
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n\x04" "5 items: 24.5 MB used, 3.0 GB free\n\n") != NULL, "the totals: 5 items, used and free");
	check(strstr(menu, "\nbad%name.map\x02  Error     1 KB  ?\n") && strstr(menu, "\nbroken.map\x02  Error    69 KB  ?\n") &&
		strstr(menu, "\ncemap.map\x02  On      3.0 MB  CE\n") && strstr(menu, "\nmygulch.map\x02  On     21.5 MB  Xbox\n") &&
		strstr(menu, "\nTest Mod\x02  On        6 KB  MCC\n"), "each entry: its name, On or Error, its size and kind");
	check(menu_fits() && menu_height_fits(), "the Mod manager page fits");
	to_line("broken.map");
	check(strstr(menu, "\n\x05" "Failed: Not a Halo map\n") &&
		strstr(menu, "\n\x06L/R: tabs   Cross: details   Square: delete   Circle: back"),
		"a corrupt file: why it failed; the buttons");
	to_line("cemap.map");
	check(strstr(menu, "\n\x05Needs: PC maps On (Settings > Multiplayer > Modded maps)\n") != NULL, "what a map needs");
	to_line("mygulch.map");
	check(strstr(menu, "\n\x05Xbox map: on or off, or delete it\n") != NULL, "a map that plays: its kind");
	/* off and on: HALO_MAPS_DISABLED, kept in settings.txt */
	press(VITA_BUTTON_LEFT);
	check(strstr(menu, "\nmygulch.map\x02  Off    21.5 MB  Xbox\n") && getenv("HALO_MAPS_DISABLED") &&
		!strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch") && strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED=mygulch\n"),
		"left: mygulch off, in the environment and settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nmygulch.map\x02  On ") && (!getenv("HALO_MAPS_DISABLED") || !getenv("HALO_MAPS_DISABLED")[0]) &&
		!strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED"), "right: on again, the line gone");
	/* a mod: HALO_MODS_DISABLED, by its key */
	to_line("Test Mod");
	check(strstr(menu, "\n\x05" "Needs") == NULL && strstr(menu, "\n\x05MCC mod: on or off, or delete it\n"),
		"the mod: its kind");
	press(VITA_BUTTON_LEFT);
	check(strstr(menu, "\nTest Mod\x02  Off ") && getenv("HALO_MODS_DISABLED") && !strcmp(getenv("HALO_MODS_DISABLED"), "testmod") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MODS_DISABLED=testmod\n") && strstr(log_text, "settings: mod testmod turned off"),
		"left: the mod off by its key, saved");
	/* its details: cross; left and right there too; circle back */
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "Test Mod", 8) && strstr(menu, "Type: MCC mod") && strstr(menu, "File: mods/TestMod") &&
		strstr(menu, "Maps: 2, 1 of them play") && strstr(menu, "Check: Passed - a30.map: not a campaign map") &&
		strstr(menu, "Status: Off: kept, but in no map list"), "cross: the details");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "Status: On: in the map lists") && (!getenv("HALO_MODS_DISABLED") || !getenv("HALO_MODS_DISABLED")[0]),
		"right in the details: on again");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\n\x03Multiplayer > Mod manager\n") != NULL, "circle: back to the list");
	to_line("mygulch.map");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "From: Received through map sharing") && strstr(menu, "Works in: Multiplayer"),
		"the details say where it came from");
	press(VITA_BUTTON_CIRCLE);
	/* delete: a question first; circle keeps it; cross asks the game */
	to_line("broken.map");
	press(VITA_BUTTON_SQUARE);
	check(!strncmp(menu, "DELETE MAP", 10) && strstr(menu, "broken.map"), "square asks before deleting");
	press(VITA_BUTTON_CIRCLE);
	check(halo_mods_request == HALO_MODS_REQUEST_NONE && strstr(menu, "> Mod manager\n"), "circle keeps it");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CROSS);
	check(halo_mods_request == HALO_MODS_REQUEST_DELETE && !strcmp(halo_mods_target, "broken.map") &&
		halo_mods_target_kind == HALO_MOD_KIND_UNKNOWN, "cross asks the game to delete broken.map");
	/* (the game deleted it and looked again) */
	memmove(&test_inventory.entries[1], &test_inventory.entries[2], sizeof(test_inventory.entries[0]) * 3);
	test_inventory.count--;
	game_answers_mods(HALO_MODS_ANSWER_DELETED);
	check(strstr(menu, "Deleted broken.map") && !strstr(menu, "\nbroken.map\x02") && strstr(menu, "\n\x04" "4 items: "),
		"deleted: said, gone from the list");
	/* a mod's delete asks about its folder; the game says it is in use */
	to_line("Test Mod");
	press(VITA_BUTTON_SQUARE);
	check(!strncmp(menu, "DELETE MOD", 10) && strstr(menu, "TestMod") && strstr(menu, "everything in its folder"),
		"a mod's question names its folder");
	press(VITA_BUTTON_CROSS);
	check(halo_mods_request == HALO_MODS_REQUEST_DELETE && !strcmp(halo_mods_target, "TestMod") &&
		halo_mods_target_kind == HALO_MOD_KIND_MCC_MOD, "cross asks the game to delete the mod's folder");
	game_answers_mods(HALO_MODS_ANSWER_IN_USE);
	check(strstr(menu, "TestMod is in play: leave it first") && strstr(menu, "\nTest Mod\x02"), "in use: kept, and said");
	/* a name the game does not take: never asked about */
	for (index = 0; index < 10 && strncmp(menu_line(menu_selected, line, sizeof(line)), "bad%name.map", 12); index++)
		press(VITA_BUTTON_UP);
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CROSS);
	check(halo_mods_request == HALO_MODS_REQUEST_NONE && strstr(menu, "Its name isn't one this game takes: use VitaShell"),
		"a name the game does not take: not deleted, why");
	press(VITA_BUTTON_LEFT);
	check(!getenv("HALO_MAPS_DISABLED") || !getenv("HALO_MAPS_DISABLED")[0], "... nor turned off");
	/* the game's MOD MANAGER sets the lists through the panel
	(vita_settings_set): kept, a '%' and a line's end left out */
	check(vita_settings_set("HALO_MAPS_DISABLED", "mygulch,wiz%ard\n") && !strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch,wizard") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED=mygulch,wizard\n"), "the game's list of maps off, kept");
	check(vita_settings_set("HALO_MODS_DISABLED", "testmod") && !strcmp(getenv("HALO_MODS_DISABLED"), "testmod") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MODS_DISABLED=testmod\n"), "the game's list of mods off, kept");
	check(vita_settings_set("HALO_MAPS_DISABLED", "") && vita_settings_set("HALO_MODS_DISABLED", "") &&
		!getenv("HALO_MAPS_DISABLED") && !getenv("HALO_MODS_DISABLED") && !strstr(file_text(SETTINGS_FILE), "_DISABLED"),
		"empty lists: unset, no lines");
	/* circle: back to Modded maps, on the Mod manager row */
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\n\x03Multiplayer > Modded maps\n") && strstr(menu, "\nMod manager\x02  4 items  >\n"),
		"circle: back to Modded maps, the row says how many");
	press(VITA_BUTTON_CIRCLE);
}


/* Multiplayer > Friends (p2p_friends.h, the test's list): the code and the
status, Add a friend (typed with the D-pad), Appear offline and Friends can
join (saved), each entry with a word of its state, its doing on the help
line and its buttons in the footer; cross, square and triangle do what the
footer says, a join going on to Join's steps, a removal asked first */
static void test_friends_page(void)
{
	char line[256], network_before[16];

	test_friend_count = 0;
	test_friend("d1", P2P_FRIEND_KIND_REQUEST, "Delta", "TBJV-CX9K-Y47D", 0, 0, "Wants to be your friend");
	test_friend("b1", P2P_FRIEND_KIND_FRIEND, "Bravo", "RYQS-BU47-4JPF", 1, 1, "Slayer on Blood Gulch");
	test_friend("c1", P2P_FRIEND_KIND_FRIEND, "Charlie", "AAZQ-L3GC-JLTL", 0, 0, "Offline");
	test_friend("o1", P2P_FRIEND_KIND_OUTGOING, "A7ES-DA9D-2NMR", "A7ES-DA9D-2NMR", 0, 0, "Request sent");
	test_friend("e1", P2P_FRIEND_KIND_BLOCKED, "Echo", "9YHX-JQPH-CWC9", 0, 0, "Blocked");
	friend_act_outcome = P2P_FRIENDS_DONE;
	/* (the row in the code row's place: a game with the PC menus, a session
	started Online) */
	snprintf(network_before, sizeof(network_before), "%s", getenv("HALO_VITA_NETWORK") ? getenv("HALO_VITA_NETWORK") :
		"wifi");
	vita_settings_set("HALO_VITA_NETWORK", "online");
	restart_pending = 0;
	vita_settings_load();
	halo_pc_menus_state = 1;
	check(to_tab("Multiplayer"), "Multiplayer tab");
	if (!to_line("Friends"))
	{
		check(0, "Multiplayer: the Friends row (Online, the PC menus)");
		printf("%s\n--\n", menu);
		return;
	}
	check(strstr(menu, "\nFriends\x02  1 online, 1 new  >\n") != NULL,
		"Multiplayer: the Friends row says who is online and what is new");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n\x03Multiplayer > Friends\n") != NULL, "cross: the Friends page");
	check(strstr(menu, "\nAdd a friend\x02  >\n") && strstr(menu, "\nAppear offline\x02  Off >\n") &&
		strstr(menu, "\nFriends can join\x02  On >\n"), "its rows: Add a friend, Appear offline, Friends can join");
	check(strstr(menu, "\n\x04Your friend code: KQ7M-3XPA-T9WE\n") && strstr(menu, "\n\x04" "1 of 2 friends online\n"),
		"the code and the status");
	/* (four entries shown at once, with the Party row: the fifth scrolled to) */
	check(strstr(menu, "\nDelta\x02  Request\n") && strstr(menu, "\nBravo\x02  Join\n") &&
		strstr(menu, "\nCharlie\x02  Offline\n") && strstr(menu, "\nA7ES-DA9D-2NMR\x02  Sent\n"),
		"each entry: its name and a word of its state");
	check(menu_fits() && menu_height_fits(), "the Friends page fits");
	/* Appear offline, Friends can join: saved */
	to_line("Appear offline");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_FRIENDS_APPEAR_OFFLINE") && !strcmp(getenv("HALO_FRIENDS_APPEAR_OFFLINE"), "true") &&
		strstr(file_text(SETTINGS_FILE), "HALO_FRIENDS_APPEAR_OFFLINE=true\n"), "Appear offline on: saved");
	press(VITA_BUTTON_LEFT);
	to_line("Friends can join");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_FRIENDS_JOIN") && !strcmp(getenv("HALO_FRIENDS_JOIN"), "false"), "Friends can join off");
	press(VITA_BUTTON_LEFT);
	/* Add a friend: the code typed */
	to_line("Add a friend");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "ADD A FRIEND") && strstr(menu, "A A A A - A A A A - A A A A"), "Add a friend: the code screen");
	press(VITA_BUTTON_UP);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_UP);
	press(VITA_BUTTON_UP);
	press(VITA_BUTTON_CROSS);
	check(!strcmp(friend_added, "BCAAAAAAAAAA") && strstr(menu, "Friend request sent"), "cross: the request sent, said");
	/* the request: accept, decline, block (the notice gone first) */
	clock_us += 6000000;
	to_line("Delta");
	check(strstr(menu, "\n\x05" "Delta: Wants to be your friend\n") &&
		strstr(menu, "\n\x06L/R: tabs   Cross: accept  Square: decline  Circle: back"),
		"a request: its help and buttons");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(friend_acted_id, "d1") && friend_acted_kind == P2P_FRIEND_KIND_REQUEST &&
		friend_acted_action == P2P_FRIENDS_ACCEPT, "cross: accepted");
	press(VITA_BUTTON_SQUARE);
	check(friend_acted_action == P2P_FRIENDS_DECLINE, "square: declined");
	press(VITA_BUTTON_TRIANGLE);
	check(!strncmp(menu, "BLOCK", 5) && strstr(menu, "Delta"), "triangle: the question before blocking");
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == P2P_FRIENDS_BLOCK && strstr(menu, "> Friends\n"), "cross: blocked, back to the page");
	/* a friend: join (Join's steps next), invite, remove after a question */
	clock_us += 6000000;
	to_line("Bravo");
	check(strstr(menu, "\n\x05" "Bravo: Slayer on Blood Gulch, 5 of 16\n") &&
		strstr(menu, "Cross: join  Square: invite  Circle: back") && strstr(menu, "\n\x04Triangle: remove a friend, block a request\n"),
		"a friend: what they do, their game's players; the buttons");
	press(VITA_BUTTON_SQUARE);
	check(friend_acted_action == P2P_FRIENDS_INVITE && strstr(menu, "Invited Bravo"), "square: invited");
	press(VITA_BUTTON_TRIANGLE);
	check(!strncmp(menu, "REMOVE FRIEND", 13) && strstr(menu, "RYQS-BU47-4JPF"), "triangle: the question before removing");
	friend_acted_action = -1;
	press(VITA_BUTTON_CIRCLE);
	check(friend_acted_action == -1 && strstr(menu, "> Friends\n"), "circle keeps the friend");
	press(VITA_BUTTON_TRIANGLE);
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == P2P_FRIENDS_REMOVE && !strcmp(friend_acted_id, "b1"), "cross: removed");
	to_line("Bravo");
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == P2P_FRIENDS_JOIN && !strncmp(menu, "JOIN A GAME\n", 12),
		"cross: joining the friend's game, Join's steps next (as a code's)");
	press(VITA_BUTTON_CIRCLE);
	friend_act_outcome = P2P_FRIENDS_OFFLINE;
	to_line("Bravo");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "Go online first") != NULL, "a join refused: why");
	friend_act_outcome = P2P_FRIENDS_DONE;
	/* the request sent: cancel; the blocked: unblock */
	to_line("A7ES-DA9D-2NMR");
	check(strstr(menu, "Square: cancel") != NULL, "a request sent: square cancels");
	press(VITA_BUTTON_SQUARE);
	check(friend_acted_action == P2P_FRIENDS_CANCEL && !strcmp(friend_acted_id, "o1"), "square: cancelled");
	to_line("Echo");
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == P2P_FRIENDS_UNBLOCK && !strcmp(friend_acted_id, "e1"), "cross: unblocked");
	(void)line;
	press(VITA_BUTTON_CIRCLE);
	halo_pc_menus_state = -1;
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	check(to_tab("Multiplayer") && !strstr(menu, "\nFriends\x02") && strstr(menu, "\nJoin with a code\x02"),
		"the Xbox's menus: the code row, no Friends row");
	/* (the session as it was) */
	vita_settings_set("HALO_VITA_NETWORK", network_before);
	restart_pending = 0;
	vita_settings_load();
	to_tab("Multiplayer");
}

/* Multiplayer > Friends > Party (p2p_party.h, the test's party): its row on
the Friends page, the page's status, invites, members (a word each), the
friends online to invite; cross on a friend invites, on an invite joins,
square declines, cross on a member mutes, triangle (the leader's) removes
after a question; Leave the party; the leader warned before joining a
friend's game with too few open slots */
static void add_party_member(const char *id, const char *name, int leader, int self)
{
	struct p2p_party_member *member = &test_party[test_party_count++];

	memset(member, 0, sizeof(*member));
	snprintf(member->id, sizeof(member->id), "%s", id);
	snprintf(member->name, sizeof(member->name), "%s", name);
	snprintf(member->code, sizeof(member->code), "AAAA-BBBB-CCCC");
	member->leader = leader;
	member->self = self;
	member->online = 1;
	member->is_friend = !self;
}

static void test_party_page(void)
{
	char network_before[16];

	test_friend_count = 0;
	test_friend("b1", P2P_FRIEND_KIND_FRIEND, "Bravo", "RYQS-BU47-4JPF", 1, 1, "Slayer on Blood Gulch");
	test_friend("c1", P2P_FRIEND_KIND_FRIEND, "Charlie", "AAZQ-L3GC-JLTL", 1, 0, "In the menus");
	test_party_count = 0;
	add_party_member("a0", "Alpha", 1, 1);
	add_party_member("d1", "Delta #K7QX", 0, 0);
	test_party_leading = 1;
	test_party_invite_count = 1;
	snprintf(test_party_invites[0].id, sizeof(test_party_invites[0].id), "inv1");
	snprintf(test_party_invites[0].name, sizeof(test_party_invites[0].name), "Echo");
	test_party_invites[0].size = 3;
	snprintf(network_before, sizeof(network_before), "%s", getenv("HALO_VITA_NETWORK") ? getenv("HALO_VITA_NETWORK") :
		"wifi");
	vita_settings_set("HALO_VITA_NETWORK", "online");
	restart_pending = 0;
	vita_settings_load();
	halo_pc_menus_state = 1;
	check(to_tab("Multiplayer") && to_line("Friends"), "the Friends row");
	press(VITA_BUTTON_CROSS);
	check(to_line("Party") && strstr(menu, "\nParty\x02  2 of 8  >\n") != NULL, "the Friends page's Party row: its size");
	check(menu_fits() && menu_height_fits(), "the Friends page fits with its Party row");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n\x03Multiplayer > Party\n") != NULL, "cross: the Party page");
	check(strstr(menu, "\nLeave the party\x02  >\n") && strstr(menu, "\n\x04Party: 2 of 8 (Alpha leads)\n"),
		"Leave the party, the party's status");
	check(strstr(menu, "\nEcho\x02  Party invite\n") && strstr(menu, "\nAlpha\x02  You, leader\n") &&
		strstr(menu, "\nDelta #K7QX\x02  In party\n"), "the invite, the members, a word each");
	check(strstr(menu, "\nBravo\x02  Invite\n") && strstr(menu, "\nCharlie\x02  Invite\n"), "the friends online to invite");
	check(menu_fits() && menu_height_fits(), "the Party page fits");
	to_line("Bravo");
	check(strstr(menu, "Cross: invite to party") != NULL, "a friend: cross invites");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_acted, "invite b1") && strstr(menu, "Invited Bravo to your party"), "cross: invited");
	clock_us += 6000000;
	to_line("Echo");
	press(VITA_BUTTON_SQUARE);
	check(!strcmp(party_acted, "decline inv1"), "square: the invite declined");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_acted, "accept inv1"), "cross: the invite accepted");
	clock_us += 6000000;
	to_line("Delta #K7QX");
	check(strstr(menu, "Cross: mute  Triangle: remove") != NULL, "a member, for the leader: mute, remove");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_acted, "mute d1"), "cross: muted");
	press(VITA_BUTTON_TRIANGLE);
	check(!strncmp(menu, "REMOVE FROM PARTY", 17) && strstr(menu, "Delta #K7QX"), "triangle: the question before removing");
	party_acted[0] = 0;
	press(VITA_BUTTON_CIRCLE);
	check(!party_acted[0] && strstr(menu, "> Party\n"), "circle keeps the member");
	press(VITA_BUTTON_TRIANGLE);
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_acted, "kick d1"), "cross: removed");
	/* party chat: the newest lines, quick chat, a message typed */
	check(strstr(menu, "\n\x04" "Delta #K7QX: ready?\n") && strstr(menu, "\n\x04" "Echo joined the party\n") &&
		strstr(menu, "\n\x04" "Bravo: go go\n") && !strstr(menu, ": hi\n"), "the chat's three newest lines");
	check(to_line("Quick chat") && strstr(menu, "\nQuick chat\x02< Ready! >\n"), "quick chat: its preset");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nQuick chat\x02< Let's go >\n") != NULL, "right: the next presets");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_said, "preset 2") && strstr(menu, "Sent to your party"), "cross: the preset sent");
	clock_us += 6000000;
	to_line("Send a message");
	press(VITA_BUTTON_CROSS);
	check(ime_open_now && !strcmp(ime_title, "Party chat") && ime_maximum == P2P_PARTY_CHAT_LENGTH,
		"Send a message: the keyboard, 120 characters");
	snprintf(ime_typed, sizeof(ime_typed), "see you there");
	ime_result = 1;
	press(0);
	check(!strcmp(party_said, "text see you there") && strstr(menu, "Sent to your party"), "the message typed: sent");
	/* SELECT + D-pad left: the Party page at once, again: closed */
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);
	check(!panel_open, "the panel closed");
	press(VITA_BUTTON_SELECT | VITA_BUTTON_LEFT);
	check(panel_open && page == PAGE_PARTY && strstr(menu, "\n\x03Multiplayer > Party\n"), "SELECT + left: the Party page");
	press(0);
	press(VITA_BUTTON_SELECT | VITA_BUTTON_LEFT);
	check(!panel_open, "SELECT + left again: closed");
	press(0);
	panel_begin();
	to_tab("Multiplayer");
	to_line("Friends");
	press(VITA_BUTTON_CROSS);
	to_line("Party");
	press(VITA_BUTTON_CROSS);
	clock_us += 6000000;
	to_line("Leave the party");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(party_acted, "leave"), "Leave the party");
	/* the leader warned before a friend's game with too few open slots: a
	second press joins */
	press(VITA_BUTTON_CIRCLE);
	to_line("Friends");
	press(VITA_BUTTON_CROSS);
	test_party_count = 0;
	add_party_member("a0", "Alpha", 1, 1);
	add_party_member("d1", "Delta", 0, 0);
	add_party_member("e1", "Echo", 0, 0);
	add_party_member("f1", "Foxtrot", 0, 0);
	add_party_member("g1", "Golf", 0, 0);
	add_party_member("h1", "Hotel", 0, 0);
	add_party_member("i1", "India", 0, 0);
	add_party_member("j1", "Juliet", 0, 0);
	add_party_member("k1", "Kilo", 0, 0);
	add_party_member("l1", "Lima", 0, 0);
	add_party_member("m1", "Mike", 0, 0);
	add_party_member("n1", "November", 0, 0);
	test_party_count = 12;
	friend_acted_action = -1;
	to_line("Bravo");
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == -1 && strstr(menu, "open slots"), "a friend's game with too few slots: the leader warned");
	press(VITA_BUTTON_CROSS);
	check(friend_acted_action == P2P_FRIENDS_JOIN, "cross again: joined anyway");
	press(VITA_BUTTON_CIRCLE);
	test_party_count = 0;
	test_party_invite_count = 0;
	halo_pc_menus_state = -1;
	vita_settings_set("HALO_VITA_NETWORK", network_before);
	restart_pending = 0;
	vita_settings_load();
	to_tab("Multiplayer");
}

static void test_multiplayer_tab(void)
{
	char line[128], labels[512];
	int info;

	/* (the network this session runs: Online, from the environment; the
	game has not said yet whether its menus have OpenCE's screens) */
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter") && strstr(menu, "< Online"),
		"Multiplayer: Connection, Join with a code (Online), Modded maps; no Play page (no ad hoc room: Online)");
	check(!strstr(menu, "Play") && !strstr(menu, "Host a game") && !strstr(menu, "Lobby name") &&
		!strstr(menu, "Max players") && !strstr(menu, "Visibility") && !strstr(menu, "Browse public games") &&
		!strstr(menu, "Games on this network") && !strstr(menu, "Host co-op campaign"),
		"Multiplayer: hosting, joining and co-op are the game's menus' (the Play page's rows are gone)");
	check(strstr(menu, "\n\x04This Vita: vitauser\n") && strstr(menu, "\n\x04No game yet: host one or join one\n") &&
		strstr(menu, "\n\x04Online: ready") && !strstr(menu, "Online menus"),
		"before the game says: this Vita's name, no game, internet play's line");
	check(menu_rows() == 8 && menu_fits(), "Multiplayer: eight rows, 46 characters a line");
	check(!strcmp(outline_of(labels, sizeof(labels), 0), "=Network|Connection|Crossplay*|Join with a code|=Bots|Bots|"
		"=Games|Co-op players|Unseen players|Modded maps|Latency meter") && menu_height_fits(),
		"Multiplayer: Network over Connection, Bots over Bots, Games over Co-op players ... Latency meter; it fits the "
		"panel's height");
	/* the offline bots: Bots on, its skill and teams rows below it, live */
	to_line("Bots");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|  Bot skill|  Bot teams|  Bot vehicles|  Bot routes|  Bot playlist|Co-op players|Unseen players|Modded maps|Latency meter") &&
		getenv("HALO_BOTS") && !strcmp(getenv("HALO_BOTS"), "3") && !strstr(menu, "Restart the game") && menu_fits(),
		"Bots 3: Bot skill, Bot teams, Bot vehicles, Bot routes and Bot playlist below it, live");
	check(!strcmp(outline_of(labels, sizeof(labels), 0), "=Network|Connection|Crossplay*|Join with a code|=Bots|Bots|"
		"  Bot skill|  Bot teams|  Bot vehicles|  Bot routes|  Bot playlist|=Games|Co-op players|Unseen players|"
		"Modded maps|Latency meter"), "Bots 3: the bot rows under the Bots heading, Games after them");
	to_line("  Bot skill");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_BOT_SKILL") && !strcmp(getenv("HALO_BOT_SKILL"), "heroic"), "Bot skill: Normal, then Heroic");
	/* the bots' routes: Direct, then Lanes and Mixed, live (bots_routes.c
	reads it each second) */
	to_line("  Bot routes");
	check(strstr(menu, "Direct") && strstr(menu, "flank") && menu_fits(), "Bot routes: Direct, its help saying the sides");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_BOT_ROUTES") && !strcmp(getenv("HALO_BOT_ROUTES"), "lanes") && strstr(menu, "Lanes") && menu_fits(),
		"Bot routes: Direct, then Lanes");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_BOT_ROUTES") && !strcmp(getenv("HALO_BOT_ROUTES"), "mixed") && strstr(menu, "Mixed"), "Bot routes: then Mixed");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_BOT_ROUTES") && !strcmp(getenv("HALO_BOT_ROUTES"), "direct"), "Bot routes: Direct again");
	/* the bots' playlist: Off, then Small maps ... Custom, live (bots_playlist.c
	reads it at each game's scores) */
	to_line("  Bot playlist");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_BOT_PLAYLIST") && !strcmp(getenv("HALO_BOT_PLAYLIST"), "small") && strstr(menu, "Small maps") &&
		menu_fits(), "Bot playlist: Off, then Small maps");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_BOT_PLAYLIST") && !strcmp(getenv("HALO_BOT_PLAYLIST"), "custom") && strstr(menu, "Custom") &&
		strstr(menu, "bots_playlist.txt") && menu_fits(), "Bot playlist: Custom, its help naming bots_playlist.txt");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_BOT_PLAYLIST") && !strcmp(getenv("HALO_BOT_PLAYLIST"), "off"), "Bot playlist: Off again");
	to_line("Bots");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter") && !strcmp(getenv("HALO_BOTS"), "0"),
		"Bots Off: the bot rows hidden again");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	check(strstr(menu, "\n\x04This Vita: vitauser  192.168.1.23\n") != NULL, "this Vita's address, as the others reach it");

	/* the game's menus with OpenCE's screens (their Direct Link joins codes):
	no code row; without them a line says what they need, and the row stays */
	halo_pc_menus_state = 1;
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Friends|Bots|Co-op players|Unseen players|Modded maps|Latency meter") &&
		!strstr(menu, "Online menus") && menu_height_fits(),
		"the PC menus in the game: Connection, Friends (in the code row's place) and Modded maps; it fits");
	halo_pc_menus_state = -1;
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	printf("%s\n--\n", menu);
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter") &&
		strstr(menu, "\n\x04Online menus need bitmaps.map, loc.map: README\n") && menu_fits(),
		"the Xbox's menus (no bitmaps.map, loc.map): one line says what the online menus need; Join with a code stays");
	/* (that line too, on the classic panel - the Halo CE menus' -: short of
	room, the first heading, Network, gives way; the Halo 3 menus' panel
	fits its rows to its height, every heading kept) */
	vita_settings_set("HALO_MENU_STYLE", "classic");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	check(menu_height_fits() && !strstr(menu, "Network") && !strncmp(menu_line(1, line, sizeof(line)), "Connection\x02", 11) &&
		!strcmp(outline_of(labels, sizeof(labels), 1), "Bots|Games"),
		"the classic panel with that line: Network gives way, Bots and Games stay; the tab fits its height");
	vita_settings_set("HALO_MENU_STYLE", "halo3");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);
	check(!strcmp(outline_of(labels, sizeof(labels), 1), "Network|Bots|Games"), "the Halo 3 panel: every heading kept");

	/* the hosted game's settings, as the game's Server Setup sets them
	(vita_settings_set): applied at once and kept in settings.txt */
	check(vita_settings_set("HALO_NET_LOBBY_NAME", "  Birch's game\x01\t of the century  ") &&
		!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Birch's game of") && !strcmp(lobby_name, "Birch's game of") &&
		strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME=Birch's game of\n"),
		"Server Setup's name: printable ASCII, trimmed, 15 at most; in the environment, the listing and settings.txt");
	vita_settings_set("HALO_NET_LOBBY_NAME", "\x01\x02   ");
	check(!getenv("HALO_NET_LOBBY_NAME")[0] && !lobby_name[0] && !strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME"),
		"nothing printable: none (the game's own name, the profile's), not saved");
	vita_settings_set("HALO_NET_LOBBY_NAME", "Birch's game");
	vita_settings_set("HALO_NET_MAX_PLAYERS", "2");
	check(!strcmp(getenv("HALO_NET_MAX_PLAYERS"), "2") && strstr(file_text(SETTINGS_FILE), "HALO_NET_MAX_PLAYERS=2\n"),
		"Max players 2, saved");
	vita_settings_set("HALO_NET_MAX_PLAYERS", "8");
	check(!strcmp(getenv("HALO_NET_MAX_PLAYERS"), "8") && !strstr(menu, "Restart the game"), "Max players 8, live");
	vita_settings_set("HALO_NET_HOST_PUBLIC", "false");
	check(lobby_public == 0 && !strcmp(getenv("HALO_NET_HOST_PUBLIC"), "false"), "Private: at once (network.host_public)");
	vita_settings_set("HALO_NET_HOST_PUBLIC", "true");
	check(lobby_public == 1 && !strcmp(getenv("HALO_NET_HOST_PUBLIC"), "true"), "Public: listed at once");
	/* (co-op's own, X on its waiting screen: coop_menu.c) */
	check(!strcmp(getenv("HALO_NET_COOP_PUBLIC"), "false") && lobby_coop_public == -1,
		"co-op: private unless chosen (network.coop_public)");
	vita_settings_set("HALO_NET_COOP_PUBLIC", "true");
	check(lobby_coop_public == 1 && lobby_public == 1 && !strcmp(getenv("HALO_NET_COOP_PUBLIC"), "true") &&
		strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP_PUBLIC=true\n"), "co-op Public: listed at once, saved");
	vita_settings_set("HALO_NET_HOST_PUBLIC", "false");
	check(lobby_coop_public == 1 && lobby_public == 0, "the other games' Visibility leaves co-op's alone");
	vita_settings_set("HALO_NET_COOP_PUBLIC", "false");
	check(lobby_coop_public == 0 && !strcmp(getenv("HALO_NET_COOP_PUBLIC"), "false"), "co-op Private again: at once");
	vita_settings_set("HALO_NET_HOST_PUBLIC", "true");
	vita_settings_set("HALO_NET_LOBBY_PASSWORD", "hunter2");
	check(!strcmp(getenv("HALO_NET_LOBBY_PASSWORD"), "hunter2") && !strcmp(lobby_password, "hunter2") &&
		strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PASSWORD=hunter2\n") &&
		strstr(log_text, "settings: password set (the game)") && !strstr(log_text, "hunter2") && !strstr(menu, "hunter2"),
		"the password: kept and saved, never in halo.log or the panel");
	vita_settings_set("HALO_NET_LOBBY_PASSWORD", "");
	check(!getenv("HALO_NET_LOBBY_PASSWORD")[0] && !strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PASSWORD"),
		"an empty password: none, not saved");

	/* the game's state, on the tab */
	game_status(SYSTEM_LINK_STATE_SEARCHING, 0, 2, 0);
	check(strstr(menu, "Looking for games: 2 found") != NULL, "the System Link list, two games");
	status_maximum = 8;
	game_status(SYSTEM_LINK_STATE_HOSTING, 1, 0, 1);
	check(strstr(menu, "\n\x04Hosting: waiting for players (max 8)\n") != NULL, "hosting alone: waiting, the most players");
	game_status(SYSTEM_LINK_STATE_HOSTING, 3, 0, 1);
	check(strstr(menu, "\n\x04Hosting: 3 of 8 players in the lobby\n") && menu_fits(), "hosting: three of eight");
	status_maximum = 0;
	game_status(SYSTEM_LINK_STATE_HOSTING, 3, 0, 1);
	check(strstr(menu, "Hosting: 3 Vitas in the lobby") != NULL, "hosting, the game not saying its most: three Vitas");
	game_status(SYSTEM_LINK_STATE_LOBBY, 2, 0, 0);
	check(strstr(menu, "In a lobby: 2 Vitas, the host starts") != NULL, "in another's lobby");
	game_status(SYSTEM_LINK_STATE_IN_GAME, 4, 0, 1);
	check(strstr(menu, "In a game: 4 Vitas (you host)") != NULL, "in a game this Vita hosts");
	game_status(SYSTEM_LINK_STATE_PLAYING, 0, 0, 0);
	check(strstr(menu, "Playing: quit the level to host or join") != NULL, "playing a level");
	game_status(SYSTEM_LINK_STATE_MENUS, 0, 0, 0);

	/* Join with a code: the code screen, then Join's steps (761c59ce) */
	to_line("Join with a code");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "JOIN WITH A CODE\n(the host's Multiplayer tab shows it)", 54) &&
		!strstr(menu, "Triangle") && menu_fits(), "Join with a code: the host's code first (no public games: the game's menus')");
	press(VITA_BUTTON_TRIANGLE);
	check(!strncmp(menu, "JOIN WITH A CODE", 16) && browsing == 0, "triangle: nothing");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN A GAME\n", 12) && strstr(menu, "\nOnline: ") && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE &&
		!strcmp(joined_code, "AAAA-AAAA"), "the code taken: Join's steps, with how the lookup goes, and nothing asked yet");
	check(menu_fits(), "Join's steps online: 46 characters a line");
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_JOIN, "Join: the game is asked (join)");
	game_answers(SYSTEM_LINK_ANSWER_IN_PLAY);
	check(menu_visible && strstr(menu, "!Leave the level first: Start, then Quit"), "the game said no: the guide says why");
	/* ... and a game that does not answer in 3 s is asked no more */
	press(VITA_BUTTON_CROSS);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_JOIN, "cross again: asked again");
	clock_us += 3100000;
	frame(0);
	check(halo_system_link_request == SYSTEM_LINK_REQUEST_NONE && strstr(menu, "!The game did not answer: try again"),
		"no answer in 3 s: the request withdrawn, the guide says so");
	press(VITA_BUTTON_CROSS);
	game_answers(SYSTEM_LINK_ANSWER_OPENED);
	check(!menu_visible && frame(0) == 0, "the System Link screen opened: the panel closes");
	open_panel();
	check(strstr(menu, "*Multiplayer") && !strstr(menu, "\n\x03"), "the panel opens again on the tab");

	/* Connection: Same Wi-Fi, Ad hoc, Online (experimental) */
	to_line("Connection");
	check(strstr(menu, "\n\x05Internet play by code (experimental)") != NULL, "Connection Online's help line");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "online") && strstr(menu_line(menu_selected, line, sizeof(line)), "< Online  "),
		"Connection: Online is the last");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && !strstr(menu, "Restart the game") && !restart_pending,
		"Connection: Ad hoc asks for no restart (the game switches live)");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_VITA_NETWORK"), "adhoc") && strstr(menu_line(menu_selected, line, sizeof(line)),
		"< Ad hoc >"), "... Same Wi-Fi and back: Online offered on either side (no dev setting needed)");
	clock_us += 5000000;
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Ad hoc room|Join the room|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter"),
		"Ad hoc chosen: the room's rows on the tab (the code's too, while Online runs)");
	check(!strcmp(outline_of(labels, sizeof(labels), 0), "=Network|Connection|Ad hoc room|Join the room|Join with a code|"
		"=Bots|Bots|=Games|Co-op players|Unseen players|Modded maps|Latency meter"),
		"Ad hoc chosen: the room's rows under Network");
	/* (the classic panel: nine rows and four lines - the headings give way
	till the tab fits) */
	vita_settings_set("HALO_MENU_STYLE", "classic");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_UP);
	check(menu_height_fits() && menu_rows() == 9 && strstr(menu_line(menu_selected, line, sizeof(line)), "Connection\x02"),
		"the classic panel, Ad hoc chosen while Online runs: the tab fits its height (headings given way), Connection "
		"still chosen");
	vita_settings_set("HALO_MENU_STYLE", "halo3");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_UP);
	check(!strcmp(outline_of(labels, sizeof(labels), 1), "Network|Bots|Games"), "... the Halo 3 panel: every heading kept");
	to_line("Join the room");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 0 && strstr(menu, "First the game switches to Ad hoc, at its menus"),
		"Join the room waits for the game to switch");

	/* the next start, in ad hoc play */
	restart_pending = 0;
	vita_settings_load();
	check(!strcmp(getenv("HALO_NET_ADHOC"), "true"), "Connection Ad hoc: ad hoc play on");
	to_tab("Multiplayer");
	clock_us += 600000;
	frame(0);
	check(strstr(menu, "\n\x04This Vita: vitauser, ad hoc room 1\n") && strstr(menu, "\n\x04" "Ad hoc: not in a group\n"),
		"ad hoc: this Vita's room, not in a group");
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Ad hoc room|Join the room|Bots|Co-op players|Unseen players|Modded maps|Latency meter") && menu_fits(),
		"ad hoc: Connection, the room, Join the room, Modded maps (no internet play rows)");
	clock_us += 5000000;
	to_line("Ad hoc room");
	check(strstr(menu, "\n\x05Vitas in the same room play together\n") != NULL, "Ad hoc room's help");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_ADHOC_ROOM"), "2"), "Ad hoc room 2");
	to_line("Join the room");
	check(strstr(menu, "\n\x05" "First the system's dialog joins ad hoc room 2\n") != NULL,
		"Join the room, not in the group: the help says the dialog joins it");
	/* the room's group: the system's dialog, the panel closed; then the
	game's own System Link screen */
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 1 && adhoc_room == 2 && adhoc_mode == 0 && !menu_visible &&
		halo_system_link_request == SYSTEM_LINK_REQUEST_NONE, "cross: the ad hoc dialog (connect, room 2), the panel closed");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(0) == 1, "the game sees no buttons while the dialog is up");
	adhoc_state_value = 2;
	check(frame(0) == 0 && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE,
		"in the group: the game has the pad, nothing asked (its menus' System Link next)");
	open_panel();
	to_line("Join the room");
	press(VITA_BUTTON_CROSS);
	check(adhoc_connects == 1 && strstr(menu, "In room 2's group already"), "in the group already: it says so");
	press(VITA_BUTTON_CIRCLE);
	/* the game's Halo 3 menus (AD HOC): the group's state each frame, and
	their join - the dialog, as the panel's own, the panel closed - asked
	once (system_link_shortcut.h) */
	check(halo_adhoc_group == HALO_ADHOC_GROUP_IN, "the game's menus see the group joined");
	adhoc_state_value = 0;
	frame(0);
	check(halo_adhoc_group == HALO_ADHOC_GROUP_NONE, "... and left");
	open_panel();
	halo_adhoc_request = HALO_ADHOC_REQUEST_JOIN;
	frame(0);
	check(adhoc_connects == 2 && adhoc_room == 2 && !menu_visible && halo_adhoc_request == HALO_ADHOC_REQUEST_NONE &&
		halo_adhoc_group == HALO_ADHOC_GROUP_JOINING, "the game's menus' join: the dialog (room 2), the panel closed");
	halo_adhoc_request = HALO_ADHOC_REQUEST_JOIN;
	frame(0);
	check(adhoc_connects == 2 && halo_adhoc_request == HALO_ADHOC_REQUEST_NONE, "... not again while the dialog is up");
	adhoc_state_value = 2;
	frame(0);
	check(halo_adhoc_group == HALO_ADHOC_GROUP_IN, "... then in the group");
	/* the game left Ad hoc: the group left (the Wi-Fi back), the panel's
	running connection the game's (network_connection.c, live) */
	halo_network_connection = HALO_NETWORK_WIFI;
	halo_adhoc_request = HALO_ADHOC_REQUEST_LEAVE;
	frame(0);
	check(adhoc_state_value == 0 && halo_adhoc_request == HALO_ADHOC_REQUEST_NONE && !strcmp(running_network, "wifi") &&
		halo_adhoc_group == HALO_ADHOC_GROUP_OFF, "the game left Ad hoc: the group left, the panel's connection Same Wi-Fi");
	halo_network_connection = HALO_NETWORK_ADHOC;
	frame(0);
	check(!strcmp(running_network, "adhoc") && halo_adhoc_group == HALO_ADHOC_GROUP_NONE,
		"... and Ad hoc again: the panel follows, not in a group");
	adhoc_state_value = 2;
	frame(0);
	halo_network_connection = -1;

	/* Same Wi-Fi: Connection and Modded maps */
	vita_settings_set("HALO_VITA_NETWORK", "wifi");
	restart_pending = 0;
	vita_settings_load();
	open_panel();
	to_tab("Multiplayer");
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Bots|Co-op players|Unseen players|Modded maps|Latency meter") && menu_fits(), "Same Wi-Fi: Connection, Modded maps");
	/* Latency meter (latency_meter.c): On as shipped, Off and back, live */
	to_line("Latency meter");
	check(strstr(menu, "\nLatency meter\x02  On >") && setting_named("HALO_LATENCY_METER") &&
		strstr(setting_named("HALO_LATENCY_METER")->help, "Ping to the host in network games"),
		"Latency meter: On as shipped, its help");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nLatency meter\x02< Off  ") && getenv("HALO_LATENCY_METER") &&
		!strcmp(getenv("HALO_LATENCY_METER"), "0"), "Latency meter Off: HALO_LATENCY_METER 0");
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_LATENCY_METER") && !strcmp(getenv("HALO_LATENCY_METER"), "1"), "Latency meter On again: 1");
	/* Unseen players (objects.c, objects_remote_lod_skipped): Auto as
	shipped (more than 16 players: crossplay's large PC games), Reduced (1)
	and Full (0), live */
	to_line("Unseen players");
	check(strstr(menu, "\nUnseen players\x02  Auto >") &&
		(!getenv("HALO_NET_REMOTE_LOD") || !strcmp(getenv("HALO_NET_REMOTE_LOD"), "auto")),
		"Unseen players: Auto as shipped");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nUnseen players\x02< Reduced >") && getenv("HALO_NET_REMOTE_LOD") &&
		!strcmp(getenv("HALO_NET_REMOTE_LOD"), "1"), "Unseen players Reduced: HALO_NET_REMOTE_LOD 1");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nUnseen players\x02< Full  ") && !strcmp(getenv("HALO_NET_REMOTE_LOD"), "0"),
		"Unseen players Full: 0");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_NET_REMOTE_LOD") && !strcmp(getenv("HALO_NET_REMOTE_LOD"), "auto"), "Unseen players Auto again: auto");
	/* Crossplay (docs/crossplay.md): Off as shipped, On and back */
	to_line("Crossplay");
	check(strstr(menu, "\nCrossplay*\x02  Off >") && setting_named("HALO_CROSSPLAY") &&
		strstr(setting_named("HALO_CROSSPLAY")->help, "play PCs (OpenCE)"), "Crossplay: Off as shipped, its help");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nCrossplay*\x02< On  ") && getenv("HALO_CROSSPLAY") && !strcmp(getenv("HALO_CROSSPLAY"), "1"),
		"Crossplay On: HALO_CROSSPLAY 1");
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_CROSSPLAY") && !strcmp(getenv("HALO_CROSSPLAY"), "0"), "Crossplay Off again: 0");
	/* (after a restart: the game reads it once; with it on the Vita hosts
	nothing, Host a game and Host co-op campaign say why) */
	check(setting_named("HALO_CROSSPLAY")->restart, "Crossplay: after a restart");
	{
		char saved_network[sizeof(running_network)];
		int saved_action = guide_action;

		snprintf(saved_network, sizeof(saved_network), "%s", running_network);
		snprintf(running_network, sizeof(running_network), "%s", chosen_network());
		test_crossplay_wire = 1;
		guide_action = ACTION_HOST;
		check(guide_blocker() && strstr(guide_blocker(), "Crossplay is on"), "Crossplay on: Host a game says to turn it off");
		guide_action = ACTION_CAMPAIGN;
		check(guide_blocker() && strstr(guide_blocker(), "Crossplay is on"), "Crossplay on: Host co-op campaign too");
		guide_action = ACTION_JOIN;
		check(!guide_blocker() || !strstr(guide_blocker(), "Crossplay"), "Crossplay on: Join a game is not blocked by it");
		test_crossplay_wire = 0;
		guide_action = ACTION_HOST;
		check(!guide_blocker() || !strstr(guide_blocker(), "Crossplay"), "Crossplay off: Host a game is not blocked by it");
		guide_action = saved_action;
		snprintf(running_network, sizeof(running_network), "%s", saved_network);
	}
	/* Co-op players (network.coop_players): 2 as shipped, 3 and 4 experimental */
	to_line("Co-op players");
	check(strstr(menu, "\nCo-op players\x02  2 >") && strstr(setting_named("HALO_NET_COOP_PLAYERS")->help, "may lag"),
		"Co-op players: 2 as shipped, its help");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nCo-op players\x02< 3 (experimental) >") && getenv("HALO_NET_COOP_PLAYERS") &&
		!strcmp(getenv("HALO_NET_COOP_PLAYERS"), "3"), "Co-op players 3, experimental: HALO_NET_COOP_PLAYERS 3");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nCo-op players\x02< 4 (experimental)  ") && !strcmp(getenv("HALO_NET_COOP_PLAYERS"), "4"),
		"Co-op players 4, experimental, the last");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_NET_COOP_PLAYERS") && !strcmp(getenv("HALO_NET_COOP_PLAYERS"), "2"), "Co-op players 2 again");
	press(VITA_BUTTON_CIRCLE);
}

/* internet play's rows: the code typed */
static int menu_lines_fit(void);

/* the public games with Crossplay on (crossplay_guard.h, docs/crossplay.md
6): badges; a large or co-op PC game asked about once */
/* Multiplayer's Forge page (Forge and File Share for the Halo CE menus too):
the map (those in the maps folder), its map variants, Start Forge asking the
game; File Share's list, square deleting one after a question */
static void test_forge_page(void)
{
	char line[128];

	test_forge_count = 0;
	test_forge_entry("bloodgulch", "gulch_a.hmv", "Gulch A", 3, 0);
	test_forge_entry("bloodgulch", "gulch_b.hmv", "Gulch B", 12, 0);
	test_forge_entry("bloodgulch", "bad.hmv", NULL, 0, 1);
	test_forge_entry("chillout", "cold.hmv", "Cold", 5, 0);
	open_panel();
	check(to_tab("Game") && !strstr(menu, "\nForge"), "Menu style Halo 3: no Forge row (the Halo 3 main menu has FORGE)");
	vita_settings_set("HALO_MENU_STYLE", "classic");
	press(VITA_BUTTON_R);
	press(VITA_BUTTON_L);
	check(to_tab("Game") && strstr(menu, "\nShow dev settings\x02  Off >\nForge and File Share\x02  4 variants  >") &&
		menu_height_fits(), "Menu style Halo CE: Game's last row Forge and File Share, how many map variants; the tab fits");
	check(open_page("Forge") && strstr(menu, "\n\x03Game > Forge and File Share\n"), "the Forge page");
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(2, line, sizeof(line)), "Map\x02  Blood Gulch") &&
		!strcmp(menu_line(3, line, sizeof(line)), "Map variant\x02  None >") &&
		!strcmp(menu_line(4, line, sizeof(line)), "Start Forge\x02  >") && !menu_line(5, line, sizeof(line))[0] &&
		!strcmp(menu_line(6, line, sizeof(line)), "\x08" "File Share") &&
		strstr(menu, "\nGulch A\x02  Blood Gulch\n") && strstr(menu, "\nDamaged file\x02  Blood Gulch\n") &&
		strstr(menu, "\nCold\x02  Chill Out\n") && menu_fits() && menu_height_fits(),
		"Forge: Map (Blood Gulch first), Map variant (None), Start Forge; File Share's map variants, each fits");
	check(menu_selected == 2 && strstr(menu, "\n\x05The multiplayer map to edit\n") &&
		strstr(menu, "Left/right: change"), "Map chosen first, its help");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nMap\x02  Chill Out\n") != NULL, "right: Chill Out (Battle Creek is not in the maps folder)");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nMap\x02  Blood Gulch\n") != NULL, "right again: Blood Gulch, past Battle Creek");
	to_line("Map variant");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nMap variant\x02< Gulch A >\n") != NULL, "Map variant right: Gulch A");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nMap variant\x02< Gulch B  \n") != NULL, "right: Gulch B, the last (the damaged one is not offered)");
	press(VITA_BUTTON_LEFT);
	/* in a level: not started */
	halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE] = SYSTEM_LINK_STATE_PLAYING;
	to_line("Start Forge");
	check(strstr(menu, "\n\x05" "A local game: you fly as the monitor and place objects\n") && strstr(menu, "Cross: select"),
		"Start Forge's help");
	press(VITA_BUTTON_CROSS);
	check(menu_visible && halo_system_link_request == SYSTEM_LINK_REQUEST_NONE &&
		strstr(menu, "Leave the level first: Start, then Quit"), "in a level: Start Forge says to leave it first");
	halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE] = SYSTEM_LINK_STATE_MENUS;
	press(VITA_BUTTON_CROSS);
	check(!menu_visible && halo_system_link_request == SYSTEM_LINK_REQUEST_FORGE && halo_forge_request_map == 1 &&
		!strcmp(halo_forge_request_variant, "gulch_a.hmv"), "at the menus: the game asked for Forge on Blood Gulch, Gulch A; closed");
	halo_system_link_request = SYSTEM_LINK_REQUEST_NONE;
	frame(0);
	/* File Share: delete, asked first */
	open_panel();
	check(to_tab("Game") && strstr(menu, "\nForge and File Share\x02  4 variants  >") && open_page("Forge"), "the Forge page again");
	to_line("Cold");
	check(strstr(menu, "\n\x05" "5 objects, saved 2026-10-18 12:00\n") && strstr(menu, "Square: delete"),
		"a map variant's help and buttons");
	press(VITA_BUTTON_SQUARE);
	check(!strncmp(menu, "DELETE MAP VARIANT", 18) && strstr(menu, "Cold  (Chill Out)"), "square asks before deleting");
	press(VITA_BUTTON_CIRCLE);
	check(test_forge_count == 4 && strstr(menu, "\nCold\x02"), "circle keeps it");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CROSS);
	check(test_forge_deleted == 1 && test_forge_count == 3 && !strstr(menu, "\nCold\x02") && strstr(menu, "Deleted \"Cold\"."),
		"cross deletes it");
	to_line("Damaged file");
	clock_us += 20000000;
	frame(0);
	check(strstr(menu, "Damaged: it can't be used. Square deletes it") != NULL, "a damaged one: its help");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nForge and File Share\x02  3 variants  >") != NULL, "back on Game: three now");
	/* in a Forge game: Clear dead AI and Clear all AI (asked first) */
	check(open_page("Forge") && !strstr(menu, "Clear dead AI"), "no Forge game: no Clear AI rows");
	press(VITA_BUTTON_CIRCLE);
	test_forge_playing = 1;
	test_forge_alive = 5;
	test_forge_dead = 3;
	check(open_page("Forge") && strstr(menu, "\nStart Forge\x02  >\nClear dead AI\x02  3  >\nClear all AI\x02  8  >\n") &&
		menu_fits() && menu_height_fits(), "a Forge game: Clear dead AI (3) and Clear all AI (8) under Start Forge; it fits");
	to_line("Clear dead AI");
	check(strstr(menu, "\n\x05" "Dead AI you placed, out of the layout: room for more\n") && strstr(menu, "Cross: select"),
		"Clear dead AI's help");
	press(VITA_BUTTON_CROSS);
	check(test_forge_cleared == 0 && strstr(menu, "3 dead AI cleared"), "cross: the dead cleared, said");
	test_forge_cleared = -1;
	test_forge_dead = 0;
	to_line("Clear all AI");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "CLEAR ALL AI", 12) && strstr(menu, "5 alive and 0 dead") && test_forge_cleared == -1,
		"Clear all AI asks first");
	press(VITA_BUTTON_CIRCLE);
	check(test_forge_cleared == -1 && strstr(menu, "\nClear all AI\x02"), "circle keeps them");
	press(VITA_BUTTON_CROSS);
	press(VITA_BUTTON_CROSS);
	check(test_forge_cleared == 1 && strstr(menu, "All AI cleared"), "cross, cross: all cleared, said");
	test_forge_playing = 0;
	test_forge_cleared = -1;
	press(VITA_BUTTON_CIRCLE);
	vita_settings_set("HALO_MENU_STYLE", "halo3");
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(!menu_visible, "closed");
}

/* Game's Player model (port/linux/game/player_models.c): shown while the
maps folder's Custom Edition maps have a player model, the Spartan first,
the profile's choice; changed with left and right, for the game to keep for
the profile, never in settings.txt; the tab still fits, the Forge row too */
static void test_player_model_row(void)
{
	char line[128];

	if (menu_visible)
		open_panel();
	test_model_count = 1;
	open_panel();
	check(to_tab("Game") && !strstr(menu, "\nPlayer model"), "no player model found: no Player model row");
	open_panel();
	test_model_count = 3;
	test_model_chosen = 2;
	test_model_choice = -2;
	open_panel();
	check(to_tab("Game") && strstr(menu, "\nPlayer model\x02< Covenant V Marines Beta 5  \n") && menu_height_fits(),
		"two found: Player model, the profile's choice (the last), its map's own name; the tab fits");
	check(to_line("Player model"), "the row is chosen");
	press(VITA_BUTTON_LEFT);
	check(test_model_choice == 1 && strstr(menu, "\nPlayer model\x02< Spartan (Extinction) >\n"),
		"left: Spartan (Extinction), given to the game for the profile");
	press(VITA_BUTTON_LEFT);
	check(test_model_choice == 0 && strstr(menu, "\nPlayer model\x02  Spartan >\n"), "left again: the Spartan");
	press(VITA_BUTTON_LEFT);
	check(test_model_choice == 0 && strstr(menu, "\nPlayer model\x02  Spartan >\n"), "no further left than the Spartan");
	press(VITA_BUTTON_RIGHT);
	check(test_model_choice == 1, "right: Spartan (Extinction) again");
	open_panel();
	check(!strstr(file_text(SETTINGS_FILE), "HALO_PLAYER_MODEL"), "the choice is the profile's: never in settings.txt");
	(void)line;
	test_model_count = 1;
	test_model_chosen = 0;
}

static void test_crossplay_browser(void)
{
	int saved_screen = screen;
	unsigned long now;

	crossplay_guard_forget();
	browsing = 1;
	screen = SCREEN_BROWSE;
	browse_count = browse_selected = 0;
	browse_refresh();
	show();
	check(browse_count == 3 && strstr(menu, "\npc host         20/128 Blood Gulch  PC EXP\n") &&
		strstr(menu, "\npc coop          2/16 Halo         PC COOP EXP\n") &&
		strstr(menu, "\npc server        2/16 Blood Gulch  SRV\n") &&
		strstr(menu, "\nExperimental: over 16 players may lag\n") && menu_lines_fit(),
		"Crossplay: the public games badged: PC or SRV, EXP (more than 16), COOP EXP; the chosen one's in words");
	if (!strstr(menu, "pc host         20/128"))
		printf("%s\n--\n", menu);
	/* a PC co-op game: experimental, asked about once (no: not joined; yes:
	joined, remembered) */
	joined_id[0] = 0;
	message_pending = 0;
	browse_selected = 1;
	show();
	check(strstr(menu, "\nPC co-op: experimental on the Vita\n") != NULL, "a PC co-op game: experimental, it says");
	browse_input(VITA_BUTTON_CROSS);
	show();
	check(screen == SCREEN_LARGE && !joined_id[0] && !message_pending && strstr(menu, "PC co-op game") &&
		strstr(menu, "Join anyway?") && !strstr(menu, "more than 4") && strstr(menu, "pc coop  2/16") && menu_lines_fit(),
		"cross on a PC co-op game: Join anyway? first, the co-op question");
	printf("%s\n--\n", menu);
	large_input(VITA_BUTTON_CIRCLE);
	now = (unsigned long)(now_us() / 1000);
	check(screen == SCREEN_BROWSE && !joined_id[0] && crossplay_guard_answered("pc coop", now) == 0,
		"circle on the co-op question: not joined, the no remembered");
	browse_input(VITA_BUTTON_CROSS);
	large_input(VITA_BUTTON_CROSS);
	now = (unsigned long)(now_us() / 1000);
	check(screen == SCREEN_GUIDE && !strcmp(joined_id, "00000000000000000000000000000012") &&
		crossplay_guard_answered("pc coop", now) == 1, "asked again, cross: the co-op game joined, the yes remembered");
	screen = SCREEN_BROWSE;
	browsing = 1;
	joined_id[0] = 0;
	message_pending = 0;
	/* a large one: asked, no; asked again, yes (remembered for its lobby);
	then joined at once */
	browse_selected = 0;
	browse_input(VITA_BUTTON_CROSS);
	show();
	check(screen == SCREEN_LARGE && !joined_id[0] &&
		strstr(menu, "Large PC game: more than 16 players can make") && strstr(menu, "Join anyway?") &&
		strstr(menu, "pc host  20/128") && menu_lines_fit(), "cross on a large PC game: Join anyway? first");
	printf("%s\n--\n", menu);
	large_input(VITA_BUTTON_CIRCLE);
	now = (unsigned long)(now_us() / 1000);
	check(screen == SCREEN_BROWSE && !joined_id[0] && crossplay_guard_answered("pc host", now) == 0,
		"circle: not joined, the no remembered");
	browse_input(VITA_BUTTON_CROSS);
	check(screen == SCREEN_LARGE && !joined_id[0], "chosen again: asked again");
	large_input(VITA_BUTTON_CROSS);
	now = (unsigned long)(now_us() / 1000);
	check(screen == SCREEN_GUIDE && !strcmp(joined_id, "00000000000000000000000000000011") &&
		crossplay_guard_answered("pc host", now) == 1, "cross: joined, the yes remembered (its lobby asks no more)");
	screen = SCREEN_BROWSE;
	browsing = 1;
	joined_id[0] = 0;
	browse_selected = 0;
	browse_input(VITA_BUTTON_CROSS);
	check(screen == SCREEN_GUIDE && !strcmp(joined_id, "00000000000000000000000000000011"),
		"asked once: chosen again, joined at once");
	/* a server of 16: joined at once */
	screen = SCREEN_BROWSE;
	browsing = 1;
	joined_id[0] = 0;
	browse_selected = 2;
	browse_input(VITA_BUTTON_CROSS);
	check(screen == SCREEN_GUIDE && !strcmp(joined_id, "00000000000000000000000000000013"), "a PC server of 16: joined at once");
	crossplay_guard_forget();
	browsing = 0;
	browse_count = browse_selected = 0;
	screen = saved_screen;
}

static void test_online_rows(void)
{
	char labels[512];
	int info;

	/* (a session started Online) */
	vita_settings_set("HALO_VITA_NETWORK", "online");
	restart_pending = 0;
	vita_settings_load();
	to_tab("Multiplayer");
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter") && !strstr(menu, "Ad hoc dialog"),
		"Online: the code row back (the ad hoc dialog stays in Dev)");

	/* a code typed with the D-pad: B (up from A) on the first, 9 (down
	from A: the digits come first) on the fifth, then cross */
	to_line("Join with a code");
	press(VITA_BUTTON_CROSS);
	check(!strncmp(menu, "JOIN WITH A CODE", 16), "Join with a code opens the code screen");
	press(VITA_BUTTON_UP);
	for (int index = 0; index < 4; index++)
		press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	printf("%s\n--\n", menu);
	check(strstr(menu, "B A A A - 9 A A A") != NULL, "the code screen shows the letters typed");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(joined_code, "BAAA-9AAA") && !strncmp(menu, "JOIN A GAME\n", 12), "cross joins the code typed: Join's steps");
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "Looking up BAAA-9AAA") != NULL && strstr(menu, "*Multiplayer"),
		"back on the tab: the help line says it is looked up");

	/* hosting: the code shows on the tab */
	hosting = 1;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA (public)") != NULL, "hosting online: the Multiplayer tab shows the code");
	check(menu_fits(), "Multiplayer with internet play: 46 characters a line");
	/* ... and whether the brokers hold its listing */
	hosting_listing = P2P_LOBBY_HOSTING_LISTED;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA; Listed on 2 of 3 servers") != NULL && menu_fits(),
		"hosting a listed game: on how many servers");
	hosting_listing = P2P_LOBBY_HOSTING_UNREACHABLE;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA; not listed: no game list") != NULL && menu_fits(),
		"hosting a public game no broker holds: not listed");
	hosting_listing = P2P_LOBBY_HOSTING_PENDING;
	clock_us += 5000000;
	frame(0);
	check(strstr(menu, "\n\x04Your code: QX7K-M2PA; getting listed...") != NULL, "being listed: getting listed");
	hosting_listing = P2P_LOBBY_HOSTING_NONE;
	hosting = 0;

	/* Crossplay on (the wire the game runs): no code row, codes join Vita
	games (docs/crossplay.md T11); the public games are PCs' */
	test_crossplay_wire = 1;
	to_tab("Dev");
	to_tab("Multiplayer");
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Bots|Co-op players|Unseen players|Modded maps|Latency meter"),
		"Crossplay on: no Join with a code row (codes join Vita games), no Friends row (friends' games are Vitas')");
	test_crossplay_browser();
	test_crossplay_wire = 0;
	to_tab("Dev");
	to_tab("Multiplayer");
	rows_of(labels, sizeof(labels), &info);
	check(!strcmp(labels, "Connection|Crossplay*|Join with a code|Bots|Co-op players|Unseen players|Modded maps|Latency meter"),
		"Crossplay off: Join with a code back");
	to_tab("Dev");
	to_line("Save report");
}

/* the tabs' rows in order (Halo Online's settings' arrangement, the owner's,
Oct 18 2026; the headings over them: test_headings): each with its variable,
values, default and restart as before the regrouping, nothing else on the
tab; Video's two pages, Detail and Performance, with theirs (the profiles'
rows: test_bruno_rows, test_particles_row). A page's row has no variable
and names the page it opens; an action's, its action */
struct layout_row
{
	int page;
	const char *label, *variable, *values;
	int restart;
	const char *shipped;
	/* (a row with no variable: ACTION_PAGE and the page it opens, or the
	action) */
	int action, opens;
};

/* the rows on tab `tab_index` (and, with `with_pages`, on the pages it
opens), in settings[]'s order, are `rows` */
static int layout_is(int tab_index, int with_pages, const struct layout_row *rows, int count)
{
	int index, row = 0, all = 1;

	for (index = 0; index < SETTING_COUNT; index++)
	{
		const struct setting *setting = &settings[index];
		char values[160];
		int choice, length = 0;

		if (pages[setting->page].tab != tab_index || (!with_pages && setting->page != tab_index))
			continue;
		if (row >= count || strcmp(setting->label, rows[row].label) != 0 || setting->page != rows[row].page)
		{
			printf("%s row %d: %s (page %d)\n", pages[tab_index].name, row, setting->label, setting->page);
			all = 0;
			row++;
			continue;
		}
		if (!rows[row].variable)
		{
			/* (a page's row, or an action's) */
			if (setting->kind != KIND_ACTION || setting->action != rows[row].action || setting->variable ||
				!setting->help || (rows[row].action == ACTION_PAGE && (setting->opens != rows[row].opens ||
				pages[setting->opens].tab != tab_index)))
			{
				printf("%s action or page row: %s\n", pages[tab_index].name, setting->label);
				all = 0;
			}
			row++;
			continue;
		}
		values[0] = 0;
		for (choice = 0; choice < setting->count && length < (int)sizeof(values); choice++)
			length += snprintf(values + length, sizeof(values) - length, "%s%s", choice ? "|" : "",
				setting->values[choice]);
		if (!setting->variable || strcmp(setting->variable, rows[row].variable) != 0 || setting->kind != KIND_CHOICE ||
			strcmp(values, rows[row].values) != 0 || setting->restart != rows[row].restart || setting->dev ||
			strcmp(setting->values[shipped_choice[index]], rows[row].shipped) != 0)
		{
			printf("%s row: %s (%s, %s, default %s)\n", pages[tab_index].name, setting->label,
				setting->variable ? setting->variable : "-", values, setting->values[shipped_choice[index]]);
			all = 0;
		}
		row++;
	}
	if (row != count)
		printf("%s: %d rows, not %d\n", pages[tab_index].name, row, count);
	return all && row == count;
}

static void test_tab_layouts(void)
{
	/* (the Update channel a build starts on: a pre-release's Experimental) */
	const char *channel = update_build_is_prerelease(HALO_VITA_VERSION) ? "experimental" : "stable";
	char languages[64];
	const struct layout_row game[] = {
		/* (Halo 3's menus as shipped from 1.1.1: the owner's) */
		{ TAB_GAME, "Menu style", "HALO_MENU_STYLE", "classic|halo3", 0, "halo3" },
		{ TAB_GAME, "Button icons", "HALO_BUTTON_ICONS", "xbox|playstation", 0,
			VITA_BUTTON_ICONS_DEFAULT ? "playstation" : "xbox" },
		{ TAB_GAME, "Language", "HALO_LANGUAGE", languages, 0, "auto" },
		{ TAB_GAME, "FPS counter", "HALO_FRAMERATE_COUNTER", "0|1", 0, "0" },
		{ TAB_GAME, "First-person legs", "HALO_FIRST_PERSON_LEGS", "0|1", 0, "1" },
		/* (shown while the maps folder has a Custom Edition player model) */
		{ TAB_GAME, "Player model", "HALO_PLAYER_MODEL", "0", 0, "0" },
		{ TAB_GAME, "Smooth weapon motion", "HALO_INTERPOLATE_FIRST_PERSON", "1|0", 0, "1" },
		{ TAB_GAME, "Check for updates", NULL, NULL, 0, NULL, ACTION_UPDATE_CHECK },
		{ TAB_GAME, "Update channel", "HALO_UPDATE_CHANNEL", "experimental|stable", 0, channel },
		{ TAB_GAME, "Show dev settings", "HALO_DEV_SETTINGS", "0|1", 0, "0" },
		/* (Forge and File Share, shown with Menu style Halo CE) */
		{ TAB_GAME, "Forge and File Share", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_FORGE },
	};
	static const struct layout_row controls[] = {
		{ TAB_CONTROLS, "Look sensitivity", "XV_LOOK_SENS", "50|75|100|125|150|200", 0, "100" },
		{ TAB_CONTROLS, "Invert look", "XV_INVERT_Y", "0|1", 0, "0" },
		{ TAB_CONTROLS, "Stick deadzone", "XV_DEADZONE", "0|5|10|15", 0, "0" },
		{ TAB_CONTROLS, "Crouch", "HALO_CROUCH_TOGGLE", "1|0", 0, "1" },
		{ TAB_CONTROLS, "Button layout", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_BUTTONS },
		{ TAB_CONTROLS, "Reset controls", NULL, NULL, 0, NULL, ACTION_RESET_CONTROLS },
		{ TAB_CONTROLS, "Gyro aiming", "HALO_GYRO", "off|on|zoomed|hold", 0, "off" },
		{ TAB_CONTROLS, "Gyro settings", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_GYRO },
		{ TAB_CONTROLS, "Rear touch guard", "HALO_TOUCH_REAR_GUARD", "off|light|normal|strong", 0, "normal" },
		{ TAB_CONTROLS, "Touch zones", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_TOUCH },
	};
	static const struct layout_row video[] = {
		{ TAB_GRAPHICS, "Profile", "HALO_PROFILE", "performance|balanced|quality|custom", 0, "balanced" },
		{ TAB_GRAPHICS, "Render resolution", "HALO_RENDER_SCALE", "1|0.875|0.75|0.625|0.5|dynamic", 0, "dynamic" },
		{ TAB_GRAPHICS, "Dynamic minimum", "HALO_DYNAMIC_RES_MIN", "0.5|0.625|0.75", 0, "0.625" },
		{ TAB_GRAPHICS, "Frame limit", "HALO_FRAME_CAP", "30|60|0", 0, "30" },
		{ TAB_GRAPHICS, "Frame interpolation", "HALO_INTERPOLATION", "false|true", 0, "false" },
		{ TAB_GRAPHICS, "Aspect ratio", "HALO_DISPLAY_WIDTH", "848|640", 0, "848" },
		{ TAB_GRAPHICS, "Upscale filter", "HALO_UPSCALE_FILTER", "0|1", 0, "0" },
		{ TAB_GRAPHICS, "Sharp interface", "HALO_SHARP_UI", "1|0", 0, "1" },
		{ TAB_GRAPHICS, "Detail", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_GRAPHICS_DETAIL },
		{ TAB_GRAPHICS, "Performance", NULL, NULL, 0, NULL, ACTION_PAGE, PAGE_GRAPHICS_PERFORMANCE },
		{ PAGE_GRAPHICS_DETAIL, "Model detail", "HALO_MODEL_LOD_SCALE", "1|0.75|0.5|0.35", 0, "0.75" },
		{ PAGE_GRAPHICS_DETAIL, "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", "0|4|8|12", 0, "8" },
		{ PAGE_GRAPHICS_DETAIL, "Object shadows", "HALO_VITA_SHADOWS", "2|1|0", 0, "0" },
		{ PAGE_GRAPHICS_DETAIL, "Dynamic lights", "HALO_MAX_SCENE_LIGHTS", "0|8|4|2", 0, "4" },
		{ PAGE_GRAPHICS_DETAIL, "Effects quality", "HALO_VITA_EFFECTS_QUALITY", "2|1|0", 0, "2" },
		{ PAGE_GRAPHICS_DETAIL, "Sun rays", "HALO_SUN_RAYS", "1|0", 0, "0" },
		{ PAGE_GRAPHICS_DETAIL, "Tiny decals", "HALO_DECAL_MIN_PIXELS", "0|2", 0, "0" },
		{ PAGE_GRAPHICS_DETAIL, "Particles", "HALO_PARTICLE_BUDGET", "full|reduced|low", 0, "reduced" },
		{ PAGE_GRAPHICS_DETAIL, "Particle density", "HALO_PARTICLE_RENDER_DIVISOR", "1|2|3|4", 0, "1" },
		{ PAGE_GRAPHICS_DETAIL, "Ragdolls", "HALO_RAGDOLLS", "0|1", 0, "1" },
		{ PAGE_GRAPHICS_PERFORMANCE, "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", "1|2|4", 0, "4" },
		{ PAGE_GRAPHICS_PERFORMANCE, "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", "1|2|3", 0, "3" },
		{ PAGE_GRAPHICS_PERFORMANCE, "AI think rate", "HALO_AI_THINK_DIVISOR", "1|0|2|3", 0, "0" },
		{ PAGE_GRAPHICS_PERFORMANCE, "Fourth core helpers", "HALO_CPU3_AUX", "0|1|2", 1, "2" },
	};
	static const struct layout_row sound[] = {
		{ TAB_AUDIO, "Sound voices", "HALO_SOUND_CHANNELS", "16|24|32|0", 1, "0" },
		{ TAB_AUDIO, "Sound occlusion", "HALO_SOUND_OBSTRUCTION_TICKS", "1|3|6", 0, "3" },
		{ TAB_AUDIO, "Sound updates", "HALO_SOUND_MANAGER_DIVISOR", "1|2|3", 0, "2" },
	};
	int index, length = snprintf(languages, sizeof(languages), "auto|en");

	/* (the Language row's choices: Automatic, English, then each language
	file's code) */
	for (index = 1; index < lang_count(); index++)
		length += snprintf(languages + length, sizeof(languages) - length, "|%s", lang_code(index));
	check(TAB_GAME == 0 && TAB_CONTROLS == 1 && TAB_GRAPHICS == 2 && TAB_AUDIO == 3 && TAB_MULTIPLAYER == 4 &&
		TAB_DEV == 5 && TAB_COUNT == 6 && !strcmp(pages[TAB_GAME].name, "Game") &&
		!strcmp(pages[TAB_CONTROLS].name, "Controls") && !strcmp(pages[TAB_GRAPHICS].name, "Video") &&
		!strcmp(pages[TAB_AUDIO].name, "Sound") && !strcmp(pages[TAB_MULTIPLAYER].name, "Multiplayer") &&
		!strcmp(pages[TAB_DEV].name, "Dev"), "the tabs in order: Game, Controls, Video, Sound, Multiplayer, Dev");
	check(layout_is(TAB_GAME, 1, game, (int)(sizeof(game) / sizeof(game[0]))),
		"Game: Menu style (Halo 3 as shipped), Button icons, Language, FPS counter; First-person legs, Smooth weapon "
		"motion; Check for updates, Update channel, Show dev settings: each row's variable, values, default and restart "
		"as before; no page of its own");
	check(layout_is(TAB_CONTROLS, 0, controls, (int)(sizeof(controls) / sizeof(controls[0]))),
		"Controls: Look sensitivity, Invert look, Stick deadzone; Crouch, Button layout, Reset controls; Gyro aiming, "
		"Gyro settings; Rear touch guard, Touch zones: each row's variable, values, default and restart as before");
	check(layout_is(TAB_GRAPHICS, 1, video, (int)(sizeof(video) / sizeof(video[0]))),
		"Video: Profile, Render resolution, Dynamic minimum, Frame limit, Frame interpolation, Aspect ratio, Upscale "
		"filter, Sharp interface (on), Detail, Performance; Detail (Model detail ... Particles, Particle density, Ragdolls), Performance "
		"(Scenery updates, Object lighting, AI think rate, Fourth core helpers*): each row's variable, values, default "
		"and restart as before");
	check(layout_is(TAB_AUDIO, 1, sound, (int)(sizeof(sound) / sizeof(sound[0]))),
		"Sound: Sound voices*, Sound occlusion, Sound updates (Language on Game): as before");
	{
		/* (the Controls tab's pages: their rows only theirs) */
		int buttons = 0, touch = 0, gyro = 0, other = 0;

		for (index = 0; index < SETTING_COUNT; index++)
			if (pages[settings[index].page].tab == TAB_CONTROLS && settings[index].page != TAB_CONTROLS)
			{
				buttons += settings[index].page == PAGE_BUTTONS && !strncmp(settings[index].variable, "HALO_XBOX_", 10);
				touch += settings[index].page == PAGE_TOUCH && !strncmp(settings[index].variable, "HALO_TOUCH_", 11);
				gyro += settings[index].page == PAGE_GYRO && !strncmp(settings[index].variable, "HALO_GYRO_", 10);
				other += settings[index].page != PAGE_BUTTONS && settings[index].page != PAGE_TOUCH &&
					settings[index].page != PAGE_GYRO;
			}
		check(buttons == 11 && touch == 6 && gyro == 5 && !other && setting_named("HALO_BUTTON_ICONS")->page == TAB_GAME,
			"Controls' pages: Button layout the eleven Xbox buttons (Button icons on Game), Touch zones the six zones, "
			"Gyro settings the gyro's five");
	}
	{
		/* (the old layout's names: no page or tab has them) */
		int old = 0;

		for (index = 0; index < PAGE_COUNT; index++)
			old |= !strcmp(pages[index].name, "Screen") || !strcmp(pages[index].name, "First person") ||
				!strcmp(pages[index].name, "Advanced") || !strcmp(pages[index].name, "General") ||
				!strcmp(pages[index].name, "Graphics") || !strcmp(pages[index].name, "Audio");
		check(!old && PAGE_GRAPHICS_DETAIL == TAB_COUNT && PAGE_COUNT == TAB_COUNT + 12 &&
			!strcmp(pages[PAGE_GRAPHICS_DETAIL].name, "Detail") && !strcmp(pages[PAGE_GRAPHICS_PERFORMANCE].name, "Performance") &&
			!strcmp(pages[PAGE_BUTTONS].name, "Button layout") && !strcmp(pages[PAGE_TOUCH].name, "Touch zones") &&
			!strcmp(pages[PAGE_GYRO].name, "Gyro settings") && !strcmp(pages[PAGE_PLAY].name, "Play") &&
			!strcmp(pages[PAGE_MAPS].name, "Modded maps") && !strcmp(pages[PAGE_DEV_AB].name, "A/B switches") &&
			!strcmp(pages[PAGE_FORGE].name, "Forge and File Share") && !strcmp(pages[PAGE_MODS].name, "Mod manager") &&
			!strcmp(pages[PAGE_FRIENDS].name, "Friends") && !strcmp(pages[PAGE_PARTY].name, "Party"),
			"the pages rows open: Detail, Performance, Button layout, Touch zones, Gyro settings, Play, Modded maps, A/B "
			"switches, Forge and File Share, Mod manager, Friends, Party (no Screen, First person or Advanced page; no "
			"General, Graphics or Audio tab)");
	}
	check(settings[0].variable && !strcmp(settings[0].variable, "HALO_PROFILE") && !strcmp(settings[1].variable, "HALO_RENDER_SCALE") &&
		!strcmp(settings[2].variable, "HALO_DYNAMIC_RES_MIN"), "Profile still settings[0], Render resolution and its minimum after it");
}

/* the headings over a tab's rows (Halo Online's, headings[]): each above
the row it names, on that row's tab, a line of its own marked '\x08' in the
panel's text (vita_gxm.c draws it smaller: 22 pixels); never chosen, up and
down step over them; none on Sound, Dev or a page a row opens */
static void test_headings(void)
{
	static const struct
	{
		const char *tab, *outline;
	} tabs[] = {
		{ "Game", "=Interface|Menu style|Button icons|Language|FPS counter|=First person|First-person legs|"
			"Smooth weapon motion|=Updates|Check for updates|Update channel|Show dev settings" },
		{ "Controls", "=Look|Look sensitivity|Invert look|Stick deadzone|=Buttons|Crouch|Button layout|Reset controls|"
			"=Gyro|Gyro aiming|Gyro settings|=Touch|Rear touch guard|Touch zones" },
		{ "Video", "=Quality|Profile|Render resolution|Dynamic minimum|Frame limit|Frame interpolation|=Screen|"
			"Aspect ratio|Upscale filter|Sharp interface|=More|Detail|Performance" },
		{ "Sound", "Sound voices*|Sound occlusion|Sound updates" },
		{ "Multiplayer", "=Network|Connection|Crossplay*|Join with a code|=Bots|Bots|=Games|Co-op players|"
			"Unseen players|Modded maps|Latency meter" },
	};
	char outline[512], line[128];
	int index, all = 1, steps = 1, fits = 1, marked = 1;

	/* (the table: each heading over a row of its tab) */
	for (index = 0; index < (int)(sizeof(headings) / sizeof(headings[0])); index++)
	{
		int setting, rows = 0;

		for (setting = 0; setting < SETTING_COUNT; setting++)
			rows += settings[setting].page == headings[index].page && !strcmp(settings[setting].label, headings[index].row);
		if (rows != 1 || headings[index].page >= TAB_COUNT || headings[index].page == TAB_AUDIO ||
			headings[index].page == TAB_DEV || !headings[index].name[0])
		{
			printf("heading %s: over %s, page %d\n", headings[index].name, headings[index].row, headings[index].page);
			all = 0;
		}
	}
	check(all && (int)(sizeof(headings) / sizeof(headings[0])) == 13,
		"the headings: thirteen, each over one row of its tab (none on Sound, Dev or a page a row opens)");
	/* each tab: its headings and rows in order, each heading a line of its
	own, the page fitting the panel; down and up through it */
	all = 1;
	for (index = 0; index < (int)(sizeof(tabs) / sizeof(tabs[0])); index++)
	{
		const char *at;

		if (!to_tab(tabs[index].tab) || strcmp(outline_of(outline, sizeof(outline), 0), tabs[index].outline) != 0)
		{
			printf("%s: %s\n%s\n--\n", tabs[index].tab, outline, menu);
			all = 0;
		}
		for (at = strstr(menu, "\n\x08"); at; at = strstr(at + 1, "\n\x08"))
		{
			int length = (int)strcspn(at + 1, "\n");

			/* (a name, not a row: no value; a row under it) */
			if (length < 2 || memchr(at + 1, '\x02', (size_t)length) || !at[1 + length] ||
				!memchr(at + 2 + length, '\x02', strcspn(at + 2 + length, "\n")))
				marked = 0;
		}
		if (!menu_fits() || !menu_height_fits())
			fits = 0;
		if (!steps_skip_headings())
		{
			printf("(%s)\n", tabs[index].tab);
			steps = 0;
		}
	}
	check(all, "each tab's headings and rows in order: Game (Interface, First person, Updates), Controls (Look, Buttons, "
		"Gyro, Touch), Video (Quality, Screen, More), Sound (none), Multiplayer (Network, Bots, Games)");
	check(marked, "a heading: '\\n\\x08' and its name, a line of its own with no value, a row right under it");
	check(fits, "each tab with its headings: every line fits, and the page the panel's height");
	check(steps, "down and up through each tab: every row chosen once each way, never a heading");
	to_tab("Game");
	check(strstr(menu, "\t*Game|") && strstr(menu, "\n\x08Interface\nMenu style\x02") &&
		strstr(menu, "\nFPS counter\x02  Off >\n\x08" "First person\nFirst-person legs\x02") &&
		strstr(menu, "\nSmooth weapon motion\x02  On >\n\x08Updates\nCheck for updates\x02"),
		"Game's text: Interface, First person and Updates each on a line of its own, marked '\\x08', over its rows");
	{
		/* (the lines a page has: a heading's passive, right before its row) */
		struct line lines[MAXIMUM_LINES];
		int count, at, good = 1, found = 0;

		to_tab("Controls");
		count = page_lines(lines);
		for (at = 0; at < count; at++)
			if (lines[at].type == LINE_HEADING)
			{
				found++;
				if (!line_passive(&lines[at]) || at + 1 >= count || lines[at + 1].type != LINE_SETTING)
					good = 0;
			}
		check(good && found == 4 && lines[0].type == LINE_HEADING && !strcmp(lines[0].text, "Look") &&
			lines[4].type == LINE_HEADING && !strcmp(lines[4].text, "Buttons"),
			"Controls' lines: four headings (Look first), each not chosen (passive), each right before its row");
		/* (a heading remembered as the line chosen - a row hidden since -:
		the panel chooses a row instead) */
		page_selected[page] = 4;
		show();
		check(page_selected[page] == 1 && menu_selected == 2 &&
			!strncmp(menu_line(menu_selected, line, sizeof(line)), "Look sensitivity\x02", 17),
			"a heading remembered as the chosen line: the panel chooses the first row, never the heading");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("XV_LOOK_SENS"), "125"), "... and a press changes that row");
		press(VITA_BUTTON_LEFT);
	}
	to_tab("Game");
}

/* every settings variable 1.0.3 had (its settings.txt) is still a row,
with the values it saved, but the co-op pair (gone: co-op is hosted from
the Campaign screen) and Online games (now Visibility, by OpenCE's name);
and the rows added since (Sun rays, Button icons, Map downloads, Max
players, Visibility, Fourth core helpers, Latency meter, Particles,
First-person legs, MCC mods, PC map vehicles, Sharp interface, Player model; the Play
page's two texts) */
static void test_variables_kept(void)
{
	static const char *const variables[] = {
		"HALO_PROFILE", "HALO_RENDER_SCALE", "HALO_DYNAMIC_RES_MIN", "HALO_DISPLAY_WIDTH", "HALO_UPSCALE_FILTER",
		"HALO_MODEL_LOD_SCALE", "HALO_MIN_OBJECT_PIXELS", "HALO_SCENERY_UPDATE_DIVISOR",
		"HALO_LIGHTING_REFRESH_DIVISOR", "HALO_INTERPOLATE_FIRST_PERSON", "HALO_FRAMERATE_COUNTER", "HALO_FRAME_CAP",
		"HALO_SOUND_CHANNELS", "HALO_SOUND_OBSTRUCTION_TICKS", "XV_LOOK_SENS", "XV_INVERT_Y", "XV_DEADZONE",
		"HALO_CROUCH_TOGGLE", "HALO_TOUCH_TOP_LEFT", "HALO_TOUCH_TOP_RIGHT", "HALO_TOUCH_LEFT_EDGE",
		"HALO_TOUCH_RIGHT_EDGE", "HALO_TOUCH_REAR_LEFT", "HALO_TOUCH_REAR_RIGHT", "HALO_TOUCH_REAR_GUARD",
		"HALO_XBOX_A", "HALO_XBOX_B", "HALO_XBOX_X", "HALO_XBOX_Y", "HALO_XBOX_BLACK", "HALO_XBOX_WHITE",
		"HALO_XBOX_LEFT_TRIGGER", "HALO_XBOX_RIGHT_TRIGGER", "HALO_XBOX_LEFT_STICK", "HALO_XBOX_RIGHT_STICK",
		"HALO_XBOX_BACK", "HALO_DEV_SETTINGS", "HALO_GYRO", "HALO_GYRO_BUTTON", "HALO_GYRO_SENS",
		"HALO_GYRO_INVERT_Y", "HALO_GYRO_TURN", "HALO_GYRO_ASSIST", "HALO_MENU_STYLE", "HALO_VITA_NETWORK", "HALO_CROSSPLAY",
		"HALO_ADHOC_ROOM", "HALO_CUSTOM_EDITION", PERFORMANCE_LOG, "HALO_HANG_CRASH",
		"XV_FPS", "HALO_DEBUG_CAMERA", "HALO_GXM_WCLAMP", "HALO_TARGET_CHAIN_MIN_SIZE", "HALO_FRAME_PHASE_LOCK",
		"HALO_GXM_RTT_SYNC", "HALO_ADHOC_DIALOG_MODE", "HALO_SUN_RAYS",
		"HALO_BUTTON_ICONS", "HALO_DECAL_MIN_PIXELS", "HALO_MAP_SHARE_FROM",
		"HALO_NET_MAX_PLAYERS", "HALO_NET_HOST_PUBLIC", "HALO_NET_COOP_PUBLIC", "HALO_CPU3_AUX",
		"HALO_VITA_SHADOWS", "HALO_MAX_SCENE_LIGHTS", "HALO_VITA_EFFECTS_QUALITY", "HALO_PARTICLE_RENDER_DIVISOR",
		"HALO_AI_THINK_DIVISOR", "HALO_SOUND_MANAGER_DIVISOR", "HALO_INTERPOLATION", "HALO_LATENCY_METER", "HALO_BOTS", "HALO_BOT_SKILL", "HALO_BOT_TEAMS",
		"HALO_BOT_VEHICLES", "HALO_BOT_ROUTES", "HALO_BOT_PLAYLIST", "HALO_NET_COOP_PLAYERS", "HALO_NET_REMOTE_LOD",
		"HALO_LANGUAGE", "HALO_UPDATE_CHANNEL", "HALO_PARTICLE_BUDGET", "HALO_FIRST_PERSON_LEGS", "HALO_RAGDOLLS",
		"HALO_MCC_MODS", "HALO_PC_MAP_VEHICLES", "HALO_SHARP_UI", "HALO_PLAYER_MODEL", "HALO_FRIENDS_APPEAR_OFFLINE",
		"HALO_FRIENDS_JOIN",
	};
	int index, all = 1, choices = 0;

	for (index = 0; index < (int)(sizeof(variables) / sizeof(variables[0])); index++)
	{
		const struct setting *setting = setting_named(variables[index]);

		if (!setting || setting->kind != KIND_CHOICE)
		{
			printf("missing: %s\n", variables[index]);
			all = 0;
		}
	}
	for (index = 0; index < SETTING_COUNT; index++)
		choices += settings[index].kind == KIND_CHOICE;
	check(all && choices == (int)(sizeof(variables) / sizeof(variables[0])),
		"every settings variable of 1.0.3 is still a row (but co-op's and Online games), and no other but those added since");
	check(!setting_named("HALO_NET_COOP_LEVEL") && !setting_named("HALO_NET_COOP_DIFFICULTY") &&
		!setting_named("HALO_NET_LOBBY_PUBLIC"), "no co-op rows (co-op is hosted from Campaign), no Online games row");
	check(setting_named("HALO_NET_LOBBY_NAME")->kind == KIND_TEXT && setting_named("HALO_NET_LOBBY_PASSWORD")->kind == KIND_TEXT &&
		setting_named("HALO_NET_LOBBY_NAME")->page == PAGE_PLAY, "the lobby name and password: texts on the Play page");
	check(!strcmp(setting_named("HALO_DEBUG_CAMERA")->names[0], "Off") &&
		setting_named("HALO_DEBUG_CAMERA")->page == TAB_DEV, "Debug camera stays in Dev");
	{
		/* the values each row saved in 1.0.3 */
		const struct setting *network = setting_named("HALO_VITA_NETWORK"), *gyro = setting_named("HALO_GYRO");
		const struct setting *button = setting_named("HALO_XBOX_WHITE"), *zone = setting_named("HALO_TOUCH_REAR_LEFT");

		check(!strcmp(network->values[0], "wifi") && !strcmp(network->values[1], "adhoc") &&
			!strcmp(network->values[2], "online") && !strcmp(gyro->values[3], "hold") &&
			!strcmp(button->values[9], "right") && !strcmp(zone->values[11], "back") &&
			!strcmp(setting_named("HALO_RENDER_SCALE")->values[5], "dynamic"), "the values saved are 1.0.3's");
	}
}

/* (the rows below unset: a settings.txt from before them then shows what
its profile gives them, as env.txt naming none) */
static void unset_bruno_rows(void)
{
	unsetenv("HALO_VITA_SHADOWS");
	unsetenv("HALO_MAX_SCENE_LIGHTS");
	unsetenv("HALO_VITA_EFFECTS_QUALITY");
	unsetenv("HALO_PARTICLE_RENDER_DIVISOR");
	unsetenv("HALO_AI_THINK_DIVISOR");
	unsetenv("HALO_SOUND_MANAGER_DIVISOR");
	/* (and Particles, added in 1.1.0-beta.4) */
	unsetenv("HALO_PARTICLE_BUDGET");
}

/* a new start for the profile's rows: as shipped, none in the environment
(env.txt naming none of them; vita_settings_load sets the environment, and
in a test it runs again) */
static void profile_rows_fresh(void)
{
	int row;

	for (row = 0; row < PROFILE_ROWS; row++)
	{
		struct setting *setting = setting_named(profile_variables[row]);

		unsetenv(profile_variables[row]);
		setting->choice = shipped_choice[setting - settings];
	}
	unsetenv("HALO_PROFILE");
	settings[0].choice = shipped_choice[0];
}

/* the profile's rows as `profile` (0 Performance, 1 Balanced, 2 Quality)
has them in this build, in the environment; the first that is not, in
`report` */
static int profile_rows_are(int profile, char *report, int size)
{
	int row;

	report[0] = 0;
	for (row = 0; row < PROFILE_ROWS; row++)
	{
		const char *value = getenv(profile_variables[row]);

		if (!value || strcmp(value, profile_values[profile][row]) != 0)
		{
			snprintf(report, (size_t)size, "%s=%s, not %s", profile_variables[row], value ? value : "(unset)",
				profile_values[profile][row]);
			return 0;
		}
	}
	return 1;
}

/* Bruno Santana's settings (Video > Detail and Performance, and Sound's
Sound updates):
his variables and values, so his build's settings.txt carries over; each in
the profiles (1.1.0-beta.4's redone Balanced and Quality: Balanced without
shadows and with 4 lights, as Performance, but Full effects and particles
drawn; Quality shadows near, 8 lights, sound every frame, Adaptive AI),
Balanced the defaults; a row
changed by hand makes the profile Custom; a settings.txt saved with a
profile before the rows were added takes the profile's values */
static void test_bruno_rows(void)
{
	static const struct
	{
		const char *variable, *performance, *balanced, *quality;
	} rows[] = {
		{ "HALO_VITA_SHADOWS", "0", "0", "1" },
		{ "HALO_MAX_SCENE_LIGHTS", "4", "4", "8" },
		{ "HALO_VITA_EFFECTS_QUALITY", "1", "2", "2" },
		{ "HALO_PARTICLE_RENDER_DIVISOR", "2", "1", "1" },
		{ "HALO_AI_THINK_DIVISOR", "0", "0", "0" },
		{ "HALO_SOUND_MANAGER_DIVISOR", "2", "2", "1" },
	};
	int index, row, all = 1;
	int profile = settings[0].choice;

	for (index = 0; index < (int)(sizeof(rows) / sizeof(rows[0])); index++)
	{
		const struct setting *setting = setting_named(rows[index].variable);
		const char *values[PROFILE_CUSTOM] = { rows[index].performance, rows[index].balanced, rows[index].quality };
		int profile_index, in_profile = -1;

		for (row = 0; row < PROFILE_ROWS; row++)
			if (!strcmp(profile_variables[row], rows[index].variable))
				in_profile = row;
		if (!setting || in_profile < 0 || setting->restart)
		{
			printf("not a profile row: %s\n", rows[index].variable);
			all = 0;
			continue;
		}
		for (profile_index = 0; profile_index < PROFILE_CUSTOM; profile_index++)
			if (strcmp(profile_values[profile_index][in_profile], values[profile_index]) != 0 ||
				find_choice(setting, values[profile_index]) < 0 ||
				strcmp(setting->values[find_choice(setting, values[profile_index])], values[profile_index]) != 0)
			{
				printf("profile %d's %s: %s\n", profile_index, rows[index].variable, profile_values[profile_index][in_profile]);
				all = 0;
			}
		/* (Balanced is the rows' own defaults) */
		if (strcmp(setting->values[shipped_choice[setting - settings]], rows[index].balanced) != 0 ||
			strcmp(getenv(rows[index].variable), rows[index].balanced) != 0)
		{
			printf("default of %s: %s\n", rows[index].variable, setting->values[shipped_choice[setting - settings]]);
			all = 0;
		}
	}
	check(all, "Bruno Santana's rows: in every profile (Quality: Near only, 8, Full, Full, every frame; Adaptive AI), "
		"Balanced their defaults and in the environment, live");
	check(!setting_named("HALO_AI_PERCEPTION_LOD"), "1.1's Distant AI row is gone (AI think rate)");
	check(!strcmp(setting_named("HALO_AI_THINK_DIVISOR")->values[0], "1") && !strcmp(setting_named("HALO_AI_THINK_DIVISOR")->values[1], "0") &&
		!strcmp(setting_named("HALO_VITA_SHADOWS")->values[0], "2") && !strcmp(setting_named("HALO_VITA_EFFECTS_QUALITY")->values[2], "0"),
		"his values: AI 1 every tick, 0 Adaptive; shadows 2 Full; effects 0 Minimal");
	/* a row changed by hand: Custom, then back */
	vita_settings_set("HALO_VITA_EFFECTS_QUALITY", "1");
	check(!strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_VITA_EFFECTS_QUALITY=1\n") &&
		!strcmp(settings[0].names[settings[0].choice], "Custom"), "Effects quality Performance: saved, the profile Custom");
	vita_settings_set("HALO_VITA_EFFECTS_QUALITY", "2");
	check(settings[0].choice == profile, "... and back");
	/* Quality: shadows near, 8 lights, the Xbox's effects, particles and
	sound, Adaptive AI; Performance its own; each profile its Dynamic
	resolution's minimum */
	vita_settings_set("HALO_PROFILE", "quality");
	check(!strcmp(getenv("HALO_VITA_SHADOWS"), "1") && !strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "8") &&
		!strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "2") && !strcmp(getenv("HALO_PARTICLE_RENDER_DIVISOR"), "1") &&
		!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0") && !strcmp(getenv("HALO_SOUND_MANAGER_DIVISOR"), "1"),
		"Quality: shadows near, 8 lights, the Xbox's effects, particles and sound; Adaptive AI");
	check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
		!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1") && !strcmp(settings[0].names[settings[0].choice], "Quality"),
		"Quality: Dynamic resolution from 75%, High model detail");
	vita_settings_set("HALO_PROFILE", "performance");
	check(!strcmp(getenv("HALO_VITA_SHADOWS"), "0") && !strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "4") &&
		!strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "1") && !strcmp(getenv("HALO_PARTICLE_RENDER_DIVISOR"), "2") &&
		!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0") && !strcmp(getenv("HALO_SOUND_MANAGER_DIVISOR"), "2"),
		"Performance: no object shadows, 4 lights, Performance effects, half the particles, Adaptive AI, sound every 2nd");
	check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5") &&
		!strcmp(settings[0].names[settings[0].choice], "Performance"), "Performance: Dynamic resolution from 50%");
	vita_settings_set("HALO_PROFILE", "balanced");
	check(!strcmp(settings[0].names[settings[0].choice], "Balanced") && !strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "4") &&
		!strcmp(getenv("HALO_VITA_SHADOWS"), "0") && !strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "2") &&
		!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625"),
		"Balanced again: no shadows, 4 lights, Full effects, Dynamic resolution from 63%");
	/* his build's settings.txt values off our list: the nearest */
	vita_settings_set("HALO_MAX_SCENE_LIGHTS", "12");
	check(!strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "8"), "Dynamic lights 12 (his build's): 8, the nearest");
	vita_settings_set("HALO_PROFILE", "balanced");
}

/* Fourth core helpers (Video > Performance; vita_fourth_core.c): Bruno
Santana's levels and variable (HALO_CPU3_AUX: Off, Audio, All async), Off
by default and in no profile, a level saved and in the environment the next
start reads, a restart's row, the profile left as it was; his build's
settings.txt line read; back Off */
/* Particles (the particle budget, port/linux/game/particle_budget.c): on
Video > Detail, Full, Reduced, Low; Reduced by default and in every profile
(Full cost Quality 16% more CPU in a 15-bot Chill Out fight); live (no restart), saved as a word; a change by
hand makes the profile Custom */
static void test_particles_row(void)
{
	struct setting *row = setting_named("HALO_PARTICLE_BUDGET");
	int profile = settings[0].choice;
	int index, in_profile = -1;
	char line[128];

	for (index = 0; index < PROFILE_ROWS; index++)
		if (!strcmp(profile_variables[index], "HALO_PARTICLE_BUDGET"))
			in_profile = index;
	check(row && row->page == PAGE_GRAPHICS_DETAIL && !row->restart && row->count == 3 &&
		!strcmp(row->names[0], "Full") && !strcmp(row->names[1], "Reduced") && !strcmp(row->names[2], "Low") &&
		!strcmp(row->values[0], "full") && !strcmp(row->values[1], "reduced") && !strcmp(row->values[2], "low") &&
		!strcmp(row->values[shipped_choice[row - settings]], "reduced") && getenv("HALO_PARTICLE_BUDGET") &&
		!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced"),
		"Particles: on Video > Detail, Full, Reduced, Low (full, reduced, low); Reduced by default, in the environment, live");
	check(in_profile >= 0 && !strcmp(profile_values[0][in_profile], "reduced") &&
		!strcmp(profile_values[1][in_profile], "reduced") && !strcmp(profile_values[2][in_profile], "reduced"),
		"Particles in the profiles: Reduced in all three");
	check(to_line("Particles") && strstr(menu, "\n\x05" "Fewer sparks, smoke and debris in big fights\n") && menu_fits(),
		"Particles: its help line fits");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "low") && strstr(file_text(SETTINGS_FILE), "HALO_PARTICLE_BUDGET=low\n") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Particles\x02< Low  ") && !restart_pending &&
		!strcmp(settings[0].names[settings[0].choice], "Custom"),
		"Particles Low: saved, in the environment, no restart, the profile Custom; the last");
	vita_settings_load();
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "low"), "Particles Low: read back from settings.txt");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && settings[0].choice == profile, "Particles Reduced: the profile again");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "full") && strstr(file_text(SETTINGS_FILE), "HALO_PARTICLE_BUDGET=full\n") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Particles\x02  Full >") &&
		!strcmp(settings[0].names[settings[0].choice], "Custom"), "Particles Full: saved, the first, the profile Custom");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && settings[0].choice == profile, "... and Reduced again");
	vita_settings_set("HALO_PROFILE", "performance");
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(settings[0].names[settings[0].choice], "Performance"),
		"Performance: Reduced particles");
	vita_settings_set("HALO_PROFILE", "quality");
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(settings[0].names[settings[0].choice], "Quality"),
		"Quality: Reduced particles");
	vita_settings_set("HALO_PROFILE", "balanced");
	check(!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(settings[0].names[settings[0].choice], "Balanced"),
		"Balanced: Reduced particles");
	/* (back up to the page's first row: bounded, so a row gone cannot
	loop) */
	for (index = 0; index < 20 && menu_selected > 2; index++)
		press(VITA_BUTTON_UP);
}

static void test_fourth_core(void)
{
	struct setting *row = setting_named("HALO_CPU3_AUX");
	int profile = settings[0].choice;
	char line[128];
	int index;

	check(row && row->page == PAGE_GRAPHICS_PERFORMANCE && row->restart && row->count == 3 &&
		!strcmp(row->values[row->choice], "2") && !strcmp(row->names[0], "Off") && !strcmp(row->names[1], "Audio") &&
		!strcmp(row->names[2], "All async") && !strcmp(row->values[1], "1") && !strcmp(row->values[2], "2") &&
		getenv("HALO_CPU3_AUX") && !strcmp(getenv("HALO_CPU3_AUX"), "2"),
		"Fourth core helpers: on Video > Performance, Off, Audio, All async; All async by default (HALO_CPU3_AUX=2: without CapUnlocker the helpers stay where they are), after a restart");
	check(strstr(row->help, "Needs CapUnlocker") && strstr(row->help, "background work"), "Fourth core helpers: the help names the plugin (the README's words)");
	{
		int in_profile = 0;

		for (index = 0; index < PROFILE_ROWS; index++)
			in_profile |= !strcmp(profile_variables[index], "HALO_CPU3_AUX");
		check(!in_profile, "no profile sets Fourth core helpers");
	}
	check(to_line("Fourth core helpers"), "Fourth core helpers: its row on Performance");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_CPU3_AUX"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_CPU3_AUX=1\n") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Fourth core helpers*\x02< Audio >") &&
		settings[0].choice == profile && restart_pending && menu_fits(),
		"Fourth core helpers Audio: saved, in the environment, the profile unchanged, a restart asked for; its lines fit");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_CPU3_AUX"), "2") && strstr(file_text(SETTINGS_FILE), "HALO_CPU3_AUX=2\n") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Fourth core helpers*\x02< All async  "),
		"Fourth core helpers All async: the last");
	vita_settings_load();
	check(!strcmp(setting_named("HALO_CPU3_AUX")->values[setting_named("HALO_CPU3_AUX")->choice], "2") &&
		!strcmp(getenv("HALO_CPU3_AUX"), "2"), "Fourth core helpers All async: read back from settings.txt (as Bruno Santana's build writes it)");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_CPU3_AUX"), "0") && strstr(file_text(SETTINGS_FILE), "HALO_CPU3_AUX=0\n") &&
		settings[0].choice == profile, "Fourth core helpers Off");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_CPU3_AUX"), "2"), "Fourth core helpers back to All async");
	for (index = 0; index < 20 && menu_selected > 2; index++)
		press(VITA_BUTTON_UP);
	/* (as after the restart it asked for: the rows after this one say
	what they do, not that a restart waits) */
	restart_pending = 0;
}

/* ---------- Button icons: the game's button icons, and the panel's terms */

/* the game's menus' typing (OpenCE's screens, menu_functions.c): the
system's keyboard opened on the game's request, the game getting no
buttons while it is up, the text handed back, or cancelled */
static void test_game_text_input(void)
{
	int opens = ime_opens;

	check(frame(0) == 0 && !menu_visible, "typing: the panel closed, the game has the pad");
	snprintf(halo_text_input_title, sizeof(halo_text_input_title), "Lobby name");
	snprintf(halo_text_input_text, sizeof(halo_text_input_text), "Halo");
	halo_text_input_maximum = 15;
	halo_text_input_password = 0;
	halo_text_input_state = HALO_TEXT_INPUT_REQUESTED;
	check(frame(0) == 1 && halo_text_input_state == HALO_TEXT_INPUT_OPEN && ime_opens == opens + 1 &&
		!strcmp(ime_title, "Lobby name") && !strcmp(ime_initial, "Halo") && ime_maximum == 15 && !ime_password,
		"typing: the game's request opens the keyboard on its text, title and length");
	check(frame(VITA_BUTTON_CROSS) == 1, "typing: while it is up the game gets no buttons");
	snprintf(ime_typed, sizeof(ime_typed), "My game");
	ime_result = 1;
	check(frame(VITA_BUTTON_CROSS) == 1 && halo_text_input_state == HALO_TEXT_INPUT_DONE &&
		!strcmp(halo_text_input_text, "My game"), "typing: the text typed goes back to the game");
	check(frame(VITA_BUTTON_CROSS) == 1 && frame(0) == 0, "typing: the cross that closed it is not the game's");
	halo_text_input_state = HALO_TEXT_INPUT_REQUESTED;
	halo_text_input_password = 1;
	frame(0);
	ime_result = -1;
	check(frame(0) == 1 && halo_text_input_state == HALO_TEXT_INPUT_CANCELLED && ime_password,
		"typing: a password's keyboard, cancelled");
	halo_text_input_state = HALO_TEXT_INPUT_IDLE;
	check(frame(0) == 0, "typing: then the game has the pad again");
	/* the Halo 3 main menu's Start asks for the panel ("port settings panel") */
	halo_settings_panel_request = 1;
	check(frame(VITA_BUTTON_START) == 1 && panel_open && menu_visible && !halo_settings_panel_request,
		"settings request: the game's menus open the panel, the request taken");
	check(frame(VITA_BUTTON_START) == 1 && panel_open, "settings request: the Start still held does not close it");
	frame(0);
	check(frame(VITA_BUTTON_CIRCLE) == 1 && !panel_open, "settings request: circle closes it as ever");
	frame(0);
	check(frame(0) == 0, "settings request: then the game has the pad again");
}

static void test_button_icons(void)
{
	/* (the row as shipped: VITA_BUTTON_ICONS_DEFAULT's choice) */
	const char *shipped_row = VITA_BUTTON_ICONS_DEFAULT ? "Button icons\x02< PlayStation  " : "Button icons\x02  Xbox >";
	struct vita_controls_config config;
	char diagram[16], line[128];
	unsigned long generation;
	int longest;

	open_panel();
	to_tab("Game");
	check(to_line("Button icons") && !strcmp(menu_line(menu_selected, line, sizeof(line)), shipped_row) &&
		strstr(menu, "\n\x05Halo's prompts: Xbox icons, or the Vita's\n"),
		"Button icons: its row on Game, under Interface (as shipped: VITA_BUTTON_ICONS_DEFAULT), its help");
	/* (from Xbox on, whichever is the default) */
	if (VITA_BUTTON_ICONS_DEFAULT)
		press(VITA_BUTTON_LEFT);
	generation = halo_settings_generation;
	press(VITA_BUTTON_RIGHT);
	printf("%s\n--\n", menu);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "playstation") && vita_button_icons_playstation() &&
		halo_settings_generation != generation && strstr(file_text(SETTINGS_FILE), "HALO_BUTTON_ICONS=playstation\n") &&
		strstr(menu, "\nButton icons\x02< PlayStation  "),
		"Button icons PlayStation: in the environment and settings.txt at once, read again (no restart)");
	check(!strstr(menu, "Button icons*"), "Button icons: live, no *");
	/* (the panel keeps the Xbox controller's names with either: the icons
	change in the game only) */
	to_tab("Controls");
	check(open_page("Button layout") && strstr(menu, "\nA\x02  Cross >") && strstr(menu, "\nBlack\x02") &&
		!strstr(menu, "\nJump\x02") && menu_rows() == 11 && menu_fits(),
		"PlayStation: the Button layout's rows keep the Xbox's names, each fits");
	/* a remap: the game's icon follows (vita_controls.c) */
	to_line("X");
	press(VITA_BUTTON_RIGHT);
	vita_controls_config_load(&config);
	check(!strcmp(getenv("HALO_XBOX_X"), "triangle") && strstr(menu, "\nX\x02< Triangle >") &&
		vita_button_glyph(&config, 2, 0) == VITA_GLYPH_TRIANGLE && vita_button_glyph(&config, 2, 1) == VITA_GLYPH_SQUARE,
		"X on Triangle: the game's X icon shows Triangle in play (Square in the menus)");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nButton layout\x02  As shipped  >") != NULL,
		"back on Controls: Button icons is not the layout (As shipped)");
	check(strstr(menu, "\nCrouch\x02") != NULL, "Controls: Crouch keeps its label");

	/* the touch zones: the Xbox's names too */
	check(open_page("Touch zones") && to_line("Rear touch left"), "Touch zones opens");
	press(VITA_BUTTON_RIGHT);
	menu_lines(&longest, diagram, sizeof(diagram));
	check(!strcmp(getenv("HALO_TOUCH_REAR_LEFT"), "a") && strstr(menu, "\nRear touch left\x02< A >") && menu_fits(),
		"PlayStation: a zone's choice keeps the Xbox's name");
	vita_controls_config_load(&config);
	check(vita_button_glyph(&config, 0, 0) == VITA_GLYPH_CROSS, "A on Cross and a zone: Cross shows");
	press(VITA_BUTTON_LEFT);
	to_tab("Controls");

	/* the guide's steps: the menus' Xbox buttons, and which Vita button each is */
	to_tab("Multiplayer");
	to_line("Join with a code");
	press(VITA_BUTTON_CROSS);
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "A on the host's game") && strstr(menu, "Menus: A Cross") && menu_fits(),
		"PlayStation: Join's steps keep the Xbox's buttons");
	press(VITA_BUTTON_CIRCLE);

	/* Reset controls keeps it; Xbox again puts the Xbox's terms back */
	to_tab("Controls");
	check(to_line("Reset controls"), "Controls: Reset controls");
	press(VITA_BUTTON_CROSS);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "playstation"), "Reset controls keeps the Button icons");
	to_tab("Game");
	check(to_line("Button icons"), "Game: Button icons again");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_BUTTON_ICONS"), "xbox") && !vita_button_icons_playstation() &&
		strstr(menu, "\nButton icons\x02  Xbox >") && strstr(file_text(SETTINGS_FILE), "HALO_BUTTON_ICONS=xbox\n"),
		"Button icons Xbox again: saved");
	to_tab("Controls");
	check(open_page("Button layout") && strstr(menu, "\nA\x02  Cross >") && strstr(menu, "\nBlack\x02") &&
		!strstr(menu, "\nJump\x02"), "Xbox again: the Xbox's names");
	press(VITA_BUTTON_CIRCLE);
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the languages (lang.c, port/vita/app0/lang)

The Language row: Automatic (the Vita's system language), English and each
language file (Español); a choice live, saved, kept at a restart. In
Spanish, every page, every row's help and every value of every row fits as
it does in English (UTF-8 counted as the overlay draws it, the tab bar
too), the guide's, the code's, the public games' and the delete screens
and the messages over the game too; and every string the panel asked for
had a Spanish entry (lang_missing), its table's (labels, values, help) and
the pages' names included. */

/* the tab bar fits between L and R at the narrower spacing the renderer
falls back to (vita_gxm.c menu_tabs: a name and a character, 8 pixels
between two, in the 880 - 104 pixels from the first tab to the R) */
static int tab_bar_fits(void)
{
	const char *at = menu + 1;
	int width = -8;

	while (*at && *at != '\n')
	{
		int bytes = (int)strcspn(at, "|\n");

		width += (characters(at, bytes) - (*at == '*') + 1) * 16 + 8;
		at += bytes + (at[bytes] == '|');
	}
	if (width > 776)
		printf("tab bar too wide: %d pixels: %.*s\n", width, (int)strcspn(menu, "\n"), menu);
	return width <= 776;
}

/* every line of the menu shown is 46 characters at most (a screen of its
own: the guide, a message) */
static int menu_lines_fit(void)
{
	const char *at = menu;

	while (*at)
	{
		int bytes = (int)strcspn(at, "\n");

		if (characters(at, bytes) - (at[0] == '!') > 46)
		{
			printf("too long: %.*s\n", bytes, at);
			return 0;
		}
		at += bytes + (at[bytes] == '\n');
	}
	return 1;
}

/* each page, each line of it chosen, each value of each row: fits; a
heading chosen is not (the panel chooses a row instead) */
static int every_page_fits(int *shown)
{
	int fits = 1, page_index;

	for (page_index = 0; page_index < PAGE_COUNT; page_index++)
	{
		struct line lines[MAXIMUM_LINES];
		int count, line;

		tab = pages[page_index].tab;
		page = page_index;
		screen = SCREEN_LIST;
		count = page_lines(lines);
		for (line = 0; line < count; line++)
		{
			page_selected[page] = line;
			show();
			(*shown)++;
			if (!menu_fits() || !tab_bar_fits() || !menu_height_fits())
				fits = 0;
			if (lines[line].type == LINE_HEADING)
			{
				char chosen[128];

				menu_line(menu_selected, chosen, sizeof(chosen));
				if (page_selected[page] == line || chosen[0] == '\x08' || !strchr(chosen, '\x02'))
				{
					printf("a heading chosen: %s, page %d\n", lines[line].text, page);
					fits = 0;
				}
			}
			if (lines[line].type == LINE_SETTING && settings[lines[line].index].kind == KIND_CHOICE)
			{
				struct setting *setting = &settings[lines[line].index];
				int kept = setting->choice, choice;

				for (choice = 0; choice < setting->count; choice++)
				{
					setting->choice = choice;
					show();
					(*shown)++;
					if (!menu_fits())
					{
						printf("(%s, choice %d)\n", setting->label, choice);
						fits = 0;
					}
				}
				setting->choice = kept;
			}
		}
	}
	tab = page = TAB_GAME;
	return fits;
}

static void test_languages(void)
{
	struct setting *language = setting_named("HALO_LANGUAGE");
	char report[2048], line[256];
	int index, shown = 0, missing;

	check(lang_count() >= 2 && !strcmp(lang_code(1), "es") && !strcmp(lang_name(1), "Español"),
		"the language files: English, then Español (app0:lang/es.txt)");
	check(lang_problems(report, sizeof(report)) == 0, "es.txt: every line taken (formats as the English's)");
	if (report[0])
		printf("%s", report);
	check(language->count == lang_count() + 1 && !strcmp(language->values[0], "auto") &&
		!strcmp(language->names[0], "Automatic") && !strcmp(language->values[2], "es") &&
		!strcmp(language->names[2], "Español"), "the Language row: Automatic, English, Español");
	check(!strcmp(lang_current(), "en") && !strcmp(getenv("HALO_SYSTEM_LANGUAGE"), "en"),
		"Automatic on an English Vita: English");

	/* (live: the panel at once in Spanish, saved) */
	open_panel();
	vita_settings_set("HALO_LANGUAGE", "es");
	check(!strcmp(lang_current(), "es") && !strcmp(getenv("HALO_LANGUAGE"), "es") &&
		strstr(file_text(SETTINGS_FILE), "HALO_LANGUAGE=es\n"), "Español: chosen at once, in settings.txt");
	tab = page = TAB_GRAPHICS;
	show();
	printf("%s\n--\n", menu);
	check(!strncmp(menu, "\tJuego|Controles|*Vídeo|Sonido|Multijugador", sizeof("\tJuego|Controles|*Vídeo|Sonido|Multijugador") - 1) &&
		strstr(menu, "\n\x08" "Calidad\nPerfil\x02") && menu_fits() && tab_bar_fits(),
		"the panel in Spanish at once: Juego, Controles, Vídeo, Sonido, Multijugador; Calidad over Perfil ...");
	{
		/* the headings in Spanish, each tab's in order (es.txt) */
		static const struct
		{
			int tab;
			const char *headings;
		} spanish[] = {
			{ TAB_GAME, "Interfaz|Primera persona|Actualizaciones" },
			{ TAB_CONTROLS, "Mirar|Botones|Giroscopio|Táctil" },
			{ TAB_GRAPHICS, "Calidad|Pantalla|Más" },
			{ TAB_AUDIO, "" },
			{ TAB_MULTIPLAYER, "Red|Bots|Partidas" },
		};
		char headings_shown[128];
		int all = 1;

		for (index = 0; index < (int)(sizeof(spanish) / sizeof(spanish[0])); index++)
		{
			tab = page = spanish[index].tab;
			page_selected[page] = 0;
			show();
			if (strcmp(outline_of(headings_shown, sizeof(headings_shown), 1), spanish[index].headings) != 0 ||
				!menu_fits() || !tab_bar_fits())
			{
				printf("%s\n--\n", menu);
				all = 0;
			}
		}
		check(all, "Spanish headings: Interfaz, Primera persona, Actualizaciones; Mirar, Botones, Giroscopio, Táctil; "
			"Calidad, Pantalla, Más; Red, Bots, Partidas (none on Sonido); each fits");
	}
	tab = page = TAB_GAME;
	show();
	check(!strncmp(menu, "\t*Juego|", 8) && strstr(menu, "\nIdioma\x02< Español ") != NULL,
		"Juego: Idioma, Español (its own name)");
	lang_missing(NULL, 0);

	/* every page, line and value; with Show dev settings, the Dev tab's
	too; on the classic panel (the Halo CE menus), whose height
	menu_height_fits checks */
	vita_settings_set("HALO_MENU_STYLE", "classic");
	vita_settings_set("HALO_DEV_SETTINGS", "1");
	check(every_page_fits(&shown), "Spanish: every page, each line chosen, each row's every value fits");
	printf("(%d pages shown)\n", shown);
	vita_settings_set("HALO_DEV_SETTINGS", "0");
	/* (Game with each answer of the update check: the version and the
	answer still fit, beside each other or not, and so do the tab's rows) */
	{
		static const int statuses[] = { UPDATE_STATUS_NONE, UPDATE_STATUS_CHECKING, UPDATE_STATUS_UP_TO_DATE,
			UPDATE_STATUS_AVAILABLE, UPDATE_STATUS_NO_CONNECTION, UPDATE_STATUS_FAILED };
		int fits = 1;

		tab = page = TAB_GAME;
		page_selected[page] = 0;
		snprintf(update_version_value, sizeof(update_version_value), "1.1.0-beta.10");
		for (index = 0; index < (int)(sizeof(statuses) / sizeof(statuses[0])); index++)
		{
			update_status_value = statuses[index];
			show();
			/* (on the classic panel: Interfaz giving way when the lines run
			to three) */
			if (!menu_fits() || !menu_height_fits() || menu_rows() != 10 ||
				!strstr(menu, "\n\x08" "Actualizaciones\nBuscar actualizaciones\x02  >\n\x04Versión "))
			{
				printf("(update status %d)\n%s\n--\n", statuses[index], menu);
				fits = 0;
			}
			if (statuses[index] == UPDATE_STATUS_AVAILABLE || statuses[index] == UPDATE_STATUS_UP_TO_DATE)
				printf("%s\n--\n", menu);
		}
		check(fits && update_status_value == UPDATE_STATUS_FAILED &&
			strstr(menu, "No se pudo comprobar ahora\nCanal de actualización\x02"),
			"Spanish: Juego with each answer of the update check fits, Buscar actualizaciones under Actualizaciones, "
			"Canal de actualización under its lines");
		update_status_value = UPDATE_STATUS_UP_TO_DATE;
		show();
		check(strstr(menu, "\n\x04Versión " HALO_VITA_VERSION " - Actualizado\n") != NULL,
			"Spanish: up to date beside the version");
		update_status_value = UPDATE_STATUS_NONE;
		update_version_value[0] = 0;
	}
	vita_settings_set("HALO_MENU_STYLE", "halo3");
	/* (the screens actions open, and the messages over the game) */
	tab = page = TAB_MULTIPLAYER;
	screen = SCREEN_CODE;
	code_for_join = 1;
	show();
	check(menu_lines_fit() && strstr(menu, "UNIRSE CON UN CÓDIGO"), "Spanish: the code screen");
	screen = SCREEN_BROWSE;
	browse_count = 0;
	show();
	check(menu_lines_fit(), "Spanish: the public games, none");
	/* (with Crossplay on: the PC games' badges, a co-op one chosen, a large
	one's question) */
	test_crossplay_wire = 1;
	browsing = 1;
	browse_refresh();
	for (index = 0; index < browse_count; index++)
	{
		browse_selected = index;
		show();
		if (!menu_lines_fit())
			printf("%s\n--\n", menu);
		check(menu_lines_fit(), "Spanish: the PC games with their badges fit");
	}
	browse_large = browse_entries[0];
	screen = SCREEN_LARGE;
	show();
	printf("%s\n--\n", menu);
	check(menu_lines_fit() && strstr(menu, "Cruz"), "Spanish: the large PC game's question fits");
	screen = SCREEN_BROWSE;
	test_crossplay_wire = 0;
	browsing = 0;
	browse_count = browse_selected = 0;
	screen = SCREEN_GUIDE;
	for (index = 0; index < 3; index++)
	{
		static const char *const networks[] = { "wifi", "adhoc", "online" };

		snprintf(running_network, sizeof(running_network), "%s", networks[index]);
		guide_action = ACTION_HOST;
		show();
		if (!menu_lines_fit())
			printf("(host, %s)\n", networks[index]);
		check(menu_lines_fit(), "Spanish: Host a game's steps fit");
		guide_action = ACTION_JOIN;
		show();
		if (!menu_lines_fit())
			printf("(join, %s)\n", networks[index]);
		check(menu_lines_fit(), "Spanish: Join a game's steps fit");
	}
	printf("%s\n--\n", menu);
	snprintf(running_network, sizeof(running_network), "online");
	screen = SCREEN_LIST;
	close_panel();
	vita_settings_message(T("Host a game"), T("The Vita did not join the ad hoc group. Try again, or another Ad hoc dialog way (Dev tab)."));
	frame(0);
	check(menu_visible && menu_lines_fit() && strstr(menu, "Crear una partida"), "Spanish: a message, in lines of 46 characters");
	printf("%s\n--\n", menu);
	frame(VITA_BUTTON_CROSS);
	frame(0);
	vita_settings_question(T("Halo CE: PC map files"), "¿Borrar halocesetup_es.exe (151 MB) para liberar espacio?");
	frame(0);
	check(menu_visible && menu_lines_fit() && strstr(menu, "Cruz: sí"), "Spanish: a question, Cross yes, Circle no");
	frame(VITA_BUTTON_CIRCLE);
	frame(0);

	/* every string the panel has: its rows' labels, values and help, the
	pages' names and the headings, each with a Spanish entry */
	for (index = 0; index < SETTING_COUNT; index++)
	{
		const struct setting *setting = &settings[index];
		int choice;

		T(setting->label);
		if (setting->help)
			T(setting->help);
		for (choice = 0; choice < setting->count && setting->kind == KIND_CHOICE; choice++)
			if (setting != language || choice == 0)
				T(setting->names[choice]);
	}
	for (index = 0; index < PAGE_COUNT; index++)
		T(pages[index].name);
	for (index = 0; index < (int)(sizeof(headings) / sizeof(headings[0])); index++)
		T(headings[index].name);
	for (index = 0; index < 3; index++)
		T(map_downloads_help[index]);
	missing = lang_missing(report, sizeof(report));
	check(missing == 0, "every string the panel showed or has: a Spanish entry");
	if (missing)
		printf("no Spanish entry for:\n%s", report);

	/* (a restart: Spanish still) */
	vita_settings_load();
	check(!strcmp(lang_current(), "es"), "a restart: Spanish still (settings.txt)");
	/* Automatic: the Vita's language, Spanish or one without a file */
	vita_settings_set("HALO_LANGUAGE", "auto");
	snprintf(system_language, sizeof(system_language), "es");
	language_apply();
	check(!strcmp(lang_current(), "es"), "Automatic on a Spanish Vita: Spanish");
	snprintf(system_language, sizeof(system_language), "pt-BR");
	language_apply();
	check(!strcmp(lang_current(), "en"), "Automatic on a Vita in a language with no file: English");
	snprintf(system_language, sizeof(system_language), "en");
	language_apply();
	vita_settings_set("HALO_LANGUAGE", "en");
	tab = page = TAB_GAME;
	open_panel();
	check(!strcmp(lang_current(), "en") && strstr(menu, "\t*Game|Controls|Video|Sound|Multiplayer") &&
		strstr(menu, "\n\x08Interface\nMenu style\x02"), "English again, at once");
	menu_line(0, line, sizeof(line));
	close_panel();
	vita_settings_set("HALO_LANGUAGE", "auto");
}

/* ---------- the update check: Game's Updates rows, Check for updates and
its lines, then its channel */

/* the page redrawn (a frame after half a second) */
static void redraw(void)
{
	clock_us += 600000;
	frame(0);
}

static void test_updates(void)
{
	char line[160];
	int prerelease = update_build_is_prerelease(HALO_VITA_VERSION);

	/* (the Halo CE menus: the classic panel, the one whose height
	menu_height_fits checks; the Halo 3 menus' again at the end) */
	vita_settings_set("HALO_MENU_STYLE", "classic");
	open_panel();
	to_tab("Game");
	check(to_line("Update channel") && !strstr(menu, "\n\x03"), "the Update channel row, on Game");
	printf("%s\n--\n", menu);
	if (prerelease)
	{
		char help[96], release[16];

		update_version_release(HALO_VITA_VERSION, release, sizeof(release));
		snprintf(help, sizeof(help), "\n\x05Stable updates arrive with %s\n", release);
		check(strstr(menu, "\nUpdate channel\x02  Experimental  \x07Stable\n") && menu_fits(),
			"a pre-release build: Experimental, Stable greyed out, no arrows");
		check(strstr(menu, help) != NULL, "its help: Stable updates arrive with the release");
		press(VITA_BUTTON_RIGHT);
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental") &&
			strstr(menu, "\nUpdate channel\x02  Experimental  \x07Stable\n"), "left and right cannot choose Stable");
		vita_settings_set("HALO_UPDATE_CHANNEL", "stable");
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental"), "nor can the game (vita_settings_set)");
		/* (a settings.txt that says stable: Experimental, said once) */
		write_file(SETTINGS_FILE, "HALO_UPDATE_CHANNEL=stable\n", 27);
		log_text[0] = 0;
		vita_settings_load();
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental") &&
			strstr(log_text, "settings: HALO_UPDATE_CHANNEL=stable is not offered by this build") &&
			!strstr(strstr(log_text, "is not offered") + 1, "is not offered"),
			"settings.txt's stable is ignored on a pre-release build, logged once");
		setenv("HALO_UPDATE_CHANNEL", "stable", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental"), "and env.txt's");
		unsetenv("HALO_UPDATE_CHANNEL");
	}
	else
	{
		check(strstr(menu, "\nUpdate channel\x02< Stable  \n") && menu_fits() && !strchr(menu, '\x07'),
			"a release build: Stable by default, Experimental offered");
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental") &&
			strstr(menu, "\nUpdate channel\x02  Experimental >\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_UPDATE_CHANNEL=experimental\n"), "Experimental chosen, saved");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "stable"), "back to Stable");
		check(strstr(file_text(SETTINGS_FILE), "HALO_SETTINGS_BUILD=" HALO_VITA_VERSION "\n") != NULL,
			"settings.txt names the build that wrote it");
		/* (a beta's settings.txt: its Experimental was no choice, so Stable;
		one this release wrote keeps Experimental) */
		write_file(SETTINGS_FILE, "HALO_UPDATE_CHANNEL=experimental\n", sizeof("HALO_UPDATE_CHANNEL=experimental\n") - 1);
		log_text[0] = 0;
		vita_settings_load();
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "stable") &&
			strstr(log_text, "settings: the Update channel a pre-release build saved (Experimental) starts on Stable"),
			"a beta's settings.txt (no build line) with Experimental: Stable, logged");
		write_file(SETTINGS_FILE, "HALO_SETTINGS_BUILD=1.1.0-beta.5\nHALO_UPDATE_CHANNEL=experimental\n",
			sizeof("HALO_SETTINGS_BUILD=1.1.0-beta.5\nHALO_UPDATE_CHANNEL=experimental\n") - 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "stable"), "a beta-written settings.txt with Experimental: Stable");
		write_file(SETTINGS_FILE, "HALO_SETTINGS_BUILD=" HALO_VITA_VERSION "\nHALO_UPDATE_CHANNEL=experimental\n",
			sizeof("HALO_SETTINGS_BUILD=" HALO_VITA_VERSION "\nHALO_UPDATE_CHANNEL=experimental\n") - 1);
		log_text[0] = 0;
		vita_settings_load();
		check(!strcmp(getenv("HALO_UPDATE_CHANNEL"), "experimental") && !strstr(log_text, "starts on Stable"),
			"a release-written settings.txt keeps Experimental");
		vita_settings_set("HALO_UPDATE_CHANNEL", "stable");
	}
	/* Check for updates: the first row under Updates, in the background,
	its answer beside the version under it; the Update channel and Show dev
	settings under its lines */
	check(strstr(menu, "\t*Game|") && !strstr(menu, "\n\x03") && to_line("Check for updates") && menu_selected == 10 &&
		!strcmp(menu_line(9, line, sizeof(line)), "\x08Updates") && !update_requests,
		"Game: Check for updates under the Updates heading, nothing asked before the press");
	check(strstr(menu, "\n\x05" "Asks GitHub for the newest version; again to download it\n") &&
		strstr(menu, "\n\x06L/R: tabs   Cross: select   Circle: close"), "Check for updates: its help, cross presses it");
	check(!strcmp(menu_line(11, line, sizeof(line)), "\x04" "Version " HALO_VITA_VERSION) &&
		!strncmp(menu_line(12, line, sizeof(line)), "Update channel\x02", 15) &&
		!strncmp(menu_line(13, line, sizeof(line)), "Show dev settings\x02", 18),
		"no look yet: the version alone under it, then Update channel and Show dev settings (no gap)");
	press(VITA_BUTTON_UP);
	check(menu_selected == 8 && !strncmp(menu_line(menu_selected, line, sizeof(line)), "Smooth weapon motion\x02", 21),
		"up from Check for updates: Smooth weapon motion (the Updates heading stepped over)");
	press(VITA_BUTTON_DOWN);
	check(menu_selected == 10, "... and down: Check for updates again");
	press(VITA_BUTTON_CROSS);
	redraw();
	check(update_requests == 1 && !strcmp(menu_line(11, line, sizeof(line)), "\x04" "Version " HALO_VITA_VERSION " - Checking...") &&
		menu_fits() && menu_height_fits(), "pressed: Checking..., beside the version");
	update_status_value = UPDATE_STATUS_AVAILABLE;
	snprintf(update_version_value, sizeof(update_version_value), "1.1.0-beta.4");
	redraw();
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n\x08Updates\nCheck for updates\x02  >\n\x04" "Version " HALO_VITA_VERSION "\n"
		"!Update available: 1.1.0-beta.4\n!github.com/BirchWoodGod/halo-ce-vita/releases\nUpdate channel\x02") &&
		menu_fits() && menu_height_fits() && menu_rows() == 10,
		"an update: under the version, in amber, the new version and where to get it; all of the tab still fits (ten "
		"rows on the classic panel: Forge and File Share's too)");
	check(!strstr(menu, "Interface") && !strncmp(menu_line(1, line, sizeof(line)), "Menu style\x02", 11) &&
		!strstr(menu, "\n\x08" "First person\n") && strstr(menu, "\n\x08Updates\n") && menu_selected == 8,
		"the classic panel, short of room: the first headings, Interface and First person, give way (the tab bar over "
		"Menu style); Updates stays, Check for updates still chosen");
	vita_settings_set("HALO_MENU_STYLE", "halo3");
	redraw();
	check(strstr(menu, "\n\x08Interface\nMenu style\x02") && strstr(menu, "\n\x08Updates\nCheck for updates\x02") &&
		menu_selected == 10, "the Halo 3 panel (its rows fitted to its height): every heading kept");
	vita_settings_set("HALO_MENU_STYLE", "classic");
	redraw();
	/* (a build with the update key: the next press downloads it) */
	test_update_key = 1;
	redraw();
	check(strstr(menu, "\n!Update available: 1.1.0-beta.4\n!Press again to download it\nUpdate channel\x02") &&
		menu_fits() && menu_height_fits(), "with the update key: press again to download it");
	to_line("Check for updates");
	press(VITA_BUTTON_CROSS);
	check(update_starts == 1 && update_requests == 1, "the press with an update found downloads it, asks GitHub nothing");
	/* (downloaded and verified: where it is, for VitaShell) */
	snprintf(update_ready_version, sizeof(update_ready_version), "1.1.0-beta.4");
	redraw();
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n!VitaShell: ux0:data/haloce-vita/update/\n!halo-1.1.0-beta.4.vpk (verified, to install)\n"
		"Update channel\x02") && menu_fits() && menu_height_fits() && menu_rows() == 10,
		"downloaded: where the VPK is, for VitaShell; all of the tab still fits");
	update_status_value = UPDATE_STATUS_NONE;
	redraw();
	check(strstr(menu, "\n!halo-1.1.0-beta.4.vpk (verified, to install)\n") != NULL, "a downloaded VPK shows without a look this session");
	update_status_value = UPDATE_STATUS_AVAILABLE;
	update_ready_version[0] = 0;
	test_update_key = 0;
	redraw();
	to_line("Update channel");
	press(VITA_BUTTON_UP);
	check(!strcmp(menu_line(menu_selected, line, sizeof(line)), "Check for updates\x02  >"),
		"up from Update channel: Check for updates (the info lines are stepped over)");
	update_status_value = UPDATE_STATUS_UP_TO_DATE;
	snprintf(update_version_value, sizeof(update_version_value), "%s", HALO_VITA_VERSION);
	redraw();
	check(!strcmp(menu_line(11, line, sizeof(line)), "\x04" "Version " HALO_VITA_VERSION " - Up to date") &&
		!strstr(menu, "github.com") && !strncmp(menu_line(12, line, sizeof(line)), "Update channel\x02", 15) && menu_fits(),
		"up to date");
	update_status_value = UPDATE_STATUS_NO_CONNECTION;
	redraw();
	/* (beside the version while the line is 46 characters at most: a
	release's short version; else on a line of its own) */
	check(strstr(menu, sizeof("Version " HALO_VITA_VERSION " - Couldn't check: no connection") - 1 <= 46 ?
		"\n\x04" "Version " HALO_VITA_VERSION " - Couldn't check: no connection\nUpdate channel\x02" :
		"\n\x04" "Version " HALO_VITA_VERSION "\n\x04" "Couldn't check: no connection\nUpdate channel\x02") && menu_fits(),
		"no connection: beside the version, or on a line of its own when that is too long");
	update_status_value = UPDATE_STATUS_FAILED;
	redraw();
	check(strstr(menu, "Couldn't check right now\nUpdate channel\x02") && menu_fits() && menu_height_fits(), "could not check");
	update_status_value = UPDATE_STATUS_NONE;
	redraw();
	check(!strstr(menu, "Couldn't") && !strstr(menu, "Checking"), "no look yet: no line");
	check(strstr(menu, "\n\x08Interface\nMenu style\x02") && menu_height_fits(),
		"room again (the version alone): Interface back");
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible, "circle on Game closes the panel");
	vita_settings_set("HALO_MENU_STYLE", "halo3");

	/* the update download's dialogs as vita_update.c drives them, from its
	thread: the question, the progress line, then the line withdrawn and the
	last message at once - closing the message leaves nothing over the game
	(the progress line came back over it, "frozen", on hardware) */
	vita_settings_question("Halo CE: update", "Download it now?");
	frame(0);
	check(menu_visible && strstr(menu, "Download it now?") != NULL, "the download's question");
	press(VITA_BUTTON_CROSS);
	check(vita_settings_question_answer() == 1, "Cross: yes");
	vita_settings_progress("Halo CE: update", "Downloading: 2.4 of 4.8 MB (50%)");
	frame(0);
	check(menu_visible && strstr(menu, "Downloading: 2.4 of 4.8 MB") != NULL, "its progress line over the game");
	vita_settings_progress("Halo CE: update", "Checking the download: 100%");
	frame(0);
	check(strstr(menu, "Checking the download: 100%") != NULL, "the progress line follows");
	vita_settings_progress(NULL, NULL);
	vita_settings_message("Halo CE: update", "1.1.0-beta.4 downloaded and verified. Install it with VitaShell: "
		"ux0:data/haloce-vita/update/halo-1.1.0-beta.4.vpk");
	frame(0);
	check(menu_visible && strstr(menu, "downloaded and verified") != NULL && !strstr(menu, "Checking the download"),
		"the last message replaces the progress line");
	press(VITA_BUTTON_CROSS);
	frame(0);
	redraw();
	check(!menu_visible && !strstr(menu, "Checking the download"),
		"closing it leaves nothing over the game (no progress line again)");
	open_panel();
	check(menu_visible && strstr(menu, "\t*Game|") != NULL, "the panel opens again as before (on Game)");
	press(VITA_BUTTON_CIRCLE);
}

/* ---------- the restart offer: a change that applies after a restart,
the panel closed at the main menu (close_panel) */

/* the Menu style row (Game, under Interface): the menus as Halo CE's or
Halo 3's (port/linux/game/menu_tags.c reads HALO_MENU_STYLE each time a
screen opens): Halo 3 as shipped from 1.1.1 (the owner's), live, no
restart; a settings.txt that chose Halo CE keeps it */
static void test_menu_style_row(void)
{
	struct setting *style = setting_named("HALO_MENU_STYLE");
	unsigned long generation;
	char line[128];

	check(style && style->page == TAB_GAME && !strcmp(style->values[shipped_choice[style - settings]], "halo3") &&
		!strcmp(style->names[shipped_choice[style - settings]], "Halo 3"), "Menu style: Halo 3 as shipped");
	open_panel();
	to_tab("Game");
	check(to_line("Menu style") && menu_selected == 2 && !strcmp(menu_line(1, line, sizeof(line)), "\x08Interface") &&
		strstr(menu, "\n\x05" "Halo 3: the new menus over the Halo ring. Halo CE: the original menus.\n") && menu_fits(),
		"Menu style: Game's first row, under Interface, and its help");
	check(!strcmp(getenv("HALO_MENU_STYLE"), "halo3") && strstr(menu, "\nMenu style\x02< Halo 3  "),
		"Menu style: Halo 3 (no settings.txt naming it), in the environment");
	generation = halo_settings_generation;
	restart_pending = 0;
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MENU_STYLE"), "classic") && halo_settings_generation != generation &&
		strstr(file_text(SETTINGS_FILE), "HALO_MENU_STYLE=classic\n") && strstr(menu, "\nMenu style\x02  Halo CE >"),
		"Menu style Halo CE: in the environment and settings.txt at once");
	check(!strstr(menu, "Menu style*") && !restart_pending, "Menu style: live, no restart");
	/* (the next start: settings.txt's Halo CE kept) */
	style->choice = shipped_choice[style - settings];
	unsetenv("HALO_MENU_STYLE");
	vita_settings_load();
	check(!strcmp(getenv("HALO_MENU_STYLE"), "classic") && style->choice == 0, "Menu style Halo CE: read back from settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_MENU_STYLE"), "halo3") && strstr(menu, "\nMenu style\x02< Halo 3  ") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MENU_STYLE=halo3\n"), "Menu style back to Halo 3, saved");
	press(VITA_BUTTON_CIRCLE);
}

static void test_restart_offer(void)
{
	const char *crossplay = getenv("HALO_CROSSPLAY");
	char before[8];

	snprintf(before, sizeof(before), "%s", crossplay ? crossplay : "0");
	main_menu_idle_now = 1;
	open_panel();
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(!menu_visible, "no change that needs a restart: closing asks nothing");

	open_panel();
	to_tab("Multiplayer");
	to_line("Crossplay");
	press(strcmp(before, "1") ? VITA_BUTTON_RIGHT : VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(menu_visible && strstr(menu, "applies after a restart") && strstr(menu, "Cross: yes"),
		"Crossplay changed, closed at the main menu: the restart is offered");
	press(VITA_BUTTON_CROSS);
	frame(0);
	check(restarts_asked == 1 && strstr(log_text, "settings: restarting the game") &&
		strstr(menu, "could not restart itself"), "Cross: the game restarts (here it says it could not)");
	press(VITA_BUTTON_CROSS);
	frame(0);

	open_panel();
	to_tab("Multiplayer");
	to_line("Crossplay");
	press(strcmp(before, "1") ? VITA_BUTTON_LEFT : VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(menu_visible && strstr(menu, "applies after a restart"), "changed back: offered again");
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(!menu_visible && restarts_asked == 1, "Circle: later, no restart");
	open_panel();
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(!menu_visible, "not asked again until another such change");

	main_menu_idle_now = 0;
	open_panel();
	to_tab("Multiplayer");
	to_line("Crossplay");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	frame(0);
	check(!menu_visible && restarts_asked == 1, "in a game: not offered (the help line says it)");
	check(!strcmp(getenv("HALO_CROSSPLAY"), before), "Crossplay as it was");
}

int main(void)
{
	char line[128];
	int index;

	/* (the Vita's folders, in this test's own) */
	mkdir("ux0:data", 0777);
	mkdir("ux0:data/haloce-vita", 0777);
	mkdir("ux0:data/haloce-vita/maps", 0777);
	setenv("HALO_MAPS_ROOT", "ux0:data/haloce-vita/maps", 1);

	test_variables_kept();
	setenv("HALO_VITA_NETWORK", "online", 1);
	vita_settings_load();
	test_tab_layouts();
	check(!strcmp(getenv("HALO_NET_ONLINE"), "true") && !strcmp(getenv("HALO_NET_ALLOW_UPNP"), "false"),
		"Network Online: internet play on, without UPnP");
	check(!getenv("HALO_NET_LOBBY_NAME")[0], "no lobby name: the game's own (its player's profile's), not the Vita's user name");
	check(getenv("HALO_NET_PLAYER_NAME") && !strcmp(getenv("HALO_NET_PLAYER_NAME"), "vitauser"),
		"a default profile's player name is the Vita's user name");
	check(!strstr(log_text, "TEST MODE"), "no dev switch on: halo.log does not say test mode");

	check(!frame(0), "closed: the game gets the buttons");
	/* SELECT + D-pad down: open at once, neither button the game's; held,
	once; again, closed; SELECT alone still the game's (Back, the scoreboard) */
	check(frame(VITA_BUTTON_SELECT | VITA_BUTTON_DOWN) && menu_visible && strstr(menu, "\t*Game|"),
		"SELECT + down opens the panel at once, the game gets neither");
	check(frame(VITA_BUTTON_SELECT | VITA_BUTTON_DOWN) && menu_visible, "SELECT + down held: still open, once per press");
	frame(0);
	check(frame(VITA_BUTTON_SELECT | VITA_BUTTON_DOWN) && !menu_visible, "SELECT + down again closes it");
	frame(0);
	check(!frame(VITA_BUTTON_SELECT), "SELECT alone still reaches the game (Back: the scoreboard)");
	frame(0);
	check(!frame(VITA_BUTTON_DOWN), "D-pad down alone still reaches the game (crouch)");
	frame(0);
	open_panel();
	printf("%s\n--\n", menu);
	check(menu_visible && menu[0] == '\t' && strstr(menu, "\t*Game|Controls|Video|Sound|Multiplayer\n"),
		"SELECT+START opens the Game tab, the first; five tabs: Game, Controls, Video, Sound, Multiplayer (Dev hidden)");
	/* Game: Halo Online's Interface, First person and Updates */
	check(!strcmp(menu_line(1, line, sizeof(line)), "\x08Interface") &&
		!strcmp(menu_line(2, line, sizeof(line)), "Menu style\x02< Halo 3  ") &&
		!strcmp(menu_line(3, line, sizeof(line)),
			VITA_BUTTON_ICONS_DEFAULT ? "Button icons\x02< PlayStation  " : "Button icons\x02  Xbox >") &&
		!strcmp(menu_line(4, line, sizeof(line)), "Language\x02  Automatic >") &&
		!strcmp(menu_line(5, line, sizeof(line)), "FPS counter\x02  Off >") &&
		!strcmp(menu_line(6, line, sizeof(line)), "\x08" "First person") &&
		!strcmp(menu_line(7, line, sizeof(line)), "First-person legs\x02< On  ") &&
		!strcmp(menu_line(8, line, sizeof(line)), "Smooth weapon motion\x02  On >") &&
		!strcmp(menu_line(9, line, sizeof(line)), "\x08Updates") &&
		!strcmp(menu_line(10, line, sizeof(line)), "Check for updates\x02  >") &&
		!strcmp(menu_line(11, line, sizeof(line)), "\x04" "Version " HALO_VITA_VERSION) &&
		!strncmp(menu_line(12, line, sizeof(line)), "Update channel\x02", 15) &&
		!strcmp(menu_line(13, line, sizeof(line)), "Show dev settings\x02  Off >") &&
		!strcmp(menu_line(14, line, sizeof(line)), "\x05" "Halo 3: the new menus over the Halo ring. Halo CE: the original menus.") &&
		!strncmp(menu_line(15, line, sizeof(line)), "\x06L/R: tabs", 10) && menu_height_fits(),
		"Game: Interface (Menu style Halo 3, Button icons, Language, FPS counter), First person (First-person legs, "
		"Smooth weapon motion), Updates (Check for updates and the version, Update channel, Show dev settings); help, buttons");
	check(menu_selected == 2 && menu_fits() && menu_rows() == 9 && !strstr(menu, "*\x02"),
		"Game: Menu style chosen first (not the heading over it); nine rows, each fits, all live (no *)");
	check(!strstr(menu, "Profile") && !strstr(menu, "Aspect ratio") && !strstr(menu, "Look sensitivity") &&
		!strstr(menu, "Stick deadzone") && !strstr(menu, "Sound voices") && !strstr(menu, "\nScreen\x02") &&
		!strstr(menu, "\nFirst person\x02") && !strstr(menu, "\nAdvanced\x02"),
		"Game: no other tab's rows, no page rows (no Screen, First person or Advanced)");
	check(!strcmp(getenv("HALO_MENU_STYLE"), "halo3") && !strstr(file_text(SETTINGS_FILE), "HALO_MENU_STYLE"),
		"no settings.txt naming it: Menu style Halo 3, the shipped default, in the environment the game reads");
	test_headings();

	/* Video: Quality, Screen, More */
	check(to_tab("Video") && strstr(menu, "\tGame|Controls|*Video|Sound|Multiplayer\n"), "R: Video");
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(1, line, sizeof(line)), "\x08Quality") &&
		!strncmp(menu_line(2, line, sizeof(line)), "Profile\x02", 8) &&
		!strncmp(menu_line(3, line, sizeof(line)), "Render resolution\x02", 18) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Dynamic minimum\x02", 16) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Frame limit\x02", 12) &&
		!strncmp(menu_line(6, line, sizeof(line)), "Frame interpolation\x02", 20) &&
		!strcmp(menu_line(7, line, sizeof(line)), "\x08Screen") &&
		!strcmp(menu_line(8, line, sizeof(line)), "Aspect ratio\x02  16:9 >") &&
		!strcmp(menu_line(9, line, sizeof(line)), "Upscale filter\x02  Smooth >") &&
		!strcmp(menu_line(10, line, sizeof(line)), "Sharp interface\x02  On >") &&
		!strcmp(menu_line(11, line, sizeof(line)), "\x08More") &&
		!strcmp(menu_line(12, line, sizeof(line)), "Detail\x02  >") &&
		!strcmp(menu_line(13, line, sizeof(line)), "Performance\x02  >") &&
		!strncmp(menu_line(14, line, sizeof(line)), "\x05Sets resolution", 16) &&
		!strncmp(menu_line(15, line, sizeof(line)), "\x06L/R: tabs", 10) && menu_height_fits() && menu_selected == 2,
		"Video: Quality (Profile, Render resolution, Dynamic minimum, Frame limit, Frame interpolation), Screen (Aspect "
		"ratio 16:9, Upscale filter Smooth, Sharp interface On), More (Detail, Performance); help, buttons; Profile "
		"chosen first");
	check(menu_fits() && menu_rows() == 10, "Video: ten rows with Dynamic minimum shown, each fits");
	check(!strstr(menu, "FPS counter") && !strstr(menu, "First-person legs") && !strstr(menu, "Smooth weapon motion") &&
		!strstr(menu, "Menu style") && !strstr(menu, "Model detail") && !strstr(menu, "\nParticles") &&
		!strstr(menu, "Particle density") && !strstr(menu, "Scenery updates") && !strstr(menu, "AI think rate") &&
		!strstr(menu, "Fourth core helpers") && !strstr(menu, "\nScreen\x02") && !strstr(menu, "First person") &&
		!strstr(menu, "Display") && !strstr(menu, "Advanced"),
		"Video: the detail rows and particles under Detail, the update rates under Performance; FPS counter, first "
		"person and Menu style on Game (no Screen, First person, Display or Advanced page)");
	check(strstr(menu, "\nProfile\x02< Balanced >") && strstr(menu, "\nRender resolution\x02< Dynamic  ") &&
		strstr(menu, "\nDynamic minimum\x02< 63% >") && strstr(menu, "\nFrame limit\x02  30 FPS >"),
		"values in a column: '< ' or two spaces before each; Balanced: Dynamic, from 63%");
	check(!strstr(menu, "Render resolution*") && !strstr(menu, "Dynamic minimum*"), "resolution and its minimum: live, no *");
	check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
		!strcmp(setting_named("HALO_RENDER_SCALE")->values[shipped_choice[1]], "dynamic") &&
		!strcmp(setting_named("HALO_DYNAMIC_RES_MIN")->values[shipped_choice[2]], "0.625"),
		"the defaults (Balanced): Render resolution Dynamic, Dynamic minimum 63%, in the environment");
	{
		/* Render resolution: a fixed scale hides the minimum (kept) and
		makes the profile Custom; Dynamic again, Balanced; the minimum, a
		row the profiles set, changed by hand: Custom */
		unsigned long generation = halo_settings_generation;

		vita_settings_set("HALO_RENDER_SCALE", "0.75");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "0.75") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_RENDER_SCALE=0.75\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.625\n") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"),
			"Render resolution 75%: in the environment and settings.txt at once (the minimum kept), the profile Custom");
		press(VITA_BUTTON_DOWN);
		press(VITA_BUTTON_UP);
		check(!strstr(menu, "Dynamic minimum") && !strncmp(menu_line(4, line, sizeof(line)), "Frame limit\x02", 12) &&
			menu_rows() == 9, "not Dynamic: the Dynamic minimum row hidden");
		vita_settings_set("HALO_RENDER_SCALE", "0.3");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "0.5"), "a scale off the list goes to the nearest number, not Dynamic");
		vita_settings_set("HALO_RENDER_SCALE", "dynamic");
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && strstr(file_text(SETTINGS_FILE), "HALO_RENDER_SCALE=dynamic\n") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"), "Dynamic again (from 63%): Balanced again");
		press(VITA_BUTTON_DOWN);
		press(VITA_BUTTON_UP);
		check(!strncmp(menu_line(4, line, sizeof(line)), "Dynamic minimum\x02", 16) && menu_rows() == 10 && menu_fits(),
			"Dynamic: the Dynamic minimum row shows, under it");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.75");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.75\n") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"), "Dynamic minimum 75%: saved, the profile Custom");
		vita_settings_set("HALO_DYNAMIC_RES_MIN", "0.625");
		check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "back to 63%: Balanced again");
	}
	{
		/* Screen: the aspect ratio and the upscale filter, on the tab under
		their heading (live, no *) */
		check(to_line("Aspect ratio") && menu_selected == 8 && strstr(menu, "\n\x05" "4:3: the Xbox's framing, black bars\n") &&
			menu_fits() && !strstr(menu, "Aspect ratio*") && !strstr(menu, "Upscale filter*"),
			"Video, Screen: Aspect ratio (16:9) on the tab under its heading, its help; live");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_DISPLAY_WIDTH"), "640") && strstr(file_text(SETTINGS_FILE), "HALO_DISPLAY_WIDTH=640\n") &&
			!strcmp(menu_line(8, line, sizeof(line)), "Aspect ratio\x02< 4:3  ") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"), "Aspect ratio 4:3: saved, the profile kept");
		press(VITA_BUTTON_DOWN);
		check(menu_selected == 9 && strstr(menu, "\n\x05Scaling to the screen: Sharp = crisp pixels\n") && menu_fits(),
			"Upscale filter: its help line fits");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_UPSCALE_FILTER"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_UPSCALE_FILTER=1\n") &&
			!strcmp(menu_line(9, line, sizeof(line)), "Upscale filter\x02< Sharp  ") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"), "Upscale filter Sharp: saved, the profile kept");
		/* (Sharp interface, under it: the interface at the display's
		resolution, on as shipped; live, no *, the profile kept) */
		press(VITA_BUTTON_DOWN);
		check(menu_selected == 10 && !strcmp(menu_line(10, line, sizeof(line)), "Sharp interface\x02  On >") &&
			strstr(menu, "\n\x05HUD, menus and text at full resolution\n") && menu_fits() &&
			!strstr(menu, "Sharp interface*") && !strcmp(getenv("HALO_SHARP_UI"), "1"),
			"down from Upscale filter: Sharp interface (On, in the environment), its help line fits; live");
		{
			unsigned long generation = halo_settings_generation;

			press(VITA_BUTTON_RIGHT);
			check(!strcmp(getenv("HALO_SHARP_UI"), "0") && strstr(file_text(SETTINGS_FILE), "HALO_SHARP_UI=0\n") &&
				halo_settings_generation != generation &&
				!strcmp(menu_line(10, line, sizeof(line)), "Sharp interface\x02< Off  ") &&
				!strcmp(settings[0].names[settings[0].choice], "Balanced"),
				"Sharp interface Off: saved, the readers told (the settings generation), the profile kept");
			press(VITA_BUTTON_LEFT);
			check(!strcmp(getenv("HALO_SHARP_UI"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_SHARP_UI=1\n") &&
				!strcmp(menu_line(10, line, sizeof(line)), "Sharp interface\x02  On >"), "Sharp interface On again: saved");
		}
		press(VITA_BUTTON_DOWN);
		check(menu_selected == 12 && !strcmp(menu_line(menu_selected, line, sizeof(line)), "Detail\x02  >"),
			"down from Sharp interface: Detail (the More heading stepped over)");
		press(VITA_BUTTON_UP);
		press(VITA_BUTTON_UP);
		press(VITA_BUTTON_UP);
		press(VITA_BUTTON_UP);
		check(menu_selected == 6 && !strncmp(menu_line(menu_selected, line, sizeof(line)), "Frame interpolation\x02", 20),
			"up from Aspect ratio: Frame interpolation (the Screen heading stepped over)");
		vita_settings_set("HALO_DISPLAY_WIDTH", "848");
		vita_settings_set("HALO_UPSCALE_FILTER", "0");
	}
	{
		/* Game's First person: the first-person legs and the weapon's
		motion (live, no *); the FPS counter, under Interface (live) */
		unsigned long generation;

		check(to_tab("Game") && to_line("First-person legs") && menu_selected == 7 &&
			!strcmp(menu_line(6, line, sizeof(line)), "\x08" "First person") &&
			!strcmp(menu_line(7, line, sizeof(line)), "First-person legs\x02< On  ") &&
			!strcmp(menu_line(8, line, sizeof(line)), "Smooth weapon motion\x02  On >") &&
			menu_fits() && menu_height_fits() && !strstr(menu, "First-person legs*") &&
			!strstr(menu, "Smooth weapon motion*") && !strcmp(getenv("HALO_FIRST_PERSON_LEGS"), "1") &&
			!strcmp(getenv("HALO_INTERPOLATE_FIRST_PERSON"), "1"),
			"Game, First person: First-person legs (On), Smooth weapon motion (On), both in the environment, live");
		/* (port/linux/game/first_person_legs.c reads it again as the
		generation changes) */
		check(strstr(menu, "\n\x05Your legs when you look down, as others see them\n") && menu_fits(),
			"First-person legs: its help line fits");
		generation = halo_settings_generation;
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_FIRST_PERSON_LEGS"), "0") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_FIRST_PERSON_LEGS=0\n") &&
			!strcmp(menu_line(7, line, sizeof(line)), "First-person legs\x02  Off >") && !strstr(notice, "Restart") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"First-person legs Off: in the environment and settings.txt at once, no restart, the profile kept");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_FIRST_PERSON_LEGS"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_FIRST_PERSON_LEGS=1\n"),
			"... and On again");
		press(VITA_BUTTON_DOWN);
		check(strstr(menu, "\n\x05The weapon's animation blended between game ticks\n") && menu_fits(),
			"Smooth weapon motion: its help line fits");
		generation = halo_settings_generation;
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_INTERPOLATE_FIRST_PERSON"), "0") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_INTERPOLATE_FIRST_PERSON=0\n") &&
			!strcmp(menu_line(8, line, sizeof(line)), "Smooth weapon motion\x02< Off  ") && !strstr(notice, "Restart") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"Smooth weapon motion Off: in the environment and settings.txt at once, no restart, the profile kept");
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_INTERPOLATE_FIRST_PERSON"), "1") &&
			strstr(file_text(SETTINGS_FILE), "HALO_INTERPOLATE_FIRST_PERSON=1\n"), "... and On again");
		press(VITA_BUTTON_UP);
		press(VITA_BUTTON_UP);
		check(menu_selected == 5 && !strncmp(menu_line(menu_selected, line, sizeof(line)), "FPS counter\x02", 12),
			"up from First-person legs: FPS counter (the First person heading stepped over)");
		check(strstr(menu, "\n\x05The game's frame counter, bottom right\n") && menu_fits(), "FPS counter: its help line fits");
		generation = halo_settings_generation;
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_FRAMERATE_COUNTER"), "1") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_FRAMERATE_COUNTER=1\n") &&
			!strcmp(menu_line(5, line, sizeof(line)), "FPS counter\x02< On  ") && !strstr(notice, "Restart") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"FPS counter On: in the environment and settings.txt at once, no restart, the profile kept");
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_FRAMERATE_COUNTER"), "0"), "... and Off again");
		for (index = 0; index < 20 && menu_selected > 2; index++)
			press(VITA_BUTTON_UP);
		check(to_tab("Video") && menu_selected == 6, "back on Video: on Frame interpolation, as left");
		for (index = 0; index < 20 && menu_selected > 2; index++)
			press(VITA_BUTTON_UP);
	}
	{
		/* Frame interpolation: Off by default (and in the environment the
		game reads, no longer a fixed default), live (no *), saved; the
		profile untouched */
		unsigned long generation = halo_settings_generation;

		check(!strcmp(getenv("HALO_INTERPOLATION"), "false") &&
			strstr(file_text(SETTINGS_FILE), "HALO_INTERPOLATION=false\n") &&
			strstr(menu, "\nFrame interpolation\x02  Off >") && !strstr(menu, "Frame interpolation*"),
			"Frame interpolation: Off by default, saved, live");
		check(to_line("Frame interpolation") && strstr(menu, "\n\x05Up to 60 FPS: frames between the 30 Hz ticks\n") &&
			menu_fits(), "Frame interpolation: its help line fits");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_INTERPOLATION"), "true") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_INTERPOLATION=true\n") &&
			strstr(menu, "\nFrame interpolation\x02< On") && !strstr(notice, "Restart") &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"Frame interpolation On: in the environment and settings.txt at once, no restart, the profile kept");
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_INTERPOLATION"), "false") &&
			strstr(file_text(SETTINGS_FILE), "HALO_INTERPOLATION=false\n"), "... and Off again");
	}
	/* Video's Detail: the detail rows (most of them the profile's), the
	particles made and drawn */
	check(to_line("Detail") && !strcmp(menu_line(menu_selected - 1, line, sizeof(line)), "\x08More") &&
		strstr(menu, "\n\x05" "Models, shadows, lights, effects, particles\n") && menu_fits(),
		"Detail: under More, its help says what is there");
	check(open_page("Detail") && strstr(menu, "\n\x03Video > Detail\n") && !strchr(menu, '\x08') &&
		!strcmp(menu_line(2, line, sizeof(line)), "Model detail\x02< Medium >") &&
		!strcmp(menu_line(3, line, sizeof(line)), "Hide distant objects\x02< Small >") &&
		!strcmp(menu_line(4, line, sizeof(line)), "Object shadows\x02< Off  ") &&
		!strcmp(menu_line(5, line, sizeof(line)), "Dynamic lights\x02< 4 >") &&
		!strcmp(menu_line(6, line, sizeof(line)), "Effects quality\x02  Full >") &&
		!strcmp(menu_line(7, line, sizeof(line)), "Sun rays\x02< Off  ") &&
		!strcmp(menu_line(8, line, sizeof(line)), "Tiny decals\x02  Shown >") &&
		!strcmp(menu_line(9, line, sizeof(line)), "Particles\x02< Reduced >") &&
		!strcmp(menu_line(10, line, sizeof(line)), "Particle density\x02  Full >") &&
		!strcmp(menu_line(11, line, sizeof(line)), "Ragdolls\x02< On  ") &&
		!strncmp(menu_line(12, line, sizeof(line)), "\x05Level of detail", 16) && menu_rows() == 10 && menu_fits() &&
		menu_height_fits() && !strstr(menu, "*\x02"),
		"Video, Detail: model detail (Medium), distant objects (Small), shadows (Off), lights (4), effects (Full), "
		"sun rays (Off), tiny decals (Shown), Particles (Reduced), particle density (Full), Ragdolls (On); all live; "
		"no headings on a page");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1") && !strcmp(settings[0].names[settings[0].choice], "Custom"),
		"a detail row changed on its page (Model detail High): the profile Custom");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "... and Balanced again");
	test_particles_row();
	/* (port/linux/game/ragdolls.c reads it again as the generation
	changes: no restart, no profile's) */
	{
		unsigned long generation;

		check(to_line("Ragdolls") && strstr(menu, "\n\x05" "Dead bodies fall limp where they land\n") && menu_fits() &&
			getenv("HALO_RAGDOLLS") && !strcmp(getenv("HALO_RAGDOLLS"), "1"),
			"Ragdolls: On by default, in the environment, its help line fits");
		generation = halo_settings_generation;
		press(VITA_BUTTON_LEFT);
		check(!strcmp(getenv("HALO_RAGDOLLS"), "0") && halo_settings_generation != generation &&
			strstr(file_text(SETTINGS_FILE), "HALO_RAGDOLLS=0\n") &&
			!strcmp(menu_line(menu_selected, line, sizeof(line)), "Ragdolls\x02  Off >") && !restart_pending &&
			!strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"Ragdolls Off: in the environment and settings.txt at once, no restart, the profile kept");
		press(VITA_BUTTON_RIGHT);
		check(!strcmp(getenv("HALO_RAGDOLLS"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_RAGDOLLS=1\n"),
			"... and On again");
	}
	press(VITA_BUTTON_CIRCLE);
	check(menu_visible && strstr(menu, "|*Video|") && !strstr(menu, "\n\x03") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Detail\x02  >"),
		"circle: back to Video, on Detail");
	/* Video's Performance: the update rates, the AI's, the fourth core */
	check(to_line("Performance") && strstr(menu, "\n\x05" "Update rates, AI think rate, fourth core helpers\n") &&
		menu_fits(), "Performance: its help says what is there");
	check(open_page("Performance") && strstr(menu, "\n\x03Video > Performance\n") && !strchr(menu, '\x08') &&
		!strcmp(menu_line(2, line, sizeof(line)), "Scenery updates\x02< Quarter  ") &&
		!strcmp(menu_line(3, line, sizeof(line)), "Object lighting\x02< Third  ") &&
		!strcmp(menu_line(4, line, sizeof(line)), "AI think rate\x02< Adaptive >") &&
		!strcmp(menu_line(5, line, sizeof(line)), "Fourth core helpers*\x02< All async  ") && menu_rows() == 4 &&
		menu_fits() && menu_height_fits() && !strstr(menu, "Scenery updates*") && !strstr(menu, "AI think rate*"),
		"Video, Performance: scenery (Quarter), lighting (Third), AI think rate (Adaptive), fourth core helpers "
		"(All async, after a restart)");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_SCENERY_UPDATE_DIVISOR"), "2") && !strcmp(settings[0].names[settings[0].choice], "Custom") &&
		!restart_pending, "a rate changed on its page (Scenery updates Half): the profile Custom, no restart");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(settings[0].names[settings[0].choice], "Balanced"), "... and Balanced again");
	test_bruno_rows();
	test_fourth_core();
	press(VITA_BUTTON_CIRCLE);
	check(menu_visible && strstr(menu, "|*Video|") && !strstr(menu, "\n\x03") &&
		!strcmp(menu_line(menu_selected, line, sizeof(line)), "Performance\x02  >"),
		"circle: back to Video, on Performance");
	/* L and R through the tabs, wrapping at either end */
	press(VITA_BUTTON_L);
	check(strstr(menu, "|*Controls|") && strstr(menu, "Look sensitivity"), "L from Video: Controls");
	press(VITA_BUTTON_L);
	check(strstr(menu, "\t*Game|") && strstr(menu, "Menu style"), "L: Game");
	press(VITA_BUTTON_L);
	check(strstr(menu, "|*Multiplayer\n") != NULL, "L from the first tab, Game, wraps to the last shown");
	press(VITA_BUTTON_R);
	check(strstr(menu, "\t*Game|") != NULL, "R from the last: Game again");
	press(VITA_BUTTON_R);
	press(VITA_BUTTON_R);
	press(VITA_BUTTON_R);
	check(strstr(menu, "|*Sound|") && strstr(menu, "\nSound voices*\x02") && strstr(menu, "\nSound occlusion\x02") &&
		strstr(menu, "\nSound updates\x02< Every 2nd >") && menu_rows() == 3 && !strstr(menu, "Language") &&
		!strchr(menu, '\x08') && menu_fits() && menu_height_fits(),
		"R, R, R: Sound: the sound rows, no heading (Language on Game)");
	check(!strncmp(menu_line(1, line, sizeof(line)), "Sound voices*\x02", 14) && menu_selected == 1,
		"Sound: Sound voices first, chosen");
	press(VITA_BUTTON_R);
	check(!strncmp(menu, "\tGame|Controls|Video|Sound|*Multiplayer\n", sizeof("\tGame|Controls|Video|Sound|*Multiplayer\n") - 1),
		"R: Multiplayer");
	printf("%s\n--\n", menu);

	test_multiplayer_tab();

	test_forge_page();
	test_player_model_row();

	/* ---------- Multiplayer's Modded maps page */
	write_map("ux0:data/haloce-vita/maps/mygulch.map", 5, 250000);
	write_map("ux0:data/haloce-vita/maps/inplay.map", 5, 2000);
	write_map("ux0:data/haloce-vita/maps/cemap.map", 609, 3 * 1024 * 1024 / 16);
	write_map_of_type("ux0:data/haloce-vita/maps/cesolo.map", 609, 0, 4096);
	write_map("ux0:data/haloce-vita/maps/bloodgulch.map", 5, 1000);
	write_map("ux0:data/haloce-vita/maps/bitmaps.map", 1, 1000);
	write_file("ux0:data/haloce-vita/maps/cemap.bmp", "BM", 2);
	write_file("ux0:data/haloce-vita/maps/notes.txt", "x", 1);
	open_panel();
	check(to_tab("Multiplayer") && strstr(menu, "\nModded maps\x02  4 maps  >"), "Multiplayer: Modded maps says how many");
	check(open_page("Modded maps") && strstr(menu, "\n\x03Multiplayer > Modded maps\n"), "the Modded maps page");
	printf("%s\n--\n", menu);
	check(!strcmp(menu_line(2, line, sizeof(line)), "Mod manager\x02  >") &&
		!strncmp(menu_line(3, line, sizeof(line)), "PC maps\x02", 8) &&
		!strcmp(menu_line(4, line, sizeof(line)), "Map downloads\x02  Ask >") && !menu_line(5, line, sizeof(line))[0] &&
		!strcmp(menu_line(6, line, sizeof(line)), "!Missing: sounds.map loc.map") &&
		!strcmp(menu_line(7, line, sizeof(line)), "!Copy them in, or the Halo CE installer") &&
		!strcmp(menu_line(8, line, sizeof(line)), "!(halocesetup*.exe) to ux0:data/haloce-vita"),
		"Mod manager, PC maps switch, Map downloads (Ask), a gap; a CE map without sounds.map/loc.map: the warning, "
		"and that the installer does");
	check(!strncmp(menu_line(9, line, sizeof(line)), "cemap\x02", 6) && !strncmp(menu_line(10, line, sizeof(line)), "cesolo\x02", 7) &&
		!strncmp(menu_line(11, line, sizeof(line)), "inplay\x02", 7) &&
		!strncmp(menu_line(12, line, sizeof(line)), "mygulch\x02", 8) && menu_line(13, line, sizeof(line))[0] == '\x05',
		"the custom maps listed in order, not the Xbox's or CE resource maps");
	/* Map downloads: Ask (the default: public games warn), Not public games,
	Never; each with its own help, saved and in the environment the game
	reads (map_share.c) */
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "ask") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=ask\n"), "Map downloads: Ask by default, saved");
	to_line("Map downloads");
	check(strstr(menu, "\n\x05" "Asks before a host's map comes; public games warn\n") && menu_fits(),
		"Ask's help: public games warn");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "private") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=private\n") &&
		strstr(menu, "\nMap downloads\x02< Not public games >\n") &&
		strstr(menu, "\n\x05" "Asks, but not in games from the public lobby\n") && menu_fits(),
		"right: Not public games, saved, its help");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "never") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAP_SHARE_FROM=never\n") &&
		strstr(menu, "\nMap downloads\x02< Never  \n") &&
		strstr(menu, "\n\x05" "No downloads: copy the host's map in yourself\n"), "right: Never, saved, its help");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MAP_SHARE_FROM"), "ask"), "back to Ask");
	check(strstr(menu, "\nmygulch\x02  On    245 KB  Xbox\n") != NULL, "mygulch: On, size, Xbox");
	check(strstr(menu, "\ncemap\x02  On ") && strstr(menu, "  CE\n"), "cemap: Custom Edition");
	check(strstr(menu, "\ncesolo\x02  On ") && strstr(menu, "  CE SP\n"), "cesolo: a Custom Edition campaign map");
	check(menu_fits(), "Modded maps: each line fits");
	to_line("mygulch");
	check(strstr(menu, "\n\x05Xbox map: on or off, or delete it") &&
		strstr(menu, "\n\x06L/R: tabs   Left/right: on/off   Square: delete   Circle: back"), "a map's help and buttons");
	press(VITA_BUTTON_LEFT);
	check(strstr(menu_line(menu_selected, line, sizeof(line)), "Off") && !strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch") &&
		strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED=mygulch\n"), "left: mygulch off, in the environment and settings.txt");
	press(VITA_BUTTON_UP);
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_MAPS_DISABLED"), "inplay,mygulch") || !strcmp(getenv("HALO_MAPS_DISABLED"), "mygulch,inplay"),
		"two maps off");
	press(VITA_BUTTON_RIGHT);
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_CROSS);
	check(!getenv("HALO_MAPS_DISABLED") || !getenv("HALO_MAPS_DISABLED")[0], "right / cross: both on again");
	check(!strstr(file_text(SETTINGS_FILE), "HALO_MAPS_DISABLED"), "none off: no line in settings.txt");
	press(VITA_BUTTON_LEFT);
	/* delete: the map in play is kept */
	to_line("inplay");
	press(VITA_BUTTON_SQUARE);
	check(!strncmp(menu, "DELETE MAP", 10) && strstr(menu, "inplay.map"), "square asks before deleting");
	press(VITA_BUTTON_CROSS);
	check(strstr(menu, "inplay is in play") && file_exists("ux0:data/haloce-vita/maps/inplay.map"), "the map in play is kept");
	/* (the campaign map gone too, so that no Custom Edition map is left) */
	unlink("ux0:data/haloce-vita/maps/cesolo.map");
	to_line("cemap");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CIRCLE);
	check(file_exists("ux0:data/haloce-vita/maps/cemap.map") && strstr(menu, "> Modded maps\n"), "circle keeps it");
	press(VITA_BUTTON_SQUARE);
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(!file_exists("ux0:data/haloce-vita/maps/cemap.map") && !file_exists("ux0:data/haloce-vita/maps/cemap.bmp") &&
		strstr(menu, "Deleted cemap.map") && !strstr(menu, "!Missing"), "cross deletes it (and its picture); no CE map, no warning");
	check(strstr(log_text, "settings: map cemap.map deleted") != NULL, "halo.log says what was deleted");
	press(VITA_BUTTON_UP);
	for (index = 0; index < 20 && strncmp(menu_line(menu_selected, line, sizeof(line)), "PC maps", 7); index++)
		press(VITA_BUTTON_UP);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_CUSTOM_EDITION"), "1") && strstr(menu, "!Missing: sounds.map loc.map"),
		"PC maps On: the warning again");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_CIRCLE);
	check(strstr(menu, "\nModded maps\x02  2 maps  >") != NULL, "back on Multiplayer: two maps now");
	/* a Custom Edition installer in ux0:data/haloce-vita (PC maps off): the
	warning, and Extract PC files, which starts taking them */
	installer_state = 1;
	open_page("Modded maps");
	printf("%s\n--\n", menu);
	check(strstr(menu, "\n!Missing: sounds.map loc.map\n!Extract PC files takes them from the installer\n") &&
		strstr(menu, "\nExtract PC files\x02") && menu_fits(), "an installer: the warning says Extract PC files, its row");
	to_line("Extract PC files");
	check(strstr(menu, "\n\x05The three files out of the Halo CE installer\n") != NULL, "Extract PC files' help");
	press(VITA_BUTTON_CROSS);
	check(installer_starts == 1 && strstr(menu, "Taking them from halocesetup_en_1.00.exe") &&
		strstr(menu, "\n!Taking them from halocesetup_en_1.00.exe\n") && !strstr(menu, "\nExtract PC files\x02") &&
		menu_fits(), "cross starts it: the row goes, the warning says it is under way");
	installer_state = 0;
	press(VITA_BUTTON_CIRCLE);

	/* MCC mods (experimental, off by default): no row without a folder in
	ux0:data/haloce-vita/mods; with one, the row, Off, and right turns it on
	(HALO_MCC_MODS, live) */
	open_page("Modded maps");
	check(!strstr(menu, "\nMCC mods\x02"), "no mods folder: no MCC mods row");
	press(VITA_BUTTON_CIRCLE);
	mkdir("ux0:data/haloce-vita/mods", 0777);
	mkdir("ux0:data/haloce-vita/mods/ruby", 0777);
	open_page("Modded maps");
	check(strstr(menu, "\nMCC mods\x02  Off >") != NULL && menu_fits(), "a mod's folder: the MCC mods row, Off");
	to_line("MCC mods");
	check(strstr(menu, "\n\x05" "Experimental: mods folder's MCC maps, solo, offline\n") != NULL, "MCC mods' help");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_MCC_MODS") && !strcmp(getenv("HALO_MCC_MODS"), "1") && strstr(menu, "\nMCC mods\x02< On  \n"),
		"right: MCC mods On");
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_MCC_MODS") && !strcmp(getenv("HALO_MCC_MODS"), "0"), "left: Off again");
	press(VITA_BUTTON_CIRCLE);
	rmdir("ux0:data/haloce-vita/mods/ruby");
	rmdir("ux0:data/haloce-vita/mods");

	/* PC map vehicles: no row while PC maps is off; with it on, Xbox, and
	right PC, then Allowed (HALO_PC_MAP_VEHICLES, live); back to Xbox, and PC
	maps off again hides it */
	open_page("Modded maps");
	check(!strstr(menu, "\nPC map vehicles\x02"), "PC maps off: no PC map vehicles row");
	to_line("PC maps");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\nPC map vehicles\x02  Xbox >") != NULL && menu_fits(), "PC maps on: the PC map vehicles row, Xbox");
	to_line("PC map vehicles");
	check(strstr(menu, "\n\x05" "Local games: PC maps' Banshees, turrets. Allowed: all the map allows\n") != NULL,
		"PC map vehicles' help");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_PC_MAP_VEHICLES") && !strcmp(getenv("HALO_PC_MAP_VEHICLES"), "pc"), "right: PC");
	press(VITA_BUTTON_RIGHT);
	check(getenv("HALO_PC_MAP_VEHICLES") && !strcmp(getenv("HALO_PC_MAP_VEHICLES"), "all") &&
		strstr(menu, "\nPC map vehicles\x02< Allowed  \n"), "right again: Allowed");
	press(VITA_BUTTON_LEFT);
	press(VITA_BUTTON_LEFT);
	check(getenv("HALO_PC_MAP_VEHICLES") && !strcmp(getenv("HALO_PC_MAP_VEHICLES"), "xbox"), "left twice: Xbox again");
	to_line("PC maps");
	press(VITA_BUTTON_LEFT);
	check(!strcmp(getenv("HALO_CUSTOM_EDITION"), "0") && !strstr(menu, "\nPC map vehicles\x02"),
		"PC maps off again: the row goes");
	press(VITA_BUTTON_CIRCLE);

	/* ---------- Modded maps' Mod manager page */
	test_mod_manager_page();
	test_friends_page();
	test_party_page();

	/* ---------- Dev, behind its switch */
	press(VITA_BUTTON_R);
	check(strstr(menu, "\t*Game|") != NULL, "R from Multiplayer: Game (Dev hidden)");
	check(to_line("Show dev settings") && menu_selected == 13 && strstr(menu, "\n\x05The Dev tab: switches for testers, Save report\n"),
		"Show dev settings: Game's last row, under Updates, its help");
	press(VITA_BUTTON_RIGHT);
	check(strstr(menu, "\t*Game|Controls|Video|Sound|Multiplayer|Dev\n") && menu_fits() && tab_bar_fits() &&
		strstr(menu, "\nShow dev settings\x02< On  "), "Show dev settings (Game): the Dev tab, last");
	to_tab("Dev");
	printf("%s\n--\n", menu);
	check(!strncmp(menu_line(1, line, sizeof(line)), "Performance logging*\x02", 21) &&
		!strncmp(menu_line(2, line, sizeof(line)), "Crash dump on hang\x02", 19) &&
		!strncmp(menu_line(3, line, sizeof(line)), "FPS overlay\x02", 12) &&
		!strncmp(menu_line(4, line, sizeof(line)), "Debug camera\x02", 13) &&
		!strncmp(menu_line(5, line, sizeof(line)), "Ad hoc dialog\x02", 14) &&
		!strcmp(menu_line(6, line, sizeof(line)), "Save report\x02  >") &&
		!strcmp(menu_line(7, line, sizeof(line)), "A/B switches\x02  >") && menu_rows() == 7 && menu_fits(),
		"Dev: the switches, Debug camera, Ad hoc dialog, Save report, then A/B switches");
	check(!strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "XV_FPS"),
		"dev switches off: not in settings.txt");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_FRAME_TIMING"), "300") && !strcmp(getenv("HALO_RENDER_PROFILE"), "1") &&
		!strcmp(getenv("HALO_TICK_PROFILE"), "1") && strstr(file_text(SETTINGS_FILE), "HALO_PERF_LOG=1\n") &&
		strstr(menu, "\n\x05Restart the game"), "Performance logging: the three timing variables, saved, a restart");
	clock_us += 5000000;
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_HANG_CRASH"), "1") && !strstr(menu, "Restart the game"), "Crash dump on hang: live");
	press(VITA_BUTTON_DOWN);
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 2 && !strcmp(getenv("XV_FPS"), "2"), "FPS overlay: FPS only, live");
	press(VITA_BUTTON_RIGHT);
	check(overlay_level == 1 && strstr(file_text(SETTINGS_FILE), "XV_FPS=1\n"), "FPS overlay: Full");
	check(open_page("A/B switches") && strstr(menu, "\n\x03" "Dev > A/B switches\n") &&
		strstr(menu, "\nGPU W clamp*\x02") && strstr(menu, "\nTarget mip minimum*\x02") &&
		strstr(menu, "\nFrame phase lock*\x02") && strstr(menu, "\nRender target sync*\x02") && menu_rows() == 4 &&
		menu_fits(), "Dev, A/B switches: the start-up ones, marked *");
	to_line("Render target sync");
	press(VITA_BUTTON_RIGHT);
	check(!strcmp(getenv("HALO_GXM_RTT_SYNC"), "0") && strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC=0\n"),
		"Render target sync Off: 0, saved");
	press(VITA_BUTTON_LEFT);
	check(!getenv("HALO_GXM_RTT_SYNC") && !strstr(file_text(SETTINGS_FILE), "HALO_GXM_RTT_SYNC"),
		"back On: unset (the default), not saved");
	press(VITA_BUTTON_RIGHT);
	restart_pending = 0;
	press(VITA_BUTTON_CIRCLE);
	/* Save report */
	write_file("ux0:data/haloce-vita/halo.log", "log", 3);
	write_file("ux0:data/haloce-vita/halo-prev.log", "prev", 4);
	write_file("ux0:data/haloce-vita/env.txt", "HALO_NO_AUDIO=1\n", 16);
	write_map("ux0:data/psp2core-1-old.psp2dmp", 0, 100);
	write_map("ux0:data/psp2core-2-new.psp2dmp", 0, 200);
	{
		struct utimbuf old_time = { 1000, 1000 };

		utime("ux0:data/psp2core-1-old.psp2dmp", &old_time);
	}
	to_line("Save report");
	press(VITA_BUTTON_CROSS);
	printf("%s\n--\n", menu);
	check(strstr(menu, "Saved: ux0:data/haloce-vita/report-20261006-153012") != NULL, "Save report shows the folder");
	check(file_exists("ux0:data/haloce-vita/report-20261006-153012/halo.log") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/halo-prev.log") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/settings.txt") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/env.txt") &&
		file_exists("ux0:data/haloce-vita/report-20261006-153012/psp2core-2-new.psp2dmp") &&
		!file_exists("ux0:data/haloce-vita/report-20261006-153012/psp2core-1-old.psp2dmp"),
		"the report: the logs, settings, env.txt and the newest dump");
	check(menu_fits(), "Dev: each line fits (the report's folder too)");
	test_online_rows();
	press(VITA_BUTTON_CIRCLE);
	check(!menu_visible, "circle closes the panel");
	/* (Show dev settings turned off - the game's @set, a settings.txt -
	with the panel last on Dev: it opens on the first tab, Game) */
	vita_settings_set("HALO_DEV_SETTINGS", "0");
	open_panel();
	check(menu_visible && strstr(menu, "\t*Game|Controls|Video|Sound|Multiplayer\n"),
		"Dev hidden since the panel was last on it: it opens on Game, the first tab");
	press(VITA_BUTTON_CIRCLE);
	vita_settings_set("HALO_DEV_SETTINGS", "1");

	/* the next start: the dev switches on say test mode in halo.log */
	log_text[0] = 0;
	unsetenv("HALO_GXM_RTT_SYNC");
	unsetenv("XV_FPS");
	vita_settings_load();
	printf("%s--\n", log_text);
	check(strstr(log_text, "settings: TEST MODE, dev switches on: HALO_PERF_LOG=1 (HALO_FRAME_TIMING=300") &&
		strstr(log_text, "HALO_GXM_RTT_SYNC=0") && strstr(log_text, "XV_FPS=1"), "test mode in halo.log");
	check(!strcmp(getenv("HALO_GXM_RTT_SYNC"), "0"), "settings.txt's dev switch applied at start-up");

	/* a dev switch env.txt sets and settings.txt does not: env.txt's
	value stands */
	unlink(SETTINGS_FILE);
	settings[0].choice = 1;
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].dev)
			settings[index].choice = 0;
	setenv("HALO_FRAME_PHASE_LOCK", "0", 1);
	setenv("HALO_TARGET_CHAIN_MIN_SIZE", "12", 1);
	unsetenv("HALO_GXM_RTT_SYNC");
	unsetenv("HALO_FRAME_TIMING");
	unsetenv("HALO_RENDER_PROFILE");
	unsetenv("HALO_TICK_PROFILE");
	vita_settings_load();
	check(!strcmp(getenv("HALO_TARGET_CHAIN_MIN_SIZE"), "12") && !strcmp(getenv("HALO_FRAME_PHASE_LOCK"), "0") &&
		choice_of("HALO_FRAME_PHASE_LOCK") == 1, "env.txt's dev values stand (shown in the panel)");

	/* settings.txt of 1.0 loads */
	{
		static const char old[] = "HALO_PROFILE=custom\nXV_FPS=1\nHALO_FRAMERATE_COUNTER=1\nHALO_FRAME_CAP=60\n"
			"HALO_RENDER_SCALE=0.5\nHALO_DISPLAY_WIDTH=640\nHALO_UPSCALE_FILTER=1\nHALO_MODEL_LOD_SCALE=1\n"
			"HALO_MIN_OBJECT_PIXELS=0\nHALO_SCENERY_UPDATE_DIVISOR=1\nHALO_INTERPOLATE_FIRST_PERSON=0\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=1\nHALO_SOUND_CHANNELS=24\nHALO_SOUND_OBSTRUCTION_TICKS=6\n"
			"XV_LOOK_SENS=150\nHALO_CROUCH_TOGGLE=0\nXV_INVERT_Y=1\nXV_DEADZONE=10\n";

		write_file(SETTINGS_FILE, old, sizeof(old) - 1);
		unsetenv("XV_FPS");
		profile_rows_fresh();
		vita_settings_load();
		check(!strcmp(getenv("XV_FPS"), "1") && !strcmp(getenv("HALO_RENDER_SCALE"), "0.5") &&
			!strcmp(getenv("HALO_DISPLAY_WIDTH"), "640") && !strcmp(getenv("XV_LOOK_SENS"), "150") &&
			!strcmp(getenv("XV_INVERT_Y"), "1") && !strcmp(getenv("HALO_SOUND_CHANNELS"), "24") &&
			!strcmp(getenv("HALO_CROUCH_TOGGLE"), "0") && !strcmp(getenv("HALO_FRAME_CAP"), "60") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"), "a 1.0 settings.txt loads, row for row");
		check(!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") && choice_of("HALO_DYNAMIC_RES_MIN") == 1,
			"a 1.0 settings.txt (Custom): Dynamic minimum at its default, 63%");
		check(!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1") && !strcmp(getenv("HALO_SOUND_OBSTRUCTION_TICKS"), "6") &&
			!strcmp(getenv("HALO_SUN_RAYS"), "0") && !strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0"),
			"a 1.0 settings.txt (Custom): its own rows kept, the rows it does not name at their defaults (no sun rays)");
	}

	/* 1.0.3's co-op choice (every game hosted became that level, the
	owner's trap) loads as Off, and is gone from settings.txt at the next
	save; its Online games row loads as Visibility */
	{
		static const char coop[] = "HALO_NET_COOP_LEVEL=a10\nHALO_NET_COOP_DIFFICULTY=2\nHALO_NET_LOBBY_PUBLIC=false\n"
			"HALO_VITA_NETWORK=online\n";

		write_file(SETTINGS_FILE, coop, sizeof(coop) - 1);
		unsetenv("HALO_NET_COOP_LEVEL");
		unsetenv("HALO_NET_COOP_DIFFICULTY");
		unsetenv("HALO_NET_HOST_PUBLIC");
		unsetenv("HALO_NET_LOBBY_PUBLIC");
		log_text[0] = 0;
		vita_settings_load();
		check(!getenv("HALO_NET_COOP_LEVEL") && !getenv("HALO_NET_COOP_DIFFICULTY") &&
			strstr(log_text, "settings: the old co-op setting (a10) is off: co-op is hosted from Campaign"),
			"a 1.0.3 settings.txt with a co-op level: Off (not set), halo.log says where co-op went");
		check(!strcmp(getenv("HALO_NET_HOST_PUBLIC"), "false") &&
			choice_of("HALO_NET_HOST_PUBLIC") == 0, "1.0.3's Online games Private: Visibility Private");
		vita_settings_set("HALO_FRAME_CAP", "30");
		check(!strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP_LEVEL") &&
			!strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP_DIFFICULTY") && !strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_PUBLIC") &&
			strstr(file_text(SETTINGS_FILE), "HALO_NET_HOST_PUBLIC=false\n"),
			"the next save: no co-op lines, Visibility under its own name");
		/* (env.txt's co-op level stands: the automated co-op tests) */
		setenv("HALO_NET_COOP_LEVEL", "a30", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_NET_COOP_LEVEL"), "a30") && !strstr(file_text(SETTINGS_FILE), "HALO_NET_COOP_LEVEL"),
			"env.txt's co-op level is left alone (tests), and never saved");
		unsetenv("HALO_NET_COOP_LEVEL");
	}

	/* the Play page's texts: a lobby name typed comes back; none typed is
	the Vita's user name, not saved */
	{
		static const char texts[] = "HALO_NET_LOBBY_NAME=Couch\x7f co-op!\nHALO_NET_LOBBY_PASSWORD=pw 1\n";

		write_file(SETTINGS_FILE, texts, sizeof(texts) - 1);
		unsetenv("HALO_NET_LOBBY_NAME");
		unsetenv("HALO_NET_LOBBY_PASSWORD");
		vita_settings_load();
		check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Couch co-op!") && !strcmp(getenv("HALO_NET_LOBBY_PASSWORD"), "pw 1"),
			"settings.txt's lobby name (printable characters only) and password at start-up");
		unlink(SETTINGS_FILE);
		unsetenv("HALO_NET_LOBBY_NAME");
		unsetenv("HALO_NET_LOBBY_PASSWORD");
		vita_settings_load();
		vita_settings_set("HALO_FRAME_CAP", "30");
		check(!getenv("HALO_NET_LOBBY_NAME")[0] && !getenv("HALO_NET_LOBBY_PASSWORD")[0] &&
			!strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME") && !strstr(file_text(SETTINGS_FILE), "PASSWORD"),
			"none saved: no lobby name (the profile's stands), no password");
		vita_settings_set("HALO_NET_LOBBY_NAME", "Test_Lobby_With_A_Long_Name");
		check(!strcmp(getenv("HALO_NET_LOBBY_NAME"), "Test_Lobby_With") &&
			strstr(file_text(SETTINGS_FILE), "HALO_NET_LOBBY_NAME=Test_Lobby_With\n"),
			"@set HALO_NET_LOBBY_NAME (the harness's): cut to 15, saved");
		vita_settings_set("HALO_NET_LOBBY_NAME", "");
	}

	/* settings.txt files saved with a profile (HALO_PROFILE is saved only
	while the rows match it) load as that profile with this build's values:
	the rows added since (Sun rays, 1.1; Bruno Santana's, 1.1; Particles,
	1.1.0-beta.4) and the ones changed (1.1.0-beta.4: Dynamic resolution
	in all three with their minimums, Quality's Adaptive AI; then Balanced
	and Quality redone: a Balanced player's shadows, sun rays and 8 lights
	go, Medium models and Reduced particles come). Custom keeps
	its rows; a row env.txt names is left to env.txt (and settings.txt,
	which has the last word over it, as for any row) */
	{
		char report[160];
		/* (1.0.3: no Sun rays, no Bruno Santana's rows, no Particles, no
		Dynamic) */
		static const char performance[] = "HALO_PROFILE=performance\nHALO_RENDER_SCALE=0.5\nHALO_MODEL_LOD_SCALE=0.5\n"
			"HALO_MIN_OBJECT_PIXELS=8\nHALO_SCENERY_UPDATE_DIVISOR=4\nHALO_LIGHTING_REFRESH_DIVISOR=3\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=6\n";
		static const char balanced_103[] = "HALO_PROFILE=balanced\nHALO_RENDER_SCALE=0.75\nHALO_MODEL_LOD_SCALE=0.5\n"
			"HALO_MIN_OBJECT_PIXELS=8\nHALO_SCENERY_UPDATE_DIVISOR=4\nHALO_LIGHTING_REFRESH_DIVISOR=3\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=3\nHALO_DISPLAY_WIDTH=640\n";
		static const char quality[] = "HALO_PROFILE=quality\nHALO_RENDER_SCALE=1\nHALO_MODEL_LOD_SCALE=1\n"
			"HALO_MIN_OBJECT_PIXELS=0\nHALO_SCENERY_UPDATE_DIVISOR=1\nHALO_LIGHTING_REFRESH_DIVISOR=1\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=1\n";
		/* (the 1.1 betas, before Particles: every row saved, the minimum
		50% and hidden) */
		static const char balanced_11[] = "HALO_PROFILE=balanced\nHALO_RENDER_SCALE=0.75\nHALO_DYNAMIC_RES_MIN=0.5\n"
			"HALO_DISPLAY_WIDTH=848\nHALO_UPSCALE_FILTER=1\nHALO_MODEL_LOD_SCALE=0.5\nHALO_MIN_OBJECT_PIXELS=8\n"
			"HALO_VITA_SHADOWS=1\nHALO_MAX_SCENE_LIGHTS=8\nHALO_VITA_EFFECTS_QUALITY=2\nHALO_PARTICLE_RENDER_DIVISOR=1\n"
			"HALO_SUN_RAYS=1\nHALO_SCENERY_UPDATE_DIVISOR=4\nHALO_LIGHTING_REFRESH_DIVISOR=3\nHALO_AI_THINK_DIVISOR=0\n"
			"HALO_SOUND_OBSTRUCTION_TICKS=3\nHALO_SOUND_MANAGER_DIVISOR=2\nHALO_FRAME_CAP=30\n";
		static const char quality_11[] = "HALO_PROFILE=quality\nHALO_RENDER_SCALE=1\nHALO_DYNAMIC_RES_MIN=0.5\n"
			"HALO_MODEL_LOD_SCALE=1\nHALO_MIN_OBJECT_PIXELS=0\nHALO_VITA_SHADOWS=2\nHALO_MAX_SCENE_LIGHTS=0\n"
			"HALO_VITA_EFFECTS_QUALITY=2\nHALO_PARTICLE_RENDER_DIVISOR=1\nHALO_SUN_RAYS=1\nHALO_SCENERY_UPDATE_DIVISOR=1\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=1\nHALO_AI_THINK_DIVISOR=1\nHALO_SOUND_OBSTRUCTION_TICKS=1\n"
			"HALO_SOUND_MANAGER_DIVISOR=1\n";
		static const char performance_11[] = "HALO_PROFILE=performance\nHALO_RENDER_SCALE=0.5\nHALO_DYNAMIC_RES_MIN=0.5\n"
			"HALO_MODEL_LOD_SCALE=0.5\nHALO_MIN_OBJECT_PIXELS=8\nHALO_VITA_SHADOWS=0\nHALO_MAX_SCENE_LIGHTS=4\n"
			"HALO_VITA_EFFECTS_QUALITY=1\nHALO_PARTICLE_RENDER_DIVISOR=2\nHALO_SUN_RAYS=0\nHALO_SCENERY_UPDATE_DIVISOR=4\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=3\nHALO_AI_THINK_DIVISOR=0\nHALO_SOUND_OBSTRUCTION_TICKS=6\n"
			"HALO_SOUND_MANAGER_DIVISOR=2\nHALO_PARTICLE_BUDGET=reduced\n";
		/* (Custom: the player's own rows, a fixed 75% and AI every tick
		among them) */
		static const char custom_11[] = "HALO_PROFILE=custom\nHALO_RENDER_SCALE=0.75\nHALO_DYNAMIC_RES_MIN=0.5\n"
			"HALO_MODEL_LOD_SCALE=0.75\nHALO_MIN_OBJECT_PIXELS=8\nHALO_VITA_SHADOWS=1\nHALO_MAX_SCENE_LIGHTS=8\n"
			"HALO_VITA_EFFECTS_QUALITY=2\nHALO_PARTICLE_RENDER_DIVISOR=1\nHALO_SUN_RAYS=1\nHALO_SCENERY_UPDATE_DIVISOR=4\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=3\nHALO_AI_THINK_DIVISOR=1\nHALO_SOUND_OBSTRUCTION_TICKS=3\n"
			"HALO_SOUND_MANAGER_DIVISOR=2\nHALO_PARTICLE_BUDGET=full\n";
		/* (1.1.0-beta.4's test builds, before Balanced was redone: every
		row saved, the old Balanced's shadows near, sun rays, 8 lights,
		Low models, Full particles) */
		static const char balanced_b4[] = "HALO_PROFILE=balanced\nHALO_RENDER_SCALE=dynamic\nHALO_DYNAMIC_RES_MIN=0.625\n"
			"HALO_MODEL_LOD_SCALE=0.5\nHALO_MIN_OBJECT_PIXELS=8\nHALO_VITA_SHADOWS=1\nHALO_MAX_SCENE_LIGHTS=8\n"
			"HALO_VITA_EFFECTS_QUALITY=2\nHALO_PARTICLE_RENDER_DIVISOR=1\nHALO_SUN_RAYS=1\nHALO_SCENERY_UPDATE_DIVISOR=4\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=3\nHALO_AI_THINK_DIVISOR=0\nHALO_SOUND_OBSTRUCTION_TICKS=3\n"
			"HALO_SOUND_MANAGER_DIVISOR=2\nHALO_PARTICLE_BUDGET=full\n";
		/* (and its Quality: the Xbox's rows but the AI's) */
		static const char quality_b4[] = "HALO_PROFILE=quality\nHALO_RENDER_SCALE=dynamic\nHALO_DYNAMIC_RES_MIN=0.75\n"
			"HALO_MODEL_LOD_SCALE=1\nHALO_MIN_OBJECT_PIXELS=0\nHALO_VITA_SHADOWS=2\nHALO_MAX_SCENE_LIGHTS=0\n"
			"HALO_VITA_EFFECTS_QUALITY=2\nHALO_PARTICLE_RENDER_DIVISOR=1\nHALO_SUN_RAYS=1\nHALO_SCENERY_UPDATE_DIVISOR=1\n"
			"HALO_LIGHTING_REFRESH_DIVISOR=1\nHALO_AI_THINK_DIVISOR=0\nHALO_SOUND_OBSTRUCTION_TICKS=1\n"
			"HALO_SOUND_MANAGER_DIVISOR=1\nHALO_PARTICLE_BUDGET=full\n";
		/* (Custom by its minimum alone: Dynamic from 75%, Balanced's rows
		otherwise) */
		static const char custom_dynamic[] = "HALO_PROFILE=custom\nHALO_RENDER_SCALE=dynamic\nHALO_DYNAMIC_RES_MIN=0.75\n";

		write_file(SETTINGS_FILE, performance, sizeof(performance) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(0, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Performance"),
			"a Performance settings.txt from 1.0.3: Performance, every row as Performance has them now");
		if (report[0])
			printf("(%s)\n", report);
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5") &&
			!strcmp(getenv("HALO_SUN_RAYS"), "0") && !strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") &&
			!strcmp(getenv("HALO_VITA_SHADOWS"), "0") && !strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "4") &&
			!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0"),
			"... its 50% Dynamic from 50%; sun rays off, Reduced particles, no shadows, 4 lights, Adaptive AI (rows added since)");
		write_file(SETTINGS_FILE, balanced_103, sizeof(balanced_103) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(1, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Balanced") &&
			!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
			!strcmp(getenv("HALO_DISPLAY_WIDTH"), "640"),
			"a Balanced settings.txt from 1.0.3: Balanced, its 75% Dynamic from 63%; its 4:3 kept (no profile's row)");
		if (report[0])
			printf("(%s)\n", report);
		write_file(SETTINGS_FILE, quality, sizeof(quality) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(2, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Quality"),
			"a Quality settings.txt from 1.0.3 (before Sun rays): Quality, every row as Quality has them now");
		if (report[0])
			printf("(%s)\n", report);
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0") && !strcmp(getenv("HALO_SUN_RAYS"), "1") &&
			!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(getenv("HALO_VITA_SHADOWS"), "1") &&
			!strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "8") && !strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "2") &&
			!strcmp(getenv("HALO_PARTICLE_RENDER_DIVISOR"), "1") && !strcmp(getenv("HALO_SOUND_MANAGER_DIVISOR"), "1") &&
			!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1") && !strcmp(getenv("HALO_SOUND_OBSTRUCTION_TICKS"), "3"),
			"... its 100% Dynamic from 75%, Adaptive AI; sun rays, Reduced particles, shadows near, 8 lights, High models, "
			"the Xbox's effects and sound, occlusion every 3rd tick");

		write_file(SETTINGS_FILE, balanced_11, sizeof(balanced_11) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(1, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Balanced") &&
			!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
			!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(getenv("HALO_UPSCALE_FILTER"), "1") &&
			!strcmp(getenv("HALO_VITA_SHADOWS"), "0") && !strcmp(getenv("HALO_SUN_RAYS"), "0") &&
			!strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "4") && !strcmp(getenv("HALO_MODEL_LOD_SCALE"), "0.75"),
			"a Balanced settings.txt of the 1.1 betas (75%, minimum 50%, before Particles): Balanced, Dynamic from 63%, "
			"Reduced particles, no shadows or sun rays, 4 lights, Medium models; its Sharp filter kept");
		if (report[0])
			printf("(%s)\n", report);
		/* (saved again: this build's values, still Balanced) */
		vita_settings_set("HALO_FRAME_CAP", "30");
		check(strstr(file_text(SETTINGS_FILE), "HALO_PROFILE=balanced\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_RENDER_SCALE=dynamic\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_DYNAMIC_RES_MIN=0.625\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_PARTICLE_BUDGET=reduced\n") &&
			strstr(file_text(SETTINGS_FILE), "HALO_VITA_SHADOWS=0\n") && strstr(file_text(SETTINGS_FILE), "HALO_SUN_RAYS=0\n"),
			"... saved again: Balanced with Dynamic from 63%, Reduced particles, no shadows, no sun rays");
		write_file(SETTINGS_FILE, balanced_b4, sizeof(balanced_b4) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(1, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Balanced") &&
			!strcmp(getenv("HALO_VITA_SHADOWS"), "0") && !strcmp(getenv("HALO_SUN_RAYS"), "0") &&
			!strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "4") && !strcmp(getenv("HALO_MODEL_LOD_SCALE"), "0.75") &&
			!strcmp(getenv("HALO_PARTICLE_BUDGET"), "reduced") && !strcmp(getenv("HALO_VITA_EFFECTS_QUALITY"), "2"),
			"a Balanced settings.txt of 1.1.0-beta.4's test builds (shadows near, sun rays, 8 lights, Full particles): "
			"the redone Balanced (none, off, 4, Reduced; Medium models, Full effects)");
		if (report[0])
			printf("(%s)\n", report);
		write_file(SETTINGS_FILE, quality_b4, sizeof(quality_b4) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(2, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Quality") &&
			!strcmp(getenv("HALO_VITA_SHADOWS"), "1") && !strcmp(getenv("HALO_MAX_SCENE_LIGHTS"), "8") &&
			!strcmp(getenv("HALO_SCENERY_UPDATE_DIVISOR"), "2") && !strcmp(getenv("HALO_SOUND_OBSTRUCTION_TICKS"), "3"),
			"a Quality settings.txt of 1.1.0-beta.4's test builds (the Xbox's rows): the redone Quality (shadows near, "
			"8 lights, scenery every 2nd tick, occlusion every 3rd)");
		if (report[0])
			printf("(%s)\n", report);
		write_file(SETTINGS_FILE, quality_11, sizeof(quality_11) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(2, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Quality") &&
			!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "0") && !strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1"),
			"a Quality settings.txt of the 1.1 betas (100%, AI every tick): Quality, Dynamic from 75%, Adaptive AI");
		if (report[0])
			printf("(%s)\n", report);
		write_file(SETTINGS_FILE, performance_11, sizeof(performance_11) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(0, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Performance") &&
			!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5"),
			"a Performance settings.txt of 1.1.0-beta.4's first builds (50%, Particles): Performance, Dynamic from 50%");
		if (report[0])
			printf("(%s)\n", report);

		/* Custom: the player's rows, as saved */
		write_file(SETTINGS_FILE, custom_11, sizeof(custom_11) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(!strcmp(settings[0].names[settings[0].choice], "Custom") && !strcmp(getenv("HALO_RENDER_SCALE"), "0.75") &&
			!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5") && !strcmp(getenv("HALO_AI_THINK_DIVISOR"), "1") &&
			!strcmp(getenv("HALO_MODEL_LOD_SCALE"), "0.75"),
			"a Custom settings.txt: Custom, its own rows kept (75%, minimum 50%, AI every tick, Medium models)");
		write_file(SETTINGS_FILE, custom_dynamic, sizeof(custom_dynamic) - 1);
		profile_rows_fresh();
		vita_settings_load();
		check(!strcmp(settings[0].names[settings[0].choice], "Custom") && !strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") &&
			!strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") && !strcmp(getenv("HALO_MODEL_LOD_SCALE"), "0.75"),
			"a Custom settings.txt, Dynamic from 75% on Balanced's rows: Custom, its minimum kept");

		/* env.txt's rows stand: a 1.0.3 Quality file (no AI row) with env.txt
		naming the AI's think rate and the minimum - those as env.txt has
		them, the profile Custom; the others Quality's */
		write_file(SETTINGS_FILE, quality, sizeof(quality) - 1);
		profile_rows_fresh();
		setenv("HALO_AI_THINK_DIVISOR", "1", 1);
		setenv("HALO_DYNAMIC_RES_MIN", "0.5", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "1") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.5") &&
			!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_MODEL_LOD_SCALE"), "1") &&
			!strcmp(getenv("HALO_VITA_SHADOWS"), "1") && !strcmp(settings[0].names[settings[0].choice], "Custom"),
			"env.txt's AI every tick and minimum 50% over a Quality settings.txt: env.txt's, the rest Quality's, Custom");
		/* (and a row env.txt names that settings.txt names too: settings.txt's,
		as for any row - the 1.0.3 Balanced file's 75% under env.txt's 50%) */
		write_file(SETTINGS_FILE, balanced_103, sizeof(balanced_103) - 1);
		profile_rows_fresh();
		setenv("HALO_RENDER_SCALE", "0.5", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "0.75") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
			!strcmp(settings[0].names[settings[0].choice], "Custom"),
			"env.txt naming the render resolution over a Balanced settings.txt: not moved by the profile (settings.txt's), Custom");
		/* (env.txt's HALO_PROFILE: that profile's rows, under the ones it
		names itself) */
		unlink(SETTINGS_FILE);
		profile_rows_fresh();
		setenv("HALO_PROFILE", "quality", 1);
		setenv("HALO_AI_THINK_DIVISOR", "1", 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.75") &&
			!strcmp(getenv("HALO_AI_THINK_DIVISOR"), "1") && !strcmp(settings[0].names[settings[0].choice], "Custom"),
			"env.txt's HALO_PROFILE=quality: Dynamic from 75%, its own AI every tick, Custom");
		profile_rows_fresh();
		vita_settings_load();
		check(profile_rows_are(1, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Balanced"),
			"no settings.txt, no env.txt: Balanced");

		/* a settings.txt of the Graphics tab's Display and Advanced pages
		(before the regrouping: its rows in that order, every one moved
		since set apart from its default) loads as it was: the rows are
		their variables, wherever the panel shows them */
		{
			static const struct
			{
				const char *variable, *value;
			} saved[] = {
				{ "HALO_PROFILE", "custom" }, { "HALO_RENDER_SCALE", "0.75" }, { "HALO_DYNAMIC_RES_MIN", "0.5" },
				{ "HALO_FRAME_CAP", "60" }, { "HALO_INTERPOLATION", "true" }, { "HALO_FRAMERATE_COUNTER", "1" },
				{ "HALO_INTERPOLATE_FIRST_PERSON", "0" }, { "HALO_PARTICLE_BUDGET", "low" },
				{ "HALO_DISPLAY_WIDTH", "640" }, { "HALO_UPSCALE_FILTER", "1" }, { "HALO_FIRST_PERSON_LEGS", "0" },
				{ "HALO_MODEL_LOD_SCALE", "0.35" }, { "HALO_MIN_OBJECT_PIXELS", "12" }, { "HALO_VITA_SHADOWS", "2" },
				{ "HALO_MAX_SCENE_LIGHTS", "2" }, { "HALO_VITA_EFFECTS_QUALITY", "0" },
				{ "HALO_PARTICLE_RENDER_DIVISOR", "3" }, { "HALO_SUN_RAYS", "1" }, { "HALO_DECAL_MIN_PIXELS", "2" },
				{ "HALO_SCENERY_UPDATE_DIVISOR", "2" }, { "HALO_LIGHTING_REFRESH_DIVISOR", "1" },
				{ "HALO_AI_THINK_DIVISOR", "3" }, { "HALO_CPU3_AUX", "1" },
			};
			static const char *const defaults[][2] = {
				{ "HALO_FRAME_CAP", "30" }, { "HALO_INTERPOLATION", "false" }, { "HALO_FRAMERATE_COUNTER", "0" },
				{ "HALO_INTERPOLATE_FIRST_PERSON", "1" }, { "HALO_DISPLAY_WIDTH", "848" }, { "HALO_UPSCALE_FILTER", "0" },
				{ "HALO_FIRST_PERSON_LEGS", "1" }, { "HALO_DECAL_MIN_PIXELS", "0" }, { "HALO_CPU3_AUX", "2" },
			};
			char text[1024];
			int length = 0, all = 1;

			for (index = 0; index < (int)(sizeof(saved) / sizeof(saved[0])); index++)
				length += snprintf(text + length, sizeof(text) - length, "%s=%s\n", saved[index].variable, saved[index].value);
			write_file(SETTINGS_FILE, text, (size_t)length);
			profile_rows_fresh();
			vita_settings_load();
			for (index = 1; index < (int)(sizeof(saved) / sizeof(saved[0])); index++)
			{
				const struct setting *setting = setting_named(saved[index].variable);

				if (!setting || strcmp(setting->values[setting->choice], saved[index].value) != 0 ||
					!getenv(saved[index].variable) || strcmp(getenv(saved[index].variable), saved[index].value) != 0)
				{
					printf("not as saved: %s=%s\n", saved[index].variable, saved[index].value);
					all = 0;
				}
			}
			check(all && !strcmp(settings[0].names[settings[0].choice], "Custom"),
				"a settings.txt saved with the Display and Advanced pages: every row as saved (now on Video and its Detail "
				"and Performance, and Game), Custom");
			/* (back to the defaults) */
			for (index = 0; index < (int)(sizeof(defaults) / sizeof(defaults[0])); index++)
				vita_settings_set(defaults[index][0], defaults[index][1]);
			unlink(SETTINGS_FILE);
			profile_rows_fresh();
			vita_settings_load();
			restart_pending = 0;
			for (index = 0; index < (int)(sizeof(defaults) / sizeof(defaults[0])); index++)
				if (strcmp(getenv(defaults[index][0]), defaults[index][1]) != 0)
					all = 0;
			check(all && profile_rows_are(1, report, sizeof(report)) && !strcmp(settings[0].names[settings[0].choice], "Balanced"),
				"... and the defaults again: Balanced");
		}
	}

	/* a settings.txt with Dynamic loads */
	{
		static const char dynamic[] = "HALO_RENDER_SCALE=dynamic\nHALO_DYNAMIC_RES_MIN=0.625\nHALO_MODEL_LOD_SCALE=0.5\n";

		write_file(SETTINGS_FILE, dynamic, sizeof(dynamic) - 1);
		vita_settings_load();
		check(!strcmp(getenv("HALO_RENDER_SCALE"), "dynamic") && !strcmp(getenv("HALO_DYNAMIC_RES_MIN"), "0.625") &&
			!strcmp(settings[1].names[settings[1].choice], "Dynamic"), "a settings.txt with Dynamic loads");
	}

	/* PR #7's message overlay (a join refused): wrapped to the menu's width,
	not dismissed by the press that was down when it opened, and taking the
	pad until a new Cross or Circle (tools/test_vita_message.py's checks) */
	{
		static const char text[] = "You are on version 5. The host is on version 9. Please install the updated build.";
		const char *at;
		int longest = 0;

		vita_settings_message("Join rejected", text);
		check(frame(VITA_BUTTON_CROSS) == 1 && menu_visible && strstr(menu, "Join rejected"),
			"message: shown, and the press that was down does not reach the game");
		for (at = menu; *at; )
		{
			int length = (int)strcspn(at, "\n");

			longest = length > longest ? length : longest;
			at += length + (at[length] == '\n');
		}
		check(longest <= 46, "message: lines of 46 characters at most");
		check(frame(VITA_BUTTON_CROSS) == 1 && menu_visible, "message: a held Cross does not close it");
		check(frame(0) == 1 && frame(VITA_BUTTON_CIRCLE) == 1 && !menu_visible, "message: a new Circle closes it");
		check(frame(0) == 0, "message: then the game has the pad again");
	}

	test_controls_tab();
	test_gyro_page();
	test_button_icons();
	test_game_text_input();
	test_updates();
	/* (the languages' pages with an update found: its lines in Spanish too) */
	update_status_value = UPDATE_STATUS_AVAILABLE;
	snprintf(update_version_value, sizeof(update_version_value), "1.1.0-beta.4");
	test_languages();
	test_menu_style_row();
	test_restart_offer();
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
