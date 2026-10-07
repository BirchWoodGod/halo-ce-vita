/*
MAP_SHARE.C

Map sharing between machines (map_share.h). A joiner whose join check
(network_client_manager.c, network_game_client_map_playable) finds that it
lacks the host's custom map, or has another copy of it, asks the host about
it instead of leaving; the host offers its copy (name, size), the player
says yes or no, and the map comes over the joiner's connection to the host,
in the lobby, before the game starts: the host's countdown already waits
for every machine to have the map precached, so nobody starts without it.

Choices:
- Only in the lobby (pregame): a host whose game starts, or that is already
  playing (a joiner in progress), refuses, and the joiner leaves with the
  old message. Gameplay traffic never shares the link with a download.
- Rate: all of a host's uploads together send at most
  MAP_SHARE_DEFAULT_BYTES_PER_SECOND (HALO_MAP_SHARE_RATE_KB), and each
  keeps at most MAP_SHARE_WINDOW_BYTES ahead of the joiner's
  acknowledgement and MAP_SHARE_QUEUE_BYTES in its connection's queue, so
  the lobby's own messages to that machine, and the other machines' link,
  are not crowded out. At most MAP_SHARE_MAXIMUM_UPLOADS at once, a message
  each in turn (two joiners share the upload evenly), and the reading,
  hashing and deflating of all of them take at most
  MAP_SHARE_DEFAULT_CPU_PERCENT of the host's frame
  (HALO_MAP_SHARE_CPU_PERCENT).
- Deflate (map_share_protocol.h): a Custom Edition map goes as a deflate
  stream (40-55% of it) to a joiner that can take one. A host whose CPU,
  not the link, holds an upload back (a Vita on a fast LAN) tries storing
  blocks as they are for a few seconds, and keeps doing so while that is
  faster (HALO_MAP_SHARE_LEVEL=0|1 fixes the level; HALO_MAP_SHARE_COMPRESS=0
  turns deflate off, either side).
- Resume: a download that stops (cancelled, the link lost, the host's game
  started, the joiner left) keeps <name>.download, with <name>.resume: the
  host's file (fingerprint, size, kind) and how much of it is kept, with
  its SHA-256 (written every MAP_SHARE_RESUME_SAVE_BYTES). Offered the same
  file again by a host that can, the joiner reads its kept part back
  (checking it against the SHA-256 written down: else it starts over), and
  the host sends the rest; the SHA-256 checked at the end is still the whole
  file's. A damaged download is deleted; so are a kept part of another file
  when the download starts, kept parts left MAP_SHARE_RESUME_KEEP_SECONDS
  (looked at with the first download), and a map's kept part when it is
  deleted from Modded maps (vita_settings.c). HALO_MAP_SHARE_RESUME=0 turns
  it off (on the joiner).
- What a host sends: only the map its game plays, when that is a custom map
  in its level list (on, not one of the Xbox's levels, never a resource map
  nor any other file: custom_edition_maps_shareable), and only the copy its
  game settings fingerprint.
- What a joiner keeps: the bytes go straight to <name>.download in the maps
  folder (no whole-file buffer: the Vita has little memory), at the offset
  expected and no further than the size offered. The cache header is
  checked as soon as it has arrived (a lying header stops the download
  early) and again from the disk at the end, where the file's SHA-256 must be
  the host's and its fingerprint the one the game settings carry. Only then
  is it renamed to <name>.map (or .yelo), replacing the joiner's other copy,
  and the level list looks again: the Modded maps tab shows it like any
  other map.
- Free space: the size offered (and a margin) must be free before starting.
- PC maps: a joiner with PC maps (HALO_CUSTOM_EDITION) off cannot load a
  Custom Edition map, and a game started on one it cannot load stops as a
  damaged disc. The question about such a map asks to turn PC maps on with
  the download (turned on, and saved, as the settings panel does, once the
  map is in place); one the joiner has already is asked about alone
  (map_share_client_offer_pc_maps). A downloaded Custom Edition map whose
  resource maps (bitmaps.map, sounds.map, loc.map) are not in the maps
  folder is kept, and the player told which are missing.
- Why not: a joiner that is not asked (the host's version, a name that
  cannot be sent, the game started), or that gives up, is told which of
  them it was.
- The host's start waits while it sends a map (the countdown also waits for
  every machine to have the map precached), and the lobby shows the
  machine's progress by its name (map_share_server_machine_percent).
- Who from: the question names the host (its game's name), and in a game
  joined from the public lobby (p2p_join_lobby_code: a stranger's) warns
  that only maps from players one trusts should be taken. The setting Map
  downloads (HALO_MAP_SHARE_FROM, Multiplayer > Modded maps) asks (the
  default), asks except in public games ("private"), or never ("never"); a
  refused offer says why (map_share_downloads_policy). Every map,
  downloaded or not, then passes the loaders' checks before the game reads
  it.
- HALO_MAP_SHARE=0 turns it off (joiners are refused as before; a host
  still answers). HALO_MAP_SHARE_ANSWER=yes|no answers the question without
  asking (the automated tests: a hidden window shows nothing),
  HALO_MAP_SHARE_CANCEL_AT=<bytes> cancels a download that far in (once a
  run), and
  HALO_MAP_SHARE_HOST_SILENT=1 has a host ignore the requests, as one of a
  version without map sharing does.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "main/main.h"
#include "tag_files/tag_files.h"
#include "bungie_net/network/transport.h"
#include "networking/network_messages.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "networking/network_connection.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_share_protocol.h"
#include "map_share.h"
#include "../src/p2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- constants */

/* the message codec's classes of the lobby's messages
(network_client_message_handler.c, network_server_message_handler.c) */
enum
{
	_packet_class_pregame = 2,
	_packet_class_client_pregame = 3,
};

/* the client's and the server's states (network_client_message_handler.c,
network_server_message_handler.c) */
enum
{
	_network_game_client_state_searching = 0,
	_network_game_client_state_joining,
	_network_game_client_state_pregame,
	_network_game_client_state_ingame,
	_network_game_client_state_postgame,
};
enum
{
	_network_game_server_state_pregame = 0,
	_network_game_server_state_ingame,
	_network_game_server_state_postgame,
};

/* the joiner's download */
enum map_share_client_state
{
	_client_idle = 0,
	/* the query sent, the offer awaited */
	_client_querying,
	/* the player asked */
	_client_asking,
	/* the kept part of an earlier download read back, before the start */
	_client_checking,
	/* the start sent, the host's acceptance (its offer again) awaited */
	_client_starting,
	_client_receiving,
	/* told why, about to leave the game */
	_client_leaving,
};

/* room left beside a download (the folder's other writes: settings, the
log) */
#define FREE_SPACE_MARGIN_BYTES (4UL * 1024UL * 1024UL)
/* how often the progress is redrawn */
#define PROGRESS_INTERVAL_MILLISECONDS 500
/* the bytes of rate a host may save up while idle */
#define MAXIMUM_RATE_BURST_BYTES (4UL * MAP_SHARE_CHUNK_BYTES)
#define TEMPORARY_EXTENSION ".download"
#define RESUME_EXTENSION ".resume"
#define REPLACED_EXTENSION ".replaced"
#define PATH_BYTES 256
/* the C library's buffer of a file sent or written (the Vita's memory card
takes few large writes better than many of a message each) */
#define FILE_BUFFER_BYTES 0x8000
/* a deflating upload's reads */
#define DEFLATE_INPUT_BYTES 0x4000
/* the reads of a host's hash catching up, and of a joiner's kept part read
back */
#define READ_BACK_BYTES 0x10000
/* a joiner's frame's share of reading its kept part back */
#define READ_BACK_MICROSECONDS 15000
/* the most of a host's CPU share its uploads save up (the work a frame may
take at most) */
#define MAXIMUM_CPU_ALLOWANCE_MICROSECONDS 15000
/* a deflating upload's level: looked at every second; stored blocks tried
this long when deflate is held back by the CPU, kept this long when faster */
#define LEVEL_PERIOD_MILLISECONDS 1000
#define LEVEL_TRIAL_MILLISECONDS 3000
#define LEVEL_HOLD_MILLISECONDS 20000

/* ---------- structures */

struct map_share_upload
{
	boolean active;
	struct network_game_server_client_machine *machine;
	/* its slot, which is its machine index (the lobby's) */
	long machine_index;
	struct network_connection *connection;
	FILE *file;
	char path[PATH_BYTES];
	char name[MAP_SHARE_NAME_BYTES];
	int32_t identity;
	int32_t flags;
	uint32_t size;
	/* where the stream starts in the file (the joiner's kept part), and the
	file's bytes read into it */
	uint32_t start;
	uint32_t position;
	/* the stream's bytes: sent, and acknowledged */
	uint32_t sent;
	uint32_t acknowledged;
	/* the stream's last byte sent */
	boolean stream_ended;
	boolean done_sent;
	unsigned long progress_time;
	/* when the host last logged how far it is */
	unsigned long logged_time;
	/* the whole file's SHA-256: hashed this far (from 0; the part before
	`start` read apart, hash_file, as the bytes sent allow) */
	struct halo_sha256_stream sha256;
	uint32_t hashed;
	FILE *hash_file;
	boolean digest_known;
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];
	/* a deflate stream: its packer, and the file's bytes read for it */
	boolean deflating;
	struct map_share_packer packer;
	uint8_t *input;
	uint32_t input_used;
	uint32_t input_size;
	/* the level: this second's frames, and those the CPU held it back in;
	the file's bytes taken at its start; the deflated rate it is tried
	against; when stored blocks are tried or kept until */
	unsigned long level_time;
	short frames;
	short cpu_held_frames;
	boolean cpu_held;
	uint32_t period_taken;
	unsigned long deflated_rate;
	unsigned long stored_until;
	boolean stored_trial;
	unsigned long long started_us;
};

/* the SHA-256 of the file a host last sent whole: a later upload of it (a
second joiner, a resumed one) hashes nothing */
struct map_share_digest_cache
{
	boolean valid;
	char path[PATH_BYTES];
	uint32_t size;
	int32_t identity;
	long modified;
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];
};

struct map_share_host
{
	struct map_share_upload uploads[MAP_SHARE_MAXIMUM_UPLOADS];
	short next_upload;
	unsigned long rate_time;
	unsigned long rate_budget;
	/* the CPU time the uploads may take: their share of the time passing,
	saved up to MAXIMUM_CPU_ALLOWANCE_MICROSECONDS, less what they took */
	unsigned long long cpu_time;
	unsigned long long cpu_allowance;
	/* what they took since it was last logged, in all and at most a frame */
	unsigned long long cpu_used;
	unsigned long long cpu_used_most;
	unsigned long cpu_logged_time;
	struct map_share_digest_cache digest_cache;
};

struct map_share_download
{
	short state;
	struct network_game_client *client;
	/* the host's level name, and its map's file name */
	char level_name[0x80];
	char name[MAP_SHARE_NAME_BYTES];
	uint32_t identity;
	boolean replacing;
	uint32_t size;
	int32_t flags;
	unsigned long state_time;
	unsigned long progress_time;
	unsigned long shown_time;
	/* the question is whether to turn PC maps on for a map this machine has
	(map_share_client_offer_pc_maps): nothing is downloaded */
	boolean pc_maps_only;
	/* PC maps is turned on once the map is in place (the player said yes to
	a question that said so) */
	boolean turn_on_pc_maps;
	FILE *file;
	char temporary_path[PATH_BYTES];
	struct map_share_receiver receiver;
	/* the host's name (its game's) and whether the game was joined from
	the public lobby (a stranger's: the question warns) */
	char host_name[40];
	boolean public_game;
	/* what this joiner said it can do (its query's and start's) */
	uint32_t capabilities;
	/* the kept part of an earlier download it continues from (0: none),
	as written down, and read back this far */
	uint32_t resume_from;
	struct map_share_resume kept;
	uint32_t checked;
	/* the bytes in hand when the resume record was last written */
	uint32_t saved;
	/* the download is damaged (or the card full): not kept */
	boolean discard;
	unsigned long long started_us;
	/* what the player is told on leaving */
	char message[600];
};

