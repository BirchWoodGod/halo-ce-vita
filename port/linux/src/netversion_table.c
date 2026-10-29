/*
NETVERSION_TABLE.C

The signed crossplay version table (netversion_table.h): checked with the
owner's release key, the signature before anything of the table is read,
then the table's form, strictly, by length. Nothing here touches the system:
netversion_fetch.c fetches and keeps it, network_wire.c uses it.
tests/netversion_table_test.c and tests/netversion_table_fuzz.c test it.
*/

#include "netversion_table.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef HALO_NETVERSIONS_TEST_KEY
#ifdef HALO_RELEASE
#error "HALO_NETVERSIONS_TEST_KEY (configure.py --netversions-test-key) is for test builds only, never a release"
#endif
#define NETVERSIONS_PUBLIC_KEY HALO_NETVERSIONS_TEST_KEY
#else
#include "../../vita/include/vita_update_key.h"
#define NETVERSIONS_PUBLIC_KEY HALO_RELEASE_PUBLIC_KEY
#endif

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	return -1;
}

/* exactly 2 * size lowercase hexadecimal digits into bytes: 1, else 0 */
static int hex_decode(const char *text, unsigned char *bytes, size_t size)
{
	size_t index;

	for (index = 0; index < size; index++)
	{
		int high = hex_value(text[2 * index]), low = hex_value(text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

void netversion_table_public_key(unsigned char key[32])
{
	static const char text[] = NETVERSIONS_PUBLIC_KEY;
	size_t index;

	typedef char key_length_check[sizeof(text) == 65 ? 1 : -1];
	(void)sizeof(key_length_check);
	memset(key, 0, 32);
	/* (the header's digits may be either case) */
	for (index = 0; index < 64; index++)
	{
		char character = text[index];
		int value;

		if (character >= 'A' && character <= 'F')
			character = (char)(character - 'A' + 'a');
		value = hex_value(character);
		if (value < 0)
		{
			memset(key, 0, 32);
			return;
		}
		key[index / 2] |= (unsigned char)(index & 1 ? value : value << 4);
	}
}

/* the reader: text and its end */
struct reader
{
	const char *at;
	const char *end;
};

/* the literal word at the reader: 1 (passed), else 0 */
static int take_word(struct reader *reader, const char *word)
{
	size_t length = strlen(word);

	if ((size_t)(reader->end - reader->at) < length || memcmp(reader->at, word, length))
		return 0;
	reader->at += length;
	return 1;
}

/* 1 to maximum_digits decimal digits, no leading zero, not 0: 1 and the
value, else 0 */
static int take_number(struct reader *reader, int maximum_digits, unsigned long long *value)
{
	int digits = 0;

	*value = 0;
	if (reader->at < reader->end && *reader->at == '0')
		return 0;
	while (reader->at < reader->end && *reader->at >= '0' && *reader->at <= '9')
	{
		if (++digits > maximum_digits)
			return 0;
		*value = *value * 10 + (unsigned long long)(*reader->at - '0');
		reader->at++;
	}
	return digits > 0;
}

static int reason_character(char character)
{
	return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
		(character >= '0' && character <= '9') || (character && strchr(" .,:;!?'()/+-", character) != NULL);
}

/* a reason up to the line's end: 1 to NETVERSION_TABLE_REASON_SIZE - 1 of
reason_character, not starting or ending with a space */
static int take_reason(struct reader *reader, char *reason)
{
	int length = 0;

	while (reader->at < reader->end && *reader->at != '\n')
	{
		if (length >= NETVERSION_TABLE_REASON_SIZE - 1 || !reason_character(*reader->at))
			return 0;
		reason[length++] = *reader->at++;
	}
	reason[length] = 0;
	return length > 0 && reason[0] != ' ' && reason[length - 1] != ' ';
}

static int fail(int code, char *error, int error_size, const char *why, struct netversion_table *table)
{
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", why);
	if (table)
		memset(table, 0, sizeof(*table));
	return code;
}

/* the signature file's text: 128 digits, a newline (or CR LF) or none */
static int signature_parse(const char *text, size_t size, unsigned char signature[P2P_SIGNATURE_SIZE])
{
	if (!text)
		return 0;
	if (size == 2 * P2P_SIGNATURE_SIZE + 2 && text[2 * P2P_SIGNATURE_SIZE] == '\r' &&
		text[2 * P2P_SIGNATURE_SIZE + 1] == '\n')
	{
		size = 2 * P2P_SIGNATURE_SIZE;
	}
	else if (size == 2 * P2P_SIGNATURE_SIZE + 1 && text[2 * P2P_SIGNATURE_SIZE] == '\n')
		size = 2 * P2P_SIGNATURE_SIZE;
	return size == 2 * P2P_SIGNATURE_SIZE && hex_decode(text, signature, P2P_SIGNATURE_SIZE);
}

int netversion_table_check(const char *text, size_t size, const char *signature_text, size_t signature_size,
	const unsigned char key[32], struct netversion_table *table, char *error, int error_size)
{
	unsigned char signature[P2P_SIGNATURE_SIZE];
	struct reader reader;
	unsigned long long value;
	unsigned int previous = 0;
	int index, zero_key = 1;

	memset(table, 0, sizeof(*table));
	if (error && error_size > 0)
		error[0] = 0;
	if (!text || size > NETVERSION_TABLE_MAXIMUM_SIZE || signature_size > NETVERSION_TABLE_SIGNATURE_MAXIMUM_SIZE)
		return fail(NETVERSION_TABLE_TOO_LARGE, error, error_size, "larger than any table can be", table);
	if (!signature_parse(signature_text, signature_size, signature))
		return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "the signature file is not one", table);
	for (index = 0; index < 32; index++)
		zero_key &= key[index] == 0;
	/* (the signature before anything of the table is read; never a zero key) */
	if (zero_key || size > 0x7FFFFFFF || !p2p_ed25519_verify(key, text, (int)size, signature))
		return fail(NETVERSION_TABLE_BAD_SIGNATURE, error, error_size, "the signature is not the owner's key's", table);

	reader.at = text;
	reader.end = text + size;
	if (!take_word(&reader, NETVERSION_TABLE_MAGIC "\nversion ") || !take_number(&reader, 10, &value) ||
		value > 0xFFFFFFFFULL || !take_word(&reader, "\n"))
	{
		return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "not a crossplay version table (its first two lines)",
			table);
	}
	table->version = (unsigned long)value;
	while (reader.at < reader.end)
	{
		struct netversion_row *row;

		if (table->count >= NETVERSION_TABLE_MAXIMUM_ROWS)
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "more rows than a table takes", table);
		row = &table->rows[table->count];
		if (take_word(&reader, "join "))
			row->kind = NETVERSION_ROW_JOIN;
		else if (take_word(&reader, "refuse "))
			row->kind = NETVERSION_ROW_REFUSE;
		else
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "a row is neither join nor refuse", table);
		if (!take_number(&reader, 5, &value) || value > 0xFFFF)
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "a row's network version is not one", table);
		if ((unsigned int)value <= previous)
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "the rows are not in order of their versions",
				table);
		row->version = (unsigned short)value;
		previous = (unsigned int)value;
		if (row->kind == NETVERSION_ROW_JOIN)
		{
			if (!take_word(&reader, " rules ") || !take_number(&reader, 5, &value) || value > 0xFFFF)
				return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "a join row's rules are not a version", table);
			row->rules = (unsigned short)value;
		}
		else if (!take_word(&reader, " ") || !take_reason(&reader, row->reason))
		{
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "a refuse row's reason is not one", table);
		}
		if (!take_word(&reader, "\n"))
			return fail(NETVERSION_TABLE_MALFORMED, error, error_size, "a row does not end its line", table);
		table->count++;
	}
	return NETVERSION_TABLE_OK;
}

int netversion_table_newer(const char *text, size_t size, const char *signature, size_t signature_size,
	const unsigned char key[32], struct netversion_table *current, char *error, int error_size)
{
	struct netversion_table candidate;
	int result = netversion_table_check(text, size, signature, signature_size, key, &candidate, error, error_size);

	if (result != NETVERSION_TABLE_OK)
		return result;
	if (candidate.version <= current->version)
	{
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "version %lu is not newer than %lu", candidate.version,
				current->version);
		return NETVERSION_TABLE_NOT_NEWER;
	}
	memcpy(current, &candidate, sizeof(candidate));
	return NETVERSION_TABLE_OK;
}

const struct netversion_row *netversion_table_find(const struct netversion_table *table, unsigned int version)
{
	int index;

	if (!table)
		return NULL;
	for (index = 0; index < table->count && index < NETVERSION_TABLE_MAXIMUM_ROWS; index++)
	{
		if (table->rows[index].version == version)
			return &table->rows[index];
	}
	return NULL;
}
