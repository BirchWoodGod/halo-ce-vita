/*
FORGE_STORE_TEST.C

The Forge variant store's last good copy (port/linux/src/forge_store.c,
run_forge_store_test.sh): a save keeps the previous good file as <name>.hmv.prev
(only a good one); a load and a list use it when the file fails its checks
(damaged, zeroed, cut); both bad is refused as before; a bad file is kept as
.bad on the next save and never replaces the .prev; a delete takes the .prev.
*/

#include "platform.h"
#include "posix.h"
#include "forge_store.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[512];
static int failures;

const char *platform_save_root(void)
{
	return root;
}

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, host_path_size, "%s", xbox_path);
}

void platform_log(const char *format, ...)
{
	(void)format;
	if (getenv("FS_TEST_LOG")) { va_list a; va_start(a, format); vprintf(format, a); va_end(a); printf("\n"); }
}

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void layout_make(struct forge_file_layout *layout, char const *title)
{
	memset(layout, 0, sizeof(*layout));
	snprintf(layout->map_name, sizeof(layout->map_name), "bloodgulch");
	layout->map_hash = 0x1234ABCD;
	snprintf(layout->title, sizeof(layout->title), "%s", title);
	snprintf(layout->author, sizeof(layout->author), "T");
	layout->paths[0].group_tag = ('s' << 24) | ('c' << 16) | ('e' << 8) | 'n';
	snprintf(layout->paths[0].path, sizeof(layout->paths[0].path), "scenery\\rocks\\boulder\\boulder");
	layout->path_count = 1;
	layout->records[0].path_index = 0;
	layout->records[0].kind = _forge_record_object;
	layout->records[0].team = -1;
	layout->records[0].respawn_seconds = 30;
	layout->records[0].count = 1;
	layout->records[0].flags = FORGE_RECORD_PLACE_AT_START;
	layout->record_count = 1;
	snprintf(layout->date, sizeof(layout->date), "2026-10-25 12:00");
}

static void file_set(char const *suffix, unsigned char const *bytes, int size)
{
	char path[600];
	FILE *file;

	snprintf(path, sizeof(path), "%s/forge/bloodgulch/v.hmv%s", root, suffix);
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(bytes, 1, (size_t)size, file);
		fclose(file);
	}
}

static int file_get(char const *suffix, unsigned char *bytes, int capacity)
{
	char path[600];
	FILE *file;
	int size;

	snprintf(path, sizeof(path), "%s/forge/bloodgulch/v.hmv%s", root, suffix);
	file = fopen(path, "rb");
	if (!file)
		return -1;
	size = (int)fread(bytes, 1, (size_t)capacity, file);
	fclose(file);
	return size;
}

int main(int argc, char **argv)
{
	static struct forge_file_layout first, second, got;
	char why[200];
	static unsigned char good1[FORGE_FILE_MAXIMUM_SIZE], good2[FORGE_FILE_MAXIMUM_SIZE], zeros[1000], now[FORGE_FILE_MAXIMUM_SIZE];
	static struct forge_store_entry entries[8];
	int size1, size2, mode;
	char command[700];

	if (argc != 2)
		return 2;
	snprintf(root, sizeof(root), "%s", argv[1]);
	layout_make(&first, "First");
	layout_make(&second, "Second");
	if (!forge_store_save(&first, "v.hmv", NULL, 0, why, sizeof(why)))
	{
		printf("FAIL: the first save: %s\n", why);
		return 1;
	}
	CHECK(file_get("", good1, sizeof(good1)) > 0);
	size1 = file_get("", good1, sizeof(good1));
	CHECK(file_get(".prev", now, sizeof(now)) < 0);
	CHECK(forge_store_save(&second, "v.hmv", NULL, 0, why, sizeof(why)));
	size2 = file_get("", good2, sizeof(good2));
	CHECK(file_get(".prev", now, sizeof(now)) == size1 && !memcmp(now, good1, (size_t)size1));
	for (mode = 0; mode < 3; mode++)
	{
		if (mode == 0)
		{
			memcpy(now, good2, (size_t)size2);
			now[size2 - 20] ^= 0x55;
			file_set("", now, size2);
		}
		else if (mode == 1)
			file_set("", zeros, sizeof(zeros));
		else
			file_set("", good2, size2 / 2);
		CHECK(forge_store_load("bloodgulch", "v.hmv", &got, why, sizeof(why)));
		CHECK(!strcmp(got.title, "First"));
		CHECK(forge_store_list("bloodgulch", entries, 8) == 1 && !entries[0].damaged && !strcmp(entries[0].title, "First"));
	}
	/* (both bad: refused, damaged in the list) */
	file_set(".prev", zeros, sizeof(zeros));
	CHECK(!forge_store_load("bloodgulch", "v.hmv", &got, why, sizeof(why)));
	CHECK(forge_store_list("bloodgulch", entries, 8) == 1 && entries[0].damaged);
	/* (a save over a bad file: kept as .bad, the .prev not replaced by it) */
	file_set(".prev", good1, size1);
	file_set("", zeros, sizeof(zeros));
	CHECK(forge_store_save(&second, "v.hmv", NULL, 0, why, sizeof(why)));
	CHECK(file_get(".bad", now, sizeof(now)) == (int)sizeof(zeros));
	CHECK(file_get(".prev", now, sizeof(now)) == size1 && !memcmp(now, good1, (size_t)size1));
	CHECK(forge_store_load("bloodgulch", "v.hmv", &got, why, sizeof(why)) && !strcmp(got.title, "Second"));
	CHECK(forge_store_delete("bloodgulch", "v.hmv") && file_get(".prev", now, sizeof(now)) < 0);
	(void)command;
	printf("%d failed\n%s\n", failures, failures ? "FAIL" : "PASS");
	return failures != 0;
}
