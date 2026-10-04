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
#include <pthread.h>
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
	{ "Online (experimental)", "HALO_NET_ONLINE", 1, 2, { "false", "true" }, { "Off", "On" },
		"Internet invites; restart after changing. See README", 0 },
	{ "Performance overlay", "XV_FPS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Frames per second and frame times, top right", 0 },
	{ "FPS counter", "HALO_FRAMERATE_COUNTER", 0, 2, { "0", "1" }, { "Off", "On" },
		"The game's frame counter, bottom right", 0 },
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

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_INTERPOLATION", "false" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* (the netcode is the distributed one, the only one network version 9
	plays: a match's client once could not reach its own host because the
	Vita refused a sendto on a connected datagram socket, fixed in
	vita_net.c) */
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
	/* Boolean environment overrides also accept 0/1, like port_config.c. */
	if (strcmp(setting->variable, "HALO_NET_ONLINE") == 0)
		return strcmp(value, "1") == 0 || strcmp(value, "TRUE") == 0;
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
			restart_pending ? "Restart the game for this change. O: close" : settings[selected].help);
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

/* Publish from the game/event thread; only the input thread writes the menu. */
static pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;
static char pending_message[2048];
static int message_pending, message_visible;

void vita_settings_message(const char *title, const char *text)
{
    char formatted[2048];
    size_t used = 0;
    int column = 0;
    const char *parts[] = {title, "\n\n", text, "\n\nCross / Circle: close"};
    for (int part = 0; part < 4; ++part)
    {
        const char *cursor = parts[part];
        while (*cursor && used < sizeof(formatted) - 1)
        {
            if (*cursor != '\n')
            {
                size_t word = strcspn(cursor, " \n");
                if (column && word && (cursor == parts[part] || cursor[-1] == ' ') && column + word > 46)
                {
                    formatted[used++] = '\n';
                    column = 0;
                    continue;
                }
            }
            char ch = *cursor++;
            if (column >= 46 && ch != '\n')
            {
                formatted[used++] = '\n';
                column = 0;
                if (used >= sizeof(formatted) - 1) break;
            }
            formatted[used++] = ch;
            column = ch == '\n' ? 0 : column + 1;
        }
    }
    formatted[used] = 0;
    pthread_mutex_lock(&message_lock);
    memcpy(pending_message, formatted, used + 1);
    message_pending = 1;
    pthread_mutex_unlock(&message_lock);
}

int vita_settings_input(const struct vita_host_pad *pad)
{
	unsigned long buttons = pad->buttons;
	unsigned long pressed = buttons & ~previous_buttons;
	int both = (buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_START);
	unsigned long long now = now_us();
    int message_opened = 0;

	previous_buttons = buttons;
    pthread_mutex_lock(&message_lock);
    if (message_pending)
    {
        vgxm_menu_set(pending_message, -1);
        message_pending = 0;
        message_visible = 1;
        message_opened = 1;
        panel_open = 0;
    }
    pthread_mutex_unlock(&message_lock);
    if (message_visible)
    {
        both_since = 0;
        if (!message_opened && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE)))
        {
            message_visible = 0;
            vgxm_menu_set(NULL, 0);
        }
        return 1;
    }
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
