/*
MENU_WORDS.C

Menu style Halo CE's main menu rows (menus/classic/main.xml: FORGE, FRIENDS,
FILE SHARE, MOD MANAGER) as words in the very letters of the game's own
CAMPAIGN, MULTIPLAYER, SETTINGS and GAME DEMOS - the player's ui.map's word
pictures (ui\shell\main_menu\menu_*), read and cut into letters on the
player's machine as the menus load, never shipped. Each of those is two
256 x 64 pictures, the word not focused and focused (frames 0 and 1: the
game's column list shows the one by focus); each letter is the columns
between the gaps of its word, half way to its neighbours (the same columns
in both; letters that touch, CA, LT, LAYE..., split at their faintest
column), set one after the other the originals' mean gap apart. F and H,
which those words have not, are made of the font's own strokes, copied
pixel for pixel: F is E down to its middle arm, E's straight stem row on
to the bottom, I's square end and side glow, the top arm's glow under the
middle arm; H is two I stems N's width apart and E's middle arm between
them (letter_f, letter_h). The words are kept in the game's data folder
(t:\menu_words_<row>_<frame>.rgba, with the pictures' checksum: made again
when the map's pictures change).

English only (lang_current "en"): another language, a word with a letter
none of those words gives, or a map without the pictures - a Custom
Edition ui.map, a damaged one; HALO_MENU_WORDS=0 - and the row is the
port's own picture (menus/classic/<lang>/, tools/classic_menu_art.py), as
before. menu_tags.c
asks (menu_words_frame) as it loads a frame of classic/{lang}/main_*.
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"

#include "../src/lang.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);

/* ---------- constants */

#define WORD_WIDTH 256
#define WORD_HEIGHT 64
#define WORD_BYTES (WORD_WIDTH * WORD_HEIGHT * 4)
#define FRAMES 2
#define MAXIMUM_LETTERS 16
/* (DXT3: 16 bytes a block of 4 x 4) */
#define BITMAP_FORMAT_DXT3 15
#define BITMAP_CACHED_BIT 7
#define WORDS_FILE_SIGNATURE 0x3844574DUL /* 'MWD8': made again when the way of making them changes */

/* the game's words and the letters of each, in their pictures */
static struct
{
	char const *tag;
	char const *letters;
} const sources[] =
{
	{ "ui\\shell\\main_menu\\menu_load_campaign", "CAMPAIGN" },
	{ "ui\\shell\\main_menu\\menu_multiplayer", "MULTIPLAYER" },
	{ "ui\\shell\\main_menu\\menu_settings", "SETTINGS" },
	{ "ui\\shell\\main_menu\\menu_game_demos", "GAMEDEMOS" },
};
#define SOURCE_COUNT ((long)NUMBEROF(sources))

/* the rows: the file name's and their words */
static struct
{
	char const *name;
	char const *word;
} const rows[] =
{
	{ "forge", "FORGE" },
	{ "friends", "FRIENDS" },
	{ "fileshare", "FILE SHARE" },
	{ "mods", "MOD MANAGER" },
};

/* ---------- types */

/* a letter: its columns in its word's pictures, and its ink's */
struct letter
{
	char character;
	short source;
	short x0, x1;
	short ink0, ink1;
	/* cut out of a run of letters that touch */
	boolean split;
};

/* ---------- globals */

static struct
{
	/* tried (1 read, -1 not to be had), as ui.map last loaded */
	int state;
	long generation;
	/* each word's two pictures, 8-bit BGRA */
	byte pixels[SOURCE_COUNT][FRAMES][WORD_BYTES];
	/* the not focused picture's strongest alpha: the ink's threshold a third of it */
	byte ink_alpha;
	struct letter letters[SOURCE_COUNT * MAXIMUM_LETTERS];
	short letter_count;
	/* the space between two letters' ink, and a space's, as the originals have them */
	short gap;
	short space;
	unsigned long checksum;
} menu_words;

/* ---------- private code */

static unsigned long checksum_add(unsigned long crc, byte const *data, long size)
{
	long index;
	int bit;

	crc = ~crc;
	for (index = 0; index < size; index++)
	{
		crc ^= data[index];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
	}
	return ~crc;
}

static void rgb565(unsigned short value, byte *bgr)
{
	bgr[2] = (byte)(((value >> 11) & 0x1F) * 255 / 31);
	bgr[1] = (byte)(((value >> 5) & 0x3F) * 255 / 63);
	bgr[0] = (byte)((value & 0x1F) * 255 / 31);
}

