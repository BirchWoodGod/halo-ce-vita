/*
NETWORK_ANTICHEAT.C

The host's answer to a modified client (network_anticheat.h; port/linux/
NETCODE.md, "A modified client").

A client machine is anyone's program: a PC player can change anything in
their game's memory, and a custom client send anything at all. The host
already decides everything that matters (damage, deaths, scores, pickups,
spawns, the game's objects; its checks of a client's hits and moves in
network_damage.c, network_distributed.c and network_objects.c), so what a
modified client sends is refused there and changes nothing. Here what is
refused is counted, to drop the machine that keeps sending it:

- Each refusal has a weight (network.anticheat_weights): what only a
  modified client sends weighs much (a hit report of damage harder than its
  weapon deals, of a weapon never held, through a wall); what a client on a
  poor link sends too weighs little or nothing (a hit out of reach of where
  the host had the shooter: the host's view is a round trip behind), and a
  player's moves count only when refused for seconds on end, which an
  honest client's never are (the host's correction brings it back within a
  round trip).
- A machine's score falls by network.anticheat_decay a second, and one past
  network.anticheat_kick_score is dropped: every machine told who and why,
  a line in cheaters.txt, and kept out of this host's games for
  network.anticheat_ban_minutes (by its real address, an internet play
  peer's endpoint rather than its stand-in address, which a new run of its
  game changes, and by its hardware id): its code and its invite join it to
  nothing meanwhile.
- Only what came over the machine's own stream, or in a datagram internet
  play's tunnel sealed to it, can drop a machine: a LAN's datagram can come
  from any machine on it as from another's address, so it is counted and
  logged, never acted on (as the speed hack's check, distributed_note_client_clock).
- Every action is logged: the violations of each kind once a second at most
  per machine (debug.txt), every drop and ban (cheaters.txt, and bans.txt
  for a ban that lasts).

The host's own KICK, BAN FOR THIS SESSION and BAN (the PLAYERS screen, the
settings panel's Players page) go through network_anticheat_drop.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "networking/network_game_globals.h"

#include "game/players.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"

#include "network_anticheat.h"
#include "network_distributed.h"
#include "../src/port_config.h"
#include "../src/system_link_shortcut.h"
#include "../src/p2p_friends.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* network_distributed.c's */
void network_distributed_machine_names(long machine_index, char *names, long size);
void network_distributed_note_drop(long machine_index, char const *names, char const *reason, char const *notice,
	boolean ban_file);
unsigned long network_distributed_real_address(unsigned long address);
/* network_server_manager.c's, cseries_windows.c's, p2p.c's */
boolean network_game_server_machine_is_remote_client(long machine_index);
void network_game_server_kick_machine(long machine_index, boolean kept_out);
unsigned long network_game_server_machine_address(long machine_index);
char const *network_game_server_machine_hardware_id(long machine_index);
unsigned long system_milliseconds(void);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* ---------- constants */

enum
{
	/* the machines kept out at once (the oldest gives way) */
	MAXIMUM_SESSION_BANS = 64,
	/* (the players one person's address may have in a game: a machine's
	own four at least, F-11) */
	MAXIMUM_PLAYERS_PER_ADDRESS_FLOOR = 4,
	/* a machine's violations logged no more often than this (milliseconds) */
	VIOLATION_LOG_MILLISECONDS = 1000,
	HARDWARE_ID_SIZE = 33,
};

/* each kind's weight, as network.anticheat_weights's defaults have them:
a few forged hits (or a few seconds of them through walls, of an empty
weapon) drop a machine; what a poor link sends never does (a weight of 0,
or less than the score falls a second at any rate a game sends it) */
static real const anticheat_default_weights[NUMBER_OF_ANTICHEAT_KINDS] =
{
	/* forged */ 25.0f,
	/* weapon */ 10.0f,
	/* wall */ 6.0f,
	/* empty */ 4.0f,
	/* part */ 6.0f,
	/* refused */ 0.0f,
	/* rate */ 0.25f,
	/* move */ 10.0f,
	/* move_wall */ 1.0f,
	/* vehicle_move */ 10.0f,
	/* forge */ 2.0f,
};

