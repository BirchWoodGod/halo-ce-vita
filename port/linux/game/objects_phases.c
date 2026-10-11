/* objects_phases.c

(HALO_TICK_PROFILE=2) objects_update by object type and phase:
objects_phases.h. A stack of (object type, phase) entries; each clock read
charges the time since the last one to the entry on top, so a phase nested
in another is taken out of it and the parts add up to objects_update. */

#include <stdio.h>
#include <stdlib.h>

#include "fine_profile.h"
#include "objects_phases.h"
#include "render_epoch.h"

void platform_log(const char *format, ...);

#define TYPES 16
/* (objects_update's own walks, outside any object) */
#define TYPE_LOOP TYPES
#define STACK_DEPTH 64

unsigned char halo_objects_phases_on;

static int enabled = -1;
static unsigned long runs;

static struct
{
	unsigned char type, phase, object;
} stack[STACK_DEPTH];
static int depth;
/* pushes past the stack's depth, popped without effect */
static int overflow;
/* the objects on the stack (a child's update runs inside its parent's) */
static int object_depth;
static unsigned long long last;

static unsigned long long phase_us[TYPES + 1][NUMBER_OF_OBJECTS_PHASES];
static unsigned long phase_calls[TYPES + 1][NUMBER_OF_OBJECTS_PHASES];
static unsigned long objects[TYPES];
static unsigned long long child_us;
static unsigned long children;

/* snprintf at the line's end, never past it */
#define APPEND(...) do { if (n < (int)sizeof(line) - 1) { int written = snprintf(line + n, sizeof(line) - n, __VA_ARGS__); \
	n += written > 0 ? written : 0; if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1; } } while (0)

static const char *const type_names[TYPES + 1] = {
	"biped", "vehicle", "weapon", "equipment", "garbage", "projectile", "scenery", "machine", "control",
	"light_fixture", "placeholder", "sound_scenery", "t12", "t13", "t14", "t15", "loop"
};
static const char *const phase_names[NUMBER_OF_OBJECTS_PHASES] = {
	"object", "type", "collision", "physics", "map", "nodes", "lights", "damage", "functions", "create"
};

/* the time since the last read, to the entry on top */
static void charge(void)
{
	unsigned long long now = halo_fine_tick_now();

	if (depth > 0)
	{
		unsigned long long elapsed = now - last;

		phase_us[stack[depth - 1].type][stack[depth - 1].phase] += elapsed;
		if (object_depth > 1)
			child_us += elapsed;
	}
	last = now;
}

/* (the tick thread's calls only: the render may reach a hooked function
while the tick runs) */
static int ours(void)
{
	return !halo_epoch_threaded || halo_epoch_on_mutator_inline();
}

static void push(unsigned char type, unsigned char phase, unsigned char object)
{
	if (depth >= STACK_DEPTH)
	{
		overflow++;
		return;
	}
	charge();
	stack[depth].type = type;
	stack[depth].phase = phase;
	stack[depth].object = object;
	depth++;
	if (object)
		object_depth++;
}

static void pop(void)
{
	if (overflow)
	{
		overflow--;
		return;
	}
	if (depth <= 0)
		return;
	charge();
	depth--;
	if (stack[depth].object)
		object_depth--;
}

void halo_objects_phases_begin(void)
{
	if (enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		enabled = setting && atoi(setting) >= 2;
	}
	halo_objects_phases_on = enabled > 0 && halo_fine_tick_on;
	if (!halo_objects_phases_on)
		return;
	depth = 0;
	overflow = 0;
	object_depth = 0;
	last = halo_fine_tick_now();
	push(TYPE_LOOP, _objects_phase_object, 0);
}

void halo_objects_phases_object_push(short type)
{
	unsigned char slot = (unsigned char)(type & (TYPES - 1));

	if (!ours())
		return;
	push(slot, _objects_phase_object, 1);
	objects[slot]++;
	if (object_depth > 1)
		children++;
}

void halo_objects_phases_object_pop(void)
{
	if (ours())
		pop();
}

void halo_objects_phase_push(int phase)
{
	unsigned char type;

	if (!ours())
		return;
	type = depth > 0 ? stack[depth - 1].type : TYPE_LOOP;
	push(type, (unsigned char)phase, 0);
	phase_calls[type][phase]++;
}

void halo_objects_phase_pop(void)
{
	if (ours())
		pop();
}

void halo_objects_phases_end(void)
{
	char line[1536];
	int n = 0, type, phase;
	/* (per timed tick: fine_profile.h) */
	static struct halo_fine_mark mark;
	unsigned long timed;
	double per;
	unsigned long long type_us[TYPES + 1], total_us = 0, phase_total_us[NUMBER_OF_OBJECTS_PHASES] = { 0 };

	if (halo_objects_phases_on)
	{
		pop();
		halo_objects_phases_on = 0;
	}
	if (enabled <= 0 || ++runs % 300)
		return;
	timed = halo_fine_tick_since(&mark);
	per = timed ? 1.0 / (timed * 1000.0) : 0.0;
	for (type = 0; type <= TYPES; type++)
	{
		type_us[type] = 0;
		for (phase = 0; phase < NUMBER_OF_OBJECTS_PHASES; phase++)
		{
			type_us[type] += phase_us[type][phase];
			phase_total_us[phase] += phase_us[type][phase];
		}
		total_us += type_us[type];
	}

	/* each type's own time (children as their own type) and objects, the phases' totals */
	APPEND(" total %.2f |", total_us * per);
	for (type = 0; type < TYPES; type++)
		if (objects[type])
			APPEND(" %s %.2f(%.1f)", type_names[type], type_us[type] * per,
				timed ? (double)objects[type] / timed : 0.0);
	APPEND(" loop %.2f |", type_us[TYPE_LOOP] * per);
	for (phase = 0; phase < NUMBER_OF_OBJECTS_PHASES; phase++)
		APPEND(" %s %.2f", phase_names[phase], phase_total_us[phase] * per);
	APPEND(" | children %.2f(%.1f) | %s", child_us * per,
			timed ? (double)children / timed : 0.0, mark.note);
	platform_log("objects-update (ms/tick, objects/tick):%s", line);

	/* each type by phase, with the phase's calls (an object's own "object" has its updates) */
	n = 0;
	for (type = 0; type <= TYPES; type++)
	{
		if (!type_us[type])
			continue;
		APPEND(" %s:", type_names[type]);
		for (phase = 0; phase < NUMBER_OF_OBJECTS_PHASES; phase++)
		{
			if (!phase_us[type][phase] && !phase_calls[type][phase])
				continue;
			if (phase == _objects_phase_object)
				APPEND(" %s %.2f", phase_names[phase], phase_us[type][phase] * per);
			else
				APPEND(" %s %.2f(%.1f)", phase_names[phase], phase_us[type][phase] * per,
					timed ? (double)phase_calls[type][phase] / timed : 0.0);
		}
		APPEND(" |");
	}
	platform_log("objects-phases (ms/tick, calls/tick):%s", line);

	for (type = 0; type <= TYPES; type++)
		for (phase = 0; phase < NUMBER_OF_OBJECTS_PHASES; phase++)
		{
			phase_us[type][phase] = 0;
			phase_calls[type][phase] = 0;
		}
	for (type = 0; type < TYPES; type++)
		objects[type] = 0;
	child_us = 0;
	children = 0;
}