/* DXT3 (explicit 4-bit alpha) to BGRA */
static void dxt3_decode(byte const *data, long width, long height, byte *pixels)
{
	long bx, by;

	for (by = 0; by < height / 4; by++)
	{
		for (bx = 0; bx < width / 4; bx++)
		{
			byte const *block = data + (by * (width / 4) + bx) * 16;
			byte colors[4][3];
			unsigned short c0 = (unsigned short)(block[8] | (block[9] << 8));
			unsigned short c1 = (unsigned short)(block[10] | (block[11] << 8));
			unsigned long indices = block[12] | (block[13] << 8) | ((unsigned long)block[14] << 16) |
				((unsigned long)block[15] << 24);
			int x, y, channel;

			rgb565(c0, colors[0]);
			rgb565(c1, colors[1]);
			for (channel = 0; channel < 3; channel++)
			{
				colors[2][channel] = (byte)((2 * colors[0][channel] + colors[1][channel] + 1) / 3);
				colors[3][channel] = (byte)((colors[0][channel] + 2 * colors[1][channel] + 1) / 3);
			}
			for (y = 0; y < 4; y++)
			{
				for (x = 0; x < 4; x++)
				{
					byte *out = pixels + ((by * 4 + y) * width + bx * 4 + x) * 4;
					int nibble = (block[y * 2 + x / 2] >> ((x & 1) * 4)) & 0x0F;
					int index = (int)((indices >> ((y * 4 + x) * 2)) & 3);

					out[0] = colors[index][0];
					out[1] = colors[index][1];
					out[2] = colors[index][2];
					out[3] = (byte)(nibble * 17);
				}
			}
		}
	}
}

/* a word's two pictures read from the loaded map (ui.map): FALSE if they are
not there as the game's own are (two 256 x 64 DXT3 pictures) */
static boolean source_read(long source)
{
	long tag_index = tag_loaded('bitm', sources[source].tag);
	struct bitmap_group *group;
	long frame;

	if (tag_index == NONE || !(group = tag_get('bitm', tag_index)) || group->bitmaps.count != FRAMES)
		return FALSE;
	for (frame = 0; frame < FRAMES; frame++)
	{
		struct bitmap_data bitmap = ((struct bitmap_data *)group->bitmaps.address)[frame];
		long size = WORD_WIDTH * WORD_HEIGHT;
		boolean done = FALSE;
		byte *data;
		long offset;

		if (bitmap.width != WORD_WIDTH || bitmap.height != WORD_HEIGHT || bitmap.format != BITMAP_FORMAT_DXT3 ||
			bitmap.depth != 1)
		{
			platform_log("menu words: %s is %dx%d format %d, not the game's own", sources[source].tag, bitmap.width,
				bitmap.height, bitmap.format);
			return FALSE;
		}
		/* (its offset in the map: the group's pixel data's added once it is
		a texture of the cache's, xbox_texture_cache.c) */
		offset = bitmap.pixels_offset + (TEST_FLAG(bitmap.flags, BITMAP_CACHED_BIT) ? 0 : group->pixel_data.file_offset);
		data = malloc(size);
		if (!data)
			return FALSE;
		/* (the cache file thread's read, the most urgent, waited for: 16 KB) */
		{
			unsigned long long started = vita_host_time_us();

			if (cache_file_read(tag_index, offset, size, data, &done, TRUE) != NONE)
			{
				while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE) && vita_host_time_us() - started < 2000000ULL)
					SwitchToThread();
			}
		}
		if (__atomic_load_n(&done, __ATOMIC_ACQUIRE) != TRUE)
		{
			free(data);
			platform_log("menu words: %s could not be read", sources[source].tag);
			return FALSE;
		}
		dxt3_decode(data, WORD_WIDTH, WORD_HEIGHT, menu_words.pixels[source][frame]);
		free(data);
	}
	return TRUE;
}

static byte alpha_at(long source, long x, long y)
{
	return menu_words.pixels[source][0][(y * WORD_WIDTH + x) * 4 + 3];
}

static boolean inked(long source, long x, long y)
{
	return alpha_at(source, x, y) * 3 > menu_words.ink_alpha;
}

static boolean column_inked(long source, long x)
{
	long y;

	for (y = 0; y < WORD_HEIGHT; y++)
	{
		if (inked(source, x, y))
			return TRUE;
	}
	return FALSE;
}

/* the letters' widths in the game's words, about (for the runs of letters
that touch: CA, PA, LT, LAYE, ETT), in pixels */
static long nominal_width(char character)
{
	static char const letters[] = "ACDEGILMNOPRSTUY";
	static short const widths[] = { 20, 18, 18, 17, 18, 4, 15, 23, 17, 19, 18, 18, 19, 17, 17, 19 };
	char const *at = strchr(letters, character);

	return at && *at ? widths[at - letters] : 17;
}

static long column_weight(long source, long x)
{
	long y, sum = 0;

	for (y = 0; y < WORD_HEIGHT; y++)
		sum += alpha_at(source, x, y);
	return sum;
}