static char const *const anticheat_kind_names[NUMBER_OF_ANTICHEAT_KINDS] =
{
	"forged hit", "hit of a weapon not held", "hit through a wall", "hit of an empty weapon",
	"hit of a part far from the shot", "hit refused", "hits past the weapon's rate", "moves refused",
	"moves through walls", "vehicle moves refused", "Forge requests malformed",
};

/* ---------- globals */

static struct
{
	boolean read;
	boolean enabled;
	real kick_score;
	real decay;
	long ban_minutes;
	real weights[NUMBER_OF_ANTICHEAT_KINDS];
} anticheat_settings;

static struct anticheat_machine
{
	/* the machine's address when it was counted (another at its index
	begins again) */
	unsigned long address;
	real score;
	unsigned long score_time;
	/* (on datagrams' word alone: logged, never acted on) */
	real unverified_score;
	boolean dropped;
	boolean unverified_logged;
	long counts[NUMBER_OF_ANTICHEAT_KINDS];
	long logged_counts[NUMBER_OF_ANTICHEAT_KINDS];
	unsigned long logged_time;
} anticheat_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

static struct anticheat_ban
{
	/* real address (host byte order), the stand-in it had, its hardware id;
	until when (system_milliseconds; 0 for the session) */
	unsigned long address;
	unsigned long stand_in;
	char hardware_id[HARDWARE_ID_SIZE];
	unsigned long until;
	boolean used;
} anticheat_bans[MAXIMUM_SESSION_BANS];
static short anticheat_ban_next;

static long anticheat_counts[NUMBER_OF_ANTICHEAT_KINDS];
static long anticheat_kicks;

/* ---------- private code */

/* the settings, read once a game (a weight list "a,b,c,...", each kind's in
network_anticheat.h's order; one missing or not a number keeps its default) */
static void anticheat_read_settings(
	void)
{
	char const *weights = config_string("network.anticheat_weights");
	short kind;

	anticheat_settings.read = TRUE;
	anticheat_settings.enabled = config_boolean("network.anticheat") != 0;
	anticheat_settings.kick_score = (real)config_integer("network.anticheat_kick_score");
	anticheat_settings.decay = (real)config_integer("network.anticheat_decay");
	anticheat_settings.ban_minutes = config_integer("network.anticheat_ban_minutes");
	if (!(anticheat_settings.kick_score >= 1.0f))
		anticheat_settings.kick_score = 100.0f;
	if (!(anticheat_settings.decay >= 0.0f))
		anticheat_settings.decay = 0.0f;
	if (anticheat_settings.ban_minutes < 0)
		anticheat_settings.ban_minutes = 0;
	if (anticheat_settings.ban_minutes > 24 * 60)
		anticheat_settings.ban_minutes = 24 * 60;
	for (kind = 0; kind < NUMBER_OF_ANTICHEAT_KINDS; kind++)
		anticheat_settings.weights[kind] = anticheat_default_weights[kind];
	for (kind = 0; weights && *weights && kind < NUMBER_OF_ANTICHEAT_KINDS; kind++)
	{
		char *end;
		double value = strtod(weights, &end);

		if (end != weights && value >= 0.0 && value <= 1000.0)
			anticheat_settings.weights[kind] = (real)value;
		weights = strchr(weights, ',');
		if (!weights)
			break;
		weights++;
	}
}

static void anticheat_settings_ready(
	void)
{
	if (!anticheat_settings.read)
		anticheat_read_settings();
}

/* the machine's record, begun again if another machine has its index */
static struct anticheat_machine *anticheat_machine_get(
	long machine_index)
{
	struct anticheat_machine *machine;
	unsigned long address;

	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		return NULL;
	machine = &anticheat_machines[machine_index];
	address = network_game_server_machine_address(machine_index);
	if (machine->address != address)
	{
		csmemset(machine, 0, sizeof(*machine));
		machine->address = address;
		machine->score_time = system_milliseconds();
	}
	return machine;
}

