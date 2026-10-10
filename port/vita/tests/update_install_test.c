/*
UPDATE_INSTALL_TEST.C

The Vita updater's checks (port/vita/host/vita_update_verify.c), built by
run_update_install_test.sh with a throwaway key made at test time
(HALO_UPDATE_PUBLIC_KEY, and its seed HALO_TEST_SEED so this test signs
manifests of its own with Monocypher), and again without one (the
placeholder: HALO_TEST_PLACEHOLDER):
- a signed manifest taken; refused: another key's signature, a changed
  manifest, a changed or malformed signature, a non-canonical signature,
  an older or the same version (no downgrade), a version not the tag's (a
  replay), another title, another file, a size of 0 or over 64 MB,
  manifests signed but malformed (each line's form, order, extra text);
- the placeholder key refuses everything ("no update key in this build");
- the assets' URLs, the redirect allow-list, the VPK's names, param.sfo's
  TITLE_ID.
*/

#include "vita_update_verify.h"
#include "update_check.h"

#include "monocypher-ed25519.h"

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

#define CURRENT "1.1.0-beta.3"
#define TITLE "HCEV00001"
#define SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" \
	"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

#ifndef HALO_TEST_PLACEHOLDER

static unsigned char secret_key[64], public_key[32], other_secret[64], other_public[32];

static void keys(void)
{
	unsigned char seed[32], built[32];
	static const char seed_text[] = HALO_TEST_SEED;

	if (!update_hex_decode(seed_text, sizeof(seed_text) - 1, seed, sizeof(seed)))
	{
		printf("FAIL HALO_TEST_SEED\n");
		exit(1);
	}
	crypto_ed25519_key_pair(secret_key, public_key, seed);
	check(update_public_key(built) && !memcmp(built, public_key, 32), "the build's key is the test seed's");
	memset(seed, 0x5a, sizeof(seed));
	crypto_ed25519_key_pair(other_secret, other_public, seed);
}

/* text's signature by secret, as a signature file (hex and a newline) */
static void sign_text(const unsigned char *secret, const char *text, size_t size, char signature_file[130])
{
	unsigned char signature[64];
	int index;

	crypto_ed25519_sign(signature, secret, (const unsigned char *)text, size);
	for (index = 0; index < 64; index++)
		sprintf(signature_file + index * 2, "%02x", signature[index]);
	signature_file[128] = '\n';
	signature_file[129] = 0;
}

static int check_signed(const char *text, const char *expected, const char *current, struct update_manifest *manifest,
	char *error)
{
	char signature[130];

	sign_text(secret_key, text, strlen(text), signature);
	return update_manifest_check(text, strlen(text), signature, 129, public_key, expected, current, TITLE, manifest,
		error, 200);
}

static void manifest_of(char *text, size_t size, const char *version, const char *title, const char *file,
	const char *bytes)
{
	snprintf(text, size, UPDATE_MANIFEST_MAGIC "\nversion %s\ntitle %s\nfile %s\nsize %s\nsha512 " SHA "\n", version,
		title, file, bytes);
}

