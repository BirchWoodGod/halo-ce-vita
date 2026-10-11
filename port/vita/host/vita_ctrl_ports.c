/*
VITA_CTRL_PORTS.C

The controller ports merged into one pad (vita_ctrl_ports.h says how).
Issue #39: a DualShock 4 through the DS4Vita plugin (with reVita) reached
the system but not the game, which read port 0 only. The paired ports are
read with sceCtrlPeekBufferPositive2 (as SDL's Vita joystick driver reads
them), only while sceCtrlGetControllerPortInfo says a controller is on
them; it is asked once a second, so a Vita with none paired pays one call
a second. If the system cannot say, each port is tried once a second and
read every frame while it answers.

A Vita played with its own buttons alone gets exactly what port 0 gives:
no port answers, or a paired pad lies untouched (its sticks never leave
the deadzone, so never take a stick, and it presses no button).
*/

#include "vita_ctrl_ports.h"

#include <stdio.h>
#include <string.h>

/* (the Vita's L and R as sceCtrlPeekBufferPositive gives them; a paired
controller's L2 and R2 as sceCtrlPeekBufferPositive2 does) */
#define CTRL_LTRIGGER 0x00000100UL
#define CTRL_RTRIGGER 0x00000200UL
/* (the buttons a game sees: the PS button, volume and such above) */
#define CTRL_GAME_BUTTONS 0x0000ffffUL

static int stick_distance(unsigned char x, unsigned char y)
{
	int dx = (int)x - 128, dy = (int)y - 128;

	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	return dx > dy ? dx : dy;
}

void vita_ctrl_ports_merge(struct vita_ctrl_ports *ports, const struct vita_ctrl_sample samples[VITA_CTRL_PORTS],
	const unsigned char have[VITA_CTRL_PORTS], struct vita_ctrl_sample *merged)
{
	int port, stick;

	/* the buttons: port 0's as they are; a paired pad's L1/R1 and L2/R2
	the Vita's L and R, its L3/R3 kept (VITA_BUTTON_L3/R3) */
	merged->buttons = samples[0].buttons;
	for (port = 1; port < VITA_CTRL_PORTS; port++)
	{
		unsigned long buttons;

		if (!have[port])
			continue;
		buttons = samples[port].buttons & CTRL_GAME_BUTTONS;
		if (buttons & VITA_CTRL_L1)
			buttons |= CTRL_LTRIGGER;
		if (buttons & VITA_CTRL_R1)
			buttons |= CTRL_RTRIGGER;
		merged->buttons |= buttons & ~(VITA_CTRL_L1 | VITA_CTRL_R1);
	}
	/* each stick: the port's furthest beyond the deadzone, else the one
	that last was (port 0 to begin with, and when that one goes away) */
	for (stick = 0; stick < 2; stick++)
	{
		int best = -1, best_distance = VITA_CTRL_STICK_DEADZONE;
		const struct vita_ctrl_sample *source;

		if (ports->stick_port[stick] < 0 || ports->stick_port[stick] >= VITA_CTRL_PORTS ||
			(ports->stick_port[stick] && !have[ports->stick_port[stick]]))
			ports->stick_port[stick] = 0;
		for (port = 0; port < VITA_CTRL_PORTS; port++)
		{
			int distance;

			if (port && !have[port])
				continue;
			distance = stick ? stick_distance(samples[port].rx, samples[port].ry) :
				stick_distance(samples[port].lx, samples[port].ly);
			if (distance > best_distance)
			{
				best = port;
				best_distance = distance;
			}
		}
		if (best >= 0)
			ports->stick_port[stick] = best;
		source = &samples[ports->stick_port[stick]];
		if (stick)
		{
			merged->rx = source->rx;
			merged->ry = source->ry;
		}
		else
		{
			merged->lx = source->lx;
			merged->ly = source->ly;
		}
	}
}

static void log_types(const struct vita_ctrl_ports *ports, const struct vita_ctrl_layer *layer, int result)
{
	char line[96];

	if (!layer->log)
		return;
	if (result < 0)
		snprintf(line, sizeof(line), "pad: the controller ports are unknown (0x%08x); trying ports 1-4 each second",
			(unsigned int)result);
	else
		snprintf(line, sizeof(line), "pad: controller ports 0-4: types %u %u %u %u %u", ports->types[0],
			ports->types[1], ports->types[2], ports->types[3], ports->types[4]);
	layer->log(line);
}

static void ask_ports(struct vita_ctrl_ports *ports, const struct vita_ctrl_layer *layer)
{
	unsigned char types[VITA_CTRL_PORTS];
	int result, port, known, changed;

	memset(types, 0, sizeof(types));
	result = layer->port_types ? layer->port_types(types) : -1;
	known = result >= 0;
	for (port = 1; port < VITA_CTRL_PORTS; port++)
		ports->live[port] = known ? types[port] != VITA_CTRL_TYPE_UNPAIRED : 1;
	changed = !ports->asked || known != ports->types_known || (known && memcmp(types, ports->types, sizeof(types)));
	ports->types_known = known;
	if (known)
		memcpy(ports->types, types, sizeof(types));
	if (changed)
		log_types(ports, layer, result);
}

void vita_ctrl_ports_read(struct vita_ctrl_ports *ports, const struct vita_ctrl_layer *layer,
	unsigned long long now_us, struct vita_ctrl_sample *merged)
{
	struct vita_ctrl_sample samples[VITA_CTRL_PORTS];
	unsigned char have[VITA_CTRL_PORTS];
	int port;

	if (!ports->asked || now_us - ports->asked_at >= VITA_CTRL_PORT_INFO_US)
	{
		ask_ports(ports, layer);
		ports->asked = 1;
		ports->asked_at = now_us;
	}
	for (port = 0; port < VITA_CTRL_PORTS; port++)
	{
		samples[port].buttons = 0;
		samples[port].lx = samples[port].ly = samples[port].rx = samples[port].ry = 128;
		have[port] = 0;
	}
	layer->read_vita(&samples[0]);
	have[0] = 1;
	for (port = 1; port < VITA_CTRL_PORTS; port++)
	{
		if (!ports->live[port])
			continue;
		if (layer->read_port(port, &samples[port]) >= 0)
			have[port] = 1;
		else
		{
			/* (not again until the ports are next asked for) */
			ports->live[port] = 0;
			samples[port].buttons = 0;
			samples[port].lx = samples[port].ly = samples[port].rx = samples[port].ry = 128;
		}
	}
	vita_ctrl_ports_merge(ports, samples, have, merged);
}
