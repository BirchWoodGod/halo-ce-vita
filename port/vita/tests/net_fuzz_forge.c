/*
NET_FUZZ_FORGE.C

A fuzz target for network Forge's messages (port/linux/game/
forge_net_protocol.c): a client's requests as the host takes them, the
host's updates, palette entries and lobby messages as a client takes them,
the lobby's answers as a host takes them, the replay window, the rate
limits, and a layout sent in pieces as a joiner reassembles it and reads it
through the map variant validator (forge_files.c's forge_file_decode). The
game's side (forge_net.inc) acts only on what these let through; the netns
cases (run_netns_online_test.sh forge) send the host junk of every kind.

net_fuzz_checks: known good and bad messages of each kind, and the
invariants of what passes (every float of a taken transform finite and in
the world, a taken palette path one a map variant may name, a taken pack's
name printable, a sequence never taken twice, a bucket never below zero).

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a request (struct forge_net_record), its first byte the palette's size
     (times 2) the host has
  1  an update, the same
  2  a palette entry
  3  a lobby message from the host (a hello or a refusal)
  4  a lobby message from a joiner (an answer)
  5  a layout piece (struct forge_net_layout_entry); a layout complete is
     read through forge_file_decode, of any map and of "bloodgulch"
  6  a sequence number (2 bytes) to the replay window
  7  a bucket's take: time, which bucket (the requests', the spawns'), the
     cost's bits
Built as net_fuzz_map_share.c is, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/forge_files.c"
#include "../../linux/game/forge_net_protocol.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int forge_transform_sane(
	struct forge_net_transform const *transform,
	int whole)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (!forge_net_finite(transform->position[axis]) || transform->position[axis] < -FORGE_NET_MAXIMUM_COORDINATE ||
			transform->position[axis] > FORGE_NET_MAXIMUM_COORDINATE || !forge_net_finite(transform->forward[axis]) ||
			(whole && !forge_net_finite(transform->up[axis])))
		{
			return 0;
		}
	}
	return 1;
}

/* what a taken request may hold */
static void fuzz_request(
	struct forge_net_record const *record,
	int palette_count)
{
	struct forge_net_record copy = *record;

	if (!forge_net_request_valid(&copy, palette_count))
		return;
	switch (copy.op)
	{
	case _forge_net_request_spawn:
		if (copy.tag >= palette_count)
			abort();
		/* (fall through: a transform) */
	case _forge_net_request_move:
	case _forge_net_request_release:
	case _forge_net_request_set:
	case _forge_net_request_duplicate:
		if (!forge_transform_sane(&copy.transform, 1))
			abort();
		forge_net_transform_square(&copy.transform);
		if (!forge_transform_sane(&copy.transform, 1))
			abort();
		break;
	case _forge_net_request_property:
		if (copy.aux >= FORGE_NET_PROPERTY_COUNT || (copy.step != 1 && copy.step != -1))
			abort();
		break;
	case _forge_net_request_monitor:
		if ((copy.flags & FORGE_NET_FLAG_MONITOR) && !forge_transform_sane(&copy.transform, 0))
			abort();
		break;
	case _forge_net_request_grab:
	case _forge_net_request_delete:
	case _forge_net_request_undo:
	case _forge_net_request_sync:
		break;
	default:
		/* (no other op is a request) */
		abort();
	}
}

static void fuzz_update(
	struct forge_net_record const *record,
	int palette_count)
{
	if (!forge_net_update_valid(record, palette_count))
		return;
	if (record->op < FORGE_NET_FIRST_UPDATE || record->op >= FORGE_NET_UPDATE_END)
		abort();
	if ((record->op == _forge_net_update_entry || record->op == _forge_net_update_link) &&
		(!forge_transform_sane(&record->transform, 1) || record->team < -1 || record->team > FORGE_NET_MAXIMUM_TEAM ||
			record->respawn_seconds > FORGE_NET_MAXIMUM_RESPAWN_SECONDS || record->ai_team >= FORGE_NET_AI_TEAM_COUNT))
	{
		abort();
	}
	if (record->op == _forge_net_update_entry && record->tag >= palette_count)
		abort();
	if (record->op == _forge_net_update_refuse && (record->aux == 0 || record->aux >= NUMBER_OF_FORGE_NET_REFUSALS))
		abort();
	forge_net_refusal_name(record->aux);
	forge_net_op_name(record->op);
}

