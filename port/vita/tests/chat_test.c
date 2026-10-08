/*
CHAT_TEST.C

Desktop test of game chat's rules (port/linux/game/chat_protocol.c): the
phrases, what is kept of typed text (printable ASCII, the game's text codes
and control characters out, the length cap) and of names, the links
refused (OpenCE PR #72's cases and more), a joiner's request and a host's
relay as every field from the wire could be, and the flood limits.
run_chat_test.sh builds it 32-bit, with AddressSanitizer and UBSan.
*/

#include "chat_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition) do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static int cleaned(char const *source, char const *expected)
{
	char text[CHAT_TEXT_BYTES];

	chat_text_clean(text, sizeof(text), source, (int)strlen(source) + 1);
	if (strcmp(text, expected))
	{
		printf("  cleaned \"%s\" -> \"%s\" (wanted \"%s\")\n", source, text, expected);
		return 0;
	}
	return 1;
}

static void test_phrases(void)
{
	int phrase;

	CHECK(chat_phrase_count() == CHAT_PHRASE_COUNT);
	for (phrase = 0; phrase < CHAT_PHRASE_COUNT; phrase++)
	{
		char text[CHAT_TEXT_BYTES];
		char const *phrase_text = chat_phrase_text(phrase);

		CHECK(phrase_text && phrase_text[0]);
		/* (the phrases keep to the rules typed text does) */
		CHECK(phrase_text && cleaned(phrase_text, phrase_text) && !chat_text_has_link(phrase_text));
		CHECK(phrase_text && chat_text_clean(text, sizeof(text), phrase_text, 64) == (int)strlen(phrase_text));
	}
	CHECK(!strcmp(chat_phrase_text(0), "Need backup"));
	CHECK(!strcmp(chat_phrase_text(1), "Enemy spotted"));
	CHECK(!strcmp(chat_phrase_text(5), "Good game"));
	CHECK(chat_phrase_text(-1) == NULL);
	CHECK(chat_phrase_text(CHAT_PHRASE_COUNT) == NULL);
	CHECK(chat_phrase_text(32767) == NULL);
}

static void test_clean(void)
{
	char text[CHAT_TEXT_BYTES];
	char long_text[400];
	char small[8];

	CHECK(cleaned("hello", "hello"));
	CHECK(cleaned("   hello   there  ", "hello there"));
	CHECK(cleaned("a\tb\r\nc\x1b[31md", "abc[31md"));
	CHECK(cleaned("|nnew line|c", "/nnew line/c"));
	CHECK(cleaned("ends with |", "ends with /"));
	CHECK(cleaned("caf\xc3\xa9 \x7f\x80\xff!", "caf !"));
	CHECK(cleaned("100% %s %n %x", "100% %s %n %x"));
	CHECK(cleaned("", ""));
	CHECK(cleaned("   ", ""));
	CHECK(cleaned("\x01\x02\x03", ""));
	/* (the cap: 80 characters, whatever the source's length) */
	memset(long_text, 'x', sizeof(long_text) - 1);
	long_text[sizeof(long_text) - 1] = 0;
	CHECK(chat_text_clean(text, sizeof(text), long_text, sizeof(long_text)) == CHAT_MAXIMUM_TEXT_LENGTH);
	CHECK(strlen(text) == CHAT_MAXIMUM_TEXT_LENGTH);
	/* (a source with no end, read no further than its size) */
	memset(long_text, 'y', sizeof(long_text));
	CHECK(chat_text_clean(text, sizeof(text), long_text, 10) == 10);
	/* (a small destination) */
	CHECK(chat_text_clean(small, sizeof(small), "abcdefghijk", 12) == 7 && !strcmp(small, "abcdefg"));
	CHECK(chat_text_clean(small, 1, "abc", 4) == 0 && small[0] == 0);
	CHECK(chat_text_clean(small, 0, "abc", 4) == 0);
	CHECK(chat_text_clean(text, sizeof(text), NULL, 10) == 0 && text[0] == 0);
}

