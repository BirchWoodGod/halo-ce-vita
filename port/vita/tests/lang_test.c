/*
LANG_TEST.C

A desktop test of the port's translations (port/linux/src/lang.c): a
language file read (its @name, C's escapes, a comment, a byte order mark),
a line whose % conversions are not its English's not taken (and said), a
line that cannot be read said, the first of two lines for one English
taken; a language chosen by its code and by a code with a region, English
for an unknown one and for "en"; a text with no entry the English itself
(the same pointer), noted while the missing ones are tracked; UTF-8
counted in characters; and the game's UTF-16 text (16-bit wchar_t, as the
game is built: this test is compiled with -fshort-wchar), the translation
made UTF-16 once and kept. Then overlay_font.h's glyphs: every character a
language file may use (tools/lang_check.py's) is drawn, an accented capital
with its mark above the cell, a character cut short by a byte limit not
drawn at all.

Run port/vita/tests/run_lang_test.sh (with tools/lang_check.py on the
shipped files).
*/

#include "lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../host/overlay_font.h"

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static void write_text(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	fputs(text, file);
	fclose(file);
}

static int wide_equals(const wchar_t *wide, const char *ascii_or_latin1_utf8)
{
	const unsigned char *text = (const unsigned char *)ascii_or_latin1_utf8;

	for (; *text; wide++)
	{
		unsigned int character = *text++;

		if (character >= 0xC0)
			character = ((character & 0x1F) << 6) | (*text++ & 0x3F);
		if ((unsigned short)*wide != character)
			return 0;
	}
	return *wide == 0;
}

