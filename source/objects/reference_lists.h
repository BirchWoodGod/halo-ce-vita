/*
REFERENCE_LISTS.H

file has inline function assertions.
*/

#ifndef __REFERENCE_LISTS_H
#define __REFERENCE_LISTS_H
#pragma once

/* ---------- headers */

#include "game_state.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct data_reference
{
	short identifier;
	word pad;
	long datum_index;
	long next_reference_index;
};

/* ---------- prototypes/EXAMPLE.C */

/* ---------- globals */

/* ---------- public code */

__inline struct data_array *reference_list_new(
	const char *name,
	short maximum_count)
{
	char buffer[256];

	sprintf(buffer, "%s reference", name);
	return game_state_data_new(buffer, maximum_count, sizeof(struct data_reference));
}

__inline long reference_list_get_next_datum_index(
	struct data_array *array,
	long *reference_index)
{
	long result;
	struct data_reference *reference;

	if (*reference_index!=NONE)
	{
		reference = (struct data_reference *)datum_get(array, *reference_index);
		*reference_index = reference->next_reference_index;
		result = reference->datum_index;
	}
	else
	{
		result = NONE;
	}

	return result;
}

#ifdef HALO_LINUX
/* (port) adds at the end of the list instead: a render walking the list
while the tick relinks a datum (removed, then added again) then still
reaches it, where an add at the head would have put it behind the walker
(objects flickered as they moved) */
__inline void reference_list_add_last(
	struct data_array *array,
	long *first_reference_index,
	long datum_index)
{
	long reference_index = datum_new(array);

	if (reference_index!=NONE)
	{
		struct data_reference *reference = (struct data_reference*)datum_get(array, reference_index);
		long *link = first_reference_index;

		reference->datum_index = datum_index;
		reference->next_reference_index = NONE;
		while (*link != NONE)
		{
			struct data_reference *last = (struct data_reference *)datum_get(array, *link);

			if (!last)
				break;
			link = &last->next_reference_index;
		}
		__atomic_thread_fence(__ATOMIC_RELEASE);
		halo_epoch_datum_ready(array, reference_index & 0xFFFF);
		*link = reference_index;
	}
	else
	{
		match_vassert("..\\objects\\reference_lists.h", 0x5b, FALSE,
			csprintf(temporary, "couldn't add to reference list %s", array->name));
	}
}
#endif

__inline void reference_list_add(
	struct data_array *array,
	long *first_reference_index,
	long datum_index)
{
	long reference_index = datum_new(array);

	if (reference_index!=NONE)
	{
		struct data_reference *reference = (struct data_reference*)datum_get(array, reference_index);
		reference->datum_index = datum_index;
		reference->next_reference_index = *first_reference_index;
#ifdef HALO_LINUX
		/* the node is complete before a render walking the list can reach
		it (render_epoch.h) */
		__atomic_thread_fence(__ATOMIC_RELEASE);
		halo_epoch_datum_ready(array, reference_index & 0xFFFF);
#endif
		*first_reference_index = reference_index;
	}
	else
	{
		error(_error_silent, "WARNING: maximum %ss per map (%d) exceeded.", array->name, array->maximum_count);
	}

	return;
}

__inline void reference_list_delete(
	struct data_array *array,
	long first_reference_index)
{
	long i;
	struct data_reference *reference;

	for (i = first_reference_index; i!=NONE; i = reference->next_reference_index)
	{
		reference = (struct data_reference*)datum_get(array, i);
		datum_delete(array, i);
	}

	return;
}

#endif // __REFERENCE_LISTS_H
