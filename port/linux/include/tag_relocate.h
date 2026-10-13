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
/* port: a Halo: CE Anniversary map's structure BSP of `size` bytes, just read
to `bsp` inside that tag cache, where its tags link it to `to_link`: its
pointers, which the map links to `from_link`, moved to `to_link` first */
void halo_tag_relocate_linked_structure_bsp_rebase(void *bsp, unsigned long size, unsigned long from_link,
	unsigned long to_link);
/* that tag cache is gone */
void halo_tag_relocate_linked_release(void);

/* called now and then as tags are walked (the loading screen's frames:
custom_edition_cache.c), or NULL */
extern void (*halo_tag_relocate_progress)(void);

#endif
