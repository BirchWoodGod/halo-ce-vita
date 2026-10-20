/*
NETWORK_ANTICHEAT_TEST.C

The host's anti-cheat (port/linux/game/network_anticheat.c), built as the
game builds it, with stand-ins for the server and the netcode around it.
Checked:

- weights: the defaults; network.anticheat_weights read in order, a value
  missing or not a number keeping its kind's default;
- a machine's score: each refusal its kind's weight, falling by
  network.anticheat_decay a second; dropped once past
  network.anticheat_kick_score (every machine told, the drop logged), and
  counted once;
- what a poor link sends (weight 0) never drops, however much; nor what
  came in a LAN's datagrams (unverified), however much;
- network.anticheat false: counted, never dropped;
- the machine dropped kept out by its real address (an internet play peer's
  endpoint: a new stand-in of the same machine refused), its stand-in or
  its hardware id, for network.anticheat_ban_minutes, then let in; another
  machine let in; 0 minutes: not kept out;
- the host's KICK (not kept out), BAN for the session (kept out until Clear
  bans), BAN for good (bans.txt); none of a machine not a client;
- another machine at a dropped one's index begins with nothing.

Prints PASS or the failures. run_network_anticheat_test.sh builds it (with
ASan and UBSan too).
*/

#include "cseries.h"

#include "network_anticheat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- stand-ins */

static unsigned long clock_ms = 100000;
static long game_ticks = 30;
static int failures;
static char weights_setting[128];
static long kick_score_setting = 100, decay_setting = 4, ban_minutes_setting = 10;
static int anticheat_setting = 1;
static short connection = 2;

/* the machines: their addresses (host byte order), hardware ids, whether
each is a client in the game, and what the server was asked */
static unsigned long addresses[128];
static char hardware_ids[128][40];
static int clients[128];
static long kicks[128];
static int kept_out_kicks[128];
static int notices, ban_lines;
static char last_notice[256];

/* the stand-ins' real addresses: 100.64.0.0/10 -> 203.0.113.<last byte> */
unsigned long network_distributed_real_address(unsigned long address)
{
	if ((address & 0xFFC00000UL) == 0x64400000UL)
		return 0xCB007100UL | (address & 0xFF);
	return address;
}

void network_distributed_machine_names(long machine_index, char *names, long size)
{
	snprintf(names, (size_t)size, "Player %ld", machine_index);
}

void network_distributed_note_drop(long machine_index, char const *names, char const *reason, char const *notice,
	boolean ban_file)
{
	(void)machine_index;
	(void)names;
	(void)reason;
	notices++;
	ban_lines += ban_file != 0;
	snprintf(last_notice, sizeof(last_notice), "%s", notice);
}

boolean network_game_server_machine_is_remote_client(long machine_index)
{
	return machine_index >= 0 && machine_index < 128 && clients[machine_index];
}

void network_game_server_kick_machine(long machine_index, boolean kept_out)
{
	kicks[machine_index]++;
	kept_out_kicks[machine_index] += kept_out != 0;
	clients[machine_index] = 0;
}

unsigned long network_game_server_machine_address(long machine_index)
{
	return machine_index >= 0 && machine_index < 128 ? addresses[machine_index] : 0;
}

char const *network_game_server_machine_hardware_id(long machine_index)
{
	return machine_index >= 0 && machine_index < 128 ? hardware_ids[machine_index] : "";
}

unsigned long system_milliseconds(void)
{
	return clock_ms;
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	int length = 0;

	for (; source && *source && length < size - 1; source++)
		if ((*source >= '0' && *source <= '9') || (*source >= 'a' && *source <= 'f'))
			destination[length++] = *source;
	destination[length] = 0;
}

short game_connection(void)
{
	return connection;
}

long game_time_get(void)
{
	return game_ticks;
}

int config_boolean(const char *name)
{
	return !strcmp(name, "network.anticheat") ? anticheat_setting : 0;
}

long config_integer(const char *name)
{
	if (!strcmp(name, "network.anticheat_kick_score"))
		return kick_score_setting;
	if (!strcmp(name, "network.anticheat_decay"))
		return decay_setting;
	if (!strcmp(name, "network.anticheat_ban_minutes"))
		return ban_minutes_setting;
	return 0;
}

const char *config_string(const char *name)
{
	return !strcmp(name, "network.anticheat_weights") ? weights_setting : "";
}

void error(short priority, char const *format, ...)
{
	(void)priority;
	(void)format;
}

