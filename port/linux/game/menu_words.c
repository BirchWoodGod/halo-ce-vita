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
which those words have not, are made of their letters' own strokes: F of
E down to its middle arm and I's stem below it, H of two I's and E's
middle arm between them. The words are kept in the game's data folder
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
#define WORDS_FILE_SIGNATURE 0x3344574DUL /* 'MWD3': made again when the way of making them changes */

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

/* E's middle arm's rows (the inked rows of E's right half between its top
and bottom arms), and I's stem's width */
static void e_arm_rows(struct letter const *e, long stem, long *top, long *bottom)
{
	long y, x, first = -1, last = -1, runs = 0;
	boolean was = FALSE;

	*top = *bottom = -1;
	for (y = 0; y < WORD_HEIGHT; y++)
	{
		boolean row = FALSE;

		for (x = e->ink0 + stem + 1; x <= e->ink1; x++)
		{
			if (inked(e->source, x, y))
				row = TRUE;
		}
		if (row && !was)
		{
			runs++;
			if (runs == 2)
				first = y;
		}
		if (!row && was && runs == 2)
			last = y - 1;
		was = row;
	}
	*top = first;
	*bottom = last;
}

/* a letter of the pictures, or made of others' strokes (F, H): its cell's
width (-1: none), and, with word, it drawn with its cell at at_x */
static long glyph(char character, long frame, byte *word, long at_x)
{
	struct letter const *letter = letter_find(character);
	struct letter const *e, *i;
	long stem, arm_top, arm_bottom, i_left;

	if (letter)
	{
		if (word)
			blit(word, at_x, letter->source, frame, letter->x0, letter->x1, 0, WORD_HEIGHT - 1);
		return letter->x1 - letter->x0 + 1;
	}
	if ((character != 'F' && character != 'H') || !(e = letter_find('E')) || !(i = letter_find('I')))
		return -1;
	stem = i->ink1 - i->ink0 + 1;
	e_arm_rows(e, stem, &arm_top, &arm_bottom);
	if (arm_top < 0 || arm_bottom < arm_top)
		return -1;
	/* (I's cell placed so that its stem is where E's is) */
	i_left = (e->ink0 - e->x0) - (i->ink0 - i->x0);
	if (character == 'F')
	{
		/* E above the gap under its middle arm (its glow with it); I's
		stem below: E without its bottom arm */
		long cut = arm_bottom + (WORD_HEIGHT > 0 ? 2 : 0);

		if (word)
		{
			blit(word, at_x, e->source, frame, e->x0, e->x1, 0, cut);
			blit(word, at_x + i_left, i->source, frame, i->x0, i->x1, arm_top, WORD_HEIGHT - 1);
		}
		return e->x1 - e->x0 + 1;
	}
	/* H: E's stem (I's), E's middle arm drawn on as long as the space between
	the stems, I's stem at the right: an E's width and a stem more */
	{
		long inner = (e->ink1 - e->ink0 + 1) - stem;
		long right_stem = (e->ink0 - e->x0) + stem + inner;

		if (word)
		{
			long arm_x0 = e->ink0 + stem, arm_x1 = e->ink1 - 1, done = 0, length = arm_x1 - arm_x0 + 1;

			blit(word, at_x + i_left, i->source, frame, i->x0, i->x1, 0, WORD_HEIGHT - 1);
			while (length > 0 && done < inner)
			{
				long piece = MIN(length, inner - done);

				blit(word, at_x + (e->ink0 - e->x0) + stem + done, e->source, frame, arm_x0, arm_x0 + piece - 1,
					arm_top - 3, arm_bottom + 3);
				done += piece;
			}
			blit(word, at_x + right_stem - (i->ink0 - i->x0), i->source, frame, i->x0, i->x1, 0, WORD_HEIGHT - 1);
		}
		return right_stem + stem + (i->x1 - i->ink1);
	}
}

/* a letter's cell's margins: its ink's left and right of it */
static void glyph_margins(char character, long *left, long *right)
{
	struct letter const *letter = letter_find(character);
	struct letter const *i = letter_find('I');

	if (!letter && (character == 'F' || character == 'H'))
		letter = letter_find('E');
	*left = letter ? letter->ink0 - letter->x0 : 0;
	*right = letter ? letter->x1 - letter->ink1 : 0;
	/* (H's right is I's) */
	if (character == 'H' && i)
		*right = i->x1 - i->ink1;
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
	if (!word_compose(rows[index].word, frame, word))
	{
		platform_log("menu words: %s cannot be made of the game's letters: the port's picture", rows[index].word);
		free(word);
		return NULL;
	}
	cache_write(name, frame, word);
	platform_log("menu words: %s (%s) made of the game's letters", rows[index].word, frame ? "focused" : "not focused");
	return word;
}