static void fuzz_palette(
	struct forge_net_palette_entry const *entry)
{
	if (!forge_net_palette_entry_valid(entry))
		return;
	if (entry->index >= entry->total || entry->total > FORGE_NET_MAXIMUM_PALETTE || entry->length < 1 ||
		entry->length >= FORGE_NET_PATH_SIZE || entry->path[entry->length] != 0 ||
		!forge_file_path_valid(entry->path, entry->length))
	{
		abort();
	}
}

static void fuzz_lobby(
	struct forge_net_lobby_message const *message,
	int from_host)
{
	int index;

	if (!forge_net_lobby_valid(message, from_host))
		return;
	if (message->pack_count > FORGE_NET_MAXIMUM_PACKS || !memchr(message->title, 0, sizeof(message->title)))
		abort();
	for (index = 0; index < message->pack_count; index++)
	{
		struct forge_net_pack pack;

		if (!forge_net_lobby_pack(message, index, &pack) || !forge_net_pack_name_valid(pack.name))
			abort();
	}
	if (!from_host && (message->pack_count || message->title[0]))
		abort();
}

static void fuzz_layout(
	struct forge_net_layout_receiver *receiver,
	struct forge_net_layout_entry const *entry)
{
	static struct forge_file_layout layout;
	uint32_t received = receiver->received;

	if (!forge_net_layout_take(receiver, entry))
		return;
	if (receiver->received > receiver->total || receiver->total > FORGE_NET_LAYOUT_MAXIMUM_BYTES ||
		(entry->offset != 0 && receiver->received != received + entry->length))
	{
		abort();
	}
	if (receiver->complete)
	{
		char why[96];

		forge_file_decode(receiver->bytes, (int)receiver->total, NULL, &layout, why, (int)sizeof(why));
		forge_file_decode(receiver->bytes, (int)receiver->total, "bloodgulch", &layout, why, (int)sizeof(why));
	}
}

