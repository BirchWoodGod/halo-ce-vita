/*
FORGE_NET.INC

Network Forge, included by forge.c (its statics shared): several players
forging one map together, the host authoritative (the rules and every
message's checks: forge_net_protocol.h).

The lobby: a host of a Forge game (forge_net_host_begin: the Forge lobby's
live game, the settings panel's Start Forge on a network, the network test's
"host:<map>:forge") says hello to each machine that joins it, again each
second until it answers; a machine that has not answered in
FORGE_NET_HELLO_MILLISECONDS (an older build drops the message unread) or
answers another version is refused from this game alone (rejected as "too
old", which an older build says as a failed join), as is a machine past
FORGE_NET_MAXIMUM_PLAYERS players ("the game is full"). A machine that
answers is a Forge game's: as the host's map loads it starts Forge as the
host does (the monitor, the browser, the host's packs - built from its own
maps, a pack it cannot build named on its screen -), but it edits nothing
itself.

The game: each edit a client's monitor makes is a request to the host
(forge_net_requests), which checks it (forge_net_protocol.c's checks of
every field, then the game's: the object there and one Forge edits - not a
player's unit, not the map's own scenery, which nothing would show the
others -, not held by another player, the tag one of the host's palette, the
budget, inside the map's world, the sender's sequence and rate) and makes it
as the host's own monitor would, as that machine (its own hold and its own
undo history: forge_net_act_begin swaps them in), saying a refusal back in
a code the client words in its own language. What the host's layout is
(forge_placed: every placed object, its tag, place, properties and holder)
goes to every client as it changes (forge_net_host_broadcast compares it
with what it sent last): an entry per object, its removal, and where the
objects the netcode does not move (scenery, machines, controls, light
fixtures, the generated building blocks) are; the netcode's own objects
(units, vehicles, weapons, equipment) are made and moved on every machine
by network_objects.c at the host's indices, so the clients find them there.
A client mirrors the layout in its own forge_placed (its HUD, browser counts,
properties window and a saved copy read it as the host's are read), making
its own copies of the objects the netcode does not make, by tag path.

A client that has loaded the map asks for the host's state (sync): the
palette (each tag by group and path: the host's allow list), the layout as
a map variant's bytes (forge_variant_capture, read on the client through
forge_file_decode with its own map expected: what a late joiner gets is a
file checked as any), each record's id, object and holder, and the end of
it; from then on the changes. Each machine's monitor flies its own camera
and tells the host where (five times a second); the host tells everyone
where each player's monitor is, drawn on the others' screens with the
player's name, the units of monitors hidden (and unhurt, on the host).
A client holding an object moves its own copy and tells the host where
(fifteen times a second); the netcode leaves the place of an object a
client holds to it (forge_net_client_holds).

Host only: NEW ROUND (a network game's round: the layout placed again where
the editor put it, the netcode's own map reset being a local game's), CLEAR
AI, ALL AI; a client's menu says so. Saving: the host saves its map
variant; a client saves a copy of what it mirrors, locally. Undo and redo:
each player their own steps, through the host.

HALO_FORGE_NET=0 (test builds) makes a machine one that does not play
network Forge (it ignores the hello, as an older build), for the tests.
*/

#include "forge_net_protocol.h"
#include "network_distributed.h"
#include "network_anticheat.h"
#include "network_wire.h"
#include "networking/network_messages.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_protocol.h"
#include "render/render_cameras_internal.h"

/* (network_client_manager.c's) */
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);
/* (main.c's) */
int halo_main_menu_loaded(void);
/* (p2p_lobby.c's, system_link_shortcut.c's) */
void p2p_lobby_set_never_listed(int never);
extern volatile int halo_forge_net_hosting;
void platform_show_message(char const *title, char const *text);

/* ---------- constants */

enum
{
	/* the records, palette entries and layout pieces a message holds */
	FORGE_NET_RECORDS_PER_MESSAGE = 60,
	FORGE_NET_PALETTE_PER_MESSAGE = 27,
	FORGE_NET_LAYOUT_PER_MESSAGE = 4,
	/* the records waiting to go to one destination at most */
	FORGE_NET_OUTGOING_RECORDS = 1024,
	/* actors: the host's own (0) and the remote machines' slots */
	FORGE_NET_ACTORS = FORGE_NET_MAXIMUM_MACHINES,
	/* how often a client says where its monitor is, and the host where
	every monitor is (milliseconds); where an object it holds is */
	FORGE_NET_MONITOR_MILLISECONDS = 200,
	FORGE_NET_MOVE_MILLISECONDS = 66,
	/* an entry's placed spot sent again at most this often while it
	settles; an object's place (the netcode's not) at most this often */
	FORGE_NET_ENTRY_MILLISECONDS = 250,
	FORGE_NET_TRANSFORM_MILLISECONDS = 66,
	/* a grant waited for this long before the client gives up on it */
	FORGE_NET_GRANT_MILLISECONDS = 4000,
	/* a client asks for the host's state again this often until it has it */
	FORGE_NET_SYNC_RETRY_MILLISECONDS = 5000,
	/* the drops logged at most this often, a machine */
	FORGE_NET_DROP_LOG_MILLISECONDS = 1000,
	/* the players tracked (absolute indices) */
	FORGE_NET_PLAYERS = 64,
};

/* the distributed kinds (network_distributed.h) */
#define FORGE_NET_KIND_REQUESTS _distributed_message_forge_requests
#define FORGE_NET_KIND_UPDATES _distributed_message_forge_updates
#define FORGE_NET_KIND_PALETTE _distributed_message_forge_palette
#define FORGE_NET_KIND_LAYOUT _distributed_message_forge_layout

/* (network_server_manager.c's server states: its game under way) */
#define FORGE_NET_SERVER_STATE_INGAME 1

/* the Bungie messages' classes (network_messages.c's packet table) */
#define FORGE_NET_CLASS_CLIENT_PREGAME 3
#define FORGE_NET_CLASS_SERVER_PREGAME 2

/* ---------- structures */

/* a remote machine as the host keeps it in game: its hold, its undo
history, what it sent and what was dropped of it, its monitor */
struct forge_net_actor
{
	boolean used;
	long machine_index;
	/* what forge_actor_swap swaps with forge_globals while acting for it */
	long held_object_index;
	real held_distance;
	real_vector3d held_forward;
	real_vector3d held_up;
	struct forge_transform held_from;
	boolean held_spawned;
	struct forge_undo undo[FORGE_UNDO_DEPTH];
	short undo_count;
	short undo_cursor;
	/* where its object held is (its last move), held there each frame */
	struct forge_net_transform held_transform;
	boolean held_transform_set;
	/* its requests */
	struct forge_net_sequence sequence;
	struct forge_net_bucket requests;
	struct forge_net_bucket spawns;
	struct forge_net_drops drops;
	unsigned long drops_logged;
	struct forge_net_drops drops_said;
	/* the host's tick its malformed or replayed requests were last counted
	against its machine at (network_anticheat.c: once a tick) */
	long anticheat_tick;
	/* its state asked for (sent at the end of the frame), and since when it
	has it (the changes then go to it too); when last asked */
	boolean sync_pending;
	boolean synced;
	unsigned long sync_time;
	boolean sync_asked;
	/* its monitor: on, where; its player's unit hidden, and whether that
	unit could take damage before */
	boolean monitor;
	struct forge_net_transform view;
	long hidden_unit;
	boolean unit_could_take_damage;
	/* the last move of its held object sent on (not the netcode's) */
	unsigned long move_sent;
};

/* what the host last sent of a placed object */
struct forge_net_shadow
{
	long id;
	struct forge_net_record entry;
	unsigned long entry_time;
	struct forge_net_transform transform;
	boolean transform_sent;
	unsigned long transform_time;
	boolean seen;
};

/* a player's monitor as this machine knows it (a client's from the host) */
struct forge_net_monitor
{
	boolean monitor;
	struct forge_net_transform view;
	unsigned long time;
	long hidden_unit;
};

/* records waiting to go */
struct forge_net_outgoing
{
	struct forge_net_record records[FORGE_NET_OUTGOING_RECORDS];
	short count;
};

struct forge_net_records_message
{
	struct distributed_message_header header;
	struct forge_net_record records[FORGE_NET_RECORDS_PER_MESSAGE];
};

struct forge_net_palette_message
{
	struct distributed_message_header header;
	struct forge_net_palette_entry entries[FORGE_NET_PALETTE_PER_MESSAGE];
};

struct forge_net_layout_message
{
	struct distributed_message_header header;
	struct forge_net_layout_entry entries[FORGE_NET_LAYOUT_PER_MESSAGE];
};

typedef char forge_net_records_message_fits[sizeof(struct forge_net_records_message) <= 0xFFF ? 1 : -1];
typedef char forge_net_palette_message_fits[sizeof(struct forge_net_palette_message) <= 0xFFF ? 1 : -1];
typedef char forge_net_layout_message_fits[sizeof(struct forge_net_layout_message) <= 0xFFF ? 1 : -1];

/* ---------- globals */

static struct
{
	/* --- the lobby, the host's: a Forge game hosted (hellos sent), its
	session, each machine's answer */
	boolean hosting;
	uint32_t session;
	struct
	{
		boolean known;
		long machine_index;
		boolean answered;
		boolean refused;
		unsigned long joined_time;
		unsigned long hello_time;
		uint32_t packs_ready;
	} machines[FORGE_NET_MAXIMUM_MACHINES];
	/* --- the lobby, a joiner's: the host said Forge (its hello, kept) */
	boolean client_session;
	uint32_t client_session_id;
	/* (a joiner) its packs and blocks to be prebuilt in the lobby */
	boolean prebuild_wanted;
	char host_title[FORGE_NET_TITLE_SIZE];
	char missing_packs[FORGE_LINE_SIZE];
	boolean refused_said;
	/* --- the game */
	/* the host: the remote machines, what it sent last, its palette (tags
	in a stable order: the browser's, then the packs' netcode tags) */
	struct forge_net_actor actors[FORGE_NET_ACTORS];
	struct forge_net_shadow shadow[FORGE_MAXIMUM_OBJECTS];
	short shadow_count;
	long net_tags[FORGE_NET_MAXIMUM_PALETTE];
	unsigned char net_flags[FORGE_NET_MAXIMUM_PALETTE];
	short net_count;
	short net_count_sent;
	uint16_t layout_serial;
	unsigned long monitors_sent;
	struct forge_net_outgoing broadcast;
	/* (the records of one machine: grants, refusals, its sync) */
	struct forge_net_outgoing direct;
	long direct_machine;
	/* acting for a remote machine (its notices not the host's) */
	struct forge_net_actor *acting;
	/* the client: its requests' sequence, the host's palette mapped to its
	own tags (NONE: not here), the host's state had */
	uint16_t sequence;
	struct forge_net_outgoing requests;
	short host_palette_count;
	long host_tags[FORGE_NET_MAXIMUM_PALETTE];
	long host_tag_indices[FORGE_NET_MAXIMUM_PALETTE];
	unsigned long host_groups[FORGE_NET_MAXIMUM_PALETTE];
	unsigned char host_flags[FORGE_NET_MAXIMUM_PALETTE];
	boolean host_palette_known[FORGE_NET_MAXIMUM_PALETTE];
	short missing_tags;
	boolean synced;
	unsigned long sync_sent;
	struct forge_net_layout_receiver layout;
	struct forge_file_layout layout_decoded;
	boolean layout_ready;
	struct forge_net_record links[FORGE_FILE_MAXIMUM_RECORDS];
	short link_count;
	/* (a client) the object it asked for and was granted (its id) or waits
	for (the request's sequence) */
	long grant_id;
	long grant_object;
	boolean grant_pending;
	uint16_t grant_sequence;
	unsigned long grant_time;
	short grant_kind;
	unsigned long move_sent;
	struct forge_net_transform move_last;
	unsigned long monitor_sent;
	boolean monitor_said;
	/* every player's monitor (absolute index) */
	struct forge_net_monitor monitors[FORGE_NET_PLAYERS];
	/* (tests) counts */
	long requests_taken;
	long requests_refused;
	long requests_dropped;
	long updates_taken;
	long updates_dropped;
	long rounds;
} forge_net;

/* ---------- the roles */

static boolean forge_net_capable(
	void)
{
#ifndef HALO_RELEASE
	char const *setting = getenv("HALO_FORGE_NET");

	if (setting && setting[0] == '0')
		return FALSE;
#endif
	return TRUE;
}

/* the host of a network Forge game in progress */
static boolean forge_net_host(
	void)
{
	return forge_globals.game_active && forge_net.hosting && game_connection() == _game_connection_network_server &&
		!network_wire_crossplay();
}

/* a client of one */
static boolean forge_net_client(
	void)
{
	return forge_globals.game_active && forge_net.client_session && game_connection() == _game_connection_network_client &&
		!network_wire_crossplay();
}

boolean forge_net_hosting(
	void)
{
	return forge_net.hosting;
}

boolean forge_net_client_session(
	void)
{
	return forge_net.client_session;
}

/* (a client) the host's map variant's title */
static char const *forge_net_host_title(
	void)
{
	return forge_net.host_title;
}

/* ---------- small helpers */

static void forge_net_transform_from(
	struct forge_net_transform *result,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	result->position[0] = position->x;
	result->position[1] = position->y;
	result->position[2] = position->z;
	result->forward[0] = forward->i;
	result->forward[1] = forward->j;
	result->forward[2] = forward->k;
	result->up[0] = up ? up->i : 0.f;
	result->up[1] = up ? up->j : 0.f;
	result->up[2] = up ? up->k : 1.f;
}

static void forge_net_transform_to(
	struct forge_net_transform const *transform,
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up)
{
	position->x = transform->position[0];
	position->y = transform->position[1];
	position->z = transform->position[2];
	forward->i = transform->forward[0];
	forward->j = transform->forward[1];
	forward->k = transform->forward[2];
	if (up)
	{
		up->i = transform->up[0];
		up->j = transform->up[1];
		up->k = transform->up[2];
	}
}

static void forge_net_transform_of_object(
	long object_index,
	struct forge_net_transform *transform)
{
	struct forge_transform current;

	forge_transform_get(object_index, &current);
	forge_net_transform_from(transform, &current.position, &current.forward, &current.up);
}

static boolean forge_net_transforms_near(
	struct forge_net_transform const *a,
	struct forge_net_transform const *b)
{
	int axis;
	real difference = 0.f;

	for (axis = 0; axis < 3; axis++)
	{
		real d = a->position[axis] - b->position[axis];

		difference += d * d;
	}
	if (difference > 0.0001f)
		return FALSE;
	return a->forward[0] * b->forward[0] + a->forward[1] * b->forward[1] + a->forward[2] * b->forward[2] > 0.99999f &&
		a->up[0] * b->up[0] + a->up[1] * b->up[1] + a->up[2] * b->up[2] > 0.99999f;
}

/* whether a point is inside the structure's world (with Forge's margin) */
static boolean forge_net_inside_world(
	struct forge_net_transform const *transform)
{
	struct structure_bsp *structure = global_structure_bsp_get();