/* (the game's stdio and strings: port/linux/include/stdio.h's, cseries') */
#undef snprintf
#undef printf
#undef strcmp
#undef vsnprintf
#undef vprintf
int halo_linux_snprintf(char *buffer, size_t count, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_printf(const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vprintf(format, arguments);
	va_end(arguments);
	return result;
}

/* (cseries.h names memset, memcpy and strcmp its own: written out here) */
long csstrcmp(const char *s1, const char *s2)
{
	for (; *s1 && *s1 == *s2; s1++, s2++)
		;
	return (long)(unsigned char)*s1 - (long)(unsigned char)*s2;
}

void *csmemset(void *buffer, long c, unsigned long size)
{
	unsigned char *bytes = (unsigned char *)buffer;

	while (size--)
		*bytes++ = (unsigned char)c;
	return buffer;
}

void *csmemcpy(void *destination, void const *source, unsigned long size)
{
	unsigned char *to = (unsigned char *)destination;
	unsigned char const *from = (unsigned char const *)source;

	while (size--)
		*to++ = *from++;
	return destination;
}

/* (friends: a ban blocks the player's friend identity) */
static int friend_blocks;
int p2p_friends_player_ask(int machine, int intent)
{
	(void)machine;
	(void)intent;
	friend_blocks++;
	return 0;
}

/* (the panel's: not under test here) */
struct network_game_server *global_network_game_server_get(void)
{
	return NULL;
}
struct network_game *network_game_server_get_game(struct network_game_server *server)
{
	(void)server;
	return NULL;
}
boolean network_player_is_valid(void const *player)
{
	(void)player;
	return FALSE;
}
char player_name_character_ascii(wchar_t character)
{
	return (char)character;
}
volatile int halo_host_players_serial;
volatile int halo_host_players_count;
volatile int halo_host_players_machines[16];
char halo_host_players_names[16][16];
volatile int halo_host_session_bans;
volatile int halo_host_player_request;
volatile int halo_host_player_answer;

/* ---------- the test */

static void check(int condition, const char *what)
{
	if (!condition)
	{
		failures++;
		printf("FAIL %s\n", what);
	}
}

/* machine at an index: a client in the game, its address and hardware id */
static void machine(long index, unsigned long address, const char *hardware_id)
{
	addresses[index] = address;
	snprintf(hardware_ids[index], sizeof(hardware_ids[index]), "%s", hardware_id);
	clients[index] = 1;
	kicks[index] = 0;
	kept_out_kicks[index] = 0;
}

static void new_game(void)
{
	network_anticheat_new_game();
	notices = ban_lines = 0;
}

int main(void)
{
	long counts[NUMBER_OF_ANTICHEAT_KINDS], dropped;
	short index;

	/* the score and the drop: four forged hits at once */
	new_game();
	machine(1, 0x64400005UL, "00112233445566778899aabbccddeeff");
	for (index = 0; index < 3; index++)
		network_anticheat_violation(1, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[1] == 0, "three forged hits (75): not dropped");
	network_anticheat_violation(1, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[1] == 1 && kept_out_kicks[1] == 0 && notices == 1 && strstr(last_notice, "forged hit"),
		"the fourth (100): dropped, every machine told why, kept out by the anti-cheat's own list");
	network_anticheat_violation(1, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[1] == 1 && notices == 1, "a machine dropped is dropped once");
	network_anticheat_statistics(counts, &dropped);
	check(counts[_anticheat_hit_forged] == 4 && dropped == 1, "the game's counts: 4 forged (the fifth after the drop not), 1 dropped");

	/* kept out: its real address (a new stand-in), its hardware id; another not */
	check(network_anticheat_refused(0x64400005UL, ""), "kept out: its stand-in");
	check(network_anticheat_refused(0x64512305UL, ""), "kept out: a new stand-in of the same real address");
	check(network_anticheat_refused(0x64512399UL, "00112233445566778899aabbccddeeff"), "kept out: its hardware id");
	check(!network_anticheat_refused(0x64512399UL, "ffffffffffffffffffffffffffffffff"), "another machine let in");
	clock_ms += 9 * 60000;
	check(network_anticheat_refused(0x64512305UL, ""), "still kept out after 9 minutes");
	clock_ms += 61000;
	check(!network_anticheat_refused(0x64512305UL, ""), "let in after network.anticheat_ban_minutes (10)");

	/* decay: forged hits slower than the score falls never drop */
	new_game();
	machine(2, 0x0A0A0202UL, "");
	for (index = 0; index < 40; index++)
	{
		network_anticheat_violation(2, _anticheat_hit_wall, TRUE, 1.0f);
		clock_ms += 2000;
	}
	check(kicks[2] == 0, "a hit through a wall every 2 s (6 against 8 fallen): never dropped");
	for (index = 0; index < 40 && !kicks[2]; index++)
	{
		network_anticheat_violation(2, _anticheat_hit_wall, TRUE, 1.0f);
		clock_ms += 500;
	}
	check(kicks[2] == 1 && index <= 30, "two a second (12 against 4): dropped within 30");

	/* a poor link's refusals, and a LAN's datagrams: never dropped */
	new_game();
	machine(3, 0x0A0A0303UL, "");
	for (index = 0; index < 1000; index++)
		network_anticheat_violation(3, _anticheat_hit_refused, TRUE, 1.0f);
	check(kicks[3] == 0, "1000 refusals of a poor link's kind: not dropped");
	for (index = 0; index < 100; index++)
		network_anticheat_violation(3, _anticheat_move, FALSE, 1.0f);
	check(kicks[3] == 0, "a LAN's datagrams past the limit (unverified): not dropped");
	check(!network_anticheat_datagram_verified(3) && (machine(4, 0x64400044UL, ""), network_anticheat_datagram_verified(4)),
		"datagrams verified only of an internet play peer (its tunnel)");

	/* weights read in order; a bad one keeps its default */
	snprintf(weights_setting, sizeof(weights_setting), "50,x,,1");
	new_game();
	machine(5, 0x0A0A0505UL, "");
	network_anticheat_violation(5, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[5] == 0, "a forged hit of weight 50: not yet");
	network_anticheat_violation(5, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[5] == 1, "the second of weight 50: dropped (network.anticheat_weights' first)");
	machine(6, 0x0A0A0606UL, "");
	for (index = 0; index < 9; index++)
		network_anticheat_violation(6, _anticheat_hit_weapon, TRUE, 1.0f);
	check(kicks[6] == 0, "a weapon not held: \"x\" keeps its 10 (9: 90)");
	network_anticheat_violation(6, _anticheat_hit_weapon, TRUE, 1.0f);
	check(kicks[6] == 1, "... the tenth drops");
	weights_setting[0] = 0;

	/* off: counted, never dropped */
	anticheat_setting = 0;
	new_game();
	machine(7, 0x0A0A0707UL, "");
	for (index = 0; index < 20; index++)
		network_anticheat_violation(7, _anticheat_hit_forged, TRUE, 1.0f);
	network_anticheat_statistics(counts, &dropped);
	check(kicks[7] == 0 && counts[_anticheat_hit_forged] == 20 && !network_anticheat_enabled(),
		"network.anticheat false: 20 forged hits counted, not dropped");
	anticheat_setting = 1;

	/* 0 minutes: dropped, not kept out */
	ban_minutes_setting = 0;
	new_game();
	machine(8, 0x0A0A0808UL, "8888");
	for (index = 0; index < 4; index++)
		network_anticheat_violation(8, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[8] == 1 && !network_anticheat_refused(0x0A0A0808UL, "8888"), "0 minutes: dropped, may join again");
	ban_minutes_setting = 10;

	/* the host's own */
	network_anticheat_clear_session_bans();
	new_game();
	machine(9, 0x64400009UL, "9999");
	friend_blocks = 0;
	check(network_anticheat_drop(9, _anticheat_drop_kick, NULL) && kicks[9] == 1 && friend_blocks == 0 &&
		!network_anticheat_refused(0x64400009UL, "9999"), "KICK: dropped, may join again, its friend identity left be");
	machine(9, 0x64400009UL, "9999");
	friend_blocks = 0;
	check(network_anticheat_drop(9, _anticheat_drop_session_ban, NULL) && kicks[9] == 1 && friend_blocks == 1 &&
		network_anticheat_refused(0x64433309UL, "") && network_anticheat_session_ban_count() == 1,
		"BAN: dropped, kept out (a new stand-in too) while the host runs, its friend identity blocked");
	clock_ms += 24 * 3600000UL;
	check(network_anticheat_refused(0x64400009UL, ""), "... a day later still");
	network_anticheat_clear_session_bans();
	check(!network_anticheat_refused(0x64400009UL, "9999") && network_anticheat_session_ban_count() == 0,
		"Clear bans: let in");
	machine(10, 0x0A0A0A0AUL, "");
	check(network_anticheat_drop(10, _anticheat_drop_ban, NULL) && ban_lines == 1 && kept_out_kicks[10] == 1,
		"BAN for good: a line in bans.txt");
	check(!network_anticheat_drop(11, _anticheat_drop_kick, NULL), "no KICK of a machine not a client");

	/* another machine at a dropped one's index begins with nothing */
	new_game();
	machine(12, 0x0A0A0C0CUL, "");
	for (index = 0; index < 3; index++)
		network_anticheat_violation(12, _anticheat_hit_forged, TRUE, 1.0f);
	machine(12, 0x0A0A0C0DUL, "");
	network_anticheat_violation(12, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[12] == 0, "a new machine at the index: its own count (25), not the last one's");

	/* a client of a game (not the host) counts nothing */
	connection = 1;
	new_game();
	machine(13, 0x0A0A0D0DUL, "");
	for (index = 0; index < 10; index++)
		network_anticheat_violation(13, _anticheat_hit_forged, TRUE, 1.0f);
	check(kicks[13] == 0, "a client counts nothing (the host's alone)");
	connection = 2;

	if (failures)
		printf("%d FAILED\n", failures);
	else
		printf("PASS\n");
	return failures != 0;
}
