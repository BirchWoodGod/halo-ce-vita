/*
GAME_STATE_XBOX.C

symbols in this file:
001AFBB0 0160:
	_game_state_allocate_buffer (0000)
001AFD10 0040:
	_game_state_free_buffer (0000)
001AFD50 00d0:
	_game_state_create_or_open_file (0000)
001AFE20 0040:
	_game_state_close_file (0000)
001AFE60 00e0:
	_game_state_write_to_file (0000)
001AFF40 0120:
	_game_state_read_from_file (0000)
001B0060 0090:
	_game_state_write_core (0000)
001B00F0 0080:
	_game_state_read_core_header (0000)
001B0170 00a0:
	_game_state_read_core (0000)
001B0210 0010:
	_game_state_get_persistent_storage_filename (0000)
001B0220 0020:
	_game_state_get_persistent_storage_path (0000)
001B0240 0030:
	_delete_persistent_storage (0000)
001B0270 0190:
	_game_state_open_persistent_storage (0000)
001B0400 01b0:
	_game_state_read_header_from_persistent_storage (0000)
001B05B0 0160:
	_game_state_write_to_persistent_storage (0000)
001B0710 00b0:
	_game_state_read_from_persistent_storage (0000)
001B07C0 0020:
	_game_state_create_persistent_storage (0000)
002A8084 001f:
	??_C@_0BP@IDAHEHI@?$CIunsigned?5long?$CJresult?$DN?$DNaddress?$AA@ (0000)
002A80A4 001e:
	??_C@_0BO@BBKFFHIP@?$CB?$CIgpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80C4 001e:
	??_C@_0BO@BHHCKDPL@?$CB?$CIcpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80E4 000b:
	??_C@_0L@JBLOLKNC@gpu_size?$DO0?$AA@ (0000)
002A80F0 000b:
	??_C@_0L@CBGHMJFA@cpu_size?$DO0?$AA@ (0000)
002A80FC 002a:
	??_C@_0CK@FIOMLFFF@?$CBxbox_game_state_globals?4buffer_@ (0000)
002A8128 002d:
	??_C@_0CN@KDCMADJG@c?3?2halo?2SOURCE?2saved?5games?2game_@ (0000)
002A8158 0029:
	??_C@_0CJ@FFFLELDG@xbox_game_state_globals?4buffer_a@ (0000)
002A8184 002e:
	??_C@_0CO@BEKHBMJJ@couldn?8t?5open?5or?5create?5saved?5ga@ (0000)
002A81B4 0010:
	??_C@_0BA@MOOKLLKO@z?3?2savegame?4bin?$AA@ (0000)
002A81C4 0023:
	??_C@_0CD@DDMIEKAL@?$CBxbox_game_state_globals?4file_op@ (0000)
002A81E8 0022:
	??_C@_0CC@CLCLOOEI@xbox_game_state_globals?4file_ope@ (0000)
002A820C 0025:
	??_C@_0CF@BLHGLGEN@couldn?8t?5write?5saved?5game?5file?5?$CI@ (0000)
002A8234 0024:
	??_C@_0CE@CPCCDNFP@couldn?8t?5read?5saved?5game?5file?5?$CI?$CD@ (0000)
002A8258 0048:
	??_C@_0EI@JODPNGEB@xbox_game_state_globals?4file_val@ (0000)
002A82A0 000b:
	??_C@_0L@BACPNKLN@d?3?2core?2?$CFs?$AA@ (0000)
002A82AC 0008:
	??_C@_07ELALHFFD@d?3?2core?$AA@ (0000)
002A82B4 0036:
	??_C@_0DG@PGBLNDAA@game?5state?5has?5been?5corrupted?5?$CIt@ (0000)
002A82EC 000d:
	??_C@_0N@KAKGDJDL@savegame?4bin?$AA@ (0000)
002A82FC 0030:
	??_C@_0DA@GAKEGFCK@couldn?8t?5open?5or?5create?5persiste@ (0000)
002A832C 0028:
	??_C@_0CI@LDKKAEDE@couldn?8t?5resize?5persistent?5stora@ (0000)
002A8354 0033:
	??_C@_0DD@EGEEEGAD@couldn?8t?5read?5header?5from?5persis@ (0000)
002A8388 0026:
	??_C@_0CG@JDCOBIBA@checksum?5failed?5on?5persistent?5st@ (0000)
002A83B0 002c:
	??_C@_0CM@LDKGDBEH@failed?5to?5write?5to?5persistent?5st@ (0000)
002A83DC 0021:
	??_C@_0CB@GFGKONGF@header_size?$DMsizeof?$CIsaved_header?$CJ@ (0000)
002A8400 002d:
	??_C@_0CN@DOEIAHAD@failed?5to?5read?5from?5persistent?5s@ (0000)
004D27D0 0014:
	_xbox_game_state_globals (0000)
*/

