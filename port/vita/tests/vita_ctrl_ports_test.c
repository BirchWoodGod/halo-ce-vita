/*
VITA_CTRL_PORTS_TEST.C

A desktop test of the controller ports' merge (port/vita/host/vita_ctrl_ports.c,
included whole, over a fake ctrl layer; issue #39): a Vita with no paired
controller gets port 0 exactly and reads no other port; the ports are asked
for once a second and only the paired ones read, once a frame each; a
paired pad's buttons ORed in (L1/R1 and L2/R2 the Vita's L and R, L3/R3
kept, the PS button and the like dropped); each stick the one that last
left the deadzone, furthest when several did, so a resting or drifting pad
never moves the Vita's sticks and a pad held just off centre keeps its
fine aim; a pad going away hands its sticks back to port 0; a port that
stops answering is dropped until the next ask; a system that cannot say
which ports are paired gets each tried once a second; a PS TV (port 0 and
1 the same pad); halo.log lines only on a change; and the stick clicks
(VITA_BUTTON_L3/R3) as the Xbox sticks' clicks in play (vita_controls.c).

Run port/vita/tests/run_vita_ctrl_ports_test.sh.
*/

#include "../host/vita_ctrl_ports.c"
#include "../host/vita_controls.c"

#include <stdio.h>
#include <stdlib.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* ---------- the fake ctrl layer */

static struct vita_ctrl_sample fake_state[VITA_CTRL_PORTS];
static unsigned char fake_types[VITA_CTRL_PORTS];
/* ports that answer a read (a paired one does, unless told not to) */
static int fake_answers[VITA_CTRL_PORTS];
static int fake_types_fail;
static int reads_vita, reads_port[VITA_CTRL_PORTS], asks, logs;
static char last_log[128];

static int fake_read_vita(struct vita_ctrl_sample *sample)
{
	reads_vita++;
	*sample = fake_state[0];
	return 1;
}

static int fake_read_port(int port, struct vita_ctrl_sample *sample)
{
	if (port < 1 || port >= VITA_CTRL_PORTS)
		return (int)0x80340001;
	reads_port[port]++;
	if (!fake_answers[port])
		return (int)0x80340020;
	*sample = fake_state[port];
	return 1;
}

static int fake_port_types(unsigned char types[VITA_CTRL_PORTS])
{
	asks++;
	if (fake_types_fail)
		return (int)0x80340021;
	memcpy(types, fake_types, VITA_CTRL_PORTS);
	return 0;
}

static void fake_log(const char *line)
{
	logs++;
	snprintf(last_log, sizeof(last_log), "%s", line);
}

static const struct vita_ctrl_layer fake_layer = { fake_read_vita, fake_read_port, fake_port_types, fake_log };

static void set_state(int port, unsigned long buttons, int lx, int ly, int rx, int ry)
{
	fake_state[port].buttons = buttons;
	fake_state[port].lx = (unsigned char)lx;
	fake_state[port].ly = (unsigned char)ly;
	fake_state[port].rx = (unsigned char)rx;
	fake_state[port].ry = (unsigned char)ry;
}

static void reset(void)
{
	int port;

	memset(fake_types, 0, sizeof(fake_types));
	memset(fake_answers, 0, sizeof(fake_answers));
	fake_types_fail = 0;
	fake_types[0] = 1;
	for (port = 0; port < VITA_CTRL_PORTS; port++)
		set_state(port, 0, 128, 128, 128, 128);
	reads_vita = asks = logs = 0;
	memset(reads_port, 0, sizeof(reads_port));
	last_log[0] = 0;
}

static void pair(int port, int type)
{
	fake_types[port] = (unsigned char)type;
	fake_answers[port] = type != VITA_CTRL_TYPE_UNPAIRED;
}

static int reads_other(void)
{
	return reads_port[1] + reads_port[2] + reads_port[3] + reads_port[4];
}

static int same(const struct vita_ctrl_sample *a, const struct vita_ctrl_sample *b)
{
	return a->buttons == b->buttons && a->lx == b->lx && a->ly == b->ly && a->rx == b->rx && a->ry == b->ry;
}

/* a frame every 33 ms */
#define FRAME_US 33333ULL

static void test_vita_only(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 5000000;
	int frame, all_same = 1;

	reset();
	memset(&ports, 0, sizeof(ports));
	/* (the Vita's own sticks drift a little at rest, and move) */
	for (frame = 0; frame < 300; frame++, now += FRAME_US)
	{
		set_state(0, (frame & 4) ? 0x4000 | 0x100 : 0, 128 + (frame % 9) - 4, 120 + (frame % 30), 255 - frame % 256,
			(frame * 7) & 255);
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
		all_same &= same(&merged, &fake_state[0]);
	}
	check(all_same, "no controller paired: every frame is port 0's buttons and sticks exactly");
	check(reads_other() == 0, "no controller paired: no other port is read");
	check(reads_vita == 300, "port 0 is read once a frame");
	check(asks == 10, "the ports are asked for once a second (10 in 300 frames of 33 ms)");
	check(logs == 1 && strstr(last_log, "types 1 0 0 0 0") != NULL, "halo.log has the ports' types once");
}