/* a word's letters: its inked runs of columns, a run as wide as several
letters split between them at the faintest column near where their widths
put the boundary; each letter given the columns half way to its
neighbours' ink (split: marked, the letters of a run of one preferred) */
static boolean letters_cut(long source)
{
	char const *word = sources[source].letters;
	long count = (long)strlen(word);
	short runs[MAXIMUM_LETTERS * 2][2];
	short inks[MAXIMUM_LETTERS][2];
	boolean split[MAXIMUM_LETTERS];
	short per_run[MAXIMUM_LETTERS * 2];
	long run_count = 0, x, index, run, letter = 0;

	for (x = 0; x < WORD_WIDTH; x++)
	{
		if (!column_inked(source, x))
			continue;
		if (run_count && runs[run_count - 1][1] == x - 1)
			runs[run_count - 1][1] = (short)x;
		else if (run_count < (long)NUMBEROF(runs))
		{
			runs[run_count][0] = runs[run_count][1] = (short)x;
			run_count++;
		}
	}
	if (run_count > count || count > MAXIMUM_LETTERS)
	{
		platform_log("menu words: %s has %ld inked runs for %ld letters", sources[source].tag, run_count, count);
		return FALSE;
	}
	/* (how many letters each run holds: the counts whose letters' widths
	come nearest the runs' all told, every run one letter at least) */
	{
		long cost[MAXIMUM_LETTERS * 2 + 1][MAXIMUM_LETTERS + 1];
		short choice[MAXIMUM_LETTERS * 2 + 1][MAXIMUM_LETTERS + 1];
		long r, l, k;

		for (r = 0; r <= run_count; r++)
			for (l = 0; l <= count; l++)
				cost[r][l] = 0x3FFFFFFF;
		cost[0][0] = 0;
		for (r = 1; r <= run_count; r++)
		{
			long width = runs[r - 1][1] - runs[r - 1][0] + 1;

			for (l = 1; l <= count; l++)
			{
				long sum = 0;

				for (k = 1; k <= l; k++)
				{
					long error;

					sum += nominal_width(word[l - k]) + (k > 1 ? 2 : 0);
					if (cost[r - 1][l - k] >= 0x3FFFFFFF)
						continue;
					error = cost[r - 1][l - k] + labs(sum - width);
					if (error < cost[r][l])
					{
						cost[r][l] = error;
						choice[r][l] = (short)k;
					}
				}
			}
		}
		if (cost[run_count][count] >= 0x3FFFFFFF)
			return FALSE;
		for (r = run_count, l = count; r > 0; r--)
		{
			per_run[r - 1] = choice[r][l];
			l -= choice[r][l];
		}
	}
	for (run = 0; run < run_count; run++)
	{
		long width = runs[run][1] - runs[run][0] + 1;
		long best = per_run[run], k, sum, position;

		/* (its letters' widths scaled to the run's, each boundary at the
		faintest column within 4 of where they put it) */
		for (k = 0, sum = 0; k < best; k++)
			sum += nominal_width(word[letter + k]);
		position = runs[run][0];
		for (k = 0; k < best; k++)
		{
			long end = runs[run][1];

			if (k < best - 1)
			{
				long guess = position + nominal_width(word[letter + k]) * width / sum - 1;
				long faintest = guess, column;

				for (column = MAX(position + 2, guess - 4); column <= MIN(runs[run][1] - 2, guess + 4); column++)
				{
					if (column_weight(source, column) < column_weight(source, faintest))
						faintest = column;
				}
				end = faintest;
			}
			inks[letter + k][0] = (short)position;
			inks[letter + k][1] = (short)end;
			split[letter + k] = best > 1;
			position = end + 1;
		}
		letter += best;
	}
	if (letter != count)
		return FALSE;
	for (index = 0; index < count && menu_words.letter_count < (short)NUMBEROF(menu_words.letters); index++)
	{
		struct letter *out = &menu_words.letters[menu_words.letter_count++];

		out->character = word[index];
		out->source = (short)source;
		out->split = split[index];
		out->ink0 = inks[index][0];
		out->ink1 = inks[index][1];
		/* (half way to the neighbours' ink; a split letter's cell its ink) */
		out->x0 = (short)(index ? (split[index] && inks[index - 1][1] + 1 == inks[index][0] ? inks[index][0] :
			(inks[index - 1][1] + inks[index][0] + 1) / 2) : MAX(0, inks[index][0] - 8));
		out->x1 = (short)(index < count - 1 ? (split[index] && inks[index][1] + 1 == inks[index + 1][0] ? inks[index][1] :
			(inks[index][1] + inks[index + 1][0]) / 2) : MIN(WORD_WIDTH - 1, inks[index][1] + 8));
	}
	return TRUE;
}

