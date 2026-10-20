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
Players lines. And on the crossplay wire's topics (network_wire.h,
crossplay_checks): an OpenCE host's listing (format 1, "hceu-lobby-1",
network version 29) taken, read as OpenCE's (its flags only), tagged, and
joined by its invite on that wire; the Vitas' format 2, their label and
another version turned away there; a Vita's game hosted for Vitas and PC
listed as OpenCE's browsers read it (format 1, its Vita bit, its name
tagged [VITA]) and tagged VITA+PC by the Vitas'; the browsers' filter.
Prints PASS or the failures.

It includes p2p_lobby.c, to reach its reader. run_p2p_lobby_test.sh builds it
with the platform layer's flags (HALO_VITA: the Vitas' topics and label).
*/

#include "../../linux/src/p2p_lobby.c"
#include "monocypher.h"

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
/* (burst_checks: the clock real, running clock_scale times as fast - a
Vita's signature check takes some 30 times this machine's - from
clock_now) */
static int clock_real;
static double clock_scale = 1.0;
static struct timespec clock_base;
static unsigned long real_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)(((double)(now.tv_sec - clock_base.tv_sec) * 1000.0 +
		(double)(now.tv_nsec - clock_base.tv_nsec) / 1e6) * clock_scale);
}
unsigned long p2p_now(void) { return clock_real ? clock_now + real_ms() : clock_now; }
int config_boolean(const char *name) { return !strcmp(name, "network.public_lobby"); }
/* (port/linux/src/network_wire.c's: the hosted game's wire, the PC host's
stand-in and the browsers' filter, a check's; the wire a listing is heard
on, hear's) */
static int test_crossplay, test_heard, test_pc_stand_in, test_filter;
int network_wire_crossplay(void) { return test_crossplay; }
unsigned int network_wire_version_of(int crossplay)
{
	return crossplay ? HALO_PORT_CROSSPLAY_NETWORK_VERSION : HALO_PORT_NETWORK_VERSION;
}
int network_wire_version_accepted_on(int crossplay, unsigned int version)
{
	return version == network_wire_version_of(crossplay);
}
int network_wire_host_pc_stand_in(void) { return test_pc_stand_in; }
/* (this machine's platform and who can join its game: the listing's stamp,
network_wire.h; a Vita for Vitas and Halo CE Vita PC unless a check says) */
static int test_platform = NETWORK_PLATFORM_VITA, test_audience = NETWORK_AUDIENCE_VITA_PC;
int network_wire_this_platform(void) { return test_platform; }
int network_wire_host_audience(void) { return test_audience; }
int network_wire_browser_shows(int vita_host)
{
	return test_filter == 0 || (test_filter == 1) == (vita_host != 0);
}
const char *config_string(const char *name)
{
	return !strcmp(name, "network.lobby_password") ? lobby_password_setting : "";
}
/* (the log's last lines, for the diagnostics' checks) */
static char logged[64][320];
static int logged_count;
void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(logged[logged_count % 64], sizeof(logged[0]), format, arguments);
	va_end(arguments);
	logged_count++;
}

