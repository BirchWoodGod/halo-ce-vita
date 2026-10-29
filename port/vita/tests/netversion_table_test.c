/*
NETVERSION_TABLE_TEST.C

The signed crossplay version table (port/linux/src/netversion_table.c),
with a throwaway key made at test time (run_netversion_table_test.sh:
NETVERSIONS_TEST_SEED, and the build's HALO_NETVERSIONS_TEST_KEY its public
half). Checked:

- a table signed with the key: taken, its version and rows (join with its
  rules, refuse with its reason), found by version;
- the build's key is the test key (netversion_table_public_key);
- a bad signature: a byte of the table or of the signature changed, one
  signed with another key, the signature file cut short, of capitals, with
  something after it; a table cut short or with something after it: refused,
  the table left empty;
- the form, signed: leading zeros, version 0, a version past 32 bits, a
  network version past 16 bits, rows out of order or twice, a row of another
  kind, a join without rules, a reason empty, too long, with a character
  outside its set or a space at its end, a CRLF, no last newline, too many
  rows, an unknown line: refused;
- size: a table larger than any can be (TOO_LARGE) and a signature file
  larger than one: refused before the signature is read;
- the newest wins: an older or equal version is not taken in place of the
  one had (NOT_NEWER, current kept), a newer one is;
- an unknown wire's rules (join 41 rules 41) and a row for a built-in
  version with other rules are taken as rows: what they mean is
  network_wire.c's (network_wire_test.c);
- (NETVERSIONS_TEST_FILE) the table tools/release/sign_netversions.py made
  with the same key reads as made.

Prints PASS or the failures. Built with ASan and UBSan.
*/

#include "netversion_table.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL %s\n", what);
	}
}

static unsigned char seed[32], public_key[32], other_seed[32], other_public[32];

static void hex_bytes(const char *text, unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		unsigned int value;

		sscanf(text + 2 * index, "%2x", &value);
		bytes[index] = (unsigned char)value;
	}
}

static void to_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + 2 * index, "%02x", bytes[index]);
}

/* the signature file of body signed with a seed: "<hex>\n" */
static void sign_body(const char *body, size_t length, const unsigned char *with_seed, char *signature)
{
	unsigned char key[32], bytes[64];

	p2p_ed25519_public(with_seed, key, NULL);
	p2p_ed25519_sign(with_seed, key, body, (int)length, bytes);
	to_hex(bytes, 64, signature);
	strcpy(signature + 128, "\n");
}

static struct netversion_table table, current;
static char signature[256];
static char big[NETVERSION_TABLE_MAXIMUM_SIZE + 4096];

static int check_signed(const char *body)
{
	char error[128];

	sign_body(body, strlen(body), seed, signature);
	return netversion_table_check(body, strlen(body), signature, strlen(signature), public_key, &table, error,
		(int)sizeof(error));
}

static int table_empty(void)
{
	return table.version == 0 && table.count == 0;
}

#define HEADER "halo-ce-vita-netversions 1\nversion 3\n"

