/* posix_dedicated_server.c

The dedicated server's platform side (port/linux/DEDICATED_SERVER.md,
port/linux/game/dedicated_server.c; built only into `ninja linux-server`,
HALO_DEDICATED_SERVER):

- Before the game starts (a constructor, before anything reads config.toml):
  the command line (/proc/self/cmdline: the game's main takes no arguments)
  -path DIR (the server's folder: maps/, init.txt, config.toml, bans.txt,
  debug.txt; default the working directory), -exec FILE (the commands run at
  start, default init.txt in that folder), -port N (internet play's UDP port,
  default 2302), -help; and what a server never has: a window, sound, vsync,
  frame interpolation, the self-updater, Discord, and the telnet console.
- The server's commands: the file's lines (read once, at the start) and the
  local console's (standard input, read on a thread of its own and queued),
  handed to the game one at a time (dedicated_platform_next_command). Nothing
  here runs a command: the game's side knows only its own sv_* commands, and
  no shell or script is ever started.
- Its output (dedicated_platform_print): standard output, a line at a time. */

#if defined(HALO_DEDICATED_SERVER) && defined(__linux__)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
	/* a command's longest line (the rest of a longer one is dropped) */
	COMMAND_SIZE = 256,
	/* the console's lines waiting for the game; more are dropped */
	MAXIMUM_QUEUED_COMMANDS = 64,
	/* the file's commands (a file of more is cut, and the operator told) */
	MAXIMUM_FILE_COMMANDS = 512,
	/* internet play's port when -port and sv_port say none: Halo PC's */
	DEFAULT_TUNNEL_PORT = 2302,
};

static char server_folder[PATH_MAX];
static char command_file[PATH_MAX];
static char *file_commands[MAXIMUM_FILE_COMMANDS];
static int file_command_count;
static int file_command_next;
static int file_read;

static volatile sig_atomic_t quit_signal;

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static char queued[MAXIMUM_QUEUED_COMMANDS][COMMAND_SIZE];
static int queue_first, queue_count;
static int console_closed;

void dedicated_platform_print(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stdout, format, arguments);
	va_end(arguments);
	fputc('\n', stdout);
	fflush(stdout);
}

static void usage(void)
{
	printf("halo-server: Halo CE for PS Vita's dedicated server (port/linux/DEDICATED_SERVER.md)\n"
		"\n"
		"  halo-server [-path DIR] [-exec FILE] [-port N]\n"
		"\n"
		"  -path DIR   the server's folder: maps/ (the game data), init.txt,\n"
		"              config.toml, bans.txt and debug.txt (default: the working\n"
		"              directory)\n"
		"  -exec FILE  the commands to run at the start (default: init.txt in the\n"
		"              server's folder): sv_name, sv_mapcycle_add and the rest\n"
		"  -port N     internet play's UDP port, the one to forward (default %d)\n"
		"\n"
		"Commands are typed on the standard input; \"help\" lists them.\n", DEFAULT_TUNNEL_PORT);
}

/* the command line's words, from /proc/self/cmdline (NUL separated) */
static int read_arguments(char *buffer, size_t size, char **words, int maximum)
{
	FILE *file = fopen("/proc/self/cmdline", "rb");
	size_t length;
	size_t index;
	int count = 0;

	if (!file)
		return 0;
	length = fread(buffer, 1, size - 1, file);
	fclose(file);
	buffer[length] = 0;
	for (index = 0; index < length && count < maximum; index += strlen(buffer + index) + 1)
		words[count++] = buffer + index;
	return count;
}

/* a full path of a folder or file (relative to the working directory) */
static void absolute(const char *path, char *result, size_t size)
{
	char here[PATH_MAX];

	if (path[0] == '/' || !getcwd(here, sizeof(here)))
		snprintf(result, size, "%s", path);
	else
		snprintf(result, size, "%s/%s", here, path);
}

