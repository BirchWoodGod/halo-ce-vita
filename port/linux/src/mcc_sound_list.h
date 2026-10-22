/*
MCC_SOUND_LIST.H

(port) The sound lists of Halo: The Master Chief Collection's Halo CE
(halo1\sound\pc\lst\sounds_*.lst.bin): which streams of an FSB bank
(fsb5.h) a sound tag's permutations are. Plain C on the bytes it is given.

The format, worked out from the player's own files (all twelve parse to
their last byte, and in sounds_adpcm the streams they name cover the bank's
3106 streams once each, in order, each stream named
"<leaf>_<pitch range>_<permutation>"), all 32-bit little-endian:

	version        1
	count          the number of records
	count records:
	    path_bytes   the tag path's length with its zero
	    path         the sound tag's path, backslashes, no extension
	                 ("sound\old_sfx\weapons\assault rifle\fire"), zero-terminated
	    first        the bank's first stream of the sound
	    streams      how many streams, consecutive from `first`: the sound's
	                 permutations, its pitch ranges one after another

Nothing follows the last record.
*/

#ifndef __MCC_SOUND_LIST_H
#define __MCC_SOUND_LIST_H

#include <stdint.h>

/* ---------- constants */

#define MCC_SOUND_LIST_VERSION 1
#define MCC_SOUND_LIST_MAXIMUM_RECORDS 100000
#define MCC_SOUND_LIST_MAXIMUM_PATH_BYTES 512

/* ---------- structures */

struct mcc_sound_list_record
{
	/* into the list's path pool */
	uint32_t path_offset;
	uint32_t first;
	uint32_t streams;
};

struct mcc_sound_list
{
	uint32_t count;
	struct mcc_sound_list_record *records;
	/* the record numbers sorted by path (case-insensitive) */
	uint32_t *sorted;
	char *paths;
	uint32_t path_bytes;
};

/* ---------- prototypes/MCC_SOUND_LIST.C */

/* Parses `size` bytes into `list`: 1, or 0 with `error` saying where the
file is wrong (the list then empty). Free with mcc_sound_list_free. */
int mcc_sound_list_parse(
	uint8_t const *bytes,
	uint32_t size,
	struct mcc_sound_list *list,
	char *error,
	unsigned long error_size);
void mcc_sound_list_free(struct mcc_sound_list *list);

/* The record of the sound tag `path` (any case; '/' is '\'; a trailing
".sound" is not part of it), or NULL. */
struct mcc_sound_list_record const *mcc_sound_list_find(struct mcc_sound_list const *list, char const *path);
char const *mcc_sound_list_path(struct mcc_sound_list const *list, struct mcc_sound_list_record const *record);

#endif
