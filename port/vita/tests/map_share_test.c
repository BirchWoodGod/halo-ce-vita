/*
MAP_SHARE_TEST.C

Desktop test of map sharing's rules (port/linux/game/map_share_protocol.c):
the names a shared map may have, the messages' field checks, the receiving
side's bookkeeping (order, bounds, digest, fingerprint), and the cache
header a download must have, with truncated, oversized, badly named,
damaged and lying files. run_map_share_test.sh builds it with the real
SHA-256 (p2p_crypto.c) and zlib's CRC-32 to compare against.

  MAP_SHARE_TEST_XBOX_MAP=<a modded Xbox .map>   also checks a real one
  MAP_SHARE_TEST_CE_MAP=<a Custom Edition .map>  (and streams both through)
*/

#include "map_share_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int failures;
static int checks;

#define CHECK(condition) do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void put_u32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* a cache header: version 5 (Xbox) or 609 (Custom Edition) */
static void make_header(uint8_t *header, int32_t version, char const *name, uint32_t file_length,
	uint32_t tag_data_offset, uint32_t tag_data_size, uint32_t checksum)
{
	memset(header, 0, MAP_SHARE_HEADER_BYTES);
	memcpy(header, "daeh", 4);
	put_u32(header + 0x04, (uint32_t)version);
	put_u32(header + 0x08, file_length);
	put_u32(header + 0x10, tag_data_offset);
	put_u32(header + 0x14, tag_data_size);
	strncpy((char *)header + 0x20, name, 31);
	strcpy((char *)header + 0x40, version == 5 ? "01.10.12.2276" : "01.00.00.0609");
	header[0x60] = 1;
	put_u32(header + 0x64, checksum);
	memcpy(header + 0x7FC, "toof", 4);
}

static void test_names(void)
{
	static char const *const good[] = { "mygulch", "My_Gulch-2", "a.b", "x", "beavercreek_halo3", "abcdefghijklmnopqrstuvwxy", "console", "com10", "uix" };
	static char const *const bad[] = {
		"", ".", "..", "../x", "a/b", "a\\b", "c:x", "a..b", ".hidden", "trailing.", "abcdefghijklmnopqrstuvwxyz",
		"bloodgulch", "BloodGulch", "a10", "D40", "bitmaps", "Sounds", "LOC", "ui", "with space", "caf\xc3\xa9",
		"tab\there", "new\nline", "star*", "q?", "pipe|", "semi;colon", "con", "CON.x", "nul", "com1", "lpt9.a",
	};
	char field[MAP_SHARE_NAME_BYTES];
	char name[MAP_SHARE_NAME_BYTES];
	unsigned index;

	for (index = 0; index < sizeof(good) / sizeof(good[0]); index++)
	{
		CHECK(map_share_name_valid(good[index]));
	}
	for (index = 0; index < sizeof(bad) / sizeof(bad[0]); index++)
	{
		if (map_share_name_valid(bad[index]))
		{
			printf("  (accepted '%s')\n", bad[index]);
		}
		CHECK(!map_share_name_valid(bad[index]));
	}
	CHECK(!map_share_name_valid(NULL));
	CHECK(map_share_name_is_stock("bloodgulch") && map_share_name_is_stock("A10") && !map_share_name_is_stock("mygulch"));

	/* a field from the network: ended within it, and valid */
	memset(field, 'a', sizeof(field));
	CHECK(!map_share_name_from_field(field, name) && !name[0]);
	memset(field, 0, sizeof(field));
	strcpy(field, "mygulch");
	CHECK(map_share_name_from_field(field, name) && !strcmp(name, "mygulch"));
	strcpy(field, "../../etc");
	CHECK(!map_share_name_from_field(field, name));
}

