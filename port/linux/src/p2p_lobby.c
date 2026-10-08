/*
P2P_LOBBY.C

Internet play's public games (the server browser; on the Vita, the settings
panel's Browse public games): a public game's host publishes a signed
listing of it through the signalling brokers (p2p_signal.c), and browsers
gather the listings and join a game by the invite a listing holds, as an
invite link would. Ported from OpenCE's server browser
(https://github.com/OpenCommunityEdition/OpenCE: port/linux/src/p2p_lobby.c
of "Server browser of public internet games" and "Password-protected public
lobbies"), for the Vita's own listings.

The listing (network byte order) is signed with the host's Ed25519 key,
whose X25519 form's hash is the invite's host part (p2p.c), so no one but
the host can list its invite, alter its listing, or list another's under
false details:

	"HL", the format (LISTING_FORMAT), the lobby version (2:
	HALO_PORT_NETWORK_VERSION; browsers hide others), flags (open, under
	way, teams, closed, password, a Halo PC map, co-op), sequence (4: newest
	wins), Unix time (4), the Ed25519 key (32), the invite's token (16; a
	password's game's sealed with the password's key, 56: p2p_seal_token; a
	tombstone's zero), players, most players, the gametype's engine (1
	each), the game's name, map and gametype (a length byte, then up to 32,
	32 and 24 characters of printable ASCII), the score to win (2), a co-op
	game's difficulty (1: 0 to 3), the players' names (a count, at most
	LISTED_PLAYERS, then each as a length byte and up to
	LISTED_PLAYER_NAME_SIZE characters), a stamp (8, reserved: zero), and
	the signature (64) of SIGNATURE_LABEL and all before it.

The Vita's listings are OpenCE's (format 1) with the score, the difficulty
and the players' names added (format 2), on the Vitas' own topics
(P2P_SIGNAL_PREFIX "hcev": Vitas play only Vitas) and signed under a label
of their own ("hcev-lobby-1"), so that no listing of either kind can be
passed off as the other's, even if copied onto the other's topics.

Each host has a slot, hcev/3/lobby/s/<its key's hash in hex>: its listing,
retained on every broker (and expiring there after LISTING_EXPIRY seconds
on MQTT 5), and the broker's will (set at the connection, p2p_signal.c)
clears it if the host vanishes. Browsers subscribe to every slot and ask the
hosts to publish again (hcev/3/lobby/q). Brokers are other people's: what
one deletes or keeps is no authority, so
- a host publishes every REPUBLISH_INTERVAL, and sooner (at most once each
  TRIGGER_INTERVAL) when its game changes, when asked, and when its slot is
  heard to hold anything but its listing (cleared, another's, or an older
  one of its own): a deletion is undone in seconds;
- a browser takes only a signed listing whose key's hash is its slot's,
  keeps the newest of each host (by sequence), drops one not heard in
  GAME_EXPIRY (on its own clock; a retained copy is taken only if its time
  is within RETAINED_WINDOW of this machine's, from a host that died and
  left it), takes a host's closing listing (a tombstone) as its end, and
  ignores an emptied slot (a wipe is not a delete).
A host that stops publishes a tombstone, then clears its slot. Going private
makes a new invite (p2p.c), so a listing seen before lets no one in.

A game with a password (p2p_lobby_set_password) is listed with its token
sealed with the password's key (Argon2id of the password, salted with the
host's key: p2p_password_key), so that only who knows the password can join
it from the browser (p2p_lobby_join); its code and invite link, which lead
to the token, still join it as they do any game. Setting or changing the
password makes a new invite, as going private does. The key takes about a
second on a Vita, so it is worked out on a thread of its own (key_thread),
for the host and for a joiner alike; the host's game is not listed until its
key is ready.

Checking signatures takes work, and anyone can send listings: at most
VERIFY_BUDGET milliseconds of it each pass of the p2p thread (never the
game's frame), from a queue of MAXIMUM_QUEUED; the rest is dropped (hosts
publish again). A game's name, as the players' names, is cleaned as the game
cleans a player's name (player_name_clean), and a game whose name has
nothing left that names it is not shown.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "halo_port_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
	LISTING_FORMAT = 2,
	_listing_open = 1,
	_listing_in_progress = 2,
	_listing_has_teams = 4,
	/* a tombstone: the host stopped */
	_listing_closed = 8,
	/* its token sealed with a password's key */
	_listing_password = 16,
	/* (format 2) on a Halo PC (Custom Edition) map; co-op on a campaign
	level */
	_listing_pc_map = 32,
	_listing_coop = 64,
	/* a dedicated server's game (port/linux/DEDICATED_SERVER.md): a host
	with no player of its own. Browsers before it ignore the bit (the
	listing reads as before), and show the game as any other */
	_listing_dedicated = 128,
	STAMP_SIZE = 8,
	LISTED_PLAYERS = P2P_LISTING_PLAYERS,
	LISTED_PLAYER_NAME_SIZE = P2P_LISTING_PLAYER_NAME_SIZE,
	MAXIMUM_LISTING_SIZE = 2 + 1 + 2 + 1 + 4 + 4 + P2P_KEY_SIZE + P2P_SEALED_TOKEN_SIZE + 3 +
		(1 + P2P_LISTING_NAME_SIZE) + (1 + P2P_LISTING_MAP_SIZE) + (1 + P2P_LISTING_GAMETYPE_SIZE) + 2 + 1 +
		1 + LISTED_PLAYERS * (1 + LISTED_PLAYER_NAME_SIZE) + STAMP_SIZE + P2P_SIGNATURE_SIZE,
	MINIMUM_LISTING_SIZE = 2 + 1 + 2 + 1 + 4 + 4 + P2P_KEY_SIZE + P2P_TOKEN_SIZE + 3 + 3 + 2 + 1 + 1 + STAMP_SIZE +
		P2P_SIGNATURE_SIZE,
	MAXIMUM_GAMES = 64,
	MAXIMUM_TOMBSTONES = 128,
	MAXIMUM_QUEUED = 64,

	/* milliseconds */
	REPUBLISH_INTERVAL = 30000,
	TRIGGER_INTERVAL = 5000,
	QUERY_JITTER = 2000,
	GAME_EXPIRY = 90000,
	TOMBSTONE_TIME = 600000,
	VERIFY_BUDGET = 2,
	/* seconds */
	RETAINED_WINDOW = 600,
};

#define SIGNATURE_LABEL P2P_SIGNAL_PREFIX "-lobby-1"
static const char signature_label[] = SIGNATURE_LABEL;

typedef char check_listing_size[MAXIMUM_LISTING_SIZE <= P2P_MAXIMUM_LISTING_SIZE ? 1 : -1];