	if (!structure)
		return TRUE;
	return transform->position[0] >= structure->world_bounds.x0 - FORGE_WORLD_BOUNDS_MARGIN &&
		transform->position[0] <= structure->world_bounds.x1 + FORGE_WORLD_BOUNDS_MARGIN &&
		transform->position[1] >= structure->world_bounds.y0 - FORGE_WORLD_BOUNDS_MARGIN &&
		transform->position[1] <= structure->world_bounds.y1 + FORGE_WORLD_BOUNDS_MARGIN &&
		transform->position[2] >= structure->world_bounds.z0 - FORGE_WORLD_BOUNDS_MARGIN &&
		transform->position[2] <= structure->world_bounds.z1 + FORGE_WORLD_BOUNDS_MARGIN;
}

/* whether an object is one the netcode makes on every machine */
static boolean forge_net_object_networked(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	return object && (FLAG(object->object.type) &
		(_object_mask_biped | _object_mask_vehicle | _object_mask_weapon | _object_mask_equipment)) != 0;
}

/* whether a tag's objects are (an actor variant's: its unit's) */
static boolean forge_net_tag_networked(
	long tag_index)
{
	unsigned long group = tag_get_group_tag(tag_index);

	return group == VEHICLE_DEFINITION_TAG || group == WEAPON_DEFINITION_TAG || group == EQUIPMENT_DEFINITION_TAG ||
		group == BIPED_DEFINITION_TAG || group == ACTOR_VARIANT_DEFINITION_TAG;
}

/* a player's name (UTF-8) */
static void forge_net_player_name(
	short player_absolute,
	char *name,
	int size)
{
	struct player_datum *player = distributed_player(player_absolute);
	int length = 0;
	int index;

	if (!player)
	{
		snprintf(name, (size_t)size, "?");
		return;
	}
	for (index = 0; index < 12 && player->name[index] && length < size - 4; index++)
	{
		unsigned long code = (unsigned short)player->name[index];

		if (code < 0x20)
			continue;
		if (code < 0x80)
			name[length++] = (char)code;
		else if (code < 0x800)
		{
			name[length++] = (char)(0xC0 | (code >> 6));
			name[length++] = (char)(0x80 | (code & 0x3F));
		}
		else
		{
			name[length++] = (char)(0xE0 | (code >> 12));
			name[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
			name[length++] = (char)(0x80 | (code & 0x3F));
		}
	}
	name[length] = 0;
}

/* the first player of a machine (absolute index), NONE */
static short forge_net_machine_player(
	long machine_index)
{
	short index;

	for (index = 0; index < FORGE_NET_PLAYERS; index++)
	{
		struct player_datum *player = distributed_player(index);

		if (player && player->network_player_data.machine_index == machine_index)
			return index;
	}
	return NONE;
}

/* this machine's first local player (absolute index), NONE */
static short forge_net_local_player(
	void)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);

	return player_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
}

static void forge_net_outgoing_add(
	struct forge_net_outgoing *outgoing,
	struct forge_net_record const *record)
{
	if (outgoing->count < FORGE_NET_OUTGOING_RECORDS)
		outgoing->records[outgoing->count++] = *record;
}

/* records sent, in messages of FORGE_NET_RECORDS_PER_MESSAGE: to every
client (machine NONE), to one, or to the host */
static void forge_net_send_records(
	struct forge_net_outgoing *outgoing,
	long machine_index,
	boolean to_host)
{
	struct forge_net_records_message message;
	short at = 0;

	while (at < outgoing->count)
	{
		short count = (short)MIN(outgoing->count - at, FORGE_NET_RECORDS_PER_MESSAGE);
		word size = (word)(sizeof(message.header) + count * sizeof(struct forge_net_record));

		csmemcpy(message.records, &outgoing->records[at], count * sizeof(struct forge_net_record));
		if (to_host)
			distributed_send(&message, FORGE_NET_KIND_REQUESTS, count, size, _distributed_to_host_reliably);
		else if (machine_index == NONE)
			distributed_send(&message, FORGE_NET_KIND_UPDATES, count, size, _distributed_to_clients_reliably);
		else
			distributed_send_to_machine_reliably(machine_index, &message, FORGE_NET_KIND_UPDATES, count, size);
		at = (short)(at + count);
	}
	outgoing->count = 0;
}

/* ---------- the lobby */

static void forge_net_machines_forget(
	void)
{
	csmemset(forge_net.machines, 0, sizeof(forge_net.machines));
}

/* (the Forge lobby, the settings panel, the network test) the game this
machine hosts is a Forge game of map: its joiners told so; the session's
packs and map variant those forge_game_start sets for a local one. FALSE
(why said) if it cannot be */
boolean forge_net_host_begin(
	char const *map_path,
	char const *variant_file,
	char *why,
	int why_size)
{
	short map_index = NONE;
	short index;

	if (why && why_size > 0)
		why[0] = 0;
	for (index = 0; map_path && index < forge_map_count(); index++)
	{
		if (!_stricmp(forge_maps[index].path, map_path))
			map_index = index;
	}
	if (map_index == NONE)
	{
		if (why)
			snprintf(why, (size_t)why_size, "%s", T("Forge plays the multiplayer maps only."));
		return FALSE;
	}
	if (network_wire_crossplay())
	{
		if (why)
			snprintf(why, (size_t)why_size, "%s", T("Forge is played with Vitas only (WHO CAN JOIN: Vitas)."));
		return FALSE;
	}
	forge_globals.variant_pending = FALSE;
	forge_globals.variant_title[0] = 0;
	forge_globals.variant_file[0] = 0;
	forge_packs_defaults();
	forge_globals.packs_session = forge_globals.packs_enabled;
	if (variant_file && variant_file[0])
	{
		char map_name[FORGE_FILE_MAP_NAME_SIZE];
		char reason[FORGE_STORE_WHY_SIZE];

		if (!forge_map_short_name(forge_maps[map_index].path, map_name, sizeof(map_name)) ||
			!forge_store_load(map_name, variant_file, &forge_variant_layout, reason, sizeof(reason)))
		{
			if (why)
				snprintf(why, (size_t)why_size, "%s", reason);
			return FALSE;
		}
		snprintf(forge_globals.variant_title, sizeof(forge_globals.variant_title), "%s", forge_variant_layout.title);
		snprintf(forge_globals.variant_file, sizeof(forge_globals.variant_file), "%s", variant_file);
		forge_globals.variant_pending = TRUE;
		for (index = 0; index < forge_variant_layout.pack_count; index++)
		{
			short pack = forge_pack_of_name(forge_variant_layout.packs[index].name);

			if (pack != NONE)
				forge_globals.packs_session |= 1UL << pack;
		}
	}
	csstrncpy(forge_globals.game_map, forge_maps[map_index].path, sizeof(forge_globals.game_map) - 1);
	forge_globals.game_map[sizeof(forge_globals.game_map) - 1] = 0;
	forge_globals.game_requested = TRUE;
	if (!forge_net.hosting)
	{
		forge_net_machines_forget();
		forge_net.session = (uint32_t)system_milliseconds() * 2654435761u ^ 0x5F0F6E7u;
		if (!forge_net.session)
			forge_net.session = 1;
	}
	forge_net.hosting = TRUE;
	forge_net.client_session = FALSE;
	/* (the owner's: a Forge game is the lobby's only - never listed online,
	whatever Visibility says; the LAN's and ad hoc's own lists show it) */
	p2p_lobby_set_never_listed(1);
	halo_forge_net_hosting = 1;
	platform_log("forge net: hosting a Forge game of %s%s%s (session %08x): joiners are asked to play it", map_path,
		forge_globals.variant_title[0] ? ", map variant " : "", forge_globals.variant_title, (unsigned)forge_net.session);
	return TRUE;
}

/* the game hosted is a Forge game no more (the lobby closed, another game) */
void forge_net_host_end(
	void)
{
	if (!forge_net.hosting)
		return;
	forge_net.hosting = FALSE;
	forge_net_machines_forget();
	p2p_lobby_set_never_listed(0);
	halo_forge_net_hosting = 0;
	if (!forge_globals.game_active)
		forge_globals.game_requested = FALSE;
	platform_log("forge net: the Forge game is hosted no more");
}

/* the hello for a machine */
static void forge_net_hello_fill(
	struct forge_net_lobby_message *message,
	boolean in_progress)
{
	short pack;

	csmemset(message, 0, sizeof(*message));
	message->command = _forge_net_lobby_hello;
	message->version = FORGE_NET_VERSION;
	message->session = forge_net.session;
	message->flags = in_progress ? FORGE_NET_SESSION_IN_PROGRESS : 0;
	for (pack = 0; pack < FORGE_PACK_COUNT && message->pack_count < FORGE_NET_MAXIMUM_PACKS; pack++)
	{
		unsigned long long hash;
		uint32_t recipe[2];

		/* (the packs it brings that it can build: from its own maps) */
		if (!(forge_globals.packs_session & (1UL << pack)) || !scene_packs_recipe_available(forge_packs[pack].name, NULL, 0))
			continue;
		hash = scene_packs_recipe_hash(forge_packs[pack].name);
		recipe[0] = (uint32_t)(hash & 0xFFFFFFFFUL);
		recipe[1] = (uint32_t)(hash >> 32);
		if (forge_net_lobby_set_pack(message, message->pack_count, recipe, forge_packs[pack].name))
			message->pack_count++;
	}
	if (forge_globals.variant_title[0] && forge_file_text_valid(forge_globals.variant_title, FORGE_NET_TITLE_SIZE))
		snprintf(message->title, sizeof(message->title), "%s", forge_globals.variant_title);
}

static boolean forge_net_server_send(
	long machine_index,
	struct forge_net_lobby_message const *lobby)
{
	void *message = create_network_game_message(_message_server_forge, lobby, sizeof(*lobby));

	return message && network_game_server_port_send_to_machine(machine_index, message);
}

static void forge_net_refuse_machine(
	short slot,
	short reason,
	word rejection)
{
	struct forge_net_lobby_message refusal;
	long machine_index = forge_net.machines[slot].machine_index;

	csmemset(&refusal, 0, sizeof(refusal));
	refusal.command = _forge_net_lobby_refused;
	refusal.version = FORGE_NET_VERSION;
	refusal.reason = (uint16_t)reason;
	refusal.session = forge_net.session;
	forge_net_server_send(machine_index, &refusal);
	network_game_server_port_refuse_machine(machine_index, rejection);
	forge_net.machines[slot].refused = TRUE;
}

/* (each frame) the host's hellos: each joined machine asked, refused if it
does not answer in time or past the players' limit */
static void forge_net_host_lobby_update(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	long machines[FORGE_NET_MAXIMUM_MACHINES * 2];
	short count;
	short index;
	unsigned long now = system_milliseconds();
	boolean in_progress;
	short players = 0;

	if (!server)
	{
		/* (the game gone: its Forge with it) */
		forge_net_host_end();
		return;
	}
	/* (none until the host's own machine is known: never asked) */
	if (network_wire_crossplay() || network_game_client_get_local_machine_index() == NONE)
		return;
	count = network_game_server_port_joined_machines(machines, (short)NUMBEROF(machines));
	in_progress = network_game_server_get_state(server, NULL) == FORGE_NET_SERVER_STATE_INGAME;
	/* (machines gone: forgotten) */
	for (index = 0; index < FORGE_NET_MAXIMUM_MACHINES; index++)
	{
		short at;
		boolean still = FALSE;

		if (!forge_net.machines[index].known)
			continue;
		for (at = 0; at < count; at++)
			still |= machines[at] == forge_net.machines[index].machine_index;
		if (!still)
			csmemset(&forge_net.machines[index], 0, sizeof(forge_net.machines[index]));
	}
	/* (the players in the game now: past the most, the newest machine
	refused) */
	{
		struct network_game *game = network_game_get_game();
		long player;

		for (player = 0; game && player < (long)NUMBEROF(game->players); player++)
		{
			if (network_player_is_valid(&game->players[player]))
				players++;
		}
	}
	for (index = 0; index < count; index++)
	{
		long machine_index = machines[index];
		short slot;
		short free_slot = NONE;

		/* (the host's own machine, joined to its own game as a lobby's
		host is: never asked) */
		if (machine_index == network_game_client_get_local_machine_index())
			continue;

		for (slot = 0; slot < FORGE_NET_MAXIMUM_MACHINES; slot++)
		{
			if (forge_net.machines[slot].known && forge_net.machines[slot].machine_index == machine_index)
				break;
			if (!forge_net.machines[slot].known && free_slot == NONE)
				free_slot = slot;
		}
		if (slot == FORGE_NET_MAXIMUM_MACHINES)
		{
			if (free_slot == NONE)
			{
				/* (more machines than a Forge game takes) */
				network_game_server_port_refuse_machine(machine_index, _rejection_code_game_is_full);
				continue;
			}
			slot = free_slot;
			csmemset(&forge_net.machines[slot], 0, sizeof(forge_net.machines[slot]));
			forge_net.machines[slot].known = TRUE;
			forge_net.machines[slot].machine_index = machine_index;
			forge_net.machines[slot].joined_time = now;
			platform_log("forge net: machine %ld joined the Forge game: asked to play it", machine_index);
		}
		if (forge_net.machines[slot].refused)
			continue;
		if (players > FORGE_NET_MAXIMUM_PLAYERS && index == count - 1)
		{
			platform_log("forge net: machine %ld refused: a Forge game takes %d players", machine_index, FORGE_NET_MAXIMUM_PLAYERS);
			forge_net_refuse_machine(slot, _forge_net_refusal_full, _rejection_code_game_is_full);
			players--;
			continue;
		}
		if (forge_net.machines[slot].answered)
			continue;
		if (now - forge_net.machines[slot].joined_time >= FORGE_NET_HELLO_MILLISECONDS)
		{
			platform_log("forge net: machine %ld did not answer the Forge hello in %d ms (a build without network Forge): "
				"refused from the Forge game", machine_index, FORGE_NET_HELLO_MILLISECONDS);
			forge_net_refuse_machine(slot, _forge_net_refusal_incapable, _rejection_code_version_too_old);
			continue;
		}
		if (!forge_net.machines[slot].hello_time ||
			now - forge_net.machines[slot].hello_time >= FORGE_NET_HELLO_RESEND_MILLISECONDS)
		{
			struct forge_net_lobby_message hello;

			forge_net_hello_fill(&hello, in_progress);
			if (forge_net_server_send(machine_index, &hello))
				forge_net.machines[slot].hello_time = now;
		}
	}
}

void forge_net_server_handle_lobby_message(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size)
{
	struct forge_net_lobby_message answer;
	short packet_type = _message_client_forge;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	long machine_index = NONE;
	short slot;

	message_size -= sizeof(word);
	if (message_size <= 0 ||
		!decode_network_game_message(&answer, message + 1, &message_size, &packet_type, &packet_version,
			FORGE_NET_CLASS_CLIENT_PREGAME))
	{
		network_event("forge net: an answer that could not be decoded");
		return;
	}
	if (!forge_net.hosting || !forge_net_lobby_valid(&answer, FALSE))
	{
		network_event("forge net: an invalid or unasked answer");
		return;
	}
	network_game_server_get_client_machine(server, machine, &machine_index);
	for (slot = 0; slot < FORGE_NET_MAXIMUM_MACHINES; slot++)
	{
		if (forge_net.machines[slot].known && forge_net.machines[slot].machine_index == machine_index)
			break;
	}
	if (slot == FORGE_NET_MAXIMUM_MACHINES || forge_net.machines[slot].refused || forge_net.machines[slot].answered)
		return;
	if (answer.session != forge_net.session)
	{
		network_event("forge net: an answer to another session");
		return;
	}
	if (answer.version != FORGE_NET_VERSION || !(answer.capabilities & FORGE_NET_CAPABILITY_FORGE))
	{
		platform_log("forge net: machine %ld plays network Forge version %d (this one %d): refused from the Forge game",
			machine_index, (int)answer.version, FORGE_NET_VERSION);
		forge_net_refuse_machine(slot, answer.version != FORGE_NET_VERSION ? _forge_net_refusal_version :
			_forge_net_refusal_incapable, _rejection_code_version_too_old);
		return;
	}
	forge_net.machines[slot].answered = TRUE;
	forge_net.machines[slot].packs_ready = answer.packs_ready;
	platform_log("forge net: machine %ld plays the Forge game (packs ready %x)", machine_index, (unsigned)answer.packs_ready);
}

