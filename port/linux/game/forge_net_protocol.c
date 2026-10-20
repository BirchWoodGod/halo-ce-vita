/*
FORGE_NET_PROTOCOL.C

Network Forge's rules (forge_net_protocol.h): every field of every message
another machine sends checked here before the game takes it, the replay
window, the rate limits, and a joiner's layout as its pieces arrive. Pure C
with no game dependency (the unit test and the fuzzer build it alone), but
for forge_files.c's path rules, which a map variant's paths follow too.
Nothing here trusts a byte: each float's bits are looked at before it is a
float, each count bounded before it is used, each text checked to its end.
*/

#include "forge_net_protocol.h"
#include "forge_files.h"

#include <string.h>

/* ---------- constants */

/* the groups whose objects the netcode makes on every machine (a pack's
tags of them are in the palette for the clients' translation): bipd vehi
weap eqip */
static uint32_t const forge_net_networked_groups[] =
{
	0x62697064, 0x76656869, 0x77656170, 0x65716970,
};
#define FORGE_NET_GROUP_ACTOR_VARIANT 0x61637476

/* ---------- private code */

static int forge_net_networked_group(
	uint32_t group_tag)
{
	unsigned index;

	for (index = 0; index < sizeof(forge_net_networked_groups) / sizeof(forge_net_networked_groups[0]); index++)
	{
		if (forge_net_networked_groups[index] == group_tag)
			return 1;
	}
	return 0;
}