/* whether the log's last lines (since logged_count was 0) hold text */
static int logged_line(const char *text)
{
	int index;

	for (index = logged_count > 64 ? logged_count - 64 : 0; index < logged_count; index++)
	{
		if (strstr(logged[index % 64], text))
			return 1;
	}
	return 0;
}

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
static int topics_listed_crossplay;
void p2p_signal_lobby_topics(int listed, int listed_crossplay, int browsing)
{
	(void)browsing;
	if (listed)
		topics_listed_crossplay = listed_crossplay;
}
void p2p_signal_lobby_query(void) {}
void p2p_signal_lobby_publish(const unsigned char *listing, int size, int closing)
{
	memcpy(published, listing, (size_t)size);
	published_size = size;
	published_closing = closing;
	publish_count++;
}
void p2p_signal_lobby_quit(void) {}
/* (the brokers as the checks set them) */
static struct p2p_signal_counts signal_counts = { 3, 3, 3, 3, 3, 3, 1, 1, 1 };
static int kicks;
void p2p_signal_counts(struct p2p_signal_counts *counts) { *counts = signal_counts; }
void p2p_signal_brokers_text(char *text, int size, int what)
{
	snprintf(text, (size_t)size, "the test's brokers (%d)", what);
}
void p2p_signal_kick(void) { kicks++; }
static int joined_wire = -2;
int p2p_join_invite_locked(const char *text, int crossplay)
{
	snprintf(joined_invite, sizeof(joined_invite), "%s", text);
	joined_wire = crossplay;
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
	p2p_password_key("hunter2", ed, key, 0);
	p2p_password_key("hunter2", ed, again, 0);
	check(!memcmp(key, again, sizeof(key)), "a password's key is the same each time");
	p2p_password_key("hunter3", ed, again, 0);
	check(memcmp(key, again, sizeof(key)) != 0, "another password, another key");
	p2p_password_key("hunter2", other_ed, again, 0);
	check(memcmp(key, again, sizeof(key)) != 0, "another host (salt), another key");
	/* (the crossplay wire's: OpenCE's three passes, another key than the
	Vitas' four of the same password) */
	p2p_password_key("hunter2", ed, again, 1);
	check(memcmp(key, again, sizeof(key)) != 0, "the crossplay wire's key (OpenCE's three passes) is not the Vitas'");
	{
		/* (OpenCE's p2p_password_key of 8102e711, Argon2id 4096 blocks, 3
		passes, salted with the host's Ed25519 key: the same key, so a PC
		player's password opens a Vita's crossplay game) */
		crypto_argon2_config config = { CRYPTO_ARGON2_ID, 4096, 3, 1 };
		crypto_argon2_inputs inputs = { (const unsigned char *)"hunter2", ed, 7, P2P_KEY_SIZE };
		static unsigned char work[4096 * 1024];
		unsigned char opence[P2P_PASSWORD_KEY_SIZE];

		crypto_argon2(opence, sizeof(opence), work, config, inputs, crypto_argon2_no_extras);
		check(!memcmp(opence, again, sizeof(opence)), "the crossplay wire's key is OpenCE's, byte for byte");
	}
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
	p2p_lobby_slot_heard(test_heard, slot ? slot : own, payload, size, retained);
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
		!strcmp(entry.players_line, "3 of 16: alpha, bravo, charlie") && !entry.locked && !entry.pc_map &&
		!strcmp(entry.badge, "VITA") && !strcmp(entry.tag, "VITA") && !entry.crossplay && !entry.experimental &&
		!entry.coop, "the entry's map, Rules and Players lines; its tag VITA (the legacy wire's games are the Vitas')");
	/* this project's marks, in the stamp's first byte (network_wire.h): the
	Vita's game for Vitas and Halo CE Vita PC says nothing; the PC build's
	is tagged VITA (PC), shown with the PCs'; a game for Vitas only is
	marked so, which a viewer on the PC build sees after its badge. A
	listing that says nothing (an older build's) is a Vita's */
	check(published[published_size - P2P_SIGNATURE_SIZE - 8] == 0 && entry.marks == 0,
		"a Vita's game for Vitas and Halo CE Vita PC: its stamp all zero, as an older build's");
	{
		unsigned char bytes[P2P_MAXIMUM_LISTING_SIZE];
		int size;

		test_platform = NETWORK_PLATFORM_VITA_PC;
		pthread_mutex_lock(&p2p_lock);
		size = listing_make(bytes, _listing_open | _listing_has_teams);
		pthread_mutex_unlock(&p2p_lock);
		check(bytes[size - P2P_SIGNATURE_SIZE - 8] == NETWORK_WIRE_STAMP_PC_HOST,
			"the PC build's listing: its stamp's first byte says so, signed with the rest");
		hear(bytes, size, 0, NULL);
		check(p2p_lobby_entry(0, &entry) && entry.platform == CROSSPLAY_PLATFORM_VITA &&
			entry.marks == CROSSPLAY_HOST_PC_BUILD && !strcmp(entry.tag, "VITA (PC)") && !strcmp(entry.badge, "VITA (PC)") &&
			!crossplay_guard_vita_game(entry.platform, entry.marks), "the PC build's game: tagged VITA (PC), a PC's game");
		test_filter = 1;
		check(games(NULL) == 0, "the browser's Vita filter leaves the PC build's game out");
		test_filter = 2;
		check(games(NULL) == 1, "its PC filter shows it");
		test_filter = 0;
		test_platform = NETWORK_PLATFORM_VITA;
		test_audience = NETWORK_AUDIENCE_VITAS;
		pthread_mutex_lock(&p2p_lock);
		size = listing_make(bytes, _listing_open | _listing_has_teams);
		pthread_mutex_unlock(&p2p_lock);
		check(bytes[size - P2P_SIGNATURE_SIZE - 8] == NETWORK_WIRE_STAMP_VITAS_ONLY, "a game for Vitas only: marked so");
		hear(bytes, size, 0, NULL);
		check(p2p_lobby_entry(0, &entry) && entry.marks == CROSSPLAY_HOST_VITAS_ONLY && !strcmp(entry.tag, "VITA") &&
			!strcmp(entry.badge, "VITA"), "a Vita viewer: VITA, as before");
		test_platform = NETWORK_PLATFORM_VITA_PC;
		check(p2p_lobby_entry(0, &entry) && !strcmp(entry.badge, "VITA VITAS ONLY"),
			"a viewer on the PC build: VITA VITAS ONLY (it would be refused)");
		test_platform = NETWORK_PLATFORM_VITA;
		test_audience = NETWORK_AUDIENCE_VITA_PC;
		pthread_mutex_lock(&p2p_lock);
		size = listing_make(bytes, _listing_open | _listing_has_teams);
		pthread_mutex_unlock(&p2p_lock);
		hear(bytes, size, 0, NULL);
		check(p2p_lobby_entry(0, &entry) && entry.marks == 0 && !strcmp(entry.badge, "VITA"), "a Vita's game again: VITA");
	}
	/* joining it: its invite, at once */
	check(p2p_lobby_join(id, NULL) && p2p_lobby_join_state() == P2P_LOBBY_JOIN_JOINING &&
		!strcmp(joined_invite, expected_invite) && joined_wire == 0, "joining an open game joins its invite, on its wire");
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
		p2p_lobby_slot_heard(0, slot, listing, size, 0);
		pthread_mutex_unlock(&p2p_lock);
		clock_now += 3;
		lobby_update(NULL, 0, 0);
	}
	check(games(NULL) == MAXIMUM_GAMES, "a flood of hosts fills the room, no more");
	p2p_lobby_browse(0);
	check(games(NULL) == 0, "a browser that stops keeps nothing");
}