/* ---------- headers */

#include "cache/physical_memory_map.h"
#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "interface/player_ui.h"
#include "memory/crc.h"
#include "saved games/game_state.h"
#include "sound/sound_manager.h"
#include <xtl.h>

/* ---------- constants */

enum
{
	CPU_PAGE_SIZE = 0x1000,
#ifdef HALO_LINUX
	/* the native builds' larger game state (halo_port_capacity.h); the saved
	game files only need to hold it */
	GAME_STATE_SIZE = HALO_PORT_GAME_STATE_SIZE,
	GAME_STATE_FILE_SIZE = HALO_PORT_GAME_STATE_SIZE
#else
	GAME_STATE_SIZE = 0x345000,
	GAME_STATE_FILE_SIZE = 0x380000
#endif
};

/* ---------- structures */

struct xbox_game_state_globals_prefix
{
	boolean buffer_allocated;
	byte reserved001[3];
	void *buffer;
	long buffer_size;
	boolean file_open;
	boolean file_valid_for_read;
	byte reserved00E[2];
	HANDLE handle;
};

typedef char verify_xbox_game_state_buffer_offset[
	offsetof(struct xbox_game_state_globals_prefix, buffer) == 0x4 ? 1 : -1];
typedef char verify_xbox_game_state_file_open_offset[
	offsetof(struct xbox_game_state_globals_prefix, file_open) == 0xC ? 1 : -1];
typedef char verify_xbox_game_state_handle_offset[
	offsetof(struct xbox_game_state_globals_prefix, handle) == 0x10 ? 1 : -1];
typedef char verify_xbox_game_state_globals_prefix_size[
	sizeof(struct xbox_game_state_globals_prefix) == 0x14 ? 1 : -1];

/* ---------- prototypes */

static HANDLE game_state_open_persistent_storage(
	const char *directory);
#ifdef HALO_LINUX
static void delete_persistent_storage(
	void);
#endif

/* ---------- globals */

struct xbox_game_state_globals_prefix xbox_game_state_globals = { 0 };

#ifdef HALO_LINUX
/* (port) The checkpoint without the hitch. The Xbox wrote the whole game
state to z:\savegame.bin at every checkpoint and read it back to revert;
the native builds' game state is 16 MB (halo_port_capacity.h, the Xbox's
was 3.3 MB), and writing it froze the Vita for a second at every
checkpoint (main_stop_time around it).

Now a checkpoint copies the game state into a buffer allocated once (tens
of milliseconds on the Vita), and that copy is what a revert loads (death,
cinematic skip, Save and Quit's revert before it writes the persistent
save): the same bytes the file would have given back. The file is still
written, from the copy, on a thread of its own, so it holds the last
checkpoint as before; a checkpoint waits for the previous one's write to
finish before it overwrites the copy, and anything that reads or closes
the file waits for the write too. Without the memory for the copy, the
file is written and read as on the Xbox. */
#include <pthread.h>
#include <stdlib.h>
#ifdef HALO_VITA
#include <psp2/kernel/sysmem.h>
/* (the Vita's renderer sees writes into guest memory only when told:
xbox_files.c's reads do the same) */
void memory_watch_prepare_write(void *address, unsigned long size);
#endif
void platform_log(const char *format, ...);

static struct
{
	void *snapshot;
	boolean snapshot_valid;
	boolean unavailable;
	boolean write_pending;
	boolean writing;
	pthread_mutex_t lock;
	pthread_cond_t changed;
	pthread_t thread;
	unsigned long writes;
} game_state_writer;

static double game_state_writer_ms(
	LARGE_INTEGER const *from)
{
	LARGE_INTEGER now, frequency;

	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&frequency);
	return frequency.QuadPart ? (double)(now.QuadPart - from->QuadPart) * 1000.0 / (double)frequency.QuadPart : 0.0;
}

