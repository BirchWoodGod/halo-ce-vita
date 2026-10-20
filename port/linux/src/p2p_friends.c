/*
P2P_FRIENDS.C

Friends (p2p_friends.h; the design, its message formats, keys, limits and
threat model: docs/superpowers/specs/2026-10-18-friends-design.md).

- The friend identity: an Ed25519 key made once and kept in the save area
  (identity.dat), apart from internet play's key, which is new each run (a
  public game's host must not be linkable from run to run). Its code is the
  first 60 bits of a hash of it, 12 letters and digits.
- Two topics on the brokers (p2p_signal.c subscribes them): the inbox, from
  the code, where strangers' requests arrive, and the mailbox, from the full
  key, where friends write. Nothing is retained.
- Messages: an envelope ("F", version, kind) sealed with ChaCha20-Poly1305
  under the code's key (a request), an ephemeral X25519 key's (an accept,
  to a requester who does not know the accepter's key yet) or the pair's
  key for one direction (presence, invites, removals); inside, the sender's
  and recipient's keys, the epoch and counter (replays), the body, and an
  Ed25519 signature by the sender's identity.
- The list (friends.dat) and the identity are written whole and atomically
  (a .tmp written, read back and checked, then put in place as
  forge_store.c does) and checked as they are read.

Threads: internet play's thread calls p2p_friends_update without
p2p_lock (the work here is crypto, which the game's threads must not wait
on), and p2p_signal.c calls p2p_friends_heard and the outgoing queue's
functions with p2p_lock held; everything here is under friends_lock, which
is taken after p2p_lock, never before it (nothing here calls into p2p.c's
locked functions while holding friends_lock).

(debug, not in release builds) HALO_TEST_FRIENDS: the automated tests'
steps, comma-separated (test_steps below); everything is logged as
"friends: ...", which the tests read.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "p2p_friends.h"
#include "p2p_party.h"
#include "lang.h"
#include "halo_port_limits.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---------- constants */

enum
{
	FRIENDS_VERSION = 1,
	ENVELOPE_HEADER_SIZE = 3,
	KIND_REQUEST = 'R',
	KIND_BOX = 'S',
	KIND_PAIR = 'P',

	TYPE_REQUEST = 1,
	TYPE_ACCEPT = 2,
	TYPE_PRESENCE = 3,
	TYPE_INVITE = 4,
	TYPE_REMOVE = 5,
	/* the party's (p2p_party.inc) */
	TYPE_PARTY_INVITE = 6,
	TYPE_PARTY_ANSWER = 7,
	TYPE_PARTY_ROSTER = 8,
	TYPE_PARTY_LEAVE = 9,
	TYPE_PARTY_STATUS = 10,
	TYPE_PARTY_FOLLOW = 11,
	TYPE_PARTY_CHAT = 12,
	TYPE_PARTY_PRESENCE = 13,
	PARTY_ID_LENGTH = 16,

	NAME_LENGTH = P2P_FRIEND_NAME_SIZE - 1,
	MAP_LENGTH = 32,
	GAMETYPE_LENGTH = 24,
	NONCE_LENGTH = 16,
	FRIENDSHIP_LENGTH = 16,
	INVITE_ID_LENGTH = 8,

	/* type, sender, recipient, epoch, counter */
	PLAIN_HEADER_SIZE = 1 + P2P_KEY_SIZE + P2P_KEY_SIZE + 8 + 4,
	/* the longest body: a party's roster (its id, serial, count, leader and
	eight keys; presence's is 116 bytes, 188 with a password's sealed token) */
	MAXIMUM_BODY_SIZE = PARTY_ID_LENGTH + 4 + 1 + 1 + P2P_PARTY_MAXIMUM * P2P_KEY_SIZE,
	MAXIMUM_PLAIN_SIZE = PLAIN_HEADER_SIZE + MAXIMUM_BODY_SIZE + P2P_SIGNATURE_SIZE,
	/* a box's: the header, the ephemeral key, the nonce, the tag */
	MAXIMUM_PAYLOAD_SIZE = ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE + P2P_NONCE_SIZE + MAXIMUM_PLAIN_SIZE + P2P_TAG_SIZE,

	/* presence flags */
	PRESENCE_ONLINE = 1,
	PRESENCE_WANT_REPLY = 2,
	PRESENCE_JOINABLE = 4,
	/* (with JOINABLE) the game has a password: the presence carries the
	host's Ed25519 key and its token sealed with the password's key, which
	the friend opens with the password (p2p_lobby_join_sealed), in place of
	the token (since Oct 19 2026: builds before it drop such a presence) */
	PRESENCE_LOCKED = 8,

	TOPIC_LENGTH = 64,
	/* the game's players' friend cards (exchanged over the tunnel: a
	player's options' Add friend and Block, p2p_friends_player_ask) */
	CARD_VERSION = 1,
	CARD_ASK = 1,
	CARD_ANSWER = 2,
	/* version, kind, from machine, to machine, nonce, then (an answer) the
	key, the name and the signature */
	CARD_HEADER_SIZE = 4 + NONCE_LENGTH,
	CARD_ANSWER_SIZE = CARD_HEADER_SIZE + P2P_KEY_SIZE + 1 + NAME_LENGTH + P2P_SIGNATURE_SIZE,
	CARD_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	MAXIMUM_CARD_INBOUND = 16,
	MAXIMUM_CARD_OUTBOUND = 16,
	/* an ask unanswered in this long: the player takes no friend requests
	this way (friends off, appearing offline, or an older build) */
	CARD_ANSWER_TIME = 10000,
	/* an ask of one player each this often; an answer to one asker each this
	often; what a host relays: a few at once, one more each this often, per
	machine and in all */
	CARD_ASK_INTERVAL = 5000,
	CARD_ANSWER_INTERVAL = 3000,
	CARD_RELAY_BUDGET = 8,
	CARD_RELAY_INTERVAL = 250,
	CARD_RELAY_MACHINE_INTERVAL = 1000,
	MAXIMUM_INBOUND = 64,
	MAXIMUM_OUTBOUND = 64,
	/* what is kept free of the outgoing queue for what is not presence */
	OUTBOUND_RESERVE = 16,
	REPLAY_WINDOW = 64,
	MAXIMUM_SEEN_REQUESTS = 64,
	MAXIMUM_TOMBSTONES = 32,
	MAXIMUM_MUTED = 32,
	MAXIMUM_BROKER_MASK = 0xFF,
	/* messages handled in one pass of internet play's thread */
	MESSAGES_PER_PASS = 16,
	/* signatures made in one pass (presence to many friends is sealed, not
	signed, per friend) */
	SIGNATURES_PER_PASS = 4,
	/* pair keys worked out in one pass (an X25519 each) */
	PAIR_KEYS_PER_PASS = 8,

	/* milliseconds */
	PRESENCE_INTERVAL = 60000,
	PRESENCE_CHANGE_INTERVAL = 3000,
	PRESENCE_TIMEOUT = 150000,
	PRESENCE_BUDGET = 6,
	PRESENCE_BUDGET_INTERVAL = 2000,
	REQUEST_FAST_INTERVAL = 30000,
	REQUEST_FAST_TIME = 600000,
	REQUEST_SLOW_INTERVAL = 300000,
	INVITE_INTERVAL = 10000,
	INVITE_LIFETIME = 300000,
	OUTBOUND_LIFETIME = 20000,
	REMOVE_REPLY_INTERVAL = 30000,
	SAVE_INTERVAL = 2000,
	COUNTERS_SAVE_INTERVAL = 300000,
	/* budgets: at most this many at once, one more each interval */
	REQUEST_BUDGET = 10,
	REQUEST_BUDGET_INTERVAL = 2000,
	BOX_BUDGET = 10,
	BOX_BUDGET_INTERVAL = 500,
	/* pair messages no key opened (each tried with every friend's): past
	this, pair messages wait unopened (dropped) a while */
	UNOPENED_BUDGET = 20,
	UNOPENED_BUDGET_INTERVAL = 250,

	/* seconds (Unix time, kept in the file) */
	OUTGOING_LIFETIME = 14 * 24 * 3600,
	MUTE_TIME = 7 * 24 * 3600,
	TOMBSTONE_TIME = 30 * 24 * 3600,

	/* the files */
	IDENTITY_VERSION = 1,
	IDENTITY_SIZE = 4 + 1 + P2P_SEED_SIZE + 8 + P2P_SHA256_SIZE,
	LIST_VERSION = 1,
	LIST_HEADER_SIZE = 4 + 1 + 6,
	FRIEND_RECORD_SIZE = P2P_KEY_SIZE + 1 + NAME_LENGTH + FRIENDSHIP_LENGTH + 8 + 8 + 4,
	REQUEST_RECORD_SIZE = P2P_KEY_SIZE + 1 + NAME_LENGTH + NONCE_LENGTH + 8,
	OUTGOING_RECORD_SIZE = P2P_FRIEND_CODE_LENGTH + NONCE_LENGTH + 8,
	BLOCKED_RECORD_SIZE = P2P_KEY_SIZE + 1 + NAME_LENGTH,
	TOMBSTONE_RECORD_SIZE = P2P_KEY_SIZE + FRIENDSHIP_LENGTH + 8,
	MUTED_RECORD_SIZE = P2P_KEY_SIZE + 8,
	MAXIMUM_LIST_SIZE = LIST_HEADER_SIZE + P2P_FRIENDS_MAXIMUM * FRIEND_RECORD_SIZE +
		P2P_FRIEND_REQUESTS_MAXIMUM * REQUEST_RECORD_SIZE + P2P_FRIEND_OUTGOING_MAXIMUM * OUTGOING_RECORD_SIZE +
		P2P_FRIEND_BLOCKED_MAXIMUM * BLOCKED_RECORD_SIZE + MAXIMUM_TOMBSTONES * TOMBSTONE_RECORD_SIZE +
		MAXIMUM_MUTED * MUTED_RECORD_SIZE + P2P_SHA256_SIZE,
	PATH_SIZE = 512,
};

static const char signature_label[] = "hcev-friends-1";
/* the friends' topics' prefix: hcev on every build, whatever its
signalling's (a PC build of this tree signals on hceu), as the labels are
(OpenCE's friends, its PR, hcev/f1 and hcev-friends-1 too) */
#define FRIENDS_TOPIC_PREFIX "hcev"

/* ---------- structures */

/* a player's machine in the game this machine plays (p2p_friends_set_game):
its address (the host's table), and its friend card once asked for */
struct card_machine
{
	unsigned long address;
	int has_card;
	unsigned char key[P2P_KEY_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	/* this machine's ask: its nonce, when, what for (P2P_FRIENDS_PLAYER_*);
	whether it went unanswered */
	int asking;
	unsigned char nonce[NONCE_LENGTH];
	unsigned long asked_time;
	int intent;
	int unanswered;
	/* (answering it) when it was last answered; (a host relaying) its relay
	budget */
	unsigned long answered_time;
	unsigned long relayed_time;
};

/* a card packet heard on the tunnel (p2p.c), or to send there */
struct card_packet
{
	/* heard: the peer it came from (its virtual address; is_host: the host
	this machine joined); to send: the peer (0: the host) */
	unsigned long address;
	int from_host;
	int size;
	unsigned char bytes[CARD_ANSWER_SIZE];
};

/* work allowed: some at once, and one more each interval (p2p_signal.c's) */
struct budget
{
	int left;
	unsigned long time;
};

/* what a friend's presence said */
struct presence
{
	int online;
	int state;
	int joinable;
	/* (wire: its game on the crossplay wire, network_wire.h; pc_host: and a
	PC's (a game they joined), else a Vita's) */
	int engine, difficulty, players, maximum, version, wire, pc_map, pc_host;
	/* (since Oct 20 2026, bits older builds ignore: network_wire.h) the
	sender is this project's PC build; the game's host is; the game takes
	Vitas only */
	int sender_pc, host_pc_build, vitas_only;
	char map[MAP_LENGTH + 1];
	char gametype[GAMETYPE_LENGTH + 1];
	unsigned char invite_hash[P2P_KEY_HASH_SIZE];
	unsigned char invite_token[P2P_TOKEN_SIZE];
	/* (a joinable game with a password: PRESENCE_LOCKED) */
	int locked;
	unsigned char signing_key[P2P_KEY_SIZE];
	unsigned char sealed_token[P2P_SEALED_TOKEN_SIZE];
};

struct friend_record
{
	unsigned char key[P2P_KEY_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned char friendship[FRIENDSHIP_LENGTH];
	unsigned long long since;
	/* replays: the newest epoch seen (kept), its highest counter and the
	REPLAY_WINDOW before it (bit n: highest - n) */
	unsigned long long epoch;
	unsigned long counter;
	unsigned long long window;
	/* the pair's keys (worked out once a run: an X25519) */
	int keys_state;
	unsigned char send_key[P2P_SHA256_SIZE];
	unsigned char receive_key[P2P_SHA256_SIZE];
	/* their presence, when it was last heard (0: never this run) */
	struct presence presence;
	unsigned long heard_time;
	unsigned long presence_time;
	/* their presences taken: a few at once, one more each
	PRESENCE_BUDGET_INTERVAL (their own sends are paced: this is a flood's
	cap, not theirs) */
	struct budget presence_budget;
	/* this machine's presence due to them; the counter of the last sent
	them; they asked for a reply */
	int presence_due;
	unsigned long sent_counter;
	int reply_asked;
	unsigned long invite_sent_time;
	unsigned long invite_heard_time;
	/* their party, as their party presence said (p2p_party.inc): its
	leader's key and size (0: none), when heard; party invites to and from
	them (rate) */
	unsigned char party_leader[P2P_KEY_SIZE];
	int party_size;
	unsigned long party_heard_time;
	unsigned long party_invite_sent_time;
	unsigned long party_invite_heard_time;
};

struct request_record
{
	unsigned char key[P2P_KEY_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned char nonce[NONCE_LENGTH];
	unsigned long long time;
};

struct outgoing_record
{
	char code[P2P_FRIEND_CODE_LENGTH + 1];
	unsigned char nonce[NONCE_LENGTH];
	unsigned long long time;
	/* this run: when it was last sent, and since when it is sent */
	unsigned long sent_time;
	unsigned long first_time;
};

struct blocked_record
{
	unsigned char key[P2P_KEY_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
};

struct tombstone_record
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned char friendship[FRIENDSHIP_LENGTH];
	unsigned long long time;
	unsigned long reply_time;
	/* the pair's keys, worked out the first time a message is tried */
	int keys_state;
	unsigned char send_key[P2P_SHA256_SIZE];
	unsigned char receive_key[P2P_SHA256_SIZE];
};

struct muted_record
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned long long time;
};

struct invite_record
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned char invite_id[INVITE_ID_LENGTH];
	struct presence game;
	unsigned long time;
	int asked;
};

/* a request heard from a key not a friend's (replays of requests) */
struct seen_request
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned long long epoch;
	unsigned long counter;
};

struct inbound
{
	int which;
	int size;
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
};

struct outbound
{
	unsigned long serial;
	char topic[TOPIC_LENGTH];
	int size;
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	int sent_mask;
	unsigned long time;
	/* (presence: whose, so a newer one replaces it) */
	int presence;
	unsigned char recipient[P2P_KEY_SIZE];
};

/* a message, read or to write */
struct message
{
	int type;
	unsigned char sender[P2P_KEY_SIZE];
	unsigned char recipient[P2P_KEY_SIZE];
	unsigned long long epoch;
	unsigned long counter;
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned char nonce[NONCE_LENGTH];
	unsigned char friendship[FRIENDSHIP_LENGTH];
	unsigned char invite_id[INVITE_ID_LENGTH];
	int flags;
	struct presence game;
	/* (the party's, p2p_party.inc: its id; a roster's serial, members and
	leader, a follow's serial; a value - an answer, a reason, flags - and a
	code - a follow's result, a chat's kind; a status's follow serial; a
	chat's text) */
	unsigned char party_id[PARTY_ID_LENGTH];
	unsigned long party_serial;
	int party_count, party_leader;
	unsigned char party_keys[P2P_PARTY_MAXIMUM][P2P_KEY_SIZE];
	int party_value, party_code;
	unsigned long party_follow;
	char chat[P2P_PARTY_CHAT_SIZE];
};

/* this machine's activity, as the game says (p2p_friends_set_activity) */
struct activity
{
	int state, engine, difficulty, pc_map, players, maximum;
	char map[MAP_LENGTH + 1];
	char gametype[GAMETYPE_LENGTH + 1];
};

/* ---------- the party's (p2p_party.inc) */

/* a member: their key (the roster's), their own name (their status's), the
pair's keys (a member not a friend's, worked out once), when last heard,
what their status said, local mute, and their messages' budgets */
struct party_member
{
	unsigned char key[P2P_KEY_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	int keys_state;
	unsigned char send_key[P2P_SHA256_SIZE];
	unsigned char receive_key[P2P_SHA256_SIZE];
	unsigned long heard_time;
	int status_flags, follow_code;
	unsigned long follow_serial;
	int muted;
	/* on this project's PC build, as its party follow said (network_wire.h) */
	int pc;
	struct budget chat_budget;
	struct budget status_budget;
};

/* the party's messages' replays, by sender (whatever their membership:
each counter of a sender taken once) */
struct party_seen
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned long long epoch;
	unsigned long counter;
	unsigned long long window;
	unsigned long time;
};

/* a party invite heard (from a friend) */
struct party_invite_record
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned char party_id[PARTY_ID_LENGTH];
	int size;
	unsigned long time;
	int asked;
	/* accepted: waiting for the leader's roster */
	int accepted;
};

/* an invite this machine sent (its leader's), not answered yet */
struct party_sent_invite
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned long time;
};

struct party_chat_record
{
	unsigned long number;
	unsigned char key[P2P_KEY_SIZE];
	char text[P2P_PARTY_CHAT_SIZE];
	int self, notice;
};

struct party_state
{
	int active;
	unsigned char id[PARTY_ID_LENGTH];
	unsigned long serial;
	/* the roster: the longest-standing first; this machine one of them */
	struct party_member members[P2P_PARTY_MAXIMUM];
	int count;
	int leader;
	struct party_sent_invite sent[P2P_PARTY_MAXIMUM];
	int sent_count;
	unsigned long roster_time;
	/* this machine's status: due, when sent, what it said */
	int status_due;
	unsigned long status_time;
	int status_flags;
	/* (the leader) the follow: its serial, what it leads to, when sent */
	unsigned long follow_serial;
	int follow_active;
	unsigned char follow_hash[P2P_KEY_HASH_SIZE];
	unsigned char follow_token[P2P_TOKEN_SIZE];
	int follow_flags;
	unsigned long follow_time;
	/* (a member) the leader's follow heard: to join, and what came of it */
	unsigned long follow_heard;
	int follow_pending;
	struct message follow_message;
	unsigned long follow_heard_time;
	unsigned long follow_done;
	int follow_result;
	/* the party presence sent to friends: its size and leader, when */
	int presence_size;
	unsigned char presence_leader[P2P_KEY_SIZE];
	unsigned long presence_time;
	int presence_due;
};

struct friends_state
{
	int loaded;
	/* the identity: its seed, keys, code, topics; the epoch of this run
	and the counter of the messages sent in it */
	int has_identity;
	unsigned char seed[P2P_SEED_SIZE];
	unsigned char key[P2P_KEY_SIZE];
	unsigned char x25519_secret[P2P_KEY_SIZE];
	unsigned char x25519_public[P2P_KEY_SIZE];
	char code[P2P_FRIEND_CODE_LENGTH + 1];
	char inbox_topic[TOPIC_LENGTH];
	char mailbox_topic[TOPIC_LENGTH];
	unsigned char inbox_key[P2P_SHA256_SIZE];
	unsigned char code_recipient[P2P_SHA256_SIZE];
	unsigned long long epoch;
	unsigned long counter;
	int epoch_written;

	struct friend_record friends[P2P_FRIENDS_MAXIMUM];
	int friend_count;
	struct request_record requests[P2P_FRIEND_REQUESTS_MAXIMUM];
	int request_count;
	struct outgoing_record outgoing[P2P_FRIEND_OUTGOING_MAXIMUM];
	int outgoing_count;
	struct blocked_record blocked[P2P_FRIEND_BLOCKED_MAXIMUM];
	int blocked_count;
	struct tombstone_record tombstones[MAXIMUM_TOMBSTONES];
	int tombstone_count;
	struct muted_record muted[MAXIMUM_MUTED];
	int muted_count;
	struct invite_record invites[P2P_FRIEND_INVITES_MAXIMUM];
	int invite_count;
	struct seen_request seen[MAXIMUM_SEEN_REQUESTS];
	int seen_next;

	/* the party (p2p_party.inc), its invites heard, its replays, its chat */
	struct party_state party;
	struct party_invite_record party_invites[P2P_FRIEND_INVITES_MAXIMUM];
	int party_invite_count;
	struct party_seen party_seen[32];
	struct party_chat_record chat[P2P_PARTY_CHAT_LINES];
	int chat_count;
	unsigned long chat_number;
	struct budget chat_send_budget;

	/* the list's file: to write (dirty: soon; counters: lazily) */
	int dirty;
	int counters_dirty;
	unsigned long saved_time;
	unsigned long counters_saved_time;

	struct inbound inbound[MAXIMUM_INBOUND];
	int inbound_count;
	struct outbound outbound[MAXIMUM_OUTBOUND];
	int outbound_count;
	unsigned long outbound_serial;

	/* internet play's side, as of the last pass; the game exiting (nothing
	more sent) */
	struct p2p_friends_host host;
	int online;
	int quitting;
	/* this machine's presence: what the game says, what was last sent
	(signed once, sealed per friend), when, and whether it was online */
	struct activity activity;
	int activity_changed;
	unsigned char presence_plain[MAXIMUM_PLAIN_SIZE];
	int presence_size;
	unsigned long presence_counter;
	unsigned long presence_time;
	unsigned long presence_change_time;
	int presence_online;
	/* (own_joinable's: 0, 1, 2 with a password) and the token it carried */
	int presence_joinable;
	unsigned char presence_token[P2P_TOKEN_SIZE];
	int presence_want_reply;
	int appear_offline;