/* the diagnostics: the host's listing said to be listed only once a broker
holds it, and its status as the brokers are; the browser's status line
(looking, the list out of reach, no games, games), its summary's counts and
its lines about games */
static void status_checks(void)
{
	static const unsigned char token[P2P_TOKEN_SIZE] = { 5, 5, 5 };
	unsigned char bytes[P2P_MAXIMUM_LISTING_SIZE];
	char text[128];
	int size, state;

	wait_for_keys();
	p2p_lobby_browse(0);
	pthread_mutex_lock(&p2p_lock);
	memset(&lobby, 0, sizeof(lobby));
	pthread_mutex_unlock(&p2p_lock);
	lobby_password_setting = "";
	memset(&signal_counts, 0, sizeof(signal_counts));
	signal_counts.brokers = 3;
	signal_counts.started_time = clock_now;
	logged_count = 0;
	kicks = 0;

	/* ---- hosting */
	hosting_token = token;
	p2p_lobby_set_public(1);
	p2p_set_game_listing("Status", "wizard", "Slayer", 2, 1, 0, 0);
	p2p_set_game_listing_details(25, -1, 0, "alpha\n");
	lobby_update(token, 1, 16);
	check(p2p_lobby_listed(), "a public game's listing is published");
	check(!logged_line("listed in everyone's public games"),
		"but it is not said to be listed before a broker acknowledges it");
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_PENDING && !strcmp(text, "Getting listed..."),
		"its status: getting listed, while the brokers are tried");
	clock_now += LISTING_ACKNOWLEDGE_TIME + 1000;
	signal_counts.tried = 3;
	lobby_update(token, 1, 16);
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_PENDING, "every broker tried, none acknowledging: getting listed still, before "
		"BROWSE_REACH_TIME");
	clock_now += BROWSE_REACH_TIME;
	signal_counts.exhausted = 2;
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_PENDING, "and after it while a broker has a way left to try (its TLS port)");
	signal_counts.exhausted = 3;
	lobby_update(token, 1, 16);
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_UNREACHABLE && !strcmp(text, "Not listed: can't reach the online game list"),
		"every way of every broker failed: not listed, the list out of reach");
	check(logged_line("the game is not listed: no broker has acknowledged its listing"), "and the log says so");
	signal_counts.ready = signal_counts.lobby = signal_counts.listing = 2;
	lobby_update(token, 1, 16);
	check(logged_line("the game is listed in everyone's public games (on 2 of 3 brokers"),
		"listed once two brokers acknowledged it, on how many said");
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_LISTED && !strcmp(text, "Listed on 2 of 3 servers"), "its status: listed on 2 of 3");
	clock_now += LISTING_COUNT_LOG_INTERVAL + 1000;
	signal_counts.listing = 1;
	lobby_update(token, 1, 16);
	check(logged_line("the listing is on 1 of 3 brokers now"), "a broker losing it is said");
	p2p_lobby_set_public(0);
	lobby_update(token, 1, 16);
	state = p2p_lobby_hosting_status(text, sizeof(text));
	check(state == P2P_LOBBY_HOSTING_PRIVATE && !p2p_lobby_listed(), "a private game: private, not listed");
	lobby_update(NULL, 0, 0);
	check(p2p_lobby_hosting_status(text, sizeof(text)) == P2P_LOBBY_HOSTING_NONE && !text[0], "not hosting: nothing");

	/* ---- browsing */
	hosting_token = NULL;
	memset(&signal_counts, 0, sizeof(signal_counts));
	signal_counts.brokers = 3;
	logged_count = 0;
	p2p_lobby_browse(1);
	check(kicks > 0, "opening the browser has a broker waiting after a failure try at once");
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_LOOKING && !strcmp(text, "Looking for public games..."),
		"looking while signalling starts");
	signal_counts.started_time = clock_now;
	signal_counts.tried = 3;
	signal_counts.exhausted = 3;
	lobby_update(NULL, 0, 0);
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_LOOKING, "every broker failed at once: looking still, for BROWSE_REACH_TIME "
		"(Oct 11 2026: \"can't reach\" after 10 s while TLS worked)");
	check(p2p_lobby_brokers_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_LOOKING, "Server Setup's line too");
	clock_now += BROWSE_REACH_TIME - 1000;
	lobby_update(NULL, 0, 0);
	check(p2p_lobby_browse_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_LOOKING, "a second before it, still");
	clock_now += 2000;
	signal_counts.exhausted = 2;
	lobby_update(NULL, 0, 0);
	check(p2p_lobby_browse_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_LOOKING,
		"past it, a broker with a way left to try (its TLS port): looking still");
	signal_counts.exhausted = 3;
	lobby_update(NULL, 0, 0);
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_UNREACHABLE &&
		!strcmp(text, "Can't reach the online game list - check your internet connection"),
		"every way of every broker failed: the list is out of reach");
	check(logged_line("browser: \"Can't reach the online game list - check your internet connection\""),
		"and the log says so");
	check(p2p_lobby_brokers_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_UNREACHABLE &&
		strstr(text, "Can't reach the online game list"), "Server Setup's line says so too");
	signal_counts.ready = signal_counts.lobby = signal_counts.browsing = 1;
	signal_counts.browsing_time = clock_now;
	check(p2p_lobby_browse_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_LOOKING, "a broker took the subscription: "
		"looking while the hosts answer");
	check(p2p_lobby_brokers_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_EMPTY && !text[0],
		"the list reached: Server Setup says nothing");
	clock_now += BROWSE_SETTLE_TIME + 1000;
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_EMPTY && !strcmp(text, "No public games right now"), "reached, no games");
	/* a listing of another network version: not shown, counted, said */
	pthread_mutex_lock(&p2p_lock);
	size = listing_make(bytes, 0);
	pthread_mutex_unlock(&p2p_lock);
	bytes[3] = (unsigned char)((HALO_PORT_NETWORK_VERSION - 1) >> 8);
	bytes[4] = (unsigned char)(HALO_PORT_NETWORK_VERSION - 1);
	hear(bytes, resign(bytes, size, SIGNATURE_LABEL), 0, NULL);
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_EMPTY && !strcmp(text, "No public games for this version (1 on another)"),
		"a game of another version: said, not shown");
	/* the summary, once due: the counts and why (the first, settled after
	opening, came with none, while the brokers were out of reach: the next
	BROWSE_SUMMARY_INTERVAL after it, as listings come) */
	clock_now += BROWSE_SUMMARY_INTERVAL;
	lobby_update(NULL, 0, 0);
	check(logged_line("browser (summary): 1 listing heard (0 retained, 0 again), 0 games shown") &&
		logged_line("dropped 1 of another version (1 host)"), "the summary counts the listing and why it was dropped");
	/* a game of this version: shown, its line logged (the key's hash only by
	its start) */
	pthread_mutex_lock(&p2p_lock);
	size = listing_make(bytes, 0);
	pthread_mutex_unlock(&p2p_lock);
	hear(bytes, size, 1, NULL);
	state = p2p_lobby_browse_status(text, sizeof(text));
	check(state == P2P_LOBBY_BROWSE_GAMES && !strcmp(text, "1 public game"), "a game: one public game");
	check(logged_line("browser: new game \"Status\" (1/16 players, wizard, Slayer) from host "),
		"the game's line names it, its players, map and gametype");
	{
		unsigned char hash[P2P_KEY_HASH_SIZE];
		char hex[2 * P2P_KEY_HASH_SIZE + 1];

		p2p_key_hash(x25519_public, hash);
		p2p_hex(hash, P2P_KEY_HASH_SIZE, hex);
		check(!logged_line(hex), "and never the key's whole hash");
	}
	/* the hosts asked again on a refresh: its counts once they answer */
	kicks = 0;
	p2p_lobby_refresh();
	check(kicks > 0 && logged_line("browser: refreshed"), "a refresh is said, and tries the brokers at once");
	p2p_lobby_browse(0);
	check(logged_line("browser (closing): 1 listing heard (1 retained, 0 again), 1 game shown") &&
		logged_line("stopped browsing the public games (1 shown)"), "closing: its counts");
	check(p2p_lobby_browse_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_OFF && !text[0], "closed: nothing");
}

/* ---------- games joining failed: hidden a while */

/* host's listing (a key of its own) with players of 8: its bytes, and its
key hash in hex (its slot, and its entry's id) and identifier */
static int host_listing(int host, int players, unsigned char *bytes, char *slot, unsigned char *identifier)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];
	int size;

	memset(seed, 0, sizeof(seed));
	seed[0] = 200;
	seed[1] = (unsigned char)host;
	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	pthread_mutex_lock(&p2p_lock);
	lobby.player_count = players;
	lobby.maximum_player_count = 8;
	snprintf(lobby.name, sizeof(lobby.name), "Host %d", host);
	snprintf(lobby.map, sizeof(lobby.map), "%s", "sidewinder");
	snprintf(lobby.gametype, sizeof(lobby.gametype), "%s", "Slayer");
	size = listing_make(bytes, _listing_open);
	pthread_mutex_unlock(&p2p_lock);
	p2p_key_hash(x25519_public, hash);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, slot);
	p2p_identifier_from_hash(hash, identifier);
	/* (the browser's own key again: none of the hosts') */
	memset(seed, 0, sizeof(seed));
	seed[0] = 250;
	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	return size;
}

/* host listed again (a new sequence) with players, and the browser's pass */
static void host_heard(int host, int players)
{
	unsigned char bytes[P2P_MAXIMUM_LISTING_SIZE], identifier[P2P_IDENTIFIER_SIZE];
	char slot[2 * P2P_KEY_HASH_SIZE + 1];
	int size = host_listing(host, players, bytes, slot, identifier);

	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_slot_heard(0, slot, bytes, size, 0);
	pthread_mutex_unlock(&p2p_lock);
	lobby_update(NULL, 0, 0);
}

