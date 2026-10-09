/*
CLUSTER_PARTITIONS.C

symbols in this file:
00180C10 0080:
	_reference_list_remove (0000)
00180C90 00c0:
	_reference_list_copy (0000)
00180D50 00d0:
	_cluster_partition_new (0000)
00180E20 0030:
	_cluster_partition_make_valid (0000)
00180E50 0030:
	_cluster_partition_make_invalid (0000)
00180E80 0030:
	_cluster_partition_delete (0000)
00180EB0 0030:
	_cluster_partition_get_next_datum (0000)
00180EE0 0040:
	_cluster_partition_get_first_cluster (0000)
00180F20 0030:
	_cluster_partition_get_next_cluster (0000)
00180F50 0050:
	_cluster_partition_copy (0000)
00180FA0 0050:
	_code_00180fa0 (0000)
00180FF0 0200:
	_cluster_partition_reconnect (0000)
001811F0 00b0:
	_cluster_partition_disconnect (0000)
001812A0 0080:
	_cluster_partition_get_first_datum (0000)
002A0A94 003a:
	??_C@_0DK@NMKBPPFN@attempt?5to?5remove?5invalid?5elemen@ (0000)
002A0AD0 001d:
	??_C@_0BN@NFEHOLIC@?4?4?2objects?2reference_lists?4h?$AA@ (0000)
002A0AF0 002d:
	??_C@_0CN@HMHOKAKO@result?9?$DOmaximum_count?$DN?$DNsource?9?$DOm@ (0000)
002A0B20 001b:
	??_C@_0BL@LCCDAOLG@result?9?$DOsize?$DN?$DNsource?9?$DOsize?$AA@ (0000)
002A0B3C 002f:
	??_C@_0CP@NPCAPMLK@couldn?8t?5allocate?5?$CFs?5cluster?5par@ (0000)
002A0B6C 000b:
	??_C@_0L@LMJLHDGO@?$CFs?5cluster?$AA@ (0000)
002A0B78 000b:
	??_C@_0L@FHNEJCED@cluster?5?$CFs?$AA@ (0000)
002A0B84 0013:
	??_C@_0BD@HGMPGNNE@cluster?5references?$AA@ (0000)
002A0B98 004d:
	??_C@_0EN@JKAMLONC@cluster_index?$DO?$DN0?5?$CG?$CG?5cluster_inde@ (0000)
002A0BE8 002f:
	??_C@_0CP@DHFJNEOJ@c?3?2halo?2SOURCE?2structures?2cluste@ (0000)
002A0C18 0028:
	??_C@_0CI@IEBPGENP@an?5object?5or?5light?5spanned?5?$CFd?5cl@ (0000)
002A0C40 001f:
	??_C@_0BP@ELGEMBFB@?$CKfirst_cluster_reference?$DN?$DNNONE?$AA@ (0000)
002A0C60 0018:
	??_C@_0BI@INKNBGDF@first_cluster_reference?$AA@ (0000)
002A0C78 000a:
	??_C@_09IKAEIPAD@partition?$AA@ (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "objects/reference_lists.h"
#include "saved games/game_state.h"
#include "scenario/scenario.h"
#include "cluster_partitions.h"
#include "structure_bsp_definitions.h"
#include "structures/structures.h"
#ifdef HALO_LINUX
/* (HALO_TICK_PROFILE=2) objects_update's phases: objects_phases.h */
#include "objects_phases.h"
#else
#define HALO_OBJECTS_PHASE_PUSH(phase) ((void)0)
#define HALO_OBJECTS_PHASE_POP() ((void)0)
#endif

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

void reference_list_remove(
	struct data_array *array,
	long *first_reference_index,
	long datum_index);
void reference_list_copy(
	struct data_array *result,
	struct data_array *source);

static long *code_00180fa0(
	struct cluster_partition *partition,
	short cluster_index);

/* ---------- globals */

/* ---------- public code */

