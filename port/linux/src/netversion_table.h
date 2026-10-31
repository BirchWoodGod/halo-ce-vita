/*
NETVERSION_TABLE.H

The signed crossplay version table (docs/crossplay.md 5.2 and 10.13): which
PC network versions this build's client may join, and which of the wire
rules already built into it (network_wire.h: the rules of 29, 32, 38 and 39)
each one plays; and versions known not to work, with a reason for the
browser. When OpenCE or ChupathingyCE raise their network version and the
new one plays a game as one of the old ones does (OpenCE 39: 38's rules
with its dedicated-server flags), the owner signs a table saying so, and
every Vita and PC build joins it without an update. The table selects
behaviour that is already built in: never code, never a message format.
Built-in versions keep their own rules whatever a table says; a table can
refuse one (a kill switch for joining it, naming the reason), never what a
host of this build plays (network_wire_choose), and the owner's tool will not
refuse 32, which Vitas host.

Signed with the owner's release key (port/vita/include/vita_update_key.h's
HALO_RELEASE_PUBLIC_KEY, the key updates and the tester list are signed
with) by tools/release/sign_netversions.py (tools/release/README.md,
"Crossplay version table"). The table and its signature are two files, a
release's assets on GitHub (netversion_fetch.c says where), fetched over the
update check's HTTPS and kept on the memory card.

netversions.txt is strict text read by length:

	halo-ce-vita-netversions 1\n
	version <1 to 4294967295, no leading zero>\n
	join <network version> rules <a built-in version whose rules it plays>\n
	refuse <network version> <reason>\n
	... (0 to NETVERSION_TABLE_MAXIMUM_ROWS of join and refuse lines, in
	    order of their network version, lowest first, each at most once)

network versions: 1 to 65535, no leading zero; reason: 1 to
NETVERSION_TABLE_REASON_SIZE - 1 of A-Z a-z 0-9 space . , : ; ! ? ' ( ) / + -
(shown in the browser as it is, never translated); nothing before, between
or after. netversions.txt.sig is its Ed25519 signature: 128 lowercase
hexadecimal digits and a newline, over every byte of netversions.txt (whose
first line keeps it from ever reading as an update's manifest or the tester
list, and them as it).

A join row whose rules this build does not have (a newer build's: "join 41
rules 41") is not joined: the browser says a newer Halo CE for PS Vita is
needed. Such rows, and a join row for a version that is built in with other
rules, are ignored, not fatal, so one table serves every build; anything
else out of form fails the whole table, which is then ignored (the built-in
list stands).

Only integers, strings and structures of them cross to the callers.
*/

#ifndef HALO_NETVERSION_TABLE_H
#define HALO_NETVERSION_TABLE_H

#include <stddef.h>

#define NETVERSION_TABLE_MAGIC "halo-ce-vita-netversions 1"
#define NETVERSION_TABLE_FILE "netversions.txt"
#define NETVERSION_TABLE_SIGNATURE_FILE "netversions.txt.sig"

enum
{
	NETVERSION_TABLE_MAXIMUM_ROWS = 64,
	NETVERSION_TABLE_REASON_SIZE = 65,
	/* the largest file taken: every row a refusal with the longest reason
	("refuse 65535 " and 64 characters) */
	NETVERSION_TABLE_MAXIMUM_SIZE = 27 + 19 + NETVERSION_TABLE_MAXIMUM_ROWS * (13 + 64 + 1),
	/* the signature file: 128 digits and a newline (CR LF taken) */
	NETVERSION_TABLE_SIGNATURE_MAXIMUM_SIZE = 130,
};

enum
{
	NETVERSION_ROW_JOIN = 1,
	NETVERSION_ROW_REFUSE = 2,
};

struct netversion_row
{
	unsigned short version;
	unsigned char kind;
	/* (join) the built-in version whose rules it plays */
	unsigned short rules;
	/* (refuse) why, for the browser */
	char reason[NETVERSION_TABLE_REASON_SIZE];
};

struct netversion_table
{
	/* 0: none (the built-in list alone) */
	unsigned long version;
	int count;
	struct netversion_row rows[NETVERSION_TABLE_MAXIMUM_ROWS];
};

enum
{
	NETVERSION_TABLE_OK = 0,
	NETVERSION_TABLE_BAD_SIGNATURE = -1,
	NETVERSION_TABLE_MALFORMED = -2,
	NETVERSION_TABLE_TOO_LARGE = -3,
	/* (netversion_table_newer) not newer than the table already had */
	NETVERSION_TABLE_NOT_NEWER = -4,
};

/* the key a table must be signed with: the owner's release key, or a test
build's (configure.py --netversions-test-key, HALO_NETVERSIONS_TEST_KEY;
never a release's) */
void netversion_table_public_key(unsigned char key[32]);

/* a table's text and its signature file's text checked with a key: the
signature first, then the form; NETVERSION_TABLE_OK with table filled, else
a code and why in error (table then empty) */
int netversion_table_check(const char *text, size_t size, const char *signature, size_t signature_size,
	const unsigned char key[32], struct netversion_table *table, char *error, int error_size);

/* a candidate taken in place of the table had (current, whose version 0 is
none) only when it checks and is newer: NETVERSION_TABLE_OK (current
replaced), else a code (current kept) */
int netversion_table_newer(const char *text, size_t size, const char *signature, size_t signature_size,
	const unsigned char key[32], struct netversion_table *current, char *error, int error_size);

/* the row of a network version, or NULL */
const struct netversion_row *netversion_table_find(const struct netversion_table *table, unsigned int version);

#endif