/* a listing, read */
struct listing
{
	int version;
	int flags;
	unsigned long sequence;
	unsigned long time;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char token[P2P_TOKEN_SIZE];
	/* (_listing_password: its token, sealed) */
	unsigned char sealed_token[P2P_SEALED_TOKEN_SIZE];
	int player_count, maximum_player_count, engine_type;
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	int score_limit, difficulty;
	int listed_player_count;
	char players[LISTED_PLAYERS][LISTED_PLAYER_NAME_SIZE + 1];
	/* the signed part's size */
	int signed_size;
};

struct game
{
	int used;
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	unsigned long sequence;
	unsigned long heard_time;
	/* as it came (one the same again needs no check) */
	unsigned char payload[MAXIMUM_LISTING_SIZE];
	int payload_size;
	struct p2p_listing listing;
};

struct tombstone
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	unsigned long sequence;
	unsigned long time;
};

struct queued
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	unsigned char payload[MAXIMUM_LISTING_SIZE];
	int size;
	int retained;
};

static struct
{
	/* hosting: the game as the game's server tells it
	(p2p_set_game_listing), and whether it is public (p2p_lobby_set_public;
	network.host_public until said) */
	int public_said;
	int public;
	/* a co-op game's own (network.coop_public, OpenCE's: private unless
	chosen), which the other's never changes */
	int coop_public_said;
	int coop_public;
	/* the game's details told once (p2p_set_game_listing_details): until
	then it is not known whether the game is a co-op one, whose visibility
	is its own */
	int details_told;
	char chosen_name[P2P_LISTING_NAME_SIZE + 1];
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	int engine_type;
	int flags;
	int score_limit, difficulty;
	int listed_player_count;
	char players[LISTED_PLAYERS][LISTED_PLAYER_NAME_SIZE + 1];
	int player_count, maximum_player_count;
	/* the password's key (p2p_lobby_set_password), if it has one; while a
	new password's is worked out (password_pending), the game is not listed */
	int has_password;
	unsigned char password_key[P2P_PASSWORD_KEY_SIZE];
	int password_pending;
	/* its key could not be worked out (no memory): not listed either */
	int password_failed;
	unsigned long password_generation;
	/* whether the password was set (else network.lobby_password is taken) */
	int password_said;
	char password[P2P_LOBBY_PASSWORD_SIZE];
	/* listed: with this token; its listing as last published */
	int listed;
	unsigned char token[P2P_TOKEN_SIZE];
	unsigned long sequence;
	unsigned char listing[MAXIMUM_LISTING_SIZE];
	int listing_size;
	unsigned long published_time;
	/* a publish wanted (the game changed, a query, the slot needs mending),
	not before this */
	int republish_wanted;
	unsigned long republish_time;

	/* browsing */
	int browsing;
	int query_wanted;
	struct queued queue[MAXIMUM_QUEUED];
	int queue_count;
	struct game games[MAXIMUM_GAMES];
	struct tombstone tombstones[MAXIMUM_TOMBSTONES];
	int next_tombstone;
	/* the identifiers of games joining failed */
	unsigned char failed[MAXIMUM_GAMES][P2P_IDENTIFIER_SIZE];
	int failed_count;

	/* joining a game from the browser (p2p_lobby_join): how it goes, the
	host's key hash, and a locked game's password, key and sealed token
	for key_thread */
	int join_state;
	unsigned char join_key_hash[P2P_KEY_HASH_SIZE];
	int join_pending;
	char join_password[P2P_LOBBY_PASSWORD_SIZE];
	unsigned char join_signing_key[P2P_KEY_SIZE];
	unsigned char join_sealed_token[P2P_SEALED_TOKEN_SIZE];
	/* key_thread runs */
	int key_thread_running;
} lobby;

static int elapsed(unsigned long since, unsigned long time)
{
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
}

/* printable ASCII, as the menus' font has it (others: '?'), cut to size */
static void sanitize(char *destination, int size, const char *source, int source_size)
{
	int index;

	for (index = 0; index < size && index < source_size && source[index]; index++)
	{
		unsigned char character = (unsigned char)source[index];

		destination[index] = character >= 0x20 && character < 0x7F ? (char)character : '?';
	}
	destination[index] = 0;
}

int p2p_lobby_clean_name(char *name)
{
	int read, written = 0, visible = 0;

	/* (player_name_clean's rules, for printable ASCII: no "|" (the game's
	text's own marks), no spaces before or after, and a character that is
	not a space) */
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

void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	char text[2 * P2P_KEY_HASH_SIZE + 1];

	p2p_hex(key_hash, P2P_KEY_HASH_SIZE, text);
	snprintf(topic, (size_t)size, "%s%s", P2P_LOBBY_SLOT_PREFIX, text);
}

/* the hash of an Ed25519 key's X25519 form (an invite's host part); 0 if
the key has a small order */
static int signing_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char x25519[P2P_KEY_SIZE];

	if (!p2p_ed25519_to_x25519(key, x25519))
		return 0;
	p2p_key_hash(x25519, hash);
	return 1;
}

/* ---------- the listing */