#ifdef HALO_LINUX
#include "render_epoch.h"
void platform_log(const char *format, ...);
/* reference_list_get_next_datum_index, ending the walk at a node the caller
cannot get instead of loading through NULL: a node freed under the walk
outside the epoch (the render thread deleted lights' nodes while the tick
walked them: object_lights.c, lights_update_unattached) cost a data abort.
The cause is fixed; this keeps a stray one to a light, object or cluster
missed for a frame, and names it. */
static long reference_list_walk_next(
	struct data_array *array,
	long *reference_index)
{
	struct data_reference *reference;

	if (*reference_index == NONE)
		return NONE;
	reference = (struct data_reference *)datum_get(array, *reference_index);
	if (!reference)
	{
		static unsigned long missed;

		if (missed++ < 8)
			platform_log("%s: walk met node 0x%08lx it cannot get (count %d, %s thread): walk ended",
				array->name, (unsigned long)*reference_index, (int)array->count,
				halo_epoch_on_mutator() ? "tick" : "render");
		*reference_index = NONE;
		return NONE;
	}
	*reference_index = reference->next_reference_index;
	return reference->datum_index;
}

/* the next datum of the walk that the caller may see: a render skips
datums the tick is still constructing this epoch (render_epoch.h) */
static long cluster_partition_next_ready_datum(
	struct cluster_partition const *partition,
	long *reference_index)
{
	long datum_index = reference_list_walk_next(partition->data_reference_data, reference_index);

	while (datum_index != NONE && partition->datum_data && !halo_epoch_on_mutator() &&
		halo_epoch_datum_state(partition->datum_data, datum_index & 0xFFFF) == _halo_epoch_datum_created)
	{
		datum_index = reference_list_walk_next(partition->data_reference_data, reference_index);
	}
	return datum_index;
}
#endif

#ifdef HALO_LINUX
/* (port) where reference_list_add_last appends to a cluster's list of datums,
without walking it: each list's last node, remembered by the address of its
head (a slot per head, by hash; a miss walks as before). A node stays in the
list it was added to until reference_list_remove takes it out, which moves
its list's entry to the node before it when it was the last (or forgets the
entry); a node that is found again with its identifier and no next node is
still that list's last. The whole table is forgotten when the lists
are rebuilt (cluster_partition_make_valid, _copy) or the game state is
replaced (halo_map_generation). The lights' and objects' reconnects walked
their clusters' lists a node at a time, every tick they moved (~95 lights a
tick in b30's beach fight). */
#define LIST_TAIL_SLOTS 1024
static struct
{
	long *head;
	long tail;
	unsigned long generation;
	unsigned long map_generation;
} list_tails[LIST_TAIL_SLOTS];
static unsigned long list_tail_generation = 1;

/* (port) A cluster's list of datums as cluster_partition_get_cluster_datums
last walked it, kept until any list of any partition changes (a connect, a
disconnect, a partition made again or copied: cluster_datums_changes) or the
game state is replaced (halo_map_generation). A vehicle's mass points test
the clusters it is in again and again between two moves: Covenant V Marines'
Banshee (22 mass points) has its ground planes, antigravity probes and up to
four passes against penetration ask for the same few clusters some 110 times a
tick, each walk a reference at a time through the cluster's list (a cache
miss each on the Vita, ~60 objects a list in its fight). The tick's own
(the render's walks are left alone); the copy is the list in its order. */
#define CLUSTER_DATUMS_CACHE_ENTRIES 16
#define CLUSTER_DATUMS_CACHE_LENGTH 256
static struct
{
	struct cluster_partition const *partition;
	short cluster_index;
	unsigned long changes;
	unsigned long map_generation;
	long count;
	long datums[CLUSTER_DATUMS_CACHE_LENGTH];
} cluster_datums_cache[CLUSTER_DATUMS_CACHE_ENTRIES];
static unsigned long cluster_datums_changes = 1;
static unsigned long cluster_datums_cache_next;

static unsigned long list_tail_slot(long *head)
{
	return (unsigned long)((((unsigned long)(size_t)head) >> 2) * 2654435761UL) % LIST_TAIL_SLOTS;
}

static void list_tail_forget(long *head)
{
	unsigned long slot = list_tail_slot(head);

	if (list_tails[slot].head == head)
		list_tails[slot].head = NULL;
}