static int folder_exists(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static void force(const char *name, const char *value)
{
	setenv(name, value, 1);
}

/* SIGTERM (systemctl stop), SIGINT (Ctrl+C), SIGHUP (the terminal gone): the
game stops at its next frame, as quit does, so that the server browser's
listing is closed (p2p_lobby.c's tombstone, at exit) */
static void quit_handler(int number)
{
	(void)number;
	quit_signal = 1;
}

int dedicated_platform_quit_requested(void)
{
	return quit_signal != 0;
}

/* the commands that take effect at the start alone, from the file before
anything reads the settings: sv_port (internet play's UDP port, unless -port
gave one), sv_public_address and sv_relay */
static void startup_commands(int port_given)
{
	FILE *file = fopen(command_file, "r");
	char line[512];
	char relays[512] = "";

	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		char word[32], value[256];
		int index;

		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, " %31s %255s", word, value) != 2)
			continue;
		for (index = 0; word[index]; index++)
			word[index] = (char)tolower((unsigned char)word[index]);
		/* (a quoted value's inside) */
		if (value[0] == '"')
		{
			memmove(value, value + 1, strlen(value));
			value[strcspn(value, "\"")] = 0;
		}
		if (!strcmp(word, "sv_port") && !port_given)
		{
			char *end;
			long port = strtol(value, &end, 10);

			if (*end || port < 1 || port > 65535)
				fprintf(stderr, "halo-server: sv_port %s is not a port (1 to 65535)\n", value);
			else
				force("HALO_NET_TUNNEL_PORT", value);
		}
		else if (!strcmp(word, "sv_public_address"))
			force("HALO_SERVER_PUBLIC_ADDRESS", value);
		else if (!strcmp(word, "sv_relay") && strlen(relays) + strlen(value) + 2 < sizeof(relays))
		{
			if (relays[0])
				strcat(relays, ",");
			strcat(relays, value);
		}
	}
	fclose(file);
	if (relays[0])
		force("HALO_NET_RELAYS", relays);
}

/* (a priority above the game's own constructors: it decides what they read) */
__attribute__((constructor(200))) static void dedicated_platform_start(void)
{
	static char arguments[8192];
	char *words[64];
	int count = read_arguments(arguments, sizeof(arguments), words, 64);
	const char *path = NULL;
	const char *exec = NULL;
	const char *port = NULL;
	int index;

	for (index = 1; index < count; index++)
	{
		const char *word = words[index];
		const char *value = index + 1 < count ? words[index + 1] : NULL;

		/* (haloceded's are one dash; two are taken too) */
		if (word[0] == '-' && word[1] == '-')
			word++;
		if (!strcmp(word, "-help") || !strcmp(word, "-h") || !strcmp(word, "-?"))
		{
			usage();
			exit(0);
		}
		else if (!strcmp(word, "-path") && value)
			path = value, index++;
		else if (!strcmp(word, "-exec") && value)
			exec = value, index++;
		else if (!strcmp(word, "-port") && value)
			port = value, index++;
		else
		{
			fprintf(stderr, "halo-server: unknown argument %s (-help lists them)\n", words[index]);
			exit(2);
		}
	}

	/* the server's folder: the game's data root (d:\), and config.toml's
	(port_config.c); the environment's HALO_DATA_ROOT when no -path */
	if (!path)
		path = getenv("HALO_DATA_ROOT") && getenv("HALO_DATA_ROOT")[0] ? getenv("HALO_DATA_ROOT") : ".";
	absolute(path, server_folder, sizeof(server_folder));
	if (!folder_exists(server_folder))
	{
		fprintf(stderr, "halo-server: the server's folder %s does not exist (-path)\n", server_folder);
		exit(2);
	}
	force("HALO_DATA_ROOT", server_folder);
	if (!getenv("HALO_SAVE_ROOT") || !getenv("HALO_SAVE_ROOT")[0])
	{
		char saves[PATH_MAX + 8];

		snprintf(saves, sizeof(saves), "%s/saves", server_folder);
		force("HALO_SAVE_ROOT", saves);
	}
	if (exec)
		absolute(exec, command_file, sizeof(command_file));
	else
		snprintf(command_file, sizeof(command_file), "%s/init.txt", server_folder);
	if (exec && access(command_file, R_OK) != 0)
	{
		fprintf(stderr, "halo-server: cannot read %s (-exec): %s\n", command_file, strerror(errno));
		exit(2);
	}
	/* internet play's port: -port, else the file's sv_port
	(startup_commands), else Halo PC's */
	if (port)
	{
		char *end;
		long value = strtol(port, &end, 10);

		if (*end || value < 1 || value > 65535)
		{
			fprintf(stderr, "halo-server: -port %s is not a port (1 to 65535)\n", port);
			exit(2);
		}
		force("HALO_NET_TUNNEL_PORT", port);
	}
	else if (!getenv("HALO_NET_TUNNEL_PORT"))
	{
		char text[16];

		snprintf(text, sizeof(text), "%d", DEFAULT_TUNNEL_PORT);
		force("HALO_NET_TUNNEL_PORT", text);
	}

	startup_commands(port != NULL);
	{
		struct sigaction action;

		memset(&action, 0, sizeof(action));
		action.sa_handler = quit_handler;
		sigemptyset(&action.sa_mask);
		sigaction(SIGTERM, &action, NULL);
		sigaction(SIGINT, &action, NULL);
		sigaction(SIGHUP, &action, NULL);
		signal(SIGPIPE, SIG_IGN);
		/* (started in the background of a shell, "halo-server &": reading the
		terminal would stop the whole server; ignored, the read fails and
		the console is off) */
		signal(SIGTTIN, SIG_IGN);
	}

	/* what a server never has (the settings' variables: port_config.c) */
	force("HALO_NULL_RENDERER", "1");
	force("HALO_HIDDEN_WINDOW", "1");
	force("HALO_NO_AUDIO", "1");
	force("HALO_NO_VSYNC", "1");
	force("HALO_INTERPOLATION", "false");
	force("HALO_FULLSCREEN", "false");
	force("HALO_UPDATE_AUTO", "false");
	force("HALO_DISCORD_APPLICATION", "");
	force("HALO_NET_JOIN_FROM_CLIPBOARD", "false");
	force("SDL_VIDEODRIVER", "dummy");
	force("SDL_AUDIODRIVER", "dummy");
	/* (no script console, local or not: the telnet console runs any
	command with no password; it is compiled out too) */
	unsetenv("HALO_TELNET_CONSOLE");
	/* (no automated test session, nor a test bot at controller 1) */
	unsetenv("HALO_NETWORK_TEST");
	unsetenv("HALO_SYSTEM_LINK_TEST");
	unsetenv("HALO_TEST_INPUT");
	unsetenv("HALO_TEST_PAD");
	unsetenv("HALO_TEST_COMMANDS");
}

