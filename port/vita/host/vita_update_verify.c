/*
VITA_UPDATE_VERIFY.C

The checks a downloaded update passes before the Vita keeps it for the
player to install (vita_update_verify.h): the Ed25519 signature of the
release's manifest (Monocypher's crypto_ed25519_check, RFC 8032 with
SHA-512, which refuses a non-canonical signature), the manifest's strict
text, the version it names against the tag asked for and the running
build, and where a download may be redirected.

Everything read here is untrusted until the signature is checked and is
read by length within fixed bounds; nothing read is run, opened or followed
here. tests/update_download_test.c and tests/update_download_fuzz.c build it
alone (under AddressSanitizer and UBSan).
*/

#include "vita_update_verify.h"
#include "vita_update_key.h"
#include "update_check.h"

#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

/* ---------- hexadecimal */

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

int update_hex_decode(const char *text, size_t length, unsigned char *bytes, size_t size)
{
	size_t index;

	if (!text || !bytes || length != size * 2)
		return 0;
	for (index = 0; index < size; index++)
	{
		int high = hex_value(text[index * 2]), low = hex_value(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

static int all_zero(const unsigned char *bytes, size_t size)
{
	unsigned char any = 0;
	size_t index;

	for (index = 0; index < size; index++)
		any |= bytes[index];
	return !any;
}

int update_public_key(unsigned char key[32])
{
	static const char text[] = HALO_UPDATE_PUBLIC_KEY;

	if (!update_hex_decode(text, sizeof(text) - 1, key, 32) || all_zero(key, 32))
	{
		memset(key, 0, 32);
		return 0;
	}
	return 1;
}

int update_signature_parse(const char *text, size_t size, unsigned char signature[64])
{
	memset(signature, 0, 64);
	if (!text)
		return 0;
	if (size == 130 && text[128] == '\r' && text[129] == '\n')
		size = 128;
	else if (size == 129 && text[128] == '\n')
		size = 128;
	if (size != 128 || !update_hex_decode(text, size, signature, 64))
	{
		memset(signature, 0, 64);
		return 0;
	}
	return 1;
}

/* ---------- the manifest */

/* "<name> <value>\n" at text + *at (within size): the value's start and
length, *at past the line; 1, else 0. The value is at most maximum bytes,
each one allowed by the class. */
enum
{
	VALUE_VERSION,
	VALUE_TITLE,
	VALUE_FILE,
	VALUE_DIGITS,
	VALUE_LOWER_HEX,
};

static int value_character(int kind, char character)
{
	int digit = character >= '0' && character <= '9';

	switch (kind)
	{
	case VALUE_VERSION:
		return digit || (character >= 'a' && character <= 'z') || character == '.' || character == '-';
	case VALUE_TITLE:
		return digit || (character >= 'A' && character <= 'Z');
	case VALUE_FILE:
		return digit || (character >= 'a' && character <= 'z') || character == '.' || character == '_' ||
			character == '-';
	case VALUE_DIGITS:
		return digit;
	case VALUE_LOWER_HEX:
		return digit || (character >= 'a' && character <= 'f');
	}
	return 0;
}

static int read_line(const char *text, size_t size, size_t *at, const char *name, int kind, size_t maximum,
	const char **value, size_t *length)
{
	size_t name_length = strlen(name), start, end;

	if (size - *at < name_length + 1 || memcmp(text + *at, name, name_length) || text[*at + name_length] != ' ')
		return 0;
	start = *at + name_length + 1;
	for (end = start; end < size && text[end] != '\n'; end++)
		if (end - start >= maximum || !value_character(kind, text[end]))
			return 0;
	if (end >= size || end == start)
		return 0;
	*value = text + start;
	*length = end - start;
	*at = end + 1;
	return 1;
}

int update_manifest_parse(const char *text, size_t size, struct update_manifest *manifest)
{
	const size_t magic_length = sizeof(UPDATE_MANIFEST_MAGIC) - 1;
	const char *value;
	size_t at, length, index;
	char tag[sizeof(manifest->version) + 1];
	unsigned long number = 0;

	if (!manifest)
		return 0;
	memset(manifest, 0, sizeof(*manifest));
	if (!text || size > UPDATE_MANIFEST_MAXIMUM || size < magic_length + 1 ||
		memcmp(text, UPDATE_MANIFEST_MAGIC, magic_length) || text[magic_length] != '\n')
	{
		return 0;
	}
	at = magic_length + 1;
	/* version: a release tag's, without the v */
	if (!read_line(text, size, &at, "version", VALUE_VERSION, sizeof(manifest->version) - 1, &value, &length))
		goto malformed;
	tag[0] = 'v';
	memcpy(tag + 1, value, length);
	tag[length + 1] = 0;
	if (!update_tag_valid(tag))
		goto malformed;
	memcpy(manifest->version, value, length);
	/* title: a Vita title ID */
	if (!read_line(text, size, &at, "title", VALUE_TITLE, 9, &value, &length) || length != 9)
		goto malformed;
	memcpy(manifest->title, value, length);
	if (!read_line(text, size, &at, "file", VALUE_FILE, sizeof(manifest->file) - 1, &value, &length))
		goto malformed;
	memcpy(manifest->file, value, length);
	/* size: 1-9 digits, no leading zero */
	if (!read_line(text, size, &at, "size", VALUE_DIGITS, 9, &value, &length) || (value[0] == '0' && length > 1))
		goto malformed;
	for (index = 0; index < length; index++)
		number = number * 10 + (unsigned long)(value[index] - '0');
	manifest->size = number;
	if (!read_line(text, size, &at, "sha512", VALUE_LOWER_HEX, 128, &value, &length) ||
		!update_hex_decode(value, length, manifest->sha512, sizeof(manifest->sha512)))
	{
		goto malformed;
	}
	/* (nothing after) */
	if (at != size)
		goto malformed;
	return 1;

malformed:
	memset(manifest, 0, sizeof(*manifest));
	return 0;
}

static int fail(int code, struct update_manifest *manifest, char *error, int error_size, const char *why)
{
	memset(manifest, 0, sizeof(*manifest));
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", why);
	return code;
}

int update_manifest_check(const char *text, size_t size, const char *signature_text, size_t signature_size,
	const unsigned char key[32], const char *expected_version, const char *current_version, const char *title,
	struct update_manifest *manifest, char *error, int error_size)
{
	unsigned char signature[64];
	char why[160];

	if (error && error_size > 0)
		error[0] = 0;
	memset(manifest, 0, sizeof(*manifest));
	if (!key || all_zero(key, 32))
		return fail(UPDATE_VERIFY_NO_KEY, manifest, error, error_size, "no update key in this build");
	if (!update_signature_parse(signature_text, signature_size, signature))
		return fail(UPDATE_VERIFY_BAD_SIGNATURE, manifest, error, error_size, "the signature file is not a signature");
	if (!text || !size || size > UPDATE_MANIFEST_MAXIMUM)
		return fail(UPDATE_VERIFY_MALFORMED, manifest, error, error_size, "the manifest is empty or too large");
	/* (the signature first: nothing of the manifest is believed before) */
	if (crypto_ed25519_check(signature, key, (const unsigned char *)text, size) != 0)
		return fail(UPDATE_VERIFY_BAD_SIGNATURE, manifest, error, error_size,
			"the manifest's signature is not the update key's");
	if (!update_manifest_parse(text, size, manifest))
		return fail(UPDATE_VERIFY_MALFORMED, manifest, error, error_size, "the signed manifest is malformed");
	if (!expected_version || strcmp(manifest->version, expected_version))
	{
		snprintf(why, sizeof(why), "the manifest is for %.40s, not %.40s", manifest->version,
			expected_version ? expected_version : "?");
		return fail(UPDATE_VERIFY_WRONG_VERSION, manifest, error, error_size, why);
	}
	if (!current_version || !update_is_newer(manifest->version, current_version))
	{
		snprintf(why, sizeof(why), "%.40s is not newer than this build (%.40s)", manifest->version,
			current_version ? current_version : "?");
		return fail(UPDATE_VERIFY_NOT_NEWER, manifest, error, error_size, why);
	}
	if (!title || strcmp(manifest->title, title))
	{
		snprintf(why, sizeof(why), "the manifest is for title %.12s, not %.12s", manifest->title, title ? title : "?");
		return fail(UPDATE_VERIFY_WRONG_TITLE, manifest, error, error_size, why);
	}
	if (strcmp(manifest->file, UPDATE_ASSET_VPK))
		return fail(UPDATE_VERIFY_WRONG_FILE, manifest, error, error_size, "the manifest names another file");
	if (!manifest->size || manifest->size > UPDATE_VPK_MAXIMUM)
	{
		snprintf(why, sizeof(why), "the update's size (%lu bytes) is not taken", manifest->size);
		return fail(UPDATE_VERIFY_TOO_LARGE, manifest, error, error_size, why);
	}
	return UPDATE_VERIFY_OK;
}

/* ---------- URLs */

/* a decimal number of 1-3 digits at most 255 at *cursor: 1 (and *cursor past
it), else 0 */
static int read_octet(const char **cursor, unsigned int *value)
{
	int digits = 0;

	*value = 0;
	while (**cursor >= '0' && **cursor <= '9')
	{
		if (++digits > 3)
			return 0;
		*value = *value * 10 + (unsigned int)(*(*cursor)++ - '0');
	}
	return digits > 0 && *value <= 255;
}

/* "http://127.a.b.c[:port]/..." with each part in range: 1, else 0 */
static int loopback_url(const char *url)
{
	const char *cursor;
	unsigned int value;
	int part;

	if (!url || strncmp(url, "http://127.", 11))
		return 0;
	cursor = url + 11;
	for (part = 0; part < 3; part++)
	{
		if (!read_octet(&cursor, &value) || (part < 2 && *cursor++ != '.'))
			return 0;
	}
	if (*cursor == ':')
	{
		unsigned long port = 0;
		int digits = 0;

		for (cursor++; *cursor >= '0' && *cursor <= '9'; cursor++)
		{
			if (++digits > 5)
				return 0;
			port = port * 10 + (unsigned long)(*cursor - '0');
		}
		if (!digits || !port || port > 65535)
			return 0;
	}
	return *cursor == '/';
}

/* the rest of a URL after its host: printable ASCII, no space, at most
2000 bytes */
static int path_clean(const char *path)
{
	size_t length = 0;

	if (*path != '/')
		return 0;
	for (; path[length]; length++)
		if ((unsigned char)path[length] <= ' ' || (unsigned char)path[length] >= 0x7f || length >= 2000)
			return 0;
	return 1;
}

/* "https://github.com/<owner>/<repository>/releases/download": GitHub's
names (an owner of letters, digits and '-', a repository of those and '.'
and '_', neither "." nor ".."): 1, else 0 */
static int github_releases_base(const char *base)
{
	static const char prefix[] = "https://github.com/", suffix[] = "/releases/download";
	const char *cursor = base + sizeof(prefix) - 1;
	int part;

	if (strncmp(base, prefix, sizeof(prefix) - 1))
		return 0;
	for (part = 0; part < 2; part++)
	{
		const char *start = cursor;

		while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9') ||
			*cursor == '-' || (part == 1 && (*cursor == '.' || *cursor == '_')))
		{
			cursor++;
		}
		if (cursor == start || cursor - start > 100 || (cursor - start <= 2 && !strncmp(start, "..", (size_t)(cursor - start))))
			return 0;
		if (part == 0 && *cursor++ != '/')
			return 0;
	}
	return !strcmp(cursor, suffix);
}

int update_asset_url(char *url, int size, const char *base, const char *version, const char *asset)
{
	char tag[80];
	int length;

	if (!url || size <= 0)
		return 0;
	url[0] = 0;
	if (!base || !version || !asset || strlen(version) >= sizeof(tag) - 1)
		return 0;
	snprintf(tag, sizeof(tag), "v%s", version);
	if (!update_tag_valid(tag))
		return 0;
	if (strcmp(asset, UPDATE_ASSET_VPK) && strcmp(asset, UPDATE_ASSET_MANIFEST) && strcmp(asset, UPDATE_ASSET_SIGNATURE))
		return 0;
	/* GitHub's, another repository's releases on GitHub (a hardware test's,
	signed with a test build's key: tools/release/README.md), or the tests'
	server on this machine */
	if (strcmp(base, UPDATE_DOWNLOAD_BASE) && !github_releases_base(base))
	{
		char probe[300];

		if (strlen(base) > 200)
			return 0;
		snprintf(probe, sizeof(probe), "%s/", base);
		if (!loopback_url(probe) || !path_clean(strchr(probe + 7, '/')))
			return 0;
	}
	length = snprintf(url, (size_t)size, "%s/%s/%s", base, tag, asset);
	if (length <= 0 || length >= size)
	{
		url[0] = 0;
		return 0;
	}
	return 1;
}

int update_redirect_allowed(const char *from_url, const char *to_url)
{
	static const char *const hosts[] =
	{
		"release-assets.githubusercontent.com",
		"objects.githubusercontent.com",
	};
	size_t index;

	if (!from_url || !to_url)
		return 0;
	if (!strncmp(to_url, "https://", 8))
	{
		for (index = 0; index < sizeof(hosts) / sizeof(*hosts); index++)
		{
			size_t length = strlen(hosts[index]);

			/* (exactly the host: no port, no user, nothing after it but the
			path) */
			if (!strncmp(to_url + 8, hosts[index], length) && to_url[8 + length] == '/')
				return path_clean(to_url + 8 + length);
		}
		return 0;
	}
	/* (the tests: loopback to loopback, plain http) */
	return loopback_url(from_url) && loopback_url(to_url) && path_clean(strchr(to_url + 7, '/'));
}