static void cluster_list_add_last(
	struct data_array *array,
	long *first_reference_index,
	long datum_index)
{
	unsigned long slot = list_tail_slot(first_reference_index);
	long reference_index = datum_new(array);

	if (reference_index != NONE)
	{
		struct data_reference *reference = (struct data_reference *)datum_get(array, reference_index);
		long *link = NULL;

		reference->datum_index = datum_index;
		reference->next_reference_index = NONE;
		if (list_tails[slot].head == first_reference_index &&
			list_tails[slot].generation == list_tail_generation &&
			list_tails[slot].map_generation == halo_map_generation &&
			*first_reference_index != NONE)
		{
			struct data_reference *last = (struct data_reference *)datum_try_and_get(array, list_tails[slot].tail);

			if (last && last != reference && last->next_reference_index == NONE)
				link = &last->next_reference_index;
		}
		if (!link)
		{
			link = first_reference_index;
			while (*link != NONE)
			{
				struct data_reference *last = (struct data_reference *)datum_get(array, *link);

				if (!last)
					break;
				link = &last->next_reference_index;
			}
		}
		__atomic_thread_fence(__ATOMIC_RELEASE);
		halo_epoch_datum_ready(array, reference_index & 0xFFFF);
		*link = reference_index;
		list_tails[slot].head = first_reference_index;
		list_tails[slot].tail = reference_index;
		list_tails[slot].generation = list_tail_generation;
		list_tails[slot].map_generation = halo_map_generation;
	}
	else
	{
		match_vassert("..\\objects\\reference_lists.h", 0x5b, FALSE,
			csprintf(temporary, "couldn't add to reference list %s", array->name));
	}
}
#endif

void reference_list_remove(
	struct data_array *array,
	long *first_reference_index,
	long datum_index)
{
	long *reference_index = first_reference_index;
	struct data_reference *reference;
#ifdef HALO_LINUX
	/* (the list's remembered last node, kept: the node before a last node
	taken out becomes the last) */
	long previous_index = NONE;
#endif

	while (*reference_index != NONE)
	{
		reference = (struct data_reference *)datum_get(array, *reference_index);
		if (reference->datum_index == datum_index)
		{
#ifdef HALO_LINUX
			unsigned long slot = list_tail_slot(first_reference_index);

			if (list_tails[slot].head == first_reference_index && list_tails[slot].tail == *reference_index)
			{
				if (previous_index != NONE && reference->next_reference_index == NONE)
					list_tails[slot].tail = previous_index;
				else
					list_tails[slot].head = NULL;
			}
#endif
			datum_delete(array, *reference_index);
			*reference_index = reference->next_reference_index;

			return;
		}

#ifdef HALO_LINUX
		previous_index = *reference_index;
#endif
		reference_index = &reference->next_reference_index;
	}
#ifdef HALO_LINUX
	list_tail_forget(first_reference_index);
#endif

	match_vassert(
		"..\\objects\\reference_lists.h",
		0x6d,
		FALSE,
		csprintf(temporary, "attempt to remove invalid element %ld from reference list", datum_index));

	return;
}

/* The initialization and cursor-advance order preserve January's coalescing
 * of the source cursor into EBX and the loop index into ESI. */
void reference_list_copy(
	struct data_array *result,
	struct data_array *source)
{
	short absolute_index;
	struct data_reference *source_reference;
	struct data_reference *result_reference;

	match_assert("..\\objects\\reference_lists.h", 0x88, result->size==source->size);
	match_assert("..\\objects\\reference_lists.h", 0x89, result->maximum_count==source->maximum_count);
	absolute_index = 0;
	result_reference = result->data;
	source_reference = source->data;
	while (absolute_index < result->maximum_count)
	{
		if (source_reference->identifier)
		{
			*result_reference = *source_reference;
		}
		else if (result_reference->identifier)
		{
			datum_delete(result, absolute_index);
		}

		absolute_index++;
		source_reference++;
		result_reference++;
	}

	return;
}