/* where the browser shows the game of that id (-1: not shown), and whether
it is marked failed */
static int shown_at(const char *id, int *failed)
{
	struct p2p_lobby_entry entry;
	int index;

	for (index = 0; p2p_lobby_entry(index, &entry); index++)
	{
		if (!strcmp(entry.id, id))
		{
			if (failed)
				*failed = entry.failed;
			return index;
		}
	}
	return -1;
}

/* the game's datagrams to a peer, one each 2 s, and the browser's pass */
static void searches(const unsigned char *identifier, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		clock_now += 2000;
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_game_sent(identifier);
		pthread_mutex_unlock(&p2p_lock);
		lobby_update(NULL, 0, 0);
	}
}

static void failed_join_checks(void)
{
	unsigned char bytes[P2P_MAXIMUM_LISTING_SIZE];
	unsigned char blaze[P2P_IDENTIFIER_SIZE], good[P2P_IDENTIFIER_SIZE], gone[P2P_IDENTIFIER_SIZE];
	char blaze_id[P2P_LOBBY_ID_SIZE], good_id[P2P_LOBBY_ID_SIZE], gone_id[P2P_LOBBY_ID_SIZE];
	unsigned char hash[P2P_KEY_HASH_SIZE];
	int failed = -1, index, start;
	char text[128];

	wait_for_keys();
	p2p_lobby_browse(0);
	pthread_mutex_lock(&p2p_lock);
	memset(&lobby, 0, sizeof(lobby));
	pthread_mutex_unlock(&p2p_lock);
	signal_counts.browsing = 3;
	signal_counts.browsing_time = clock_now;
	host_listing(1, 8, bytes, blaze_id, blaze);
	host_listing(2, 3, bytes, good_id, good);
	host_listing(3, 1, bytes, gone_id, gone);
	p2p_lobby_browse(1);
	host_heard(1, 8);
	host_heard(2, 3);
	host_heard(3, 1);
	check(shown_at(blaze_id, &failed) == 0 && !failed && shown_at(good_id, NULL) == 1 && shown_at(gone_id, NULL) == 2,
		"three hosts' games shown, the most players first");
	/* an older build's Split Screen game: joined, its host reached, the
	System Link searches unanswered */
	check(p2p_lobby_join(blaze_id, NULL) && p2p_lobby_join_state() == P2P_LOBBY_JOIN_JOINING, "joining it");
	searches(good, 6);
	check(shown_at(blaze_id, NULL) == 0 && shown_at(good_id, NULL) == 1,
		"another host's datagrams say nothing of the game joined");
	searches(blaze, 5);
	check(shown_at(blaze_id, NULL) == 0, "five searches unanswered (8 s): still shown");
	logged_count = 0;
	searches(blaze, 1);
	check(shown_at(blaze_id, NULL) == -1 && shown_at(good_id, NULL) == 0,
		"six searches over 10 s unanswered: the game is hidden");
	check(logged_line("joining \"Host 1\"") && logged_line("answered none of 6 System Link searches in 10 s") &&
		logged_line("hidden for 5 minutes"), "said in the log");
	check(p2p_lobby_browse_status(text, sizeof(text)) == P2P_LOBBY_BROWSE_GAMES && !strcmp(text, "2 public games"),
		"the status line counts the games shown");
	/* its host listing it again, the same: still hidden, through a browser
	closed and opened too */
	clock_now += 30000;
	host_heard(1, 8);
	check(shown_at(blaze_id, NULL) == -1, "listed again the same: still hidden");
	p2p_lobby_browse(0);
	p2p_lobby_browse(1);
	host_heard(1, 8);
	host_heard(2, 3);
	host_heard(3, 1);
	check(shown_at(blaze_id, NULL) == -1 && shown_at(good_id, NULL) == 0, "the browser opened again: still hidden");
	/* 5 minutes on: shown again, marked failed */
	clock_now += FAILED_HIDE_TIME;
	host_heard(1, 8);
	host_heard(2, 3);
	host_heard(3, 1);
	check(shown_at(blaze_id, &failed) >= 0 && failed == 1,
		"after 5 minutes: shown again, marked failed (after the others of as many players)");
	/* failing again, then listed anew with something changed: shown, not
	failed */
	check(p2p_lobby_join(blaze_id, NULL), "joining it again");
	searches(blaze, 6);
	check(shown_at(blaze_id, NULL) == -1, "failing again: hidden again");
	host_heard(1, 7);
	check(shown_at(blaze_id, &failed) == 0 && !failed, "listed anew with another player count: shown, not failed");
	check(logged_line("shown again after joining it failed"), "said in the log");
	/* a host whose game answers: never hidden */
	logged_count = 0;
	check(p2p_lobby_join(good_id, NULL), "joining a good game");
	searches(good, 1);
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_game_heard(good);
	pthread_mutex_unlock(&p2p_lock);
	searches(good, 20);
	check(shown_at(good_id, &failed) >= 0 && !failed && !logged_line("failed"), "a game that answers is not hidden");
	/* the datagrams sent before the host was reached do not count (p2p.c
	sends to reached peers only): an answer late in the window is in time */
	check(p2p_lobby_join(good_id, NULL), "joining it again");
	searches(good, 5);
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_game_heard(good);
	pthread_mutex_unlock(&p2p_lock);
	searches(good, 5);
	check(shown_at(good_id, &failed) >= 0 && !failed, "an answer after five searches is in time");
	/* a host never reached (p2p.c's JOIN_TIMEOUT) */
	check(p2p_lobby_join(gone_id, NULL), "joining a host that is never reached");
	parse_hex(gone_id, hash, P2P_KEY_HASH_SIZE);
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_join_timed_out(hash);
	pthread_mutex_unlock(&p2p_lock);
	check(p2p_lobby_join_state() == P2P_LOBBY_JOIN_GONE && shown_at(gone_id, NULL) == -1,
		"a host not reached: its game hidden");
	check(logged_line("its host was not reached"), "said in the log");
	/* the menus' mark (the game gone): listed last, as before, if it was
	not watched */
	p2p_lobby_mark_failed(good_id);
	check(shown_at(good_id, &failed) >= 0 && failed, "a game marked failed by the menus, not watched: shown, marked failed");
	/* a datagram from no one watched, or with nothing watched: nothing */
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_game_heard(blaze);
	p2p_lobby_game_sent(blaze);
	pthread_mutex_unlock(&p2p_lock);
	/* failures past the room: the oldest forgotten */
	start = logged_count;
	for (index = 0; index < 2 * MAXIMUM_GAMES; index++)
	{
		char id[P2P_LOBBY_ID_SIZE];

		snprintf(id, sizeof(id), "%08x%024x", index + 1, 0);
		clock_now += 10;
		p2p_lobby_mark_failed(id);
	}
	check(lobby.failed_count == MAXIMUM_GAMES && logged_count - start == 2 * MAXIMUM_GAMES,
		"failures past the room: the oldest forgotten");
	p2p_lobby_browse(0);
}