/* the machine's score fallen by the time since it was last counted */
static void anticheat_decay(
	struct anticheat_machine *machine)
{
	unsigned long now = system_milliseconds();
	real fallen = anticheat_settings.decay * (real)(now - machine->score_time) / 1000.0f;

	machine->score = machine->score > fallen ? machine->score - fallen : 0.0f;
	machine->unverified_score = machine->unverified_score > fallen ? machine->unverified_score - fallen : 0.0f;
	machine->score_time = now;
}

/* the machine's violations since they were last logged, once a second at
most (and at once when it is dropped) */
static void anticheat_log_violations(
	long machine_index,
	struct anticheat_machine *machine,
	boolean now)
{
	char line[512];
	int length = 0;
	short kind;

	if (!now && system_milliseconds() - machine->logged_time < VIOLATION_LOG_MILLISECONDS)
		return;
	for (kind = 0; kind < NUMBER_OF_ANTICHEAT_KINDS && length < (int)sizeof(line) - 64; kind++)
	{
		long count = machine->counts[kind] - machine->logged_counts[kind];

		if (count <= 0)
			continue;
		length += snprintf(line + length, sizeof(line) - length, "%s%ld %s", length ? ", " : "", count,
			anticheat_kind_names[kind]);
		machine->logged_counts[kind] = machine->counts[kind];
	}
	if (!length)
		return;
	machine->logged_time = system_milliseconds();
	error(_error_log, "anticheat: machine #%ld: %s (score %.1f of %.0f%s)", machine_index, line, machine->score,
		anticheat_settings.kick_score, machine->unverified_score > 0.0f ? ", some of it unverified" : "");
}

/* the most weighty kind the machine was counted for, for why it was dropped */
static short anticheat_main_kind(
	struct anticheat_machine const *machine)
{
	short kind;
	short main = NONE;
	real most = 0.0f;

	for (kind = 0; kind < NUMBER_OF_ANTICHEAT_KINDS; kind++)
	{
		real weight = anticheat_settings.weights[kind] * (real)machine->counts[kind];

		if (weight > most)
		{
			most = weight;
			main = kind;
		}
	}
	return main;
}

/* a machine kept out: its real address, stand-in and hardware id, for the
minutes given (0: while this host runs) */
static void anticheat_ban_add(
	long machine_index,
	long minutes)
{
	struct anticheat_ban *ban = &anticheat_bans[anticheat_ban_next++ % MAXIMUM_SESSION_BANS];
	unsigned long address = network_game_server_machine_address(machine_index);

	csmemset(ban, 0, sizeof(*ban));
	ban->used = TRUE;
	ban->stand_in = address;
	ban->address = network_distributed_real_address(address);
	p2p_hardware_id_sanitize(ban->hardware_id, HARDWARE_ID_SIZE, network_game_server_machine_hardware_id(machine_index));
	/* (so that 0 is never a time: the session's) */
	ban->until = minutes > 0 ? (system_milliseconds() + (unsigned long)minutes * 60000UL) | 1 : 0;
}

/* ---------- public code */

boolean network_anticheat_enabled(
	void)
{
	anticheat_settings_ready();
	return anticheat_settings.enabled;
}

boolean network_anticheat_datagram_verified(
	long machine_index)
{
	unsigned long address = network_game_server_machine_address(machine_index);

	/* (internet play's stand-ins, 100.64.0.0/10 in host byte order: its
	tunnel seals each datagram to its peer) */
	return (address & 0xFFC00000UL) == 0x64400000UL;
}

