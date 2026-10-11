/*
CROSSPLAY_MAPS_TEST.C

Desktop test of the crossplay join's map check (port/linux/game/
crossplay_maps.c, docs/crossplay.md task T3): the map names a PC host's game
settings record may carry (OpenCE's custom_maps\<name>, the Xbox's level
paths, ChupathingyCE's maps_md\ and maps_pc\), hostile ones among them
(paths climbing out of the maps folder, device and resource map names, no
end, too long, characters a file system takes for something else), and the
verdict for every kind of file this machine may have of the map named:
missing, the Xbox's, a Custom Edition copy with the host's checksum or
another, one that cannot load, resource maps missing, PC maps off.

With real maps, the checksum a PC host sends is the one this machine reads:
cache_file_identify's checksum of a Custom Edition map is the raw field at
0x64 of its header (what OpenCE's custom_edition_map_checksum sends), and an
Xbox map identifies as one.
  CROSSPLAY_MAPS_TEST_CE_MAP=<a Custom Edition .map> (Invader's pcgulch.map)
  CROSSPLAY_MAPS_TEST_XBOX_MAP=<an Xbox .map> (bloodgulch.map)
Built by run_crossplay_maps_test.sh as map_share_test.c is.
*/

#include "crossplay_maps.h"
#include "map_share_protocol.h"
#include "cache_file_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition) do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void check_parse(char const *name, short kind, char const *file)
{
	char got[CROSSPLAY_MAP_FILE_BYTES];
	char record[0x80];
	short result;

	/* (as the record carries it: 0x80 bytes) */
	memset(record, 0, sizeof(record));
	strncpy(record, name, sizeof(record) - 1);
	result = crossplay_map_name_parse(record, (long)sizeof(record), got);
	checks++;
	if (result != kind || strcmp(got, file))
	{
		failures++;
		printf("FAIL parse '%s': kind %d file '%s', wanted %d '%s'\n", name, result, got, kind, file);
	}
}