/* (debug) HALO_MENU_WORDS_DUMP=1: the pictures read, as PAM, in the data
folder (t:\menu_words_source<n>_<frame>.pam) */
static void sources_dump(void)
{
	long source, frame, index;

	if (!getenv("HALO_MENU_WORDS_DUMP") || !atoi(getenv("HALO_MENU_WORDS_DUMP")))
		return;
	for (source = 0; source < SOURCE_COUNT; source++)
	{
		for (frame = 0; frame < FRAMES; frame++)
		{
			char path[64];
			FILE *file;

			snprintf(path, sizeof(path), "t:\\menu_words_source%ld_%ld.pam", source, frame);
			if ((file = fopen(path, "wb")) == NULL)
				continue;
			fprintf(file, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", WORD_WIDTH,
				WORD_HEIGHT);
			for (index = 0; index < WORD_WIDTH * WORD_HEIGHT; index++)
			{
				byte const *pixel = &menu_words.pixels[source][frame][index * 4];
				byte rgba[4];

				rgba[0] = pixel[2];
				rgba[1] = pixel[1];
				rgba[2] = pixel[0];
				rgba[3] = pixel[3];
				fwrite(rgba, 1, 4, file);
			}
			fclose(file);
		}
	}
}

/* the sources read and cut, once a ui.map load */
static boolean menu_words_ready(void)
{
	extern long menu_tags_generation(void);
	long generation = menu_tags_generation();
	long source, index, gaps = 0, gap_total = 0;

	if (menu_words.state && menu_words.generation == generation)
		return menu_words.state > 0;
	memset(&menu_words, 0, sizeof(menu_words));
	menu_words.generation = generation;
	menu_words.state = -1;
	for (source = 0; source < SOURCE_COUNT; source++)
	{
		if (!source_read(source))
			return FALSE;
		menu_words.checksum = checksum_add(menu_words.checksum, menu_words.pixels[source][0], FRAMES * WORD_BYTES);
		for (index = 0; index < WORD_WIDTH * WORD_HEIGHT; index++)
		{
			if (menu_words.pixels[source][0][index * 4 + 3] > menu_words.ink_alpha)
				menu_words.ink_alpha = menu_words.pixels[source][0][index * 4 + 3];
		}
	}
	sources_dump();
	for (source = 0; source < SOURCE_COUNT; source++)
	{
		if (!letters_cut(source))
			return FALSE;
	}
	/* (the space between letters: the originals' mean; GAME DEMOS's word
	gap a space's) */
	for (index = 1; index < menu_words.letter_count; index++)
	{
		struct letter const *before = &menu_words.letters[index - 1];
		struct letter const *letter = &menu_words.letters[index];
		long gap = letter->ink0 - before->ink1 - 1;

		if (letter->source != before->source || letter->split || before->split)
			continue;
		if (letter->source == 3 && before->character == 'E' && letter->character == 'D')
		{
			menu_words.space = (short)gap;
			continue;
		}
		gap_total += gap;
		gaps++;
	}
	menu_words.gap = (short)(gaps ? (gap_total + gaps / 2) / gaps : 2);
	if (!menu_words.space)
		menu_words.space = (short)(menu_words.gap * 4);
	menu_words.state = 1;
	platform_log("menu words: %d letters from the game's main menu words, %d pixels between letters, %d a space",
		menu_words.letter_count, menu_words.gap, menu_words.space);
	return TRUE;
}

/* a letter of the pictures: one that touched none preferred */
static struct letter const *letter_find(char character)
{
	struct letter const *found = NULL;
	long index;

	for (index = 0; index < menu_words.letter_count; index++)
	{
		if (menu_words.letters[index].character != character)
			continue;
		if (!menu_words.letters[index].split)
			return &menu_words.letters[index];
		if (!found)
			found = &menu_words.letters[index];
	}
	return found;
}

/* a piece of a picture (columns x0 to x1, rows y0 to y1) drawn at at_x of a
word's picture, keeping the stronger of what is there and it (letters'
glows meeting) */
static void blit(byte *word, long at_x, long source, long frame, long x0, long x1, long y0, long y1)
{
	long x, y;

	for (y = MAX(0, y0); y <= MIN(WORD_HEIGHT - 1, y1); y++)
	{
		for (x = x0; x <= x1; x++)
		{
			long to = at_x + (x - x0);
			byte const *from = &menu_words.pixels[source][frame][(y * WORD_WIDTH + x) * 4];
			byte *out;

			if (to < 0 || to >= WORD_WIDTH)
				continue;
			out = &word[(y * WORD_WIDTH + to) * 4];
			if (from[3] > out[3])
				memcpy(out, from, 4);
		}
	}
}

/* ---- F and H, of the font's own strokes

E (GAME DEMOS's, cut clean) gives: its stem (the inked run from the left of
a row between its middle and bottom arms: straight there, its own soft
edge), its three arms' rows, and the glow over its top arm (the rows above
it: a horizontal stroke's glow alone, nothing else near). I gives a stem's
square ends and the glow at its side and ends. N gives a letter's width.
Each piece is copied pixel for pixel, never scaled: the strokes are the
font's own. Glow meeting glow keeps the stronger (blit, put) */