void network_anticheat_violation(
	long machine_index,
	short kind,
	boolean verified,
	real amount)
{
	struct anticheat_machine *machine;
	real weight;

	if (kind < 0 || kind >= NUMBER_OF_ANTICHEAT_KINDS || !(amount > 0.0f) ||
		game_connection() != _game_connection_network_server)
	{
		return;
	}
	anticheat_settings_ready();
	machine = anticheat_machine_get(machine_index);
	if (!machine || machine->dropped)
		return;
	anticheat_counts[kind]++;
	machine->counts[kind]++;
	anticheat_decay(machine);
	weight = anticheat_settings.weights[kind] * amount;
	if (verified)
		machine->score += weight;
	else
		machine->unverified_score += weight;
	anticheat_log_violations(machine_index, machine, FALSE);
	if (!anticheat_settings.enabled)
		return;
	if (!verified)
	{
		/* (logged once: never acted on) */
		if (machine->unverified_score >= anticheat_settings.kick_score && !machine->unverified_logged)
		{
			machine->unverified_logged = TRUE;
			error(_error_log, "anticheat: machine #%ld's datagrams alone (a LAN's: any machine on it can send "
				"them as from another) are past the limit: not dropped (unverified)", machine_index);
		}
		return;
	}
	if (machine->score >= anticheat_settings.kick_score)
	{
		short main = anticheat_main_kind(machine);
		char reason[96];

		snprintf(reason, sizeof(reason), "%s (the host's anti-cheat limit)",
			main != NONE ? anticheat_kind_names[main] : "violations");
		anticheat_log_violations(machine_index, machine, TRUE);
		if (network_anticheat_drop(machine_index, NONE, reason))
			anticheat_kicks++;
	}
}

void network_anticheat_tick(
	void)
{
	long machine_index;

	if (game_time_get() % TICKS_PER_SECOND != 0)
		return;
	/* (what was counted and not logged yet) */
	for (machine_index = 0; machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine_index++)
	{
		struct anticheat_machine *machine = &anticheat_machines[machine_index];

		if (machine->address || machine->score > 0.0f)
			anticheat_log_violations(machine_index, machine, FALSE);
	}
}

void network_anticheat_new_game(
	void)
{
	short kind;
	long total = 0;

	for (kind = 0; kind < NUMBER_OF_ANTICHEAT_KINDS; kind++)
		total += anticheat_counts[kind];
	if (total || anticheat_kicks)
	{
		char line[512];
		int length = 0;

		for (kind = 0; kind < NUMBER_OF_ANTICHEAT_KINDS && length < (int)sizeof(line) - 64; kind++)
		{
			if (anticheat_counts[kind])
				length += snprintf(line + length, sizeof(line) - length, " %s %ld,", anticheat_kind_names[kind],
					anticheat_counts[kind]);
		}
		if (length)
			line[length - 1] = 0;
		else
			line[0] = 0;
		error(_error_log, "anticheat: the last game:%s; %ld machines dropped by the limit", line, anticheat_kicks);
	}
	csmemset(anticheat_machines, 0, sizeof(anticheat_machines));
	csmemset(anticheat_counts, 0, sizeof(anticheat_counts));
	anticheat_kicks = 0;
	anticheat_read_settings();
}