/* ---------- prototypes */

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
void platform_show_message(char const *title, char const *message);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
void platform_show_progress(char const *title, char const *text);
int platform_progress_cancelled(void);
unsigned long system_milliseconds(void);
int halo_cache_map_in_use(char const *name);
boolean cache_files_forget_cached_map(char const *map_name);
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);
/* main.c's: the Vita's settings panel sets a row as its own (vita_settings_set,
which applies and saves it); and the settings' generation the readers watch */
extern int (*halo_test_setting_hook)(const char *variable, const char *value);
extern volatile unsigned long halo_settings_generation;
/* (stdlib.h's, hidden by __STRICT_ANSI__) */
int setenv(const char *name, const char *value, int overwrite);

/* ---------- globals */

static struct map_share_host map_share_host;
static struct map_share_download map_share_download;

/* ---------- private code: both */

static unsigned long map_share_environment_number(
	char const *name,
	unsigned long default_value)
{
	char const *value = getenv(name);

	return value && *value ? strtoul(value, NULL, 10) : default_value;
}

static void map_share_size_text(
	char *text,
	long text_size,
	uint32_t bytes)
{
	snprintf(text, (size_t)text_size, "%lu.%lu MB", (unsigned long)(bytes >> 20),
		(unsigned long)(((bytes & 0xFFFFFUL) * 10) >> 20));

	return;
}

/* the maps folder's file `name``extension` */
static boolean map_share_path(
	char *path,
	char const *name,
	char const *extension)
{
	char const *directory = cache_files_map_directory();

	if (strlen(directory) + strlen(name) + strlen(extension) + strlen(REPLACED_EXTENSION) >= PATH_BYTES)
	{
		return FALSE;
	}
	snprintf(path, PATH_BYTES, "%s%s%s", directory, name, extension);

	return TRUE;
}

static boolean map_share_file_exists(
	char const *path)
{
	FILE *file = fopen(path, "rb");

	if (file)
	{
		fclose(file);
	}

	return file != NULL;
}

/* PC maps turned on as the settings panel turns it on (on the Vita saved
in settings.txt, the panel's row showing it); elsewhere the variable */
static void map_share_pc_maps_turn_on(
	void)
{
	if (!(halo_test_setting_hook && halo_test_setting_hook("HALO_CUSTOM_EDITION", "1")))
	{
		setenv("HALO_CUSTOM_EDITION", "1", 1);
	}
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	network_event("map share: PC maps turned on");

	return;
}

/* Halo Custom Edition's resource maps not in the maps folder, named in
`missing` (", " between them) */
static void map_share_resource_maps_absent(
	char *missing,
	long missing_size)
{
	static char const *const names[] = { "bitmaps", "sounds", "loc" };
	char path[PATH_BYTES];
	long length = 0;
	short index;

	missing[0] = 0;
	for (index = 0; index < NUMBEROF(names); index++)
	{
		if (map_share_path(path, names[index], ".map") && !map_share_file_exists(path) && length < missing_size - 1)
		{
			length += snprintf(missing + length, (size_t)(missing_size - length), "%s%s.map", length ? ", " : "", names[index]);
		}
	}

	return;
}

/* the resource maps the files `missing` names, said in a sentence */
static void map_share_resource_maps_text(
	char *text,
	long text_size,
	char const *name,
	char const *missing)
{
	snprintf(text, (size_t)text_size,
		"%s is a PC (Custom Edition) map that needs %s from Halo Custom Edition in your maps folder. Copy %s "
		"there to play it.", name, missing, strchr(missing, ',') ? "them" : "it");

	return;
}

/* ---------- private code: host */

static boolean map_share_server_send_answer(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	struct map_share_answer_message const *answer)
{
	void *message = create_network_game_message(_message_server_map_download_answer, answer, sizeof(*answer));

	return message && network_game_server_send_message_to_client_machine(server, machine, message);
}

static void map_share_server_refuse(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	char const *name,
	enum map_share_refusal reason)
{
	struct map_share_answer_message answer;

	csmemset(&answer, 0, sizeof(answer));
	answer.kind = _map_share_answer_refused;
	answer.reason = (int16_t)reason;
	csstrncpy(answer.name, name, MAP_SHARE_NAME_BYTES - 1);
	network_event("map share: refusing '%s' to a machine: %s", name, map_share_refusal_describe(reason));
	map_share_server_send_answer(server, machine, &answer);

	return;
}

static void map_share_upload_close(
	struct map_share_upload *upload)
{
	if (upload->file)
	{
		fclose(upload->file);
	}
	if (upload->hash_file)
	{
		fclose(upload->hash_file);
	}
	map_share_packer_end(&upload->packer);
	/* (the game's free, cseries.h's, takes no NULL) */
	if (upload->input)
	{
		free(upload->input);
	}
	csmemset(upload, 0, sizeof(*upload));

	return;
}

static struct map_share_upload *map_share_upload_find(
	struct network_game_server_client_machine *machine)
{
	short index;

	for (index = 0; index < MAP_SHARE_MAXIMUM_UPLOADS; index++)
	{
		if (map_share_host.uploads[index].active && map_share_host.uploads[index].machine == machine)
		{
			return &map_share_host.uploads[index];
		}
	}

	return NULL;
}