	struct budget request_budget;
	struct budget box_budget;
	struct budget unopened_budget;
	/* the player's name as the game says (p2p_friends_set_name) */
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned long started_time;
	/* drops, for the log and the tests */
	int dropped_replays, dropped_forged, dropped_blocked, dropped_rate, dropped_malformed;
	unsigned long drops_logged_time;
	/* ... and all of them this run, as logged */
	int total_replays, total_forged, total_blocked, total_rate, total_malformed;

	/* the game this machine plays: its machine's index (-1 none), whether it
	hosts (it relays cards between its machines), the machines */
	int in_game;
	int game_machine;
	int game_hosting;
	/* what a card answered asks of the lists (done with the lock let go):
	a request to its code, or its key blocked */
	int card_action;
	unsigned char card_action_key[P2P_KEY_SIZE];
	char card_action_name[P2P_FRIEND_NAME_SIZE];
	struct card_machine machines[CARD_MACHINES];
	struct card_packet card_inbound[MAXIMUM_CARD_INBOUND];
	int card_inbound_count;
	struct card_packet card_outbound[MAXIMUM_CARD_OUTBOUND];
	int card_outbound_count;
	struct budget card_relay_budget;
	int game_said;

	/* (debug) the tests' steps */
	int test_parsed;
	char test_add_file[PATH_SIZE];
	char test_code_file[PATH_SIZE];
	int test_accept, test_decline, test_block;
	long test_invite_after, test_appear_offline_at, test_remove_at, test_replay_at, test_forge_at, test_cancel_at;
	int test_invited, test_appeared_offline, test_removed, test_replayed, test_forged, test_cancelled;
	int test_added;
	/* (the party's steps, HALO_TEST_PARTY: p2p_party.inc) */
	int test_party_parsed;
	long test_party_invite_at, test_party_leave_at, test_party_kick_at, test_party_chat_at, test_party_forge_at;
	int test_party_accept, test_party_decline;
	int test_party_invited, test_party_left, test_party_kicked, test_party_chatted, test_party_forged;
	char test_party_chat[P2P_PARTY_CHAT_SIZE];
	unsigned char test_last_sent[MAXIMUM_PAYLOAD_SIZE];
	int test_last_sent_size;
	char test_last_topic[TOPIC_LENGTH];
};

/* ---------- globals */

static pthread_mutex_t friends_lock = PTHREAD_MUTEX_INITIALIZER;
/* the files are written one at a time, without friends_lock */
static pthread_mutex_t friends_file_lock = PTHREAD_MUTEX_INITIALIZER;
static struct friends_state friends_instance;
/* (the tests run two machines in one process: p2p_friends_test.c) */
static struct friends_state *friends = &friends_instance;

/* ---------- small helpers */

static int elapsed(unsigned long since, unsigned long time)
{
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
}

static unsigned long now_stamp(void)
{
	unsigned long now = p2p_now();

	return now ? now : (unsigned long)-1;
}

static unsigned long long unix_now(void)
{
	time_t now = time(NULL);

	return now > 0 ? (unsigned long long)now : 0;
}

static int budget_left(struct budget *budget, int maximum, int interval, int spend)
{
	unsigned long now = p2p_now();
	unsigned long earned = (now - budget->time) / (unsigned long)interval;

	if (!budget->time || earned >= (unsigned long)maximum || budget->left + (int)earned >= maximum)
	{
		budget->left = maximum;
		budget->time = now;
	}
	else if (earned)
	{
		budget->left += (int)earned;
		budget->time += earned * (unsigned long)interval;
	}
	if (budget->left <= 0)
		return 0;
	if (spend)
		budget->left--;
	return 1;
}

static void put_u16(unsigned char *bytes, unsigned int value)
{
	bytes[0] = (unsigned char)(value >> 8);
	bytes[1] = (unsigned char)value;
}

static unsigned int get_u16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] << 8 | bytes[1];
}

static void put_u32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

static unsigned long get_u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

static void put_u64(unsigned char *bytes, unsigned long long value)
{
	put_u32(bytes, (unsigned long)(value >> 32));
	put_u32(bytes + 4, (unsigned long)(value & 0xFFFFFFFFUL));
}

static unsigned long long get_u64(const unsigned char *bytes)
{
	return (unsigned long long)get_u32(bytes) << 32 | get_u32(bytes + 4);
}

/* text as it is shown: printable ASCII ('?' for the rest), at most maximum
characters; a name also cleaned as a player's (no "|", no spaces before or
after: p2p_lobby_clean_name). Returns 0 if a name names nothing */
static int clean_text(char *destination, int maximum, const unsigned char *source, int length, int name)
{
	int index;

	for (index = 0; index < length && index < maximum; index++)
	{
		unsigned char character = source[index];

		destination[index] = character >= 0x20 && character < 0x7F ? (char)character : '?';
	}
	destination[index] = 0;
	if (!name)
		return 1;
	return p2p_lobby_clean_name(destination);
}

/* a length byte and up to maximum characters of text */
static int put_text(unsigned char *bytes, const char *text, int maximum)
{
	int length = (int)strlen(text);

	if (length > maximum)
		length = maximum;
	bytes[0] = (unsigned char)length;
	memcpy(bytes + 1, text, (size_t)length);
	return 1 + length;
}

/* the reverse, bounded by size: 0 if it runs past the end or is too long */
static int get_text(const unsigned char *bytes, int size, int *offset, char *text, int maximum, int name)
{
	int length;

	if (*offset >= size)
		return 0;
	length = bytes[*offset];
	if (length > maximum || *offset + 1 + length > size)
		return 0;
	clean_text(text, maximum, bytes + *offset + 1, length, name);
	*offset += 1 + length;
	return 1;
}

static void key_id(const unsigned char *key, char *id)
{
	p2p_hex(key, 8, id);
}

static int key_zero(const unsigned char *key)
{
	static const unsigned char zero[P2P_KEY_SIZE];

	return p2p_equal(key, zero, P2P_KEY_SIZE);
}

/* a setting the settings panel and the menus change live (their
environment variable, set as they set it), else as config.toml has it */
static int setting_on(const char *name, const char *variable)
{
	const char *value = getenv(variable);

	if (value && value[0])
		return !strcmp(value, "true") || !strcmp(value, "1");
	return config_boolean(name);
}

/* ---------- the code */

