/*
NET_FUZZ_MESSAGES.C

A fuzz target for the game's network messages (source/networking/
network_messages.c and the packet encoding under it, source/memory/
data_packet*.c, data_encoding.c): decode_network_game_message, which every
message from another machine goes through, its packet's type and version,
fields, counts, strings and sizes all from the wire.

An input is a message as the handlers pass it to the decoder (after the
message header): its last byte names the packet type, and the decoded
message goes into a buffer exactly as large as that type's structure, as
the handlers' locals are (network_*_message_handler.c, map_share.c), so
that a write past it is caught. Each of the packet classes the handlers
expect is tried in turn (one that is not the packet's must decode nothing).
A message that decodes is encoded again and decoded once more, which must
give the same structure.

Built as the game's code is (its flags, from build.ninja; DEBUG, so its
assertions are fatal) by run_net_fuzz_test.sh.
*/

#include "cseries.h"
#include "memory/data_packet_groups.h"
#include "memory/data_packets.h"
#include "networking/network_messages.h"

#include <stdint.h>

/* network_messages.c's packet group (its globals are static: the file is
built into this one) */
#include "../../../source/networking/network_messages.c"
#include "net_fuzz_game.h"

/* ---------- what the game gives the code under test */

unsigned long system_milliseconds(void)
{
	static unsigned long now = 1000;

	return now += 7;
}

int map_share_name_valid(char const *name)
{
	(void)name;
	return 1;
}

/* the size of each type's structure, as the handlers declare it (the
typedefs above, in the order of enum network_game_message_type) */
static long const fuzz_structure_sizes[NUMBER_OF_NETWORK_GAME_MESSAGE_TYPES] =
{
	sizeof(message_client_broadcast_game_search), sizeof(message_client_ping), sizeof(message_server_game_advertise),
	sizeof(message_server_pong), sizeof(message_server_machine_accepted), sizeof(message_server_machine_rejected),
	sizeof(message_server_game_settings_update), sizeof(message_server_pregame_countdown),
	sizeof(message_server_begin_game), sizeof(message_server_graceful_game_exit_pregame),
	sizeof(message_server_pregame_keep_alive), sizeof(message_server_postgame_keep_alive),
	sizeof(message_client_join_game_request), sizeof(message_client_add_player_request_pregame),
	sizeof(message_client_remove_player_request_pregame), sizeof(message_client_settings_request),
	sizeof(message_client_player_settings_request), sizeof(message_client_game_start_request),
	sizeof(message_client_graceful_game_exit_pregame), sizeof(message_client_map_is_precached_pregame),
	sizeof(message_server_game_update), sizeof(message_server_add_player_ingame),
	sizeof(message_server_remove_player_ingame), sizeof(message_server_game_over), sizeof(message_client_loaded),
	sizeof(message_client_game_update), sizeof(message_client_add_player_request_ingame),
	sizeof(message_client_remove_player_request_ingame), sizeof(message_client_host_crashed_cry_for_help),
	sizeof(message_client_join_new_host), sizeof(message_server_switch_to_pregame),
	sizeof(message_server_graceful_game_exit_postgame), sizeof(message_client_remove_player_request_postgame),
	sizeof(message_client_switch_to_pregame), sizeof(message_client_graceful_game_exit_postgame),
	sizeof(message_client_map_download), sizeof(message_server_map_download_answer),
	sizeof(message_server_map_download_data), sizeof(message_client_chat), sizeof(message_server_chat),
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static int initialized;
	struct data_packet_group_definition *group = &data_0030aa68.group;
	byte encoded[HALO_PORT_NETWORK_PACKET_SIZE + 8];
	short packet_class;
	short type;

	if (!initialized)
	{
		initialize_network_game_packets();
		initialized = TRUE;
	}
	if (size < 1 || size > HALO_PORT_NETWORK_PACKET_SIZE)
		return 0;
	type = (short)(char)data[size - 1];
	for (packet_class = 0; packet_class < group->packet_class_count; packet_class++)
	{
		short encoded_size = (short)size;
		short packet_type = 0;
		short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
		long structure_size = type >= 0 && type < NUMBER_OF_NETWORK_GAME_MESSAGE_TYPES ? fuzz_structure_sizes[type] : 1;
		byte *decoded = malloc((size_t)structure_size);

		/* (the decoder reads the type, and byte swaps, in place) */
		memcpy(encoded, data, size);
		memset(decoded, 0xA5, (size_t)structure_size);
		if (decode_network_game_message(decoded, encoded, &encoded_size, &packet_type, &packet_version, packet_class))
		{
			byte *again = malloc((size_t)structure_size);
			void *message;

			if (packet_type != type || group->packets[type].packet_class != packet_class)
				abort();
			/* encoded again (as create_network_game_message does), and decoded:
			the same */
			message = create_network_game_message(packet_type, decoded, (short)structure_size);
			if (message)
			{
				word message_size = GET_MESSAGE_SIZE(*(message_header *)message);
				short again_size = (short)(message_size - sizeof(message_header));
				short again_type = 0;
				short again_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;

				memset(again, 0x5A, (size_t)structure_size);
				if (!decode_network_game_message(again, (byte *)message + sizeof(message_header), &again_size,
					&again_type, &again_version, packet_class) || again_type != packet_type)
				{
					abort();
				}
			}
			free(again);
		}
		free(decoded);
	}

	return 0;
}

