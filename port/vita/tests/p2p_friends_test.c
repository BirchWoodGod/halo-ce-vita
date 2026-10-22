/*
P2P_FRIENDS_TEST.C

Friends (port/linux/src/p2p_friends.c, included whole, as the Vita builds
it: HALO_VITA) between three machines in one process (the module's state
swapped: friends), with a broker of this file's that carries each queued
message to the machines subscribed to its topic, and stand-ins for the rest
of internet play. Checked:

- codes: 12 characters of the alphabet, any case and dashes taken, the own
  code and garbage refused;
- a request to a code, accepted: both friends, the ACCEPT taken only by the
  requester, for its nonce; presence both ways, its activity line; a third
  machine reads nothing of theirs;
- a declined request: no friendship, nothing sent, the re-sent request
  muted; a blocked player's requests dropped, and blocking a friend tells
  them REMOVE;
- joinable only for a public game without a password, with room and
  Friends can join on; Join hands the invite link to internet play; a
  private or password game's invite never in presence; Invite: refused with
  a password, sent otherwise, its pop-up and Accept joining;
- appear offline: an offline presence, then nothing; the friend shows
  offline;
- replays (a presence, an invite, a REMOVE of an old friendship, an ACCEPT
  again) and forgeries (a flipped byte, another key's signature, another
  sender, another code's recipient, a presence with a recipient) dropped;
- the files: written and read back the same; a list that fails its checks
  (a flipped byte, a cut, a bad count, a duplicate key, the machine's own
  key) set aside and not used, the .old one taken after a write cut short;
  the identity kept, its epoch newer each run;
- remove: the friend told, gone on both sides; a tombstone answering later
  messages with REMOVE again;
- the friend identity's backup (p2p_friends_backup.inc): a round trip to
  another machine (the same code, its friends list, its friend seeing it as
  the same friend; two machines with one identity: the newest run heard, the
  other's dropped as replays), the file's size and contents (no seed, no
  password in it; its key's parameters), a wrong password, a cut file, a
  flipped byte, a newer version (the file's and its contents'), what a file
  may ask of the key, names and passwords refused, the identity replaced
  kept, the code alone restored, the same identity restored on its own
  machine (a newer run), a save of the old list waiting not written; each
  job's thread made as the Vita sizes it (4 KiB for attributes with no
  size) and the most of its stack it used measured: four times that fits.

Prints PASS or the failures. run_p2p_friends_test.sh builds it (with ASan
and UBSan too).
*/

/* (the threads p2p_friends.c starts made with the stacks the Vita gives:
vita_pthread_create, below) */
#include <errno.h>
#include <pthread.h>
static int vita_pthread_attr_init(pthread_attr_t *attributes);
static int vita_pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size);
static int vita_pthread_attr_destroy(pthread_attr_t *attributes);
static int vita_pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*function)(void *),
	void *argument);
#define pthread_attr_init vita_pthread_attr_init
#define pthread_attr_setstacksize vita_pthread_attr_setstacksize
#define pthread_attr_destroy vita_pthread_attr_destroy
#define pthread_create vita_pthread_create

/* (the files in the test's folders, not ux0:) */
#define P2P_FRIENDS_TEST
#include "../../linux/src/p2p_friends.c"

#undef pthread_attr_init
#undef pthread_attr_setstacksize
#undef pthread_attr_destroy
#undef pthread_create

/* (the wires: which a friend's game is joined on, network_wire.h; this
machine's platform each test machine's own, below) */
#define network_wire_this_platform network_wire_this_platform_built
#include "../../linux/src/network_wire.c"
#undef network_wire_this_platform

#include <stdarg.h>
/* (the platform layer's headers keep sys/stat.h out) */
int mkdir(const char *path, unsigned int mode);
#include <unistd.h>
#include <dirent.h>

/* ---------- stand-ins */

DWORD D3D__RenderState[D3DRS_MAX];

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long clock_now = 1000;
static int failures;
static char save_root[512];
static char joined_link[P2P_LINK_SIZE];
static int join_count;
static int appear_offline_setting, friends_join_setting = 1;
/* (each machine's platform, network_wire.h: a Vita unless a check makes it
the port's PC build) */
static int current;
static int machine_platform[8];
int network_wire_this_platform(void)
{
	return machine_platform[current];
}

void posix_random_bytes(void *buffer, unsigned long size)
{
	FILE *file = fopen("/dev/urandom", "rb");

	if (!file || fread(buffer, 1, size, file) != size)
		abort();
	fclose(file);
}
int posix_make_directory(const char *path)
{
	return mkdir(path, 0755);
}
unsigned long p2p_now(void)
{
	return clock_now;
}
const char *platform_save_root(void)
{
	return save_root;
}
int config_boolean(const char *name)
{
	if (!strcmp(name, "friends.enabled"))
		return 1;
	if (!strcmp(name, "friends.appear_offline"))
		return appear_offline_setting;
	if (!strcmp(name, "friends.join"))
		return friends_join_setting;
	return 0;
}
static const char *lobby_name_setting = "";
const char *config_string(const char *name)
{
	return !strcmp(name, "network.lobby_name") ? lobby_name_setting : "";
}
static int quiet;
static int replay_logs;
void platform_log(const char *format, ...)
{
	va_list arguments;

	if (strstr(format, "dropped a replayed party message"))
		replay_logs++;
	if (quiet)
		return;
	va_start(arguments, format);
	printf("  log: ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}
void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}
int p2p_lobby_clean_name(char *name)
{
	int read, written = 0, visible = 0;

	for (read = 0; name[read]; read++)
	{
		char character = name[read];

		if (character == '|' || (character == ' ' && !written))
			continue;
		if (character != ' ')
			visible = 1;
		name[written++] = character;
	}
	while (written > 0 && name[written - 1] == ' ')
		written--;
	name[written] = 0;
	return visible;
}
const char *p2p_lobby_map_title(const char *map, int pc_map)
{
	if (!pc_map && !strcmp(map, "bloodgulch"))
		return "Blood Gulch";
	if (!pc_map && !strcmp(map, "a10"))
		return "The Pillar of Autumn";
	return map;
}
int p2p_join_invite(const char *text)
{
	snprintf(joined_link, sizeof(joined_link), "%s", text);
	join_count++;
	return 1;
}
/* (the wire of the game joined: its presence's, invite's or party's) */
static int joined_wire = -1;
/* (p2p_lobby.c's: a friend's game with a password opened with it) */
static int sealed_joins;
static char sealed_password[64];
static unsigned char sealed_hash[P2P_KEY_HASH_SIZE], sealed_key[P2P_KEY_SIZE], sealed_seal[P2P_SEALED_TOKEN_SIZE];
int p2p_lobby_join_sealed(const unsigned char *key_hash, const unsigned char *signing_key, const unsigned char *sealed,
	const char *password, int crossplay)
{
	(void)crossplay;
	memcpy(sealed_hash, key_hash, sizeof(sealed_hash));
	memcpy(sealed_key, signing_key, sizeof(sealed_key));
	memcpy(sealed_seal, sealed, sizeof(sealed_seal));
	snprintf(sealed_password, sizeof(sealed_password), "%s", password ? password : "");
	sealed_joins++;
	return 1;
}
int p2p_join_invite_on(const char *text, int crossplay)
{
	joined_wire = crossplay;
	return p2p_join_invite(text);
}

/* ---------- the machines and the broker */

enum
{
	MACHINES = 4,
	/* (two more for the backup's checks, apart from the broker's passes:
	test_backup) */
	ALL_MACHINES = MACHINES + 2,
	LOGGED = 256,
};

static struct friends_state machines[ALL_MACHINES];
static struct p2p_friends_host hosts[ALL_MACHINES];
static char roots[ALL_MACHINES][512];

/* every message the broker carried, as it was (replays) */
static struct
{
	char topic[TOPIC_LENGTH];
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	int size;
	int from;
} carried[LOGGED];
static int carried_count;
/* how many brokers carry each message (the game publishes through every
broker at once: each copy heard, the same signed message again) */
static int broker_copies = 1;

static void use(int machine)
{
	current = machine;
	friends = &machines[machine];
	snprintf(save_root, sizeof(save_root), "%s", roots[machine]);
	appear_offline_setting = 0;
}

static void check(int condition, const char *what)
{
	if (!condition)
	{
		failures++;
		printf("FAIL %s\n", what);
	}
}

/* a message to a machine's topics, as the broker would hand it on */
static void hand(int machine, const char *topic, const unsigned char *payload, int size)
{
	int was = current;

	use(machine);
	if (!strcmp(topic, friends->inbox_topic))
		p2p_friends_heard(P2P_FRIENDS_INBOX, payload, size);
	else if (!strcmp(topic, friends->mailbox_topic))
		p2p_friends_heard(P2P_FRIENDS_MAILBOX, payload, size);
	use(was);
}

/* each machine's queue carried to the others (and logged) */
static void broker(void)
{
	int from, index, to;

	for (from = 0; from < MACHINES; from++)
	{
		use(from);
		for (index = 0; index < friends->outbound_count; index++)
		{
			struct outbound entry = friends->outbound[index];

			if (carried_count < LOGGED)
			{
				snprintf(carried[carried_count].topic, TOPIC_LENGTH, "%s", entry.topic);
				memcpy(carried[carried_count].payload, entry.payload, (size_t)entry.size);
				carried[carried_count].size = entry.size;
				carried[carried_count].from = from;
				carried_count++;
			}
			for (to = 0; to < MACHINES; to++)
				if (to != from)
				{
					int copy;

					for (copy = 0; copy < broker_copies; copy++)
						hand(to, entry.topic, entry.payload, entry.size);
				}
		}
		use(from);
		friends->outbound_count = 0;
	}
}

/* a pass of every machine's thread, the broker between them */
static void pass(int count)
{
	int index, machine;

	for (index = 0; index < count; index++)
	{
		clock_now += 500;
		for (machine = 0; machine < MACHINES; machine++)
		{
			use(machine);
			p2p_friends_update(&hosts[machine]);
		}
		broker();
	}
}

static void code_of(int machine, char *code)
{
	use(machine);
	p2p_friends_code(code, P2P_FRIEND_CODE_SIZE);
}

/* the entry of a kind on a machine's list (its index-th of that kind) */
static int entry_of(int machine, int kind, int nth, struct p2p_friend_entry *entry)
{
	struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	int count, index;

	use(machine);
	count = p2p_friends_list(entries, P2P_FRIEND_ENTRIES_MAXIMUM);
	for (index = 0; index < count; index++)
		if (entries[index].kind == kind && nth-- == 0)
		{
			*entry = entries[index];
			return 1;
		}
	return 0;
}

static int count_of(int machine, int kind)
{
	struct p2p_friend_entry entries[P2P_FRIEND_ENTRIES_MAXIMUM];
	int count, index, of = 0;

	use(machine);
	count = p2p_friends_list(entries, P2P_FRIEND_ENTRIES_MAXIMUM);
	for (index = 0; index < count; index++)
		of += entries[index].kind == kind;
	return of;
}

/* the drops a machine counted since its last log of them */
static int drops(int machine)
{
	use(machine);
	return friends->dropped_replays + friends->dropped_forged + friends->dropped_blocked + friends->dropped_malformed;
}

static void reset_drops(int machine)
{
	use(machine);
	friends->dropped_replays = friends->dropped_forged = friends->dropped_blocked = 0;
	friends->dropped_rate = friends->dropped_malformed = 0;
	friends->total_replays = friends->total_forged = friends->total_blocked = 0;
	friends->total_rate = friends->total_malformed = 0;
	friends->drops_logged_time = clock_now;
}

/* a machine's blocked players' messages dropped since reset_drops */
static int blocked_drops(int machine)
{
	use(machine);
	return friends->dropped_blocked + friends->total_blocked;
}

/* ---------- the checks */

static void test_codes(void)
{
	char code[P2P_FRIEND_CODE_SIZE], typed[64], normalized[P2P_FRIEND_CODE_LENGTH + 1];
	int index;

	code_of(0, code);
	check(strlen(code) == 14 && code[4] == '-' && code[9] == '-', "a code is ABCD-EFGH-JKLM");
	for (index = 0; index < 14; index++)
		if (index != 4 && index != 9)
			check(strchr(P2P_CODE_ALPHABET, code[index]) != NULL, "a code's characters are the alphabet's");
	snprintf(typed, sizeof(typed), " %c%c%c%c %c%c%c%c-%c%c%c%c ", code[0] | 0x20, code[1], code[2], code[3], code[5],
		code[6], code[7], code[8], code[10], code[11], code[12], code[13]);
	check(code_normalize(typed, normalized), "a code typed any case, with spaces, is taken");
	check(!code_normalize("ABCD-EFGH-JKL0", normalized), "a 0 is no code's");
	check(!code_normalize("ABCD-EFGH", normalized), "a game's code is no friend code");
	check(!code_normalize("ABCD-EFGH-JKLMN", normalized), "13 characters are no code");
	check(p2p_friends_add(code) == P2P_FRIENDS_ADD_OWN_CODE, "the own code refused");
	check(p2p_friends_add("hello") == P2P_FRIENDS_ADD_NOT_A_CODE, "garbage refused");
	{
		char text[P2P_FRIEND_ACTIVITY_SIZE];

		p2p_friends_activity_text(text, sizeof(text), P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "", 2, -1, 0);
		check(!strcmp(text, "Slayer on Blood Gulch"), "a game type with no name: its engine's");
		p2p_friends_activity_text(text, sizeof(text), P2P_FRIEND_STATE_CAMPAIGN, "a10", "", 0, 2, 0);
		check(!strcmp(text, "Campaign: The Pillar of Autumn, Heroic"), "the campaign's activity");
		p2p_friends_activity_text(text, sizeof(text), P2P_FRIEND_STATE_MENUS, "", "", 0, -1, 0);
		check(!strcmp(text, "In the menus"), "In the menus");
	}
}

static void test_request_accept(void)
{
	char code_a[P2P_FRIEND_CODE_SIZE], code_b[P2P_FRIEND_CODE_SIZE];
	struct p2p_friend_entry entry;

	code_of(0, code_a);
	code_of(1, code_b);
	use(0);
	p2p_friends_set_name("Alpha");
	use(1);
	p2p_friends_set_name("Bravo|");
	use(0);
	check(p2p_friends_add(code_b) == P2P_FRIENDS_ADD_SENT, "a request sent");
	check(p2p_friends_add(code_b) == P2P_FRIENDS_ADD_ALREADY_SENT, "a request sent once");
	check(count_of(0, P2P_FRIEND_KIND_OUTGOING) == 1, "the request sent listed");
	pass(2);
	check(count_of(1, P2P_FRIEND_KIND_REQUEST) == 1, "the request heard");
	check(entry_of(1, P2P_FRIEND_KIND_REQUEST, 0, &entry) && !strcmp(entry.name, "Alpha") &&
		!strcmp(entry.code, code_a), "the request names its sender and their code");
	check(count_of(2, P2P_FRIEND_KIND_REQUEST) == 0, "a third machine hears no request");
	/* (re-sent, through another broker: one request) */
	pass(80);
	check(count_of(1, P2P_FRIEND_KIND_REQUEST) == 1, "a request re-sent is one request");
	use(1);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT) == P2P_FRIENDS_DONE, "accepted");
	pass(3);
	check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 1 && count_of(1, P2P_FRIEND_KIND_FRIEND) == 1, "both friends");
	check(count_of(0, P2P_FRIEND_KIND_OUTGOING) == 0, "the request sent gone once accepted");
	check(count_of(2, P2P_FRIEND_KIND_FRIEND) == 0, "the third machine is no one's friend");
	use(0);
	check(p2p_friends_add(code_b) == P2P_FRIENDS_ADD_ALREADY_FRIENDS, "a friend's code: already friends");
	check(!memcmp(machines[0].friends[0].friendship, machines[1].friends[0].friendship, FRIENDSHIP_LENGTH),
		"one friendship id on both sides");
	/* presence */
	use(1);
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 3, 16);
	pass(8);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online, "the friend online");
	check(!strcmp(entry.activity, "Slayer on Blood Gulch (VITA)") && !entry.crossplay,
		"the friend's activity: Slayer on Blood Gulch, tagged VITA (the Vitas' wire)");
	check(!strcmp(entry.name, "Bravo"), "the friend's name cleaned as a player's");
	check(!entry.pc, "a Vita friend: not the PC build");
	/* the port's PC build says so in its presence (bits older builds ignore,
	network_wire.h): the lists say PC; its hosted game tagged VITA (PC) */
	use(1);
	machine_platform[1] = NETWORK_PLATFORM_VITA_PC;
	hosts[1].host_pc_build = 1;
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 4, 16);
	pass(8);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online && entry.pc &&
		!strcmp(entry.activity, "Slayer on Blood Gulch (VITA (PC))"),
		"a friend on the PC build: entry.pc, their game tagged VITA (PC)");
	use(1);
	machine_platform[1] = NETWORK_PLATFORM_VITA;
	hosts[1].host_pc_build = 0;
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 3, 16);
	pass(8);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.pc &&
		!strcmp(entry.activity, "Slayer on Blood Gulch (VITA)"), "a Vita again: VITA");
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online && !strcmp(entry.activity, "In the menus"),
		"presence both ways: In the menus");
	use(0);
	p2p_friends_set_activity(P2P_FRIEND_STATE_COOP, "a10", "", 0, 3, 0, 1, 4);
	pass(8);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) &&
		!strcmp(entry.activity, "Co-op: The Pillar of Autumn, Legendary (VITA)"), "co-op's activity with its difficulty, tagged");
	use(0);
	p2p_friends_set_activity(P2P_FRIEND_STATE_FORGE, "", "", 0, -1, 0, 0, 0);
	pass(8);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !strcmp(entry.activity, "In Forge"), "In Forge");
	check(drops(0) == 0 && drops(1) == 0, "nothing honest dropped");
}

