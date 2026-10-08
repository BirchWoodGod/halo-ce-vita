/*
P2P_INTERNAL.H

Shared internals of internet play (p2p.c, p2p_signal.c, p2p_crypto.c,
p2p_discord.c; see p2p.c for the design).
*/

#ifndef __HALO_LINUX_P2P_INTERNAL_H
#define __HALO_LINUX_P2P_INTERNAL_H

#include "p2p.h"

#include "halo_port_limits.h"

#include <pthread.h>

enum
{
	/* a machine's identifier: from the hash of its public key (which is new
	each run), and also its XNADDR's abEnet */
	P2P_IDENTIFIER_SIZE = 6,
	/* what an invite holds of the host's public key: the first bytes of its
	SHA-256 (of which the identifier is the first 6), so that no other key
	can be found to pass for it */
	P2P_KEY_HASH_SIZE = 16,
	/* an invite's secret */
	P2P_TOKEN_SIZE = 16,
	/* the addresses a machine offers to be reached at */
	P2P_MAXIMUM_CANDIDATES = 4,
	/* an invite link's text: "halo://join/", the host's key hash and the
	token in hexadecimal, and a terminator */
	P2P_LINK_SIZE = 12 + 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1,
	/* the most machines one tunnels to: a host and the rest of a system
	link game's 128 machines (include/halo_port_limits.h) */
	P2P_MAXIMUM_PEERS = 127,
	/* a short code's characters (P2P_CODE_ALPHABET), shown as ABCD-EFGH */
	P2P_CODE_LENGTH = 8,
};

/* the prefix of internet play's topics on the brokers, of its key
derivations' labels and of its MQTT client identifiers: Vitas signal on
their own (Vitas play only Vitas, halo_port_limits.h), so a PC's invite,
code or public game never reaches a Vita's game, nor a Vita's a PC's */
#ifdef HALO_PORT_VITA_NETWORK
#define P2P_SIGNAL_PREFIX "hcev"
#else
#define P2P_SIGNAL_PREFIX "hceu"
#endif

/* the prefix of a public game's slot (a key hash in hex follows), and the
topic of queries (p2p_lobby.c) */
#define P2P_LOBBY_SLOT_PREFIX P2P_SIGNAL_PREFIX "/3/lobby/s/"
#define P2P_LOBBY_QUERY_TOPIC P2P_SIGNAL_PREFIX "/3/lobby/q"

/* internet play's state (p2p.c), which the game's threads and the p2p
thread share */
extern pthread_mutex_t p2p_lock;

struct p2p_candidate
{
	/* network byte order */
	unsigned long address;
	unsigned short port;
};

/* ---------- p2p.c: what the signalling side calls back */

