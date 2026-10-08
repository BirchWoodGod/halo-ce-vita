/*
P2P_LOBBY_TEST.C

The server browser's listings and their signatures and passwords
(port/linux/src/p2p_lobby.c, p2p_crypto.c), as the Vita builds them, ported
from OpenCE's tools/p2p_lobby_check.c: RFC 8032's Ed25519 vectors; the X25519
key a seed gives against its Ed25519 key's conversion; bad signatures, other
keys and small-order keys turned away; and a listing from a host (this
program, through stand-ins for p2p.c and p2p_signal.c) to a browser (this
program again): taken, and a tampered one, one on another's slot, an older
one, a replayed one, a retained one of long ago, a tombstone and a listing
older than it each handled as they must be; a game with a password, not
listed until its key is ready, then listed locked (its token nowhere in the
listing) and opened by its password alone; and the Vita's own: a listing
signed under the PCs' label, or of OpenCE's format 1, or of another network
version, turned away; names cleaned as players' are; the entries' Rules and
Players lines. Prints PASS or the failures.

It includes p2p_lobby.c, to reach its reader. run_p2p_lobby_test.sh builds it
with the platform layer's flags (HALO_VITA: the Vitas' topics and label).
*/

#include "../../linux/src/p2p_lobby.c"

#include <stdarg.h>
#include <unistd.h>

/* ---------- stand-ins for the rest of internet play */

/* (the XDK headers' inline functions' state, never used: a sanitized build
keeps them) */
DWORD D3D__RenderState[D3DRS_MAX];

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long clock_now = 1000;
static unsigned char seed[P2P_SEED_SIZE] = { 42 };
static unsigned char signing_key[P2P_KEY_SIZE], x25519_secret[P2P_KEY_SIZE], x25519_public[P2P_KEY_SIZE];
static unsigned char published[P2P_MAXIMUM_LISTING_SIZE];
static int published_size, published_closing, publish_count, new_invites;
static char joined_invite[P2P_LINK_SIZE];
static const char *lobby_password_setting = "";

void posix_random_bytes(void *buffer, unsigned long size)
{
	static unsigned char counter;
	unsigned char *bytes = buffer;
	unsigned long index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(0x5a + counter++ * 7);
}
unsigned long p2p_now(void) { return clock_now; }
int config_boolean(const char *name) { return !strcmp(name, "network.public_lobby"); }
const char *config_string(const char *name)
{
	return !strcmp(name, "network.lobby_password") ? lobby_password_setting : "";
}
void platform_log(const char *format, ...) { (void)format; }

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + 2 * index, "%02x", bytes[index]);
	text[2 * size] = 0;
}

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier)
{
	memcpy(identifier, hash, P2P_IDENTIFIER_SIZE);
	identifier[0] = (unsigned char)((identifier[0] & 0xFC) | 0x02);
}

const unsigned char *p2p_public_key(void) { return x25519_public; }
const unsigned char *p2p_signing_key(void) { return signing_key; }
void p2p_sign(const void *message, int size, unsigned char *signature)
{
	p2p_ed25519_sign(seed, signing_key, message, size, signature);
}
void p2p_new_invite_if_listed(void) { new_invites++; }
void p2p_signal_lobby_topics(int listed, int browsing) { (void)listed; (void)browsing; }
void p2p_signal_lobby_query(void) {}
void p2p_signal_lobby_publish(const unsigned char *listing, int size, int closing)
{
	memcpy(published, listing, (size_t)size);
	published_size = size;
	published_closing = closing;
	publish_count++;
}
void p2p_signal_lobby_quit(void) {}
int p2p_join_invite_locked(const char *text)
{
	snprintf(joined_invite, sizeof(joined_invite), "%s", text);
	return 1;
}
void p2p_set_status(const char *format, ...) { (void)format; }

/* the p2p thread's calls: under p2p_lock, as there (p2p_lobby_update lets
go of it while it checks signatures) */
static void lobby_update(const unsigned char *token, int player_count, int maximum_player_count)
{
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_update(token, player_count, maximum_player_count);
	pthread_mutex_unlock(&p2p_lock);
}