static void test_join_invite(void)
{
	struct p2p_friend_entry entry;
	struct p2p_friends_host *host = &hosts[1];
	char expected[P2P_LINK_SIZE];
	int index;

	/* Bravo hosts: public, open, room */
	memset(host, 0, sizeof(*host));
	host->online = 1;
	host->hosting = 1;
	for (index = 0; index < P2P_KEY_HASH_SIZE; index++)
		host->key_hash[index] = (unsigned char)(0xA0 + index);
	for (index = 0; index < P2P_TOKEN_SIZE; index++)
		host->token[index] = (unsigned char)(0x10 + index);
	host->listed = 1;
	host->open = 1;
	host->players = 2;
	host->maximum = 16;
	use(1);
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "CTF", 1, -1, 0, 2, 16);
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.joinable, "a public open game joinable");
	invite_link(host->key_hash, host->token, expected);
	use(0);
	join_count = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_JOIN) == P2P_FRIENDS_DONE && join_count == 1 &&
		!strcmp(joined_link, expected), "Join hands the friend's invite link to internet play");
	/* a password, its sealed token not ready yet: not joinable */
	host->locked = 1;
	host->has_sealed = 0;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.joinable, "a password's game, its seal not ready: not joinable");
	/* ... ready: joinable, the password asked; the presence carries the host's
	key and the sealed token, never the token (the owner, Oct 19 2026: a game
	with a password still asks the friend for it) */
	host->has_sealed = 1;
	for (index = 0; index < P2P_KEY_SIZE; index++)
		host->signing_key[index] = (unsigned char)(0xC0 + index);
	for (index = 0; index < P2P_SEALED_TOKEN_SIZE; index++)
		host->sealed_token[index] = (unsigned char)(0x60 + index);
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.joinable && entry.locked,
		"a password's game, sealed: joinable, with its password");
	use(1);
	check(machines[1].presence_joinable == 2 && !memmem(machines[1].presence_plain, (size_t)machines[1].presence_size,
		host->token, P2P_TOKEN_SIZE), "its presence holds no token (the sealed one in its place)");
	use(0);
	join_count = sealed_joins = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_JOIN) == P2P_FRIENDS_PASSWORD && !join_count,
		"Join asks for the password");
	check(p2p_friends_join_password(entry.id, "hunter2") == P2P_FRIENDS_DONE && sealed_joins == 1 &&
		!strcmp(sealed_password, "hunter2") && !memcmp(sealed_hash, host->key_hash, P2P_KEY_HASH_SIZE) &&
		!memcmp(sealed_key, host->signing_key, P2P_KEY_SIZE) && !memcmp(sealed_seal, host->sealed_token,
		P2P_SEALED_TOKEN_SIZE), "the password opens the presence's sealed token (the lobby's thread)");
	/* ... an invite into it admits without the password (the host chose them) */
	use(1);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.invitable &&
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_DONE,
		"Invite into a password's game sent");
	pass(2);
	use(0);
	check(p2p_friends_take_invite(&entry), "the invite heard");
	join_count = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_INVITE, P2P_FRIENDS_INVITE_ACCEPT) == P2P_FRIENDS_DONE &&
		join_count == 1 && !strcmp(joined_link, expected), "the invite joins with no password");
	host->locked = 0;
	host->has_sealed = 0;
	/* private (not listed): joinable from presence, no code typed (the
	owner, Oct 19 2026), and to friends alone */
	host->listed = 0;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.joinable && !entry.locked,
		"a private game joinable by a friend");
	use(0);
	join_count = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_JOIN) == P2P_FRIENDS_DONE && join_count == 1 &&
		!strcmp(joined_link, expected), "Join of the private game: its invite link, no code");
	{
		int from_bravo = 0;

		for (index = 0; index < carried_count; index++)
			from_bravo += carried[index].from == 1 && !strcmp(carried[index].topic, machines[2].mailbox_topic);
		check(!entry_of(2, P2P_FRIEND_KIND_FRIEND, 0, &entry) && from_bravo == 0,
			"a player who is not a friend hears nothing of it");
	}
	use(1);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.invitable &&
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_TOO_SOON,
		"an invite a moment after the last refused");
	/* (the invites' pace: one each INVITE_INTERVAL either way) */
	pass(24);
	use(1);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) &&
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_DONE, "Invite sent");
	pass(2);
	check(count_of(0, P2P_FRIEND_KIND_INVITE) == 1, "the invite heard");
	use(0);
	check(p2p_friends_take_invite(&entry) && !strcmp(entry.name, "Bravo") && strstr(entry.activity, "CTF on Blood Gulch"),
		"the invite's pop-up: who and what");
	check(!p2p_friends_take_invite(&entry), "the pop-up once");
	join_count = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_INVITE, P2P_FRIENDS_INVITE_ACCEPT) == P2P_FRIENDS_DONE &&
		join_count == 1 && !strcmp(joined_link, expected), "Accept joins the invite");
	check(count_of(0, P2P_FRIEND_KIND_INVITE) == 0, "an invite answered is gone");
	/* Friends can join off: not joinable */
	host->listed = 1;
	friends_join_setting = 0;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.joinable, "Friends can join off: not joinable");
	friends_join_setting = 1;
	/* full */
	host->players = 16;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.joinable, "a full game not joinable");
	host->players = 2;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.joinable, "joinable again");
}

static void test_replays_and_forgeries(void)
{
	struct p2p_friend_entry entry;
	int index, before, replayed = 0;
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];

	reset_drops(0);
	reset_drops(1);
	/* every message Bravo sent Alpha's mailbox, again */
	use(0);
	for (index = 0; index < carried_count; index++)
		if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[0].mailbox_topic))
		{
			hand(0, carried[index].topic, carried[index].payload, carried[index].size);
			replayed++;
		}
	before = machines[0].invite_count;
	pass(1);
	pass(1);
	check(replayed > 3, "messages to replay");
	use(0);
	check(machines[0].dropped_replays >= replayed - 1, "every replayed message dropped");
	check(machines[0].invite_count == before, "a replayed invite shows nothing");
	/* an eavesdropper (the third machine) given Bravo's messages to Alpha:
	nothing opens, nothing is shown */
	reset_drops(2);
	{
		int given = 0;

		for (index = 0; index < carried_count; index++)
			if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[0].mailbox_topic))
			{
				use(2);
				p2p_friends_heard(P2P_FRIENDS_MAILBOX, carried[index].payload, carried[index].size);
				given++;
				if (given >= 8)
					break;
			}
		use(2);
		p2p_friends_update(&hosts[2]);
		check(given > 0 && machines[2].dropped_forged + machines[2].dropped_rate >= given &&
			count_of(2, P2P_FRIEND_KIND_FRIEND) == 0 && count_of(2, P2P_FRIEND_KIND_INVITE) == 0,
			"an eavesdropper given a pair's messages opens none of them");
	}
	/* a flipped byte */
	for (index = carried_count - 1; index >= 0; index--)
		if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[0].mailbox_topic))
			break;
	memcpy(payload, carried[index].payload, (size_t)carried[index].size);
	payload[carried[index].size / 2] ^= 1;
	reset_drops(0);
	hand(0, carried[index].topic, payload, carried[index].size);
	pass(1);
	check(machines[0].dropped_forged == 1, "a flipped byte dropped");
	/* the forgeries a machine's test step makes (another key's signature, a
	flipped byte, another sender) */
	reset_drops(0);
	use(1);
	test_forge();
	broker();
	pass(1);
	check(machines[0].dropped_forged >= 3, "the three forgeries dropped");
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.state != P2P_FRIEND_STATE_FORGE &&
		strcmp(entry.name, "Forged"), "a forgery changes nothing");
	/* a request sealed for another code (the recipient another's) */
	{
		struct message message;
		unsigned char plain[MAXIMUM_PLAIN_SIZE], seal_key[P2P_SHA256_SIZE];
		char topic[TOPIC_LENGTH];
		int size;

		use(2);
		code_derive(machines[0].code, topic, seal_key, NULL);
		message_start(&message, TYPE_REQUEST, machines[2].code_recipient);
		snprintf(message.name, sizeof(message.name), "Charlie");
		size = plain_encode(&message, plain);
		size = envelope_seal(KIND_REQUEST, seal_key, plain, size, payload);
		reset_drops(0);
		hand(0, topic, payload, size);
		pass(1);
		check(machines[0].dropped_forged == 1 && count_of(0, P2P_FRIEND_KIND_REQUEST) == 0,
			"a request naming another code dropped");
	}
	/* a presence that names a recipient (presence goes to all) */
	{
		struct message message;
		unsigned char plain[MAXIMUM_PLAIN_SIZE];
		char topic[TOPIC_LENGTH];
		int size;

		use(1);
		message_start(&message, TYPE_PRESENCE, machines[0].key);
		message.flags = PRESENCE_ONLINE;
		message.game.state = P2P_FRIEND_STATE_MENUS;
		message.game.difficulty = -1;
		size = plain_encode(&message, plain);
		friend_keys(&machines[1].friends[0]);
		size = envelope_seal(KIND_PAIR, machines[1].friends[0].send_key, plain, size, payload);
		mailbox_topic(machines[0].key, topic);
		reset_drops(0);
		hand(0, topic, payload, size);
		pass(1);
		check(machines[0].dropped_malformed == 1, "a presence with a recipient dropped");
	}
}

static void test_appear_offline(void)
{
	struct p2p_friend_entry entry;
	int sent;

	pass(4);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online, "online before appearing offline");
	/* (the setting read in each pass: on for Bravo's) */
	machines[1].test_appeared_offline = 1;
	pass(4);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.online && !strcmp(entry.activity, "Offline"),
		"appearing offline: the friend sees it offline at once");
	sent = carried_count;
	pass(300);
	{
		int index, from_bravo = 0;

		for (index = sent; index < carried_count; index++)
			from_bravo += carried[index].from == 1;
		check(from_bravo == 0, "appearing offline: nothing sent");
	}
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online, "appearing offline: friends still seen");
	/* (a friend's friend request to the code of a machine appearing offline,
	which answered a friend who lost it: no ACCEPT, no presence, nothing
	that says it is on; security review 1.1.1-beta.1 F-04) */
	{
		struct outgoing_record probe;
		char code_b[P2P_FRIEND_CODE_SIZE];
		int index, from_bravo = 0;

		code_of(1, code_b);
		use(0);
		memset(&probe, 0, sizeof(probe));
		check(code_normalize(code_b, probe.code), "the probe's code");
		posix_random_bytes(probe.nonce, NONCE_LENGTH);
		send_request(&probe);
		sent = carried_count;
		pass(6);
		for (index = sent; index < carried_count; index++)
			from_bravo += carried[index].from == 1;
		check(from_bravo == 0, "appearing offline: a friend's request to the code answered with nothing");
		check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 1, "the friendship stands");
	}
	machines[1].test_appeared_offline = 0;
	pass(8);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online, "online again");
}

static void test_decline_block(void)
{
	char code_a[P2P_FRIEND_CODE_SIZE], code_c[P2P_FRIEND_CODE_SIZE];
	struct p2p_friend_entry entry;

	code_of(0, code_a);
	code_of(2, code_c);
	use(2);
	p2p_friends_set_name("Charlie");
	check(p2p_friends_add(code_a) == P2P_FRIENDS_ADD_SENT, "Charlie's request sent");
	pass(2);
	check(entry_of(0, P2P_FRIEND_KIND_REQUEST, 0, &entry) && !strcmp(entry.name, "Charlie"), "Charlie's request heard");
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_DECLINE) == P2P_FRIENDS_DONE, "declined");
	{
		int sent = carried_count, index, from_alpha = 0;

		pass(2);
		for (index = sent; index < carried_count; index++)
			from_alpha += carried[index].from == 0 && !strcmp(carried[index].topic, machines[2].mailbox_topic);
		check(from_alpha == 0, "a decline sends nothing");
	}
	check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 1 && count_of(2, P2P_FRIEND_KIND_FRIEND) == 0,
		"a declined request leaves no friendship");
	check(count_of(2, P2P_FRIEND_KIND_OUTGOING) == 1, "the decliner's silence: the request still sent on its side");
	pass(130);
	check(count_of(0, P2P_FRIEND_KIND_REQUEST) == 0, "the declined sender's requests muted");
	/* block Charlie from his entry once more asked: unmute by a new key is a
	new player; here: the mute lifted and blocked */
	use(0);
	machines[0].muted_count = 0;
	pass(130);
	check(entry_of(0, P2P_FRIEND_KIND_REQUEST, 0, &entry), "Charlie's request again once unmuted");
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_BLOCK) == P2P_FRIENDS_DONE, "blocked");
	check(count_of(0, P2P_FRIEND_KIND_BLOCKED) == 1 && count_of(0, P2P_FRIEND_KIND_REQUEST) == 0, "blocked listed");
	reset_drops(0);
	pass(130);
	check(count_of(0, P2P_FRIEND_KIND_REQUEST) == 0, "a blocked player's requests dropped");
	check(blocked_drops(0) >= 1, "a blocked player's requests counted dropped");
	/* unblocked, then accepted: friends; then blocking a friend tells them */
	check(entry_of(0, P2P_FRIEND_KIND_BLOCKED, 0, &entry), "the blocked entry");
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_BLOCKED, P2P_FRIENDS_UNBLOCK) == P2P_FRIENDS_DONE, "unblocked");
	pass(130);
	check(entry_of(0, P2P_FRIEND_KIND_REQUEST, 0, &entry), "a request again after unblocking");
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT) == P2P_FRIENDS_DONE, "Charlie accepted");
	pass(4);
	check(count_of(2, P2P_FRIEND_KIND_FRIEND) == 1 && count_of(0, P2P_FRIEND_KIND_FRIEND) == 2, "Charlie a friend");
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 1, &entry) || entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry), "entries");
	{
		int index;

		for (index = 0; index < 2; index++)
			if (entry_of(0, P2P_FRIEND_KIND_FRIEND, index, &entry) && !strcmp(entry.name, "Charlie"))
				break;
	}
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_BLOCK) == P2P_FRIENDS_DONE, "a friend blocked");
	pass(4);
	check(count_of(2, P2P_FRIEND_KIND_FRIEND) == 0, "the blocked friend told: no longer friends");
	use(2);
	check(p2p_friends_add(code_a) == P2P_FRIENDS_ADD_SENT, "the blocked asks again");
	reset_drops(0);
	pass(4);
	check(count_of(0, P2P_FRIEND_KIND_REQUEST) == 0 && blocked_drops(0) >= 1, "and is dropped");
	/* Alpha hosts a private game: the blocked player hears nothing of it */
	{
		struct p2p_friends_host *host = &hosts[0];
		int sent = carried_count, index, to_charlie = 0;

		memset(host, 0, sizeof(*host));
		host->online = host->hosting = host->open = 1;
		host->players = 1;
		host->maximum = 16;
		host->key_hash[0] = 0xAB;
		host->token[0] = 0xCD;
		use(0);
		p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 1, 16);
		pass(10);
		for (index = sent; index < carried_count; index++)
			to_charlie += carried[index].from == 0 && !strcmp(carried[index].topic, machines[2].mailbox_topic);
		check(to_charlie == 0 && count_of(2, P2P_FRIEND_KIND_FRIEND) == 0,
			"a blocked player hears nothing of Alpha's private game: no presence, no join");
		memset(host, 0, sizeof(*host));
		host->online = 1;
		use(0);
		p2p_friends_set_activity(P2P_FRIEND_STATE_MENUS, "", "", 0, -1, 0, 0, 0);
		pass(4);
	}
	use(2);
	machines[2].outgoing_count = 0;
}

static void test_remove(void)
{
	struct p2p_friend_entry entry;
	unsigned char old_friendship[FRIENDSHIP_LENGTH];
	int index, removes = -1;

	memcpy(old_friendship, machines[0].friends[0].friendship, FRIENDSHIP_LENGTH);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry), "Bravo's friend");
	use(1);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_REMOVE) == P2P_FRIENDS_DONE, "removed");
	for (index = carried_count; index >= 0; index--)
		;
	removes = carried_count;
	pass(3);
	check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 0 && count_of(1, P2P_FRIEND_KIND_FRIEND) == 0, "gone on both sides");
	/* friends again; the old REMOVE replayed does nothing */
	{
		char code_b[P2P_FRIEND_CODE_SIZE];

		code_of(1, code_b);
		use(0);
		check(p2p_friends_add(code_b) == P2P_FRIENDS_ADD_SENT, "asked again");
		pass(2);
		check(entry_of(1, P2P_FRIEND_KIND_REQUEST, 0, &entry), "heard again");
		use(1);
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT);
		pass(4);
		check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 1 && count_of(1, P2P_FRIEND_KIND_FRIEND) == 1, "friends again");
		check(memcmp(old_friendship, machines[0].friends[0].friendship, FRIENDSHIP_LENGTH), "a new friendship id");
		for (index = removes; index < carried_count; index++)
			if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[0].mailbox_topic))
			{
				hand(0, carried[index].topic, carried[index].payload, carried[index].size);
				break;
			}
		pass(2);
		check(count_of(0, P2P_FRIEND_KIND_FRIEND) == 1, "the old REMOVE replayed changes nothing");
	}
}

static void test_files(void)
{
	unsigned char bytes[MAXIMUM_LIST_SIZE + 64];
	struct friends_state copy;
	char path[600], old[600];
	int size, friends_before, outgoing_before;
	unsigned long long epoch_before;
	char code_before[P2P_FRIEND_CODE_SIZE], code_after[P2P_FRIEND_CODE_SIZE];

	use(0);
	save(1);
	friends_before = machines[0].friend_count;
	outgoing_before = machines[0].outgoing_count;
	epoch_before = machines[0].epoch;
	code_of(0, code_before);
	/* a new run of the same machine */
	copy = machines[0];
	memset(&machines[0], 0, sizeof(machines[0]));
	use(0);
	code_of(0, code_after);
	check(!strcmp(code_before, code_after), "the identity kept: the same code");
	check(machines[0].epoch > epoch_before, "a new run's epoch newer");
	check(machines[0].friend_count == friends_before && machines[0].outgoing_count == outgoing_before,
		"the list read back");
	check(machines[0].friend_count > 0 && !memcmp(machines[0].friends[0].key, copy.friends[0].key, P2P_KEY_SIZE) &&
		!strcmp(machines[0].friends[0].name, copy.friends[0].name), "a friend read back the same");
	/* what failed its checks: set aside, not used */
	snprintf(path, sizeof(path), "%s/friends/friends.dat", roots[0]);
	size = file_read(path, bytes, sizeof(bytes));
	check(size > LIST_HEADER_SIZE, "the list's file");
	use(0);
	check(list_decode(bytes, size), "the file reads");
	bytes[LIST_HEADER_SIZE + 3] ^= 0x10;
	check(!list_decode(bytes, size), "a flipped byte fails the hash");
	bytes[LIST_HEADER_SIZE + 3] ^= 0x10;
	check(!list_decode(bytes, size - 1), "a cut file fails");
	bytes[5] = P2P_FRIENDS_MAXIMUM + 1;
	check(!list_decode(bytes, size), "a count past its bound fails");
	/* the machine's own key in its list: refused even with a good hash */
	{
		unsigned char forged[MAXIMUM_LIST_SIZE];
		int forged_size;

		use(0);
		memcpy(forged, bytes, (size_t)size);
		forged[5] = 1;
		forged[6] = forged[7] = forged[8] = forged[9] = forged[10] = 0;
		memcpy(forged + LIST_HEADER_SIZE, machines[0].key, P2P_KEY_SIZE);
		forged_size = LIST_HEADER_SIZE + FRIEND_RECORD_SIZE;
		p2p_sha256(forged, forged_size, forged + forged_size);
		check(!list_decode(forged, forged_size + P2P_SHA256_SIZE), "the own key in the list refused");
		/* the same key twice */
		forged[5] = 2;
		memcpy(forged + LIST_HEADER_SIZE, copy.friends[0].key, P2P_KEY_SIZE);
		memcpy(forged + LIST_HEADER_SIZE + FRIEND_RECORD_SIZE, forged + LIST_HEADER_SIZE, FRIEND_RECORD_SIZE);
		forged_size = LIST_HEADER_SIZE + 2 * FRIEND_RECORD_SIZE;
		p2p_sha256(forged, forged_size, forged + forged_size);
		check(!list_decode(forged, forged_size + P2P_SHA256_SIZE), "a key twice refused");
		/* a name with a length past its room */
		forged[5] = 1;
		memcpy(forged + LIST_HEADER_SIZE, copy.friends[0].key, P2P_KEY_SIZE);
		forged[LIST_HEADER_SIZE + P2P_KEY_SIZE] = NAME_LENGTH + 1;
		forged_size = LIST_HEADER_SIZE + FRIEND_RECORD_SIZE;
		p2p_sha256(forged, forged_size, forged + forged_size);
		check(!list_decode(forged, forged_size + P2P_SHA256_SIZE), "a name's length past its room refused");
	}
	/* a damaged file on disk: the next run sets it aside and starts empty */
	bytes[5] = (unsigned char)friends_before;
	bytes[20] ^= 0xFF;
	file_write(path, bytes, size);
	memset(&machines[0], 0, sizeof(machines[0]));
	use(0);
	quiet = 1;
	code_of(0, code_after);
	quiet = 0;
	check(machines[0].friend_count == 0, "a damaged list not used");
	{
		char bad[600];

		snprintf(bad, sizeof(bad), "%s.bad", path);
		check(access(bad, F_OK) == 0, "a damaged list kept as .bad");
	}
	/* a write cut short between its renames: the .old one taken */
	machines[0] = copy;
	use(0);
	machines[0].dirty = 1;
	save(1);
	snprintf(old, sizeof(old), "%s.old", path);
	check(rename(path, old) == 0, "the list moved to .old");
	memset(&machines[0], 0, sizeof(machines[0]));
	use(0);
	code_of(0, code_after);
	check(machines[0].friend_count == friends_before, "the .old list taken");
	machines[0] = copy;
	machines[0].loaded = 1;
	use(0);
	machines[0].dirty = 1;
	save(1);
	/* the identity's file checked too */
	{
		unsigned char identity[IDENTITY_SIZE + 8], seed[P2P_SEED_SIZE];
		unsigned long long epoch;
		int identity_size;

		snprintf(path, sizeof(path), "%s/friends/identity.dat", roots[0]);
		identity_size = file_read(path, identity, sizeof(identity));
		check(identity_decode(identity, identity_size, seed, &epoch) && !memcmp(seed, copy.seed, P2P_SEED_SIZE),
			"the identity reads");
		identity[10] ^= 1;
		check(!identity_decode(identity, identity_size, seed, &epoch), "a flipped identity byte fails");
		identity[10] ^= 1;
		check(!identity_decode(identity, identity_size - 1, seed, &epoch), "a cut identity fails");
	}
}

