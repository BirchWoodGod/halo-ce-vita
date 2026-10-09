/* objects_phases.h

(HALO_TICK_PROFILE=2, in the ticks HALO_PROFILE_SAMPLE picks) objects_update
by object type and by phase, every 300 ticks: "objects-update" (each type's
own time and objects, the phases' totals, the children's share) and
"objects-phases" (each type's time by phase, with calls). objects_phases.c.

Each object's update is timed as its own type (a child - a held weapon, a
grenade stuck to a biped - as the child's type, not its parent's), and the
time inside it is split exclusively among the phases below: a phase nested
in another (the collision tests of a biped's move, the cluster references of
a light's reconnect) is taken out of the outer one. The phases:

	type		the type's own update (unit, biped, weapon... logic) not in a phase below
	collision	collision queries: BSP and object tests (collisions.c's entry points)
	physics		mass-point physics (vehicles), a biped's physics and a dead
			biped's relaxation, outside their collision queries
	map		cluster references (cluster_partition_reconnect / _disconnect:
			objects and lights moved between clusters)
	nodes		node matrices (animation, aiming, matrix products) and their post-processing
	lights		attached lights put back on the map (outside their cluster references)
	damage		object_damage_update
	functions	function values, exports and change colours
	create		objects made and deleted (object_new, object_delete_recursive)
	object		object_update's own (and objects_update's walks, as "loop")

Hooks cost one byte test when the profile is off. Called on the tick thread
only while objects_update runs; another thread's calls are not counted. */

#ifndef __HALO_OBJECTS_PHASES_H
#define __HALO_OBJECTS_PHASES_H

enum
{
	_objects_phase_object,
	_objects_phase_type,
	_objects_phase_collision,
	_objects_phase_physics,
	_objects_phase_map,
	_objects_phase_nodes,
	_objects_phase_lights,
	_objects_phase_damage,
	_objects_phase_functions,
	_objects_phase_create,
	NUMBER_OF_OBJECTS_PHASES
};

/* nonzero while objects_update runs in a timed tick with the profile on */
extern unsigned char halo_objects_phases_on;

/* objects_update's start and end (the report every 300 ticks) */
void halo_objects_phases_begin(void);
void halo_objects_phases_end(void);
/* an object's update starts (its type) and ends */
void halo_objects_phases_object_push(short type);
void halo_objects_phases_object_pop(void);
/* a phase starts and ends (paired: the switch changes only between objects_update's runs) */
void halo_objects_phase_push(int phase);
void halo_objects_phase_pop(void);

#define HALO_OBJECTS_PHASE_PUSH(phase) do { if (halo_objects_phases_on) halo_objects_phase_push(phase); } while (0)
#define HALO_OBJECTS_PHASE_POP() do { if (halo_objects_phases_on) halo_objects_phase_pop(); } while (0)
#define HALO_OBJECTS_OBJECT_PUSH(type) do { if (halo_objects_phases_on) halo_objects_phases_object_push(type); } while (0)
#define HALO_OBJECTS_OBJECT_POP() do { if (halo_objects_phases_on) halo_objects_phases_object_pop(); } while (0)

#endif