static void fuzz_record(
	int kind,
	unsigned char const *data,
	int size,
	struct forge_net_layout_receiver *receiver,
	struct forge_net_sequence *sequence,
	struct forge_net_bucket bucket[2])
{
	switch (kind)
	{
	case 0:
	case 1:
	{
		struct forge_net_record record;
		int palette_count = size > 0 ? data[0] * 2 : 0;

		memset(&record, 0, sizeof(record));
		if (size > 1)
			memcpy(&record, data + 1, (size_t)(size - 1 < (int)sizeof(record) ? size - 1 : (int)sizeof(record)));
		if (kind == 0)
			fuzz_request(&record, palette_count);
		else
			fuzz_update(&record, palette_count);
		break;
	}
	case 2:
	{
		struct forge_net_palette_entry entry;

		memset(&entry, 0, sizeof(entry));
		memcpy(&entry, data, (size_t)(size < (int)sizeof(entry) ? size : (int)sizeof(entry)));
		fuzz_palette(&entry);
		break;
	}
	case 3:
	case 4:
	{
		struct forge_net_lobby_message message;

		memset(&message, 0, sizeof(message));
		memcpy(&message, data, (size_t)(size < (int)sizeof(message) ? size : (int)sizeof(message)));
		fuzz_lobby(&message, kind == 3);
		break;
	}
	case 5:
	{
		struct forge_net_layout_entry entry;

		memset(&entry, 0, sizeof(entry));
		memcpy(&entry, data, (size_t)(size < (int)sizeof(entry) ? size : (int)sizeof(entry)));
		fuzz_layout(receiver, &entry);
		break;
	}
	case 6:
	{
		uint16_t value = 0;
		uint16_t last = sequence->last;
		int started = sequence->started;

		memcpy(&value, data, (size_t)(size < 2 ? size : 2));
		if (forge_net_sequence_accept(sequence, value) && started && (uint16_t)(value - last) == 0)
			abort();
		break;
	}
	case 7:
	{
		uint32_t field[3];
		int spawns;
		float rate, burst, cost;

		memset(field, 0, sizeof(field));
		memcpy(field, data, (size_t)(size < (int)sizeof(field) ? size : (int)sizeof(field)));
		/* (the game's two limits, a bucket each; a cost of the input's when
		it is a number from 0 to 16) */
		spawns = field[1] & 1;
		rate = spawns ? FORGE_NET_SPAWNS_PER_SECOND : FORGE_NET_REQUESTS_PER_SECOND;
		burst = spawns ? FORGE_NET_SPAWNS_BURST : FORGE_NET_REQUESTS_BURST;
		memcpy(&cost, &field[2], sizeof(cost));
		if (!forge_net_finite(cost) || !(cost >= 0.f) || cost > 16.f)
			cost = 1.f;
		forge_net_bucket_take(&bucket[spawns], field[0], rate, burst, cost);
		if (bucket[spawns].started && (bucket[spawns].tokens < 0.f || bucket[spawns].tokens > burst))
			abort();
		break;
	}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct forge_net_layout_receiver receiver;
	struct forge_net_sequence sequence;
	struct forge_net_bucket bucket[2];
	const unsigned char *cursor = data, *end = data + size;
	int count = 0;

	forge_net_layout_reset(&receiver);
	memset(&sequence, 0, sizeof(sequence));
	memset(bucket, 0, sizeof(bucket));
	while (count++ < 256 && end - cursor >= 3)
	{
		int kind = cursor[0] % 8;
		int length = cursor[1] | cursor[2] << 8;

		cursor += 3;
		if (length > end - cursor)
			length = (int)(end - cursor);
		fuzz_record(kind, cursor, length, &receiver, &sequence, bucket);
		cursor += length;
	}
	return 0;
}

/* ---------- the checks */

/* (net_fuzz_main.c's) */
void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size);

static int failures;

#define CHECK(condition, what) \
	do { if (!(condition)) { printf("FAIL forge: %s\n", what); failures++; } } while (0)

static void good_transform(
	struct forge_net_transform *transform)
{
	memset(transform, 0, sizeof(*transform));
	transform->position[0] = 60.f;
	transform->position[1] = -100.f;
	transform->position[2] = 1.6f;
	transform->forward[0] = 1.f;
	transform->up[2] = 1.f;
}

static void record_init(
	struct forge_net_record *record,
	int op)
{
	memset(record, 0, sizeof(*record));
	record->op = (uint8_t)op;
	record->tag = FORGE_NET_NO_TAG;
	record->player = FORGE_NET_NO_PLAYER;
	record->object = FORGE_NET_NO_OBJECT;
	good_transform(&record->transform);
}

