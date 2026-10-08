/*
VITA_INPUT.C

The Vita's buttons and sticks for port/vita/platform/vita_pad.c (sceCtrl, in
the wide analog mode real firmware needs for the sticks to move), and the
touch zones held on the front screen and the rear pad (sceTouch;
vita_controls.c says where the zones are and when a finger counts).

(debug) HALO_PAD_FILE=ux0:data/haloce-vita/pad.txt: presses for automated
tests in Vita3K, without typing into its window. The file is looked for every
half second; its steps, "name:hold_ms:pause_ms" separated by spaces, are
pressed one after another and the file is deleted. Names: x c z v (cross
circle square triangle), up down left right, start select, l r, w a s d (the
left stick), i j k ll (the right stick), a+b for buttons together, and the
touch zones: tl tr (the front screen's top corners), el er (its left and
right edges), rl rr (the rear pad's halves), touched at their middle (a
rear one counts after its hold time, as a finger does). gx=N, gy=N, gz=N
turn the gyroscope at N degrees a second about the Vita's x, y or z axis
while held (vita_controls.h: gy=60 turns the view left, gx=60 looks up),
instead of the sensor; (debug) HALO_GYRO_SIM=x,y,z (degrees a second) does
the same all the time. Neither teaches the gyro's bias.

The gyroscope (sceMotion) is read every frame, each sample the sensor took
since the last (vita_controls.c filters them into angles, learning the bias
while the Vita lies still); it never counts as input for the idle dim (a
Vita lying on a table drifts).
*/

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_controls.h"
#include "lang.h"
#include "vita_host.h"

/* bumped by the settings panel (port_config.c) */
extern volatile unsigned long halo_settings_generation;

#define MAXIMUM_PAD_STEPS 64

struct pad_step
{
	unsigned int buttons;
	/* the touch zones (a bit each) */
	unsigned int touch;
	int lx, ly, rx, ry;
	/* the gyroscope's rates (degrees a second, the Vita's axes), if set */
	int gyro_set;
	float gyro[3];
	unsigned int hold_ms;
	unsigned int pause_ms;
};

static struct pad_step pad_steps[MAXIMUM_PAD_STEPS];
static int pad_step_count;
static int pad_step_index;
static unsigned long long pad_step_start;
static unsigned long long pad_last_poll;

static void pad_step_add_key(struct pad_step *step, const char *name)
{
	static const struct
	{
		const char *name;
		unsigned int buttons;
	} buttons[] = {
		{ "x", SCE_CTRL_CROSS }, { "c", SCE_CTRL_CIRCLE }, { "z", SCE_CTRL_SQUARE },
		{ "v", SCE_CTRL_TRIANGLE }, { "up", SCE_CTRL_UP }, { "down", SCE_CTRL_DOWN },
		{ "left", SCE_CTRL_LEFT }, { "right", SCE_CTRL_RIGHT }, { "start", SCE_CTRL_START },
		{ "select", SCE_CTRL_SELECT }, { "l", SCE_CTRL_LTRIGGER }, { "r", SCE_CTRL_RTRIGGER },
	};
	int index;

	for (index = 0; index < VITA_ZONE_COUNT; index++)
		if (!strcmp(name, vita_touch_zones[index].script_name))
			step->touch |= 1u << index;
	if (name[0] == 'g' && name[1] >= 'x' && name[1] <= 'z' && name[2] == '=')
	{
		step->gyro_set = 1;
		step->gyro[name[1] - 'x'] = (float)atof(name + 3);
	}
	else if (!strcmp(name, "w"))
		step->ly = 0;
	else if (!strcmp(name, "s"))
		step->ly = 255;
	else if (!strcmp(name, "a"))
		step->lx = 0;
	else if (!strcmp(name, "d"))
		step->lx = 255;
	else if (!strcmp(name, "i"))
		step->ry = 0;
	else if (!strcmp(name, "k"))
		step->ry = 255;
	else if (!strcmp(name, "j"))
		step->rx = 0;
	else if (!strcmp(name, "ll"))
		step->rx = 255;
	for (index = 0; index < (int)(sizeof(buttons) / sizeof(buttons[0])); index++)
	{
		if (!strcmp(name, buttons[index].name))
			step->buttons |= buttons[index].buttons;
	}
}

