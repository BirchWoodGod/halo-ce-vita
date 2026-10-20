/*
NET_FUZZ_TWIN.C

In-game mutation fuzzing of the game's message handlers (net_fuzz_twin.h):
tests only, compiled out unless HALO_NET_FUZZ_TWIN is defined (never in
release builds).

  HALO_NET_FUZZ_RATE=N   one message in N gets a twin (0, the default: none)
  HALO_NET_FUZZ_SEED=n   the mutations' sequence (1)
  HALO_NET_FUZZ_DUMP=f   the last twins, and the messages they came from, as
                         hex, written to f when a sanitizer ends the process
                         and every 1024 twins (so that a hang leaves them too)
  HALO_NET_FUZZ_CAPTURE=d  the first NET_FUZZ_CAPTURE_EACH of each kind of
                         what a client takes from its host, written to d
                         (which must be there) as fuzz targets' seeds
  HALO_NET_FUZZ_HOST_ONLY=1  twins of what a host takes alone (its clients'
                         messages and datagrams), none of what a client takes:
                         a host's own client, in its process, then reads its
                         game as sent (run_netns_online_test.sh hostfuzz)
  HALO_NET_FUZZ_KINDS=list  only the distributed netcode's messages of these
                         kinds get twins (a comma list of numbers and ranges,
                         "64-77" for the AI and co-op messages, 16 for the
                         batches that carry a tick's unreliable ones); the
                         game's own packets none. Unset: every message
*/

#include "net_fuzz_twin.h"

#ifdef NET_FUZZ_TWIN_ENABLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's: the game's fopen takes an Xbox path, port/linux/include/
stdio.h; these are the host's) */
#undef fopen

/* (AddressSanitizer's, when the build has it) */
#if defined(__SANITIZE_ADDRESS__)
#define NET_FUZZ_TWIN_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NET_FUZZ_TWIN_SANITIZER 1
#endif
#endif
#ifdef NET_FUZZ_TWIN_SANITIZER
void __sanitizer_set_death_callback(void (*callback)(void));
#endif

enum
{
	NET_FUZZ_TWIN_RING = 64,
	NET_FUZZ_TWIN_MAXIMUM = 0x1100,
};

static struct
{
	int ready;
	int rate;
	int host_only;
	/* HALO_NET_FUZZ_KINDS: the distributed kinds twinned, a bit each (none
	set: every message) */
	int filtered;
	unsigned char kinds[32];
	unsigned long state;
	unsigned long count;
	char dump_path[512];
	struct
	{
		unsigned long sequence;
		int direction;
		unsigned short original_size, size;
		unsigned char original[NET_FUZZ_TWIN_MAXIMUM];
		unsigned char bytes[NET_FUZZ_TWIN_MAXIMUM];
	} ring[NET_FUZZ_TWIN_RING];
} net_fuzz;

enum
{
	NET_FUZZ_CAPTURE_EACH = 4,
};

static char const *const net_fuzz_capture_names[NUMBER_OF_NET_FUZZ_CAPTURES] =
{
	"packet", "data", "record", "accept", "listing"
};

void net_fuzz_capture(
	int kind,
	int key,
	void const *prefix,
	int prefix_size,
	void const *bytes,
	int size)
{
	static unsigned char counts[NUMBER_OF_NET_FUZZ_CAPTURES][256];
	static int ready;
	static char const *folder;
	char path[768];
	FILE *file;

	if (!ready)
	{
		ready = 1;
		folder = getenv("HALO_NET_FUZZ_CAPTURE");
	}
	if (!folder || !folder[0] || kind < 0 || kind >= NUMBER_OF_NET_FUZZ_CAPTURES || prefix_size < 0 || size < 0)
		return;
	key &= 0xFF;
	if (counts[kind][key] >= NET_FUZZ_CAPTURE_EACH)
		return;
	snprintf(path, sizeof(path), "%s/%s-%02x-%d", folder, net_fuzz_capture_names[kind], key, counts[kind][key]);
	counts[kind][key]++;
	if (!(file = fopen(path, "wb")))
		return;
	if (prefix_size)
		fwrite(prefix, 1, (size_t)prefix_size, file);
	if (size)
		fwrite(bytes, 1, (size_t)size, file);
	fclose(file);
}

