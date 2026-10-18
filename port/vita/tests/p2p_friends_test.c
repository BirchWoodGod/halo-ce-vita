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

/* ---------- the machines and the broker */

enum
{
	MACHINES = 3,
	LOGGED = 256,
};

static struct friends_state machines[MACHINES];
static struct p2p_friends_host hosts[MACHINES];
static char roots[MACHINES][512];
static int current;

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
	check(!strcmp(entry.activity, "Slayer on Blood Gulch"), "the friend's activity: Slayer on Blood Gulch");
	check(!strcmp(entry.name, "Bravo"), "the friend's name cleaned as a player's");
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.online && !strcmp(entry.activity, "In the menus"),
		"presence both ways: In the menus");
	use(0);
	p2p_friends_set_activity(P2P_FRIEND_STATE_COOP, "a10", "", 0, 3, 0, 1, 4);
	pass(8);
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) &&
		!strcmp(entry.activity, "Co-op: The Pillar of Autumn, Legendary"), "co-op's activity with its difficulty");
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
	/* a password: never joinable from presence, never invited */
	host->locked = 1;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.joinable, "a password's game not joinable");
	use(0);
	join_count = 0;
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_JOIN) == P2P_FRIENDS_NOT_JOINABLE &&
		!join_count, "Join refused for a password's game");
	use(1);
	check(!memchr(machines[1].presence_plain, host->token[0], 0) && !machines[1].presence_joinable,
		"a password's game's invite not in presence");
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) &&
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_NOT_JOINABLE,
		"Invite refused with a password");
	/* private: not joinable, but invited */
	host->locked = 0;
	host->listed = 0;
	pass(10);
	check(entry_of(0, P2P_FRIEND_KIND_FRIEND, 0, &entry) && !entry.joinable, "a private game not joinable");
	use(1);
	check(!machines[1].presence_joinable, "a private game's invite not in presence");
	check(entry_of(1, P2P_FRIEND_KIND_FRIEND, 0, &entry) && entry.invitable &&
		p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_DONE, "Invite sent");
	check(p2p_friends_act(entry.id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE) == P2P_FRIENDS_TOO_SOON,
		"a second invite a moment later refused");
	pass(2);
	check(count_of(0, P2P_FRIEND_KIND_INVITE) == 1, "the invite heard");
	use(0);
	check(p2p_friends_take_invite(&entry) && !strcmp(entry.name, "Bravo") && !strcmp(entry.activity, "CTF on Blood Gulch"),
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

#ifndef P2P_FRIENDS_FUZZ
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
	test_codes();
	test_request_accept();
	test_join_invite();
	test_replays_and_forgeries();
	test_appear_offline();
	test_decline_block();
	test_remove();
	test_files();
	test_limits();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
#endif