static void test_limits(void)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1], shown[P2P_FRIEND_CODE_SIZE];
	unsigned char key[P2P_KEY_SIZE], seed[P2P_SEED_SIZE];
	int index, result = P2P_FRIENDS_ADD_SENT;

	use(2);
	machines[2].outgoing_count = 0;
	for (index = 0; index < P2P_FRIEND_OUTGOING_MAXIMUM + 2 && result != P2P_FRIENDS_ADD_FULL; index++)
	{
		posix_random_bytes(seed, sizeof(seed));
		p2p_ed25519_public(seed, key, NULL);
		code_of_key(key, code);
		code_shown(code, shown, sizeof(shown));
		result = p2p_friends_add(shown);
	}
	check(result == P2P_FRIENDS_ADD_FULL && machines[2].outgoing_count == P2P_FRIEND_OUTGOING_MAXIMUM,
		"requests sent: 16 at most");
	use(2);
	machines[2].outgoing_count = 0;
	machines[2].dirty = 1;
	save(1);
	/* a flood of requests from new keys: some taken, the rest past the
	budget, never more than the list's room */
	reset_drops(0);
	for (index = 0; index < 200; index++)
	{
		struct friends_state *was = friends;
		static struct friends_state stranger;
		struct message message;
		unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE], seal_key[P2P_SHA256_SIZE];
		unsigned char recipient[P2P_SHA256_SIZE];
		char topic[TOPIC_LENGTH];
		int size;

		memset(&stranger, 0, sizeof(stranger));
		friends = &stranger;
		posix_random_bytes(stranger.seed, sizeof(stranger.seed));
		identity_from_seed();
		code_derive(machines[0].code, topic, seal_key, recipient);
		message_start(&message, TYPE_REQUEST, recipient);
		snprintf(message.name, sizeof(message.name), "Spam%d", index);
		size = plain_encode(&message, plain);
		size = envelope_seal(KIND_REQUEST, seal_key, plain, size, payload);
		friends = was;
		hand(0, topic, payload, size);
		if (index % 20 == 19)
		{
			use(0);
			p2p_friends_update(&hosts[0]);
		}
	}
	use(0);
	check(machines[0].request_count <= P2P_FRIEND_REQUESTS_MAXIMUM, "requests heard: 16 at most");
	check(machines[0].request_count > 0, "some of the flood heard");
	machines[0].request_count = 0;
	machines[0].dirty = 1;
	save(1);
}


/* ---------- the game's players' cards (a player's options) */

/* machine i's virtual address, as p2p.c has it (100.64.0.i, network byte
order) */
static unsigned long virtual_of(int machine)
{
	return 100UL | 64UL << 8 | (unsigned long)(machine + 2) << 24;
}

/* each machine's cards carried over the tunnel: to its peer, or (0) to the
host; the host the machine whose game it hosts */
static int tunnel_route(int host)
{
	int from, index, carried_cards = 0;

	for (from = 0; from < MACHINES; from++)
	{
		struct card_packet packets[MAXIMUM_CARD_OUTBOUND];
		int count;

		use(from);
		count = machines[from].card_outbound_count;
		memcpy(packets, machines[from].card_outbound, sizeof(packets[0]) * (size_t)count);
		machines[from].card_outbound_count = 0;
		for (index = 0; index < count; index++)
		{
			int to = host, machine;

			if (packets[index].address)
				for (machine = 0, to = -1; machine < MACHINES; machine++)
					if (virtual_of(machine) == packets[index].address)
						to = machine;
			if (to < 0 || to == from)
				continue;
			use(to);
			p2p_friends_tunnel_heard(virtual_of(from), from == host, packets[index].bytes, packets[index].size);
			carried_cards++;
		}
	}
	return carried_cards;
}

static void game_pass(int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		pass(1);
		tunnel_route(0);
	}
}

static void test_cards(void)
{
	unsigned long addresses[MACHINES];
	int index;

	/* Alpha hosts (machine 0), Bravo and Charlie joined (1, 2); Charlie is
	no friend of Bravo's, and asked nothing yet */
	use(2);
	machines[2].outgoing_count = 0;
	machines[2].test_appeared_offline = 0;
	for (index = 0; index < MACHINES; index++)
		addresses[index] = virtual_of(index);
	use(0);
	p2p_friends_set_game(0, 1, addresses, MACHINES);
	use(1);
	p2p_friends_set_game(1, 0, NULL, 0);
	use(2);
	p2p_friends_set_game(2, 0, NULL, 0);
	use(1);
	check(p2p_friends_player_state(2) == P2P_FRIENDS_PLAYER_NONE, "a player not asked: nothing known");
	check(p2p_friends_player_state(1) == P2P_FRIENDS_PLAYER_THIS_VITA, "the machine's own players");
	check(p2p_friends_player_ask(1, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_GONE, "this machine is no one to ask");
	check(p2p_friends_player_ask(2, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_DONE, "Bravo asks Charlie's card");
	check(machines[1].card_outbound_count == 1 && machines[1].card_outbound[0].size == CARD_HEADER_SIZE &&
		machines[1].card_outbound[0].address == 0, "an ask holds a nonce alone, to the host");
	check(p2p_friends_player_ask(2, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_TOO_SOON, "asked once at a time");
	check(p2p_friends_player_state(2) == P2P_FRIENDS_PLAYER_ASKING, "asking");
	game_pass(6);
	check(count_of(2, P2P_FRIEND_KIND_REQUEST) == 1, "the host relayed, Charlie answered, Bravo's request reached Charlie");
	{
		struct p2p_friend_entry entry;

		check(entry_of(2, P2P_FRIEND_KIND_REQUEST, 0, &entry) && !strcmp(entry.name, "Bravo"), "the request is Bravo's");
		use(1);
		check(p2p_friends_player_state(2) == P2P_FRIENDS_PLAYER_REQUEST_SENT, "Bravo's options: request sent");
		use(2);
		check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT) == P2P_FRIENDS_DONE, "Charlie accepts");
	}
	game_pass(6);
	use(1);
	check(p2p_friends_player_state(2) == P2P_FRIENDS_PLAYER_FRIEND, "Bravo and Charlie friends");
	/* appearing offline (or friends off): no answer, nothing of the player's sent */
	use(0);
	machines[0].test_appeared_offline = 1;
	pass(1);
	use(1);
	check(p2p_friends_player_ask(0, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_DONE, "Bravo asks the host's card");
	{
		int answers = 0, step;

		for (step = 0; step < 30; step++)
		{
			pass(1);
			use(0);
			for (index = 0; index < machines[0].card_outbound_count; index++)
				answers += machines[0].card_outbound[index].size == CARD_ANSWER_SIZE;
			tunnel_route(0);
		}
		use(1);
		check(answers == 0 && p2p_friends_player_state(0) == P2P_FRIENDS_PLAYER_NOT_TAKING,
			"a player appearing offline answers nothing: not taking requests");
	}
	machines[0].test_appeared_offline = 0;
	/* forgeries: an answer to no ask, one signed by another key, a joiner's
	claim to be another machine */
	{
		unsigned char packet[CARD_ANSWER_SIZE];
		int before;

		use(2);
		memset(packet, 0, sizeof(packet));
		packet[0] = CARD_VERSION;
		packet[1] = CARD_ANSWER;
		packet[2] = 2;
		packet[3] = 1;
		memcpy(packet + CARD_HEADER_SIZE, machines[2].key, P2P_KEY_SIZE);
		reset_drops(1);
		use(1);
		p2p_friends_tunnel_heard(virtual_of(0), 1, packet, CARD_ANSWER_SIZE);
		pass(1);
		check(machines[1].dropped_replays + machines[1].total_replays >= 1, "an answer to no ask dropped");
		/* (an ask made, the answer's signature not the key's) */
		use(1);
		machines[1].machines[0].asked_time = 0;
		machines[1].machines[0].unanswered = 0;
		check(p2p_friends_player_ask(0, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_DONE, "asked again");
		memcpy(packet + 4, machines[1].machines[0].nonce, NONCE_LENGTH);
		packet[2] = 0;
		memcpy(packet + CARD_HEADER_SIZE, machines[0].key, P2P_KEY_SIZE);
		machines[1].card_outbound_count = 0;
		reset_drops(1);
		use(1);
		p2p_friends_tunnel_heard(virtual_of(0), 1, packet, CARD_ANSWER_SIZE);
		pass(1);
		check(machines[1].dropped_forged + machines[1].total_forged >= 1 && !machines[1].machines[0].has_card,
			"an answer not signed by its key dropped");
		/* (a joiner's packet heard by another joiner straight, not from the host) */
		reset_drops(2);
		use(2);
		before = count_of(2, P2P_FRIEND_KIND_REQUEST);
		p2p_friends_tunnel_heard(virtual_of(1), 0, packet, CARD_HEADER_SIZE);
		pass(1);
		check(machines[2].dropped_forged + machines[2].dropped_malformed + machines[2].total_forged +
			machines[2].total_malformed >= 1 && count_of(2, P2P_FRIEND_KIND_REQUEST) == before,
			"a card not from the host dropped by a joiner");
		/* (sizes: one byte short, one past) */
		reset_drops(0);
		use(0);
		p2p_friends_tunnel_heard(virtual_of(1), 0, packet, CARD_HEADER_SIZE - 1);
		p2p_friends_tunnel_heard(virtual_of(1), 0, packet, CARD_ANSWER_SIZE + 1);
		pass(1);
		check(machines[0].dropped_malformed + machines[0].total_malformed >= 1, "cards of the wrong size dropped");
		/* (the host's relay: past its budget, dropped) */
		reset_drops(0);
		use(0);
		for (index = 0; index < 40; index++)
		{
			packet[0] = CARD_VERSION;
			packet[1] = CARD_ASK;
			packet[2] = 1;
			packet[3] = 2;
			p2p_friends_tunnel_heard(virtual_of(1), 0, packet, CARD_HEADER_SIZE);
		}
		use(0);
		p2p_friends_update(&hosts[0]);
		check(machines[0].card_outbound_count <= 1 && machines[0].dropped_rate + machines[0].total_rate >= 10,
			"a joiner's flood relayed one a second at most");
		machines[0].card_outbound_count = 0;
	}
	/* Block, from a player's options: the card asked, the key blocked */
	use(1);
	machines[1].machines[0].asked_time = 0;
	machines[1].machines[0].asking = 0;
	check(p2p_friends_player_ask(0, P2P_FRIENDS_PLAYER_BLOCK) == P2P_FRIENDS_DONE, "Bravo blocks the host's player");
	game_pass(8);
	use(1);
	check(p2p_friends_player_state(0) == P2P_FRIENDS_PLAYER_BLOCKED && count_of(1, P2P_FRIEND_KIND_BLOCKED) >= 1,
		"blocked through its card");
	/* the game over: the cards forgotten */
	p2p_friends_set_game(-1, 0, NULL, 0);
	check(p2p_friends_player_state(0) == P2P_FRIENDS_PLAYER_NONE && !machines[1].in_game, "the game over: forgotten");
	use(0);
	p2p_friends_set_game(-1, 0, NULL, 0);
	use(2);
	p2p_friends_set_game(-1, 0, NULL, 0);
}

#ifndef P2P_FRIENDS_FUZZ
/* ---------- the party (p2p_party.inc) */

/* a machine left out of the passes and the broker (gone offline at once,
as a Vita switched off) */
static int party_offline = -1;

static void party_pass(int count)
{
	int index, machine;

	for (index = 0; index < count; index++)
	{
		clock_now += 500;
		for (machine = 0; machine < MACHINES; machine++)
		{
			if (machine == party_offline)
				continue;
			use(machine);
			p2p_friends_update(&hosts[machine]);
		}
		if (party_offline >= 0)
		{
			/* (what the machine off queued never leaves it) */
			use(party_offline);
			friends->outbound_count = 0;
		}
		broker();
		if (party_offline >= 0)
		{
			use(party_offline);
			friends->inbound_count = 0;
		}
	}
}

/* every machine with no friends, party or chat, then the friendships given */
static void party_reset(void)
{
	int index;

	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		friends->friend_count = friends->request_count = friends->outgoing_count = 0;
		friends->blocked_count = friends->tombstone_count = friends->muted_count = friends->invite_count = 0;
		memset(&friends->party, 0, sizeof(friends->party));
		friends->party_invite_count = 0;
		friends->chat_count = 0;
		friends->test_appeared_offline = 0;
		memset(&hosts[index], 0, sizeof(hosts[index]));
		hosts[index].online = 1;
		p2p_friends_set_game(-1, 0, NULL, 0);
		reset_drops(index);
	}
	party_offline = -1;
}

static void party_make_friends(int a, int b)
{
	unsigned char friendship[FRIENDSHIP_LENGTH];
	char name[P2P_FRIEND_NAME_SIZE];

	posix_random_bytes(friendship, sizeof(friendship));
	use(b);
	own_name(name);
	use(a);
	friend_add(machines[b].key, name, friendship, 0, 0);
	use(a);
	own_name(name);
	use(b);
	friend_add(machines[a].key, name, friendship, 0, 0);
}

static int party_count_of(int machine, struct p2p_party_member *members)
{
	struct p2p_party_member local[P2P_PARTY_MAXIMUM];

	use(machine);
	return p2p_party_members(members ? members : local, P2P_PARTY_MAXIMUM);
}

/* the id of a machine's friend entry for another machine */
static int party_friend_id(int machine, int other, char *id)
{
	use(machine);
	if (find_friend_index(machines[other].key) < 0)
		return 0;
	key_id(machines[other].key, id);
	return 1;
}

static int party_member_id(int machine, int other, char *id)
{
	struct p2p_party_member members[P2P_PARTY_MAXIMUM];
	char want[P2P_FRIEND_ID_SIZE];
	int count = party_count_of(machine, members), index;

	key_id(machines[other].key, want);
	for (index = 0; index < count; index++)
		if (!strcmp(members[index].id, want))
		{
			snprintf(id, P2P_FRIEND_ID_SIZE, "%s", members[index].id);
			return 1;
		}
	return 0;
}

/* a machine's leader as its list says (-1: none) */
static int party_leader_of(int machine)
{
	struct p2p_party_member members[P2P_PARTY_MAXIMUM];
	int count = party_count_of(machine, members), index, other;

	for (index = 0; index < count; index++)
		if (members[index].leader)
			for (other = 0; other < MACHINES; other++)
			{
				char id[P2P_FRIEND_ID_SIZE];

				key_id(machines[other].key, id);
				if (!strcmp(id, members[index].id))
					return other;
			}
	return -1;
}

/* the machine's invites answered (accept: yes) */
static int party_answer_all(int machine, int accept)
{
	struct p2p_party_invite invite;
	int answered = 0;

	use(machine);
	while (p2p_party_take_invite(&invite))
	{
		use(machine);
		answered += p2p_party_answer(invite.id, accept) == P2P_PARTY_DONE;
	}
	return answered;
}

/* the newest chat line of a machine ("" none) */
static void party_last_line(int machine, struct p2p_party_chat_line *line)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	int count;

	use(machine);
	count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, 0);
	memset(line, 0, sizeof(*line));
	while (count > 0 && lines[count - 1].notice)
		count--;
	if (count > 0)
		*line = lines[count - 1];
}

static int party_notices_with(int machine, const char *text)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	int count, index, found = 0;

	use(machine);
	count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, 0);
	for (index = 0; index < count; index++)
		found += lines[index].notice && strstr(lines[index].text, text) != NULL;
	return found;
}

static int party_lines_from(int machine, const char *text)
{
	struct p2p_party_chat_line lines[P2P_PARTY_CHAT_LINES];
	int count, index, found = 0;

	use(machine);
	count = p2p_party_chat_lines(lines, P2P_PARTY_CHAT_LINES, 0);
	for (index = 0; index < count; index++)
		found += !lines[index].notice && strstr(lines[index].text, text) != NULL;
	return found;
}

