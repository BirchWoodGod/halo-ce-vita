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
  acknowledgement, so the lobby's own messages to that machine, and the
  other machines' link, are not crowded out. At most
  MAP_SHARE_MAXIMUM_UPLOADS at once.
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
  other map. A cancelled or failed download is deleted (no resuming).
- Free space: the size offered (and a margin) must be free before starting.
- HALO_MAP_SHARE=0 turns it off (joiners are refused as before; a host
  still answers). HALO_MAP_SHARE_ANSWER=yes|no answers the question without
  asking (the automated tests: a hidden window shows nothing), and
  HALO_MAP_SHARE_CANCEL_AT=<bytes> cancels a download that far in.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "main/main.h"
#include "tag_files/tag_files.h"
#include "networking/network_messages.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_share_protocol.h"
#include "map_share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define REPLACED_EXTENSION ".replaced"
#define PATH_BYTES 256

/* ---------- structures */

struct map_share_upload
{
	boolean active;
	struct network_game_server_client_machine *machine;
	struct network_connection *connection;
	FILE *file;
	char name[MAP_SHARE_NAME_BYTES];
	int32_t identity;
	int32_t flags;
	uint32_t size;
	uint32_t sent;
	uint32_t acknowledged;
	boolean done_sent;
	unsigned long progress_time;
	struct halo_sha256_stream sha256;
};

struct map_share_host
{
	struct map_share_upload uploads[MAP_SHARE_MAXIMUM_UPLOADS];
	short next_upload;
	unsigned long rate_time;
	unsigned long rate_budget;
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
	FILE *file;
	char temporary_path[PATH_BYTES];
	struct map_share_receiver receiver;
	/* what the player is told on leaving */
	char message[320];
};

/* ---------- prototypes */

void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *message);
void platform_ask_question(char const *title, char const *text);
int platform_question_answer(void);
void platform_show_progress(char const *title, char const *text);
int platform_progress_cancelled(void);
unsigned long system_milliseconds(void);
int halo_cache_map_in_use(char const *name);
boolean cache_files_forget_cached_map(char const *map_name);
boolean network_game_client_send_to_server(struct network_game_client *client, void *message);

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