/* ---------- the crossplay wire: OpenCE's listings */

/* (opence_listing's players, most players and game engine: a check's) */
static int opence_players = 5, opence_maximum = 128, opence_engine = 2;
static const char *opence_map = "bloodgulch";
/* (its name: "PC game" unless a check sets one) */
static const char *opence_text_name;

/* an OpenCE host's listing (its p2p_lobby.c listing_make at 6399ed9d, CC0):
format, version, flags and label given, signed with the PC host's key; its
size */
static int opence_listing(unsigned char *bytes, const unsigned char *pc_seed, const unsigned char *pc_key,
	int format, int version, int flags, unsigned long sequence, const unsigned char *token, const char *label,
	int extra)
{
	unsigned char data[64 + P2P_MAXIMUM_LISTING_SIZE];
	int size = 0, label_size = (int)strlen(label), index;
	unsigned long now = (unsigned long)time(NULL);
	const char *const texts[3] = { opence_text_name ? opence_text_name : "PC game", opence_map, "Slayer" };

	bytes[size++] = 'H';
	bytes[size++] = 'L';
	bytes[size++] = (unsigned char)format;
	bytes[size++] = (unsigned char)(version >> 8);
	bytes[size++] = (unsigned char)version;
	bytes[size++] = (unsigned char)flags;
	for (index = 3; index >= 0; index--)
		bytes[size++] = (unsigned char)(sequence >> (index * 8));
	for (index = 3; index >= 0; index--)
		bytes[size++] = (unsigned char)(now >> (index * 8));
	memcpy(bytes + size, pc_key, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	memcpy(bytes + size, token, P2P_TOKEN_SIZE);
	size += P2P_TOKEN_SIZE;
	bytes[size++] = (unsigned char)opence_players;
	bytes[size++] = (unsigned char)opence_maximum;
	bytes[size++] = (unsigned char)opence_engine;
	for (index = 0; index < 3; index++)
	{
		bytes[size++] = (unsigned char)strlen(texts[index]);
		memcpy(bytes + size, texts[index], strlen(texts[index]));
		size += (int)strlen(texts[index]);
	}
	/* (format 2's score, difficulty and names, where a check wants them) */
	for (index = 0; index < extra; index++)
		bytes[size++] = 0;
	memset(bytes + size, 0, 8);
	size += 8;
	memcpy(data, label, (size_t)label_size);
	memcpy(data + label_size, bytes, (size_t)size);
	p2p_ed25519_sign(pc_seed, pc_key, data, label_size + size, bytes + size);
	return size + P2P_SIGNATURE_SIZE;
}

/* the browser's entry of the game listed under the slot id: 1 if listed */
static int entry_of(const char *id, struct p2p_lobby_entry *entry)
{
	int index;

	for (index = 0; p2p_lobby_entry(index, entry); index++)
	{
		if (!strcmp(entry->id, id))
			return 1;
	}
	return 0;
}

#ifndef P2P_LOBBY_FUZZ
/* ---------- a flood of retained listings, as brokers send them all at once
to a browser that subscribes (insider.15/16's logs: 1386 heard, 981 dropped
past the queue): burst_checks, and the files of run_browser_flood_test.sh */

struct flood_listing
{
	int crossplay;
	char slot[2 * P2P_KEY_HASH_SIZE + 1];
	unsigned char payload[P2P_MAXIMUM_LISTING_SIZE];
	int size;
};

/* host number's listing on a wire: its sequence, its time this many seconds
off flood_time (the same listing the same on every broker), its network
version this much off this build's, signed with the host's own key (its
seed from number) */
static unsigned long flood_time;
static void flood_listing_make(int number, int crossplay, unsigned long sequence, long time_offset, int version_offset,
	struct flood_listing *out)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];
	unsigned int version = network_wire_version_of(crossplay) + (unsigned int)version_offset;
	char saved_name[sizeof(lobby.name)];
	int saved_crossplay, saved_password;
	unsigned long saved_sequence;

	memset(seed, 0, sizeof(seed));
	seed[0] = 0xF1;
	seed[1] = (unsigned char)(number >> 8);
	seed[2] = (unsigned char)number;
	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	pthread_mutex_lock(&p2p_lock);
	saved_crossplay = lobby.listed_crossplay;
	saved_password = lobby.has_password;
	saved_sequence = lobby.sequence;
	memcpy(saved_name, lobby.name, sizeof(saved_name));
	lobby.listed_crossplay = crossplay;
	lobby.has_password = 0;
	lobby.sequence = sequence - 1;
	snprintf(lobby.name, sizeof(lobby.name), "Flood %04d", number);
	out->size = listing_make(out->payload, _listing_open);
	lobby.listed_crossplay = saved_crossplay;
	lobby.has_password = saved_password;
	lobby.sequence = saved_sequence;
	memcpy(lobby.name, saved_name, sizeof(saved_name));
	pthread_mutex_unlock(&p2p_lock);
	out->payload[3] = (unsigned char)(version >> 8);
	out->payload[4] = (unsigned char)version;
	put_long(out->payload + 10, flood_time + (unsigned long)time_offset);
	resign(out->payload, out->size, crossplay ? crossplay_signature_label : signature_label);
	p2p_key_hash(x25519_public, hash);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, out->slot);
	out->crossplay = crossplay;
}

/* what each of brokers holds retained: the live games (fresh, this build's
version; a quarter on the crossplay wire where this build reads it; the last
broker holding an older listing of every third, as a broker that missed a
publish does), stale copies of hosts long gone (an hour old: three quarters
the same on every broker, the rest each broker's own) and hosts of another
version (OpenCE's newer PC builds), in an order of each broker's own. Each
list is malloc'd; the browser must show the live games, all of them */
static int flood_plan(int brokers, int live, int stale, int other, struct flood_listing **lists)
{
#ifdef HALO_PORT_VITA_NETWORK
	const int wires = 2;
#else
	const int wires = 1;
#endif
	int broker, count = live + stale + other;

	flood_time = (unsigned long)time(NULL);
	for (broker = 0; broker < brokers; broker++)
	{
		struct flood_listing *list = malloc(sizeof(*list) * (size_t)count);
		unsigned int random = 12345u + (unsigned int)broker * 977u;
		int index, used = 0;

		if (!list)
			return 0;
		for (index = 0; index < live; index++)
		{
			int older = broker == brokers - 1 && index % 3 == 0;

			flood_listing_make(index, wires > 1 && index % 4 == 3, older ? 9 : 10, 0, 0, &list[used++]);
		}
		for (index = 0; index < stale; index++)
		{
			int number = index < stale * 3 / 4 ? 1000 + index : 2000 + broker * 1000 + index;

			flood_listing_make(number, wires > 1 && index % 2, 4, -7200, 0, &list[used++]);
		}
		for (index = 0; index < other; index++)
			flood_listing_make(9000 + index, wires > 1, 4, 0, 40, &list[used++]);
		/* (each broker's own order) */
		for (index = count - 1; index > 0; index--)
		{
			struct flood_listing swap;
			int other_index;

			random = random * 1103515245u + 12345u;
			other_index = (int)((random >> 8) % (unsigned int)(index + 1));
			swap = list[index];
			list[index] = list[other_index];
			list[other_index] = swap;
		}
		lists[broker] = list;
	}
	/* (this machine's own key again: none of the flood's) */
	memset(seed, 0, sizeof(seed));
	seed[0] = 42;
	p2p_ed25519_public(seed, signing_key, x25519_secret);
	p2p_x25519(x25519_public, x25519_secret, NULL);
	return count;
}

