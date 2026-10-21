/*
TESTER_LIST.C

The signed tester list (tester_list.h): checked with the owner's release
key, the signature before anything of the list is read, then the list's
form, strictly, by length.
*/

#include "tester_list.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef HALO_TESTERS_TEST_KEY
#ifdef HALO_RELEASE
#error "HALO_TESTERS_TEST_KEY (configure.py --testers-test-key) is for test builds only, never a release"
#endif
#define TESTERS_PUBLIC_KEY HALO_TESTERS_TEST_KEY
#else
#include "../../vita/include/vita_update_key.h"
#define TESTERS_PUBLIC_KEY HALO_RELEASE_PUBLIC_KEY
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

void tester_list_public_key(unsigned char key[32])
{
	static const char text[] = TESTERS_PUBLIC_KEY;
	unsigned char decoded[32];
	size_t index;

	typedef char key_length_check[sizeof(text) == 65 ? 1 : -1];
	(void)sizeof(key_length_check);
	/* (the header's digits may be either case) */
	for (index = 0; index < 64; index++)
	{
		char character = text[index];

		if (character >= 'A' && character <= 'F')
			character = (char)(character - 'A' + 'a');
		if (hex_value(character) < 0)
		{
			memset(key, 0, 32);
			return;
		}
		if (index & 1)
			decoded[index / 2] |= (unsigned char)hex_value(character);
		else
			decoded[index / 2] = (unsigned char)(hex_value(character) << 4);
	}
	memcpy(key, decoded, 32);
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

/* 1 to maximum_digits decimal digits, no leading zero unless the number is
0 alone (zero_allowed): 1 and the value, else 0 */
static int take_number(struct reader *reader, int maximum_digits, int zero_allowed, unsigned long long *value)
{
	int digits = 0;

	*value = 0;
	if (reader->at < reader->end && *reader->at == '0')
	{
		if (!zero_allowed)
			return 0;
		reader->at++;
		return reader->at >= reader->end || *reader->at < '0' || *reader->at > '9';
	}
	while (reader->at < reader->end && *reader->at >= '0' && *reader->at <= '9')
	{
		if (++digits > maximum_digits)
			return 0;
		*value = *value * 10 + (unsigned long long)(*reader->at - '0');
		reader->at++;
	}
	return digits > 0;
}

static int take_hex(struct reader *reader, unsigned char *bytes, size_t size)
{
	if ((size_t)(reader->end - reader->at) < 2 * size || !hex_decode(reader->at, bytes, size))
		return 0;
	reader->at += 2 * size;
	return 1;
}

static int label_character(char character)
{
	return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
		(character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
}

static int take_label(struct reader *reader, char *label)
{
	int length = 0;

	while (reader->at < reader->end && label_character(*reader->at))
	{
		if (length >= TESTER_LIST_LABEL_SIZE - 1)
			return 0;
		label[length++] = *reader->at++;
	}
	label[length] = 0;
	return length > 0;
}

static int fail(int code, char *error, int error_size, const char *why, struct tester_list *list)
{
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", why);
	if (list)
		memset(list, 0, sizeof(*list));
	return code;
}

int tester_list_check(const char *text, size_t size, const unsigned char key[32], struct tester_list *list,
	char *error, int error_size)
{
	static const char signature_word[] = "signature ";
	unsigned char signature[P2P_SIGNATURE_SIZE];
	struct reader reader;
	unsigned long long value;
	size_t signed_size;
	const char *signature_line;

	memset(list, 0, sizeof(*list));
	if (error && error_size > 0)
		error[0] = 0;
	if (!text || size > TESTER_LIST_MAXIMUM_SIZE)
		return fail(TESTER_LIST_TOO_LARGE, error, error_size, "the list is larger than any list can be", list);
	/* the signature line: the last, exactly "signature <128 digits>\n" */
	if (size < sizeof(signature_word) - 1 + 2 * P2P_SIGNATURE_SIZE + 1)
		return fail(TESTER_LIST_MALFORMED, error, error_size, "no signature line", list);
	signed_size = size - (sizeof(signature_word) - 1 + 2 * P2P_SIGNATURE_SIZE + 1);
	signature_line = text + signed_size;
	if ((signed_size && text[signed_size - 1] != '\n') ||
		memcmp(signature_line, signature_word, sizeof(signature_word) - 1) || text[size - 1] != '\n' ||
		!hex_decode(signature_line + sizeof(signature_word) - 1, signature, P2P_SIGNATURE_SIZE))
	{
		return fail(TESTER_LIST_MALFORMED, error, error_size, "no signature line", list);
	}
	if (signed_size > 0x7FFFFFFF || !p2p_ed25519_verify(key, text, (int)signed_size, signature))
		return fail(TESTER_LIST_BAD_SIGNATURE, error, error_size, "the signature is not the owner's key's", list);

	/* the list, signed: its form */
	reader.at = text;
	reader.end = text + signed_size;
	if (!take_word(&reader, TESTER_LIST_MAGIC "\nversion ") || !take_number(&reader, 10, 0, &value) ||
		value > 0xFFFFFFFFULL || !take_word(&reader, "\n"))
	{
		return fail(TESTER_LIST_MALFORMED, error, error_size, "not a tester list (its first two lines)", list);
	}
	list->version = (unsigned long)value;
	while (reader.at < reader.end)
	{
		struct tester_entry *entry;

		if (list->count >= TESTER_LIST_MAXIMUM_ENTRIES)
			return fail(TESTER_LIST_MALFORMED, error, error_size, "more testers than a list takes", list);
		entry = &list->entries[list->count];
		if (!take_word(&reader, "tester ") || !take_hex(&reader, entry->key, 32) || !take_word(&reader, " ") ||
			!take_number(&reader, 12, 1, &entry->expires) || !take_word(&reader, " ") ||
			!take_label(&reader, entry->label) || !take_word(&reader, "\n"))
		{
			return fail(TESTER_LIST_MALFORMED, error, error_size, "a tester line is not one", list);
		}
		list->count++;
	}
	return TESTER_LIST_OK;
}

int tester_list_newer(const char *text, size_t size, const unsigned char key[32], struct tester_list *current,
	char *error, int error_size)
{
	static struct tester_list candidate;
	int result = tester_list_check(text, size, key, &candidate, error, error_size);

	if (result != TESTER_LIST_OK)
		return result;
	if (candidate.version <= current->version)
	{
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "version %lu is not newer than %lu", candidate.version,
				current->version);
		return TESTER_LIST_NOT_NEWER;
	}
	memcpy(current, &candidate, sizeof(candidate));
	return TESTER_LIST_OK;
}

int tester_list_find(const struct tester_list *list, const unsigned char key[32], unsigned long long now)
{
	int index, expired = 0;

	for (index = 0; index < list->count; index++)
	{
		const struct tester_entry *entry = &list->entries[index];

		if (!p2p_equal(entry->key, key, 32))
			continue;
		if (!entry->expires || (now && now < entry->expires))
			return index;
		expired = 1;
	}
	return expired ? -2 : -1;
}
