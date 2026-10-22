/*
P2P_PARTY.INC

The party (p2p_party.h), part of p2p_friends.c, which includes it at its
end: it shares friends' identity, keys, envelopes, signatures, queue and
lock (docs/superpowers/specs/2026-10-18-friends-design.md has those).

- Messages (friends' pair messages, kind P, sealed per pair and signed by
  the sender's friend identity): INVITE and ANSWER between the leader and a
  friend (to that one key); ROSTER, LEAVE, STATUS, FOLLOW and CHAT among the
  members (to none: signed once, sealed for each member); PRESENCE to the
  friends (the party's leader and size, which their lists show).
- The members who are not this machine's friends are known by the leader's
  roster: their keys open their pair messages (the pair's key needs only the
  two keys), their mailbox topic is their key's. Nobody else reads anything.
- Replays: every party message's (epoch, counter) is taken once per sender
  (party_seen, whatever their membership); an invite also no older than the
  friend's epoch kept in the friends file. A roster is taken only from the
  leader (or, the leader gone, the longest-standing member: the hand-off)
  with a serial above the last; a follow from the leader with a serial above
  the last; a member's chat and status only from a member, within budgets.
- The leader moves the party: hosting, its own invite (never with a
  password: the members are told the game has one); joined to another's
  game through internet play, that game's invite (never a locked game's).
  A member joins it through the game's own join, whose checks stay where
  they are.

Threat model (as friends'): protected - who is in a party and what it says,
from everyone but its members (sealed per pair, hashed topics, nothing
retained); impersonation (every message signed by its sender's identity; a
member not a friend shown with their code's characters, never by a name
alone); a member taking the lead (a roster only from the leader, or from the
longest-standing member once the leader is silent a minute: never over a
leader still heard); a forced join (a follow is the leader's alone and goes
through the game's own join and its checks; a password's invite is never
passed on); floods (budgets per member for chat and status, this machine's
own chat paced; every length checked; chat cleaned to printable ASCII
without markup). Not protected (stated): a member sees the others' keys and
self-chosen names (as a game's players see names); traffic analysis of the
brokers (who publishes where, when); no forward secrecy (friends'
static-static pair keys); a hostile broker can drop messages (members then
look gone). Appearing offline, nothing of a party is sent or taken.
Old builds hear a party presence as a malformed message (counted, dropped).

Everything here runs with friends_lock held (the public calls take it).
*/

enum
{
	PARTY_STATUS_IN_GAME = 1,
	PARTY_STATUS_HOSTING = 2,
	PARTY_STATUS_FLAGS = 3,

	PARTY_FOLLOW_HOSTING = 1,
	PARTY_FOLLOW_HAS_INVITE = 2,
	PARTY_FOLLOW_LOCKED = 4,
	PARTY_FOLLOW_UNREACHABLE = 8,
	/* the leader back at the menus from a game (nothing to join; never with
	another flag; an older build drops it as malformed) */
	PARTY_FOLLOW_MENUS = 16,
	PARTY_FOLLOW_FLAGS = 31,

	PARTY_LEAVE_LEFT = 0,
	PARTY_LEAVE_REMOVED = 1,

	PARTY_CHAT_TEXT = 0,
	PARTY_CHAT_PRESET = 1,

	/* milliseconds */
	PARTY_STATUS_INTERVAL = 20000,
	PARTY_ROSTER_INTERVAL = 30000,
	PARTY_FOLLOW_INTERVAL = 30000,
	PARTY_TIMEOUT = 70000,
	PARTY_INVITE_LIFETIME = 120000,
	PARTY_INVITE_INTERVAL = 10000,
	PARTY_INVITE_HEARD_INTERVAL = 5000,
	PARTY_FOLLOW_LIFETIME = 600000,
	/* the leader in a game no member reaches (system link, a game being
	closed): said once it has lasted this long, never for the moment the
	game's client outlives the hosted game it closed (insider.21: members
	told "can't be reached online" each time the leader left the lobby) */
	PARTY_UNREACHABLE_SETTLE = 3000,
	PARTY_PRESENCE_INTERVAL = 60000,
	PARTY_PRESENCE_TIMEOUT = 150000,
	/* a member's chat taken: a few at once, one more each interval; this
	machine's own sent likewise */
	PARTY_CHAT_BUDGET = 5,
	PARTY_CHAT_BUDGET_INTERVAL = 2000,
	PARTY_CHAT_SEND_BUDGET = 3,
	PARTY_CHAT_SEND_INTERVAL = 1500,
	PARTY_STATUS_BUDGET = 4,
	PARTY_STATUS_BUDGET_INTERVAL = 1000,
	PARTY_SEEN = 32,
};

static const char *const party_presets[P2P_PARTY_PRESETS] = {
	N_("Ready!"), N_("Wait for me"), N_("Let's go"), N_("Good game"),
	N_("Join me"), N_("Back in a minute"), N_("Yes"), N_("No"),
};

/* ---------- small helpers */

static int party_self_index(void)
{
	int index;

	for (index = 0; index < friends->party.count; index++)
		if (p2p_equal(friends->party.members[index].key, friends->key, P2P_KEY_SIZE))
			return index;
	return -1;
}

static int party_leading_now(void)
{
	return friends->party.active && friends->party.leader == party_self_index();
}

static struct party_member *party_member_by_key(const unsigned char *key)
{
	int index;

	for (index = 0; friends->party.active && index < friends->party.count; index++)
		if (p2p_equal(friends->party.members[index].key, key, P2P_KEY_SIZE))
			return &friends->party.members[index];
	return NULL;
}

static int party_member_index(const unsigned char *key)
{
	struct party_member *member = party_member_by_key(key);

	return member ? (int)(member - friends->party.members) : -1;
}

/* a member's pair keys (one not a friend's: friends' records keep theirs) */
static int party_member_keys(struct party_member *member)
{
	if (!member->keys_state)
		member->keys_state = pair_keys(member->key, member->send_key, member->receive_key) ? 1 : -1;
	return member->keys_state > 0;
}

/* a member's name as shown: this machine's own, a friend's as the friends
list keeps it, another's own name with their code's first four characters
(no one passes for a friend by a name), a blocked one's said so */
static void party_name_of(const unsigned char *key, const char *own, char *text, int size)
{
	const struct friend_record *record;
	char code[P2P_FRIEND_CODE_LENGTH + 1];

	if (p2p_equal(key, friends->key, P2P_KEY_SIZE))
	{
		char name[P2P_FRIEND_NAME_SIZE];

		own_name(name);
		snprintf(text, (size_t)size, "%s", name);
		return;
	}
	if (find_blocked(key) >= 0)
	{
		snprintf(text, (size_t)size, "%s", T("Blocked player"));
		return;
	}
	record = find_friend(key);
	if (record)
	{
		snprintf(text, (size_t)size, "%s", record->name);
		return;
	}
	code_of_key(key, code);
	if (own && own[0])
		snprintf(text, (size_t)size, "%s #%.4s", own, code);
	else
		snprintf(text, (size_t)size, "#%.4s-%.4s", code, code + 4);
}

static void party_member_name(const struct party_member *member, char *text, int size)
{
	party_name_of(member->key, member->name, text, size);
}

/* a chat line kept (the oldest goes) */
static void party_chat_add(const unsigned char *key, const char *name, const char *text, int self, int notice)
{
	struct party_chat_record *line;

	if (friends->chat_count >= P2P_PARTY_CHAT_LINES)
	{
		memmove(&friends->chat[0], &friends->chat[1], sizeof(friends->chat[0]) * (P2P_PARTY_CHAT_LINES - 1));
		friends->chat_count--;
	}
	line = &friends->chat[friends->chat_count++];
	memset(line, 0, sizeof(*line));
	line->number = ++friends->chat_number;
	if (key)
		memcpy(line->key, key, P2P_KEY_SIZE);
	snprintf(line->text, sizeof(line->text), "%s", text);
	line->self = self;
	line->notice = notice;
	(void)name;
}

/* the party's word in the chat (a member joined, left, leads) and the log */
static void party_notice(const char *format, ...)
{
	char text[P2P_PARTY_CHAT_SIZE + 64];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	party_chat_add(NULL, "", text, 0, 1);
	platform_log("party: %s", text);
}