static void *game_state_writer_thread(
	void *unused)
{
	(void)unused;
	for (;;)
	{
		OVERLAPPED overlapped;
		LARGE_INTEGER started;
		unsigned long bytes_written = 0;
		BOOL written;

		pthread_mutex_lock(&game_state_writer.lock);
		while (!game_state_writer.write_pending)
			pthread_cond_wait(&game_state_writer.changed, &game_state_writer.lock);
		game_state_writer.write_pending = FALSE;
		game_state_writer.writing = TRUE;
		pthread_mutex_unlock(&game_state_writer.lock);

		/* (at offset 0 without moving the file pointer) */
		memset(&overlapped, 0, sizeof(overlapped));
		QueryPerformanceCounter(&started);
		written = WriteFile(xbox_game_state_globals.handle, game_state_writer.snapshot,
			xbox_game_state_globals.buffer_size, &bytes_written, &overlapped) &&
			bytes_written == (unsigned long)xbox_game_state_globals.buffer_size;
		if (!written)
			platform_log("game state: couldn't write the checkpoint to the saved game file (#%d)", (int)GetLastError());
		else if (game_state_writer.writes++ < 4)
			platform_log("game state: checkpoint written to the saved game file in %.1f ms (in the background)",
				game_state_writer_ms(&started));

		pthread_mutex_lock(&game_state_writer.lock);
		game_state_writer.writing = FALSE;
		pthread_cond_broadcast(&game_state_writer.changed);
		pthread_mutex_unlock(&game_state_writer.lock);
	}
	return NULL;
}

/* the copy and its thread, made at the first checkpoint */
static boolean game_state_writer_ready(
	void)
{
	if (game_state_writer.snapshot)
		return TRUE;
	if (game_state_writer.unavailable)
		return FALSE;
	game_state_writer.unavailable = TRUE;
#ifdef HALO_VITA
	{
		SceUID block = sceKernelAllocMemBlock("game state checkpoint", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
			(xbox_game_state_globals.buffer_size + 0xFFF) & ~0xFFF, NULL);
		void *base = NULL;

		if (block < 0 || sceKernelGetMemBlockBase(block, &base) < 0)
			base = NULL;
		game_state_writer.snapshot = base;
	}
#else
	game_state_writer.snapshot = malloc(xbox_game_state_globals.buffer_size);
#endif
	if (!game_state_writer.snapshot)
	{
		platform_log("game state: no memory for the checkpoint copy (%ld bytes): checkpoints write the file as they are taken",
			(long)xbox_game_state_globals.buffer_size);
		return FALSE;
	}
	pthread_mutex_init(&game_state_writer.lock, NULL);
	pthread_cond_init(&game_state_writer.changed, NULL);
	{
		pthread_attr_t attributes;
		int created;

		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attributes, 64 * 1024);
		created = pthread_create(&game_state_writer.thread, &attributes, game_state_writer_thread, NULL);
		pthread_attr_destroy(&attributes);
		if (created != 0)
		{
			/* (the copy stays allocated; it is not used) */
			platform_log("game state: couldn't start the checkpoint writer: checkpoints write the file as they are taken");
			game_state_writer.snapshot = NULL;
			return FALSE;
		}
	}
	game_state_writer.unavailable = FALSE;
	return TRUE;
}

/* until the copy's write, if any, is done */
static void game_state_writer_wait(
	void)
{
	if (!game_state_writer.snapshot)
		return;
	pthread_mutex_lock(&game_state_writer.lock);
	while (game_state_writer.write_pending || game_state_writer.writing)
		pthread_cond_wait(&game_state_writer.changed, &game_state_writer.lock);
	pthread_mutex_unlock(&game_state_writer.lock);
}
#endif

/* ---------- public code */

void *game_state_allocate_buffer(
	unsigned long address,
	unsigned long cpu_size,
	unsigned long gpu_size)
{
	void *result;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		46,
		!xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		48,
		address);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		49,
		cpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		50,
		gpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		53,
		!(cpu_size&(CPU_PAGE_SIZE-1)));
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		54,
		!(gpu_size&(CPU_PAGE_SIZE-1)));

	result = physical_memory_get_game_state_base_address();
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		58,
		result);
#ifndef HALO_RELOCATABLE_TAG_CACHE
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		59,
		(unsigned long)result==address);
#else
	/* port: the game state goes wherever physical_memory_allocate put it */
	address = (unsigned long)result;
#endif

	XPhysicalProtect(
		(void *)(address+cpu_size),
		gpu_size,
		PAGE_READWRITE|PAGE_WRITECOMBINE);

	xbox_game_state_globals.buffer_allocated = TRUE;
	xbox_game_state_globals.buffer = (void *)address;
	xbox_game_state_globals.buffer_size = cpu_size+gpu_size;

	return result;
}

