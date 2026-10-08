/*
TAG_GROUPS.H

header included in hcex build.
*/

#ifndef __TAG_GROUPS_H
#define __TAG_GROUPS_H
#pragma once

/* ---------- headers */

#include "memory/byte_swapping.h"

/* ---------- constants */

enum tag_field_type
{
	_tag_field_string = 0,
	_tag_field_char_integer = 1,
	_tag_field_short_integer = 2,
	_tag_field_long_integer = 3,
	_tag_field_enum = 6,
	_tag_field_word_flags = 8,
	_tag_field_byte_flags = 9,
	_tag_field_real_point3d = 17,
	_tag_field_real_plane2d = 23,
	_tag_field_real_plane3d = 24,
	_tag_field_tag_reference = 33,
	_tag_field_block = 34,
	_tag_field_data = 37,
	_tag_field_pad = 40,
	_tag_field_terminator = 44,
};

/* ---------- macros */

#define TAG_BLOCK_GET_ELEMENT(block_address, index, type) ((type *)tag_block_get_element_with_size((block_address), (index), sizeof(type)))

/* ---------- structures */

typedef void (*byte_swap_block_proc)(void *);
typedef boolean (*postprocess_block_proc)(void *, boolean);
typedef byte *(*format_block_proc)(long, struct tag_block *, long, byte *);
typedef void (*delete_block_proc)(struct tag_block *, long);
typedef void (*byte_swap_data_proc)(void *, void *, long);

struct tag_enum_definition
{
	long count;
	char **names;
	void *unused;
};

struct tag_field
{
	short type;
	word pad;
	char *name;
	void *definition;
};

struct tag_data_definition
{
	char *name;
	unsigned long flags;
	long maximum_size;
	byte_swap_data_proc byte_swap_data;
};

struct tag_block_definition
{
	char *name;
	unsigned long flags;
	long maximum_element_count;
	long element_size;
	void *default_element;
	struct tag_field *fields;
	byte_swap_block_proc byte_swap_block;
	postprocess_block_proc postprocess_block;
	format_block_proc format_block;
	delete_block_proc delete_block;
	byte_swap_code *byte_swap_codes;
};

struct tag_block
{
	long count;
	void *address;
	struct tag_block_definition *definition;
};

struct tag_reference
{
	unsigned long group_tag;
	char *name;
	long name_length;
	long index;
};

struct tag_reference_definition
{
	unsigned long flags;
	unsigned long group_tag;
	unsigned long *group_tags;
};

typedef char tag_reference_definition_size_assert[
	sizeof(struct tag_reference_definition) == 0xC ? 1 : -1];

struct tag_data
{
	long size;
	unsigned long pad;
	long file_offset;
	void *address;
	struct tag_data_definition *definition;
};

/* ---------- prototypes/TAG_GROUPS.C */

long verify_tag_reference(struct tag_reference const *reference);
void *tag_data_get_pointer(struct tag_data const *data, long offset, long size);
void *tag_block_get_element_with_size(struct tag_block const *block, long index, long element_size);
#ifdef HALO_LINUX
/* port: zeros in place of an element, data or tag that is not there (a
map's bad index: tag_groups.c), and the log of one */
void *tag_empty_data_sized(long size);
void tag_index_error(char const *what, long index, long count);
#endif

#if defined(HALO_RELEASE) && defined(HALO_LINUX)
/* (port) release builds check nothing in tag_block_get_element_with_size
(match_assert is empty): the element's address, inline - a call per
element was ~2% of the Pi's CPU in BSP and collision walks. Unchecked here
too (a check per element cost 4% of the tick on the Pi): a map's blocks are
checked once as it loads (tag_relocate.c: every block's elements within the
tags, every tag reference a tag of its group), and the element indices a
crafted map was found to use past their blocks are checked where they are
read (object_types.c) */
static __inline__ void *tag_block_get_element_inline(struct tag_block const *block, long index, long element_size)
{
	return (void *)((char *)block->address + index * element_size);
}
#undef TAG_BLOCK_GET_ELEMENT
#define TAG_BLOCK_GET_ELEMENT(block_address, index, type) \
	((type *)tag_block_get_element_inline((block_address), (index), sizeof(type)))
#endif

/* ---------- prototypes/CACHE_FILES.C */

long tag_loaded(long group_tag, const char *name);

void *tag_get(long group_tag, long tag_index);

#if defined(HALO_LINUX) && defined(HALO_RELEASE)
/* (port, release builds) tag_get inline: cache_files.c's function is the
tag instance's base address and assertions, which release builds do not
check (the layout below is cache_files.c's struct cache_file_tag_instance,
checked there); a few thousand calls a frame in a fight */
struct cache_file_tag_instance;
extern struct cache_file_tag_instance *global_tag_instances;
struct halo_tag_instance_layout
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};
#endif
#if defined(HALO_LINUX) && defined(HALO_RELEASE) && !defined(HALO_CACHE_FILES_C)
/* (the loaded tags' count: cache_files.c) */
extern long halo_loaded_tag_count;
static __inline__ void *tag_get_inline(long group_tag, long tag_index)
{
	struct halo_tag_instance_layout *instance;
	short absolute_index = (short)tag_index;

	/* port: a map's tag reference is untrusted: an index past the tags, or
	a tag of another group than the one asked for (which the debug builds
	assert on), gets the empty data (tag_groups.c) rather than memory past
	the tags or another kind of tag read as this one */
	if (__builtin_expect(absolute_index < 0 || absolute_index >= halo_loaded_tag_count, 0))
	{
		tag_index_error("tag", tag_index, halo_loaded_tag_count);
		return tag_empty_data_sized(0x10000);
	}
	instance = &((struct halo_tag_instance_layout *)global_tag_instances)[absolute_index];
	if (__builtin_expect(!instance->base_address || (instance->group_tag != group_tag &&
		instance->parent_group_tags[0] != group_tag && instance->parent_group_tags[1] != group_tag), 0))
	{
		tag_index_error("tag of its group", tag_index, halo_loaded_tag_count);
		return tag_empty_data_sized(0x10000);
	}
	return instance->base_address;
}
#define tag_get(group_tag, tag_index) tag_get_inline((group_tag), (tag_index))
#endif

/* ---------- globals */

/* ---------- public code */

#endif // __TAG_GROUPS_H
