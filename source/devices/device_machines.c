/*
DEVICE_MACHINES.C

symbols in this file:
00084100 0010:
	_machines_initialize (0000)
00084110 0010:
	_machines_dispose (0000)
00084120 0010:
	_machines_initialize_for_new_map (0000)
00084130 0010:
	_machines_dispose_from_old_map (0000)
00084140 0080:
	_machine_place (0000)
000841C0 0080:
	_machine_new (0000)
00084240 0010:
	_machine_delete (0000)
00084250 0030:
	_machine_bumped (0000)
00084280 0050:
	_machine_try_to_open_with_damage (0000)
000842D0 0400:
	_machine_update (0000)
*/

/* ---------- headers */

#include "cseries.h"

#include "device_machines.h"
#include "game/game.h"
#include "memory/data.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#ifdef HALO_LINUX
#include "game/players.h"
#include "network_wire.h"
#include <stdlib.h>
#include <string.h>
#endif
#ifdef HALO_LINUX
/* port/linux/game/network_coop.c: a group's value changed (the co-op host lists the groups again) */
void network_coop_note_device_group(short group_index);
#endif

/* ---------- constants */

enum
{
	_machine_type_door = 0,
	_machine_type_platform = 2,
};

enum
{
	_device_position_changed_bit = 2,
};

enum
{
	_object_function2_active_bit = 2,
	_object_runtime_unit_cannot_open_doors_automatically_bit = 14,
};

/* ---------- macros */

/* ---------- structures */

struct device_group_datum
{
	short identifier;
	word pad;
	real actual_value;
};

/* ---------- prototypes */

boolean game_team_is_enemy(
	short team_index0,
	short team_index1);
/* ---------- globals */

/* ---------- public code */

void machines_initialize(
	void)
{
	return;
}

void machines_dispose(
	void)
{
	return;
}

void machines_initialize_for_new_map(
	void)
{
	return;
}

void machines_dispose_from_old_map(
	void)
{
	return;
}

void machine_place(
	long machine_index,
	struct scenario_machine_datum *scenario_machine)
{
	struct machine_datum *machine = machine_get(machine_index);

	device_add_scenario_information(machine_index, &scenario_machine->device);
	if (TEST_FLAG(scenario_machine->flags, _machine_does_not_operate_automatically_bit))
	{
		machine->machine.flags |= FLAG(_machine_does_not_operate_automatically_bit);
	}
	if (TEST_FLAG(scenario_machine->flags, _machine_one_sided_bit))
	{
		machine->machine.flags |= FLAG(_machine_one_sided_bit);
	}
	if (TEST_FLAG(scenario_machine->flags, _machine_never_appears_locked_bit))
	{
		machine->machine.flags |= FLAG(_machine_never_appears_locked_bit);
	}
	if (TEST_FLAG(scenario_machine->flags, _machine_opened_by_melee_attack_bit))
	{
		machine->machine.flags |= FLAG(_machine_opened_by_melee_attack_bit);
	}

	return;
}

boolean machine_new(
	long machine_index)
{
	struct machine_datum *machine = machine_get(machine_index);
	struct machine_definition *definition = machine_definition_get(machine->definition_index);

	machine->object.flags |= FLAG(13);
	if (TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit))
	{
		machine->object.flags |= FLAG(14);
	}
	else
	{
		machine->object.flags &= ~FLAG(14);
	}

	if (TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit))
	{
		machine->object.flags |= FLAG(15);
	}
	else
	{
		machine->object.flags &= ~FLAG(15);
	}

	return TRUE;
}

void machine_delete(
	long machine_index)
{
	return;
}

void machine_bumped(
	long machine_index,
	long biped_index)
{
	struct machine_datum *machine = machine_get(machine_index);

	machine_definition_get(machine->definition_index);

	return;
}

void machine_try_to_open_with_damage(
	long machine_index)
{
	struct machine_datum *machine = machine_get(machine_index);

	machine_definition_get(machine->definition_index);
	if (TEST_FLAG(machine->machine.flags, _machine_opened_by_melee_attack_bit))
	{
		device_set_actual_position(machine_index, 1.0f);
	}

	return;
}

#ifdef HALO_LINUX
/* (port) whether this machine's game carries an elevator's riders by the
same tick's movement (OpenCE's 30: network_wire_v30_rules); in test builds
also HALO_TEST_ELEVATOR_CARRY=same (a single player or legacy game played
so, for the carry's tests), =xbox the other way */
static boolean machine_elevator_same_tick_carry(
	void)
{
#ifndef HALO_RELEASE
	static int forced = -1;

	if (forced < 0)
	{
		char const *value = getenv("HALO_TEST_ELEVATOR_CARRY");

		forced = value && !strcmp(value, "same") ? 1 : value && !strcmp(value, "xbox") ? 0 : 2;
	}
	if (forced != 2)
		return forced == 1;
#endif
	return network_wire_v30_rules() != 0;
}

