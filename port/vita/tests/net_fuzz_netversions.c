/*
NET_FUZZ_NETVERSIONS.C

A fuzz target for the signed crossplay version table (port/linux/src/
netversion_table.c) and what the network wire makes of one
(port/linux/src/network_wire.c): what GitHub, or anyone between, could
serve as netversions.txt and its .sig. The input's first byte picks how it
is read:
  even  the rest is the table, signed here with the target's own key (the
        owner's stands in for it) and checked: the form's reader sees every
        byte, as only the owner's table can reach it;
  odd   the second byte is the signature file's length: that many bytes
        are the signature file, the rest the table, checked as fetched (a
        forged or broken signature: never taken).
Each is given in a buffer exactly its size (AddressSanitizer catches a byte
read past it). A table taken must hold what netversion_table.h says (rows in
order of their versions, at most NETVERSION_TABLE_MAXIMUM_ROWS, each a join
with rules or a refusal with a reason of its characters), and once in
effect (network_wire_table_set) every version's answers must agree: a
version joined plays a built-in version's rules, one refused is not joined
and says why, the built-in versions keep their own rules, 18 is never a
crossplay version, the joined list's text ends within its buffer, and with
no table again the built-in list stands. Built as the other targets are, by
run_net_fuzz_test.sh.
*/

#include "../../linux/src/network_wire.c"
#include "netversion_table.h"
#include "p2p_internal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- stand-ins: the settings, the log and the translations */

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

void platform_log(const char *format, ...)
{
	(void)format;
}

const char *lang_text(const char *english)
{
	return english;
}

void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);

static void fuzz_abort(const char *what)
{
	fprintf(stderr, "net_fuzz_netversions: %s\n", what);
	abort();
}

static unsigned char fuzz_seed[32], fuzz_key[32];

static void fuzz_keys(void)
{
	static int made;
	int index;

	if (made)
		return;
	made = 1;
	for (index = 0; index < 32; index++)
		fuzz_seed[index] = (unsigned char)(0x5A ^ index * 13);
	p2p_ed25519_public(fuzz_seed, fuzz_key, NULL);
}

static int reason_ok(const char *reason)
{
	size_t length = strlen(reason), index;

	if (!length || length >= NETVERSION_TABLE_REASON_SIZE || reason[0] == ' ' || reason[length - 1] == ' ')
		return 0;
	for (index = 0; index < length; index++)
	{
		char character = reason[index];

		if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9') || strchr(" .,:;!?'()/+-", character)))
		{
			return 0;
		}
	}
	return 1;
}