#define STROKE_GLOW 5

struct strokes
{
	struct letter const *e, *i, *n;
	/* E's stem's ink columns and a straight row of it */
	long stem0, stem1, stem_row;
	/* E's arms' rows (top, middle, bottom: first and last) and the middle
	arm's last inked column */
	long arm[3][2];
	long middle_end;
	/* I's stem's ink columns */
	long i_stem0, i_stem1;
};

static boolean strokes_find(struct strokes *strokes)
{
	struct letter const *e = letter_find('E');
	long y, x, runs = 0;
	boolean was = FALSE;

	memset(strokes, 0, sizeof(*strokes));
	strokes->e = e;
	strokes->i = letter_find('I');
	strokes->n = letter_find('N');
	if (!e || !strokes->i || !strokes->n || e->split || strokes->i->split)
		return FALSE;
	strokes->i_stem0 = strokes->i->ink0;
	strokes->i_stem1 = strokes->i->ink1;
	/* (the arms: the runs of rows inked right of the stem's width from E's
	ink: an I's width and two) */
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		boolean row = FALSE;

		for (x = e->ink0 + (strokes->i_stem1 - strokes->i_stem0 + 1) + 2; x <= e->ink1; x++)
		{
			if (inked(e->source, x, y))
				row = TRUE;
		}
		if (row && !was && runs < 3)
			strokes->arm[runs][0] = y;
		if (!row && was && runs < 3)
			strokes->arm[runs++][1] = y - 1;
		was = row;
	}
	if (runs != 3)
		return FALSE;
	/* (the stem: a row half way between the middle and bottom arms) */
	/* (the row of the gap between the middle and bottom arms whose stem has
	the least ink left of its run - E's stem bows a little: its straightest) */
	{
		long best = 0x7FFFFFFF, row;

		strokes->stem_row = (strokes->arm[1][1] + strokes->arm[2][0]) / 2;
		for (row = strokes->arm[1][1] + 2; row <= strokes->arm[2][0] - 2; row++)
		{
			long first, left = 0;

			for (first = e->x0; first <= e->x1 && !inked(e->source, first, row); first++)
				left += alpha_at(e->source, first, row);
			if (first <= e->x1 && alpha_at(e->source, first - 1, row) < best)
			{
				best = alpha_at(e->source, first - 1, row);
				strokes->stem_row = row;
			}
		}
	}
	for (x = e->x0; x <= e->x1 && !inked(e->source, x, strokes->stem_row); x++)
		;
	strokes->stem0 = x;
	for (; x <= e->x1 && inked(e->source, x, strokes->stem_row); x++)
		;
	strokes->stem1 = x - 1;
	for (x = e->ink1; x > strokes->stem1 && !inked(e->source, x, (strokes->arm[1][0] + strokes->arm[1][1]) / 2); x--)
		;
	strokes->middle_end = x;
	return strokes->stem1 >= strokes->stem0 && strokes->middle_end > strokes->stem1 + 2;
}

static byte const *pixel_at(long source, long frame, long x, long y)
{
	static byte const none[4] = { 0, 0, 0, 0 };

	if (x < 0 || x >= WORD_WIDTH || y < 0 || y >= WORD_HEIGHT)
		return none;
	return &menu_words.pixels[source][frame][(y * WORD_WIDTH + x) * 4];
}

/* a pixel of a letter's cell, none outside it (a neighbour's ink there) */
static byte const *cell_pixel(struct letter const *letter, long frame, long x, long y)
{
	static byte const none[4] = { 0, 0, 0, 0 };

	if (x < letter->x0 || x > letter->x1)
		return none;
	return pixel_at(letter->source, frame, x, y);
}

/* one pixel put into a word's picture, the stronger kept */
static void put(byte *word, long x, long y, byte const *from)
{
	byte *out;

	if (x < 0 || x >= WORD_WIDTH || y < 0 || y >= WORD_HEIGHT)
		return;
	out = &word[(y * WORD_WIDTH + x) * 4];
	if (from[3] > out[3])
		memcpy(out, from, 4);
}

/* E's top arm's glow above it, k rows up from the arm, at a column (the
glow of a horizontal stroke alone) */
static byte const *bar_glow(struct strokes const *strokes, long frame, long x, long k)
{
	return cell_pixel(strokes->e, frame, x, strokes->arm[0][0] - k);
}