/* when the file at `path` was last written (0: unknown) */
static long map_share_file_modified(
	char const *path)
{
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA(path, &data);

	if (find == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	/* (FindClose is the XDK's CloseHandle) */
	CloseHandle(find);

	return (long)(data.ftLastWriteTime.dwLowDateTime ^ data.ftLastWriteTime.dwHighDateTime);
}

/* Whether the host serves `name`, fingerprinted `identity`, now: opens it
(*file, at `path`, its *size and offer *flags) when it does, else says why
not. */
static enum map_share_refusal map_share_server_check(
	struct network_game_server *server,
	char const *name,
	int32_t identity,
	FILE **file,
	char *path,
	uint32_t *size,
	int32_t *flags)
{
	char const *level_name = main_get_multiplayer_map_name();
	long length;
	long path_length;

	*file = NULL;
	*size = 0;
	*flags = 0;
	if (network_game_server_get_state(server, NULL) != _network_game_server_state_pregame)
	{
		return _map_share_refusal_not_in_lobby;
	}
	/* the map the game plays, and no other: one of the Xbox's own levels, a
	map turned off or not in the level list, a resource map, any other
	file, is never sent */
	if (!level_name || !level_name[0] ||
		csstrcasecmp(tag_name_strip_path(level_name), name) ||
		!map_share_name_valid(name) ||
		!custom_edition_maps_shareable(level_name))
	{
		return _map_share_refusal_not_shared;
	}
	if ((uint32_t)custom_edition_maps_network_identity(level_name) != (uint32_t)identity)
	{
		return _map_share_refusal_changed;
	}
	if (!custom_edition_cache_map_file_path(level_name, path, PATH_BYTES))
	{
		return _map_share_refusal_read_failed;
	}
	*file = fopen(path, "rb");
	if (!*file)
	{
		return _map_share_refusal_read_failed;
	}
	/* (before any other use of it) */
	setvbuf(*file, NULL, _IOFBF, FILE_BUFFER_BYTES);
	if (fseek(*file, 0, SEEK_END) != 0 || (length = ftell(*file)) < 0 || fseek(*file, 0, SEEK_SET) != 0)
	{
		fclose(*file);
		*file = NULL;
		return _map_share_refusal_read_failed;
	}
	if ((unsigned long)length > MAP_SHARE_MAXIMUM_FILE_BYTES || length < MAP_SHARE_HEADER_BYTES)
	{
		fclose(*file);
		*file = NULL;
		return _map_share_refusal_too_large;
	}
	*size = (uint32_t)length;
	if (custom_edition_cache_playable(level_name))
	{
		*flags |= 1 << _map_share_offer_custom_edition_bit;
	}
	path_length = (long)strlen(path);
	if (path_length > 5 && !csstrcasecmp(path + path_length - 5, ".yelo"))
	{
		*flags |= 1 << _map_share_offer_yelo_bit;
	}

	return _map_share_refusal_none;
}

/* the offer's flags of what the host does of the joiner's `capabilities`:
it continues from a kept part, and deflates a Custom Edition map (an Xbox
map is compressed already) */
static int32_t map_share_server_offer_flags(
	int32_t file_flags,
	uint32_t capabilities)
{
	char const *compress = getenv("HALO_MAP_SHARE_COMPRESS");
	int32_t flags = file_flags;

	if (capabilities & 1u << _map_share_capability_resume_bit)
	{
		flags |= 1 << _map_share_offer_resume_bit;
	}
	if ((capabilities & 1u << _map_share_capability_deflate_bit) &&
		TEST_FLAG(file_flags, _map_share_offer_custom_edition_bit) &&
		!(compress && !csstrcmp(compress, "0")))
	{
		flags |= 1 << _map_share_offer_deflate_bit;
	}

	return flags;
}

static void map_share_server_answer_offer(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	char const *name,
	int32_t identity,
	uint32_t size,
	int32_t flags)
{
	struct map_share_answer_message answer;

	csmemset(&answer, 0, sizeof(answer));
	answer.kind = _map_share_answer_offer;
	answer.size = (int32_t)size;
	answer.identity = identity;
	answer.flags = flags;
	csstrncpy(answer.name, name, MAP_SHARE_NAME_BYTES - 1);
	map_share_server_send_answer(server, machine, &answer);

	return;
}

static void map_share_server_query_or_start(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	struct map_share_request const *request,
	char const *name)
{
	struct map_share_upload *upload = map_share_upload_find(machine);
	struct map_share_digest_cache const *cache = &map_share_host.digest_cache;
	uint32_t capabilities = map_share_request_capabilities(request);
	enum map_share_refusal refusal;
	char path[PATH_BYTES];
	uint32_t size;
	uint32_t start = 0;
	int32_t flags;
	FILE *file;
	short index;

	/* (a machine asks again: its earlier upload ends) */
	if (upload)
	{
		map_share_upload_close(upload);
	}
	refusal = map_share_server_check(server, name, request->identity, &file, path, &size, &flags);
	if (refusal != _map_share_refusal_none)
	{
		map_share_server_refuse(server, machine, name, refusal);
		return;
	}
	flags = map_share_server_offer_flags(flags, capabilities);
	if (request->command == _map_share_command_query)
	{
		fclose(file);
		network_event("map share: offering '%s' (%lu bytes%s%s) to a machine", name, (unsigned long)size,
			TEST_FLAG(flags, _map_share_offer_resume_bit) ? ", resumable" : "",
			TEST_FLAG(flags, _map_share_offer_deflate_bit) ? ", deflated" : "");
		map_share_server_answer_offer(server, machine, name, request->identity, size, flags);
		return;
	}
	/* (a joiner continuing its kept part: from there, inside the file) */
	if (TEST_FLAG(flags, _map_share_offer_resume_bit))
	{
		start = (uint32_t)request->offset;
		if (start >= size)
		{
			fclose(file);
			map_share_server_refuse(server, machine, name, _map_share_refusal_protocol);
			return;
		}
	}

	for (index = 0; index < MAP_SHARE_MAXIMUM_UPLOADS && map_share_host.uploads[index].active; index++)
	{
	}
	if (index == MAP_SHARE_MAXIMUM_UPLOADS)
	{
		fclose(file);
		map_share_server_refuse(server, machine, name, _map_share_refusal_busy);
		return;
	}
	upload = &map_share_host.uploads[index];
	csmemset(upload, 0, sizeof(*upload));
	upload->active = TRUE;
	upload->machine = machine;
	upload->machine_index = NONE;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
	{
		if (network_game_server_get_client_machine_at_index(server, index) == machine)
		{
			upload->machine_index = index;
		}
	}
	upload->connection = network_game_server_get_client_connection(machine);
	upload->file = file;
	csstrncpy(upload->path, path, sizeof(upload->path) - 1);
	csstrncpy(upload->name, name, MAP_SHARE_NAME_BYTES - 1);
	upload->identity = request->identity;
	upload->size = size;
	upload->start = upload->position = start;
	upload->progress_time = upload->level_time = system_milliseconds();
	upload->started_us = vita_host_time_us();
	if (start && fseek(file, (long)start, SEEK_SET) != 0)
	{
		map_share_upload_close(upload);
		map_share_server_refuse(server, machine, name, _map_share_refusal_read_failed);
		return;
	}
	/* (the file sent whole before, unchanged: its SHA-256 is known) */
	if (cache->valid && !csstrcmp(cache->path, path) && cache->size == size && cache->identity == request->identity &&
		cache->modified == map_share_file_modified(path))
	{
		upload->digest_known = TRUE;
		csmemcpy(upload->digest, cache->digest, sizeof(upload->digest));
	}
	halo_sha256_begin(&upload->sha256);
	/* deflated: its state and reads (else as the file's own bytes, which the
	start's answer then says) */
	if (TEST_FLAG(flags, _map_share_offer_deflate_bit))
	{
		upload->input = malloc(DEFLATE_INPUT_BYTES);
		if (upload->input && map_share_packer_begin(&upload->packer))
		{
			upload->deflating = TRUE;
		}
		else
		{
			network_event("map share: no memory to deflate '%s': sending it as it is", name);
			if (upload->input)
			{
				free(upload->input);
			}
			upload->input = NULL;
			flags &= ~(1 << _map_share_offer_deflate_bit);
		}
	}
	{
		char const *level = getenv("HALO_MAP_SHARE_LEVEL");

		if (upload->deflating && level && *level)
		{
			upload->packer.level = level[0] == '0' ? 0 : MAP_SHARE_DEFLATE_LEVEL;
		}
	}
	upload->flags = flags;
	/* (the file's own bytes: the stream's offsets are the file's) */
	if (!upload->deflating)
	{
		upload->sent = upload->acknowledged = start;
	}
	network_event("map share: sending '%s' (%lu bytes%s%s%s) to a machine", name, (unsigned long)size,
		upload->deflating ? ", deflated" : "", start ? ", from " : "", start ? "its kept part" : "");
	if (start)
	{
		network_event("map share: the machine has %lu bytes of '%s'", (unsigned long)start, name);
	}
	/* (the offer again: the start is taken) */
	map_share_server_answer_offer(server, machine, name, request->identity, size, flags);

	return;
}

/* the file's next bytes read for the stream (and hashed when the hash is
there); FALSE when they cannot be */
static boolean map_share_upload_read(
	struct map_share_upload *upload,
	uint8_t *buffer,
	uint32_t length)
{
	if (fread(buffer, 1, length, upload->file) != length)
	{
		return FALSE;
	}
	if (!upload->digest_known && upload->hashed == upload->position)
	{
		halo_sha256_add(&upload->sha256, buffer, length);
		upload->hashed += length;
	}
	upload->position += length;

	return TRUE;
}

/* The upload's next data message (`data`): the file's own bytes, or the
deflate stream's; FALSE when the file cannot be read or deflated. */
static boolean map_share_upload_fill(
	struct map_share_upload *upload,
	struct map_share_data_message *data)
{
	uint32_t filled = 0;

	csmemset(data, 0, sizeof(*data));
	data->offset = (int32_t)upload->sent;
	if (!upload->deflating)
	{
		filled = MIN(upload->size - upload->position, (uint32_t)MAP_SHARE_CHUNK_BYTES);
		if (!map_share_upload_read(upload, data->data, filled))
		{
			return FALSE;
		}
		upload->stream_ended = upload->position == upload->size;
	}
	else
	{
		short stuck = 0;

		while (filled < MAP_SHARE_CHUNK_BYTES && !upload->packer.ended)
		{
			uint32_t taken;
			long made;

			if (upload->input_used == upload->input_size && upload->position < upload->size)
			{
				uint32_t length = MIN(upload->size - upload->position, (uint32_t)DEFLATE_INPUT_BYTES);

				if (!map_share_upload_read(upload, upload->input, length))
				{
					return FALSE;
				}
				upload->input_used = 0;
				upload->input_size = length;
			}
			made = map_share_packer_pack(&upload->packer, upload->input + upload->input_used,
				upload->input_size - upload->input_used, upload->position == upload->size, &taken,
				data->data + filled, MAP_SHARE_CHUNK_BYTES - filled);
			if (made < 0 || (stuck = made || taken ? 0 : stuck + 1) > 4)
			{
				return FALSE;
			}
			upload->input_used += taken;
			upload->period_taken += taken;
			filled += (uint32_t)made;
		}
		upload->stream_ended = upload->packer.ended;
	}
	data->length = (int16_t)filled;

	return TRUE;
}

/* The file's bytes the joiner has (acknowledged): a deflate stream's
counted as the file's bytes taken so far, in proportion. */
static uint32_t map_share_upload_progress(
	struct map_share_upload const *upload)
{
	uint32_t taken;

	if (!upload->deflating)
	{
		return upload->acknowledged;
	}
	if (upload->done_sent && upload->acknowledged == upload->sent)
	{
		return upload->size;
	}
	taken = upload->position - upload->start - (upload->input_size - upload->input_used);

	return upload->start + (upload->sent ?
		(uint32_t)((unsigned long long)taken * upload->acknowledged / upload->sent) : 0);
}

/* Whether the upload may send a message now: its window, its connection's
queue, the host's rate. */
static boolean map_share_upload_may_send(
	struct map_share_upload const *upload)
{
	return upload->active && !upload->stream_ended &&
		upload->sent - upload->acknowledged < MAP_SHARE_WINDOW_BYTES &&
		map_share_host.rate_budget >= MAP_SHARE_CHUNK_BYTES &&
		network_connection_reliable_queued_bytes(upload->connection) < MAP_SHARE_QUEUE_BYTES;
}

/* Sends the upload's next message; FALSE when it ended (refused). */
static boolean map_share_upload_send(
	struct network_game_server *server,
	struct map_share_upload *upload)
{
	/* (static: 3 KB, and one frame sends at a time) */
	static struct map_share_data_message data;
	void *message;

	if (!map_share_upload_fill(upload, &data))
	{
		network_event("map share: '%s' could not be read%s", upload->name, upload->deflating ? " or deflated" : "");
		map_share_server_refuse(server, upload->machine, upload->name, _map_share_refusal_read_failed);
		return FALSE;
	}
	if (!data.length)
	{
		return TRUE;
	}
	message = create_network_game_message(_message_server_map_download_data, &data, sizeof(data));
	if (!message || !network_game_server_send_message_to_client_machine(server, upload->machine, message))
	{
		network_event("map share: sending '%s' to a machine failed", upload->name);
		return FALSE;
	}
	upload->sent += (uint32_t)data.length;
	map_share_host.rate_budget -= MIN(map_share_host.rate_budget, (unsigned long)data.length);

	return TRUE;
}

/* The hash catching up with the bytes sent (the part before a resumed
upload's start, read apart), until `until_us` (a read at least); FALSE when the
file cannot be read. */
static boolean map_share_upload_hash(
	struct map_share_upload *upload,
	unsigned long long until_us)
{
	/* (static: 64 KB, and one frame hashes at a time) */
	static uint8_t buffer[READ_BACK_BYTES];

	if (upload->digest_known || upload->hashed >= upload->position)
	{
		return TRUE;
	}
	if (!upload->hash_file)
	{
		upload->hash_file = fopen(upload->path, "rb");
		if (!upload->hash_file || fseek(upload->hash_file, (long)upload->hashed, SEEK_SET) != 0)
		{
			return FALSE;
		}
	}
	/* (at least a read a frame, so that it ends) */
	do
	{
		uint32_t length = MIN(upload->position - upload->hashed, (uint32_t)sizeof(buffer));

		if (fread(buffer, 1, length, upload->hash_file) != length)
		{
			return FALSE;
		}
		halo_sha256_add(&upload->sha256, buffer, length);
		upload->hashed += length;
	}
	while (upload->hashed < upload->position && vita_host_time_us() < until_us);
	if (upload->hashed == upload->position)
	{
		fclose(upload->hash_file);
		upload->hash_file = NULL;
	}

	return TRUE;
}

/* The done (the whole file's SHA-256) once the stream has been sent and
the file hashed; FALSE when it could not be sent. */
static boolean map_share_upload_finish(
	struct network_game_server *server,
	struct map_share_upload *upload)
{
	struct map_share_digest_cache *cache = &map_share_host.digest_cache;
	struct map_share_answer_message answer;
	unsigned long long elapsed;

	if (!upload->stream_ended || upload->done_sent || (!upload->digest_known && upload->hashed < upload->size))
	{
		return TRUE;
	}
	if (!upload->digest_known)
	{
		halo_sha256_end(&upload->sha256, upload->digest);
		upload->digest_known = TRUE;
		cache->valid = TRUE;
		csstrncpy(cache->path, upload->path, sizeof(cache->path) - 1);
		cache->size = upload->size;
		cache->identity = upload->identity;
		cache->modified = map_share_file_modified(upload->path);
		csmemcpy(cache->digest, upload->digest, sizeof(cache->digest));
	}
	csmemset(&answer, 0, sizeof(answer));
	answer.kind = _map_share_answer_done;
	answer.size = (int32_t)upload->size;
	answer.identity = upload->identity;
	answer.flags = upload->flags;
	csstrncpy(answer.name, upload->name, MAP_SHARE_NAME_BYTES - 1);
	csmemcpy(answer.digest, upload->digest, sizeof(answer.digest));
	upload->done_sent = TRUE;
	fclose(upload->file);
	upload->file = NULL;
	elapsed = vita_host_time_us() - upload->started_us;
	network_event("map share: '%s' sent: %lu bytes of the file as %lu in %lu ms (%lu KB/s of the file)", upload->name,
		(unsigned long)(upload->size - upload->start), (unsigned long)(upload->sent - (upload->deflating ? 0 : upload->start)),
		(unsigned long)(elapsed / 1000),
		(unsigned long)(elapsed ? (unsigned long long)(upload->size - upload->start) * 1000000 / 1024 / elapsed : 0));

	return map_share_server_send_answer(server, upload->machine, &answer);
}

/* A deflating upload's level, looked at every second: stored blocks tried
when the CPU held deflate back in most of its frames, kept while they move
the file faster */
static void map_share_upload_level(
	struct map_share_upload *upload,
	unsigned long now)
{
	unsigned long elapsed = now - upload->level_time;
	unsigned long rate;
	char const *fixed = getenv("HALO_MAP_SHARE_LEVEL");

	if (!upload->deflating || upload->packer.finishing || (fixed && *fixed) || elapsed < LEVEL_PERIOD_MILLISECONDS)
	{
		return;
	}
	rate = (unsigned long)((unsigned long long)upload->period_taken * 1000 / elapsed);
	if (upload->packer.level)
	{
		if (upload->cpu_held_frames * 2 > upload->frames)
		{
			upload->deflated_rate = rate;
			upload->packer.level = 0;
			upload->stored_trial = TRUE;
			upload->stored_until = now + LEVEL_TRIAL_MILLISECONDS;
			network_event("map share: deflating '%s' is held back by the CPU (%lu KB/s): trying stored blocks",
				upload->name, rate / 1024);
		}
	}
	else if ((long)(now - upload->stored_until) >= 0)
	{
		if (upload->stored_trial && rate > upload->deflated_rate + upload->deflated_rate / 10)
		{
			upload->stored_until = now + LEVEL_HOLD_MILLISECONDS;
			network_event("map share: stored blocks of '%s' are faster (%lu KB/s): kept", upload->name, rate / 1024);
		}
		else
		{
			upload->packer.level = MAP_SHARE_DEFLATE_LEVEL;
			network_event("map share: deflating '%s' again (stored: %lu KB/s)", upload->name, rate / 1024);
		}
		upload->stored_trial = FALSE;
	}
	upload->level_time = now;
	upload->period_taken = 0;
	upload->frames = upload->cpu_held_frames = 0;

	return;
}

/* ---------- private code: joiner */

/* a file's kind among an offer's flags (what a resume record keeps) */
#define FILE_KIND_FLAGS (FLAG(_map_share_offer_yelo_bit) | FLAG(_map_share_offer_custom_edition_bit))

static boolean map_share_setting_off(
	char const *name)
{
	char const *value = getenv(name);

	return value && !csstrcmp(value, "0");
}

/* what this joiner can do (HALO_MAP_SHARE_RESUME=0, HALO_MAP_SHARE_COMPRESS=0
turn either off) */
static uint32_t map_share_client_capabilities(
	void)
{
	uint32_t capabilities = 0;

	if (!map_share_setting_off("HALO_MAP_SHARE_RESUME"))
	{
		capabilities |= 1u << _map_share_capability_resume_bit;
	}
	if (!map_share_setting_off("HALO_MAP_SHARE_COMPRESS"))
	{
		capabilities |= 1u << _map_share_capability_deflate_bit;
	}

	return capabilities;
}

static boolean map_share_client_send(
	struct network_game_client *client,
	short command,
	short reason,
	uint32_t offset)
{
	struct map_share_download *download = &map_share_download;
	struct map_share_request request;
	void *message;

	csmemset(&request, 0, sizeof(request));
	request.command = command;
	request.reason = reason;
	request.offset = (int32_t)offset;
	request.identity = (int32_t)download->identity;
	csstrncpy(request.name, download->name, MAP_SHARE_NAME_BYTES - 1);
	message = create_network_game_message(_message_client_map_download, &request, sizeof(request));

	return message && network_game_client_send_to_server(client, message);
}

/* a map's kept part (<name>.download and <name>.resume) deleted */
static void map_share_kept_part_delete(
	char const *name)
{
	char path[PATH_BYTES];

	if (map_share_path(path, name, TEMPORARY_EXTENSION))
	{
		remove(path);
	}
	if (map_share_path(path, name, RESUME_EXTENSION))
	{
		remove(path);
	}

	return;
}

/* the resume record `name` has (FALSE: none that can be read) */
static boolean map_share_resume_read(
	char const *name,
	struct map_share_resume *resume)
{
	uint8_t bytes[MAP_SHARE_RESUME_RECORD_BYTES];
	char path[PATH_BYTES];
	FILE *file;
	size_t length = 0;

	if (!map_share_path(path, name, RESUME_EXTENSION) || !(file = fopen(path, "rb")))
	{
		return FALSE;
	}
	length = fread(bytes, 1, sizeof(bytes), file);
	fclose(file);

	return length == sizeof(bytes) && map_share_resume_decode(bytes, resume);
}

/* The download's resume record written: the file's bytes in hand, flushed
to the card first, and their SHA-256; FALSE when it could not be. */
static boolean map_share_client_save_resume(
	void)
{
	struct map_share_download *download = &map_share_download;
	struct map_share_resume resume;
	uint8_t bytes[MAP_SHARE_RESUME_RECORD_BYTES];
	char path[PATH_BYTES];
	FILE *file;
	boolean written;

	if (!download->file || fflush(download->file) != 0 || ferror(download->file) ||
		!map_share_path(path, download->name, RESUME_EXTENSION))
	{
		return FALSE;
	}
	csmemset(&resume, 0, sizeof(resume));
	csstrncpy(resume.name, download->name, MAP_SHARE_NAME_BYTES - 1);
	resume.identity = download->identity;
	resume.size = download->size;
	resume.flags = (uint32_t)download->flags & FILE_KIND_FLAGS;
	resume.kept = download->receiver.received;
	map_share_receiver_digest_so_far(&download->receiver, resume.kept_digest);
	resume.saved_time = (uint32_t)time(NULL);
	map_share_resume_encode(&resume, bytes);
	file = fopen(path, "wb");
	if (!file)
	{
		return FALSE;
	}
	written = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
	written = fclose(file) == 0 && written;
	download->saved = download->receiver.received;

	return written;
}

/* The download's file closed: kept, with its resume record, when it can be
continued (not damaged, long enough, read back first when it was a kept
part); else deleted. */
static void map_share_client_close_file(
	void)
{
	struct map_share_download *download = &map_share_download;
	boolean keep = FALSE;

	if (download->file)
	{
		if (download->discard)
		{
		}
		else if (download->state == _client_checking)
		{
			/* (its record as it was) */
			keep = TRUE;
		}
		else if ((download->state == _client_starting || download->state == _client_receiving) &&
			download->receiver.received >= MAP_SHARE_RESUME_MINIMUM_BYTES && download->receiver.received < download->size &&
			!map_share_setting_off("HALO_MAP_SHARE_RESUME"))
		{
			keep = map_share_client_save_resume();
		}
		fclose(download->file);
		download->file = NULL;
		if (keep)
		{
			network_event("map share: %lu bytes of '%s' kept, to continue later", (unsigned long)(
				download->state == _client_checking ? download->resume_from : download->receiver.received), download->name);
		}
	}
	if (!keep && download->temporary_path[0])
	{
		map_share_kept_part_delete(download->name);
	}
	download->temporary_path[0] = 0;
	map_share_receiver_end(&download->receiver);

	return;
}

static void map_share_client_reset(
	void)
{
	struct map_share_download *download = &map_share_download;

	map_share_client_close_file();
	if (download->state == _client_asking)
	{
		platform_ask_question(NULL, NULL);
	}
	if (download->state == _client_checking || download->state == _client_starting || download->state == _client_receiving)
	{
		platform_show_progress(NULL, NULL);
	}
	csmemset(download, 0, sizeof(*download));

	return;
}

/* the old refusal (network_client_manager.c's), for a joiner that does not
download, and `why` it does not */
static void map_share_client_refusal_text(
	char *text,
	long text_size,
	char const *why)
{
	struct map_share_download *download = &map_share_download;

	if (download->pc_maps_only)
	{
		snprintf(text, (size_t)text_size, "The host is playing the PC (Custom Edition) map %s, and PC maps is off. "
			"Turn on PC maps (Multiplayer > Modded maps) to play it.\n\n%s", download->name, why);
		return;
	}
	snprintf(
		text,
		(size_t)text_size,
		!download->replacing ?
			"The host is playing the custom map %s, which isn't in your maps folder.\n\n%s" :
			"The host's custom map %s isn't the same as yours. Copy the host's map to your maps folder.\n\n%s",
		download->name,
		why);

	return;
}

/* Gives up: the host is told when it is sending, the file kept to continue
(else deleted), and the joiner leaves the game at its next frame, telling
the player `why`. */
static void map_share_client_fail(
	char const *why)
{
	struct map_share_download *download = &map_share_download;

	network_event("map share: '%s' not downloaded: %s", download->name, why);
	if (download->state == _client_asking)
	{
		platform_ask_question(NULL, NULL);
	}
	/* (the host is told while it still has bytes to send) */
	if ((download->state == _client_starting || download->state == _client_receiving) && download->client &&
		!map_share_receiver_complete(&download->receiver))
	{
		map_share_client_send(download->client, _map_share_command_cancel, _map_share_refusal_cancelled, download->receiver.stream_received);
	}
	map_share_client_close_file();
	if (download->state == _client_checking || download->state == _client_starting || download->state == _client_receiving)
	{
		platform_show_progress(NULL, NULL);
	}
	csstrncpy(download->message, why, sizeof(download->message) - 1);
	download->state = _client_leaving;

	return;
}

/* gives up on a download that is damaged (or cannot be written): its file
is not kept */
static void map_share_client_fail_damaged(
	char const *why)
{
	map_share_download.discard = TRUE;
	map_share_client_fail(why);

	return;
}

/* the file a download has written, checked from the disk: its length and
cache header */
static boolean map_share_client_check_file(
	char const *path,
	char *why,
	long why_size)
{
	struct map_share_download *download = &map_share_download;
	static uint8_t header[MAP_SHARE_HEADER_BYTES];
	enum map_share_header_status status;
	FILE *file = fopen(path, "rb");
	long length = -1;
	int custom_edition;

	if (file)
	{
		if (fseek(file, 0, SEEK_END) == 0)
		{
			length = ftell(file);
		}
		if (length != (long)download->size ||
			fseek(file, 0, SEEK_SET) != 0 ||
			fread(header, 1, sizeof(header), file) != sizeof(header))
		{
			length = -1;
		}
		fclose(file);
	}
	if (length < 0)
	{
		snprintf(why, (size_t)why_size, "The downloaded map %s couldn't be read back.", download->name);
		return FALSE;
	}
	status = map_share_header_validate(header, download->size, download->name, &custom_edition);
	if (status != _map_share_header_ok)
	{
		snprintf(why, (size_t)why_size, "The host's map %s isn't a map this game can play (%s).",
			download->name, map_share_header_status_describe(status));
		return FALSE;
	}

	return TRUE;
}

/* the downloaded file in place of the joiner's copy: <name>.map or .yelo
(the other copy kept aside until the new one is in place) */
static boolean map_share_client_install(
	char *why,
	long why_size)
{
	struct map_share_download *download = &map_share_download;
	char const *extension = TEST_FLAG(download->flags, _map_share_offer_yelo_bit) ? ".yelo" : ".map";
	char final_path[PATH_BYTES];
	char replaced_path[PATH_BYTES];
	boolean replaced = FALSE;

	if (!map_share_path(final_path, download->name, extension) ||
		!map_share_path(replaced_path, download->name, REPLACED_EXTENSION))
	{
		snprintf(why, (size_t)why_size, "The map's name is too long for this maps folder.");
		return FALSE;
	}
	/* (nor its cache partition copy, which would be played in its place) */
	if (halo_cache_map_in_use(download->name) || !cache_files_forget_cached_map(download->name))
	{
		snprintf(why, (size_t)why_size, "Your copy of %s is in use: leave it first.", download->name);
		return FALSE;
	}
	if (map_share_file_exists(final_path))
	{
		remove(replaced_path);
		if (rename(final_path, replaced_path) != 0)
		{
			snprintf(why, (size_t)why_size, "Your copy of %s couldn't be replaced.", download->name);
			return FALSE;
		}
		replaced = TRUE;
	}
	if (rename(download->temporary_path, final_path) != 0)
	{
		if (replaced)
		{
			rename(replaced_path, final_path);
		}
		snprintf(why, (size_t)why_size, "The downloaded map %s couldn't be put in the maps folder.", download->name);
		return FALSE;
	}
	download->temporary_path[0] = 0;
	/* (its resume record, of a kept part now in place) */
	if (map_share_path(replaced_path, download->name, RESUME_EXTENSION))
	{
		remove(replaced_path);
	}
	if (replaced && map_share_path(replaced_path, download->name, REPLACED_EXTENSION))
	{
		remove(replaced_path);
	}
	/* (a copy under the other extension, of the joiner's: the loader reads
	the .map before the .yelo, custom_edition_cache.c) */
	if (map_share_path(replaced_path, download->name, extension[1] == 'y' ? ".map" : ".yelo") &&
		map_share_file_exists(replaced_path))
	{
		network_event("map share: '%s' replaces '%s'", final_path, replaced_path);
		remove(replaced_path);
	}
	network_event("map share: '%s' downloaded to '%s'", download->name, final_path);

	return TRUE;
}

/* Whether this machine cannot load the download's map as it is now
(custom_edition_maps_loadable), and then `why`, its name followed by
`what` ("was downloaded to your maps folder, but") and the reason. */
static boolean map_share_client_unloadable(
	char *why,
	long why_size,
	char const *what)
{
	struct map_share_download *download = &map_share_download;
	char missing[96];

	switch (custom_edition_maps_loadable(download->level_name, missing, sizeof(missing)))
	{
	case _custom_edition_maps_loadable:
		return FALSE;
	case _custom_edition_maps_needs_pc_maps:
		snprintf(why, (size_t)why_size, "%s %s it is a PC (Custom Edition) map, and PC maps is off. Turn on PC maps "
			"(Multiplayer > Modded maps) to play it.", download->name, what);
		break;
	case _custom_edition_maps_needs_resource_maps:
	{
		char text[256];

		map_share_resource_maps_text(text, sizeof(text), "it", missing);
		snprintf(why, (size_t)why_size, "%s %s %s", download->name, what, text);
		break;
	}
	default:
		snprintf(why, (size_t)why_size, "%s %s it isn't a multiplayer map this game can load.", download->name, what);
		break;
	}

	return TRUE;
}

/* The host's done: every byte, the digest, the fingerprint, the header
on the disk; then the map goes in place and is precached. */
static void map_share_client_finish(
	struct map_share_answer_message const *answer)
{
	struct map_share_download *download = &map_share_download;
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];
	char why[320];
	boolean missing;

	if (!map_share_receiver_complete(&download->receiver) || (uint32_t)answer->size != download->size)
	{
		map_share_client_fail_damaged("The host ended the download early.");
		return;
	}
	if (download->file && (fflush(download->file) != 0 || ferror(download->file)))
	{
		map_share_client_fail_damaged("The map couldn't be written (is the memory card full?).");
		return;
	}
	if (download->file)
	{
		fclose(download->file);
		download->file = NULL;
	}
	{
		unsigned long long elapsed = vita_host_time_us() - download->started_us;
		uint32_t moved = download->size - download->resume_from;

		network_event("map share: '%s' received: %lu bytes of the file as %lu in %lu ms (%lu KB/s of the file)%s",
			download->name, (unsigned long)moved, (unsigned long)download->receiver.stream_received -
				(download->receiver.inflater ? 0 : download->resume_from),
			(unsigned long)(elapsed / 1000), (unsigned long)(elapsed ? (unsigned long long)moved * 1000000 / 1024 / elapsed : 0),
			download->receiver.inflater ? ", deflated" : "");
	}
	/* (the whole file's: the kept part as read back, and the rest) */
	map_share_receiver_digest(&download->receiver, digest);
	if (csmemcmp(digest, answer->digest, sizeof(digest)))
	{
		map_share_client_fail_damaged("The download was damaged (its SHA-256 isn't the host's).");
		return;
	}
	if (map_share_receiver_identity(&download->receiver) != download->identity)
	{
		snprintf(why, sizeof(why), "The host sent a copy of %s that isn't the one its game plays.", download->name);
		map_share_client_fail_damaged(why);
		return;
	}
	if (!map_share_client_check_file(download->temporary_path, why, sizeof(why)) ||
		!map_share_client_install(why, sizeof(why)))
	{
		map_share_client_fail_damaged(why);
		return;
	}
	map_share_receiver_end(&download->receiver);

	/* the game's own checks, on the map in place */
	custom_edition_cache_map_identity_forget(download->name);
	if (download->turn_on_pc_maps && !halo_custom_edition_enabled())
	{
		map_share_pc_maps_turn_on();
	}
	custom_edition_maps_look_again();
	platform_show_progress(NULL, NULL);
	if (!custom_edition_maps_host_copy_matches(download->level_name, download->identity, &missing))
	{
		snprintf(why, sizeof(why), "The downloaded map %s doesn't match the host's.", download->name);
		map_share_client_fail(why);
		return;
	}
	/* kept (it is the host's map, checked), but this machine does not play
	it as it is: told why (a game on it would stop as a damaged disc) */
	if (map_share_client_unloadable(why, sizeof(why), "was downloaded to your maps folder, but"))
	{
		map_share_client_fail(why);
		return;
	}

	network_event("map share: '%s' verified; precaching map '%s'...", download->name, download->level_name);
	main_set_multiplayer_map_name(download->level_name);
	download->state = _client_idle;
	download->client = NULL;

	return;
}