void forge_net_client_handle_lobby_message(
	struct network_game_client *client,
	word *message,
	short message_size)
{
	struct forge_net_lobby_message hello;
	struct forge_net_lobby_message answer;
	short packet_type = _message_server_forge;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	short index;

	message_size -= sizeof(word);
	if (message_size <= 0 ||
		!decode_network_game_message(&hello, message + 1, &message_size, &packet_type, &packet_version,
			FORGE_NET_CLASS_SERVER_PREGAME))
	{
		network_event("forge net: a host's message that could not be decoded");
		return;
	}
	/* (a machine that does not play it ignores it, as an older build; the
	host its own, joined to its own game) */
	if (!forge_net_capable() || forge_net.hosting)
		return;
	if (!forge_net_lobby_valid(&hello, TRUE))
	{
		network_event("forge net: an invalid host's message");
		return;
	}
	if (hello.command == _forge_net_lobby_refused)
	{
		static char const *const reasons[NUMBER_OF_FORGE_NET_LOBBY_REFUSALS] =
		{
			"", N_("This Forge game is another version of network Forge: update Halo CE to join it."),
			N_("This Forge game is full: it takes 8 players."), N_("This Forge game needs a newer Halo CE."),
		};

		platform_log("forge net: refused from the host's Forge game (%d)", (int)hello.reason);
		if (!forge_net.refused_said)
		{
			forge_net.refused_said = TRUE;
			platform_show_message(T("Halo: cannot join this Forge game"), T(reasons[hello.reason]));
		}
		forge_net.client_session = FALSE;
		return;
	}
	if (forge_net.client_session && forge_net.client_session_id == hello.session)
	{
		/* (asked again: the answer was lost, or is on its way) */
	}
	else
	{
		forge_net.client_session = TRUE;
		forge_net.client_session_id = hello.session;
		forge_net.refused_said = FALSE;
		forge_net.hosting = FALSE;
		snprintf(forge_net.host_title, sizeof(forge_net.host_title), "%s", hello.title);
		forge_globals.packs_session = 0;
		forge_net.missing_packs[0] = 0;
	}
	csmemset(&answer, 0, sizeof(answer));
	answer.command = _forge_net_lobby_answer;
	answer.version = FORGE_NET_VERSION;
	answer.capabilities = FORGE_NET_CAPABILITY_FORGE;
	answer.session = hello.session;
	/* (the host's packs: each this machine has a recipe of, the same one,
	brought - built from its own maps -; the others named, their objects
	not shown) */
	forge_globals.packs_session = 0;
	forge_net.missing_packs[0] = 0;
	for (index = 0; index < hello.pack_count; index++)
	{
		struct forge_net_pack pack;
		short ours;

		if (!forge_net_lobby_pack(&hello, index, &pack))
			continue;
		ours = forge_pack_of_name(pack.name);
		if (ours != NONE)
		{
			unsigned long long hash = scene_packs_recipe_hash(forge_packs[ours].name);

			if ((uint32_t)(hash & 0xFFFFFFFFUL) == pack.recipe[0] && (uint32_t)(hash >> 32) == pack.recipe[1] &&
				scene_packs_recipe_available(forge_packs[ours].name, NULL, 0))
			{
				forge_globals.packs_session |= 1UL << ours;
				answer.packs_ready |= 1u << index;
				continue;
			}
		}
		snprintf(forge_net.missing_packs + strlen(forge_net.missing_packs),
			sizeof(forge_net.missing_packs) - strlen(forge_net.missing_packs), "%s%s",
			forge_net.missing_packs[0] ? ", " : "", ours != NONE ? T(forge_packs[ours].label) : pack.name);
	}
	{
		void *reply = create_network_game_message(_message_client_forge, &answer, sizeof(answer));

		if (!reply || !network_game_client_send_to_server(client, reply))
			network_event("forge net: the answer could not be sent");
	}
	/* (the packs it brings, and the building blocks of the host's map,
	built meanwhile in the lobby, at the main menu's pace: what is not
	built yet when the map loads is built then - its load stops a prebuild's
	pauses, scene_packs_load_now -) */
	forge_net.prebuild_wanted = TRUE;
	platform_log("forge net: the host's game is a Forge game (session %08x%s%s): playing it, packs %lx%s%s",
		(unsigned)hello.session, hello.title[0] ? ", map variant " : "", hello.title, forge_globals.packs_session,
		forge_net.missing_packs[0] ? ", missing: " : "", forge_net.missing_packs);
}

/* (main.c, each frame) the lobby: the host's hellos; a joiner's session
gone with its game */
void forge_net_frame(
	void)
{
	if (forge_net.hosting)
		forge_net_host_lobby_update();
	if (forge_net.client_session && !global_network_game_client_get())
	{
		forge_net.client_session = FALSE;
		platform_log("forge net: the host's Forge game left");
	}
	/* (a joiner in the host's lobby: its packs and the map's blocks
	prebuilt, once the lobby's map is known) */
	if (forge_net.client_session && forge_net.prebuild_wanted && halo_main_menu_loaded())
	{
		struct network_game *game = network_game_get_game();
		char map_name[FORGE_FILE_MAP_NAME_SIZE];
		short index;

		if (game && game->map.name[0] && forge_map_short_name(game->map.name, map_name, sizeof(map_name)) &&
			forge_map_of_short(map_name) != NONE)
		{
			forge_net.prebuild_wanted = FALSE;
			for (index = 0; index < FORGE_PACK_COUNT; index++)
			{
				if (forge_globals.packs_session & (1UL << index))
					scene_packs_prebuild(forge_packs[index].name);
			}
			forge_blocks_prebuild(map_name);
			platform_log("forge net: the host's packs and %s's blocks prebuilt in its lobby", map_name);
		}
	}
}

/* ---------- the host: its palette */

/* the host's palette entry of a tag, NONE */
static short forge_net_tag_entry(
	long tag_index)
{
	short index;

	for (index = 0; index < forge_net.net_count; index++)
	{
		if (forge_net.net_tags[index] == tag_index)
			return index;
	}
	return NONE;
}

/* the palette grown by the browser's tags and the packs' netcode tags it
does not have yet (stable order: an entry never moves) */
static void forge_net_palette_update(
	void)
{
	static unsigned long const networked_groups[] =
	{
		BIPED_DEFINITION_TAG, VEHICLE_DEFINITION_TAG, WEAPON_DEFINITION_TAG, EQUIPMENT_DEFINITION_TAG,
	};
	short index;
	short group;

	forge_palette_ready();
	for (index = 0; index < forge_globals.palette_count && forge_net.net_count < FORGE_NET_MAXIMUM_PALETTE; index++)
	{
		long tag_index = forge_globals.palette[index].tag_index;
		char const *name = tag_get_name(tag_index);

		if (forge_net_tag_entry(tag_index) != NONE || !name || !forge_file_path_valid(name, (int)strlen(name)) ||
			!forge_file_group_allowed((unsigned int)tag_get_group_tag(tag_index)))
		{
			continue;
		}
		forge_net.net_tags[forge_net.net_count] = tag_index;
		forge_net.net_flags[forge_net.net_count] = FORGE_NET_PALETTE_PLACEABLE;
		forge_net.net_count++;
	}
	for (group = 0; group < (short)NUMBEROF(networked_groups); group++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, networked_groups[group]);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE && forge_net.net_count < FORGE_NET_MAXIMUM_PALETTE)
		{
			char const *name = tag_get_name(tag_index);

			if (tag_get_group_tag(tag_index) != networked_groups[group] || !scene_packs_owner(tag_index) ||
				forge_net_tag_entry(tag_index) != NONE || !name || !forge_file_path_valid(name, (int)strlen(name)))
			{
				continue;
			}
			forge_net.net_tags[forge_net.net_count] = tag_index;
			forge_net.net_flags[forge_net.net_count] = 0;
			forge_net.net_count++;
		}
	}
}

static void forge_net_palette_entry_fill(
	short index,
	struct forge_net_palette_entry *entry)
{
	long tag_index = forge_net.net_tags[index];
	char const *name = tag_get_name(tag_index);

	csmemset(entry, 0, sizeof(*entry));
	entry->index = (uint16_t)index;
	entry->total = (uint16_t)forge_net.net_count;
	entry->group_tag = (uint32_t)tag_get_group_tag(tag_index);
	entry->host_tag = (uint32_t)tag_index;
	entry->flags = forge_net.net_flags[index];
	entry->length = (uint8_t)strlen(name);
	csmemcpy(entry->path, name, entry->length);
}

/* palette entries first to last sent: to one machine, or every client */
static void forge_net_palette_send(
	long machine_index,
	short first,
	short last)
{
	struct forge_net_palette_message message;
	short index = first;

	while (index < last)
	{
		short count = 0;

		for (; index < last && count < FORGE_NET_PALETTE_PER_MESSAGE; index++)
			forge_net_palette_entry_fill(index, &message.entries[count++]);
		if (machine_index == NONE)
		{
			distributed_send(&message, FORGE_NET_KIND_PALETTE, count,
				(word)(sizeof(message.header) + count * sizeof(message.entries[0])), _distributed_to_clients_reliably);
		}
		else
		{
			distributed_send_to_machine_reliably(machine_index, &message, FORGE_NET_KIND_PALETTE, count,
				(word)(sizeof(message.header) + count * sizeof(message.entries[0])));
		}
	}
}

/* ---------- the host: acting for a machine */

static struct forge_net_actor *forge_net_actor_of(
	long machine_index,
	boolean make)
{
	short index;
	short free_index = NONE;

	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		if (forge_net.actors[index].used && forge_net.actors[index].machine_index == machine_index)
			return &forge_net.actors[index];
		if (!forge_net.actors[index].used && free_index == NONE)
			free_index = index;
	}
	if (!make || free_index == NONE)
		return NULL;
	csmemset(&forge_net.actors[free_index], 0, sizeof(forge_net.actors[free_index]));
	forge_net.actors[free_index].used = TRUE;
	forge_net.actors[free_index].machine_index = machine_index;
	forge_net.actors[free_index].held_object_index = NONE;
	forge_net.actors[free_index].held_distance = 4.f;
	forge_net.actors[free_index].held_forward = *global_forward3d;
	forge_net.actors[free_index].held_up = *global_up3d;
	forge_net.actors[free_index].hidden_unit = NONE;
	return &forge_net.actors[free_index];
}

/* the host's own hold and history swapped with a machine's */
static void forge_net_actor_swap(
	struct forge_net_actor *actor)
{
	long held_object_index = forge_globals.held_object_index;
	real held_distance = forge_globals.held_distance;
	real_vector3d held_forward = forge_globals.held_forward;
	real_vector3d held_up = forge_globals.held_up;
	struct forge_transform held_from = forge_globals.held_from;
	boolean held_spawned = forge_globals.held_spawned;
	short undo_count = forge_globals.undo_count;
	short undo_cursor = forge_globals.undo_cursor;
	static struct forge_undo undo[FORGE_UNDO_DEPTH];

	forge_globals.held_object_index = actor->held_object_index;
	forge_globals.held_distance = actor->held_distance;
	forge_globals.held_forward = actor->held_forward;
	forge_globals.held_up = actor->held_up;
	forge_globals.held_from = actor->held_from;
	forge_globals.held_spawned = actor->held_spawned;
	forge_globals.undo_count = actor->undo_count;
	forge_globals.undo_cursor = actor->undo_cursor;
	csmemcpy(undo, forge_globals.undo, sizeof(undo));
	csmemcpy(forge_globals.undo, actor->undo, sizeof(undo));
	actor->held_object_index = held_object_index;
	actor->held_distance = held_distance;
	actor->held_forward = held_forward;
	actor->held_up = held_up;
	actor->held_from = held_from;
	actor->held_spawned = held_spawned;
	actor->undo_count = undo_count;
	actor->undo_cursor = undo_cursor;
	csmemcpy(actor->undo, undo, sizeof(undo));
}

static void forge_net_act_begin(
	struct forge_net_actor *actor)
{
	forge_net_actor_swap(actor);
	forge_net.acting = actor;
}

static void forge_net_act_end(
	struct forge_net_actor *actor)
{
	forge_net.acting = NULL;
	forge_net_actor_swap(actor);
}

/* (forge_notice) whether a notice is a remote machine's (the host's HUD
does not show it: that machine says its own) */
static boolean forge_net_quiet(
	void)
{
	return forge_net.acting != NULL;
}

/* the player holding an object (the host's own monitor's, or a machine's),
NONE: none; but for the one asking (FORGE_NET_HOST_ASKS: the host's own; a
client: NONE, its own) */
static short forge_net_holder(
	long object_index,
	long asking_machine)
{
	short index;

	if (object_index == NONE)
		return NONE;
	if (forge_net_host())
	{
		/* (the host's own hold: in forge_globals, or swapped out to the
		machine acted for) */
		long own = forge_net.acting ? forge_net.acting->held_object_index : forge_globals.held_object_index;

		if (asking_machine != FORGE_NET_HOST_ASKS && object_index == own)
			return forge_net_local_player();
		for (index = 0; index < FORGE_NET_ACTORS; index++)
		{
			struct forge_net_actor *actor = &forge_net.actors[index];
			long held;

			if (!actor->used || actor->machine_index == asking_machine)
				continue;
			held = actor == forge_net.acting ? forge_globals.held_object_index : actor->held_object_index;
			if (object_index == held)
				return forge_net_machine_player(actor->machine_index);
		}
		return NONE;
	}
	if (forge_net_client())
	{
		struct forge_placed *placed = forge_placed_of_object(object_index);

		if (placed && placed->holder != NONE && placed->holder != forge_net_local_player())
			return (short)placed->holder;
	}
	return NONE;
}

/* (forge.c's own edits) the object held by another player: said, TRUE */
static boolean forge_net_held_by_other(
	long object_index)
{
	short holder;
	char name[48];

	if (!forge_net_host() && !forge_net_client())
		return FALSE;
	holder = forge_net_holder(object_index, forge_net_host() ? FORGE_NET_HOST_ASKS : NONE);
	if (holder == NONE)
		return FALSE;
	forge_net_player_name(holder, name, sizeof(name));
	forge_notice(T("%s is holding it"), name);
	return TRUE;
}