static void signatures(void)
{
	char text[2048], signature[130], error[200];
	struct update_manifest manifest;
	int code;

	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "4678468");
	code = check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error);
	check(code == UPDATE_VERIFY_OK, "a signed manifest is taken");
	check(!strcmp(manifest.version, "1.1.0-beta.4") && !strcmp(manifest.title, TITLE) && manifest.size == 4678468 &&
		manifest.sha512[0] == 0x01 && manifest.sha512[63] == 0xef, "its fields");
	/* (the release that ends the betas) */
	manifest_of(text, sizeof(text), "1.1.0", TITLE, "halo.vpk", "1");
	check(check_signed(text, "1.1.0", CURRENT, &manifest, error) == UPDATE_VERIFY_OK, "1.1.0 over a beta");
	/* the signature: another key's, changed, malformed */
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "4678468");
	sign_text(other_secret, text, strlen(text), signature);
	code = update_manifest_check(text, strlen(text), signature, 129, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
		error, sizeof(error));
	check(code == UPDATE_VERIFY_BAD_SIGNATURE && manifest.size == 0, "another key's signature refused");
	code = update_manifest_check(text, strlen(text), signature, 129, other_public, "1.1.0-beta.4", CURRENT, TITLE,
		&manifest, error, sizeof(error));
	check(code == UPDATE_VERIFY_OK, "(it is that key's)");
	sign_text(secret_key, text, strlen(text), signature);
	{
		size_t index;
		int refused = 1;

		/* every byte of the manifest changed in turn */
		for (index = 0; index < strlen(text); index++)
		{
			text[index] ^= 0x01;
			refused &= update_manifest_check(text, strlen(text), signature, 129, public_key, "1.1.0-beta.4", CURRENT, TITLE,
				&manifest, error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE;
			text[index] ^= 0x01;
		}
		check(refused, "a manifest changed in any byte refused");
		refused = 1;
		for (index = 0; index < 128; index++)
		{
			char saved = signature[index];

			signature[index] = signature[index] == '0' ? '1' : '0';
			refused &= update_manifest_check(text, strlen(text), signature, 129, public_key, "1.1.0-beta.4", CURRENT, TITLE,
				&manifest, error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE;
			signature[index] = saved;
		}
		check(refused, "a signature changed in any digit refused");
	}
	check(update_manifest_check(text, strlen(text), signature, 128, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
		error, sizeof(error)) == UPDATE_VERIFY_OK, "a signature without its newline");
	check(update_manifest_check(text, strlen(text), signature, 127, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
		error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE, "a signature cut short refused");
	{
		char longer[140];

		memcpy(longer, signature, 128);
		memcpy(longer + 128, "\n\n", 3);
		check(update_manifest_check(text, strlen(text), longer, 130, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
			error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE, "a signature with more after it refused");
		memcpy(longer + 128, "\r\n", 3);
		check(update_manifest_check(text, strlen(text), longer, 130, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
			error, sizeof(error)) == UPDATE_VERIFY_OK, "a signature ending \\r\\n");
		memcpy(longer, "zz", 2);
		check(update_manifest_check(text, strlen(text), longer, 130, public_key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
			error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE, "a signature not hexadecimal refused");
	}
	/* a non-canonical signature (S + L, the same signature to a lax
	checker): refused */
	{
		static const unsigned char order[32] =
		{
			0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
		};
		unsigned char raw[64];
		char hex[130];
		int index, carry = 0;

		crypto_ed25519_sign(raw, secret_key, (const unsigned char *)text, strlen(text));
		for (index = 0; index < 32; index++)
		{
			int sum = raw[32 + index] + order[index] + carry;

			raw[32 + index] = (unsigned char)sum;
			carry = sum >> 8;
		}
		for (index = 0; index < 64; index++)
			sprintf(hex + index * 2, "%02x", raw[index]);
		check(!carry && update_manifest_check(text, strlen(text), hex, 128, public_key, "1.1.0-beta.4", CURRENT, TITLE,
			&manifest, error, sizeof(error)) == UPDATE_VERIFY_BAD_SIGNATURE, "a non-canonical signature refused");
	}
	/* the key: none (zeros) */
	{
		unsigned char zeros[32] = { 0 };

		check(update_manifest_check(text, strlen(text), signature, 129, zeros, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
			error, sizeof(error)) == UPDATE_VERIFY_NO_KEY && strstr(error, "no update key"), "a zero key refuses");
	}
}

static void versions(void)
{
	char text[2048], error[200];
	struct update_manifest manifest;

	manifest_of(text, sizeof(text), "1.1.0-beta.2", TITLE, "halo.vpk", "100");
	check(check_signed(text, "1.1.0-beta.2", CURRENT, &manifest, error) == UPDATE_VERIFY_NOT_NEWER,
		"an older version refused (no downgrade)");
	manifest_of(text, sizeof(text), CURRENT, TITLE, "halo.vpk", "100");
	check(check_signed(text, CURRENT, CURRENT, &manifest, error) == UPDATE_VERIFY_NOT_NEWER,
		"the same version refused (no reinstall)");
	manifest_of(text, sizeof(text), "1.0.3", TITLE, "halo.vpk", "100");
	check(check_signed(text, "1.0.3", CURRENT, &manifest, error) == UPDATE_VERIFY_NOT_NEWER, "1.0.3 under a beta of 1.1.0");
	/* a replay: beta.4's signed manifest under the tag v1.1.0-beta.5 */
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "100");
	check(check_signed(text, "1.1.0-beta.5", CURRENT, &manifest, error) == UPDATE_VERIFY_WRONG_VERSION &&
		strstr(error, "not 1.1.0-beta.5"), "a manifest under another tag refused");
	check(check_signed(text, NULL, CURRENT, &manifest, error) == UPDATE_VERIFY_WRONG_VERSION, "no tag refused");
	check(check_signed(text, "1.1.0-beta.4", "nonsense", &manifest, error) == UPDATE_VERIFY_NOT_NEWER,
		"a running build without a version takes nothing");
}

static void fields(void)
{
	char text[2048], error[200];
	struct update_manifest manifest;

	manifest_of(text, sizeof(text), "1.1.0-beta.4", "HCEV00002", "halo.vpk", "100");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_WRONG_TITLE,
		"another title refused");
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "eboot.bin", "100");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_WRONG_FILE,
		"another file refused");
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "67108864");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_OK, "64 MB taken");
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "67108865");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_TOO_LARGE &&
		manifest.size == 0, "over 64 MB refused");
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "999999999");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_TOO_LARGE, "999999999 refused");
	manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "0");
	check(check_signed(text, "1.1.0-beta.4", CURRENT, &manifest, error) == UPDATE_VERIFY_TOO_LARGE, "0 bytes refused");
}

