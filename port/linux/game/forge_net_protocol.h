/*
FORGE_NET_PROTOCOL.H

The rules of network Forge (forge.c's forge_net.inc): several players
forging one map together, the host authoritative. This unit holds what can
be checked without the game, so that it is tested and fuzzed on its own
(port/vita/tests/forge_net_test.c, net_fuzz_forge.c): the messages' layouts,
every field's checks, the replay window, the rate limits, and the receiving
side of a layout sent to a joiner.

An optional capability, negotiated between machines: no network version
changes. A Forge host tells each machine that joins it, in the lobby (and a
machine joining its game in progress as it is accepted), that the game is
Forge, with the packs it brings and its map variant's title (a lobby hello:
the Bungie message _message_server_forge, past upstream's and OpenCE's
types). A machine that can play network Forge answers (_message_client_forge,
its capability bits and protocol version); one that does not answer in
FORGE_NET_HELLO_MILLISECONDS - an older build, which drops a message type it
does not know - or answers another version is refused from the Forge game
(the rejection an older build shows as "failed to join"), and plays every
other game as before. A game that is not Forge sends none of it.

In the game the host is the only one that edits: every client edit is a
request (a record of forge_net_requests), checked by the host - the object
exists and may be edited, the tag is one of the host's palette (its allow
list), the budget, the world's bounds, every number finite and in range,
the sender's own sequence (a record replayed or older is dropped) and its
rate (a bucket a machine) - and applied there, then sent to every machine
(forge_net_updates). The first grab wins. Records travel over the client's
reliable stream only: a datagram claiming to be a client's is never one.

  client                                  host
  sync (its palette built)          ->
                                    <-    palette (path by path: the host's
                                          allow list, by tag path and group),
                                          layout (a map variant's bytes, read
                                          through forge_file_decode), links
                                          (each record's id and object)
  spawn, grab, move, release, set,
  delete, duplicate, property,
  undo, monitor                     ->
                                    <-    entry, remove, transform, hold,
                                          grant, refuse, monitor, budget, round

Tags travel as tag paths (the palette), never as indices: a joiner whose
packs differ, or the generated building blocks made on each machine from the
map, resolve each path against their own loaded tags and allow list; a path
they do not have is left out, said, and its objects not shown.
*/

#ifndef __FORGE_NET_PROTOCOL_H
#define __FORGE_NET_PROTOCOL_H

#include <stdint.h>

/* ---------- constants */

/* the protocol's version (the lobby hello's and its answer's) */
#define FORGE_NET_VERSION 1
/* the version a host's hello says (its answer's stays FORGE_NET_VERSION,
which every host takes: a joiner reads any hello of version 1 or more):
2, the host takes a client's menu commands (_forge_net_request_command:
CLEAR DEAD AI, CLEAR ALL AI, ALL AI, NEW ROUND); a client sends them only
to a host of 2 or more - one of 1 (insider.21 and before) would count the
record as malformed - and says "Only the host can do that" to an older one */
#define FORGE_NET_HOST_VERSION 2
#define FORGE_NET_HOST_VERSION_COMMANDS 2
/* a joiner's capabilities (its answer's bits) */
#define FORGE_NET_CAPABILITY_FORGE 0x00000001u
#define FORGE_NET_CAPABILITIES_KNOWN FORGE_NET_CAPABILITY_FORGE
/* the most players of a network Forge game (the owner's, Oct 2026) */
#define FORGE_NET_MAXIMUM_PLAYERS 8
/* machine slots (the netcode's machine indices are below this) */
#define FORGE_NET_MAXIMUM_MACHINES 16
/* how long a joiner has to answer the host's hello before it is refused */
#define FORGE_NET_HELLO_MILLISECONDS 6000
/* the host's hello sent again this often until answered */
#define FORGE_NET_HELLO_RESEND_MILLISECONDS 1000
/* a joiner still building the host's packs says so this often (its
_forge_net_lobby_building), and the host holds its game's start for it
while the last said is this recent (a joiner gone silent holds nothing) */
#define FORGE_NET_BUILDING_MILLISECONDS 2000
#define FORGE_NET_BUILDING_STALE_MILLISECONDS 10000
/* the longest a joiner's build holds the start (then the game starts, and
it loads what is left as before) */
#define FORGE_NET_BUILDING_MAXIMUM_MILLISECONDS 300000

