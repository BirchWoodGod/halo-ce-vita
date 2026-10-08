/*
NET_FUZZ_MAP_SHARE.C

A fuzz target for map sharing's message rules (port/linux/game/
map_share_protocol.c): the fields of a joiner's request and of a host's
answer, the names they carry, a joiner's bookkeeping of the data messages
as they arrive, and the cache header a downloaded map must have. The
transfer itself (map_share.c) is not here.

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a request (struct map_share_request, as decoded)
  1  an answer (struct map_share_answer_message), for the map "fuzzmap"
  2  a file starts: the first 4 bytes its size
  3  a data message: offset, length, then the bytes (as many as there are)
  4  a cache header, then the file's size (4 bytes)
Built as net_fuzz_p2p.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/map_share_protocol.c"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void fuzz_record(int kind, const unsigned char *data, int size, struct map_share_receiver *receiver)
{
	switch (kind)
	{
	case 0:
	{
		struct map_share_request request;
		char name[MAP_SHARE_NAME_BYTES];

		memset(&request, 0, sizeof(request));
		memcpy(&request, data, (size_t)(size < (int)sizeof(request) ? size : (int)sizeof(request)));
		if (map_share_request_valid(&request) && (!map_share_name_from_field(request.name, name) ||
			!memchr(name, 0, sizeof(name)) || !map_share_name_valid(name)))
		{
			abort();
		}
		break;
	}
	case 1:
	{
		struct map_share_answer_message answer;

		memset(&answer, 0, sizeof(answer));
		memcpy(&answer, data, (size_t)(size < (int)sizeof(answer) ? size : (int)sizeof(answer)));
		if (map_share_answer_valid(&answer, "fuzzmap", 0x1234, (1u << NUMBER_OF_MAP_SHARE_CAPABILITIES) - 1) && answer.kind == _map_share_answer_offer &&
			(answer.size < MAP_SHARE_HEADER_BYTES || (uint32_t)answer.size > MAP_SHARE_MAXIMUM_FILE_BYTES))
		{
			abort();
		}
		break;
	}
	case 2:
	{
		uint32_t file_size = 0;

		memcpy(&file_size, data, (size_t)(size < 4 ? size : 4));
		map_share_receiver_begin(receiver, file_size % (MAP_SHARE_MAXIMUM_FILE_BYTES + 1));
		break;
	}
	case 3:
	{
		int32_t offset = 0, length = 0;
		uint8_t chunk[MAP_SHARE_CHUNK_BYTES];
		uint32_t received = receiver->received;

		if (size < 8)
			break;
		memcpy(&offset, data, 4);
		memcpy(&length, data + 4, 4);
		/* (the message's whole buffer, as decoded, whatever its length says) */
		memset(chunk, 0, sizeof(chunk));
		memcpy(chunk, data + 8, (size_t)(size - 8 < (int)sizeof(chunk) ? size - 8 : (int)sizeof(chunk)));
		if (map_share_receiver_accept(receiver, offset, length, chunk, NULL, NULL) == _map_share_chunk_ok &&
			(receiver->received != received + (uint32_t)length || receiver->received > receiver->size))
		{
			abort();
		}
		map_share_receiver_ack_due(receiver);
		break;
	}
	case 4:
	{
		uint8_t header[MAP_SHARE_HEADER_BYTES];
		uint32_t file_size = 0;
		int custom_edition;

		memset(header, 0, sizeof(header));
		memcpy(header, data, (size_t)(size < (int)sizeof(header) ? size : (int)sizeof(header)));
		if (size > (int)sizeof(header))
			memcpy(&file_size, data + sizeof(header), (size_t)(size - (int)sizeof(header) < 4 ? size - (int)sizeof(header) : 4));
		map_share_header_status_describe(map_share_header_validate(header, file_size, "fuzzmap", &custom_edition));
		break;
	}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct map_share_receiver receiver;
	const unsigned char *cursor = data, *end = data + size;
	int count = 0;

	map_share_receiver_begin(&receiver, 0x10000);
	while (count++ < 64 && end - cursor >= 3)
	{
		int kind = cursor[0] % 5;
		int length = cursor[1] | cursor[2] << 8;

		cursor += 3;
		if (length > end - cursor)
			length = (int)(end - cursor);
		fuzz_record(kind, cursor, length, &receiver);
		cursor += length;
	}
	return 0;
}

/* (p2p_crypto.c's sealing, which this target does not use, takes random
bytes from the platform layer) */
void posix_random_bytes(void *buffer, unsigned int size)
{
	memset(buffer, 0, size);
}
