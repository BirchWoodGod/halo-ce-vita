/*
DATA.H

header included in hcex build.
*/

#ifndef __DATA_H
#define __DATA_H
#pragma once

/* ---------- headers */

#include "tag_files.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct data_array
{
	char name[TAG_STRING_LENGTH+1];
	short maximum_count;
	short size;
	boolean valid;
	boolean identifier_zero_invalid;
	unsigned long signature;
	short first_free_absolute_index;
	short count;
	short actual_count;
	short next_identifier;
	void *data;
};

struct datum_header
{
	short identifier;
};

struct data_iterator
{
	struct data_array *data;
	short absolute_index;
	short pad;
	long datum_index;
	unsigned long signature;
};

typedef char data_iterator_size_assert[
	sizeof(struct data_iterator) == 0x10 ? 1 : -1];

/* ---------- prototypes/DATA.C */

long data_allocation_size(short maximum_count, short size);
void data_initialize(struct data_array *data, const char *name, short maximum_count, short size);
void *datum_try_and_get(struct data_array *data, long index);
void data_verify(struct data_array *data);
void *datum_get(struct data_array *data, long index);
struct data_array *data_new(const char *name, short maximum_count, short size);
void data_dispose(struct data_array *data);
void data_make_invalid(struct data_array *data);
long datum_new_at_index(struct data_array *data, long index);
long datum_new(struct data_array *data);
void datum_delete(struct data_array *data, long index);
void data_delete_all(struct data_array *data);
void data_iterator_new(struct data_iterator *iterator, struct data_array *data);
void *data_iterator_next(struct data_iterator *iterator);
long data_next_index(struct data_array *data, long index);
long data_prev_index(struct data_array *data, long index);
void data_compact(struct data_array *data);
void data_make_valid(struct data_array *data);

#if defined(HALO_LINUX) && defined(HALO_RELEASE) && !defined(HALO_DATA_C)
/* (port, release builds) datum_get and datum_try_and_get inline for the
datum found the usual way: the index in range, the slot in use with a
matching identifier, and the array carrying no render-epoch marks
(render_epoch.c keeps its marked flag in the name's last byte, the
terminator data_initialize leaves zero); anything else, the not-found
paths included, goes to data.c's functions as before. Release builds
check no assertions, so the result is the same; the lookups are among the
tick's commonest calls, and the Vita build, without link-time
optimisation, paid a call into data.c and another into render_epoch.c for
each. */
#include "render_epoch.h"

/* with the tick on its own thread, an array the tick created or deleted
in this epoch carries marks (render_epoch.c), and the lookup asks whether
the slot is one: the usual answer, an unmarked slot, read inline from the
array's table entry (the same entry and byte render_epoch.c reads); 0 for
anything else, which data.c's function settles */
static __inline int datum_unmarked_inline(const struct data_array *data, short absolute_index)
{
	unsigned long hint = *(const unsigned short *)((const unsigned char *)data + 38);

	if (hint && hint <= HALO_EPOCH_ARRAY_TABLE_SIZE)
	{
		const struct halo_epoch_marked_array *entry = &halo_epoch_marked_arrays[hint - 1];

		if (entry->data == data && absolute_index < entry->maximum_count)
			return !entry->marks[absolute_index];
	}
	return 0;
}

static __inline void *datum_get_inline(struct data_array *data, long index)
{
	short absolute_index = (short)index;
	short identifier = (short)(index >> 16);

	if (absolute_index >= 0 && absolute_index < data->count)
	{
		struct datum_header *header = (struct datum_header *)((char *)data->data + data->size * absolute_index);

		if (header->identifier && (!identifier || identifier == header->identifier) &&
			(!data->name[TAG_STRING_LENGTH] || datum_unmarked_inline(data, absolute_index)))
			return header;
	}
	return datum_get(data, index);
}

static __inline void *datum_try_and_get_inline(struct data_array *data, long index)
{
	short absolute_index = (short)index;
	short identifier = (short)(index >> 16);

	if (index != -1 && absolute_index >= 0 && absolute_index < data->maximum_count)
	{
		struct datum_header *header = (struct datum_header *)((char *)data->data + data->size * absolute_index);

		if (!header->identifier || (identifier && header->identifier != identifier))
			return 0;
		if (!data->name[TAG_STRING_LENGTH] || datum_unmarked_inline(data, absolute_index))
			return header;
	}
	return datum_try_and_get(data, index);
}

#define datum_get(data, index) datum_get_inline((data), (index))
#define datum_try_and_get(data, index) datum_try_and_get_inline((data), (index))
#endif


/* ---------- globals */

/* ---------- public code */

#endif // __DATA_H