/* the lobby message's room */
#define FORGE_NET_MAXIMUM_PACKS 8
#define FORGE_NET_PACK_NAME_SIZE 24
#define FORGE_NET_PACK_BYTES 32
#define FORGE_NET_TITLE_SIZE 32

/* a record (forge_net_requests, forge_net_updates) */
#define FORGE_NET_RECORD_SIZE 64
/* a palette entry (forge_net_palette) */
#define FORGE_NET_PATH_SIZE 128
#define FORGE_NET_PALETTE_ENTRY_SIZE (16 + FORGE_NET_PATH_SIZE)
#define FORGE_NET_MAXIMUM_PALETTE 512
/* a piece of a layout sent to a joiner (forge_net_layout) */
#define FORGE_NET_LAYOUT_CHUNK_BYTES 960
#define FORGE_NET_LAYOUT_ENTRY_SIZE (12 + FORGE_NET_LAYOUT_CHUNK_BYTES)
/* a layout's most bytes (forge_files.h FORGE_FILE_MAXIMUM_SIZE) */
#define FORGE_NET_LAYOUT_MAXIMUM_BYTES 65536

/* none */
#define FORGE_NET_NO_TAG 0xFFFF
#define FORGE_NET_NO_PLAYER 0xFF
#define FORGE_NET_NO_OBJECT (-1)
/* a coordinate's bounds (the flying camera's and the files': forge_files.h) */
#define FORGE_NET_MAXIMUM_COORDINATE 5000.f
/* a direction's length within this of one, and two directions' dot within
this of square */
#define FORGE_NET_UNIT_TOLERANCE 0.02f
#define FORGE_NET_SQUARE_TOLERANCE 0.05f
/* the properties window's rows (forge.h NUMBER_OF_FORGE_PROPERTIES) */
#define FORGE_NET_PROPERTY_COUNT 9
/* properties' ranges (forge_files.h's) */
#define FORGE_NET_MAXIMUM_TEAM 7
#define FORGE_NET_MAXIMUM_RESPAWN_SECONDS 600
#define FORGE_NET_AI_TEAM_COUNT 6
#define FORGE_NET_RECORD_FLAGS_KNOWN 0x03

/* rate limits, per machine (records a second, the most at once): every
request; and the ones that make objects (spawn, duplicate) */
#define FORGE_NET_REQUESTS_PER_SECOND 90.f
#define FORGE_NET_REQUESTS_BURST 180.f
#define FORGE_NET_SPAWNS_PER_SECOND 4.f
#define FORGE_NET_SPAWNS_BURST 12.f
/* and the Forge menu's commands (CLEAR AI, ALL AI, NEW ROUND): a NEW ROUND
places the whole layout again */
#define FORGE_NET_COMMANDS_PER_SECOND 1.f
#define FORGE_NET_COMMANDS_BURST 3.f
/* a joiner's sync (palette and layout) at most once in this long */
#define FORGE_NET_SYNC_MILLISECONDS 5000

/* the lobby message's commands */
enum
{
	_forge_net_lobby_none = 0,
	/* (the host's) this game is Forge: its version, session, packs, title */
	_forge_net_lobby_hello,
	/* (a joiner's) it plays it: its version, capabilities, the packs it has */
	_forge_net_lobby_answer,
	/* (the host's) it is refused from this Forge game, why */
	_forge_net_lobby_refused,
	/* (a joiner's, after its answer) the hello's packs it is still
	building (packs_ready: their bits; 0: all built) and how far (flags:
	0 to 100 percent): the host holds the game's start until each is built -
	a joiner that built them as the map loaded took longer on a Vita than the
	host waits for a machine's load, and was dropped. An older host drops it
	unread (an invalid answer), as before */
	_forge_net_lobby_building,
	NUMBER_OF_FORGE_NET_LOBBY_COMMANDS
};