/* a seed for the fuzzer: records of each kind */
static void seed_write(
	char const *folder)
{
	unsigned char buffer[4096];
	int at = 0;
	struct forge_net_record record;
	struct forge_net_palette_entry entry;
	struct forge_net_lobby_message hello;
	uint32_t recipe[2] = { 0x12345678, 0x9ABCDEF0 };
	unsigned char layout_bytes[FORGE_FILE_MAXIMUM_SIZE];
	static struct forge_file_layout layout;
	struct forge_net_layout_entry piece;
	int size, result;

#define SEED_ADD(kind, pointer, length) \
	do { buffer[at++] = (unsigned char)(kind); buffer[at++] = (unsigned char)((length) & 0xFF); \
		buffer[at++] = (unsigned char)((length) >> 8); memcpy(buffer + at, (pointer), (size_t)(length)); at += (length); } while (0)

	record_init(&record, _forge_net_request_spawn);
	record.tag = 3;
	{
		unsigned char with_count[1 + sizeof(record)];

		with_count[0] = 20;
		memcpy(with_count + 1, &record, sizeof(record));
		SEED_ADD(0, with_count, (int)sizeof(with_count));
		record_init(&record, _forge_net_update_entry);
		record.id = 7;
		record.tag = 3;
		record.object = 0x12340005;
		record.flags = FORGE_NET_FLAG_NETWORKED;
		memcpy(with_count + 1, &record, sizeof(record));
		SEED_ADD(1, with_count, (int)sizeof(with_count));
	}
	memset(&entry, 0, sizeof(entry));
	entry.total = 4;
	entry.index = 1;
	entry.group_tag = 0x76656869;
	entry.flags = FORGE_NET_PALETTE_PLACEABLE;
	strcpy(entry.path, "vehicles\\warthog\\warthog");
	entry.length = (uint8_t)strlen(entry.path);
	SEED_ADD(2, &entry, (int)sizeof(entry));
	memset(&hello, 0, sizeof(hello));
	hello.command = _forge_net_lobby_hello;
	hello.version = FORGE_NET_VERSION;
	hello.session = 99;
	forge_net_lobby_set_pack(&hello, 0, recipe, "covenant_vehicles");
	hello.pack_count = 1;
	strcpy(hello.title, "Gulch Fort");
	SEED_ADD(3, &hello, (int)sizeof(hello));
	net_fuzz_write_seed(folder, "seed-messages", buffer, (unsigned long)at);
	/* (a layout in pieces) */
	at = 0;
	memset(&layout, 0, sizeof(layout));
	strcpy(layout.map_name, "bloodgulch");
	strcpy(layout.title, "Gulch Fort");
	layout.path_count = 1;
	layout.paths[0].group_tag = 0x76656869;
	strcpy(layout.paths[0].path, "vehicles\\warthog\\warthog");
	layout.record_count = 1;
	layout.records[0].count = 1;
	layout.records[0].position[0] = 60.f;
	size = forge_file_encode(&layout, layout_bytes, (int)sizeof(layout_bytes), &result);
	if (size > 0 && forge_net_layout_piece(layout_bytes, size, 1, 0, &piece))
		SEED_ADD(5, &piece, (int)sizeof(piece));
	net_fuzz_write_seed(folder, "seed-layout", buffer, (unsigned long)at);
#undef SEED_ADD
}