/* the server's folder (absolute, no separator at the end) */
const char *dedicated_platform_folder(void)
{
	return server_folder;
}

/* (the console's thread) standard input, a line at a time, queued */
static void *console_thread(void *unused)
{
	char line[1024];

	(void)unused;
	while (fgets(line, sizeof(line), stdin))
	{
		size_t length = strlen(line);

		/* (a longer line's rest: read and dropped) */
		if (length && line[length - 1] != '\n' && !feof(stdin))
		{
			int character;

			while ((character = fgetc(stdin)) != EOF && character != '\n')
				;
		}
		line[strcspn(line, "\r\n")] = 0;
		pthread_mutex_lock(&queue_lock);
		if (queue_count < MAXIMUM_QUEUED_COMMANDS)
		{
			snprintf(queued[(queue_first + queue_count) % MAXIMUM_QUEUED_COMMANDS], COMMAND_SIZE, "%s", line);
			queue_count++;
		}
		pthread_mutex_unlock(&queue_lock);
	}
	pthread_mutex_lock(&queue_lock);
	console_closed = 1;
	pthread_mutex_unlock(&queue_lock);
	return NULL;
}

/* the console starts (once the game is up: nothing typed before is lost,
the terminal keeps it) */
void dedicated_platform_console_start(void)
{
	static int started;
	pthread_t thread;

	if (started)
		return;
	started = 1;
	/* (no terminal, e.g. under systemd with standard input /dev/null: no
	console, and nothing to read) */
	if (pthread_create(&thread, NULL, console_thread, NULL) == 0)
		pthread_detach(thread);
}

/* the file's commands, read once: 1 if it was there */
static int read_command_file(void)
{
	FILE *file;
	char line[1024];

	file_read = 1;
	file = fopen(command_file, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file))
	{
		size_t length = strlen(line);

		if (length && line[length - 1] != '\n' && !feof(file))
		{
			int character;

			while ((character = fgetc(file)) != EOF && character != '\n')
				;
		}
		line[strcspn(line, "\r\n")] = 0;
		if (file_command_count == MAXIMUM_FILE_COMMANDS)
		{
			dedicated_platform_print("server: %s has more than %d lines; the rest are left out", command_file,
				MAXIMUM_FILE_COMMANDS);
			break;
		}
		file_commands[file_command_count] = strdup(line);
		if (file_commands[file_command_count])
			file_command_count++;
	}
	fclose(file);
	return 1;
}

/* the start's file: its name, and whether it was there (read on the first
question) */
const char *dedicated_platform_command_file(int *found)
{
	static int there;

	if (!file_read)
		there = read_command_file();
	if (found)
		*found = there;
	return command_file;
}

/* the next command (the file's first, all of them, then the console's):
1 with the line in text (from_file says which), 0 if none waits */
int dedicated_platform_next_command(char *text, int size, int *from_file)
{
	int result = 0;

	if (!file_read)
		read_command_file();
	if (file_command_next < file_command_count)
	{
		snprintf(text, (size_t)size, "%s", file_commands[file_command_next]);
		free(file_commands[file_command_next]);
		file_commands[file_command_next++] = NULL;
		*from_file = 1;
		return 1;
	}
	pthread_mutex_lock(&queue_lock);
	if (queue_count)
	{
		snprintf(text, (size_t)size, "%s", queued[queue_first]);
		queue_first = (queue_first + 1) % MAXIMUM_QUEUED_COMMANDS;
		queue_count--;
		*from_file = 0;
		result = 1;
	}
	pthread_mutex_unlock(&queue_lock);
	return result;
}

#endif
