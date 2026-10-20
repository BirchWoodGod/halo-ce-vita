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
  messages with REMOVE again.

Prints PASS or the failures. run_p2p_friends_test.sh builds it (with ASan
and UBSan too).
*/

/* (the files in the test's folders, not ux0:) */
#define P2P_FRIENDS_TEST
#include "../../linux/src/p2p_friends.c"
/* (the wires: which a friend's game is joined on, network_wire.h; this
machine's platform each test machine's own, below) */
#define network_wire_this_platform network_wire_this_platform_built
#include "../../linux/src/network_wire.c"
#undef network_wire_this_platform

#include <stdarg.h>
/* (the platform layer's headers keep sys/stat.h out) */
int mkdir(const char *path, unsigned int mode);
#include <unistd.h>

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
const char *config_string(const char *name)
{
	(void)name;
	return "";
}
static int quiet;
void platform_log(const char *format, ...)
{
	va_list arguments;

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
	LOGGED = 256,
};

static struct friends_state machines[MACHINES];
static struct p2p_friends_host hosts[MACHINES];
static char roots[MACHINES][512];

/* every message the broker carried, as it was (replays) */
static struct
{
	char topic[TOPIC_LENGTH];
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	int size;
	int from;
} carried[LOGGED];
static int carried_count;

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
					hand(to, entry.topic, entry.payload, entry.size);
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
	use(0);
	p2p_friends_set_game(-1, 0, NULL, 0);

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

int main(void)
{
	char root[512];
	int index;

	if (getenv("FRIENDS_TEST_SEED_DIR"))
		return seed(getenv("FRIENDS_TEST_SEED_DIR"));

	snprintf(root, sizeof(root), "%s", getenv("FRIENDS_TEST_ROOT") ? getenv("FRIENDS_TEST_ROOT") : ".");
	for (index = 0; index < MACHINES; index++)
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
	test_cards();
	test_party();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
#endif