/* F: E above its middle arm's last row (its rounded top, both arms as they
are); below it E's straight stem row again to the bottom, I's square end
under it, I's glow at the stem's right, the top arm's glow mirrored under
the middle arm. at_x: the cell's place (E's cell's columns) */
static void letter_f(struct strokes const *strokes, long frame, byte *word, long at_x)
{
	struct letter const *e = strokes->e;
	long bottom = strokes->arm[2][1];
	long y, x, k;
	/* (I's columns put on E's: its stem's left edge on E's stem's - both
	stems' full columns alike, E's with a soft one at its right) */
	long i_shift = strokes->stem0 - strokes->i_stem0;

	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = e->x0; x <= e->x1; x++)
		{
			long to = at_x + (x - e->x0);

			if (y <= strokes->arm[1][1])
				put(word, to, y, cell_pixel(e, frame, x, y));
			else if (y <= bottom)
			{
				/* (the stem and its soft right edge: E's straight row; past
				them, I's side at that row) */
				if (x <= strokes->stem1 + 1)
					put(word, to, y, cell_pixel(e, frame, x, strokes->stem_row));
				else
					put(word, to, y, cell_pixel(strokes->i, frame, x - i_shift, y));
			}
			else
			{
				/* (under the stem: I's square end and its glow) */
				put(word, to, y, cell_pixel(strokes->i, frame, x - i_shift, y));
			}
		}
	}
	/* (under the middle arm: the glow a bar has, mirrored from over the top
	arm, along the arm's columns) */
	for (k = 1; k <= STROKE_GLOW; k++)
	{
		for (x = strokes->stem1 + 1; x <= e->x1; x++)
			put(word, at_x + (x - e->x0), strokes->arm[1][1] + k, bar_glow(strokes, frame, x, k));
	}
}

/* H: two of I's stems, N's width apart (ink to ink), E's middle arm between
them at its rows - one column of its middle, the same in every column, its
glow over and under it the top arm's - square ends and glow all I's. Its
cell: I's left margin, N's ink's width, I's right margin */
static long letter_h(struct strokes const *strokes, long frame, byte *word, long at_x)
{
	struct letter const *i = strokes->i;
	long width = strokes->n->ink1 - strokes->n->ink0 + 1;
	long left = i->ink0 - i->x0;
	long cell = left + width + (i->x1 - i->ink1);
	long bar_column = (strokes->stem1 + strokes->middle_end) / 2;
	long inner0 = left + (i->ink1 - i->ink0) + 1, inner1 = left + width - (i->ink1 - i->ink0 + 1) - 1;
	long x, y, k;

	if (!word)
		return cell;
	/* (the stems: I's cell twice) */
	blit(word, at_x, i->source, frame, i->x0, i->x1, 0, WORD_HEIGHT - 1);
	blit(word, at_x + width - (i->ink1 - i->ink0 + 1), i->source, frame, i->x0, i->x1, 0, WORD_HEIGHT - 1);
	/* (the bar, between the stems' ink) */
	for (x = inner0; x <= inner1; x++)
	{
		for (y = strokes->arm[1][0]; y <= strokes->arm[1][1]; y++)
			put(word, at_x + x, y, cell_pixel(strokes->e, frame, bar_column, y));
		for (k = 1; k <= STROKE_GLOW; k++)
		{
			put(word, at_x + x, strokes->arm[1][0] - k, bar_glow(strokes, frame, bar_column, k));
			put(word, at_x + x, strokes->arm[1][1] + k, bar_glow(strokes, frame, bar_column, k));
		}
	}
	return cell;
}

/* L, cut out of LT (MULTIPLAYER's): T's bar's end over its foot's columns
taken away - L is its stem and its foot alone: nothing right of its stem
above its foot's rows */
static void letter_l_clean(struct letter const *letter, byte *word, long at_x)
{
	long middle = 0, top = WORD_HEIGHT, bottom = -1, foot, stem1, x, y;

	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = letter->ink0; x <= letter->ink1; x++)
		{
			if (inked(letter->source, x, y))
			{
				top = MIN(top, y);
				bottom = MAX(bottom, y);
			}
		}
	}
	if (bottom < top)
		return;
	middle = (top + bottom) / 2;
	for (x = letter->ink0; x <= letter->ink1 && !inked(letter->source, x, middle); x++)
		;
	for (; x <= letter->ink1 && inked(letter->source, x, middle); x++)
		;
	stem1 = x - 1;
	/* (the foot: the rows inked right of the stem going up from the bottom) */
	for (foot = bottom; foot > middle; foot--)
	{
		boolean row = FALSE;

		for (x = stem1 + 2; x <= letter->ink1; x++)
		{
			if (inked(letter->source, x, foot))
				row = TRUE;
		}
		if (!row)
			break;
	}
	for (y = 0; y <= foot; y++)
	{
		for (x = stem1 + 2; x <= letter->x1; x++)
		{
			long to = at_x + (x - letter->x0);

			if (to >= 0 && to < WORD_WIDTH)
				memset(&word[(y * WORD_WIDTH + to) * 4], 0, 4);
		}
	}
}