static void test_links(void)
{
	static char const *const links[] =
	{
		"go to example.com now", "EXAMPLE.COM", "www.something", "http://x", "https x", "ftp://host",
		"join discord.gg/abc", "free stuff at x (.) com", "x [dot] net", "x dot org", "x(dot)io",
		"my ip 192.168.1.20", "10.0.0.1:2302", "1.2.3.4", "bit.ly/zzz", "a-b.co", "site.xyz", "x.tk",
		"evil  .  com", "evil{.}ru",
	};
	static char const *const words[] =
	{
		"good game", "need backup", "i.e. go left", "3.5 seconds", "v1.0.3 is out", "nice shot.", "ok...",
		"is so", "Mr. Chief", "1.2.3", "dot", "e.g", "the end.", "score 25.50",
	};
	int index;

	for (index = 0; index < (int)(sizeof(links) / sizeof(links[0])); index++)
	{
		char text[CHAT_TEXT_BYTES];

		chat_text_clean(text, sizeof(text), links[index], 200);
		if (!chat_text_has_link(text))
			printf("  not a link: \"%s\"\n", links[index]);
		CHECK(chat_text_has_link(text));
	}
	for (index = 0; index < (int)(sizeof(words) / sizeof(words[0])); index++)
	{
		if (chat_text_has_link(words[index]))
			printf("  a link: \"%s\"\n", words[index]);
		CHECK(!chat_text_has_link(words[index]));
	}
}

static void test_names(void)
{
	static uint16_t const plain[CHAT_NAME_CHARACTERS] = { 'C', 'h', 'i', 'e', 'f', 0 };
	static uint16_t const full[CHAT_NAME_CHARACTERS] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L' };
	static uint16_t const odd[CHAT_NAME_CHARACTERS] = { ' ', 0x263A, '|', 'n', 7, 'x', ' ', ' ', 0 };
	static uint16_t const blank[CHAT_NAME_CHARACTERS] = { ' ', ' ', 0 };
	char name[CHAT_NAME_BYTES];

	CHECK(chat_name_clean(name, sizeof(name), plain, CHAT_NAME_CHARACTERS) == 5 && !strcmp(name, "Chief"));
	/* (no end within the twelve: twelve, never past them) */
	CHECK(chat_name_clean(name, sizeof(name), full, CHAT_NAME_CHARACTERS) == 12 && !strcmp(name, "ABCDEFGHIJKL"));
	CHECK(chat_name_clean(name, sizeof(name), full, 400) == 12);
	CHECK(chat_name_clean(name, sizeof(name), odd, CHAT_NAME_CHARACTERS) == 5 && !strcmp(name, "?/n?x"));
	CHECK(chat_name_clean(name, sizeof(name), blank, CHAT_NAME_CHARACTERS) == 1 && !strcmp(name, "?"));
	CHECK(chat_name_clean(name, 4, full, CHAT_NAME_CHARACTERS) == 3 && !strcmp(name, "ABC"));
	CHECK(chat_name_clean(name, sizeof(name), NULL, 3) == 1 && !strcmp(name, "?"));
}

static void test_request(void)
{
	struct chat_request_message request;
	char text[CHAT_TEXT_BYTES];

	memset(&request, 0, sizeof(request));
	request.kind = _chat_kind_quick;
	request.phrase = 2;
	CHECK(chat_request_valid(&request, text) && text[0] == 0);
	request.flags = 1 << _chat_flag_team_bit;
	CHECK(chat_request_valid(&request, text));
	request.flags = 2;
	CHECK(!chat_request_valid(&request, text));
	request.flags = -1;
	CHECK(!chat_request_valid(&request, text));
	request.flags = 0;
	request.phrase = CHAT_PHRASE_COUNT;
	CHECK(!chat_request_valid(&request, text));
	request.phrase = -1;
	CHECK(!chat_request_valid(&request, text));
	request.phrase = 0;
	request.local_player = 4;
	CHECK(!chat_request_valid(&request, text));
	request.local_player = -1;
	CHECK(!chat_request_valid(&request, text));
	request.local_player = 0;
	request.kind = 2;
	CHECK(!chat_request_valid(&request, text));
	request.kind = -32768;
	CHECK(!chat_request_valid(&request, text));

	request.kind = _chat_kind_typed;
	strcpy(request.text, "  hello |n world ");
	CHECK(chat_request_valid(&request, text) && !strcmp(text, "hello /n world"));
	/* (no end in the field: CHAT_TEXT_BYTES read at most, 80 kept) */
	memset(request.text, 'z', sizeof(request.text));
	CHECK(chat_request_valid(&request, text) && strlen(text) == CHAT_MAXIMUM_TEXT_LENGTH);
	memset(request.text, 0, sizeof(request.text));
	CHECK(!chat_request_valid(&request, text));
	strcpy(request.text, "\x01\x02 \t ");
	CHECK(!chat_request_valid(&request, text));
	strcpy(request.text, "free skins at evil.com");
	CHECK(!chat_request_valid(&request, text) && text[0] == 0);
}