static void test_request(void)
{
	struct map_share_request request;

	memset(&request, 0, sizeof(request));
	request.command = _map_share_command_query;
	request.identity = 0x1234;
	strcpy(request.name, "mygulch");
	CHECK(map_share_request_valid(&request));
	request.command = _map_share_command_cancel;
	request.reason = _map_share_refusal_cancelled;
	CHECK(map_share_request_valid(&request));
	request.command = 0;
	CHECK(!map_share_request_valid(&request));
	request.command = NUMBER_OF_MAP_SHARE_COMMANDS;
	CHECK(!map_share_request_valid(&request));
	request.command = -3;
	CHECK(!map_share_request_valid(&request));
	request.command = _map_share_command_ack;
	request.offset = -1;
	CHECK(!map_share_request_valid(&request));
	request.offset = (int32_t)MAP_SHARE_MAXIMUM_FILE_BYTES + 1;
	CHECK(!map_share_request_valid(&request));
	request.offset = 4096;
	request.reason = NUMBER_OF_MAP_SHARE_REFUSALS;
	CHECK(!map_share_request_valid(&request));
	request.reason = 0;
	strcpy(request.name, "sounds");
	CHECK(!map_share_request_valid(&request));
	memset(request.name, 'x', sizeof(request.name));
	CHECK(!map_share_request_valid(&request));
}