int main(void)
{
	const char *seed_text = getenv("NETVERSIONS_TEST_SEED");
	unsigned char built_key[32];
	char error[128], body[2048], tampered[2048], tampered_signature[256];
	const struct netversion_row *row;
	size_t length;
	int index;

	if (!seed_text || strlen(seed_text) != 64)
	{
		printf("FAIL no NETVERSIONS_TEST_SEED\n");
		return 1;
	}
	hex_bytes(seed_text, seed, 32);
	p2p_ed25519_public(seed, public_key, NULL);
	for (index = 0; index < 32; index++)
		other_seed[index] = (unsigned char)(index * 7 + 3);
	p2p_ed25519_public(other_seed, other_public, NULL);
	netversion_table_public_key(built_key);
	check(!memcmp(built_key, public_key, 32), "the build's key is the test key");

	/* a good table */
	snprintf(body, sizeof(body), "%s", HEADER "join 40 rules 39\njoin 41 rules 41\nrefuse 50 OpenCE 50 changed the record\n"
		"refuse 60 Its host crashes Vitas (see the release notes): wait!\n");
	check(check_signed(body) == NETVERSION_TABLE_OK, "a signed table: taken");
	check(table.version == 3 && table.count == 4, "its version and four rows");
	row = netversion_table_find(&table, 40);
	check(row && row->kind == NETVERSION_ROW_JOIN && row->rules == 39, "join 40 rules 39");
	row = netversion_table_find(&table, 41);
	check(row && row->kind == NETVERSION_ROW_JOIN && row->rules == 41, "join 41 rules 41: a row (an unknown wire's)");
	row = netversion_table_find(&table, 50);
	check(row && row->kind == NETVERSION_ROW_REFUSE && !strcmp(row->reason, "OpenCE 50 changed the record"),
		"refuse 50 with its reason");
	check(netversion_table_find(&table, 60) != NULL && !netversion_table_find(&table, 39) &&
		!netversion_table_find(NULL, 40), "60 found; 39 not; no table, nothing");
	check(check_signed(HEADER) == NETVERSION_TABLE_OK && table.version == 3 && table.count == 0,
		"a table of no rows: taken (the built-in list alone)");
	check(check_signed("halo-ce-vita-netversions 1\nversion 4294967295\njoin 65535 rules 39\n") == NETVERSION_TABLE_OK &&
		table.version == 4294967295UL && table.rows[0].version == 65535, "the largest version and network version");
	check(check_signed(HEADER "join 39 rules 32\n") == NETVERSION_TABLE_OK,
		"a row for a built-in version with other rules: a row (network_wire.c ignores it)");
	check(check_signed(HEADER "join 18 rules 32\n") == NETVERSION_TABLE_OK, "a row for 18: a row (ignored there too)");

	/* signatures */
	sign_body(body, strlen(body), seed, signature);
	length = strlen(body);
	for (index = 0; index < (int)length; index += 7)
	{
		memcpy(tampered, body, length);
		tampered[index] ^= 0x01;
		if (netversion_table_check(tampered, length, signature, strlen(signature), public_key, &table, error,
				(int)sizeof(error)) != NETVERSION_TABLE_BAD_SIGNATURE || !table_empty())
		{
			check(0, "a byte of the table changed: refused, the table empty");
			break;
		}
	}
	check(index >= (int)length, "every changed byte (one in 7) of the table: a bad signature");
	strcpy(tampered_signature, signature);
	tampered_signature[5] = tampered_signature[5] == '0' ? '1' : '0';
	check(netversion_table_check(body, length, tampered_signature, strlen(tampered_signature), public_key, &table,
		error, (int)sizeof(error)) == NETVERSION_TABLE_BAD_SIGNATURE && table_empty(), "a digit of the signature changed");
	sign_body(body, length, other_seed, tampered_signature);
	check(netversion_table_check(body, length, tampered_signature, strlen(tampered_signature), public_key, &table,
		error, (int)sizeof(error)) == NETVERSION_TABLE_BAD_SIGNATURE, "signed with another key: refused");
	check(netversion_table_check(body, length, tampered_signature, strlen(tampered_signature), other_public, &table,
		error, (int)sizeof(error)) == NETVERSION_TABLE_OK, "... which that key's check takes");
	{
		unsigned char zero[32];

		memset(zero, 0, sizeof(zero));
		check(netversion_table_check(body, length, signature, strlen(signature), zero, &table, error,
			(int)sizeof(error)) == NETVERSION_TABLE_BAD_SIGNATURE, "a zero key (a placeholder): nothing taken");
	}
	check(netversion_table_check(body, length, signature, 100, public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_MALFORMED, "a signature file cut short: refused");
	check(netversion_table_check(body, length, signature, 128, public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_OK, "a signature file without its newline: taken");
	strcpy(tampered_signature, signature);
	strcpy(tampered_signature + 128, "\r\n");
	check(netversion_table_check(body, length, tampered_signature, 130, public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_OK, "... with CR LF: taken");
	strcpy(tampered_signature + 128, "\nx");
	check(netversion_table_check(body, length, tampered_signature, 130, public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_MALFORMED, "... with something after it: refused");
	for (index = 0; index < 128; index++)
		tampered_signature[index] = (char)(signature[index] >= 'a' ? signature[index] - 'a' + 'A' : signature[index]);
	tampered_signature[128] = '\n';
	check(memcmp(tampered_signature, signature, 128) == 0 ||
		netversion_table_check(body, length, tampered_signature, 129, public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_MALFORMED, "a signature in capitals: refused");
	check(netversion_table_check(body, length - 1, signature, strlen(signature), public_key, &table, error,
		(int)sizeof(error)) == NETVERSION_TABLE_BAD_SIGNATURE, "a table cut short: refused");
	memcpy(tampered, body, length);
	memcpy(tampered + length, "join 70 rules 39\n", 18);
	check(netversion_table_check(tampered, length + 17, signature, strlen(signature), public_key, &table, error,
		(int)sizeof(error)) == NETVERSION_TABLE_BAD_SIGNATURE, "a row added after signing: refused");
	check(netversion_table_check(NULL, 0, signature, strlen(signature), public_key, &table, error, (int)sizeof(error)) ==
		NETVERSION_TABLE_TOO_LARGE && netversion_table_check(body, length, NULL, 0, public_key, &table, error,
		(int)sizeof(error)) == NETVERSION_TABLE_MALFORMED, "no table, no signature: refused");

	/* the form, signed */
	{
		static const char *const bad[] =
		{
			"",
			"halo-ce-vita-netversions 2\nversion 3\n",
			"halo-ce-vita-testers 1\nversion 3\n",
			"halo-ce-vita-update 1\nversion 3\n",
			"halo-ce-vita-netversions 1\nversion 03\n",
			"halo-ce-vita-netversions 1\nversion 0\n",
			"halo-ce-vita-netversions 1\nversion 4294967296\n",
			"halo-ce-vita-netversions 1\nversion 3",
			"halo-ce-vita-netversions 1\r\nversion 3\r\n",
			"halo-ce-vita-netversions 1\nversion 3\n\n",
			" halo-ce-vita-netversions 1\nversion 3\n",
			HEADER "join 040 rules 39\n",
			HEADER "join 40 rules 039\n",
			HEADER "join 0 rules 39\n",
			HEADER "join 40 rules 0\n",
			HEADER "join 65536 rules 39\n",
			HEADER "join 40 rules 65536\n",
			HEADER "join 40 rules 39",
			HEADER "join 40 rules 39\r\n",
			HEADER "join 40  rules 39\n",
			HEADER "join 40 rules\n",
			HEADER "join 40\n",
			HEADER "join 40 39\n",
			HEADER "JOIN 40 rules 39\n",
			HEADER "join -40 rules 39\n",
			HEADER "join 40 rules 39 \n",
			HEADER "join 41 rules 39\njoin 40 rules 39\n",
			HEADER "join 40 rules 39\njoin 40 rules 38\n",
			HEADER "join 40 rules 39\nrefuse 40 twice\n",
			HEADER "refuse 40\n",
			HEADER "refuse 40 \n",
			HEADER "refuse 40  two spaces first\n",
			HEADER "refuse 40 ends with a space \n",
			HEADER "refuse 40 a tab\there\n",
			HEADER "refuse 40 a quote \" here\n",
			HEADER "refuse 40 percent %s here\n",
			HEADER "refuse 40 a backslash \\ here\n",
			HEADER "refuse 40 \xc3\xa9t\xc3\xa9\n",
			HEADER "refuse 40 an <html> tag\n",
			HEADER "refuse 40 x\x01y\n",
			HEADER "refuse 40 0123456789012345678901234567890123456789012345678901234567890123456789\n",
			HEADER "allow 40\n",
			HEADER "# a comment\n",
			HEADER "join 40 rules 39\nsignature 00\n",
		};

		for (index = 0; index < (int)(sizeof(bad) / sizeof(bad[0])); index++)
		{
			char what[200];
			int result = check_signed(bad[index]);

			snprintf(what, sizeof(what), "malformed #%d refused (signed)", index);
			check(result == NETVERSION_TABLE_MALFORMED && table_empty(), what);
		}
		check(check_signed(HEADER "refuse 40 0123456789012345678901234567890123456789012345678901234567890123\n") ==
			NETVERSION_TABLE_OK && strlen(table.rows[0].reason) == 64, "a reason of 64 characters: taken");
		check(check_signed(HEADER "refuse 40 A-Z a-z 0-9 . , : ; ! ? ' ( ) / + -\n") == NETVERSION_TABLE_OK,
			"a reason of every character allowed: taken");
	}

	/* rows: as many as a table takes, and one more */
	{
		int at = snprintf(big, sizeof(big), "%s", HEADER);

		for (index = 0; index < NETVERSION_TABLE_MAXIMUM_ROWS; index++)
			at += snprintf(big + at, sizeof(big) - (size_t)at, "refuse %d "
				"0123456789012345678901234567890123456789012345678901234567890123\n", 1000 + index);
		check((size_t)at <= NETVERSION_TABLE_MAXIMUM_SIZE, "the largest table fits NETVERSION_TABLE_MAXIMUM_SIZE");
		check(check_signed(big) == NETVERSION_TABLE_OK && table.count == NETVERSION_TABLE_MAXIMUM_ROWS,
			"the most rows: taken");
		snprintf(big + at, sizeof(big) - (size_t)at, "join 2000 rules 39\n");
		check(check_signed(big) == NETVERSION_TABLE_MALFORMED || check_signed(big) == NETVERSION_TABLE_TOO_LARGE,
			"one row more: refused");
		memset(big, 'a', sizeof(big));
		check(netversion_table_check(big, NETVERSION_TABLE_MAXIMUM_SIZE + 1, signature, strlen(signature), public_key,
			&table, error, (int)sizeof(error)) == NETVERSION_TABLE_TOO_LARGE, "larger than any table: refused unread");
		check(netversion_table_check(body, length, big, NETVERSION_TABLE_SIGNATURE_MAXIMUM_SIZE + 1, public_key, &table,
			error, (int)sizeof(error)) == NETVERSION_TABLE_TOO_LARGE, "a signature file larger than one: refused unread");
	}

	/* the newest wins */
	memset(&current, 0, sizeof(current));
	sign_body(body, length, seed, signature);
	check(netversion_table_newer(body, length, signature, strlen(signature), public_key, &current, error,
		(int)sizeof(error)) == NETVERSION_TABLE_OK && current.version == 3, "version 3 over none: taken");
	check(netversion_table_newer(body, length, signature, strlen(signature), public_key, &current, error,
		(int)sizeof(error)) == NETVERSION_TABLE_NOT_NEWER && current.version == 3 && current.count == 4,
		"version 3 again: not newer, kept");
	snprintf(tampered, sizeof(tampered), "%s", "halo-ce-vita-netversions 1\nversion 2\njoin 45 rules 39\n");
	sign_body(tampered, strlen(tampered), seed, tampered_signature);
	check(netversion_table_newer(tampered, strlen(tampered), tampered_signature, strlen(tampered_signature), public_key,
		&current, error, (int)sizeof(error)) == NETVERSION_TABLE_NOT_NEWER && current.version == 3 &&
		netversion_table_find(&current, 40), "an older version (2, a replay): not taken, 3 kept");
	snprintf(tampered, sizeof(tampered), "%s", "halo-ce-vita-netversions 1\nversion 9\njoin 45 rules 39\n");
	tampered_signature[0] ^= 1;
	check(netversion_table_newer(tampered, strlen(tampered), tampered_signature, strlen(tampered_signature), public_key,
		&current, error, (int)sizeof(error)) != NETVERSION_TABLE_OK && current.version == 3,
		"a newer version with a bad signature: not taken, 3 kept");
	sign_body(tampered, strlen(tampered), seed, tampered_signature);
	check(netversion_table_newer(tampered, strlen(tampered), tampered_signature, strlen(tampered_signature), public_key,
		&current, error, (int)sizeof(error)) == NETVERSION_TABLE_OK && current.version == 9 && current.count == 1 &&
		netversion_table_find(&current, 45) && !netversion_table_find(&current, 40), "version 9: taken whole");

	/* the tool's table */
	{
		const char *file = getenv("NETVERSIONS_TEST_FILE");

		if (file && file[0])
		{
			char path[1024];
			static char file_text[NETVERSION_TABLE_MAXIMUM_SIZE + 2];
			char file_signature[256];
			FILE *stream;
			size_t file_size = 0, signature_size = 0;

			stream = fopen(file, "rb");
			if (stream)
			{
				file_size = fread(file_text, 1, sizeof(file_text), stream);
				fclose(stream);
			}
			snprintf(path, sizeof(path), "%s.sig", file);
			stream = fopen(path, "rb");
			if (stream)
			{
				signature_size = fread(file_signature, 1, sizeof(file_signature), stream);
				fclose(stream);
			}
			check(netversion_table_check(file_text, file_size, file_signature, signature_size, public_key, &table, error,
				(int)sizeof(error)) == NETVERSION_TABLE_OK, "sign_netversions.py's table: taken");
			check(table.version == strtoul(getenv("NETVERSIONS_TEST_FILE_VERSION") ? getenv("NETVERSIONS_TEST_FILE_VERSION") :
				"0", NULL, 10), "... at the version the tool said");
			row = netversion_table_find(&table, 40);
			check(row && row->kind == NETVERSION_ROW_JOIN && row->rules == 39, "... join 40 rules 39");
			row = netversion_table_find(&table, 50);
			check(row && row->kind == NETVERSION_ROW_REFUSE && !strcmp(row->reason, "OpenCE 50 changed the record"),
				"... refuse 50 with its reason");
		}
	}

	printf("%s (%d of %d checks failed)\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures != 0;
}