void cluster_partition_new(
	struct cluster_partition *partition,
	char const *name)
{
	char cluster_name[256];

#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif

	partition->cluster_first_data_references = game_state_malloc(
		name,
		"cluster references",
		MAXIMUM_CLUSTERS_PER_STRUCTURE * sizeof(*partition->cluster_first_data_references));

	sprintf(cluster_name, "cluster %s", name);
#ifdef HALO_LINUX
	/* the native builds' longer reference lists (halo_port_capacity.h): an
	object or light that cannot be referenced drops out of its clusters */
	partition->data_reference_data = reference_list_new(cluster_name, HALO_PORT_MAXIMUM_CLUSTER_REFERENCES);
#else
	partition->data_reference_data = reference_list_new(cluster_name, 2048);
#endif

	sprintf(cluster_name, "%s cluster", name);
#ifdef HALO_LINUX
	partition->cluster_reference_data = reference_list_new(cluster_name, HALO_PORT_MAXIMUM_CLUSTER_REFERENCES);
#else
	partition->cluster_reference_data = reference_list_new(cluster_name, 2048);
#endif

	if (!partition->cluster_first_data_references ||
		!partition->cluster_reference_data ||
		!partition->data_reference_data)
	{
		error(_error_immediate, "couldn't allocate %s cluster partition globals", name);
	}

	return;
}

void cluster_partition_make_valid(
	struct cluster_partition *partition)
{
#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif
#ifdef HALO_LINUX
	list_tail_generation++;
#endif
	csmemset(
		partition->cluster_first_data_references,
		NONE,
		MAXIMUM_CLUSTERS_PER_STRUCTURE * sizeof(*partition->cluster_first_data_references));
	data_make_valid(partition->cluster_reference_data);
	data_make_valid(partition->data_reference_data);

	return;
}

void cluster_partition_make_invalid(
	struct cluster_partition *partition)
{
#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif
	if (partition->cluster_reference_data->valid)
		data_make_invalid(partition->cluster_reference_data);

	if (partition->data_reference_data->valid)
		data_make_invalid(partition->data_reference_data);

	return;
}

void cluster_partition_delete(
	struct cluster_partition *partition)
{
#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif
	if (partition->cluster_first_data_references)
		partition->cluster_first_data_references = NULL;

	if (partition->cluster_reference_data)
		partition->cluster_reference_data = NULL;

	if (partition->data_reference_data)
		partition->data_reference_data = NULL;

	return;
}

void cluster_partition_copy(
	struct cluster_partition *result,
	struct cluster_partition const *source)
{
#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif
#ifdef HALO_LINUX
	list_tail_generation++;
#endif
	/* port (from OpenCE, MrBruh's "Harden map and network input"): no
	more clusters than the partitions hold (a map's bsp may say it has
	more) */
	csmemcpy(
		result->cluster_first_data_references,
		source->cluster_first_data_references,
		PIN(global_structure_bsp_get()->clusters.count, 0, MAXIMUM_CLUSTERS_PER_STRUCTURE) *
			sizeof(*result->cluster_first_data_references));
	reference_list_copy(
		result->cluster_reference_data,
		source->cluster_reference_data);
	reference_list_copy(
		result->data_reference_data,
		source->data_reference_data);

	return;
}

long cluster_partition_get_next_datum(
	struct cluster_partition const *partition,
	long *reference_index)
{
#ifdef HALO_LINUX
	return cluster_partition_next_ready_datum(partition, reference_index);
#else
	return reference_list_get_next_datum_index(partition->data_reference_data, reference_index);
#endif
}

#ifdef HALO_LINUX
/* (port) the walk cluster_partition_get_first_datum and _get_next_datum make
of a cluster's datums, all of it into indices, in order: the tick's
collision queries walk ~6000 objects a tick (b30's beach fight), and a call
or two per object, each asking which thread it is on, cost more than the
tests most of them fail. A render walk that must skip the tick's new datums,
and a list longer than maximum, are left to those (NONE). */
unsigned long cluster_partition_changes(
	void)
{
	return cluster_datums_changes;
}