/* (port) the elevator node's matrix of an elevator machine whose model has
that node, else NULL */
static real_matrix4x3 *machine_elevator_node_matrix(
	long machine_index)
{
	struct machine_datum *machine = machine_get(machine_index);
	struct machine_definition *definition = machine_definition_get(machine->definition_index);
	short node_index = definition->machine.elevator_node_index;

	if (!TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) || node_index == NONE || node_index < 0 ||
		node_index >= machine->object.node_matrices.size / (short)sizeof(real_matrix4x3))
	{
		return NULL;
	}
	return object_get_node_matrix(machine_index, node_index);
}

boolean machine_elevator_carry_begin(
	long machine_index,
	real_point3d *before)
{
	real_matrix4x3 *node_matrix;

	if (!machine_elevator_same_tick_carry())
		return FALSE;
	node_matrix = machine_elevator_node_matrix(machine_index);
	if (!node_matrix)
		return FALSE;
	*before = node_matrix->position;
	return TRUE;
}

void machine_elevator_carry_end(
	long machine_index,
	real_point3d const *before)
{
	/* (as the Xbox's carry's query: every biped in the machine's sphere) */
	static long rider_indices[2048];
	struct machine_datum *machine = machine_get(machine_index);
	real_matrix4x3 *node_matrix = machine_elevator_node_matrix(machine_index);
	real_vector3d offset;
	short count;
	short index;

	if (!node_matrix)
		return;
	offset.i = node_matrix->position.x - before->x;
	offset.j = node_matrix->position.y - before->y;
	offset.k = node_matrix->position.z - before->z;
	if (offset.i != 0.0f || offset.j != 0.0f || offset.k != 0.0f)
	{
		count = objects_in_sphere(
			_object_mask_biped,
			_object_mask_biped,
			&machine->object.location,
			&machine->object.bounding_sphere_center,
			machine->object.bounding_sphere_radius,
			rider_indices,
			NUMBEROF(rider_indices));
		for (index = 0; index < count; index++)
		{
			long biped_index = rider_indices[index];
			struct biped_datum *biped = biped_get(biped_index);

			if (biped->biped.elevator_object_index == machine_index)
			{
				real_point3d new_position;

				add_vectors3d(&offset, (real_vector3d const *)&biped->object.position, (real_vector3d *)&new_position);
				object_translate(biped_index, &new_position, NULL);
			}
		}
	}
	/* (kept as the Xbox's carry keeps it, though this one does not read it) */
	machine->machine.elevator_position = node_matrix->position;
}
#endif

#if defined(HALO_LINUX) && !defined(HALO_RELEASE)
#include "cseries/errors.h"
/* devices.c's, cinematics.c's */
boolean device_set_desired_position(long device_index, real position);
void device_set_power(long device_index, real power);
boolean cinematic_in_progress(void);