static float forge_net_dot(
	float const a[3],
	float const b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int forge_net_direction_valid(
	float const vector[3])
{
	float length;

	if (!forge_net_finite(vector[0]) || !forge_net_finite(vector[1]) || !forge_net_finite(vector[2]))
		return 0;
	/* (each part within one, before the square is taken: no overflow) */
	if (vector[0] < -1.5f || vector[0] > 1.5f || vector[1] < -1.5f || vector[1] > 1.5f || vector[2] < -1.5f ||
		vector[2] > 1.5f)
	{
		return 0;
	}
	length = forge_net_dot(vector, vector);
	return length > (1.f - FORGE_NET_UNIT_TOLERANCE) * (1.f - FORGE_NET_UNIT_TOLERANCE) &&
		length < (1.f + FORGE_NET_UNIT_TOLERANCE) * (1.f + FORGE_NET_UNIT_TOLERANCE);
}

static int forge_net_position_valid(
	float const position[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (!forge_net_finite(position[axis]) || position[axis] < -FORGE_NET_MAXIMUM_COORDINATE ||
			position[axis] > FORGE_NET_MAXIMUM_COORDINATE)
		{
			return 0;
		}
	}
	return 1;
}

/* a placed object's properties as an entry or link carries them */
static int forge_net_properties_valid(
	struct forge_net_record const *record)
{
	if (record->team < -1 || record->team > FORGE_NET_MAXIMUM_TEAM)
		return 0;
	if (record->record_flags & ~FORGE_NET_RECORD_FLAGS_KNOWN)
		return 0;
	if (record->respawn_seconds > FORGE_NET_MAXIMUM_RESPAWN_SECONDS)
		return 0;
	if (record->ai_team >= FORGE_NET_AI_TEAM_COUNT)
		return 0;
	/* (a character's settings on a character only) */
	if (!(record->flags & FORGE_NET_FLAG_AI) && (record->ai_team != 0 || (record->record_flags & 0x02)))
		return 0;
	return 1;
}

static int forge_net_player_valid(
	uint8_t player)
{
	/* (an absolute player index: the netcode's players are below 255) */
	return player != FORGE_NET_NO_PLAYER;
}

/* ---------- public code */

int forge_net_finite(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return ((bits >> 23) & 0xFF) != 0xFF;
}

int forge_net_view_valid(
	struct forge_net_transform const *transform)
{
	return transform && forge_net_position_valid(transform->position) && forge_net_direction_valid(transform->forward);
}

int forge_net_transform_valid(
	struct forge_net_transform const *transform)
{
	float dot;

	if (!forge_net_view_valid(transform) || !forge_net_direction_valid(transform->up))
		return 0;
	dot = forge_net_dot(transform->forward, transform->up);
	return dot > -FORGE_NET_SQUARE_TOLERANCE && dot < FORGE_NET_SQUARE_TOLERANCE;
}

void forge_net_transform_square(
	struct forge_net_transform *transform)
{
	float dot = forge_net_dot(transform->forward, transform->up);
	float length;
	int axis;

	/* (forward made unit length, up made square to it and unit length:
	both were within the tolerances, so neither is ever near zero) */
	length = forge_net_dot(transform->forward, transform->forward);
	if (length > 0.f)
	{
		float scale = 1.f;

		/* (a Newton step of 1/sqrt: no libm in the fuzz build, and the
		vector is already near unit length) */
		scale = scale * (1.5f - 0.5f * length * scale * scale);
		scale = scale * (1.5f - 0.5f * length * scale * scale);
		for (axis = 0; axis < 3; axis++)
			transform->forward[axis] *= scale;
	}
	dot = forge_net_dot(transform->forward, transform->up);
	for (axis = 0; axis < 3; axis++)
		transform->up[axis] -= dot * transform->forward[axis];
	length = forge_net_dot(transform->up, transform->up);
	if (length > 0.f)
	{
		float scale = 1.f;

		scale = scale * (1.5f - 0.5f * length * scale * scale);
		scale = scale * (1.5f - 0.5f * length * scale * scale);
		for (axis = 0; axis < 3; axis++)
			transform->up[axis] *= scale;
	}
}

int forge_net_request_valid(
	struct forge_net_record const *record,
	int palette_count)
{
	if (!record)
		return 0;
	if (record->flags & ~FORGE_NET_FLAGS_KNOWN)
		return 0;
	switch (record->op)
	{
	case _forge_net_request_spawn:
		if (record->tag == FORGE_NET_NO_TAG || (int)record->tag >= palette_count)
			return 0;
		if (record->flags & ~FORGE_NET_FLAG_HOLD)
			return 0;
		return forge_net_transform_valid(&record->transform);
	case _forge_net_request_grab:
	case _forge_net_request_delete:
		return (record->flags & ~FORGE_NET_FLAG_MAP_OBJECT) == 0;
	case _forge_net_request_move:
	case _forge_net_request_release:
	case _forge_net_request_set:
		if (record->flags & ~FORGE_NET_FLAG_MAP_OBJECT)
			return 0;
		return forge_net_transform_valid(&record->transform);
	case _forge_net_request_duplicate:
		if (record->flags & ~(FORGE_NET_FLAG_MAP_OBJECT | FORGE_NET_FLAG_HOLD))
			return 0;
		return forge_net_transform_valid(&record->transform);
	case _forge_net_request_property:
		if (record->flags & ~FORGE_NET_FLAG_MAP_OBJECT)
			return 0;
		return record->aux < FORGE_NET_PROPERTY_COUNT && (record->step == 1 || record->step == -1);
	case _forge_net_request_undo:
		return record->flags == 0 && record->aux <= 1;
	case _forge_net_request_monitor:
		if (record->flags & ~FORGE_NET_FLAG_MONITOR)
			return 0;
		/* (the Spartan's: no view; the monitor's: where it flies) */
		return !(record->flags & FORGE_NET_FLAG_MONITOR) || forge_net_view_valid(&record->transform);
	case _forge_net_request_sync:
		return record->flags == 0;
	default:
		return 0;
	}
}

int forge_net_update_valid(
	struct forge_net_record const *record,
	int palette_count)
{
	if (!record)
		return 0;
	if (record->flags & ~FORGE_NET_FLAGS_KNOWN)
		return 0;
	switch (record->op)
	{
	case _forge_net_update_entry:
		if (record->id == 0 || record->tag == FORGE_NET_NO_TAG || (int)record->tag >= palette_count)
			return 0;
		if (record->flags & ~(FORGE_NET_FLAG_AI | FORGE_NET_FLAG_NETWORKED))
			return 0;
		/* (an object index is a datum's: its identifier in the high half,
		any but none's; the client checks it is one of its objects) */
		if ((record->flags & FORGE_NET_FLAG_NETWORKED) && record->object == FORGE_NET_NO_OBJECT)
			return 0;
		return forge_net_properties_valid(record) && forge_net_transform_valid(&record->transform);
	case _forge_net_update_link:
		if (record->id == 0 || record->value >= 256)
			return 0;
		if (record->flags & ~(FORGE_NET_FLAG_AI | FORGE_NET_FLAG_NETWORKED))
			return 0;
		if ((record->flags & FORGE_NET_FLAG_NETWORKED) && record->object == FORGE_NET_NO_OBJECT)
			return 0;
		return forge_net_properties_valid(record) && forge_net_transform_valid(&record->transform);
	case _forge_net_update_remove:
	case _forge_net_update_released:
		return record->id != 0 && record->flags == 0;
	case _forge_net_update_transform:
		return record->id != 0 && record->flags == 0 && forge_net_transform_valid(&record->transform);
	case _forge_net_update_hold:
		return record->id != 0 && record->flags == 0;
	case _forge_net_update_grant:
		/* (a map object's: its index too) */
		if (record->flags & ~FORGE_NET_FLAG_MAP_OBJECT)
			return 0;
		if ((record->flags & FORGE_NET_FLAG_MAP_OBJECT) && record->object == FORGE_NET_NO_OBJECT)
			return 0;
		return record->id != 0 && record->aux < NUMBER_OF_FORGE_NET_GRANTS;
	case _forge_net_update_refuse:
		return record->flags == 0 && record->aux > _forge_net_refuse_none && record->aux < NUMBER_OF_FORGE_NET_REFUSALS;
	case _forge_net_update_monitor:
		if (!forge_net_player_valid(record->player) || (record->flags & ~FORGE_NET_FLAG_MONITOR))
			return 0;
		return !(record->flags & FORGE_NET_FLAG_MONITOR) || forge_net_view_valid(&record->transform);
	case _forge_net_update_synced:
		return record->flags == 0 && record->value <= 256;
	case _forge_net_update_round:
		return record->flags == 0;
	default:
		return 0;
	}
}

int forge_net_palette_entry_valid(
	struct forge_net_palette_entry const *entry)
{
	int index;

	if (!entry || entry->total == 0 || entry->total > FORGE_NET_MAXIMUM_PALETTE || entry->index >= entry->total)
		return 0;
	if (entry->flags & ~FORGE_NET_PALETTE_FLAGS_KNOWN || entry->reserved != 0)
		return 0;
	if (entry->flags & FORGE_NET_PALETTE_PLACEABLE)
	{
		if (!forge_file_group_allowed(entry->group_tag))
			return 0;
	}
	else if (!forge_net_networked_group(entry->group_tag))
	{
		return 0;
	}
	if (entry->length < 1 || entry->length > FORGE_FILE_PATH_MAXIMUM_LENGTH || entry->length >= FORGE_NET_PATH_SIZE)
		return 0;
	for (index = entry->length; index < FORGE_NET_PATH_SIZE; index++)
	{
		if (entry->path[index])
			return 0;
	}
	if (memchr(entry->path, 0, entry->length))
		return 0;
	return forge_file_path_valid(entry->path, entry->length);
}

int forge_net_pack_name_valid(
	char const *name)
{
	int length = 0;

	if (!name)
		return 0;
	for (; length <= FORGE_NET_PACK_NAME_SIZE && name[length]; length++)
	{
		char c = name[length];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ' ' || c == '-'))
			return 0;
	}
	return length >= 1 && length <= FORGE_NET_PACK_NAME_SIZE;
}