/* why a joiner is refused */
enum
{
	_forge_net_refusal_none = 0,
	/* its protocol is another version */
	_forge_net_refusal_version,
	/* the game has its most players */
	_forge_net_refusal_full,
	/* it does not play network Forge (or did not answer) */
	_forge_net_refusal_incapable,
	NUMBER_OF_FORGE_NET_LOBBY_REFUSALS
};

/* the hello's flags */
enum
{
	/* the host's game is under way (a machine joining it in progress) */
	FORGE_NET_SESSION_IN_PROGRESS = 1 << 0,
	/* the game plays a map variant (the multiplayer lobby's MAP), not
	Forge: the layout given to the joiner as Forge's is, nothing edited,
	no monitor; a build without this bit drops the hello as invalid and is
	refused from the game, as one that does not play network Forge */
	FORGE_NET_SESSION_PLAY = 1 << 1,
	FORGE_NET_SESSION_FLAGS_KNOWN = FORGE_NET_SESSION_IN_PROGRESS | FORGE_NET_SESSION_PLAY,
};

/* a record's operation: a client's request, or the host's update */
enum
{
	_forge_net_op_none = 0,
	/* requests (client to host) */
	_forge_net_request_spawn,       /* tag, transform; flags HOLD */
	_forge_net_request_grab,        /* ref */
	_forge_net_request_move,        /* ref, transform (an object it holds) */
	_forge_net_request_release,     /* ref, transform */
	_forge_net_request_set,         /* ref, transform (one it does not hold) */
	_forge_net_request_delete,      /* ref */
	_forge_net_request_duplicate,   /* ref, transform (the copy's, held) */
	_forge_net_request_property,    /* ref, aux property, step */
	_forge_net_request_undo,        /* aux: 0 undo, 1 redo */
	_forge_net_request_monitor,     /* flags MONITOR, position and forward */
	_forge_net_request_sync,        /* the palette and the layout, please */
	_forge_net_request_command,     /* aux: _forge_net_command_* (the Forge menu's, made by the host) */
	FORGE_NET_FIRST_UPDATE = 32,
	/* updates (host to client) */
	_forge_net_update_entry = FORGE_NET_FIRST_UPDATE, /* id, object, tag, holder, transform, properties */
	_forge_net_update_remove,       /* id */
	_forge_net_update_transform,    /* id, transform (an object the netcode does not move) */
	_forge_net_update_hold,         /* id, player holding it (or none) */
	_forge_net_update_grant,        /* id: the requester holds it; aux how; sequence its request's */
	_forge_net_update_refuse,       /* aux refusal, value and value2 its numbers, sequence the request's */
	_forge_net_update_monitor,      /* player, flags MONITOR, position, forward */
	_forge_net_update_synced,       /* the end of a sync: value its links, value2 its layout's serial (0: none) */
	_forge_net_update_link,         /* value the layout's record, id, object, holder, flags */
	_forge_net_update_round,        /* value the round's number */
	_forge_net_update_released,     /* id: the requester holds it no more */
	FORGE_NET_UPDATE_END
};