/* Whether the host serves `name`, fingerprinted `identity`, now: opens it
(*file, its *size and offer *flags) when it does, else says why not. */
static enum map_share_refusal map_share_server_check(
	struct network_game_server *server,
	char const *name,
	int32_t identity,
	FILE **file,
	uint32_t *size,
	int32_t *flags)
{
	char const *level_name = main_get_multiplayer_map_name();
	char path[PATH_BYTES];
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
	if (!custom_edition_cache_map_file_path(level_name, path, sizeof(path)))
	{
		return _map_share_refusal_read_failed;
	}
	*file = fopen(path, "rb");
	if (!*file)
	{
		return _map_share_refusal_read_failed;
	}
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
	enum map_share_refusal refusal;
	uint32_t size;
	int32_t flags;
	FILE *file;
	short index;

	/* (a machine asks again: its earlier upload ends) */
	if (upload)
	{
		map_share_upload_close(upload);
	}
	refusal = map_share_server_check(server, name, request->identity, &file, &size, &flags);
	if (refusal != _map_share_refusal_none)
	{
		map_share_server_refuse(server, machine, name, refusal);
		return;
	}
	if (request->command == _map_share_command_query)
	{
		fclose(file);
		network_event("map share: offering '%s' (%lu bytes) to a machine", name, (unsigned long)size);
		map_share_server_answer_offer(server, machine, name, request->identity, size, flags);
		return;
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
	upload->connection = network_game_server_get_client_connection(machine);
	upload->file = file;
	csstrncpy(upload->name, name, MAP_SHARE_NAME_BYTES - 1);
	upload->identity = request->identity;
	upload->flags = flags;
	upload->size = size;
	upload->progress_time = system_milliseconds();
	halo_sha256_begin(&upload->sha256);
	network_event("map share: sending '%s' (%lu bytes) to a machine", name, (unsigned long)size);
	/* (the offer again: the start is taken) */
	map_share_server_answer_offer(server, machine, name, request->identity, size, flags);

	return;
}

/* Sends what the upload's window and the rate allow; FALSE when it ended. */
static boolean map_share_upload_send(
	struct network_game_server *server,
	struct map_share_upload *upload)
{
	while (!upload->done_sent)
	{
		if (upload->sent == upload->size)
		{
			struct map_share_answer_message answer;

			csmemset(&answer, 0, sizeof(answer));
			answer.kind = _map_share_answer_done;
			answer.size = (int32_t)upload->size;
			answer.identity = upload->identity;
			answer.flags = upload->flags;
			csstrncpy(answer.name, upload->name, MAP_SHARE_NAME_BYTES - 1);
			halo_sha256_end(&upload->sha256, answer.digest);
			upload->done_sent = TRUE;
			fclose(upload->file);
			upload->file = NULL;
			if (!map_share_server_send_answer(server, upload->machine, &answer))
			{
				return FALSE;
			}
			break;
		}
		if (upload->sent - upload->acknowledged >= MAP_SHARE_WINDOW_BYTES ||
			map_share_host.rate_budget < MAP_SHARE_CHUNK_BYTES)
		{
			break;
		}
		{
			/* (static: 3 KB, and one frame sends at a time) */
			static struct map_share_data_message data;
			uint32_t length = MIN(upload->size - upload->sent, (uint32_t)MAP_SHARE_CHUNK_BYTES);
			void *message;

			csmemset(&data, 0, sizeof(data));
			data.offset = (int32_t)upload->sent;
			data.length = (int16_t)length;
			if (fread(data.data, 1, length, upload->file) != length)
			{
				map_share_server_refuse(server, upload->machine, upload->name, _map_share_refusal_read_failed);
				return FALSE;
			}
			halo_sha256_add(&upload->sha256, data.data, length);
			message = create_network_game_message(_message_server_map_download_data, &data, sizeof(data));
			if (!message || !network_game_server_send_message_to_client_machine(server, upload->machine, message))
			{
				network_event("map share: sending '%s' to a machine failed", upload->name);
				return FALSE;
			}
			upload->sent += length;
			map_share_host.rate_budget -= length;
		}
	}

	return TRUE;
}

/* ---------- private code: joiner */

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

/* the download's file closed, and deleted unless kept */
static void map_share_client_close_file(
	boolean delete_it)
{
	struct map_share_download *download = &map_share_download;

	if (download->file)
	{
		fclose(download->file);
		download->file = NULL;
	}
	if (delete_it && download->temporary_path[0])
	{
		remove(download->temporary_path);
	}
	download->temporary_path[0] = 0;

	return;
}

static void map_share_client_reset(
	void)
{
	struct map_share_download *download = &map_share_download;

	map_share_client_close_file(TRUE);
	if (download->state == _client_asking)
	{
		platform_ask_question(NULL, NULL);
	}
	if (download->state == _client_starting || download->state == _client_receiving)
	{
		platform_show_progress(NULL, NULL);
	}
	csmemset(download, 0, sizeof(*download));

	return;
}

/* the old refusal (network_client_manager.c's), for a joiner that does not
download */
static void map_share_client_refusal_text(
	char *text,
	long text_size)
{
	struct map_share_download *download = &map_share_download;

	snprintf(
		text,
		(size_t)text_size,
		!download->replacing ?
			"The host is playing the custom map %s, which isn't in your maps folder." :
			"The host's custom map %s isn't the same as yours. Copy the host's map to your maps folder.",
		download->name);

	return;
}

/* Gives up: the host is told when it is sending, the file deleted, and the
joiner leaves the game at its next frame, telling the player `why`. */
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
		download->receiver.received < download->size)
	{
		map_share_client_send(download->client, _map_share_command_cancel, _map_share_refusal_cancelled, download->receiver.received);
	}
	map_share_client_close_file(TRUE);
	if (download->state == _client_starting || download->state == _client_receiving)
	{
		platform_show_progress(NULL, NULL);
	}
	csstrncpy(download->message, why, sizeof(download->message) - 1);
	download->state = _client_leaving;

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
	if (replaced)
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

/* The host's done: every byte, the digest, the fingerprint, the header
on the disk; then the map goes in place and is precached. */
static void map_share_client_finish(
	struct map_share_answer_message const *answer)
{
	struct map_share_download *download = &map_share_download;
	uint8_t digest[MAP_SHARE_DIGEST_BYTES];
	char why[320];
	boolean missing;

	if (download->receiver.received != download->size || (uint32_t)answer->size != download->size)
	{
		map_share_client_fail("The host ended the download early.");
		return;
	}
	if (download->file && (fflush(download->file) != 0 || ferror(download->file)))
	{
		map_share_client_fail("The map couldn't be written (is the memory card full?).");
		return;
	}
	if (download->file)
	{
		fclose(download->file);
		download->file = NULL;
	}
	map_share_receiver_digest(&download->receiver, digest);
	if (csmemcmp(digest, answer->digest, sizeof(digest)))
	{
		map_share_client_fail("The download was damaged (its SHA-256 isn't the host's).");
		return;
	}
	if (map_share_receiver_identity(&download->receiver) != download->identity)
	{
		snprintf(why, sizeof(why), "The host sent a copy of %s that isn't the one its game plays.", download->name);
		map_share_client_fail(why);
		return;
	}
	if (!map_share_client_check_file(download->temporary_path, why, sizeof(why)) ||
		!map_share_client_install(why, sizeof(why)))
	{
		map_share_client_fail(why);
		return;
	}

