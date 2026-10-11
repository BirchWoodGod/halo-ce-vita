/*
LANG.H

The port's own text in the player's language (lang.c): T("English") is the
text in the language chosen, or the English itself when the language has no
entry for it (or the language is English). The English stays in the code,
as the key; each other language is a file of UTF-8 text,
port/vita/app0/lang/<code>.txt (app0:lang/ on the Vita), read once at
start. N_("English") marks a string that is translated where it is shown
(the settings panel's rows), for tools/lang_check.py, which checks that every
marked string has an entry in every language file.

Halo's own text (its menus, HUD, objectives, subtitles) comes from the
player's map files, not from here: see port/vita/README.md, "Translations".
*/

#ifndef PORT_LANG_H
#define PORT_LANG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the most languages (English, then a file each) */
#define LANG_MAXIMUM 24

/* reads every language file found in `directory` (no trailing slash); the
language chosen stays English until lang_select. Again: no change */
void lang_init(const char *directory);

/* chooses the language by its code ("es", "pt-BR"; a code with a region
falls back to the language's file, "pt"; "en", "", NULL or an unknown code:
English); nonzero if a file of it was found */
int lang_select(const char *code);

/* the code of the language chosen ("en" for English) */
const char *lang_current(void);

/* the languages there are: English first, then one per file found, in the
order of lang.c's codes; each one's code and name (its own word for itself,
"Español", from the file's @name line) */
int lang_count(void);
const char *lang_code(int index);
const char *lang_name(int index);

/* the text in the language chosen, or `english` itself */
const char *lang_text(const char *english);

/* the count of characters (UTF-8 code points) of `text` */
int lang_utf8_length(const char *text);

/* (tests) the lines a language file had that were not taken (a format
that does not match its English, a line that cannot be read), each in
`report` ("es.txt:12: ..."); and the strings lang_text was asked for that
the language chosen has no entry for, since the last call (letters only:
a number or a name with no letters is never translated) */
int lang_problems(char *report, int size);
int lang_missing(char *report, int size);

#define T(english) lang_text(english)
#define N_(english) english

#if defined(__SIZEOF_WCHAR_T__) && __SIZEOF_WCHAR_T__ == 2
/* the game's UTF-16 text (its 16-bit wchar_t), for strings the port draws
with Halo's fonts: the translation, made UTF-16 once, or `english` */
const wchar_t *lang_text_wide(const wchar_t *english);
#define TW(english) lang_text_wide(english)
#endif

#ifdef __cplusplus
}
#endif

#endif
