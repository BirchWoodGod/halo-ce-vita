/*
PORT_CONFIG_TEST.C

A desktop test of the settings file, config.toml (port/linux/src/port_config.c,
included whole), built twice by run_port_config_test.sh: as the Vita's
(HALO_NOT_DESKTOP: the file in HALO_DATA_ROOT) and as the desktop's (the
file beside the executable, with SDL's file calls standing in). In a folder
of its own:

  defaults   written at the first start: each section once, every setting
             of the build in it with its type; read at the next start
             without a word of error (1.1's had [game] and [debug] twice:
             "table defined more than once", every setting at its default)
  writes     the settings the game writes (co-op's Public and Private, the
             updater's Do not ask again, ...) one after another: each
             changes its line and nothing else, and they are all read back
             at the next start
  hand       a section written "[ network ]", Windows line ends, a key
             written dotted before any section: the settings new in this
             version and the writes go where those are, no section twice
  110        1.1's file (config_110_vita.toml, as 1.1 wrote it on the Vita,
             five settings changed in it, in the sections there twice):
             repaired at the start with all five kept, the file as it was
             kept as config.toml.broken, read as it is at the next start
  salvage    a value that does not read: the file written again from the
             defaults with its other values, that one at its default; the
             file as it was kept as config.toml.broken1 (.broken is there)
  gone       the file deleted while the game runs: a write puts all of it
             back, with the setting written
  unreadable a file there that cannot be read: left as it is, and a write
             leaves it too

Run port/vita/tests/run_port_config_test.sh.
*/

#define __HALO_LINUX_PLATFORM_H
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#include "port_config.c"

#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static char folder[800], config_file[900], fixture[1024];
static char log_text[65536];
static int failures;

void platform_log(const char *format, ...)
{
	size_t length = strlen(log_text);
	va_list arguments;

	va_start(arguments, format);
	if (length + 2 < sizeof(log_text))
		vsnprintf(log_text + length, sizeof(log_text) - length - 1, format, arguments);
	va_end(arguments);
	length = strlen(log_text);
	if (length + 1 < sizeof(log_text))
		strcat(log_text, "\n");
}

#ifndef HALO_NOT_DESKTOP
/* (SDL's file calls, which the desktop's config.toml goes through) */
static char base_path[900];

const char *SDL_GetBasePath(void)
{
	return base_path;
}

void *SDL_LoadFile(const char *file, size_t *datasize)
{
	FILE *stream = fopen(file, "rb");
	char *data = NULL;
	long length;

	if (!stream)
		return NULL;
	if (fseek(stream, 0, SEEK_END) == 0 && (length = ftell(stream)) >= 0 && fseek(stream, 0, SEEK_SET) == 0)
	{
		data = malloc((size_t)length + 1);
		if (data && fread(data, 1, (size_t)length, stream) == (size_t)length)
		{
			data[length] = 0;
			*datasize = (size_t)length;
		}
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(stream);
	return data;
}

bool SDL_SaveFile(const char *file, const void *data, size_t datasize)
{
	FILE *stream = fopen(file, "wb");
	int written;

	if (!stream)
		return false;
	written = fwrite(data, 1, datasize, stream) == datasize;
	return fclose(stream) == 0 && written;
}

void SDL_free(void *mem)
{
	free(mem);
}

bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct stat information;

	(void)info;
	return stat(path, &information) == 0;
}
#endif

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* the game started again: nothing read yet, the log empty */
static void restart(void)
{
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		free(config_values[index].string);
		memset(&config_values[index], 0, sizeof(config_values[index]));
	}
	config_loaded = 0;
	log_text[0] = 0;
}

static char *read_whole(const char *path, size_t *size)
{
	FILE *stream = fopen(path, "rb");
	char *text;
	long length;

	if (!stream)
		return NULL;
	fseek(stream, 0, SEEK_END);
	length = ftell(stream);
	fseek(stream, 0, SEEK_SET);
	text = malloc((size_t)length + 1);
	if (fread(text, 1, (size_t)length, stream) != (size_t)length)
		length = 0;
	text[length] = 0;
	fclose(stream);
	if (size)
		*size = (size_t)length;
	return text;
}

