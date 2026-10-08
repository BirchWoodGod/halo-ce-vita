/*
CHAT_TEST.C

Desktop test of game chat's rules (port/linux/game/chat_protocol.c): the
phrases, what is kept of typed text (printable ASCII, the game's text codes
and control characters out, the length cap) and of names, the links
refused (OpenCE PR #72's cases and more), a joiner's request and a host's
relay as every field from the wire could be, the host's notices and what
its own Game chat lets through, the mutes (a new name, a machine gone, a
player back), and the flood limits.
run_chat_test.sh builds it 32-bit, with AddressSanitizer and UBSan.
*/

#include "chat_protocol.h"
#include "../../linux/src/chat_link.h"

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
	/* (as the host keeps names apart: a Latin letter with a mark its plain
	letter, the spaces within kept, so "Bo  b" and "Bo b", or "B\u00f6b" and
	"B?b", which the host tells apart, are not shown the same) */
	{
		static uint16_t const accented[CHAT_NAME_CHARACTERS] = { 'B', 0xF6, 'b', ' ', 0xC9, 0x17F, 0x180, 0 };
		static uint16_t const spaced[CHAT_NAME_CHARACTERS] = { 'B', 'o', ' ', ' ', 'b', 0 };

		CHECK(chat_name_clean(name, sizeof(name), accented, CHAT_NAME_CHARACTERS) == 7 && !strcmp(name, "Bob Es?"));
		CHECK(chat_name_clean(name, sizeof(name), spaced, CHAT_NAME_CHARACTERS) == 5 && !strcmp(name, "Bo  b"));
	}
}

static void test_notices(void)
{
	struct chat_relay_message relay;
	struct chat_request_message request;
	char name[CHAT_NAME_BYTES];
	char text[CHAT_TEXT_BYTES];

	CHECK(!strcmp(chat_notice_text(_chat_notice_host_off), "Chat is off in this game"));
	CHECK(!strcmp(chat_notice_text(_chat_notice_host_quick), "Only quick chat in this game"));
	CHECK(chat_notice_text(-1) == NULL && chat_notice_text(NUMBER_OF_CHAT_NOTICES) == NULL);
	CHECK(cleaned(chat_notice_text(0), chat_notice_text(0)) && cleaned(chat_notice_text(1), chat_notice_text(1)));
	/* (the host's own Game chat is its game's) */
	CHECK(chat_host_notice(HALO_CHAT_MODE_ON, _chat_kind_quick) == -1);
	CHECK(chat_host_notice(HALO_CHAT_MODE_ON, _chat_kind_typed) == -1);
	CHECK(chat_host_notice(HALO_CHAT_MODE_QUICK, _chat_kind_quick) == -1);
	CHECK(chat_host_notice(HALO_CHAT_MODE_QUICK, _chat_kind_typed) == _chat_notice_host_quick);
	CHECK(chat_host_notice(HALO_CHAT_MODE_OFF, _chat_kind_quick) == _chat_notice_host_off);
	CHECK(chat_host_notice(HALO_CHAT_MODE_OFF, _chat_kind_typed) == _chat_notice_host_off);
	CHECK(chat_host_notice(7, _chat_kind_quick) == _chat_notice_host_off);

	/* a notice from the host: its number only, naming nobody */
	memset(&relay, 0, sizeof(relay));
	relay.kind = _chat_kind_notice;
	relay.phrase = _chat_notice_host_quick;
	relay.team = -1;
	relay.name[0] = 'E';
	strcpy(relay.text, "visit evil.com");
	memset(name, 0x5A, sizeof(name));
	CHECK(chat_relay_valid(&relay, name, text) && !name[0] && !text[0]);
	relay.phrase = NUMBER_OF_CHAT_NOTICES;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.phrase = -1;
	CHECK(!chat_relay_valid(&relay, name, text));
	relay.phrase = 0;
	relay.flags = 1 << _chat_flag_team_bit;
	CHECK(!chat_relay_valid(&relay, name, text));
	/* ... and a joiner's request may not be one */
	memset(&request, 0, sizeof(request));
	request.kind = _chat_kind_notice;
	CHECK(!chat_request_valid(&request, text));
}