int p2p_party_chat_clean(char *destination, int size, const char *source)
{
	int length = 0, space = 0;

	if (size <= 0)
		return 0;
	for (; source && *source && length < size - 1 && length < P2P_PARTY_CHAT_LENGTH; source++)
	{
		unsigned char character = (unsigned char)*source;

		/* (printable ASCII, which every font of the game and the panel draws;
		nothing a text field reads as markup or a format: no '|', '%', '^',
		'<', '>', '\\' or '`'; runs of spaces made one, none first) */
		if (character < 0x20 || character > 0x7E || strchr("|%^<>\\`", character))
			continue;
		if (character == ' ')
		{
			if (!length || space)
				continue;
			space = 1;
		}
		else
			space = 0;
		destination[length++] = (char)character;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	return length;
}

/* a party message's replay: 1 new (taken), 0 a copy of one taken (the same
message through another broker: every message goes out on each), -1 older
than those kept (a replay) */
static int party_replay_check(const unsigned char *key, unsigned long long epoch, unsigned long counter)
{
	struct party_seen *seen = NULL, *oldest = &friends->party_seen[0];
	int index;

	for (index = 0; index < PARTY_SEEN && !seen; index++)
	{
		struct party_seen *each = &friends->party_seen[index];

		if (each->time && p2p_equal(each->key, key, P2P_KEY_SIZE))
			seen = each;
		else if (!each->time || (oldest->time && (long)(each->time - oldest->time) < 0))
			oldest = each;
	}
	if (!seen)
	{
		seen = oldest;
		memset(seen, 0, sizeof(*seen));
		memcpy(seen->key, key, P2P_KEY_SIZE);
		seen->epoch = epoch;
		seen->counter = counter;
		seen->window = 1;
		seen->time = now_stamp();
		return 1;
	}
	seen->time = now_stamp();
	if (epoch < seen->epoch)
		return -1;
	if (epoch > seen->epoch)
	{
		seen->epoch = epoch;
		seen->counter = counter;
		seen->window = 1;
		return 1;
	}
	if (counter > seen->counter)
	{
		unsigned long shift = counter - seen->counter;

		seen->window = shift >= REPLAY_WINDOW ? 1 : seen->window << shift | 1;
		seen->counter = counter;
		return 1;
	}
	if (seen->counter - counter >= REPLAY_WINDOW)
		return -1;
	if (seen->window >> (seen->counter - counter) & 1)
		return 0;
	seen->window |= 1ULL << (seen->counter - counter);
	return 1;
}

/* ---------- the messages' bodies */

static int party_body_encode(const struct message *message, unsigned char *bytes)
{
	int size = 0, index;

	if (message->type != TYPE_PARTY_PRESENCE)
	{
		memcpy(bytes, message->party_id, PARTY_ID_LENGTH);
		size = PARTY_ID_LENGTH;
	}
	switch (message->type)
	{
	case TYPE_PARTY_INVITE:
		bytes[size++] = (unsigned char)message->party_count;
		break;
	case TYPE_PARTY_ANSWER:
		bytes[size++] = (unsigned char)message->party_value;
		break;
	case TYPE_PARTY_ROSTER:
		put_u32(bytes + size, message->party_serial);
		size += 4;
		bytes[size++] = (unsigned char)message->party_count;
		bytes[size++] = (unsigned char)message->party_leader;
		for (index = 0; index < message->party_count && index < P2P_PARTY_MAXIMUM; index++)
		{
			memcpy(bytes + size, message->party_keys[index], P2P_KEY_SIZE);
			size += P2P_KEY_SIZE;
		}
		break;
	case TYPE_PARTY_LEAVE:
		put_u32(bytes + size, message->party_serial);
		size += 4;
		bytes[size++] = (unsigned char)message->party_value;
		memcpy(bytes + size, message->party_keys[0], P2P_KEY_SIZE);
		size += P2P_KEY_SIZE;
		break;
	case TYPE_PARTY_STATUS:
		bytes[size++] = (unsigned char)message->party_value;
		bytes[size++] = (unsigned char)message->game.state;
		bytes[size++] = (unsigned char)message->party_code;
		put_u32(bytes + size, message->party_follow);
		size += 4;
		size += put_text(bytes + size, message->name, NAME_LENGTH);
		break;
	case TYPE_PARTY_FOLLOW:
		put_u32(bytes + size, message->party_serial);
		size += 4;
		bytes[size++] = (unsigned char)message->party_value;
		bytes[size++] = (unsigned char)message->game.state;
		bytes[size++] = (unsigned char)message->game.engine;
		bytes[size++] = (unsigned char)(message->game.difficulty < 0 ? 0xFF : message->game.difficulty);
		bytes[size++] = (unsigned char)message->game.players;
		bytes[size++] = (unsigned char)message->game.maximum;
		put_u16(bytes + size, (unsigned int)message->game.version);
		size += 2;
		bytes[size++] = game_flags(&message->game);
		size += put_text(bytes + size, message->game.map, MAP_LENGTH);
		size += put_text(bytes + size, message->game.gametype, GAMETYPE_LENGTH);
		if (message->party_value & PARTY_FOLLOW_HAS_INVITE)
		{
			memcpy(bytes + size, message->game.invite_hash, P2P_KEY_HASH_SIZE);
			size += P2P_KEY_HASH_SIZE;
			memcpy(bytes + size, message->game.invite_token, P2P_TOKEN_SIZE);
			size += P2P_TOKEN_SIZE;
		}
		break;
	case TYPE_PARTY_CHAT:
		bytes[size++] = (unsigned char)message->party_code;
		bytes[size++] = (unsigned char)message->party_value;
		size += put_text(bytes + size, message->chat, P2P_PARTY_CHAT_LENGTH);
		break;
	case TYPE_PARTY_PRESENCE:
		memcpy(bytes, message->party_keys[0], P2P_KEY_SIZE);
		size = P2P_KEY_SIZE;
		bytes[size++] = (unsigned char)message->party_count;
		break;
	}
	return size;
}

/* a party message's body read and checked, whole (the header is read):
0 if it is not one */
static int party_body_decode(struct message *message, const unsigned char *bytes, int offset, int body_end)
{
	int index;

	if (message->type != TYPE_PARTY_PRESENCE)
	{
		if (offset + PARTY_ID_LENGTH > body_end)
			return 0;
		memcpy(message->party_id, bytes + offset, PARTY_ID_LENGTH);
		offset += PARTY_ID_LENGTH;
	}
	switch (message->type)
	{
	case TYPE_PARTY_INVITE:
		if (offset + 1 != body_end)
			return 0;
		message->party_count = bytes[offset];
		return message->party_count >= 1 && message->party_count < P2P_PARTY_MAXIMUM;
	case TYPE_PARTY_ANSWER:
		if (offset + 1 != body_end || bytes[offset] > 1)
			return 0;
		message->party_value = bytes[offset];
		return 1;
	case TYPE_PARTY_ROSTER:
		if (offset + 6 > body_end)
			return 0;
		message->party_serial = get_u32(bytes + offset);
		message->party_count = bytes[offset + 4];
		message->party_leader = bytes[offset + 5];
		offset += 6;
		if (message->party_count < 1 || message->party_count > P2P_PARTY_MAXIMUM ||
			message->party_leader >= message->party_count ||
			offset + message->party_count * P2P_KEY_SIZE != body_end)
		{
			return 0;
		}
		for (index = 0; index < message->party_count; index++)
			memcpy(message->party_keys[index], bytes + offset + index * P2P_KEY_SIZE, P2P_KEY_SIZE);
		return 1;
	case TYPE_PARTY_LEAVE:
		if (offset + 5 + P2P_KEY_SIZE != body_end || bytes[offset + 4] > PARTY_LEAVE_REMOVED)
			return 0;
		message->party_serial = get_u32(bytes + offset);
		message->party_value = bytes[offset + 4];
		memcpy(message->party_keys[0], bytes + offset + 5, P2P_KEY_SIZE);
		return 1;
	case TYPE_PARTY_STATUS:
		if (offset + 7 > body_end)
			return 0;
		message->party_value = bytes[offset];
		message->game.state = bytes[offset + 1];
		message->party_code = bytes[offset + 2];
		message->party_follow = get_u32(bytes + offset + 3);
		offset += 7;
		if ((message->party_value & ~PARTY_STATUS_FLAGS) || message->game.state >= P2P_FRIEND_STATE_COUNT ||
			message->party_code >= P2P_PARTY_FOLLOW_COUNT)
		{
			return 0;
		}
		if (!get_text(bytes, body_end, &offset, message->name, NAME_LENGTH, 1) || offset != body_end)
			return 0;
		return 1;
	case TYPE_PARTY_FOLLOW:
		if (offset + 5 > body_end)
			return 0;
		message->party_serial = get_u32(bytes + offset);
		message->party_value = bytes[offset + 4];
		offset += 5;
		if ((message->party_value & ~PARTY_FOLLOW_FLAGS) ||
			((message->party_value & PARTY_FOLLOW_HAS_INVITE) &&
			(message->party_value & (PARTY_FOLLOW_LOCKED | PARTY_FOLLOW_UNREACHABLE))) ||
			((message->party_value & PARTY_FOLLOW_MENUS) && message->party_value != PARTY_FOLLOW_MENUS))
		{
			return 0;
		}
		if (!game_decode(bytes, body_end, &offset, &message->game, 1) ||
			!get_text(bytes, body_end, &offset, message->game.map, MAP_LENGTH, 0) ||
			!get_text(bytes, body_end, &offset, message->game.gametype, GAMETYPE_LENGTH, 0))
		{
			return 0;
		}
		if (message->party_value & PARTY_FOLLOW_HAS_INVITE)
		{
			if (offset + P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE != body_end)
				return 0;
			memcpy(message->game.invite_hash, bytes + offset, P2P_KEY_HASH_SIZE);
			memcpy(message->game.invite_token, bytes + offset + P2P_KEY_HASH_SIZE, P2P_TOKEN_SIZE);
		}
		else if (offset != body_end)
			return 0;
		return 1;
	case TYPE_PARTY_CHAT:
	{
		int length;

		if (offset + 3 > body_end)
			return 0;
		message->party_code = bytes[offset];
		message->party_value = bytes[offset + 1];
		length = bytes[offset + 2];
		offset += 3;
		if (length > P2P_PARTY_CHAT_LENGTH || offset + length != body_end)
			return 0;
		if (message->party_code == PARTY_CHAT_PRESET)
			return message->party_value < P2P_PARTY_PRESETS && length == 0;
		if (message->party_code != PARTY_CHAT_TEXT || message->party_value != 0 || !length)
			return 0;
		{
			char raw[P2P_PARTY_CHAT_SIZE];

			memcpy(raw, bytes + offset, (size_t)length);
			raw[length] = 0;
			/* (a 0 inside, or nothing left once cleaned: not a message) */
			if ((int)strlen(raw) != length || !p2p_party_chat_clean(message->chat, sizeof(message->chat), raw))
				return 0;
		}
		return 1;
	}
	case TYPE_PARTY_PRESENCE:
		if (offset + P2P_KEY_SIZE + 1 != body_end || bytes[offset + P2P_KEY_SIZE] > P2P_PARTY_MAXIMUM)
			return 0;
		memcpy(message->party_keys[0], bytes + offset, P2P_KEY_SIZE);
		message->party_count = bytes[offset + P2P_KEY_SIZE];
		/* (no party: no leader; a party: of two at least) */
		return message->party_count == 0 ? key_zero(message->party_keys[0]) : message->party_count >= 2;
	}
	return 0;
}

/* ---------- sending */

/* a plaintext sealed for one key (a friend's, or a member's) and queued */
static int party_seal_to(const unsigned char *key, const unsigned char *plain, int size)
{
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	char topic[TOPIC_LENGTH];
	struct friend_record *record;
	const unsigned char *send_key;

	/* (a blocked player is never told anything) */
	if (find_blocked(key) >= 0 || p2p_equal(key, friends->key, P2P_KEY_SIZE))
		return 0;
	record = find_friend(key);
	if (record)
	{
		if (!friend_keys(record))
			return 0;
		send_key = record->send_key;
	}
	else
	{
		struct party_member *member = party_member_by_key(key);

		if (!member || !party_member_keys(member))
			return 0;
		send_key = member->send_key;
	}
	size = envelope_seal(KIND_PAIR, send_key, plain, size, payload);
	mailbox_topic(key, topic);
	return outbound_add(topic, payload, size, 0, key) != NULL;
}

/* a message to one key (signed with it as the recipient) */
static int party_send_to(const unsigned char *key, struct message *message)
{
	unsigned char plain[MAXIMUM_PLAIN_SIZE];
	int size = plain_encode(message, plain);

	return size && party_seal_to(key, plain, size);
}

/* a message to every member but this machine (signed once, no recipient) */
static void party_broadcast(struct message *message)
{
	unsigned char plain[MAXIMUM_PLAIN_SIZE];
	int size = plain_encode(message, plain), index;

	if (!size)
		return;
	for (index = 0; index < friends->party.count; index++)
		party_seal_to(friends->party.members[index].key, plain, size);
}

static void party_start_message(struct message *message, int type, const unsigned char *recipient)
{
	message_start(message, type, recipient);
	memcpy(message->party_id, friends->party.id, PARTY_ID_LENGTH);
}

static void party_send_roster(void)
{
	struct message message;
	int index;

	party_start_message(&message, TYPE_PARTY_ROSTER, NULL);
	message.party_serial = friends->party.serial;
	message.party_count = friends->party.count;
	message.party_leader = friends->party.leader;
	for (index = 0; index < friends->party.count; index++)
		memcpy(message.party_keys[index], friends->party.members[index].key, P2P_KEY_SIZE);
	party_broadcast(&message);
	friends->party.roster_time = now_stamp();
}

static int party_status_flags(void)
{
	return (friends->in_game ? PARTY_STATUS_IN_GAME : 0) | (friends->host.hosting ? PARTY_STATUS_HOSTING : 0);
}

static void party_send_status(void)
{
	struct message message;

	party_start_message(&message, TYPE_PARTY_STATUS, NULL);
	message.party_value = party_status_flags();
	message.game.state = friends->activity.state ? friends->activity.state : P2P_FRIEND_STATE_MENUS;
	message.party_code = friends->party.follow_result;
	message.party_follow = friends->party.follow_done ? friends->party.follow_done : friends->party.follow_heard;
	own_name(message.name);
	party_broadcast(&message);
	friends->party.status_flags = message.party_value;
	friends->party.status_due = 0;
	friends->party.status_time = now_stamp();
}

static void party_send_follow(void)
{
	struct message message;
	const struct activity *activity = &friends->activity;

	party_start_message(&message, TYPE_PARTY_FOLLOW, NULL);
	message.party_serial = friends->party.follow_serial;
	message.party_value = friends->party.follow_flags;
	message.game.state = activity->state ? activity->state : P2P_FRIEND_STATE_MULTIPLAYER;
	/* (a game to join while the activity still says the menus - the game's
	activity comes each second: a member told "hosts a game: In the menus") */
	if ((message.party_value & PARTY_FOLLOW_HAS_INVITE) && message.game.state == P2P_FRIEND_STATE_MENUS)
		message.game.state = P2P_FRIEND_STATE_MULTIPLAYER;
	if (message.party_value & PARTY_FOLLOW_MENUS)
		message.game.state = P2P_FRIEND_STATE_MENUS;
	message.game.engine = activity->engine & 15;
	message.game.difficulty = activity->difficulty;
	message.game.players = activity->players;
	message.game.maximum = activity->maximum;
	if (message.game.players > message.game.maximum)
		message.game.players = message.game.maximum;
	message.game.version = (int)network_wire_version_of(friends->host.crossplay);
	message.game.wire = friends->host.crossplay;
	message.game.pc_host = friends->host.pc_host;
	own_platform_marks(&message.game);
	message.game.pc_map = activity->pc_map;
	snprintf(message.game.map, sizeof(message.game.map), "%s", activity->map);
	snprintf(message.game.gametype, sizeof(message.game.gametype), "%s", activity->gametype);
	if (message.party_value & PARTY_FOLLOW_HAS_INVITE)
	{
		memcpy(message.game.invite_hash, friends->party.follow_hash, P2P_KEY_HASH_SIZE);
		memcpy(message.game.invite_token, friends->party.follow_token, P2P_TOKEN_SIZE);
	}
	party_broadcast(&message);
	friends->party.follow_time = now_stamp();
}

/* the party's presence to the friends online (its leader and size; none: a
party left), signed once */
static void party_send_presence(void)
{
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE];
	int size, index;

	message_start(&message, TYPE_PARTY_PRESENCE, NULL);
	message.party_count = friends->party.active && friends->party.count >= 2 ? friends->party.count : 0;
	if (message.party_count)
		memcpy(message.party_keys[0], friends->party.members[friends->party.leader].key, P2P_KEY_SIZE);
	size = plain_encode(&message, plain);
	friends->party.presence_size = message.party_count;
	memcpy(friends->party.presence_leader, message.party_keys[0], P2P_KEY_SIZE);
	friends->party.presence_time = now_stamp();
	friends->party.presence_due = 0;
	if (!size)
		return;
	for (index = 0; index < friends->friend_count; index++)
	{
		const struct friend_record *record = &friends->friends[index];

		if (record->heard_time && record->presence.online && !party_member_by_key(record->key))
			party_seal_to(record->key, plain, size);
	}
}