/* a table taken: its form, then what the wire makes of it */
static void fuzz_taken(const struct netversion_table *table)
{
	static const unsigned int built_in[] = { 29, 32, 38, 39, 40, 41 };
	enum { BUILT_IN = (int)(sizeof(built_in) / sizeof(built_in[0])) };
	unsigned int previous = 0, version;
	char text[48], reason[128];
	int index;

	if (!table->version || table->count < 0 || table->count > NETVERSION_TABLE_MAXIMUM_ROWS)
		fuzz_abort("a table taken out of range");
	for (index = 0; index < table->count; index++)
	{
		const struct netversion_row *row = &table->rows[index];

		if (row->version <= previous)
			fuzz_abort("rows out of order");
		previous = row->version;
		if (row->kind == NETVERSION_ROW_JOIN ? !row->rules || row->reason[0] :
			row->kind != NETVERSION_ROW_REFUSE || !reason_ok(row->reason))
		{
			fuzz_abort("a row that is not one");
		}
		if (netversion_table_find(table, row->version) != row)
			fuzz_abort("a row not found by its version");
	}
	network_wire_table_set(table);
	if (network_wire_table_version() != table->version)
		fuzz_abort("the table in effect is not the one set");
	for (index = 0; index <= table->count + BUILT_IN; index++)
	{
		const struct netversion_row *row = index < table->count ? &table->rows[index] : NULL;
		unsigned int rules;
		int refused;

		version = row ? row->version : index == table->count ? 18 : built_in[index - table->count - 1];
		rules = network_wire_rules_version(version);
		refused = network_wire_crossplay_refusal(version, reason, (int)sizeof(reason));
		if (network_wire_of_version(version) == 1)
		{
			if (!network_wire_rules_built_in(rules) || refused)
				fuzz_abort("a version joined by rules not built in, or refused");
			if (network_wire_rules_built_in(version) && rules != version)
				fuzz_abort("a built-in version played by other rules");
			network_wire_choose_version(version, 2, "fuzz");
			if (!network_wire_crossplay() || network_wire_version() != rules)
				fuzz_abort("a version joined is not played by its rules");
			network_wire_reset();
		}
		else
		{
			if (version == 18 ? network_wire_of_version(18) != 0 : rules != version)
				fuzz_abort("a version not joined is not given back as it is");
			/* (a row for 18 is ignored: the legacy wire's own, never the table's) */
			if (version == 18 && refused)
				fuzz_abort("18 refused by the table");
			if (version != 18 && row && row->kind == NETVERSION_ROW_REFUSE && (!refused || strcmp(reason, row->reason)))
				fuzz_abort("a refused version without its reason");
		}
	}
	network_wire_crossplay_versions_text(text, (int)sizeof(text));
	if (!memchr(text, 0, sizeof(text)))
		fuzz_abort("the joined list's text past its buffer");
	(void)network_wire_crossplay_version_near(0xFFFF);
	network_wire_table_set(NULL);
	for (index = 0; index < BUILT_IN; index++)
	{
		if (network_wire_of_version(built_in[index]) != 1 || network_wire_rules_version(built_in[index]) != built_in[index])
			fuzz_abort("no table: a built-in version not joined by its own rules");
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct netversion_table table;
	char error[128];
	char *text, *signature;
	size_t text_size, signature_size;
	int result;

	fuzz_keys();
	if (size < 1)
		return 0;
	if (!(data[0] & 1))
	{
		unsigned char bytes[64];
		static const char digits[] = "0123456789abcdef";
		int index;

		text_size = size - 1;
		text = malloc(text_size ? text_size : 1);
		signature = malloc(129);
		if (!text || !signature)
			fuzz_abort("out of memory");
		memcpy(text, data + 1, text_size);
		p2p_ed25519_sign(fuzz_seed, fuzz_key, text, (int)text_size, bytes);
		for (index = 0; index < 64; index++)
		{
			signature[2 * index] = digits[bytes[index] >> 4];
			signature[2 * index + 1] = digits[bytes[index] & 15];
		}
		signature[128] = '\n';
		signature_size = 129;
	}
	else
	{
		if (size < 2)
			return 0;
		signature_size = data[1];
		if (signature_size > size - 2)
			signature_size = size - 2;
		text_size = size - 2 - signature_size;
		signature = malloc(signature_size ? signature_size : 1);
		text = malloc(text_size ? text_size : 1);
		if (!text || !signature)
			fuzz_abort("out of memory");
		memcpy(signature, data + 2, signature_size);
		memcpy(text, data + 2 + signature_size, text_size);
	}
	result = netversion_table_check(text, text_size, signature, signature_size, fuzz_key, &table, error,
		(int)sizeof(error));
	if (!memchr(error, 0, sizeof(error)))
		fuzz_abort("the error past its buffer");
	if (result == NETVERSION_TABLE_OK)
		fuzz_taken(&table);
	else if (table.version || table.count)
		fuzz_abort("a table refused but not emptied");
	else if (!(data[0] & 1) && text_size <= NETVERSION_TABLE_MAXIMUM_SIZE && result == NETVERSION_TABLE_BAD_SIGNATURE)
		fuzz_abort("a table signed here refused for its signature");
	free(text);
	free(signature);
	return 0;
}

/* the checks: the seeds (a signed table of each kind of row), and a forged
signature never taken */
int net_fuzz_checks(void)
{
	static const char *const seeds[] =
	{
		"\0halo-ce-vita-netversions 1\nversion 3\njoin 40 rules 39\njoin 41 rules 41\nrefuse 50 OpenCE 50 changed it\n",
		"\0halo-ce-vita-netversions 1\nversion 1\n",
		"\0halo-ce-vita-netversions 1\nversion 9\njoin 18 rules 32\nrefuse 38 kill switch\njoin 39 rules 32\njoin 42 rules 38\n",
		"\0halo-ce-vita-netversions 1\nversion 4294967295\nrefuse 65535 0123456789012345678901234567890123456789012345678901234567890123\n",
	};
	const char *folder = getenv("NET_FUZZ_SEEDS");
	int index, failures = 0;
	char name[32];

	fuzz_keys();
	for (index = 0; index < (int)(sizeof(seeds) / sizeof(seeds[0])); index++)
	{
		size_t length = 1 + strlen(seeds[index] + 1);
		struct netversion_table table;
		char error[64], signature[129];
		unsigned char bytes[64];
		int digit;

		LLVMFuzzerTestOneInput((const uint8_t *)seeds[index], length);
		p2p_ed25519_sign(fuzz_seed, fuzz_key, seeds[index] + 1, (int)length - 1, bytes);
		for (digit = 0; digit < 64; digit++)
			snprintf(signature + 2 * digit, 3, "%02x", bytes[digit]);
		if (netversion_table_check(seeds[index] + 1, length - 1, signature, 128, fuzz_key, &table, error,
				(int)sizeof(error)) != NETVERSION_TABLE_OK)
		{
			printf("FAIL seed %d not taken: %s\n", index, error);
			failures++;
		}
		signature[3] ^= 1;
		if (netversion_table_check(seeds[index] + 1, length - 1, signature, 128, fuzz_key, &table, error,
				(int)sizeof(error)) == NETVERSION_TABLE_OK)
		{
			printf("FAIL seed %d taken with a forged signature\n", index);
			failures++;
		}
		if (folder)
		{
			snprintf(name, sizeof(name), "seed-%d", index);
			net_fuzz_write_seed(folder, name, seeds[index], length);
		}
	}
	printf("%s netversions checks (%d failed)\n", failures ? "FAIL" : "PASS", failures);
	return failures;
}