/* a record's flags */
enum
{
	/* (requests naming an object) the reference is a map object's index,
	not a placed object's id */
	FORGE_NET_FLAG_MAP_OBJECT = 1 << 0,
	/* (spawn, duplicate) held by the requester once made */
	FORGE_NET_FLAG_HOLD = 1 << 1,
	/* (monitor) the player is the monitor (else the Spartan) */
	FORGE_NET_FLAG_MONITOR = 1 << 2,
	/* (entry, link) a character: an actor variant's unit */
	FORGE_NET_FLAG_AI = 1 << 3,
	/* (entry, link) an object the netcode makes everywhere (a biped,
	vehicle, weapon or equipment): the client finds it at the host's index;
	else the client makes its own copy */
	FORGE_NET_FLAG_NETWORKED = 1 << 4,
	FORGE_NET_FLAGS_KNOWN = 0x1F,
};

/* how a grant came */
enum
{
	_forge_net_grant_spawn = 0,
	_forge_net_grant_grab,
	_forge_net_grant_duplicate,
	NUMBER_OF_FORGE_NET_GRANTS
};

/* why the host refused a request, or what came of one (the client says it
in its own words; an undo's and a redo's with the step's kind in value and
its tag in tag) */
enum
{
	_forge_net_refuse_none = 0,
	_forge_net_refuse_budget,        /* value: the most objects */
	_forge_net_refuse_ai_budget,     /* value: the most characters, value2: the dead */
	_forge_net_refuse_held,          /* another player holds it (value: the player) */
	_forge_net_refuse_not_allowed,   /* not a tag of the host's palette */
	_forge_net_refuse_outside,       /* outside the map */
	_forge_net_refuse_cannot_place,
	_forge_net_refuse_cannot_copy,
	_forge_net_refuse_host_only,
	_forge_net_refuse_rate,
	_forge_net_refuse_gone,          /* the object is no longer there */
	_forge_net_refuse_map_scenery,   /* the map's own scenery is not edited online */
	_forge_net_refuse_nothing_to_undo,
	_forge_net_refuse_nothing_to_redo,
	_forge_net_refuse_not_held,      /* a move or release of one it does not hold */
	_forge_net_refuse_undone,
	_forge_net_refuse_redone,
	_forge_net_refuse_undo_gone,
	_forge_net_refuse_redo_gone,
	_forge_net_refuse_undo_budget,
	_forge_net_refuse_redo_budget,
	/* (what came of a command: sent only to a client that sent one) */
	_forge_net_refuse_cleared_dead,  /* value: the dead characters cleared */
	_forge_net_refuse_cleared_all,   /* value: the characters cleared */
	_forge_net_refuse_ai_set,        /* value: 1 AI on, 0 off */
	_forge_net_refuse_round,         /* a new round asked (the host starts it) */
	_forge_net_refuse_not_now,       /* not while the host's game cannot (a round starting) */
	NUMBER_OF_FORGE_NET_REFUSALS
};

/* a client's command (the Forge menu's rows a client asks the host for) */
enum
{
	_forge_net_command_clear_dead_ai = 0,
	_forge_net_command_clear_all_ai,
	/* (ALL AI: the record's value 1 on, 0 off) */
	_forge_net_command_all_ai,
	_forge_net_command_new_round,
	NUMBER_OF_FORGE_NET_COMMANDS
};

/* a palette entry's flags */
enum
{
	/* the browser offers it (a placeable tag); else a pack's tag the netcode
	makes objects of (a vehicle's gun, a character's unit) whose index
	clients translate */
	FORGE_NET_PALETTE_PLACEABLE = 1 << 0,
	FORGE_NET_PALETTE_FLAGS_KNOWN = FORGE_NET_PALETTE_PLACEABLE,
};

/* ---------- structures */

/* the lobby's Bungie message, both ways (network_messages.c encodes it:
shorts, longs, then the packs and the title as bytes) */
struct forge_net_lobby_message
{
	uint16_t command;
	uint16_t version;
	uint16_t reason;
	uint16_t pack_count;
	/* (the joiner's) */
	uint32_t capabilities;
	/* the host's session (the hello's, echoed by the answer) */
	uint32_t session;
	/* (the answer, whose pack count is 0) which of the hello's packs the
	joiner will load, a bit each */
	uint32_t packs_ready;
	/* (the hello) FORGE_NET_SESSION_* */
	uint32_t flags;
	/* the packs: { recipe hash u32[2] little-endian, name char[24] } */
	uint8_t packs[FORGE_NET_MAXIMUM_PACKS * FORGE_NET_PACK_BYTES];
	/* (the hello) the map variant's title, UTF-8, "" for none */
	char title[FORGE_NET_TITLE_SIZE];
};