static void write_whole(const char *path, const char *text)
{
	FILE *stream = fopen(path, "wb");

	fwrite(text, 1, strlen(text), stream);
	fclose(stream);
}

static int log_has(const char *text)
{
	return strstr(log_text, text) != NULL;
}

/* whether the file reads, each section in it once, every setting of this
build there with its type */
static int file_reads_whole(const char *what)
{
	char *text = read_whole(config_file, NULL);
	char names[64][64];
	int count = 0, good = 1;
	const char *line;
	toml_result_t result;
	size_t index;

	if (!text)
	{
		printf("FAIL: %s: no config.toml\n", what);
		failures++;
		return 0;
	}
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		char name[64];
		int other;

		if (config_line_section(line, end, name, sizeof(name)))
		{
			for (other = 0; other < count; other++)
			{
				if (!strcmp(names[other], name))
				{
					printf("FAIL: %s: [%s] twice\n", what, name);
					good = 0;
				}
			}
			if (count < 64)
				snprintf(names[count++], sizeof(names[0]), "%s", name);
		}
		line = *end ? end + 1 : end;
	}
	result = toml_parse(text, (int)strlen(text));
	if (!result.ok)
	{
		printf("FAIL: %s: config.toml does not read: %s\n", what, result.errmsg);
		good = 0;
	}
	else
	{
		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			const struct config_setting *setting = &config_settings[index];
			toml_datum_t datum;
			int fits = 0;

			if (!(setting->platforms & CONFIG_PLATFORM))
				continue;
			datum = toml_seek(result.toptab, setting->name);
			switch (setting->type)
			{
			case _config_boolean: fits = datum.type == TOML_BOOLEAN; break;
			case _config_integer: fits = datum.type == TOML_INT64; break;
			case _config_real: fits = datum.type == TOML_FP64 || datum.type == TOML_INT64; break;
			case _config_string: fits = datum.type == TOML_STRING; break;
			}
			if (!fits)
			{
				printf("FAIL: %s: %s missing or of the wrong type\n", what, setting->name);
				good = 0;
			}
		}
	}
	toml_free(result);
	free(text);
	if (!good)
		failures++;
	return good;
}

/* the lines of a and b that differ (a and b of as many lines), or -1 */
static int lines_changed(const char *a, const char *b)
{
	int changed = 0;

	while (*a || *b)
	{
		size_t length_a = strcspn(a, "\n"), length_b = strcspn(b, "\n");

		if (!*a || !*b)
			return -1;
		if (length_a != length_b || memcmp(a, b, length_a))
			changed++;
		a += length_a + (a[length_a] != 0);
		b += length_b + (b[length_b] != 0);
	}
	return changed;
}

static void test_defaults(void)
{
	printf("--- defaults\n");
	unlink(config_file);
	restart();
	check(config_boolean("network.online") == 1, "network.online true by default");
	check(log_has("settings: wrote the defaults"), "the defaults written at the first start");
	file_reads_whole("the defaults");
	restart();
	check(config_boolean("network.online") == 1, "network.online still true");
	check(!log_has("config.toml:"), "the defaults read at the next start without a word");
	check(!log_has("added"), "nothing missing from the defaults");
	if (log_has("config.toml:"))
		printf("%s", log_text);
}