/* ---------- the roster */

/* no party (what the friends were last told of it kept: they are told
it is over) */
static void party_clear(void)
{
	int presence_size = friends->party.presence_size;
	unsigned char presence_leader[P2P_KEY_SIZE];
	unsigned long presence_time = friends->party.presence_time;

	memcpy(presence_leader, friends->party.presence_leader, P2P_KEY_SIZE);
	memset(&friends->party, 0, sizeof(friends->party));
	friends->party.presence_size = presence_size;
	memcpy(friends->party.presence_leader, presence_leader, P2P_KEY_SIZE);
	friends->party.presence_time = presence_time;
	friends->party.presence_due = 1;
}

/* this machine out of its party: its members told (tell), then nothing */
static void party_leave_now(int tell, const char *why)
{
	if (!friends->party.active)
		return;
	if (tell && friends->party.count > 1)
	{
		struct message message;

		party_start_message(&message, TYPE_PARTY_LEAVE, NULL);
		message.party_serial = friends->party.serial;
		message.party_value = PARTY_LEAVE_LEFT;
		memcpy(message.party_keys[0], friends->key, P2P_KEY_SIZE);
		party_broadcast(&message);
	}
	if (friends->party.count > 1 && why && why[0])
		party_notice(T("You left the party (%s)"), why);
	else if (friends->party.count > 1)
		party_notice("%s", T("You left the party"));
	party_clear();
	/* (appearing offline or stopping: no presence after, the friends'
	offline presence says enough) */
	if (!tell || friends->appear_offline)
	{
		friends->party.presence_due = 0;
		friends->party.presence_size = 0;
	}
}

/* a member gone from the roster (left, removed, timed out); the leader
gone, the longest-standing member leads (deterministic: every member works
out the same one) */
static void party_remove_index(int index, const char *why)
{
	char name[P2P_PARTY_NAME_SIZE];
	int was_leader = index == friends->party.leader;

	party_member_name(&friends->party.members[index], name, sizeof(name));
	memmove(&friends->party.members[index], &friends->party.members[index + 1],
		sizeof(friends->party.members[0]) * (size_t)(friends->party.count - index - 1));
	friends->party.count--;
	if (was_leader)
	{
		char leader[P2P_PARTY_NAME_SIZE];

		/* (the first of those left: the longest-standing) */
		friends->party.leader = 0;
		party_notice(T("%s %s"), name, why);
		if (friends->party.count)
		{
			party_member_name(&friends->party.members[0], leader, sizeof(leader));
			party_notice(T("%s leads the party now"), leader);
		}
		friends->party.follow_heard = friends->party.follow_done;
		friends->party.follow_pending = 0;
		/* (this machine leads: its roster out at once) */
		if (party_leading_now())
		{
			friends->party.serial++;
			friends->party.roster_time = 0;
			friends->party.follow_active = 0;
		}
	}
	else
	{
		if (index < friends->party.leader)
			friends->party.leader--;
		party_notice(T("%s %s"), name, why);
		if (party_leading_now())
		{
			friends->party.serial++;
			friends->party.roster_time = 0;
		}
	}
	friends->party.presence_due = 1;
}

/* a friend heard from in a new run (p2p_friends.c's replay_check): the
invites to and from the run that closed are gone with it (one sent there
stood for PARTY_INVITE_LIFETIME, a new one to the new run refused as too soon
meanwhile, though the run it went to could never answer), and a member who
was that run is not in the party any more (its party went with it: not
waited for for PARTY_TIMEOUT, its slot not followed into games) */
static void party_friend_new_run(struct friend_record *record)
{
	int index, cleared = 0;

	invites_of_remove(record->key);
	for (index = friends->party.sent_count - 1; index >= 0; index--)
		if (p2p_equal(friends->party.sent[index].key, record->key, P2P_KEY_SIZE))
		{
			memmove(&friends->party.sent[index], &friends->party.sent[index + 1],
				sizeof(friends->party.sent[0]) * (size_t)(friends->party.sent_count - index - 1));
			friends->party.sent_count--;
			cleared++;
		}
	for (index = friends->party_invite_count - 1; index >= 0; index--)
		if (p2p_equal(friends->party_invites[index].key, record->key, P2P_KEY_SIZE))
		{
			memmove(&friends->party_invites[index], &friends->party_invites[index + 1],
				sizeof(friends->party_invites[0]) * (size_t)(friends->party_invite_count - index - 1));
			friends->party_invite_count--;
			cleared++;
		}
	record->party_invite_sent_time = 0;
	index = friends->party.active ? party_member_index(record->key) : -1;
	if (index >= 0 && !p2p_equal(record->key, friends->key, P2P_KEY_SIZE))
	{
		party_remove_index(index, T("went offline"));
		cleared++;
	}
	if (cleared)
		platform_log("party: %s's game started again: %d invites or places of its last run let go", record->name, cleared);
}

static int party_keys_unique(const struct message *message)
{
	int index, other, self = 0;

	for (index = 0; index < message->party_count; index++)
	{
		if (p2p_equal(message->party_keys[index], friends->key, P2P_KEY_SIZE))
			self++;
		else
		{
			unsigned char x25519[P2P_KEY_SIZE];

			if (!p2p_ed25519_to_x25519(message->party_keys[index], x25519))
				return 0;
		}
		for (other = 0; other < index; other++)
			if (p2p_equal(message->party_keys[index], message->party_keys[other], P2P_KEY_SIZE))
				return 0;
	}
	return self <= 1;
}

/* a roster taken: the members kept as they were (their names, replays,
keys), the new ones added, those gone said */
static void party_roster_apply(const struct message *message, int joining)
{
	struct party_member members[P2P_PARTY_MAXIMUM];
	int index, count = message->party_count;

	memset(members, 0, sizeof(members));
	for (index = 0; index < count; index++)
	{
		struct party_member *kept = joining ? NULL : party_member_by_key(message->party_keys[index]);

		if (kept)
			members[index] = *kept;
		else
		{
			memcpy(members[index].key, message->party_keys[index], P2P_KEY_SIZE);
			members[index].heard_time = now_stamp();
			if (!joining && friends->party.active)
			{
				char name[P2P_PARTY_NAME_SIZE];

				party_name_of(message->party_keys[index], "", name, sizeof(name));
				party_notice(T("%s joined the party"), name);
			}
		}
	}
	if (!joining)
	{
		for (index = 0; index < friends->party.count; index++)
		{
			int still = 0, each;

			for (each = 0; each < count; each++)
				still |= p2p_equal(friends->party.members[index].key, message->party_keys[each], P2P_KEY_SIZE);
			if (!still)
			{
				char name[P2P_PARTY_NAME_SIZE];

				party_member_name(&friends->party.members[index], name, sizeof(name));
				party_notice(T("%s left the party"), name);
			}
		}
	}
	memcpy(friends->party.members, members, sizeof(members));
	friends->party.count = count;
	friends->party.leader = message->party_leader;
	friends->party.serial = message->party_serial;
	friends->party.active = 1;
	friends->party.presence_due = 1;
	friends->party.status_due = 1;
}

/* ---------- what was heard */