/* (automated tests only) HALO_TEST_ELEVATOR_RIDE=n: in a game of one
machine, 3 s in, the n-th elevator machine of the map (each logged) gets
the first local player's unit on its platform; 2 s later it goes to its
other end, back half way, over again (a reversal in its stride), and down
to its end once more; every tick, where its elevator node and the rider
are (run_elevator_test.sh compares the two: carried the same tick, the
rider's height over the node holds while it moves) */
void machine_elevator_test_tick(
	void)
{
	static int setting = -1;
	static boolean listed;
	static long chosen = NONE;
	static long phase_time;
	static short phase;
	static short legs;
	static real target;
	long player_index;
	struct player_datum *player;
	struct machine_datum *machine;
	real_matrix4x3 *node_matrix;
	struct biped_datum *rider;

	if (setting < 0)
	{
		char const *value = getenv("HALO_TEST_ELEVATOR_RIDE");

		setting = value ? atoi(value) : 0;
	}
	if (setting <= 0 || !game_in_progress() || game_time_get() < 3 * TICKS_PER_SECOND)
		return;
	/* (a level's opening cinematic over: the player its own) */
	if (!listed && cinematic_in_progress())
		return;
	player_index = local_player_get_player_index(0);
	player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	if (!player || player->unit_index == NONE)
		return;
	if (!listed)
	{
		struct object_iterator iterator;
		struct machine_datum *each;
		int count = 0;

		listed = TRUE;
		object_iterator_new(&iterator, _object_mask_machine, 0);
		while ((each = (struct machine_datum *)object_iterator_next(&iterator)) != NULL)
		{
			real_matrix4x3 *matrix = machine_elevator_node_matrix(iterator.index);

			if (!matrix)
				continue;
			count++;
			error(_error_silent, "elevator test: elevator %d: machine %08lx at (%.2f %.2f %.2f) position %.3f group %d", count,
				(unsigned long)iterator.index, matrix->position.x, matrix->position.y, matrix->position.z,
				each->device.position, (int)each->device.position_group_index);
			if (count == setting && each->device.position_group_index != NONE)
				chosen = iterator.index;
		}
		error(_error_silent, "elevator test: %d elevators, riding %s (%s carry)", count, chosen != NONE ? "one" : "none",
			machine_elevator_same_tick_carry() ? "the same tick's" : "the Xbox's");
		phase_time = game_time_get();
	}
	if (chosen == NONE || phase >= 4)
		return;
	machine = (struct machine_datum *)object_try_and_get_and_verify_type(chosen, _object_mask_machine);
	node_matrix = machine ? machine_elevator_node_matrix(chosen) : NULL;
	rider = (struct biped_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_biped);
	if (!machine || !node_matrix || !rider)
		return;
	switch (phase)
	{
	case 0:
	{
		real_point3d position = node_matrix->position;

		position.z += 1.0f;
		object_translate(player->unit_index, &position, NULL);
		phase = 1;
		phase_time = game_time_get();
		break;
	}
	case 1:
		/* (a level's script that puts the player somewhere once its
		cinematic is over: put back, until it stays) */
		{
			real dx = rider->object.position.x - node_matrix->position.x;
			real dy = rider->object.position.y - node_matrix->position.y;
			real dz = rider->object.position.z - node_matrix->position.z;

			if (dx * dx + dy * dy + dz * dz > 9.0f)
			{
				phase = 0;
				break;
			}
		}
		if (game_time_get() - phase_time >= 2 * TICKS_PER_SECOND)
		{
			target = machine->device.position < 0.5f ? 1.0f : 0.0f;
			device_set_power(chosen, 1.0f);
			device_set_desired_position(chosen, target);
			phase = 2;
			legs = 1;
			phase_time = game_time_get();
		}
		break;
	case 2:
		/* (the second leg turned back half way: a reversal) */
		if (legs == 2 && game_time_get() - phase_time >= TICKS_PER_SECOND &&
			machine->device.position - target < 0.5f && target - machine->device.position < 0.5f &&
			(machine->device.position - target > 0.2f || target - machine->device.position > 0.2f))
		{
			target = 1.0f - target;
			device_set_desired_position(chosen, target);
			legs++;
			phase_time = game_time_get();
			error(_error_silent, "elevator test: tick %ld reversed", game_time_get());
		}
		else if (machine->device.position == target && machine->device.position_velocity == 0.0f &&
			game_time_get() - phase_time >= TICKS_PER_SECOND)
		{
			if (legs >= 4)
			{
				phase = 4;
				error(_error_silent, "elevator test: done");
				break;
			}
			target = 1.0f - target;
			device_set_desired_position(chosen, target);
			legs++;
			phase_time = game_time_get();
		}
		break;
	}
	error(_error_silent, "elevator test: tick %ld leg %d node %.4f rider %.4f over %.4f velocity %.5f on %d ticks %d airborne %d",
		game_time_get(), (int)legs, node_matrix->position.z, rider->object.position.z,
		rider->object.position.z - node_matrix->position.z, machine->device.position_velocity,
		rider->biped.elevator_object_index == chosen, (int)rider->biped.elevator_ticks,
		TEST_FLAG(rider->biped.flags, _biped_airborne_bit) ? 1 : 0);
}
#endif

