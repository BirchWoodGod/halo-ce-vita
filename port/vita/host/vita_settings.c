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

The Profile row at the top sets the speed-related rows at once
(Performance, Balanced - the defaults - or Quality); it reads Custom when
those rows match none of them. Rows marked * apply after a restart.
*/

#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
	{ "Profile", "HALO_PROFILE", 0, 4, { "performance", "balanced", "quality", "custom" },
		{ "Performance", "Balanced", "Quality", "Custom" }, "Sets resolution, detail, update rates at once", 1 },
	{ "Performance overlay", "XV_FPS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Frames per second and frame times, top right", 0 },
	{ "FPS counter", "HALO_FRAMERATE_COUNTER", 0, 2, { "0", "1" }, { "Off", "On" },
		"The game's frame counter, bottom right", 0 },
	{ "Frame limit", "HALO_FRAME_CAP", 0, 3, { "30", "60", "0" }, { "30 FPS", "60 FPS", "Off" },
		"The most frames shown a second", 0 },
	{ "Render resolution", "HALO_RENDER_SCALE", 1, 5, { "1", "0.875", "0.75", "0.625", "0.5" },
		{ "100%", "88%", "75%", "63%", "50%" }, "Lower is faster and softer (after a restart)", 2 },
	{ "Aspect ratio", "HALO_DISPLAY_WIDTH", 1, 2, { "848", "640" }, { "16:9", "4:3" },
		"4:3: the Xbox's framing, black bars (restart)", 0 },
	{ "Upscale filter", "HALO_UPSCALE_FILTER", 0, 2, { "0", "1" }, { "Smooth", "Sharp" },
		"Scaling to the screen: Sharp = crisp pixels", 0 },
	{ "Model detail", "HALO_MODEL_LOD_SCALE", 0, 4, { "1", "0.75", "0.5", "0.35" },
		{ "High", "Medium", "Low", "Lowest" }, "Level of detail of characters and vehicles", 2 },
	{ "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", 0, 4, { "0", "4", "8", "12" },
		{ "Off", "Tiny", "Small", "Medium" }, "Skip objects this small on screen", 2 },
	{ "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", 0, 3, { "1", "2", "4" },
		{ "Every tick", "Half", "Quarter" }, "How often static props are updated", 2 },
	{ "Smooth weapon motion", "HALO_INTERPOLATE_FIRST_PERSON", 0, 2, { "1", "0" }, { "On", "Off" },
		"The weapon's animation blended between game ticks", 0 },
	{ "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Full", "Half", "Third" }, "How often object lighting is recomputed", 2 },
	{ "Sound voices", "HALO_SOUND_CHANNELS", 1, 4, { "16", "24", "32", "0" },
		{ "16", "24", "32", "Original" }, "Fewer is faster; the AI then differs (after a restart)", 3 },
	{ "Sound occlusion", "HALO_SOUND_OBSTRUCTION_TICKS", 0, 3, { "1", "3", "6" },
		{ "Every tick", "Every 3rd", "Every 6th" }, "How often muffling behind walls is rechecked", 1 },
	{ "Look sensitivity", "XV_LOOK_SENS", 0, 6, { "50", "75", "100", "125", "150", "200" },
		{ "50%", "75%", "100%", "125%", "150%", "200%" }, "Right stick turning speed", 2 },
	{ "Crouch", "HALO_CROUCH_TOGGLE", 0, 2, { "1", "0" }, { "Toggle", "Hold" },
		"D-pad down: a press crouches, the next stands (Toggle)", 0 },
	{ "Invert look", "XV_INVERT_Y", 0, 2, { "0", "1" }, { "No", "Yes" }, "Reverse the right stick's up and down", 0 },
	{ "Stick deadzone", "XV_DEADZONE", 0, 4, { "0", "5", "10", "15" }, { "Off", "5%", "10%", "15%" },
		"Raise if the sticks drift", 0 },
};