int main(void)
{
	static const char spanish[] =
		"\xEF\xBB\xBF# a test file\n"
		"@name = \"Español\"\n"
		"\"Graphics\" = \"Gráficos\"\n"
		"  \"Line\\none\\t\\\"two\\\" \\\\\" = \"Línea\\nuno\\t\\\"dos\\\" \\\\\"   # a comment after it\n"
		"\"%d of %d\" = \"%d de %d\"\n"
		"\"Saved: %.56s\" = \"Guardado: %s\"\n"
		"\"Joining %s...\" = \"Uniéndose a %s y %d...\"\n"
		"\"broken\" = Roto\n"
		"\"Twice\" = \"Una vez\"\n"
		"\"Twice\" = \"Dos veces\"\n"
		"\"100%% sure\" = \"Seguro al 100%%\"\n"
		"\"Press %b-button to cancel\" = \"Pulsa %b-button para cancelar\"\n"
		"\"Ping %ld ms\" = \"Ping %ld ms\"\r\n"
		"\"Can't host\\r\\nnow\" = \"No se puede\\r\\nahora\"\n";
	char report[2048];
	const char *english = "Graphics";
	int problems;

	mkdir("lang", 0777);
	write_text("lang/es.txt", spanish);
	write_text("lang/fr.txt", "@name = \"Français\"\n\"Graphics\" = \"Graphismes\"\n");
	lang_init("lang");
	check(lang_count() == 3 && !strcmp(lang_code(0), "en") && !strcmp(lang_name(0), "English") &&
		!strcmp(lang_code(1), "es") && !strcmp(lang_name(1), "Español") && !strcmp(lang_code(2), "fr") &&
		!strcmp(lang_name(2), "Français"), "English, then the files found in lang.c's order, by their @name");
	check(!strcmp(lang_current(), "en") && T(english) == english, "English until one is chosen: the same pointer");
	check(lang_select("es") && !strcmp(lang_current(), "es") && !strcmp(T("Graphics"), "Gráficos"), "Spanish chosen");
	check(!strcmp(T("Line\none\t\"two\" \\"), "Línea\nuno\t\"dos\" \\"), "C's escapes, either side; a comment after");
	check(!strcmp(T("%d of %d"), "%d de %d") && !strcmp(T("100%% sure"), "Seguro al 100%%") &&
		!strcmp(T("Press %b-button to cancel"), "Pulsa %b-button para cancelar"),
		"the same conversions (and %%, and Halo's %b-button): taken");
	check(!strcmp(T("Saved: %.56s"), "Saved: %.56s") && !strcmp(T("Joining %s..."), "Joining %s..."),
		"other conversions (%s for %.56s, one more): not taken, the English used");
	check(!strcmp(T("Twice"), "Dos veces"), "two lines for one English: the last taken");
	check(!strcmp(T("Ping %ld ms"), "Ping %ld ms") && !strcmp(T("Can't host\r\nnow"), "No se puede\r\nahora"),
		"a CR LF line end; \\r");
	problems = lang_problems(report, sizeof(report));
	printf("%s", report);
	check(problems == 4 && strstr(report, "es.txt:6: its % conversions") && strstr(report, "es.txt:7: its % conversions") &&
		strstr(report, "es.txt:8: the translation is not one quoted string") && strstr(report, "es.txt:10: a second line"),
		"the lines not taken are said, by file and line");
	check(lang_select("es-MX") && !strcmp(lang_current(), "es") && lang_select("es_ES") && !strcmp(lang_current(), "es"),
		"a code with a region: the language's file");
	check(!lang_select("pt-BR") && !strcmp(lang_current(), "en") && T(english) == english, "no file of it: English");
	check(!lang_select("en") && !strcmp(lang_current(), "en") && !lang_select(NULL) && !lang_select(""),
		"en, none, empty: English");
	lang_select("es");
	lang_missing(NULL, 0);
	check(!strcmp(T("Not translated"), "Not translated") && !strcmp(T("42%"), "42%") && T("Not translated") &&
		lang_missing(report, sizeof(report)) == 1 && !strcmp(report, "Not translated\n"),
		"no entry: the English, noted once (a text with no letters never)");
	check(lang_utf8_length("Gráficos") == 8 && lang_utf8_length("¿Sí?") == 4 && lang_utf8_length("") == 0,
		"UTF-8 counted in characters");
	{
		const wchar_t *wide = TW(L"Graphics");
		const wchar_t *untranslated = L"Untranslated";

		check(sizeof(wchar_t) == 2 && wide_equals(wide, "Gráficos") && TW(L"Graphics") == wide,
			"the game's UTF-16: the translation, made once and kept");
		check(TW(untranslated) == untranslated, "the game's UTF-16 with no entry: the English itself");
		check(wide_equals(TW(L"Can't host\r\nnow"), "No se puede\r\nahora"), "the game's UTF-16 with its line breaks");
	}
	lang_select("fr");
	check(!strcmp(T("Graphics"), "Graphismes") && !strcmp(T("%d of %d"), "%d of %d"), "French: its own, English for the rest");
	lang_init("elsewhere");
	check(lang_count() == 3, "lang_init again: no change");

	/* the overlay's glyphs */
	{
		static const char drawn[] = "ÀÁÂÃÄÅÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝàáâãäåçèéêëìíîïðñòóôõöøùúûüýÿ¡ª«º»¿ß";
		const char *at = drawn;
		unsigned char rows[OVERLAY_ROWS], a[OVERLAY_ROWS];
		int all = 1, count = 0;

		while (*at)
		{
			const char *before = at;

			if (!overlay_glyph(&at, rows))
			{
				printf("not drawn: %.*s\n", (int)(at - before), before);
				all = 0;
			}
			count++;
		}
		check(all && count == lang_utf8_length(drawn), "every Latin-1 character a language file may use is drawn");
		at = "a";
		overlay_glyph(&at, a);
		at = "á";
		overlay_glyph(&at, rows);
		check(!memcmp(rows + OVERLAY_ROWS_ABOVE, a + OVERLAY_ROWS_ABOVE, 8) && rows[0] | rows[1] && !rows[2] &&
			!a[0] && !a[1], "á: A's cell, the mark above it, a row's gap");
		at = "ñ";
		overlay_glyph(&at, rows);
		check(rows[0] && rows[1] && !rows[2], "ñ: the tilde above");
		at = "\xC3";
		check(!overlay_glyph(&at, rows) && !*at, "a character cut short by a byte limit: not drawn");
		at = "\x80" "A";
		check(overlay_glyph(&at, rows) == 0 && !strcmp(at, "A"), "a stray byte: a cell, nothing drawn");
	}
	printf("-- %d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