static void forge_net_direct(
	struct forge_net_actor *actor,
	struct forge_net_record const *record)
{
	if (forge_net.direct_machine != actor->machine_index && forge_net.direct.count)
		forge_net_send_records(&forge_net.direct, forge_net.direct_machine, FALSE);
	forge_net.direct_machine = actor->machine_index;
	forge_net_outgoing_add(&forge_net.direct, record);
}

static void forge_net_refuse(
	struct forge_net_actor *actor,
	struct forge_net_record const *request,
	short refusal,
	long value,
	long value2)
{
	struct forge_net_record record;

	csmemset(&record, 0, sizeof(record));
	record.op = _forge_net_update_refuse;
	record.sequence = request ? request->sequence : 0;
	record.aux = (uint8_t)refusal;
	record.value = (uint16_t)value;
	record.value2 = (uint32_t)value2;
	record.tag = FORGE_NET_NO_TAG;
	record.player = FORGE_NET_NO_PLAYER;
	record.object = FORGE_NET_NO_OBJECT;
	forge_net_direct(actor, &record);
	actor->drops.refused++;
	forge_net.requests_refused++;
	platform_log("forge net: refused machine %ld's %s: %s", actor->machine_index,
		request ? forge_net_op_name(request->op) : "request", forge_net_refusal_name(refusal));
}

/* the object asked for the requester's: a placed one's (its id), or a map
object's (its index, map_object) */
static void forge_net_grant(
	struct forge_net_actor *actor,
	struct forge_net_record const *request,
	long id,
	short kind,
	long map_object)
{
	struct forge_net_record record;

	csmemset(&record, 0, sizeof(record));
	record.op = _forge_net_update_grant;
	record.sequence = request->sequence;
	record.id = (uint32_t)id;
	record.aux = (uint8_t)kind;
	record.tag = FORGE_NET_NO_TAG;
	record.player = FORGE_NET_NO_PLAYER;
	record.object = FORGE_NET_NO_OBJECT;
	if (map_object != NONE)
	{
		record.flags = FORGE_NET_FLAG_MAP_OBJECT;
		record.object = map_object;
		record.id = 1;
	}
	forge_net_direct(actor, &record);
}

/* the object a request names: a placed one's (by id), or a map object the
netcode makes everywhere (by its index); NONE, the refusal said */
static long forge_net_request_object(
	struct forge_net_actor *actor,
	struct forge_net_record const *request)
{
	long object_index = NONE;

	if (request->flags & FORGE_NET_FLAG_MAP_OBJECT)
	{
		long index = (long)request->id;

		if (!distributed_object_index_valid(index) || !forge_object_alive(index) || forge_placed_of_object(index))
		{
			forge_net_refuse(actor, request, _forge_net_refuse_gone, 0, 0);
			return NONE;
		}
		/* (the map's own scenery: the netcode shows no one its move) */
		if (!forge_net_object_networked(index))
		{
			forge_net_refuse(actor, request, _forge_net_refuse_map_scenery, 0, 0);
			return NONE;
		}
		object_index = index;
	}
	else
	{
		short index;

		for (index = 0; index < forge_globals.placed_count; index++)
		{
			if (forge_globals.placed[index].id == (long)request->id)
			{
				object_index = forge_globals.placed[index].object_index;
				break;
			}
		}
		if (object_index == NONE || !forge_object_alive(object_index))
		{
			forge_net_refuse(actor, request, _forge_net_refuse_gone, 0, 0);
			return NONE;
		}
	}
	/* (never a player's unit, nor what one carries) */
	{
		long parent = object_get_ultimate_parent(object_index);
		struct object_datum *object = object_try_and_get(parent);

		if (object && (FLAG(object->object.type) & _object_mask_unit) &&
			((struct unit_datum *)object)->unit.player_index != NONE)
		{
			forge_net_refuse(actor, request, _forge_net_refuse_cannot_place, 0, 0);
			return NONE;
		}
	}
	return object_index;
}

/* the actor's hold let go where it is (a machine gone, a new round) */
static void forge_net_actor_release(
	struct forge_net_actor *actor)
{
	if (actor->held_object_index == NONE)
		return;
	forge_net_act_begin(actor);
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	forge_globals.held_object_index = NONE;
	forge_net_act_end(actor);
}

/* a machine's malformed or replayed request counted against it
(network_anticheat.c), once a host tick: it comes over its stream alone, and
a burst of them (a client's bug, a tick's worth) weighs as one; a machine
sending them tick after tick is dropped */
static void forge_net_count_violation(
	struct forge_net_actor *actor)
{
	if (actor->anticheat_tick == game_time_get() + 1)
		return;
	actor->anticheat_tick = game_time_get() + 1;
	network_anticheat_violation(actor->machine_index, _anticheat_forge, TRUE, 1.0f);
}

/* a machine's request, checked and made */
static void forge_net_host_request(
	struct forge_net_actor *actor,
	struct forge_net_record const *source)
{
	struct forge_net_record request = *source;
	unsigned long now = system_milliseconds();
	short holder;
	long object_index;

	if (!forge_net_request_valid(&request, forge_net.net_count))
	{
		actor->drops.malformed++;
		forge_net.requests_dropped++;
		forge_net_count_violation(actor);
		return;
	}
	if (!forge_net_sequence_accept(&actor->sequence, request.sequence))
	{
		actor->drops.replayed++;
		forge_net.requests_dropped++;
		forge_net_count_violation(actor);
		return;
	}
	if (!forge_net_bucket_take(&actor->requests, now, FORGE_NET_REQUESTS_PER_SECOND, FORGE_NET_REQUESTS_BURST, 1.f) ||
		((request.op == _forge_net_request_spawn || request.op == _forge_net_request_duplicate) &&
			!forge_net_bucket_take(&actor->spawns, now, FORGE_NET_SPAWNS_PER_SECOND, FORGE_NET_SPAWNS_BURST, 1.f)))
	{
		actor->drops.rate_limited++;
		forge_net.requests_dropped++;
		/* (said once in a while, not for each) */
		if (actor->drops.rate_limited % 16 == 1)
			forge_net_refuse(actor, &request, _forge_net_refuse_rate, 0, 0);
		return;
	}
	if (request.op != _forge_net_request_monitor && request.op != _forge_net_request_sync &&
		request.op != _forge_net_request_undo && request.op != _forge_net_request_property &&
		request.op != _forge_net_request_grab && request.op != _forge_net_request_delete)
	{
		forge_net_transform_square(&request.transform);
		if (!forge_net_inside_world(&request.transform))
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_outside, 0, 0);
			return;
		}
	}
	forge_net.requests_taken++;
	switch (request.op)
	{
	case _forge_net_request_sync:
		if (actor->sync_asked && now - actor->sync_time < FORGE_NET_SYNC_MILLISECONDS)
		{
			actor->drops.rate_limited++;
			return;
		}
		actor->sync_asked = TRUE;
		actor->sync_time = now;
		actor->sync_pending = TRUE;
		return;
	case _forge_net_request_monitor:
		actor->monitor = (request.flags & FORGE_NET_FLAG_MONITOR) != 0;
		if (actor->monitor)
			actor->view = request.transform;
		return;
	case _forge_net_request_spawn:
	case _forge_net_request_duplicate:
	{
		long tag_index = NONE;
		struct forge_placed original;
		boolean has_original = FALSE;
		real_point3d position;
		real_vector3d forward, up;
		short refusal;
		long made;
		struct forge_placed *placed;

		if (request.op == _forge_net_request_spawn)
		{
			if (!(forge_net.net_flags[request.tag] & FORGE_NET_PALETTE_PLACEABLE) ||
				forge_palette_of_tag(forge_net.net_tags[request.tag]) == NONE)
			{
				forge_net_refuse(actor, &request, _forge_net_refuse_not_allowed, 0, 0);
				return;
			}
			tag_index = forge_net.net_tags[request.tag];
		}
		else
		{
			if ((object_index = forge_net_request_object(actor, &request)) == NONE)
				return;
			placed = forge_placed_of_object(object_index);
			if (placed)
			{
				original = *placed;
				has_original = TRUE;
				tag_index = placed->tag_index;
			}
			else if (forge_palette_of_tag(object_get(object_index)->definition_index) != NONE)
			{
				tag_index = object_get(object_index)->definition_index;
			}
			if (tag_index == NONE)
			{
				forge_net_refuse(actor, &request, _forge_net_refuse_cannot_copy, 0, 0);
				return;
			}
		}
		forge_net_transform_to(&request.transform, &position, &forward, &up);
		forge_net_act_begin(actor);
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		forge_globals.held_object_index = NONE;
		made = forge_place(tag_index, &position, &forward, &up, TRUE, &refusal);
		if (made != NONE)
		{
			forge_globals.held_object_index = made;
			forge_globals.held_forward = forward;
			forge_globals.held_up = up;
			forge_globals.held_spawned = TRUE;
			placed = forge_placed_of_object(made);
			if (placed && has_original)
			{
				placed->team = original.team;
				placed->flags = original.flags;
				placed->respawn_seconds = original.respawn_seconds;
				placed->ai_team = original.ai_team;
				forge_ai_apply(placed);
			}
			if (!(request.flags & FORGE_NET_FLAG_HOLD))
			{
				forge_drop();
				forge_globals.held_object_index = NONE;
			}
		}
		forge_net_act_end(actor);
		actor->held_transform = request.transform;
		actor->held_transform_set = made != NONE && (request.flags & FORGE_NET_FLAG_HOLD);
		if (made == NONE)
		{
			if (refusal == _forge_place_budget)
				forge_net_refuse(actor, &request, _forge_net_refuse_budget, FORGE_MAXIMUM_OBJECTS, 0);
			else if (refusal == _forge_place_ai_budget)
				forge_net_refuse(actor, &request, _forge_net_refuse_ai_budget, FORGE_MAXIMUM_AI, forge_globals.ai_dead_count);
			else
				forge_net_refuse(actor, &request, _forge_net_refuse_cannot_place, 0, 0);
			return;
		}
		placed = forge_placed_of_object(made);
		platform_log("forge net: machine %ld %s %s (#%ld) at %.2f %.2f %.2f", actor->machine_index,
			request.op == _forge_net_request_spawn ? "spawned" : "copied", forge_tag_leaf(tag_index), placed ? placed->id : 0,
			position.x, position.y, position.z);
		if (placed && (request.flags & FORGE_NET_FLAG_HOLD))
		{
			placed->holder = forge_net_machine_player(actor->machine_index);
			forge_net_grant(actor, &request, placed->id,
				request.op == _forge_net_request_spawn ? _forge_net_grant_spawn : _forge_net_grant_duplicate, NONE);
		}
		return;
	}
	case _forge_net_request_grab:
	{
		struct forge_placed *placed;

		if ((object_index = forge_net_request_object(actor, &request)) == NONE)
			return;
		holder = forge_net_holder(object_index, actor->machine_index);
		if (holder != NONE)
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_held, holder, 0);
			return;
		}
		forge_net_act_begin(actor);
		if (object_try_and_get(forge_globals.held_object_index) && forge_globals.held_object_index != object_index)
			forge_drop();
		forge_globals.held_object_index = object_index;
		forge_globals.held_forward = object_get(object_index)->object.forward;
		forge_globals.held_up = object_get(object_index)->object.up;
		forge_transform_get(object_index, &forge_globals.held_from);
		forge_globals.held_spawned = FALSE;
		forge_net_act_end(actor);
		forge_net_transform_of_object(object_index, &actor->held_transform);
		actor->held_transform_set = forge_net_transform_valid(&actor->held_transform);
		placed = forge_placed_of_object(object_index);
		if (placed)
			placed->holder = forge_net_machine_player(actor->machine_index);
		platform_log("forge net: machine %ld grabbed %s (#%ld)", actor->machine_index,
			forge_tag_leaf(object_get(object_index)->definition_index), placed ? placed->id : -1L);
		forge_net_grant(actor, &request, placed ? placed->id : 1, _forge_net_grant_grab, placed ? NONE : object_index);
		return;
	}
	case _forge_net_request_move:
	case _forge_net_request_release:
	{
		real_point3d position;
		real_vector3d forward, up;
		struct object_datum *object;
		struct forge_placed *placed;

		if ((object_index = forge_net_request_object(actor, &request)) == NONE)
			return;
		if (actor->held_object_index != object_index)
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_not_held, 0, 0);
			return;
		}
		forge_net_transform_to(&request.transform, &position, &forward, &up);
		object = object_get(object_index);
		object_set_position(object_index, &position, &forward, &up);
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		actor->held_forward = forward;
		actor->held_up = up;
		actor->held_transform = request.transform;
		actor->held_transform_set = TRUE;
		if (request.op == _forge_net_request_release)
		{
			forge_net_act_begin(actor);
			forge_drop();
			forge_globals.held_object_index = NONE;
			forge_net_act_end(actor);
			placed = forge_placed_of_object(object_index);
			if (placed)
				placed->holder = NONE;
			platform_log("forge net: machine %ld dropped %s (#%ld) at %.2f %.2f %.2f", actor->machine_index,
				forge_tag_leaf(object->definition_index), placed ? placed->id : -1L, position.x, position.y, position.z);
		}
		return;
	}
	case _forge_net_request_set:
	{
		real_point3d position;
		real_vector3d forward, up;

		if ((object_index = forge_net_request_object(actor, &request)) == NONE)
			return;
		holder = forge_net_holder(object_index, actor->machine_index);
		if (holder != NONE)
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_held, holder, 0);
			return;
		}
		forge_net_transform_to(&request.transform, &position, &forward, &up);
		forge_net_act_begin(actor);
		if (object_index == forge_globals.held_object_index)
		{
			object_set_position(object_index, &position, &forward, &up);
			forge_globals.held_forward = forward;
			forge_globals.held_up = up;
		}
		else
		{
			struct forge_transform from;

			forge_transform_get(object_index, &from);
			object_set_position(object_index, &position, &forward, &up);
			SET_FLAG(object_get(object_index)->object.flags, _object_at_rest_bit, FALSE);
			forge_undo_record(_forge_undo_move, object_index, &from);
			forge_settle(object_index);
		}
		forge_net_act_end(actor);
		return;
	}
	case _forge_net_request_delete:
	{
		struct forge_placed *placed;
		long id;

		if ((object_index = forge_net_request_object(actor, &request)) == NONE)
			return;
		holder = forge_net_holder(object_index, actor->machine_index);
		if (holder != NONE)
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_held, holder, 0);
			return;
		}
		placed = forge_placed_of_object(object_index);
		id = placed ? placed->id : -1;
		forge_net_act_begin(actor);
		forge_delete(object_index);
		forge_net_act_end(actor);
		platform_log("forge net: machine %ld deleted #%ld", actor->machine_index, id);
		return;
	}
	case _forge_net_request_property:
	{
		struct forge_placed *placed;

		if ((object_index = forge_net_request_object(actor, &request)) == NONE)
			return;
		placed = forge_placed_of_object(object_index);
		/* (a placed object it holds; its facing is its moves') */
		if (!placed || actor->held_object_index != object_index)
		{
			forge_net_refuse(actor, &request, _forge_net_refuse_not_held, 0, 0);
			return;
		}
		if (request.aux >= _forge_property_yaw && request.aux <= _forge_property_reset_rotation)
			return;
		forge_net_act_begin(actor);
		forge_property_apply(request.aux, request.step);
		forge_net_act_end(actor);
		return;
	}
	case _forge_net_request_undo:
	{
		short kind = NONE;
		long tag_index = NONE;
		short result;

		/* (its hold let go first, as the host's own undo does) */
		if (actor->held_object_index != NONE)
		{
			struct forge_placed *placed = forge_placed_of_object(actor->held_object_index);

			forge_net_actor_release(actor);
			if (placed)
				placed->holder = NONE;
		}
		forge_net_act_begin(actor);
		result = forge_undo_step_result(request.aux != 0, actor->machine_index, &kind, &tag_index, &holder);
		forge_net_act_end(actor);
		{
			struct forge_net_record record;

			csmemset(&record, 0, sizeof(record));
			record.op = _forge_net_update_refuse;
			record.sequence = request.sequence;
			record.aux = (uint8_t)result;
			record.value = (uint16_t)(result == _forge_net_refuse_held ? holder : kind == NONE ? 0 : kind);
			record.tag = tag_index != NONE && forge_net_tag_entry(tag_index) != NONE ? (uint16_t)forge_net_tag_entry(tag_index) :
				FORGE_NET_NO_TAG;
			record.player = FORGE_NET_NO_PLAYER;
			record.object = FORGE_NET_NO_OBJECT;
			forge_net_direct(actor, &record);
		}
		platform_log("forge net: machine %ld %s: %s", actor->machine_index, request.aux ? "redo" : "undo",
			forge_net_refusal_name(result));
		return;
	}
	default:
		actor->drops.malformed++;
		return;
	}
}