/* HALO_NET_FUZZ_KINDS read: "a,b-c,..." of kinds 0-255 */
static void net_fuzz_kinds_read(
	char const *list)
{
	while (list && *list)
	{
		char *end;
		long first = strtol(list, &end, 10), last = first, kind;

		if (end == list)
			break;
		if (*end == '-')
		{
			char const *from = end + 1;

			last = strtol(from, &end, 10);
			if (end == from)
				last = first;
		}
		for (kind = first < 0 ? 0 : first; kind <= last && kind < 256; kind++)
		{
			net_fuzz.kinds[kind / 8] |= (unsigned char)(1 << (kind % 8));
			net_fuzz.filtered = 1;
		}
		list = *end == ',' ? end + 1 : end;
		if (*end && *end != ',')
			break;
	}
}

static int net_fuzz_random(
	void)
{
	net_fuzz.state = net_fuzz.state * 1103515245UL + 12345UL;
	return (int)((net_fuzz.state >> 8) & 0x7FFFFF);
}

static void net_fuzz_dump(
	void)
{
	unsigned long first = net_fuzz.count > NET_FUZZ_TWIN_RING ? net_fuzz.count - NET_FUZZ_TWIN_RING : 0;
	unsigned long sequence;
	FILE *file;

	if (!net_fuzz.dump_path[0] || !(file = fopen(net_fuzz.dump_path, "w")))
		return;
	for (sequence = first; sequence < net_fuzz.count; sequence++)
	{
		int slot = (int)(sequence % NET_FUZZ_TWIN_RING);
		int index;

		fprintf(file, "twin %lu direction %d size %d original %d\noriginal ", net_fuzz.ring[slot].sequence,
			net_fuzz.ring[slot].direction, net_fuzz.ring[slot].size, net_fuzz.ring[slot].original_size);
		for (index = 0; index < net_fuzz.ring[slot].original_size; index++)
			fprintf(file, "%02x", net_fuzz.ring[slot].original[index]);
		fprintf(file, "\ntwin ");
		for (index = 0; index < net_fuzz.ring[slot].size; index++)
			fprintf(file, "%02x", net_fuzz.ring[slot].bytes[index]);
		fprintf(file, "\n");
	}
	fclose(file);
}