/* the milliseconds of a monotonic clock */
unsigned long p2p_now(void);
/* looks up a host name (posix_resolve_ipv4), letting go of the p2p lock
while it waits; the p2p thread's */
unsigned long p2p_resolve(const char *host);
/* registers this executable for links of scheme (posix_register_url_scheme),
unless it is an automated run (debug.exit_after, a hidden window, no
renderer), which must not take the links over. The p2p thread's: it lets
go of the p2p lock while it may wait for a program */
void p2p_register_url_scheme(const char *scheme, const char *description);
/* formats bytes as lower-case hexadecimal (text holds 2 * size + 1) */
void p2p_hex(const unsigned char *bytes, int size, char *text);
/* the addresses this machine can be reached at; returns their count */
int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count);
/* this run's X25519 public key (P2P_KEY_SIZE bytes), whose hash the
identifier is (p2p_identifier) */
const unsigned char *p2p_public_key(void);
/* this run's Ed25519 public key (the X25519 one's, p2p_ed25519_to_x25519),
and a signature with it */
const unsigned char *p2p_signing_key(void);
void p2p_sign(const void *message, int size, unsigned char *signature);
/* the identifier of the machine with this public key */
void p2p_identifier_for(const unsigned char *public_key, unsigned char *identifier);
/* the hash of a public key an invite holds (P2P_KEY_HASH_SIZE bytes), and
the identifier of the machine whose key has that hash */
void p2p_key_hash(const unsigned char *public_key, unsigned char *hash);
void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier);
/* the X25519 secret this machine shares with the one with that public key;
0 if the key is unusable (one giving a known secret). The p2p thread's: it
lets go of the p2p lock while it works it out */
int p2p_shared_secret(const unsigned char *public_key, unsigned char *shared);
/* a joiner (on the host) or the host (on a joiner) offered its addresses
through signalling, with the secret of a session (P2P_SHA256_SIZE bytes) its
tunnel's keys come from; the tunnel starts reaching it. Returns 0 if it was
turned away: another session with that machine lives (it must lapse first),
this one has ended, or there is no room */
int p2p_peer_offered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int is_host);
/* whether p2p_peer_offered would turn a new session with that machine away
now (it is this machine, a session with it lives, there is no room, or, as
a host, too many players are being reached): checked before its secret is
worked out */
int p2p_peer_turned_away(const unsigned char *identifier, int is_host);
/* ... more addresses of a machine whose session (that secret's) lives: 0 if
none does (a session that has ended is never taken up again: its keys'
packet numbers would start again) */
int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count);
/* whether the session of that secret has ended (one never comes back: its
packet numbers would start over); under p2p_lock */
int p2p_session_retired(const unsigned char *secret);
/* an invite that arrived on the p2p thread (from Discord, or another copy
of the game) */
void p2p_invite_received(const char *text);
/* the invite link a code being looked up led to (p2p_signal.c) */
void p2p_code_found(const char *text);
/* an invite link joined, as p2p_join_invite does, under p2p_lock (the
server browser's, p2p_lobby.c) */
int p2p_join_invite_locked(const char *text);
/* the line p2p_status shows (and logs); under p2p_lock */
void p2p_set_status(const char *format, ...);
/* a new invite (token) for the game hosted, if its invite was listed in the
server browser (going private, or a new password: those who saw it must not
get in); under p2p_lock */
void p2p_new_invite_if_listed(void);

/* ---------- p2p_signal.c: signalling through public MQTT brokers */

/* connects to the brokers, if not already; called from the p2p thread */
void p2p_signal_start(void);
/* adds the signalling sockets to the p2p thread's select lists */
void p2p_signal_select_sets(int *read, int *read_count, int *write, int *write_count, int maximum_count);
/* services the sockets and timers; called from the p2p thread each pass */
void p2p_signal_update(const int *read, int read_count, const int *write, int write_count);
/* hosting: listen for joiners who hold this token, and keep the invite
under the code ("ABCD-EFGH"; NULL for none) */
void p2p_signal_host(const unsigned char *token, const char *code);
void p2p_signal_stop_hosting(void);
/* looking up a code's eight characters (no dash): its invite comes back
through p2p_code_found; with host (an identifier; NULL for any), only a
record of the host with that identifier */
void p2p_signal_lookup_code(const char *code, const unsigned char *host);
void p2p_signal_stop_lookup(void);
/* joining: ask the host whose public key has this hash (p2p_key_hash),
holding this token, until it answers (or p2p_signal_stop_joining); each call
asks anew, with a new nonce (as after the session with the host ended
before the tunnel reached it: the host makes one session of a request) */
void p2p_signal_join(const unsigned char *host_hash, const unsigned char *token);
void p2p_signal_stop_joining(void);
/* whether any broker is connected */
int p2p_signal_connected(void);
/* the server browser's topics: the own slot and the queries (a listed
game), and every slot (browsing) */
void p2p_signal_lobby_topics(int listed, int browsing);
/* publishes a listing to the own slot on every broker, retained, and again
on each broker that connects later; closing: a tombstone, after which the
slot is cleared and nothing is published again */
void p2p_signal_lobby_publish(const unsigned char *listing, int size, int closing);
/* asks the hosts to publish again */
void p2p_signal_lobby_query(void);
/* the game is quitting: the tombstone published (p2p_signal_lobby_publish)
and the slot cleared on every ready broker now, whatever their buckets, and
the connections closed cleanly */
void p2p_signal_lobby_quit(void);

