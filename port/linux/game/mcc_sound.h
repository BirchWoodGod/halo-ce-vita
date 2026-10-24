/*
MCC_SOUND.H

(port) The sounds of MCC's own Halo CE maps (docs/mcc_cea_maps.md): MCC
keeps them in FMOD banks beside the maps (halo1\sound\pc\*.fsb, fsb5.h),
and in lists (halo1\sound\pc\lst\sounds_*.lst.bin, mcc_sound_list.h) that
say which streams of a bank each sound tag is. This opens the sound effects'
bank (sounds_adpcm.fsb: IMA ADPCM, the Xbox's own format, so its streams are
the permutations' samples as they lie, read from the bank by the sound
cache like a sounds.map's) and gives custom_edition_cache_convert the
streams of each sound tag (cache_file_formats.h, mcc_sound_source).

The dialogue, the music and the streamed ambience are in banks of CELT
streams (sounds_dialogs, sounds_music, sounds_stream, sounds_debug): the
lists of those banks are read only to count what stays silent.

Opened when an MCC map's tags are converted, closed straight after: it is
a few hundred kilobytes for as long as the load takes.
*/

#ifndef __MCC_SOUND_H
#define __MCC_SOUND_H

#include "cache_file_formats.h"

/* the banks and lists, in the content folder (d:\mcc\) */
#define MCC_SOUND_DIRECTORY "d:\\mcc\\halo1\\sound\\pc\\"
#define MCC_SOUND_ADPCM_BANK "sounds_adpcm.fsb"
#define MCC_SOUND_ADPCM_LIST "lst\\sounds_adpcm.lst.bin"

/* Opens the sound effects' bank and the lists; `combined_offset` is where
the bank starts in the combined offset space and `bank_bytes` its size (the
file the sounds slot has open). FALSE, `source` empty, with `reason` (a
sentence, may be NULL) when the bank or its list is not there or not whole. */
boolean mcc_sound_open(
	struct mcc_sound_source *source,
	uint32_t combined_offset,
	uint32_t bank_bytes,
	char *reason,
	long reason_size);
void mcc_sound_close(void);

/* The count of what is not played (the CELT banks' sounds and the streams
the lists name there), for the log. */
void mcc_sound_log(struct custom_edition_conversion_report const *report);

#endif