static void test_answer(void)
{
	struct map_share_answer_message answer;

	memset(&answer, 0, sizeof(answer));
	answer.kind = _map_share_answer_offer;
	answer.size = 22310912;
	answer.identity = 0x0BADF00D;
	strcpy(answer.name, "mygulch");
	CHECK(map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	CHECK(map_share_answer_valid(&answer, "MyGulch", 0x0BADF00D));
	CHECK(!map_share_answer_valid(&answer, "othermap", 0x0BADF00D));
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00E));
	answer.flags = 1;
	CHECK(map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.flags = 3;
	CHECK(map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.flags = 4;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.flags = -1;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.flags = 0;
	/* oversized, truncated below a header, negative */
	answer.size = (int32_t)MAP_SHARE_MAXIMUM_FILE_BYTES + 1;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.size = (int32_t)MAP_SHARE_MAXIMUM_FILE_BYTES;
	CHECK(map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.size = MAP_SHARE_HEADER_BYTES - 1;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.size = -5;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.size = 4096;
	answer.kind = 0;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.kind = NUMBER_OF_MAP_SHARE_ANSWERS;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	/* a refusal names the map, with a known reason */
	answer.kind = _map_share_answer_refused;
	answer.reason = _map_share_refusal_busy;
	answer.identity = 0;
	CHECK(map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	answer.reason = 99;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
	/* an offer of identity 0 (no map) is no offer */
	answer.kind = _map_share_answer_offer;
	answer.reason = 0;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0));
	memset(answer.name, 'm', sizeof(answer.name));
	answer.identity = 0x0BADF00D;
	CHECK(!map_share_answer_valid(&answer, "mygulch", 0x0BADF00D));
}

static void test_sha256(void)
{
	static uint8_t const abc[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
	};
	struct halo_sha256_stream context;
	uint8_t digest[32];

	halo_sha256_begin(&context);
	halo_sha256_add(&context, "a", 1);
	halo_sha256_add(&context, "bc", 2);
	halo_sha256_end(&context, digest);
	CHECK(!memcmp(digest, abc, 32));
}

/* streams `file` (size bytes) to a receiver as a host does, `chunk` bytes
a message; `damage` flips a byte the host sends after hashing (a damaged
transfer); `stop` ends it early (truncated). Returns whether the receiver's
digest is the host's, and its fingerprint. */
static int stream(uint8_t const *file, uint32_t size, uint32_t chunk, uint32_t damage, uint32_t stop,
	uint32_t *identity, uint32_t *received)
{
	struct map_share_receiver receiver;
	struct halo_sha256_stream host;
	uint8_t host_digest[32];
	uint8_t digest[32];
	static uint8_t buffer[MAP_SHARE_CHUNK_BYTES];
	uint32_t sent = 0;
	uint32_t acknowledged = 0;
	uint32_t acks = 0;

	map_share_receiver_begin(&receiver, size);
	halo_sha256_begin(&host);
	while (sent < size && sent < stop)
	{
		uint32_t length = size - sent < chunk ? size - sent : chunk;

		/* the window: the host waits for an acknowledgement */
		if (sent - acknowledged >= MAP_SHARE_WINDOW_BYTES)
		{
			return -1;
		}
		memcpy(buffer, file + sent, length);
		halo_sha256_add(&host, buffer, length);
		if (damage && damage >= sent && damage < sent + length)
		{
			buffer[damage - sent] ^= 0x40;
		}
		if (map_share_receiver_accept(&receiver, (int32_t)sent, (int32_t)length, buffer) != _map_share_chunk_ok)
		{
			return -2;
		}
		sent += length;
		if (map_share_receiver_ack_due(&receiver))
		{
			receiver.acknowledged = receiver.received;
			acknowledged = receiver.received;
			acks++;
		}
	}
	halo_sha256_end(&host, host_digest);
	map_share_receiver_digest(&receiver, digest);
	*identity = map_share_receiver_identity(&receiver);
	*received = receiver.received;
	CHECK(size < MAP_SHARE_ACK_BYTES || acks > 0);

	return !memcmp(digest, host_digest, 32);
}

static void test_receiver(void)
{
	enum { SIZE = 300000 };
	static uint8_t file[SIZE];
	struct map_share_receiver receiver;
	uint8_t chunk[MAP_SHARE_CHUNK_BYTES];
	uint32_t identity;
	uint32_t received;
	uint32_t index;
	uLong crc;
	int custom_edition;

	for (index = 0; index < SIZE; index++)
	{
		file[index] = (uint8_t)(index * 2654435761u >> 13);
	}
	make_header(file, 5, "mygulch", 0x01000000, 0x00800000, 0x100000, 0xFFFFFFFF);
	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, file, SIZE);

	/* whole, in chunks of every size up to the largest */
	CHECK(stream(file, SIZE, MAP_SHARE_CHUNK_BYTES, 0, ~0u, &identity, &received) == 1);
	CHECK(received == SIZE);
	CHECK(identity == (uint32_t)crc);
	CHECK(identity == map_share_identity(0xFFFFFFFF, SIZE, (uint32_t)crc));
	CHECK(stream(file, SIZE, 1000, 0, ~0u, &identity, &received) == 1 && identity == (uint32_t)crc);
	CHECK(stream(file, SIZE, 1, 0, 70000, &identity, &received) == 1 && received == 70000);
	/* damaged in transit: the digests differ, and so does the CRC */
	CHECK(stream(file, SIZE, MAP_SHARE_CHUNK_BYTES, 123456, ~0u, &identity, &received) == 0);
	CHECK(identity != (uint32_t)crc);
	/* truncated: short of the size */
	CHECK(stream(file, SIZE, MAP_SHARE_CHUNK_BYTES, 0, 200000, &identity, &received) >= 0 && received < SIZE);

	/* the header is kept as it arrives, across chunks */
	map_share_receiver_begin(&receiver, SIZE);
	CHECK(map_share_receiver_accept(&receiver, 0, 700, file) == _map_share_chunk_ok);
	CHECK(map_share_receiver_accept(&receiver, 700, 1000, file + 700) == _map_share_chunk_ok);
	CHECK(memcmp(receiver.header, file, MAP_SHARE_HEADER_BYTES));
	CHECK(map_share_receiver_accept(&receiver, 1700, 1000, file + 1700) == _map_share_chunk_ok);
	CHECK(!memcmp(receiver.header, file, MAP_SHARE_HEADER_BYTES));
	CHECK(map_share_header_validate(receiver.header, SIZE, "mygulch", &custom_edition) == _map_share_header_ok && !custom_edition);

	/* out of order, repeated, bad lengths, past the end */
	memset(chunk, 0, sizeof(chunk));
	map_share_receiver_begin(&receiver, 5000);
	CHECK(map_share_receiver_accept(&receiver, 100, 100, chunk) == _map_share_chunk_out_of_order);
	CHECK(map_share_receiver_accept(&receiver, -1, 100, chunk) == _map_share_chunk_out_of_order);
	CHECK(map_share_receiver_accept(&receiver, 0, 0, chunk) == _map_share_chunk_bad_length);
	CHECK(map_share_receiver_accept(&receiver, 0, -5, chunk) == _map_share_chunk_bad_length);
	CHECK(map_share_receiver_accept(&receiver, 0, MAP_SHARE_CHUNK_BYTES + 1, chunk) == _map_share_chunk_bad_length);
	CHECK(map_share_receiver_accept(&receiver, 0, MAP_SHARE_CHUNK_BYTES, chunk) == _map_share_chunk_ok);
	CHECK(map_share_receiver_accept(&receiver, 0, 100, chunk) == _map_share_chunk_out_of_order);
	CHECK(map_share_receiver_accept(&receiver, MAP_SHARE_CHUNK_BYTES, MAP_SHARE_CHUNK_BYTES, chunk) == _map_share_chunk_past_end);
	CHECK(receiver.received == MAP_SHARE_CHUNK_BYTES);
	CHECK(map_share_receiver_accept(&receiver, MAP_SHARE_CHUNK_BYTES, 5000 - MAP_SHARE_CHUNK_BYTES, chunk) == _map_share_chunk_ok);
	CHECK(map_share_receiver_ack_due(&receiver));
	CHECK(map_share_receiver_accept(&receiver, 5000, 1, chunk) == _map_share_chunk_past_end);
	/* an offset near the top of the range does not wrap */
	map_share_receiver_begin(&receiver, 0xFFFFFFF0u);
	receiver.received = 0xFFFFFF00u;
	CHECK(map_share_receiver_accept(&receiver, (int32_t)0xFFFFFF00u, 0x200, chunk) == _map_share_chunk_out_of_order);
}