long cluster_partition_get_cluster_datums(
	struct cluster_partition const *partition,
	short cluster_index,
	long *indices,
	long maximum)
{
	long reference_index;
	long count = 0;
	boolean cached = !halo_epoch_threaded || halo_epoch_on_mutator();
	unsigned long slot;

	/* (the render's skip of the tick's new datums: those exist only while a
	tick's epoch is open, until its join) */
	if (partition->datum_data && halo_epoch_active && !halo_epoch_on_mutator())
		return NONE;
	if (cached)
	{
		for (slot = 0; slot < CLUSTER_DATUMS_CACHE_ENTRIES; slot++)
		{
			if (cluster_datums_cache[slot].partition == partition &&
				cluster_datums_cache[slot].cluster_index == cluster_index &&
				cluster_datums_cache[slot].changes == cluster_datums_changes &&
				cluster_datums_cache[slot].map_generation == halo_map_generation)
			{
				if (cluster_datums_cache[slot].count > maximum)
					return NONE;
				csmemcpy(indices, cluster_datums_cache[slot].datums, cluster_datums_cache[slot].count * sizeof(*indices));
				return cluster_datums_cache[slot].count;
			}
		}
	}
	reference_index = *code_00180fa0((struct cluster_partition *)partition, cluster_index);
	while (reference_index != NONE)
	{
		long datum_index = reference_list_walk_next(partition->data_reference_data, &reference_index);

		if (datum_index == NONE)
			break;
		if (count >= maximum)
			return NONE;
		indices[count++] = datum_index;
	}
	if (cached && count <= CLUSTER_DATUMS_CACHE_LENGTH)
	{
		slot = cluster_datums_cache_next++ % CLUSTER_DATUMS_CACHE_ENTRIES;
		cluster_datums_cache[slot].partition = partition;
		cluster_datums_cache[slot].cluster_index = cluster_index;
		cluster_datums_cache[slot].changes = cluster_datums_changes;
		cluster_datums_cache[slot].map_generation = halo_map_generation;
		cluster_datums_cache[slot].count = count;
		csmemcpy(cluster_datums_cache[slot].datums, indices, count * sizeof(*indices));
	}
	return count;
}
#endif

long cluster_partition_get_first_cluster(
	struct cluster_partition const *partition,
	long *reference_index,
	long first_cluster_reference)
{
	*reference_index = first_cluster_reference;

#ifdef HALO_LINUX
	return reference_list_walk_next(partition->cluster_reference_data, reference_index);
#else
	return reference_list_get_next_datum_index(partition->cluster_reference_data, reference_index);
#endif
}

long cluster_partition_get_next_cluster(
	struct cluster_partition const *partition,
	long *reference_index)
{
#ifdef HALO_LINUX
	return reference_list_walk_next(partition->cluster_reference_data, reference_index);
#else
	return reference_list_get_next_datum_index(partition->cluster_reference_data, reference_index);
#endif
}

void cluster_partition_reconnect(
	struct cluster_partition *partition,
	long datum_index,
	long *first_cluster_reference,
	real_point3d const *position,
	float radius,
	struct location const *location)
{
	HALO_OBJECTS_PHASE_PUSH(_objects_phase_map);
	short cluster_indices[64];
	short cluster_count;
	short cluster_index_index;

#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif

	match_assert("c:\\halo\\SOURCE\\structures\\cluster_partitions.c", 0x6f, partition);
	match_assert("c:\\halo\\SOURCE\\structures\\cluster_partitions.c", 0x70, first_cluster_reference);
	match_assert("c:\\halo\\SOURCE\\structures\\cluster_partitions.c", 0x71, *first_cluster_reference==NONE);
	match_assert("c:\\halo\\SOURCE\\structures\\cluster_partitions.c", 0x72, position);
	match_assert("c:\\halo\\SOURCE\\structures\\cluster_partitions.c", 0x73, location);

	cluster_count = structure_clusters_in_sphere(
		location->cluster_index,
		position,
		radius,
		NUMBEROF(cluster_indices),
		cluster_indices);

	if (cluster_count > 64)
	{
		error(_error_silent, "an object or light spanned %d clusters.", cluster_count);
		cluster_count = NUMBEROF(cluster_indices);
	}

	for (cluster_index_index = 0; cluster_index_index < cluster_count; cluster_index_index++)
	{
		short const cluster_index = cluster_indices[cluster_index_index];

		reference_list_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index);

#ifdef HALO_LINUX
		cluster_list_add_last(
			partition->data_reference_data,
			code_00180fa0(partition, cluster_index),
			datum_index);
#else
		reference_list_add(
			partition->data_reference_data,
			code_00180fa0(partition, cluster_index),
			datum_index);
#endif
	}

	HALO_OBJECTS_PHASE_POP();
	return;
}