static void party_invite_heard(struct friend_record *record, const struct message *message)
{
	struct party_invite_record *invite = NULL;
	int index;

	if (record->party_invite_heard_time && !elapsed(record->party_invite_heard_time, PARTY_INVITE_HEARD_INTERVAL))
	{
		friends->dropped_rate++;
		return;
	}
	record->party_invite_heard_time = now_stamp();
	if (friends->party.active && p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH))
		return;
	for (index = 0; index < friends->party_invite_count && !invite; index++)
		if (p2p_equal(friends->party_invites[index].key, record->key, P2P_KEY_SIZE))
			invite = &friends->party_invites[index];
	if (!invite)
	{
		if (friends->party_invite_count >= P2P_FRIEND_INVITES_MAXIMUM)
		{
			memmove(&friends->party_invites[0], &friends->party_invites[1],
				sizeof(friends->party_invites[0]) * (P2P_FRIEND_INVITES_MAXIMUM - 1));
			friends->party_invite_count--;
		}
		invite = &friends->party_invites[friends->party_invite_count++];
	}
	memset(invite, 0, sizeof(*invite));
	memcpy(invite->key, record->key, P2P_KEY_SIZE);
	memcpy(invite->party_id, message->party_id, PARTY_ID_LENGTH);
	invite->size = message->party_count;
	invite->time = now_stamp();
	platform_log("party: %s invited this machine to their party (%d of %d)", record->name, message->party_count,
		P2P_PARTY_MAXIMUM);
}

static void party_answer_heard(struct friend_record *record, const struct message *message)
{
	int index;

	if (!party_leading_now() || !p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH))
	{
		friends->dropped_replays++;
		return;
	}
	for (index = 0; index < friends->party.sent_count; index++)
		if (p2p_equal(friends->party.sent[index].key, record->key, P2P_KEY_SIZE))
			break;
	/* (only to an invite sent, still waiting; once taken it is gone) */
	if (index == friends->party.sent_count)
	{
		friends->dropped_replays++;
		return;
	}
	memmove(&friends->party.sent[index], &friends->party.sent[index + 1],
		sizeof(friends->party.sent[0]) * (size_t)(friends->party.sent_count - index - 1));
	friends->party.sent_count--;
	if (!message->party_value)
	{
		party_notice(T("%s said no to the party"), record->name);
		return;
	}
	if (friends->party.count >= P2P_PARTY_MAXIMUM || party_member_by_key(record->key))
	{
		platform_log("party: %s said yes, but the party is full", record->name);
		return;
	}
	memset(&friends->party.members[friends->party.count], 0, sizeof(friends->party.members[0]));
	memcpy(friends->party.members[friends->party.count].key, record->key, P2P_KEY_SIZE);
	friends->party.members[friends->party.count].heard_time = now_stamp();
	friends->party.count++;
	friends->party.serial++;
	friends->party.roster_time = 0;
	friends->party.presence_due = 1;
	/* (the follow again at once, for the new member) */
	if (friends->party.follow_active)
		friends->party.follow_time = 0;
	party_notice(T("%s joined the party"), record->name);
}

/* the member a leader gone hands the party to: the first of those left */
static int party_successor_is(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends->party.count; index++)
	{
		if (index == friends->party.leader)
			continue;
		return p2p_equal(friends->party.members[index].key, key, P2P_KEY_SIZE);
	}
	return 0;
}

static void party_roster_heard(const unsigned char *sender, const struct message *message)
{
	int self = -1, index, joining = 0;

	if (!party_keys_unique(message) || !p2p_equal(message->party_keys[message->party_leader], sender, P2P_KEY_SIZE))
	{
		friends->dropped_malformed++;
		return;
	}
	for (index = 0; index < message->party_count; index++)
		if (p2p_equal(message->party_keys[index], friends->key, P2P_KEY_SIZE))
			self = index;
	if (friends->party.active && p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH))
	{
		const unsigned char *leader = friends->party.members[friends->party.leader].key;

		/* (the leader's, or - the leader silent a while, gone for the
		member who sends it - the one a leader gone hands the party to; never
		over a leader still heard from; newer than the last) */
		if (message->party_serial <= friends->party.serial || party_leading_now() ||
			!(p2p_equal(sender, leader, P2P_KEY_SIZE) || (party_successor_is(sender) &&
			elapsed(friends->party.members[friends->party.leader].heard_time, PARTY_TIMEOUT - 10000))))
		{
			friends->dropped_replays++;
			return;
		}
	}
	else
	{
		/* (a party joined: only the roster of the party whose invite this
		machine accepted, from its inviter) */
		for (index = 0; index < friends->party_invite_count; index++)
		{
			const struct party_invite_record *invite = &friends->party_invites[index];

			if (invite->accepted && p2p_equal(invite->key, sender, P2P_KEY_SIZE) &&
				p2p_equal(invite->party_id, message->party_id, PARTY_ID_LENGTH))
			{
				break;
			}
		}
		if (index == friends->party_invite_count || self < 0)
		{
			friends->dropped_replays++;
			return;
		}
		memmove(&friends->party_invites[index], &friends->party_invites[index + 1],
			sizeof(friends->party_invites[0]) * (size_t)(friends->party_invite_count - index - 1));
		friends->party_invite_count--;
		if (friends->party.active)
			party_leave_now(1, T("joined another party"));
		party_clear();
		memcpy(friends->party.id, message->party_id, PARTY_ID_LENGTH);
		joining = 1;
	}
	if (self < 0)
	{
		/* (the leader's roster without this machine: removed, or the party
		over) */
		party_notice("%s", message->party_count > 1 ? T("You were removed from the party") : T("The party is over"));
		party_clear();
		return;
	}
	party_roster_apply(message, joining);
	if (joining)
	{
		char name[P2P_PARTY_NAME_SIZE];

		party_name_of(sender, "", name, sizeof(name));
		party_notice(T("You joined %s's party"), name);
	}
}

static void party_leave_heard(const unsigned char *sender, const struct message *message)
{
	int sender_index = party_member_index(sender), subject;

	if (!friends->party.active || !p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH) || sender_index < 0)
	{
		friends->dropped_replays++;
		return;
	}
	if (message->party_value == PARTY_LEAVE_REMOVED)
	{
		/* (the leader's alone) */
		if (sender_index != friends->party.leader)
		{
			friends->dropped_forged++;
			return;
		}
		if (p2p_equal(message->party_keys[0], friends->key, P2P_KEY_SIZE))
		{
			party_notice("%s", T("You were removed from the party"));
			party_clear();
			return;
		}
		subject = party_member_index(message->party_keys[0]);
		if (subject >= 0)
			party_remove_index(subject, T("was removed from the party"));
		return;
	}
	/* (one's own leaving alone) */
	if (!p2p_equal(message->party_keys[0], sender, P2P_KEY_SIZE))
	{
		friends->dropped_forged++;
		return;
	}
	party_remove_index(sender_index, T("left the party"));
	if (friends->party.count <= 1)
	{
		party_notice("%s", T("The party is over"));
		party_clear();
	}
}