void game_state_free_buffer(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		75,
		xbox_game_state_globals.buffer_allocated);
	XPhysicalFree(xbox_game_state_globals.buffer);
	xbox_game_state_globals.buffer_allocated = FALSE;

	return;
}

void game_state_create_or_open_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		86,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		87,
		!xbox_game_state_globals.file_open);

	xbox_game_state_globals.handle = CreateFileA("z:\\savegame.bin",
		GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
		FILE_FLAG_NO_BUFFERING|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (xbox_game_state_globals.handle != INVALID_HANDLE_VALUE &&
		SetFilePointer(xbox_game_state_globals.handle, GAME_STATE_FILE_SIZE, NULL,
			FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		SetEndOfFile(xbox_game_state_globals.handle))
	{
		xbox_game_state_globals.file_open = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			97,
			FALSE,
			csprintf(temporary, "couldn't open or create saved game file (#%d)",
				GetLastError()));
	}

	return;
}

void game_state_close_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		106,
		xbox_game_state_globals.file_open);
#ifdef HALO_LINUX
	game_state_writer_wait();
#endif
	CloseHandle(xbox_game_state_globals.handle);
	xbox_game_state_globals.file_open = FALSE;

	return;
}

boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		120,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		121,
		xbox_game_state_globals.file_open);

#ifdef HALO_LINUX
	/* (the copy, written in the background: above) */
	if (game_state_writer_ready())
	{
		static unsigned long checkpoints_logged;
		LARGE_INTEGER started;

		QueryPerformanceCounter(&started);
		game_state_writer_wait();
		memcpy(game_state_writer.snapshot, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
		if (checkpoints_logged++ < 8)
			platform_log("game state: checkpoint taken in %.1f ms", game_state_writer_ms(&started));
		game_state_writer.snapshot_valid = TRUE;
		xbox_game_state_globals.file_valid_for_read = TRUE;
		pthread_mutex_lock(&game_state_writer.lock);
		game_state_writer.write_pending = TRUE;
		pthread_cond_broadcast(&game_state_writer.changed);
		pthread_mutex_unlock(&game_state_writer.lock);

		return TRUE;
	}
#endif
	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)
	{
		xbox_game_state_globals.file_valid_for_read = TRUE;
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			132,
			FALSE,
			csprintf(temporary, "couldn't write saved game file (#%d)", GetLastError()));
	}

	return result;
}

boolean game_state_read_from_file(
	void)
{
	unsigned long bytes_read;
	boolean result = FALSE;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		144,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		145,
		xbox_game_state_globals.file_open);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		146,
		xbox_game_state_globals.file_valid_for_read || recover_saved_games_hack);

#ifdef HALO_LINUX
	/* (the last checkpoint's copy holds what the file would give back:
	above) */
	if (game_state_writer.snapshot_valid)
	{
#ifdef HALO_VITA
		memory_watch_prepare_write(xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
#endif
		memcpy(xbox_game_state_globals.buffer, game_state_writer.snapshot, xbox_game_state_globals.buffer_size);

		return TRUE;
	}
	game_state_writer_wait();
#endif

	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size)
	{
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			156,
			FALSE,
			csprintf(temporary, "couldn't read saved game file (#%d)", GetLastError()));
	}

	return result;
}