static void map_share_client_ask(
	void)
{
	struct map_share_download *download = &map_share_download;
	char size_text[32];
	char kept_text[96];
	char text[800];

	map_share_size_text(size_text, sizeof(size_text), download->size);
	kept_text[0] = 0;
	if (download->resume_from)
	{
		char kept_size[32];

		map_share_size_text(kept_size, sizeof(kept_size), download->resume_from);
		snprintf(kept_text, sizeof(kept_text), "\n\n(%s of it was downloaded before: the download goes on from there.)",
			kept_size);
	}
	if (download->pc_maps_only)
	{
		snprintf(text, sizeof(text), "The host is playing the PC (Custom Edition) map %s, which is in your maps "
			"folder, but PC maps is off.\n\nTurn on PC maps and play it?", download->name);
	}
	else if (download->turn_on_pc_maps)
	{
		char missing[64];

		/* (the resource maps most Custom Edition maps need: which this one
		does is known once it is here) */
		missing[0] = 0;
		map_share_resource_maps_absent(missing, sizeof(missing));
		snprintf(
			text,
			sizeof(text),
			"The host is playing the PC (Custom Edition) map %s (%s), which %s.\n\nDownload it from %s and turn on PC maps?%s%s%s%s",
			download->name,
			size_text,
			!download->replacing ? "isn't in your maps folder" : "isn't the same as yours",
			download->host_name,
			missing[0] ? "\n\n(Most PC maps also need " : "",
			missing,
			missing[0] ? " from Halo Custom Edition, which your maps folder lacks.)" : "",
			kept_text);
	}
	else
	{
		snprintf(
			text,
			sizeof(text),
			!download->replacing ?
				"The host is playing the custom map %s (%s), which isn't in your maps folder.\n\nDownload it from %s?%s" :
				"The host's custom map %s (%s) isn't the same as yours.\n\nDownload %s's copy in place of yours?%s",
			download->name,
			size_text,
			download->host_name,
			kept_text);
	}
	/* (a public lobby's game: its host is a stranger) */
	if (download->public_game && !download->pc_maps_only)
	{
		size_t length = strlen(text);

		snprintf(text + length, sizeof(text) - length, "\n\nThis is a public game: only accept maps from players you trust.");
	}
	network_event("map share: asking the player about '%s' (%lu bytes%s%s) from '%s'%s", download->name,
		(unsigned long)download->size, download->pc_maps_only ? ", PC maps only" : download->turn_on_pc_maps ?
		", and PC maps" : "", download->resume_from ? ", a part kept" : "", download->host_name,
		download->public_game ? ", a public game" : "");
	if (!getenv("HALO_MAP_SHARE_ANSWER"))
	{
		platform_ask_question("Halo: custom map", text);
	}
	download->state = _client_asking;
	download->state_time = system_milliseconds();

	return;
}

