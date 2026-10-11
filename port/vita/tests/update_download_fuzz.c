/*
UPDATE_DOWNLOAD_FUZZ.C

Arbitrary bytes at the Vita updater's readers (port/vita/host/
vita_update_verify.c), under AddressSanitizer and UBSan
(run_update_download_test.sh): the manifest's parser, the whole check of a
manifest with its signature, the signature file's parser, the assets' URLs
and the redirect allow-list. Built with the
tests' key (HALO_UPDATE_PUBLIC_KEY, HALO_TEST_SEED):

- every truncation of a signed manifest and its signature file, then
  UPDATE_FUZZ_ITERATIONS (default 30000) random changes to either from a
  fixed seed: nothing but the original pair is ever taken;
- the same number of random inputs to each reader;
- with UPDATE_FUZZ_LIBFUZZER, libFuzzer's inputs (split at the first NUL
  into manifest and signature, the rest to the others).
*/

#include "vita_update_verify.h"
#include "update_check.h"

#include "monocypher-ed25519.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char secret_key[64], public_key[32];

static void keys(void)
{
	static const char seed_text[] = HALO_TEST_SEED;
	unsigned char seed[32];

	if (!update_hex_decode(seed_text, sizeof(seed_text) - 1, seed, sizeof(seed)))
		exit(2);
	crypto_ed25519_key_pair(secret_key, public_key, seed);
}

/* every reader at once on data (size bytes) */
static int one_input(const unsigned char *data, size_t size)
{
	const unsigned char *zero = memchr(data, 0, size);
	size_t manifest_size = zero ? (size_t)(zero - data) : size;
	struct update_manifest manifest;
	unsigned char signature[64];
	char error[200], text[4096];
	int accepted;

	update_manifest_parse((const char *)data, size, &manifest);
	update_signature_parse((const char *)data, size, signature);
	accepted = update_manifest_check((const char *)data, manifest_size,
		zero ? (const char *)zero + 1 : "", zero ? size - manifest_size - 1 : 0, public_key, "1.1.0-beta.4",
		"1.1.0-beta.3", "HCEV00001", &manifest, error, sizeof(error)) == UPDATE_VERIFY_OK;
	/* (the URL readers take strings) */
	if (size < sizeof(text))
	{
		memcpy(text, data, size);
		text[size] = 0;
		update_redirect_allowed("http://127.0.0.1:1/x", text);
		update_redirect_allowed(text, text);
		update_asset_url(error, sizeof(error), text, "1.1.0", UPDATE_ASSET_VPK);
		update_asset_url(error, sizeof(error), UPDATE_DOWNLOAD_BASE, text, UPDATE_ASSET_VPK);
	}
	return accepted;
}

#ifdef UPDATE_FUZZ_LIBFUZZER

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static int ready;

	if (!ready)
	{
		keys();
		ready = 1;
	}
	/* (a random input is never signed by the key) */
	if (one_input(data, size))
		abort();
	return 0;
}

#else

static unsigned long long random_state = 0x9E3779B97F4A7C15ULL;

static unsigned long next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned long)(random_state >> 11);
}

int main(void)
{
	char manifest[512], signature_file[130];
	unsigned char signature[64], buffer[2048];
	struct update_manifest parsed;
	char error[200];
	size_t manifest_size, cut, index;
	long iterations = getenv("UPDATE_FUZZ_ITERATIONS") ? atol(getenv("UPDATE_FUZZ_ITERATIONS")) : 30000, iteration;
	int failures = 0;

	keys();
	manifest_size = (size_t)snprintf(manifest, sizeof(manifest), UPDATE_MANIFEST_MAGIC
		"\nversion 1.1.0-beta.4\ntitle HCEV00001\nfile halo.vpk\nsize 4678468\nsha512 %0128d\n", 7);
	crypto_ed25519_sign(signature, secret_key, (const unsigned char *)manifest, manifest_size);
	for (index = 0; index < 64; index++)
		sprintf(signature_file + index * 2, "%02x", signature[index]);
	signature_file[128] = '\n';
	signature_file[129] = 0;
	if (update_manifest_check(manifest, manifest_size, signature_file, 129, public_key, "1.1.0-beta.4", "1.1.0-beta.3",
		"HCEV00001", &parsed, error, sizeof(error)) != UPDATE_VERIFY_OK)
	{
		printf("FAIL the original pair is refused: %s\n", error);
		return 1;
	}
	/* every truncation of either */
	for (cut = 0; cut < manifest_size; cut++)
		if (update_manifest_check(manifest, cut, signature_file, 129, public_key, "1.1.0-beta.4", "1.1.0-beta.3",
			"HCEV00001", &parsed, error, sizeof(error)) == UPDATE_VERIFY_OK)
		{
			printf("FAIL a manifest cut to %zu bytes taken\n", cut);
			failures++;
		}
	for (cut = 0; cut < 128; cut++)
		if (update_manifest_check(manifest, manifest_size, signature_file, cut, public_key, "1.1.0-beta.4",
			"1.1.0-beta.3", "HCEV00001", &parsed, error, sizeof(error)) == UPDATE_VERIFY_OK)
		{
			printf("FAIL a signature cut to %zu taken\n", cut);
			failures++;
		}
	/* random changes to the pair */
	for (iteration = 0; iteration < iterations; iteration++)
	{
		char changed[sizeof(manifest)], changed_signature[sizeof(signature_file)];
		size_t size = manifest_size, signature_size = 129;
		int changes = 1 + (int)(next_random() % 4), change;

		memcpy(changed, manifest, sizeof(manifest));
		memcpy(changed_signature, signature_file, sizeof(signature_file));
		for (change = 0; change < changes; change++)
		{
			switch (next_random() % 5)
			{
			case 0:
				changed[next_random() % size] ^= (char)(1 << (next_random() % 8));
				break;
			case 1:
				changed[next_random() % size] = (char)next_random();
				break;
			case 2:
				size = 1 + next_random() % size;
				break;
			case 3:
				changed_signature[next_random() % 128] = "0123456789abcdefXZ"[next_random() % 18];
				break;
			default:
				signature_size = next_random() % 131;
				break;
			}
		}
		if (update_manifest_check(changed, size, changed_signature, signature_size, public_key, "1.1.0-beta.4",
			"1.1.0-beta.3", "HCEV00001", &parsed, error, sizeof(error)) == UPDATE_VERIFY_OK &&
			(size != manifest_size || memcmp(changed, manifest, size) ||
				memcmp(changed_signature, signature_file, 128)))
		{
			printf("FAIL a changed pair taken (iteration %ld)\n", iteration);
			failures++;
		}
	}
	/* random inputs to every reader */
	for (iteration = 0; iteration < iterations; iteration++)
	{
		size_t size = next_random() % sizeof(buffer);

		for (index = 0; index < size; index++)
			buffer[index] = iteration & 1 ? (unsigned char)next_random() :
				(unsigned char)" \n.0123456789abcdefv/:-PSF"[next_random() % 26];
		if (one_input(buffer, size))
		{
			printf("FAIL a random input taken\n");
			failures++;
		}
	}
	printf("%ld changes and %ld random inputs, %d failed\n", iterations, iterations, failures);
	return failures ? 1 : 0;
}

#endif