/* 3 brokers each sending ~500 retained listings at once as the browser
subscribes, as fast as p2p_signal.c reads them (each pass of the p2p
thread: up to MAXIMUM_BROKER_READS of 4096 bytes from each broker), on a
clock running at a Vita's pace for the signatures (clock_scale): no listing
dropped past the queue, every live game shown */
static void burst_checks(void)
{
	enum { BROKERS = 3, LIVE = 60, STALE = 400, OTHER = 40, PASS_BYTES = 8 * 4096 };
	struct flood_listing *lists[BROKERS];
	int position[BROKERS] = { 0 };
	int count, broker, more, passes = 0, arrival_passes, heard = 0, shown;
	int past_queue, stale_dropped, other_dropped, older_dropped, signature_dropped, repeated, room;
	unsigned long slowest_enqueue_us = 0, verify_us;
	struct timespec start, end;

	count = flood_plan(BROKERS, LIVE, STALE, OTHER, lists);
	check(count == LIVE + STALE + OTHER, "the flood's listings made");
	if (count != LIVE + STALE + OTHER)
		return;
	/* (one signature check on this machine, for the scale) */
	{
		struct listing read;
		int index;

		p2p_lobby_listing_read(lists[0][0].crossplay, lists[0][0].payload, lists[0][0].size, &read);
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (index = 0; index < 100; index++)
			listing_signed(lists[0][0].payload, &read);
		clock_gettime(CLOCK_MONOTONIC, &end);
		verify_us = (unsigned long)(((double)(end.tv_sec - start.tv_sec) * 1e6 +
			(double)(end.tv_nsec - start.tv_nsec) / 1e3) / 100.0);
	}
	p2p_lobby_browse(0);
	clock_now += 20 * 60000;
	p2p_lobby_browse(1);
	/* (no summary while it runs: its counts are this check's) */
	lobby.summary_time = 0;
	lobby.summary_logged_time = now_stamp();
	clock_scale = 30.0;
	clock_gettime(CLOCK_MONOTONIC, &clock_base);
	clock_real = 1;
	do
	{
		more = 0;
		for (broker = 0; broker < BROKERS; broker++)
		{
			int bytes = 0;

			clock_gettime(CLOCK_MONOTONIC, &start);
			pthread_mutex_lock(&p2p_lock);
			while (position[broker] < count && bytes < PASS_BYTES)
			{
				const struct flood_listing *listing = &lists[broker][position[broker]++];

				p2p_lobby_slot_heard(listing->crossplay, listing->slot, listing->payload, listing->size, 1);
				/* (the PUBLISH's header and topic) */
				bytes += listing->size + 2 + 2 + (int)strlen(P2P_LOBBY_SLOT_PREFIX(0)) + 2 * P2P_KEY_HASH_SIZE;
				heard++;
			}
			pthread_mutex_unlock(&p2p_lock);
			clock_gettime(CLOCK_MONOTONIC, &end);
			{
				unsigned long us = (unsigned long)((double)(end.tv_sec - start.tv_sec) * 1e6 +
					(double)(end.tv_nsec - start.tv_nsec) / 1e3);

				if (us > slowest_enqueue_us)
					slowest_enqueue_us = us;
			}
			more |= position[broker] < count;
		}
		lobby_update(NULL, 0, 0);
		passes++;
	}
	while (more);
	arrival_passes = passes;
	while (lobby.queue_count && passes < 100000)
	{
		lobby_update(NULL, 0, 0);
		passes++;
	}
	clock_now += real_ms();
	clock_real = 0;
	shown = games(NULL);
	room = lobby.queue_room;
	past_queue = lobby.counts.drops[_drop_queue_full];
	stale_dropped = lobby.counts.drops[_drop_stale];
	other_dropped = lobby.counts.drops[_drop_version];
	older_dropped = lobby.counts.drops[_drop_older];
	signature_dropped = lobby.counts.drops[_drop_signature];
	repeated = lobby.counts.repeated;
	printf("burst: %d brokers x %d retained listings (%d live games, %d stale, %d of another version): %d heard, "
		"%d again, %d games shown; dropped %d past the queue, %d stale, %d of another version, %d older, %d badly "
		"signed; %d passes while they came, %d more to check them; a signature check %lu us here; the longest "
		"broker's pass of hearing them %lu us; the queue's room %d\n", BROKERS, count, LIVE, STALE, OTHER, heard, repeated, shown,
		past_queue, stale_dropped, other_dropped, older_dropped, signature_dropped, arrival_passes,
		passes - arrival_passes, verify_us, slowest_enqueue_us, room);
	check(past_queue == 0, "a burst of retained listings from 3 brokers: none dropped past the queue");
	check(shown == LIVE, "a burst of retained listings from 3 brokers: every live game shown");
	check(signature_dropped == 0, "a burst of retained listings: none badly signed");
	check(room <= MAXIMUM_QUEUED, "the queue's room at most MAXIMUM_QUEUED");
	p2p_lobby_browse(0);
	check(!lobby.queue && !lobby.queue_room, "a browser that stops gives the queue's memory back");
	/* a forgery on a host's slot claiming a newer sequence, heard first (a
	broker anyone publishes to): the host's own listing is still checked and
	its game shown; the forgery dropped, badly signed */
	clock_now += 20 * 60000;
	p2p_lobby_browse(1);
	{
		struct flood_listing *genuine = &lists[0][0], forged;
		int index;

		/* (a live game's: sequence 10) */
		for (index = 0; index < count && get_long(lists[0][index].payload + 6) != 10; index++)
			;
		genuine = &lists[0][index < count ? index : 0];
		forged = *genuine;

		put_long(forged.payload + 6, 99);
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_slot_heard(forged.crossplay, forged.slot, forged.payload, forged.size, 1);
		p2p_lobby_slot_heard(genuine->crossplay, genuine->slot, genuine->payload, genuine->size, 1);
		pthread_mutex_unlock(&p2p_lock);
		lobby_update(NULL, 0, 0);
		check(games(NULL) == 1 && lobby.counts.drops[_drop_signature] == 1,
			"a forged listing claiming a newer sequence hides no game");
		/* and heard after it: checked first, and the host's still shown */
		p2p_lobby_browse(0);
		clock_now += 20 * 60000;
		p2p_lobby_browse(1);
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_slot_heard(genuine->crossplay, genuine->slot, genuine->payload, genuine->size, 1);
		p2p_lobby_slot_heard(forged.crossplay, forged.slot, forged.payload, forged.size, 1);
		check(lobby.queue_count == 2 && get_long(queued_at(0)->payload + 6) == 99,
			"a host's listing claiming a newer sequence is checked first");
		pthread_mutex_unlock(&p2p_lock);
		lobby_update(NULL, 0, 0);
		check(games(NULL) == 1 && lobby.counts.drops[_drop_signature] == 1,
			"... and, forged, hides no game");
	}
	for (broker = 0; broker < BROKERS; broker++)
		free(lists[broker]);
	p2p_lobby_browse(0);
}