static void test_mutes(void)
{
	struct chat_mutes mutes;
	int index;

	memset(&mutes, 0, sizeof(mutes));
	/* muted where the game has them (machine 2, controller 0) */
	CHECK(chat_mutes_set(&mutes, "Troll", 2, 0, 1) && mutes.count == 1);
	CHECK(!chat_mutes_set(&mutes, "Troll", 2, 0, 1) && mutes.count == 1);
	CHECK(chat_mutes_match(&mutes, "Troll", 2, 0));
	CHECK(chat_mutes_match(&mutes, "Troll", -1, -1));
	CHECK(!chat_mutes_match(&mutes, "Friend", 3, 0));
	CHECK(!chat_mutes_match(&mutes, "Friend", 2, 1));
	/* a new name: still muted, the mute following it; the name it left,
	taken by another, is not muted */
	chat_mutes_player(&mutes, 2, 0, "Nice");
	CHECK(chat_mutes_match(&mutes, "Nice", 2, 0) && !strcmp(mutes.entries[0].name, "Nice"));
	chat_mutes_player(&mutes, 4, 0, "Troll");
	CHECK(!chat_mutes_match(&mutes, "Troll", 4, 0));
	/* ... nor its new name, once the muted one took another again */
	chat_mutes_player(&mutes, 2, 0, "Troll2");
	CHECK(!chat_mutes_match(&mutes, "Nice", 5, 0) && chat_mutes_match(&mutes, "Troll2", 2, 0));
	/* the machine gone: by name; back in the game (another machine), known
	by its place again, then a new name does not shake it off */
	chat_mutes_forget_machine(&mutes, 2);
	CHECK(mutes.count == 1 && mutes.entries[0].machine == -1 && chat_mutes_match(&mutes, "Troll2", 6, 0));
	CHECK(!chat_mutes_match(&mutes, "x", 2, 0));
	chat_mutes_player(&mutes, 6, 1, "Troll2");
	chat_mutes_player(&mutes, 6, 1, "Angel");
	CHECK(chat_mutes_match(&mutes, "Angel", 6, 1) && !chat_mutes_match(&mutes, "Troll2", 7, 0));
	/* heard again, by name or by place */
	CHECK(chat_mutes_set(&mutes, "Angel", -1, -1, 0) && mutes.count == 0);
	CHECK(!chat_mutes_set(&mutes, "Angel", -1, -1, 0));
	chat_mutes_set(&mutes, "Troll", 2, 0, 1);
	CHECK(chat_mutes_set(&mutes, "Renamed", 2, 0, 0) && mutes.count == 0);
	/* (a mute whose name another took and whose machine went: gone) */
	chat_mutes_set(&mutes, "Troll", 2, 0, 1);
	chat_mutes_player(&mutes, 3, 0, "Troll");
	chat_mutes_forget_machine(&mutes, -1);
	CHECK(mutes.count == 0);
	/* by name alone (not in the game's record) */
	CHECK(chat_mutes_set(&mutes, "Ghost", -1, 5, 1) && mutes.entries[0].machine == -1 && mutes.entries[0].controller == -1);
	CHECK(!chat_mutes_match(&mutes, "", -1, -1) && !chat_mutes_match(&mutes, NULL, -1, -1));
	CHECK(!chat_mutes_set(&mutes, "", 1, 0, 1) && !chat_mutes_set(&mutes, NULL, 1, 0, 1));
	/* (full: no more, nothing written past the list) */
	memset(&mutes, 0, sizeof(mutes));
	for (index = 0; index < CHAT_MUTED_PLAYERS + 4; index++)
	{
		char name[CHAT_NAME_BYTES];

		snprintf(name, sizeof(name), "p%d", index);
		chat_mutes_set(&mutes, name, index, 0, 1);
	}
	CHECK(mutes.count == CHAT_MUTED_PLAYERS && chat_mutes_match(&mutes, "p31", -1, -1) && !chat_mutes_match(&mutes, "p32", 32, 0));
	/* (a long name: kept to a name's size) */
	CHECK(chat_mutes_set(&mutes, "p0", 0, 0, 0));
	CHECK(chat_mutes_set(&mutes, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 40, 0, 1) &&
		strlen(mutes.entries[CHAT_MUTED_PLAYERS - 1].name) == CHAT_NAME_BYTES - 1);
	/* (a count from nowhere is not trusted) */
	mutes.count = 1000;
	CHECK(!chat_mutes_set(&mutes, "x", 1, 0, 1));
	chat_mutes_forget_machine(&mutes, -1);
	CHECK(mutes.count == 0);
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
	test_notices();
	test_mutes();
	test_bucket();
	printf("%s chat_test: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