/* manifests signed but not in the form: refused as malformed */
static void malformed(void)
{
	static const char *const texts[] =
	{
		"",
		UPDATE_MANIFEST_MAGIC "\n",
		"halo-ce-vita-update 2\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA,
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\nx 1\n",
		UPDATE_MANIFEST_MAGIC "\r\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\ntitle HCEV00001\nversion 1.1.0-beta.4\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-rc.1\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion v1.1.0\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4 \ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion  1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle hcev00001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV0001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV000001\nfile halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile ../halo.vpk\nsize 100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 0100\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize -1\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 1234567890\nsha512 " SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 " SHA "0\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha512 ABCDEF" SHA "\n",
		UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 100\nsha256 " SHA "\n",
	};
	char error[200];
	struct update_manifest manifest;
	size_t index;

	for (index = 0; index < sizeof(texts) / sizeof(*texts); index++)
	{
		char what[64], signature[130];
		int code;

		sign_text(secret_key, texts[index], strlen(texts[index]), signature);
		code = update_manifest_check(texts[index], strlen(texts[index]), signature, 129, public_key, "1.1.0-beta.4",
			CURRENT, TITLE, &manifest, error, sizeof(error));
		snprintf(what, sizeof(what), "malformed manifest %zu refused", index);
		check(code == UPDATE_VERIFY_MALFORMED, what);
	}
	/* (signed, but over the size the game reads) */
	{
		char text[1100], signature[130];

		manifest_of(text, sizeof(text), "1.1.0-beta.4", TITLE, "halo.vpk", "100");
		memset(text + strlen(text), '#', sizeof(text) - strlen(text) - 1);
		text[sizeof(text) - 1] = 0;
		sign_text(secret_key, text, strlen(text), signature);
		check(update_manifest_check(text, strlen(text), signature, 129, public_key, "1.1.0-beta.4", CURRENT, TITLE,
			&manifest, error, sizeof(error)) == UPDATE_VERIFY_MALFORMED, "a manifest over 1 KB refused");
	}
}

#endif

static void urls(void)
{
	char url[600];

	check(update_asset_url(url, sizeof(url), UPDATE_DOWNLOAD_BASE, "1.1.0-beta.4", "halo.vpk") &&
		!strcmp(url, "https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v1.1.0-beta.4/halo.vpk"),
		"the VPK's URL");
	check(update_asset_url(url, sizeof(url), UPDATE_DOWNLOAD_BASE, "1.1.0", "halo-update.txt.sig") &&
		!strcmp(url, "https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v1.1.0/halo-update.txt.sig"),
		"the signature's URL");
	check(update_asset_url(url, sizeof(url), "http://127.0.0.1:8080/dl", "1.1.0", "halo-update.txt") &&
		!strcmp(url, "http://127.0.0.1:8080/dl/v1.1.0/halo-update.txt"), "the tests' server");
	check(!update_asset_url(url, sizeof(url), UPDATE_DOWNLOAD_BASE, "1.1.0-rc.1", "halo.vpk"), "not a tag: no URL");
	check(!update_asset_url(url, sizeof(url), UPDATE_DOWNLOAD_BASE, "1.1.0/../../x", "halo.vpk"), "a path: no URL");
	check(!update_asset_url(url, sizeof(url), UPDATE_DOWNLOAD_BASE, "1.1.0", "eboot.bin"), "another asset: no URL");
	check(!update_asset_url(url, sizeof(url), "https://evil.example/dl", "1.1.0", "halo.vpk"), "another host: no URL");
	check(update_asset_url(url, sizeof(url), "https://github.com/someone/halo-update-test/releases/download", "1.1.0",
		"halo.vpk") && !strcmp(url, "https://github.com/someone/halo-update-test/releases/download/v1.1.0/halo.vpk"),
		"another repository's releases on GitHub (a test build's)");
	{
		static const char *const bases[] =
		{
			"https://github.com/someone/releases/download", "https://github.com/../x/releases/download",
			"https://github.com/someone/../releases/download", "https://github.com/some.one/x/releases/download",
			"https://github.com/someone/x/releases/download/", "https://github.com/someone/x/releases",
			"https://github.com.evil.example/someone/x/releases/download", "http://github.com/someone/x/releases/download",
			"https://github.com/someone/x y/releases/download", "https://github.com//x/releases/download",
			"https://github.com/someone/x/releases/download?y",
		};
		size_t index;
		int refused = 1;

		for (index = 0; index < sizeof(bases) / sizeof(*bases); index++)
			if (update_asset_url(url, sizeof(url), bases[index], "1.1.0", "halo.vpk"))
			{
				printf("FAIL base taken: %s\n", bases[index]);
				refused = 0;
			}
		check(refused, "GitHub look-alike bases refused");
	}
	check(!update_asset_url(url, sizeof(url), "http://127.0.0.1.evil.example/dl", "1.1.0", "halo.vpk"),
		"a loopback look-alike: no URL");
	check(!update_asset_url(url, sizeof(url), "http://127.0.0.1:0/dl", "1.1.0", "halo.vpk"), "port 0: no URL");
	check(!update_asset_url(url, 20, UPDATE_DOWNLOAD_BASE, "1.1.0", "halo.vpk") && !url[0], "too long: no URL");
}