static void pad_script_poll(unsigned long long now)
{
	static int checked;
	static const char *path;
	char text[2048];
	char *token, *token_end;
	FILE *file;
	size_t size;

	if (!checked)
	{
		checked = 1;
		path = getenv("HALO_PAD_FILE");
	}
	if (!path || !*path || pad_step_index < pad_step_count || now - pad_last_poll < 500000)
		return;
	pad_last_poll = now;
	file = fopen(path, "r");
	if (!file)
		return;
	size = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	remove(path);
	text[size] = 0;

	pad_step_count = 0;
	pad_step_index = 0;
	for (token = strtok_r(text, " \t\r\n", &token_end); token && pad_step_count < MAXIMUM_PAD_STEPS;
		token = strtok_r(NULL, " \t\r\n", &token_end))
	{
		struct pad_step *step = &pad_steps[pad_step_count++];
		char *hold = strchr(token, ':');
		char *pause = hold ? strchr(hold + 1, ':') : NULL;
		char *key, *key_end;

		memset(step, 0, sizeof(*step));
		step->lx = step->ly = step->rx = step->ry = 128;
		step->hold_ms = 150;
		step->pause_ms = 1500;
		if (hold)
		{
			*hold = 0;
			step->hold_ms = (unsigned int)atoi(hold + 1);
		}
		if (pause)
		{
			*pause = 0;
			step->pause_ms = (unsigned int)atoi(pause + 1);
		}
		for (key = strtok_r(token, "+", &key_end); key; key = strtok_r(NULL, "+", &key_end))
			pad_step_add_key(step, key);
	}
	pad_step_start = now;
}

/* the zones the script touches now, and the gyroscope's rates it sets
(degrees a second) */
static unsigned int pad_script_touch;
static int pad_script_gyro_set;
static float pad_script_gyro[3];

static void pad_script_apply(struct vita_host_pad *pad, unsigned long long now)
{
	pad_script_touch = 0;
	pad_script_gyro_set = 0;
	while (pad_step_index < pad_step_count)
	{
		const struct pad_step *step = &pad_steps[pad_step_index];
		unsigned long long elapsed_ms = (now - pad_step_start) / 1000;

		if (elapsed_ms < step->hold_ms)
		{
			pad->buttons |= step->buttons;
			pad_script_touch = step->touch;
			pad_script_gyro_set = step->gyro_set;
			memcpy(pad_script_gyro, step->gyro, sizeof(pad_script_gyro));
			if (step->lx != 128)
				pad->lx = step->lx;
			if (step->ly != 128)
				pad->ly = step->ly;
			if (step->rx != 128)
				pad->rx = step->rx;
			if (step->ry != 128)
				pad->ry = step->ry;
			return;
		}
		if (elapsed_ms < step->hold_ms + step->pause_ms)
			return;
		pad_step_start += (unsigned long long)(step->hold_ms + step->pause_ms) * 1000;
		pad_step_index++;
	}
}

/* when the player last pressed a button, moved a stick or put a finger on
a touch panel (process time,
us): the heartbeat stops telling the system the Vita is in use a while after
it, so an untouched Vita dims and sleeps as its settings say (an OLED screen
left on the pause menu would otherwise show the HUD for hours) */
volatile unsigned long long vita_host_last_input_us;

static int stick_moved(unsigned char value)
{
	return value < 128 - 32 || value > 128 + 32;
}

/* each panel's active area (sceTouchGetPanelInfo), to scale its positions
to the screen's pixels */
static int touch_left[2], touch_top[2], touch_width[2], touch_height[2];
static struct vita_touch_tracker touch_tracker;

