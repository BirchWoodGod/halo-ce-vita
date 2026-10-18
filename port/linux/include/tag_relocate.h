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

/* port: the layouts the walk follows (tag_layouts.h), for the scene packs'
builder (port/linux/game/scene_pack.c): a group's root layout
(HALO_TAG_LAYOUT_NONE: its root has no pointers; HALO_TAG_LAYOUT_UNKNOWN: no
such group), a layout's fields */
enum
{
	HALO_TAG_LAYOUT_FIELD_BLOCK,
	HALO_TAG_LAYOUT_FIELD_DATA,
	HALO_TAG_LAYOUT_FIELD_REFERENCE,
	HALO_TAG_LAYOUT_FIELD_POINTER,
	HALO_TAG_LAYOUT_FIELD_STRUCT,
	HALO_TAG_LAYOUT_NONE = 0xFFFF,
	HALO_TAG_LAYOUT_UNKNOWN = 0xFFFE
};
struct halo_tag_layout_field
{
	unsigned short offset;
	unsigned char kind;
	/* a block's element layout or a struct's (HALO_TAG_LAYOUT_NONE: none
	with pointers), and a block element's size */
	unsigned short child;
	unsigned long child_size;
};
unsigned short halo_tag_layout_root(unsigned long group_tag);
long halo_tag_layout_field_count(unsigned short layout);
int halo_tag_layout_field(unsigned short layout, long index, struct halo_tag_layout_field *field);

/* port: a scene pack's tags (scene_pack.c), size bytes at blob laid out as a
map's tag data and linked to link_base: their pointers moved to where blob
is, each followed only into the pack, each reference checked against the
pack's own table. The map's walk is left as it was. Returns how many were
wrong (0: the pack may be checked further), (unsigned long)-1 without memory */
unsigned long halo_tag_relocate_pack(void *blob, unsigned long size, unsigned long link_base);

/* called now and then as tags are walked (the loading screen's frames:
custom_edition_cache.c), or NULL */
extern void (*halo_tag_relocate_progress)(void);

#endif
