/*
UPDATE_CHECK_FUZZ.C

A fuzz target for what the update check reads from the network and its
cache file (port/linux/src/update_check.c): the input's first byte picks
the channel, the rest is given, in a buffer exactly its size (AddressSanitizer
catches a byte read past it), to the releases' JSON reader, the cache file's
reader, and - NUL terminated, when it is short - the version and tag
readers. Every input must only be taken or refused: a version taken must be
a valid tag's, and the cache must read back what it wrote.

run_update_check_test.sh builds it with AddressSanitizer and UBSan, with
libFuzzer (UPDATE_FUZZ_SECONDS) and without it, when main below runs the
cases in update_check_cases/ and made-up ones (malformed JSON, huge
numbers, long strings, bad tags, deep nesting, truncations), then
UPDATE_FUZZ_ITERATIONS changes of them (bytes flipped, set to edge values,
cut, repeated, spliced), from a fixed seed.
*/

#include "update_check.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fuzz_abort(const char *what)
{
	fprintf(stderr, "update_check_fuzz: %s\n", what);
	abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char latest[UPDATE_VERSION_SIZE];
	struct update_cache cache;
	char *copy;
	int channel, result;

	if (!size)
		return 0;
	channel = data[0] & 1 ? UPDATE_CHANNEL_STABLE : UPDATE_CHANNEL_EXPERIMENTAL;
	data++;
	size--;
	copy = malloc(size ? size : 1);
	memcpy(copy, data, size);
	result = update_releases_pick(copy, size, channel, latest, sizeof(latest));
	if (result < -1 || result > 1)
		fuzz_abort("pick answered neither -1, 0 nor 1");
	if (result == 1)
	{
		char tag[UPDATE_VERSION_SIZE + 1];
		struct update_version version;

		snprintf(tag, sizeof(tag), "v%s", latest);
		if (!update_tag_valid(tag) || !update_version_parse(latest, &version))
			fuzz_abort("a version taken that is not a valid tag's");
		if (channel == UPDATE_CHANNEL_STABLE && version.prerelease[0])
			fuzz_abort("Stable took a pre-release");
	}
	else if (latest[0])
		fuzz_abort("latest written without a release taken");
	if (update_cache_parse(copy, size, &cache))
	{
		char text[256];
		struct update_cache again;
		int length = update_cache_format(text, sizeof(text), &cache);

		if (length <= 0 || !update_cache_parse(text, (size_t)length, &again) || again.checked != cache.checked ||
			again.channel != cache.channel || again.ok != cache.ok || strcmp(again.latest, cache.latest))
		{
			fuzz_abort("the cache does not read back what it wrote");
		}
	}
	free(copy);
	if (size < 200)
	{
		char text[200];
		struct update_version version;

		memcpy(text, data, size);
		text[size] = 0;
		if (update_version_parse(text, &version))
		{
			struct update_version same;

			if (update_version_compare(&version, &version) != 0)
				fuzz_abort("a version not equal to itself");
			if (update_version_parse("1.1.0-beta.3", &same) &&
				update_version_compare(&version, &same) != -update_version_compare(&same, &version))
				fuzz_abort("comparison not antisymmetric");
		}
		if (update_tag_valid(text) && !update_version_parse(text, &version))
			fuzz_abort("a valid tag that is not a version");
		update_is_newer(text, "1.1.0-beta.3");
		update_channel_parse(text, 0);
	}
	return 0;
}

#ifndef UPDATE_FUZZ_LIBFUZZER

static unsigned int fuzz_seed = 12345;

static unsigned int fuzz_random(void)
{
	fuzz_seed = fuzz_seed * 1103515245u + 12345u;
	return fuzz_seed >> 8;
}

#define CASES_MAXIMUM 96
static unsigned char *cases[CASES_MAXIMUM];
static size_t case_sizes[CASES_MAXIMUM];
static int case_count;

static void case_add(const void *data, size_t size)
{
	if (case_count >= CASES_MAXIMUM)
		return;
	cases[case_count] = malloc(size + 1);
	memcpy(cases[case_count], data, size);
	case_sizes[case_count++] = size;
}

/* a case with the channel byte first (both channels: each is run with the
first byte as given and flipped) */
static void case_text(const char *text)
{
	size_t size = strlen(text);
	unsigned char *data = malloc(size + 1);

	data[0] = 0;
	memcpy(data + 1, text, size);
	case_add(data, size + 1);
	free(data);
}