boolean machine_update(
	long machine_index)
{
	boolean activate;
	struct machine_definition *definition;
	long *object_index;
	real_point3d *node_position;
	long *elevator_object_index;
	short object_count;
	real_vector3d elevator_offset;
	real_point3d new_position;
	long object_indices[16];
	long elevator_object_indices[2048];
	struct machine_datum *machine = machine_get(machine_index);

	definition = machine_definition_get(machine->definition_index);
	if (definition->machine.type == _machine_type_platform)
	{
		machine->device.position +=
			(1.0f - machine->device.power) * definition->device.runtime_maximum_depowered_position_velocity +
			machine->device.power * definition->device.runtime_maximum_powered_position_velocity;
		if (machine->device.position >= 1.0f)
		{
			machine->device.position -= 1.0f;
		}
		machine->device.position_velocity = 0.0f;
		machine->device.flags |= FLAG(_device_position_changed_bit);
		if (machine->device.position_group_index != NONE)
		{
			struct device_group_datum *group = (struct device_group_datum *)datum_get(
				device_groups_data,
				machine->device.position_group_index);

			group->actual_value = machine->device.position;
#ifdef HALO_LINUX
			network_coop_note_device_group(machine->device.position_group_index);
#endif
		}
	}

	if (!TEST_FLAG(machine->machine.flags, _machine_does_not_operate_automatically_bit) &&
		definition->machine.type == _machine_type_door &&
		((game_time_get() + machine_index) & 3) == 0)
	{
		real automatic_activation_radius;

		activate = FALSE;
		automatic_activation_radius = definition->device.automatic_activation_radius < _real_epsilon
			? machine->object.bounding_sphere_radius
			: definition->device.automatic_activation_radius;
		object_count = objects_in_sphere(
			_object_mask_biped,
			_object_mask_biped,
			&machine->object.location,
			&machine->object.bounding_sphere_center,
			automatic_activation_radius,
			object_indices,
			NUMBEROF(object_indices));
		if (object_count > 0)
		{
			long remaining_object_count;

			object_index = object_indices;
			remaining_object_count = (unsigned short)object_count;
			do
			{
				struct unit_datum *unit = unit_get(*object_index);
				struct unit_definition *unit_definition = unit_definition_get(unit->definition_index);
				boolean can_open = TRUE;

				if (TEST_FLAG(*((byte *)unit + 0xb6), _object_function2_active_bit) ||
					TEST_FLAG(*(unsigned long *)((byte *)unit_definition + 0x17c), _object_runtime_unit_cannot_open_doors_automatically_bit))
				{
					can_open = FALSE;
				}

				if (!(TEST_FLAG(machine->machine.flags, _machine_one_sided_bit) &&
					machine->device.position == 0.0f &&
					!game_team_is_enemy(_game_team_player, unit->object.owner_team_index) &&
					dot_product3d(
						vector_from_points3d(
							&machine->object.bounding_sphere_center,
							&unit->object.bounding_sphere_center,
							&elevator_offset),
						&machine->object.forward) > 0.0f))
				{
					if (can_open)
					{
						activate = TRUE;
					}
				}

				object_index++;
			}
			while (--remaining_object_count);
		}

		if (activate)
		{
			if (machine->device.position_group_index != NONE)
			{
				device_group_set_desired_value(machine->device.position_group_index, 1.0f);
			}
			machine->machine.door_open_ticks = -3;
		}
	}

	if (definition->machine.type == _machine_type_door)
	{
		if (machine->device.position == 1.0f)
		{
			machine->machine.door_open_ticks++;
			if (machine->machine.door_open_ticks > definition->machine.runtime_door_open_ticks &&
				machine->device.position_group_index != NONE)
			{
				device_group_set_desired_value(machine->device.position_group_index, 0.0f);
			}
		}
		else
		{
			machine->machine.door_open_ticks = 0;
		}
	}

	if (TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) &&
		definition->machine.elevator_node_index != NONE
#ifdef HALO_LINUX
		/* (port: OpenCE's 30 carries them after the nodes are recomputed,
		machine_elevator_carry_end, not here a tick late) */
		&& !machine_elevator_same_tick_carry()
#endif
		)
	{
		real_matrix4x3 *node_matrix = object_get_node_matrix(
			machine_index,
			(unsigned short)definition->machine.elevator_node_index);

		elevator_offset.i = node_matrix->position.x - machine->machine.elevator_position.x;
		node_position = &node_matrix->position;
		elevator_offset.j = node_position->y - machine->machine.elevator_position.y;
		elevator_offset.k = node_position->z - machine->machine.elevator_position.z;
		if (elevator_offset.i != 0.0f || elevator_offset.j != 0.0f || elevator_offset.k != 0.0f)
		{
			object_count = objects_in_sphere(
				_object_mask_biped,
				_object_mask_biped,
				&machine->object.location,
				&machine->object.bounding_sphere_center,
				machine->object.bounding_sphere_radius,
				elevator_object_indices,
				NUMBEROF(elevator_object_indices));
			if (object_count > 0)
			{
				long remaining_object_count;

				elevator_object_index = elevator_object_indices;
				remaining_object_count = (unsigned short)object_count;
				do
				{
					long biped_index = *elevator_object_index;
					struct biped_datum *biped = biped_get(biped_index);

					if (biped->biped.elevator_object_index == machine_index)
					{
						add_vectors3d(&elevator_offset, (real_vector3d const *)&biped->object.position, (real_vector3d *)&new_position);
						object_translate(biped_index, &new_position, NULL);
					}

					elevator_object_index++;
				}
				while (--remaining_object_count);
			}
		}
		machine->machine.elevator_position = *node_position;
	}

	if (TEST_FLAG(machine->device.flags, _device_position_changed_bit))
	{
		object_translate(machine_index, &machine->object.position, NULL);
		machine->device.flags &= ~FLAG(_device_position_changed_bit);
	}

	return TRUE;
}

/* ---------- private code */