#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* the profiles (the Profile row's first three choices): the value of each
row a profile sets. Balanced is the release's defaults (the rows' own) */
#define PROFILE_CUSTOM 3
#define PROFILE_ROWS 6

static const char *const profile_variables[PROFILE_ROWS] = {
	"HALO_RENDER_SCALE", "HALO_MODEL_LOD_SCALE", "HALO_MIN_OBJECT_PIXELS", "HALO_SCENERY_UPDATE_DIVISOR",
	"HALO_LIGHTING_REFRESH_DIVISOR", "HALO_SOUND_OBSTRUCTION_TICKS",
};

static const char *const profile_values[PROFILE_CUSTOM][PROFILE_ROWS] = {
	/* Performance: 50%, Low, Small, Quarter, Third, Every 6th */
	{ "0.5", "0.5", "8", "4", "3", "6" },
	/* Balanced (the release's defaults): 75%, Low, Small, Quarter, Third,
	Every 3rd */
	{ "0.75", "0.5", "8", "4", "3", "3" },
	/* Quality: 100%, High, Off, Every tick, Full, Every tick */
	{ "1", "1", "0", "1", "1", "1" },
};

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_INTERPOLATION", "false" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* (the netcode is the platform's default, distributed: lockstep had
	been the Vita's because a match's client could not reach its own host -
	that was the Vita refusing a sendto on a connected datagram socket,
	vita_net.c; with it fixed the distributed netcode plays, with less
	waiting each frame. HALO_NETCODE=lockstep in env.txt plays the Xbox's) */
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

static struct setting *setting_named(const char *variable)
{
	int index;

	for (index = 0; index < SETTING_COUNT; index++)
		if (strcmp(settings[index].variable, variable) == 0)
			return &settings[index];
	return NULL;
}

/* the profile the rows match, or Custom */
static int matching_profile(void)
{
	int profile, row;

	for (profile = 0; profile < PROFILE_CUSTOM; profile++)
	{
		for (row = 0; row < PROFILE_ROWS; row++)
		{
			const struct setting *setting = setting_named(profile_variables[row]);

			if (!setting || strcmp(setting->values[setting->choice], profile_values[profile][row]) != 0)
				break;
		}
		if (row == PROFILE_ROWS)
			return profile;
	}
	return PROFILE_CUSTOM;
}