/* a key's code: the first 60 bits of a hash of it, 12 characters */
static void code_of_key(const unsigned char *key, char *code)
{
	static const char alphabet[] = P2P_CODE_ALPHABET;
	unsigned char data[sizeof("hcev-friend-code-1") - 1 + P2P_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned long long bits;
	int index;

	memcpy(data, "hcev-friend-code-1", sizeof("hcev-friend-code-1") - 1);
	memcpy(data + sizeof("hcev-friend-code-1") - 1, key, P2P_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	bits = get_u64(digest) >> 4;
	for (index = 0; index < P2P_FRIEND_CODE_LENGTH; index++)
		code[index] = alphabet[(bits >> (5 * (P2P_FRIEND_CODE_LENGTH - 1 - index))) & 31];
	code[P2P_FRIEND_CODE_LENGTH] = 0;
}

/* a code as typed: its 12 characters (any case; dashes and spaces left
out); 0 if it is no code */
static int code_normalize(const char *text, char *code)
{
	int length = 0;

	if (!text)
		return 0;
	for (; *text; text++)
	{
		char character = *text >= 'a' && *text <= 'z' ? (char)(*text - 'a' + 'A') : *text;

		if (character == '-' || character == ' ')
			continue;
		if (!strchr(P2P_CODE_ALPHABET, character) || !character || length >= P2P_FRIEND_CODE_LENGTH)
			return 0;
		code[length++] = character;
	}
	code[length] = 0;
	return length == P2P_FRIEND_CODE_LENGTH;
}

static void code_shown(const char *code, char *text, int size)
{
	snprintf(text, (size_t)size, "%.4s-%.4s-%.4s", code, code + 4, code + 8);
}

/* what a code derives: its inbox's topic, the key requests to it are
sealed with, and what a request names as its recipient */
static void code_derive(const char *code, char *topic, unsigned char *seal_key, unsigned char *recipient)
{
	unsigned char data[32 + P2P_FRIEND_CODE_LENGTH];
	unsigned char digest[P2P_SHA256_SIZE];
	char hex[2 * P2P_SHA256_SIZE + 1];
	int size = (int)strlen("hcev-friends-1 code ");

	memcpy(data, "hcev-friends-1 code ", (size_t)size);
	memcpy(data + size, code, P2P_FRIEND_CODE_LENGTH);
	size += P2P_FRIEND_CODE_LENGTH;
	p2p_sha256(data, size, digest);
	if (recipient)
		memcpy(recipient, digest, P2P_SHA256_SIZE);
	if (seal_key)
		p2p_hmac_sha256(digest, P2P_SHA256_SIZE, "seal", 4, seal_key);
	if (topic)
	{
		unsigned char topic_digest[P2P_SHA256_SIZE];

		p2p_hmac_sha256((const unsigned char *)code, P2P_FRIEND_CODE_LENGTH, "inbox", 5, topic_digest);
		p2p_hex(topic_digest, 16, hex);
		snprintf(topic, TOPIC_LENGTH, "%s/f1/i/%s", FRIENDS_TOPIC_PREFIX, hex);
	}
}

static void mailbox_topic(const unsigned char *key, char *topic)
{
	static const char label[] = "hcev-friends-1 mailbox";
	unsigned char digest[P2P_SHA256_SIZE];
	char hex[2 * P2P_SHA256_SIZE + 1];

	p2p_hmac_sha256(key, P2P_KEY_SIZE, label, (int)sizeof(label) - 1, digest);
	p2p_hex(digest, 16, hex);
	snprintf(topic, TOPIC_LENGTH, "%s/f1/m/%s", FRIENDS_TOPIC_PREFIX, hex);
}

/* ---------- the identity */

static void identity_from_seed(void)
{
	p2p_ed25519_public(friends->seed, friends->key, friends->x25519_secret);
	p2p_ed25519_to_x25519(friends->key, friends->x25519_public);
	code_of_key(friends->key, friends->code);
	code_derive(friends->code, friends->inbox_topic, friends->inbox_key, friends->code_recipient);
	mailbox_topic(friends->key, friends->mailbox_topic);
	friends->has_identity = 1;
}

/* ---------- the files */

static int friends_folder(char *path, int size)
{
#if defined(HALO_VITA) && !defined(P2P_FRIENDS_TEST)
	return snprintf(path, (size_t)size, "ux0:data/haloce-vita/friends") < size;
#else
	return snprintf(path, (size_t)size, "%s/friends", platform_save_root()) < size;
#endif
}

static int file_path(const char *name, const char *extension, char *path, int size)
{
	char folder[PATH_SIZE];

	if (!friends_folder(folder, sizeof(folder)))
		return 0;
	return snprintf(path, (size_t)size, "%s/%s%s", folder, name, extension) < size;
}

static int file_read(const char *path, unsigned char *bytes, int capacity)
{
	FILE *file = fopen(path, "rb");
	size_t count;

	if (!file)
		return -1;
	count = fread(bytes, 1, (size_t)capacity, file);
	fclose(file);
	return (int)count;
}

static int file_write(const char *path, const unsigned char *bytes, int size)
{
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(bytes, 1, (size_t)size, file) == (size_t)size;
	if (fclose(file) != 0)
		written = 0;
	if (!written)
		remove(path);
	return written;
}

static int file_exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}

/* bytes written to <name>: a .tmp written, read back and compared, the old
file renamed .old, the new one renamed in, the old one deleted (a file
system that does not rename over a file - the Vita's - is served the same
way). Whatever fails, the old file is where it was. Under
friends_file_lock */
static int file_put(const char *name, const unsigned char *bytes, int size)
{
	char path[PATH_SIZE], temporary[PATH_SIZE], old[PATH_SIZE], folder[PATH_SIZE];
	unsigned char *check;
	int replaced = 0, result;

	if (!friends_folder(folder, sizeof(folder)) || !file_path(name, "", path, sizeof(path)) ||
		!file_path(name, ".tmp", temporary, sizeof(temporary)) || !file_path(name, ".old", old, sizeof(old)))
	{
		return 0;
	}
	posix_make_directory(folder);
	check = malloc((size_t)size + 1);
	if (!check)
		return 0;
	remove(temporary);
	if (!file_write(temporary, bytes, size))
	{
		free(check);
		platform_log("friends: cannot write %s (a full card?)", temporary);
		return 0;
	}
	result = file_read(temporary, check, size + 1);
	if (result != size || memcmp(check, bytes, (size_t)size))
	{
		free(check);
		remove(temporary);
		platform_log("friends: %s did not read back", temporary);
		return 0;
	}
	free(check);
	if (file_exists(path))
	{
		remove(old);
		if (rename(path, old) != 0)
		{
			remove(temporary);
			return 0;
		}
		replaced = 1;
	}
	if (rename(temporary, path) != 0)
	{
		remove(temporary);
		if (replaced)
			rename(old, path);
		return 0;
	}
	if (replaced)
		remove(old);
	return 1;
}

/* the identity file's bytes */
static int identity_encode(const unsigned char *seed, unsigned long long epoch, unsigned char *bytes)
{
	int size = 0;

	memcpy(bytes, "HFID", 4);
	size += 4;
	bytes[size++] = IDENTITY_VERSION;
	memcpy(bytes + size, seed, P2P_SEED_SIZE);
	size += P2P_SEED_SIZE;
	put_u64(bytes + size, epoch);
	size += 8;
	p2p_sha256(bytes, size, bytes + size);
	return size + P2P_SHA256_SIZE;
}

/* read: 1 and the seed and epoch, or 0 if the bytes are no identity */
static int identity_decode(const unsigned char *bytes, int size, unsigned char *seed, unsigned long long *epoch)
{
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned char public_key[P2P_KEY_SIZE], x25519[P2P_KEY_SIZE];

	if (size != IDENTITY_SIZE || memcmp(bytes, "HFID", 4) || bytes[4] != IDENTITY_VERSION)
		return 0;
	p2p_sha256(bytes, size - P2P_SHA256_SIZE, digest);
	if (!p2p_equal(digest, bytes + size - P2P_SHA256_SIZE, P2P_SHA256_SIZE))
		return 0;
	memcpy(seed, bytes + 5, P2P_SEED_SIZE);
	*epoch = get_u64(bytes + 5 + P2P_SEED_SIZE);
	/* (a seed whose key has no X25519 form: none Ed25519 makes, but the
	file is not trusted) */
	p2p_ed25519_public(seed, public_key, NULL);
	return p2p_ed25519_to_x25519(public_key, x25519);
}

/* a key usable as a friend's: an X25519 form, not this machine's */
static int key_usable(const unsigned char *key)
{
	unsigned char x25519[P2P_KEY_SIZE];

	return p2p_ed25519_to_x25519(key, x25519) && !(friends->has_identity && p2p_equal(key, friends->key, P2P_KEY_SIZE));
}

static int find_friend_index(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->friend_count; index++)
		if (p2p_equal(friends->friends[index].key, key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static struct friend_record *find_friend(const unsigned char *key)
{
	int index = find_friend_index(key);

	return index >= 0 ? &friends->friends[index] : NULL;
}

static int find_request(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->request_count; index++)
		if (p2p_equal(friends->requests[index].key, key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static int find_blocked(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->blocked_count; index++)
		if (p2p_equal(friends->blocked[index].key, key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static int find_tombstone(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->tombstone_count; index++)
		if (p2p_equal(friends->tombstones[index].key, key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static int find_muted(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->muted_count; index++)
		if (p2p_equal(friends->muted[index].key, key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static int find_outgoing(const char *code)
{
	int index;

	for (index = 0; index < friends->outgoing_count; index++)
		if (!strcmp(friends->outgoing[index].code, code))
			return index;
	return -1;
}

/* the list's bytes (friends.dat) */
static int list_encode(unsigned char *bytes)
{
	int size = 0, index;

	memcpy(bytes, "HFRL", 4);
	size += 4;
	bytes[size++] = LIST_VERSION;
	bytes[size++] = (unsigned char)friends->friend_count;
	bytes[size++] = (unsigned char)friends->request_count;
	bytes[size++] = (unsigned char)friends->outgoing_count;
	bytes[size++] = (unsigned char)friends->blocked_count;
	bytes[size++] = (unsigned char)friends->tombstone_count;
	bytes[size++] = (unsigned char)friends->muted_count;
	for (index = 0; index < friends->friend_count; index++)
	{
		const struct friend_record *record = &friends->friends[index];

		memcpy(bytes + size, record->key, P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		memset(bytes + size, 0, 1 + NAME_LENGTH);
		put_text(bytes + size, record->name, NAME_LENGTH);
		size += 1 + NAME_LENGTH;
		memcpy(bytes + size, record->friendship, FRIENDSHIP_LENGTH);
		size += FRIENDSHIP_LENGTH;
		put_u64(bytes + size, record->since);
		size += 8;
		put_u64(bytes + size, record->epoch);
		size += 8;
		put_u32(bytes + size, record->counter);
		size += 4;
	}
	for (index = 0; index < friends->request_count; index++)
	{
		const struct request_record *record = &friends->requests[index];

		memcpy(bytes + size, record->key, P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		memset(bytes + size, 0, 1 + NAME_LENGTH);
		put_text(bytes + size, record->name, NAME_LENGTH);
		size += 1 + NAME_LENGTH;
		memcpy(bytes + size, record->nonce, NONCE_LENGTH);
		size += NONCE_LENGTH;
		put_u64(bytes + size, record->time);
		size += 8;
	}
	for (index = 0; index < friends->outgoing_count; index++)
	{
		const struct outgoing_record *record = &friends->outgoing[index];

		memcpy(bytes + size, record->code, P2P_FRIEND_CODE_LENGTH);
		size += P2P_FRIEND_CODE_LENGTH;
		memcpy(bytes + size, record->nonce, NONCE_LENGTH);
		size += NONCE_LENGTH;
		put_u64(bytes + size, record->time);
		size += 8;
	}
	for (index = 0; index < friends->blocked_count; index++)
	{
		memcpy(bytes + size, friends->blocked[index].key, P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		memset(bytes + size, 0, 1 + NAME_LENGTH);
		put_text(bytes + size, friends->blocked[index].name, NAME_LENGTH);
		size += 1 + NAME_LENGTH;
	}
	for (index = 0; index < friends->tombstone_count; index++)
	{
		memcpy(bytes + size, friends->tombstones[index].key, P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		memcpy(bytes + size, friends->tombstones[index].friendship, FRIENDSHIP_LENGTH);
		size += FRIENDSHIP_LENGTH;
		put_u64(bytes + size, friends->tombstones[index].time);
		size += 8;
	}
	for (index = 0; index < friends->muted_count; index++)
	{
		memcpy(bytes + size, friends->muted[index].key, P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		put_u64(bytes + size, friends->muted[index].time);
		size += 8;
	}
	p2p_sha256(bytes, size, bytes + size);
	return size + P2P_SHA256_SIZE;
}

/* a key read from the file: usable, and in none of the lists read before
it (each key once in all of them) */
static int list_key_fresh(const unsigned char *key)
{
	return key_usable(key) && find_friend_index(key) < 0 && find_request(key) < 0 && find_blocked(key) < 0 &&
		find_tombstone(key) < 0 && find_muted(key) < 0;
}

/* a name read from the file (a length byte, then its room): cleaned; 0 if
the length runs past its room or the name names nothing */
static int list_name(const unsigned char *bytes, char *name)
{
	if (bytes[0] > NAME_LENGTH)
		return 0;
	return clean_text(name, NAME_LENGTH, bytes + 1, bytes[0], 1);
}

/* the list read into friends (cleared first): 1, or 0 if the bytes are no
list this build wrote (friends then empty) */
static int list_decode(const unsigned char *bytes, int size)
{
	unsigned char digest[P2P_SHA256_SIZE];
	int counts[6], expected, offset, index;

	friends->friend_count = friends->request_count = friends->outgoing_count = 0;
	friends->blocked_count = friends->tombstone_count = friends->muted_count = 0;
	if (size < LIST_HEADER_SIZE + P2P_SHA256_SIZE || memcmp(bytes, "HFRL", 4) || bytes[4] != LIST_VERSION)
		return 0;
	for (index = 0; index < 6; index++)
		counts[index] = bytes[5 + index];
	if (counts[0] > P2P_FRIENDS_MAXIMUM || counts[1] > P2P_FRIEND_REQUESTS_MAXIMUM ||
		counts[2] > P2P_FRIEND_OUTGOING_MAXIMUM || counts[3] > P2P_FRIEND_BLOCKED_MAXIMUM ||
		counts[4] > MAXIMUM_TOMBSTONES || counts[5] > MAXIMUM_MUTED)
	{
		return 0;
	}
	expected = LIST_HEADER_SIZE + counts[0] * FRIEND_RECORD_SIZE + counts[1] * REQUEST_RECORD_SIZE +
		counts[2] * OUTGOING_RECORD_SIZE + counts[3] * BLOCKED_RECORD_SIZE + counts[4] * TOMBSTONE_RECORD_SIZE +
		counts[5] * MUTED_RECORD_SIZE + P2P_SHA256_SIZE;
	if (size != expected)
		return 0;
	p2p_sha256(bytes, size - P2P_SHA256_SIZE, digest);
	if (!p2p_equal(digest, bytes + size - P2P_SHA256_SIZE, P2P_SHA256_SIZE))
		return 0;
	offset = LIST_HEADER_SIZE;
	for (index = 0; index < counts[0]; index++, offset += FRIEND_RECORD_SIZE)
	{
		struct friend_record *record = &friends->friends[friends->friend_count];

		memset(record, 0, sizeof(*record));
		memcpy(record->key, bytes + offset, P2P_KEY_SIZE);
		if (!list_key_fresh(record->key) || !list_name(bytes + offset + P2P_KEY_SIZE, record->name))
			goto bad;
		memcpy(record->friendship, bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH, FRIENDSHIP_LENGTH);
		record->since = get_u64(bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH + FRIENDSHIP_LENGTH);
		record->epoch = get_u64(bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH + FRIENDSHIP_LENGTH + 8);
		record->counter = get_u32(bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH + FRIENDSHIP_LENGTH + 16);
		/* (the counter read is the highest seen: the window behind it full) */
		record->window = ~0ULL;
		friends->friend_count++;
	}
	for (index = 0; index < counts[1]; index++, offset += REQUEST_RECORD_SIZE)
	{
		struct request_record *record = &friends->requests[friends->request_count];

		memset(record, 0, sizeof(*record));
		memcpy(record->key, bytes + offset, P2P_KEY_SIZE);
		if (!list_key_fresh(record->key) || !list_name(bytes + offset + P2P_KEY_SIZE, record->name))
			goto bad;
		memcpy(record->nonce, bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH, NONCE_LENGTH);
		record->time = get_u64(bytes + offset + P2P_KEY_SIZE + 1 + NAME_LENGTH + NONCE_LENGTH);
		friends->request_count++;
	}
	for (index = 0; index < counts[2]; index++, offset += OUTGOING_RECORD_SIZE)
	{
		struct outgoing_record *record = &friends->outgoing[friends->outgoing_count];
		char typed[P2P_FRIEND_CODE_LENGTH + 1];

		memset(record, 0, sizeof(*record));
		memcpy(typed, bytes + offset, P2P_FRIEND_CODE_LENGTH);
		typed[P2P_FRIEND_CODE_LENGTH] = 0;
		if (!code_normalize(typed, record->code) || find_outgoing(record->code) >= 0 ||
			(friends->has_identity && !strcmp(record->code, friends->code)))
		{
			goto bad;
		}
		memcpy(record->nonce, bytes + offset + P2P_FRIEND_CODE_LENGTH, NONCE_LENGTH);
		record->time = get_u64(bytes + offset + P2P_FRIEND_CODE_LENGTH + NONCE_LENGTH);
		friends->outgoing_count++;
	}
	for (index = 0; index < counts[3]; index++, offset += BLOCKED_RECORD_SIZE)
	{
		struct blocked_record *record = &friends->blocked[friends->blocked_count];

		memset(record, 0, sizeof(*record));
		memcpy(record->key, bytes + offset, P2P_KEY_SIZE);
		if (!list_key_fresh(record->key))
			goto bad;
		/* (a blocked player's name may name nothing: shown as such) */
		if (!list_name(bytes + offset + P2P_KEY_SIZE, record->name) && bytes[offset + P2P_KEY_SIZE] > NAME_LENGTH)
			goto bad;
		friends->blocked_count++;
	}
	for (index = 0; index < counts[4]; index++, offset += TOMBSTONE_RECORD_SIZE)
	{
		struct tombstone_record *record = &friends->tombstones[friends->tombstone_count];

		memset(record, 0, sizeof(*record));
		memcpy(record->key, bytes + offset, P2P_KEY_SIZE);
		if (!list_key_fresh(record->key))
			goto bad;
		memcpy(record->friendship, bytes + offset + P2P_KEY_SIZE, FRIENDSHIP_LENGTH);
		record->time = get_u64(bytes + offset + P2P_KEY_SIZE + FRIENDSHIP_LENGTH);
		friends->tombstone_count++;
	}
	for (index = 0; index < counts[5]; index++, offset += MUTED_RECORD_SIZE)
	{
		struct muted_record *record = &friends->muted[friends->muted_count];

		memset(record, 0, sizeof(*record));
		memcpy(record->key, bytes + offset, P2P_KEY_SIZE);
		if (!list_key_fresh(record->key))
			goto bad;
		record->time = get_u64(bytes + offset + P2P_KEY_SIZE);
		friends->muted_count++;
	}
	return 1;
bad:
	friends->friend_count = friends->request_count = friends->outgoing_count = 0;
	friends->blocked_count = friends->tombstone_count = friends->muted_count = 0;
	return 0;
}

/* a file's bytes, the file itself or (a write cut short between its
renames) its .old; -1 if neither */
static int file_take(const char *name, unsigned char *bytes, int capacity, int old)
{
	char path[PATH_SIZE];

	if (!file_path(name, old ? ".old" : "", path, sizeof(path)))
		return -1;
	return file_read(path, bytes, capacity);
}

/* keeps a file that failed its checks as <name>.bad, for a report */
static void file_set_aside(const char *name)
{
	char path[PATH_SIZE], bad[PATH_SIZE];

	if (!file_path(name, "", path, sizeof(path)) || !file_path(name, ".bad", bad, sizeof(bad)))
		return;
	remove(bad);
	if (rename(path, bad) == 0)
		platform_log("friends: %s failed its checks; kept as %s, not used", path, bad);
}

/* the identity and the list read, once; a new identity made if there is
none (under friends_lock) */
static void load(void)
{
	unsigned char bytes[MAXIMUM_LIST_SIZE + 1];
	int size, taken = 0, old;

	if (friends->loaded)
		return;
	friends->loaded = 1;
	pthread_mutex_lock(&friends_file_lock);
	for (old = 0; old < 2 && !taken; old++)
	{
		size = file_take("identity.dat", bytes, IDENTITY_SIZE + 1, old);
		if (size >= 0)
		{
			taken = identity_decode(bytes, size, friends->seed, &friends->epoch);
			if (!taken && !old)
				file_set_aside("identity.dat");
		}
	}
	if (!taken)
	{
		posix_random_bytes(friends->seed, sizeof(friends->seed));
		friends->epoch = 0;
		platform_log("friends: a new friend identity made");
	}
	identity_from_seed();
	/* (this run's epoch: newer than every one before, written before
	anything is sent) */
	friends->epoch = friends->epoch + 1 > unix_now() ? friends->epoch + 1 : unix_now();
	size = identity_encode(friends->seed, friends->epoch, bytes);
	friends->epoch_written = file_put("identity.dat", bytes, size);
	if (!friends->epoch_written)
		platform_log("friends: the identity could not be written; friends will see this run's messages, not keep it");
	taken = 0;
	for (old = 0; old < 2 && !taken; old++)
	{
		size = file_take("friends.dat", bytes, sizeof(bytes), old);
		if (size >= 0)
		{
			taken = list_decode(bytes, size);
			if (!taken && !old)
				file_set_aside("friends.dat");
		}
	}
	pthread_mutex_unlock(&friends_file_lock);
	{
		char shown[P2P_FRIEND_CODE_SIZE];

		code_shown(friends->code, shown, sizeof(shown));
		platform_log("friends: this machine's friend code is %s (%d friends, %d requests, %d sent, %d blocked)", shown,
			friends->friend_count, friends->request_count, friends->outgoing_count, friends->blocked_count);
	}
	friends->started_time = now_stamp();
}

/* the list written if it changed (counters: lazily; force: now, whatever
the interval). Takes friends_lock itself */
static void save(int force)
{
	unsigned char bytes[MAXIMUM_LIST_SIZE];
	int size, written;

	pthread_mutex_lock(&friends_lock);
	if (!friends->loaded || (!friends->dirty && !(friends->counters_dirty && (force ||
		elapsed(friends->counters_saved_time, COUNTERS_SAVE_INTERVAL)))) ||
		(!force && friends->saved_time && !elapsed(friends->saved_time, SAVE_INTERVAL)))
	{
		pthread_mutex_unlock(&friends_lock);
		return;
	}
	size = list_encode(bytes);
	friends->dirty = 0;
	friends->counters_dirty = 0;
	friends->saved_time = now_stamp();
	friends->counters_saved_time = friends->saved_time;
	pthread_mutex_unlock(&friends_lock);
	pthread_mutex_lock(&friends_file_lock);
	written = file_put("friends.dat", bytes, size);
	pthread_mutex_unlock(&friends_file_lock);
	if (!written)
	{
		pthread_mutex_lock(&friends_lock);
		friends->dirty = 1;
		pthread_mutex_unlock(&friends_lock);
		platform_log("friends: the friends list could not be written; trying again");
	}
}

/* ---------- the party's hooks (p2p_party.inc, at the end of this file) */

static int party_body_encode(const struct message *message, unsigned char *bytes);
static int party_body_decode(struct message *message, const unsigned char *bytes, int offset, int body_end);
static struct party_member *party_member_by_key(const unsigned char *key);
static int party_member_keys(struct party_member *member);
static void party_pair_heard(struct friend_record *record, struct party_member *member, const struct message *message,
	const unsigned char *plain, int size);
static void party_update(void);
static void party_leave_now(int tell, const char *why);
static void party_key_blocked(const unsigned char *key);
static void party_friend_entry(const struct friend_record *record, struct p2p_friend_entry *entry);

/* ---------- messages */

/* the pair's keys with a friend: 0 if their key gives a known secret */
static int pair_keys(const unsigned char *key, unsigned char *send_key, unsigned char *receive_key)
{
	static const unsigned char zero[P2P_KEY_SIZE];
	static const char label[] = "hcev-friends-1 pair";
	unsigned char x25519[P2P_KEY_SIZE], shared[P2P_KEY_SIZE], pair[P2P_SHA256_SIZE];
	unsigned char data[sizeof(label) - 1 + 2 * P2P_KEY_SIZE], direction[3 + 2 * P2P_KEY_SIZE];
	int first = memcmp(friends->key, key, P2P_KEY_SIZE) < 0;

	if (!p2p_ed25519_to_x25519(key, x25519))
		return 0;
	p2p_x25519(shared, friends->x25519_secret, x25519);
	if (p2p_equal(shared, zero, P2P_KEY_SIZE))
		return 0;
	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, first ? friends->key : key, P2P_KEY_SIZE);
	memcpy(data + sizeof(label) - 1 + P2P_KEY_SIZE, first ? key : friends->key, P2P_KEY_SIZE);
	p2p_hmac_sha256(shared, P2P_KEY_SIZE, data, (int)sizeof(data), pair);
	memcpy(direction, "dir", 3);
	memcpy(direction + 3, friends->key, P2P_KEY_SIZE);
	memcpy(direction + 3 + P2P_KEY_SIZE, key, P2P_KEY_SIZE);
	p2p_hmac_sha256(pair, P2P_SHA256_SIZE, direction, (int)sizeof(direction), send_key);
	memcpy(direction + 3, key, P2P_KEY_SIZE);
	memcpy(direction + 3 + P2P_KEY_SIZE, friends->key, P2P_KEY_SIZE);
	p2p_hmac_sha256(pair, P2P_SHA256_SIZE, direction, (int)sizeof(direction), receive_key);
	memset(shared, 0, sizeof(shared));
	memset(pair, 0, sizeof(pair));
	return 1;
}

static int friend_keys(struct friend_record *record)
{
	if (!record->keys_state)
		record->keys_state = pair_keys(record->key, record->send_key, record->receive_key) ? 1 : -1;
	return record->keys_state > 0;
}

/* a box's key: of the ephemeral key's secret with the recipient's X25519
key (sending), or of this machine's secret with the ephemeral key
(opening); bound to both keys */
static int box_key(const unsigned char *shared, const unsigned char *ephemeral, const unsigned char *recipient,
	unsigned char *key)
{
	static const unsigned char zero[P2P_KEY_SIZE];
	static const char label[] = "hcev-friends-1 box";
	unsigned char data[sizeof(label) - 1 + 2 * P2P_KEY_SIZE];

	if (p2p_equal(shared, zero, P2P_KEY_SIZE))
		return 0;
	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, ephemeral, P2P_KEY_SIZE);
	memcpy(data + sizeof(label) - 1 + P2P_KEY_SIZE, recipient, P2P_KEY_SIZE);
	p2p_hmac_sha256(shared, P2P_KEY_SIZE, data, (int)sizeof(data), key);
	return 1;
}

/* the body of a message; its size */
/* a presence's, an invite's and a party follow's game flags byte: 1 its
wire (the crossplay wire), 2 a Halo PC map, 4 a PC's game (a crossplay
game's); and, since Oct 20 2026 (older builds read none of them: game_decode
then took bits 1, 2 and 4 alone), 8 the sender is this project's PC build,
16 the game's host is, 32 the game takes Vitas only (network_wire.h). What
the sender says: best effort */
static unsigned char game_flags(const struct presence *game)
{
	return (unsigned char)((game->wire ? 1 : 0) | (game->pc_map ? 2 : 0) | (game->wire && game->pc_host ? 4 : 0) |
		(game->sender_pc ? 8 : 0) | (game->host_pc_build ? 16 : 0) | (!game->wire && game->vitas_only ? 32 : 0));
}

static int body_encode(const struct message *message, unsigned char *bytes)
{
	int size = 0;

	switch (message->type)
	{
	case TYPE_REQUEST:
		size += put_text(bytes + size, message->name, NAME_LENGTH);
		memcpy(bytes + size, message->nonce, NONCE_LENGTH);
		size += NONCE_LENGTH;
		break;
	case TYPE_ACCEPT:
		size += put_text(bytes + size, message->name, NAME_LENGTH);
		memcpy(bytes + size, message->nonce, NONCE_LENGTH);
		size += NONCE_LENGTH;
		memcpy(bytes + size, message->friendship, FRIENDSHIP_LENGTH);
		size += FRIENDSHIP_LENGTH;
		break;
	case TYPE_PRESENCE:
		bytes[size++] = (unsigned char)message->flags;
		bytes[size++] = (unsigned char)message->game.state;
		bytes[size++] = (unsigned char)message->game.engine;
		bytes[size++] = (unsigned char)(message->game.difficulty < 0 ? 0xFF : message->game.difficulty);
		bytes[size++] = (unsigned char)message->game.players;
		bytes[size++] = (unsigned char)message->game.maximum;
		put_u16(bytes + size, (unsigned int)message->game.version);
		size += 2;
		bytes[size++] = game_flags(&message->game);
		size += put_text(bytes + size, message->name, NAME_LENGTH);
		size += put_text(bytes + size, message->game.map, MAP_LENGTH);
		size += put_text(bytes + size, message->game.gametype, GAMETYPE_LENGTH);
		if (message->flags & PRESENCE_JOINABLE)
		{
			memcpy(bytes + size, message->game.invite_hash, P2P_KEY_HASH_SIZE);
			size += P2P_KEY_HASH_SIZE;
			if (message->flags & PRESENCE_LOCKED)
			{
				memcpy(bytes + size, message->game.signing_key, P2P_KEY_SIZE);
				size += P2P_KEY_SIZE;
				memcpy(bytes + size, message->game.sealed_token, P2P_SEALED_TOKEN_SIZE);
				size += P2P_SEALED_TOKEN_SIZE;
			}
			else
			{
				memcpy(bytes + size, message->game.invite_token, P2P_TOKEN_SIZE);
				size += P2P_TOKEN_SIZE;
			}
		}
		break;
	case TYPE_INVITE:
		memcpy(bytes + size, message->friendship, FRIENDSHIP_LENGTH);
		size += FRIENDSHIP_LENGTH;
		memcpy(bytes + size, message->invite_id, INVITE_ID_LENGTH);
		size += INVITE_ID_LENGTH;
		bytes[size++] = (unsigned char)message->game.state;
		bytes[size++] = (unsigned char)message->game.engine;
		bytes[size++] = (unsigned char)(message->game.difficulty < 0 ? 0xFF : message->game.difficulty);
		put_u16(bytes + size, (unsigned int)message->game.version);
		size += 2;
		bytes[size++] = game_flags(&message->game);
		size += put_text(bytes + size, message->game.map, MAP_LENGTH);
		size += put_text(bytes + size, message->game.gametype, GAMETYPE_LENGTH);
		memcpy(bytes + size, message->game.invite_hash, P2P_KEY_HASH_SIZE);
		size += P2P_KEY_HASH_SIZE;
		memcpy(bytes + size, message->game.invite_token, P2P_TOKEN_SIZE);
		size += P2P_TOKEN_SIZE;
		break;
	case TYPE_REMOVE:
		memcpy(bytes + size, message->friendship, FRIENDSHIP_LENGTH);
		size += FRIENDSHIP_LENGTH;
		break;
	default:
		size = party_body_encode(message, bytes);
		break;
	}
	return size;
}

/* a message's plaintext, signed by this machine's identity; its size */
static int plain_encode(struct message *message, unsigned char *bytes)
{
	int size = 0;
	unsigned char *signed_data;
	int signed_size;

	bytes[size++] = (unsigned char)message->type;
	memcpy(bytes + size, friends->key, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	memcpy(bytes + size, message->recipient, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	put_u64(bytes + size, message->epoch);
	size += 8;
	put_u32(bytes + size, message->counter);
	size += 4;
	size += body_encode(message, bytes + size);
	/* (the label, then everything before the signature) */
	signed_size = (int)sizeof(signature_label) - 1 + size;
	signed_data = malloc((size_t)signed_size);
	if (!signed_data)
		return 0;
	memcpy(signed_data, signature_label, sizeof(signature_label) - 1);
	memcpy(signed_data + sizeof(signature_label) - 1, bytes, (size_t)size);
	p2p_ed25519_sign(friends->seed, friends->key, signed_data, signed_size, bytes + size);
	free(signed_data);
	return size + P2P_SIGNATURE_SIZE;
}

/* a presence's or an invite's game fields, read and checked (the flags
byte: game_flags') */
static int game_decode(const unsigned char *bytes, int size, int *offset, struct presence *game, int with_counts)
{
	int at = *offset;

	if (at + (with_counts ? 6 : 3) + 3 > size)
		return 0;
	game->state = bytes[at++];
	game->engine = bytes[at++];
	game->difficulty = bytes[at] == 0xFF ? -1 : bytes[at];
	at++;
	if (with_counts)
	{
		game->players = bytes[at++];
		game->maximum = bytes[at++];
	}
	game->version = (int)get_u16(bytes + at);
	at += 2;
	game->wire = bytes[at] & 1;
	game->pc_map = (bytes[at] & 2) != 0;
	/* (since Oct 19 2026: a PC's game; builds before it read nothing of the
	bit) */
	game->pc_host = game->wire && (bytes[at] & 4) != 0;
	/* (since Oct 20 2026: this project's marks; builds before them read
	nothing of these bits) */
	game->sender_pc = (bytes[at] & 8) != 0;
	game->host_pc_build = (bytes[at] & 16) != 0;
	game->vitas_only = !game->wire && (bytes[at] & 32) != 0;
	at++;
	if (game->state >= P2P_FRIEND_STATE_COUNT || (game->difficulty > 3) || game->engine > 15 ||
		game->players > HALO_PORT_MAXIMUM_NETWORK_PLAYERS || game->maximum > HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
		game->players > game->maximum)
	{
		return 0;
	}
	*offset = at;
	return 1;
}

/* a message's plaintext read (not its signature: plain_verify): 0 if it is
not one, whole, within every bound */
static int plain_decode(const unsigned char *bytes, int size, struct message *message)
{
	int offset = PLAIN_HEADER_SIZE, body_end = size - P2P_SIGNATURE_SIZE;

	memset(message, 0, sizeof(*message));
	if (size < PLAIN_HEADER_SIZE + P2P_SIGNATURE_SIZE || size > MAXIMUM_PLAIN_SIZE)
		return 0;
	message->type = bytes[0];
	memcpy(message->sender, bytes + 1, P2P_KEY_SIZE);
	memcpy(message->recipient, bytes + 1 + P2P_KEY_SIZE, P2P_KEY_SIZE);
	message->epoch = get_u64(bytes + 1 + 2 * P2P_KEY_SIZE);
	message->counter = get_u32(bytes + 1 + 2 * P2P_KEY_SIZE + 8);
	message->game.difficulty = -1;
	switch (message->type)
	{
	case TYPE_REQUEST:
		if (!get_text(bytes, body_end, &offset, message->name, NAME_LENGTH, 1) || offset + NONCE_LENGTH != body_end)
			return 0;
		memcpy(message->nonce, bytes + offset, NONCE_LENGTH);
		break;
	case TYPE_ACCEPT:
		if (!get_text(bytes, body_end, &offset, message->name, NAME_LENGTH, 1) ||
			offset + NONCE_LENGTH + FRIENDSHIP_LENGTH != body_end)
		{
			return 0;
		}
		memcpy(message->nonce, bytes + offset, NONCE_LENGTH);
		memcpy(message->friendship, bytes + offset + NONCE_LENGTH, FRIENDSHIP_LENGTH);
		break;
	case TYPE_PRESENCE:
		if (offset >= body_end)
			return 0;
		message->flags = bytes[offset++];
		if ((message->flags & ~(PRESENCE_ONLINE | PRESENCE_WANT_REPLY | PRESENCE_JOINABLE | PRESENCE_LOCKED)) ||
			((message->flags & PRESENCE_LOCKED) && !(message->flags & PRESENCE_JOINABLE)))
		{
			return 0;
		}
		if (!game_decode(bytes, body_end, &offset, &message->game, 1) ||
			!get_text(bytes, body_end, &offset, message->name, NAME_LENGTH, 1) ||
			!get_text(bytes, body_end, &offset, message->game.map, MAP_LENGTH, 0) ||
			!get_text(bytes, body_end, &offset, message->game.gametype, GAMETYPE_LENGTH, 0))
		{
			return 0;
		}
		message->game.online = (message->flags & PRESENCE_ONLINE) != 0;
		message->game.joinable = (message->flags & PRESENCE_JOINABLE) != 0;
		message->game.locked = (message->flags & PRESENCE_LOCKED) != 0;
		if (message->game.joinable && message->game.locked)
		{
			if (offset + P2P_KEY_HASH_SIZE + P2P_KEY_SIZE + P2P_SEALED_TOKEN_SIZE != body_end || !message->game.online)
				return 0;
			memcpy(message->game.invite_hash, bytes + offset, P2P_KEY_HASH_SIZE);
			memcpy(message->game.signing_key, bytes + offset + P2P_KEY_HASH_SIZE, P2P_KEY_SIZE);
			memcpy(message->game.sealed_token, bytes + offset + P2P_KEY_HASH_SIZE + P2P_KEY_SIZE, P2P_SEALED_TOKEN_SIZE);
		}
		else if (message->game.joinable)
		{
			if (offset + P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE != body_end || !message->game.online)
				return 0;
			memcpy(message->game.invite_hash, bytes + offset, P2P_KEY_HASH_SIZE);
			memcpy(message->game.invite_token, bytes + offset + P2P_KEY_HASH_SIZE, P2P_TOKEN_SIZE);
		}
		else if (offset != body_end)
			return 0;
		/* (presence goes to every friend: no recipient) */
		if (!key_zero(message->recipient))
			return 0;
		break;
	case TYPE_INVITE:
		if (offset + FRIENDSHIP_LENGTH + INVITE_ID_LENGTH > body_end)
			return 0;
		memcpy(message->friendship, bytes + offset, FRIENDSHIP_LENGTH);
		offset += FRIENDSHIP_LENGTH;
		memcpy(message->invite_id, bytes + offset, INVITE_ID_LENGTH);
		offset += INVITE_ID_LENGTH;
		if (!game_decode(bytes, body_end, &offset, &message->game, 0) ||
			!get_text(bytes, body_end, &offset, message->game.map, MAP_LENGTH, 0) ||
			!get_text(bytes, body_end, &offset, message->game.gametype, GAMETYPE_LENGTH, 0) ||
			offset + P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE != body_end)
		{
			return 0;
		}
		memcpy(message->game.invite_hash, bytes + offset, P2P_KEY_HASH_SIZE);
		memcpy(message->game.invite_token, bytes + offset + P2P_KEY_HASH_SIZE, P2P_TOKEN_SIZE);
		message->game.online = 1;
		break;
	case TYPE_REMOVE:
		if (offset + FRIENDSHIP_LENGTH != body_end)
			return 0;
		memcpy(message->friendship, bytes + offset, FRIENDSHIP_LENGTH);
		break;
	case TYPE_PARTY_INVITE:
	case TYPE_PARTY_ANSWER:
	case TYPE_PARTY_ROSTER:
	case TYPE_PARTY_LEAVE:
	case TYPE_PARTY_STATUS:
	case TYPE_PARTY_FOLLOW:
	case TYPE_PARTY_CHAT:
	case TYPE_PARTY_PRESENCE:
		return party_body_decode(message, bytes, offset, body_end);
	default:
		return 0;
	}
	return 1;
}

/* whether the plaintext's signature is its sender's */
static int plain_verify(const unsigned char *bytes, int size)
{
	unsigned char *signed_data;
	int signed_size = (int)sizeof(signature_label) - 1 + size - P2P_SIGNATURE_SIZE, result;

	if (size < PLAIN_HEADER_SIZE + P2P_SIGNATURE_SIZE)
		return 0;
	signed_data = malloc((size_t)signed_size);
	if (!signed_data)
		return 0;
	memcpy(signed_data, signature_label, sizeof(signature_label) - 1);
	memcpy(signed_data + sizeof(signature_label) - 1, bytes, (size_t)(size - P2P_SIGNATURE_SIZE));
	result = p2p_ed25519_verify(bytes + 1, signed_data, signed_size, bytes + size - P2P_SIGNATURE_SIZE);
	free(signed_data);
	return result;
}

/* an envelope sealed with a key (kind R or P); its size */
static int envelope_seal(int kind, const unsigned char *key, const unsigned char *plain, int size,
	unsigned char *payload)
{
	unsigned char nonce[P2P_NONCE_SIZE];

	payload[0] = 'F';
	payload[1] = FRIENDS_VERSION;
	payload[2] = (unsigned char)kind;
	posix_random_bytes(nonce, sizeof(nonce));
	memcpy(payload + ENVELOPE_HEADER_SIZE, nonce, P2P_NONCE_SIZE);
	return ENVELOPE_HEADER_SIZE + P2P_NONCE_SIZE + p2p_aead_seal(key, nonce, payload, ENVELOPE_HEADER_SIZE, plain, size,
		payload + ENVELOPE_HEADER_SIZE + P2P_NONCE_SIZE);
}

/* the reverse: the plaintext's size, or -1 */
static int envelope_open(int kind, const unsigned char *key, const unsigned char *payload, int size,
	unsigned char *plain)
{
	if (size < ENVELOPE_HEADER_SIZE + P2P_NONCE_SIZE + P2P_TAG_SIZE || size > MAXIMUM_PAYLOAD_SIZE ||
		payload[0] != 'F' || payload[1] != FRIENDS_VERSION || payload[2] != kind)
	{
		return -1;
	}
	return p2p_aead_open(key, payload + ENVELOPE_HEADER_SIZE, payload, ENVELOPE_HEADER_SIZE,
		payload + ENVELOPE_HEADER_SIZE + P2P_NONCE_SIZE, size - ENVELOPE_HEADER_SIZE - P2P_NONCE_SIZE, plain);
}

/* a box to the owner of an Ed25519 key; its size, 0 if the key has no
X25519 form */
static int box_seal(const unsigned char *recipient, const unsigned char *plain, int size, unsigned char *payload)
{
	unsigned char secret[P2P_KEY_SIZE], ephemeral[P2P_KEY_SIZE], x25519[P2P_KEY_SIZE], shared[P2P_KEY_SIZE];
	unsigned char key[P2P_SHA256_SIZE], nonce[P2P_NONCE_SIZE];
	int sealed;

	if (!p2p_ed25519_to_x25519(recipient, x25519))
		return 0;
	posix_random_bytes(secret, sizeof(secret));
	p2p_x25519(ephemeral, secret, NULL);
	p2p_x25519(shared, secret, x25519);
	memset(secret, 0, sizeof(secret));
	if (!box_key(shared, ephemeral, recipient, key))
		return 0;
	payload[0] = 'F';
	payload[1] = FRIENDS_VERSION;
	payload[2] = KIND_BOX;
	memcpy(payload + ENVELOPE_HEADER_SIZE, ephemeral, P2P_KEY_SIZE);
	posix_random_bytes(nonce, sizeof(nonce));
	memcpy(payload + ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE, nonce, P2P_NONCE_SIZE);
	sealed = p2p_aead_seal(key, nonce, payload, ENVELOPE_HEADER_SIZE, plain, size,
		payload + ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE + P2P_NONCE_SIZE);
	return ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE + P2P_NONCE_SIZE + sealed;
}

static int box_open(const unsigned char *payload, int size, unsigned char *plain)
{
	unsigned char shared[P2P_KEY_SIZE], key[P2P_SHA256_SIZE];
	int opened;

	if (size < ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE + P2P_NONCE_SIZE + P2P_TAG_SIZE || size > MAXIMUM_PAYLOAD_SIZE ||
		payload[0] != 'F' || payload[1] != FRIENDS_VERSION || payload[2] != KIND_BOX)
	{
		return -1;
	}
	p2p_x25519(shared, friends->x25519_secret, payload + ENVELOPE_HEADER_SIZE);
	if (!box_key(shared, payload + ENVELOPE_HEADER_SIZE, friends->key, key))
		return -1;
	opened = p2p_aead_open(key, payload + ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE, payload, ENVELOPE_HEADER_SIZE,
		payload + ENVELOPE_HEADER_SIZE + P2P_KEY_SIZE + P2P_NONCE_SIZE,
		size - ENVELOPE_HEADER_SIZE - P2P_KEY_SIZE - P2P_NONCE_SIZE, plain);
	memset(shared, 0, sizeof(shared));
	memset(key, 0, sizeof(key));
	return opened;
}

/* ---------- the outgoing queue (p2p_signal.c publishes it) */

static struct outbound *outbound_add(const char *topic, const unsigned char *payload, int size, int presence,
	const unsigned char *recipient)
{
	struct outbound *entry;
	int index;

	if (size <= 0 || size > MAXIMUM_PAYLOAD_SIZE)
		return NULL;
	/* (a newer presence to a friend replaces one not sent yet) */
	if (presence)
	{
		for (index = 0; index < friends->outbound_count; index++)
		{
			entry = &friends->outbound[index];
			if (entry->presence && !entry->sent_mask && p2p_equal(entry->recipient, recipient, P2P_KEY_SIZE))
			{
				memcpy(entry->payload, payload, (size_t)size);
				entry->size = size;
				entry->time = now_stamp();
				entry->serial = ++friends->outbound_serial;
				return entry;
			}
		}
		if (friends->outbound_count >= MAXIMUM_OUTBOUND - OUTBOUND_RESERVE)
			return NULL;
	}
	if (friends->outbound_count >= MAXIMUM_OUTBOUND)
	{
		friends->dropped_rate++;
		return NULL;
	}
	entry = &friends->outbound[friends->outbound_count++];
	memset(entry, 0, sizeof(*entry));
	entry->serial = ++friends->outbound_serial;
	snprintf(entry->topic, sizeof(entry->topic), "%s", topic);
	memcpy(entry->payload, payload, (size_t)size);
	entry->size = size;
	entry->time = now_stamp();
	entry->presence = presence;
	if (recipient)
		memcpy(entry->recipient, recipient, P2P_KEY_SIZE);
#ifndef HALO_RELEASE
	/* (the tests' replays: the last message sent, as sent) */
	memcpy(friends->test_last_sent, payload, (size_t)size);
	friends->test_last_sent_size = size;
	snprintf(friends->test_last_topic, sizeof(friends->test_last_topic), "%s", topic);
#endif
	return entry;
}

/* the next message of this run (its counter) */
static void message_start(struct message *message, int type, const unsigned char *recipient)
{
	memset(message, 0, sizeof(*message));
	message->type = type;
	if (recipient)
		memcpy(message->recipient, recipient, P2P_KEY_SIZE);
	message->epoch = friends->epoch;
	message->counter = ++friends->counter;
}

/* the player's name as friends see it: the profile's (the game's
network name), else the Vita's */
static void own_name(char *name)
{
	const char *lobby = config_string("network.lobby_name");
	const char *chosen = friends->name[0] ? friends->name : lobby && lobby[0] ? lobby : "Player";

	if (!clean_text(name, NAME_LENGTH, (const unsigned char *)chosen, (int)strlen(chosen), 1))
		snprintf(name, P2P_FRIEND_NAME_SIZE, "Player");
}

/* a pair message to a friend */
static int send_pair(struct friend_record *record, struct message *message)
{
	unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	int size;

	if (!friend_keys(record))
		return 0;
	size = plain_encode(message, plain);
	if (!size)
		return 0;
	size = envelope_seal(KIND_PAIR, record->send_key, plain, size, payload);
	mailbox_topic(record->key, topic);
	return outbound_add(topic, payload, size, 0, record->key) != NULL;
}

/* a REMOVE to a key (a friend's, or a tombstone's), of that friendship */
static void send_remove(const unsigned char *key, const unsigned char *friendship)
{
	struct friend_record record;
	struct message message;

	memset(&record, 0, sizeof(record));
	memcpy(record.key, key, P2P_KEY_SIZE);
	message_start(&message, TYPE_REMOVE, key);
	memcpy(message.friendship, friendship, FRIENDSHIP_LENGTH);
	send_pair(&record, &message);
}

static void send_request(struct outgoing_record *record)
{
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE], seal_key[P2P_SHA256_SIZE];
	unsigned char recipient[P2P_SHA256_SIZE];
	char topic[TOPIC_LENGTH];
	int size;

	code_derive(record->code, topic, seal_key, recipient);
	message_start(&message, TYPE_REQUEST, recipient);
	own_name(message.name);
	memcpy(message.nonce, record->nonce, NONCE_LENGTH);
	size = plain_encode(&message, plain);
	if (!size)
		return;
	size = envelope_seal(KIND_REQUEST, seal_key, plain, size, payload);
	if (outbound_add(topic, payload, size, 0, NULL))
	{
		record->sent_time = now_stamp();
		if (!record->first_time)
			record->first_time = record->sent_time;
	}
}

static void send_accept(const unsigned char *key, const unsigned char *nonce, const unsigned char *friendship)
{
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	int size;

	message_start(&message, TYPE_ACCEPT, key);
	own_name(message.name);
	memcpy(message.nonce, nonce, NONCE_LENGTH);
	memcpy(message.friendship, friendship, FRIENDSHIP_LENGTH);
	size = plain_encode(&message, plain);
	if (!size)
		return;
	size = box_seal(key, plain, size, payload);
	mailbox_topic(key, topic);
	if (size)
		outbound_add(topic, payload, size, 0, key);
}

/* ---------- presence */

/* this machine's marks on a message's game (network_wire.h): it is the PC
build; the game it hosts is the PC build's, or for Vitas only */
static void own_platform_marks(struct presence *game)
{
	game->sender_pc = network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC;
	game->host_pc_build = friends->host.host_pc_build;
	game->vitas_only = !game->wire && friends->host.vitas_only;
}

/* whether this machine's game can be joined by a friend from presence: 0
no, 1 yes, 2 with its password. The design's rule: hosted for the internet,
public or private (the owner, Oct 19 2026: "when I have a private lobby my
friend should be able to join without a join code" - the presence, sealed
for each friend alone, carries what joins it, never a listing or a
stranger), open with room, Friends can join, not appearing offline; a game
with a password once its sealed token is ready (the friend types the
password); on either wire, which the presence says */
static int own_joinable(void)
{
	const struct p2p_friends_host *host = &friends->host;

	if (!friends->online || !host->hosting || !host->open || host->players >= host->maximum ||
		!setting_on("friends.join", "HALO_FRIENDS_JOIN") || friends->appear_offline)
	{
		return 0;
	}
	if (host->locked)
		return host->has_sealed ? 2 : 0;
	return 1;
}

/* the wire a game a friend's presence, invite or party names is joined on
(network_wire.h): 0 the legacy wire, 1 the crossplay wire, -1 not here.
This build's own games say the version of their wire: 18 (the legacy wire;
a builds' before the crossplay hosts' with the wire bit, the crossplay wire)
or the crossplay wire's (a Vita's game for Vitas and PC, so that a PC
friend (OpenCE's friends) sees a version it plays); a PC friend's game says
its build's version, joined on the crossplay wire only when it is one that
wire joins (the version list, network_wire_version_accepted_on), as any PC
host's is; the crossplay wire only where this build speaks it */
static int game_wire_of(int version, int wire)
{
	if (version == HALO_PORT_NETWORK_VERSION)
		return !wire ? 0 : p2p_wire_count() > 1 ? 1 : -1;
	return p2p_wire_count() > 1 && network_wire_version_accepted_on(1, (unsigned int)version) ? 1 : -1;
}

static int game_wire_joinable(int version, int wire)
{
	return game_wire_of(version, wire) >= 0;
}

/* whether a friend can be invited now: hosting for the internet, room; a
game with a password too (the owner, Oct 19 2026: an invite admits, as the
code does - the host chose them) */
static int own_invitable(void)
{
	const struct p2p_friends_host *host = &friends->host;

	return friends->online && host->hosting && host->players < host->maximum;
}

/* this machine's presence signed anew (a new counter): online or not */
static int presence_sign(int online, int want_reply)
{
	struct message message;
	int joinable = 0;

	message_start(&message, TYPE_PRESENCE, NULL);
	own_name(message.name);
	message.flags = (online ? PRESENCE_ONLINE : 0) | (want_reply ? PRESENCE_WANT_REPLY : 0);
	message.game.difficulty = -1;
	if (online)
	{
		const struct activity *activity = &friends->activity;

		message.game.state = activity->state ? activity->state : P2P_FRIEND_STATE_MENUS;
		message.game.engine = activity->engine & 15;
		message.game.difficulty = activity->difficulty;
		message.game.pc_map = activity->pc_map;
		message.game.players = activity->players;
		message.game.maximum = activity->maximum;
		if (message.game.players > message.game.maximum)
			message.game.players = message.game.maximum;
		snprintf(message.game.map, sizeof(message.game.map), "%s", activity->map);
		snprintf(message.game.gametype, sizeof(message.game.gametype), "%s", activity->gametype);
		/* (the version of its wire: a PC friend's build compares its own) */
		message.game.version = (int)network_wire_version_of(friends->host.crossplay);
		message.game.wire = friends->host.crossplay;
		message.game.pc_host = friends->host.pc_host;
		own_platform_marks(&message.game);
		joinable = own_joinable();
		if (joinable)
		{
			message.flags |= PRESENCE_JOINABLE | (joinable == 2 ? PRESENCE_LOCKED : 0);
			memcpy(message.game.invite_hash, friends->host.key_hash, P2P_KEY_HASH_SIZE);
			if (joinable == 2)
			{
				message.game.locked = 1;
				memcpy(message.game.signing_key, friends->host.signing_key, P2P_KEY_SIZE);
				memcpy(message.game.sealed_token, friends->host.sealed_token, P2P_SEALED_TOKEN_SIZE);
			}
			else
				memcpy(message.game.invite_token, friends->host.token, P2P_TOKEN_SIZE);
			message.game.players = friends->host.players;
			message.game.maximum = friends->host.maximum;
		}
	}
	friends->presence_size = plain_encode(&message, friends->presence_plain);
	friends->presence_counter = message.counter;
	friends->presence_time = now_stamp();
	friends->presence_online = online;
	friends->presence_joinable = joinable;
	memcpy(friends->presence_token, friends->host.token, P2P_TOKEN_SIZE);
	friends->presence_want_reply = want_reply;
	return friends->presence_size > 0;
}

/* the presence signed last, sealed for a friend and queued */
static int presence_send(struct friend_record *record)
{
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	int size;

	if (!friends->presence_size || !friend_keys(record))
		return 0;
	size = envelope_seal(KIND_PAIR, record->send_key, friends->presence_plain, friends->presence_size, payload);
	mailbox_topic(record->key, topic);
	if (!outbound_add(topic, payload, size, 1, record->key))
		return 0;
	record->sent_counter = friends->presence_counter;
	record->presence_due = 0;
	record->reply_asked = 0;
	return 1;
}

static void presence_due_all(void)
{
	int index;

	for (index = 0; index < friends->friend_count; index++)
		friends->friends[index].presence_due = 1;
}

/* ---------- the lists */

static void name_or_code(const unsigned char *key, const char *name, char *text, int size)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1], shown[P2P_FRIEND_CODE_SIZE];

	code_of_key(key, code);
	code_shown(code, shown, sizeof(shown));
	snprintf(text, (size_t)size, "%s (%s)", name[0] ? name : "?", shown);
}

static void request_remove(int index)
{
	memmove(&friends->requests[index], &friends->requests[index + 1],
		sizeof(friends->requests[0]) * (size_t)(friends->request_count - index - 1));
	friends->request_count--;
}

static void outgoing_remove(int index)
{
	memmove(&friends->outgoing[index], &friends->outgoing[index + 1],
		sizeof(friends->outgoing[0]) * (size_t)(friends->outgoing_count - index - 1));
	friends->outgoing_count--;
}

static void invite_remove(int index)
{
	memmove(&friends->invites[index], &friends->invites[index + 1],
		sizeof(friends->invites[0]) * (size_t)(friends->invite_count - index - 1));
	friends->invite_count--;
}

static void invites_of_remove(const unsigned char *key)
{
	int index;

	for (index = friends->invite_count - 1; index >= 0; index--)
		if (p2p_equal(friends->invites[index].key, key, P2P_KEY_SIZE))
			invite_remove(index);
}

static void friend_remove_index(int index)
{
	invites_of_remove(friends->friends[index].key);
	memmove(&friends->friends[index], &friends->friends[index + 1],
		sizeof(friends->friends[0]) * (size_t)(friends->friend_count - index - 1));
	friends->friend_count--;
}

static void tombstone_add(const unsigned char *key, const unsigned char *friendship)
{
	int index = find_tombstone(key);

	if (index < 0)
	{
		/* (the oldest goes, when full) */
		if (friends->tombstone_count >= MAXIMUM_TOMBSTONES)
		{
			memmove(&friends->tombstones[0], &friends->tombstones[1],
				sizeof(friends->tombstones[0]) * (MAXIMUM_TOMBSTONES - 1));
			friends->tombstone_count--;
		}
		index = friends->tombstone_count++;
	}
	memset(&friends->tombstones[index], 0, sizeof(friends->tombstones[index]));
	memcpy(friends->tombstones[index].key, key, P2P_KEY_SIZE);
	memcpy(friends->tombstones[index].friendship, friendship, FRIENDSHIP_LENGTH);
	friends->tombstones[index].time = unix_now();
}

static void tombstone_remove(int index)
{
	memmove(&friends->tombstones[index], &friends->tombstones[index + 1],
		sizeof(friends->tombstones[0]) * (size_t)(friends->tombstone_count - index - 1));
	friends->tombstone_count--;
}

static void muted_add(const unsigned char *key)
{
	int index = find_muted(key);

	if (index < 0)
	{
		if (friends->muted_count >= MAXIMUM_MUTED)
		{
			memmove(&friends->muted[0], &friends->muted[1], sizeof(friends->muted[0]) * (MAXIMUM_MUTED - 1));
			friends->muted_count--;
		}
		index = friends->muted_count++;
	}
	memcpy(friends->muted[index].key, key, P2P_KEY_SIZE);
	friends->muted[index].time = unix_now();
}

/* a new friend (from a request accepted here, or an accept heard) */
static struct friend_record *friend_add(const unsigned char *key, const char *name, const unsigned char *friendship,
	unsigned long long epoch, unsigned long counter)
{
	struct friend_record *record;
	int index;

	if (friends->friend_count >= P2P_FRIENDS_MAXIMUM)
		return NULL;
	index = find_tombstone(key);
	if (index >= 0)
		tombstone_remove(index);
	index = find_muted(key);
	if (index >= 0)
	{
		memmove(&friends->muted[index], &friends->muted[index + 1],
			sizeof(friends->muted[0]) * (size_t)(friends->muted_count - index - 1));
		friends->muted_count--;
	}
	record = &friends->friends[friends->friend_count++];
	memset(record, 0, sizeof(*record));
	memcpy(record->key, key, P2P_KEY_SIZE);
	snprintf(record->name, sizeof(record->name), "%s", name);
	memcpy(record->friendship, friendship, FRIENDSHIP_LENGTH);
	record->since = unix_now();
	record->epoch = epoch;
	record->counter = counter;
	record->window = 1;
	record->presence_due = 1;
	record->reply_asked = 1;
	friends->dirty = 1;
	return record;
}

/* ---------- what was heard */

/* a friend's message's epoch and counter, against what was seen: 1 new
(taken into the window), 0 a replay */
static int replay_check(struct friend_record *record, unsigned long long epoch, unsigned long counter)
{
	if (epoch < record->epoch)
		return 0;
	if (epoch > record->epoch)
	{
		record->epoch = epoch;
		record->counter = counter;
		record->window = 1;
		/* (an epoch is kept: once a run of theirs) */
		friends->dirty = 1;
		return 1;
	}
	if (counter > record->counter)
	{
		unsigned long shift = counter - record->counter;

		record->window = shift >= REPLAY_WINDOW ? 1 : record->window << shift | 1;
		record->counter = counter;
		friends->counters_dirty = 1;
		return 1;
	}
	if (record->counter - counter >= REPLAY_WINDOW || record->window >> (record->counter - counter) & 1)
		return 0;
	record->window |= 1ULL << (record->counter - counter);
	return 1;
}

/* a request's epoch and counter from a key not a friend's: 0 if seen */
static int seen_check(const unsigned char *key, unsigned long long epoch, unsigned long counter)
{
	int index;

	for (index = 0; index < MAXIMUM_SEEN_REQUESTS; index++)
	{
		struct seen_request *seen = &friends->seen[index];

		if (p2p_equal(seen->key, key, P2P_KEY_SIZE))
		{
			if (epoch < seen->epoch || (epoch == seen->epoch && counter <= seen->counter))
				return 0;
			seen->epoch = epoch;
			seen->counter = counter;
			return 1;
		}
	}
	memcpy(friends->seen[friends->seen_next].key, key, P2P_KEY_SIZE);
	friends->seen[friends->seen_next].epoch = epoch;
	friends->seen[friends->seen_next].counter = counter;
	friends->seen_next = (friends->seen_next + 1) % MAXIMUM_SEEN_REQUESTS;
	return 1;
}

static void request_heard(const struct message *message)
{
	struct friend_record *record;
	char text[64];
	int index;

	if (!p2p_equal(message->recipient, friends->code_recipient, P2P_SHA256_SIZE))
	{
		friends->dropped_forged++;
		return;
	}
	if (!key_usable(message->sender))
	{
		friends->dropped_malformed++;
		return;
	}
	if (find_blocked(message->sender) >= 0)
	{
		friends->dropped_blocked++;
		platform_log("friends: dropped a friend request from a blocked player");
		return;
	}
	if (!seen_check(message->sender, message->epoch, message->counter))
	{
		friends->dropped_replays++;
		return;
	}
	if (find_muted(message->sender) >= 0)
		return;
	name_or_code(message->sender, message->name, text, sizeof(text));
	/* (a friend who lost this machine: agreed to already, so answered) */
	record = find_friend(message->sender);
	if (record)
	{
		snprintf(record->name, sizeof(record->name), "%s", message->name);
		send_accept(record->key, message->nonce, record->friendship);
		record->presence_due = 1;
		platform_log("friends: %s asked again (their list lost this one?); accepted again", text);
		return;
	}
	/* (this machine asked them too: both said yes) */
	{
		char their[P2P_FRIEND_CODE_LENGTH + 1];
		int outgoing;

		code_of_key(message->sender, their);
		outgoing = find_outgoing(their);
		if (outgoing >= 0)
		{
			unsigned char friendship[FRIENDSHIP_LENGTH];

			posix_random_bytes(friendship, sizeof(friendship));
			record = friend_add(message->sender, message->name, friendship, message->epoch, message->counter);
			if (record)
			{
				outgoing_remove(outgoing);
				send_accept(record->key, message->nonce, record->friendship);
				platform_log("friends: %s asked too; now friends", text);
				return;
			}
		}
	}
	index = find_request(message->sender);
	if (index < 0)
	{
		if (friends->request_count >= P2P_FRIEND_REQUESTS_MAXIMUM)
		{
			friends->dropped_rate++;
			return;
		}
		index = friends->request_count++;
		platform_log("friends: a friend request from %s", text);
	}
	memcpy(friends->requests[index].key, message->sender, P2P_KEY_SIZE);
	snprintf(friends->requests[index].name, sizeof(friends->requests[index].name), "%s", message->name);
	memcpy(friends->requests[index].nonce, message->nonce, NONCE_LENGTH);
	friends->requests[index].time = unix_now();
	friends->dirty = 1;
}

static void accept_heard(const struct message *message)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1], text[64];
	struct friend_record *record;
	int index;

	if (!p2p_equal(message->recipient, friends->key, P2P_KEY_SIZE) || !key_usable(message->sender))
	{
		friends->dropped_forged++;
		return;
	}
	if (find_blocked(message->sender) >= 0)
	{
		friends->dropped_blocked++;
		return;
	}
	code_of_key(message->sender, code);
	index = find_outgoing(code);
	name_or_code(message->sender, message->name, text, sizeof(text));
	record = find_friend(message->sender);
	if (record)
	{
		/* (their answer again, through another broker or to a request
		repeated: the friendship stands; two friendships made at once - both
		asked - become the one of the lower id on both sides) */
		if (index >= 0 && p2p_equal(friends->outgoing[index].nonce, message->nonce, NONCE_LENGTH))
		{
			outgoing_remove(index);
			friends->dirty = 1;
		}
		if (memcmp(message->friendship, record->friendship, FRIENDSHIP_LENGTH) < 0)
		{
			memcpy(record->friendship, message->friendship, FRIENDSHIP_LENGTH);
			friends->dirty = 1;
		}
		return;
	}
	/* (only the code typed, whose hash the key is, and the nonce sent with
	it: a request still pending; once taken it is gone, so a replay finds
	nothing) */
	if (index < 0 || !p2p_equal(friends->outgoing[index].nonce, message->nonce, NONCE_LENGTH))
	{
		friends->dropped_replays++;
		return;
	}
	outgoing_remove(index);
	record = friend_add(message->sender, message->name, message->friendship, message->epoch, message->counter);
	if (!record)
	{
		platform_log("friends: %s accepted, but the friends list is full", text);
		return;
	}
	friends->dirty = 1;
	platform_log("friends: %s accepted the friend request; now friends", text);
}

static void presence_heard(struct friend_record *record, const struct message *message)
{
	char activity[P2P_FRIEND_ACTIVITY_SIZE];
	int was_online = record->heard_time && record->presence.online;

	/* (a friend's flood capped: their honest sends are paced far below it) */
	if (!budget_left(&record->presence_budget, PRESENCE_BUDGET, PRESENCE_BUDGET_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	record->presence_time = now_stamp();
	record->presence = message->game;
	record->heard_time = now_stamp();
	if (message->name[0] && strcmp(record->name, message->name))
	{
		snprintf(record->name, sizeof(record->name), "%s", message->name);
		friends->dirty = 1;
	}
	if ((message->flags & PRESENCE_WANT_REPLY) && !friends->appear_offline)
	{
		record->presence_due = 1;
		record->reply_asked = 1;
	}
	p2p_friends_activity_text(activity, sizeof(activity), message->game.online ? message->game.state :
		P2P_FRIEND_STATE_OFFLINE, message->game.map, message->game.gametype, message->game.engine, message->game.difficulty,
		message->game.pc_map);
	if (message->game.online)
		platform_log("friends: %s is online: %s%s", record->name, activity, message->game.joinable ? " (joinable)" : "");
	else if (was_online)
		platform_log("friends: %s went offline", record->name);
	else
		platform_log("friends: %s is offline", record->name);
}

static void invite_heard(struct friend_record *record, const struct message *message)
{
	struct invite_record *invite;
	char activity[P2P_FRIEND_ACTIVITY_SIZE];
	int index;

	if (!p2p_equal(message->friendship, record->friendship, FRIENDSHIP_LENGTH))
	{
		friends->dropped_forged++;
		return;
	}
	if (record->invite_heard_time && !elapsed(record->invite_heard_time, INVITE_INTERVAL))
	{
		friends->dropped_rate++;
		return;
	}
	record->invite_heard_time = now_stamp();
	for (index = 0; index < friends->invite_count; index++)
		if (p2p_equal(friends->invites[index].key, record->key, P2P_KEY_SIZE))
			break;
	if (index == friends->invite_count)
	{
		if (friends->invite_count >= P2P_FRIEND_INVITES_MAXIMUM)
			invite_remove(0);
		index = friends->invite_count++;
	}
	invite = &friends->invites[index];
	memset(invite, 0, sizeof(*invite));
	memcpy(invite->key, record->key, P2P_KEY_SIZE);
	memcpy(invite->invite_id, message->invite_id, INVITE_ID_LENGTH);
	invite->game = message->game;
	invite->time = now_stamp();
	p2p_friends_activity_text(activity, sizeof(activity), message->game.state, message->game.map,
		message->game.gametype, message->game.engine, message->game.difficulty, message->game.pc_map);
	platform_log("friends: %s invited this machine: %s", record->name, activity);
}

static void remove_heard(int index, const struct message *message)
{
	struct friend_record *record = &friends->friends[index];

	if (!p2p_equal(message->friendship, record->friendship, FRIENDSHIP_LENGTH))
	{
		friends->dropped_replays++;
		return;
	}
	platform_log("friends: %s removed this machine from their friends", record->name);
	friend_remove_index(index);
	friends->dirty = 1;
}

/* a pair message: tried with each friend's key (and each tombstone's,
whose sender is told REMOVE again) */
static void pair_heard(const unsigned char *payload, int size)
{
	unsigned char plain[MAXIMUM_PAYLOAD_SIZE];
	struct message message;
	int index, opened = -1, tombstone = -1;
	struct friend_record *record = NULL;
	struct party_member *member = NULL;

	/* (each tried with every key: a flood of messages none opens waits) */
	if (!budget_left(&friends->unopened_budget, UNOPENED_BUDGET, UNOPENED_BUDGET_INTERVAL, 0))
	{
		friends->dropped_rate++;
		return;
	}
	for (index = 0; index < friends->friend_count && opened < 0; index++)
	{
		if (!friend_keys(&friends->friends[index]))
			continue;
		opened = envelope_open(KIND_PAIR, friends->friends[index].receive_key, payload, size, plain);
		if (opened >= 0)
			record = &friends->friends[index];
	}
	/* (then the party's members who are not friends: p2p_party.inc) */
	for (index = 0; friends->party.active && index < friends->party.count && opened < 0; index++)
	{
		struct party_member *each = &friends->party.members[index];

		if (p2p_equal(each->key, friends->key, P2P_KEY_SIZE) || find_friend(each->key) || !party_member_keys(each))
			continue;
		opened = envelope_open(KIND_PAIR, each->receive_key, payload, size, plain);
		if (opened >= 0)
			member = each;
	}
	for (index = 0; index < friends->tombstone_count && opened < 0; index++)
	{
		struct tombstone_record *stone = &friends->tombstones[index];

		if (!stone->keys_state)
			stone->keys_state = pair_keys(stone->key, stone->send_key, stone->receive_key) ? 1 : -1;
		if (stone->keys_state < 0)
			continue;
		opened = envelope_open(KIND_PAIR, stone->receive_key, payload, size, plain);
		if (opened >= 0)
			tombstone = index;
	}
	if (opened < 0)
	{
		budget_left(&friends->unopened_budget, UNOPENED_BUDGET, UNOPENED_BUDGET_INTERVAL, 1);
		friends->dropped_forged++;
		return;
	}
	if (!plain_decode(plain, opened, &message))
	{
		friends->dropped_malformed++;
		return;
	}
	/* the party's messages (p2p_party.inc checks their sender, signature,
	replays and everything else) */
	if (message.type >= TYPE_PARTY_INVITE && message.type <= TYPE_PARTY_PRESENCE && tombstone < 0)
	{
		party_pair_heard(record, member, &message, plain, opened);
		return;
	}
	if (member)
	{
		/* (a member not a friend sends nothing of friends') */
		friends->dropped_forged++;
		return;
	}
	if (tombstone >= 0)
	{
		struct tombstone_record *stone = &friends->tombstones[tombstone];

		if (p2p_equal(message.sender, stone->key, P2P_KEY_SIZE) && plain_verify(plain, opened) &&
			(!stone->reply_time || elapsed(stone->reply_time, REMOVE_REPLY_INTERVAL)))
		{
			stone->reply_time = now_stamp();
			send_remove(stone->key, stone->friendship);
		}
		return;
	}
	/* (the sender inside must be the friend whose key opened it, and the
	recipient this machine, or none for presence) */
	if (!p2p_equal(message.sender, record->key, P2P_KEY_SIZE) ||
		(message.type != TYPE_PRESENCE && !p2p_equal(message.recipient, friends->key, P2P_KEY_SIZE)) ||
		(message.type != TYPE_PRESENCE && message.type != TYPE_INVITE && message.type != TYPE_REMOVE))
	{
		friends->dropped_forged++;
		return;
	}
	if (!plain_verify(plain, opened))
	{
		friends->dropped_forged++;
		platform_log("friends: dropped a message from %s whose signature is not theirs", record->name);
		return;
	}
	if (!replay_check(record, message.epoch, message.counter))
	{
		friends->dropped_replays++;
		platform_log("friends: dropped a replayed message from %s", record->name);
		return;
	}
	switch (message.type)
	{
	case TYPE_PRESENCE:
		presence_heard(record, &message);
		break;
	case TYPE_INVITE:
		invite_heard(record, &message);
		break;
	case TYPE_REMOVE:
		remove_heard((int)(record - friends->friends), &message);
		break;
	}
}

/* one message heard: its envelope opened, its plaintext read, signed, new */
static void handle(int which, const unsigned char *payload, int size)
{
	unsigned char plain[MAXIMUM_PAYLOAD_SIZE];
	struct message message;
	int opened;

	if (size < ENVELOPE_HEADER_SIZE || payload[0] != 'F' || payload[1] != FRIENDS_VERSION)
	{
		friends->dropped_malformed++;
		return;
	}
	if (which == P2P_FRIENDS_INBOX)
	{
		if (payload[2] != KIND_REQUEST)
		{
			friends->dropped_malformed++;
			return;
		}
		if (!budget_left(&friends->request_budget, REQUEST_BUDGET, REQUEST_BUDGET_INTERVAL, 1))
		{
			friends->dropped_rate++;
			return;
		}
		opened = envelope_open(KIND_REQUEST, friends->inbox_key, payload, size, plain);
		if (opened < 0)
		{
			friends->dropped_forged++;
			return;
		}
		if (!plain_decode(plain, opened, &message) || message.type != TYPE_REQUEST)
		{
			friends->dropped_malformed++;
			return;
		}
		/* (a blocked key's: dropped before the work of its signature) */
		if (find_blocked(message.sender) >= 0)
		{
			friends->dropped_blocked++;
			platform_log("friends: dropped a friend request from a blocked player");
			return;
		}
		if (!plain_verify(plain, opened))
		{
			friends->dropped_forged++;
			platform_log("friends: dropped a friend request whose signature is not its sender's");
			return;
		}
		request_heard(&message);
		return;
	}
	if (payload[2] == KIND_PAIR)
	{
		pair_heard(payload, size);
		return;
	}
	if (payload[2] != KIND_BOX)
	{
		friends->dropped_malformed++;
		return;
	}
	/* (an X25519 for each: strangers can send them) */
	if (!budget_left(&friends->box_budget, BOX_BUDGET, BOX_BUDGET_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	opened = box_open(payload, size, plain);
	if (opened < 0)
	{
		friends->dropped_forged++;
		return;
	}
	if (!plain_decode(plain, opened, &message) || message.type != TYPE_ACCEPT)
	{
		friends->dropped_malformed++;
		return;
	}
	if (!plain_verify(plain, opened))
	{
		friends->dropped_forged++;
		platform_log("friends: dropped an answer whose signature is not its sender's");
		return;
	}
	accept_heard(&message);
}

/* ---------- the tests' steps (debug) */

#ifndef HALO_RELEASE
/* HALO_TEST_FRIENDS, comma-separated: code-file=PATH (this machine's code
written there), add-file=PATH (each code in it, once it is there, sent a
request), accept, decline, block (each request heard), invite-after=S (each
friend online invited, S seconds after hosting), appear-offline-at=S,
remove-at=S (every friend removed), cancel-at=S (every request sent taken
back), replay-at=S (the last message sent, sent again as it was), forge-at=S
(a presence tampered with, one signed by another key, one of another sender:
all must be dropped) */
static void test_parse(void)
{
	const char *setting = getenv("HALO_TEST_FRIENDS");
	char copy[1024];
	char *step, *save_pointer = NULL;

	friends->test_parsed = 1;
	friends->test_invite_after = friends->test_appear_offline_at = friends->test_remove_at = -1;
	friends->test_replay_at = friends->test_forge_at = friends->test_cancel_at = -1;
	if (!setting || !setting[0])
		return;
	snprintf(copy, sizeof(copy), "%s", setting);
	for (step = strtok_r(copy, ",", &save_pointer); step; step = strtok_r(NULL, ",", &save_pointer))
	{
		if (!strncmp(step, "code-file=", 10))
			snprintf(friends->test_code_file, sizeof(friends->test_code_file), "%s", step + 10);
		else if (!strncmp(step, "add-file=", 9))
			snprintf(friends->test_add_file, sizeof(friends->test_add_file), "%s", step + 9);
		else if (!strcmp(step, "accept"))
			friends->test_accept = 1;
		else if (!strcmp(step, "decline"))
			friends->test_decline = 1;
		else if (!strcmp(step, "block"))
			friends->test_block = 1;
		else if (!strncmp(step, "invite-after=", 13))
			friends->test_invite_after = atol(step + 13);
		else if (!strncmp(step, "appear-offline-at=", 18))
			friends->test_appear_offline_at = atol(step + 18);
		else if (!strncmp(step, "remove-at=", 10))
			friends->test_remove_at = atol(step + 10);
		else if (!strncmp(step, "cancel-at=", 10))
			friends->test_cancel_at = atol(step + 10);
		else if (!strncmp(step, "replay-at=", 10))
			friends->test_replay_at = atol(step + 10);
		else if (!strncmp(step, "forge-at=", 9))
			friends->test_forge_at = atol(step + 9);
	}
	platform_log("friends: test steps: %s", setting);
}

static int test_due(long at)
{
	return at >= 0 && elapsed(friends->started_time, (unsigned long)at * 1000UL);
}

static void test_forge(void)
{
	struct friend_record *record;
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE];
	unsigned char saved_seed[P2P_SEED_SIZE], saved_key[P2P_KEY_SIZE];
	char topic[TOPIC_LENGTH];
	int size;

	if (!friends->friend_count)
		return;
	record = &friends->friends[0];
	if (!friend_keys(record))
		return;
	mailbox_topic(record->key, topic);
	/* (1) a real presence, one byte of its ciphertext flipped */
	message_start(&message, TYPE_PRESENCE, NULL);
	message.flags = PRESENCE_ONLINE;
	message.game.state = P2P_FRIEND_STATE_FORGE;
	message.game.difficulty = -1;
	snprintf(message.name, sizeof(message.name), "Forged");
	size = plain_encode(&message, plain);
	size = envelope_seal(KIND_PAIR, record->send_key, plain, size, payload);
	payload[size - P2P_TAG_SIZE - 3] ^= 0x40;
	outbound_add(topic, payload, size, 0, NULL);
	/* (2) sealed with the pair's key, signed by another key (its sender
	field this machine's) */
	memcpy(saved_seed, friends->seed, sizeof(saved_seed));
	memcpy(saved_key, friends->key, sizeof(saved_key));
	posix_random_bytes(friends->seed, sizeof(friends->seed));
	message_start(&message, TYPE_PRESENCE, NULL);
	message.flags = PRESENCE_ONLINE;
	message.game.state = P2P_FRIEND_STATE_FORGE;
	message.game.difficulty = -1;
	snprintf(message.name, sizeof(message.name), "Forged");
	size = plain_encode(&message, plain);
	memcpy(friends->seed, saved_seed, sizeof(saved_seed));
	size = envelope_seal(KIND_PAIR, record->send_key, plain, size, payload);
	outbound_add(topic, payload, size, 0, NULL);
	/* (3) signed by another key that names itself the sender: the pair's
	key opens it, but the sender is not the friend */
	posix_random_bytes(friends->seed, sizeof(friends->seed));
	p2p_ed25519_public(friends->seed, friends->key, NULL);
	message_start(&message, TYPE_INVITE, record->key);
	memcpy(message.friendship, record->friendship, FRIENDSHIP_LENGTH);
	message.game.state = P2P_FRIEND_STATE_MULTIPLAYER;
	message.game.difficulty = -1;
	size = plain_encode(&message, plain);
	memcpy(friends->seed, saved_seed, sizeof(saved_seed));
	memcpy(friends->key, saved_key, sizeof(saved_key));
	size = envelope_seal(KIND_PAIR, record->send_key, plain, size, payload);
	outbound_add(topic, payload, size, 0, NULL);
	platform_log("friends: test: three forged messages sent to %s", record->name);
}

static void test_add_codes(void);

static void test_steps(void)
{
	int index;

	if (!friends->test_parsed)
		test_parse();
	if (friends->test_code_file[0])
	{
		char shown[P2P_FRIEND_CODE_SIZE + 1];
		FILE *file;

		code_shown(friends->code, shown, P2P_FRIEND_CODE_SIZE);
		strcat(shown, "\n");
		file = fopen(friends->test_code_file, "wb");
		if (file)
		{
			fwrite(shown, 1, strlen(shown), file);
			fclose(file);
		}
		friends->test_code_file[0] = 0;
	}
	if (friends->test_add_file[0] && !friends->test_added)
		test_add_codes();
	for (index = friends->request_count - 1; index >= 0 && (friends->test_accept || friends->test_decline ||
		friends->test_block); index--)
	{
		char id[P2P_FRIEND_ID_SIZE];

		key_id(friends->requests[index].key, id);
		pthread_mutex_unlock(&friends_lock);
		p2p_friends_act(id, P2P_FRIEND_KIND_REQUEST, friends->test_block ? P2P_FRIENDS_BLOCK : friends->test_accept ?
			P2P_FRIENDS_ACCEPT : P2P_FRIENDS_DECLINE);
		pthread_mutex_lock(&friends_lock);
		if (index > friends->request_count)
			index = friends->request_count;
	}
	if (friends->test_invite_after >= 0 && !friends->test_invited && friends->host.hosting && friends->started_time &&
		test_due(friends->test_invite_after))
	{
		for (index = 0; index < friends->friend_count; index++)
		{
			char id[P2P_FRIEND_ID_SIZE];

			if (!friends->friends[index].heard_time || !friends->friends[index].presence.online)
				continue;
			key_id(friends->friends[index].key, id);
			friends->test_invited = 1;
			pthread_mutex_unlock(&friends_lock);
			platform_log("friends: test: inviting a friend: %s",
				p2p_friends_outcome_text(p2p_friends_act(id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_INVITE)));
			pthread_mutex_lock(&friends_lock);
		}
	}
	if (test_due(friends->test_appear_offline_at) && !friends->test_appeared_offline)
	{
		friends->test_appeared_offline = 1;
		platform_log("friends: test: appearing offline");
	}
	if (test_due(friends->test_remove_at) && !friends->test_removed)
	{
		friends->test_removed = 1;
		while (friends->friend_count)
		{
			char id[P2P_FRIEND_ID_SIZE];

			key_id(friends->friends[0].key, id);
			pthread_mutex_unlock(&friends_lock);
			p2p_friends_act(id, P2P_FRIEND_KIND_FRIEND, P2P_FRIENDS_REMOVE);
			pthread_mutex_lock(&friends_lock);
		}
	}
	if (test_due(friends->test_cancel_at) && !friends->test_cancelled)
	{
		friends->test_cancelled = 1;
		friends->outgoing_count = 0;
		friends->dirty = 1;
		platform_log("friends: test: every request sent taken back");
	}
	if (test_due(friends->test_replay_at) && !friends->test_replayed && friends->test_last_sent_size)
	{
		friends->test_replayed = 1;
		outbound_add(friends->test_last_topic, friends->test_last_sent, friends->test_last_sent_size, 0, NULL);
		platform_log("friends: test: the last message sent, sent again as it was");
	}
	if (test_due(friends->test_forge_at) && !friends->test_forged)
	{
		friends->test_forged = 1;
		test_forge();
	}
}

static void test_add_codes(void)
{
	char text[256];
	FILE *file = fopen(friends->test_add_file, "rb");
	size_t length;
	char *line, *save_pointer = NULL;

	if (!file)
		return;
	length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	friends->test_added = 1;
	for (line = strtok_r(text, "\r\n", &save_pointer); line; line = strtok_r(NULL, "\r\n", &save_pointer))
	{
		pthread_mutex_unlock(&friends_lock);
		platform_log("friends: test: adding %s: %s", line, p2p_friends_add_text(p2p_friends_add(line)));
		pthread_mutex_lock(&friends_lock);
	}
}
#endif

/* whether this machine appears offline: the setting, or the tests' step */
static int appear_offline_now(void)
{
#ifndef HALO_RELEASE
	if (friends->test_appeared_offline)
		return 1;
#endif
	return setting_on("friends.appear_offline", "HALO_FRIENDS_APPEAR_OFFLINE");
}

static int block_key(const unsigned char *key, const char *name);
static int friends_on(void);

/* ---------- the game's players (a player's options: Add friend, Block)

Players in a game know nothing of each other's friend identity. A player's
Add friend (or Block) asks that player's machine for its friend card over
internet play's tunnel (p2p.c's packet type 7, which builds before it drop
unread: no network change; never on the crossplay wire): an ASK holds a
nonce alone, nothing of the asker's; a machine whose player has friends on
and does not appear offline answers with its card - its friend key and name
and a signature of the asker's nonce with that key - and a machine with
friends off, appearing offline or of an older build answers nothing. The
asker then sends a friend request to the card's code as a typed code would
(the other player accepts or declines), or blocks the key. The tunnel joins
each joiner to the host alone: the host relays a card between two of its
joiners, its machine table (p2p_friends_set_game) naming each machine's
peer, the "from" machine of what it relays its own reading of the sender,
never the sender's word; a few a second at most, per machine and in all.
Every field is checked: the sizes, the machines, the nonce of an ask this
machine made, the signature, the key. */

static void card_queue(unsigned long address, const unsigned char *bytes, int size)
{
	struct card_packet *packet;

	if (friends->card_outbound_count >= MAXIMUM_CARD_OUTBOUND || size > CARD_ANSWER_SIZE)
	{
		friends->dropped_rate++;
		return;
	}
	packet = &friends->card_outbound[friends->card_outbound_count++];
	packet->address = address;
	packet->from_host = 0;
	packet->size = size;
	memcpy(packet->bytes, bytes, (size_t)size);
}

/* the peer a card to a machine goes to: a host's, the machine's own; a
joiner's, the host (0) */
static int card_route(int machine, unsigned long *address)
{
	if (machine < 0 || machine >= CARD_MACHINES)
		return 0;
	if (!friends->game_hosting)
	{
		*address = 0;
		return 1;
	}
	*address = friends->machines[machine].address;
	return *address != 0;
}

static void card_signed_data(const unsigned char *packet, unsigned char *data, int *size)
{
	static const char label[] = "hcev-friends-1 card";
	int at = 0;

	memcpy(data, label, sizeof(label) - 1);
	at += (int)sizeof(label) - 1;
	/* (the kind, the nonce, the key and the name: the machines are the
	game's numbers, which the host's relay may correct) */
	data[at++] = packet[1];
	memcpy(data + at, packet + 4, NONCE_LENGTH + P2P_KEY_SIZE + 1 + NAME_LENGTH);
	at += NONCE_LENGTH + P2P_KEY_SIZE + 1 + NAME_LENGTH;
	*size = at;
}

/* this machine's answer to an ask of a machine (its nonce) */
static void card_answer(int machine, const unsigned char *nonce)
{
	unsigned char packet[CARD_ANSWER_SIZE], data[64 + CARD_ANSWER_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned long address;
	int size;

	if (!card_route(machine, &address))
		return;
	memset(packet, 0, sizeof(packet));
	packet[0] = CARD_VERSION;
	packet[1] = CARD_ANSWER;
	packet[2] = (unsigned char)friends->game_machine;
	packet[3] = (unsigned char)machine;
	memcpy(packet + 4, nonce, NONCE_LENGTH);
	memcpy(packet + CARD_HEADER_SIZE, friends->key, P2P_KEY_SIZE);
	own_name(name);
	put_text(packet + CARD_HEADER_SIZE + P2P_KEY_SIZE, name, NAME_LENGTH);
	card_signed_data(packet, data, &size);
	p2p_ed25519_sign(friends->seed, friends->key, data, size, packet + CARD_ANSWER_SIZE - P2P_SIGNATURE_SIZE);
	card_queue(address, packet, CARD_ANSWER_SIZE);
}

/* a host's machine by its peer's address (-1: none) */
static int card_machine_of(unsigned long address)
{
	int index;

	for (index = 0; index < CARD_MACHINES && address; index++)
		if (friends->machines[index].address == address)
			return index;
	return -1;
}

static void card_heard(const struct card_packet *packet)
{
	const unsigned char *bytes = packet->bytes;
	int from, to;

	if (packet->size < CARD_HEADER_SIZE || bytes[0] != CARD_VERSION || (bytes[1] != CARD_ASK && bytes[1] != CARD_ANSWER) ||
		(bytes[1] == CARD_ASK && packet->size != CARD_HEADER_SIZE) ||
		(bytes[1] == CARD_ANSWER && packet->size != CARD_ANSWER_SIZE) || !friends->in_game)
	{
		friends->dropped_malformed++;
		return;
	}
	from = bytes[2];
	to = bytes[3];
	if (from >= CARD_MACHINES || to >= CARD_MACHINES || from == to)
	{
		friends->dropped_malformed++;
		return;
	}
	if (friends->game_hosting)
	{
		/* (the sender's machine as this host has it, whatever it said) */
		int sender = card_machine_of(packet->address);
		unsigned char relayed[CARD_ANSWER_SIZE];

		if (sender < 0 || sender == friends->game_machine)
		{
			friends->dropped_forged++;
			return;
		}
		from = sender;
		if (to != friends->game_machine)
		{
			struct card_machine *machine = &friends->machines[sender];

			if (!friends->machines[to].address ||
				(machine->relayed_time && !elapsed(machine->relayed_time, CARD_RELAY_MACHINE_INTERVAL)) ||
				!budget_left(&friends->card_relay_budget, CARD_RELAY_BUDGET, CARD_RELAY_INTERVAL, 1))
			{
				friends->dropped_rate++;
				return;
			}
			machine->relayed_time = now_stamp();
			memcpy(relayed, bytes, (size_t)packet->size);
			relayed[2] = (unsigned char)from;
			card_queue(friends->machines[to].address, relayed, packet->size);
			return;
		}
	}
	else if (!packet->from_host || to != friends->game_machine)
	{
		/* (a joiner hears cards from its host alone, to its own machine) */
		friends->dropped_forged++;
		return;
	}
	if (bytes[1] == CARD_ASK)
	{
		struct card_machine *machine = &friends->machines[from];

		/* (friends off, appearing offline: nothing; never told why) */
		if (!friends_on() || friends->appear_offline)
			return;
		if (machine->answered_time && !elapsed(machine->answered_time, CARD_ANSWER_INTERVAL))
		{
			friends->dropped_rate++;
			return;
		}
		machine->answered_time = now_stamp();
		card_answer(from, bytes + 4);
		platform_log("friends: a player in this game (machine %d) asked for this machine's friend card; answered", from);
		return;
	}
	/* an answer: to an ask this machine made of that machine, signed by the
	key it holds */
	{
		struct card_machine *machine = &friends->machines[from];
		unsigned char data[64 + CARD_ANSWER_SIZE];
		const unsigned char *key = bytes + CARD_HEADER_SIZE;
		char name[P2P_FRIEND_NAME_SIZE];
		int size;

		if (!machine->asking || !p2p_equal(machine->nonce, bytes + 4, NONCE_LENGTH) ||
			bytes[CARD_HEADER_SIZE + P2P_KEY_SIZE] > NAME_LENGTH)
		{
			friends->dropped_replays++;
			return;
		}
		card_signed_data(bytes, data, &size);
		if (!key_usable(key) || !p2p_ed25519_verify(key, data, size, bytes + CARD_ANSWER_SIZE - P2P_SIGNATURE_SIZE))
		{
			friends->dropped_forged++;
			platform_log("friends: a friend card that is not its key's dropped");
			return;
		}
		if (!clean_text(name, NAME_LENGTH, bytes + CARD_HEADER_SIZE + P2P_KEY_SIZE + 1,
			bytes[CARD_HEADER_SIZE + P2P_KEY_SIZE], 1))
		{
			snprintf(name, sizeof(name), "Player");
		}
		machine->asking = 0;
		machine->unanswered = 0;
		machine->has_card = 1;
		memcpy(machine->key, key, P2P_KEY_SIZE);
		snprintf(machine->name, sizeof(machine->name), "%s", name);
		platform_log("friends: %s (machine %d) answered with their friend card", name, from);
		friends->card_action = machine->intent;
		memcpy(friends->card_action_key, key, P2P_KEY_SIZE);
		snprintf(friends->card_action_name, sizeof(friends->card_action_name), "%s", name);
	}
}

/* (p2p.c, under p2p_lock) a card heard from a peer (its virtual address;
from_host: the host this machine joined): queued */
void p2p_friends_tunnel_heard(unsigned long address, int from_host, const unsigned char *bytes, int size)
{
	struct card_packet *packet;

	if (size <= 0 || size > CARD_ANSWER_SIZE)
		return;
	pthread_mutex_lock(&friends_lock);
	if (friends->card_inbound_count >= MAXIMUM_CARD_INBOUND)
		friends->dropped_rate++;
	else
	{
		packet = &friends->card_inbound[friends->card_inbound_count++];
		packet->address = address;
		packet->from_host = from_host;
		packet->size = size;
		memcpy(packet->bytes, bytes, (size_t)size);
	}
	pthread_mutex_unlock(&friends_lock);
}

/* (p2p.c, under p2p_lock) the cards to send: the index-th (its peer, 0
the host): its size, or -1 past the end; then all let go */
int p2p_friends_tunnel_outgoing(int index, unsigned long *address, unsigned char *bytes, int capacity)
{
	int size = -1;

	pthread_mutex_lock(&friends_lock);
	if (index >= 0 && index < friends->card_outbound_count && friends->card_outbound[index].size <= capacity)
	{
		*address = friends->card_outbound[index].address;
		size = friends->card_outbound[index].size;
		memcpy(bytes, friends->card_outbound[index].bytes, (size_t)size);
	}
	pthread_mutex_unlock(&friends_lock);
	return size;
}

void p2p_friends_tunnel_sent(void)
{
	pthread_mutex_lock(&friends_lock);
	friends->card_outbound_count = 0;
	pthread_mutex_unlock(&friends_lock);
}

/* (p2p_friends_update, friends_lock held) the cards heard, the asks
unanswered */
static void cards_update(void)
{
	int index;

	while (friends->card_inbound_count)
	{
		struct card_packet packet = friends->card_inbound[0];

		memmove(&friends->card_inbound[0], &friends->card_inbound[1],
			sizeof(friends->card_inbound[0]) * (size_t)(friends->card_inbound_count - 1));
		friends->card_inbound_count--;
		card_heard(&packet);
	}
	for (index = 0; index < CARD_MACHINES && friends->in_game; index++)
	{
		struct card_machine *machine = &friends->machines[index];

		if (machine->asking && elapsed(machine->asked_time, CARD_ANSWER_TIME))
		{
			machine->asking = 0;
			machine->unanswered = 1;
			platform_log("friends: machine %d did not answer for its friend card (friends off, appearing offline, "
				"or an older build)", index);
		}
	}
}

void p2p_friends_set_game(int machine, int hosting, const unsigned long *addresses, int count)
{
	int index;

	pthread_mutex_lock(&friends_lock);
	if (machine < 0 || machine >= CARD_MACHINES)
	{
		if (friends->in_game)
		{
			/* (the game over: its players' cards forgotten) */
			memset(friends->machines, 0, sizeof(friends->machines));
			friends->card_inbound_count = 0;
			friends->in_game = 0;
		}
		pthread_mutex_unlock(&friends_lock);
		return;
	}
	if (!friends->in_game || friends->game_machine != machine || friends->game_hosting != (hosting != 0))
	{
		memset(friends->machines, 0, sizeof(friends->machines));
		friends->game_said = 0;
	}
	friends->in_game = 1;
	friends->game_machine = machine;
	friends->game_hosting = hosting != 0;
	for (index = 0; index < CARD_MACHINES; index++)
	{
		unsigned long address = hosting && addresses && index < count && index != machine ? addresses[index] : 0;

		/* (internet play's virtual addresses, 100.64.0.0/10, in network byte
		order as p2p.c has them; the game's may be in host byte order; any
		other is no peer's) */
		if ((address & 0xFF) == 100 && ((address >> 8) & 0xC0) == 0x40)
			;
		else if ((address >> 24 & 0xFF) == 100 && ((address >> 16) & 0xC0) == 0x40)
			address = (address & 0xFF) << 24 | (address & 0xFF00) << 8 | (address >> 8 & 0xFF00) | (address >> 24 & 0xFF);
		else
			address = 0;

		/* (a machine gone, or another in its place: what was known of it
		forgotten) */
		if (friends->machines[index].address != address && friends->game_hosting)
			memset(&friends->machines[index], 0, sizeof(friends->machines[index]));
		friends->machines[index].address = address;
	}
	if (!friends->game_said)
	{
		friends->game_said = 1;
		platform_log("friends: in a game, machine %d%s", machine, hosting ? ", hosting (cards relayed)" : "");
	}
	pthread_mutex_unlock(&friends_lock);
}

int p2p_friends_player_ask(int machine, int intent)
{
	unsigned char packet[CARD_HEADER_SIZE];
	struct card_machine *record;
	unsigned long address;
	int result = P2P_FRIENDS_DONE;

	pthread_mutex_lock(&friends_lock);
	load();
	if (!friends->in_game || machine < 0 || machine >= CARD_MACHINES || machine == friends->game_machine)
		result = P2P_FRIENDS_GONE;
	/* (the cards ride the tunnel: never to a PC machine's, a crossplay
	game's, p2p.c) */
	else if (!friends_on() || friends->host.crossplay)
		result = P2P_FRIENDS_OFFLINE;
	else if (friends->appear_offline)
		result = P2P_FRIENDS_APPEARING_OFFLINE;
	else if (!card_route(machine, &address))
		result = P2P_FRIENDS_GONE;
	else
	{
		record = &friends->machines[machine];
		if (record->has_card)
		{
			/* (asked before this game: its card kept) */
			friends->card_action = intent;
			memcpy(friends->card_action_key, record->key, P2P_KEY_SIZE);
			snprintf(friends->card_action_name, sizeof(friends->card_action_name), "%s", record->name);
		}
		else if (record->asking || (record->asked_time && !elapsed(record->asked_time, CARD_ASK_INTERVAL)))
			result = P2P_FRIENDS_TOO_SOON;
		else
		{
			record->asking = 1;
			record->unanswered = 0;
			record->intent = intent;
			record->asked_time = now_stamp();
			posix_random_bytes(record->nonce, NONCE_LENGTH);
			packet[0] = CARD_VERSION;
			packet[1] = CARD_ASK;
			packet[2] = (unsigned char)friends->game_machine;
			packet[3] = (unsigned char)machine;
			memcpy(packet + 4, record->nonce, NONCE_LENGTH);
			card_queue(address, packet, CARD_HEADER_SIZE);
			platform_log("friends: asked machine %d for its friend card (to %s)", machine,
				intent == P2P_FRIENDS_PLAYER_BLOCK ? "block it" : intent == P2P_FRIENDS_PLAYER_PARTY ?
				"invite it to the party" : "add it");
		}
	}
	pthread_mutex_unlock(&friends_lock);
	return result;
}

/* (friends_lock held) the lists' state of a key: a friend, a request sent
to its code, blocked, or none */
static int key_list_state(const unsigned char *key)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1];

	if (find_friend(key))
		return P2P_FRIENDS_PLAYER_FRIEND;
	if (find_blocked(key) >= 0)
		return P2P_FRIENDS_PLAYER_BLOCKED;
	code_of_key(key, code);
	if (find_outgoing(code) >= 0)
		return P2P_FRIENDS_PLAYER_REQUEST_SENT;
	if (find_request(key) >= 0)
		return P2P_FRIENDS_PLAYER_ASKED_YOU;
	return P2P_FRIENDS_PLAYER_NONE;
}

int p2p_friends_player_state(int machine)
{
	int state = P2P_FRIENDS_PLAYER_NONE;

	pthread_mutex_lock(&friends_lock);
	if (friends->in_game && machine >= 0 && machine < CARD_MACHINES)
	{
		const struct card_machine *record = &friends->machines[machine];

		if (machine == friends->game_machine)
			state = P2P_FRIENDS_PLAYER_THIS_VITA;
		else if (record->has_card)
			state = key_list_state(record->key);
		else if (record->asking)
			state = P2P_FRIENDS_PLAYER_ASKING;
		else if (record->unanswered)
			state = P2P_FRIENDS_PLAYER_NOT_TAKING;
	}
	pthread_mutex_unlock(&friends_lock);
	return state;
}

int p2p_friends_player_friend_id(int machine, char *id)
{
	int found = 0;

	pthread_mutex_lock(&friends_lock);
	if (friends->in_game && machine >= 0 && machine < CARD_MACHINES && machine != friends->game_machine &&
		friends->machines[machine].has_card && find_friend(friends->machines[machine].key))
	{
		key_id(friends->machines[machine].key, id);
		found = 1;
	}
	pthread_mutex_unlock(&friends_lock);
	return found;
}

/* (p2p_friends_update, the lock let go) what an answered card asked */
static void card_action_do(int action, const unsigned char *key, const char *name)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1], shown[P2P_FRIEND_CODE_SIZE];

	if (action == P2P_FRIENDS_PLAYER_PARTY)
	{
		char id[P2P_FRIEND_ID_SIZE];
		int is_friend;

		pthread_mutex_lock(&friends_lock);
		is_friend = find_friend(key) != NULL;
		key_id(key, id);
		pthread_mutex_unlock(&friends_lock);
		platform_log("party: inviting %s (a player in this game): %s", name, is_friend ?
			p2p_party_outcome_text(p2p_party_invite(id)) : p2p_party_outcome_text(P2P_PARTY_NOT_FRIEND));
		return;
	}
	if (action == P2P_FRIENDS_PLAYER_BLOCK)
	{
		pthread_mutex_lock(&friends_lock);
		if (block_key(key, name) == P2P_FRIENDS_DONE)
		{
			friends->dirty = 1;
			platform_log("friends: blocked %s (a player in this game)", name);
		}
		pthread_mutex_unlock(&friends_lock);
		save(1);
		return;
	}
	code_of_key(key, code);
	code_shown(code, shown, sizeof(shown));
	platform_log("friends: adding %s (a player in this game): %s", name, p2p_friends_add_text(p2p_friends_add(shown)));
}

/* ---------- internet play's side (p2p_internal.h) */

/* whether friends want the brokers: internet play runs and friends are on */
static int friends_on(void)
{
#ifdef HALO_DEDICATED_SERVER
	/* (a dedicated server has no player of its own: no friends) */
	return 0;
#else
	return friends->online && friends->has_identity && setting_on("friends.enabled", "HALO_FRIENDS");
#endif
}

int p2p_friends_wanted(void)
{
	int wanted;

	pthread_mutex_lock(&friends_lock);
	wanted = friends_on();
	pthread_mutex_unlock(&friends_lock);
	return wanted;
}

void p2p_friends_topics(char *inbox, char *mailbox, int size)
{
	pthread_mutex_lock(&friends_lock);
	if (friends_on())
	{
		snprintf(inbox, (size_t)size, "%s", friends->inbox_topic);
		/* (no friend, no request sent: nobody writes there) */
		snprintf(mailbox, (size_t)size, "%s", friends->friend_count || friends->outgoing_count ||
			friends->tombstone_count || friends->party.active ? friends->mailbox_topic : "");
	}
	else
	{
		inbox[0] = 0;
		mailbox[0] = 0;
	}
	pthread_mutex_unlock(&friends_lock);
}

void p2p_friends_heard(int which, const unsigned char *payload, int size)
{
	struct inbound *entry;

	if (size <= 0 || size > MAXIMUM_PAYLOAD_SIZE || (which != P2P_FRIENDS_INBOX && which != P2P_FRIENDS_MAILBOX))
		return;
	pthread_mutex_lock(&friends_lock);
	if (friends->inbound_count >= MAXIMUM_INBOUND)
		friends->dropped_rate++;
	else
	{
		entry = &friends->inbound[friends->inbound_count++];
		entry->which = which;
		entry->size = size;
		memcpy(entry->payload, payload, (size_t)size);
	}
	pthread_mutex_unlock(&friends_lock);
}

int p2p_friends_outgoing(int index, unsigned long *serial, char *topic, int topic_size, unsigned char *payload,
	int *sent_mask)
{
	int size = -1;

	pthread_mutex_lock(&friends_lock);
	if (index >= 0 && index < friends->outbound_count)
	{
		const struct outbound *entry = &friends->outbound[index];

		*serial = entry->serial;
		snprintf(topic, (size_t)topic_size, "%s", entry->topic);
		memcpy(payload, entry->payload, (size_t)entry->size);
		*sent_mask = entry->sent_mask;
		size = entry->size;
	}
	pthread_mutex_unlock(&friends_lock);
	return size;
}

void p2p_friends_outgoing_sent(unsigned long serial, int broker)
{
	int index;

	pthread_mutex_lock(&friends_lock);
	for (index = 0; index < friends->outbound_count; index++)
		if (friends->outbound[index].serial == serial)
			friends->outbound[index].sent_mask |= 1 << broker;
	pthread_mutex_unlock(&friends_lock);
}

void p2p_friends_outgoing_settle(int ready_mask)
{
	int index;

	pthread_mutex_lock(&friends_lock);
	for (index = friends->outbound_count - 1; index >= 0; index--)
	{
		const struct outbound *entry = &friends->outbound[index];

		if ((ready_mask && (entry->sent_mask & ready_mask) == ready_mask) || elapsed(entry->time, OUTBOUND_LIFETIME))
		{
			memmove(&friends->outbound[index], &friends->outbound[index + 1],
				sizeof(friends->outbound[0]) * (size_t)(friends->outbound_count - index - 1));
			friends->outbound_count--;
		}
	}
	pthread_mutex_unlock(&friends_lock);
}

/* (internet play stopping) an offline presence to every friend, queued for
the brokers' last publishes, then nothing more until it runs again (the
game exiting: nothing more at all) */
void p2p_friends_stopping(int quitting)
{
	int index;

	pthread_mutex_lock(&friends_lock);
	if (quitting)
		friends->quitting = 1;
	/* (a party left: its members told, as the friends are told below) */
	if (friends->party.active)
		party_leave_now(friends_on(), quitting ? "quitting" : "internet play stopped");
	if (friends_on() && friends->presence_online && friends->friend_count)
	{
		presence_sign(0, 0);
		for (index = 0; index < friends->friend_count; index++)
			presence_send(&friends->friends[index]);
	}
	friends->online = 0;
	friends->presence_online = 0;
	friends->presence_size = 0;
	for (index = 0; index < friends->friend_count; index++)
		friends->friends[index].heard_time = 0;
	pthread_mutex_unlock(&friends_lock);
	save(1);
}

void p2p_friends_update(const struct p2p_friends_host *host)
{
	int index, handled = 0, signatures = 0, keys = 0;
	int was_online, was_on, appear_offline, joinable;

#ifdef HALO_DEDICATED_SERVER
	/* (no identity made, nothing heard or sent) */
	(void)host;
	return;
#endif
	pthread_mutex_lock(&friends_lock);
	load();
	was_online = friends->online;
	was_on = friends_on();
	friends->host = *host;
	friends->online = host->online && !friends->quitting;
#ifndef HALO_RELEASE
	test_steps();
#endif
	/* the game's players' cards: answered only with friends on, relayed by a
	host whatever its own (card_heard) */
	cards_update();
	if (!friends_on())
	{
		/* (friends turned off with the brokers still there: said once) */
		if (was_on && friends->online && friends->presence_online)
		{
			presence_sign(0, 0);
			for (index = 0; index < friends->friend_count; index++)
				presence_send(&friends->friends[index]);
		}
		friends->presence_online = 0;
		friends->presence_size = 0;
		friends->inbound_count = 0;
		if (friends->party.active)
			party_leave_now(was_on && friends->online, "friends turned off");
		pthread_mutex_unlock(&friends_lock);
		save(0);
		return;
	}
	if (!was_online)
	{
		platform_log("friends: online (%d friends; inbox and mailbox subscribed)", friends->friend_count);
		friends->presence_size = 0;
		friends->presence_online = 0;
		for (index = 0; index < friends->outgoing_count; index++)
			friends->outgoing[index].sent_time = friends->outgoing[index].first_time = 0;
	}
	/* (the pair keys, a few a pass) */
	for (index = 0; index < friends->friend_count && keys < PAIR_KEYS_PER_PASS; index++)
		if (!friends->friends[index].keys_state)
		{
			friend_keys(&friends->friends[index]);
			keys++;
		}
	/* what was heard, one at a time (the lock let go between: the game's
	threads read the lists) */
	while (friends->inbound_count && handled < MESSAGES_PER_PASS)
	{
		struct inbound entry = friends->inbound[0];

		memmove(&friends->inbound[0], &friends->inbound[1], sizeof(friends->inbound[0]) *
			(size_t)(friends->inbound_count - 1));
		friends->inbound_count--;
		handle(entry.which, entry.payload, entry.size);
		handled++;
		pthread_mutex_unlock(&friends_lock);
		pthread_mutex_lock(&friends_lock);
	}
	/* what an answered card asked, done with the lock let go (the lists' own
	calls take it) */
	if (friends->card_action)
	{
		int action = friends->card_action;
		unsigned char key[P2P_KEY_SIZE];
		char name[P2P_FRIEND_NAME_SIZE];

		friends->card_action = 0;
		memcpy(key, friends->card_action_key, P2P_KEY_SIZE);
		memcpy(name, friends->card_action_name, sizeof(name));
		pthread_mutex_unlock(&friends_lock);
		card_action_do(action, key, name);
		pthread_mutex_lock(&friends_lock);
	}
	/* the requests sent: often at first, then now and then; old ones go */
	for (index = friends->outgoing_count - 1; index >= 0; index--)
	{
		struct outgoing_record *record = &friends->outgoing[index];
		unsigned long interval = !record->first_time || !elapsed(record->first_time, REQUEST_FAST_TIME) ?
			REQUEST_FAST_INTERVAL : REQUEST_SLOW_INTERVAL;

		if (record->time && unix_now() > record->time + OUTGOING_LIFETIME)
		{
			platform_log("friends: the request to %.4s-%.4s-%.4s expired", record->code, record->code + 4,
				record->code + 8);
			outgoing_remove(index);
			friends->dirty = 1;
			continue;
		}
		if (!record->sent_time || elapsed(record->sent_time, interval))
			send_request(record);
	}
	/* old mutes and tombstones go */
	for (index = friends->muted_count - 1; index >= 0; index--)
		if (unix_now() > friends->muted[index].time + MUTE_TIME)
		{
			memmove(&friends->muted[index], &friends->muted[index + 1],
				sizeof(friends->muted[0]) * (size_t)(friends->muted_count - index - 1));
			friends->muted_count--;
			friends->dirty = 1;
		}
	for (index = friends->tombstone_count - 1; index >= 0; index--)
		if (unix_now() > friends->tombstones[index].time + TOMBSTONE_TIME)
		{
			tombstone_remove(index);
			friends->dirty = 1;
		}
	/* old invites go */
	for (index = friends->invite_count - 1; index >= 0; index--)
		if (elapsed(friends->invites[index].time, INVITE_LIFETIME))
			invite_remove(index);
	/* friends not heard in a while: offline */
	for (index = 0; index < friends->friend_count; index++)
	{
		struct friend_record *record = &friends->friends[index];

		if (record->heard_time && record->presence.online && elapsed(record->heard_time, PRESENCE_TIMEOUT))
		{
			record->presence.online = 0;
			platform_log("friends: %s went offline (not heard from)", record->name);
		}
	}
	/* this machine's presence: on a change (not too often), on Appear
	offline, every PRESENCE_INTERVAL; to the friends who asked */
	appear_offline = appear_offline_now();
	joinable = own_joinable();
	if (appear_offline != friends->appear_offline)
	{
		friends->appear_offline = appear_offline;
		platform_log("friends: %s", appear_offline ? "appearing offline" : "appearing online");
		/* (appearing offline: no party; its members told now, nothing after) */
		if (appear_offline && friends->party.active)
			party_leave_now(1, "appearing offline");
		if (appear_offline && friends->presence_online && signatures < SIGNATURES_PER_PASS)
		{
			presence_sign(0, 0);
			signatures++;
			presence_due_all();
		}
		else if (!appear_offline)
			friends->presence_size = 0;
	}
	if (!friends->appear_offline && friends->friend_count && signatures < SIGNATURES_PER_PASS)
	{
		/* (a new invite - gone private, a new password - is a change: the
		presence carries the token) */
		int change = friends->activity_changed || joinable != friends->presence_joinable ||
			(joinable && !p2p_equal(friends->presence_token, friends->host.token, P2P_TOKEN_SIZE));

		if (!friends->presence_size || !friends->presence_online)
		{
			/* (the first since online: friends asked to answer at once) */
			presence_sign(1, 1);
			signatures++;
			friends->activity_changed = 0;
			presence_due_all();
		}
		else if ((change && elapsed(friends->presence_time, PRESENCE_CHANGE_INTERVAL)) ||
			elapsed(friends->presence_time, PRESENCE_INTERVAL))
		{
			presence_sign(1, 0);
			signatures++;
			friends->activity_changed = 0;
			presence_due_all();
		}
		else
		{
			/* (a friend who asked has had this one: a new one for them) */
			for (index = 0; index < friends->friend_count; index++)
			{
				struct friend_record *record = &friends->friends[index];

				if (record->reply_asked && record->sent_counter == friends->presence_counter)
				{
					presence_sign(1, 0);
					signatures++;
					break;
				}
			}
		}
	}
	if (friends->presence_size && (!friends->appear_offline || !friends->presence_online))
	{
		for (index = 0; index < friends->friend_count; index++)
			if (friends->friends[index].presence_due && !presence_send(&friends->friends[index]))
				break;
	}
	/* the party: its timeouts, hand-off, roster, status, follow, presence */
	party_update();
	/* drops, now and then */
	if ((friends->dropped_replays || friends->dropped_forged || friends->dropped_blocked || friends->dropped_rate ||
		friends->dropped_malformed) && (!friends->drops_logged_time || elapsed(friends->drops_logged_time, 10000)))
	{
		platform_log("friends: dropped: %d replayed, %d forged or not for this machine, %d from blocked players, %d past "
			"the limits, %d malformed", friends->dropped_replays, friends->dropped_forged, friends->dropped_blocked,
			friends->dropped_rate, friends->dropped_malformed);
		friends->total_replays += friends->dropped_replays;
		friends->total_forged += friends->dropped_forged;
		friends->total_blocked += friends->dropped_blocked;
		friends->total_rate += friends->dropped_rate;
		friends->total_malformed += friends->dropped_malformed;
		friends->dropped_replays = friends->dropped_forged = friends->dropped_blocked = 0;
		friends->dropped_rate = friends->dropped_malformed = 0;
		friends->drops_logged_time = now_stamp();
	}
	pthread_mutex_unlock(&friends_lock);
	save(0);
}

/* ---------- the game's side (p2p_friends.h) */

int p2p_friends_code(char *code, int size)
{
	pthread_mutex_lock(&friends_lock);
	load();
	code_shown(friends->code, code, size);
	pthread_mutex_unlock(&friends_lock);
	return 1;
}

static const char *const difficulty_names[] = { N_("Easy"), N_("Normal"), N_("Heroic"), N_("Legendary") };

void p2p_friends_activity_text(char *text, int size, int state, const char *map, const char *gametype, int engine,
	int difficulty, int pc_map)
{
	/* (a variant with no name: its game's, as the server browser shows it) */
	static const char *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
	const char *title = map && map[0] ? p2p_lobby_map_title(map, pc_map) : "";
	const char *level = difficulty >= 0 && difficulty < 4 ? T(difficulty_names[difficulty]) : "";

	if ((!gametype || !gametype[0]) && engine > 0 && engine < (int)(sizeof(engines) / sizeof(engines[0])))
		gametype = engines[engine];

	switch (state)
	{
	case P2P_FRIEND_STATE_MENUS:
		snprintf(text, (size_t)size, "%s", T("In the menus"));
		break;
	case P2P_FRIEND_STATE_MULTIPLAYER:
		if (title[0])
			snprintf(text, (size_t)size, T("%s on %s"), gametype && gametype[0] ? gametype : T("A game"), title);
		else
			snprintf(text, (size_t)size, "%s", T("In a multiplayer game"));
		break;
	case P2P_FRIEND_STATE_COOP:
		snprintf(text, (size_t)size, T("Co-op: %s%s%s"), title[0] ? title : "-", level[0] ? ", " : "", level);
		break;
	case P2P_FRIEND_STATE_CAMPAIGN:
		snprintf(text, (size_t)size, T("Campaign: %s%s%s"), title[0] ? title : "-", level[0] ? ", " : "", level);
		break;
	case P2P_FRIEND_STATE_FORGE:
		if (title[0])
			snprintf(text, (size_t)size, T("In Forge on %s"), title);
		else
			snprintf(text, (size_t)size, "%s", T("In Forge"));
		break;
	default:
		snprintf(text, (size_t)size, "%s", T("Offline"));
		break;
	}
}

/* a game's tag after an activity line (a game's only: multiplayer or
co-op), as the browsers tag their rows (crossplay_guard.h): VITA, VITA+PC
(a Vita's game PC players can join too), PC (a PC's game they joined) */
static void activity_tag(char *text, int size, int state, int wire, int pc_host, int host_pc_build)
{
	size_t length = strlen(text);

	if ((state != P2P_FRIEND_STATE_MULTIPLAYER && state != P2P_FRIEND_STATE_COOP) || length + 1 >= (size_t)size)
		return;
	/* (the PC build's game: VITA (PC), as the browsers tag it) */
	snprintf(text + length, (size_t)size - length, " (%s%s)", pc_host && wire ? "PC" : host_pc_build ? "VITA (PC)" :
		"VITA", wire && !pc_host ? "+PC" : "");
}

/* a friend's entry */
static void friend_entry(const struct friend_record *record, struct p2p_friend_entry *entry)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1];
	int online = friends->online && record->heard_time && record->presence.online;

	memset(entry, 0, sizeof(*entry));
	key_id(record->key, entry->id);
	entry->kind = P2P_FRIEND_KIND_FRIEND;
	snprintf(entry->name, sizeof(entry->name), "%s", record->name);
	code_of_key(record->key, code);
	code_shown(code, entry->code, sizeof(entry->code));
	entry->online = online;
	entry->state = online ? record->presence.state : P2P_FRIEND_STATE_OFFLINE;
	entry->joinable = online && record->presence.joinable &&
		game_wire_joinable(record->presence.version, record->presence.wire);
	entry->crossplay = game_wire_of(record->presence.version, record->presence.wire) == 1;
	entry->locked = entry->joinable && record->presence.locked;
	entry->invitable = online && own_invitable();
	entry->players = online ? record->presence.players : 0;
	entry->maximum = online ? record->presence.maximum : 0;
	p2p_friends_activity_text(entry->activity, sizeof(entry->activity), entry->state, record->presence.map,
		record->presence.gametype, record->presence.engine, record->presence.difficulty, record->presence.pc_map);
	if (online)
		activity_tag(entry->activity, sizeof(entry->activity), entry->state, entry->crossplay,
			record->presence.pc_host || record->presence.version != HALO_PORT_NETWORK_VERSION,
			record->presence.host_pc_build);
	/* (they play on this project's PC build: the lists say PC) */
	entry->pc = online && record->presence.sender_pc;
	if (online)
		party_friend_entry(record, entry);
}

static int friend_order(const void *first, const void *second)
{
	const struct p2p_friend_entry *a = first, *b = second;

	if (a->online != b->online)
		return b->online - a->online;
	return strcasecmp(a->name, b->name);
}

#ifndef HALO_RELEASE
/* (the menus' tests' pictures, HALO_TEST_FRIENDS_SHOW=online: the friends
shown online, the first in a public game, the second in co-op, as the lists
would show them; nothing is sent or heard) */
static void test_show(struct p2p_friend_entry *entries, int count)
{
	const char *setting = getenv("HALO_TEST_FRIENDS_SHOW");
	int index, friend_index = 0;

	if (!setting || strcmp(setting, "online"))
		return;
	for (index = 0; index < count; index++)
	{
		struct p2p_friend_entry *entry = &entries[index];

		if (entry->kind != P2P_FRIEND_KIND_FRIEND)
			continue;
		entry->online = 1;
		if (friend_index == 0)
		{
			entry->state = P2P_FRIEND_STATE_MULTIPLAYER;
			entry->joinable = 1;
			entry->players = 5;
			entry->maximum = 16;
			p2p_friends_activity_text(entry->activity, sizeof(entry->activity), entry->state, "bloodgulch", "Slayer", 2, -1, 0);
		}
		else
		{
			entry->state = P2P_FRIEND_STATE_COOP;
			entry->players = 2;
			entry->maximum = 4;
			p2p_friends_activity_text(entry->activity, sizeof(entry->activity), entry->state, "a10", "", 0, 3, 0);
		}
		friend_index++;
	}
}
#endif

int p2p_friends_list(struct p2p_friend_entry *entries, int maximum_count)
{
	int count = 0, index, first_friend;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = friends->request_count - 1; index >= 0 && count < maximum_count; index--)
	{
		struct p2p_friend_entry *entry = &entries[count++];
		char code[P2P_FRIEND_CODE_LENGTH + 1];

		memset(entry, 0, sizeof(*entry));
		key_id(friends->requests[index].key, entry->id);
		entry->kind = P2P_FRIEND_KIND_REQUEST;
		snprintf(entry->name, sizeof(entry->name), "%s", friends->requests[index].name);
		code_of_key(friends->requests[index].key, code);
		code_shown(code, entry->code, sizeof(entry->code));
		snprintf(entry->activity, sizeof(entry->activity), "%s", T("Wants to be your friend"));
	}
	for (index = 0; index < friends->invite_count && count < maximum_count; index++)
	{
		const struct invite_record *invite = &friends->invites[index];
		const struct friend_record *record = find_friend(invite->key);
		struct p2p_friend_entry *entry;
		char game[P2P_FRIEND_ACTIVITY_SIZE];

		if (!record)
			continue;
		entry = &entries[count++];
		memset(entry, 0, sizeof(*entry));
		p2p_hex(invite->invite_id, INVITE_ID_LENGTH, entry->id);
		entry->kind = P2P_FRIEND_KIND_INVITE;
		snprintf(entry->name, sizeof(entry->name), "%s", record->name);
		entry->online = 1;
		entry->state = invite->game.state;
		entry->joinable = game_wire_joinable(invite->game.version, invite->game.wire);
		entry->crossplay = game_wire_of(invite->game.version, invite->game.wire) == 1;
		p2p_friends_activity_text(game, sizeof(game), invite->game.state, invite->game.map, invite->game.gametype,
			invite->game.engine, invite->game.difficulty, invite->game.pc_map);
		activity_tag(game, sizeof(game), invite->game.state, entry->crossplay,
			invite->game.pc_host || invite->game.version != HALO_PORT_NETWORK_VERSION, invite->game.host_pc_build);
		entry->pc = invite->game.sender_pc;
		snprintf(entry->activity, sizeof(entry->activity), T("Invited you: %s"), game);
	}
	first_friend = count;
	for (index = 0; index < friends->friend_count && count < maximum_count; index++)
		friend_entry(&friends->friends[index], &entries[count++]);
	qsort(entries + first_friend, (size_t)(count - first_friend), sizeof(entries[0]), friend_order);
	for (index = 0; index < friends->outgoing_count && count < maximum_count; index++)
	{
		struct p2p_friend_entry *entry = &entries[count++];

		memset(entry, 0, sizeof(*entry));
		p2p_hex(friends->outgoing[index].nonce, 8, entry->id);
		entry->kind = P2P_FRIEND_KIND_OUTGOING;
		code_shown(friends->outgoing[index].code, entry->code, sizeof(entry->code));
		snprintf(entry->name, sizeof(entry->name), "%s", entry->code);
		snprintf(entry->activity, sizeof(entry->activity), "%s", T("Request sent"));
	}
	for (index = 0; index < friends->blocked_count && count < maximum_count; index++)
	{
		struct p2p_friend_entry *entry = &entries[count++];
		char code[P2P_FRIEND_CODE_LENGTH + 1];

		memset(entry, 0, sizeof(*entry));
		key_id(friends->blocked[index].key, entry->id);
		entry->kind = P2P_FRIEND_KIND_BLOCKED;
		snprintf(entry->name, sizeof(entry->name), "%s", friends->blocked[index].name[0] ? friends->blocked[index].name :
			"?");
		code_of_key(friends->blocked[index].key, code);
		code_shown(code, entry->code, sizeof(entry->code));
		snprintf(entry->activity, sizeof(entry->activity), "%s", T("Blocked"));
	}
	pthread_mutex_unlock(&friends_lock);
#ifndef HALO_RELEASE
	test_show(entries, count);
#endif
	return count;
}

int p2p_friends_count(int *online)
{
	int count, index, on = 0;

	pthread_mutex_lock(&friends_lock);
	load();
	count = friends->friend_count;
	for (index = 0; index < count; index++)
		on += friends->online && friends->friends[index].heard_time && friends->friends[index].presence.online;
	pthread_mutex_unlock(&friends_lock);
	if (online)
		*online = on;
	return count;
}

int p2p_friends_add(const char *text)
{
	char code[P2P_FRIEND_CODE_LENGTH + 1], shown[P2P_FRIEND_CODE_SIZE];
	struct outgoing_record *record;
	int index, result = P2P_FRIENDS_ADD_SENT;

	if (!code_normalize(text, code))
		return P2P_FRIENDS_ADD_NOT_A_CODE;
	pthread_mutex_lock(&friends_lock);
	load();
	code_shown(code, shown, sizeof(shown));
	if (!strcmp(code, friends->code))
		result = P2P_FRIENDS_ADD_OWN_CODE;
	else
	{
		for (index = 0; index < friends->friend_count && result == P2P_FRIENDS_ADD_SENT; index++)
		{
			char their[P2P_FRIEND_CODE_LENGTH + 1];

			code_of_key(friends->friends[index].key, their);
			if (!strcmp(their, code))
				result = P2P_FRIENDS_ADD_ALREADY_FRIENDS;
		}
		/* (their request came in already: this is a yes) */
		for (index = 0; index < friends->request_count && result == P2P_FRIENDS_ADD_SENT; index++)
		{
			char their[P2P_FRIEND_CODE_LENGTH + 1];

			code_of_key(friends->requests[index].key, their);
			if (!strcmp(their, code))
				result = P2P_FRIENDS_ADD_ACCEPTED;
		}
		if (result == P2P_FRIENDS_ADD_SENT && find_outgoing(code) >= 0)
			result = P2P_FRIENDS_ADD_ALREADY_SENT;
		if (result == P2P_FRIENDS_ADD_SENT && friends->outgoing_count >= P2P_FRIEND_OUTGOING_MAXIMUM)
			result = P2P_FRIENDS_ADD_FULL;
	}
	if (result == P2P_FRIENDS_ADD_ACCEPTED)
	{
		char id[P2P_FRIEND_ID_SIZE];

		for (index = 0; index < friends->request_count; index++)
		{
			char their[P2P_FRIEND_CODE_LENGTH + 1];

			code_of_key(friends->requests[index].key, their);
			if (!strcmp(their, code))
				break;
		}
		key_id(friends->requests[index].key, id);
		pthread_mutex_unlock(&friends_lock);
		if (p2p_friends_act(id, P2P_FRIEND_KIND_REQUEST, P2P_FRIENDS_ACCEPT) != P2P_FRIENDS_DONE)
			result = P2P_FRIENDS_ADD_FULL;
		return result;
	}
	if (result == P2P_FRIENDS_ADD_SENT)
	{
		record = &friends->outgoing[friends->outgoing_count++];
		memset(record, 0, sizeof(*record));
		memcpy(record->code, code, sizeof(record->code));
		posix_random_bytes(record->nonce, NONCE_LENGTH);
		record->time = unix_now();
		friends->dirty = 1;
		platform_log("friends: a friend request to %s%s", shown, friends_on() ? "" : " (sent once online)");
		/* (now, if online) */
		if (friends_on())
			send_request(record);
	}
	pthread_mutex_unlock(&friends_lock);
	if (result == P2P_FRIENDS_ADD_SENT)
		save(1);
	return result;
}

/* the key of an entry by its id and kind (requests, friends, blocked);
NULL if none */
static const unsigned char *entry_key(const char *id, int kind, int *index)
{
	char each[P2P_FRIEND_ID_SIZE];
	int at;

	switch (kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		for (at = 0; at < friends->request_count; at++)
		{
			key_id(friends->requests[at].key, each);
			if (!strcmp(each, id))
			{
				*index = at;
				return friends->requests[at].key;
			}
		}
		break;
	case P2P_FRIEND_KIND_FRIEND:
		for (at = 0; at < friends->friend_count; at++)
		{
			key_id(friends->friends[at].key, each);
			if (!strcmp(each, id))
			{
				*index = at;
				return friends->friends[at].key;
			}
		}
		break;
	case P2P_FRIEND_KIND_BLOCKED:
		for (at = 0; at < friends->blocked_count; at++)
		{
			key_id(friends->blocked[at].key, each);
			if (!strcmp(each, id))
			{
				*index = at;
				return friends->blocked[at].key;
			}
		}
		break;
	case P2P_FRIEND_KIND_INVITE:
		for (at = 0; at < friends->invite_count; at++)
		{
			p2p_hex(friends->invites[at].invite_id, INVITE_ID_LENGTH, each);
			if (!strcmp(each, id))
			{
				*index = at;
				return friends->invites[at].key;
			}
		}
		break;
	}
	return NULL;
}

/* blocks a key (its name kept for the list) */
static int block_key(const unsigned char *key, const char *name)
{
	int index = find_friend_index(key);

	if (find_blocked(key) >= 0)
		return P2P_FRIENDS_DONE;
	if (friends->blocked_count >= P2P_FRIEND_BLOCKED_MAXIMUM)
		return P2P_FRIENDS_FULL;
	if (index >= 0)
	{
		/* (told it is over, then never told anything again) */
		if (friends_on())
			send_remove(key, friends->friends[index].friendship);
		friend_remove_index(index);
	}
	index = find_request(key);
	if (index >= 0)
		request_remove(index);
	index = find_tombstone(key);
	if (index >= 0)
		tombstone_remove(index);
	index = find_muted(key);
	if (index >= 0)
	{
		memmove(&friends->muted[index], &friends->muted[index + 1],
			sizeof(friends->muted[0]) * (size_t)(friends->muted_count - index - 1));
		friends->muted_count--;
	}
	invites_of_remove(key);
	party_key_blocked(key);
	memcpy(friends->blocked[friends->blocked_count].key, key, P2P_KEY_SIZE);
	snprintf(friends->blocked[friends->blocked_count].name, sizeof(friends->blocked[0].name), "%s", name);
	friends->blocked_count++;
	friends->dirty = 1;
	return P2P_FRIENDS_DONE;
}

/* an invite link of a key hash and a token */
static void invite_link(const unsigned char *hash, const unsigned char *token, char *link)
{
	memcpy(link, "halo://join/", 12);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, link + 12);
	p2p_hex(token, P2P_TOKEN_SIZE, link + 12 + 2 * P2P_KEY_HASH_SIZE);
}

int p2p_friends_act(const char *id, int kind, int action)
{
	const unsigned char *key;
	unsigned char key_copy[P2P_KEY_SIZE];
	char link[P2P_LINK_SIZE], name[P2P_FRIEND_NAME_SIZE];
	int index = -1, result = P2P_FRIENDS_DONE, join = 0, changed = 0;
	/* (the wire the game joined plays: its presence's or invite's) */
	int join_wire = 0;

	if (!id)
		return P2P_FRIENDS_GONE;
	pthread_mutex_lock(&friends_lock);
	load();
	if (kind == P2P_FRIEND_KIND_OUTGOING)
	{
		for (index = 0; index < friends->outgoing_count; index++)
		{
			char each[P2P_FRIEND_ID_SIZE];

			p2p_hex(friends->outgoing[index].nonce, 8, each);
			if (!strcmp(each, id))
				break;
		}
		if (index == friends->outgoing_count)
			result = P2P_FRIENDS_GONE;
		else if (action != P2P_FRIENDS_CANCEL)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			platform_log("friends: the request to %.4s-%.4s-%.4s taken back", friends->outgoing[index].code,
				friends->outgoing[index].code + 4, friends->outgoing[index].code + 8);
			outgoing_remove(index);
			friends->dirty = changed = 1;
		}
		pthread_mutex_unlock(&friends_lock);
		if (changed)
			save(1);
		return result;
	}
	key = entry_key(id, kind, &index);
	if (!key)
	{
		pthread_mutex_unlock(&friends_lock);
		return P2P_FRIENDS_GONE;
	}
	memcpy(key_copy, key, P2P_KEY_SIZE);
	switch (action)
	{
	case P2P_FRIENDS_ACCEPT:
		if (kind != P2P_FRIEND_KIND_REQUEST)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			struct request_record request = friends->requests[index];
			unsigned char friendship[FRIENDSHIP_LENGTH];
			struct friend_record *record;
			char text[64];

			posix_random_bytes(friendship, sizeof(friendship));
			record = friend_add(request.key, request.name, friendship, 0, 0);
			if (!record)
			{
				result = P2P_FRIENDS_FULL;
				break;
			}
			/* (their epoch is learnt from their next message) */
			record->epoch = 0;
			record->counter = 0;
			record->window = 0;
			request_remove(index);
			send_accept(request.key, request.nonce, friendship);
			name_or_code(request.key, request.name, text, sizeof(text));
			platform_log("friends: accepted %s's friend request; now friends%s", text, friends_on() ? "" :
				" (they hear it once online)");
			changed = 1;
		}
		break;
	case P2P_FRIENDS_DECLINE:
		if (kind != P2P_FRIEND_KIND_REQUEST)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			muted_add(friends->requests[index].key);
			platform_log("friends: declined %s's friend request (nothing sent)", friends->requests[index].name);
			request_remove(index);
			changed = 1;
		}
		break;
	case P2P_FRIENDS_BLOCK:
		if (kind == P2P_FRIEND_KIND_BLOCKED)
			break;
		if (kind == P2P_FRIEND_KIND_REQUEST)
			snprintf(name, sizeof(name), "%s", friends->requests[index].name);
		else
		{
			const struct friend_record *record = find_friend(key_copy);

			snprintf(name, sizeof(name), "%s", record ? record->name : "");
		}
		result = block_key(key_copy, name);
		if (result == P2P_FRIENDS_DONE)
		{
			platform_log("friends: blocked %s", name[0] ? name : "a player");
			changed = 1;
		}
		break;
	case P2P_FRIENDS_UNBLOCK:
		if (kind != P2P_FRIEND_KIND_BLOCKED)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			platform_log("friends: unblocked %s", friends->blocked[index].name);
			memmove(&friends->blocked[index], &friends->blocked[index + 1],
				sizeof(friends->blocked[0]) * (size_t)(friends->blocked_count - index - 1));
			friends->blocked_count--;
			changed = 1;
		}
		break;
	case P2P_FRIENDS_REMOVE:
		if (kind != P2P_FRIEND_KIND_FRIEND)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			struct friend_record *record = &friends->friends[index];

			platform_log("friends: removed %s", record->name);
			tombstone_add(record->key, record->friendship);
			if (friends_on())
				send_remove(record->key, record->friendship);
			friend_remove_index(index);
			changed = 1;
		}
		break;
	case P2P_FRIENDS_JOIN:
		if (kind != P2P_FRIEND_KIND_FRIEND)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else if (!friends_on())
			result = P2P_FRIENDS_OFFLINE;
		else
		{
			struct p2p_friend_entry entry;

			friend_entry(&friends->friends[index], &entry);
			if (!entry.joinable)
				result = P2P_FRIENDS_NOT_JOINABLE;
			/* (a game with a password: the player types it, then
			p2p_friends_join_password) */
			else if (entry.locked)
				result = P2P_FRIENDS_PASSWORD;
			else
			{
				invite_link(friends->friends[index].presence.invite_hash, friends->friends[index].presence.invite_token,
					link);
				snprintf(name, sizeof(name), "%s", friends->friends[index].name);
				join_wire = game_wire_of(friends->friends[index].presence.version, friends->friends[index].presence.wire);
				join = 1;
			}
		}
		break;
	case P2P_FRIENDS_INVITE:
		if (kind != P2P_FRIEND_KIND_FRIEND)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else if (!friends_on())
			result = P2P_FRIENDS_OFFLINE;
		else if (!own_invitable())
			result = P2P_FRIENDS_NOT_JOINABLE;
		else if (friends->friends[index].invite_sent_time &&
			!elapsed(friends->friends[index].invite_sent_time, INVITE_INTERVAL))
		{
			result = P2P_FRIENDS_TOO_SOON;
		}
		else
		{
			struct friend_record *record = &friends->friends[index];
			const struct activity *activity = &friends->activity;
			struct message message;

			message_start(&message, TYPE_INVITE, record->key);
			memcpy(message.friendship, record->friendship, FRIENDSHIP_LENGTH);
			posix_random_bytes(message.invite_id, INVITE_ID_LENGTH);
			message.game.state = activity->state == P2P_FRIEND_STATE_COOP ? P2P_FRIEND_STATE_COOP :
				P2P_FRIEND_STATE_MULTIPLAYER;
			message.game.engine = activity->engine & 15;
			message.game.difficulty = activity->difficulty;
			message.game.pc_map = activity->pc_map;
			message.game.version = (int)network_wire_version_of(friends->host.crossplay);
			message.game.wire = friends->host.crossplay;
			message.game.pc_host = friends->host.pc_host;
			own_platform_marks(&message.game);
		message.game.pc_host = friends->host.pc_host;
			snprintf(message.game.map, sizeof(message.game.map), "%s", activity->map);
			snprintf(message.game.gametype, sizeof(message.game.gametype), "%s", activity->gametype);
			memcpy(message.game.invite_hash, friends->host.key_hash, P2P_KEY_HASH_SIZE);
			memcpy(message.game.invite_token, friends->host.token, P2P_TOKEN_SIZE);
			if (!send_pair(record, &message))
				result = P2P_FRIENDS_FULL;
			else
			{
				record->invite_sent_time = now_stamp();
				platform_log("friends: invited %s into this machine's game", record->name);
			}
		}
		break;
	case P2P_FRIENDS_INVITE_ACCEPT:
	case P2P_FRIENDS_INVITE_IGNORE:
		if (kind != P2P_FRIEND_KIND_INVITE)
			result = P2P_FRIENDS_NOT_ALLOWED;
		else
		{
			struct invite_record invite = friends->invites[index];
			const struct friend_record *record = find_friend(invite.key);

			invite_remove(index);
			snprintf(name, sizeof(name), "%s", record ? record->name : "?");
			if (action == P2P_FRIENDS_INVITE_IGNORE)
				platform_log("friends: ignored %s's invite", name);
			else if (!friends_on())
				result = P2P_FRIENDS_OFFLINE;
			else if (!game_wire_joinable(invite.game.version, invite.game.wire))
				result = P2P_FRIENDS_NOT_JOINABLE;
			else
			{
				invite_link(invite.game.invite_hash, invite.game.invite_token, link);
				join_wire = game_wire_of(invite.game.version, invite.game.wire);
				join = 1;
			}
		}
		break;
	default:
		result = P2P_FRIENDS_NOT_ALLOWED;
		break;
	}
	if (changed)
		friends->dirty = 1;
	pthread_mutex_unlock(&friends_lock);
	if (changed)
		save(1);
	if (join)
	{
		/* (the invite's own join, as a link pasted joins it: the host's checks
		are where they always are) */
		platform_log("friends: joining %s's game (%s)", name, join_wire ? "Vitas and PC" : "Vitas");
		if (!p2p_join_invite_on(link, join_wire))
			result = P2P_FRIENDS_OFFLINE;
	}
	return result;
}

int p2p_friends_join_password(const char *id, const char *password)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], signing_key[P2P_KEY_SIZE], sealed[P2P_SEALED_TOKEN_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	int index = -1, result = P2P_FRIENDS_DONE, wire = 0;

	if (!id)
		return P2P_FRIENDS_GONE;
	pthread_mutex_lock(&friends_lock);
	load();
	if (!entry_key(id, P2P_FRIEND_KIND_FRIEND, &index) || index < 0)
		result = P2P_FRIENDS_GONE;
	else if (!friends_on())
		result = P2P_FRIENDS_OFFLINE;
	else
	{
		struct p2p_friend_entry entry;

		friend_entry(&friends->friends[index], &entry);
		if (!entry.joinable || !entry.locked)
			result = P2P_FRIENDS_NOT_JOINABLE;
		else
		{
			memcpy(hash, friends->friends[index].presence.invite_hash, sizeof(hash));
			memcpy(signing_key, friends->friends[index].presence.signing_key, sizeof(signing_key));
			memcpy(sealed, friends->friends[index].presence.sealed_token, sizeof(sealed));
			wire = game_wire_of(friends->friends[index].presence.version, friends->friends[index].presence.wire);
			snprintf(name, sizeof(name), "%s", friends->friends[index].name);
		}
	}
	pthread_mutex_unlock(&friends_lock);
	if (result != P2P_FRIENDS_DONE)
		return result;
	/* (the password's key on the lobby's thread, the sealed token opened,
	its invite joined as any; a wrong password: p2p_lobby_join_state) */
	platform_log("friends: joining %s's game with its password (%s)", name, wire ? "Vitas and PC" : "Vitas");
	return p2p_lobby_join_sealed(hash, signing_key, sealed, password, wire) ? P2P_FRIENDS_DONE : P2P_FRIENDS_NOT_JOINABLE;
}

const char *p2p_friends_outcome_text(int outcome)
{
	switch (outcome)
	{
	case P2P_FRIENDS_PASSWORD:
		return T("Their game has a password: type it");
	case P2P_FRIENDS_DONE:
		return T("Done");
	case P2P_FRIENDS_GONE:
		return T("That's gone");
	case P2P_FRIENDS_NOT_ALLOWED:
		return T("Not for this one");
	case P2P_FRIENDS_OFFLINE:
		return T("Go online first");
	case P2P_FRIENDS_NOT_JOINABLE:
		return T("Their game can't be joined now");
	case P2P_FRIENDS_TOO_SOON:
		return T("Invited a moment ago");
	case P2P_FRIENDS_FULL:
		return T("The list is full");
	case P2P_FRIENDS_APPEARING_OFFLINE:
		return T("You're appearing offline");
	}
	return "";
}

const char *p2p_friends_add_text(int result)
{
	switch (result)
	{
	case P2P_FRIENDS_ADD_SENT:
		return T("Friend request sent");
	case P2P_FRIENDS_ADD_NOT_A_CODE:
		return T("That's not a friend code");
	case P2P_FRIENDS_ADD_OWN_CODE:
		return T("That's your own code");
	case P2P_FRIENDS_ADD_ALREADY_FRIENDS:
		return T("Already friends");
	case P2P_FRIENDS_ADD_ALREADY_SENT:
		return T("Request already sent");
	case P2P_FRIENDS_ADD_FULL:
		return T("The list is full");
	case P2P_FRIENDS_ADD_ACCEPTED:
		return T("They asked first: now friends");
	}
	return "";
}

int p2p_friends_take_invite(struct p2p_friend_entry *entry)
{
	int index, found = 0;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = 0; index < friends->invite_count && !found; index++)
	{
		struct invite_record *invite = &friends->invites[index];
		const struct friend_record *record = find_friend(invite->key);
		char game[P2P_FRIEND_ACTIVITY_SIZE];

		if (invite->asked || !record)
			continue;
		invite->asked = 1;
		found = 1;
		memset(entry, 0, sizeof(*entry));
		p2p_hex(invite->invite_id, INVITE_ID_LENGTH, entry->id);
		entry->kind = P2P_FRIEND_KIND_INVITE;
		snprintf(entry->name, sizeof(entry->name), "%s", record->name);
		entry->online = 1;
		entry->state = invite->game.state;
		p2p_friends_activity_text(game, sizeof(game), invite->game.state, invite->game.map, invite->game.gametype,
			invite->game.engine, invite->game.difficulty, invite->game.pc_map);
		snprintf(entry->activity, sizeof(entry->activity), "%s", game);
	}
	pthread_mutex_unlock(&friends_lock);
	return found;
}