static void test_idle_pad(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 0;
	int frame, all_same = 1;

	reset();
	memset(&ports, 0, sizeof(ports));
	pair(1, 8);
	/* a DS4 lying untouched (drifting inside the deadzone) while the
	Vita is played */
	for (frame = 0; frame < 120; frame++, now += FRAME_US)
	{
		set_state(1, 0, 128 + VITA_CTRL_STICK_DEADZONE, 128 - VITA_CTRL_STICK_DEADZONE, 140, 116);
		set_state(0, (frame & 8) ? 0x2000 : 0, 128 + (frame % 5), 128 - (frame % 7), (frame * 3) & 255, 200);
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
		all_same &= same(&merged, &fake_state[0]);
	}
	check(all_same, "a paired pad lying untouched (drift up to the deadzone) changes nothing");
	check(reads_port[1] == 120 && reads_port[2] + reads_port[3] + reads_port[4] == 0,
		"a paired port is read once a frame, the unpaired ones never");
}

static void test_buttons(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;

	reset();
	memset(&ports, 0, sizeof(ports));
	pair(2, 8);
	set_state(0, 0x4000 /* cross */, 128, 128, 128, 128);
	set_state(2, 0x1000 /* triangle */ | VITA_CTRL_L1 | 0x10000 /* PS */ | 0x00100000 /* volume */, 128, 128, 128, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, 0, &merged);
	check(merged.buttons == (0x4000 | 0x1000 | 0x100), "buttons ORed; a pad's L1 is the Vita's L; PS and volume dropped");
	set_state(2, VITA_CTRL_R1, 128, 128, 128, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, FRAME_US, &merged);
	check(merged.buttons == (0x4000 | 0x200), "a pad's R1 is the Vita's R");
	set_state(2, 0x100 | 0x200, 128, 128, 128, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, 2 * FRAME_US, &merged);
	check(merged.buttons == (0x4000 | 0x100 | 0x200), "a pad's L2/R2 are the Vita's L and R");
	set_state(2, VITA_BUTTON_L3 | VITA_BUTTON_R3, 128, 128, 128, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, 3 * FRAME_US, &merged);
	check(merged.buttons == (0x4000 | VITA_BUTTON_L3 | VITA_BUTTON_R3), "a pad's L3/R3 come through");
	set_state(0, 0x00080000 /* headphone */ | 0x8000, 128, 128, 128, 128);
	set_state(2, 0, 128, 128, 128, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, 4 * FRAME_US, &merged);
	check(merged.buttons == (0x00080000 | 0x8000), "port 0's buttons pass as they are");
}

static void test_sticks(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 0;

	reset();
	memset(&ports, 0, sizeof(ports));
	pair(1, 8);
	/* the pad's left stick pushed: it takes the left stick, not the right */
	set_state(0, 0, 130, 126, 60, 128);
	set_state(1, 0, 20, 128, 128, 131);
	vita_ctrl_ports_read(&ports, &fake_layer, now += FRAME_US, &merged);
	check(merged.lx == 20 && merged.ly == 128, "a pad's stick pushed past the deadzone takes the stick");
	check(merged.rx == 60 && merged.ry == 128, "the other stick stays the Vita's");
	/* held just off centre (fine aim): still the pad's */
	set_state(1, 0, 128 - 6, 128 + 3, 128, 131);
	set_state(0, 0, 128 + 10, 128 - 12, 60, 128);
	vita_ctrl_ports_read(&ports, &fake_layer, now += FRAME_US, &merged);
	check(merged.lx == 122 && merged.ly == 131, "the pad keeps the stick inside the deadzone (fine aim; the Vita's drift ignored)");
	/* both pushed: the furthest */
	set_state(0, 0, 0, 128, 60, 128);
	set_state(1, 0, 128 + 40, 128, 128, 131);
	vita_ctrl_ports_read(&ports, &fake_layer, now += FRAME_US, &merged);
	check(merged.lx == 0, "both sticks pushed: the one furthest from the centre");
	/* the Vita's let go: back inside, it keeps the stick */
	set_state(0, 0, 128 + 3, 128, 60, 128);
	set_state(1, 0, 128 + 2, 128, 128, 131);
	vita_ctrl_ports_read(&ports, &fake_layer, now += FRAME_US, &merged);
	check(merged.lx == 131, "the stick last pushed keeps it at rest");
	/* the pad pushed again, then unpaired: port 0's again */
	set_state(1, 0, 128, 255, 128, 131);
	vita_ctrl_ports_read(&ports, &fake_layer, now += FRAME_US, &merged);
	check(merged.ly == 255, "the pad takes it back");
	pair(1, VITA_CTRL_TYPE_UNPAIRED);
	now += VITA_CTRL_PORT_INFO_US;
	vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
	check(merged.lx == 131 && merged.ly == 128 && merged.buttons == 0, "a pad gone: port 0's sticks again");
	check(strstr(last_log, "types 1 0 0 0 0") != NULL, "halo.log has the change");
}

