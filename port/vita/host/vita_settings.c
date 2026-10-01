/*
VITA_SETTINGS.C

The settings panel: hold SELECT and START together for a moment, in play
or in the menus, and a panel over the game lists the Vita's quality and
control settings. Up and down choose one, left and right change it, circle
(or SELECT+START again) closes the panel; the game sees no buttons while it
is open. Each setting is one of the environment variables the port already
reads (HALO_MODEL_LOD_SCALE...), kept in ux0:data/haloce-vita/settings.txt
and set before the game starts; a change bumps halo_settings_generation,
and the readers that can take a new value mid-game read theirs again (the
render resolution sizes the screen's targets once: it waits for a restart).

The release's defaults, the ones measured best on the Vita, are set here
too, under whatever env.txt and settings.txt say.
*/

#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_gxm.h"
#include "vita_host.h"

#define SETTINGS_FILE "ux0:data/haloce-vita/settings.txt"
#define MAXIMUM_CHOICES 6

/* read again by the readers that take a change mid-game (port_config.c) */
extern volatile unsigned long halo_settings_generation;

struct setting
{
	const char *label;
	const char *variable;
	int restart;
	int count;
	const char *values[MAXIMUM_CHOICES];
	const char *names[MAXIMUM_CHOICES];
	const char *help;
	int choice;
};

static struct setting settings[] = {
	{ "Performance overlay", "XV_FPS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Frames per second and frame times, top right", 0 },
	{ "Frame limit", "HALO_FRAME_CAP", 0, 3, { "30", "60", "0" }, { "30 FPS", "60 FPS", "Off" },
		"The most frames shown a second", 0 },
	{ "Render resolution", "HALO_RENDER_SCALE", 1, 5, { "1", "0.875", "0.75", "0.625", "0.5" },
		{ "100%", "88%", "75%", "63%", "50%" }, "Lower is faster and softer (after a restart)", 2 },
	{ "Model detail", "HALO_MODEL_LOD_SCALE", 0, 4, { "1", "0.75", "0.5", "0.35" },
		{ "High", "Medium", "Low", "Lowest" }, "Level of detail of characters and vehicles", 2 },
	{ "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", 0, 4, { "0", "4", "8", "12" },
		{ "Off", "Tiny", "Small", "Medium" }, "Skip objects this small on screen", 2 },
	{ "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", 0, 3, { "1", "2", "4" },
		{ "Every tick", "Half", "Quarter" }, "How often static props are updated", 2 },
	{ "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Full", "Half", "Third" }, "How often object lighting is recomputed", 2 },
	{ "Look sensitivity", "XV_LOOK_SENS", 0, 6, { "50", "75", "100", "125", "150", "200" },
		{ "50%", "75%", "100%", "125%", "150%", "200%" }, "Right stick turning speed", 2 },
	{ "Invert look", "XV_INVERT_Y", 0, 2, { "0", "1" }, { "No", "Yes" }, "Reverse the right stick's up and down", 0 },
	{ "Stick deadzone", "XV_DEADZONE", 0, 4, { "0", "5", "10", "15" }, { "Off", "5%", "10%", "15%" },
		"Raise if the sticks drift", 0 },
};

#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_INTERPOLATION", "false" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* the Xbox game's lockstep netcode: split screen is a host and its
	own client on one machine, which the distributed netcode (built for
	the desktop's internet play) left behind its own host on the Vita - a
	player could look around but not move. A machine that joins a host
	plays the host's netcode either way */
	{ "HALO_NETCODE", "lockstep" },
};

static int panel_open, selected;
static unsigned long long both_since, last_move;
static unsigned long previous_buttons;
static int restart_pending;

static unsigned long long now_us(void)
{
	return sceKernelGetProcessTimeWide();
}

static int find_choice(const struct setting *setting, const char *value)
{
	int index;

	for (index = 0; index < setting->count; index++)
		if (strcmp(setting->values[index], value) == 0)
			return index;
	/* (a value not on the list, from env.txt: the nearest by number) */
	{
		double wanted = atof(value), best_distance = 1e30;
		int best = setting->choice;

		for (index = 0; index < setting->count; index++)
		{
			double distance = atof(setting->values[index]) - wanted;

			if (distance < 0)
				distance = -distance;
			if (distance < best_distance)
			{
				best_distance = distance;
				best = index;
			}
		}
		return best;
	}
}