void forge_net_handle_requests(
	long machine_index,
	void const *entries,
	short count)
{
	struct forge_net_actor *actor;
	short index;

	if (!forge_net_host() || count <= 0)
		return;
	actor = forge_net_actor_of(machine_index, TRUE);
	if (!actor)
		return;
	for (index = 0; index < count; index++)
	{
		struct forge_net_record record;

		csmemcpy(&record, (byte const *)entries + index * sizeof(record), sizeof(record));
		forge_net_host_request(actor, &record);
	}
}

/* ---------- the host: what goes to the clients */

/* a placed object as an entry */
static void forge_net_entry_fill(
	struct forge_placed const *placed,
	struct forge_net_record *record)
{
	struct forge_transform home;
	short tag = forge_net_tag_entry(placed->tag_index);

	csmemset(record, 0, sizeof(*record));
	record->op = _forge_net_update_entry;
	record->id = (uint32_t)placed->id;
	record->tag = tag == NONE ? FORGE_NET_NO_TAG : (uint16_t)tag;
	record->object = forge_object_alive(placed->object_index) ? placed->object_index : FORGE_NET_NO_OBJECT;
	{
		/* (whoever holds it now: the host's own monitor, or a machine's) */
		short holder = forge_net_holder(placed->object_index, NONE);

		record->player = holder != NONE ? (uint8_t)holder : FORGE_NET_NO_PLAYER;
	}
	if (placed->ai)
		record->flags |= FORGE_NET_FLAG_AI;
	if (forge_net_tag_networked(placed->tag_index))
		record->flags |= FORGE_NET_FLAG_NETWORKED;
	if ((record->flags & FORGE_NET_FLAG_NETWORKED) && record->object == FORGE_NET_NO_OBJECT)
		record->flags &= (uint8_t)~FORGE_NET_FLAG_NETWORKED;
	record->team = placed->team;
	record->record_flags = (uint8_t)(placed->flags & FORGE_RECORD_FLAGS_KNOWN);
	record->respawn_seconds = (uint16_t)MIN(placed->respawn_seconds, FORGE_FILE_MAXIMUM_RESPAWN_SECONDS);
	record->ai_team = placed->ai ? (uint8_t)(placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS) : 0;
	if (!placed->ai)
		record->record_flags &= (uint8_t)~FORGE_RECORD_AI_OFF;
	if (placed->has_home)
		home = placed->home;
	else if (forge_object_alive(placed->object_index))
		forge_transform_get(placed->object_index, &home);
	else
	{
		home.position = *global_origin3d;
		home.forward = *global_forward3d;
		home.up = *global_up3d;
	}
	forge_net_transform_from(&record->transform, &home.position, &home.forward, &home.up);
}

/* (each frame, the host) the layout compared with what was sent: each new
or changed entry, each removal and where each object the netcode does not
move is, to every client */
static void forge_net_host_broadcast(
	void)
{
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < forge_net.shadow_count; index++)
		forge_net.shadow[index].seen = FALSE;
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct forge_net_record entry;
		struct forge_net_shadow *shadow = NULL;
		short at;

		if (forge_net_tag_entry(placed->tag_index) == NONE)
			continue;
		forge_net_entry_fill(placed, &entry);
		if (!forge_net_update_valid(&entry, forge_net.net_count))
			continue;
		for (at = 0; at < forge_net.shadow_count; at++)
		{
			if (forge_net.shadow[at].id == placed->id)
			{
				shadow = &forge_net.shadow[at];
				break;
			}
		}
		if (!shadow)
		{
			if (forge_net.shadow_count >= FORGE_MAXIMUM_OBJECTS)
				continue;
			shadow = &forge_net.shadow[forge_net.shadow_count++];
			csmemset(shadow, 0, sizeof(*shadow));
			shadow->id = placed->id;
			shadow->entry.op = _forge_net_op_none;
		}
		shadow->seen = TRUE;
		{
			struct forge_net_record before = shadow->entry;
			boolean home_only;

			before.sequence = entry.sequence = 0;
			home_only = before.op == entry.op && before.object == entry.object && before.tag == entry.tag &&
				before.player == entry.player && before.flags == entry.flags && before.team == entry.team &&
				before.record_flags == entry.record_flags && before.respawn_seconds == entry.respawn_seconds &&
				before.ai_team == entry.ai_team;
			if (!home_only || (!forge_net_transforms_near(&before.transform, &entry.transform) &&
				now - shadow->entry_time >= FORGE_NET_ENTRY_MILLISECONDS))
			{
				entry.sequence = ++forge_net.sequence;
				forge_net_outgoing_add(&forge_net.broadcast, &entry);
				shadow->entry = entry;
				shadow->entry_time = now;
			}
		}
		/* (an object the netcode does not move: where it is) */
		if (!(entry.flags & FORGE_NET_FLAG_NETWORKED) && entry.object != FORGE_NET_NO_OBJECT &&
			now - shadow->transform_time >= FORGE_NET_TRANSFORM_MILLISECONDS)
		{
			struct forge_net_transform current;

			forge_net_transform_of_object(placed->object_index, &current);
			if (forge_net_transform_valid(&current) &&
				(!shadow->transform_sent || !forge_net_transforms_near(&current, &shadow->transform)))
			{
				struct forge_net_record record;

				csmemset(&record, 0, sizeof(record));
				record.op = _forge_net_update_transform;
				record.sequence = ++forge_net.sequence;
				record.id = (uint32_t)placed->id;
				record.tag = FORGE_NET_NO_TAG;
				record.player = FORGE_NET_NO_PLAYER;
				record.object = FORGE_NET_NO_OBJECT;
				record.transform = current;
				forge_net_outgoing_add(&forge_net.broadcast, &record);
				shadow->transform = current;
				shadow->transform_sent = TRUE;
				shadow->transform_time = now;
			}
		}
	}
	for (index = 0; index < forge_net.shadow_count;)
	{
		if (!forge_net.shadow[index].seen)
		{
			struct forge_net_record record;

			csmemset(&record, 0, sizeof(record));
			record.op = _forge_net_update_remove;
			record.sequence = ++forge_net.sequence;
			record.id = (uint32_t)forge_net.shadow[index].id;
			record.tag = FORGE_NET_NO_TAG;
			record.player = FORGE_NET_NO_PLAYER;
			record.object = FORGE_NET_NO_OBJECT;
			forge_net_outgoing_add(&forge_net.broadcast, &record);
			forge_net.shadow[index] = forge_net.shadow[--forge_net.shadow_count];
			continue;
		}
		index++;
	}
	/* (every monitor, five times a second) */
	if (now - forge_net.monitors_sent >= FORGE_NET_MONITOR_MILLISECONDS)
	{
		struct forge_net_record record;
		short player = forge_net_local_player();

		forge_net.monitors_sent = now;
		if (player != NONE)
		{
			real_point3d position;
			real_vector3d forward;

			csmemset(&record, 0, sizeof(record));
			record.op = _forge_net_update_monitor;
			record.player = (uint8_t)player;
			record.tag = FORGE_NET_NO_TAG;
			record.object = FORGE_NET_NO_OBJECT;
			if (forge_globals.active && forge_camera_get(&position, &forward))
			{
				record.flags = FORGE_NET_FLAG_MONITOR;
				forge_net_transform_from(&record.transform, &position, &forward, NULL);
				if (!forge_net_view_valid(&record.transform))
					record.flags = 0;
			}
			forge_net_outgoing_add(&forge_net.broadcast, &record);
		}
		for (index = 0; index < FORGE_NET_ACTORS; index++)
		{
			struct forge_net_actor *actor = &forge_net.actors[index];

			if (!actor->used || (player = forge_net_machine_player(actor->machine_index)) == NONE)
				continue;
			csmemset(&record, 0, sizeof(record));
			record.op = _forge_net_update_monitor;
			record.player = (uint8_t)player;
			record.tag = FORGE_NET_NO_TAG;
			record.object = FORGE_NET_NO_OBJECT;
			if (actor->monitor)
			{
				record.flags = FORGE_NET_FLAG_MONITOR;
				record.transform = actor->view;
			}
			forge_net_outgoing_add(&forge_net.broadcast, &record);
		}
	}
}

/* a machine's sync: the palette, the layout as a map variant's bytes, each
record's id and object, the end of it */
static void forge_net_host_sync(
	struct forge_net_actor *actor)
{
	static struct forge_file_layout layout;
	static unsigned char bytes[FORGE_FILE_MAXIMUM_SIZE];
	char why[FORGE_STORE_WHY_SIZE];
	int size = -1;
	int result = 0;
	short index;
	short pieces;
	struct forge_net_record record;
	short links = 0;
	boolean linked[FORGE_MAXIMUM_OBJECTS];

	if (forge_net.direct.count)
		forge_net_send_records(&forge_net.direct, forge_net.direct_machine, FALSE);
	forge_net_palette_send(actor->machine_index, 0, forge_net.net_count);
	csmemset(linked, 0, sizeof(linked));
	csmemset(forge_net_capture_placed, 0xFF, sizeof(forge_net_capture_placed));
	if (forge_variant_capture(&layout, forge_globals.variant_title[0] ? forge_globals.variant_title : "Forge", NULL, why,
		sizeof(why)))
	{
		size = forge_file_encode(&layout, bytes, (int)sizeof(bytes), &result);
	}
	forge_net.layout_serial++;
	if (size > 0)
	{
		struct forge_net_layout_message message;

		pieces = (short)forge_net_layout_piece_count(size);
		for (index = 0; index < pieces;)
		{
			short count = 0;

			for (; index < pieces && count < FORGE_NET_LAYOUT_PER_MESSAGE; index++)
				forge_net_layout_piece(bytes, size, forge_net.layout_serial, index, &message.entries[count++]);
			distributed_send_to_machine_reliably(actor->machine_index, &message, FORGE_NET_KIND_LAYOUT, count,
				(word)(sizeof(message.header) + count * sizeof(message.entries[0])));
		}
		for (index = 0; index < layout.record_count; index++)
		{
			short placed_index = forge_net_capture_placed[index];
			struct forge_placed *placed;

			if (placed_index < 0 || placed_index >= forge_globals.placed_count)
				continue;
			placed = &forge_globals.placed[placed_index];
			forge_net_entry_fill(placed, &record);
			record.op = _forge_net_update_link;
			record.value = (uint16_t)index;
			if (forge_net_update_valid(&record, forge_net.net_count))
			{
				record.sequence = ++forge_net.sequence;
				forge_net_outgoing_add(&forge_net.direct, &record);
				linked[placed_index] = TRUE;
				links++;
			}
		}
	}
	else
	{
		platform_log("forge net: the layout for machine %ld could not be made (%s %d): sent as entries", actor->machine_index,
			why, result);
	}
	forge_net.direct_machine = actor->machine_index;
	/* (what the map variant could not hold: as entries) */
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (linked[index] || forge_net_tag_entry(forge_globals.placed[index].tag_index) == NONE)
			continue;
		forge_net_entry_fill(&forge_globals.placed[index], &record);
		if (forge_net_update_valid(&record, forge_net.net_count))
		{
			record.sequence = ++forge_net.sequence;
			forge_net_outgoing_add(&forge_net.direct, &record);
		}
	}
	csmemset(&record, 0, sizeof(record));
	record.op = _forge_net_update_synced;
	record.sequence = ++forge_net.sequence;
	record.value = (uint16_t)links;
	record.value2 = size > 0 ? forge_net.layout_serial : 0;
	record.tag = FORGE_NET_NO_TAG;
	record.player = FORGE_NET_NO_PLAYER;
	record.object = FORGE_NET_NO_OBJECT;
	forge_net_outgoing_add(&forge_net.direct, &record);
	forge_net_send_records(&forge_net.direct, actor->machine_index, FALSE);
	actor->sync_pending = FALSE;
	actor->synced = TRUE;
	platform_log("forge net: machine %ld given the Forge game: %d palette tags, layout %d bytes, %d linked of %d objects",
		actor->machine_index, forge_net.net_count, size, links, forge_globals.placed_count);
}

/* the units of the machines' monitors hidden and unhurt, those of their
Spartans as they were */
static void forge_net_host_monitors(
	void)
{
	short index;

	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		struct forge_net_actor *actor = &forge_net.actors[index];
		short player = actor->used ? forge_net_machine_player(actor->machine_index) : NONE;
		struct player_datum *datum = player != NONE ? distributed_player(player) : NULL;
		long unit = datum ? datum->unit_index : NONE;

		if (actor->hidden_unit != NONE && (!actor->used || !actor->monitor || actor->hidden_unit != unit))
		{
			struct object_datum *object = object_try_and_get(actor->hidden_unit);

			if (object)
			{
				forge_unit_set_visible(actor->hidden_unit, TRUE);
				if (actor->unit_could_take_damage)
					SET_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
			}
			actor->hidden_unit = NONE;
		}
		if (actor->used && actor->monitor && unit != NONE && actor->hidden_unit == NONE && object_try_and_get(unit))
		{
			struct object_datum *object = object_get(unit);

			actor->hidden_unit = unit;
			actor->unit_could_take_damage = !TEST_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit);
			SET_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
			forge_unit_set_visible(unit, FALSE);
		}
	}
}

/* the drops of each machine logged, once in a while */
static void forge_net_host_drops_log(
	void)
{
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		struct forge_net_actor *actor = &forge_net.actors[index];

		if (!actor->used || now - actor->drops_logged < FORGE_NET_DROP_LOG_MILLISECONDS ||
			!csmemcmp(&actor->drops, &actor->drops_said, sizeof(actor->drops)))
		{
			continue;
		}
		actor->drops_logged = now;
		actor->drops_said = actor->drops;
		platform_log("forge net: machine %ld: %lu malformed, %lu replayed, %lu rate limited dropped; %lu refused",
			actor->machine_index, (unsigned long)actor->drops.malformed, (unsigned long)actor->drops.replayed,
			(unsigned long)actor->drops.rate_limited, (unsigned long)actor->drops.refused);
	}
}