static void test_writes(void)
{
	static const struct
	{
		const char *name;
		int value;
	} writes[] =
	{
		{ "network.coop_public", 1 },
		{ "update.auto", 0 },
		{ "network.host_public", 0 },
		{ "network.coop_public", 0 },
		{ "network.coop_public", 1 },
		{ "display.vsync", 0 },
		{ "display.interpolation", 0 },
		{ "audio.enabled", 0 },
		{ "game.custom_edition", 1 },
		{ "network.online", 0 },
		{ "network.join_from_clipboard", 0 },
		{ "network.allow_upnp", 0 },
		{ "network.allow_relay", 0 },
		{ "network.adhoc", 1 },
		{ "network.public_lobby", 0 },
		{ "debug.telnet_console", 1 },
		{ "debug.null_renderer", 1 },
		{ "update.auto", 1 },
		{ "update.auto", 0 },
	};
	size_t index;

	printf("--- writes\n");
	restart();
	for (index = 0; index < sizeof(writes) / sizeof(writes[0]); index++)
	{
		char *before = read_whole(config_file, NULL);
		char *after;
		char what[160];

		snprintf(what, sizeof(what), "%s = %d written", writes[index].name, writes[index].value);
		check(config_write_boolean(writes[index].name, writes[index].value) == 1, what);
		after = read_whole(config_file, NULL);
		snprintf(what, sizeof(what), "%s = %d: only its line changed", writes[index].name, writes[index].value);
		check(before && after && lines_changed(before, after) <= 1, what);
		snprintf(what, sizeof(what), "after %s = %d", writes[index].name, writes[index].value);
		file_reads_whole(what);
		check(config_boolean(writes[index].name) == writes[index].value, "the value written is the value now");
		free(before);
		free(after);
	}
	restart();
	check(config_boolean("network.coop_public") == 1, "network.coop_public read back");
	check(config_boolean("update.auto") == 0, "update.auto read back");
	check(config_boolean("network.host_public") == 0, "network.host_public read back");
	check(config_boolean("display.vsync") == 0, "display.vsync read back");
	check(config_boolean("game.custom_edition") == 1, "game.custom_edition read back");
	check(config_boolean("network.adhoc") == 1, "network.adhoc read back");
	check(config_boolean("debug.telnet_console") == 1, "debug.telnet_console read back");
	check(!log_has("config.toml:"), "read back without a word");
}

static void test_hand(void)
{
	char *text;

	printf("--- hand\n");
	write_whole(config_file,
		"# mine\r\n"
		"update.auto = true\r\n"
		"\r\n"
		"[ network ]   # spaced\r\n"
		"online = true\r\n"
		"\r\n"
		"[game]\r\n"
		"language = \"de\"\r\n");
	restart();
	check(!strcmp(config_string("game.language"), "de"), "game.language read from a hand-made file");
	check(config_boolean("update.auto") == 1, "a dotted update.auto read");
	check(!log_has("config.toml:"), "a hand-made file read without an error");
	file_reads_whole("the settings new in this version added to a hand-made file");
	check(config_write_boolean("update.auto", 0) == 1, "update.auto written to a dotted key");
	check(config_write_boolean("network.coop_public", 1) == 1, "network.coop_public written into [ network ]");
	file_reads_whole("written into a hand-made file");
	text = read_whole(config_file, NULL);
	check(text && strstr(text, "update.auto = false\r\n") != NULL, "the dotted key changed where it is");
	check(text && strstr(text, "[update]") == NULL, "no [update] added beside the dotted key");
	free(text);
	restart();
	check(config_boolean("update.auto") == 0 && config_boolean("network.coop_public") == 1, "read back");
	check(!log_has("config.toml:"), "read back without an error");
}

static void test_110(void)
{
	char *original, *broken;
	char backup[1000];
	size_t original_size = 0, broken_size = 0;

	printf("--- 110\n");
	original = read_whole(fixture, &original_size);
	check(original != NULL, "config_110_vita.toml there");
	if (!original)
		return;
	write_whole(config_file, original);
	snprintf(backup, sizeof(backup), "%s.broken", config_file);
	unlink(backup);
	restart();
	check(!strcmp(config_string("game.console_log"), "all"), "game.console_log (the first [game]) kept");
	check(!strcmp(config_string("game.language"), "fr"), "game.language (the second [game]) kept");
	check(config_boolean("network.coop_public") == 1, "network.coop_public kept");
	check(config_boolean("update.auto") == 0, "update.auto kept");
	check(config_integer("debug.telnet_console_port") == 2424, "debug.telnet_console_port (the first [debug]) kept");
	check(log_has("table defined more than once") && log_has("repaired"), "the repair logged, with the error");
	file_reads_whole("1.1's file repaired");
	broken = read_whole(backup, &broken_size);
	check(broken && broken_size == original_size && !memcmp(broken, original, original_size),
		"the file as it was kept as config.toml.broken");
	free(broken);
	restart();
	check(!strcmp(config_string("game.language"), "fr") && config_boolean("update.auto") == 0, "read at the next start");
	check(!log_has("config.toml:"), "the repaired file read without an error");
	check(config_write_boolean("network.coop_public", 0) == 1, "a write to the repaired file");
	file_reads_whole("a write to the repaired file");
	free(original);
}

