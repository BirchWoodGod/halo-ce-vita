/*
DEVICE_MACHINES.H
*/

#ifndef __DEVICE_MACHINES_H
#define __DEVICE_MACHINES_H
#pragma once

/* ---------- headers */

#include "device_definitions.h"
#include "devices.h"
#include "scenario/scenario_definitions.h"

/* ---------- macros */

#define machine_get(index) ((struct machine_datum *)object_get_and_verify_type((index), _object_mask_machine))
#define machine_try_and_get(index) ((struct machine_datum *)object_try_and_get_and_verify_type((index), _object_mask_machine))

/* ---------- structures */

struct scenario_machine_datum
{
	struct scenario_object_datum object;
	struct scenario_device_datum device;
	byte flags;
	byte pad[3];
};

/* ---------- prototypes/DEVICE_MACHINES.C */

void machines_initialize(
	void);
void machines_dispose(
	void);
void machines_initialize_for_new_map(
	void);
void machines_dispose_from_old_map(
	void);
void machine_place(
	long machine_index,
	struct scenario_machine_datum *scenario_machine);
boolean machine_new(
	long machine_index);
void machine_delete(
	long machine_index);
void machine_bumped(
	long machine_index,
	long biped_index);
void machine_try_to_open_with_damage(
	long machine_index);
boolean machine_update(
	long machine_index);

#ifdef HALO_LINUX
/* port: an elevator's riders carried by the same tick's movement, after
the machine's nodes are recomputed (OpenCE's network version 30, which
every machine of a crossplay game of 32 plays: network_wire_v30_rules;
docs/opence-net32-spec.md 2), around a machine's update in object_update
(objects.c): begin, before its type's update, notes where its elevator node
is (FALSE: no carry this way - not an elevator, a node it does not have, or
a game that plays the Xbox's carry, a tick late, in machine_update); end,
once its nodes are recomputed, moves its riders by how far that node went */
boolean machine_elevator_carry_begin(
	long machine_index,
	real_point3d *before);
void machine_elevator_carry_end(
	long machine_index,
	real_point3d const *before);
#ifndef HALO_RELEASE
/* (automated tests only, after each tick: HALO_TEST_ELEVATOR_RIDE) */
void machine_elevator_test_tick(
	void);
#endif
#endif

#endif // __DEVICE_MACHINES_H