void cluster_partition_disconnect(
	struct cluster_partition *partition,
	long datum_index,
	long *first_cluster_reference)
{
	HALO_OBJECTS_PHASE_PUSH(_objects_phase_map);
	long cluster_reference_index = *first_cluster_reference;

#ifdef HALO_LINUX
	cluster_datums_changes++;
#endif

	while (cluster_reference_index != NONE)
	{
		struct data_reference *cluster_reference = (struct data_reference *)datum_get(
			partition->cluster_reference_data,
			cluster_reference_index);
#ifdef HALO_LINUX
		if (!cluster_reference)
		{
			/* (port) the chain names a node the tick cannot see: which
			state it is in tells whether the chain was walked twice this
			epoch or the node vanished (render_epoch.h) */
			struct datum_header *raw = (struct datum_header *)((unsigned char *)partition->cluster_reference_data->data +
				partition->cluster_reference_data->size * (cluster_reference_index & 0xFFFF));
			platform_log("cluster partition %s: disconnecting datum 0x%08lx met node 0x%08lx (slot identifier 0x%04x, epoch state %d, head 0x%08lx): crashing for the dump",
				partition->cluster_reference_data->name, (unsigned long)datum_index, (unsigned long)cluster_reference_index,
				(unsigned)(unsigned short)raw->identifier, halo_epoch_datum_state(partition->cluster_reference_data, cluster_reference_index & 0xFFFF),
				(unsigned long)*first_cluster_reference);
			*(volatile int *)88 = 0;
		}
#endif
		short const cluster_index = (short)cluster_reference->datum_index;

		datum_delete(partition->cluster_reference_data, cluster_reference_index);

		reference_list_remove(
			partition->data_reference_data,
			code_00180fa0(partition, cluster_index),
			datum_index);

		cluster_reference_index = cluster_reference->next_reference_index;
	}

	*first_cluster_reference = NONE;

	HALO_OBJECTS_PHASE_POP();
	return;
}

#ifdef HALO_LINUX
/* (port) cluster_partition_get_first_datum and _get_next_datum's walk, a
batch at a time: the same references read in the same order, each datum
passed over as cluster_partition_next_ready_datum would (a render's walk
skips the datums the tick is still constructing), with which thread this is
asked once for the batch rather than for each datum (see the header) */
long cluster_partition_walk_datums(
	struct cluster_partition const *partition,
	short cluster_index,
	long *reference_index,
	long *indices,
	long *references_after,
	long maximum)
{
	boolean skip_constructing = partition->datum_data && !halo_epoch_on_mutator();
	long count = 0;

	if (*reference_index == NONE && cluster_index != NONE)
	{
		*reference_index = *code_00180fa0((struct cluster_partition *)partition, cluster_index);
	}
	while (count < maximum)
	{
		long datum_index = reference_list_walk_next(partition->data_reference_data, reference_index);

		while (datum_index != NONE && skip_constructing &&
			halo_epoch_datum_state(partition->datum_data, datum_index & 0xFFFF) == _halo_epoch_datum_created)
		{
			datum_index = reference_list_walk_next(partition->data_reference_data, reference_index);
		}
		if (datum_index == NONE)
		{
			break;
		}
		indices[count] = datum_index;
		references_after[count] = *reference_index;
		count++;
	}
	return count;
}
#endif

long cluster_partition_get_first_datum(
	struct cluster_partition const *partition,
	long *reference_index,
	short cluster_index)
{
	*reference_index = *code_00180fa0((struct cluster_partition *)partition, cluster_index);

#ifdef HALO_LINUX
	return cluster_partition_next_ready_datum(partition, reference_index);
#else
	return reference_list_get_next_datum_index(partition->data_reference_data, reference_index);
#endif
}

/* ---------- private code */

static long *code_00180fa0(
	struct cluster_partition *partition,
	short cluster_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\structures\\cluster_partitions.c",
		0xd5,
		cluster_index>=0 && cluster_index<global_structure_bsp_get()->clusters.count);

	/* port (from OpenCE, MrBruh's "Harden map and network input"): a
	cluster index that fits no cluster's list has an empty list (what is
	put in it is not kept), not one past the lists */
	if (cluster_index < 0 || cluster_index >= MAXIMUM_CLUSTERS_PER_STRUCTURE)
	{
		static long no_cluster_first_reference;

		no_cluster_first_reference = NONE;
		return &no_cluster_first_reference;
	}

	return &partition->cluster_first_data_references[cluster_index];
}
