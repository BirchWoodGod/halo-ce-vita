/*
TESTER_LIST_TEST.C

The signed tester list (port/linux/src/tester_list.c), with a throwaway key
made at test time (run_tester_list_test.sh: TESTERS_TEST_SEED, and the
build's HALO_TESTERS_TEST_KEY its public half). Checked:

- a list signed with the key: taken, its version, testers, expiries and
  labels; its testers found, others not;
- the build's key is the test key (tester_list_public_key);
- a byte of it changed (in the version, a key, an expiry, a label, the
  signature), a list signed with another key, one cut short, one with
  something after the signature: refused, the list left empty;
- a list's form, signed: leading zeros, capitals, a missing label, a label
  too long, a CRLF, a version past 32 bits, an expiry of 13 digits, a line of
  another kind, too many testers: refused (still signed: the form is checked
  as well as the signature);
- expiry: an entry past its second does not count (found -2), one before
  it does; with the clock unknown (0) only the entries that never end count;
- the newest wins: an older or equal version is not taken in place of the
  one had, a newer one is (and revokes whoever it leaves out);
- (TESTERS_TEST_FILE) the list tools/release/sign_testers.py made with the
  same key reads as made.

Prints PASS or the failures. Built with ASan and UBSan.
*/

#include "tester_list.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int condition, const char *what)
{
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

/* body signed with a seed: body then "signature <hex>\n", into text; its size */
static size_t sign_body(const char *body, const unsigned char *with_seed, char *text)
{
	unsigned char key[32], signature[64];
	char hex[129];
	size_t length = strlen(body);

	p2p_ed25519_public(with_seed, key, NULL);
	p2p_ed25519_sign(with_seed, key, body, (int)length, signature);
	to_hex(signature, 64, hex);
	memcpy(text, body, length);
	length += (size_t)sprintf(text + length, "signature %s\n", hex);
	return length;
}

static unsigned char tester_keys[4][32];
static char tester_hex[4][65];

static struct tester_list list, current;
static char text[TESTER_LIST_MAXIMUM_SIZE + 4096];
static char body[TESTER_LIST_MAXIMUM_SIZE + 4096];

static int check_text(const char *data, size_t size)
{
	char error[128];

	return tester_list_check(data, size, public_key, &list, error, (int)sizeof(error));
}

static int check_body(const char *with_body)
{
	size_t size = sign_body(with_body, seed, text);

	return check_text(text, size);
}

int main(void)
{
	const char *seed_text = getenv("TESTERS_TEST_SEED");
	unsigned char built_key[32];
	char error[128];
	size_t size;
	int index;

	if (!seed_text || strlen(seed_text) != 64)
	{
		printf("FAIL no TESTERS_TEST_SEED\n");
		return 1;
	}
	hex_bytes(seed_text, seed, 32);
	p2p_ed25519_public(seed, public_key, NULL);
	for (index = 0; index < 32; index++)
		other_seed[index] = (unsigned char)(index * 7 + 3);
	p2p_ed25519_public(other_seed, other_public, NULL);
	for (index = 0; index < 4; index++)
	{
		unsigned char tester_seed[32];

		memset(tester_seed, 0x40 + index, sizeof(tester_seed));
		p2p_ed25519_public(tester_seed, tester_keys[index], NULL);
		to_hex(tester_keys[index], 32, tester_hex[index]);
	}

	/* the build's key */
	tester_list_public_key(built_key);
	check(!memcmp(built_key, public_key, 32), "the build trusts the test key (HALO_TESTERS_TEST_KEY)");

	/* a good list */
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 7\ntester %s 0 Alpha\ntester %s 2000000000 Bravo-AB12\n",
		tester_hex[0], tester_hex[1]);
	size = sign_body(body, seed, text);
	check(check_text(text, size) == TESTER_LIST_OK, "a signed list taken");
	check(list.version == 7 && list.count == 2, "its version and testers");
	check(list.entries[1].expires == 2000000000ULL && !strcmp(list.entries[1].label, "Bravo-AB12"),
		"an entry's expiry and label");
	check(tester_list_find(&list, tester_keys[0], 1800000000ULL) == 0, "a tester found");
	check(tester_list_find(&list, tester_keys[1], 1800000000ULL) == 1, "a tester before their expiry found");
	check(tester_list_find(&list, tester_keys[1], 2000000000ULL) == -2, "a tester at their expiry: expired");
	check(tester_list_find(&list, tester_keys[1], 0) == -2, "the clock unknown: an entry that ends does not count");
	check(tester_list_find(&list, tester_keys[0], 0) == 0, "the clock unknown: one that never ends does");
	check(tester_list_find(&list, tester_keys[2], 1800000000ULL) == -1, "someone else not found");
	check(tester_list_find(&list, public_key, 1800000000ULL) == -1, "the owner's key itself is no tester");

	/* tampered: every byte of it changed, one at a time */
	{
		char copy[sizeof(text)];
		int refused = 0, total = 0;
		size_t at;

		for (at = 0; at < size; at++)
		{
			memcpy(copy, text, size);
			copy[at] ^= 0x01;
			total++;
			if (check_text(copy, size) != TESTER_LIST_OK && list.count == 0 && list.version == 0)
				refused++;
		}
		check(refused == total, "a list with any byte changed refused, nothing of it kept");
		memcpy(copy, text, size);
		check(check_text(copy, size - 1) != TESTER_LIST_OK, "a list cut short refused");
		memcpy(copy + size, "x", 1);
		check(check_text(copy, size + 1) != TESTER_LIST_OK, "something after the signature refused");
		check(check_text(copy, 0) != TESTER_LIST_OK && check_text(copy, 10) != TESTER_LIST_OK, "empty, tiny refused");
	}
	{
		size_t other_size = sign_body(body, other_seed, text);

		check(check_text(text, other_size) == TESTER_LIST_BAD_SIGNATURE, "a list signed with another key refused");
		check(tester_list_check(text, other_size, other_public, &list, error, (int)sizeof(error)) == TESTER_LIST_OK,
			"(the same list taken with that key: the key is what refused it)");
	}

	/* the form, signed */
	{
		static const char *const bad[] = {
			"halo-ce-vita-testers 2\nversion 1\n",
			"halo-ce-vita-testers 1\nversion 01\n",
			"halo-ce-vita-testers 1\nversion 0\n",
			"halo-ce-vita-testers 1\nversion 4294967296\n",
			"halo-ce-vita-testers 1\nversion 1\r\n",
			"halo-ce-vita-testers 1\nversion 1",
			"halo-ce-vita-testers 1\nversion 1\n\n",
			"halo-ce-vita-testers 1\r\nversion 1\n",
			" halo-ce-vita-testers 1\nversion 1\n",
		};
		char line[512];

		for (index = 0; index < (int)(sizeof(bad) / sizeof(bad[0])); index++)
		{
			snprintf(line, sizeof(line), "a malformed header (%d) refused", index);
			check(check_body(bad[index]) == TESTER_LIST_MALFORMED, line);
		}
		check(check_body("halo-ce-vita-testers 1\nversion 4294967295\n") == TESTER_LIST_OK && list.count == 0,
			"the highest version, no testers: taken");
		{
			char upper[65];

			snprintf(upper, sizeof(upper), "%s", tester_hex[0]);
			for (index = 0; upper[index]; index++)
				if (upper[index] >= 'a' && upper[index] <= 'f')
					upper[index] = (char)(upper[index] - 32);
			snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 1\ntester %s 0 Alpha\n", upper);
			check(strcmp(upper, tester_hex[0]) && check_body(body) == TESTER_LIST_MALFORMED, "a key in capitals refused");
		}
		{
			static const char *const lines[] = {
				"tester %s 0 \n", "tester %s 0\n", "tester %s 00 Alpha\n", "tester %s 0123 Alpha\n",
				"tester %s 1234567890123 Alpha\n", "tester %s 0 Al pha\n", "tester %s 0 Al|pha\n",
				"tester %s 0 abcdefghijabcdefghijabcdefghijabc\n", "tester  %s 0 Alpha\n", "tester %s -1 Alpha\n",
				"tester %s 0 Alpha \n", "Tester %s 0 Alpha\n", "tester %s 0 Alpha\r\n", "tester %.62s 0 Alpha\n",
			};
			char entry[256];

			for (index = 0; index < (int)(sizeof(lines) / sizeof(lines[0])); index++)
			{
				snprintf(entry, sizeof(entry), lines[index], tester_hex[0]);
				snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 1\n%s", entry);
				snprintf(line, sizeof(line), "a malformed tester line (%d) refused", index);
				check(check_body(body) == TESTER_LIST_MALFORMED, line);
			}
			snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 1\ntester %s 0 abcdefghijabcdefghijabcdefghijab\n",
				tester_hex[0]);
			check(check_body(body) == TESTER_LIST_OK, "a label of 32 taken");
			snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 1\ntester %s 999999999999 a.b_c-D\n",
				tester_hex[0]);
			check(check_body(body) == TESTER_LIST_OK && list.entries[0].expires == 999999999999ULL,
				"an expiry of 12 digits taken");
		}
		/* the most testers, and one more */
		{
			size_t at = (size_t)sprintf(body, "halo-ce-vita-testers 1\nversion 3\n");

			for (index = 0; index < TESTER_LIST_MAXIMUM_ENTRIES; index++)
				at += (size_t)sprintf(body + at, "tester %s 999999999999 abcdefghijabcdefghijabcdefghijab\n",
					tester_hex[index & 3]);
			check(check_body(body) == TESTER_LIST_OK && list.count == TESTER_LIST_MAXIMUM_ENTRIES,
				"the most testers, the longest lines: taken");
			sprintf(body + at, "tester %s 0 x\n", tester_hex[0]);
			check(check_body(body) != TESTER_LIST_OK && list.count == 0, "one tester more refused");
		}
	}

	/* the newest wins */
	memset(&current, 0, sizeof(current));
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 5\ntester %s 0 Alpha\ntester %s 0 Charlie\n",
		tester_hex[0], tester_hex[2]);
	size = sign_body(body, seed, text);
	check(tester_list_newer(text, size, public_key, &current, error, (int)sizeof(error)) == TESTER_LIST_OK &&
		current.version == 5, "a first list taken");
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 4\ntester %s 0 Alpha\ntester %s 0 Bravo\n",
		tester_hex[0], tester_hex[1]);
	size = sign_body(body, seed, text);
	check(tester_list_newer(text, size, public_key, &current, error, (int)sizeof(error)) == TESTER_LIST_NOT_NEWER &&
		current.version == 5 && tester_list_find(&current, tester_keys[1], 1) == -1,
		"an older version not taken (Bravo of the older list not listed)");
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 5\ntester %s 0 Bravo\n", tester_hex[1]);
	size = sign_body(body, seed, text);
	check(tester_list_newer(text, size, public_key, &current, error, (int)sizeof(error)) == TESTER_LIST_NOT_NEWER,
		"the same version not taken");
	snprintf(body, sizeof(body), "halo-ce-vita-testers 1\nversion 6\ntester %s 0 Alpha\n", tester_hex[0]);
	size = sign_body(body, seed, text);
	text[40] ^= 1;
	check(tester_list_newer(text, size, public_key, &current, error, (int)sizeof(error)) != TESTER_LIST_OK &&
		current.version == 5, "a newer list tampered with not taken");
	text[40] ^= 1;
	check(tester_list_newer(text, size, public_key, &current, error, (int)sizeof(error)) == TESTER_LIST_OK &&
		current.version == 6 && tester_list_find(&current, tester_keys[2], 1) == -1 &&
		tester_list_find(&current, tester_keys[0], 1) == 0, "a newer list taken: Charlie revoked");

	/* the tool's list */
	if (getenv("TESTERS_TEST_FILE"))
	{
		FILE *file = fopen(getenv("TESTERS_TEST_FILE"), "rb");
		size_t read = file ? fread(text, 1, sizeof(text), file) : 0;
		const char *expected = getenv("TESTERS_TEST_FILE_KEY");
		unsigned char key[32];

		if (file)
			fclose(file);
		check(check_text(text, read) == TESTER_LIST_OK, "sign_testers.py's list taken");
		if (expected && strlen(expected) == 64)
		{
			hex_bytes(expected, key, 32);
			check(tester_list_find(&list, key, 1800000000ULL) >= 0, "sign_testers.py's tester listed");
		}
		check(list.version == (unsigned long)atol(getenv("TESTERS_TEST_FILE_VERSION") ?
			getenv("TESTERS_TEST_FILE_VERSION") : "0"), "sign_testers.py's version");
	}

	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