static void put_long(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

static unsigned long get_long(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

static int put_text(unsigned char *bytes, const char *text, int maximum)
{
	int length = (int)strlen(text);

	length = length > maximum ? maximum : length;
	bytes[0] = (unsigned char)length;
	memcpy(bytes + 1, text, (size_t)length);
	return 1 + length;
}

/* the listing of the game hosted, signed: flags (_listing_closed for a
tombstone) */
static int listing_make(unsigned char *bytes, int flags)
{
	unsigned char data[sizeof(signature_label) - 1 + MAXIMUM_LISTING_SIZE];
	int size = 0;
	int count, index;

	if (lobby.has_password && !(flags & _listing_closed))
		flags |= _listing_password;
#ifdef HALO_DEDICATED_SERVER
	flags |= _listing_dedicated;
#endif
	bytes[size++] = 'H';
	bytes[size++] = 'L';
	bytes[size++] = LISTING_FORMAT;
	bytes[size++] = (unsigned char)(HALO_PORT_NETWORK_VERSION >> 8);
	bytes[size++] = (unsigned char)HALO_PORT_NETWORK_VERSION;
	bytes[size++] = (unsigned char)flags;
	put_long(bytes + size, ++lobby.sequence);
	size += 4;
	put_long(bytes + size, (unsigned long)time(NULL));
	size += 4;
	memcpy(bytes + size, p2p_signing_key(), P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	if (flags & _listing_password)
	{
		p2p_seal_token(lobby.password_key, p2p_signing_key(), lobby.token, bytes + size);
		size += P2P_SEALED_TOKEN_SIZE;
	}
	else
	{
		/* (a tombstone's none: it may end a game with a password) */
		if (flags & _listing_closed)
			memset(bytes + size, 0, P2P_TOKEN_SIZE);
		else
			memcpy(bytes + size, lobby.token, P2P_TOKEN_SIZE);
		size += P2P_TOKEN_SIZE;
	}
	bytes[size++] = (unsigned char)(lobby.player_count > 255 ? 255 : lobby.player_count);
	bytes[size++] = (unsigned char)(lobby.maximum_player_count > 255 ? 255 : lobby.maximum_player_count);
	bytes[size++] = (unsigned char)lobby.engine_type;
	size += put_text(bytes + size, lobby.name, P2P_LISTING_NAME_SIZE);
	size += put_text(bytes + size, lobby.map, P2P_LISTING_MAP_SIZE);
	size += put_text(bytes + size, lobby.gametype, P2P_LISTING_GAMETYPE_SIZE);
	bytes[size++] = (unsigned char)(lobby.score_limit >> 8);
	bytes[size++] = (unsigned char)lobby.score_limit;
	bytes[size++] = (unsigned char)lobby.difficulty;
	/* (a tombstone's none) */
	count = flags & _listing_closed ? 0 : lobby.listed_player_count;
	bytes[size++] = (unsigned char)count;
	for (index = 0; index < count; index++)
		size += put_text(bytes + size, lobby.players[index], LISTED_PLAYER_NAME_SIZE);
	memset(bytes + size, 0, STAMP_SIZE);
	size += STAMP_SIZE;
	memcpy(data, signature_label, sizeof(signature_label) - 1);
	memcpy(data + sizeof(signature_label) - 1, bytes, (size_t)size);
	p2p_sign(data, (int)(sizeof(signature_label) - 1) + size, bytes + size);
	return size + P2P_SIGNATURE_SIZE;
}

static int get_text(const unsigned char *bytes, int size, int *offset, char *text, int maximum)
{
	int length;

	if (*offset >= size)
		return 0;
	length = bytes[(*offset)++];
	if (length > maximum || *offset + length > size)
		return 0;
	sanitize(text, maximum, (const char *)bytes + *offset, length);
	*offset += length;
	return 1;
}

static int p2p_lobby_listing_read(const unsigned char *bytes, int size, struct listing *listing)
{
	int offset = 0;
	int index;

	memset(listing, 0, sizeof(*listing));
	if (size < MINIMUM_LISTING_SIZE || size > MAXIMUM_LISTING_SIZE || bytes[0] != 'H' || bytes[1] != 'L' ||
		bytes[2] != LISTING_FORMAT)
	{
		return 0;
	}
	offset = 3;
	listing->version = bytes[offset] << 8 | bytes[offset + 1];
	offset += 2;
	listing->flags = bytes[offset++];
	listing->sequence = get_long(bytes + offset);
	offset += 4;
	listing->time = get_long(bytes + offset);
	offset += 4;
	memcpy(listing->key, bytes + offset, P2P_KEY_SIZE);
	offset += P2P_KEY_SIZE;
	if (listing->flags & _listing_password)
	{
		/* (its token sealed, which is longer: the counts after it too) */
		if (offset + P2P_SEALED_TOKEN_SIZE + 3 > size)
			return 0;
		memcpy(listing->sealed_token, bytes + offset, P2P_SEALED_TOKEN_SIZE);
		offset += P2P_SEALED_TOKEN_SIZE;
	}
	else
	{
		memcpy(listing->token, bytes + offset, P2P_TOKEN_SIZE);
		offset += P2P_TOKEN_SIZE;
	}
	listing->player_count = bytes[offset++];
	listing->maximum_player_count = bytes[offset++];
	listing->engine_type = bytes[offset++];
	if (!get_text(bytes, size, &offset, listing->name, P2P_LISTING_NAME_SIZE) ||
		!get_text(bytes, size, &offset, listing->map, P2P_LISTING_MAP_SIZE) ||
		!get_text(bytes, size, &offset, listing->gametype, P2P_LISTING_GAMETYPE_SIZE))
	{
		return 0;
	}
	/* (the score, the difficulty and the count of names) */
	if (offset + 4 > size)
		return 0;
	listing->score_limit = bytes[offset] << 8 | bytes[offset + 1];
	listing->difficulty = bytes[offset + 2];
	listing->listed_player_count = bytes[offset + 3];
	offset += 4;
	if (listing->listed_player_count > LISTED_PLAYERS)
		return 0;
	for (index = 0; index < listing->listed_player_count; index++)
	{
		if (!get_text(bytes, size, &offset, listing->players[index], LISTED_PLAYER_NAME_SIZE))
			return 0;
	}
	offset += STAMP_SIZE;
	if (offset + P2P_SIGNATURE_SIZE != size)
		return 0;
	listing->signed_size = offset;
	return 1;
}

static int listing_signed(const unsigned char *bytes, const struct listing *listing)
{
	unsigned char data[sizeof(signature_label) - 1 + MAXIMUM_LISTING_SIZE];

	memcpy(data, signature_label, sizeof(signature_label) - 1);
	memcpy(data + sizeof(signature_label) - 1, bytes, (size_t)listing->signed_size);
	return p2p_ed25519_verify(listing->key, data, (int)(sizeof(signature_label) - 1) + listing->signed_size,
		bytes + listing->signed_size);
}

/* ---------- hosting */

static void publish(void)
{
	lobby.listing_size = listing_make(lobby.listing, lobby.flags);
	lobby.published_time = p2p_now();
	lobby.republish_wanted = 0;
	p2p_signal_lobby_publish(lobby.listing, lobby.listing_size, 0);
}

/* a publish soon: at once if none was lately, else when TRIGGER_INTERVAL
has passed (plus jitter) */
static void republish_soon(unsigned long jitter)
{
	unsigned long earliest = lobby.published_time + TRIGGER_INTERVAL;
	unsigned long when = p2p_now() + jitter;

	if ((long)(earliest - when) > 0)
		when = earliest;
	if (!lobby.republish_wanted || (long)(lobby.republish_time - when) > 0)
		lobby.republish_time = when;
	lobby.republish_wanted = 1;
}

static void stop_listing(void)
{
	unsigned char tombstone[MAXIMUM_LISTING_SIZE];
	int size = listing_make(tombstone, _listing_closed);

	lobby.listed = 0;
	lobby.republish_wanted = 0;
	p2p_signal_lobby_publish(tombstone, size, 1);
	p2p_signal_lobby_topics(0, lobby.browsing);
	platform_log("Internet play: the game is no longer listed in the public games");
}

/* public: as the game says, else as the settings (a co-op game's own,
network.coop_public, private unless set); never with the server browser off
(network.public_lobby) */
static int hosting_public(void)
{
	if (!config_boolean("network.public_lobby"))
		return 0;
	if (lobby.flags & _listing_coop)
		return lobby.coop_public_said ? lobby.coop_public : config_boolean("network.coop_public");
	return lobby.public_said ? lobby.public : config_boolean("network.host_public");
}

/* the name listed: the one chosen, else network.lobby_name, else the
game's; NULL if the game has none yet */
static void listed_name(char *name)
{
	const char *configured = config_string("network.lobby_name");

	if (lobby.chosen_name[0])
		memcpy(name, lobby.chosen_name, P2P_LISTING_NAME_SIZE + 1);
	else if (configured && configured[0])
		sanitize(name, P2P_LISTING_NAME_SIZE, configured, P2P_LISTING_NAME_SIZE);
	else
		memcpy(name, lobby.name, P2P_LISTING_NAME_SIZE + 1);
	if (!p2p_lobby_clean_name(name))
		snprintf(name, P2P_LISTING_NAME_SIZE + 1, "Halo");
}

static void set_password(const char *password);
static void start_key_thread(void);

static void update_hosting(const unsigned char *token, int player_count, int maximum_player_count)
{
	char name[P2P_LISTING_NAME_SIZE + 1];
	int want;

	/* (network.lobby_password, until the game says) */
	if (!lobby.password_said)
	{
		lobby.password_said = 1;
		set_password(config_string("network.lobby_password"));
	}
	/* (a password's key, on its thread: only while internet play runs, as
	this does) */
	if (lobby.password_pending)
		start_key_thread();
	listed_name(name);
	/* (while a password's key is worked out, the game is not listed: it
	would be open) */
	want = token && lobby.details_told && hosting_public() && !lobby.password_pending && !lobby.password_failed;
	if (lobby.listed && (!want || memcmp(token, lobby.token, P2P_TOKEN_SIZE)))
	{
		/* (the same game gone private by itself, as a game that becomes
		co-op does, whose visibility is its own: a new invite, so that the
		one listed lets no one in, as p2p_lobby_set_public's) */
		if (token && !memcmp(token, lobby.token, P2P_TOKEN_SIZE) && lobby.details_told && !hosting_public())
			p2p_new_invite_if_listed();
		stop_listing();
	}
	if (!want)
		return;
	if (player_count != lobby.player_count || maximum_player_count != lobby.maximum_player_count)
	{
		lobby.player_count = player_count;
		lobby.maximum_player_count = maximum_player_count;
		if (lobby.listed)
			republish_soon(0);
	}
	if (strcmp(name, lobby.name))
	{
		memcpy(lobby.name, name, sizeof(lobby.name));
		if (lobby.listed)
			republish_soon(0);
	}
	if (!lobby.listed)
	{
		memcpy(lobby.token, token, P2P_TOKEN_SIZE);
		lobby.listed = 1;
		p2p_signal_lobby_topics(1, lobby.browsing);
		publish();
		platform_log("Internet play: the game is listed in everyone's public games%s",
			lobby.has_password ? ", with a password" : "");
		return;
	}
	if ((lobby.republish_wanted && (long)(p2p_now() - lobby.republish_time) >= 0) ||
		elapsed(lobby.published_time, REPUBLISH_INTERVAL))
	{
		publish();
	}
}

void p2p_lobby_query_heard(void)
{
	if (lobby.listed)
	{
		unsigned short jitter;

		posix_random_bytes(&jitter, sizeof(jitter));
		republish_soon(jitter % QUERY_JITTER);
	}
}

/* the host's own slot was heard: anything but its listing is mended */
static void own_slot_heard(const unsigned char *payload, int size, int retained)
{
	struct listing listing;

	if (!lobby.listed || (size == lobby.listing_size && !memcmp(payload, lobby.listing, (size_t)size)))
		return;
	/* (an older listing of its own, forwarded live, is the echo of a publish
	before the last; as the slot's retained copy, the slot holds it) */
	if (!retained && p2p_lobby_listing_read(payload, size, &listing) &&
		!memcmp(listing.key, p2p_signing_key(), P2P_KEY_SIZE) && listing.sequence < lobby.sequence)
	{
		return;
	}
	republish_soon(0);
}

/* ---------- browsing */

static struct game *find_game(const unsigned char *key_hash)
{
	int index;

	for (index = 0; index < MAXIMUM_GAMES; index++)
	{
		if (lobby.games[index].used && !memcmp(lobby.games[index].key_hash, key_hash, P2P_KEY_HASH_SIZE))
			return &lobby.games[index];
	}
	return NULL;
}

static struct tombstone *find_tombstone(const unsigned char *key_hash)
{
	int index;

	for (index = 0; index < MAXIMUM_TOMBSTONES; index++)
	{
		struct tombstone *tombstone = &lobby.tombstones[index];

		if (tombstone->time && !elapsed(tombstone->time, TOMBSTONE_TIME) &&
			!memcmp(tombstone->key_hash, key_hash, P2P_KEY_HASH_SIZE))
		{
			return tombstone;
		}
	}
	return NULL;
}

static int identifier_failed(const unsigned char *identifier)
{
	int index;

	for (index = 0; index < lobby.failed_count; index++)
	{
		if (!memcmp(lobby.failed[index], identifier, P2P_IDENTIFIER_SIZE))
			return 1;
	}
	return 0;
}

/* hex (lower case, 2 * size digits exactly) to bytes; 0 if it is not */
static int parse_hex(const char *text, unsigned char *bytes, int size)
{
	int index;

	if (strlen(text) != (size_t)(2 * size) || strspn(text, "0123456789abcdef") != (size_t)(2 * size))
		return 0;
	for (index = 0; index < size; index++)
	{
		int high = text[2 * index], low = text[2 * index + 1];

		high = high <= '9' ? high - '0' : high - 'a' + 10;
		low = low <= '9' ? low - '0' : low - 'a' + 10;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	struct game *game;
	struct queued *queued;
	int index;

	if (!parse_hex(hash_text, key_hash, P2P_KEY_HASH_SIZE))
		return;
	{
		unsigned char own[P2P_KEY_HASH_SIZE];

		p2p_key_hash(p2p_public_key(), own);
		if (!memcmp(own, key_hash, P2P_KEY_HASH_SIZE))
			own_slot_heard(payload, size, retained);
	}
	/* (an emptied slot is no news: a wipe is not a delete) */
	if (!lobby.browsing || size < MINIMUM_LISTING_SIZE || size > MAXIMUM_LISTING_SIZE)
		return;
	/* the same listing again: heard, no work */
	game = find_game(key_hash);
	if (game && game->payload_size == size && !memcmp(game->payload, payload, (size_t)size))
	{
		game->heard_time = p2p_now();
		return;
	}
	for (index = 0; index < lobby.queue_count; index++)
	{
		if (lobby.queue[index].size == size && !memcmp(lobby.queue[index].payload, payload, (size_t)size))
			return;
	}
	if (lobby.queue_count == MAXIMUM_QUEUED)
		return;
	queued = &lobby.queue[lobby.queue_count++];
	memcpy(queued->key_hash, key_hash, P2P_KEY_HASH_SIZE);
	memcpy(queued->payload, payload, (size_t)size);
	queued->size = size;
	queued->retained = retained;
}

static void make_invite(const unsigned char *key_hash, const unsigned char *token, char *invite, int size)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char text[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

	memcpy(bytes, key_hash, P2P_KEY_HASH_SIZE);
	memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
	p2p_hex(bytes, sizeof(bytes), text);
	snprintf(invite, (size_t)size, "halo://join/%s", text);
	memset(bytes, 0, sizeof(bytes));
}

/* a queued listing, its signature checked: taken or not */
static void listing_take(const struct queued *queued, const struct listing *listing)
{
	struct tombstone *tombstone = find_tombstone(queued->key_hash);
	struct game *game = find_game(queued->key_hash);
	struct p2p_listing *shown;
	int index;

	if (tombstone && listing->sequence <= tombstone->sequence)
		return;
	if (game && listing->sequence <= game->sequence)
		return;
	if (listing->flags & _listing_closed)
	{
		if (!tombstone)
		{
			tombstone = &lobby.tombstones[lobby.next_tombstone];
			lobby.next_tombstone = (lobby.next_tombstone + 1) % MAXIMUM_TOMBSTONES;
			memcpy(tombstone->key_hash, queued->key_hash, P2P_KEY_HASH_SIZE);
		}
		tombstone->sequence = listing->sequence;
		tombstone->time = p2p_now() ? p2p_now() : 1;
		if (game)
			game->used = 0;
		return;
	}
	if (!game)
	{
		/* the room of one gone longest, if there is none free */
		for (index = 0; index < MAXIMUM_GAMES && lobby.games[index].used; index++)
			;
		if (index == MAXIMUM_GAMES)
		{
			int oldest = 0;

			for (index = 1; index < MAXIMUM_GAMES; index++)
			{
				if ((long)(lobby.games[index].heard_time - lobby.games[oldest].heard_time) < 0)
					oldest = index;
			}
			index = oldest;
		}
		game = &lobby.games[index];
		memset(game, 0, sizeof(*game));
		memcpy(game->key_hash, queued->key_hash, P2P_KEY_HASH_SIZE);
		game->used = 1;
	}
	game->sequence = listing->sequence;
	game->heard_time = p2p_now();
	memcpy(game->payload, queued->payload, (size_t)queued->size);
	game->payload_size = queued->size;
	shown = &game->listing;
	memset(shown, 0, sizeof(*shown));
	shown->locked = (listing->flags & _listing_password) != 0;
	memcpy(shown->key_hash, queued->key_hash, P2P_KEY_HASH_SIZE);
	if (shown->locked)
	{
		/* (its invite once the password opens its token: p2p_lobby_join) */
		memcpy(shown->signing_key, listing->key, P2P_KEY_SIZE);
		memcpy(shown->sealed_token, listing->sealed_token, P2P_SEALED_TOKEN_SIZE);
	}
	else
	{
		make_invite(queued->key_hash, listing->token, shown->invite, sizeof(shown->invite));
	}
	p2p_identifier_from_hash(queued->key_hash, shown->identifier);
	memcpy(shown->name, listing->name, sizeof(shown->name));
	memcpy(shown->map, listing->map, sizeof(shown->map));
	memcpy(shown->gametype, listing->gametype, sizeof(shown->gametype));
	shown->player_count = (unsigned char)listing->player_count;
	shown->maximum_player_count = (unsigned char)listing->maximum_player_count;
	shown->engine_type = (unsigned char)listing->engine_type;
	shown->open = (listing->flags & _listing_open) != 0;
	shown->in_progress = (listing->flags & _listing_in_progress) != 0;
	shown->has_teams = (listing->flags & _listing_has_teams) != 0;
	shown->pc_map = (listing->flags & _listing_pc_map) != 0;
	shown->coop = (listing->flags & _listing_coop) != 0;
	shown->dedicated = (listing->flags & _listing_dedicated) != 0;
	shown->score_limit = listing->score_limit;
	shown->difficulty = listing->difficulty;
	shown->listed_player_count = 0;
	for (index = 0; index < listing->listed_player_count; index++)
	{
		char name[LISTED_PLAYER_NAME_SIZE + 1];

		memcpy(name, listing->players[index], sizeof(name));
		/* (a player's name with nothing that names it: left out) */
		if (p2p_lobby_clean_name(name))
			memcpy(shown->players[shown->listed_player_count++], name, sizeof(name));
	}
	shown->ping = -1;
}

/* checks a queued listing: 1 if it is to be taken (its signature too, with
check), as a browser does */
static int listing_acceptable(const struct queued *queued, struct listing *listing, int check)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];

	if (!p2p_lobby_listing_read(queued->payload, queued->size, listing) ||
		listing->version != HALO_PORT_NETWORK_VERSION || !signing_key_hash(listing->key, key_hash) ||
		memcmp(key_hash, queued->key_hash, P2P_KEY_HASH_SIZE))
	{
		return 0;
	}
	/* (a game whose name names nothing is not shown, as a player with such a
	name would not be: d578f88b) */
	if (!(listing->flags & _listing_closed) && !p2p_lobby_clean_name(listing->name))
		return 0;
	/* a slot's retained copy: only one of about now (a host that died left
	it, and nothing cleared it) */
	if (queued->retained)
	{
		long difference = (long)(listing->time - (unsigned long)time(NULL));

		if (difference > RETAINED_WINDOW || difference < -RETAINED_WINDOW)
			return 0;
	}
	return !check || listing_signed(queued->payload, listing);
}

static void update_browsing(void)
{
	unsigned long start = p2p_now();
	int index;

	if (lobby.query_wanted)
	{
		lobby.query_wanted = 0;
		p2p_signal_lobby_query();
	}
	/* the queue, within the budget (one at least) */
	while (lobby.queue_count && (p2p_now() == start || !elapsed(start, VERIFY_BUDGET)))
	{
		struct queued queued = lobby.queue[0];
		struct listing listing;
		int good;

		memmove(lobby.queue, lobby.queue + 1, sizeof(*lobby.queue) * (size_t)(--lobby.queue_count));
		if (!listing_acceptable(&queued, &listing, 0))
			continue;
		/* (the work, without the lock: the game's threads need not wait) */
		pthread_mutex_unlock(&p2p_lock);
		good = listing_signed(queued.payload, &listing);
		pthread_mutex_lock(&p2p_lock);
		if (good && lobby.browsing)
			listing_take(&queued, &listing);
	}
	if (lobby.queue_count && !lobby.browsing)
		lobby.queue_count = 0;
	for (index = 0; index < MAXIMUM_GAMES; index++)
	{
		if (lobby.games[index].used && elapsed(lobby.games[index].heard_time, GAME_EXPIRY))
			lobby.games[index].used = 0;
	}
}

/* ---------- p2p.c's side */

void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count)
{
	update_hosting(token, player_count, maximum_player_count);
	update_browsing();
}

int p2p_lobby_listed(void)
{
	return lobby.listed;
}

void p2p_lobby_quit(void)
{
	if (!lobby.listed)
		return;
	stop_listing();
	/* (the game is ending: written now, not on the coming passes) */
	p2p_signal_lobby_quit();
}

int p2p_lobby_browsing(void)
{
	return lobby.browsing;
}

void p2p_lobby_join_timed_out(const unsigned char *host_hash)
{
	unsigned char identifier[P2P_IDENTIFIER_SIZE];

	if (lobby.join_state != P2P_LOBBY_JOIN_JOINING || memcmp(host_hash, lobby.join_key_hash, P2P_KEY_HASH_SIZE))
		return;
	lobby.join_state = P2P_LOBBY_JOIN_GONE;
	p2p_identifier_from_hash(host_hash, identifier);
	if (!identifier_failed(identifier) && lobby.failed_count < MAXIMUM_GAMES)
		memcpy(lobby.failed[lobby.failed_count++], identifier, P2P_IDENTIFIER_SIZE);
}

/* ---------- the passwords' keys, on a thread of their own: the host's
(password_pending) and a joiner's (join_pending), newest first */

static void *key_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&p2p_lock);
	for (;;)
	{
		unsigned char key[P2P_PASSWORD_KEY_SIZE];
		char password[P2P_LOBBY_PASSWORD_SIZE];
		int made;

		if (lobby.password_pending)
		{
			unsigned long generation = lobby.password_generation;

			memcpy(password, lobby.password, sizeof(password));
			pthread_mutex_unlock(&p2p_lock);
			made = p2p_password_key(password, p2p_signing_key(), key);
			pthread_mutex_lock(&p2p_lock);
			if (!made && generation == lobby.password_generation)
			{
				/* (no memory: the game stays unlisted, rather than listed
				with a key no one could open, or open; the next password
				tries again) */
				lobby.password_pending = 0;
				lobby.password_failed = 1;
				p2p_set_status("not enough memory for the password: the game is not listed");
			}
			/* (another password since: that one's key next) */
			else if (generation == lobby.password_generation)
			{
				/* (a new invite, if one was listed: who saw it, with no
				password or another, cannot join with it) */
				p2p_new_invite_if_listed();
				memcpy(lobby.password_key, key, sizeof(key));
				lobby.has_password = 1;
				lobby.password_pending = 0;
				platform_log("Internet play: the game's password is set");
			}
		}
		else if (lobby.join_pending)
		{
			unsigned char signing_key[P2P_KEY_SIZE], sealed[P2P_SEALED_TOKEN_SIZE], key_hash[P2P_KEY_HASH_SIZE];
			unsigned char token[P2P_TOKEN_SIZE];
			char invite[P2P_LINK_SIZE];
			int opened;

			lobby.join_pending = 0;
			memcpy(password, lobby.join_password, sizeof(password));
			memset(lobby.join_password, 0, sizeof(lobby.join_password));
			memcpy(signing_key, lobby.join_signing_key, sizeof(signing_key));
			memcpy(sealed, lobby.join_sealed_token, sizeof(sealed));
			memcpy(key_hash, lobby.join_key_hash, sizeof(key_hash));
			pthread_mutex_unlock(&p2p_lock);
			made = p2p_password_key(password, signing_key, key);
			opened = made && p2p_unseal_token(key, signing_key, sealed, token);
			pthread_mutex_lock(&p2p_lock);
			/* (only if no other join was asked for since) */
			if (!lobby.join_pending && lobby.join_state == P2P_LOBBY_JOIN_UNLOCKING &&
				!memcmp(key_hash, lobby.join_key_hash, sizeof(key_hash)))
			{
				if (opened)
				{
					make_invite(key_hash, token, invite, sizeof(invite));
					lobby.join_state = P2P_LOBBY_JOIN_JOINING;
					p2p_join_invite_locked(invite);
					memset(invite, 0, sizeof(invite));
				}
				else if (!made)
				{
					lobby.join_state = P2P_LOBBY_JOIN_GONE;
					p2p_set_status("not enough memory to try the password");
				}
				else
				{
					lobby.join_state = P2P_LOBBY_JOIN_WRONG_PASSWORD;
					p2p_set_status("the password is not that game's");
				}
			}
			memset(token, 0, sizeof(token));
		}
		else
		{
			break;
		}
		memset(key, 0, sizeof(key));
		memset(password, 0, sizeof(password));
	}
	lobby.key_thread_running = 0;
	pthread_mutex_unlock(&p2p_lock);
	return NULL;
}