	/* the game's own checks, on the map in place */
	custom_edition_cache_map_identity_forget(download->name);
	custom_edition_maps_look_again();
	platform_show_progress(NULL, NULL);
	if (!custom_edition_maps_host_copy_matches(download->level_name, download->identity, &missing))
	{
		snprintf(why, sizeof(why), "The downloaded map %s doesn't match the host's.", download->name);
		map_share_client_fail(why);
		return;
	}
	if (custom_edition_maps_display_index(download->level_name) == NONE)
	{
		/* kept (it is the host's map, checked), but this machine does not
		play it as it is */
		snprintf(why, sizeof(why), TEST_FLAG(download->flags, _map_share_offer_custom_edition_bit) ?
			"%s was downloaded to your maps folder, but this Vita can't play it yet: a Custom Edition map needs "
			"PC maps on (Multiplayer) and bitmaps.map, sounds.map and loc.map in the maps folder." :
			"%s was downloaded to your maps folder, but it isn't a multiplayer map this game lists.",
			download->name);
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
	char text[400];

	map_share_size_text(size_text, sizeof(size_text), download->size);
	snprintf(
		text,
		sizeof(text),
		!download->replacing ?
			"The host is playing the custom map %s (%s), which isn't in your maps folder.\n\nDownload it from the host?" :
			"The host's custom map %s (%s) isn't the same as yours.\n\nDownload the host's copy in place of yours?",
		download->name,
		size_text);
	network_event("map share: asking the player about '%s' (%lu bytes)", download->name, (unsigned long)download->size);
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

/* the offer taken: room, a file, the start */
static void map_share_client_start(
	void)
{
	struct map_share_download *download = &map_share_download;
	ULARGE_INTEGER free_bytes;
	char size_text[32];
	char why[320];

	if (GetDiskFreeSpaceExA(cache_files_map_directory(), &free_bytes, NULL, NULL))
	{
		unsigned long long needed = (unsigned long long)download->size + FREE_SPACE_MARGIN_BYTES;
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
	remove(download->temporary_path);
	download->file = fopen(download->temporary_path, "wb");
	if (!download->file)
	{
		download->temporary_path[0] = 0;
		map_share_client_fail("The map couldn't be written to the maps folder.");
		return;
	}
	map_share_receiver_begin(&download->receiver, download->size);
	if (!map_share_client_send(download->client, _map_share_command_start, _map_share_refusal_none, 0))
	{
		map_share_client_fail("The host couldn't be asked for the map.");
		return;
	}
	download->state = _client_starting;
	download->state_time = download->progress_time = system_milliseconds();
	download->shown_time = 0;
	network_event("map share: downloading '%s' (%lu bytes)", download->name, (unsigned long)download->size);

	return;
}

static void map_share_client_show_progress(
	void)
{
	struct map_share_download *download = &map_share_download;
	char received_text[32];
	char size_text[32];
	char text[256];
	unsigned long percent = download->size ?
		(unsigned long)((unsigned long long)download->receiver.received * 100 / download->size) : 0;

	map_share_size_text(received_text, sizeof(received_text), download->receiver.received);
	map_share_size_text(size_text, sizeof(size_text), download->size);
	snprintf(text, sizeof(text), "Downloading %s from the host\n\n%s of %s (%lu%%)",
		download->name, received_text, size_text, percent);
	platform_show_progress("Halo: custom map", text);

	return;
}

/* ---------- public code: joiner */

boolean map_share_client_offer(
	struct network_game_client *client,
	char const *level_name,
	unsigned long identity,
	boolean replacing)
{
	struct map_share_download *download = &map_share_download;
	char const *name = level_name ? tag_name_strip_path(level_name) : "";
	short state = network_game_client_get_state(client, NULL);
	char const *setting = getenv("HALO_MAP_SHARE");

	if ((setting && !csstrcmp(setting, "0")) ||
		!identity ||
		!map_share_name_valid(name) ||
		strlen(level_name) >= sizeof(download->level_name) ||
		(state != _network_game_client_state_pregame && state != _network_game_client_state_joining))
	{
		return FALSE;
	}
	map_share_client_reset();
	download->client = client;
	csstrncpy(download->level_name, level_name, sizeof(download->level_name) - 1);
	csstrncpy(download->name, name, MAP_SHARE_NAME_BYTES - 1);
	download->identity = (uint32_t)identity;
	download->replacing = replacing;
	if (!map_share_client_send(client, _map_share_command_query, _map_share_refusal_none, 0))
	{
		csmemset(download, 0, sizeof(*download));
		return FALSE;
	}
	download->state = _client_querying;
	download->state_time = system_milliseconds();
	network_event("map share: asking the host about '%s' (0x%08lX)", name, identity);

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
			map_share_client_send(download->client, _map_share_command_cancel, _map_share_refusal_cancelled, download->receiver.received);
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
		char text[320];

		/* (the host's game began without this machine's map: it cannot
		load it) */
		map_share_client_refusal_text(text, sizeof(text));
		map_share_client_fail(text);
	}

	switch (download->state)
	{
	case _client_querying:
		if (now - download->state_time > MAP_SHARE_QUERY_TIMEOUT_MILLISECONDS)
		{
			char text[320];

			/* a host without map sharing: as before */
			map_share_client_refusal_text(text, sizeof(text));
			map_share_client_fail(text);
		}
		break;

	case _client_asking:
	{
		int answer = map_share_client_answer();

		if (answer > 0)
		{
			map_share_client_start();
		}
		else if (!answer)
		{
			char text[320];

			map_share_client_refusal_text(text, sizeof(text));
			map_share_client_fail(text);
		}
		break;
	}

	case _client_starting:
	case _client_receiving:
	{
		unsigned long cancel_at = map_share_environment_number("HALO_MAP_SHARE_CANCEL_AT", 0);

		if (platform_progress_cancelled() || (cancel_at && download->receiver.received >= cancel_at))
		{
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
	if (!map_share_answer_valid(&answer, download->name, download->identity))
	{
		map_share_client_fail("The host's answer about the map wasn't valid.");
		return;
	}

	switch (answer.kind)
	{
	case _map_share_answer_refused:
		snprintf(why, sizeof(why), "The host's custom map %s couldn't be downloaded: %s.",
			download->name, map_share_refusal_describe((enum map_share_refusal)answer.reason));
		map_share_client_fail(why);
		break;

	case _map_share_answer_offer:
		if (download->state == _client_querying)
		{
			download->size = (uint32_t)answer.size;
			download->flags = answer.flags;
			if (TEST_FLAG(download->flags, _map_share_offer_custom_edition_bit) && !halo_custom_edition_enabled())
			{
				snprintf(why, sizeof(why), "The host is playing the Custom Edition map %s. Turn on PC maps (settings, "
					"Multiplayer) to download and play it.", download->name);
				map_share_client_fail(why);
			}
			else
			{
				map_share_client_ask();
			}
		}
		else if (download->state == _client_starting &&
			(uint32_t)answer.size == download->size && answer.flags == download->flags)
		{
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

	status = map_share_receiver_accept(&download->receiver, data.offset, data.length, data.data);
	if (status != _map_share_chunk_ok)
	{
		network_event("map share: data at %ld (%d bytes) refused (%d)", (long)data.offset, (int)data.length, (int)status);
		map_share_client_fail("The host sent the map out of order.");
		return;
	}
	if (fwrite(data.data, 1, (size_t)data.length, download->file) != (size_t)data.length)
	{
		map_share_client_fail("The map couldn't be written (is the memory card full?).");
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
			map_share_client_fail("The host's map isn't the kind of map it offered.");
			return;
		}
		if (header_status != _map_share_header_ok)
		{
			char why[320];

			snprintf(why, sizeof(why), "The host's map %s isn't a map this game can play (%s).",
				download->name, map_share_header_status_describe(header_status));
			map_share_client_fail(why);
			return;
		}
	}
	if (map_share_receiver_ack_due(&download->receiver))
	{
		download->receiver.acknowledged = download->receiver.received;
		map_share_client_send(client, _map_share_command_ack, _map_share_refusal_none, download->receiver.received);
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
			if (upload->done_sent && upload->acknowledged == upload->size)
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
			network_event("map share: a machine cancelled '%s' at %lu bytes", upload->name, (unsigned long)upload->acknowledged);
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
	unsigned long rate = map_share_environment_number("HALO_MAP_SHARE_RATE_KB", MAP_SHARE_DEFAULT_BYTES_PER_SECOND / 1024) * 1024;
	boolean pregame = network_game_server_get_state(server, NULL) == _network_game_server_state_pregame;
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
		return;
	}
	/* the rate, saved up to a few chunks */
	{
		unsigned long elapsed = now - host->rate_time;

		host->rate_time = now;
		host->rate_budget += (unsigned long)((unsigned long long)rate * MIN(elapsed, 1000UL) / 1000);
		host->rate_budget = MIN(host->rate_budget, MAX(MAXIMUM_RATE_BURST_BYTES, rate / 30));
	}

	/* (in turn, starting where the last frame's rate ran out) */
	for (count = 0; count < MAP_SHARE_MAXIMUM_UPLOADS; count++)
	{
		struct map_share_upload *upload = &host->uploads[(host->next_upload + count) % MAP_SHARE_MAXIMUM_UPLOADS];

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
		if (!map_share_upload_send(server, upload))
		{
			map_share_upload_close(upload);
		}
	}
	host->next_upload = (short)((host->next_upload + 1) % MAP_SHARE_MAXIMUM_UPLOADS);

	return;
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