/* a pack of the hello, read */
struct forge_net_pack
{
	uint32_t recipe[2];
	char name[FORGE_NET_PACK_NAME_SIZE + 1];
};

/* an object's place and facing */
struct forge_net_transform
{
	float position[3];
	float forward[3];
	float up[3];
};

/* a request or an update (each op reads the fields it names; the others
are zero when sent and never read) */
struct forge_net_record
{
	uint8_t op;
	uint8_t flags;
	/* a request's sequence (the sender's, rising); an update's (the host's) */
	uint16_t sequence;
	/* a placed object's id (the host's), or a map object's index */
	uint32_t id;
	/* the host's object index, FORGE_NET_NO_OBJECT */
	int32_t object;
	/* the host's palette entry, FORGE_NET_NO_TAG */
	uint16_t tag;
	/* a player (absolute index), FORGE_NET_NO_PLAYER */
	uint8_t player;
	/* a property, a refusal, a grant's kind, undo or redo */
	uint8_t aux;
	struct forge_net_transform transform;
	/* properties: team (-1 neutral), FORGE_RECORD_* flags, respawn, AI team */
	int8_t team;
	uint8_t record_flags;
	uint16_t respawn_seconds;
	uint8_t ai_team;
	/* a property's step (-1 or 1) */
	int8_t step;
	uint16_t value;
	uint32_t value2;
};

/* a palette entry: the host's tag by path */
struct forge_net_palette_entry
{
	uint16_t index;
	/* the palette's size as it is sent (it only grows) */
	uint16_t total;
	uint32_t group_tag;
	/* the host's tag index (the netcode's object messages name it: a client
	translates it to its own tag of that path, network_objects.c) */
	uint32_t host_tag;
	uint8_t length;
	uint8_t flags;
	uint16_t reserved;
	char path[FORGE_NET_PATH_SIZE];
};

/* a piece of the layout (a map variant's bytes) */
struct forge_net_layout_entry
{
	uint32_t total;
	uint32_t offset;
	uint16_t length;
	/* the layout's number this session (each sync sends a new one) */
	uint16_t serial;
	uint8_t data[FORGE_NET_LAYOUT_CHUNK_BYTES];
};

/* a joiner's layout as its pieces arrive (in order, of one serial) */
struct forge_net_layout_receiver
{
	uint8_t bytes[FORGE_NET_LAYOUT_MAXIMUM_BYTES];
	uint32_t total;
	uint32_t received;
	uint16_t serial;
	uint8_t started;
	uint8_t complete;
};

/* a sender's requests' sequence (the host's, per machine) */
struct forge_net_sequence
{
	uint16_t last;
	uint8_t started;
};

/* a rate limit (tokens, refilled by the clock) */
struct forge_net_bucket
{
	float tokens;
	uint32_t time;
	uint8_t started;
};

/* what a machine sent that was dropped, counted (the host's) */
struct forge_net_drops
{
	uint32_t malformed;
	uint32_t replayed;
	uint32_t rate_limited;
	uint32_t refused;
};

typedef char forge_net_record_size_assert[sizeof(struct forge_net_record) == FORGE_NET_RECORD_SIZE ? 1 : -1];
typedef char forge_net_palette_size_assert[
	sizeof(struct forge_net_palette_entry) == FORGE_NET_PALETTE_ENTRY_SIZE ? 1 : -1];