/* the player's answer: -1 not yet, 0 no, 1 yes */
static int map_share_client_answer(
	void)
{
	char const *answer = getenv("HALO_MAP_SHARE_ANSWER");

	if (answer)
	{
		return !csstrcasecmp(answer, "yes") || !csstrcmp(answer, "1");
	}

	return platform_question_answer();
}

/* the bytes of the file the receiver passes on, written */
static int map_share_client_write(
	void *context,
	uint8_t const *data,
	uint32_t length)
{
	return fwrite(data, 1, length, (FILE *)context) == length;
}

/* The kept part of an earlier download of the offered file, as its resume
record says (0: none, or another file's, which the start deletes). */
static uint32_t map_share_client_kept_part(
	void)
{
	struct map_share_download *download = &map_share_download;
	struct map_share_resume *kept = &download->kept;
	char path[PATH_BYTES];
	FILE *file;
	long length = -1;

	if (!TEST_FLAG(download->flags, _map_share_offer_resume_bit) || !map_share_resume_read(download->name, kept))
	{
		return 0;
	}
	if (csstrcasecmp(kept->name, download->name) || kept->identity != download->identity || kept->size != download->size ||
		kept->flags != ((uint32_t)download->flags & FILE_KIND_FLAGS) ||
		kept->kept < MAP_SHARE_RESUME_MINIMUM_BYTES || kept->kept >= download->size)
	{
		network_event("map share: the kept part of '%s' is of another file: it starts over", download->name);
		return 0;
	}
	if (map_share_path(path, download->name, TEMPORARY_EXTENSION) && (file = fopen(path, "rb")))
	{
		if (fseek(file, 0, SEEK_END) == 0)
		{
			length = ftell(file);
		}
		fclose(file);
	}
	if (length < (long)kept->kept)
	{
		return 0;
	}

	return kept->kept;
}

/* The stream begins: deflated when the host offered it and the inflater
can be had, from the kept part's end; the start sent. */
static void map_share_client_begin_stream(
	void)
{
	struct map_share_download *download = &map_share_download;
	boolean deflate = TEST_FLAG(download->flags, _map_share_offer_deflate_bit);

	if (deflate && !map_share_receiver_start_stream(&download->receiver, TRUE))
	{
		network_event("map share: no memory to inflate '%s': asking for it as it is", download->name);
		deflate = FALSE;
	}
	if (!deflate)
	{
		download->capabilities &= ~(1u << _map_share_capability_deflate_bit);
		map_share_receiver_start_stream(&download->receiver, FALSE);
	}
	if (download->resume_from && fseek(download->file, (long)download->resume_from, SEEK_SET) != 0)
	{
		map_share_client_fail_damaged("The map couldn't be written to the maps folder.");
		return;
	}
	if (!map_share_client_send(download->client, _map_share_command_start, (short)download->capabilities, download->resume_from))
	{
		map_share_client_fail("The host couldn't be asked for the map.");
		return;
	}
	download->state = _client_starting;
	download->state_time = download->progress_time = system_milliseconds();
	download->saved = download->receiver.received;
	download->started_us = vita_host_time_us();
	network_event("map share: downloading '%s' (%lu bytes%s%s)", download->name, (unsigned long)download->size,
		deflate ? ", deflated" : "", download->resume_from ? ", from its kept part" : "");
	if (download->resume_from)
	{
		network_event("map share: resuming '%s' at %lu bytes", download->name, (unsigned long)download->resume_from);
	}

	return;
}

/* the download from 0: a new file */
static void map_share_client_start_over(
	void)
{
	struct map_share_download *download = &map_share_download;

	if (download->file)
	{
		fclose(download->file);
		download->file = NULL;
	}
	map_share_receiver_end(&download->receiver);
	map_share_kept_part_delete(download->name);
	download->resume_from = 0;
	download->file = fopen(download->temporary_path, "wb");
	if (!download->file)
	{
		download->temporary_path[0] = 0;
		map_share_client_fail("The map couldn't be written to the maps folder.");
		return;
	}
	setvbuf(download->file, NULL, _IOFBF, FILE_BUFFER_BYTES);
	map_share_receiver_begin(&download->receiver, download->size);
	map_share_client_begin_stream();

	return;
}