static uint32_t forge_net_read32(
	uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void forge_net_write32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

int forge_net_lobby_pack(
	struct forge_net_lobby_message const *message,
	int index,
	struct forge_net_pack *pack)
{
	uint8_t const *bytes;

	if (!message || !pack || index < 0 || index >= message->pack_count || index >= FORGE_NET_MAXIMUM_PACKS)
		return 0;
	bytes = message->packs + index * FORGE_NET_PACK_BYTES;
	pack->recipe[0] = forge_net_read32(bytes);
	pack->recipe[1] = forge_net_read32(bytes + 4);
	memcpy(pack->name, bytes + 8, FORGE_NET_PACK_NAME_SIZE);
	pack->name[FORGE_NET_PACK_NAME_SIZE] = 0;
	return 1;
}

int forge_net_lobby_set_pack(
	struct forge_net_lobby_message *message,
	int index,
	uint32_t const recipe[2],
	char const *name)
{
	uint8_t *bytes;
	size_t length;

	if (!message || index < 0 || index >= FORGE_NET_MAXIMUM_PACKS || !forge_net_pack_name_valid(name))
		return 0;
	bytes = message->packs + index * FORGE_NET_PACK_BYTES;
	memset(bytes, 0, FORGE_NET_PACK_BYTES);
	forge_net_write32(bytes, recipe[0]);
	forge_net_write32(bytes + 4, recipe[1]);
	length = strlen(name);
	memcpy(bytes + 8, name, length);
	return 1;
}

int forge_net_lobby_valid(
	struct forge_net_lobby_message const *message,
	int from_host)
{
	int index;

	if (!message || message->version < 1)
		return 0;
	if (from_host)
	{
		if (message->command != _forge_net_lobby_hello && message->command != _forge_net_lobby_refused)
			return 0;
		if (message->capabilities != 0 || message->packs_ready != 0)
			return 0;
	}
	else
	{
		if (message->command != _forge_net_lobby_answer)
			return 0;
		if (message->capabilities & ~FORGE_NET_CAPABILITIES_KNOWN || message->flags != 0)
			return 0;
	}
	if (message->reason >= NUMBER_OF_FORGE_NET_LOBBY_REFUSALS)
		return 0;
	if (message->command == _forge_net_lobby_refused ? message->reason == _forge_net_refusal_none :
		message->reason != _forge_net_refusal_none)
	{
		return 0;
	}
	if (message->flags & ~FORGE_NET_SESSION_FLAGS_KNOWN)
		return 0;
	if (message->pack_count > FORGE_NET_MAXIMUM_PACKS)
		return 0;
	/* (an answer names no pack: its packs_ready are bits of the hello's) */
	if (!from_host && (message->pack_count != 0 || message->packs_ready >> FORGE_NET_MAXIMUM_PACKS))
		return 0;
	/* (each pack named, zeros after the name; the rest all zeros) */
	for (index = 0; index < FORGE_NET_MAXIMUM_PACKS; index++)
	{
		uint8_t const *bytes = message->packs + index * FORGE_NET_PACK_BYTES;
		int at;

		if (index >= message->pack_count)
		{
			for (at = 0; at < FORGE_NET_PACK_BYTES; at++)
			{
				if (bytes[at])
					return 0;
			}
			continue;
		}
		{
			char name[FORGE_NET_PACK_NAME_SIZE + 1];
			int length;

			memcpy(name, bytes + 8, FORGE_NET_PACK_NAME_SIZE);
			name[FORGE_NET_PACK_NAME_SIZE] = 0;
			if (!forge_net_pack_name_valid(name))
				return 0;
			length = (int)strlen(name);
			for (at = 8 + length; at < FORGE_NET_PACK_BYTES; at++)
			{
				if (bytes[at])
					return 0;
			}
		}
	}
	/* (the title: UTF-8 ended within its room, zeros after it) */
	if (!memchr(message->title, 0, FORGE_NET_TITLE_SIZE))
		return 0;
	if (message->title[0] && !forge_file_text_valid(message->title, FORGE_NET_TITLE_SIZE))
		return 0;
	for (index = (int)strlen(message->title); index < FORGE_NET_TITLE_SIZE; index++)
	{
		if (message->title[index])
			return 0;
	}
	if (!from_host && message->title[0])
		return 0;
	return 1;
}

int forge_net_sequence_accept(
	struct forge_net_sequence *sequence,
	uint16_t value)
{
	uint16_t ahead;

	if (!sequence)
		return 0;
	if (!sequence->started)
	{
		sequence->started = 1;
		sequence->last = value;
		return 1;
	}
	ahead = (uint16_t)(value - sequence->last);
	if (ahead == 0 || ahead >= 0x8000)
		return 0;
	sequence->last = value;
	return 1;
}

int forge_net_bucket_take(
	struct forge_net_bucket *bucket,
	uint32_t time,
	float rate,
	float burst,
	float cost)
{
	if (!bucket || !(rate > 0.f) || !(burst > 0.f) || !(cost >= 0.f))
		return 0;
	if (!bucket->started)
	{
		bucket->started = 1;
		bucket->tokens = burst;
		bucket->time = time;
	}
	else
	{
		uint32_t elapsed = time - bucket->time;

		/* (a clock that went back, or a long silence: no more than full) */
		if (elapsed > 600000u)
			elapsed = 600000u;
		bucket->tokens += (float)elapsed * rate / 1000.f;
		if (bucket->tokens > burst)
			bucket->tokens = burst;
		bucket->time = time;
	}
	if (bucket->tokens < cost)
		return 0;
	bucket->tokens -= cost;
	return 1;
}

void forge_net_layout_reset(
	struct forge_net_layout_receiver *receiver)
{
	if (!receiver)
		return;
	receiver->total = 0;
	receiver->received = 0;
	receiver->serial = 0;
	receiver->started = 0;
	receiver->complete = 0;
}

int forge_net_layout_take(
	struct forge_net_layout_receiver *receiver,
	struct forge_net_layout_entry const *entry)
{
	if (!receiver || !entry)
		return 0;
	if (entry->total < FORGE_FILE_HEADER_SIZE || entry->total > FORGE_NET_LAYOUT_MAXIMUM_BYTES ||
		entry->length == 0 || entry->length > FORGE_NET_LAYOUT_CHUNK_BYTES || entry->offset > entry->total ||
		entry->length > entry->total - entry->offset)
	{
		return 0;
	}
	/* (a new layout: its first piece starts it over) */
	if (entry->offset == 0 && (!receiver->started || receiver->complete || entry->serial != receiver->serial))
	{
		receiver->started = 1;
		receiver->complete = 0;
		receiver->serial = entry->serial;
		receiver->total = entry->total;
		receiver->received = 0;
	}
	if (!receiver->started || receiver->complete || entry->serial != receiver->serial ||
		entry->total != receiver->total || entry->offset != receiver->received)
	{
		return 0;
	}
	memcpy(receiver->bytes + entry->offset, entry->data, entry->length);
	receiver->received += entry->length;
	if (receiver->received == receiver->total)
		receiver->complete = 1;
	return 1;
}

int forge_net_layout_piece_count(
	int size)
{
	if (size <= 0 || size > FORGE_NET_LAYOUT_MAXIMUM_BYTES)
		return 0;
	return (size + FORGE_NET_LAYOUT_CHUNK_BYTES - 1) / FORGE_NET_LAYOUT_CHUNK_BYTES;
}

int forge_net_layout_piece(
	unsigned char const *bytes,
	int size,
	uint16_t serial,
	int index,
	struct forge_net_layout_entry *entry)
{
	int offset;
	int length;

	if (!bytes || !entry || index < 0 || index >= forge_net_layout_piece_count(size))
		return 0;
	offset = index * FORGE_NET_LAYOUT_CHUNK_BYTES;
	length = size - offset < FORGE_NET_LAYOUT_CHUNK_BYTES ? size - offset : FORGE_NET_LAYOUT_CHUNK_BYTES;
	memset(entry, 0, sizeof(*entry));
	entry->total = (uint32_t)size;
	entry->offset = (uint32_t)offset;
	entry->length = (uint16_t)length;
	entry->serial = serial;
	memcpy(entry->data, bytes + offset, (size_t)length);
	return 1;
}

char const *forge_net_refusal_name(
	int refusal)
{
	static char const *const names[NUMBER_OF_FORGE_NET_REFUSALS] =
	{
		"none", "budget", "AI budget", "held by another", "not allowed", "outside the map", "cannot be placed",
		"cannot be copied", "host only", "rate limited", "gone", "the map's scenery", "nothing to undo",
		"nothing to redo", "not held", "undone", "redone", "undo: gone", "redo: gone", "undo: budget full",
		"redo: budget full",
	};

	return refusal >= 0 && refusal < NUMBER_OF_FORGE_NET_REFUSALS ? names[refusal] : "?";
}

char const *forge_net_op_name(
	int op)
{
	static char const *const requests[] =
	{
		"none", "spawn", "grab", "move", "release", "set", "delete", "duplicate", "property", "undo", "monitor", "sync",
	};
	static char const *const updates[] =
	{
		"entry", "remove", "transform", "hold", "grant", "refuse", "monitor", "synced", "link", "round", "released",
	};

	if (op >= 0 && op < (int)(sizeof(requests) / sizeof(requests[0])))
		return requests[op];
	if (op >= FORGE_NET_FIRST_UPDATE && op < FORGE_NET_UPDATE_END)
		return updates[op - FORGE_NET_FIRST_UPDATE];
	return "?";
}