/* text with the line that starts with start (the first) made line */
static char *with_line(char *text, const char *start, const char *line)
{
	char *at = strstr(text, start);
	char *result;
	size_t length;

	if (!at)
		return text;
	length = strcspn(at, "\n");
	result = malloc(strlen(text) + strlen(line) + 1);
	memcpy(result, text, (size_t)(at - text));
	strcpy(result + (at - text), line);
	strcat(result, at + length);
	free(text);
	return result;
}

static void test_salvage(void)
{
	char *defaults = config_default_text(), *broken;
	char backup[1000];

	printf("--- salvage\n");
	defaults = with_line(defaults, "lobby_name = ", "lobby_name = \"unterminated");
	defaults = with_line(defaults, "max_players = ", "max_players = 8");
	defaults = with_line(defaults, "auto = ", "auto = false");
	defaults = with_line(defaults, "coop_players = ", "coop_players = \"many\"");
	write_whole(config_file, defaults);
	restart();
	check(!strcmp(config_string("network.lobby_name"), ""), "the value that does not read at its default");
	check(config_integer("network.coop_players") == strtol(COOP_PLAYERS_DEFAULT, NULL, 10),
		"a value of the wrong type at its default");
	check(config_integer("network.max_players") == 8, "network.max_players kept");
	check(config_boolean("update.auto") == 0, "update.auto kept");
	check(log_has("does not read") && log_has("written again with the defaults"), "the salvage logged");
	file_reads_whole("written again from the defaults");
	snprintf(backup, sizeof(backup), "%s.broken1", config_file);
	broken = read_whole(backup, NULL);
	check(broken && !strcmp(broken, defaults), "the file as it was kept as config.toml.broken1");
	free(broken);
	free(defaults);
	restart();
	check(config_integer("network.max_players") == 8 && !log_has("config.toml:"), "read at the next start");
}

static void test_gone(void)
{
	printf("--- gone\n");
	restart();
	check(config_boolean("network.coop_public") == 0, "read");
	unlink(config_file);
	check(config_write_boolean("network.coop_public", 1) == 1, "written with the file gone");
	file_reads_whole("the file written again whole");
	restart();
	check(config_boolean("network.coop_public") == 1, "read back");
}

static void test_unreadable(void)
{
	char *before, *after;

	printf("--- unreadable\n");
	if (geteuid() == 0)
	{
		printf("(skipped: root reads anything)\n");
		return;
	}
	before = read_whole(config_file, NULL);
	chmod(config_file, 0);
	restart();
	check(config_boolean("network.coop_public") == 0, "the defaults used");
	check(log_has("cannot read") && log_has("left as it is"), "logged");
	check(config_write_boolean("update.auto", 1) == 0, "a write refused");
	chmod(config_file, 0644);
	after = read_whole(config_file, NULL);
	check(before && after && !strcmp(before, after), "the file left as it is");
	free(before);
	free(after);
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "port_config_test FOLDER config_110_vita.toml\n");
		return 2;
	}
	snprintf(folder, sizeof(folder), "%s", argv[1]);
	snprintf(fixture, sizeof(fixture), "%s", argv[2]);
	snprintf(config_file, sizeof(config_file), "%s/config.toml", folder);
#ifdef HALO_NOT_DESKTOP
	setenv("HALO_DATA_ROOT", folder, 1);
	printf("(the Vita's)\n");
#else
	snprintf(base_path, sizeof(base_path), "%s/", folder);
	printf("(the desktop's)\n");
#endif
	test_defaults();
	test_writes();
	test_hand();
	test_110();
	test_salvage();
	test_gone();
	test_unreadable();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
