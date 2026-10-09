/*
VITA_CTRL_PORTS.H

The controller ports merged into one pad (port/vita/host/vita_ctrl_ports.c,
read by vita_input.c): port 0 is the Vita's own buttons and sticks (on a
PS TV, its first paired controller), ports 1 to 4 the paired DualShocks
(a PS TV's, or a pad a plugin puts there on a Vita). Their buttons are
ORed; each stick is the port's whose stick last left the centre's
deadzone (furthest from the centre when several did), so a resting pad
never fights the one being played. A paired pad's L1/R1 and L2/R2 are the
Vita's L and R; its L3/R3 come through as VITA_BUTTON_L3/R3 (the Xbox
sticks' clicks, vita_controls.c).

The ctrl layer is a table of calls, faked by the desktop test
(port/vita/tests/vita_ctrl_ports_test.c).
*/

#ifndef __HALO_VITA_CTRL_PORTS_H
#define __HALO_VITA_CTRL_PORTS_H

/* port 0 and the four paired controllers' */
#define VITA_CTRL_PORTS 5
/* how often the paired controllers are asked for (process time, us) */
#define VITA_CTRL_PORT_INFO_US 1000000ULL
/* how far a stick must leave the centre (of 128) to take the stick */
#define VITA_CTRL_STICK_DEADZONE 24

/* the controller types sceCtrlGetControllerPortInfo gives
(SceCtrlExternalInputMode) */
#define VITA_CTRL_TYPE_UNPAIRED 0

/* the buttons a paired controller reports that the Vita's own do not
(sceCtrlPeekBufferPositive2) */
#define VITA_CTRL_L1 0x00000400UL
#define VITA_CTRL_R1 0x00000800UL

struct vita_ctrl_sample
{
	/* SCE_CTRL_* bits (the Vita's: VITA_BUTTON_*, vita_host.h) */
	unsigned long buttons;
	unsigned char lx, ly, rx, ry;
};

struct vita_ctrl_layer
{
	/* port 0 (sceCtrlPeekBufferPositive): the Vita's own buttons */
	int (*read_vita)(struct vita_ctrl_sample *sample);
	/* a paired controller, port 1 to 4 (sceCtrlPeekBufferPositive2); <0 if
	none answers */
	int (*read_port)(int port, struct vita_ctrl_sample *sample);
	/* each port's controller type (sceCtrlGetControllerPortInfo); <0 if
	the system cannot say */
	int (*port_types)(unsigned char types[VITA_CTRL_PORTS]);
	/* a line for halo.log (may be NULL) */
	void (*log)(const char *line);
};

struct vita_ctrl_ports
{
	/* when the ports were last asked for, and whether they have been */
	unsigned long long asked_at;
	int asked;
	/* the types last seen (logged when they change), and whether the
	system could say */
	unsigned char types[VITA_CTRL_PORTS];
	int types_known;
	/* the paired ports read every frame */
	unsigned char live[VITA_CTRL_PORTS];
	/* each stick's port (left, right) */
	int stick_port[2];
};

/* the ports' state merged: the Vita's own (port 0) always, the paired
controllers' every frame while paired, asked for once a second */
void vita_ctrl_ports_read(struct vita_ctrl_ports *ports, const struct vita_ctrl_layer *layer,
	unsigned long long now_us, struct vita_ctrl_sample *merged);

/* (the merge alone) samples[port] for the ports have[port] says answered;
port 0 always has */
void vita_ctrl_ports_merge(struct vita_ctrl_ports *ports, const struct vita_ctrl_sample samples[VITA_CTRL_PORTS],
	const unsigned char have[VITA_CTRL_PORTS], struct vita_ctrl_sample *merged);

#endif