boolean network_anticheat_refused(
	unsigned long address,
	char const *hardware_id)
{
	char kept_hardware_id[HARDWARE_ID_SIZE];
	unsigned long real_address = network_distributed_real_address(address);
	unsigned long now = system_milliseconds();
	short index;

	p2p_hardware_id_sanitize(kept_hardware_id, HARDWARE_ID_SIZE, hardware_id ? hardware_id : "");
	for (index = 0; index < MAXIMUM_SESSION_BANS; index++)
	{
		struct anticheat_ban *ban = &anticheat_bans[index];

		if (!ban->used)
			continue;
		/* (its time over: let go) */
		if (ban->until && (long)(now - ban->until) >= 0)
		{
			ban->used = FALSE;
			continue;
		}
		if ((address && (address == ban->stand_in || (real_address && real_address == ban->address))) ||
			(kept_hardware_id[0] && !strcmp(kept_hardware_id, ban->hardware_id)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

unsigned long network_anticheat_person_address(
	unsigned long address)
{
	unsigned long real = address ? network_distributed_real_address(address) : 0;

	return real ? real : address;
}

long network_anticheat_address_player_limit(
	long maximum_players)
{
	long limit = (maximum_players + 1) / 2;

	return limit < MAXIMUM_PLAYERS_PER_ADDRESS_FLOOR ? MAXIMUM_PLAYERS_PER_ADDRESS_FLOOR : limit;
}

boolean network_anticheat_address_room(
	unsigned long address,
	unsigned long const *addresses,
	long const *players,
	long count,
	long maximum_players,
	long adding)
{
	unsigned long person = network_anticheat_person_address(address);
	long index, total = adding;

	if (!person)
		return TRUE;
	for (index = 0; index < count; index++)
	{
		if (addresses[index] && network_anticheat_person_address(addresses[index]) == person)
			total += players[index] > 0 ? players[index] : 1;
	}
	return total <= network_anticheat_address_player_limit(maximum_players);
}

boolean network_anticheat_drop(
	long machine_index,
	short how,
	char const *why)
{
	struct anticheat_machine *machine;
	char names[64];
	char reason[128];
	char notice[160];

	if (game_connection() != _game_connection_network_server && !global_network_game_server_get())
		return FALSE;
	if (!network_game_server_machine_is_remote_client(machine_index))
		return FALSE;
	anticheat_settings_ready();
	network_distributed_machine_names(machine_index, names, sizeof(names));
	switch (how)
	{
	case _anticheat_drop_kick:
		snprintf(reason, sizeof(reason), "kicked by the host%s%s", why ? ": " : "", why ? why : "");
		snprintf(notice, sizeof(notice), "%s kicked by the host", names);
		network_distributed_note_drop(machine_index, names, reason, notice, FALSE);
		network_game_server_kick_machine(machine_index, FALSE);
		break;
	case _anticheat_drop_session_ban:
		/* (kept out by anticheat_bans alone - its real address, stand-in
		and hardware id -, which Clear bans lets go; not by the server's
		own list of stand-ins, which lasts the session whatever) */
		snprintf(reason, sizeof(reason), "banned by the host for this session%s%s", why ? ": " : "", why ? why : "");
		snprintf(notice, sizeof(notice), "%s banned by the host", names);
		anticheat_ban_add(machine_index, 0);
		network_distributed_note_drop(machine_index, names, reason, notice, FALSE);
		network_game_server_kick_machine(machine_index, FALSE);
		break;
	case _anticheat_drop_ban:
		snprintf(reason, sizeof(reason), "banned by the host%s%s", why ? ": " : "", why ? why : "");
		snprintf(notice, sizeof(notice), "%s banned by the host", names);
		anticheat_ban_add(machine_index, 0);
		network_distributed_note_drop(machine_index, names, reason, notice, TRUE);
		network_game_server_kick_machine(machine_index, TRUE);
		break;
	default:
		/* (the limit: kept out for network.anticheat_ban_minutes, by
		anticheat_bans alone; 0, not kept out) */
		snprintf(reason, sizeof(reason), "dropped: %s", why ? why : "the host's anti-cheat limit");
		snprintf(notice, sizeof(notice), "%s dropped by the host: %s", names, why ? why : "cheating");
		if (anticheat_settings.ban_minutes > 0)
			anticheat_ban_add(machine_index, anticheat_settings.ban_minutes);
		network_distributed_note_drop(machine_index, names, reason, notice, FALSE);
		network_game_server_kick_machine(machine_index, FALSE);
		break;
	}
	/* (a ban: the player's friend identity blocked too, when their machine
	gives its friend card - at once if it gave it before - so that their
	friend requests, invites and party messages are dropped as well: friends'
	block, p2p_friends.c; nothing without internet play and friends on) */
	if (how == _anticheat_drop_session_ban || how == _anticheat_drop_ban)
	{
		int asked = p2p_friends_player_ask((int)machine_index, P2P_FRIENDS_PLAYER_BLOCK);

		error(_error_log, "anticheat: machine #%ld's friend identity %s", machine_index,
			asked == P2P_FRIENDS_DONE ? "blocked (or asked for, to block)" : "not known (no friend card)");
	}
	machine = anticheat_machine_get(machine_index);
	if (machine)
		machine->dropped = TRUE;
	return TRUE;
}

void network_anticheat_clear_session_bans(
	void)
{
	csmemset(anticheat_bans, 0, sizeof(anticheat_bans));
	anticheat_ban_next = 0;
	error(_error_log, "anticheat: the session's bans let go");
}

short network_anticheat_session_ban_count(
	void)
{
	unsigned long now = system_milliseconds();
	short count = 0;
	short index;

	for (index = 0; index < MAXIMUM_SESSION_BANS; index++)
	{
		if (anticheat_bans[index].used && (!anticheat_bans[index].until || (long)(now - anticheat_bans[index].until) < 0))
			count++;
	}
	return count;
}

void network_anticheat_statistics(
	long *counts,
	long *kicks)
{
	if (counts)
		csmemcpy(counts, anticheat_counts, sizeof(anticheat_counts));
	if (kicks)
		*kicks = anticheat_kicks;
}

/* (the main loop, each frame: system_link_shortcut.c) the settings panel's
Players page: the hosted game's other machines' players published twice a
second (none while this machine hosts nothing), and its KICK and BAN done */
void network_anticheat_panel_update(
	void)
{
	static unsigned long published;
	struct network_game_server *server = global_network_game_server_get();
	int request = __atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE);

	if (request)
	{
		short how = (short)(request >> 8);
		long machine_index = request & 0xFF;
		boolean done;

		if (how == HALO_HOST_PLAYER_CLEAR_BANS)
		{
			network_anticheat_clear_session_bans();
			done = TRUE;
		}
		else if (how == HALO_HOST_PLAYER_ADD_BOT || how == HALO_HOST_PLAYER_DROP_BOT)
		{
			boolean network_game_server_port_bots_command(char const *word, char const *arguments, char *answer,
				long answer_size);
			char answer[160];

			answer[0] = 0;
			network_game_server_port_bots_command(how == HALO_HOST_PLAYER_ADD_BOT ? "add_bot" : "drop_bot", NULL, answer,
				sizeof(answer));
			done = server != NULL && !network_game_is_splitscreen_local();
		}
		else
		{
			done = network_anticheat_drop(machine_index, how == HALO_HOST_PLAYER_KICK ? _anticheat_drop_kick :
				how == HALO_HOST_PLAYER_BAN ? _anticheat_drop_session_ban : _anticheat_drop_ban, "the settings panel");
		}
		error(_error_log, "anticheat: the settings panel's %s of machine #%ld: %s",
			how == HALO_HOST_PLAYER_KICK ? "kick" : how == HALO_HOST_PLAYER_CLEAR_BANS ? "clearing of the bans" :
			how == HALO_HOST_PLAYER_ADD_BOT ? "bot added" : how == HALO_HOST_PLAYER_DROP_BOT ? "bot dropped" : "ban",
			machine_index, done ? "done" : "not a player of the game");
		__atomic_store_n(&halo_host_player_answer, done ? HALO_HOST_ANSWER_DONE : HALO_HOST_ANSWER_GONE,
			__ATOMIC_RELEASE);
		__atomic_store_n(&halo_host_player_request, 0, __ATOMIC_RELEASE);
		published = 0;
	}
	if (published && system_milliseconds() - published < 500)
		return;
	published = system_milliseconds() | 1;
	__atomic_add_fetch(&halo_host_players_serial, 1, __ATOMIC_ACQ_REL);
	if (!server)
		halo_host_players_count = -1;
	else
	{
		struct network_game *game = network_game_server_get_game(server);
		long index;
		int count = 0;

		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < HALO_HOST_PLAYERS_MAXIMUM; index++)
		{
			struct network_player const *player = &game->players[index];
			long character;

			if (!network_player_is_valid(player) || !network_game_server_machine_is_remote_client(player->machine_index))
				continue;
			for (character = 0; character < HALO_HOST_PLAYER_NAME_SIZE - 1 && character < (long)NUMBEROF(player->name) &&
				player->name[character]; character++)
			{
				char ascii = player_name_character_ascii(player->name[character]);

				halo_host_players_names[count][character] = ascii >= 32 && ascii < 127 ? ascii : '?';
			}
			halo_host_players_names[count][character] = 0;
			halo_host_players_machines[count] = (int)player->machine_index;
			count++;
		}
		halo_host_players_count = count;
	}
	halo_host_session_bans = network_anticheat_session_ban_count();
	__atomic_add_fetch(&halo_host_players_serial, 1, __ATOMIC_ACQ_REL);
}