static void cases_load(const char *folder)
{
	DIR *directory = opendir(folder);
	struct dirent *entry;

	if (!directory)
		return;
	while ((entry = readdir(directory)) != NULL)
	{
		char path[1024];
		FILE *file;
		long size;
		unsigned char *data;

		if (entry->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		file = fopen(path, "rb");
		if (!file)
			continue;
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		fseek(file, 0, SEEK_SET);
		data = malloc((size_t)size + 1);
		data[0] = 0;
		if (size >= 0 && fread(data + 1, 1, (size_t)size, file) == (size_t)size)
			case_add(data, (size_t)size + 1);
		free(data);
		fclose(file);
	}
	closedir(directory);
}

static void made_up_cases(void)
{
	char *text;
	size_t index;

	case_text("[{\"tag_name\":\"v1.1.0-beta.4\",\"prerelease\":true,\"draft\":false},{\"tag_name\":\"v1.0.3\"}]");
	case_text("{\"tag_name\":\"v1.1.0\",\"prerelease\":false,\"draft\":false}");
	case_text("checked=1791554278\nchannel=experimental\nresult=ok\nlatest=1.1.0-beta.4\n");
	case_text("checked=1\nchannel=stable\nresult=failed\nlatest=\n");
	case_text("[{\"tag_name\":\"v999999999999999999.0.0\"},{\"id\":1e999999999},{\"n\":-0.0000000000000000001}]");
	case_text("[{\"tag_name\":\"v1.1.0-beta.1000\"},{\"tag_name\":\"v1.0\"},{\"tag_name\":\"1.0.0\"},{\"tag_name\":\"v1.0.0 \"}]");
	case_text("[{\"tag_name\":\"\\u0076\\u0031.0.0\"},{\"tag_name\":\"v1.0.0\\ud800\"},{\"tag_name\":\"v1.0.0\\\"\"}]");
	case_text("[{\"draft\":true,\"tag_name\":\"v9.9.9\"},{\"prerelease\":null,\"tag_name\":\"v8.0.0\"}]");
	case_text("1.1.0-beta.3");
	case_text("v1.1.0-beta.3");
	case_text("1.1.0+build.1.2");
	/* (deep nesting, both kinds) */
	text = malloc(4096);
	for (index = 0; index < 2000; index++)
		text[index] = index % 2 ? '[' : '{';
	text[2000] = 0;
	case_text(text);
	for (index = 0; index < 64; index++)
		text[index] = '[';
	for (index = 64; index < 128; index++)
		text[index] = ']';
	text[128] = 0;
	case_text(text);
	free(text);
	/* (a long string and a long number) */
	text = malloc(70000);
	strcpy(text, "[{\"body\":\"");
	memset(text + 10, 'x', 65000);
	strcpy(text + 65010, "\",\"tag_name\":\"v2.0.0\"}]");
	case_text(text);
	strcpy(text, "[");
	memset(text + 1, '9', 60000);
	strcpy(text + 60001, "]");
	case_text(text);
	free(text);
}

static void run_case(const unsigned char *data, size_t size)
{
	unsigned char *copy = malloc(size ? size : 1);

	memcpy(copy, data, size);
	LLVMFuzzerTestOneInput(copy, size);
	if (size)
	{
		copy[0] ^= 1;
		LLVMFuzzerTestOneInput(copy, size);
	}
	free(copy);
}

int main(int argc, char **argv)
{
	const char *iterations_text = getenv("UPDATE_FUZZ_ITERATIONS");
	long iterations = iterations_text ? atol(iterations_text) : 30000, iteration;
	unsigned char *work = malloc(512 * 1024);
	int index;

	made_up_cases();
	for (index = 1; index < argc; index++)
		cases_load(argv[index]);
	printf("update_check_fuzz: %d cases\n", case_count);
	for (index = 0; index < case_count; index++)
	{
		size_t cut;

		run_case(cases[index], case_sizes[index]);
		/* (each case cut at every length, up to 4 KB, then every 97th) */
		for (cut = 0; cut < case_sizes[index]; cut += cut < 4096 ? 1 : 97)
			run_case(cases[index], cut);
	}
	for (iteration = 0; iteration < iterations; iteration++)
	{
		int which = (int)(fuzz_random() % (unsigned int)case_count);
		size_t size = case_sizes[which], changes = 1 + fuzz_random() % 8, change;

		if (size > 256 * 1024)
			size = 256 * 1024;
		memcpy(work, cases[which], size);
		for (change = 0; change < changes && size; change++)
		{
			size_t at = fuzz_random() % size;

			switch (fuzz_random() % 7)
			{
			case 0: work[at] ^= (unsigned char)(1u << (fuzz_random() % 8)); break;
			case 1: work[at] = (unsigned char)"\"{}[],:\\0x\xff-.e9"[fuzz_random() % 16]; break;
			case 2: size = at; break;
			case 3:
			{
				/* (a piece repeated) */
				size_t length = 1 + fuzz_random() % 64;

				if (at + length <= size && size + length <= 512 * 1024)
				{
					memmove(work + at + length, work + at, size - at);
					size += length;
				}
				break;
			}
			case 4:
			{
				/* (a piece of another case spliced in) */
				int other = (int)(fuzz_random() % (unsigned int)case_count);
				size_t from = case_sizes[other] ? fuzz_random() % case_sizes[other] : 0;
				size_t length = 1 + fuzz_random() % 128;

				if (from + length <= case_sizes[other] && at + length <= size)
					memcpy(work + at, cases[other] + from, length);
				break;
			}
			case 5: work[at] = (unsigned char)fuzz_random(); break;
			default:
				/* (a piece taken out) */
				if (size > at + 1)
				{
					size_t length = 1 + fuzz_random() % (size - at - 1);

					memmove(work + at, work + at + length, size - at - length);
					size -= length;
				}
				break;
			}
		}
		run_case(work, size);
	}
	printf("update_check_fuzz: PASS, %ld changed inputs\n", iterations);
	for (index = 0; index < case_count; index++)
		free(cases[index]);
	free(work);
	return 0;
}

#endif