/* the offer taken: room, a file (the kept part read back first), the start */
static void map_share_client_start(
	void)
{
	struct map_share_download *download = &map_share_download;
	ULARGE_INTEGER free_bytes;
	char size_text[32];
	char why[320];

	if (GetDiskFreeSpaceExA(cache_files_map_directory(), &free_bytes, NULL, NULL))
	{
		unsigned long long needed = (unsigned long long)(download->size - download->resume_from) + FREE_SPACE_MARGIN_BYTES;
		unsigned long long available = (unsigned long long)free_bytes.HighPart << 32 | free_bytes.LowPart;

		if (available < needed)
		{
			map_share_size_text(size_text, sizeof(size_text), download->size);
			snprintf(why, sizeof(why), "There isn't room for the host's map %s (%s) on the memory card.", download->name, size_text);
			map_share_client_fail(why);
			return;
		}
	}
	else
	{
		network_event("map share: the free space could not be asked; trying");
	}
	if (!map_share_path(download->temporary_path, download->name, TEMPORARY_EXTENSION))
	{
		download->temporary_path[0] = 0;
		map_share_client_fail("The map's name is too long for this maps folder.");
		return;
	}
	if (download->resume_from)
	{
		download->file = fopen(download->temporary_path, "r+b");
		if (download->file)
		{
			setvbuf(download->file, NULL, _IOFBF, FILE_BUFFER_BYTES);
			map_share_receiver_begin(&download->receiver, download->size);
			download->checked = 0;
			download->state = _client_checking;
			download->state_time = download->progress_time = system_milliseconds();
			download->shown_time = 0;
			download->started_us = vita_host_time_us();
			network_event("map share: reading back the %lu bytes of '%s' kept", (unsigned long)download->resume_from, download->name);
			return;
		}
	}
	map_share_client_start_over();

	return;
}

/* The kept part read back, as much as a frame's share allows: once all of
it, checked against its record's SHA-256 (else the download starts over),
then the start. */
static void map_share_client_read_back(
	void)
{
	struct map_share_download *download = &map_share_download;
	/* (static: 64 KB, and one frame reads at a time) */
	static uint8_t buffer[READ_BACK_BYTES];
	unsigned long long until_us = vita_host_time_us() + READ_BACK_MICROSECONDS;
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];

	do
	{
		uint32_t length = MIN(download->resume_from - download->checked, (uint32_t)sizeof(buffer));

		if (fread(buffer, 1, length, download->file) != length)
		{
			network_event("map share: the kept part of '%s' couldn't be read back: it starts over", download->name);
			map_share_client_start_over();
			return;
		}
		map_share_receiver_add(&download->receiver, buffer, length);
		download->checked += length;
	}
	while (download->checked < download->resume_from && vita_host_time_us() < until_us);
	download->progress_time = system_milliseconds();
	if (download->checked < download->resume_from)
	{
		return;
	}
	map_share_receiver_digest_so_far(&download->receiver, digest);
	if (csmemcmp(digest, download->kept.kept_digest, sizeof(digest)))
	{
		network_event("map share: the kept part of '%s' isn't what was written down: it starts over", download->name);
		map_share_client_start_over();
		return;
	}
	network_event("map share: the kept part of '%s' read back in %lu ms", download->name,
		(unsigned long)((vita_host_time_us() - download->started_us) / 1000));
	map_share_client_begin_stream();

	return;
}

static void map_share_client_show_progress(
	void)
{
	struct map_share_download *download = &map_share_download;
	char received_text[32];
	char size_text[32];
	char text[256];
	uint32_t done = download->state == _client_checking ? download->checked : download->receiver.received;
	uint32_t total = download->state == _client_checking ? download->resume_from : download->size;
	unsigned long percent = total ? (unsigned long)((unsigned long long)done * 100 / total) : 0;

	map_share_size_text(received_text, sizeof(received_text), done);
	map_share_size_text(size_text, sizeof(size_text), total);
	snprintf(text, sizeof(text), download->state == _client_checking ?
		"Checking the part of %s downloaded before\n\n%s of %s (%lu%%)" :
		"Downloading %s from the host\n\n%s of %s (%lu%%)",
		download->name, received_text, size_text, percent);
	platform_show_progress("Halo: custom map", text);

	return;
}

/* Whether the joiner's host was reached from the public lobby (a
stranger's game: p2p.c knows how each internet play peer was joined; a
host on the LAN or in the ad hoc group is not one), and its name in
`host_name` (its game's, which is the host's machine name:
network_server_manager.c), from the game settings `game` it sent. */
static boolean map_share_client_host(
	struct network_game_client *client,
	struct network_game const *game,
	char *host_name,
	long host_name_size)
{
	struct transport_address address;
	unsigned long host_order;
	int origin;

	map_share_host_name_text(host_name, host_name_size, (uint16_t const *)game->name, NUMBEROF(game->name));
	csmemset(&address, 0, sizeof(address));
	network_game_client_get_remote_server_address(client, &address);
	host_order = address.address.long_words[0];
	/* (p2p.c's addresses are in network byte order: the first number the
	lowest byte) */
	origin = p2p_address_origin((host_order >> 24) | ((host_order >> 8) & 0xFF00) | ((host_order << 8) & 0xFF0000) |
		(host_order << 24));
	network_event("map share: the host '%s' (%lu.%lu.%lu.%lu) is %s", host_name, (host_order >> 24) & 255,
		(host_order >> 16) & 255, (host_order >> 8) & 255, host_order & 255,
		origin == P2P_ORIGIN_PUBLIC ? "a public game's" : origin == P2P_ORIGIN_PRIVATE ? "joined by code or invite" :
		origin == P2P_ORIGIN_ADHOC ? "in the ad hoc group" : "on the LAN");

	return origin == P2P_ORIGIN_PUBLIC;
}