boolean game_state_write_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean result = FALSE;

	CreateDirectoryA("d:\\core", NULL);
	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (WriteFile(file, buffer, buffer_size, &bytes_written, NULL) &&
			bytes_written == buffer_size)
		{
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

boolean game_state_read_core_header(
	const char *name,
	void *header,
	long header_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;
	boolean result = FALSE;

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (ReadFile(file, header, header_size, &bytes_read, NULL) &&
			bytes_read == header_size)
		{
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

void game_state_read_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	match_vassert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		226,
		file != INVALID_HANDLE_VALUE &&
			ReadFile(file, buffer, buffer_size, &bytes_read, NULL) &&
			bytes_read == buffer_size,
		"game state has been corrupted (thank you, come again)");
	CloseHandle(file);

	return;
}

const char *game_state_get_persistent_storage_filename(
	void)
{
	return "savegame.bin";
}

static __inline boolean game_state_get_persistent_storage_path(
	char *path)
{
	boolean result;

#ifdef HALO_LINUX
	{
		/* (port, debug) HALO_TEST_PERSISTENT_DIR: the campaign save's
		directory without a profile chosen in the menu, for driving Save
		and Quit and the resume from the harness (HALO_TEST_COMMANDS) */
		const char *directory = getenv("HALO_TEST_PERSISTENT_DIR");

		if (directory && *directory)
		{
			strcpy(path, directory);
			return TRUE;
		}
	}
#endif
	result = player_ui_get_path_to_local_player_profile_directory(0, path) != FALSE;

	return result;
}

static void delete_persistent_storage(
	void)
{
	char path[256];

	if (game_state_get_persistent_storage_path(path))
		DeleteFileA(path);

	return;
}

static HANDLE game_state_open_persistent_storage(
	const char *directory)
{
	char path[256];
	byte zeroes[16*1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean success = FALSE;

	if (directory || game_state_get_persistent_storage_path(path))
	{
		if (directory)
		{
			strcpy(path, directory);
		}
		else
		{
			game_state_get_persistent_storage_path(path);
		}
		strcat(path, "savegame.bin");

		file = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0,
			NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			success = TRUE;
			if (GetFileSize(file, NULL) != GAME_STATE_FILE_SIZE)
			{
				memset(zeroes, 0, sizeof(zeroes));
				if (!WriteFile(file, zeroes, sizeof(zeroes), &bytes_written, NULL) ||
					bytes_written != sizeof(zeroes) ||
					SetFilePointer(file, GAME_STATE_FILE_SIZE, NULL, FILE_BEGIN) ==
						INVALID_SET_FILE_POINTER ||
					!SetEndOfFile(file))
				{
#ifdef HALO_LINUX
					/* (port) the release builds compile the assertions out:
					debug.txt is the only trace a failed save leaves */
					error(_error_silent, "couldn't resize persistent storage \"%s\" (#%d)", path,
						(int)GetLastError());
#endif
					match_vassert(
						"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
						491,
						FALSE,
						csprintf(temporary,
							"couldn't resize persistent storage \"%s\"", path));
					delete_persistent_storage();
					CloseHandle(file);
					success = FALSE;
				}
			}
		}
		else
		{
#ifdef HALO_LINUX
			error(_error_silent, "couldn't open or create persistent storage \"%s\" (#%d)", path,
				(int)GetLastError());
#endif
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				498,
				FALSE,
				csprintf(temporary, "couldn't open or create persistent storage \"%s\"",
					path));
		}
	}

	return success ? file : INVALID_HANDLE_VALUE;
}

boolean game_state_read_header_from_persistent_storage(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
	byte buffer[128*1024];
	HANDLE file;
	unsigned long checksum;
	unsigned long stored_checksum;
	unsigned long bytes_read;
	long remaining_size;
	long read_size;
	boolean result;

	file = game_state_open_persistent_storage(NULL);
	result = FALSE;
	if (corrupted)
		*corrupted = FALSE;

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, header, header_size, &bytes_read, NULL) ||
			bytes_read != header_size)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				298,
				FALSE,
				csprintf(temporary,
					"couldn't read header from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		else
		{
			stored_checksum = *header_checksum;

			crc_new(&checksum);
			*header_checksum = 0;
			crc_checksum_buffer(&checksum, header, header_size);

			remaining_size = buffer_size-header_size;
			while (remaining_size>0)
			{
				read_size = MIN(sizeof(buffer), remaining_size);
				if (ReadFile(file, buffer, read_size, &bytes_read, NULL) &&
					bytes_read == read_size)
				{
					crc_checksum_buffer(&checksum, buffer, read_size);
				}
				sound_idle();
				remaining_size -= read_size;
			}

			if (checksum == stored_checksum)
			{
				result = TRUE;
			}
			else
			{
				if (corrupted && stored_checksum)
					*corrupted = TRUE;
				error(_error_silent, "checksum failed on persistent storage");
			}
		}
		CloseHandle(file);
	}

	return result;
}

void game_state_write_to_persistent_storage(
	void *buffer,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size)
{
	byte saved_header[2048];
	HANDLE file;
	unsigned long checksum;
	unsigned long bytes_written;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		*header_checksum = 0;

		crc_new(&checksum);
		crc_checksum_buffer(&checksum, buffer, GAME_STATE_SIZE);
		*header_checksum = checksum;

		match_assert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			333,
			header_size<sizeof(saved_header));

		memcpy(saved_header, buffer, header_size);
		memset(buffer, 0, header_size);

		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, buffer, buffer_size, &bytes_written, NULL) ||
			bytes_written != buffer_size ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, saved_header, header_size, &bytes_written, NULL) ||
			bytes_written != header_size)
		{
#ifdef HALO_LINUX
			error(_error_silent, "failed to write to persistent storage (#%d)", (int)GetLastError());
#endif
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				351,
				FALSE,
				csprintf(temporary, "failed to write to persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
#ifdef HALO_LINUX
		else
		{
			/* (port) so a device's debug.txt shows that Save and Quit
			wrote the campaign save */
			error(_error_silent, "saved the last checkpoint to persistent storage");
		}
#endif

		memcpy(buffer, saved_header, header_size);
		CloseHandle(file);
	}

	return;
}

