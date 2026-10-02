/*
VITA_INPUT.C

The Vita's buttons and sticks for port/vita/platform/vita_pad.c (sceCtrl, in
the wide analog mode real firmware needs for the sticks to move).

(debug) HALO_PAD_FILE=ux0:data/haloce-vita/pad.txt: presses for automated
tests in Vita3K, without typing into its window. The file is looked for every
half second; its steps, "name:hold_ms:pause_ms" separated by spaces, are
pressed one after another and the file is deleted. Names: x c z v (cross
circle square triangle), up down left right, start select, l r, w a s d (the
left stick), i j k ll (the right stick), a+b for buttons together.
*/

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"

#define MAXIMUM_PAD_STEPS 64

struct pad_step
{
	unsigned int buttons;
	int lx, ly, rx, ry;
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

	if (!strcmp(name, "w"))
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

static void pad_script_apply(struct vita_host_pad *pad, unsigned long long now)
{
	while (pad_step_index < pad_step_count)
	{
		const struct pad_step *step = &pad_steps[pad_step_index];
		unsigned long long elapsed_ms = (now - pad_step_start) / 1000;

		if (elapsed_ms < step->hold_ms)
		{
			pad->buttons |= step->buttons;
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

void vita_host_pad_read(struct vita_host_pad *pad)
{
	static int started;
	SceCtrlData data;
	unsigned long long now;

	if (!started)
	{
		started = 1;
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
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
}
