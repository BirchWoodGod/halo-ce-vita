/*
TESTER_LIST.H

The signed tester list: whose machines may wear the tester's flaming helmet
(the blue flame on a player's head, p2p_flame.inc). Open source cannot stop
anyone drawing a flame on their own screen, so what everyone else sees is
backed by a signature only the owner can make: a list of friend identities
(the per-machine Ed25519 key of p2p_friends.c) signed with the owner's
release key (port/vita/include/vita_update_key.h's HALO_RELEASE_PUBLIC_KEY,
the key updates are signed with), and, in each game, each wearer's proof
that the listed identity is theirs (p2p_flame.inc). The owner makes the
list with tools/release/sign_testers.py (tools/release/README.md, "Tester
list").

The file, testers.txt, is strict text read by length:

	halo-ce-vita-testers 1\n
	version <1 to 4294967295, no leading zero>\n
	tester <the identity: 64 lowercase hexadecimal digits> <expiry> <label>\n
	... (0 to TESTER_LIST_MAXIMUM_ENTRIES of them)
	signature <128 lowercase hexadecimal digits>\n

expiry: 0 (never) or the UTC Unix second from which the entry no longer
counts (1 to 12 digits, no leading zero); label: 1 to 32 of A-Z a-z 0-9 . _ -
(the owner's note of who it is: a name, a friend code). The signature is
Ed25519 over every byte before "signature" (the first line's magic keeps it
from ever reading as an update's manifest, and the manifest as it). Nothing
before, between or after; every buffer has a fixed size.

The newest valid list wins: the one in the game's package (Vita
app0:testers.txt; PC beside the executable) or one dropped in the save
folder (ux0:data/haloce-vita/testers.txt; the PC's save folder), whichever
has the higher version and a good signature; a list that fails its checks
is ignored. A new version without someone is their flame revoked, on each
machine that has it.

Only integers, strings and structures of them cross to the callers.
*/

#ifndef HALO_TESTER_LIST_H
#define HALO_TESTER_LIST_H

#include <stddef.h>

#define TESTER_LIST_MAGIC "halo-ce-vita-testers 1"
#define TESTER_LIST_FILE "testers.txt"

enum
{
	TESTER_LIST_MAXIMUM_ENTRIES = 256,
	TESTER_LIST_LABEL_SIZE = 33,
	/* the largest file taken: every entry with the longest fields */
	TESTER_LIST_MAXIMUM_SIZE = 64 + 24 + TESTER_LIST_MAXIMUM_ENTRIES * (7 + 64 + 1 + 12 + 1 + 32 + 1) + 10 + 128 + 1,
};

struct tester_entry
{
	unsigned char key[32];
	/* the UTC Unix second it ends (0 never) */
	unsigned long long expires;
	char label[TESTER_LIST_LABEL_SIZE];
};

struct tester_list
{
	unsigned long version;
	int count;
	struct tester_entry entries[TESTER_LIST_MAXIMUM_ENTRIES];
};

enum
{
	TESTER_LIST_OK = 0,
	TESTER_LIST_BAD_SIGNATURE = -1,
	TESTER_LIST_MALFORMED = -2,
	TESTER_LIST_TOO_LARGE = -3,
	/* (tester_list_newer) not newer than the list already had */
	TESTER_LIST_NOT_NEWER = -4,
};

/* the key a list must be signed with: the owner's release key, or a test
build's (configure.py --testers-test-key, HALO_TESTERS_TEST_KEY; never a
release's) */
void tester_list_public_key(unsigned char key[32]);

/* a list's text checked with a key: the signature first, then its form;
TESTER_LIST_OK with list filled, else a code and why in error (list then
empty) */
int tester_list_check(const char *text, size_t size, const unsigned char key[32], struct tester_list *list,
	char *error, int error_size);

/* a candidate list's text taken in place of the list had (current, whose
version 0 is none) only when it checks and is newer: TESTER_LIST_OK (current
replaced), else a code (current kept) */
int tester_list_newer(const char *text, size_t size, const unsigned char key[32], struct tester_list *current,
	char *error, int error_size);

/* whether a key is listed and not expired at now (UTC Unix seconds; 0 if
the clock is unknown: entries with an expiry then do not count): its entry's
index, else -1 (not listed) or -2 (listed, expired) */
int tester_list_find(const struct tester_list *list, const unsigned char key[32], unsigned long long now);

#endif