void game_state_read_from_persistent_storage(
	void *buffer,
	long buffer_size)
{
	HANDLE file;
	unsigned long bytes_read;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, buffer, buffer_size, &bytes_read, NULL) ||
			bytes_read != buffer_size)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				383,
				FALSE,
				csprintf(temporary, "failed to read from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		CloseHandle(file);
	}

	return;
}

void game_state_create_persistent_storage(
	const char *path)
{
	HANDLE handle;

	handle = game_state_open_persistent_storage(path);
	if (handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);

	return;
}

#ifdef HALO_LINUX
/* (port) Loading the persistent save at a map's start. The Xbox read its
header and the whole file to check the checksum, and if the header named
this map, read the whole file again into the game state: 32 MB from the
memory card on the native builds, for every new map while a save exists,
even one for another level (the header is checked only after the
checksum).

game_state_peek_persistent_storage_header reads the header alone, so a
save that cannot be loaded is not read through; game_state_read_persistent
_storage_staged reads the file once, into the checkpoint copy (above),
checks the checksum as the Xbox did (the header's checksum field taken as
zero, then everything), and game_state_load_staged_persistent_storage
copies it into the game state: the same bytes, one read. The checkpoint
copy is the last checkpoint of the map being left, which no revert can
use once a new map is loading (game_state_initialize_for_new_map marks it
invalid); the game state's save right after the load makes a new one. */
boolean game_state_peek_persistent_storage_header(
	void *header,
	long header_size)
{
	HANDLE file = game_state_open_persistent_storage(NULL);
	unsigned long bytes_read;
	boolean result = FALSE;

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, header, header_size, &bytes_read, NULL) ||
			bytes_read != (unsigned long)header_size)
		{
			/* (as the full read does when the header cannot be read) */
			error(_error_silent, "couldn't read header from persistent storage (#%d)", (int)GetLastError());
			delete_persistent_storage();
		}
		else
		{
			result = TRUE;
		}
		CloseHandle(file);
	}
	return result;
}

/* 1: read and checked into the copy; 0: unreadable or the checksum failed;
-1: no copy to read into (the caller reads as the Xbox did) */
int game_state_read_persistent_storage_staged(
	long header_size,
	long checksum_offset,
	long buffer_size)
{
	byte saved_header[2048];
	unsigned long stored_checksum;
	unsigned long checksum;
	unsigned long bytes_read;
	HANDLE file;
	int result = 0;

	if (!game_state_writer_ready() || buffer_size > xbox_game_state_globals.buffer_size ||
		header_size > (long)sizeof(saved_header))
	{
		return -1;
	}
	/* (the copy's last write is done before it is overwritten) */
	game_state_writer_wait();
	game_state_writer.snapshot_valid = FALSE;
	file = game_state_open_persistent_storage(NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	if (SetFilePointer(file, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		ReadFile(file, game_state_writer.snapshot, buffer_size, &bytes_read, NULL) &&
		bytes_read == (unsigned long)buffer_size)
	{
		memcpy(saved_header, game_state_writer.snapshot, header_size);
		memcpy(&stored_checksum, saved_header + checksum_offset, sizeof(stored_checksum));
		memset(saved_header + checksum_offset, 0, sizeof(stored_checksum));
		crc_new(&checksum);
		crc_checksum_buffer(&checksum, saved_header, header_size);
		crc_checksum_buffer(&checksum, (byte *)game_state_writer.snapshot + header_size, buffer_size - header_size);
		if (checksum == stored_checksum)
			result = 1;
		else
			error(_error_silent, "checksum failed on persistent storage");
	}
	else
	{
		error(_error_silent, "failed to read from persistent storage (#%d)", (int)GetLastError());
	}
	CloseHandle(file);
	return result;
}

void game_state_load_staged_persistent_storage(
	void *buffer,
	long buffer_size)
{
#ifdef HALO_VITA
	memory_watch_prepare_write(buffer, buffer_size);
#endif
	memcpy(buffer, game_state_writer.snapshot, buffer_size);
}
#endif

/* ---------- private code */