/* run_browser_flood_test.sh's brokers' retained listings: PREFIX.0 ...
PREFIX.<brokers - 1>, each line a slot's topic and its listing in hex */
static int listings_files(const char *prefix, int live, int stale, int other, int brokers)
{
	struct flood_listing *lists[8];
	int count, broker, index;

	if (brokers < 1 || brokers > 8 || live < 0 || live > MAXIMUM_GAMES || stale < 0 || other < 0)
		return 2;
	count = flood_plan(brokers, live, stale, other, lists);
	if (!count && live + stale + other)
		return 1;
	for (broker = 0; broker < brokers; broker++)
	{
		char path[1024], hex[2 * P2P_MAXIMUM_LISTING_SIZE + 1];
		FILE *file;

		snprintf(path, sizeof(path), "%s.%d", prefix, broker);
		file = fopen(path, "w");
		if (!file)
			return 1;
		for (index = 0; index < count; index++)
		{
			p2p_hex(lists[broker][index].payload, lists[broker][index].size, hex);
			fprintf(file, "%s%s %s\n", P2P_LOBBY_SLOT_PREFIX(lists[broker][index].crossplay), lists[broker][index].slot,
				hex);
		}
		fclose(file);
		free(lists[broker]);
	}
	printf("%d listings for each of %d brokers: %d live games\n", count, brokers, live);
	return 0;
}