static void party_status_heard(struct party_member *member, const struct message *message)
{
	if (!budget_left(&member->status_budget, PARTY_STATUS_BUDGET, PARTY_STATUS_BUDGET_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	if (message->name[0])
		snprintf(member->name, sizeof(member->name), "%s", message->name);
	member->status_flags = message->party_value;
	if (member->follow_code != message->party_code || member->follow_serial != message->party_follow)
	{
		member->follow_code = message->party_code;
		member->follow_serial = message->party_follow;
		if (message->party_code >= P2P_PARTY_FOLLOW_PASSWORD && message->party_code <= P2P_PARTY_FOLLOW_FAILED)
		{
			char name[P2P_PARTY_NAME_SIZE];

			party_member_name(member, name, sizeof(name));
			platform_log("party: %s could not follow: %s", name, p2p_party_follow_text(message->party_code));
		}
	}
}

/* what a follow heard comes to before any join: a locked or unreachable
game, another version, a full one; this machine there already (0: to join) */
static int party_follow_precheck(const struct message *message)
{
	/* (a leader not this machine's friend - the longest-standing member, a
	friend of the leader gone, who leads once that leader is silent - leads
	no one into a game by itself: a stranger would choose which host this
	machine reaches, and its address with it; security review 1.1.1-beta.1
	F-07) */
	if (friends->party.active && friends->party.leader >= 0 && friends->party.leader < friends->party.count &&
		!find_friend(friends->party.members[friends->party.leader].key))
	{
		return P2P_PARTY_FOLLOW_FAILED;
	}
	if (message->party_value & PARTY_FOLLOW_LOCKED)
		return P2P_PARTY_FOLLOW_PASSWORD;
	if ((message->party_value & PARTY_FOLLOW_UNREACHABLE) || !(message->party_value & PARTY_FOLLOW_HAS_INVITE))
		return P2P_PARTY_FOLLOW_UNREACHABLE;
	if (!game_wire_joinable(message->game.version, message->game.wire))
		return P2P_PARTY_FOLLOW_VERSION;
	/* (there: in that game now - its host's peer still connected from a
	game it closed is not: insider.21, a member at the menus told "there
	already" when the leader hosted again on the same invite, never
	joining) */
	if (friends->host.joined && friends->in_game && !friends->game_hosting &&
		p2p_equal(friends->host.joined_hash, message->game.invite_hash, P2P_KEY_HASH_SIZE))
	{
		return P2P_PARTY_FOLLOW_THERE;
	}
	if (message->game.maximum && message->game.players >= message->game.maximum)
		return P2P_PARTY_FOLLOW_FULL;
	return P2P_PARTY_FOLLOW_NONE;
}

static void party_follow_heard(const unsigned char *sender, const struct message *message)
{
	unsigned long previous = friends->party.follow_heard;
	int result;

	if (!friends->party.active || !p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH) ||
		!p2p_equal(sender, friends->party.members[friends->party.leader].key, P2P_KEY_SIZE))
	{
		friends->dropped_forged++;
		return;
	}
	/* (each follow once: the leader sends it again now and then) */
	if (message->party_serial <= friends->party.follow_heard)
		return;
	friends->party.follow_heard = message->party_serial;
	friends->party.follow_message = *message;
	/* (the leader on this project's PC build, as it says: the lists) */
	friends->party.members[friends->party.leader].pc = message->game.sender_pc;
	friends->party.follow_heard_time = now_stamp();
	/* (the leader back at the menus: nothing to join, nothing wrong) */
	if (message->party_value & PARTY_FOLLOW_MENUS)
	{
		char name[P2P_PARTY_NAME_SIZE];

		friends->party.follow_pending = 0;
		friends->party.follow_done = message->party_serial;
		friends->party.follow_result = P2P_PARTY_FOLLOW_NONE;
		friends->party.status_due = 1;
		/* (a member new to the party heard no game before: nothing said) */
		if (previous)
		{
			party_name_of(sender, "", name, sizeof(name));
			party_notice(T("%s is back in the menus"), name);
		}
		else
			platform_log("party: the leader is in the menus");
		return;
	}
	if ((message->party_value & PARTY_FOLLOW_HOSTING) && (message->party_value & PARTY_FOLLOW_HAS_INVITE))
		memcpy(friends->party.leader_game_hash, message->game.invite_hash, P2P_KEY_HASH_SIZE);
	result = party_follow_precheck(message);
	{
		char activity[P2P_FRIEND_ACTIVITY_SIZE], name[P2P_PARTY_NAME_SIZE];

		p2p_friends_activity_text(activity, sizeof(activity), message->game.state, message->game.map,
			message->game.gametype, message->game.engine, message->game.difficulty, message->game.pc_map);
		party_name_of(sender, "", name, sizeof(name));
		platform_log("party: the leader, %s, %s a game: %s%s", name, message->party_value & PARTY_FOLLOW_HOSTING ?
			"hosts" : "joined", activity, result ? "" : "; following");
	}
	if (result)
	{
		friends->party.follow_pending = 0;
		friends->party.follow_done = message->party_serial;
		friends->party.follow_result = result;
		friends->party.status_due = 1;
		if (result != P2P_PARTY_FOLLOW_THERE)
			party_notice(T("Couldn't follow the leader: %s"), p2p_party_follow_text(result));
		return;
	}
	friends->party.follow_pending = 1;
}

static void party_chat_heard(struct party_member *member, const struct message *message)
{
	char name[P2P_PARTY_NAME_SIZE];

	if (member->muted)
		return;
	if (!budget_left(&member->chat_budget, PARTY_CHAT_BUDGET, PARTY_CHAT_BUDGET_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	party_member_name(member, name, sizeof(name));
	party_chat_add(member->key, name, message->party_code == PARTY_CHAT_PRESET ? party_presets[message->party_value] :
		message->chat, 0, message->party_code == PARTY_CHAT_PRESET ? 2 : 0);
	platform_log("party: chat from %s: %s", name, message->party_code == PARTY_CHAT_PRESET ?
		party_presets[message->party_value] : message->chat);
}

static void party_presence_heard(struct friend_record *record, const struct message *message)
{
	memcpy(record->party_leader, message->party_keys[0], P2P_KEY_SIZE);
	record->party_size = message->party_count;
	record->party_heard_time = now_stamp();
}

/* a party message opened (its sender a friend's record, or a member not a
friend's): checked, then handled */
static void party_pair_heard(struct friend_record *record, struct party_member *member, const struct message *message,
	const unsigned char *plain, int size)
{
	const unsigned char *sender = record ? record->key : member->key;
	int directed = message->type == TYPE_PARTY_INVITE || message->type == TYPE_PARTY_ANSWER;

	if (!p2p_equal(message->sender, sender, P2P_KEY_SIZE) ||
		(directed ? !p2p_equal(message->recipient, friends->key, P2P_KEY_SIZE) : !key_zero(message->recipient)) ||
		((directed || message->type == TYPE_PARTY_PRESENCE) && !record))
	{
		friends->dropped_forged++;
		return;
	}
	if (find_blocked(sender) >= 0)
	{
		friends->dropped_blocked++;
		return;
	}
	/* (appearing offline: nothing of a party taken) */
	if (friends->appear_offline)
		return;
	if (!plain_verify(plain, size))
	{
		friends->dropped_forged++;
		platform_log("party: dropped a party message whose signature is not its sender's");
		return;
	}
	/* (an invite: never older than the friend's run last seen, kept over
	restarts) */
	{
		int fresh = party_replay_check(sender, message->epoch, message->counter);

		if (fresh <= 0 || (message->type == TYPE_PARTY_INVITE && message->epoch < record->epoch))
		{
			/* (a copy through another broker: counted, not logged - insider.21
			logged ~15 a minute in a party; an older one is a replay) */
			friends->dropped_replays++;
			if (fresh != 0)
				platform_log("party: dropped a replayed party message");
			return;
		}
	}
	if (message->type != TYPE_PARTY_INVITE && message->type != TYPE_PARTY_ANSWER &&
		message->type != TYPE_PARTY_PRESENCE && message->type != TYPE_PARTY_ROSTER)
	{
		/* (a member's alone, of this party) */
		struct party_member *from = party_member_by_key(sender);

		if (!from || !p2p_equal(friends->party.id, message->party_id, PARTY_ID_LENGTH))
		{
			friends->dropped_forged++;
			return;
		}
		from->heard_time = now_stamp();
	}
	switch (message->type)
	{
	case TYPE_PARTY_INVITE:
		party_invite_heard(record, message);
		break;
	case TYPE_PARTY_ANSWER:
		party_answer_heard(record, message);
		break;
	case TYPE_PARTY_ROSTER:
		party_roster_heard(sender, message);
		if (party_member_by_key(sender))
			party_member_by_key(sender)->heard_time = now_stamp();
		break;
	case TYPE_PARTY_LEAVE:
		party_leave_heard(sender, message);
		break;
	case TYPE_PARTY_STATUS:
		party_status_heard(party_member_by_key(sender), message);
		break;
	case TYPE_PARTY_FOLLOW:
		party_follow_heard(sender, message);
		break;
	case TYPE_PARTY_CHAT:
		party_chat_heard(party_member_by_key(sender), message);
		break;
	case TYPE_PARTY_PRESENCE:
		party_presence_heard(record, message);
		break;
	}
}

/* (block_key) a member blocked: the leader removes them; another member
shows and tells them nothing more */
static void party_key_blocked(const unsigned char *key)
{
	int index = party_member_index(key);

	if (index < 0)
		return;
	if (party_leading_now() && index != friends->party.leader)
	{
		struct message message;

		party_start_message(&message, TYPE_PARTY_LEAVE, NULL);
		message.party_serial = friends->party.serial;
		message.party_value = PARTY_LEAVE_REMOVED;
		memcpy(message.party_keys[0], key, P2P_KEY_SIZE);
		party_broadcast(&message);
		party_remove_index(index, T("was removed from the party"));
	}
}

static void party_friend_entry(const struct friend_record *record, struct p2p_friend_entry *entry)
{
	char leader[P2P_PARTY_NAME_SIZE];

	if (!record->party_size || !record->party_heard_time || elapsed(record->party_heard_time, PARTY_PRESENCE_TIMEOUT))
		return;
	entry->party_size = record->party_size;
	/* (the leader by this machine's own word for them: itself, a friend, the
	friend whose presence it is; else just "a party") */
	if (p2p_equal(record->party_leader, friends->key, P2P_KEY_SIZE))
		snprintf(entry->party, sizeof(entry->party), T("In your party (%d of %d)"), record->party_size,
			P2P_PARTY_MAXIMUM);
	else if (p2p_equal(record->party_leader, record->key, P2P_KEY_SIZE) || find_friend(record->party_leader))
	{
		party_name_of(record->party_leader, "", leader, sizeof(leader));
		snprintf(entry->party, sizeof(entry->party), T("In %s's party (%d of %d)"), leader, record->party_size,
			P2P_PARTY_MAXIMUM);
	}
	else
		snprintf(entry->party, sizeof(entry->party), T("In a party (%d of %d)"), record->party_size, P2P_PARTY_MAXIMUM);
}

/* ---------- the tests' steps (debug) */

#ifndef HALO_RELEASE
/* HALO_TEST_PARTY, comma-separated: invite-at=S (every friend online invited
S seconds in), accept, decline (every party invite heard), leave-at=S,
kick-at=S (the leader removes its last member), chat-at=S:TEXT (TEXT sent,
then the first preset), forge-at=S (a chat signed by another key, a roster
from a member not the leader, the last chat sent again: all dropped) */
static void party_test_parse(void)
{
	const char *setting = getenv("HALO_TEST_PARTY");
	char copy[512];
	char *step, *save_pointer = NULL;

	friends->test_party_parsed = 1;
	friends->test_party_invite_at = friends->test_party_leave_at = friends->test_party_kick_at = -1;
	friends->test_party_chat_at = friends->test_party_forge_at = -1;
	if (!setting || !setting[0])
		return;
	snprintf(copy, sizeof(copy), "%s", setting);
	for (step = strtok_r(copy, ",", &save_pointer); step; step = strtok_r(NULL, ",", &save_pointer))
	{
		if (!strncmp(step, "invite-at=", 10))
			friends->test_party_invite_at = atol(step + 10);
		else if (!strcmp(step, "accept"))
			friends->test_party_accept = 1;
		else if (!strcmp(step, "decline"))
			friends->test_party_decline = 1;
		else if (!strncmp(step, "leave-at=", 9))
			friends->test_party_leave_at = atol(step + 9);
		else if (!strncmp(step, "kick-at=", 8))
			friends->test_party_kick_at = atol(step + 8);
		else if (!strncmp(step, "forge-at=", 9))
			friends->test_party_forge_at = atol(step + 9);
		else if (!strncmp(step, "chat-at=", 8))
		{
			const char *colon = strchr(step + 8, ':');

			friends->test_party_chat_at = atol(step + 8);
			snprintf(friends->test_party_chat, sizeof(friends->test_party_chat), "%s", colon ? colon + 1 : "hello");
		}
	}
	platform_log("party: test steps: %s", setting);
}

static void party_test_forge(void)
{
	struct message message;
	unsigned char plain[MAXIMUM_PLAIN_SIZE];
	unsigned char saved_seed[P2P_SEED_SIZE];
	int size, index;

	if (!friends->party.active || friends->party.count < 2)
		return;
	/* (1) a chat signed by another key (its sender field this machine's) */
	memcpy(saved_seed, friends->seed, sizeof(saved_seed));
	posix_random_bytes(friends->seed, sizeof(friends->seed));
	party_start_message(&message, TYPE_PARTY_CHAT, NULL);
	message.party_code = PARTY_CHAT_TEXT;
	snprintf(message.chat, sizeof(message.chat), "forged chat");
	size = plain_encode(&message, plain);
	memcpy(friends->seed, saved_seed, sizeof(saved_seed));
	for (index = 0; index < friends->party.count && size; index++)
		party_seal_to(friends->party.members[index].key, plain, size);
	/* (2) a roster from a member not the leader (this machine, if it is not
	the leader), naming itself the leader */
	party_start_message(&message, TYPE_PARTY_ROSTER, NULL);
	message.party_serial = friends->party.serial + 100;
	message.party_count = friends->party.count;
	for (index = 0; index < friends->party.count; index++)
		memcpy(message.party_keys[index], friends->party.members[index].key, P2P_KEY_SIZE);
	message.party_leader = party_self_index();
	if (!party_leading_now())
		party_broadcast(&message);
	/* (3) the last message sent, sent again as it was */
	if (friends->test_last_sent_size)
		outbound_add(friends->test_last_topic, friends->test_last_sent, friends->test_last_sent_size, 0, NULL);
	platform_log("party: test: forged party messages sent");
}

static void party_test_steps(void)
{
	int index;

	if (!friends->test_party_parsed)
		party_test_parse();
	if (friends->test_party_invite_at >= 0 && !friends->test_party_invited && test_due(friends->test_party_invite_at))
	{
		friends->test_party_invited = 1;
		for (index = 0; index < friends->friend_count; index++)
		{
			char id[P2P_FRIEND_ID_SIZE];

			if (!friends->friends[index].heard_time || !friends->friends[index].presence.online)
				continue;
			key_id(friends->friends[index].key, id);
			pthread_mutex_unlock(&friends_lock);
			platform_log("party: test: inviting a friend: %s", p2p_party_outcome_text(p2p_party_invite(id)));
			pthread_mutex_lock(&friends_lock);
		}
	}
	for (index = friends->party_invite_count - 1; index >= 0 && (friends->test_party_accept ||
		friends->test_party_decline); index--)
	{
		char id[P2P_FRIEND_ID_SIZE];

		if (friends->party_invites[index].accepted)
			continue;
		p2p_hex(friends->party_invites[index].party_id, 8, id);
		pthread_mutex_unlock(&friends_lock);
		platform_log("party: test: %s a party invite: %s", friends->test_party_accept ? "accepting" : "declining",
			p2p_party_outcome_text(p2p_party_answer(id, friends->test_party_accept)));
		pthread_mutex_lock(&friends_lock);
		if (index > friends->party_invite_count)
			index = friends->party_invite_count;
	}
	if (test_due(friends->test_party_leave_at) && !friends->test_party_left && friends->party.active)
	{
		friends->test_party_left = 1;
		party_leave_now(1, "the test's step");
	}
	if (test_due(friends->test_party_kick_at) && !friends->test_party_kicked && party_leading_now() &&
		friends->party.count > 1)
	{
		char id[P2P_FRIEND_ID_SIZE];

		friends->test_party_kicked = 1;
		key_id(friends->party.members[friends->party.count - 1].key, id);
		pthread_mutex_unlock(&friends_lock);
		platform_log("party: test: removing the last member: %s", p2p_party_outcome_text(p2p_party_kick(id)));
		pthread_mutex_lock(&friends_lock);
	}
	if (test_due(friends->test_party_chat_at) && !friends->test_party_chatted && friends->party.active &&
		friends->party.count > 1)
	{
		char text[P2P_PARTY_CHAT_SIZE];

		friends->test_party_chatted = 1;
		snprintf(text, sizeof(text), "%s", friends->test_party_chat);
		pthread_mutex_unlock(&friends_lock);
		platform_log("party: test: chat: %s", p2p_party_outcome_text(p2p_party_chat_send(text)));
		platform_log("party: test: quick chat: %s", p2p_party_outcome_text(p2p_party_chat_preset(0)));
		pthread_mutex_lock(&friends_lock);
	}
	if (test_due(friends->test_party_forge_at) && !friends->test_party_forged && friends->party.active)
	{
		friends->test_party_forged = 1;
		party_test_forge();
	}
}
#endif

/* ---------- each pass (p2p_friends_update, friends_lock held) */

static void party_update(void)
{
	int index, flags;

#ifndef HALO_RELEASE
	party_test_steps();
#endif
	/* old invites go (heard, and sent) */
	for (index = friends->party_invite_count - 1; index >= 0; index--)
		if (elapsed(friends->party_invites[index].time, PARTY_INVITE_LIFETIME))
		{
			memmove(&friends->party_invites[index], &friends->party_invites[index + 1],
				sizeof(friends->party_invites[0]) * (size_t)(friends->party_invite_count - index - 1));
			friends->party_invite_count--;
		}
	for (index = friends->party.sent_count - 1; index >= 0; index--)
		if (elapsed(friends->party.sent[index].time, PARTY_INVITE_LIFETIME))
		{
			memmove(&friends->party.sent[index], &friends->party.sent[index + 1],
				sizeof(friends->party.sent[0]) * (size_t)(friends->party.sent_count - index - 1));
			friends->party.sent_count--;
		}
	if (friends->appear_offline)
		return;
	/* (a member) the game this machine is in, joined (p2p_party_host_left_quiet) */
	if (friends->party.active && friends->host.joined && friends->in_game && !friends->game_hosting)
		memcpy(friends->party.joined_hash, friends->host.joined_hash, P2P_KEY_HASH_SIZE);
	if (friends->party.active)
	{
		/* members not heard from of late, or a friend's offline presence:
		gone */
		for (index = friends->party.count - 1; index >= 0; index--)
		{
			struct party_member *member = &friends->party.members[index];
			const struct friend_record *record = find_friend(member->key);

			if (p2p_equal(member->key, friends->key, P2P_KEY_SIZE))
				continue;
			if ((member->heard_time && elapsed(member->heard_time, PARTY_TIMEOUT)) ||
				(record && record->heard_time && !record->presence.online &&
				(long)(record->presence_time - member->heard_time) > 0))
			{
				platform_log("party: a member not heard from for %lu ms%s", p2p_now() - member->heard_time,
					record && !record->presence.online ? " (their presence: offline)" : "");
				party_remove_index(index, T("went offline"));
			}
		}
		/* (alone, no invite waiting: no party) */
		if (friends->party.count <= 1 && !friends->party.sent_count)
		{
			if (friends->party.serial > 1)
				party_notice("%s", T("The party is over"));
			party_clear();
		}
	}
	if (friends->party.active && party_leading_now())
	{
		const struct p2p_friends_host *host = &friends->host;
		const unsigned char *hash = NULL, *token = NULL;
		int settling = 0;

		/* the roster: on a change, and now and then */
		if (friends->party.count > 1 && (!friends->party.roster_time ||
			elapsed(friends->party.roster_time, PARTY_ROSTER_INTERVAL)))
		{
			party_send_roster();
		}
		/* the follow: hosting (its own invite, never with a password),
		joined another's game through internet play (that game's, never a
		locked game's), in a game no member reaches (once it has lasted:
		PARTY_UNREACHABLE_SETTLE), or back at the menus from one of those
		(the members told, nothing to join) */
		flags = 0;
		if (host->hosting)
		{
			flags = PARTY_FOLLOW_HOSTING | (host->locked ? PARTY_FOLLOW_LOCKED : PARTY_FOLLOW_HAS_INVITE);
			hash = host->key_hash;
			token = host->token;
		}
		else if (host->joined && friends->in_game && !friends->game_hosting)
		{
			flags = host->joined_locked ? PARTY_FOLLOW_LOCKED : PARTY_FOLLOW_HAS_INVITE;
			hash = host->joined_hash;
			token = host->joined_token;
		}
		else if (friends->in_game)
		{
			if (!friends->party.unreachable_since)
				friends->party.unreachable_since = now_stamp();
			if (elapsed(friends->party.unreachable_since, PARTY_UNREACHABLE_SETTLE))
				flags = PARTY_FOLLOW_UNREACHABLE;
			else
				settling = 1;
		}
		else if (friends->party.follow_active)
			flags = PARTY_FOLLOW_MENUS;
		if (!friends->in_game || host->hosting || (flags != PARTY_FOLLOW_UNREACHABLE && !settling))
			friends->party.unreachable_since = 0;
		if (settling)
			;
		else if (flags)
		{
			int changed = !friends->party.follow_active || flags != friends->party.follow_flags ||
				(hash && (!p2p_equal(hash, friends->party.follow_hash, P2P_KEY_HASH_SIZE) ||
				!p2p_equal(token, friends->party.follow_token, P2P_TOKEN_SIZE)));

			if (changed)
			{
				friends->party.follow_active = 1;
				friends->party.follow_flags = flags;
				friends->party.follow_serial++;
				memset(friends->party.follow_hash, 0, sizeof(friends->party.follow_hash));
				memset(friends->party.follow_token, 0, sizeof(friends->party.follow_token));
				if (hash)
				{
					memcpy(friends->party.follow_hash, hash, P2P_KEY_HASH_SIZE);
					memcpy(friends->party.follow_token, token, P2P_TOKEN_SIZE);
				}
				friends->party.follow_time = 0;
				platform_log("party: the party follows this machine: %s%s", flags & PARTY_FOLLOW_HOSTING ? "hosting" :
					flags & PARTY_FOLLOW_UNREACHABLE ? "in a game no member can reach" : flags & PARTY_FOLLOW_MENUS ?
					"back in the menus (nothing to join)" : "joined a game",
					flags & PARTY_FOLLOW_LOCKED ? " (a password: not passed on)" : "");
			}
			if (friends->party.count > 1 && (!friends->party.follow_time ||
				elapsed(friends->party.follow_time, PARTY_FOLLOW_INTERVAL)))
			{
				party_send_follow();
			}
		}
		else
			friends->party.follow_active = 0;
	}
	/* (a member) a follow not joined in time goes */
	if (friends->party.follow_pending && elapsed(friends->party.follow_heard_time, PARTY_FOLLOW_LIFETIME))
		friends->party.follow_pending = 0;
	/* the status: on a change, and now and then */
	if (friends->party.active && friends->party.count > 1)
	{
		flags = party_status_flags();
		if (flags != friends->party.status_flags)
			friends->party.status_due = 1;
		if (friends->party.status_due || !friends->party.status_time ||
			elapsed(friends->party.status_time, PARTY_STATUS_INTERVAL))
		{
			party_send_status();
		}
	}
	/* the party's presence to the friends: on a change, now and then */
	{
		int size = friends->party.active && friends->party.count >= 2 ? friends->party.count : 0;

		if (size != friends->party.presence_size || friends->party.presence_due ||
			(size && elapsed(friends->party.presence_time, PARTY_PRESENCE_INTERVAL)))
		{
			if (friends->friend_count && (size || friends->party.presence_size))
				party_send_presence();
			else
			{
				friends->party.presence_size = size;
				friends->party.presence_due = 0;
				friends->party.presence_time = now_stamp();
			}
		}
	}
}

/* ---------- the game's side (p2p_party.h) */

/* a friend by their entry's id (NULL: none) */
static struct friend_record *party_friend_of_id(const char *id)
{
	int index;

	for (index = 0; id && index < friends->friend_count; index++)
	{
		char each[P2P_FRIEND_ID_SIZE];

		key_id(friends->friends[index].key, each);
		if (!strcmp(each, id))
			return &friends->friends[index];
	}
	return NULL;
}

static struct party_member *party_member_of_id(const char *id)
{
	int index;

	for (index = 0; id && friends->party.active && index < friends->party.count; index++)
	{
		char each[P2P_FRIEND_ID_SIZE];

		key_id(friends->party.members[index].key, each);
		if (!strcmp(each, id))
			return &friends->party.members[index];
	}
	return NULL;
}

/* whether a friend can be invited (the outcome if not) */
static int party_invite_check(struct friend_record *record)
{
	int index;

	if (!friends_on())
		return P2P_PARTY_OFFLINE;
	if (friends->appear_offline)
		return P2P_PARTY_APPEARING_OFFLINE;
	if (!record)
		return P2P_PARTY_NOT_FRIEND;
	if (find_blocked(record->key) >= 0)
		return P2P_PARTY_NOT_FRIEND;
	if (party_member_by_key(record->key))
		return P2P_PARTY_ALREADY_IN;
	if (friends->party.active && !party_leading_now())
		return P2P_PARTY_NOT_LEADER;
	if (!record->heard_time || !record->presence.online)
		return P2P_PARTY_FRIEND_OFFLINE;
	if ((friends->party.active ? friends->party.count : 1) + friends->party.sent_count >= P2P_PARTY_MAXIMUM)
		return P2P_PARTY_FULL;
	for (index = 0; index < friends->party.sent_count; index++)
		if (p2p_equal(friends->party.sent[index].key, record->key, P2P_KEY_SIZE))
			return P2P_PARTY_TOO_SOON;
	if (record->party_invite_sent_time && !elapsed(record->party_invite_sent_time, PARTY_INVITE_INTERVAL))
		return P2P_PARTY_TOO_SOON;
	return P2P_PARTY_DONE;
}

int p2p_party_invite(const char *friend_id)
{
	struct friend_record *record;
	struct message message;
	int result;

	pthread_mutex_lock(&friends_lock);
	load();
	record = party_friend_of_id(friend_id);
	result = party_invite_check(record);
	if (result == P2P_PARTY_DONE)
	{
		if (!friends->party.active)
		{
			/* (a party made: this machine its leader, alone until a yes) */
			party_clear();
			posix_random_bytes(friends->party.id, PARTY_ID_LENGTH);
			memcpy(friends->party.members[0].key, friends->key, P2P_KEY_SIZE);
			friends->party.members[0].heard_time = now_stamp();
			friends->party.count = 1;
			friends->party.leader = 0;
			friends->party.serial = 1;
			friends->party.active = 1;
			friends->party.presence_due = 0;
			platform_log("party: a party made, this machine its leader");
		}
		party_start_message(&message, TYPE_PARTY_INVITE, record->key);
		message.party_count = friends->party.count;
		if (!party_send_to(record->key, &message))
			result = P2P_PARTY_FULL;
		else
		{
			memcpy(friends->party.sent[friends->party.sent_count].key, record->key, P2P_KEY_SIZE);
			friends->party.sent[friends->party.sent_count].time = now_stamp();
			friends->party.sent_count++;
			record->party_invite_sent_time = now_stamp();
			platform_log("party: invited %s to the party", record->name);
		}
	}
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_invitable(const char *friend_id)
{
	int result;

	pthread_mutex_lock(&friends_lock);
	load();
	result = party_invite_check(party_friend_of_id(friend_id)) == P2P_PARTY_DONE;
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_friend_state(const char *friend_id)
{
	struct friend_record *record;
	int state = 0, index;

	pthread_mutex_lock(&friends_lock);
	load();
	record = party_friend_of_id(friend_id);
	if (record && party_member_by_key(record->key))
		state = 1;
	for (index = 0; record && !state && index < friends->party.sent_count; index++)
		if (p2p_equal(friends->party.sent[index].key, record->key, P2P_KEY_SIZE))
			state = 2;
	pthread_mutex_unlock(&friends_lock);
	return state;
}

int p2p_party_leave(void)
{
	int result = P2P_PARTY_DONE;

	pthread_mutex_lock(&friends_lock);
	load();
	if (!friends->party.active)
		result = P2P_PARTY_NOT_IN_PARTY;
	else
		party_leave_now(friends_on() && !friends->appear_offline, "");
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_kick(const char *member_id)
{
	struct party_member *member;
	int result = P2P_PARTY_DONE;

	pthread_mutex_lock(&friends_lock);
	load();
	member = party_member_of_id(member_id);
	if (!friends->party.active)
		result = P2P_PARTY_NOT_IN_PARTY;
	else if (!party_leading_now())
		result = P2P_PARTY_NOT_LEADER;
	else if (!member || p2p_equal(member->key, friends->key, P2P_KEY_SIZE))
		result = P2P_PARTY_GONE;
	else if (!friends_on() || friends->appear_offline)
		result = P2P_PARTY_OFFLINE;
	else
	{
		struct message message;

		party_start_message(&message, TYPE_PARTY_LEAVE, NULL);
		message.party_serial = friends->party.serial;
		message.party_value = PARTY_LEAVE_REMOVED;
		memcpy(message.party_keys[0], member->key, P2P_KEY_SIZE);
		party_broadcast(&message);
		party_remove_index((int)(member - friends->party.members), T("was removed from the party"));
		if (friends->party.count > 1)
			party_send_roster();
	}
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_mute(const char *member_id, int muted)
{
	struct party_member *member;
	int result = P2P_PARTY_DONE;

	pthread_mutex_lock(&friends_lock);
	member = party_member_of_id(member_id);
	if (!member || p2p_equal(member->key, friends->key, P2P_KEY_SIZE))
		result = P2P_PARTY_GONE;
	else
	{
		char name[P2P_PARTY_NAME_SIZE];

		member->muted = muted != 0;
		party_member_name(member, name, sizeof(name));
		platform_log("party: %s %s here", name, muted ? "muted" : "unmuted");
	}
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_members(struct p2p_party_member *members, int maximum_count)
{
	int count = 0, index;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = 0; friends->party.active && index < friends->party.count && count < maximum_count; index++)
	{
		const struct party_member *member = &friends->party.members[index];
		struct p2p_party_member *entry = &members[count++];
		char code[P2P_FRIEND_CODE_LENGTH + 1];

		memset(entry, 0, sizeof(*entry));
		key_id(member->key, entry->id);
		party_member_name(member, entry->name, sizeof(entry->name));
		code_of_key(member->key, code);
		code_shown(code, entry->code, sizeof(entry->code));
		entry->leader = index == friends->party.leader;
		entry->self = p2p_equal(member->key, friends->key, P2P_KEY_SIZE);
		entry->is_friend = find_friend(member->key) != NULL;
		/* (on this project's PC build: this machine, a friend whose presence
		says so, or the leader whose follow did: network_wire.h) */
		entry->pc = entry->self ? network_wire_this_platform() == NETWORK_PLATFORM_VITA_PC :
			(entry->is_friend && find_friend(member->key)->presence.sender_pc) || member->pc;
		entry->muted = member->muted;
		entry->blocked = find_blocked(member->key) >= 0;
		entry->online = entry->self || (member->heard_time && !elapsed(member->heard_time, PARTY_TIMEOUT));
		if (entry->self)
		{
			if (friends->party.follow_pending)
				snprintf(entry->note, sizeof(entry->note), "%s", T("Following the leader when back at the menus"));
			else if (friends->party.follow_result >= P2P_PARTY_FOLLOW_PASSWORD)
				snprintf(entry->note, sizeof(entry->note), T("Couldn't follow: %s"),
					p2p_party_follow_text(friends->party.follow_result));
		}
		else if (member->follow_code >= P2P_PARTY_FOLLOW_PASSWORD && member->follow_serial &&
			(!party_leading_now() || member->follow_serial == friends->party.follow_serial))
		{
			snprintf(entry->note, sizeof(entry->note), T("Couldn't follow: %s"), p2p_party_follow_text(member->follow_code));
		}
		else if (member->follow_code == P2P_PARTY_FOLLOW_WAITING)
			snprintf(entry->note, sizeof(entry->note), "%s", T("In another game"));
		else if (member->status_flags & PARTY_STATUS_HOSTING)
			snprintf(entry->note, sizeof(entry->note), "%s", T("Hosting a game"));
		else if (member->status_flags & PARTY_STATUS_IN_GAME)
			snprintf(entry->note, sizeof(entry->note), "%s", T("In a game"));
		if (entry->muted && !entry->note[0])
			snprintf(entry->note, sizeof(entry->note), "%s", T("Muted"));
	}
	pthread_mutex_unlock(&friends_lock);
	return count;
}

int p2p_party_status(char *text, int size)
{
	int count;

	pthread_mutex_lock(&friends_lock);
	load();
	count = friends->party.active ? friends->party.count : 0;
	if (!friends_on())
		snprintf(text, (size_t)size, "%s", T("Go online to make a party"));
	else if (friends->appear_offline)
		snprintf(text, (size_t)size, "%s", T("Appearing offline: no party"));
	else if (!friends->party.active || (count <= 1 && !friends->party.sent_count))
		snprintf(text, (size_t)size, "%s", T("Not in a party: invite a friend"));
	else if (count <= 1)
		snprintf(text, (size_t)size, "%s", T("Party: waiting for your invites"));
	else
	{
		char leader[P2P_PARTY_NAME_SIZE];

		party_member_name(&friends->party.members[friends->party.leader], leader, sizeof(leader));
		snprintf(text, (size_t)size, T("Party: %d of %d (%s leads)"), count, P2P_PARTY_MAXIMUM, leader);
	}
	pthread_mutex_unlock(&friends_lock);
	return count > 1 ? count : 0;
}

int p2p_party_in_party(void)
{
	int in_party;

	pthread_mutex_lock(&friends_lock);
	in_party = friends->party.active && (friends->party.count > 1 || friends->party.sent_count > 0);
	pthread_mutex_unlock(&friends_lock);
	return in_party;
}

int p2p_party_host_left_quiet(void)
{
	static const unsigned char zero[P2P_KEY_HASH_SIZE];
	int quiet;

	pthread_mutex_lock(&friends_lock);
	quiet = friends->party.active && friends->party.count > 1 && !party_leading_now() &&
		!p2p_equal(friends->party.leader_game_hash, zero, P2P_KEY_HASH_SIZE) &&
		p2p_equal(friends->party.leader_game_hash, friends->party.joined_hash, P2P_KEY_HASH_SIZE);
	pthread_mutex_unlock(&friends_lock);
	return quiet;
}

int p2p_party_leading(void)
{
	int leading;

	pthread_mutex_lock(&friends_lock);
	leading = party_leading_now() && friends->party.count > 1;
	pthread_mutex_unlock(&friends_lock);
	return leading;
}

const char *p2p_party_outcome_text(int outcome)
{
	switch (outcome)
	{
	case P2P_PARTY_DONE:
		return T("Done");
	case P2P_PARTY_GONE:
		return T("That's gone");
	case P2P_PARTY_NOT_LEADER:
		return T("Only the party leader can do that");
	case P2P_PARTY_NOT_FRIEND:
		return T("Only friends can be invited");
	case P2P_PARTY_OFFLINE:
		return T("Go online first");
	case P2P_PARTY_APPEARING_OFFLINE:
		return T("You're appearing offline");
	case P2P_PARTY_FULL:
		return T("The party is full");
	case P2P_PARTY_ALREADY_IN:
		return T("Already in your party");
	case P2P_PARTY_TOO_SOON:
		return T("Invited a moment ago");
	case P2P_PARTY_NOT_IN_PARTY:
		return T("Not in a party");
	case P2P_PARTY_FRIEND_OFFLINE:
		return T("They're offline");
	case P2P_PARTY_EMPTY:
		return T("Nothing to send");
	case P2P_PARTY_SLOW_DOWN:
		return T("Slow down: too many messages");
	}
	return "";
}

int p2p_party_take_invite(struct p2p_party_invite *invite)
{
	int index, found = 0;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = 0; index < friends->party_invite_count && !found; index++)
	{
		struct party_invite_record *record = &friends->party_invites[index];
		const struct friend_record *from = find_friend(record->key);

		if (record->asked || record->accepted || !from)
			continue;
		record->asked = 1;
		found = 1;
		memset(invite, 0, sizeof(*invite));
		p2p_hex(record->party_id, 8, invite->id);
		snprintf(invite->name, sizeof(invite->name), "%s", from->name);
		invite->size = record->size;
	}
	pthread_mutex_unlock(&friends_lock);
	return found;
}

int p2p_party_invites(struct p2p_party_invite *invites, int maximum_count)
{
	int index, count = 0;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = 0; index < friends->party_invite_count && count < maximum_count; index++)
	{
		const struct party_invite_record *record = &friends->party_invites[index];
		const struct friend_record *from = find_friend(record->key);

		if (!from || record->accepted)
			continue;
		memset(&invites[count], 0, sizeof(invites[count]));
		p2p_hex(record->party_id, 8, invites[count].id);
		snprintf(invites[count].name, sizeof(invites[count].name), "%s", from->name);
		invites[count].size = record->size;
		count++;
	}
	pthread_mutex_unlock(&friends_lock);
	return count;
}

int p2p_party_answer(const char *invite_id, int accept)
{
	int index, result = P2P_PARTY_GONE;

	pthread_mutex_lock(&friends_lock);
	load();
	for (index = 0; invite_id && index < friends->party_invite_count; index++)
	{
		struct party_invite_record *record = &friends->party_invites[index];
		struct friend_record *from = find_friend(record->key);
		char each[P2P_FRIEND_ID_SIZE];
		struct message message;

		p2p_hex(record->party_id, 8, each);
		if (strcmp(each, invite_id) || !from)
			continue;
		if (!friends_on())
		{
			result = P2P_PARTY_OFFLINE;
			break;
		}
		if (friends->appear_offline)
		{
			result = P2P_PARTY_APPEARING_OFFLINE;
			break;
		}
		message_start(&message, TYPE_PARTY_ANSWER, from->key);
		memcpy(message.party_id, record->party_id, PARTY_ID_LENGTH);
		message.party_value = accept ? 1 : 0;
		party_send_to(from->key, &message);
		platform_log("party: %s %s's party invite", accept ? "accepted" : "declined", from->name);
		if (accept)
		{
			/* (the leader's roster makes this machine a member) */
			record->accepted = 1;
			record->asked = 1;
			record->time = now_stamp();
		}
		else
		{
			memmove(&friends->party_invites[index], &friends->party_invites[index + 1],
				sizeof(friends->party_invites[0]) * (size_t)(friends->party_invite_count - index - 1));
			friends->party_invite_count--;
		}
		result = P2P_PARTY_DONE;
		break;
	}
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_follow_pending(struct p2p_party_follow *follow)
{
	int pending;

	pthread_mutex_lock(&friends_lock);
	pending = friends->party.active && friends->party.follow_pending && !party_leading_now();
	if (pending && follow)
	{
		const struct message *message = &friends->party.follow_message;

		memset(follow, 0, sizeof(*follow));
		follow->serial = message->party_serial;
		party_name_of(friends->party.members[friends->party.leader].key, "", follow->leader, sizeof(follow->leader));
		follow->hosting = (message->party_value & PARTY_FOLLOW_HOSTING) != 0;
		follow->state = message->game.state;
		snprintf(follow->map, sizeof(follow->map), "%s", message->game.map);
		snprintf(follow->gametype, sizeof(follow->gametype), "%s", message->game.gametype);
		follow->pc_map = message->game.pc_map;
		follow->players = message->game.players;
		follow->maximum = message->game.maximum;
		p2p_friends_activity_text(follow->activity, sizeof(follow->activity), message->game.state, message->game.map,
			message->game.gametype, message->game.engine, message->game.difficulty, message->game.pc_map);
	}
	pthread_mutex_unlock(&friends_lock);
	return pending;
}

int p2p_party_follow_join(unsigned long serial)
{
	char link[P2P_LINK_SIZE], leader[P2P_PARTY_NAME_SIZE];
	int result, wire = 0;

	pthread_mutex_lock(&friends_lock);
	if (!friends->party.active || !friends->party.follow_pending || friends->party.follow_message.party_serial != serial)
	{
		pthread_mutex_unlock(&friends_lock);
		return P2P_PARTY_FOLLOW_NONE;
	}
	result = party_follow_precheck(&friends->party.follow_message);
	friends->party.follow_pending = 0;
	friends->party.follow_done = serial;
	if (!result)
	{
		invite_link(friends->party.follow_message.game.invite_hash, friends->party.follow_message.game.invite_token, link);
		/* (on the leader's game's wire, whichever: network_wire.h) */
		wire = game_wire_of(friends->party.follow_message.game.version, friends->party.follow_message.game.wire);
		party_name_of(friends->party.members[friends->party.leader].key, "", leader, sizeof(leader));
	}
	pthread_mutex_unlock(&friends_lock);
	if (!result)
	{
		/* (the invite's own join, as an invite accepted joins: the host's
		checks are where they always are) */
		platform_log("party: following %s into their game", leader);
		result = p2p_join_invite_on(link, wire) ? P2P_PARTY_FOLLOW_JOINING : P2P_PARTY_FOLLOW_FAILED;
	}
	p2p_party_follow_result(serial, result);
	return result;
}

void p2p_party_follow_result(unsigned long serial, int result)
{
	if (result < 0 || result >= P2P_PARTY_FOLLOW_COUNT)
		return;
	pthread_mutex_lock(&friends_lock);
	if (friends->party.active && (serial == friends->party.follow_done || serial == friends->party.follow_heard))
	{
		if (result != friends->party.follow_result)
		{
			friends->party.follow_result = result;
			friends->party.status_due = 1;
			if (result >= P2P_PARTY_FOLLOW_PASSWORD)
				party_notice(T("Couldn't follow the leader: %s"), p2p_party_follow_text(result));
		}
		/* (waiting: joined once back at the menus; anything else: done) */
		if (result != P2P_PARTY_FOLLOW_WAITING)
		{
			friends->party.follow_pending = 0;
			friends->party.follow_done = serial;
		}
	}
	pthread_mutex_unlock(&friends_lock);
}

const char *p2p_party_follow_text(int result)
{
	switch (result)
	{
	case P2P_PARTY_FOLLOW_JOINING:
		return T("joining");
	case P2P_PARTY_FOLLOW_THERE:
		return T("there already");
	case P2P_PARTY_FOLLOW_WAITING:
		return T("in another game");
	case P2P_PARTY_FOLLOW_PASSWORD:
		return T("the game has a password");
	case P2P_PARTY_FOLLOW_FULL:
		return T("the game is full");
	case P2P_PARTY_FOLLOW_NO_MAP:
		return T("the map is missing (map downloads off)");
	case P2P_PARTY_FOLLOW_VERSION:
		return T("another version of the game");
	case P2P_PARTY_FOLLOW_UNREACHABLE:
		return T("the game can't be reached online");
	case P2P_PARTY_FOLLOW_FAILED:
		return T("the join failed");
	}
	return "";
}

int p2p_party_join_warning(int players, int maximum, char *text, int size)
{
	int count, warn;

	pthread_mutex_lock(&friends_lock);
	count = party_leading_now() ? friends->party.count : 0;
	warn = count > 1 && maximum > 0 && maximum - players < count;
	if (warn && text && size > 0)
		snprintf(text, (size_t)size, T("Your party has %d players but this game has %d open slots: some may not get in "
			"(they stay in the party)."), count, maximum - players > 0 ? maximum - players : 0);
	pthread_mutex_unlock(&friends_lock);
	return warn;
}

/* ---------- chat */

static int party_chat_send_message(int kind, int preset, const char *text)
{
	struct message message;
	char name[P2P_FRIEND_NAME_SIZE];

	if (!friends->party.active || friends->party.count < 2)
		return P2P_PARTY_NOT_IN_PARTY;
	if (!friends_on())
		return P2P_PARTY_OFFLINE;
	if (friends->appear_offline)
		return P2P_PARTY_APPEARING_OFFLINE;
	if (!budget_left(&friends->chat_send_budget, PARTY_CHAT_SEND_BUDGET, PARTY_CHAT_SEND_INTERVAL, 1))
		return P2P_PARTY_SLOW_DOWN;
	party_start_message(&message, TYPE_PARTY_CHAT, NULL);
	message.party_code = kind;
	message.party_value = preset;
	if (kind == PARTY_CHAT_TEXT)
		snprintf(message.chat, sizeof(message.chat), "%s", text);
	party_broadcast(&message);
	own_name(name);
	party_chat_add(friends->key, name, kind == PARTY_CHAT_PRESET ? party_presets[preset] : text, 1,
		kind == PARTY_CHAT_PRESET ? 2 : 0);
	platform_log("party: chat sent: %s", kind == PARTY_CHAT_PRESET ? party_presets[preset] : text);
	return P2P_PARTY_DONE;
}

int p2p_party_chat_send(const char *text)
{
	char cleaned[P2P_PARTY_CHAT_SIZE];
	int result;

	if (!p2p_party_chat_clean(cleaned, sizeof(cleaned), text))
		return P2P_PARTY_EMPTY;
	pthread_mutex_lock(&friends_lock);
	result = party_chat_send_message(PARTY_CHAT_TEXT, 0, cleaned);
	pthread_mutex_unlock(&friends_lock);
	return result;
}

int p2p_party_chat_preset(int preset)
{
	int result;

	if (preset < 0 || preset >= P2P_PARTY_PRESETS)
		return P2P_PARTY_EMPTY;
	pthread_mutex_lock(&friends_lock);
	result = party_chat_send_message(PARTY_CHAT_PRESET, preset, NULL);
	pthread_mutex_unlock(&friends_lock);
	return result;
}

const char *p2p_party_chat_preset_text(int preset)
{
	return preset >= 0 && preset < P2P_PARTY_PRESETS ? T(party_presets[preset]) : "";
}

int p2p_party_chat_lines(struct p2p_party_chat_line *lines, int maximum_count, unsigned long after)
{
	int index, count = 0;

	pthread_mutex_lock(&friends_lock);
	for (index = 0; index < friends->chat_count && count < maximum_count; index++)
	{
		const struct party_chat_record *record = &friends->chat[index];
		struct p2p_party_chat_line *line;

		if (record->number <= after)
			continue;
		line = &lines[count++];
		memset(line, 0, sizeof(*line));
		line->number = record->number;
		line->self = record->self;
		line->notice = record->notice == 1;
		if (record->notice != 1)
			party_name_of(record->key, party_member_by_key(record->key) ? party_member_by_key(record->key)->name : "",
				line->name, sizeof(line->name));
		/* (a preset in the language chosen here) */
		snprintf(line->text, sizeof(line->text), "%s", record->notice == 2 ? T(record->text) : record->text);
	}
	pthread_mutex_unlock(&friends_lock);
	return count;
}

unsigned long p2p_party_chat_newest(void)
{
	unsigned long newest;

	pthread_mutex_lock(&friends_lock);
	newest = friends->chat_count ? friends->chat[friends->chat_count - 1].number : 0;
	pthread_mutex_unlock(&friends_lock);
	return newest;
}