int p2p_friends_status(char *text, int size)
{
	int count, online = 0, index, on;

	pthread_mutex_lock(&friends_lock);
	load();
	on = friends_on();
	count = friends->friend_count;
	for (index = 0; index < count; index++)
		online += on && friends->friends[index].heard_time && friends->friends[index].presence.online;
	if (!setting_on("friends.enabled", "HALO_FRIENDS"))
		snprintf(text, (size_t)size, "%s", T("Friends are off"));
	else if (!on)
		snprintf(text, (size_t)size, "%s", T("Go online to see your friends"));
	else if (friends->appear_offline)
		snprintf(text, (size_t)size, T("Appearing offline: %d of %d friends online"), online, count);
	else if (!count)
		snprintf(text, (size_t)size, "%s", T("No friends yet: add one with their code"));
	else
		snprintf(text, (size_t)size, T("%d of %d friends online"), online, count);
	pthread_mutex_unlock(&friends_lock);
	return on;
}

void p2p_friends_set_name(const char *name)
{
	char cleaned[P2P_FRIEND_NAME_SIZE];

	if (!name || !clean_text(cleaned, NAME_LENGTH, (const unsigned char *)name, (int)strlen(name), 1))
		cleaned[0] = 0;
	pthread_mutex_lock(&friends_lock);
	if (strcmp(cleaned, friends->name))
	{
		memcpy(friends->name, cleaned, sizeof(cleaned));
		friends->activity_changed = 1;
	}
	pthread_mutex_unlock(&friends_lock);
}