static void test_names(void)
{
	char record[0x80];
	char got[CROSSPLAY_MAP_FILE_BYTES];
	char longest[0x80];

	/* the Xbox's own maps, as every PC build names them */
	check_parse("levels\\test\\bloodgulch\\bloodgulch", _crossplay_map_kind_xbox, "bloodgulch");
	check_parse("levels\\test\\beavercreek\\beavercreek", _crossplay_map_kind_xbox, "beavercreek");
	check_parse("levels\\a10\\a10", _crossplay_map_kind_xbox, "a10");
	check_parse("bloodgulch", _crossplay_map_kind_xbox, "bloodgulch");
	/* a modded Xbox map's level path */
	check_parse("levels\\test\\mygulch\\mygulch", _crossplay_map_kind_xbox, "mygulch");
	/* OpenCE's Custom Edition maps (v22), its prefix in either case */
	check_parse("custom_maps\\pcgulch", _crossplay_map_kind_custom_edition, "pcgulch");
	check_parse("CUSTOM_MAPS\\pcgulch", _crossplay_map_kind_custom_edition, "pcgulch");
	check_parse("custom_maps\\Race-Track-#1", _crossplay_map_kind_custom_edition, "Race-Track-#1");
	check_parse("custom_maps\\[h3] coldsnap", _crossplay_map_kind_custom_edition, "[h3] coldsnap");
	/* Halo PC's versions of the Xbox's levels: Custom Edition maps */
	check_parse("custom_maps\\bloodgulch", _crossplay_map_kind_custom_edition, "bloodgulch");
	check_parse("custom_maps\\a30", _crossplay_map_kind_custom_edition, "a30");
	/* ChupathingyCE's families */
	check_parse("maps_md\\hugeass.md", _crossplay_map_kind_halomd, "hugeass");
	check_parse("maps_pc\\bloodgulch.pc", _crossplay_map_kind_halo_pc_retail, "bloodgulch");
	check_parse("MAPS_PC\\bloodgulch.PC", _crossplay_map_kind_halo_pc_retail, "bloodgulch");
	check_parse("maps_md\\hugeass", _crossplay_map_kind_invalid, "");
	check_parse("maps_pc\\.pc", _crossplay_map_kind_invalid, "");
	check_parse("maps_md\\a\\b.md", _crossplay_map_kind_invalid, "");
	/* 25 characters, the map list's; 26 refused */
	check_parse("custom_maps\\abcdefghijklmnopqrstuvwxy", _crossplay_map_kind_custom_edition, "abcdefghijklmnopqrstuvwxy");
	check_parse("custom_maps\\abcdefghijklmnopqrstuvwxyz", _crossplay_map_kind_invalid, "");
	check_parse("levels\\test\\x\\abcdefghijklmnopqrstuvwxyz", _crossplay_map_kind_invalid, "");

	/* hostile names: none looks at a file */
	check_parse("", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\..\\..\\ux0_data\\x", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\..", _crossplay_map_kind_invalid, "");
	check_parse("levels\\..\\..\\x", _crossplay_map_kind_invalid, "");
	check_parse("..\\x", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\sub\\x", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps/x", _crossplay_map_kind_invalid, "");
	check_parse("ux0:data\\x", _crossplay_map_kind_invalid, "");
	check_parse("c:\\maps\\x", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\con", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\NUL.foo", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\lpt1", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\bitmaps", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\Sounds", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\loc", _crossplay_map_kind_invalid, "");
	check_parse("levels\\ui\\ui", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\100%n", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\a,b", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\a*b", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\a\"b", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\name.", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\.hidden", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\ lead", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\trail ", _crossplay_map_kind_invalid, "");
	check_parse("levels\\test\\x\\", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\a\tb", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\caf\xe9", _crossplay_map_kind_invalid, "");
	check_parse("custom_maps\\a\x7f", _crossplay_map_kind_invalid, "");
	/* longer than a game keeps (63) */
	memset(longest, 0, sizeof(longest));
	memset(longest, 'a', 64);
	memcpy(longest, "levels\\test\\", 12);
	memcpy(longest + 50, "\\bloodgulch", 11);
	check_parse(longest, _crossplay_map_kind_invalid, "");
	longest[63] = 0;
	memcpy(longest + 52, "\\bloodgulch", 11);
	check_parse(longest, _crossplay_map_kind_xbox, "bloodgulch");
	/* no end in the record's bytes */
	memset(record, 'a', sizeof(record));
	CHECK(crossplay_map_name_parse(record, (long)sizeof(record), got) == _crossplay_map_kind_invalid && !got[0]);
	memcpy(record, "custom_maps\\pcgulch", 19);
	CHECK(crossplay_map_name_parse(record, 19, got) == _crossplay_map_kind_invalid);
	record[19] = 0;
	CHECK(crossplay_map_name_parse(record, 20, got) == _crossplay_map_kind_custom_edition && !strcmp(got, "pcgulch"));
	CHECK(crossplay_map_name_parse(record, 0, got) == _crossplay_map_kind_invalid);
	CHECK(crossplay_map_name_parse(NULL, 20, got) == _crossplay_map_kind_invalid);

	CHECK(crossplay_map_file_is_stock("bloodgulch") && crossplay_map_file_is_stock("BloodGulch") &&
		crossplay_map_file_is_stock("a10") && !crossplay_map_file_is_stock("pcgulch") && !crossplay_map_file_is_stock(NULL));
}

static void test_printable(void)
{
	char record[0x80];
	char shown[CROSSPLAY_MAP_FILE_BYTES];

	memset(record, 0, sizeof(record));
	strcpy(record, "custom_maps\\pcgulch");
	crossplay_map_name_printable(record, (long)sizeof(record), shown);
	CHECK(!strcmp(shown, "pcgulch"));
	/* a refused name: its first 25 characters, safe ones */
	strcpy(record, "custom_maps\\..\\%n%s\x1b[2J");
	crossplay_map_name_printable(record, (long)sizeof(record), shown);
	CHECK(strlen(shown) <= CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH && !strchr(shown, '%') && !strchr(shown, '\\') &&
		!strchr(shown, '\x1b'));
	printf("a refused name shown as '%s'\n", shown);
	memset(record, 'z', sizeof(record));
	crossplay_map_name_printable(record, (long)sizeof(record), shown);
	CHECK(strlen(shown) == CROSSPLAY_MAP_MAXIMUM_FILE_LENGTH);
	crossplay_map_name_printable(NULL, 0, shown);
	CHECK(!shown[0]);
}