static void forge_net_host_update(
	void)
{
	long machines[FORGE_NET_MAXIMUM_MACHINES * 2];
	short count = distributed_client_machines(machines, (short)NUMBEROF(machines));
	short index;

	/* (machines gone: their holds let go, their monitors gone) */
	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		struct forge_net_actor *actor = &forge_net.actors[index];
		short at;
		boolean still = FALSE;

		if (!actor->used)
			continue;
		for (at = 0; at < count; at++)
			still |= machines[at] == actor->machine_index;
		if (!still)
		{
			struct forge_placed *placed = forge_placed_of_object(actor->held_object_index);

			platform_log("forge net: machine %ld left the Forge game", actor->machine_index);
			forge_net_actor_release(actor);
			if (placed)
				placed->holder = NONE;
			actor->monitor = FALSE;
			actor->used = FALSE;
			forge_net_host_monitors();
			actor->hidden_unit = NONE;
		}
	}
	/* (an object a machine held gone: its hold too) */
	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		struct forge_net_actor *actor = &forge_net.actors[index];

		if (actor->used && actor->held_object_index != NONE && !forge_object_alive(actor->held_object_index))
		{
			struct forge_net_record record;

			csmemset(&record, 0, sizeof(record));
			record.op = _forge_net_update_released;
			record.id = 1;
			record.tag = FORGE_NET_NO_TAG;
			record.player = FORGE_NET_NO_PLAYER;
			record.object = FORGE_NET_NO_OBJECT;
			forge_net_direct(actor, &record);
			actor->held_object_index = NONE;
		}
	}
	/* (the objects the machines hold where they hold them: no fall between
	their moves) */
	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		struct forge_net_actor *actor = &forge_net.actors[index];
		struct object_datum *object;

		if (!actor->used || !actor->held_transform_set || !forge_object_alive(actor->held_object_index))
			continue;
		object = object_get(actor->held_object_index);
		{
			real_point3d position;
			real_vector3d forward, up;

			forge_net_transform_to(&actor->held_transform, &position, &forward, &up);
			object_set_position(actor->held_object_index, &position, &forward, &up);
		}
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
	}
	forge_net_host_monitors();
	forge_net_palette_update();
	/* (the palette grown: the new entries to every client that has it) */
	if (forge_net.net_count > forge_net.net_count_sent)
	{
		forge_net_palette_send(NONE, forge_net.net_count_sent, forge_net.net_count);
		forge_net.net_count_sent = forge_net.net_count;
	}
	forge_net_host_broadcast();
	if (forge_net.direct.count)
		forge_net_send_records(&forge_net.direct, forge_net.direct_machine, FALSE);
	if (forge_net.broadcast.count)
		forge_net_send_records(&forge_net.broadcast, NONE, FALSE);
	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		if (forge_net.actors[index].used && forge_net.actors[index].sync_pending)
			forge_net_host_sync(&forge_net.actors[index]);
	}
	forge_net_host_drops_log();
}

/* ---------- the client */

static void forge_net_request_send(
	struct forge_net_record *record)
{
	record->sequence = ++forge_net.sequence;
	forge_net_outgoing_add(&forge_net.requests, record);
}

static void forge_net_request_init(
	struct forge_net_record *record,
	short op)
{
	csmemset(record, 0, sizeof(*record));
	record->op = (uint8_t)op;
	record->tag = FORGE_NET_NO_TAG;
	record->player = FORGE_NET_NO_PLAYER;
	record->object = FORGE_NET_NO_OBJECT;
}

/* the host's palette entry of one of this machine's tags, NONE */
static short forge_net_host_entry_of(
	long tag_index)
{
	short index;

	for (index = 0; index < forge_net.host_palette_count; index++)
	{
		if (forge_net.host_palette_known[index] && forge_net.host_tags[index] == tag_index)
			return index;
	}
	return NONE;
}

/* a request's reference to an object: TRUE, filled; FALSE (said) if it
is not one the host can be asked about */
static boolean forge_net_reference(
	long object_index,
	struct forge_net_record *record)
{
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (placed)
	{
		record->id = (uint32_t)placed->id;
		return TRUE;
	}
	if (!forge_object_alive(object_index))
		return FALSE;
	if (!forge_net_object_networked(object_index) ||
		DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) >= MAXIMUM_OBJECTS_PER_MAP / 2)
	{
		forge_notice("%s", T("The map's own scenery can't be moved in a network Forge game"));
		return FALSE;
	}
	record->id = (uint32_t)object_index;
	record->flags |= FORGE_NET_FLAG_MAP_OBJECT;
	return TRUE;
}

static void forge_net_grant_wait(
	short kind)
{
	forge_net.grant_pending = TRUE;
	forge_net.grant_sequence = forge_net.sequence;
	forge_net.grant_time = system_milliseconds();
	forge_net.grant_kind = kind;
	forge_net.grant_id = NONE;
}

/* (forge_spawn, a client) a palette tag asked for in the monitor's hand */
static void forge_net_client_spawn(
	long tag_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real distance)
{
	struct forge_net_record record;
	short entry = forge_net_host_entry_of(tag_index);
	char name[FORGE_NAME_SIZE];

	forge_tag_readable_name(tag_index, name, sizeof(name));
	if (entry == NONE || !(forge_net.host_flags[entry] & FORGE_NET_PALETTE_PLACEABLE))
	{
		forge_notice(T("The host's game has no %s"), name);
		return;
	}
	forge_net_request_init(&record, _forge_net_request_spawn);
	record.tag = (uint16_t)entry;
	record.flags = FORGE_NET_FLAG_HOLD;
	forge_net_transform_from(&record.transform, position, forward, global_up3d);
	forge_net_request_send(&record);
	forge_net_grant_wait(_forge_net_grant_spawn);
	forge_globals.held_distance = distance;
	forge_globals.held_forward = *forward;
	forge_globals.held_up = *global_up3d;
	platform_log("forge net: asked the host to spawn %s", forge_tag_leaf(tag_index));
}

static void forge_net_client_grab(
	long object_index)
{
	struct forge_net_record record;

	forge_net_request_init(&record, _forge_net_request_grab);
	if (!forge_net_reference(object_index, &record))
		return;
	if (forge_net_held_by_other(object_index))
		return;
	forge_net_request_send(&record);
	forge_net_grant_wait(_forge_net_grant_grab);
	platform_log("forge net: asked the host to grab #%lu", (unsigned long)record.id);
}

/* (forge_drop, a client) the object held let go where it is */
static void forge_net_client_release(
	void)
{
	struct forge_net_record record;
	long object_index = forge_globals.held_object_index;

	forge_globals.held_object_index = NONE;
	forge_net.grant_id = NONE;
	if (!forge_object_alive(object_index))
		return;
	forge_net_request_init(&record, _forge_net_request_release);
	if (!forge_net_reference(object_index, &record))
		return;
	forge_net_transform_of_object(object_index, &record.transform);
	{
		real_vector3d forward = forge_globals.held_forward;
		real_vector3d up = forge_globals.held_up;

		forge_net_transform_from(&record.transform, &object_get(object_index)->object.position, &forward, &up);
	}
	forge_net_request_send(&record);
	platform_log("forge net: dropped #%lu", (unsigned long)record.id);
}

static void forge_net_client_delete(
	long object_index)
{
	struct forge_net_record record;

	forge_net_request_init(&record, _forge_net_request_delete);
	if (!forge_net_reference(object_index, &record))
		return;
	if (forge_net_held_by_other(object_index))
		return;
	if (object_index == forge_globals.held_object_index)
	{
		/* (its own hold let go first: the host deletes no object held) */
		forge_net_client_release();
	}
	forge_net_request_send(&record);
	platform_log("forge net: asked the host to delete #%lu", (unsigned long)record.id);
}

/* (forge_duplicate, a client) a copy of an object, in the monitor's hand,
facing as it faces */
static void forge_net_client_duplicate(
	long object_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	real distance)
{
	struct forge_net_record record;

	forge_net_request_init(&record, _forge_net_request_duplicate);
	if (!forge_net_reference(object_index, &record))
		return;
	if (object_index == forge_globals.held_object_index)
		forge_net_client_release();
	record.flags |= FORGE_NET_FLAG_HOLD;
	forge_net_transform_from(&record.transform, position, forward, up);
	forge_net_request_send(&record);
	forge_net_grant_wait(_forge_net_grant_duplicate);
	forge_globals.held_distance = distance;
	forge_globals.held_forward = *forward;
	forge_globals.held_up = *up;
}

/* (forge_set_facing, a client, an object it does not hold) its facing */
static void forge_net_client_set(
	long object_index,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	struct forge_net_record record;

	forge_net_request_init(&record, _forge_net_request_set);
	if (!forge_net_reference(object_index, &record))
		return;
	if (forge_net_held_by_other(object_index))
		return;
	forge_net_transform_from(&record.transform, &object_get(object_index)->object.position, forward, up);
	forge_net_request_send(&record);
}

/* (forge_property_apply, a client) a property of the object held: its
facing its own (its moves say it), the rest the host's */
static boolean forge_net_client_property(
	short property,
	short step)
{
	struct forge_net_record record;

	if (property >= _forge_property_yaw && property <= _forge_property_reset_rotation)
		return FALSE;
	forge_net_request_init(&record, _forge_net_request_property);
	if (!forge_net_reference(forge_globals.held_object_index, &record))
		return TRUE;
	record.aux = (uint8_t)property;
	record.step = (int8_t)(step < 0 ? -1 : 1);
	forge_net_request_send(&record);
	return TRUE;
}

static void forge_net_client_undo(
	boolean redo)
{
	struct forge_net_record record;

	if (object_try_and_get(forge_globals.held_object_index))
		forge_net_client_release();
	forge_net_request_init(&record, _forge_net_request_undo);
	record.aux = redo ? 1 : 0;
	forge_net_request_send(&record);
}

/* this machine's tag of a host's palette entry (by group and path; a
placeable one only if it is one of this machine's palette: its allow
list), NONE */
static long forge_net_local_tag(
	unsigned long group_tag,
	char const *path,
	boolean placeable)
{
	long tag_index = tag_loaded((long)group_tag, path);

	if (tag_index == NONE || tag_get_group_tag(tag_index) != group_tag)
		return NONE;
	if (placeable)
		return forge_palette_of_tag(tag_index) != NONE ? tag_index : NONE;
	return tag_index;
}

void forge_net_handle_palette(
	void const *entries,
	short count)
{
	short index;

	if (!forge_net_client() || network_connection_last_read_was_unreliable())
		return;
	forge_palette_ready();
	for (index = 0; index < count; index++)
	{
		struct forge_net_palette_entry entry;
		long tag_index;

		csmemcpy(&entry, (byte const *)entries + index * sizeof(entry), sizeof(entry));
		if (!forge_net_palette_entry_valid(&entry) || entry.total < forge_net.host_palette_count)
		{
			forge_net.updates_dropped++;
			continue;
		}
		forge_net.host_palette_count = (short)entry.total;
		{
			char path[FORGE_NET_PATH_SIZE];

			csmemcpy(path, entry.path, entry.length);
			path[entry.length] = 0;
			tag_index = forge_net_local_tag(entry.group_tag, path, (entry.flags & FORGE_NET_PALETTE_PLACEABLE) != 0);
			if (tag_index == NONE && !forge_net.host_palette_known[entry.index])
			{
				forge_net.missing_tags++;
				platform_log("forge net: the host's %s is not on this machine: its objects are not shown", path);
			}
		}
		forge_net.host_tags[entry.index] = tag_index;
		forge_net.host_tag_indices[entry.index] = (long)entry.host_tag;
		forge_net.host_groups[entry.index] = entry.group_tag;
		forge_net.host_flags[entry.index] = entry.flags;
		forge_net.host_palette_known[entry.index] = TRUE;
		forge_net.updates_taken++;
	}
}

long forge_net_client_tag(
	long host_tag_index)
{
	short index;

	if (!forge_net_client() || host_tag_index == NONE)
		return host_tag_index;
	for (index = 0; index < forge_net.host_palette_count; index++)
	{
		if (forge_net.host_palette_known[index] && forge_net.host_tag_indices[index] == host_tag_index)
			return forge_net.host_tags[index];
	}
	return host_tag_index;
}

void forge_net_handle_layout(
	void const *entries,
	short count)
{
	short index;

	if (!forge_net_client() || network_connection_last_read_was_unreliable())
		return;
	for (index = 0; index < count; index++)
	{
		struct forge_net_layout_entry entry;

		csmemcpy(&entry, (byte const *)entries + index * sizeof(entry), sizeof(entry));
		if (!forge_net_layout_take(&forge_net.layout, &entry))
		{
			forge_net.updates_dropped++;
			continue;
		}
		if (forge_net.layout.complete)
		{
			char why[96];
			char map_name[FORGE_FILE_MAP_NAME_SIZE];
			int result;

			forge_net.layout_ready = FALSE;
			if (global_scenario_index == NONE ||
				!forge_map_short_name(tag_get_name(global_scenario_index), map_name, sizeof(map_name)))
			{
				map_name[0] = 0;
			}
			result = forge_file_decode(forge_net.layout.bytes, (int)forge_net.layout.total, map_name[0] ? map_name : NULL,
				&forge_net.layout_decoded, why, (int)sizeof(why));
			if (result != _forge_file_ok)
			{
				platform_log("forge net: the host's layout was refused: %s (%s)", forge_file_result_name(result), why);
				forge_net.updates_dropped++;
			}
			else
			{
				forge_net.layout_ready = TRUE;
				platform_log("forge net: the host's layout read: %d objects, %d paths (%u bytes, checked)",
					forge_net.layout_decoded.record_count, forge_net.layout_decoded.path_count, (unsigned)forge_net.layout.total);
			}
		}
	}
}

/* the mirror's entry of an id (NULL) */
static struct forge_placed *forge_net_mirror(
	long id)
{
	short index;

	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].id == id)
			return &forge_globals.placed[index];
	}
	return NULL;
}

/* a mirrored object's own copy (one the netcode does not make) gone */
static void forge_net_mirror_object_delete(
	struct forge_placed *placed)
{
	if (!placed->networked && forge_object_alive(placed->object_index))
		object_delete(placed->object_index);
	placed->object_index = NONE;
}

/* the mirror emptied (a new sync, the session's end) */
static void forge_net_mirror_clear(
	void)
{
	short index;

	for (index = 0; index < forge_globals.placed_count; index++)
		forge_net_mirror_object_delete(&forge_globals.placed[index]);
	forge_globals.placed_count = 0;
	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
}

/* an entry or link taken into the mirror (tag: this machine's, NONE) */
static void forge_net_mirror_take(
	struct forge_net_record const *record,
	long tag_index,
	struct forge_net_transform const *home)
{
	struct forge_placed *placed = forge_net_mirror((long)record->id);
	boolean networked = (record->flags & FORGE_NET_FLAG_NETWORKED) != 0;