/* a letter of the pictures, or made of the font's strokes (F, H): its
cell's width (-1: none), and, with word, it drawn with its cell at at_x */
static long glyph(char character, long frame, byte *word, long at_x)
{
	struct letter const *letter = letter_find(character);
	struct strokes strokes;

	if (letter)
	{
		if (word)
		{
			blit(word, at_x, letter->source, frame, letter->x0, letter->x1, 0, WORD_HEIGHT - 1);
			if (character == 'L' && letter->split)
				letter_l_clean(letter, word, at_x);
		}
		return letter->x1 - letter->x0 + 1;
	}
	if ((character != 'F' && character != 'H') || !strokes_find(&strokes))
		return -1;
	if (character == 'F')
	{
		if (word)
			letter_f(&strokes, frame, word, at_x);
		return strokes.e->x1 - strokes.e->x0 + 1;
	}
	return letter_h(&strokes, frame, word, at_x);
}

/* a letter's cell's margins: its ink's left and right of it */
static void glyph_margins(char character, long *left, long *right)
{
	struct letter const *letter = letter_find(character);

	if (!letter && character == 'F')
		letter = letter_find('E');
	if (!letter && character == 'H')
		letter = letter_find('I');
	*left = letter ? letter->ink0 - letter->x0 : 0;
	*right = letter ? letter->x1 - letter->ink1 : 0;
}

/* a word composed: its letters' ink the originals' gap apart, centred as
the originals are (their ink's middle column); FALSE if a letter cannot be
had */
static boolean word_compose(char const *text, long frame, byte *word)
{
	long width = 0, x, index, length = (long)strlen(text), centre = 0, sources_counted = 0;

	for (index = 0; index < length; index++)
	{
		long left, right, cell;

		if (text[index] == ' ')
		{
			width += menu_words.space - menu_words.gap;
			continue;
		}
		if ((cell = glyph(text[index], frame, NULL, 0)) < 0)
			return FALSE;
		glyph_margins(text[index], &left, &right);
		width += cell - left - right + (index ? menu_words.gap : 0);
	}
	/* (the originals' ink's middle) */
	for (index = 0; index < menu_words.letter_count; index++)
	{
		struct letter const *letter = &menu_words.letters[index];

		if (index + 1 == menu_words.letter_count || menu_words.letters[index + 1].source != letter->source)
		{
			struct letter const *first = letter;

			while (first > menu_words.letters && (first - 1)->source == letter->source)
				first--;
			centre += (first->ink0 + letter->ink1 + 1) / 2;
			sources_counted++;
		}
	}
	centre = sources_counted ? centre / sources_counted : WORD_WIDTH / 2;
	memset(word, 0, WORD_BYTES);
	x = centre - width / 2;
	for (index = 0; index < length; index++)
	{
		long left, right, cell;

		if (text[index] == ' ')
		{
			x += menu_words.space - menu_words.gap;
			continue;
		}
		cell = glyph(text[index], frame, NULL, 0);
		glyph_margins(text[index], &left, &right);
		glyph(text[index], frame, word, x - left);
		x += cell - left - right + menu_words.gap;
	}
	return TRUE;
}

/* the focused picture's glow, made again over the whole word from the not
focused one's shape - each letter's own glow (cut at its cell's edges, its
first neighbours' in it) would show seams where letters from different words
meet. As the originals' is, measured: their alpha 1.25 times a 7 x 7 maximum
of the not focused alpha, blurred (Gaussian, sigma 1.5) - within 10 of 255
of theirs -, the colour a blue that pales with it (red 0.18 alpha + 25,
green 0.07 alpha + 144, blue 255). The letters themselves (the not focused
alpha past 15) keep their own focused pixels */
static void glow_remake(byte const *shape, byte *focused)
{
	static float most[WORD_HEIGHT][WORD_WIDTH], blurred[WORD_HEIGHT][WORD_WIDTH];
	float kernel[9], total = 0.0f;
	long x, y, k;

	for (k = -4; k <= 4; k++)
		total += kernel[k + 4] = (float)exp(-(double)(k * k) / (2.0 * 1.5 * 1.5));
	for (k = 0; k < 9; k++)
		kernel[k] /= total;
	/* (the 7 x 7 maximum: rows, then columns) */
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = 0; x < WORD_WIDTH; x++)
		{
			byte value = 0;

			for (k = MAX(0, x - 3); k <= MIN(WORD_WIDTH - 1, x + 3); k++)
				value = MAX(value, shape[(y * WORD_WIDTH + k) * 4 + 3]);
			blurred[y][x] = value;
		}
	}
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = 0; x < WORD_WIDTH; x++)
		{
			float value = 0.0f;

			for (k = MAX(0, y - 3); k <= MIN(WORD_HEIGHT - 1, y + 3); k++)
				value = MAX(value, blurred[k][x]);
			most[y][x] = value;
		}
	}
	/* (the blur: rows, then columns; outside the picture none) */
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = 0; x < WORD_WIDTH; x++)
		{
			float value = 0.0f;

			for (k = -4; k <= 4; k++)
			{
				if (x + k >= 0 && x + k < WORD_WIDTH)
					value += most[y][x + k] * kernel[k + 4];
			}
			blurred[y][x] = value;
		}
	}
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		for (x = 0; x < WORD_WIDTH; x++)
		{
			float value = 0.0f;
			byte *out = &focused[(y * WORD_WIDTH + x) * 4];
			long alpha;

			for (k = -4; k <= 4; k++)
			{
				if (y + k >= 0 && y + k < WORD_HEIGHT)
					value += blurred[y + k][x] * kernel[k + 4];
			}
			if (shape[(y * WORD_WIDTH + x) * 4 + 3] > 15)
				continue;
			alpha = (long)(value * 1.25f + 0.5f);
			alpha = MIN(alpha, 255);
			out[0] = 255;
			out[1] = (byte)MIN(255, (long)(0.07f * alpha + 144.5f));
			out[2] = (byte)MIN(255, (long)(0.18f * alpha + 25.0f));
			out[3] = (byte)alpha;
		}
	}
}