static struct crossplay_local_map local_map(short file, unsigned long checksum)
{
	struct crossplay_local_map local;

	memset(&local, 0, sizeof(local));
	local.file = file;
	local.checksum = checksum;
	local.pc_maps_on = 1;
	local.xbox_multiplayer = file == _crossplay_map_file_xbox;
	return local;
}

#define VERDICT(kind, file, version, local, want) do { struct crossplay_local_map l_ = (local); short v_ = \
	crossplay_map_verdict((kind), (file), (version), &l_); checks++; if (v_ != (want)) { failures++; \
	printf("FAIL %s:%d: verdict %s, wanted %s\n", __FILE__, __LINE__, crossplay_map_verdict_describe(v_), \
	crossplay_map_verdict_describe(want)); } } while (0)

static void test_verdicts(void)
{
	struct crossplay_local_map local;
	enum { HOST = 0xFB74B9F4UL };

	/* the Xbox's levels: never compared (version 0), this machine's file the
	Xbox map, whole */
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_playable);
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 7, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_identity_unknown);
	local = local_map(_crossplay_map_file_absent, 0);
	local.stock_problem = 1;
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local, _crossplay_map_missing);
	/* (Halo PC's Blood Gulch in the maps folder, with PC maps on: the Xbox
	level's file is not the Xbox map) */
	local = local_map(_crossplay_map_file_custom_edition, HOST);
	local.stock_problem = 1;
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local, _crossplay_map_stock_unloadable);
	local.stock_problem = 0;
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local, _crossplay_map_xbox_is_custom_edition_here);
	local = local_map(_crossplay_map_file_unusable, 0);
	local.stock_problem = 1;
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local, _crossplay_map_stock_unloadable);
	/* (PC maps off does not matter to an Xbox map) */
	local = local_map(_crossplay_map_file_xbox, 0);
	local.pc_maps_on = 0;
	VERDICT(_crossplay_map_kind_xbox, "bloodgulch", 0, local, _crossplay_map_playable);
	/* another Xbox map by its level path */
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 0, local_map(_crossplay_map_file_absent, 0), _crossplay_map_missing);
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 0, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_playable);
	local = local_map(_crossplay_map_file_xbox, 0);
	local.xbox_multiplayer = 0;
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 0, local, _crossplay_map_not_multiplayer);
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 0, local_map(_crossplay_map_file_custom_edition, HOST),
		_crossplay_map_xbox_is_custom_edition_here);
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 0, local_map(_crossplay_map_file_unusable, 0), _crossplay_map_unusable);
	VERDICT(_crossplay_map_kind_xbox, "mygulch", 1, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_identity_unknown);

	/* Custom Edition maps: the host's copy, by its header's checksum */
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local_map(_crossplay_map_file_custom_edition, HOST),
		_crossplay_map_playable);
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local_map(_crossplay_map_file_custom_edition, HOST + 1),
		_crossplay_map_custom_edition_different);
	/* (0: the host could not read its own; nothing compared, as OpenCE) */
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", 0, local_map(_crossplay_map_file_custom_edition, HOST),
		_crossplay_map_playable);
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local_map(_crossplay_map_file_absent, 0),
		_crossplay_map_missing);
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local_map(_crossplay_map_file_xbox, 0),
		_crossplay_map_custom_edition_is_xbox_here);
	VERDICT(_crossplay_map_kind_custom_edition, "bloodgulch", HOST, local_map(_crossplay_map_file_xbox, 0),
		_crossplay_map_custom_edition_is_xbox_here);
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local_map(_crossplay_map_file_unusable, 0),
		_crossplay_map_unusable);
	local = local_map(_crossplay_map_file_custom_edition, HOST);
	local.resource_maps_missing = 1;
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local, _crossplay_map_needs_resource_maps);
	local = local_map(_crossplay_map_file_custom_edition, HOST);
	local.pc_maps_on = 0;
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST, local, _crossplay_map_needs_pc_maps);
	/* (another copy is said first: turning PC maps on would not help) */
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", HOST + 1, local, _crossplay_map_custom_edition_different);
	/* (the checksum's 32 bits: a record's long) */
	VERDICT(_crossplay_map_kind_custom_edition, "pcgulch", (unsigned long)(long)(int)0xFB74B9F4,
		local_map(_crossplay_map_file_custom_edition, HOST), _crossplay_map_playable);

	/* what no build of this game plays, whatever is in the maps folder */
	VERDICT(_crossplay_map_kind_halomd, "hugeass", HOST, local_map(_crossplay_map_file_custom_edition, HOST),
		_crossplay_map_halomd);
	VERDICT(_crossplay_map_kind_halo_pc_retail, "bloodgulch", HOST, local_map(_crossplay_map_file_xbox, 0),
		_crossplay_map_halo_pc_retail);
	VERDICT(_crossplay_map_kind_invalid, "", 0, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_name_refused);
	VERDICT(99, "x", 0, local_map(_crossplay_map_file_xbox, 0), _crossplay_map_name_refused);

	CHECK(!strcmp(crossplay_map_verdict_describe(-1), "unknown") &&
		!strcmp(crossplay_map_verdict_describe(NUMBER_OF_CROSSPLAY_MAP_VERDICTS), "unknown"));
}

