/*
VITA_INPUT.C

The Vita's buttons and sticks for port/vita/platform/vita_pad.c (sceCtrl, in
the wide analog mode real firmware needs for the sticks to move).
*/

#include <psp2/ctrl.h>

#include <string.h>

#include "vita_host.h"

void vita_host_pad_read(struct vita_host_pad *pad)
{
	static int started;
	SceCtrlData data;

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
}