/* the password thread done */
static void wait_for_keys(void)
{
	int tries;

	for (tries = 0; tries < 2000; tries++)
	{
		int running;

		pthread_mutex_lock(&p2p_lock);
		running = lobby.key_thread_running;
		pthread_mutex_unlock(&p2p_lock);
		if (!running)
			return;
		usleep(5000);
	}
}

/* ---------- the checks */

static int failures, checks;

static void check(int good, const char *what)
{
	checks++;
	if (!good)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static void unhex(const char *text, unsigned char *bytes)
{
	size_t index;

	for (index = 0; index < strlen(text) / 2; index++)
	{
		unsigned int byte;

		sscanf(text + 2 * index, "%2x", &byte);
		bytes[index] = (unsigned char)byte;
	}
}

static void rfc8032(const char *seed_hex, const char *public_hex, const char *message_hex, const char *signature_hex)
{
	unsigned char test_seed[32], expected_public[32], public_key[32], message[64], expected[64], signature[64];
	int size = (int)strlen(message_hex) / 2;

	unhex(seed_hex, test_seed);
	unhex(public_hex, expected_public);
	unhex(message_hex, message);
	unhex(signature_hex, expected);
	p2p_ed25519_public(test_seed, public_key, NULL);
	p2p_ed25519_sign(test_seed, public_key, message, size, signature);
	check(!memcmp(public_key, expected_public, 32), "RFC 8032 public key");
	check(!memcmp(signature, expected, 64), "RFC 8032 signature");
	check(p2p_ed25519_verify(public_key, message, size, signature), "RFC 8032 verification");
}

static void crypto_checks(void)
{
	unsigned char test_seed[32], ed[32], secret[32], derived[32], converted[32], signature[64], other_ed[32];
	unsigned char small[32] = { 1 };
	unsigned char forged[64] = { 1 };
	unsigned char key[P2P_PASSWORD_KEY_SIZE], again[P2P_PASSWORD_KEY_SIZE], sealed[P2P_SEALED_TOKEN_SIZE];
	unsigned char token[P2P_TOKEN_SIZE] = { 9, 8, 7 }, opened[P2P_TOKEN_SIZE];
	int index;

	rfc8032("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
		"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
		"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
	rfc8032("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
		"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
		"92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
	/* the X25519 key from a seed is its Ed25519 key's conversion, and
	agrees on secrets (an old build's joiner and a new build's host agree) */
	for (index = 0; index < 16; index++)
	{
		unsigned char other[32], other_public[32], first[32], second[32];

		memset(test_seed, index * 13 + 1, sizeof(test_seed));
		p2p_ed25519_public(test_seed, ed, secret);
		p2p_x25519(derived, secret, NULL);
		check(p2p_ed25519_to_x25519(ed, converted) && !memcmp(converted, derived, 32), "X25519 key of a seed");
		memset(other, index + 99, sizeof(other));
		p2p_x25519(other_public, other, NULL);
		p2p_x25519(first, secret, other_public);
		p2p_x25519(second, other, converted);
		check(!memcmp(first, second, 32), "secrets agree");
	}
	memset(test_seed, 3, sizeof(test_seed));
	p2p_ed25519_public(test_seed, ed, NULL);
	p2p_ed25519_sign(test_seed, ed, "listing", 7, signature);
	check(p2p_ed25519_verify(ed, "listing", 7, signature), "a good signature");
	check(!p2p_ed25519_verify(ed, "listinG", 7, signature), "another message");
	signature[5] ^= 1;
	check(!p2p_ed25519_verify(ed, "listing", 7, signature), "an altered R");
	signature[5] ^= 1;
	signature[40] ^= 1;
	check(!p2p_ed25519_verify(ed, "listing", 7, signature), "an altered S");
	signature[40] ^= 1;
	memset(test_seed, 4, sizeof(test_seed));
	p2p_ed25519_public(test_seed, other_ed, NULL);
	check(!p2p_ed25519_verify(other_ed, "listing", 7, signature), "another key");
	/* the identity, an order-8 point, and a signature anyone makes for the
	identity */
	check(!p2p_ed25519_to_x25519(small, converted), "the identity's conversion");
	unhex("c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a", small);
	check(!p2p_ed25519_to_x25519(small, converted), "an order-8 key's conversion");
	memset(small, 0, sizeof(small));
	small[0] = 1;
	check(!p2p_ed25519_verify(small, "anything", 8, forged), "a small-order key's signature");
	/* a password's key: the same for the same password and salt, another
	for another of either; a token sealed opens only with its key and salt */
	p2p_password_key("hunter2", ed, key);
	p2p_password_key("hunter2", ed, again);
	check(!memcmp(key, again, sizeof(key)), "a password's key is the same each time");
	p2p_password_key("hunter3", ed, again);
	check(memcmp(key, again, sizeof(key)) != 0, "another password, another key");
	p2p_password_key("hunter2", other_ed, again);
	check(memcmp(key, again, sizeof(key)) != 0, "another host (salt), another key");
	p2p_seal_token(key, ed, token, sealed);
	check(p2p_unseal_token(key, ed, sealed, opened) && !memcmp(opened, token, sizeof(token)), "a sealed token opens");
	check(!p2p_unseal_token(again, ed, sealed, opened), "another key opens nothing");
	check(!p2p_unseal_token(key, other_ed, sealed, opened), "another host's key opens nothing");
	sealed[30] ^= 1;
	check(!p2p_unseal_token(key, ed, sealed, opened), "an altered seal opens nothing");
}

static int games(struct p2p_listing *listing)
{
	static struct p2p_listing all[MAXIMUM_GAMES];
	int count = p2p_lobby_games(all, MAXIMUM_GAMES);

	if (count && listing)
		*listing = all[0];
	return count;
}

static const unsigned char *hosting_token;

/* a payload heard on the host's slot (or another), then the browser's pass */
static void hear(const unsigned char *payload, int size, int retained, const char *slot)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];
	char own[2 * P2P_KEY_HASH_SIZE + 1];

	p2p_key_hash(x25519_public, hash);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, own);
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_slot_heard(slot ? slot : own, payload, size, retained);
	pthread_mutex_unlock(&p2p_lock);
	lobby_update(hosting_token, 3, 16);
}