static int stdio_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	FILE *file = context;

	return fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, file) == size;
}

/* a real map: what this machine reads of it, as custom_edition_cache.c's
custom_edition_cache_crossplay_facts does */
static void test_real_map(char const *path, int custom_edition)
{
	struct cache_file_source source;
	struct cache_file_identity identity;
	enum cache_file_status status;
	unsigned char header[0x800];
	FILE *file;
	long size;

	if (!path || !path[0])
	{
		printf("(no %s map given: its real checks skipped)\n", custom_edition ? "Custom Edition" : "Xbox");
		return;
	}
	file = fopen(path, "rb");
	CHECK(file != NULL);
	if (!file)
		return;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	CHECK(fseek(file, 0, SEEK_SET) == 0 && fread(header, 1, sizeof(header), file) == sizeof(header));
	source.context = file;
	source.read = stdio_read;
	source.size = (uint32_t)size;
	memset(&identity, 0, sizeof(identity));
	status = cache_file_identify(&source, &identity);
	CHECK(status == _cache_file_status_ok);
	if (custom_edition)
	{
		uint32_t raw = (uint32_t)header[0x64] | (uint32_t)header[0x65] << 8 | (uint32_t)header[0x66] << 16 |
			(uint32_t)header[0x67] << 24;

		CHECK(identity.format == _cache_file_format_custom_edition_cache);
		/* (what OpenCE's host sends in map.version) */
		CHECK(identity.checksum == raw);
		printf("%s: a Custom Edition map, checksum 0x%08lX (the header's raw field 0x%08lX)\n", path,
			(unsigned long)identity.checksum, (unsigned long)raw);
	}
	else
	{
		CHECK(identity.format == _cache_file_format_xbox_cache);
		printf("%s: an Xbox map (build %s)\n", path, identity.build);
	}
	fclose(file);
}

int main(void)
{
	test_names();
	test_printable();
	test_verdicts();
	test_real_map(getenv("CROSSPLAY_MAPS_TEST_CE_MAP"), 1);
	test_real_map(getenv("CROSSPLAY_MAPS_TEST_XBOX_MAP"), 0);
	printf("%d checks, %d failed\n", checks, failures);
	printf(failures ? "FAIL crossplay_maps_test\n" : "PASS crossplay_maps_test\n");
	return failures ? 1 : 0;
}