/* kept parts not taken up for MAP_SHARE_RESUME_KEEP_SECONDS (or whose
record cannot be read), and .download files without a record (an older
build's, or a run that stopped before writing one), deleted from the maps
folder: once a run, before the first download */
static void map_share_kept_parts_tidy(
	void)
{
	static boolean tidied;
	static char const *const extensions[] = { "*" RESUME_EXTENSION, "*" TEMPORARY_EXTENSION };
	char names[16][MAP_SHARE_NAME_BYTES];
	short name_count = 0;
	unsigned long now = (unsigned long)time(NULL);
	short index;

	if (tidied)
	{
		return;
	}
	tidied = TRUE;
	for (index = 0; index < NUMBEROF(extensions); index++)
	{
		char pattern[PATH_BYTES];
		WIN32_FIND_DATAA data;
		HANDLE find;

		if (!map_share_path(pattern, "", extensions[index]) ||
			(find = FindFirstFileA(pattern, &data)) == INVALID_HANDLE_VALUE)
		{
			continue;
		}
		do
		{
			char name[MAP_SHARE_NAME_BYTES];
			char *dot = strrchr(data.cFileName, '.');
			struct map_share_resume resume;
			boolean stale;

			if (!dot || dot - data.cFileName >= MAP_SHARE_NAME_BYTES || name_count >= NUMBEROF(names))
			{
				continue;
			}
			csmemcpy(name, data.cFileName, (size_t)(dot - data.cFileName));
			name[dot - data.cFileName] = 0;
			if (!map_share_name_valid(name))
			{
				continue;
			}
			if (index == 0)
			{
				stale = !map_share_resume_read(name, &resume) ||
					(now > resume.saved_time && now - resume.saved_time > MAP_SHARE_RESUME_KEEP_SECONDS);
			}
			else
			{
				char path[PATH_BYTES];

				stale = map_share_path(path, name, RESUME_EXTENSION) && !map_share_file_exists(path);
			}
			if (stale)
			{
				csstrncpy(names[name_count++], name, MAP_SHARE_NAME_BYTES - 1);
			}
		}
		while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
	for (index = 0; index < name_count; index++)
	{
		network_event("map share: deleting the kept part of '%s' (old, or unreadable)", names[index]);
		map_share_kept_part_delete(names[index]);
	}

	return;
}

/* ---------- public code: joiner */

/* Whether the joiner may ask the host about its map `level_name` (named
`name`) now; else `why` not (an empty `why`: nothing to add to the
refusal). */
static boolean map_share_client_may_ask(
	struct network_game_client *client,
	char const *level_name,
	char const *name,
	boolean public_game,
	char *why,
	long why_size)
{
	short state = network_game_client_get_state(client, NULL);
	char const *setting = getenv("HALO_MAP_SHARE");
	enum map_share_downloads downloads = map_share_downloads_policy(getenv("HALO_MAP_SHARE_FROM"), public_game);

	why[0] = 0;
	if (setting && !csstrcmp(setting, "0"))
	{
		snprintf(why, (size_t)why_size, "Map sharing is off on this machine (HALO_MAP_SHARE=0).");
	}
	else if (downloads == _map_share_downloads_refused)
	{
		snprintf(why, (size_t)why_size, "Map downloads are off (Multiplayer > Modded maps).");
	}
	else if (downloads == _map_share_downloads_refused_public)
	{
		snprintf(why, (size_t)why_size, "Map downloads from public games are off (Multiplayer > Modded maps).");
	}
	else if (state != _network_game_client_state_pregame && state != _network_game_client_state_joining)
	{
		snprintf(why, (size_t)why_size, "The host's game had already started: join while the host is in the lobby to "
			"download it.");
	}
	else if (!map_share_name_valid(name) || strlen(level_name) >= sizeof(map_share_download.level_name))
	{
		snprintf(why, (size_t)why_size, "Its file name can't be sent: map sharing sends maps named with at most %d "
			"plain characters, and none of / \\ : * ? \" < > | , %%. Copy the host's map to your maps folder.",
			MAP_SHARE_MAXIMUM_NAME_LENGTH);
	}
	else
	{
		return TRUE;
	}
	network_event("map share: not asking the host about '%s': %s", name, why);

	return FALSE;
}

boolean map_share_client_offer(
	struct network_game_client *client,
	struct network_game const *game,
	char const *level_name,
	unsigned long identity,
	boolean replacing,
	char *why,
	long why_size)
{
	struct map_share_download *download = &map_share_download;
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	char host_name[sizeof(download->host_name)];
	boolean public_game = map_share_client_host(client, game, host_name, sizeof(host_name));

	if (!map_share_client_may_ask(client, level_name, name, public_game, why, why_size))
	{
		return FALSE;
	}
	if (!identity)
	{
		/* (a host of a version without map sharing sends none: Xbox games
		and this port's before it, network_server_manager.c) */
		snprintf(why, (size_t)why_size, "The host's version doesn't share maps (it may be an older version): copy the "
			"host's map to your maps folder, or ask the host to update.");
		network_event("map share: not asking the host about '%s': it sent no fingerprint", name);
		return FALSE;
	}
	map_share_client_reset();
	map_share_kept_parts_tidy();
	download->client = client;
	csstrncpy(download->level_name, level_name, sizeof(download->level_name) - 1);
	csstrncpy(download->name, name, MAP_SHARE_NAME_BYTES - 1);
	download->identity = (uint32_t)identity;
	download->replacing = replacing;
	csstrncpy(download->host_name, host_name, sizeof(download->host_name) - 1);
	download->public_game = public_game;
	download->capabilities = map_share_client_capabilities();
	if (!map_share_client_send(client, _map_share_command_query, (short)download->capabilities, 0))
	{
		csmemset(download, 0, sizeof(*download));
		return FALSE;
	}
	download->state = _client_querying;
	download->state_time = system_milliseconds();
	network_event("map share: asking the host about '%s' (0x%08lX)", name, identity);

	return TRUE;
}

boolean map_share_client_offer_pc_maps(
	struct network_game_client *client,
	char const *level_name,
	char *why,
	long why_size)
{
	struct map_share_download *download = &map_share_download;
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	short state = network_game_client_get_state(client, NULL);

	why[0] = 0;
	if (strlen(name) >= sizeof(download->name) || strlen(level_name) >= sizeof(download->level_name))
	{
		return FALSE;
	}
	if (state != _network_game_client_state_pregame && state != _network_game_client_state_joining)
	{
		snprintf(why, (size_t)why_size, "The host's game had already started: turn on PC maps, then join again.");
		return FALSE;
	}
	map_share_client_reset();
	download->client = client;
	csstrncpy(download->level_name, level_name, sizeof(download->level_name) - 1);
	csstrncpy(download->name, name, sizeof(download->name) - 1);
	download->pc_maps_only = TRUE;
	map_share_client_ask();

	return TRUE;
}

void map_share_client_map_changed(
	char const *level_name)
{
	struct map_share_download *download = &map_share_download;

	if (download->state != _client_idle && download->state != _client_leaving &&
		(!level_name || csstrcmp(level_name, download->level_name)))
	{
		network_event("map share: the host changed maps; '%s' not downloaded", download->name);
		if ((download->state == _client_starting || download->state == _client_receiving) && download->client)
		{
			map_share_client_send(download->client, _map_share_command_cancel, _map_share_refusal_cancelled,
				download->receiver.stream_received);
		}
		map_share_client_reset();
	}

	return;
}

void map_share_client_dispose(
	struct network_game_client *client)
{
	struct map_share_download *download = &map_share_download;

	if (download->state != _client_idle && download->client == client)
	{
		if (download->state == _client_leaving)
		{
			platform_show_message("Halo: custom map", download->message);
		}
		else
		{
			network_event("map share: the game was left; '%s' not downloaded", download->name);
		}
		map_share_client_reset();
	}

	return;
}

boolean map_share_client_busy(
	void)
{
	return map_share_download.state != _client_idle;
}

boolean map_share_client_game_starting(
	struct network_game_client *client,
	char const *level_name)
{
	struct map_share_download *download = &map_share_download;
	char why[320];
	char text[480];

	if (download->state == _client_leaving)
	{
		return FALSE;
	}
	if (download->state != _client_idle)
	{
		/* (a question or a download under way: the host's game started
		without this machine having its map, as a game joined in progress
		does) */
		network_event("map share: the host's game started before '%s' was here", download->name);
		map_share_client_refusal_text(text, sizeof(text), download->pc_maps_only ?
			"The host's game started before you answered." :
			"The host's game started before the map could be downloaded: join while the host is in the lobby.");
		map_share_client_fail(text);
		return FALSE;
	}
	/* (the host's own machine plays the map it chose, network_client_manager.c) */
	if (network_game_is_splitscreen_local() || global_network_game_server_get() || !level_name ||
		custom_edition_maps_loadable(level_name, why, sizeof(why)) == _custom_edition_maps_loadable)
	{
		return TRUE;
	}

	/* (a map this machine cannot load, which a game joined in progress or
	any other way to the start without the join checks would load: told,
	and the game left, never a damaged disc) */
	csmemset(download, 0, sizeof(*download));
	download->client = client;
	csstrncpy(download->level_name, level_name, sizeof(download->level_name) - 1);
	csstrncpy(download->name, tag_name_strip_path(level_name), sizeof(download->name) - 1);
	if (!map_share_client_unloadable(why, sizeof(why), "is the host's map, but"))
	{
		snprintf(why, sizeof(why), "The host's map %s can't be loaded here.", download->name);
	}
	network_event("map share: not loading the host's map '%s'", level_name);
	map_share_client_fail(why);

	return FALSE;
}

boolean map_share_client_update(
	struct network_game_client *client)
{
	struct map_share_download *download = &map_share_download;
	unsigned long now = system_milliseconds();
	short state;

	if (download->state == _client_idle)
	{
		return TRUE;
	}
	state = network_game_client_get_state(client, NULL);
	if (download->client != client)
	{
		/* (another client: the game this one was for is gone) */
		map_share_client_reset();
		return TRUE;
	}
	if (download->state != _client_leaving &&
		state != _network_game_client_state_pregame && state != _network_game_client_state_joining)
	{
		char text[480];

		/* (the host's game began without this machine's map: it cannot
		load it) */
		map_share_client_refusal_text(text, sizeof(text),
			"The host's game started before the map could be downloaded: join while the host is in the lobby.");
		map_share_client_fail(text);
	}

	switch (download->state)
	{
	case _client_querying:
		if (now - download->state_time > MAP_SHARE_QUERY_TIMEOUT_MILLISECONDS)
		{
			char text[480];

			/* a host without map sharing (it ignores the query): as before */
			map_share_client_refusal_text(text, sizeof(text),
				"The host didn't answer the request for it: it may be on an older version without map sharing. "
				"Copy the host's map to your maps folder, or ask the host to update.");
			map_share_client_fail(text);
		}
		break;

	case _client_asking:
	{
		int answer = map_share_client_answer();

		if (answer > 0 && download->pc_maps_only)
		{
			char why[320];

			/* the map is here: PC maps on, and it is precached */
			map_share_pc_maps_turn_on();
			custom_edition_maps_look_again();
			if (map_share_client_unloadable(why, sizeof(why), "is in your maps folder, but"))
			{
				map_share_client_fail(why);
			}
			else
			{
				network_event("map share: PC maps on; precaching map '%s'...", download->level_name);
				main_set_multiplayer_map_name(download->level_name);
				map_share_client_reset();
			}
		}
		else if (answer > 0)
		{
			map_share_client_start();
		}
		else if (!answer)
		{
			char text[480];

			map_share_client_refusal_text(text, sizeof(text), download->pc_maps_only ?
				"You answered no." : "You answered no to downloading it.");
			map_share_client_fail(text);
		}
		break;
	}

	case _client_checking:
		if (platform_progress_cancelled())
		{
			map_share_client_fail("Download cancelled.");
			break;
		}
		map_share_client_read_back();
		if (download->state == _client_checking && (!download->shown_time || now - download->shown_time >= PROGRESS_INTERVAL_MILLISECONDS))
		{
			download->shown_time = now;
			map_share_client_show_progress();
		}
		break;

	case _client_starting:
	case _client_receiving:
	{
		/* (the tests' cut: once a run, so that a download continued goes on) */
		static boolean cancelled_at;
		unsigned long cancel_at = cancelled_at ? 0 : map_share_environment_number("HALO_MAP_SHARE_CANCEL_AT", 0);

		if (platform_progress_cancelled() || (cancel_at && download->receiver.received >= cancel_at))
		{
			cancelled_at |= cancel_at != 0;
			map_share_client_fail("Download cancelled.");
		}
		else if (now - download->progress_time > MAP_SHARE_STALL_MILLISECONDS)
		{
			map_share_client_fail("The download from the host stopped.");
		}
		else if (!download->shown_time || now - download->shown_time >= PROGRESS_INTERVAL_MILLISECONDS)
		{
			download->shown_time = now;
			map_share_client_show_progress();
		}
		break;
	}

	default:
		break;
	}

	if (download->state == _client_leaving)
	{
		platform_show_message("Halo: custom map", download->message);
		csmemset(download, 0, sizeof(*download));
		return FALSE;
	}

	return TRUE;
}

void map_share_client_handle_answer(
	struct network_game_client *client,
	word *message,
	short message_size)
{
	struct map_share_download *download = &map_share_download;
	struct map_share_answer_message answer;
	short packet_type = _message_server_map_download_answer;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	char why[320];

	message_size -= sizeof(word);
	if (message_size <= 0 ||
		!decode_network_game_message(&answer, message + 1, &message_size, &packet_type, &packet_version, _packet_class_pregame))
	{
		network_event("map share: an answer that could not be decoded");
		return;
	}
	if (download->client != client ||
		download->state == _client_idle || download->state == _client_leaving)
	{
		/* (late: a download given up) */
		return;
	}
	if (!map_share_answer_valid(&answer, download->name, download->identity, download->capabilities))
	{
		map_share_client_fail("The host's answer about the map wasn't valid.");
		return;
	}

	switch (answer.kind)
	{
	case _map_share_answer_refused:
		if (answer.reason == _map_share_refusal_not_in_lobby)
		{
			snprintf(why, sizeof(why), "The host's custom map %s couldn't be downloaded: the host's game had already "
				"started. Join while the host is in the lobby to download it.", download->name);
		}
		else
		{
			snprintf(why, sizeof(why), "The host's custom map %s couldn't be downloaded: %s.",
				download->name, map_share_refusal_describe((enum map_share_refusal)answer.reason));
		}
		map_share_client_fail(why);
		break;

	case _map_share_answer_offer:
		if (download->state == _client_querying)
		{
			download->size = (uint32_t)answer.size;
			download->flags = answer.flags;
			/* (an OpenSauce map, .yelo: more for the loader to trust than a
			plain map, so never from a stranger's game - the owner's choice;
			a code's, Wi-Fi's or ad hoc's game still offers it) */
			if (download->public_game && TEST_FLAG(download->flags, _map_share_offer_yelo_bit))
			{
				char why[320];

				snprintf(why, sizeof(why), "The host's map %s is an OpenSauce map (.yelo), which isn't downloaded "
					"from public games. Join with the host's code, or copy the map to your maps folder.",
					download->name);
				map_share_client_fail(why);
				break;
			}
			/* (a Custom Edition map with PC maps off: the question asks to
			turn it on with the download) */
			download->turn_on_pc_maps =
				TEST_FLAG(download->flags, _map_share_offer_custom_edition_bit) && !halo_custom_edition_enabled();
			/* (a kept part of this file, from an earlier download) */
			download->resume_from = map_share_client_kept_part();
			map_share_client_ask();
		}
		else if (download->state == _client_starting &&
			(uint32_t)answer.size == download->size &&
			(answer.flags | FLAG(_map_share_offer_deflate_bit)) == (download->flags | FLAG(_map_share_offer_deflate_bit)))
		{
			/* (deflated as offered, or the file's own bytes: a host short of
			memory sends them) */
			if (!TEST_FLAG(answer.flags, _map_share_offer_deflate_bit) && download->receiver.inflater)
			{
				network_event("map share: the host sends '%s' as it is", download->name);
				map_share_receiver_start_stream(&download->receiver, FALSE);
			}
			download->flags = answer.flags;
			download->state = _client_receiving;
			download->progress_time = system_milliseconds();
		}
		else
		{
			map_share_client_fail("The host's offer of the map changed.");
		}
		break;

	case _map_share_answer_done:
		if (download->state != _client_receiving)
		{
			map_share_client_fail("The host ended a download that hadn't started.");
		}
		else
		{
			map_share_client_finish(&answer);
		}
		break;
	}

	return;
}

void map_share_client_handle_data(
	struct network_game_client *client,
	word *message,
	short message_size)
{
	struct map_share_download *download = &map_share_download;
	/* (static: 3 KB) */
	static struct map_share_data_message data;
	short packet_type = _message_server_map_download_data;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	enum map_share_chunk_status status;

	message_size -= sizeof(word);
	if (message_size <= 0 ||
		!decode_network_game_message(&data, message + 1, &message_size, &packet_type, &packet_version, _packet_class_pregame))
	{
		network_event("map share: data that could not be decoded");
		return;
	}
	if (download->client != client || download->state != _client_receiving)
	{
		/* (late: a download given up or not started) */
		return;
	}

	status = map_share_receiver_accept(&download->receiver, data.offset, data.length, data.data,
		map_share_client_write, download->file);
	if (status == _map_share_chunk_write_failed)
	{
		map_share_client_fail_damaged("The map couldn't be written (is the memory card full?).");
		return;
	}
	if (status != _map_share_chunk_ok)
	{
		network_event("map share: data at %ld (%d bytes) refused (%d)", (long)data.offset, (int)data.length, (int)status);
		map_share_client_fail_damaged(status == _map_share_chunk_bad_stream ?
			"The download was damaged (it doesn't inflate)." : "The host sent the map out of order.");
		return;
	}
	download->progress_time = system_milliseconds();

	/* the header, checked as soon as it is whole: a lying one stops the
	download before the rest comes */
	if (!download->receiver.header_checked && download->receiver.received >= MAP_SHARE_HEADER_BYTES)
	{
		int custom_edition;
		enum map_share_header_status header_status =
			map_share_header_validate(download->receiver.header, download->size, download->name, &custom_edition);

		download->receiver.header_checked = TRUE;
		if (header_status == _map_share_header_ok &&
			!custom_edition != !TEST_FLAG(download->flags, _map_share_offer_custom_edition_bit))
		{
			map_share_client_fail_damaged("The host's map isn't the kind of map it offered.");
			return;
		}
		if (header_status != _map_share_header_ok)
		{
			char why[320];

			snprintf(why, sizeof(why), "The host's map %s isn't a map this game can play (%s).",
				download->name, map_share_header_status_describe(header_status));
			map_share_client_fail_damaged(why);
			return;
		}
	}
	if (map_share_receiver_ack_due(&download->receiver))
	{
		download->receiver.acknowledged = download->receiver.stream_received;
		map_share_client_send(client, _map_share_command_ack, _map_share_refusal_none, download->receiver.stream_received);
	}
	/* (what is here written down now and then: a download cut off goes on
	from there) */
	if (download->receiver.received - download->saved >= MAP_SHARE_RESUME_SAVE_BYTES &&
		download->receiver.received < download->size && !map_share_setting_off("HALO_MAP_SHARE_RESUME"))
	{
		map_share_client_save_resume();
	}

	return;
}

/* ---------- public code: host */

void map_share_server_handle_request(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size)
{
	struct map_share_request request;
	struct map_share_upload *upload;
	short packet_type = _message_client_map_download;
	short packet_version = HALO_PORT_NETWORK_GAME_MESSAGE_VERSION;
	char name[MAP_SHARE_NAME_BYTES];

	message_size -= sizeof(word);
	if (message_size <= 0 ||
		!decode_network_game_message(&request, message + 1, &message_size, &packet_type, &packet_version, _packet_class_client_pregame))
	{
		network_event("map share: a request that could not be decoded");
		return;
	}
	if (!map_share_request_valid(&request) || !map_share_name_from_field(request.name, name))
	{
		network_event("map share: an invalid request (command %d)", (int)request.command);
		return;
	}
	if (network_game_server_client_machine_is_local(server, machine))
	{
		return;
	}
	/* (the automated tests' host of a version without map sharing, which
	ignores the request) */
	if (getenv("HALO_MAP_SHARE_HOST_SILENT"))
	{
		return;
	}

	switch (request.command)
	{
	case _map_share_command_query:
	case _map_share_command_start:
		map_share_server_query_or_start(server, machine, &request, name);
		break;

	case _map_share_command_ack:
		upload = map_share_upload_find(machine);
		if (upload && !csstrcasecmp(upload->name, name))
		{
			if ((uint32_t)request.offset < upload->acknowledged || (uint32_t)request.offset > upload->sent)
			{
				network_event("map share: a machine acknowledged %ld of '%s' (sent %lu)",
					(long)request.offset, name, (unsigned long)upload->sent);
				map_share_server_refuse(server, machine, name, _map_share_refusal_protocol);
				map_share_upload_close(upload);
				break;
			}
			upload->acknowledged = (uint32_t)request.offset;
			upload->progress_time = system_milliseconds();
			if (upload->done_sent && upload->acknowledged == upload->sent)
			{
				network_event("map share: '%s' sent to a machine", name);
				map_share_upload_close(upload);
			}
		}
		break;

	case _map_share_command_cancel:
		upload = map_share_upload_find(machine);
		if (upload)
		{
			network_event("map share: a machine cancelled '%s' at %lu bytes", upload->name,
				(unsigned long)map_share_upload_progress(upload));
			map_share_upload_close(upload);
		}
		break;
	}

	return;
}

void map_share_server_update(
	struct network_game_server *server)
{
	struct map_share_host *host = &map_share_host;
	unsigned long now = system_milliseconds();
	unsigned long long now_us = vita_host_time_us();
	unsigned long rate = map_share_environment_number("HALO_MAP_SHARE_RATE_KB", MAP_SHARE_DEFAULT_BYTES_PER_SECOND / 1024) * 1024;
	unsigned long cpu_percent = map_share_environment_number("HALO_MAP_SHARE_CPU_PERCENT", MAP_SHARE_DEFAULT_CPU_PERCENT);
	boolean pregame = network_game_server_get_state(server, NULL) == _network_game_server_state_pregame;
	unsigned long long until_us;
	boolean sent;
	short count;
	short active = 0;

	for (count = 0; count < MAP_SHARE_MAXIMUM_UPLOADS; count++)
	{
		active += host->uploads[count].active;
	}
	if (!active)
	{
		host->rate_time = now;
		host->rate_budget = 0;
		host->cpu_time = now_us;
		host->cpu_allowance = MAXIMUM_CPU_ALLOWANCE_MICROSECONDS;
		host->cpu_logged_time = 0;
		return;
	}
	/* the rate, saved up to a few chunks */
	{
		unsigned long elapsed = now - host->rate_time;

		host->rate_time = now;
		host->rate_budget += (unsigned long)((unsigned long long)rate * MIN(elapsed, 1000UL) / 1000);
		host->rate_budget = MIN(host->rate_budget, MAX(MAXIMUM_RATE_BURST_BYTES, rate / 30));
	}
	/* the CPU's share (of the time since the last look) */
	host->cpu_allowance += (now_us - host->cpu_time) * cpu_percent / 100;
	host->cpu_allowance = MIN(host->cpu_allowance, (unsigned long long)MAXIMUM_CPU_ALLOWANCE_MICROSECONDS);
	host->cpu_time = now_us;
	until_us = now_us + host->cpu_allowance;

	for (count = 0; count < MAP_SHARE_MAXIMUM_UPLOADS; count++)
	{
		struct map_share_upload *upload = &host->uploads[count];

		if (!upload->active)
		{
			continue;
		}
		if (!network_game_server_client_machine_is_joined_to_game(server, upload->machine) ||
			network_game_server_get_client_connection(upload->machine) != upload->connection)
		{
			network_event("map share: the machine downloading '%s' left", upload->name);
			map_share_upload_close(upload);
			continue;
		}
		if (!pregame)
		{
			map_share_server_refuse(server, upload->machine, upload->name, _map_share_refusal_not_in_lobby);
			map_share_upload_close(upload);
			continue;
		}
		if (now - upload->progress_time > MAP_SHARE_STALL_MILLISECONDS)
		{
			network_event("map share: a machine took nothing of '%s' for %d s", upload->name, MAP_SHARE_STALL_MILLISECONDS / 1000);
			map_share_server_refuse(server, upload->machine, upload->name, _map_share_refusal_cancelled);
			map_share_upload_close(upload);
			continue;
		}
		if (!upload->logged_time || now - upload->logged_time >= 5000)
		{
			upload->logged_time = now;
			network_event("map share: machine #%d is downloading '%s' (%d%%)", (int)upload->machine_index,
				upload->name, (int)(upload->size ? (unsigned long long)map_share_upload_progress(upload) * 100 / upload->size : 0));
		}
		map_share_upload_level(upload, now);
		upload->frames++;
		upload->cpu_held = FALSE;
	}

	/* a message each in turn (starting with the next upload each frame),
	while the windows, the rate and the frame's share of the CPU allow */
	do
	{
		sent = FALSE;
		for (count = 0; count < MAP_SHARE_MAXIMUM_UPLOADS; count++)
		{
			struct map_share_upload *upload = &host->uploads[(host->next_upload + count) % MAP_SHARE_MAXIMUM_UPLOADS];

			if (!map_share_upload_may_send(upload))
			{
				continue;
			}
			if (vita_host_time_us() >= until_us)
			{
				upload->cpu_held = TRUE;
				continue;
			}
			if (!map_share_upload_send(server, upload))
			{
				map_share_upload_close(upload);
				continue;
			}
			sent = TRUE;
		}
	}
	while (sent);
	host->next_upload = (short)((host->next_upload + 1) % MAP_SHARE_MAXIMUM_UPLOADS);

	/* the hashes behind (resumed uploads), then the done */
	for (count = 0; count < MAP_SHARE_MAXIMUM_UPLOADS; count++)
	{
		struct map_share_upload *upload = &host->uploads[count];

		if (!upload->active)
		{
			continue;
		}
		upload->cpu_held_frames += upload->cpu_held;
		if (!map_share_upload_hash(upload, until_us))
		{
			network_event("map share: '%s' could not be read to hash it", upload->name);
			map_share_server_refuse(server, upload->machine, upload->name, _map_share_refusal_read_failed);
			map_share_upload_close(upload);
			continue;
		}
		if (!map_share_upload_finish(server, upload))
		{
			map_share_upload_close(upload);
		}
	}
	/* (what the uploads took of it) */
	{
		unsigned long long used = vita_host_time_us() - now_us;

		host->cpu_allowance -= MIN(used, host->cpu_allowance);
		host->cpu_used += used;
		host->cpu_used_most = MAX(host->cpu_used_most, used);
		if (now - host->cpu_logged_time >= 5000)
		{
			if (host->cpu_logged_time)
			{
				network_event("map share: the uploads took %lu ms of the CPU in %lu ms (%lu ms at most in a frame)",
					(unsigned long)(host->cpu_used / 1000), now - host->cpu_logged_time,
					(unsigned long)(host->cpu_used_most / 1000));
			}
			host->cpu_logged_time = now;
			host->cpu_used = host->cpu_used_most = 0;
		}
	}

	return;
}

short map_share_server_machine_percent(
	long machine_index)
{
	short index;

	for (index = 0; index < MAP_SHARE_MAXIMUM_UPLOADS; index++)
	{
		struct map_share_upload const *upload = &map_share_host.uploads[index];

		if (upload->active && upload->machine_index == machine_index && upload->size)
		{
			return (short)((unsigned long long)map_share_upload_progress(upload) * 100 / upload->size);
		}
	}

	return NONE;
}

boolean map_share_server_holds_start(
	struct network_game_server *server)
{
	static unsigned long logged_time;
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < MAP_SHARE_MAXIMUM_UPLOADS; index++)
	{
		struct map_share_upload const *upload = &map_share_host.uploads[index];

		if (upload->active && network_game_server_client_machine_is_joined_to_game(server, upload->machine))
		{
			if (!logged_time || now - logged_time >= 5000)
			{
				network_event("map share: the game's start waits for machine #%d's download of '%s' (%d%%)",
					(int)upload->machine_index, upload->name,
					(int)map_share_server_machine_percent(upload->machine_index));
				logged_time = now;
			}
			return TRUE;
		}
	}
	logged_time = 0;

	return FALSE;
}

void map_share_server_dispose(
	void)
{
	short index;

	for (index = 0; index < MAP_SHARE_MAXIMUM_UPLOADS; index++)
	{
		if (map_share_host.uploads[index].active)
		{
			map_share_upload_close(&map_share_host.uploads[index]);
		}
	}

	return;
}