/* the published listing, signed again under label (after changing it) */
static int resign(unsigned char *bytes, int size, const char *label)
{
	unsigned char data[64 + P2P_MAXIMUM_LISTING_SIZE];
	int signed_size = size - P2P_SIGNATURE_SIZE;
	int label_size = (int)strlen(label);

	memcpy(data, label, (size_t)label_size);
	memcpy(data + label_size, bytes, (size_t)signed_size);
	p2p_ed25519_sign(seed, signing_key, data, label_size + signed_size, bytes + signed_size);
	return size;
}

static void lobby_checks(void)
{
	static const unsigned char token[P2P_TOKEN_SIZE] = { 7, 7, 7 };
	unsigned char first[P2P_MAXIMUM_LISTING_SIZE], tampered[P2P_MAXIMUM_LISTING_SIZE];
	int first_size, count;
	struct p2p_listing listing;
	struct p2p_lobby_entry entry;
	char expected_invite[P2P_LINK_SIZE];
	char id[P2P_LOBBY_ID_SIZE];

	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	hosting_token = token;
	/* hosting a public game: listed, under the game's name */
	p2p_lobby_set_public(1);
	p2p_set_game_listing(" Test|game ", "bloodgulch", "Slayer", 2, 1, 0, 1);
	p2p_set_game_listing_details(50, -1, 0, "alpha\n|\nbravo\n  charlie  \n");
	lobby_update(token, 3, 16);
	check(publish_count == 1 && !published_closing && p2p_lobby_listed(), "the host publishes its listing");
	check(published_size <= P2P_MAXIMUM_LISTING_SIZE && published[2] == LISTING_FORMAT, "the listing is format 2");
	memcpy(first, published, (size_t)published_size);
	first_size = published_size;
	/* the browser takes it */
	p2p_lobby_browse(1);
	hear(first, first_size, 0, NULL);
	count = games(&listing);
	check(count == 1, "the browser takes the listing");
	{
		unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
		char text[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

		p2p_key_hash(x25519_public, bytes);
		p2p_hex(bytes, P2P_KEY_HASH_SIZE, id);
		memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
		p2p_hex(bytes, sizeof(bytes), text);
		snprintf(expected_invite, sizeof(expected_invite), "halo://join/%s", text);
	}
	check(count == 1 && !strcmp(listing.invite, expected_invite), "the listing's invite is the host's");
	check(count == 1 && !strcmp(listing.name, "Testgame") && !strcmp(listing.map, "bloodgulch") &&
		!strcmp(listing.gametype, "Slayer") && listing.player_count == 3 && listing.maximum_player_count == 16 &&
		listing.engine_type == 2 && listing.open && listing.has_teams && !listing.in_progress &&
		listing.score_limit == 50, "the listing's details (the name cleaned as a player's)");
	check(count == 1 && listing.listed_player_count == 3 && !strcmp(listing.players[0], "alpha") &&
		!strcmp(listing.players[1], "bravo") && !strcmp(listing.players[2], "charlie"),
		"the players' names, cleaned (one that names nothing left out)");
	check(p2p_lobby_entry(0, &entry) && !strcmp(entry.id, id) && entry.own && !strcmp(entry.map, "Blood Gulch") &&
		!strcmp(entry.rules, "Slayer to 50 on Blood Gulch") &&
		!strcmp(entry.players_line, "3 of 16: alpha, bravo, charlie") && !entry.locked && !entry.pc_map,
		"the entry's map, Rules and Players lines");
	/* joining it: its invite, at once */
	check(p2p_lobby_join(id, NULL) && p2p_lobby_join_state() == P2P_LOBBY_JOIN_JOINING &&
		!strcmp(joined_invite, expected_invite), "joining an open game joins its invite");
	check(!p2p_lobby_join("00112233445566778899aabbccddeeff", NULL) &&
		p2p_lobby_join_state() == P2P_LOBBY_JOIN_GONE, "a game not listed is not joined");
	check(!p2p_lobby_join("not hex", NULL) && !p2p_lobby_join(NULL, NULL), "an id that is not one is not joined");
	/* a change: published again after TRIGGER_INTERVAL */
	p2p_set_game_listing(NULL, NULL, NULL, 2, 1, 1, 1);
	clock_now += 1000;
	lobby_update(token, 3, 16);
	check(publish_count == 1, "no publish sooner than 5 s after the last");
	clock_now += 5000;
	lobby_update(token, 3, 16);
	check(publish_count == 2, "a change published within 5 s");
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "the browser takes the newer listing");
	check(p2p_lobby_entry(0, &entry) && !strcmp(entry.rules, "Slayer to 50 on Blood Gulch: under way"),
		"the Rules line says the game is under way");
	/* the older one again (a replay): no change */
	hear(first, first_size, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "an older listing (a replay) is ignored");
	/* tampered: a newer sequence and another name, with the old signature */
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 50);
	tampered[60] = 'X';
	hear(tampered, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.name[0] == 'T' && listing.in_progress, "a tampered listing is ignored");
	/* signed under the PCs' label, or of OpenCE's format 1, or of another
	network version: turned away (each signed again, as its host would) */
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 60);
	tampered[8 + 0] = tampered[8];
	resign(tampered, published_size, "hceu-lobby-1");
	hear(tampered, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "a listing signed under the PCs' label is ignored");
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 70);
	tampered[2] = 1;
	resign(tampered, published_size, SIGNATURE_LABEL);
	hear(tampered, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "a listing of format 1 is ignored");
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 80);
	tampered[4] ^= 1;
	resign(tampered, published_size, SIGNATURE_LABEL);
	hear(tampered, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "a listing of another network version is ignored");
	/* a good signature, but a name that names nothing: not shown */
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 90);
	{
		int name_offset = 6 + 4 + 4 + P2P_KEY_SIZE + P2P_TOKEN_SIZE + 3;
		int length = tampered[name_offset];

		memset(tampered + name_offset + 1, '|', (size_t)length);
	}
	resign(tampered, published_size, SIGNATURE_LABEL);
	hear(tampered, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.name[0] == 'T', "a listing whose name names nothing is ignored");
	/* on another's slot; on a slot that is not hex */
	hear(published, published_size, 0, "00112233445566778899aabbccddeeff");
	hear(published, published_size, 0, "00112233445566778899AABBCCDDEEFF");
	hear(published, published_size, 0, "0011");
	check(games(NULL) == 1, "a listing on another's slot is ignored");
	/* sizes: cut short, one byte more, empty */
	hear(published, published_size - 1, 0, NULL);
	memcpy(tampered, published, (size_t)published_size);
	tampered[published_size] = 0;
	hear(tampered, published_size + 1, 0, NULL);
	hear(published, 0, 0, NULL);
	check(games(&listing) == 1 && listing.in_progress, "cut, longer and emptied listings change nothing");
	/* expiry: 90 s unheard */
	clock_now += 91000;
	lobby_update(token, 3, 16);
	check(games(NULL) == 0, "a listing unheard for 90 s expires");
	/* the slot's retained copy, of about now: taken (the host published it
	again on its 30 s); one of long ago: not */
	lobby_update(token, 3, 16);
	memcpy(tampered, published, (size_t)published_size);
	tampered[10] = (unsigned char)(tampered[10] - 1);
	resign(tampered, published_size, SIGNATURE_LABEL);
	hear(tampered, published_size, 1, NULL);
	check(games(NULL) == 0, "a retained listing of long ago is ignored");
	hear(published, published_size, 1, NULL);
	check(games(NULL) == 1, "a retained listing of about now is taken");
	/* the host stops: a tombstone, which removes the game */
	new_invites = 0;
	p2p_lobby_set_public(0);
	check(new_invites == 1, "going private makes a new invite");
	lobby_update(token, 3, 16);
	check(published_closing && !p2p_lobby_listed(), "the host publishes a tombstone");
	{
		int offset, found = 0;

		for (offset = 0; offset + P2P_TOKEN_SIZE <= published_size; offset++)
			found |= !memcmp(published + offset, token, P2P_TOKEN_SIZE);
		check(!found, "a tombstone does not hold the token");
	}
	hear(published, published_size, 0, NULL);
	check(games(NULL) == 0, "a tombstone removes the game");
	/* the listing before the tombstone does not bring it back */
	hear(first, first_size, 0, NULL);
	check(games(NULL) == 0, "a listing older than the tombstone is ignored");
	/* listed again (a new sequence): shown again */
	p2p_lobby_set_public(1);
	clock_now += 10000;
	lobby_update(token, 3, 16);
	hear(published, published_size, 0, NULL);
	check(games(NULL) == 1, "a listing newer than the tombstone is taken");
	/* a password: not listed while its key is worked out, then listed
	locked, its token sealed */
	new_invites = 0;
	p2p_lobby_set_password("hunter2");
	wait_for_keys();
	check(lobby.password_pending && !lobby.has_password, "a password's key waits for internet play's thread");
	lobby_update(token, 3, 16);
	check(published_closing && !p2p_lobby_listed(), "not listed (a tombstone) while the password's key is worked out");
	wait_for_keys();
	check(new_invites == 1, "a password makes a new invite");
	clock_now += 6000;
	lobby_update(token, 3, 16);
	check(p2p_lobby_listed() && !published_closing, "listed again once the key is ready");
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 1 && listing.locked && !listing.invite[0], "a game with a password is listed locked");
	{
		int offset, found = 0;

		for (offset = 0; offset + P2P_TOKEN_SIZE <= published_size; offset++)
			found |= !memcmp(published + offset, token, P2P_TOKEN_SIZE);
		check(!found, "a locked listing does not hold its token");
	}
	check(p2p_lobby_entry(0, &entry) && entry.locked, "its entry is locked");
	joined_invite[0] = 0;
	check(p2p_lobby_join(id, "hunter3") && p2p_lobby_join_state() == P2P_LOBBY_JOIN_UNLOCKING,
		"a locked game's join opens it on the password thread");
	wait_for_keys();
	check(p2p_lobby_join_state() == P2P_LOBBY_JOIN_WRONG_PASSWORD && !joined_invite[0], "a wrong password opens nothing");
	p2p_lobby_join(id, "");
	wait_for_keys();
	check(p2p_lobby_join_state() == P2P_LOBBY_JOIN_WRONG_PASSWORD && !joined_invite[0], "no password opens nothing");
	p2p_lobby_join(id, "hunter2");
	wait_for_keys();
	check(p2p_lobby_join_state() == P2P_LOBBY_JOIN_JOINING && !strcmp(joined_invite, expected_invite),
		"the password opens the host's invite");
	/* a locked listing whose sealed token was changed (and signed again, as
	a host could): nothing opens */
	memcpy(tampered, published, (size_t)published_size);
	tampered[9] = (unsigned char)(tampered[9] + 3);
	tampered[6 + 4 + 4 + P2P_KEY_SIZE + 30] ^= 1;
	resign(tampered, published_size, SIGNATURE_LABEL);
	hear(tampered, published_size, 0, NULL);
	joined_invite[0] = 0;
	p2p_lobby_join(id, "hunter2");
	wait_for_keys();
	check(p2p_lobby_join_state() == P2P_LOBBY_JOIN_WRONG_PASSWORD && !joined_invite[0],
		"an altered sealed token opens nothing");
	/* (the host's next listings come after the one made here) */
	lobby.sequence += 10;
	/* the password taken off: listed open again */
	new_invites = 0;
	p2p_lobby_set_password(NULL);
	check(new_invites == 1, "taking the password off makes a new invite");
	clock_now += 6000;
	lobby_update(token, 3, 16);
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 1 && !listing.locked && !strcmp(listing.invite, expected_invite),
		"a game whose password is taken off is listed open");
	/* a listed game that becomes co-op (network.coop_public: private unless
	chosen) is no longer listed, and its listed invite lets no one in */
	new_invites = 0;
	p2p_set_game_listing(NULL, "a10", "", 0, 1, 0, 0);
	p2p_set_game_listing_details(0, 2, 0, "one\n");
	clock_now += 6000;
	lobby_update(token, 1, 4);
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 0 && !p2p_lobby_listed() && new_invites == 1,
		"a public game turned co-op: no longer listed, a new invite");
	/* co-op on a Halo PC map, and many players (a co-op game listed: its
	own visibility, network.coop_public, chosen public) */
	p2p_lobby_set_coop_public(1);
	p2p_set_game_listing(NULL, "a10", "", 0, 1, 0, 0);
	p2p_set_game_listing_details(0, 2, 0, "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n");
	clock_now += 6000;
	lobby_update(token, 10, 16);
	hear(published, published_size, 0, NULL);
	check(p2p_lobby_entry(0, &entry) && !strcmp(entry.rules, "Co-op: The Pillar of Autumn, Heroic"),
		"a co-op game's Rules line");
	check(!strcmp(entry.players_line, "10 of 16: one, two, three, four, five, six, seven... +3 more") ||
		!strncmp(entry.players_line, "10 of 16: one, two", 18), "the Players line ends with the rest's count");
	check(strlen(entry.players_line) < sizeof(entry.players_line) && strstr(entry.players_line, "more"),
		"the Players line fits");
	/* ... and private again: no longer listed, though other games are
	public (p2p_lobby_set_public does not change a co-op game's) */
	p2p_lobby_set_coop_public(0);
	clock_now += 6000;
	lobby_update(token, 10, 16);
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 0, "a co-op game made private is no longer listed (its own visibility)");
	{
		int password = -1;

		check(!p2p_lobby_coop_public(&password) && password == 0,
			"the waiting screen reads co-op's visibility as chosen: private, no password");
		p2p_lobby_set_coop_public(1);
		check(p2p_lobby_coop_public(NULL) == 1, "... and public once chosen");
	}
	p2p_set_game_listing(NULL, "bloodgulch", "CTF", 1, 1, 0, 1);
	p2p_set_game_listing_details(3, -1, 1, "");
	clock_now += 6000;
	lobby_update(token, 0, 16);
	hear(published, published_size, 0, NULL);
	check(p2p_lobby_entry(0, &entry) && entry.pc_map && !strcmp(entry.map, "bloodgulch") &&
		!strcmp(entry.rules, "CTF to 3 on bloodgulch (HALO PC)") && !strcmp(entry.players_line, "0 of 16"),
		"a Halo PC map: its own name, and (HALO PC)");
	/* the listing's most: the longest of everything, within its size */
	p2p_set_game_listing("NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN", "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
		"GGGGGGGGGGGGGGGGGGGGGGGGGGGGGG", 2, 1, 0, 0);
	p2p_set_game_listing_details(999999, -1, 0,
		"PPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\n"
		"PPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\nPPPPPPPPPPPPPPPP\n");
	p2p_lobby_set_name("NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN");
	p2p_lobby_set_password("a password of thirty-two letters!");
	lobby_update(token, 255, 255);
	wait_for_keys();
	clock_now += 6000;
	lobby_update(token, 255, 255);
	check(p2p_lobby_listed() && published_size <= MAXIMUM_LISTING_SIZE && published_size > 350,
		"the longest listing fits");
	hear(published, published_size, 0, NULL);
	check(games(&listing) == 1 && strlen(listing.name) == P2P_LISTING_NAME_SIZE &&
		listing.listed_player_count == LISTED_PLAYERS && listing.score_limit == 0xFFFF,
		"the longest listing is read whole");
	check(p2p_lobby_entry(0, &entry) && strlen(entry.rules) < sizeof(entry.rules) &&
		strlen(entry.players_line) < sizeof(entry.players_line), "its entry's lines fit");
	/* network.lobby_password: taken until the game says */
	lobby.password_said = 0;
	lobby_password_setting = "from the config";
	lobby_update(token, 3, 16);
	wait_for_keys();
	check(lobby.has_password && !strcmp(lobby.password, "from the config"), "network.lobby_password is taken");
}

