/* tag_relocate.h: moving cache file data off the Xbox tag cache address (port/linux/src/tag_relocate.c) */
#ifndef HALO_TAG_RELOCATE_H
#define HALO_TAG_RELOCATE_H

/* the tag data of a cache file was just read to the start of the tag cache */
void halo_tag_relocate_tags(void *tag_cache, unsigned long size);
/* a structure BSP was just read to bsp, inside the tag cache */
void halo_tag_relocate_structure_bsp(void *tag_cache, void *bsp, unsigned long size);

#endif