/* ---------- p2p_adhoc.c: ad hoc play's bridge between the group and the
tunnel */

/* starts the bridge (network.adhoc), with the tunnel's port (network byte
order); called from p2p_initialize */
void p2p_adhoc_start(unsigned short tunnel_port);
/* offers the group's machines to p2p.c as peers; the p2p thread's, each
pass, under p2p_lock */
void p2p_adhoc_update(void);

/* ---------- p2p_crypto.c */

enum
{
	P2P_SHA256_SIZE = 32,
	/* an X25519 secret or public key, and a shared secret */
	P2P_KEY_SIZE = 32,
	/* ChaCha20-Poly1305's nonce and tag */
	P2P_NONCE_SIZE = 12,
	P2P_TAG_SIZE = 16,
	/* what p2p_seal adds: a random nonce and the tag */
	P2P_SEAL_OVERHEAD = P2P_NONCE_SIZE + P2P_TAG_SIZE,
};

void p2p_sha256(const void *data, int size, unsigned char *digest);
void p2p_hmac_sha256(const unsigned char *key, int key_size, const void *data, int size, unsigned char *digest);
/* ChaCha20-Poly1305: encrypts plaintext with a 32-byte key and a nonce used
with that key once, and authenticates it and the additional data, into
sealed (size + P2P_TAG_SIZE bytes); returns the sealed size */
int p2p_aead_seal(const unsigned char *key, const unsigned char *nonce, const void *additional,
	int additional_size, const void *plaintext, int size, unsigned char *sealed);
/* the reverse: the plaintext size, or -1 if sealed (or the additional data)
was not made so or was altered */
int p2p_aead_open(const unsigned char *key, const unsigned char *nonce, const void *additional,
	int additional_size, const unsigned char *sealed, int size, unsigned char *plaintext);
/* the same with a random nonce ahead of the ciphertext (size +
P2P_SEAL_OVERHEAD bytes) and no additional data */
int p2p_seal(const unsigned char *key, const void *plaintext, int size, unsigned char *sealed);
int p2p_open(const unsigned char *key, const unsigned char *sealed, int size, unsigned char *plaintext);
/* whether two byte strings are the same, compared in constant time */
int p2p_equal(const void *first, const void *second, int size);
/* X25519: scalar times point (NULL: the base point, which gives the public
key of the secret key scalar) */
void p2p_x25519(unsigned char *result, const unsigned char *scalar, const unsigned char *point);

enum
{
	/* an Ed25519 seed (a run's key comes from one) and signature */
	P2P_SEED_SIZE = 32,
	P2P_SIGNATURE_SIZE = 64,
	P2P_SHA512_SIZE = 64,
};

void p2p_sha512(const void *data, int size, unsigned char *digest);
/* Ed25519 (with SHA-512): a seed's public key (P2P_KEY_SIZE bytes) and,
unless NULL, its X25519 secret key (the scalar it signs with, whose X25519
public key is p2p_ed25519_to_x25519 of the Ed25519 one) */
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);
void p2p_ed25519_sign(const unsigned char *seed, const unsigned char *public_key, const void *message, int size,
	unsigned char *signature);
/* whether the signature is the key's, of the message (never for a key of
small order, which anyone can sign for) */
int p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size,
	const unsigned char *signature);
/* the X25519 public key of an Ed25519 one; 0 if it has a small order */
int p2p_ed25519_to_x25519(const unsigned char *public_key, unsigned char *x25519_public);

enum
{
	/* a password-protected listing's token, sealed (p2p_seal_token): its
	nonce (24), its tag (16), then the token sealed */
	P2P_PASSWORD_KEY_SIZE = 32,
	P2P_SEALED_TOKEN_SIZE = 24 + 16 + 16,
};