/* under p2p_lock: key_thread, if it is not running */
static void start_key_thread(void)
{
	pthread_t thread;

	if (lobby.key_thread_running)
		return;
	lobby.key_thread_running = 1;
	if (pthread_create(&thread, NULL, key_thread, NULL) != 0)
	{
		/* (the work stays pending: a host's game stays unlisted, and a join
		fails, rather than either going without its password) */
		lobby.key_thread_running = 0;
		if (lobby.join_pending)
		{
			lobby.join_pending = 0;
			lobby.join_state = P2P_LOBBY_JOIN_GONE;
		}
		platform_log("Internet play: cannot start the password thread");
		return;
	}
	pthread_detach(thread);
}

/* ---------- the game's side */

void p2p_lobby_set_public(int public)
{
	pthread_mutex_lock(&p2p_lock);
	public = public ? 1 : 0;
	/* (a new invite going private: p2p.c) */
	if (hosting_public() && !public && !(lobby.flags & _listing_coop))
		p2p_new_invite_if_listed();
	lobby.public = public;
	lobby.public_said = 1;
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_lobby_set_coop_public(int public)
{
	pthread_mutex_lock(&p2p_lock);
	public = public ? 1 : 0;
	/* (a new invite going private, as for any game) */
	if (hosting_public() && !public && (lobby.flags & _listing_coop))
		p2p_new_invite_if_listed();
	lobby.coop_public = public;
	lobby.coop_public_said = 1;
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_lobby_coop_public(int *has_password)
{
	int public;

	pthread_mutex_lock(&p2p_lock);
	public = config_boolean("network.public_lobby") &&
		(lobby.coop_public_said ? lobby.coop_public : config_boolean("network.coop_public"));
	if (has_password)
		*has_password = lobby.password_said ? lobby.has_password || lobby.password_pending :
			config_string("network.lobby_password")[0] != 0;
	pthread_mutex_unlock(&p2p_lock);
	return public;
}

void p2p_lobby_set_name(const char *name)
{
	char chosen[P2P_LISTING_NAME_SIZE + 1];

	sanitize(chosen, P2P_LISTING_NAME_SIZE, name ? name : "", P2P_LISTING_NAME_SIZE);
	pthread_mutex_lock(&p2p_lock);
	memcpy(lobby.chosen_name, chosen, sizeof(chosen));
	pthread_mutex_unlock(&p2p_lock);
}

/* under p2p_lock: the host's password, its key worked out on key_thread */
static void set_password(const char *password)
{
	char copy[P2P_LOBBY_PASSWORD_SIZE];
	int has_password;

	/* (as typed, cut to size: never shown, so not cleaned) */
	snprintf(copy, sizeof(copy), "%s", password ? password : "");
	has_password = copy[0] != 0;
	lobby.password_said = 1;
	if (lobby.password_pending ? strcmp(copy, lobby.password) != 0 :
		has_password != lobby.has_password || (has_password && strcmp(copy, lobby.password)))
	{
		lobby.password_generation++;
		lobby.password_failed = 0;
		memcpy(lobby.password, copy, sizeof(copy));
		if (has_password)
		{
			/* (not listed until its key is ready; worked out once internet
			play runs: update_hosting) */
			lobby.password_pending = 1;
		}
		else
		{
			lobby.password_pending = 0;
			p2p_new_invite_if_listed();
			lobby.has_password = 0;
			memset(lobby.password_key, 0, sizeof(lobby.password_key));
			if (lobby.listed)
				republish_soon(0);
		}
	}
	memset(copy, 0, sizeof(copy));
}

void p2p_lobby_set_password(const char *password)
{
	pthread_mutex_lock(&p2p_lock);
	set_password(password);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int open,
	int in_progress, int has_teams)
{
	char new_map[P2P_LISTING_MAP_SIZE + 1];
	char new_gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	char new_name[P2P_LISTING_NAME_SIZE + 1];
	int flags = (open ? _listing_open : 0) | (in_progress ? _listing_in_progress : 0) |
		(has_teams ? _listing_has_teams : 0);

	sanitize(new_name, P2P_LISTING_NAME_SIZE, name ? name : lobby.name, P2P_LISTING_NAME_SIZE);
	sanitize(new_map, P2P_LISTING_MAP_SIZE, map ? map : lobby.map, P2P_LISTING_MAP_SIZE);
	sanitize(new_gametype, P2P_LISTING_GAMETYPE_SIZE, gametype ? gametype : lobby.gametype,
		P2P_LISTING_GAMETYPE_SIZE);
	pthread_mutex_lock(&p2p_lock);
	flags |= lobby.flags & (_listing_pc_map | _listing_coop);
	if (lobby.chosen_name[0] == 0 && !config_string("network.lobby_name")[0] && strcmp(new_name, lobby.name))
	{
		memcpy(lobby.name, new_name, sizeof(lobby.name));
		if (lobby.listed)
			republish_soon(0);
	}
	if (strcmp(new_map, lobby.map) || strcmp(new_gametype, lobby.gametype) || engine_type != lobby.engine_type ||
		flags != lobby.flags)
	{
		memcpy(lobby.map, new_map, sizeof(lobby.map));
		memcpy(lobby.gametype, new_gametype, sizeof(lobby.gametype));
		lobby.engine_type = engine_type;
		lobby.flags = flags;
		if (lobby.listed)
			republish_soon(0);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_set_game_listing_details(int score_limit, int coop_difficulty, int pc_map, const char *player_names)
{
	char players[LISTED_PLAYERS][LISTED_PLAYER_NAME_SIZE + 1];
	int count = 0;
	int flags;

	memset(players, 0, sizeof(players));
	while (player_names && *player_names && count < LISTED_PLAYERS)
	{
		const char *end = strchr(player_names, '\n');
		int length = end ? (int)(end - player_names) : (int)strlen(player_names);

		sanitize(players[count], LISTED_PLAYER_NAME_SIZE, player_names, length);
		if (p2p_lobby_clean_name(players[count]))
			count++;
		player_names += length + (end ? 1 : 0);
	}
	score_limit = score_limit < 0 ? 0 : score_limit > 0xFFFF ? 0xFFFF : score_limit;
	coop_difficulty = coop_difficulty < 0 || coop_difficulty > 3 ? 255 : coop_difficulty;
	pthread_mutex_lock(&p2p_lock);
	lobby.details_told = 1;
	flags = (lobby.flags & ~(_listing_pc_map | _listing_coop)) | (pc_map ? _listing_pc_map : 0) |
		(coop_difficulty != 255 ? _listing_coop : 0);
	if (score_limit != lobby.score_limit || coop_difficulty != lobby.difficulty || flags != lobby.flags ||
		count != lobby.listed_player_count || memcmp(players, lobby.players, sizeof(players)))
	{
		lobby.score_limit = score_limit;
		lobby.difficulty = coop_difficulty;
		lobby.flags = flags;
		lobby.listed_player_count = count;
		memcpy(lobby.players, players, sizeof(players));
		if (lobby.listed)
			republish_soon(0);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_lobby_browse(int on)
{
	pthread_mutex_lock(&p2p_lock);
	on = on && config_boolean("network.public_lobby") ? 1 : 0;
	if (on != lobby.browsing)
	{
		lobby.browsing = on;
		if (!on)
		{
			memset(lobby.games, 0, sizeof(lobby.games));
			lobby.queue_count = 0;
		}
		lobby.query_wanted = on;
		p2p_signal_lobby_topics(lobby.listed, on);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_lobby_refresh(void)
{
	pthread_mutex_lock(&p2p_lock);
	if (lobby.browsing)
		lobby.query_wanted = 1;
	pthread_mutex_unlock(&p2p_lock);
}

/* the order shown: the most players first; then those joining has not
failed, the open ones, and by name */
static int listing_order(const void *first, const void *second)
{
	const struct p2p_listing *a = first, *b = second;

	if (a->player_count != b->player_count)
		return b->player_count - a->player_count;
	if (a->failed != b->failed)
		return a->failed - b->failed;
	if (a->open != b->open)
		return b->open - a->open;
	return strcmp(a->name, b->name);
}

int p2p_lobby_games(struct p2p_listing *games, int maximum_count)
{
	int count = 0;
	int index;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < MAXIMUM_GAMES && count < maximum_count; index++)
	{
		if (!lobby.games[index].used)
			continue;
		games[count] = lobby.games[index].listing;
		games[count].failed = (unsigned char)identifier_failed(games[count].identifier);
		count++;
	}
	pthread_mutex_unlock(&p2p_lock);
	qsort(games, (size_t)count, sizeof(*games), listing_order);
	return count;
}

/* ---------- the Vita's settings panel's side: entries as text */

static const char *const map_titles[][2] = {
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" },
	{ "wizard", "Wizard" },
	{ "a10", "The Pillar of Autumn" }, { "a30", "Halo" }, { "a50", "The Truth and Reconciliation" },
	{ "b30", "The Silent Cartographer" }, { "b40", "Assault on the Control Room" }, { "c10", "343 Guilty Spark" },
	{ "c20", "The Library" }, { "c40", "Two Betrayals" }, { "d20", "Keyes" }, { "d40", "The Maw" },
};

static const char *map_title(const char *map, int pc_map)
{
	size_t index;

	/* (a Halo PC map of an Xbox map's name is not that map) */
	for (index = 0; !pc_map && index < sizeof(map_titles) / sizeof(map_titles[0]); index++)
	{
		if (!strcmp(map, map_titles[index][0]))
			return map_titles[index][1];
	}
	return map;
}

static void entry_from_listing(const struct p2p_listing *listing, struct p2p_lobby_entry *entry)
{
	static const char *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };
	unsigned char own[P2P_KEY_HASH_SIZE];
	int length, index;

	memset(entry, 0, sizeof(*entry));
	p2p_hex(listing->key_hash, P2P_KEY_HASH_SIZE, entry->id);
	snprintf(entry->name, sizeof(entry->name), "%s", listing->name);
	snprintf(entry->map, sizeof(entry->map), "%s", map_title(listing->map, listing->pc_map));
	/* (a variant with no name: its game's) */
	{
		static const char *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };

		snprintf(entry->gametype, sizeof(entry->gametype), "%s", listing->gametype[0] ? listing->gametype :
			listing->engine_type < (int)(sizeof(engines) / sizeof(engines[0])) ? engines[listing->engine_type] : "");
	}
	entry->players = listing->player_count;
	entry->maximum = listing->maximum_player_count;
	entry->score_limit = listing->score_limit;
	entry->compatible = 1;
	p2p_key_hash(p2p_public_key(), own);
	entry->own = !memcmp(own, listing->key_hash, P2P_KEY_HASH_SIZE);
	entry->locked = listing->locked;
	entry->pc_map = listing->pc_map;
	entry->open = listing->open;
	entry->in_progress = listing->in_progress;
	entry->has_teams = listing->has_teams;
	entry->failed = listing->failed;
	entry->dedicated = listing->dedicated;
	/* the Rules line: "Slayer to 50 on Wizard", "Co-op: Halo, Heroic" */
	if (listing->coop)
	{
		length = snprintf(entry->rules, sizeof(entry->rules), "Co-op: %s%s%s", entry->map,
			listing->difficulty < 4 ? ", " : "", listing->difficulty < 4 ? difficulties[listing->difficulty] : "");
	}
	else if (listing->score_limit > 0)
	{
		length = snprintf(entry->rules, sizeof(entry->rules), "%s to %d on %s", entry->gametype[0] ?
			entry->gametype : "A game", listing->score_limit, entry->map);
	}
	else
	{
		length = snprintf(entry->rules, sizeof(entry->rules), "%s on %s", entry->gametype[0] ? entry->gametype :
			"A game", entry->map);
	}
	length = length < (int)sizeof(entry->rules) ? length : (int)sizeof(entry->rules) - 1;
	if (listing->in_progress)
		length += snprintf(entry->rules + length, sizeof(entry->rules) - (size_t)length, ": under way");
	else if (!listing->open)
		length += snprintf(entry->rules + length, sizeof(entry->rules) - (size_t)length, ": full or starting");
	length = length < (int)sizeof(entry->rules) ? length : (int)sizeof(entry->rules) - 1;
	if (listing->pc_map)
		length += snprintf(entry->rules + length, sizeof(entry->rules) - (size_t)length, " (HALO PC)");
	length = length < (int)sizeof(entry->rules) ? length : (int)sizeof(entry->rules) - 1;
	if (listing->dedicated)
		snprintf(entry->rules + length, sizeof(entry->rules) - (size_t)length, " (DEDICATED)");
	/* the Players line: "5 of 16: name, name... +3 more" (the names that
fit, with room left for the end) */
	length = snprintf(entry->players_line, sizeof(entry->players_line), "%d of %d", entry->players, entry->maximum);
	for (index = 0; index < listing->listed_player_count; index++)
	{
		const char *name = listing->players[index];
		int left = entry->players - index - 1;

		/* (": " or ", ", the name, and "... +NNN more" if any are left) */
		if (length + 2 + (int)strlen(name) + (left > 0 ? 14 : 0) >= (int)sizeof(entry->players_line))
			break;
		length += snprintf(entry->players_line + length, sizeof(entry->players_line) - (size_t)length, "%s%s",
			index ? ", " : ": ", name);
	}
	if (index && entry->players > index)
	{
		snprintf(entry->players_line + length, sizeof(entry->players_line) - (size_t)length, "... +%d more",
			entry->players - index);
	}
}

int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry)
{
	static struct p2p_listing games[MAXIMUM_GAMES];
	static pthread_mutex_t games_lock = PTHREAD_MUTEX_INITIALIZER;
	int count;

	if (index < 0)
		return 0;
	pthread_mutex_lock(&games_lock);
	count = p2p_lobby_games(games, MAXIMUM_GAMES);
	if (index < count)
		entry_from_listing(&games[index], entry);
	pthread_mutex_unlock(&games_lock);
	return index < count;
}

int p2p_lobby_join(const char *id, const char *password)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	char invite[P2P_LINK_SIZE];
	struct game *game;
	int result = 0;

	if (!id || !parse_hex(id, key_hash, P2P_KEY_HASH_SIZE))
		return 0;
	pthread_mutex_lock(&p2p_lock);
	game = lobby.browsing ? find_game(key_hash) : NULL;
	if (game && !(lobby.key_thread_running && lobby.join_state == P2P_LOBBY_JOIN_UNLOCKING))
	{
		memcpy(lobby.join_key_hash, key_hash, P2P_KEY_HASH_SIZE);
		if (game->listing.locked)
		{
			snprintf(lobby.join_password, sizeof(lobby.join_password), "%s", password ? password : "");
			memcpy(lobby.join_signing_key, game->listing.signing_key, P2P_KEY_SIZE);
			memcpy(lobby.join_sealed_token, game->listing.sealed_token, P2P_SEALED_TOKEN_SIZE);
			lobby.join_pending = 1;
			lobby.join_state = P2P_LOBBY_JOIN_UNLOCKING;
			p2p_set_status("opening the game \"%s\" with its password", game->listing.name);
			start_key_thread();
		}
		else
		{
			memcpy(invite, game->listing.invite, sizeof(invite));
			lobby.join_state = P2P_LOBBY_JOIN_JOINING;
			p2p_join_invite_locked(invite);
		}
		result = 1;
	}
	else if (!game)
	{
		lobby.join_state = P2P_LOBBY_JOIN_GONE;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_lobby_join_state(void)
{
	int state;

	pthread_mutex_lock(&p2p_lock);
	state = lobby.join_state;
	pthread_mutex_unlock(&p2p_lock);
	return state;
}

void p2p_lobby_mark_failed(const char *id)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	unsigned char identifier[P2P_IDENTIFIER_SIZE];

	if (!id || !parse_hex(id, key_hash, P2P_KEY_HASH_SIZE))
		return;
	p2p_identifier_from_hash(key_hash, identifier);
	pthread_mutex_lock(&p2p_lock);
	if (!identifier_failed(identifier) && lobby.failed_count < MAXIMUM_GAMES)
		memcpy(lobby.failed[lobby.failed_count++], identifier, P2P_IDENTIFIER_SIZE);
	pthread_mutex_unlock(&p2p_lock);
}