static void redirects(void)
{
	static const char *const from = "https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v1.1.0/halo.vpk";
	static const char *const allowed[] =
	{
		"https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=a%2Bb&jwt=x.y.z",
		"https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/2?X-Amz-Signature=ab",
	};
	static const char *const refused[] =
	{
		"http://release-assets.githubusercontent.com/x",
		"https://release-assets.githubusercontent.com.evil.example/x",
		"https://release-assets.githubusercontent.com@evil.example/x",
		"https://evil.example/release-assets.githubusercontent.com/x",
		"https://release-assets.githubusercontent.com:443/x",
		"https://release-assets.githubusercontent.com:8443/x",
		"https://RELEASE-ASSETS.githubusercontent.com/x",
		"https://release-assets.githubusercontent.com",
		"https://release-assets.githubusercontent.com?x",
		"https://release-assets.githubusercontent.com/x y",
		"https://release-assets.githubusercontent.com/x\r\nHost: evil",
		"https://githubusercontent.com/x",
		"https://raw.githubusercontent.com/x",
		"https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v1.1.0/halo.vpk",
		"https://api.github.com/x",
		"http://127.0.0.1/x",
		"file:///ux0:/app",
		"",
	};
	size_t index;

	for (index = 0; index < sizeof(allowed) / sizeof(*allowed); index++)
		check(update_redirect_allowed(from, allowed[index]), allowed[index]);
	for (index = 0; index < sizeof(refused) / sizeof(*refused); index++)
	{
		char what[160];

		snprintf(what, sizeof(what), "redirect refused: %.120s", refused[index]);
		check(!update_redirect_allowed(from, refused[index]), what);
	}
	check(update_redirect_allowed("http://127.0.0.1:5000/redirect/x", "http://127.0.0.1:5000/dl/x"), "loopback to loopback");
	check(!update_redirect_allowed("http://127.0.0.1:5000/x", "http://10.0.0.1/x"), "loopback to elsewhere refused");
	check(!update_redirect_allowed("http://127.0.0.1:5000/x", "http://127.0.0.1:99999/x"), "a bad port refused");
	check(!update_redirect_allowed("http://127.0.0.1:5000/x", "http://127.1/x"), "a short loopback refused");
	check(!update_redirect_allowed(NULL, allowed[0]) && !update_redirect_allowed(from, NULL), "NULLs refused");
}

static void names(void)
{
	static const char *const good[] =
	{
		"eboot.bin", "sce_sys/param.sfo", "sce_sys/livearea/contents/template.xml", "lang/es.txt", "a/b/c/d/e/f/g/h",
		"shaders.pak", "x..y", ".hidden",
	};
	static const char *const bad[] =
	{
		"", "/eboot.bin", "../eboot.bin", "a/../../b", "a/./b", ".", "..", "a//b", "a/", "a\\b", "C:x", "ux0:app/x",
		"a b", "a/b/c/d/e/f/g/h/i", "\xc3\xa9", "a\tb",
	};
	size_t index;
	char longer[260];

	for (index = 0; index < sizeof(good) / sizeof(*good); index++)
		check(update_entry_name_safe(good[index], strlen(good[index])), good[index]);
	for (index = 0; index < sizeof(bad) / sizeof(*bad); index++)
	{
		char what[80];

		snprintf(what, sizeof(what), "name refused: %s", bad[index]);
		check(!update_entry_name_safe(bad[index], strlen(bad[index])), what);
	}
	check(!update_entry_name_safe("eboot.bin\0x", 11), "a NUL in a name refused");
	memset(longer, 'a', sizeof(longer));
	check(update_entry_name_safe(longer, 200) && !update_entry_name_safe(longer, 201), "200 bytes at most");
}