void p2p_friends_set_activity(int state, const char *map, const char *gametype, int engine, int difficulty,
	int pc_map, int players, int maximum)
{
	struct activity activity;

	memset(&activity, 0, sizeof(activity));
	activity.state = state > P2P_FRIEND_STATE_OFFLINE && state < P2P_FRIEND_STATE_COUNT ? state : P2P_FRIEND_STATE_MENUS;
	clean_text(activity.map, MAP_LENGTH, (const unsigned char *)(map ? map : ""), map ? (int)strlen(map) : 0, 0);
	clean_text(activity.gametype, GAMETYPE_LENGTH, (const unsigned char *)(gametype ? gametype : ""),
		gametype ? (int)strlen(gametype) : 0, 0);
	activity.engine = engine >= 0 && engine < 16 ? engine : 0;
	activity.difficulty = difficulty >= 0 && difficulty < 4 ? difficulty : -1;
	activity.pc_map = pc_map ? 1 : 0;
	activity.maximum = maximum >= 0 && maximum <= HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? maximum : 0;
	activity.players = players >= 0 && players <= activity.maximum ? players : 0;
	pthread_mutex_lock(&friends_lock);
	if (memcmp(&activity, &friends->activity, sizeof(activity)))
	{
		friends->activity = activity;
		friends->activity_changed = 1;
	}
	pthread_mutex_unlock(&friends_lock);
}

/* ---------- the party (p2p_party.h) */

#include "p2p_party.inc"