static void touch_start(void)
{
	int panel;

	for (panel = 0; panel < 2; panel++)
	{
		SceTouchPanelInfo info;

		sceTouchSetSamplingState(panel ? SCE_TOUCH_PORT_BACK : SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		memset(&info, 0, sizeof(info));
		if (sceTouchGetPanelInfo(panel ? SCE_TOUCH_PORT_BACK : SCE_TOUCH_PORT_FRONT, &info) >= 0 &&
			info.maxAaX > info.minAaX && info.maxAaY > info.minAaY)
		{
			touch_left[panel] = info.minAaX;
			touch_top[panel] = info.minAaY;
			touch_width[panel] = info.maxAaX - info.minAaX + 1;
			touch_height[panel] = info.maxAaY - info.minAaY + 1;
		}
		else
		{
			/* (the panels' usual areas: front 1920 x 1088, rear 1920 x
			782 from y 108) */
			touch_left[panel] = 0;
			touch_top[panel] = panel ? 108 : 0;
			touch_width[panel] = 1920;
			touch_height[panel] = panel ? 782 : 1088;
		}
	}
}

/* the fingers on both panels (and the script's), in screen pixels: the
zones they hold this frame; *started is how many came down */
static unsigned int touch_read(unsigned long long now, int *started)
{
	struct vita_touch_contact contacts[2 * SCE_TOUCH_MAX_REPORT + VITA_ZONE_COUNT];
	int count = 0, panel, index;
	static unsigned long settings_seen;
	static int settings_read;

	/* the rear pad's guard (the panel's Rear touch guard; read again
	after a change in the settings panel) */
	if (!settings_read || settings_seen != halo_settings_generation)
	{
		settings_read = 1;
		settings_seen = halo_settings_generation;
		touch_tracker.rear_guard = vita_rear_guard_named(getenv("HALO_TOUCH_REAR_GUARD"));
	}

	for (panel = 0; panel < 2; panel++)
	{
		SceTouchData data;

		memset(&data, 0, sizeof(data));
		if (sceTouchPeek(panel ? SCE_TOUCH_PORT_BACK : SCE_TOUCH_PORT_FRONT, &data, 1) < 0)
			continue;
		for (index = 0; index < (int)data.reportNum && index < SCE_TOUCH_MAX_REPORT; index++)
		{
			struct vita_touch_contact *contact = &contacts[count++];

			contact->panel = panel ? VITA_TOUCH_REAR : VITA_TOUCH_FRONT;
			contact->id = data.report[index].id;
			contact->x = (data.report[index].x - touch_left[panel]) * VITA_TOUCH_WIDTH / touch_width[panel];
			contact->y = (data.report[index].y - touch_top[panel]) * VITA_TOUCH_HEIGHT / touch_height[panel];
		}
	}
	for (index = 0; index < VITA_ZONE_COUNT; index++)
		if (pad_script_touch & (1u << index))
		{
			const struct vita_touch_zone *zone = &vita_touch_zones[index];
			struct vita_touch_contact *contact = &contacts[count++];

			contact->panel = zone->panel;
			/* (ids the panels never report) */
			contact->id = 0x100 + index;
			contact->x = (zone->left + zone->right) / 2;
			contact->y = (zone->top + zone->bottom) / 2;
		}
	return (unsigned int)vita_touch_update(&touch_tracker, contacts, count, now, started);
}

/* ---------- the gyroscope */

#define GYRO_RECORDS 64
#define DEGREES 0.017453293f

static struct vita_gyro_filter gyro_filter;
static int gyro_sampling;
static unsigned int gyro_counter, gyro_timestamp;
static int gyro_have_sample;
/* when the sensor last gave a sample, and the simulation last ran
(process time, us) */
static unsigned long long gyro_sensor_seen, gyro_simulated_at;
/* the sensor's samples counted over its first seconds, for halo.log */
static unsigned long long gyro_counted_since;
static int gyro_counted, gyro_count_logged;
/* HALO_GYRO_SIM's rates (degrees a second) */
static int gyro_sim_set = -1;
static float gyro_sim[3];

static void gyro_start(void)
{
	int result = sceMotionStartSampling();

	/* (SDL's sensor driver may have started it already) */
	gyro_sampling = result >= 0 || (unsigned int)result == SCE_MOTION_ERROR_ALREADY_SAMPLING;
	if (gyro_sampling)
		sceMotionReset();
	{
		const char *sim = getenv("HALO_GYRO_SIM");

		gyro_sim_set = sim && *sim && sscanf(sim, "%f,%f,%f", &gyro_sim[0], &gyro_sim[1], &gyro_sim[2]) >= 1;
	}
	{
		char line[96];

		snprintf(line, sizeof(line), "gyro: sampling %s (0x%08x)%s", gyro_sampling ? "on" : "off",
			(unsigned int)result, gyro_sim_set ? ", HALO_GYRO_SIM set" : "");
		vita_host_log(line);
	}
}

/* the angles the Vita turned since the last read (radians, its axes) */
static void gyro_read(unsigned long long now, float angle[3])
{
	static SceMotionSensorState records[GYRO_RECORDS];
	int simulated = pad_script_gyro_set || gyro_sim_set > 0;
	int count = 0, index;

	angle[0] = angle[1] = angle[2] = 0.0f;
	if (gyro_sampling)
	{
		memset(records, 0, sizeof(records));
		if (sceMotionGetSensorState(records, GYRO_RECORDS) >= 0)
			count = GYRO_RECORDS;
	}
	/* the records not seen before, oldest first (sorted by their counter) */
	for (index = 1; index < count; index++)
	{
		SceMotionSensorState record = records[index];
		int at = index;

		while (at > 0 && records[at - 1].counter > record.counter)
		{
			records[at] = records[at - 1];
			at--;
		}
		records[at] = record;
	}
	for (index = 0; index < count; index++)
	{
		const SceMotionSensorState *record = &records[index];
		float rate[3], accel[3], dt;

		if (gyro_have_sample && (int)(record->counter - gyro_counter) <= 0)
			continue;
		if (!record->counter && !record->timestamp)
			continue;
		dt = gyro_have_sample ? (float)(unsigned int)(record->timestamp - gyro_timestamp) * 1e-6f : 0.0f;
		gyro_have_sample = 1;
		gyro_counter = record->counter;
		gyro_timestamp = record->timestamp;
		gyro_sensor_seen = now;
		gyro_counted++;
		/* (a simulation in place of the sensor: its samples only counted) */
		if (simulated)
			continue;
		rate[0] = record->gyro.x;
		rate[1] = record->gyro.y;
		rate[2] = record->gyro.z;
		accel[0] = record->accelerometer.x;
		accel[1] = record->accelerometer.y;
		accel[2] = record->accelerometer.z;
		vita_gyro_filter_sample(&gyro_filter, rate, accel, dt, 1, angle);
	}
	/* (once, in halo.log: how often the sensor samples, and the bias then;
	a hardware report says whether the gyro works) */
	if (!gyro_counted_since)
		gyro_counted_since = now;
	else if (!gyro_count_logged && now - gyro_counted_since >= 5000000)
	{
		char line[128];

		gyro_count_logged = 1;
		snprintf(line, sizeof(line), "gyro: %d sensor samples a second; bias %+.2f %+.2f %+.2f deg/s (%d still seconds)",
			gyro_counted / 5, gyro_filter.bias[0] / DEGREES, gyro_filter.bias[1] / DEGREES,
			gyro_filter.bias[2] / DEGREES, gyro_filter.still_count);
		vita_host_log(line);
	}
	if (simulated)
	{
		float rate[3];
		float dt = gyro_simulated_at ? (float)(now - gyro_simulated_at) * 1e-6f : 0.0f;

		for (index = 0; index < 3; index++)
			rate[index] = ((pad_script_gyro_set ? pad_script_gyro[index] : 0.0f) +
				(gyro_sim_set > 0 ? gyro_sim[index] : 0.0f)) * DEGREES;
		/* (in samples of 5 ms, as the sensor's, however long the frame
		took - Vita3K's are 120 ms - up to a quarter of a second) */
		if (dt > 0.25f)
			dt = 0.25f;
		while (dt > 0.0f)
		{
			float step = dt > 0.005f ? 0.005f : dt;

			vita_gyro_filter_sample(&gyro_filter, rate, NULL, step, 0, angle);
			dt -= step;
		}
		gyro_simulated_at = now;
	}
	else
		gyro_simulated_at = 0;
}

void vita_gyro_status(char *text, int size)
{
	unsigned long long now = sceKernelGetProcessTimeWide();
	int rate[3], axis;

	for (axis = 0; axis < 3; axis++)
	{
		float degrees = gyro_filter.rate[axis] / DEGREES;

		rate[axis] = degrees > 999.0f ? 999 : degrees < -999.0f ? -999 : (int)(degrees + (degrees < 0 ? -0.5f : 0.5f));
	}
	/* (the settings panel's line: in the language chosen, lang.c) */
	if (!gyro_sampling && !gyro_simulated_at)
		snprintf(text, (size_t)size, "%s", T("Gyro: no sensor"));
	else if (!gyro_simulated_at && (!gyro_sensor_seen || now - gyro_sensor_seen > 1000000))
		snprintf(text, (size_t)size, "%s", T("Gyro: no samples"));
	else
		/* (degrees a second, the game's way round: yaw left +, pitch up +) */
		snprintf(text, (size_t)size, T("Gyro: yaw %+d pitch %+d roll %+d %s"), rate[1], rate[0], rate[2],
			gyro_simulated_at ? T("simulated") : gyro_filter.still_count ? T("learnt") : T("lay still"));
}

void vita_host_pad_read(struct vita_host_pad *pad)
{
	static int started;
	SceCtrlData data;
	unsigned long long now;
	int touches_started = 0;

	if (!started)
	{
		started = 1;
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
		touch_start();
		gyro_start();
	}
	memset(&data, 0, sizeof(data));
	data.lx = data.ly = data.rx = data.ry = 128;
	sceCtrlPeekBufferPositive(0, &data, 1);
	pad->buttons = data.buttons;
	pad->lx = data.lx;
	pad->ly = data.ly;
	pad->rx = data.rx;
	pad->ry = data.ry;

	now = sceKernelGetProcessTimeWide();
	pad_script_poll(now);
	pad_script_apply(pad, now);
	pad->touch = touch_read(now, &touches_started);
	gyro_read(now, pad->gyro);
	/* (a finger coming down is input too; one left resting on the rear
	pad is not, after its first frame; the gyroscope never is) */
	if (data.buttons || stick_moved(data.lx) || stick_moved(data.ly) || stick_moved(data.rx) || stick_moved(data.ry) ||
		touches_started || !vita_host_last_input_us)
		vita_host_last_input_us = now;
}