static void test_read_fails(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 0;
	int frame;

	reset();
	memset(&ports, 0, sizeof(ports));
	pair(3, 4);
	fake_answers[3] = 0;
	for (frame = 0; frame < 60; frame++, now += FRAME_US)
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
	check(reads_port[3] == 2, "a paired port that does not answer is tried again only when the ports are asked for");
	check(same(&merged, &fake_state[0]), "and gives nothing");
}

static void test_types_unknown(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 0;
	int frame, ok = 1;

	reset();
	memset(&ports, 0, sizeof(ports));
	fake_types_fail = 1;
	for (frame = 0; frame < 60; frame++, now += FRAME_US)
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
	check(reads_port[1] == 2 && reads_port[2] == 2 && reads_port[3] == 2 && reads_port[4] == 2,
		"types unknown, nothing answering: each port tried once a second");
	check(logs == 1 && strstr(last_log, "unknown") != NULL, "halo.log says the ports are unknown, once");
	fake_answers[4] = 1;
	set_state(4, 0x4000, 128, 128, 128, 128);
	memset(reads_port, 0, sizeof(reads_port));
	for (frame = 0; frame < 60; frame++, now += FRAME_US)
	{
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
		if (frame >= 30)
			ok &= merged.buttons == 0x4000;
	}
	check(ok, "types unknown: a port that answers is used");
	check(reads_port[4] >= 30 && reads_port[1] <= 2, "and read every frame, the others once a second");
}

static void test_pstv(void)
{
	struct vita_ctrl_ports ports;
	struct vita_ctrl_sample merged;
	unsigned long long now = 0;
	int frame, ok = 1;

	reset();
	memset(&ports, 0, sizeof(ports));
	/* a PS TV: port 0 the virtual pad (its first DualShock), port 1 the
	same DualShock */
	fake_types[0] = 2;
	pair(1, 4);
	for (frame = 0; frame < 90; frame++, now += FRAME_US)
	{
		int lx = (frame * 11) & 255, ry = 255 - ((frame * 5) & 255);

		/* (port 0's plain call gives L1 as L; port 1's gives L1 as L1) */
		set_state(0, (frame & 2) ? 0x100 : 0, lx, 128, 128, ry);
		set_state(1, (frame & 2) ? VITA_CTRL_L1 : 0, lx, 128, 128, ry);
		vita_ctrl_ports_read(&ports, &fake_layer, now, &merged);
		ok &= same(&merged, &fake_state[0]);
	}
	check(ok, "a PS TV (port 0 and 1 the same pad): the same as port 0 alone");
}

static void test_stick_clicks(void)
{
	struct vita_controls_config config;
	struct vita_controls_state state;
	struct vita_controls_output output;
	int index;

	for (index = 0; index < VITA_ZONE_COUNT; index++)
		unsetenv(vita_touch_variables[index]);
	for (index = 0; index < VITA_XBOX_COUNT; index++)
		if (vita_xbox_variables[index])
			unsetenv(vita_xbox_variables[index]);
	setenv("HALO_CROUCH_TOGGLE", "0", 1);
	vita_controls_config_load(&config);
	memset(&state, 0, sizeof(state));
	vita_controls_map(&config, &state, VITA_BUTTON_L3, 0, 0, &output);
	check(output.digital == VITA_PAD_LEFT_THUMB, "L3 in play: the left stick's click (crouch)");
	vita_controls_map(&config, &state, VITA_BUTTON_R3, 0, 0, &output);
	check(output.digital == VITA_PAD_RIGHT_THUMB, "R3 in play: the right stick's click (zoom)");
	vita_controls_map(&config, &state, VITA_BUTTON_L3 | VITA_BUTTON_R3, 0, 1, &output);
	check(output.digital == 0, "the stick clicks do nothing in the menus");
	setenv("HALO_CROUCH_TOGGLE", "1", 1);
	vita_controls_config_load(&config);
	memset(&state, 0, sizeof(state));
	vita_controls_map(&config, &state, VITA_BUTTON_L3, 0, 0, &output);
	vita_controls_map(&config, &state, 0, 0, 0, &output);
	check(output.digital == VITA_PAD_LEFT_THUMB, "crouch toggle: an L3 press crouches");
	vita_controls_map(&config, &state, VITA_BUTTON_L3, 0, 0, &output);
	vita_controls_map(&config, &state, 0, 0, 0, &output);
	check(output.digital == 0, "and the next stands");
	unsetenv("HALO_CROUCH_TOGGLE");
	/* the Vita's own buttons alone never set the clicks */
	vita_controls_config_load(&config);
	memset(&state, 0, sizeof(state));
	vita_controls_map(&config, &state, 0x4000 | 0x100 | 0x200 | 0x1000 | 0x2000 | 0x8000, 0, 0, &output);
	check(!(output.digital & (VITA_PAD_LEFT_THUMB | VITA_PAD_RIGHT_THUMB)),
		"the face buttons and L/R press no stick click");
}

int main(void)
{
	test_vita_only();
	test_idle_pad();
	test_buttons();
	test_sticks();
	test_read_fails();
	test_types_unknown();
	test_pstv();
	test_stick_clicks();
	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