static void test_headers(void)
{
	static uint8_t header[MAP_SHARE_HEADER_BYTES];
	int custom_edition;

	/* an Xbox map: compressed on disk, its offsets in its decompressed
	length */
	make_header(header, 5, "mygulch", 0x029FEA00, 0x02304E00, 0x006F9A18, 0xFFFFFFFF);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_ok && !custom_edition);
	CHECK(map_share_header_validate(header, 22310912, "MYGULCH", &custom_edition) == _map_share_header_ok);
	/* named otherwise inside (a renamed Xbox map does not load) */
	CHECK(map_share_header_validate(header, 22310912, "othermap", &custom_edition) == _map_share_header_wrong_name);
	/* decompressed too large for the cache partition */
	put_u32(header + 0x08, 0x02F00000);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_length);
	put_u32(header + 0x08, 0x029FEA00);
	/* tag data past the end, or wrapping */
	put_u32(header + 0x10, 0x02900000);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_tag_data);
	put_u32(header + 0x10, 0xFFFFF000);
	put_u32(header + 0x14, 0x2000);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_tag_data);
	put_u32(header + 0x10, 0x100);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_tag_data);
	put_u32(header + 0x10, 0x02304E00);
	put_u32(header + 0x14, 0x10);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_tag_data);
	put_u32(header + 0x14, 0x006F9A18);
	/* not a multiplayer map */
	header[0x60] = 0;
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_not_multiplayer);
	header[0x60] = 2;
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_not_multiplayer);
	header[0x60] = 1;
	header[0x61] = 1;
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_not_multiplayer);
	header[0x61] = 0;
	/* strings not ended in their fields */
	memset(header + 0x20, 'm', 0x20);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_unterminated_string);
	make_header(header, 5, "mygulch", 0x029FEA00, 0x02304E00, 0x006F9A18, 0xFFFFFFFF);
	memset(header + 0x40, '1', 0x20);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_unterminated_string);
	make_header(header, 5, "mygulch", 0x029FEA00, 0x02304E00, 0x006F9A18, 0xFFFFFFFF);
	/* signatures, version */
	header[0] = 'x';
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_signatures);
	header[0] = 'd';
	header[0x7FF] = 'x';
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_bad_signatures);
	header[0x7FF] = 'f';
	put_u32(header + 0x04, 7);
	CHECK(map_share_header_validate(header, 22310912, "mygulch", &custom_edition) == _map_share_header_unsupported_version);
	put_u32(header + 0x04, 5);
	/* the file's own size: below a header, above the cap */
	CHECK(map_share_header_validate(header, MAP_SHARE_HEADER_BYTES - 1, "mygulch", &custom_edition) == _map_share_header_too_small);
	CHECK(map_share_header_validate(header, MAP_SHARE_MAXIMUM_FILE_BYTES + 1, "mygulch", &custom_edition) == _map_share_header_bad_length);

	/* a Custom Edition map: read in place, so every length is in the file */
	make_header(header, 609, "pcgulch", 0x02C0EDC0, 0x027D7D44, 0x0043707C, 0xFB74B9F4);
	CHECK(map_share_header_validate(header, 46198208, "pcgulch", &custom_edition) == _map_share_header_ok && custom_edition);
	/* (a Custom Edition map's file may be named otherwise) */
	CHECK(map_share_header_validate(header, 46198208, "renamed_gulch", &custom_edition) == _map_share_header_ok);
	/* the header claims more than the file holds (a truncated file) */
	CHECK(map_share_header_validate(header, 46198208 - 1, "pcgulch", &custom_edition) == _map_share_header_bad_length);
	CHECK(map_share_header_validate(header, 1000000, "pcgulch", &custom_edition) == _map_share_header_bad_length);
	put_u32(header + 0x0C, 0x1000);
	CHECK(map_share_header_validate(header, 46198208, "pcgulch", &custom_edition) == _map_share_header_compressed);
	put_u32(header + 0x0C, 0);
	put_u32(header + 0x14, 0x00500000);
	CHECK(map_share_header_validate(header, 46198208, "pcgulch", &custom_edition) == _map_share_header_bad_tag_data);
	put_u32(header + 0x14, 0x0043707C);
	put_u32(header + 0x08, 0x100);
	CHECK(map_share_header_validate(header, 46198208, "pcgulch", &custom_edition) == _map_share_header_bad_length);

	/* every status has words */
	CHECK(strcmp(map_share_header_status_describe(_map_share_header_bad_tag_data), "unknown"));
	CHECK(!strcmp(map_share_header_status_describe((enum map_share_header_status)99), "unknown"));
	CHECK(!strcmp(map_share_refusal_describe((enum map_share_refusal)-1), "unknown"));
}