static void test_party(void)
{
	char id[P2P_FRIEND_ID_SIZE], text[256], expected[P2P_LINK_SIZE];
	struct p2p_party_member members[P2P_PARTY_MAXIMUM];
	struct p2p_party_chat_line line;
	struct p2p_party_follow follow;
	int index, before, count;

	/* Alpha is friends with Bravo and Charlie, Bravo with Delta; Charlie and
	Delta are no one else's */
	party_reset();
	use(0);
	p2p_friends_set_name("Alpha");
	use(1);
	p2p_friends_set_name("Bravo");
	use(2);
	p2p_friends_set_name("Charlie");
	use(3);
	p2p_friends_set_name("Delta");
	party_make_friends(0, 1);
	party_make_friends(0, 2);
	party_make_friends(1, 3);
	party_pass(10);
	use(0);
	check(p2p_party_status(text, sizeof(text)) == 0 && strstr(text, "Not in a party"), "no party at first");

	/* invites: only friends, only the leader */
	check(!party_friend_id(0, 3, id), "Delta is not Alpha's friend");
	key_id(machines[3].key, id);
	use(0);
	check(p2p_party_invite(id) == P2P_PARTY_NOT_FRIEND, "a stranger cannot be invited");
	check(party_friend_id(0, 1, id) && p2p_party_invite(id) == P2P_PARTY_DONE, "Alpha invites Bravo");
	use(0);
	check(p2p_party_invite(id) == P2P_PARTY_TOO_SOON, "an invite again at once refused");
	check(party_friend_id(0, 2, id) && p2p_party_invite(id) == P2P_PARTY_DONE, "Alpha invites Charlie");
	party_pass(2);
	check(party_count_of(3, NULL) == 0, "Delta hears nothing of the invites");
	check(party_answer_all(1, 1) == 1 && party_answer_all(2, 1) == 1, "Bravo and Charlie accept");
	party_pass(4);
	check(party_count_of(0, members) == 3 && party_count_of(1, NULL) == 3 && party_count_of(2, NULL) == 3,
		"a party of three on each machine");
	check(party_leader_of(0) == 0 && party_leader_of(1) == 0 && party_leader_of(2) == 0, "Alpha leads on each");
	use(1);
	p2p_party_status(text, sizeof(text));
	check(strstr(text, "3 of 8") && strstr(text, "Alpha leads"), "the status: 3 of 8, Alpha leads");
	/* (Bravo knows Charlie by the roster only: Charlie's own name with his
	code's first characters, never as a friend) */
	party_pass(2);
	count = party_count_of(1, members);
	for (index = 0; index < count; index++)
		if (strstr(members[index].name, "Charlie"))
			break;
	check(index < count && strstr(members[index].name, " #") && !members[index].is_friend,
		"a member not a friend shown with their code's characters");
	check(party_friend_id(1, 3, id), "Delta is Bravo's friend");
	use(1);
	check(p2p_party_invite(id) == P2P_PARTY_NOT_LEADER, "a member who does not lead cannot invite");
	/* the friends' lists show the party */
	party_pass(4);
	{
		struct p2p_friend_entry entry;
		int found = 0, nth;

		for (nth = 0; entry_of(3, P2P_FRIEND_KIND_FRIEND, nth, &entry); nth++)
			found |= !strcmp(entry.name, "Bravo") && strstr(entry.party, "party (3 of 8)") != NULL;
		check(found, "Bravo's friend Delta sees Bravo in a party of 3");
	}
	use(0);
	check(p2p_party_join_warning(14, 16, text, sizeof(text)) && strstr(text, "3 players") &&
		!p2p_party_join_warning(10, 16, NULL, 0), "the leader is warned of a game with fewer open slots than the party");

	/* chat: cleaned, shown with the friends list's names, the presets */
	use(1);
	check(p2p_party_chat_send("  hi |%^ <b>\x01\x7f team  ") == P2P_PARTY_DONE, "Bravo chats");
	party_pass(2);
	party_last_line(2, &line);
	check(!strcmp(line.text, "hi b team") && strstr(line.name, "Bravo #"), "Charlie reads it cleaned, Bravo's name tagged");
	party_last_line(0, &line);
	check(!strcmp(line.text, "hi b team") && !strcmp(line.name, "Bravo"), "Alpha reads it under the friend's name");
	check(party_lines_from(3, "hi b team") == 0, "Delta, not in the party, reads nothing");
	use(2);
	check(p2p_party_chat_preset(0) == P2P_PARTY_DONE, "Charlie's quick chat");
	party_pass(2);
	party_last_line(0, &line);
	check(!strcmp(line.text, "Ready!"), "a preset shown");
	{
		char longer[300];

		memset(longer, 'x', sizeof(longer) - 1);
		longer[sizeof(longer) - 1] = 0;
		clock_now += 10000;
		use(1);
		check(p2p_party_chat_send(longer) == P2P_PARTY_DONE, "a long message sent");
		party_pass(2);
		party_last_line(0, &line);
		check(strlen(line.text) == P2P_PARTY_CHAT_LENGTH, "cut to 120 characters");
		use(1);
		check(p2p_party_chat_send("\x01\x02 |%") == P2P_PARTY_EMPTY, "nothing left once cleaned: not sent");
	}
	clock_now += 10000;
	use(1);
	for (index = 0, before = 0; index < 10; index++)
		before += p2p_party_chat_send("flood") == P2P_PARTY_DONE;
	check(before == 3, "a sender's flood stopped at 3");
	party_pass(2);
	/* (a flood sent past the sender's limit: the receiver takes 5) */
	clock_now += 10000;
	reset_drops(2);
	for (index = 0; index < 9; index++)
	{
		use(1);
		machines[1].chat_send_budget.left = 3;
		machines[1].chat_send_budget.time = clock_now;
		snprintf(text, sizeof(text), "burst %d", index);
		p2p_party_chat_send(text);
	}
	party_pass(2);
	check(party_lines_from(2, "burst") == 5 && machines[2].dropped_rate + machines[2].total_rate >= 4,
		"a member's flood capped at the receiver");
	/* mute: here only */
	check(party_member_id(0, 1, id), "Bravo's member id");
	use(0);
	check(p2p_party_mute(id, 1) == P2P_PARTY_DONE, "Alpha mutes Bravo");
	clock_now += 12000;
	use(1);
	p2p_party_chat_send("muted words");
	party_pass(2);
	check(party_lines_from(0, "muted words") == 0 && party_lines_from(2, "muted words") == 1,
		"a muted member's chat hidden here, shown to the others");
	use(0);
	p2p_party_mute(id, 0);

	/* replays and forgeries */
	reset_drops(2);
	before = party_lines_from(2, "muted words");
	for (index = 0; index < carried_count; index++)
		if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[2].mailbox_topic))
			hand(2, carried[index].topic, carried[index].payload, carried[index].size);
	party_pass(2);
	check(party_lines_from(2, "muted words") == before && machines[2].dropped_replays + machines[2].total_replays >= 3,
		"Bravo's party messages replayed to Charlie: dropped");
	reset_drops(0);
	reset_drops(2);
	use(1);
	party_test_forge();
	party_pass(2);
	check(party_lines_from(0, "forged chat") == 0 && party_lines_from(2, "forged chat") == 0,
		"a chat signed by another key dropped");
	check(party_leader_of(0) == 0 && party_leader_of(2) == 0, "a roster from a member not the leader dropped");
	check(machines[0].dropped_forged + machines[0].total_forged + machines[0].dropped_replays + machines[0].total_replays
		>= 2, "the forgeries counted");
	/* a roster with a key twice (from the leader itself): malformed */
	{
		struct message message;

		reset_drops(2);
		use(0);
		party_start_message(&message, TYPE_PARTY_ROSTER, NULL);
		message.party_serial = machines[0].party.serial + 5;
		message.party_count = 3;
		message.party_leader = 0;
		memcpy(message.party_keys[0], machines[0].key, P2P_KEY_SIZE);
		memcpy(message.party_keys[1], machines[1].key, P2P_KEY_SIZE);
		memcpy(message.party_keys[2], machines[1].key, P2P_KEY_SIZE);
		party_broadcast(&message);
		party_pass(2);
		check(machines[2].dropped_malformed + machines[2].total_malformed >= 1 && party_count_of(2, NULL) == 3,
			"a roster naming one member twice dropped");
	}
	/* an eavesdropper (Delta) handed the party's messages to Charlie opens none */
	reset_drops(3);
	for (index = 0, before = 0; index < carried_count && before < 6; index++)
		if (carried[index].from == 1 && !strcmp(carried[index].topic, machines[2].mailbox_topic))
		{
			use(3);
			p2p_friends_heard(P2P_FRIENDS_MAILBOX, carried[index].payload, carried[index].size);
			before++;
		}
	use(3);
	p2p_friends_update(&hosts[3]);
	check(before > 0 && party_count_of(3, NULL) == 0 && machines[3].dropped_forged + machines[3].dropped_rate >= before,
		"an eavesdropper opens none of the party's messages");

	/* the follow: Alpha hosts */
	hosts[0].hosting = 1;
	for (index = 0; index < P2P_KEY_HASH_SIZE; index++)
		hosts[0].key_hash[index] = (unsigned char)(0x30 + index);
	for (index = 0; index < P2P_TOKEN_SIZE; index++)
		hosts[0].token[index] = (unsigned char)(0x50 + index);
	hosts[0].open = 1;
	hosts[0].players = 1;
	hosts[0].maximum = 16;
	use(0);
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 1, 16);
	party_pass(4);
	use(1);
	check(p2p_party_follow_pending(&follow) && follow.hosting && !strcmp(follow.leader, "Alpha") &&
		!strcmp(follow.activity, "Slayer on Blood Gulch"), "Bravo is told to follow Alpha's game");
	invite_link(hosts[0].key_hash, hosts[0].token, expected);
	join_count = 0;
	use(1);
	check(p2p_party_follow_join(follow.serial) == P2P_PARTY_FOLLOW_JOINING && join_count == 1 &&
		!strcmp(joined_link, expected), "the follow joins Alpha's invite, as an invite accepted");
	use(1);
	check(!p2p_party_follow_pending(NULL), "a follow joined once");
	use(2);
	check(p2p_party_follow_pending(&follow), "Charlie told too");
	/* a password: never passed on */
	hosts[0].locked = 1;
	party_pass(4);
	use(2);
	check(!p2p_party_follow_pending(NULL) && machines[2].party.follow_result == P2P_PARTY_FOLLOW_PASSWORD,
		"a game with a password: not followed, Charlie told why");
	party_pass(42);
	count = party_count_of(0, members);
	for (index = 0; index < count; index++)
		if (strstr(members[index].name, "Charlie"))
			break;
	check(index < count && strstr(members[index].note, "password"), "the leader sees why Charlie did not follow");
	{
		int token_seen = 0, at;

		for (at = 0; at < carried_count; at++)
			token_seen |= carried[at].from == 0 && memmem(carried[at].payload, (size_t)carried[at].size, hosts[0].token,
				P2P_TOKEN_SIZE) != NULL;
		check(!token_seen, "the invite never travels in clear");
	}
	/* Alpha joins another's game: that game's invite, unless it is locked */
	hosts[0].hosting = 0;
	hosts[0].locked = 0;
	hosts[0].joined = 1;
	for (index = 0; index < P2P_KEY_HASH_SIZE; index++)
		hosts[0].joined_hash[index] = (unsigned char)(0x70 + index);
	for (index = 0; index < P2P_TOKEN_SIZE; index++)
		hosts[0].joined_token[index] = (unsigned char)(0x90 + index);
	use(0);
	p2p_friends_set_game(3, 0, NULL, 0);
	party_pass(4);
	use(2);
	check(p2p_party_follow_pending(&follow) && !follow.hosting, "Charlie told to follow Alpha into another's game");
	/* (Bravo is there already) */
	hosts[1].joined = 1;
	memcpy(hosts[1].joined_hash, hosts[0].joined_hash, P2P_KEY_HASH_SIZE);
	hosts[0].joined_locked = 1;
	party_pass(4);
	use(2);
	check(!p2p_party_follow_pending(NULL) && machines[2].party.follow_result == P2P_PARTY_FOLLOW_PASSWORD,
		"another's locked game: not followed");
	hosts[0].joined_locked = 0;
	hosts[0].joined_hash[0] ^= 1;
	hosts[1].joined_hash[0] = hosts[0].joined_hash[0];
	/* (there: in its game - machine 2 of it) */
	use(1);
	p2p_friends_set_game(2, 0, NULL, 0);
	party_pass(4);
	use(1);
	check(!p2p_party_follow_pending(NULL) && machines[1].party.follow_result == P2P_PARTY_FOLLOW_THERE,
		"a member in that game already: nothing to join");
	use(2);
	check(p2p_party_follow_pending(&follow), "Charlie told again");
	p2p_party_follow_result(follow.serial, P2P_PARTY_FOLLOW_WAITING);
	check(p2p_party_follow_pending(NULL), "in another game: the follow waits");
	p2p_party_follow_result(follow.serial, P2P_PARTY_FOLLOW_NO_MAP);
	check(!p2p_party_follow_pending(NULL), "a map missing: the follow given up, the party told");
	hosts[0].joined = 0;
	hosts[1].joined = 0;
	use(1);
	p2p_friends_set_game(-1, 0, NULL, 0);
	use(0);
	p2p_friends_set_game(-1, 0, NULL, 0);
	/* (insider.21) the leader back at the menus: the members told so, no
	error, nothing to join */
	party_pass(4);
	use(1);
	check(!p2p_party_follow_pending(NULL) && machines[1].party.follow_result == P2P_PARTY_FOLLOW_NONE &&
		party_notices_with(1, "Alpha is back in the menus") == 1, "the leader back at the menus: Bravo told, no error");
	use(2);
	check(!p2p_party_follow_pending(NULL) && machines[2].party.follow_result == P2P_PARTY_FOLLOW_NONE &&
		party_notices_with(2, "Alpha is back in the menus") == 1 && party_notices_with(2, "reached online") == 0,
		"... Charlie told too, never \"can't be reached online\"");
	party_pass(64);
	check(party_notices_with(1, "Alpha is back in the menus") == 1, "the menus said once (the follow sent again, not taken again)");
	/* the leader hosts, Bravo follows into it (machine 1 of it), then the
	leader leaves its lobby: its game's client outlives its hosting a moment
	(insider.21: "in a game no member can reach" sent then) */
	hosts[0].hosting = 1;
	hosts[0].locked = 0;
	use(0);
	p2p_friends_set_game(0, 1, NULL, 0);
	party_pass(4);
	use(1);
	check(p2p_party_follow_pending(&follow) && follow.hosting, "the leader hosts: Bravo told to follow");
	join_count = 0;
	check(p2p_party_follow_join(follow.serial) == P2P_PARTY_FOLLOW_JOINING && join_count == 1, "Bravo follows");
	hosts[1].joined = 1;
	memcpy(hosts[1].joined_hash, hosts[0].key_hash, P2P_KEY_HASH_SIZE);
	memcpy(hosts[1].joined_token, hosts[0].token, P2P_TOKEN_SIZE);
	use(1);
	p2p_friends_set_game(1, 0, NULL, 0);
	use(2);
	p2p_party_follow_result(machines[2].party.follow_heard, P2P_PARTY_FOLLOW_WAITING);
	party_pass(2);
	use(1);
	before = (int)machines[0].party.follow_serial;
	hosts[0].hosting = 0;
	party_pass(4);
	check(machines[0].party.follow_flags != PARTY_FOLLOW_UNREACHABLE && (int)machines[0].party.follow_serial == before,
		"the leader's lobby closed, its client a moment longer: no \"no member can reach\" follow");
	use(1);
	check(p2p_party_host_left_quiet(), "Bravo, in the leader's game as it closed: no error (the party follows the leader)");
	use(0);
	p2p_friends_set_game(-1, 0, NULL, 0);
	party_pass(4);
	check(machines[0].party.follow_flags == PARTY_FOLLOW_MENUS, "then the leader at the menus: the members told so");
	use(1);
	p2p_friends_set_game(-1, 0, NULL, 0);
	use(2);
	check(party_notices_with(2, "reached online") == 0 && party_notices_with(2, "Alpha is back in the menus") == 2,
		"Charlie: back in the menus again, never \"can't be reached\"");
	/* the leader hosts again, on the same invite (its key's): Bravo, at the
	menus with its peer still connected, follows the new game (insider.21:
	"there already", never joined) */
	hosts[0].hosting = 1;
	use(0);
	p2p_friends_set_game(0, 1, NULL, 0);
	party_pass(4);
	use(1);
	check(p2p_party_follow_pending(&follow) && follow.hosting && machines[1].party.follow_result != P2P_PARTY_FOLLOW_THERE,
		"the leader hosts again: Bravo (at the menus, the host's peer kept) follows the new game");
	join_count = 0;
	check(p2p_party_follow_join(follow.serial) == P2P_PARTY_FOLLOW_JOINING && join_count == 1 &&
		!strcmp(joined_link, expected), "... through its invite's join");
	/* a game no member reaches (system link), once it has lasted */
	hosts[0].hosting = 0;
	hosts[1].joined = 0;
	use(0);
	p2p_friends_set_game(0, 1, NULL, 0);
	party_pass(10);
	check(machines[0].party.follow_flags == PARTY_FOLLOW_UNREACHABLE, "a game no member reaches, lasting: said");
	use(2);
	check(machines[2].party.follow_result == P2P_PARTY_FOLLOW_UNREACHABLE, "... Charlie told why it can't follow");
	use(0);
	p2p_friends_set_game(-1, 0, NULL, 0);
	party_pass(4);
	/* the same message through a second broker: dropped, counted, not logged */
	{
		int at, handed = 0;

		replay_logs = 0;
		reset_drops(1);
		use(0);
		check(p2p_party_chat_send("twice") == P2P_PARTY_DONE, "Alpha says something");
		use(0);
		p2p_friends_update(&hosts[0]);
		broker();
		for (at = 0; at < carried_count; at++)
			if (carried[at].from == 0 && !strcmp(carried[at].topic, machines[1].mailbox_topic))
				handed = at;
		hand(1, carried[handed].topic, carried[handed].payload, carried[handed].size);
		use(1);
		p2p_friends_update(&hosts[1]);
		check(party_lines_from(1, "twice") == 1 && machines[1].dropped_replays + machines[1].total_replays >= 1 &&
			replay_logs == 0, "a copy through another broker: dropped and counted, not logged");
	}

	/* leave, hand-off, kick */
	use(2);
	check(p2p_party_leave() == P2P_PARTY_DONE, "Charlie leaves");
	party_pass(2);
	check(party_count_of(0, NULL) == 2 && party_count_of(1, NULL) == 2 && party_count_of(2, NULL) == 0,
		"Charlie gone on every side");
	use(0);
	check(p2p_party_leave() == P2P_PARTY_DONE, "Alpha, the leader, leaves");
	party_pass(4);
	check(party_count_of(1, NULL) == 0, "Bravo alone: no party");
	/* a party of Alpha, Bravo, Charlie again; Bravo invites Delta once he leads */
	clock_now += 20000;
	check(party_friend_id(0, 1, id) && (use(0), p2p_party_invite(id)) == P2P_PARTY_DONE, "Alpha invites Bravo again");
	check(party_friend_id(0, 2, id) && (use(0), p2p_party_invite(id)) == P2P_PARTY_DONE, "and Charlie");
	party_pass(2);
	party_answer_all(1, 1);
	party_answer_all(2, 1);
	party_pass(4);
	check(party_count_of(2, NULL) == 3, "the party of three again");
	party_pass(50);
	{
		int b = party_count_of(1, members), at, online = 0;

		for (at = 0; at < b; at++)
			online += members[at].online;
		check(online == 3, "every member heard from, friends or not");
	}
	/* Alpha goes offline at once (no word): after the timeout Bravo, the
	longest-standing, leads */
	party_offline = 0;
	party_pass(PARTY_TIMEOUT / 500 + 8);
	check(party_leader_of(1) == 1 && party_leader_of(2) == 1 && party_count_of(1, NULL) == 2,
		"the leader gone silent: Bravo leads on both machines");
	party_offline = -1;
	/* (Bravo, leading, is Charlie's party member, not his friend: Charlie
	follows him into no game by himself; security review F-07) */
	{
		struct p2p_party_follow follow;

		int at;

		hosts[1].hosting = 1;
		for (at = 0; at < P2P_KEY_HASH_SIZE; at++)
			hosts[1].key_hash[at] = (unsigned char)(0x60 + at);
		for (at = 0; at < P2P_TOKEN_SIZE; at++)
			hosts[1].token[at] = (unsigned char)(0x70 + at);
		hosts[1].open = 1;
		hosts[1].players = 1;
		hosts[1].maximum = 16;
		use(1);
		p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 1, 16);
		party_pass(4);
		check(!p2p_party_follow_pending(NULL) && (use(2), !p2p_party_follow_pending(&follow)) &&
			machines[2].party.follow_result == P2P_PARTY_FOLLOW_FAILED,
			"a leader not Charlie's friend: not followed by himself");
		hosts[1].hosting = 0;
		use(1);
		p2p_friends_set_activity(P2P_FRIEND_STATE_MENUS, "", "", 0, -1, 0, 0, 0);
		party_pass(2);
	}
	check(party_friend_id(1, 3, id) && (use(1), p2p_party_invite(id)) == P2P_PARTY_DONE, "Bravo, leading, invites Delta");
	party_pass(2);
	party_answer_all(3, 1);
	party_pass(4);
	check(party_count_of(3, NULL) == 3 && party_count_of(2, NULL) == 3, "Delta joins Bravo's party");
	check(party_member_id(1, 3, id) && (use(1), p2p_party_kick(id)) == P2P_PARTY_DONE, "Bravo removes Delta");
	use(2);
	check(party_member_id(2, 1, id) && p2p_party_kick(id) == P2P_PARTY_NOT_LEADER, "a member who does not lead cannot");
	party_pass(4);
	check(party_count_of(3, NULL) == 0 && party_count_of(2, NULL) == 2, "Delta out of the party on every side");
	/* appearing offline: the party left, nothing taken */
	machines[2].test_appeared_offline = 1;
	party_pass(4);
	check(party_count_of(2, NULL) == 0 && party_count_of(1, NULL) == 0, "appearing offline leaves the party");
	clock_now += 20000;
	check(party_friend_id(0, 2, id) && (use(0), p2p_party_invite(id)) != P2P_PARTY_DONE,
		"a friend appearing offline (offline to Alpha) not invited");
	{
		struct message message;

		use(0);
		message_start(&message, TYPE_PARTY_INVITE, machines[2].key);
		posix_random_bytes(message.party_id, PARTY_ID_LENGTH);
		message.party_count = 1;
		party_send_to(machines[2].key, &message);
		party_pass(2);
		use(2);
		check(machines[2].party_invite_count == 0, "an invite to a machine appearing offline taken nowhere");
	}
	machines[2].test_appeared_offline = 0;
	/* a blocked member: the leader removes them */
	party_pass(10);
	clock_now += 20000;
	check(party_friend_id(0, 1, id) && (use(0), p2p_party_invite(id)) == P2P_PARTY_DONE &&
		party_friend_id(0, 2, id) && (use(0), p2p_party_invite(id)) == P2P_PARTY_DONE, "a party of three once more");
	party_pass(2);
	party_answer_all(1, 1);
	party_answer_all(2, 1);
	party_pass(4);
	check(party_friend_id(0, 2, id) && (use(0), p2p_friends_act(id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_BLOCK)) ==
		P2P_FRIENDS_DONE, "Alpha blocks Charlie");
	party_pass(4);
	check(party_count_of(0, NULL) == 2 && party_count_of(2, NULL) == 0, "the blocked member removed from the party");
	/* the decline */
	use(0);
	p2p_friends_act(id, P2P_FRIEND_KIND_BLOCKED, P2P_FRIENDS_UNBLOCK);
	party_reset();
	party_make_friends(0, 1);
	party_pass(10);
	check(party_friend_id(0, 1, id) && (use(0), p2p_party_invite(id)) == P2P_PARTY_DONE, "an invite to decline");
	party_pass(2);
	check(party_answer_all(1, 0) == 1, "Bravo says no");
	party_pass(2);
	check(party_count_of(1, NULL) == 0 && party_lines_from(0, "") == 0, "no party for Bravo");
	party_pass(PARTY_INVITE_LIFETIME / 500 + 4);
	check(party_count_of(0, NULL) == 0, "Alpha alone once its invites are over: no party");
	/* chat cleaning, alone */
	check(p2p_party_chat_clean(text, sizeof(text), "a\tb\nc\x80\xff%n|x^1<i>`") == 7 && !strcmp(text, "abcnx1i"),
		"chat cleaning keeps printable ASCII without markup");
}

