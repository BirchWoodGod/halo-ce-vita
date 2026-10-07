/* tag_relocate.h: moving cache file data off the Xbox tag cache address (port/linux/src/tag_relocate.c) */
#ifndef HALO_TAG_RELOCATE_H
#define HALO_TAG_RELOCATE_H

/* the tag data of a cache file was just read to the start of the tag cache */
void halo_tag_relocate_tags(void *tag_cache, unsigned long size);
/* a structure BSP was just read to bsp, inside the tag cache */
void halo_tag_relocate_structure_bsp(void *tag_cache, void *bsp, unsigned long size);

/* port: inconsistent tag blocks the last relocation walk found (a count or
element pointer that does not fit the loaded region: a crafted map). Not
zero: the caller refuses the map. */
unsigned long halo_tag_relocate_anomalies(void);

/* Halo Custom Edition tags (custom_edition_cache.c), linked to link_base
with a window of window_bytes there, loaded and converted to this build's
layouts at tag_cache: their pointers moved there (0 when out of memory) */
int halo_tag_relocate_linked_tags(void *tag_cache, unsigned long size, unsigned long link_base,
	unsigned long window_bytes);
/* a structure BSP of those, just read to bsp inside their tag cache */
void halo_tag_relocate_linked_structure_bsp(void *tag_cache, void *bsp, unsigned long size);
/* that tag cache is gone */
void halo_tag_relocate_linked_release(void);

#endif