static void save(void)
{
	FILE *file = fopen(SETTINGS_FILE, "w");
	int index;

	if (!file)
		return;
	for (index = 0; index < SETTING_COUNT; index++)
		fprintf(file, "%s=%s\n", settings[index].variable, settings[index].values[settings[index].choice]);
	fclose(file);
}

void vita_settings_load(void)
{
	FILE *file = fopen(SETTINGS_FILE, "r");
	char line[256];
	int index;

	/* env.txt (already read) gives a starting choice; settings.txt, the
	panel's own file, has the last word */
	for (index = 0; index < SETTING_COUNT; index++)
	{
		const char *value = getenv(settings[index].variable);

		if (value)
			settings[index].choice = find_choice(&settings[index], value);
	}
	if (file)
	{
		while (fgets(line, sizeof(line), file))
		{
			char *equals = strchr(line, '=');
			char *end = line + strlen(line);

			while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
				*--end = 0;
			if (!equals)
				continue;
			*equals = 0;
			for (index = 0; index < SETTING_COUNT; index++)
				if (strcmp(settings[index].variable, line) == 0)
					settings[index].choice = find_choice(&settings[index], equals + 1);
		}
		fclose(file);
	}
	for (index = 0; index < SETTING_COUNT; index++)
		setenv(settings[index].variable, settings[index].values[settings[index].choice], 1);
	for (index = 0; index < (int)(sizeof(fixed_defaults) / sizeof(fixed_defaults[0])); index++)
		setenv(fixed_defaults[index][0], fixed_defaults[index][1], 0);
}

static void show(void)
{
	char text[2048];
	int length, index;

	length = snprintf(text, sizeof(text), "SETTINGS");
	for (index = 0; index < SETTING_COUNT && length < (int)sizeof(text); index++)
	{
		const struct setting *setting = &settings[index];

		length += snprintf(text + length, sizeof(text) - length, "\n%-22s%c %s %c", setting->label,
			setting->choice > 0 ? '<' : ' ', setting->names[setting->choice],
			setting->choice < setting->count - 1 ? '>' : ' ');
	}
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\n%s",
			restart_pending ? "Restart the game for the new resolution. O: close" : settings[selected].help);
	vgxm_menu_set(text, selected + 1);
}

static void change(int step)
{
	struct setting *setting = &settings[selected];
	int choice = setting->choice + step;

	if (choice < 0 || choice >= setting->count)
		return;
	setting->choice = choice;
	setenv(setting->variable, setting->values[choice], 1);
	if (setting->restart)
		restart_pending = 1;
	if (strcmp(setting->variable, "XV_FPS") == 0)
		vgxm_overlay_enable(choice != 0);
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
}

static void close_panel(void)
{
	panel_open = 0;
	vgxm_menu_set(NULL, 0);
}

int vita_settings_input(const struct vita_host_pad *pad)
{
	unsigned long buttons = pad->buttons;
	unsigned long pressed = buttons & ~previous_buttons;
	int both = (buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_START);
	unsigned long long now = now_us();

	previous_buttons = buttons;
	if (both)
	{
		if (!both_since)
			both_since = now;
		/* held for 0.8 s: the panel opens or closes, once per hold */
		if (both_since != 1 && now - both_since > 800000)
		{
			if (panel_open)
				close_panel();
			else
			{
				panel_open = 1;
				show();
			}
			both_since = 1;
		}
		/* (the pair never reaches the game while held: no pause menu) */
		return 1;
	}
	both_since = 0;
	if (!panel_open)
		return 0;
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		close_panel();
		return 1;
	}
	{
		/* up and down step, and repeat while held */
		int step = 0;

		if (pressed & VITA_BUTTON_UP)
			step = -1;
		else if (pressed & VITA_BUTTON_DOWN)
			step = 1;
		else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 250000)
			step = (buttons & VITA_BUTTON_UP) ? -1 : 1;
		if (step)
		{
			selected = (selected + step + SETTING_COUNT) % SETTING_COUNT;
			last_move = now;
		}
	}
	if (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS))
		change((pressed & VITA_BUTTON_LEFT) ? -1 : 1);
	show();
	return 1;
}