/* the key of a password (Argon2id: P2P_PASSWORD_KEY_SIZE bytes), for the
host whose Ed25519 key salt is (P2P_KEY_SIZE bytes): it takes half a second
on a Vita, so that guessing passwords at a listing takes long. 0 if there
was no memory for it (the key is then random: nothing opens with it) */
int p2p_password_key(const char *password, const unsigned char *salt, unsigned char *key);
/* a token (P2P_TOKEN_SIZE bytes) sealed with a password's key, bound to the
host's Ed25519 key (P2P_SEALED_TOKEN_SIZE bytes); and opened: 0 if the key
is not the one it was sealed with (a wrong password), or it was altered */
void p2p_seal_token(const unsigned char *key, const unsigned char *signing_key, const unsigned char *token,
	unsigned char *sealed);
int p2p_unseal_token(const unsigned char *key, const unsigned char *signing_key, const unsigned char *sealed,
	unsigned char *token);

/* ---------- p2p_lobby.c: public games' listings */

enum
{
	P2P_LISTING_NAME_SIZE = 32,
	P2P_LISTING_MAP_SIZE = 32,
	P2P_LISTING_GAMETYPE_SIZE = 24,
	/* the players' names a Vita's listing holds, and their size */
	P2P_LISTING_PLAYERS = 8,
	P2P_LISTING_PLAYER_NAME_SIZE = 12,
	/* the largest listing p2p_signal.c carries */
	P2P_MAXIMUM_LISTING_SIZE = 512,
};

/* a public game found (p2p_lobby_games) */
struct p2p_listing
{
	/* (empty for a locked one: its token is sealed with its password) */
	char invite[P2P_LINK_SIZE];
	/* the host's (its XNADDR's abEnet once reached) and its key's hash */
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	unsigned char player_count, maximum_player_count, engine_type;
	unsigned char open, in_progress, has_teams, pc_map, coop;
	/* joining it failed this run */
	unsigned char failed;
	/* milliseconds, -1 if not known */
	short ping;
	int score_limit;
	/* a co-op game's (0 to 3; else not said) */
	int difficulty;
	int listed_player_count;
	char players[P2P_LISTING_PLAYERS][P2P_LISTING_PLAYER_NAME_SIZE + 1];
	/* it has a password: its key and its token sealed (p2p_lobby_join) */
	unsigned char locked;
	unsigned char signing_key[P2P_KEY_SIZE];
	unsigned char sealed_token[P2P_SEALED_TOKEN_SIZE];
};

/* a copy of the public games found, in the order shown (the most players
first; then those not failed, the open ones, by name); returns their count */
int p2p_lobby_games(struct p2p_listing *games, int maximum_count);
/* a name cleaned as player_name_clean cleans a player's (printable ASCII:
no "|", no spaces before or after): 0 if nothing is left that names it */
int p2p_lobby_clean_name(char *name);
/* joining the host of that key hash timed out (p2p.c): a game the browser
joined is marked failed; under p2p_lock */
void p2p_lobby_join_timed_out(const unsigned char *host_hash);

/* the p2p thread's pass: the token of the game hosted for the internet
(NULL if none) and its player counts */
void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count);
/* whether the game hosted is listed now, and whether the browser is open */
int p2p_lobby_listed(void);
int p2p_lobby_browsing(void);
/* a message on a slot (its key hash in hex) through a broker; retained: the
slot's retained copy, sent on subscribing */
void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained);
/* a query was heard */
void p2p_lobby_query_heard(void);
/* the game is quitting: a listed game's tombstone, and its slot cleared,
written to the brokers at once; under p2p_lock */
void p2p_lobby_quit(void);
/* a key hash's slot */
void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size);

/* ---------- p2p_discord.c: rich presence and invites through the Discord
desktop client */

/* called from the p2p thread each pass */
void p2p_discord_update(void);
/* the Discord user signed in, as told (empty if none): under p2p_lock */
void p2p_discord_user(char *id, int id_size, char *name, int name_size);
/* what to show: hosting with an invite link's secret and player counts, or
not (secret NULL) */
void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count);

#endif