int net_fuzz_checks(void)
{
	struct forge_net_record record;
	struct forge_net_palette_entry entry;
	struct forge_net_lobby_message message;
	struct forge_net_sequence sequence;
	struct forge_net_bucket bucket;
	static struct forge_net_layout_receiver receiver;
	static struct forge_file_layout layout;
	static unsigned char bytes[FORGE_FILE_MAXIMUM_SIZE];
	uint32_t recipe[2] = { 1, 2 };
	uint32_t nan_bits = 0x7FC00000u, infinity_bits = 0x7F800000u;
	int size, result, index, pieces;

	/* requests */
	record_init(&record, _forge_net_request_spawn);
	record.tag = 3;
	CHECK(forge_net_request_valid(&record, 10), "a spawn taken");
	CHECK(!forge_net_request_valid(&record, 3), "a spawn of a tag past the palette refused");
	record.tag = FORGE_NET_NO_TAG;
	CHECK(!forge_net_request_valid(&record, 512), "a spawn of no tag refused");
	record.tag = 3;
	memcpy(&record.transform.position[1], &nan_bits, 4);
	CHECK(!forge_net_request_valid(&record, 10), "a spawn at a NaN refused");
	record.transform.position[1] = 5001.f;
	CHECK(!forge_net_request_valid(&record, 10), "a spawn past the world's bounds refused");
	record.transform.position[1] = -100.f;
	memcpy(&record.transform.up[2], &infinity_bits, 4);
	CHECK(!forge_net_request_valid(&record, 10), "a spawn facing an infinity refused");
	good_transform(&record.transform);
	record.transform.up[0] = 1.f;
	record.transform.up[2] = 0.f;
	CHECK(!forge_net_request_valid(&record, 10), "a spawn whose forward and up are not square refused");
	good_transform(&record.transform);
	record.transform.forward[0] = 3.f;
	CHECK(!forge_net_request_valid(&record, 10), "a spawn whose forward is not a direction refused");
	good_transform(&record.transform);
	record.flags = FORGE_NET_FLAG_MAP_OBJECT;
	CHECK(!forge_net_request_valid(&record, 10), "a spawn of a map object refused");
	record_init(&record, _forge_net_request_property);
	record.aux = 2;
	record.step = 1;
	CHECK(forge_net_request_valid(&record, 0), "a property step taken");
	record.step = 2;
	CHECK(!forge_net_request_valid(&record, 0), "a property step of 2 refused");
	record.step = -1;
	record.aux = FORGE_NET_PROPERTY_COUNT;
	CHECK(!forge_net_request_valid(&record, 0), "a property past the rows refused");
	record_init(&record, _forge_net_request_undo);
	record.aux = 2;
	CHECK(!forge_net_request_valid(&record, 0), "an undo of another kind refused");
	record_init(&record, _forge_net_request_monitor);
	CHECK(forge_net_request_valid(&record, 0), "a Spartan taken");
	record.flags = FORGE_NET_FLAG_MONITOR;
	CHECK(forge_net_request_valid(&record, 0), "a monitor taken");
	memcpy(&record.transform.forward[0], &nan_bits, 4);
	CHECK(!forge_net_request_valid(&record, 0), "a monitor looking at a NaN refused");
	record_init(&record, _forge_net_update_entry);
	record.id = 1;
	record.tag = 0;
	CHECK(!forge_net_request_valid(&record, 10), "an update sent as a request refused");
	record_init(&record, 200);
	CHECK(!forge_net_request_valid(&record, 10), "an op that is none refused");
	record_init(&record, _forge_net_request_grab);
	record.flags = 0x80;
	CHECK(!forge_net_request_valid(&record, 10), "a grab with unknown flags refused");

	/* updates */
	record_init(&record, _forge_net_update_entry);
	record.id = 5;
	record.tag = 2;
	record.object = (int32_t)0xE2CE0035u;
	record.flags = FORGE_NET_FLAG_NETWORKED;
	record.team = -1;
	CHECK(forge_net_update_valid(&record, 3), "an entry taken (an object index with its identifier's high bit)");
	record.object = FORGE_NET_NO_OBJECT;
	CHECK(!forge_net_update_valid(&record, 3), "a networked entry with no object refused");
	record.object = 4;
	record.team = 8;
	CHECK(!forge_net_update_valid(&record, 3), "an entry of team 8 refused");
	record.team = 1;
	record.respawn_seconds = 601;
	CHECK(!forge_net_update_valid(&record, 3), "an entry respawning past 600 s refused");
	record.respawn_seconds = 30;
	record.ai_team = 2;
	CHECK(!forge_net_update_valid(&record, 3), "an AI team on an object that is no character refused");
	record.flags |= FORGE_NET_FLAG_AI;
	CHECK(forge_net_update_valid(&record, 3), "a character's AI team taken");
	record.ai_team = FORGE_NET_AI_TEAM_COUNT;
	CHECK(!forge_net_update_valid(&record, 3), "an AI team past the teams refused");
	record.ai_team = 0;
	record.tag = 3;
	CHECK(!forge_net_update_valid(&record, 3), "an entry of a tag past the palette refused");
	record.tag = 1;
	record.id = 0;
	CHECK(!forge_net_update_valid(&record, 3), "an entry of id 0 refused");
	record_init(&record, _forge_net_update_refuse);
	record.aux = _forge_net_refuse_budget;
	CHECK(forge_net_update_valid(&record, 0), "a refusal taken");
	record.aux = NUMBER_OF_FORGE_NET_REFUSALS;
	CHECK(!forge_net_update_valid(&record, 0), "a refusal past the refusals refused");
	record_init(&record, _forge_net_update_grant);
	record.id = 3;
	record.flags = FORGE_NET_FLAG_MAP_OBJECT;
	CHECK(!forge_net_update_valid(&record, 0), "a map object's grant with no object refused");
	record.object = 0x10002;
	CHECK(forge_net_update_valid(&record, 0), "a map object's grant taken");
	record_init(&record, _forge_net_request_spawn);
	CHECK(!forge_net_update_valid(&record, 10), "a request sent as an update refused");

	/* palette entries */
	memset(&entry, 0, sizeof(entry));
	entry.total = 4;
	entry.index = 3;
	entry.group_tag = 0x7363656E;
	entry.flags = FORGE_NET_PALETTE_PLACEABLE;
	strcpy(entry.path, "scenery\\rocks\\boulder\\boulder");
	entry.length = (uint8_t)strlen(entry.path);
	CHECK(forge_net_palette_entry_valid(&entry), "a palette entry taken");
	entry.index = 4;
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette entry past its total refused");
	entry.index = 0;
	entry.path[3] = 'A';
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette path with a capital refused");
	strcpy(entry.path, "scenery\\..\\boulder");
	entry.length = (uint8_t)strlen(entry.path);
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette path with .. refused");
	strcpy(entry.path, "scenery\\rocks\\boulder\\boulder");
	entry.length = (uint8_t)(strlen(entry.path) + 3);
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette path with zeros inside its length refused");
	entry.length = (uint8_t)(strlen(entry.path) - 2);
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette path with bytes after its length refused");
	entry.length = (uint8_t)strlen(entry.path);
	entry.group_tag = 0x73636E72;
	CHECK(!forge_net_palette_entry_valid(&entry), "a scenario's group refused");
	entry.group_tag = 0x7363656E;
	entry.flags = 0;
	CHECK(!forge_net_palette_entry_valid(&entry), "a pack's netcode tag of scenery refused");
	entry.group_tag = 0x62697064;
	CHECK(forge_net_palette_entry_valid(&entry), "a pack's biped taken for the translation");
	entry.total = FORGE_NET_MAXIMUM_PALETTE + 1;
	CHECK(!forge_net_palette_entry_valid(&entry), "a palette past its most refused");

	/* lobby messages */
	memset(&message, 0, sizeof(message));
	message.command = _forge_net_lobby_hello;
	message.version = FORGE_NET_VERSION;
	CHECK(forge_net_lobby_set_pack(&message, 0, recipe, "covenant_ai"), "a pack written");
	message.pack_count = 1;
	strcpy(message.title, "Gulch Fort");
	CHECK(forge_net_lobby_valid(&message, 1), "a hello taken");
	CHECK(!forge_net_lobby_valid(&message, 0), "a hello from a joiner refused");
	message.pack_count = 2;
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello counting a pack it does not name refused");
	message.pack_count = 9;
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello of 9 packs refused");
	message.pack_count = 1;
	message.title[31] = 'x';
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello whose title does not end refused");
	message.title[31] = 0;
	message.title[12] = 'x';
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello with bytes after its title refused");
	message.title[12] = 0;
	message.title[2] = 0x01;
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello whose title has a control character refused");
	strcpy(message.title, "Gulch Fort");
	message.packs[8] = 'A';
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello whose pack's name has a capital refused");
	message.packs[8] = 'c';
	message.packs[FORGE_NET_PACK_BYTES + 3] = 1;
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello with bytes in a pack it does not count refused");
	message.packs[FORGE_NET_PACK_BYTES + 3] = 0;
	message.capabilities = 1;
	CHECK(!forge_net_lobby_valid(&message, 1), "a hello with a joiner's capabilities refused");
	memset(&message, 0, sizeof(message));
	message.command = _forge_net_lobby_answer;
	message.version = FORGE_NET_VERSION;
	message.capabilities = FORGE_NET_CAPABILITY_FORGE;
	message.packs_ready = 0x5;
	CHECK(forge_net_lobby_valid(&message, 0), "an answer taken");
	message.packs_ready = 0x100;
	CHECK(!forge_net_lobby_valid(&message, 0), "an answer of a ninth pack refused");
	message.packs_ready = 1;
	message.capabilities = 0x80;
	CHECK(!forge_net_lobby_valid(&message, 0), "an answer with unknown capabilities refused");
	message.capabilities = 1;
	message.pack_count = 1;
	CHECK(!forge_net_lobby_valid(&message, 0), "an answer naming packs refused");
	message.pack_count = 0;
	message.version = 0;
	CHECK(!forge_net_lobby_valid(&message, 0), "an answer of version 0 refused");
	memset(&message, 0, sizeof(message));
	message.command = _forge_net_lobby_refused;
	message.version = 1;
	CHECK(!forge_net_lobby_valid(&message, 1), "a refusal of no reason refused");
	message.reason = _forge_net_refusal_full;
	CHECK(forge_net_lobby_valid(&message, 1), "a refusal taken");
	/* (a joiner's packs still building: their bits and its percent) */
	memset(&message, 0, sizeof(message));
	message.command = _forge_net_lobby_building;
	message.version = FORGE_NET_VERSION;
	message.packs_ready = 0x3;
	message.flags = 40;
	CHECK(forge_net_lobby_valid(&message, 0), "a joiner's build said taken");
	CHECK(!forge_net_lobby_valid(&message, 1), "a build said by the host refused");
	message.flags = 101;
	CHECK(!forge_net_lobby_valid(&message, 0), "a build past 100% refused");
	message.flags = 100;
	message.packs_ready = 0x100;
	CHECK(!forge_net_lobby_valid(&message, 0), "a build of a ninth pack refused");
	message.packs_ready = 0;
	message.capabilities = 1;
	CHECK(!forge_net_lobby_valid(&message, 0), "a build said with capabilities refused");
	message.capabilities = 0;
	message.pack_count = 1;
	CHECK(!forge_net_lobby_valid(&message, 0), "a build said naming packs refused");
	message.pack_count = 0;
	strcpy(message.title, "x");
	CHECK(!forge_net_lobby_valid(&message, 0), "a build said with a title refused");
	message.title[0] = 0;
	message.reason = _forge_net_refusal_full;
	CHECK(!forge_net_lobby_valid(&message, 0), "a build said with a reason refused");
	message.reason = 0;
	CHECK(forge_net_lobby_valid(&message, 0), "all built said taken");

	/* the replay window */
	memset(&sequence, 0, sizeof(sequence));
	CHECK(forge_net_sequence_accept(&sequence, 65530), "the first sequence taken");
	CHECK(!forge_net_sequence_accept(&sequence, 65530), "a sequence again refused");
	CHECK(forge_net_sequence_accept(&sequence, 2), "a sequence past the wrap taken");
	CHECK(!forge_net_sequence_accept(&sequence, 65531), "an older sequence refused");
	CHECK(!forge_net_sequence_accept(&sequence, (uint16_t)(2 + 0x8000)), "a sequence half the numbers ahead refused");

	/* the rate limits */
	memset(&bucket, 0, sizeof(bucket));
	for (index = 0; index < 12; index++)
		CHECK(forge_net_bucket_take(&bucket, 1000, 4.f, 12.f, 1.f), "a burst's spawn taken");
	CHECK(!forge_net_bucket_take(&bucket, 1000, 4.f, 12.f, 1.f), "the spawn past the burst refused");
	CHECK(forge_net_bucket_take(&bucket, 1250, 4.f, 12.f, 1.f), "a spawn a quarter second after taken");
	CHECK(!forge_net_bucket_take(&bucket, 1250, 4.f, 12.f, 1.f), "a second spawn that quarter second refused");

	/* a layout in pieces: a map variant's bytes, reassembled, read */
	memset(&layout, 0, sizeof(layout));
	strcpy(layout.map_name, "bloodgulch");
	strcpy(layout.title, "Gulch Fort");
	layout.path_count = 1;
	layout.paths[0].group_tag = 0x76656869;
	strcpy(layout.paths[0].path, "vehicles\\warthog\\warthog");
	layout.record_count = FORGE_FILE_MAXIMUM_RECORDS;
	for (index = 0; index < layout.record_count; index++)
	{
		layout.records[index].count = 1;
		layout.records[index].position[0] = (float)index;
	}
	size = forge_file_encode(&layout, bytes, (int)sizeof(bytes), &result);
	CHECK(size > 0, "the layout written");
	pieces = forge_net_layout_piece_count(size);
	CHECK(pieces == (size + FORGE_NET_LAYOUT_CHUNK_BYTES - 1) / FORGE_NET_LAYOUT_CHUNK_BYTES, "the layout's pieces counted");
	forge_net_layout_reset(&receiver);
	for (index = 0; index < pieces; index++)
	{
		struct forge_net_layout_entry piece;

		forge_net_layout_piece(bytes, size, 7, index, &piece);
		/* (one out of order: dropped, the next taken) */
		if (index == 1)
		{
			struct forge_net_layout_entry later;

			forge_net_layout_piece(bytes, size, 7, 2, &later);
			CHECK(!forge_net_layout_take(&receiver, &later), "a piece out of order dropped");
		}
		CHECK(forge_net_layout_take(&receiver, &piece), "a piece taken");
	}
	CHECK(receiver.complete && receiver.total == (uint32_t)size, "the layout complete");
	{
		static struct forge_file_layout read;
		char why[96];

		CHECK(forge_file_decode(receiver.bytes, (int)receiver.total, "bloodgulch", &read, why, (int)sizeof(why)) == _forge_file_ok,
			"the reassembled layout read");
		CHECK(forge_file_decode(receiver.bytes, (int)receiver.total, "chillout", &read, why, (int)sizeof(why)) != _forge_file_ok,
			"the reassembled layout refused for another map");
		receiver.bytes[100] ^= 0x40;
		CHECK(forge_file_decode(receiver.bytes, (int)receiver.total, "bloodgulch", &read, why, (int)sizeof(why)) != _forge_file_ok,
			"a layout damaged in flight refused");
	}
	{
		struct forge_net_layout_entry piece;

		memset(&piece, 0, sizeof(piece));
		piece.total = FORGE_NET_LAYOUT_MAXIMUM_BYTES + 1;
		piece.length = 10;
		CHECK(!forge_net_layout_take(&receiver, &piece), "a layout past 64 KB refused");
		piece.total = 1000;
		piece.length = FORGE_NET_LAYOUT_CHUNK_BYTES + 1;
		CHECK(!forge_net_layout_take(&receiver, &piece), "a piece past its room refused");
		piece.length = 100;
		piece.offset = 950;
		CHECK(!forge_net_layout_take(&receiver, &piece), "a piece past its layout's end refused");
	}
	if (getenv("NET_FUZZ_SEEDS"))
		seed_write(getenv("NET_FUZZ_SEEDS"));
	printf("%s forge: %d checks failed\n", failures ? "FAIL" : "PASS", failures);
	return failures;
}