/* many hosts: as many as there is room for, the queue's budget, and a
browser that stops */
static void flood_checks(void)
{
	unsigned char listing[P2P_MAXIMUM_LISTING_SIZE];
	int index, size;

	p2p_lobby_browse(0);
	p2p_lobby_browse(1);
	for (index = 0; index < 3 * MAXIMUM_GAMES; index++)
	{
		unsigned char hash[P2P_KEY_HASH_SIZE];
		char slot[2 * P2P_KEY_HASH_SIZE + 1];

		memset(seed, index + 1, 16);
		seed[16] = (unsigned char)(index >> 8);
		p2p_ed25519_public(seed, signing_key, x25519_secret);
		p2p_x25519(x25519_public, x25519_secret, NULL);
		pthread_mutex_lock(&p2p_lock);
		size = listing_make(listing, 0);
		pthread_mutex_unlock(&p2p_lock);
		p2p_key_hash(x25519_public, hash);
		p2p_hex(hash, P2P_KEY_HASH_SIZE, slot);
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_slot_heard(slot, listing, size, 0);
		pthread_mutex_unlock(&p2p_lock);
		clock_now += 3;
		lobby_update(NULL, 0, 0);
	}
	check(games(NULL) == MAXIMUM_GAMES, "a flood of hosts fills the room, no more");
	p2p_lobby_browse(0);
	check(games(NULL) == 0, "a browser that stops keeps nothing");
}

#ifndef P2P_LOBBY_FUZZ
int main(void)
{
	crypto_checks();
	lobby_checks();
	flood_checks();
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
#endif