/* (NET_FUZZ_SEED_DIR set: a message of each type, encoded from a structure
of a pattern, written there as seeds for the fuzzer) */
void net_fuzz_write_seed(char const *folder, char const *name, void const *data, unsigned long size);

int net_fuzz_checks(void)
{
	struct data_packet_group_definition *group = &data_0030aa68.group;
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	short type;
	int failures = 0;

	initialize_network_game_packets();
	for (type = 0; type < group->packet_type_count; type++)
	{
		if (getenv("NET_FUZZ_LOG") && group->packets[type].definition &&
			group->packets[type].definition->size != fuzz_structure_sizes[type])
		{
			__builtin_fprintf(stderr, "type %d: %s decodes %ld bytes into a %ld-byte structure\n", type,
				group->packets[type].definition->name, (long)group->packets[type].definition->size,
				fuzz_structure_sizes[type]);
		}
	}
	/* every type encoded from, and decoded into, a structure of its own size
(the pregame keep alive's definition holds 4 bytes, its structure 2:
network_messages.c) */
	for (type = 0; type < group->packet_type_count; type++)
	{
		long structure_size = fuzz_structure_sizes[type];
		byte *structure = __builtin_malloc((size_t)structure_size);
		byte *decoded = __builtin_malloc((size_t)structure_size);
		byte *message;

		__builtin_memset(structure, 0, (size_t)structure_size);
		structure[0] = 0x2A;
		message = create_network_game_message(type, structure, (short)structure_size);
		if (message)
		{
			short size = (short)(GET_MESSAGE_SIZE(*(message_header *)message) - sizeof(message_header));
			short decoded_type = 0;
			short version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
			boolean decoded_ok = decode_network_game_message(decoded, message + sizeof(message_header), &size,
				&decoded_type, &version, group->packets[type].packet_class);

			if (!decoded_ok || decoded_type != type || decoded[0] != 0x2A)
			{
				__builtin_printf("FAIL type %d does not decode as encoded\n", type);
				failures++;
			}
		}
		__builtin_free(structure);
		__builtin_free(decoded);
	}
	__builtin_printf("%s every message type decodes into its own structure as encoded\n", failures ? "FAIL" : "PASS");
	for (type = 0; folder && type < group->packet_type_count; type++)
	{
		long structure_size;
		byte *structure;
		byte *message;
		long index;

		if (!group->packets[type].definition)
			continue;
		structure_size = fuzz_structure_sizes[type];
		structure = __builtin_malloc((size_t)structure_size);
		/* (small values, and none where a count is: within its field) */
		for (index = 0; index < structure_size; index++)
			structure[index] = (byte)(index % 3);
		if (type == _message_server_game_update || type == _message_client_game_update)
			__builtin_memset(structure, 0, (size_t)structure_size);
		message = create_network_game_message(type, structure, (short)structure_size);
		if (message)
		{
			char name[16];

			__builtin_snprintf(name, sizeof(name), "type%02d", type);
			net_fuzz_write_seed(folder, name, message + sizeof(message_header),
				GET_MESSAGE_SIZE(*(message_header *)message) - sizeof(message_header));
		}
		__builtin_free(structure);
	}
	return failures;
}