static void test_relay(void)
{
	struct chat_relay_message relay;
	char name[CHAT_NAME_BYTES];
	char text[CHAT_TEXT_BYTES];
	int index;

	memset(&relay, 0, sizeof(relay));
	relay.kind = _chat_kind_typed;
	relay.team = -1;
	relay.player = 3;
	relay.name[0] = 'B';
	relay.name[1] = 'o';
	relay.name[2] = 'b';
	strcpy(relay.text, "hi");
	CHECK(chat_relay_valid(&relay, name, text) && !strcmp(name, "Bob") && !strcmp(text, "hi"));
	relay.team = 16;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.team = -2;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.team = 1;
	relay.player = 128;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.player = -1;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.player = 0;
	/* (a name with no end, a host's lie: twelve at most) */
	for (index = 0; index < CHAT_NAME_CHARACTERS; index++)
		relay.name[index] = 'N';
	CHECK(chat_relay_valid(&relay, name, text) && strlen(name) == 12);
	relay.name[0] = 0;
	CHECK(chat_relay_valid(&relay, name, text) && !strcmp(name, "?"));
	relay.kind = _chat_kind_quick;
	relay.phrase = 7;
	strcpy(relay.text, "ignored");
	CHECK(chat_relay_valid(&relay, name, text) && text[0] == 0);
	relay.phrase = 8;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.kind = _chat_kind_typed;
	strcpy(relay.text, "see 8.8.8.8");
	CHECK(!chat_relay_valid(&relay, name, text));
}

static void test_bucket(void)
{
	struct chat_bucket bucket;
	uint32_t now = 1000;
	int index, taken;

	memset(&bucket, 0, sizeof(bucket));
	/* (a burst, then none until a refill) */
	for (index = 0; index < CHAT_BURST; index++)
		CHECK(chat_bucket_take(&bucket, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(!chat_bucket_take(&bucket, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(chat_bucket_wait(&bucket, now, CHAT_BURST, CHAT_REFILL_MILLISECONDS) == CHAT_REFILL_MILLISECONDS);
	CHECK(!chat_bucket_take(&bucket, now + CHAT_REFILL_MILLISECONDS - 1, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(chat_bucket_wait(&bucket, now + CHAT_REFILL_MILLISECONDS - 1, CHAT_BURST, CHAT_REFILL_MILLISECONDS) <= 2);
	CHECK(chat_bucket_take(&bucket, now + CHAT_REFILL_MILLISECONDS + 1, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(!chat_bucket_take(&bucket, now + CHAT_REFILL_MILLISECONDS + 2, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	/* (a flood of a thousand lines a second for a minute: the burst and one
	every refill get through, no more) */
	memset(&bucket, 0, sizeof(bucket));
	taken = 0;
	for (index = 0; index < 60000; index++)
		taken += chat_bucket_take(&bucket, now + (uint32_t)index, CHAT_BURST, CHAT_REFILL_MILLISECONDS);
	CHECK(taken >= CHAT_BURST + 60000 / CHAT_REFILL_MILLISECONDS - 1 && taken <= CHAT_BURST + 60000 / CHAT_REFILL_MILLISECONDS);
	/* (full again after a long while; the clock's wrap is time going by) */
	memset(&bucket, 0, sizeof(bucket));
	for (index = 0; index < CHAT_BURST; index++)
		CHECK(chat_bucket_take(&bucket, 0xFFFFFF00u, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(!chat_bucket_take(&bucket, 0xFFFFFF00u, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(chat_bucket_take(&bucket, 0x00002000u, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	/* (a clock that goes back gives nothing) */
	memset(&bucket, 0, sizeof(bucket));
	for (index = 0; index < CHAT_BURST; index++)
		chat_bucket_take(&bucket, 50000, CHAT_BURST, CHAT_REFILL_MILLISECONDS);
	CHECK(!chat_bucket_take(&bucket, 40000, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	CHECK(!chat_bucket_take(&bucket, 40001, CHAT_BURST, CHAT_REFILL_MILLISECONDS));
	/* (the host's own limit, many machines at once) */
	memset(&bucket, 0, sizeof(bucket));
	taken = 0;
	for (index = 0; index < 10000; index++)
		taken += chat_bucket_take(&bucket, now + (uint32_t)index / 10u, CHAT_HOST_BURST, CHAT_HOST_REFILL_MILLISECONDS);
	CHECK(taken <= CHAT_HOST_BURST + 1000 / CHAT_HOST_REFILL_MILLISECONDS + 1);
}

int main(void)
{
	test_phrases();
	test_clean();
	test_links();
	test_names();
	test_request();
	test_relay();
	test_bucket();
	printf("%s chat_test: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