	if (tag_index == NONE)
	{
		if (placed)
		{
			forge_net_mirror_object_delete(placed);
			forge_placed_remove((short)(placed - forge_globals.placed));
		}
		return;
	}
	if (placed && placed->tag_index != tag_index)
	{
		forge_net_mirror_object_delete(placed);
		placed->tag_index = tag_index;
	}
	if (!placed)
	{
		if (forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
			return;
		placed = &forge_globals.placed[forge_globals.placed_count++];
		csmemset(placed, 0, sizeof(*placed));
		placed->object_index = NONE;
		placed->tag_index = tag_index;
		placed->id = (long)record->id;
		placed->net_object = NONE;
	}
	placed->ai = (record->flags & FORGE_NET_FLAG_AI) != 0;
	placed->team = record->team;
	placed->flags = record->record_flags;
	placed->respawn_seconds = record->respawn_seconds;
	placed->ai_team = record->ai_team;
	placed->holder = record->player != FORGE_NET_NO_PLAYER ? record->player : NONE;
	forge_net_transform_to(home, &placed->home.position, &placed->home.forward, &placed->home.up);
	placed->has_home = TRUE;
	if (networked != placed->networked)
	{
		forge_net_mirror_object_delete(placed);
		placed->networked = networked;
	}
	/* (an object index from the host: a whole one, else none - never one
whose identifier is 0, which any object at its index would match) */
	placed->net_object = record->object != FORGE_NET_NO_OBJECT && distributed_object_index_valid(record->object) ?
		record->object : FORGE_NET_NO_OBJECT;
	/* (gone on the host: gone here) */
	if (placed->net_object == FORGE_NET_NO_OBJECT)
		forge_net_mirror_object_delete(placed);
}

/* (test builds) HALO_FORGE_NET_TRACE=1: every update taken and request
made in the log */
static boolean forge_net_trace(
	void)
{
#ifndef HALO_RELEASE
	static int trace = -1;

	if (trace < 0)
		trace = getenv("HALO_FORGE_NET_TRACE") && getenv("HALO_FORGE_NET_TRACE")[0] == '1';
	return trace > 0;
#else
	return FALSE;
#endif
}

/* a host's update taken */
static void forge_net_client_update_take(
	struct forge_net_record const *record)
{
	if (forge_net_trace() && record->op != _forge_net_update_monitor)
	{
		platform_log("forge net: update %s #%lu object %lx tag %d player %d aux %d (synced %d)", forge_net_op_name(record->op),
			(unsigned long)record->id, (unsigned long)record->object, (int)record->tag, (int)record->player, (int)record->aux,
			(int)forge_net.synced);
	}
	switch (record->op)
	{
	case _forge_net_update_entry:
		if (!forge_net.synced || !forge_net.host_palette_known[record->tag])
			return;
		forge_net_mirror_take(record, forge_net.host_tags[record->tag], &record->transform);
		return;
	case _forge_net_update_link:
	{
		struct forge_file_record const *source;
		long tag_index = NONE;
		struct forge_net_record entry = *record;
		struct forge_net_transform home;

		if (forge_net.synced || !forge_net.layout_ready || record->value >= forge_net.layout_decoded.record_count)
			return;
		source = &forge_net.layout_decoded.records[record->value];
		/* (the record's tag by its path, this machine's allow list's; its
		properties and placed spot the map variant's, checked by
		forge_file_decode) */
		if (source->kind == _forge_record_object && source->path_index < forge_net.layout_decoded.path_count)
		{
			struct forge_file_path const *path = &forge_net.layout_decoded.paths[source->path_index];

			tag_index = forge_net_local_tag(path->group_tag, path->path, TRUE);
			if (tag_index == NONE)
				platform_log("forge net: the host's %s is not on this machine: not shown", path->path);
		}
		entry.team = source->team;
		entry.record_flags = source->flags;
		entry.respawn_seconds = source->respawn_seconds;
		entry.ai_team = source->ai_team;
		{
			real_point3d position;
			real_vector3d forward, up;

			position.x = source->position[0];
			position.y = source->position[1];
			position.z = source->position[2];
			forge_facing_from_angles(source->angles, &forward, &up);
			forge_net_transform_from(&home, &position, &forward, &up);
		}
		forge_net_mirror_take(&entry, tag_index, &home);
		forge_net.link_count++;
		return;
	}
	case _forge_net_update_synced:
		if (forge_net.synced)
			return;
		forge_net.synced = TRUE;
		platform_log("forge net: the host's Forge game had: %d objects (%d linked), %d palette tags, %d not on this machine",
			forge_globals.placed_count, forge_net.link_count, forge_net.host_palette_count, forge_net.missing_tags);
		if (forge_net.missing_tags || forge_net.missing_packs[0])
		{
			if (forge_net.missing_packs[0])
				forge_notice(T("Not shown (packs this Vita can't build): %s"), forge_net.missing_packs);
			else
				forge_notice(T("%d of the host's objects aren't on this Vita: not shown"), forge_net.missing_tags);
		}
		return;
	case _forge_net_update_remove:
	{
		struct forge_placed *placed;

		if (!forge_net.synced || (placed = forge_net_mirror((long)record->id)) == NULL)
			return;
		if (placed->object_index == forge_globals.held_object_index)
			forge_globals.held_object_index = NONE;
		forge_net_mirror_object_delete(placed);
		forge_placed_remove((short)(placed - forge_globals.placed));
		return;
	}
	case _forge_net_update_transform:
	{
		struct forge_placed *placed;
		real_point3d position;
		real_vector3d forward, up;

		if (!forge_net.synced || (placed = forge_net_mirror((long)record->id)) == NULL || placed->networked ||
			!forge_object_alive(placed->object_index) || placed->object_index == forge_globals.held_object_index)
		{
			return;
		}
		forge_net_transform_to(&record->transform, &position, &forward, &up);
		object_set_position(placed->object_index, &position, &forward, &up);
		return;
	}
	case _forge_net_update_grant:
		if (!forge_net.grant_pending || record->sequence != forge_net.grant_sequence)
			return;
		forge_net.grant_pending = FALSE;
		forge_net.grant_id = (long)record->id;
		forge_net.grant_object = NONE;
		/* (a map object's: its index, one of this machine's objects) */
		if (record->flags & FORGE_NET_FLAG_MAP_OBJECT)
		{
			if (!distributed_object_index_valid(record->object) || !forge_object_alive(record->object))
			{
				forge_net.grant_id = NONE;
				return;
			}
			forge_net.grant_object = record->object;
		}
		forge_net.grant_time = system_milliseconds();
		forge_net.grant_kind = record->aux;
		return;
	case _forge_net_update_released:
		if (object_try_and_get(forge_globals.held_object_index))
			forge_notice("%s", T("The object you held is gone"));
		forge_globals.held_object_index = NONE;
		forge_net.grant_id = NONE;
		return;
	case _forge_net_update_refuse:
	{
		char name[FORGE_NAME_SIZE];
		long tag_index = record->tag < forge_net.host_palette_count && record->tag != FORGE_NET_NO_TAG &&
			forge_net.host_palette_known[record->tag] ? forge_net.host_tags[record->tag] : NONE;
		static char const *const kinds[NUMBER_OF_FORGE_UNDO_KINDS] = { N_("Place %s"), N_("Delete %s"), N_("Move %s") };
		char words[FORGE_LINE_SIZE];

		if (forge_net.grant_pending && record->sequence == forge_net.grant_sequence)
			forge_net.grant_pending = FALSE;
		name[0] = 0;
		if (tag_index != NONE)
		{
			short entry = forge_palette_of_tag(tag_index);

			if (entry != NONE)
				snprintf(name, sizeof(name), "%s", forge_globals.palette[entry].name);
			else
				forge_tag_readable_name(tag_index, name, sizeof(name));
		}
		snprintf(words, sizeof(words), T(kinds[record->value < NUMBER_OF_FORGE_UNDO_KINDS ? record->value : 0]),
			name[0] ? name : "?");
		switch (record->aux)
		{
		case _forge_net_refuse_budget:
			forge_notice(T("Budget full: %d objects. Delete one to place another."), (int)record->value);
			break;
		case _forge_net_refuse_ai_budget:
			if (record->value2 > 0)
				forge_notice(T("AI budget full: %d characters, %d of them dead. Clear dead AI to place more."),
					(int)record->value, (int)record->value2);
			else
				forge_notice(T("AI budget full: %d characters."), (int)record->value);
			break;
		case _forge_net_refuse_held:
		{
			char player[48];

			forge_net_player_name((short)record->value, player, sizeof(player));
			forge_notice(T("%s is holding it"), player);
			break;
		}
		case _forge_net_refuse_not_allowed: forge_notice("%s", T("The host's game can't place that")); break;
		case _forge_net_refuse_outside: forge_notice("%s", T("That's outside the map")); break;
		case _forge_net_refuse_cannot_place: forge_notice("%s", T("That can't be placed here")); break;
		case _forge_net_refuse_cannot_copy: forge_notice("%s", T("That can't be copied")); break;
		case _forge_net_refuse_host_only: forge_notice("%s", T("Only the host can do that")); break;
		case _forge_net_refuse_rate: forge_notice("%s", T("Slow down: the host takes so many edits a second")); break;
		case _forge_net_refuse_gone: forge_notice("%s", T("It is gone")); break;
		case _forge_net_refuse_map_scenery:
			forge_notice("%s", T("The map's own scenery can't be moved in a network Forge game"));
			break;
		case _forge_net_refuse_nothing_to_undo: forge_menu_notice("%s", T("Nothing to undo")); break;
		case _forge_net_refuse_nothing_to_redo: forge_menu_notice("%s", T("Nothing to redo")); break;
		case _forge_net_refuse_not_held: forge_notice("%s", T("You aren't holding it")); break;
		case _forge_net_refuse_undone: forge_menu_notice(T("Undone: %s"), words); break;
		case _forge_net_refuse_redone: forge_menu_notice(T("Redone: %s"), words); break;
		case _forge_net_refuse_undo_gone: forge_menu_notice(T("Can't undo %s: it is gone"), words); break;
		case _forge_net_refuse_redo_gone: forge_menu_notice(T("Can't redo %s: it is gone"), words); break;
		case _forge_net_refuse_undo_budget: forge_menu_notice(T("Can't undo %s: the budget is full"), words); break;
		case _forge_net_refuse_redo_budget: forge_menu_notice(T("Can't redo %s: the budget is full"), words); break;
		}
		platform_log("forge net: the host said: %s", forge_net_refusal_name(record->aux));
		return;
	}
	case _forge_net_update_monitor:
		if (record->player < FORGE_NET_PLAYERS)
		{
			struct forge_net_monitor *monitor = &forge_net.monitors[record->player];

			monitor->monitor = (record->flags & FORGE_NET_FLAG_MONITOR) != 0;
			if (monitor->monitor)
				monitor->view = record->transform;
			monitor->time = system_milliseconds();
		}
		return;
	case _forge_net_update_round:
		forge_net.rounds++;
		forge_notice(T("New round: %d objects placed"), (int)record->value);
		platform_log("forge net: the host started round %lu", (unsigned long)record->value2);
		return;
	}
}

void forge_net_handle_updates(
	void const *entries,
	short count)
{
	short index;

	if (!forge_net_client() || network_connection_last_read_was_unreliable())
		return;
	for (index = 0; index < count; index++)
	{
		struct forge_net_record record;

		csmemcpy(&record, (byte const *)entries + index * sizeof(record), sizeof(record));
		if (!forge_net_update_valid(&record, forge_net.host_palette_count))
		{
			forge_net.updates_dropped++;
			continue;
		}
		forge_net.updates_taken++;
		forge_net_client_update_take(&record);
	}
}

/* the mirror's objects found (the netcode's, at the host's index) or made
(this machine's copies) */
static void forge_net_mirror_objects(
	void)
{
	short index;

	for (index = 0; index < forge_globals.placed_count; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (placed->net_object == FORGE_NET_NO_OBJECT || forge_object_alive(placed->object_index))
			continue;
		placed->object_index = NONE;
		if (placed->networked)
		{
			long expected = placed->tag_index;
			struct object_datum *object = object_try_and_get(placed->net_object);

			if (placed->ai && tag_get_group_tag(placed->tag_index) == ACTOR_VARIANT_DEFINITION_TAG)
				expected = actor_variant_definition_get(placed->tag_index)->unit_reference.index;
			if (object && object->definition_index == expected)
				placed->object_index = placed->net_object;
		}
		else
		{
			struct object_placement_data placement;

			object_placement_data_new(&placement, placed->tag_index, NONE);
			SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
			placement.position = placed->home.position;
			placement.forward = placed->home.forward;
			placement.up = placed->home.up;
			forge_placing_exact = 1;
			placed->object_index = object_new(&placement);
			forge_placing_exact = 0;
		}
	}
}

static void forge_net_client_frame(
	void)
{
	unsigned long now = system_milliseconds();
	short index;

	/* (the host's state asked for, once this machine has its palette) */
	if (!forge_net.synced && forge_globals.palette_built &&
		(!forge_net.sync_sent || now - forge_net.sync_sent >= FORGE_NET_SYNC_RETRY_MILLISECONDS))
	{
		struct forge_net_record record;

		forge_net_request_init(&record, _forge_net_request_sync);
		forge_net_request_send(&record);
		forge_net.sync_sent = now ? now : 1;
		forge_net_mirror_clear();
		forge_net_layout_reset(&forge_net.layout);
		forge_net.layout_ready = FALSE;
		forge_net.link_count = 0;
		platform_log("forge net: asked the host for its Forge game");
	}
	if (forge_net.synced)
		forge_net_mirror_objects();
	/* (a grant: the object in the monitor's hand once it is here) */
	if (forge_net.grant_pending && now - forge_net.grant_time >= FORGE_NET_GRANT_MILLISECONDS)
	{
		forge_net.grant_pending = FALSE;
		platform_log("forge net: the host did not answer");
	}
	if (forge_net.grant_id != NONE && !object_try_and_get(forge_globals.held_object_index))
	{
		struct forge_placed *placed = forge_net.grant_object == NONE ? forge_net_mirror(forge_net.grant_id) : NULL;
		long object_index = placed ? placed->object_index : forge_net.grant_object;
		if (forge_object_alive(object_index))
		{
			if (forge_net.grant_kind == _forge_net_grant_grab)
				forge_grab_local(object_index);
			else
			{
				forge_globals.held_object_index = object_index;
			}
			forge_net.move_sent = 0;
			platform_log("forge net: holding #%ld", forge_net.grant_id);
			forge_net.grant_id = NONE;
		}
		else if (now - forge_net.grant_time >= FORGE_NET_GRANT_MILLISECONDS)
		{
			forge_net.grant_id = NONE;
		}
	}
	/* (holding one: where, to the host) */
	if (forge_object_alive(forge_globals.held_object_index) && now - forge_net.move_sent >= FORGE_NET_MOVE_MILLISECONDS)
	{
		struct forge_net_record record;

		forge_net_request_init(&record, _forge_net_request_move);
		if (forge_net_reference(forge_globals.held_object_index, &record))
		{
			forge_net_transform_from(&record.transform, &object_get(forge_globals.held_object_index)->object.position,
				&forge_globals.held_forward, &forge_globals.held_up);
			if (!forge_net.move_sent || !forge_net_transforms_near(&record.transform, &forge_net.move_last))
			{
				forge_net_request_send(&record);
				forge_net.move_last = record.transform;
			}
			forge_net.move_sent = now;
		}
	}
	/* (its monitor, five times a second) */
	if (now - forge_net.monitor_sent >= FORGE_NET_MONITOR_MILLISECONDS)
	{
		struct forge_net_record record;
		real_point3d position;
		real_vector3d forward;

		forge_net.monitor_sent = now;
		forge_net_request_init(&record, _forge_net_request_monitor);
		if (forge_globals.active && forge_camera_get(&position, &forward))
		{
			record.flags = FORGE_NET_FLAG_MONITOR;
			forge_net_transform_from(&record.transform, &position, &forward, NULL);
			if (!forge_net_view_valid(&record.transform))
				record.flags = 0;
		}
		forge_net_request_send(&record);
	}
	/* (the others' monitors' units hidden) */
	for (index = 0; index < FORGE_NET_PLAYERS; index++)
	{
		struct forge_net_monitor *monitor = &forge_net.monitors[index];
		struct player_datum *player = distributed_player(index);
		long unit = player && index != forge_net_local_player() ? player->unit_index : NONE;

		if (monitor->hidden_unit != NONE && (!monitor->monitor || monitor->hidden_unit != unit))
		{
			if (object_try_and_get(monitor->hidden_unit))
				forge_unit_set_visible(monitor->hidden_unit, TRUE);
			monitor->hidden_unit = NONE;
		}
		if (monitor->monitor && unit != NONE && monitor->hidden_unit == NONE && object_try_and_get(unit))
		{
			monitor->hidden_unit = unit;
			forge_unit_set_visible(unit, FALSE);
		}
	}
	if (forge_net.requests.count)
		forge_net_send_records(&forge_net.requests, NONE, TRUE);
}

/* (forge_update, each frame) */
static void forge_net_update(
	void)
{
	if (forge_net_host())
		forge_net_host_update();
	else if (forge_net_client())
		forge_net_client_frame();
}

/* (network_objects.c) whether this client holds the object: its place is
this machine's own while held */
boolean forge_net_client_holds(
	long object_index)
{
	return object_index != NONE && forge_net_client() && object_index == forge_globals.held_object_index;
}

/* (forge_map_loaded) a new map: the network session's state started over */
static void forge_net_map_loaded(
	void)
{
	short index;

	forge_net.shadow_count = 0;
	forge_net.net_count = 0;
	forge_net.net_count_sent = 0;
	forge_net.broadcast.count = 0;
	forge_net.direct.count = 0;
	forge_net.requests.count = 0;
	forge_net.acting = NULL;
	for (index = 0; index < FORGE_NET_ACTORS; index++)
		forge_net.actors[index].used = FALSE;
	forge_net.host_palette_count = 0;
	csmemset(forge_net.host_palette_known, 0, sizeof(forge_net.host_palette_known));
	forge_net.missing_tags = 0;
	forge_net.synced = FALSE;
	forge_net.sync_sent = 0;
	forge_net_layout_reset(&forge_net.layout);
	forge_net.layout_ready = FALSE;
	forge_net.link_count = 0;
	forge_net.grant_pending = FALSE;
	forge_net.grant_id = NONE;
	forge_net.grant_object = NONE;
	forge_net.move_sent = 0;
	forge_net.monitor_sent = 0;
	forge_net.monitors_sent = 0;
	for (index = 0; index < FORGE_NET_PLAYERS; index++)
	{
		forge_net.monitors[index].monitor = FALSE;
		forge_net.monitors[index].hidden_unit = NONE;
	}
}

/* (forge_new_round_start, the host of a network game) a network game's new
round: every machine's hold let go, the layout's objects placed again where
the editor put them (destroyed vehicles whole, dead characters alive), the
undo histories cleared; the engine's own map reset is a local game's */
static void forge_net_new_round(
	void)
{
	short index;
	struct forge_file_layout *layout = &forge_round_layout;
	char why[FORGE_STORE_WHY_SIZE];
	struct forge_net_record record;

	for (index = 0; index < FORGE_NET_ACTORS; index++)
	{
		if (forge_net.actors[index].used)
		{
			forge_net_actor_release(&forge_net.actors[index]);
			forge_net.actors[index].undo_count = 0;
			forge_net.actors[index].undo_cursor = 0;
		}
	}
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	forge_globals.held_object_index = NONE;
	if (!forge_variant_capture(layout, forge_globals.variant_title[0] ? forge_globals.variant_title : "Forge", NULL, why,
		sizeof(why)))
	{
		platform_log("forge net: the new round's layout could not be taken: %s", why);
		return;
	}
	/* (the layout's objects gone, the entries with them: placed again as the
	map variant's records, each a new entry) */
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_object_alive(forge_globals.placed[index].object_index))
			object_delete(forge_globals.placed[index].object_index);
	}
	forge_globals.placed_count = 0;
	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
	forge_globals.undo_count = 0;
	forge_globals.undo_cursor = 0;
	forge_globals.round_count++;
	forge_variant_apply(layout, TRUE);
	csmemset(&record, 0, sizeof(record));
	record.op = _forge_net_update_round;
	record.sequence = ++forge_net.sequence;
	record.value = (uint16_t)forge_globals.placed_count;
	record.value2 = (uint32_t)forge_globals.round_count;
	record.tag = FORGE_NET_NO_TAG;
	record.player = FORGE_NET_NO_PLAYER;
	record.object = FORGE_NET_NO_OBJECT;
	forge_net_outgoing_add(&forge_net.broadcast, &record);
	platform_log("forge net: new round %ld: the layout's %d objects placed again for every machine", forge_globals.round_count,
		forge_globals.placed_count);
}