/* the word kept in the data folder, if it is of these pictures */
static boolean cache_read(char const *name, long frame, byte *word)
{
	char path[64];
	unsigned long header[3];
	FILE *file;
	boolean read = FALSE;

	snprintf(path, sizeof(path), "t:\\menu_words_%s_%ld.rgba", name, frame);
	if ((file = fopen(path, "rb")) == NULL)
		return FALSE;
	if (fread(header, sizeof(header), 1, file) == 1 && header[0] == WORDS_FILE_SIGNATURE &&
		header[1] == menu_words.checksum && header[2] == WORD_BYTES)
	{
		read = fread(word, WORD_BYTES, 1, file) == 1;
	}
	fclose(file);
	return read;
}

static void cache_write(char const *name, long frame, byte const *word)
{
	char path[64];
	unsigned long header[3];
	FILE *file;

	header[0] = WORDS_FILE_SIGNATURE;
	header[1] = menu_words.checksum;
	header[2] = WORD_BYTES;
	snprintf(path, sizeof(path), "t:\\menu_words_%s_%ld.rgba", name, frame);
	if ((file = fopen(path, "wb")) == NULL)
		return;
	if (fwrite(header, sizeof(header), 1, file) != 1 || fwrite(word, WORD_BYTES, 1, file) != 1)
	{
		fclose(file);
		remove(path);
		return;
	}
	fclose(file);
}

/* the focused picture's glow made of the not focused word's shape */
static boolean focused_finish(char const *text, byte *focused)
{
	byte *shape = malloc(WORD_BYTES);

	if (!shape)
		return FALSE;
	if (!word_compose(text, 0, shape))
	{
		free(shape);
		return FALSE;
	}
	glow_remake(shape, focused);
	free(shape);
	return TRUE;
}

/* ---------- public code */

/* (menu_tags.c, a frame of a PNG of the menus folder about to be read) the
row's picture made of the game's letters, 8-bit BGRA, malloc's (the caller
frees it); NULL: the PNG as ever. png: the frame's path as written
("classic/{lang}/main_forge_0.png"); width, height: the frame's */
unsigned char *menu_words_frame(
	char const *png,
	long width,
	long height)
{
	char name[32];
	long frame, index;
	byte *word;

	if (!png || strncmp(png, "classic/{lang}/main_", 20) || width != WORD_WIDTH || height != WORD_HEIGHT)
		return NULL;
	if (strcmp(lang_current(), "en") || (getenv("HALO_MENU_WORDS") && !strcmp(getenv("HALO_MENU_WORDS"), "0")))
		return NULL;
	if (sscanf(png + 20, "%31[a-z]_%ld.png", name, &frame) != 2 || frame < 0 || frame >= FRAMES)
		return NULL;
	for (index = 0; index < (long)NUMBEROF(rows) && strcmp(rows[index].name, name); index++)
		;
	if (index == (long)NUMBEROF(rows) || !menu_words_ready())
		return NULL;
	word = malloc(WORD_BYTES);
	if (!word)
		return NULL;
	if (cache_read(name, frame, word))
		return word;
	if (!word_compose(rows[index].word, frame, word) ||
		(frame == 1 && !focused_finish(rows[index].word, word)))
	{
		platform_log("menu words: %s cannot be made of the game's letters: the port's picture", rows[index].word);
		free(word);
		return NULL;
	}
	cache_write(name, frame, word);
	platform_log("menu words: %s (%s) made of the game's letters", rows[index].word, frame ? "focused" : "not focused");
	return word;
}