/* (run_menus_test.sh's FRIENDS cases) a save folder's friends: SEED_DIR
gets an identity and a list of two friends, a request, a request sent and a
blocked player, as a Vita's would be */
static int seed(const char *folder)
{
	static const char *const names[] = { "Bravo", "Charlie", "Delta", "Echo" };
	unsigned char keys[4][P2P_KEY_SIZE], seeds[4][P2P_SEED_SIZE], friendship[FRIENDSHIP_LENGTH];
	char code[P2P_FRIEND_CODE_LENGTH + 1];
	int index;

	snprintf(roots[0], sizeof(roots[0]), "%s", folder);
	use(0);
	quiet = 1;
	p2p_friends_code(code, sizeof(code));
	for (index = 0; index < 4; index++)
	{
		memset(seeds[index], 0x40 + index, P2P_SEED_SIZE);
		p2p_ed25519_public(seeds[index], keys[index], NULL);
	}
	memset(friendship, 9, sizeof(friendship));
	friend_add(keys[0], names[0], friendship, 0, 0);
	friend_add(keys[1], names[1], friendship, 0, 0);
	memcpy(machines[0].requests[0].key, keys[2], P2P_KEY_SIZE);
	snprintf(machines[0].requests[0].name, sizeof(machines[0].requests[0].name), "%s", names[2]);
	machines[0].requests[0].time = unix_now();
	machines[0].request_count = 1;
	memcpy(machines[0].blocked[0].key, keys[3], P2P_KEY_SIZE);
	snprintf(machines[0].blocked[0].name, sizeof(machines[0].blocked[0].name), "%s", names[3]);
	machines[0].blocked_count = 1;
	memset(seeds[0], 0x50, P2P_SEED_SIZE);
	p2p_ed25519_public(seeds[0], keys[0], NULL);
	code_of_key(keys[0], machines[0].outgoing[0].code);
	memset(machines[0].outgoing[0].nonce, 0x11, NONCE_LENGTH);
	machines[0].outgoing[0].time = unix_now();
	machines[0].outgoing_count = 1;
	machines[0].dirty = 1;
	save(1);
	printf("seeded %s/friends: %d friends, %d request, %d sent, %d blocked\n", folder, machines[0].friend_count,
		machines[0].request_count, machines[0].outgoing_count, machines[0].blocked_count);
	return 0;
}

/* the wire a friend's game is joined on (network_wire.h): this build's own
by its version (18, the wire bit; the crossplay wire's version for a Vita's
game for Vitas and PC), a PC friend's (OpenCE's friends) by its version,
on the crossplay wire when that wire joins it; the friends' topics hcev's */
static void test_wires(void)
{
	char topic[TOPIC_LENGTH + 1];
	unsigned char key[P2P_KEY_SIZE] = { 1, 2, 3 };

	check(game_wire_of(HALO_PORT_NETWORK_VERSION, 0) == 0, "a friend's Vita game (18): the legacy wire");
	check(game_wire_of(HALO_PORT_NETWORK_VERSION, 1) == 1, "a game of 18 with the wire bit: the crossplay wire");
	check(game_wire_of(HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1) == 1 && game_wire_of(HALO_PORT_CROSSPLAY_NETWORK_VERSION, 0) == 1,
		"a PC friend's game (OpenCE's 32), or a Vita's for Vitas and PC: the crossplay wire, joinable");
	check(game_wire_of(29, 0) == 1 && game_wire_of(29, 1) == 1,
		"a PC friend's game of 29 (ChupathingyCE, OpenCE build-206): the crossplay wire, joinable");
	check(game_wire_of(28, 0) == -1 && game_wire_of(30, 1) == -1 && game_wire_of(31, 1) == -1 && game_wire_of(33, 1) == -1 &&
		game_wire_of(19, 0) == -1 && game_wire_of(0, 0) == -1,
		"a version neither wire joins (28, 30, 31, 33, 19, 0): not joinable");
	check(network_wire_version_of(1) == HALO_PORT_CROSSPLAY_NETWORK_VERSION && network_wire_version_of(0) == 18,
		"the version a presence says: its game's wire's");
	mailbox_topic(key, topic);
	check(!strncmp(topic, "hcev/f1/m/", 10), "the friends' topics are hcev/f1's on every build");
}

/* a friend whose game closed and started again (a new run: the same
identity, a newer epoch, nothing of the party kept) is invited again at once,
the invite heard and the party joined, with every message carried by two
brokers (each heard twice: the copies dropped as replays, nothing new
dropped) - insider.18: the invite sent to the run that closed stood for two
minutes, a new one refused as too soon, so "I didn't get it, try again?" got
nothing */
static void test_party_restart(void)
{
	char id[P2P_FRIEND_ID_SIZE];
	struct p2p_party_follow follow;
	unsigned long long epoch_before;

	party_reset();
	broker_copies = 2;
	use(0);
	p2p_friends_set_name("Alpha");
	use(1);
	p2p_friends_set_name("Bravo");
	party_make_friends(0, 1);
	party_pass(10);
	check(party_friend_id(0, 1, id) && p2p_party_invite(id) == P2P_PARTY_DONE, "restart: Alpha invites Bravo");
	party_pass(2);
	use(1);
	check(friends->party_invite_count == 1, "restart: Bravo heard the invite once (its copy dropped)");
	check(drops(1) > 0, "restart: the second broker's copies dropped as replays");
	/* Bravo's game closes before answering, and starts again */
	use(1);
	save(1);
	epoch_before = machines[1].epoch;
	memset(&machines[1], 0, sizeof(machines[1]));
	memset(&hosts[1], 0, sizeof(hosts[1]));
	hosts[1].online = 1;
	use(1);
	p2p_friends_set_game(-1, 0, NULL, 0);
	/* (its profile not chosen yet: its first presences carry no name, never
	the lobby's) */
	lobby_name_setting = "Bloodgulch ctf";
	party_pass(1);
	use(1);
	check(machines[1].epoch > epoch_before && friends->party_invite_count == 0, "restart: Bravo's new run, no invite");
	party_pass(4);
	use(0);
	check(find_friend(machines[1].key) && !strcmp(find_friend(machines[1].key)->name, "Bravo"),
		"restart: Bravo's name kept before their profile's is said (not the lobby's)");
	lobby_name_setting = "";
	use(1);
	p2p_friends_set_name("Bravo");
	party_pass(10);
	use(0);
	check(find_friend(machines[1].key) && find_friend(machines[1].key)->presence.online, "restart: Bravo online again");
	check(party_friend_id(0, 1, id) && p2p_party_invite(id) == P2P_PARTY_DONE,
		"restart: Alpha invites Bravo's new run at once");
	party_pass(2);
	use(1);
	check(friends->party_invite_count == 1, "restart: the new run heard the invite");
	check(party_answer_all(1, 1) == 1, "restart: Bravo accepts");
	party_pass(4);
	check(party_count_of(0, NULL) == 2 && party_count_of(1, NULL) == 2, "restart: a party of two on each machine");
	/* the leader hosts: the member is told to follow, through both brokers */
	{
		char expected[P2P_LINK_SIZE];
		int index;

		hosts[0].hosting = 1;
		for (index = 0; index < P2P_KEY_HASH_SIZE; index++)
			hosts[0].key_hash[index] = (unsigned char)(0x31 + index);
		for (index = 0; index < P2P_TOKEN_SIZE; index++)
			hosts[0].token[index] = (unsigned char)(0x51 + index);
		hosts[0].open = 1;
		hosts[0].players = 1;
		hosts[0].maximum = 16;
		use(0);
		p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Forge", 2, -1, 0, 1, 16);
		party_pass(4);
		use(1);
		check(p2p_party_follow_pending(&follow) && follow.hosting, "restart: the member is told to follow the leader");
		invite_link(hosts[0].key_hash, hosts[0].token, expected);
		join_count = 0;
		use(1);
		check(p2p_party_follow_join(follow.serial) == P2P_PARTY_FOLLOW_JOINING && join_count == 1 &&
			!strcmp(joined_link, expected), "restart: the follow joins the leader's invite");
		hosts[0].hosting = hosts[0].open = 0;
	}
	broker_copies = 1;
}

/* ---------- the security review's follow-ups (1.1.1-beta.1 F-05, F-06,
F-10) */

/* identities that are no machine of the test's: requests to a machine's
code, signed and sealed as a stranger's game would */
static struct friends_state strangers[2];

static void stranger_make(int which, unsigned long long epoch)
{
	struct friends_state *was = friends;

	memset(&strangers[which], 0, sizeof(strangers[which]));
	friends = &strangers[which];
	posix_random_bytes(friends->seed, sizeof(friends->seed));
	identity_from_seed();
	friends->epoch = epoch;
	friends = was;
}

/* a request of a stranger's (or a new key's, with stranger NULL) to a
machine; its payload and topic */
static int stranger_request(struct friends_state *stranger, int to, const char *name, unsigned char *payload,
	char *topic)
{
	struct friends_state *was = friends;
	static struct friends_state fresh;
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE], seal_key[P2P_SHA256_SIZE], recipient[P2P_SHA256_SIZE];
	int size;

	if (!stranger)
	{
		memset(&fresh, 0, sizeof(fresh));
		stranger = &fresh;
		friends = stranger;
		posix_random_bytes(friends->seed, sizeof(friends->seed));
		identity_from_seed();
		friends->epoch = 1700000000ULL;
	}
	friends = stranger;
	code_derive(machines[to].code, topic, seal_key, recipient);
	message_start(&message, TYPE_REQUEST, recipient);
	snprintf(message.name, sizeof(message.name), "%s", name);
	posix_random_bytes(message.nonce, NONCE_LENGTH);
	size = plain_encode(&message, plain);
	size = envelope_seal(KIND_REQUEST, seal_key, plain, size, payload);
	friends = was;
	return size;
}

/* a machine's request of that name: its index on the list (-1 none) */
static int request_named(int machine, const char *name, struct p2p_friend_entry *entry)
{
	int nth;

	for (nth = 0; entry_of(machine, P2P_FRIEND_KIND_REQUEST, nth, entry); nth++)
		if (!strcmp(entry->name, name))
			return nth;
	return -1;
}

/* requests of new keys, one every two seconds (the strangers' budget), so
that each is heard: the replay ring of 64 turned over */
static void strangers_heard(int to, int count)
{
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	int index, size;

	for (index = 0; index < count; index++)
	{
		size = stranger_request(NULL, to, "Passerby", payload, topic);
		hand(to, topic, payload, size);
		pass(4);
	}
}

/* a machine's new run: its state read back from its files */
static void restart(int machine)
{
	char code[P2P_FRIEND_CODE_SIZE];

	use(machine);
	friends->dirty = 1;
	save(1);
	memset(&machines[machine], 0, sizeof(machines[machine]));
	code_of(machine, code);
}

static void test_request_replays(void)
{
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE], echo[MAXIMUM_PAYLOAD_SIZE], bravo[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH], echo_topic[TOPIC_LENGTH];
	struct p2p_friend_entry entry;
	int size, echo_size, bravo_size = 0, index;

	/* Alpha's first request to Bravo (they are friends since), as the
	broker carried it */
	for (index = 0; index < carried_count && !bravo_size; index++)
		if (carried[index].from == 0 && !strcmp(carried[index].topic, machines[1].inbox_topic))
		{
			memcpy(bravo, carried[index].payload, (size_t)carried[index].size);
			bravo_size = carried[index].size;
		}
	check(bravo_size > 0, "F-10: Alpha's old request carried");
	use(1);
	check(find_friend(machines[0].key) != NULL, "F-10: Alpha a friend of Bravo's");
	use(0);
	machines[0].request_count = 0;
	machines[0].muted_count = 0;
	/* (the budgets the flood before spent, earned again) */
	pass(40);
	/* Echo asks Alpha; Alpha declines */
	stranger_make(0, 1700000100ULL);
	echo_size = stranger_request(&strangers[0], 0, "Echo", echo, echo_topic);
	hand(0, echo_topic, echo, echo_size);
	pass(2);
	check(request_named(0, "Echo", &entry) >= 0, "F-10: Echo's request heard");
	use(0);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_DECLINE) == P2P_FRIENDS_DONE, "F-10: declined");
	/* a week on (the mute over), and 70 other strangers' requests heard
	since: Echo's old request replayed off the broker */
	use(0);
	machines[0].muted_count = 0;
	strangers_heard(0, 70);
	use(0);
	machines[0].request_count = 0;
	reset_drops(0);
	hand(0, echo_topic, echo, echo_size);
	pass(2);
	check(request_named(0, "Echo", &entry) < 0, "F-10: a declined request replayed after 64 others: not shown again");
	check(machines[0].dropped_replays + machines[0].total_replays >= 1, "F-10: and counted a replay");
	/* Alpha's old request replayed to Bravo (64 others since: the ring
	turned over): no answer again, nothing changed */
	strangers_heard(1, 70);
	use(1);
	machines[1].request_count = 0;
	pass(2);
	reset_drops(1);
	hand(1, machines[1].inbox_topic, bravo, bravo_size);
	use(1);
	p2p_friends_update(&hosts[1]);
	check(machines[1].dropped_replays + machines[1].total_replays >= 1 && machines[1].outbound_count == 0,
		"F-10: a friend's old request replayed: dropped, not answered");
	machines[1].outbound_count = 0;
	/* a new run of Alpha's (the ring empty): the replay again */
	restart(0);
	hand(0, echo_topic, echo, echo_size);
	pass(2);
	check(request_named(0, "Echo", &entry) < 0, "F-10: a declined request replayed after a restart: not shown");
	/* Echo asks again (a newer counter), then from a new run (a newer
	epoch): heard */
	size = stranger_request(&strangers[0], 0, "Echo", payload, topic);
	hand(0, topic, payload, size);
	pass(2);
	check(request_named(0, "Echo", &entry) >= 0, "F-10: Echo's new request heard");
	use(0);
	machines[0].request_count = 0;
	strangers[0].epoch++;
	strangers[0].counter = 0;
	size = stranger_request(&strangers[0], 0, "Echo", payload, topic);
	hand(0, topic, payload, size);
	pass(2);
	check(request_named(0, "Echo", &entry) >= 0, "F-10: Echo's request from a new run heard");
	use(0);
	machines[0].request_count = 0;
	machines[0].dirty = 1;
	save(1);
}

static void test_request_floods(void)
{
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	struct p2p_friend_entry entry;
	int size, index;

	use(0);
	machines[0].request_count = 0;
	/* 16 strangers fill Alpha's requests, then stop */
	for (index = 0; index < P2P_FRIEND_REQUESTS_MAXIMUM; index++)
	{
		size = stranger_request(NULL, 0, "Filler", payload, topic);
		hand(0, topic, payload, size);
		pass(4);
	}
	check(machines[0].request_count == P2P_FRIEND_REQUESTS_MAXIMUM, "F-06: the list filled");
	/* Foxtrot's request: no room yet */
	stranger_make(1, 1700000200ULL);
	size = stranger_request(&strangers[1], 0, "Foxtrot", payload, topic);
	hand(0, topic, payload, size);
	pass(4);
	check(request_named(0, "Foxtrot", &entry) < 0, "F-06: a full list takes no new request at once");
	/* the fillers silent past a sender's longest wait: Foxtrot's request
	sent again makes room */
	pass((REQUEST_IDLE_TIME + 2000) / 500);
	size = stranger_request(&strangers[1], 0, "Foxtrot", payload, topic);
	hand(0, topic, payload, size);
	pass(2);
	check(request_named(0, "Foxtrot", &entry) >= 0, "F-06: a list filled once and left takes a new request");
	/* a flood of new keys' requests in one pass: Foxtrot's request (a key
	known here now) sent again, renamed, and Bravo's messages to Alpha,
	still heard */
	use(0);
	reset_drops(0);
	for (index = 0; index < 200; index++)
	{
		size = stranger_request(NULL, 0, "Flood", payload, topic);
		hand(0, topic, payload, size);
	}
	size = stranger_request(&strangers[1], 0, "Foxtrot2", payload, topic);
	hand(0, topic, payload, size);
	for (index = 0; index < 10; index++)
	{
		use(0);
		p2p_friends_update(&hosts[0]);
	}
	check(request_named(0, "Foxtrot2", &entry) >= 0, "F-06: a known key's request heard through a flood");
	check(machines[0].request_count <= P2P_FRIEND_REQUESTS_MAXIMUM, "F-06: the list's room kept");
	{
		int queued_inbox = 0;

		for (index = 0; index < 200; index++)
		{
			size = stranger_request(NULL, 0, "Flood", payload, topic);
			hand(0, topic, payload, size);
		}
		use(0);
		for (index = 0; index < machines[0].inbound_count; index++)
			queued_inbox += machines[0].inbound[index].which == P2P_FRIENDS_INBOX;
		check(queued_inbox <= MAXIMUM_INBOUND_STRANGERS + MAXIMUM_INBOUND_KNOWN &&
			machines[0].inbound_count < MAXIMUM_INBOUND, "F-06: a flood leaves the friends' messages room");
		machines[0].inbound_count = 0;
	}
	/* Alpha and Charlie (strangers) both ask each other during a flood:
	friends */
	{
		char code_a[P2P_FRIEND_CODE_SIZE], code_c[P2P_FRIEND_CODE_SIZE];
		int step;

		code_of(0, code_a);
		code_of(2, code_c);
		use(2);
		machines[2].outgoing_count = 0;
		use(0);
		machines[0].outgoing_count = 0;
		if (find_friend(machines[2].key))
			friend_remove_index(find_friend_index(machines[2].key));
		while (find_blocked(machines[2].key) >= 0)
			machines[0].blocked_count = 0;
		use(2);
		while (find_blocked(machines[0].key) >= 0)
			machines[2].blocked_count = 0;
		if (find_friend(machines[0].key))
			friend_remove_index(find_friend_index(machines[0].key));
		machines[0].muted_count = 0;
		machines[2].muted_count = 0;
		machines[0].mark_count = 0;
		use(0);
		check(p2p_friends_add(code_c) == P2P_FRIENDS_ADD_SENT, "F-06: Alpha asks Charlie");
		for (step = 0; step < 40; step++)
		{
			for (index = 0; index < 30; index++)
			{
				size = stranger_request(NULL, 0, "Flood", payload, topic);
				hand(0, topic, payload, size);
			}
			if (step == 1)
			{
				use(2);
				p2p_friends_add(code_a);
			}
			pass(1);
		}
		use(0);
		check(find_friend(machines[2].key) != NULL, "F-06: two who both ask become friends through a flood");
	}
	use(0);
	machines[0].request_count = 0;
	machines[0].inbound_count = 0;
	machines[0].dirty = 1;
	save(1);
}