/* (forge_draw) the other players' monitors: where each is, its name */
static void forge_net_draw_monitors(
	long font_index,
	short height)
{
	short index;
	short local = forge_net_local_player();

	if (!forge_net_host() && !forge_net_client())
		return;
	for (index = 0; index < FORGE_NET_PLAYERS; index++)
	{
		struct forge_net_transform view;
		boolean monitor = FALSE;
		real_point3d position;
		real_point2d screen;
		real_vector3d to_monitor;

		if (index == local || !distributed_player(index))
			continue;
		if (forge_net_host())
		{
			short slot;

			for (slot = 0; slot < FORGE_NET_ACTORS; slot++)
			{
				struct forge_net_actor *actor = &forge_net.actors[slot];

				if (actor->used && actor->monitor && forge_net_machine_player(actor->machine_index) == index)
				{
					monitor = TRUE;
					view = actor->view;
				}
			}
		}
		else if (forge_net.monitors[index].monitor)
		{
			monitor = TRUE;
			view = forge_net.monitors[index].view;
		}
		if (!monitor)
			continue;
		position.x = view.position[0];
		position.y = view.position[1];
		position.z = view.position[2];
		to_monitor.i = position.x - render.camera.position.x;
		to_monitor.j = position.y - render.camera.position.y;
		to_monitor.k = position.z - render.camera.position.z;
		if (dot_product3d(&to_monitor, &render.camera.forward) <= 0.1f)
			continue;
		if (render_camera_world_to_screen(&render.camera, &render.frustum, &position, &screen))
		{
			rectangle2d bounds;
			char name[48];
			short x = (short)(screen.x - render.camera.viewport_bounds.x0);
			short y = (short)(screen.y - render.camera.viewport_bounds.y0);

			forge_net_player_name(index, name, sizeof(name));
			bounds.x0 = (short)(x - 120);
			bounds.x1 = (short)(x + 120);
			bounds.y0 = (short)(y - height);
			bounds.y1 = y;
			forge_draw_line(font_index, &bounds, 2, 0.47f, 0.84f, 1.f, "( O )");
			bounds.y0 = y;
			bounds.y1 = (short)(y + height);
			forge_draw_line(font_index, &bounds, 2, 0.95f, 0.62f, 0.30f, name);
		}
	}
}

#ifndef HALO_RELEASE
/* (tests: "forge net junk <count>", a client) requests no honest build
sends, to the host: random bytes, every field out of range, numbers that
are not numbers, replayed and stale sequences, and a burst past the rate
limits; the host must drop them all (counted, logged) and go on */
static void forge_net_junk(
	long count)
{
	unsigned long seed = system_milliseconds() | 1;
	long index;

	if (!forge_net_client())
	{
		platform_log("forge net: junk: not a network Forge game's client");
		return;
	}
	for (index = 0; index < count; index++)
	{
		struct forge_net_record record;
		unsigned char *bytes = (unsigned char *)&record;
		short at;
		uint32_t nan_bits = 0x7FC00000u;
		uint32_t infinity_bits = 0x7F800000u;

		forge_net_request_init(&record, _forge_net_request_spawn);
		switch (index % 12)
		{
		case 0:
			/* (random bytes, a random op) */
			for (at = 0; at < (short)sizeof(record); at++)
			{
				seed = seed * 1103515245u + 12345u;
				bytes[at] = (unsigned char)(seed >> 16);
			}
			break;
		case 1:
			/* (a tag past the palette) */
			record.tag = 0xFFFE;
			record.transform.forward[0] = 1.f;
			record.transform.up[2] = 1.f;
			break;
		case 2:
			/* (a move of a number that is not one) */
			record.op = _forge_net_request_move;
			record.id = 1;
			csmemcpy(&record.transform.position[0], &nan_bits, sizeof(nan_bits));
			record.transform.forward[0] = 1.f;
			record.transform.up[2] = 1.f;
			break;
		case 3:
			/* (far outside the world) */
			record.op = _forge_net_request_set;
			record.id = 1;
			record.transform.position[2] = 4999.f;
			record.transform.forward[0] = 1.f;
			record.transform.up[2] = 1.f;
			break;
		case 4:
			/* (an object that is none: a map object's index made up) */
			record.op = _forge_net_request_delete;
			record.flags = FORGE_NET_FLAG_MAP_OBJECT;
			record.id = 0x7FFF1234;
			break;
		case 5:
			/* (a property past the rows) */
			record.op = _forge_net_request_property;
			record.aux = 200;
			record.step = 1;
			break;
		case 6:
			/* (a monitor looking nowhere) */
			record.op = _forge_net_request_monitor;
			record.flags = FORGE_NET_FLAG_MONITOR;
			csmemcpy(&record.transform.forward[1], &infinity_bits, sizeof(infinity_bits));
			break;
		case 7:
			/* (flags no request has) */
			record.op = _forge_net_request_grab;
			record.flags = 0xE0;
			record.id = 1;
			break;
		case 8:
			/* (an undo of a kind that is none) */
			record.op = _forge_net_request_undo;
			record.aux = 9;
			break;
		case 9:
			/* (a grab of an id no object has) */
			record.op = _forge_net_request_grab;
			record.id = 0x7FFFFFFF;
			break;
		case 10:
			/* (a host's update sent as a request) */
			record.op = _forge_net_update_entry;
			record.id = 1;
			break;
		default:
			/* (forward and up not square) */
			record.transform.forward[0] = 1.f;
			record.transform.up[0] = 1.f;
			record.tag = 0;
			break;
		}
		if (index % 12 != 0)
			record.sequence = ++forge_net.sequence;
		forge_net_outgoing_add(&forge_net.requests, &record);
		/* (and the same again: replayed) */
		if (index % 5 == 0)
			forge_net_outgoing_add(&forge_net.requests, &record);
	}
	/* (a burst of spawns past the rate limits: the first few placed) */
	for (index = 0; index < 40; index++)
	{
		struct forge_net_record record;
		real_point3d position;
		real_vector3d forward;

		if (!forge_camera_get(&position, &forward))
			break;
		forge_net_request_init(&record, _forge_net_request_spawn);
		record.tag = 0;
		position.x += forward.i * 6.f;
		position.y += forward.j * 6.f;
		position.z += forward.k * 6.f;
		forge_net_transform_from(&record.transform, &position, global_forward3d, global_up3d);
		forge_net_request_send(&record);
	}
	forge_net_send_records(&forge_net.requests, NONE, TRUE);
	platform_log("forge net: junk: %ld bad requests and 40 spawns at once sent to the host", count);
}
#endif

/* (forge net, the console) the session's state in the log */
static void forge_net_log_state(
	char const *arguments)
{
	short index;

#ifndef HALO_RELEASE
	if (arguments && !strncmp(arguments, "junk", 4))
	{
		forge_net_junk(MAX(1, atol(arguments + 4)));
		return;
	}
	/* (tests: "forge net grab #<id>", a client) the grab asked of the host
	whatever this machine's own copy of the layout says of its holder: the
	host's own check of the first grab, as when both ask at once */
	if (arguments && !strncmp(arguments, "grab #", 6) && forge_net_client())
	{
		struct forge_net_record record;

		forge_net_request_init(&record, _forge_net_request_grab);
		record.id = (uint32_t)atol(arguments + 6);
		forge_net_request_send(&record);
		forge_net_grant_wait(_forge_net_grant_grab);
		platform_log("forge net: asked the host to grab #%lu, unchecked here", (unsigned long)record.id);
		return;
	}
#endif

	platform_log("forge net: %s, %d objects, %d palette tags, %ld requests taken, %ld refused, %ld dropped; %ld updates taken, "
		"%ld dropped; synced %s, monitor %s", forge_net_host() ? "host" : forge_net_client() ? "client" : "not in a network game",
		forge_globals.placed_count, forge_net_host() ? forge_net.net_count : forge_net.host_palette_count,
		forge_net.requests_taken, forge_net.requests_refused, forge_net.requests_dropped, forge_net.updates_taken,
		forge_net.updates_dropped, forge_net.synced ? "yes" : "no", forge_globals.active ? "on" : "off");
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct object_datum *object = object_try_and_get(placed->object_index);

		boolean networked = forge_net_host() ? forge_net_tag_networked(placed->tag_index) : placed->networked;
		long holder = forge_net_host() ? forge_net_holder(placed->object_index, NONE) : placed->holder;

		platform_log("forge net: #%ld %s %s object %lx holder %ld home %.2f %.2f %.2f%s", placed->id,
			forge_tag_leaf(placed->tag_index), networked ? "networked" : forge_net_host() ? "the host's own" : "own copy",
			(unsigned long)placed->object_index, holder, placed->home.position.x, placed->home.position.y,
			placed->home.position.z, object ? "" : ", gone");
		if (object)
		{
			platform_log("forge net: #%ld at %.2f %.2f %.2f", placed->id, object->object.position.x, object->object.position.y,
				object->object.position.z);
		}
	}
	for (index = 0; index < FORGE_NET_PLAYERS; index++)
	{
		char name[48];

		if (!distributed_player(index))
			continue;
		forge_net_player_name(index, name, sizeof(name));
		if (forge_net_host())
		{
			short slot;
			boolean monitor = index == forge_net_local_player() && forge_globals.active;

			for (slot = 0; slot < FORGE_NET_ACTORS; slot++)
			{
				if (forge_net.actors[slot].used && forge_net.actors[slot].monitor &&
					forge_net_machine_player(forge_net.actors[slot].machine_index) == index)
				{
					monitor = TRUE;
				}
			}
			platform_log("forge net: player %d (%s): %s", index, name, monitor ? "monitor" : "Spartan");
		}
		else
		{
			platform_log("forge net: player %d (%s): %s", index, name,
				index == forge_net_local_player() ? (forge_globals.active ? "monitor" : "Spartan") :
				forge_net.monitors[index].monitor ? "monitor" : "Spartan");
		}
	}
}