static void crossplay_checks(void)
{
	static const unsigned char pc_seed[P2P_SEED_SIZE] = { 99, 1, 2 };
	static const unsigned char token[P2P_TOKEN_SIZE] = { 0x26, 0x26 };
	unsigned char pc_key[P2P_KEY_SIZE], pc_secret[P2P_KEY_SIZE], pc_public[P2P_KEY_SIZE];
	unsigned char hash[P2P_KEY_HASH_SIZE], bytes[P2P_MAXIMUM_LISTING_SIZE];
	char slot[2 * P2P_KEY_HASH_SIZE + 1], invite[P2P_LINK_SIZE];
	struct p2p_lobby_entry entry;
	unsigned long sequence = 1;
	int size;

	p2p_ed25519_public(pc_seed, pc_key, pc_secret);
	p2p_x25519(pc_public, pc_secret, NULL);
	p2p_key_hash(pc_public, hash);
	p2p_hex(hash, P2P_KEY_HASH_SIZE, slot);
	test_heard = 1;
	p2p_lobby_browse(1);
	check(!strcmp(P2P_LOBBY_SLOT_PREFIX(1), "hceu/3/lobby/s/") && !strcmp(P2P_LOBBY_SLOT_PREFIX(0),
		P2P_SIGNAL_PREFIX "/3/lobby/s/"), "both wires' slots: OpenCE's (hceu/3/lobby/s/) and the Vitas'");
	/* the Vitas' own kind of listing, their label, another version: not taken */
	size = opence_listing(bytes, pc_seed, pc_key, 2, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 4);
	hear(bytes, size, 0, slot);
	check(!entry_of(slot, &entry), "crossplay: a listing of format 2 (the Vitas') is not taken");
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		SIGNATURE_LABEL, 0);
	hear(bytes, size, 0, slot);
	check(!entry_of(slot, &entry), "crossplay: one signed under the Vitas' label is not");
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(!entry_of(slot, &entry), "crossplay: one of network version 18 (the Vitas') is not");
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 4);
	hear(bytes, size, 0, slot);
	check(!entry_of(slot, &entry), "crossplay: one of format 1 with bytes left over is not");
	/* OpenCE's own: taken, its flags OpenCE's (32, this build's Halo PC map
	in format 2, not read; 128 ChupathingyCE's dedicated) */
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1 | 2 | 32 | 128,
		sequence++, token, "hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && !strcmp(entry.name, "PC game") && !strcmp(entry.map, "Blood Gulch") &&
		entry.players == 5 && entry.maximum == 128 && entry.open && entry.in_progress && !entry.pc_map &&
		entry.dedicated && !entry.locked && !entry.own && entry.compatible && entry.score_limit == 0 && entry.crossplay,
		"crossplay: an OpenCE host's listing (format 1, hceu-lobby-1, version 29) is taken, its flags OpenCE's");
	check(entry.platform == CROSSPLAY_PLATFORM_CHUPA && !strcmp(entry.tag, "CHUPA"),
		"crossplay: a dedicated server's listing is ChupathingyCE's: tagged CHUPA");
	{
		unsigned char parts[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
		char text[2 * sizeof(parts) + 1];

		memcpy(parts, hash, P2P_KEY_HASH_SIZE);
		memcpy(parts + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
		p2p_hex(parts, sizeof(parts), text);
		snprintf(invite, sizeof(invite), "halo://join/%s", text);
	}
	check(p2p_lobby_join(slot, NULL) && !strcmp(joined_invite, invite) && joined_wire == 1,
		"crossplay: joining it joins its invite, on the crossplay wire");
	/* the guardrails' badges (crossplay_guard.h, docs/crossplay.md 6): a
	maximum above 16 is Experimental (that server's 128), so is a game of
	more than 16 players; 16 of 16 is not; no game engine is co-op (OpenCE's
	listing has no co-op bit), shown without a difficulty it never sent */
	check(entry.experimental && !entry.coop && !strcmp(entry.badge, "CHUPA SERVER EXPERIMENTAL"),
		"crossplay: a PC server of 128: badged CHUPA SERVER EXPERIMENTAL");
	opence_players = 16;
	opence_maximum = 16;
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && !entry.experimental && !entry.coop && !entry.dedicated && !strcmp(entry.badge, "PC") &&
		entry.platform == CROSSPLAY_PLATFORM_PC,
		"crossplay: a PC host of 16 of 16: badged PC (its build not told by a listing), not Experimental");
	opence_players = 17;
	opence_maximum = 16;
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && entry.experimental && !strcmp(entry.badge, "PC EXPERIMENTAL"),
		"crossplay: 17 players (more than its maximum says): Experimental by its players");
	opence_players = 2;
	opence_maximum = 16;
	opence_engine = 0;
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && !entry.coop && !strcmp(entry.badge, "PC"),
		"crossplay: no game engine on Blood Gulch (a host between games): not co-op");
	opence_map = "a10";
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && entry.coop && entry.experimental && !strcmp(entry.badge, "PC CO-OP EXPERIMENTAL") &&
		!strcmp(entry.rules, "Co-op: The Pillar of Autumn") && !strstr(entry.rules, "Easy"),
		"crossplay: no game engine on a10: a PC co-op game, experimental, badged PC CO-OP EXPERIMENTAL, no difficulty "
		"made up");
	opence_map = "bloodgulch";
	opence_players = 255;
	opence_maximum = 0;
	opence_engine = 255;
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && entry.experimental && !entry.coop && strlen(entry.badge) < sizeof(entry.badge),
		"crossplay: counts and engine at their ends: Experimental, not co-op, the badge whole");
	opence_players = 5;
	opence_maximum = 128;
	opence_engine = 2;
	/* the browsers' filter (network.browser_filter) */
	test_filter = 1;
	check(!entry_of(slot, &entry), "Browser shows Vita games: a PC game is not listed");
	test_filter = 2;
	check(entry_of(slot, &entry), "Browser shows PC games: it is");
	test_filter = 0;
	/* a Vita's game on the crossplay wire, heard there: its listing's bit
	(64), its name's [VITA] taken off; tagged VITA+PC, never Experimental */
	opence_maximum = 16;
	opence_text_name = "[VITA] Vita Host";
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1 | 64, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && entry.platform == CROSSPLAY_PLATFORM_VITA && entry.crossplay &&
		!strcmp(entry.badge, "VITA+PC") && !strcmp(entry.name, "Vita Host") && !entry.experimental,
		"crossplay: a Vita's game for Vitas and PC: tagged VITA+PC, its name as the Vita chose it");
	test_filter = 1;
	check(entry_of(slot, &entry), "Browser shows Vita games: a Vita's crossplay game is listed");
	test_filter = 0;
	opence_text_name = "[VITA] ";
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1 | 64, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(entry_of(slot, &entry) && entry.name[0] && strcmp(entry.name, "[VITA]"),
		"crossplay: a Vita's listing named [VITA] and nothing: a name still");
	opence_text_name = NULL;
	opence_maximum = 128;
	/* a listing of OpenCE's format heard on the Vitas' wire's topics: not taken */
	test_heard = 0;
	p2p_lobby_browse(0);
	p2p_lobby_browse(1);
	size = opence_listing(bytes, pc_seed, pc_key, 1, HALO_PORT_CROSSPLAY_NETWORK_VERSION, 1, sequence++, token,
		"hceu-lobby-1", 0);
	hear(bytes, size, 0, slot);
	check(!entry_of(slot, &entry), "the Vitas' topics: a listing of OpenCE's format is not taken there");
	/* this Vita hosting for Vitas and PC: its listing OpenCE's (format 1,
	version 29, under hceu-lobby-1), its Vita bit, its name tagged [VITA],
	on the crossplay wire's slot */
	{
		static const unsigned char host_token[P2P_TOKEN_SIZE] = { 0x29, 0x29 };
		struct listing read;
		struct queued queued;
		char own[2 * P2P_KEY_HASH_SIZE + 1];

		lobby_update(NULL, 0, 0);
		test_crossplay = 1;
		hosting_token = host_token;
		p2p_lobby_set_public(1);
		p2p_set_game_listing("Vita Host", "bloodgulch", "Slayer", 2, 1, 0, 0);
		p2p_set_game_listing_details(25, -1, 0, "alpha\n");
		clock_now += 60000;
		lobby_update(host_token, 2, 16);
		check(p2p_lobby_listed() && topics_listed_crossplay && published[2] == LISTING_FORMAT_CROSSPLAY &&
			(published[3] << 8 | published[4]) == HALO_PORT_CROSSPLAY_NETWORK_VERSION && (published[5] & 64) &&
			!(published[5] & (32 | 128)), "hosting for Vitas and PC: listed on the crossplay wire, OpenCE's format 1, "
			"version 29, its Vita bit");
		check(p2p_lobby_listing_read(1, published, published_size, &read) && listing_signed(published, &read) &&
			!strcmp(read.name, "[VITA] Vita Host") && read.maximum_player_count == 16,
			"its listing reads as OpenCE's, signed under hceu-lobby-1, its name [VITA] Vita Host");
		check(!p2p_lobby_listing_read(0, published, published_size, &read), "it is no listing of the Vitas' format 2");
		memset(&queued, 0, sizeof(queued));
		test_heard = 1;
		p2p_key_hash(x25519_public, hash);
		p2p_hex(hash, P2P_KEY_HASH_SIZE, own);
		hear(published, published_size, 0, NULL);
		check(entry_of(own, &entry) && entry.own && !strcmp(entry.badge, "VITA+PC") && !strcmp(entry.name, "Vita Host"),
			"the Vitas' browser: its own game, tagged VITA+PC");
		/* the PC host's stand-in (the tests'): no Vita bit */
		test_pc_stand_in = 1;
		p2p_set_game_listing(NULL, NULL, NULL, 2, 1, 1, 0);
		clock_now += 60000;
		lobby_update(host_token, 2, 16);
		check(published[2] == LISTING_FORMAT_CROSSPLAY && !(published[5] & 64) &&
			p2p_lobby_listing_read(1, published, published_size, &read) && !strcmp(read.name, "Vita Host"),
			"the PC host's stand-in: no Vita bit, its name as it is");
		test_pc_stand_in = 0;
		/* hosting on the legacy wire again: its tombstone on the crossplay
		slot, then listed in format 2 */
		test_crossplay = 0;
		clock_now += 60000;
		lobby_update(host_token, 2, 16);
		check(p2p_lobby_listed() && !topics_listed_crossplay && published[2] == LISTING_FORMAT,
			"a game for Vitas next: listed in the Vitas' format 2, on their wire");
		lobby_update(NULL, 0, 0);
		test_heard = 0;
	}
}

int main(int argc, char **argv)
{
	/* (run_browser_flood_test.sh: listings PREFIX LIVE STALE OTHER BROKERS) */
	if (argc == 7 && !strcmp(argv[1], "listings"))
		return listings_files(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
	crypto_checks();
	lobby_checks();
	status_checks();
	flood_checks();
	burst_checks();
	failed_join_checks();
	crossplay_checks();
	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
#endif