typedef char forge_net_layout_size_assert[sizeof(struct forge_net_layout_entry) == FORGE_NET_LAYOUT_ENTRY_SIZE ? 1 : -1];
typedef char forge_net_lobby_size_assert[sizeof(struct forge_net_lobby_message) ==
	8 + 16 + FORGE_NET_MAXIMUM_PACKS * FORGE_NET_PACK_BYTES + FORGE_NET_TITLE_SIZE ? 1 : -1];

/* ---------- prototypes/FORGE_NET_PROTOCOL.C */

/* whether a float's bits are a finite number (looked at before it is used) */
int forge_net_finite(
	float value);

/* a transform's checks: every coordinate finite and within
FORGE_NET_MAXIMUM_COORDINATE, forward and up finite, each about one long and
about square (made exactly so by forge_net_transform_square) */
int forge_net_transform_valid(
	struct forge_net_transform const *transform);
/* ... and only its position and forward (a monitor's) */
int forge_net_view_valid(
	struct forge_net_transform const *transform);
/* forward and up made unit length and square (after forge_net_transform_valid) */
void forge_net_transform_square(
	struct forge_net_transform *transform);

/* a client's request, checked as the host takes it: its op and every field
it names (palette_count: the host's palette size). 1 if it may be acted on */
int forge_net_request_valid(
	struct forge_net_record const *record,
	int palette_count);

/* the host's update, checked as a client takes it (palette_count: the
host's palette size as its palette said, 0 before it came) */
int forge_net_update_valid(
	struct forge_net_record const *record,
	int palette_count);

/* a palette entry checked: its index below its total (at most
FORGE_NET_MAXIMUM_PALETTE), a group Forge places (or, a pack's netcode tag, a
biped, vehicle, weapon or equipment), its path a map variant's path, zeros
after it */
int forge_net_palette_entry_valid(
	struct forge_net_palette_entry const *entry);

/* a lobby message checked: from_host the host's (hello, refused), else a
joiner's (answer) */
int forge_net_lobby_valid(
	struct forge_net_lobby_message const *message,
	int from_host);
/* a hello's pack, read (index below its pack count) */
int forge_net_lobby_pack(
	struct forge_net_lobby_message const *message,
	int index,
	struct forge_net_pack *pack);
/* ... and written; 0 if the name cannot be sent */
int forge_net_lobby_set_pack(
	struct forge_net_lobby_message *message,
	int index,
	uint32_t const recipe[2],
	char const *name);
/* whether a pack's name is one Forge's recipes have: 1 to 24 of
[a-z0-9_ -] */
int forge_net_pack_name_valid(
	char const *name);

/* whether a request's sequence is newer than the last taken (a window of
half the numbers): taken as the last if so */
int forge_net_sequence_accept(
	struct forge_net_sequence *sequence,
	uint16_t value);

/* whether cost tokens can be taken from a bucket at time (milliseconds),
refilled at rate a second up to burst: taken if so */
int forge_net_bucket_take(
	struct forge_net_bucket *bucket,
	uint32_t time,
	float rate,
	float burst,
	float cost);

/* a joiner's layout receiver: started over (empty) */
void forge_net_layout_reset(
	struct forge_net_layout_receiver *receiver);
/* a piece taken: 1 if it was the next of the layout being received (or the
first of a new serial), 0 if dropped (out of order, too large, another
total); the receiver complete once every byte is in */
int forge_net_layout_take(
	struct forge_net_layout_receiver *receiver,
	struct forge_net_layout_entry const *entry);
/* the pieces of a layout of size bytes: how many, and the index-th filled */
int forge_net_layout_piece_count(
	int size);
int forge_net_layout_piece(
	unsigned char const *bytes,
	int size,
	uint16_t serial,
	int index,
	struct forge_net_layout_entry *entry);

/* a refusal's and an op's words (English, for the log) */
char const *forge_net_refusal_name(
	int refusal);
char const *forge_net_op_name(
	int op);

#endif