/* F-05: the host of a game presents one player's card as another's */
static void test_card_binding(void)
{
	unsigned long addresses[MACHINES];
	unsigned char ask[CARD_ANSWER_SIZE], answer[CARD_ANSWER_SIZE];
	int index, ask_size = 0, answer_size = 0;

	for (index = 0; index < MACHINES; index++)
	{
		addresses[index] = virtual_of(index);
		use(index);
		machines[index].test_appeared_offline = 0;
		memset(machines[index].machines, 0, sizeof(machines[index].machines));
		machines[index].card_outbound_count = machines[index].card_inbound_count = 0;
	}
	/* Alpha hosts (0); Bravo (1), Charlie (2) and Delta (3) joined */
	use(0);
	p2p_friends_set_game(0, 1, addresses, MACHINES);
	for (index = 1; index < MACHINES; index++)
	{
		use(index);
		p2p_friends_set_game(index, 0, NULL, 0);
	}
	pass(8);
	/* Bravo asks Delta's card; the host sends the ask to Charlie instead,
	and Charlie's answer back as Delta's */
	use(1);
	check(p2p_friends_player_ask(3, P2P_FRIENDS_PLAYER_ADD) == P2P_FRIENDS_DONE, "F-05: Bravo asks Delta's card");
	if (machines[1].card_outbound_count == 1)
	{
		ask_size = machines[1].card_outbound[0].size;
		memcpy(ask, machines[1].card_outbound[0].bytes, (size_t)ask_size);
	}
	machines[1].card_outbound_count = 0;
	check(ask_size == CARD_HEADER_SIZE, "F-05: the ask the same size as before (older hosts relay it)");
	ask[2] = 1;
	ask[3] = 2;
	use(2);
	p2p_friends_tunnel_heard(virtual_of(0), 1, ask, ask_size);
	p2p_friends_update(&hosts[2]);
	if (machines[2].card_outbound_count == 1)
	{
		answer_size = machines[2].card_outbound[0].size;
		memcpy(answer, machines[2].card_outbound[0].bytes, (size_t)answer_size);
	}
	machines[2].card_outbound_count = 0;
	check(answer_size == CARD_ANSWER_SIZE, "F-05: Charlie answered (the same size as before)");
	answer[2] = 3;
	reset_drops(1);
	use(1);
	p2p_friends_tunnel_heard(virtual_of(0), 1, answer, answer_size);
	p2p_friends_update(&hosts[1]);
	check(!machines[1].machines[3].has_card, "F-05: Charlie's card presented as Delta's: dropped");
	check(machines[1].dropped_forged + machines[1].total_forged >= 1, "F-05: and counted forged");
	machines[1].outbound_count = 0;
	machines[1].card_outbound_count = 0;
	/* the honest relay: Delta's own card taken, bound */
	clock_now += CARD_ASK_INTERVAL + 1000;
	machines[1].machines[3].asking = 0;
	machines[1].machines[3].asked_time = 0;
	use(1);
	check(p2p_friends_player_ask(3, P2P_FRIENDS_PLAYER_BLOCK) == P2P_FRIENDS_DONE, "F-05: Bravo asks again");
	game_pass(6);
	use(1);
	check(machines[1].machines[3].has_card && p2p_equal(machines[1].machines[3].key, machines[3].key, P2P_KEY_SIZE),
		"F-05: Delta's own card, relayed honestly, taken");
	/* an older build's ask (no mark in its nonce): answered as before, over
	the nonce alone, which an older asker checks */
	{
		unsigned char data[64 + CARD_ANSWER_SIZE];
		int data_size;

		memset(ask, 0, sizeof(ask));
		ask[0] = CARD_VERSION;
		ask[1] = CARD_ASK;
		ask[2] = 1;
		ask[3] = 2;
		posix_random_bytes(ask + 4, NONCE_LENGTH);
		ask[4] = 'x';
		use(2);
		clock_now += CARD_ANSWER_INTERVAL + 1000;
		p2p_friends_tunnel_heard(virtual_of(0), 1, ask, CARD_HEADER_SIZE);
		p2p_friends_update(&hosts[2]);
		check(machines[2].card_outbound_count == 1, "F-05: an older build's ask answered");
		if (machines[2].card_outbound_count == 1)
		{
			memcpy(answer, machines[2].card_outbound[0].bytes, CARD_ANSWER_SIZE);
			card_signed_data(answer, data, &data_size, 0);
			check(p2p_ed25519_verify(machines[2].key, data, data_size, answer + CARD_ANSWER_SIZE - P2P_SIGNATURE_SIZE),
				"F-05: ... signed as an older build checks it");
		}
		machines[2].card_outbound_count = 0;
		/* an older build's answer (signed over the nonce alone) to this
		build's ask: taken */
		use(1);
		machines[1].machines[2].asking = 0;
		machines[1].machines[2].asked_time = 0;
		machines[1].machines[2].has_card = 0;
		check(p2p_friends_player_ask(2, P2P_FRIENDS_PLAYER_BLOCK) == P2P_FRIENDS_DONE, "F-05: Bravo asks Charlie's card");
		machines[1].card_outbound_count = 0;
		memset(answer, 0, sizeof(answer));
		answer[0] = CARD_VERSION;
		answer[1] = CARD_ANSWER;
		answer[2] = 2;
		answer[3] = 1;
		memcpy(answer + 4, machines[1].machines[2].nonce, NONCE_LENGTH);
		memcpy(answer + CARD_HEADER_SIZE, machines[2].key, P2P_KEY_SIZE);
		card_signed_data(answer, data, &data_size, 0);
		p2p_ed25519_sign(machines[2].seed, machines[2].key, data, data_size, answer + CARD_ANSWER_SIZE - P2P_SIGNATURE_SIZE);
		use(1);
		p2p_friends_tunnel_heard(virtual_of(0), 1, answer, CARD_ANSWER_SIZE);
		p2p_friends_update(&hosts[1]);
		check(machines[1].machines[2].has_card, "F-05: an older build's card taken");
		machines[1].outbound_count = 0;
	}
	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		p2p_friends_set_game(-1, 0, NULL, 0);
		machines[index].card_outbound_count = 0;
	}
	use(1);
	machines[1].blocked_count = 0;
	machines[1].dirty = 1;
	save(1);
}

/* ---------- the tester's flaming helmet (p2p_flame.inc; tester_list.h) */

#ifdef FLAME_TEST_SEED
/* a list signed with the build's test key (FLAME_TEST_SEED its private
half), written to path */
static void flame_list_write(const char *path, const char *body)
{
	unsigned char seed[32], key[32], signature[64];
	const char *hex = FLAME_TEST_SEED;
	FILE *file;
	int index;

	for (index = 0; index < 32; index++)
	{
		unsigned int value;

		sscanf(hex + 2 * index, "%2x", &value);
		seed[index] = (unsigned char)value;
	}
	p2p_ed25519_public(seed, key, NULL);
	p2p_ed25519_sign(seed, key, body, (int)strlen(body), signature);
	file = fopen(path, "wb");
	if (!file)
		return;
	fputs(body, file);
	fputs("signature ", file);
	for (index = 0; index < 64; index++)
		fprintf(file, "%02x", signature[index]);
	fputs("\n", file);
	fclose(file);
}

static void flame_key_hex(int machine, char *text)
{
	int index;

	for (index = 0; index < P2P_KEY_SIZE; index++)
		sprintf(text + 2 * index, "%02x", machines[machine].key[index]);
}

static unsigned short flame_names[MACHINES][12];

static void flame_name(int machine, const char *name)
{
	int index;

	memset(flame_names[machine], 0, sizeof(flame_names[machine]));
	for (index = 0; name[index] && index < 12; index++)
		flame_names[machine][index] = (unsigned short)name[index];
}

/* the game's record told to every machine: claims[] each machine's claim */
static void flame_players(const int *claims)
{
	int index;

	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		p2p_flame_set_players(claims, claims, &flame_names[0][0], MACHINES, flame_names[index]);
	}
}

/* a flame packet of a machine's outbound queue (its first of a kind): its
size, else 0 */
static int flame_take(int machine, int kind, unsigned char *bytes)
{
	int index;

	for (index = 0; index < machines[machine].flame_outbound_count; index++)
		if (machines[machine].flame_outbound[index].bytes[1] == kind)
		{
			memcpy(bytes, machines[machine].flame_outbound[index].bytes, (size_t)machines[machine].flame_outbound[index].size);
			return machines[machine].flame_outbound[index].size;
		}
	return 0;
}

/* an answer for a nonce, as machine "claimant" (to "asker"), with a key and
a seed and a name of the caller's */
static void flame_make_answer(unsigned char *packet, int claimant, int asker, const unsigned char *nonce,
	const unsigned char *key, const unsigned char *seed, const unsigned short *name)
{
	unsigned char data[256], name_bytes[FLAME_NAME_SIZE];
	int size, unit;

	for (unit = 0; unit < 12; unit++)
	{
		name_bytes[2 * unit] = (unsigned char)(name[unit] & 0xFF);
		name_bytes[2 * unit + 1] = (unsigned char)(name[unit] >> 8);
	}
	packet[0] = CARD_VERSION;
	packet[1] = CARD_FLAME_ANSWER;
	packet[2] = (unsigned char)claimant;
	packet[3] = (unsigned char)asker;
	memcpy(packet + 4, nonce, NONCE_LENGTH);
	memcpy(packet + CARD_HEADER_SIZE, key, P2P_KEY_SIZE);
	size = flame_signed_data(claimant, asker, nonce, key, name_bytes, data);
	p2p_ed25519_sign(seed, key, data, size, packet + FLAME_ANSWER_SIZE - P2P_SIGNATURE_SIZE);
}

/* Delta (3) asks a machine afresh: its nonce */
static void flame_reask(int asker, int machine)
{
	struct card_machine *record = &machines[asker].machines[machine];

	record->flame_verified = record->flame_refused = record->flame_asking = 0;
	record->flame_tries = 0;
	record->flame_asked_time = 0;
	use(asker);
	machines[asker].flame_outbound_count = 0;
	pthread_mutex_lock(&friends_lock);
	flame_update();
	pthread_mutex_unlock(&friends_lock);
	machines[asker].flame_outbound_count = 0;
}

static void flame_hear(int machine, const unsigned char *packet)
{
	use(machine);
	p2p_flame_heard(-1, packet, FLAME_ANSWER_SIZE);
	p2p_flame_update();
	machines[machine].flame_outbound_count = 0;
}

/* a pass of every machine's flame work (the game's main loop's), its
packets carried as the game's messages: a joiner's to the host (which
reads the sender's machine itself), the host's to its destination */
static void flame_pass(int count, int host)
{
	int pass_index, from, index;

	for (pass_index = 0; pass_index < count; pass_index++)
	{
		clock_now += 500;
		for (from = 0; from < MACHINES; from++)
		{
			use(from);
			p2p_flame_update();
		}
		for (from = 0; from < MACHINES; from++)
		{
			struct flame_packet packets[MAXIMUM_FLAME_PACKETS];
			int count_out = machines[from].flame_outbound_count;

			memcpy(packets, machines[from].flame_outbound, sizeof(packets[0]) * (size_t)count_out);
			machines[from].flame_outbound_count = 0;
			for (index = 0; index < count_out; index++)
			{
				int to = from == host ? packets[index].machine : host;

				if (to < 0 || to >= MACHINES || to == from)
					continue;
				use(to);
				p2p_flame_heard(from == host ? -1 : from, packets[index].bytes, packets[index].size);
			}
		}
	}
}

static void test_flame(void)
{
	char body[2048], keys[MACHINES][65], path[600];
	unsigned long addresses[MACHINES];
	unsigned char answer[FLAME_ANSWER_SIZE], old_answer[FLAME_ANSWER_SIZE];
	int claims[MACHINES], index, size;

	setenv("HALO_FLAMING_HELMET", "1", 1);
	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		load();
		flame_key_hex(index, keys[index]);
		addresses[index] = virtual_of(index);
		memset(machines[index].machines, 0, sizeof(machines[index].machines));
		machines[index].flame_outbound_count = machines[index].flame_inbound_count = 0;
		machines[index].test_appeared_offline = 0;
	}
	flame_name(0, "Alpha");
	flame_name(1, "Bravo");
	flame_name(2, "Charlie");
	flame_name(3, "Delta");
	/* the shipped list (version 1) lists Charlie; the newer one dropped in
	the save folder (version 2) Alpha and Bravo, Delta expired: Charlie
	revoked */
	for (index = 0; index < MACHINES; index++)
	{
		snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 1\ntester %s 0 Charlie\n", keys[2]);
		snprintf(path, sizeof(path), "%s/shipped-testers.txt", roots[index]);
		flame_list_write(path, body);
		snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 2\ntester %s 0 Alpha\ntester %s 0 Bravo\n"
			"tester %s 1000 Delta\n", keys[0], keys[1], keys[3]);
		snprintf(path, sizeof(path), "%s/testers.txt", roots[index]);
		flame_list_write(path, body);
		machines[index].flame_loaded = 0;
	}
	/* (Charlie's shipped list: a version 9 listing Charlie, a byte changed
	after it was signed: not taken, the version 2 one is) */
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 9\ntester %s 0 Charlie\n", keys[2]);
	snprintf(path, sizeof(path), "%s/shipped-testers.txt", roots[2]);
	flame_list_write(path, body);
	{
		FILE *file = fopen(path, "r+b");

		if (file)
		{
			/* (the version's digit: 9 made 8) */
			fseek(file, 31, SEEK_SET);
			fputc('8', file);
			fclose(file);
		}
	}
	use(0);
	check(p2p_flame_listed(), "flame: Alpha listed (the newer list)");
	use(1);
	check(p2p_flame_listed() && p2p_flame_wearing(), "flame: Bravo listed and wearing it");
	use(2);
	check(!p2p_flame_listed() && !p2p_flame_wearing(), "flame: Charlie revoked by the newer list");
	check(machines[2].flame_list.version == 2, "flame: the newest valid list taken");
	use(3);
	check(!p2p_flame_listed(), "flame: Delta's entry expired");
	setenv("HALO_FLAMING_HELMET", "0", 1);
	use(1);
	check(!p2p_flame_wearing(), "flame: the option off: not worn");
	setenv("HALO_FLAMING_HELMET", "1", 1);

	/* a game: Alpha hosts (0); Bravo (1), Charlie (2) and Delta (3) joined.
	Alpha and Bravo claim it; Charlie forced the claim (and answers as if
	listed); Delta too (expired) */
	machines[2].test_flame_force = 1;
	machines[3].test_flame_force = 1;
	use(0);
	p2p_friends_set_game(0, 1, addresses, MACHINES);
	for (index = 1; index < MACHINES; index++)
	{
		use(index);
		p2p_friends_set_game(index, 0, NULL, 0);
	}
	for (index = 0; index < MACHINES; index++)
		claims[index] = 1;
	flame_players(claims);
	for (index = 0; index < MACHINES; index++)
		reset_drops(index);
	flame_pass(40, 0);
	use(1);
	check(p2p_flame_verified(0), "flame: Bravo checked the host's (Alpha's) flame");
	use(2);
	check(p2p_flame_verified(0) && p2p_flame_verified(1), "flame: Charlie checked Alpha's and Bravo's");
	use(3);
	check(p2p_flame_verified(0) && p2p_flame_verified(1), "flame: Delta checked Alpha's and Bravo's");
	use(0);
	check(p2p_flame_verified(1), "flame: the host checked Bravo's");
	check(!p2p_flame_verified(2) && machines[0].machines[2].flame_refused, "flame: the host refused Charlie's forced claim");
	check(!p2p_flame_verified(3) && machines[0].machines[3].flame_refused, "flame: the host refused Delta's expired one");
	use(1);
	check(!p2p_flame_verified(2) && !p2p_flame_verified(3), "flame: Bravo draws neither forced claim");
	use(3);
	check(!p2p_flame_verified(2), "flame: Delta draws not Charlie's");
	check(machines[0].dropped_forged + machines[0].total_forged >= 2,
		"flame: the host refused to relay the unproven proofs (counted forged)");
	use(0);
	check(!p2p_flame_verified(0), "flame: a machine's own is not asked about");

	/* a replay: Bravo's proof for Delta, heard again with no ask, and for a
	new ask (another nonce) */
	flame_reask(3, 1);
	memcpy(old_answer, answer, 0);
	{
		unsigned char nonce[NONCE_LENGTH];

		memcpy(nonce, machines[3].machines[1].flame_nonce, NONCE_LENGTH);
		flame_make_answer(old_answer, 1, 3, nonce, machines[1].key, machines[1].seed, flame_names[1]);
		flame_hear(3, old_answer);
		check(p2p_flame_verified(1), "flame: Bravo's proof for Delta's nonce taken");
		reset_drops(3);
		flame_hear(3, old_answer);
		check(machines[3].dropped_replays >= 1, "flame: the same proof again (no ask): dropped as a replay");
		flame_reask(3, 1);
		reset_drops(3);
		flame_hear(3, old_answer);
		check(!machines[3].machines[1].flame_verified && machines[3].dropped_replays >= 1,
			"flame: an old proof for a new ask (another nonce): dropped as a replay");
	}
	/* forged: Alpha's listed key, signed by Charlie (who does not hold it);
	a proof for another name; a proof by another listed key for Charlie's
	machine once that key lit Alpha (a tester lending their flame) */
	flame_reask(3, 2);
	flame_make_answer(answer, 2, 3, machines[3].machines[2].flame_nonce, machines[0].key, machines[2].seed,
		flame_names[2]);
	reset_drops(3);
	flame_hear(3, answer);
	check(!machines[3].machines[2].flame_verified && machines[3].dropped_forged >= 1,
		"flame: a listed key's proof signed by another key: refused");
	check(!machines[3].machines[2].flame_refused, "flame: ... asked again while tries remain (a name may have changed)");
	flame_reask(3, 2);
	machines[3].machines[2].flame_tries = FLAME_TRIES;
	flame_make_answer(answer, 2, 3, machines[3].machines[2].flame_nonce, machines[0].key, machines[2].seed,
		flame_names[2]);
	flame_hear(3, answer);
	check(!machines[3].machines[2].flame_verified && machines[3].machines[2].flame_refused,
		"flame: ... refused for good once its tries are spent");
	flame_reask(3, 1);
	{
		unsigned short other[12];

		memset(other, 0, sizeof(other));
		other[0] = 'M';
		flame_make_answer(answer, 1, 3, machines[3].machines[1].flame_nonce, machines[1].key, machines[1].seed, other);
		reset_drops(3);
		flame_hear(3, answer);
		check(!machines[3].machines[1].flame_verified && machines[3].dropped_forged >= 1,
			"flame: a proof for another player's name: refused");
	}
	flame_reask(3, 1);
	flame_make_answer(answer, 1, 2, machines[3].machines[1].flame_nonce, machines[1].key, machines[1].seed,
		flame_names[1]);
	flame_hear(3, answer);
	check(!machines[3].machines[1].flame_verified, "flame: a proof made for another asker: refused");
	flame_reask(3, 2);
	flame_make_answer(answer, 2, 3, machines[3].machines[2].flame_nonce, machines[0].key, machines[0].seed,
		flame_names[2]);
	flame_hear(3, answer);
	check(!machines[3].machines[2].flame_verified && machines[3].machines[0].flame_verified,
		"flame: a key that lit another player of the game: refused");
	flame_reask(3, 2);
	flame_make_answer(answer, 2, 3, machines[3].machines[2].flame_nonce, machines[3].key, machines[3].seed,
		flame_names[2]);
	flame_hear(3, answer);
	check(!machines[3].machines[2].flame_verified, "flame: this machine's own key as another's: refused");
	/* a machine that does not know its own player's name in the record yet
	(what its proof signs) answers nothing: asked again later */
	{
		unsigned short none[12];
		unsigned char ask[CARD_HEADER_SIZE];

		memset(none, 0, sizeof(none));
		use(1);
		p2p_flame_set_players(claims, claims, &flame_names[0][0], MACHINES, none);
		machines[1].flame_outbound_count = 0;
		machines[1].machines[0].flame_answered_time = 0;
		ask[0] = CARD_VERSION;
		ask[1] = CARD_FLAME_ASK;
		ask[2] = 0;
		ask[3] = 1;
		posix_random_bytes(ask + 4, NONCE_LENGTH);
		p2p_flame_heard(-1, ask, CARD_HEADER_SIZE);
		p2p_flame_update();
		check(!flame_take(1, CARD_FLAME_ANSWER, answer), "flame: no proof before its own name is known");
		machines[1].flame_outbound_count = 0;
		p2p_flame_set_players(claims, claims, &flame_names[0][0], MACHINES, flame_names[1]);
		p2p_flame_heard(-1, ask, CARD_HEADER_SIZE);
		clock_now += CARD_ANSWER_INTERVAL;
		p2p_flame_update();
		check(flame_take(1, CARD_FLAME_ANSWER, answer) == FLAME_ANSWER_SIZE, "flame: ... and a proof once it is");
		machines[1].flame_outbound_count = 0;
	}
	/* a claim gone (the player left, another in the slot): forgotten */
	claims[1] = 0;
	flame_players(claims);
	use(2);
	check(!p2p_flame_verified(1), "flame: a claim gone: not drawn");
	claims[1] = 1;
	flame_players(claims);
	use(2);
	check(!p2p_flame_verified(1), "flame: a claim again: asked again, not taken as before");
	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		p2p_friends_set_game(-1, 0, NULL, 0);
		machines[index].flame_outbound_count = 0;
	}

	/* a modified host: Charlie hosts (2), claims the flame for everyone and
	answers every ask itself with Alpha's listed key, relaying unchecked;
	Alpha (0), Bravo (1) and Delta (3) joined; nobody forced but its host */
	machines[2].test_flame_force = 0;
	machines[3].test_flame_force = 0;
	machines[2].test_flame_forge = 1;
	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		memset(machines[index].machines, 0, sizeof(machines[index].machines));
		p2p_friends_set_game(index, index == 2, index == 2 ? addresses : NULL, index == 2 ? MACHINES : 0);
	}
	/* (machine addresses as the host has them: its peers) */
	flame_name(0, "Alpha");
	for (index = 0; index < MACHINES; index++)
		claims[index] = 1;
	flame_players(claims);
	/* (the joiners route their packets to the host, Charlie) */
	flame_pass(40, 2);
	use(0);
	check(!p2p_flame_verified(2) && !p2p_flame_verified(3) && !p2p_flame_verified(1),
		"flame: a modified host's forged proofs: Alpha draws none of them");
	use(3);
	check(!p2p_flame_verified(2) && !p2p_flame_verified(0) && !p2p_flame_verified(1),
		"flame: ... nor Delta (Alpha's own proof answered by the host, forged)");
	check(machines[3].machines[0].flame_refused || machines[3].machines[2].flame_refused,
		"flame: ... refused as forged");
	machines[2].test_flame_forge = 0;
	for (index = 0; index < MACHINES; index++)
	{
		use(index);
		p2p_friends_set_game(-1, 0, NULL, 0);
		machines[index].flame_outbound_count = 0;
		machines[index].flame_loaded = 0;
	}
	unsetenv("HALO_FLAMING_HELMET");
	(void)size;
}
#endif