int net_fuzz_twin(
	void const *message,
	unsigned short size,
	unsigned short buffer_size,
	void *twin,
	unsigned short *twin_size,
	int direction)
{
	static unsigned char const interesting[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0xFE, 0x10, 0x40, 0x81 };
	unsigned char *bytes = twin;
	int mutations, mutation;
	int length = size;
	int slot;

	if (!net_fuzz.ready)
	{
		char const *rate = getenv("HALO_NET_FUZZ_RATE");
		char const *seed = getenv("HALO_NET_FUZZ_SEED");
		char const *dump = getenv("HALO_NET_FUZZ_DUMP");

		net_fuzz.ready = 1;
		net_fuzz.rate = rate ? atoi(rate) : 0;
		net_fuzz.host_only = getenv("HALO_NET_FUZZ_HOST_ONLY") && atoi(getenv("HALO_NET_FUZZ_HOST_ONLY"));
		net_fuzz.state = seed ? strtoul(seed, NULL, 10) : 1;
		net_fuzz_kinds_read(getenv("HALO_NET_FUZZ_KINDS"));
		fprintf(stderr, "net fuzz twin: one message in %d twinned (direction %d), capture %s, kinds %s\n", net_fuzz.rate,
			direction, getenv("HALO_NET_FUZZ_CAPTURE") ? getenv("HALO_NET_FUZZ_CAPTURE") : "off",
			net_fuzz.filtered ? getenv("HALO_NET_FUZZ_KINDS") : "all");
		if (dump)
		{
			snprintf(net_fuzz.dump_path, sizeof(net_fuzz.dump_path), "%s", dump);
#ifdef NET_FUZZ_TWIN_SANITIZER
			__sanitizer_set_death_callback(net_fuzz_dump);
#endif
		}
	}
	/* (the host's messages: seeds, a game message as net_fuzz_messages.c
	takes it, after its header) */
	if (direction == _net_fuzz_twin_client_from_host && size >= 3)
	{
		unsigned short header;

		memcpy(&header, message, 2);
		if (((header >> 2) & 3) == 3)
			net_fuzz_capture(_net_fuzz_capture_packet, ((unsigned char const *)message)[size - 1], NULL, 0,
				(unsigned char const *)message + 2, size - 2);
		else if (((header >> 2) & 3) == 2)
			net_fuzz_capture(_net_fuzz_capture_data, ((unsigned char const *)message)[2], NULL, 0, message, size);
	}
	if (net_fuzz.rate <= 0 || size < 3 || size > buffer_size || buffer_size > NET_FUZZ_TWIN_MAXIMUM ||
		(net_fuzz.host_only && direction == _net_fuzz_twin_client_from_host))
	{
		return 0;
	}
	/* (HALO_NET_FUZZ_KINDS: a distributed message of a kind listed, its type
	the byte after the message header) */
	if (net_fuzz.filtered)
	{
		unsigned short header;
		unsigned char kind = ((unsigned char const *)message)[2];

		memcpy(&header, message, 2);
		if (((header >> 2) & 3) != 2 || !(net_fuzz.kinds[kind / 8] & (1 << (kind % 8))))
			return 0;
	}
	if (net_fuzz_random() % net_fuzz.rate)
		return 0;
	memcpy(bytes, message, size);
	mutations = 1 + net_fuzz_random() % 6;
	for (mutation = 0; mutation < mutations; mutation++)
	{
		/* (after the message header, the first 2 bytes) */
		int at = 2 + net_fuzz_random() % (length > 2 ? length - 2 : 1);

		switch (net_fuzz_random() % 9)
		{
		case 0: case 1:
			bytes[at] ^= (unsigned char)(1 << (net_fuzz_random() % 8));
			break;
		case 2: case 3:
			bytes[at] = interesting[net_fuzz_random() % sizeof(interesting)];
			break;
		case 4:
			bytes[at] = (unsigned char)net_fuzz_random();
			break;
		case 5:
			/* a 16- or 32-bit value at an extreme */
			if (at + 4 <= length)
			{
				unsigned long value = net_fuzz_random() % 2 ? 0x7FFFFFFFUL : 0x80000000UL;

				if (net_fuzz_random() % 2)
					value = 0xFFFFFFFFUL;
				memcpy(bytes + at, &value, net_fuzz_random() % 2 ? 2 : 4);
			}
			break;
		case 6:
			/* shorter */
			if (length > 3)
				length = 3 + net_fuzz_random() % (length - 3);
			break;
		case 7:
			/* longer */
			{
				int more = 1 + net_fuzz_random() % 64;
				int index;

				if (length + more > buffer_size)
					more = buffer_size - length;
				for (index = 0; index < more; index++)
					bytes[length + index] = (unsigned char)net_fuzz_random();
				length += more;
			}
			break;
		case 8:
			/* another packet type (a game message's last byte) */
			bytes[length - 1] = (unsigned char)(net_fuzz_random() % 48);
			break;
		}
	}
	/* (the header's size the twin's, mostly: else only the handlers' size
	check runs) */
	if (net_fuzz_random() % 8)
	{
		unsigned short header;

		memcpy(&header, bytes, 2);
		header = (unsigned short)((header & 15) | (length << 4));
		memcpy(bytes, &header, 2);
	}
	*twin_size = (unsigned short)length;
	slot = (int)(net_fuzz.count % NET_FUZZ_TWIN_RING);
	net_fuzz.ring[slot].sequence = net_fuzz.count;
	net_fuzz.ring[slot].direction = direction;
	net_fuzz.ring[slot].original_size = size;
	net_fuzz.ring[slot].size = (unsigned short)length;
	memcpy(net_fuzz.ring[slot].original, message, size);
	memcpy(net_fuzz.ring[slot].bytes, bytes, (size_t)length);
	net_fuzz.count++;
	if (!(net_fuzz.count % 1024) || net_fuzz.count == 16 || net_fuzz.count == 256)
	{
		fprintf(stderr, "net fuzz twin: %lu twins (direction %d)\n", net_fuzz.count, direction);
		net_fuzz_dump();
	}
	return 1;
}

#endif

/* (a translation unit with something in it, built without the twins) */
typedef int net_fuzz_twin_translation_unit;