/* a profile's values set in its rows; nonzero if a row that waits for a
restart changed */
static int apply_profile(int profile)
{
	int row, restart = 0;

	if (profile < 0 || profile >= PROFILE_CUSTOM)
		return 0;
	for (row = 0; row < PROFILE_ROWS; row++)
	{
		struct setting *setting = setting_named(profile_variables[row]);
		int choice;

		if (!setting)
			continue;
		choice = find_choice(setting, profile_values[profile][row]);
		if (choice != setting->choice)
		{
			setting->choice = choice;
			if (setting->restart)
				restart = 1;
		}
		setenv(setting->variable, setting->values[setting->choice], 1);
	}
	return restart;
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

	/* env.txt (already read) gives a starting choice (HALO_PROFILE=
	performance, balanced or quality there: that profile's, under the rows
	env.txt names itself); settings.txt, the panel's own file, has the last
	word */
	{
		const char *profile = getenv("HALO_PROFILE");
		int profile_index;

		for (profile_index = 0; profile && profile_index < PROFILE_CUSTOM; profile_index++)
			if (strcasecmp(profile, settings[0].values[profile_index]) == 0)
			{
				int row;

				for (row = 0; row < PROFILE_ROWS; row++)
				{
					struct setting *setting = setting_named(profile_variables[row]);

					if (setting && !getenv(setting->variable))
						setting->choice = find_choice(setting, profile_values[profile_index][row]);
				}
			}
	}
	for (index = 0; index < SETTING_COUNT; index++)
	{
		const char *value = getenv(settings[index].variable);

		if (value && strcmp(settings[index].variable, "HALO_PROFILE") != 0)
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
			/* (the profile line is what the rows were: worked out again) */
			for (index = 0; index < SETTING_COUNT; index++)
				if (strcmp(settings[index].variable, line) == 0 && strcmp(line, "HALO_PROFILE") != 0)
					settings[index].choice = find_choice(&settings[index], equals + 1);
		}
		fclose(file);
	}
	/* (the profile is what the rows are: Custom when env.txt or the panel
	set them apart from every profile) */
	setting_named("HALO_PROFILE")->choice = matching_profile();
	for (index = 0; index < SETTING_COUNT; index++)
		setenv(settings[index].variable, settings[index].values[settings[index].choice], 1);
	for (index = 0; index < (int)(sizeof(fixed_defaults) / sizeof(fixed_defaults[0])); index++)
		setenv(fixed_defaults[index][0], fixed_defaults[index][1], 0);
	{
		/* (in halo.log: the profile and the rows it sets, the picture's) */
		char message[300];
		int length = snprintf(message, sizeof(message), "settings: profile %s:", settings[0].names[settings[0].choice]);

		for (index = 0; index < PROFILE_ROWS && length < (int)sizeof(message); index++)
			length += snprintf(message + length, sizeof(message) - length, " %s=%s", profile_variables[index],
				getenv(profile_variables[index]));
		if (length < (int)sizeof(message))
			snprintf(message + length, sizeof(message) - length, " HALO_DISPLAY_WIDTH=%s HALO_UPSCALE_FILTER=%s",
				getenv("HALO_DISPLAY_WIDTH"), getenv("HALO_UPSCALE_FILTER"));
		vita_host_log(message);
	}
}

static void show(void)
{
	char text[2048];
	int length, index;

	length = snprintf(text, sizeof(text), "SETTINGS");
	for (index = 0; index < SETTING_COUNT && length < (int)sizeof(text); index++)
	{
		const struct setting *setting = &settings[index];

		char label[32];

		/* (a row that applies after a restart: marked *) */
		snprintf(label, sizeof(label), "%s%s", setting->label, setting->restart ? "*" : "");
		int last = setting == &settings[0] ? PROFILE_CUSTOM - 1 : setting->count - 1;

		length += snprintf(text + length, sizeof(text) - length, "\n%-21s%c %s %c", label,
			setting->choice > 0 ? '<' : ' ', setting->names[setting->choice],
			setting->choice < last ? '>' : ' ');
	}
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\n%s",
			restart_pending ? "Restart the game for this change. O: close" : settings[selected].help);
	vgxm_menu_set(text, selected + 1);
}

static void change(int step)
{
	struct setting *setting = &settings[selected];
	int choice = setting->choice + step;

	if (choice < 0 || choice >= setting->count)
		return;
	/* (Custom is not chosen: the Profile row reads it when a row it sets
	is changed) */
	if (strcmp(setting->variable, "HALO_PROFILE") == 0 && choice == PROFILE_CUSTOM)
		return;
	setting->choice = choice;
	setenv(setting->variable, setting->values[choice], 1);
	if (setting->restart)
		restart_pending = 1;
	if (strcmp(setting->variable, "HALO_PROFILE") == 0)
	{
		/* (a profile sets its rows; Custom leaves them as they are) */
		if (apply_profile(choice))
			restart_pending = 1;
	}
	else
	{
		/* (a row a profile sets, changed: the profile it now matches, or
		Custom) */
		struct setting *profile = setting_named("HALO_PROFILE");

		profile->choice = matching_profile();
		setenv(profile->variable, profile->values[profile->choice], 1);
	}
	if (strcmp(setting->variable, "XV_FPS") == 0)
		vgxm_overlay_enable(choice != 0);
	if (strcmp(setting->variable, "HALO_UPSCALE_FILTER") == 0)
		vgxm_upscale_filter_set(choice);
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