/* ---------- the Vita's thread stacks, for the threads p2p_friends.c starts
(the backup's job): the SDK's pthread library (vitasdk's pthread-embedded,
libpthread.a's pthread_create and pte_osThreadCreate) gives a thread made
with no attributes the stack _pthread_stack_default_user names (256 KiB,
vita_stubs.c), and one made with attributes the size they were given, none
given (0) taken as its minimum, 4 KiB. insider.21's backup job was made with
attributes and no size: Argon2id's hashing overflowed its 4 KiB the first
time a tester backed up (Oct 22 2026, a data abort in memset under
extended_hash). Here each such thread runs on a stack of the test's, painted
before it starts, the most of it the job used measured when it ends; the
Vita's stack for it must be four times that or more (the Vita build's frames
are not this build's: its own chain for the job, from the insider.21 ELF,
was about 4.5 KiB) */

enum
{
	VITA_STACK_DEFAULT_USER = 256 * 1024,
	VITA_STACK_MINIMUM = 4096,
	VITA_STACK_TEST_SIZE = 512 * 1024,
	VITA_STACK_PAINT = 0xA5,
	VITA_STACK_HEADROOM = 4,
	VITA_STACK_RECORDS = 64,
};

static pthread_mutex_t vita_stack_lock = PTHREAD_MUTEX_INITIALIZER;
/* (the sizes attributes were given, as the Vita's library keeps them) */
static struct
{
	const pthread_attr_t *attributes;
	size_t size;
} vita_attributes[8];
struct vita_thread_start
{
	void *(*function)(void *);
	void *argument;
	unsigned char *stack;
	size_t vita_size;
};
/* (each job's: its operation and flags, the Vita's stack, the most used) */
static struct
{
	int operation, flags;
	size_t vita_size, used;
} vita_stack_records[VITA_STACK_RECORDS];
static int vita_stack_record_count;
/* (the test's stacks, kept: a detached thread is still on its own as it
ends) */
static unsigned char *vita_stacks[VITA_STACK_RECORDS];
static int vita_stack_count;

static int vita_pthread_attr_init(pthread_attr_t *attributes)
{
	int index, result = pthread_attr_init(attributes);

	pthread_mutex_lock(&vita_stack_lock);
	for (index = 0; index < (int)(sizeof(vita_attributes) / sizeof(vita_attributes[0])); index++)
		if (vita_attributes[index].attributes == attributes || !vita_attributes[index].attributes)
		{
			vita_attributes[index].attributes = attributes;
			vita_attributes[index].size = 0;
			break;
		}
	pthread_mutex_unlock(&vita_stack_lock);
	return result;
}

static int vita_pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size)
{
	int index;

	pthread_mutex_lock(&vita_stack_lock);
	for (index = 0; index < (int)(sizeof(vita_attributes) / sizeof(vita_attributes[0])); index++)
		if (vita_attributes[index].attributes == attributes)
			vita_attributes[index].size = size;
	pthread_mutex_unlock(&vita_stack_lock);
	return pthread_attr_setstacksize(attributes, size);
}

static int vita_pthread_attr_destroy(pthread_attr_t *attributes)
{
	int index;

	pthread_mutex_lock(&vita_stack_lock);
	for (index = 0; index < (int)(sizeof(vita_attributes) / sizeof(vita_attributes[0])); index++)
		if (vita_attributes[index].attributes == attributes)
			vita_attributes[index].attributes = NULL;
	pthread_mutex_unlock(&vita_stack_lock);
	return pthread_attr_destroy(attributes);
}

/* (the stack's lowest byte the thread wrote: its paint read, unchecked) */
__attribute__((no_sanitize_address, noinline)) static size_t vita_stack_untouched(const unsigned char *stack,
	size_t size)
{
	const volatile unsigned char *bytes = stack;
	size_t index = 0;

	while (index < size && bytes[index] == VITA_STACK_PAINT)
		index++;
	return index;
}

static void *vita_thread_trampoline(void *argument)
{
	struct vita_thread_start start = *(struct vita_thread_start *)argument;
	unsigned char *top = __builtin_frame_address(0);
	int operation = backup.operation, flags = backup.flags;
	void *result;
	size_t untouched;

	free(argument);
	result = start.function(start.argument);
	untouched = vita_stack_untouched(start.stack, VITA_STACK_TEST_SIZE);
	pthread_mutex_lock(&vita_stack_lock);
	if (vita_stack_record_count < VITA_STACK_RECORDS)
	{
		vita_stack_records[vita_stack_record_count].operation = operation;
		vita_stack_records[vita_stack_record_count].flags = flags;
		vita_stack_records[vita_stack_record_count].vita_size = start.vita_size;
		vita_stack_records[vita_stack_record_count].used = (size_t)(top - (start.stack + untouched));
		vita_stack_record_count++;
	}
	pthread_mutex_unlock(&vita_stack_lock);
	return result;
}

static int vita_pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*function)(void *),
	void *argument)
{
	struct vita_thread_start *start = malloc(sizeof(*start));
	pthread_attr_t made;
	void *stack = NULL;
	int index, detached = PTHREAD_CREATE_JOINABLE, result;

	if (!start || vita_stack_count >= VITA_STACK_RECORDS || posix_memalign(&stack, 4096, VITA_STACK_TEST_SIZE))
	{
		free(start);
		return EAGAIN;
	}
	/* (no attributes: the default; attributes: their size, or the
	minimum) */
	start->vita_size = VITA_STACK_DEFAULT_USER;
	if (attributes)
	{
		start->vita_size = 0;
		pthread_mutex_lock(&vita_stack_lock);
		for (index = 0; index < (int)(sizeof(vita_attributes) / sizeof(vita_attributes[0])); index++)
			if (vita_attributes[index].attributes == attributes)
				start->vita_size = vita_attributes[index].size;
		pthread_mutex_unlock(&vita_stack_lock);
		if (start->vita_size < VITA_STACK_MINIMUM)
			start->vita_size = VITA_STACK_MINIMUM;
		pthread_attr_getdetachstate(attributes, &detached);
	}
	start->function = function;
	start->argument = argument;
	start->stack = stack;
	memset(stack, VITA_STACK_PAINT, VITA_STACK_TEST_SIZE);
	vita_stacks[vita_stack_count++] = stack;
	pthread_attr_init(&made);
	pthread_attr_setdetachstate(&made, detached);
	pthread_attr_setstack(&made, stack, VITA_STACK_TEST_SIZE);
	result = pthread_create(thread, &made, vita_thread_trampoline, start);
	pthread_attr_destroy(&made);
	if (result)
		free(start);
	return result;
}

/* every backup job's thread so far fit the stack the Vita gives it, with
room to spare; a backup with the list, the code alone, and a restore among
them */
static void vita_stack_check(void)
{
	int index, with_list = 0, code_alone = 0, restores = 0, fits = 1, done = 0;
	size_t most = 0, most_vita = 0;

	/* (the last job's thread measures as it ends, after its outcome) */
	for (index = 0; index < 5000; index++)
	{
		pthread_mutex_lock(&vita_stack_lock);
		done = vita_stack_record_count == vita_stack_count;
		pthread_mutex_unlock(&vita_stack_lock);
		if (done)
			break;
		usleep(1000);
	}
	pthread_mutex_lock(&vita_stack_lock);
	for (index = 0; index < vita_stack_record_count; index++)
	{
		size_t used = vita_stack_records[index].used, vita_size = vita_stack_records[index].vita_size;
		int operation = vita_stack_records[index].operation;

		if (operation == BACKUP_EXPORT && (vita_stack_records[index].flags & BACKUP_FLAG_LIST))
			with_list++;
		else if (operation == BACKUP_EXPORT)
			code_alone++;
		else if (operation == BACKUP_RESTORE)
			restores++;
		if (used * VITA_STACK_HEADROOM > vita_size)
		{
			if (fits)
				printf("  (a backup job's thread (%s) used %lu bytes of its stack; the Vita gives it %lu)\n",
					operation == BACKUP_EXPORT ? "a backup" : "a restore", (unsigned long)used,
					(unsigned long)vita_size);
			fits = 0;
		}
	}
	for (index = 0; index < vita_stack_record_count; index++)
		if (vita_stack_records[index].used > most)
		{
			most = vita_stack_records[index].used;
			most_vita = vita_stack_records[index].vita_size;
		}
	printf("  backup: %d jobs; the most stack one used: %lu bytes, of the Vita's %lu\n", vita_stack_record_count,
		(unsigned long)most, (unsigned long)most_vita);
	pthread_mutex_unlock(&vita_stack_lock);
	check(done && with_list && code_alone && restores,
		"backup: a backup with the list, the code alone and a restore measured");
	check(fits, "backup: each job's thread fits the stack the Vita gives it, four times over");
}

/* ---------- the friend identity's backup (p2p_friends_backup.inc) */

static char backup_said[PATH_SIZE + 64];

/* (a save on a thread of its own, the machine's state chosen before) */
static void *backup_save_thread(void *unused)
{
	(void)unused;
	save(1);
	return NULL;
}

/* the job's outcome, once its thread is done */
static int backup_wait(void)
{
	int state = -1, rounds;

	for (rounds = 0; rounds < 120000; rounds++)
	{
		state = p2p_friends_backup_state(backup_said, sizeof(backup_said));
		if (state != P2P_FRIENDS_BACKUP_WORKING)
			return state;
		usleep(2000);
	}
	return state;
}

/* a pass of some machines' threads, the broker carrying their messages
between them alone (the others' dropped) */
static void backup_pass(const int *list, int list_count, int count)
{
	int index, member, other, entry;

	for (index = 0; index < count; index++)
	{
		clock_now += 500;
		for (member = 0; member < list_count; member++)
		{
			use(list[member]);
			p2p_friends_update(&hosts[list[member]]);
		}
		for (member = 0; member < list_count; member++)
		{
			use(list[member]);
			for (entry = 0; entry < friends->outbound_count; entry++)
			{
				struct outbound copy = friends->outbound[entry];

				for (other = 0; other < list_count; other++)
					if (other != member)
						hand(list[other], copy.topic, copy.payload, copy.size);
			}
			use(list[member]);
			friends->outbound_count = 0;
		}
	}
}

static int file_bytes(const char *path, unsigned char *bytes, int capacity)
{
	return file_read(path, bytes, capacity);
}

static int bytes_find(const unsigned char *bytes, int size, const void *needle, int needle_size)
{
	int index;

	for (index = 0; index + needle_size <= size; index++)
		if (!memcmp(bytes + index, needle, (size_t)needle_size))
			return 1;
	return 0;
}

/* a file written as the backup writes it, from contents and a key of the
test's choosing */
static void backup_write_crafted(const char *path, unsigned char *plain, int flags, const char *password,
	unsigned long blocks, unsigned long passes)
{
	unsigned char *file = malloc(BACKUP_FILE_SIZE);

	check(file && backup_seal_file(plain, flags, password, (int)strlen(password), blocks, passes, file),
		"backup: a crafted file sealed");
	if (file)
		file_write(path, file, BACKUP_FILE_SIZE);
	free(file);
}

/* a restore that must fail, and how; nothing changed on the machine */
static void backup_refused(int machine, const char *name, const char *password, int expected, const char *what)
{
	unsigned char before[IDENTITY_SIZE + 8], after[IDENTITY_SIZE + 8];
	char path[600], code_before[P2P_FRIEND_CODE_SIZE], code_after[P2P_FRIEND_CODE_SIZE];
	int before_size, after_size, outcome, friends_before;

	code_of(machine, code_before);
	friends_before = machines[machine].friend_count;
	snprintf(path, sizeof(path), "%s/friends/identity.dat", roots[machine]);
	before_size = file_bytes(path, before, sizeof(before));
	use(machine);
	outcome = p2p_friends_backup_restore(name, password);
	if (outcome == P2P_FRIENDS_BACKUP_WORKING)
		outcome = backup_wait();
	if (outcome != expected)
		printf("  (%s: %d, not %d: %s)\n", what, outcome, expected, backup_said);
	check(outcome == expected, what);
	check(p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_NOTHING, "backup: nothing to confirm after a refusal");
	code_of(machine, code_after);
	after_size = file_bytes(path, after, sizeof(after));
	check(!strcmp(code_before, code_after) && machines[machine].friend_count == friends_before &&
		before_size == after_size && !memcmp(before, after, (size_t)before_size), "backup: a refusal changes nothing");
}