static void test_identity(void)
{
	/* custom_edition_cache.c's formula: (checksum ^ size * 0x9E3779B1) in 32
	bits, else the CRC; never 0 */
	CHECK(map_share_identity(0xFB74B9F4, 46198208, 0) == (uint32_t)(0xFB74B9F4u ^ (uint32_t)(46198208u * 0x9E3779B1u)));
	CHECK(map_share_identity(0, 1000, 0xCAFEBABE) == 0xCAFEBABE);
	CHECK(map_share_identity(0xFFFFFFFF, 1000, 0xCAFEBABE) == 0xCAFEBABE);
	CHECK(map_share_identity(0, 1000, 0) == 1);
	CHECK(map_share_identity(46198208u * 0x9E3779B1u, 46198208, 0) == 1);
}

/* a real map through the whole path: header checked, streamed, fingerprinted */
static void test_real_map(char const *path, char const *name, int expect_custom_edition)
{
	FILE *file = path ? fopen(path, "rb") : NULL;
	uint8_t *bytes;
	long size;
	uint32_t identity;
	uint32_t received;
	uint32_t checksum;
	uLong crc;
	int custom_edition;

	if (!file)
	{
		if (path)
			printf("  (skipped %s: cannot open)\n", path);
		return;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc((size_t)size);
	CHECK(bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size);
	fclose(file);
	if (!bytes)
		return;
	CHECK(map_share_header_validate(bytes, (uint32_t)size, name, &custom_edition) == _map_share_header_ok);
	CHECK(custom_edition == expect_custom_edition);
	CHECK(stream(bytes, (uint32_t)size, MAP_SHARE_CHUNK_BYTES, 0, ~0u, &identity, &received) == 1);
	CHECK(received == (uint32_t)size);
	crc = crc32(crc32(0L, Z_NULL, 0), bytes, (uInt)size);
	checksum = (uint32_t)bytes[0x64] | (uint32_t)bytes[0x65] << 8 | (uint32_t)bytes[0x66] << 16 | (uint32_t)bytes[0x67] << 24;
	CHECK(identity == map_share_identity(checksum, (uint32_t)size, (uint32_t)crc));
	printf("  %s: %ld bytes, %s, fingerprint 0x%08X\n", name, size, custom_edition ? "Custom Edition" : "Xbox", identity);
	/* truncated on disk: its header no longer fits (Custom Edition), and
	the fingerprint changes */
	if (custom_edition)
		CHECK(map_share_header_validate(bytes, (uint32_t)size / 2, name, &custom_edition) == _map_share_header_bad_length);
	free(bytes);
}

int main(void)
{
	test_names();
	test_request();
	test_answer();
	test_sha256();
	test_receiver();
	test_headers();
	test_identity();
	test_real_map(getenv("MAP_SHARE_TEST_XBOX_MAP"), "mygulch", 0);
	test_real_map(getenv("MAP_SHARE_TEST_CE_MAP"), "pcgulch", 1);
	printf("map_share_test: %d/%d checks passed\n", checks - failures, checks);

	return failures ? 1 : 0;
}