/* a param.sfo of (key, value) strings, as vita-mksfoex lays one out */
static size_t sfo_make(unsigned char *sfo, const char *const *keys, const char *const *values, int count)
{
	size_t key_table = 20 + (size_t)count * 16, key_size = 0, data_size = 0, data_table;
	int index;

	for (index = 0; index < count; index++)
		key_size += strlen(keys[index]) + 1;
	data_table = key_table + ((key_size + 3) & ~(size_t)3);
	memset(sfo, 0, 4096);
	memcpy(sfo, "\0PSF\x01\x01\0\0", 8);
	sfo[8] = (unsigned char)key_table;
	sfo[9] = (unsigned char)(key_table >> 8);
	sfo[12] = (unsigned char)data_table;
	sfo[13] = (unsigned char)(data_table >> 8);
	sfo[16] = (unsigned char)count;
	key_size = 0;
	for (index = 0; index < count; index++)
	{
		unsigned char *entry = sfo + 20 + index * 16;
		size_t length = strlen(values[index]) + 1;

		entry[0] = (unsigned char)key_size;
		entry[2] = 4;
		entry[3] = 2;
		entry[4] = (unsigned char)length;
		entry[8] = (unsigned char)((length + 3) & ~3u);
		entry[12] = (unsigned char)data_size;
		entry[13] = (unsigned char)(data_size >> 8);
		strcpy((char *)sfo + key_table + key_size, keys[index]);
		strcpy((char *)sfo + data_table + data_size, values[index]);
		key_size += strlen(keys[index]) + 1;
		data_size += (length + 3) & ~(size_t)3;
	}
	return data_table + data_size;
}

static void sfo(void)
{
	static const char *const keys[] = { "APP_VER", "TITLE", "TITLE_ID" };
	static const char *const values[] = { "01.10", "Halo CE", "HCEV00001" };
	static const char *const lower[] = { "01.10", "Halo CE", "hcev00001" };
	unsigned char data[4096];
	char title[16];
	size_t size = sfo_make(data, keys, values, 3), cut;
	int refused = 1;

	check(update_sfo_title(data, size, title, sizeof(title)) && !strcmp(title, "HCEV00001"), "param.sfo's TITLE_ID");
	check(!update_sfo_title(data, size, title, 9), "a title buffer too small refused");
	for (cut = 0; cut < size; cut++)
		refused &= !update_sfo_title(data, cut, title, sizeof(title)) || cut >= size - 4;
	check(refused, "a param.sfo cut short refused");
	size = sfo_make(data, keys, lower, 3);
	check(!update_sfo_title(data, size, title, sizeof(title)), "a lowercase title refused");
	size = sfo_make(data, keys, values, 2);
	check(!update_sfo_title(data, size, title, sizeof(title)), "no TITLE_ID");
	size = sfo_make(data, keys, values, 3);
	data[20 + 2 * 16 + 12] = 0xff;
	data[20 + 2 * 16 + 15] = 0xff;
	check(!update_sfo_title(data, size, title, sizeof(title)), "a value offset past the end refused");
	size = sfo_make(data, keys, values, 3);
	data[16] = 0xff;
	data[17] = 0xff;
	check(!update_sfo_title(data, size, title, sizeof(title)), "a huge count refused");
}

int main(void)
{
	unsigned char key[32];

#ifdef HALO_TEST_PLACEHOLDER
	{
		char error[200];
		struct update_manifest manifest;
		static const char text[] = UPDATE_MANIFEST_MAGIC "\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 1\n"
			"sha512 " SHA "\n";
		char signature[130];

		memset(signature, 'a', 128);
		signature[128] = 0;
		check(!update_public_key(key), "the placeholder is no key");
		check(update_manifest_check(text, strlen(text), signature, 128, key, "1.1.0-beta.4", CURRENT, TITLE, &manifest,
			error, sizeof(error)) == UPDATE_VERIFY_NO_KEY && !strcmp(error, "no update key in this build"),
			"the placeholder build refuses: no update key in this build");
	}
#else
	keys();
	check(update_public_key(key), "a key in this build");
	signatures();
	versions();
	fields();
	malformed();
#endif
	urls();
	redirects();
	names();
	sfo();
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