static void test_backup(void)
{
	static const int trio[] = { 1, 4, 5 };
	unsigned char *file = malloc(BACKUP_FILE_SIZE + 64), *plain = malloc(BACKUP_PLAIN_SIZE);
	char code_a[P2P_FRIEND_CODE_SIZE], code_b[P2P_FRIEND_CODE_SIZE], code_f[P2P_FRIEND_CODE_SIZE], found[32];
	char current[32], path[600], copy[600], name[P2P_FRIENDS_BACKUP_NAME_SIZE];
	struct p2p_friend_entry entry;
	int size, friend_count, flags, outcome, f_friends, index;
	unsigned long long a_epoch;

	if (!file || !plain)
	{
		check(0, "backup: memory for the checks");
		return;
	}
	/* names and passwords */
	check(p2p_friends_backup_name(" friends-backup.HFB ", name) && !strcmp(name, "friends-backup"),
		"backup: a name typed with .hfb and spaces taken");
	check(!p2p_friends_backup_name("../identity", name) && !p2p_friends_backup_name("a/b", name) &&
		!p2p_friends_backup_name("", name) && !p2p_friends_backup_name("ux0:data", name) &&
		!p2p_friends_backup_name("a b", name) && !p2p_friends_backup_name("abcdefghijklmnopqrstuvwxyz0123456", name),
		"backup: names with a path, a colon, a space, none or too long refused");
	check(p2p_friends_backup_password_check("1234567") == P2P_FRIENDS_BACKUP_SHORT_PASSWORD &&
		p2p_friends_backup_password_check("  1234567  ") == P2P_FRIENDS_BACKUP_SHORT_PASSWORD &&
		p2p_friends_backup_password_check("12345678") == P2P_FRIENDS_BACKUP_IDLE &&
		p2p_friends_backup_password_check("caf\xc3\xa9 au lait") == P2P_FRIENDS_BACKUP_BAD_PASSWORD &&
		p2p_friends_backup_password_check("0123456789012345678901234567890123456789012345678901234567890123") ==
		P2P_FRIENDS_BACKUP_IDLE &&
		p2p_friends_backup_password_check("01234567890123456789012345678901234567890123456789012345678901234") ==
		P2P_FRIENDS_BACKUP_BAD_PASSWORD, "backup: passwords: 8 to 64 printable characters");
	use(4);
	check(p2p_friends_backup_export("x", "short", 1) == P2P_FRIENDS_BACKUP_SHORT_PASSWORD &&
		p2p_friends_backup_export("../x", "long enough", 1) == P2P_FRIENDS_BACKUP_BAD_NAME,
		"backup: a short password or a bad name: nothing started");
	/* Echo (machine 4) and Bravo (machine 1) friends */
	use(4);
	p2p_friends_set_name("Echo");
	use(5);
	p2p_friends_set_name("Foxtrot");
	code_of(4, code_a);
	code_of(5, code_b);
	code_of(1, code_f);
	check(strcmp(code_a, code_b) != 0, "backup: two machines, two codes");
	use(4);
	check(p2p_friends_add(code_f) == P2P_FRIENDS_ADD_SENT, "backup: Echo asks Bravo");
	backup_pass(trio, 3, 2);
	check(entry_of(1, P2P_FRIEND_KIND_REQUEST, 0, &entry) && !strcmp(entry.code, code_a), "backup: Bravo heard Echo");
	use(1);
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT) == P2P_FRIENDS_DONE,
		"backup: Bravo accepts");
	backup_pass(trio, 3, 4);
	check(count_of(4, P2P_FRIEND_KIND_FRIEND) == 1, "backup: Echo has Bravo for a friend");
	f_friends = count_of(1, P2P_FRIEND_KIND_FRIEND);
	/* the backup written: its size whatever the list, its key's parameters,
	nothing of the seed or the password in it */
	use(4);
	outcome = p2p_friends_backup_export("echo", "correct horse battery", 1);
	check(outcome == P2P_FRIENDS_BACKUP_WORKING, "backup: started");
	check(p2p_friends_backup_export("echo2", "correct horse battery", 1) == P2P_FRIENDS_BACKUP_BUSY,
		"backup: one at a time");
	outcome = backup_wait();
	snprintf(path, sizeof(path), "%s/echo.hfb", roots[4]);
	check(outcome == P2P_FRIENDS_BACKUP_SAVED && strstr(backup_said, path), "backup: saved, and where it went said");
	check(p2p_friends_backup_state(NULL, 0) == P2P_FRIENDS_BACKUP_IDLE, "backup: its outcome given once");
	size = file_bytes(path, file, BACKUP_FILE_SIZE + 64);
	check(size == BACKUP_FILE_SIZE, "backup: the file's size");
	check(!memcmp(file, "HFBK", 4) && file[4] == BACKUP_VERSION && file[5] == BACKUP_KDF_ARGON2ID &&
		file[6] == BACKUP_FLAG_LIST && get_u32(file + 8) == 8192 && get_u32(file + 12) == 5,
		"backup: Argon2id, 8 MiB, five passes, the list in it");
	check(!bytes_find(file, size, machines[4].seed, P2P_SEED_SIZE) && !bytes_find(file, size, "correct horse", 13) &&
		!bytes_find(file, size, machines[4].key, P2P_KEY_SIZE) && !bytes_find(file, size, "HFRL", 4),
		"backup: no seed, key, list or password in the file");
	{
		unsigned char second[BACKUP_HEADER_SIZE];

		memcpy(second, file, sizeof(second));
		use(4);
		check(p2p_friends_backup_export("echo-again", "correct horse battery", 1) == P2P_FRIENDS_BACKUP_WORKING &&
			backup_wait() == P2P_FRIENDS_BACKUP_SAVED, "backup: written again");
		snprintf(copy, sizeof(copy), "%s/echo-again.hfb", roots[4]);
		check(file_bytes(copy, file, BACKUP_FILE_SIZE + 64) == BACKUP_FILE_SIZE &&
			memcmp(file + 16, second + 16, BACKUP_SALT_SIZE) && memcmp(file + 32, second + 32, BACKUP_NONCE_SIZE),
			"backup: a salt and a nonce of its own each time");
		check(!bytes_find((const unsigned char *)backup.password, sizeof(backup.password), "correct", 7) && !backup.plain,
			"backup: the job's copy of the password and the contents wiped");
	}
	/* the file taken to another machine (Foxtrot, machine 5) */
	snprintf(copy, sizeof(copy), "%s/echo.hfb", roots[5]);
	size = file_bytes(path, file, BACKUP_FILE_SIZE + 64);
	file_write(copy, file, size);
	/* refusals: nothing changes */
	backup_refused(5, "echo", "correct horse batterY", P2P_FRIENDS_BACKUP_WRONG_PASSWORD, "backup: a wrong password");
	backup_refused(5, "echo", "short", P2P_FRIENDS_BACKUP_WRONG_PASSWORD, "backup: a short password opens nothing");
	backup_refused(5, "nobody", "correct horse battery", P2P_FRIENDS_BACKUP_NO_FILE, "backup: no such file");
	backup_refused(5, "../echo", "correct horse battery", P2P_FRIENDS_BACKUP_BAD_NAME, "backup: a name with a path");
	snprintf(copy, sizeof(copy), "%s/cut.hfb", roots[5]);
	file_write(copy, file, size - 1);
	backup_refused(5, "cut", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: a cut file");
	file_write(copy, file, 40);
	backup_refused(5, "cut", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: a file cut in its header");
	{
		unsigned char *altered = malloc(BACKUP_FILE_SIZE + 64);

		snprintf(copy, sizeof(copy), "%s/altered.hfb", roots[5]);
		/* a byte of the sealed contents, of the tag, of the salt, of the flags */
		memcpy(altered, file, (size_t)size);
		altered[BACKUP_HEADER_SIZE + 100] ^= 1;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_WRONG_PASSWORD,
			"backup: a flipped byte of the contents");
		memcpy(altered, file, (size_t)size);
		altered[size - 1] ^= 0x80;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_WRONG_PASSWORD,
			"backup: a flipped byte of the tag");
		memcpy(altered, file, (size_t)size);
		altered[20] ^= 1;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_WRONG_PASSWORD,
			"backup: a flipped byte of the salt");
		memcpy(altered, file, (size_t)size);
		altered[6] = 0;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_WRONG_PASSWORD,
			"backup: the list's flag taken off (the header is bound to the contents)");
		memcpy(altered, file, (size_t)size);
		altered[0] = 'X';
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: not a backup");
		/* versions */
		memcpy(altered, file, (size_t)size);
		altered[4] = BACKUP_VERSION + 1;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_NEWER, "backup: a newer file's version");
		memcpy(altered, file, (size_t)size);
		altered[4] = 0;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: a version 0");
		memcpy(altered, file, (size_t)size);
		altered[5] = 2;
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_NEWER, "backup: another key derivation");
		/* what the key may ask: refused before any work */
		memcpy(altered, file, (size_t)size);
		put_u32(altered + 8, 4096);
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: less memory refused");
		put_u32(altered + 8, BACKUP_MAXIMUM_BLOCKS + 1);
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: more memory refused");
		put_u32(altered + 8, 8192);
		put_u32(altered + 12, 4);
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: fewer passes refused");
		put_u32(altered + 12, BACKUP_MAXIMUM_PASSES + 1);
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: more passes refused");
		memcpy(altered, file, (size_t)size);
		put_u32(altered + 56, BACKUP_PLAIN_SIZE - 1);
		file_write(copy, altered, size);
		backup_refused(5, "altered", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED,
			"backup: a size not the file's refused");
		free(altered);
	}
	/* the contents' own version, under a good key; a seed that is not the
	key named; another memory and passes as strong, taken */
	use(4);
	memset(plain, 0, BACKUP_PLAIN_SIZE);
	memcpy(plain, "HFBP", 4);
	plain[4] = BACKUP_VERSION + 1;
	memcpy(plain + 8, machines[4].seed, P2P_SEED_SIZE);
	memcpy(plain + 40, machines[4].key, P2P_KEY_SIZE);
	snprintf(copy, sizeof(copy), "%s/crafted.hfb", roots[5]);
	backup_write_crafted(copy, plain, 0, "correct horse battery", BACKUP_BLOCKS, BACKUP_PASSES);
	backup_refused(5, "crafted", "correct horse battery", P2P_FRIENDS_BACKUP_NEWER, "backup: newer contents");
	plain[4] = BACKUP_VERSION;
	plain[40] ^= 1;
	backup_write_crafted(copy, plain, 0, "correct horse battery", BACKUP_BLOCKS, BACKUP_PASSES);
	backup_refused(5, "crafted", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: a key not the seed's");
	plain[40] ^= 1;
	plain[5] = BACKUP_FLAG_LIST;
	backup_write_crafted(copy, plain, 0, "correct horse battery", BACKUP_BLOCKS, BACKUP_PASSES);
	backup_refused(5, "crafted", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: flags not the header's");
	plain[5] = 0;
	plain[BACKUP_PLAIN_SIZE - 1] = 1;
	backup_write_crafted(copy, plain, 0, "correct horse battery", BACKUP_BLOCKS, BACKUP_PASSES);
	backup_refused(5, "crafted", "correct horse battery", P2P_FRIENDS_BACKUP_DAMAGED, "backup: bytes past the contents");
	plain[BACKUP_PLAIN_SIZE - 1] = 0;
	backup_write_crafted(copy, plain, 0, "correct horse battery", 16384, 3);
	use(5);
	check(p2p_friends_backup_restore("crafted", "correct horse battery") == P2P_FRIENDS_BACKUP_WORKING &&
		backup_wait() == P2P_FRIENDS_BACKUP_READY, "backup: 16 MiB and three passes taken (as strong)");
	check(p2p_friends_backup_found(found, current, sizeof(found), &friend_count) && !strcmp(found, code_a) &&
		!strcmp(current, code_b) && friend_count == -1, "backup: what it holds said: the code, no list");
	p2p_friends_backup_cancel();
	check(!backup.plain && p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_NOTHING,
		"backup: cancelled: wiped, nothing to confirm");
	code_of(5, current);
	check(!strcmp(current, code_b), "backup: cancelled: Foxtrot's code its own");
	/* opened and left: let go a while later */
	use(5);
	check(p2p_friends_backup_restore("crafted", "correct horse battery") == P2P_FRIENDS_BACKUP_WORKING &&
		backup_wait() == P2P_FRIENDS_BACKUP_READY, "backup: opened again");
	clock_now += BACKUP_READY_LIFETIME + 1000;
	check(!p2p_friends_backup_found(found, NULL, sizeof(found), &friend_count) && !backup.plain &&
		p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_NOTHING, "backup: opened and left: let go, wiped");
	/* restored: the same code, the list; the identity replaced kept */
	{
		unsigned char old_identity[IDENTITY_SIZE + 8], seed[P2P_SEED_SIZE], old_seed[P2P_SEED_SIZE];
		unsigned long long epoch;
		char kept[700];
		DIR *folder;
		struct dirent *item;
		int kept_found = 0, old_size;

		memcpy(old_seed, machines[5].seed, P2P_SEED_SIZE);
		snprintf(path, sizeof(path), "%s/friends/identity.dat", roots[5]);
		old_size = file_bytes(path, old_identity, sizeof(old_identity));
		check(old_size == IDENTITY_SIZE, "backup: Foxtrot's own identity on file");
		a_epoch = machines[4].epoch;
		use(5);
		check(p2p_friends_backup_restore("echo", "  correct horse battery ") == P2P_FRIENDS_BACKUP_WORKING &&
			backup_wait() == P2P_FRIENDS_BACKUP_READY, "backup: opened (spaces at its ends ignored)");
		check(p2p_friends_backup_found(found, current, sizeof(found), &friend_count) && !strcmp(found, code_a) &&
			friend_count == 1, "backup: its code and its one friend said");
		outcome = p2p_friends_backup_confirm();
		check(outcome == P2P_FRIENDS_BACKUP_RESTORED, "backup: restored");
		check(p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_NOTHING, "backup: restored once");
		code_of(5, current);
		check(!strcmp(current, code_a), "backup: Foxtrot has Echo's friend code");
		check(machines[5].epoch > a_epoch, "backup: its run newer than Echo's");
		check(count_of(5, P2P_FRIEND_KIND_FRIEND) == 1 && entry_of(5, P2P_FRIEND_KIND_FRIEND, 0, &entry) &&
			!strcmp(entry.code, code_f), "backup: Echo's friends list restored: Bravo");
		check(identity_decode((file_bytes(path, file, BACKUP_FILE_SIZE), file), IDENTITY_SIZE, seed, &epoch) &&
			!memcmp(seed, machines[4].seed, P2P_SEED_SIZE) && epoch == machines[5].epoch, "backup: identity.dat written");
		snprintf(kept, sizeof(kept), "%s/friends", roots[5]);
		folder = opendir(kept);
		while (folder && (item = readdir(folder)))
			if (!strncmp(item->d_name, "replaced-", 9))
			{
				unsigned char replaced[IDENTITY_SIZE + 8];

				snprintf(kept, sizeof(kept), "%s/friends/%s/identity.dat", roots[5], item->d_name);
				kept_found = file_bytes(kept, replaced, sizeof(replaced)) == old_size &&
					identity_decode(replaced, old_size, seed, &epoch) && !memcmp(seed, old_seed, P2P_SEED_SIZE);
			}
		if (folder)
			closedir(folder);
		check(kept_found, "backup: the identity replaced kept in friends/replaced-<date>");
		/* a new run reads it back */
		memset(&machines[5], 0, sizeof(machines[5]));
		use(5);
		p2p_friends_set_name("Foxtrot");
		code_of(5, current);
		check(!strcmp(current, code_a) && machines[5].friend_count == 1, "backup: a new run of Foxtrot keeps it");
	}
	/* two machines with one identity: the newest run is heard, the other's
	dropped as replays; Bravo sees one friend, Foxtrot's name */
	backup_pass(trio, 3, 4);
	reset_drops(1);
	/* (Echo still running, its game changing after Bravo heard Foxtrot's
	run: its presence sent again) */
	use(4);
	p2p_friends_set_activity(P2P_FRIEND_STATE_MULTIPLAYER, "bloodgulch", "Slayer", 2, -1, 0, 2, 16);
	backup_pass(trio, 3, 12);
	check(count_of(1, P2P_FRIEND_KIND_FRIEND) == f_friends, "backup: Bravo has as many friends as before");
	{
		int found_friend = 0;

		for (index = 0; index < count_of(1, P2P_FRIEND_KIND_FRIEND); index++)
			if (entry_of(1, P2P_FRIEND_KIND_FRIEND, index, &entry) && !strcmp(entry.code, code_a))
				found_friend = entry.online && !strcmp(entry.name, "Foxtrot");
		check(found_friend, "backup: Bravo sees Echo's code online, as Foxtrot (its name now)");
	}
	use(1);
	{
		const struct friend_record *record = find_friend(machines[4].key);

		check(record && record->epoch == machines[5].epoch, "backup: Bravo takes the newest run (Foxtrot's)");
	}
	check(machines[1].dropped_replays + machines[1].total_replays > 0, "backup: Echo's older run dropped as replays");
	check(entry_of(5, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online, "backup: Foxtrot sees Bravo online");
	/* the same identity restored on its own machine: a newer run again */
	{
		unsigned long long epoch_before = machines[5].epoch;

		snprintf(path, sizeof(path), "%s/echo.hfb", roots[5]);
		use(5);
		check(p2p_friends_backup_restore("echo", "correct horse battery") == P2P_FRIENDS_BACKUP_WORKING &&
			backup_wait() == P2P_FRIENDS_BACKUP_READY && p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_RESTORED,
			"backup: restored again on the machine that has it");
		check(machines[5].epoch > epoch_before, "backup: ... its run newer than the one it replaced");
		reset_drops(1);
		backup_pass(trio, 3, 4);
		use(1);
		check(find_friend(machines[4].key) && find_friend(machines[4].key)->epoch == machines[5].epoch,
			"backup: ... and heard by its friend");
	}
	/* a save that read the list before the files were replaced (a restore
	meanwhile): not written over the new ones */
	{
		pthread_t thread;
		unsigned char marker[16] = "restored files", read_back[32];

		use(5);
		machines[5].dirty = 1;
		machines[5].saved_time = 0;
		snprintf(path, sizeof(path), "%s/friends/friends.dat", roots[5]);
		pthread_mutex_lock(&friends_file_lock);
		check(pthread_create(&thread, NULL, backup_save_thread, NULL) == 0, "backup: a save started");
		usleep(300000);
		/* (as a restore does: its files written, the generation moved on) */
		file_write(path, marker, sizeof(marker));
		machines[5].files_generation++;
		pthread_mutex_unlock(&friends_file_lock);
		pthread_join(thread, NULL);
		check(file_bytes(path, read_back, sizeof(read_back)) == (int)sizeof(marker) &&
			!memcmp(read_back, marker, sizeof(marker)), "backup: the old list's save waiting not written over them");
		machines[5].dirty = 1;
		save(1);
		check(file_bytes(path, read_back, sizeof(read_back)) != (int)sizeof(marker), "backup: the next save writes");
	}
	/* the code alone: a third machine (a new folder) gets it, no list */
	use(4);
	check(p2p_friends_backup_export("echo-code", "correct horse battery", 0) == P2P_FRIENDS_BACKUP_WORKING &&
		backup_wait() == P2P_FRIENDS_BACKUP_SAVED, "backup: the code alone saved");
	snprintf(path, sizeof(path), "%s/echo-code.hfb", roots[4]);
	size = file_bytes(path, file, BACKUP_FILE_SIZE + 64);
	check(size == BACKUP_FILE_SIZE && file[6] == 0, "backup: the same size without the list");
	snprintf(roots[5], sizeof(roots[5]), "%s-new", roots[4]);
	mkdir(roots[5], 0755);
	snprintf(copy, sizeof(copy), "%s/echo-code.hfb", roots[5]);
	file_write(copy, file, size);
	memset(&machines[5], 0, sizeof(machines[5]));
	code_of(5, current);
	check(strcmp(current, code_a) != 0, "backup: a new machine, a new code");
	use(5);
	flags = p2p_friends_backup_restore("echo-code", "correct horse battery") == P2P_FRIENDS_BACKUP_WORKING &&
		backup_wait() == P2P_FRIENDS_BACKUP_READY;
	check(flags && p2p_friends_backup_found(found, NULL, sizeof(found), &friend_count) && friend_count == -1,
		"backup: the code alone opened");
	check(p2p_friends_backup_confirm() == P2P_FRIENDS_BACKUP_RESTORED, "backup: the code alone restored");
	code_of(5, current);
	check(!strcmp(current, code_a) && machines[5].friend_count == 0, "backup: Echo's code, no friends");
	vita_stack_check();
	free(file);
	free(plain);
}

int main(void)
{
	char root[512];
	int index;

	if (getenv("FRIENDS_TEST_SEED_DIR"))
		return seed(getenv("FRIENDS_TEST_SEED_DIR"));

	snprintf(root, sizeof(root), "%s", getenv("FRIENDS_TEST_ROOT") ? getenv("FRIENDS_TEST_ROOT") : ".");
	for (index = 0; index < ALL_MACHINES; index++)
	{
		snprintf(roots[index], sizeof(roots[index]), "%s/machine%d", root, index);
		mkdir(roots[index], 0755);
		memset(&hosts[index], 0, sizeof(hosts[index]));
		hosts[index].online = 1;
	}
	test_wires();
	test_codes();
	test_request_accept();
	test_join_invite();
	test_replays_and_forgeries();
	test_appear_offline();
	test_decline_block();
	test_remove();
	test_files();
	test_limits();
	test_request_replays();
	test_request_floods();
	test_cards();
	test_card_binding();
#ifdef FLAME_TEST_SEED
	test_flame();
#endif
	test_party();
	test_party_restart();
	test_backup();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
#endif
