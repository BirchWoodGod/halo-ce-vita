/*
VITA_SETTINGS.C

The settings panel: hold SELECT and START together for a moment, in play
or in the menus, and a panel over the game shows the Vita's settings in
four tabs that L and R switch between: Graphics, Controls, Audio and
Multiplayer, and Dev once "Show dev settings" (Controls, Advanced) is on.
No tab is longer than a screen: the rows few players change open from a
row marked > as a page of their own (Graphics' Advanced; Controls' Button
layout, Touch zones, Gyro settings and Advanced; Multiplayer's Play and
Modded maps; Dev's A/B switches), and
circle goes back.
Up and down choose a line, left and right change it, cross does what an
action line says, circle (or SELECT+START again) closes the panel; the
game sees no buttons while it is open. Each row's value is drawn in a
column of its own (a '\x02' between label and value, vita_gxm.c
menu_build), the selected row's help on one line below the rows and the
panel's buttons on the line under it. Each setting is one of the
environment variables the port already reads (HALO_MODEL_LOD_SCALE...),
kept in ux0:data/haloce-vita/settings.txt and set before the game starts;
which page shows a row changes nothing of that. A change
bumps halo_settings_generation, and the readers that can take a new value
mid-game read theirs again (the render resolution and the aspect ratio too:
the screen's targets are made again between two frames, d3d8_gxm.c
screen_settings_apply; if the memory for the new size cannot be had, the
panel says the change waits for a restart). Render resolution's Dynamic
lets the GPU's load set the scale, frame by frame, between the Dynamic
minimum (shown while Dynamic is chosen) and 100% (d3d8_gxm.c, the dynamic
resolution).

The release's defaults, the ones measured best on the Vita, are set here
too, under whatever env.txt and settings.txt say.

The Profile row at the top of Graphics sets the speed-related rows at once
(Performance, Balanced - the defaults - or Quality); it reads Custom when
those rows match none of them (the render resolution, on the tab, and the
detail and update rates, the sun rays, the shadows, lights, effects and
particles and the AI's think rate, under Advanced, and Audio's sound
updates and occlusion). Quality is the Xbox game's every one. Rows marked *
apply after a restart (the sound voices, the network, most dev switches).

Controls has the rows players change (look, crouch, gyro aiming, the rear
touch guard) and three pages laid out as the Xbox controller. Button
layout puts each Xbox button (A B X Y, Black, White, the triggers, the
sticks' clicks, Back) on a Vita button of the player's choosing, in play.
Touch zones: each zone (the front screen's top corners and its left and
right edges, the rear pad's halves; vita_controls.c says where they are
and when a finger counts; Rear touch guard keeps the hands holding the
Vita off the rear ones) presses an Xbox button, and the page draws the
zones beside its rows (touch_diagram). Button layout's first row, Button
icons, chooses the game's button icons: Xbox (the default, the game's own)
or PlayStation, each icon then showing the Vita button its Xbox button is
on now (vita_controls.h, source/interface/hud_draw.c; live, kept by Reset
controls); with PlayStation the panel too names the Xbox buttons by what
they do (Jump, Melee...) and the guide's steps the Vita's buttons. Gyro settings: gyro aiming
(vita_controls.h) turns the view as the Vita turns, on top of the right
stick - Off (the default), On, While zoomed, or While holding the Gyro
button (which then does nothing else in play) - with its sensitivity (1x:
the view turns as far as the Vita), vertical direction and whether turning
is the Vita's yaw or its roll; a line shows the gyroscope's rates now and
whether its bias was learnt (it is, each time the Vita lies still for a
second). Advanced: the sticks' deadzone, Reset controls (look, crouch,
buttons and touch back as shipped; the gyro's rows and Show dev settings
stay) and Show dev settings.

Multiplayer is for playing with other Vitas: Connection, the ad hoc room
(with the connection Ad hoc), Join with a code (see below) and Modded maps,
then lines saying this Vita's name and address and what the game is doing:
looking for games and how many it found, hosting and how many players are
in (of the most the game takes), in another's lobby, in a game; and,
hosting online, the game's short code (ABCD-EFGH) for others to type in.
Hosting, joining and co-op are the game's own Multiplayer menu's: with Halo
PC's bitmaps.map and loc.map in the maps folder, OpenCE's screens (server
browser, Server Setup, Direct Link: port/linux/game/menu_functions.c), else
the Xbox's (System Link, where Y creates a game); then a line says "Online
menus need bitmaps.map, loc.map: README" (halo_pc_menus_state).

- "Connection" (after a restart) chooses how the Vitas reach each other:
  Same Wi-Fi, the default, the system link that works on hardware; Ad hoc
  (p2p_adhoc.c, vita_net.c; not yet verified on hardware): Vitas side by
  side without a router, in the room chosen on the tab (the group joined
  through the system's ad hoc dialog when the game's System Link screen
  needs it); or Online (internet play, port/linux/src/p2p.c; experimental).
- "Join with a code" (Online, on a Vita without the PC menus, whose Direct
  Link does it in the game) types the host's code in with the D-pad, then
  shows Join's steps with how the lookup goes, and Cross opens the game's
  System Link screen (port/linux/game/system_link_shortcut.c), where the
  host's game shows once reached. The steps name the buttons as the game's
  menus do, the Xbox's (A is Cross there, B Circle, X Square, Y Triangle).
- The hosted game's settings (HALO_NET_LOBBY_NAME, its printable ASCII, 15
  characters at most, the Vita's user name until one is typed;
  HALO_NET_MAX_PLAYERS, 2 to 16; HALO_NET_HOST_PUBLIC, OpenCE's name;
  HALO_NET_LOBBY_PASSWORD) are the game's Server Setup's, which sets them as
  this panel sets a row (vita_settings_set) and settings.txt keeps them; the
  Play page that showed them is gone (its rows are kept on a page no row
  opens). So is co-op's own visibility (HALO_NET_COOP_PUBLIC, OpenCE's
  network.coop_public: Private unless chosen), which X on the waiting screen
  of co-op hosted online sets (port/linux/game/coop_menu.c). 1.0.3's
  "Online games" row (HALO_NET_LOBBY_PUBLIC) loads as Visibility; 1.0.3's Co-op page variables load as Off.

Multiplayer's Modded maps page lists the maps in the maps folder that are not the Xbox's own:
name, size, Xbox or Custom Edition (CE; CE+OS for OpenSauce's .yelo), and
whether it is on. Left and right turn one off or on (an off map stays in
the folder but leaves the level list: HALO_MAPS_DISABLED, read by
port/linux/game/custom_edition_maps.c each time the list opens); square
deletes one, with its picture and description, after a confirmation. The
"PC maps" switch (Custom Edition maps in the level list) is there too, with
a warning when the Custom Edition resource maps those need (bitmaps.map,
sounds.map, loc.map) are not in the folder (copy them in, or the Halo CE
installer: with one in ux0:data/haloce-vita, "Extract PC files" takes them
from it, vita_ce_installer.c), and "Map downloads": whether a
host's map is offered for download in a game joined without it (Ask, the
default, warns in a game joined from the public lobby; Not public games
asks only in the others; Never: port/linux/game/map_share.c).

Dev holds a few switches for testers (each a debug environment variable;
most are read once, at start-up, and say so; the renderer's A/B ones on a
page of their own) and "Save report", which
copies halo.log, halo-prev.log, settings.txt, env.txt and the newest crash
dump into ux0:data/haloce-vita/report-<date>/ for sending. A dev switch is
saved in settings.txt only while it is on; off, env.txt's value (or the
default) applies. When any is on, halo.log says so near its top. "Ad hoc
dialog" (how the system's dialog joins a group) is there too, out of the
Multiplayer tab's way.
*/

#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/system_param.h>

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "p2p.h"
#include "system_link_shortcut.h"
#include "vita_controls.h"
#include "vita_gxm.h"
#include "vita_host.h"

#define DATA_DIRECTORY "ux0:data/haloce-vita"
#define SETTINGS_FILE DATA_DIRECTORY "/settings.txt"
/* where the system writes its crash dumps (psp2core-*.psp2dmp) */
#define DUMP_DIRECTORY "ux0:data"
#define MAXIMUM_CHOICES 16
/* a code's characters as typed (p2p.h shows them ABCD-EFGH) */
#define P2P_CODE_LENGTH_TYPED 8

/* read again by the readers that take a change mid-game (port_config.c) */
extern volatile unsigned long halo_settings_generation;
/* the render resolution or aspect ratio asked for could not be made
(d3d8_gxm.c screen_settings_apply): it waits for a restart */
int halo_screen_restart_needed(void);
/* whether the map file `name` (no extension) has its tags loaded
(cache_files.c): such a map is not deleted */
int halo_cache_map_in_use(const char *name);

/* the pages: the tabs L and R switch between, then the pages a row of a tab
opens (circle goes back to the tab) */
enum
{
	TAB_GRAPHICS,
	TAB_CONTROLS,
	TAB_AUDIO,
	TAB_MULTIPLAYER,
	TAB_DEV,
	TAB_COUNT,

	PAGE_GRAPHICS_ADVANCED = TAB_COUNT,
	PAGE_BUTTONS,
	PAGE_TOUCH,
	PAGE_GYRO,
	PAGE_CONTROLS_ADVANCED,
	PAGE_PLAY,
	PAGE_MAPS,
	PAGE_DEV_AB,
	PAGE_COUNT
};

/* each page's name and the tab it is on (a tab's own: itself) */
static const struct
{
	const char *name;
	int tab;
} pages[PAGE_COUNT] = {
	{ "Graphics", TAB_GRAPHICS },
	{ "Controls", TAB_CONTROLS },
	{ "Audio", TAB_AUDIO },
	{ "Multiplayer", TAB_MULTIPLAYER },
	{ "Dev", TAB_DEV },
	{ "Advanced", TAB_GRAPHICS },
	{ "Button layout", TAB_CONTROLS },
	{ "Touch zones", TAB_CONTROLS },
	{ "Gyro settings", TAB_CONTROLS },
	{ "Advanced", TAB_CONTROLS },
	{ "Play", TAB_MULTIPLAYER },
	{ "Modded maps", TAB_MULTIPLAYER },
	{ "A/B switches", TAB_DEV },
};

enum
{
	/* a line whose value left and right change (the default) */
	KIND_CHOICE,
	/* a line that does something when chosen (cross, or right) */
	KIND_ACTION,
	/* a line of text (the Play page's lobby name and password): cross
	opens the system's keyboard (vita_ime.c); kept in play_texts */
	KIND_TEXT,
};

enum
{
	ACTION_NONE,
	ACTION_JOIN_CODE,
	ACTION_BROWSE,
	ACTION_SAVE_REPORT,
	ACTION_RESET_CONTROLS,
	ACTION_HOST,
	ACTION_JOIN,
	/* the Campaign screen, where Y on the difficulty hosts co-op */
	ACTION_CAMPAIGN,
	/* the ad hoc room's group joined (the system's dialog), for the game's
	System Link screen */
	ACTION_ADHOC_JOIN,
	/* opens the page `opens` */
	ACTION_PAGE,
	/* the Custom Edition installer's maps taken (vita_ce_installer.c) */
	ACTION_CE_EXTRACT,
};

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
	/* the page the row is on (a TAB_* or PAGE_*) */
	int page;
	int kind;
	int action;
	/* a dev switch: its first choice is the default (an empty value: the
	variable unset), it is saved only when on, and env.txt's value stands
	while settings.txt does not name it */
	int dev;
	/* ACTION_PAGE: the page the row opens */
	int opens;
};

/* a row that opens a page of rows */
#define PAGE_ROW(label, help, page, opened) \
	{ label, NULL, 0, 0, { NULL }, { NULL }, help, 0, page, KIND_ACTION, ACTION_PAGE, 0, opened }

/* (the performance logging switch: the three timing variables at once) */
#define PERFORMANCE_LOG "HALO_PERF_LOG"

static struct setting settings[] = {
	/* (the Profile row is the first: settings[0]; Render resolution the
	second) */
	{ "Profile", "HALO_PROFILE", 0, 4, { "performance", "balanced", "quality", "custom" },
		{ "Performance", "Balanced", "Quality", "Custom" }, "Sets resolution, detail, update rates at once", 1,
		TAB_GRAPHICS },
	/* (Dynamic: the dynamic resolution, d3d8_gxm.c - the scale follows the
	GPU's load, from the Dynamic minimum up to 100%) */
	{ "Render resolution", "HALO_RENDER_SCALE", 0, 6, { "1", "0.875", "0.75", "0.625", "0.5", "dynamic" },
		{ "100%", "88%", "75%", "63%", "50%", "Dynamic" }, "Lower is faster, softer. Dynamic: by GPU load", 2,
		TAB_GRAPHICS },
	/* (shown while Render resolution is Dynamic: setting_shown) */
	{ "Dynamic minimum", "HALO_DYNAMIC_RES_MIN", 0, 3, { "0.5", "0.625", "0.75" }, { "50%", "63%", "75%" },
		"The lowest Dynamic goes in heavy scenes", 0, TAB_GRAPHICS },
	{ "Aspect ratio", "HALO_DISPLAY_WIDTH", 0, 2, { "848", "640" }, { "16:9", "4:3" },
		"4:3: the Xbox's framing, black bars", 0, TAB_GRAPHICS },
	{ "Upscale filter", "HALO_UPSCALE_FILTER", 0, 2, { "0", "1" }, { "Smooth", "Sharp" },
		"Scaling to the screen: Sharp = crisp pixels", 0, TAB_GRAPHICS },
	{ "Frame limit", "HALO_FRAME_CAP", 0, 3, { "30", "60", "0" }, { "30 FPS", "60 FPS", "Off" },
		"The most frames shown a second", 0, TAB_GRAPHICS },
	/* (port/linux/game/render_interpolation.c: frames drawn between the
	game's 30 ticks a second, blended; up to two a tick while both fit
	(main.c), else one; triple buffered (vita_gxm.c). On, it is the frame
	limit, and the weapon's blend is part of it. Live) */
	{ "Frame interpolation", "HALO_INTERPOLATION", 0, 2, { "false", "true" }, { "Off", "On" },
		"Up to 60 FPS: frames between the 30 Hz ticks", 0, TAB_GRAPHICS },
	{ "FPS counter", "HALO_FRAMERATE_COUNTER", 0, 2, { "0", "1" }, { "Off", "On" },
		"The game's frame counter, bottom right", 0, TAB_GRAPHICS },
	{ "Smooth weapon motion", "HALO_INTERPOLATE_FIRST_PERSON", 0, 2, { "1", "0" }, { "On", "Off" },
		"The weapon's animation blended between game ticks", 0, TAB_GRAPHICS },
	PAGE_ROW("Advanced", "Detail and update rates (the Profile sets them)", TAB_GRAPHICS, PAGE_GRAPHICS_ADVANCED),
	{ "Model detail", "HALO_MODEL_LOD_SCALE", 0, 4, { "1", "0.75", "0.5", "0.35" },
		{ "High", "Medium", "Low", "Lowest" }, "Level of detail of characters and vehicles", 2,
		PAGE_GRAPHICS_ADVANCED },
	{ "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", 0, 4, { "0", "4", "8", "12" },
		{ "Off", "Tiny", "Small", "Medium" }, "Skip objects this small on screen", 2, PAGE_GRAPHICS_ADVANCED },
	/* (Bruno Santana's settings, his variables and values, so his build's
	settings.txt carries over: render_objects.c's shadows - Near only for
	objects over 60 pixels across, the Xbox's 30 -, object_lights.c's
	dynamic lights cap, render.c's passes, render_particles.c's density) */
	{ "Object shadows", "HALO_VITA_SHADOWS", 0, 3, { "2", "1", "0" }, { "Full", "Near only", "Off" },
		"Shadows under characters and vehicles", 1, PAGE_GRAPHICS_ADVANCED },
	{ "Dynamic lights", "HALO_MAX_SCENE_LIGHTS", 0, 4, { "0", "8", "4", "2" }, { "All", "8", "4", "2" },
		"The most lights from shots and blasts a frame", 1, PAGE_GRAPHICS_ADVANCED },
	{ "Effects quality", "HALO_VITA_EFFECTS_QUALITY", 0, 3, { "2", "1", "0" },
		{ "Full", "Performance", "Minimal" }, "Performance: no reflections, specular, grass", 0,
		PAGE_GRAPHICS_ADVANCED },
	{ "Particle density", "HALO_PARTICLE_RENDER_DIVISOR", 0, 4, { "1", "2", "3", "4" },
		{ "Full", "Half", "Third", "Quarter" }, "Smoke, sparks, debris drawn (your gun's: all)", 0,
		PAGE_GRAPHICS_ADVANCED },
	/* (the sun's glow and rays, rasterizer_lights.c: seven small scenes a
	frame while the sun is in view) */
	{ "Sun rays", "HALO_SUN_RAYS", 0, 2, { "1", "0" }, { "On", "Off" },
		"The sun's glow and light shafts outdoors", 0, PAGE_GRAPHICS_ADVANCED },
	/* (decals.c: decals under 2 pixels across are not made. Off as on the
	Xbox, in no profile yet: to be tried on hardware) */
	{ "Tiny decals", "HALO_DECAL_MIN_PIXELS", 0, 2, { "0", "2" }, { "Shown", "Skipped" },
		"Bullet holes too small to see", 0, PAGE_GRAPHICS_ADVANCED },
	{ "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", 0, 3, { "1", "2", "4" },
		{ "Every tick", "Half", "Quarter" }, "How often static props are updated", 2, PAGE_GRAPHICS_ADVANCED },
	{ "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Full", "Half", "Third" }, "How often object lighting is recomputed", 2, PAGE_GRAPHICS_ADVANCED },
	/* (actors.c, Bruno Santana's variable and values: how often an actor
	thinks - its perception, situation, emotions and decision -, its
	movement and firing every tick; Adaptive by its distance to the nearest
	player. 1.1's Distant AI row, HALO_AI_PERCEPTION_LOD, is gone: a
	settings.txt naming it loads as the profile has this row) */
	{ "AI think rate", "HALO_AI_THINK_DIVISOR", 0, 4, { "1", "0", "2", "3" },
		{ "Every tick", "Adaptive", "15 Hz", "10 Hz" }, "Adaptive: far-off enemies think less often", 1,
		PAGE_GRAPHICS_ADVANCED },
	/* (vita_fourth_core.c, with Bruno Santana's levels and variable, so his
	build's settings.txt carries over: Audio puts the sound mixer on the
	fourth core, All async the mixer, GXM's display queue, the cache file
	thread, the map decompression, the checkpoint writer, the shader
	compiler and clean-up and the log's writer. The system gives a game
	that core only with a kernel plugin such as CapUnlocker; halo.log says
	where each went. Off until the hardware says what it is worth; in no
	profile) */
	{ "Fourth core helpers", "HALO_CPU3_AUX", 1, 3, { "0", "1", "2" }, { "Off", "Audio", "All async" },
		"Needs CapUnlocker: audio, or all background work", 2, PAGE_GRAPHICS_ADVANCED },

	{ "Sound voices", "HALO_SOUND_CHANNELS", 1, 4, { "16", "24", "32", "0" },
		{ "16", "24", "32", "Original" }, "Fewer is faster; the AI then differs (after a restart)", 3, TAB_AUDIO },
	{ "Sound occlusion", "HALO_SOUND_OBSTRUCTION_TICKS", 0, 3, { "1", "3", "6" },
		{ "Every tick", "Every 3rd", "Every 6th" }, "How often muffling behind walls is rechecked", 1, TAB_AUDIO },
	/* (sound_manager.c, Bruno Santana's variable: the sound manager's work
	- positions, volumes, which sounds get the voices - every nth frame;
	the mixer plays on) */
	{ "Sound updates", "HALO_SOUND_MANAGER_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Every frame", "Every 2nd", "Every 3rd" }, "Sounds placed and started; lower is faster", 1, TAB_AUDIO },

	/* (Controls: the rows players change, then the pages laid out as the
	Xbox controller) */
	{ "Look sensitivity", "XV_LOOK_SENS", 0, 6, { "50", "75", "100", "125", "150", "200" },
		{ "50%", "75%", "100%", "125%", "150%", "200%" }, "Right stick turning speed", 2, TAB_CONTROLS },
	{ "Invert look", "XV_INVERT_Y", 0, 2, { "0", "1" }, { "No", "Yes" }, "Reverse the right stick's up and down", 0,
		TAB_CONTROLS },
	{ "Crouch", "HALO_CROUCH_TOGGLE", 0, 2, { "1", "0" }, { "Toggle", "Hold" },
		"Left stick click: a press crouches, the next stands (Toggle)", 0, TAB_CONTROLS },
	/* (gyro aiming: vita_controls.h; its details on Gyro settings) */
	{ "Gyro aiming", "HALO_GYRO", 0, VITA_GYRO_MODES, { "off", "on", "zoomed", "hold" },
		{ "Off", "On", "While zoomed", "While holding" }, "Turn the Vita to aim, with the right stick", 0, TAB_CONTROLS },
	/* (the rear zones' guard against the hands holding the Vita:
	vita_controls.c - the pad's border never counts, and a hold time) */
	{ "Rear touch guard", "HALO_TOUCH_REAR_GUARD", 0, VITA_REAR_GUARD_COUNT, { VITA_REAR_GUARD_VALUES },
		{ VITA_REAR_GUARD_NAMES }, "Rear pad: its edges (the grip) ignored, a hold time",
		VITA_REAR_GUARD_DEFAULT, TAB_CONTROLS },
	PAGE_ROW("Button layout", "Which Vita button each Xbox button is on, in play", TAB_CONTROLS, PAGE_BUTTONS),
	PAGE_ROW("Touch zones", "Front and rear touch zones as Xbox buttons", TAB_CONTROLS, PAGE_TOUCH),
	PAGE_ROW("Gyro settings", "Gyro button, sensitivity, direction", TAB_CONTROLS, PAGE_GYRO),
	PAGE_ROW("Advanced", "Stick deadzone, Reset controls, Show dev settings", TAB_CONTROLS, PAGE_CONTROLS_ADVANCED),
	/* (the game's button icons: the Xbox's, or the Vita button each is on,
	in the menus and in play: vita_controls.h; not part of the layout,
	which Reset controls and the page's summary leave out) */
	{ "Button icons", "HALO_BUTTON_ICONS", 0, 2, { VITA_BUTTON_ICONS_VALUES }, { VITA_BUTTON_ICONS_NAMES },
		"Halo's prompts: Xbox icons, or the Vita's", 0, PAGE_BUTTONS },
	/* (the Vita button of each Xbox button, in play: vita_controls.c) */
	{ "A", "HALO_XBOX_A", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"A: jump (in the menus Cross stays A)", 0, PAGE_BUTTONS },
	{ "B", "HALO_XBOX_B", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"B: melee (in the menus Circle stays B)", 1, PAGE_BUTTONS },
	{ "X", "HALO_XBOX_X", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"X: reload, action (in play only)", 2, PAGE_BUTTONS },
	{ "Y", "HALO_XBOX_Y", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Y: switch weapon (in play only)", 3, PAGE_BUTTONS },
	{ "Black", "HALO_XBOX_BLACK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Black: switch grenades (in play only)", 8, PAGE_BUTTONS },
	{ "White", "HALO_XBOX_WHITE", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"White: flashlight (in play only)", 9, PAGE_BUTTONS },
	{ "Left trigger", "HALO_XBOX_LEFT_TRIGGER", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Left trigger: throw a grenade (in play only)", 4, PAGE_BUTTONS },
	{ "Right trigger", "HALO_XBOX_RIGHT_TRIGGER", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Right trigger: fire (in play only)", 5, PAGE_BUTTONS },
	{ "Left stick click", "HALO_XBOX_LEFT_STICK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Left stick click: crouch (in play only)", 7, PAGE_BUTTONS },
	{ "Right stick click", "HALO_XBOX_RIGHT_STICK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Right stick click: zoom (in play only)", 6, PAGE_BUTTONS },
	{ "Back", "HALO_XBOX_BACK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Back: scoreboard (in the menus Select stays Back)", 10, PAGE_BUTTONS },
	/* (the touch zones, in vita_controls.h's order: the Xbox button each
	presses) */
	{ "Touch top left", "HALO_TOUCH_TOP_LEFT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, top left corner: counts at once", 0, PAGE_TOUCH },
	{ "Touch top right", "HALO_TOUCH_TOP_RIGHT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, top right corner: counts at once", 0, PAGE_TOUCH },
	{ "Touch left edge", "HALO_TOUCH_LEFT_EDGE", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, left edge by the D-pad: counts at once", 0, PAGE_TOUCH },
	{ "Touch right edge", "HALO_TOUCH_RIGHT_EDGE", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, right edge by the buttons: counts at once", 0, PAGE_TOUCH },
	{ "Rear touch left", "HALO_TOUCH_REAR_LEFT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Rear pad, left half: held a moment (Rear touch guard)", 0, PAGE_TOUCH },
	{ "Rear touch right", "HALO_TOUCH_REAR_RIGHT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Rear pad, right half: held a moment (Rear touch guard)", 0, PAGE_TOUCH },
	/* (the button's choices are the Xbox buttons' rows') */
	{ "Gyro button", "HALO_GYRO_BUTTON", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"While holding: aims while held, nothing else in play", 4, PAGE_GYRO },
	{ "Gyro sensitivity", "HALO_GYRO_SENS", 0, 8, { "50", "75", "100", "125", "150", "200", "250", "300" },
		{ "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "2.5x", "3x" }, "1x: the view turns as far as the Vita does", 4,
		PAGE_GYRO },
	{ "Gyro vertical", "HALO_GYRO_INVERT_Y", 0, 2, { "0", "1" }, { "Normal", "Inverted" },
		"Normal: tilt the top edge towards you to look up", 0, PAGE_GYRO },
	{ "Gyro turning", "HALO_GYRO_TURN", 0, 2, { "yaw", "roll" }, { "Turn (yaw)", "Tilt (roll)" },
		"Turn the Vita left/right, or tilt it like a wheel", 0, PAGE_GYRO },
	{ "Stick deadzone", "XV_DEADZONE", 0, 4, { "0", "5", "10", "15" }, { "Off", "5%", "10%", "15%" },
		"Raise if the sticks drift", 0, PAGE_CONTROLS_ADVANCED },
	{ "Reset controls", NULL, 0, 0, { NULL }, { NULL }, "Look, crouch, buttons, touch as shipped (gyro kept)", 0,
		PAGE_CONTROLS_ADVANCED, KIND_ACTION, ACTION_RESET_CONTROLS },
	{ "Show dev settings", "HALO_DEV_SETTINGS", 0, 2, { "0", "1" }, { "Off", "On" },
		"The Dev tab: switches for testers, Save report", 0, PAGE_CONTROLS_ADVANCED },

	/* (Multiplayer: the connection, the ad hoc room (Ad hoc), a code typed
	in (Online, for a Vita without the in-game PC menus: setting_shown), the
	modded maps. Hosting, joining and co-op are the game's own Multiplayer
	menu's: OpenCE's screens, menu_functions.c) */
	/* (Online: internet play, experimental) */
	{ "Connection", "HALO_VITA_NETWORK", 1, 3, { "wifi", "adhoc", "online" }, { "Same Wi-Fi", "Ad hoc", "Online" },
		"Vitas on one Wi-Fi network play together", 0, TAB_MULTIPLAYER },
	{ "Ad hoc room", "HALO_ADHOC_ROOM", 0, 4, { "1", "2", "3", "4" }, { "1", "2", "3", "4" },
		"Vitas in the same room play together", 0, TAB_MULTIPLAYER },
	{ "Join the room", NULL, 0, 0, { NULL }, { NULL }, "Then Multiplayer, System Link (LAN) in the game", 0,
		TAB_MULTIPLAYER, KIND_ACTION, ACTION_ADHOC_JOIN },
	{ "Join with a code", NULL, 0, 0, { NULL }, { NULL }, "Type the code another player's game shows", 0,
		TAB_MULTIPLAYER, KIND_ACTION, ACTION_JOIN_CODE },
	PAGE_ROW("Modded maps", "Custom maps: on, off, delete; PC maps", TAB_MULTIPLAYER, PAGE_MAPS),
	/* (the hosted game's settings, which the game's Server Setup sets
	(vita_settings_set, menu_functions.c) and settings.txt keeps: on a page
	no row opens) */
	{ "  Lobby name", "HALO_NET_LOBBY_NAME", 0, 0, { NULL }, { NULL }, "The name others see for your game", 0,
		PAGE_PLAY, KIND_TEXT },
	/* (network_server_manager.c: the game's maximum_players; co-op takes
	two) */
	{ "  Max players", "HALO_NET_MAX_PLAYERS", 0, 15,
		{ "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" },
		{ "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" },
		"The most players your games take (co-op: 2)", 14, PAGE_PLAY },
	/* (network.host_public, OpenCE's: p2p.c) */
	{ "  Visibility", "HALO_NET_HOST_PUBLIC", 0, 2, { "false", "true" }, { "Private", "Public" },
		"Private: others join by code. Public: listed too", 1, PAGE_PLAY },
	{ "  Password", "HALO_NET_LOBBY_PASSWORD", 0, 0, { NULL }, { NULL }, "A public game asks joiners for it", 0,
		PAGE_PLAY, KIND_TEXT },
	/* (network.coop_public, OpenCE's: co-op hosted online is private
	unless chosen with X on its waiting screen, which sets this row; it
	takes effect at once: port/linux/game/coop_menu.c) */
	{ "  Co-op visibility", "HALO_NET_COOP_PUBLIC", 0, 2, { "false", "true" }, { "Private", "Public" },
		"Co-op online: Private, by code; Public, listed too", 0, PAGE_PLAY },

	/* (custom maps: the Custom Edition maps join the multiplayer level
	list; off by default while their colours are wrong on the Vita; the
	maps themselves follow on the page, page_lines) */
	{ "PC maps", "HALO_CUSTOM_EDITION", 0, 2, { "0", "1" }, { "Off", "On" },
		"Experimental: Custom Edition maps in the map list", 0, PAGE_MAPS },
	/* (shown while a Custom Edition installer is in ux0:data/haloce-vita
	and the resource maps are missing: vita_ce_installer.c) */
	{ "Extract PC files", NULL, 0, 0, { NULL }, { NULL }, "The three files out of the Halo CE installer",
		0, PAGE_MAPS, KIND_ACTION, ACTION_CE_EXTRACT },
	/* (a host's offer of its map, in a game this Vita joined:
	port/linux/game/map_share.c; the help is each choice's own,
	map_downloads_help) */
	{ "Map downloads", "HALO_MAP_SHARE_FROM", 0, 3, { "ask", "private", "never" }, { "Ask", "Not public games", "Never" },
		"A host's map, when you join without it", 0, PAGE_MAPS },

	{ "Performance logging", PERFORMANCE_LOG, 1, 2, { "", "1" }, { "Off", "On" },
		"Frame, render and tick timing in halo.log (at start-up)", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Crash dump on hang", "HALO_HANG_CRASH", 0, 2, { "", "1" }, { "Off", "On" },
		"A hang of 8 s crashes for a dump (psp2core) to send", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "FPS overlay", "XV_FPS", 0, 3, { "0", "2", "1" }, { "Off", "FPS only", "Full" },
		"Top right. Full: tick and render times, the cores' load", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Debug camera", "HALO_DEBUG_CAMERA", 0, 2, { "0", "1" }, { "Off", "On" },
		"Hold Black 1 s: follow, orbit, then a flying camera", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	/* (the system's ad hoc dialog's way of joining, for testers) */
	{ "Ad hoc dialog", "HALO_ADHOC_DIALOG_MODE", 0, 3, { "0", "1", "2" }, { "Connect", "Create", "Join" },
		"How the system dialog joins: try Connect first", 0, TAB_DEV },
	{ "Save report", NULL, 0, 0, { NULL }, { NULL }, "Logs, settings and the newest crash dump in one folder", 0,
		TAB_DEV, KIND_ACTION, ACTION_SAVE_REPORT },
	PAGE_ROW("A/B switches", "Renderer switches a bug report may ask for", TAB_DEV, PAGE_DEV_AB),
	{ "GPU W clamp", "HALO_GXM_WCLAMP", 1, 2, { "", "0" }, { "Default", "Off" },
		"A/B: models close to the camera dropping out (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Target mip minimum", "HALO_TARGET_CHAIN_MIN_SIZE", 1, 3, { "", "16", "8" }, { "32 px", "16 px", "8 px" },
		"A/B: smallest mip level of render targets (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Frame phase lock", "HALO_FRAME_PHASE_LOCK", 1, 2, { "", "0" }, { "On", "Off" },
		"A/B: 30 FPS frames kept between two ticks (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Render target sync", "HALO_GXM_RTT_SYNC", 1, 2, { "", "0" }, { "On", "Off" },
		"A/B: a scene waits for a target drawn before (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
};

#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* each row's choice as shipped (Reset controls), kept before anything is
loaded */
static int shipped_choice[SETTING_COUNT];
static int shipped_kept;

/* the profiles (the Profile row's first three choices): the value of each
row a profile sets. Balanced is the release's defaults (the rows' own) */
#define PROFILE_CUSTOM 3
#define PROFILE_ROWS 13

static const char *const profile_variables[PROFILE_ROWS] = {
	"HALO_RENDER_SCALE", "HALO_MODEL_LOD_SCALE", "HALO_MIN_OBJECT_PIXELS", "HALO_SCENERY_UPDATE_DIVISOR",
	"HALO_LIGHTING_REFRESH_DIVISOR", "HALO_SOUND_OBSTRUCTION_TICKS", "HALO_SUN_RAYS",
	"HALO_VITA_SHADOWS", "HALO_MAX_SCENE_LIGHTS", "HALO_VITA_EFFECTS_QUALITY", "HALO_PARTICLE_RENDER_DIVISOR",
	"HALO_AI_THINK_DIVISOR", "HALO_SOUND_MANAGER_DIVISOR",
};

/* (a settings.txt saved with a profile before a row was added takes the
profile's value for it: vita_settings_load) */
static const char *const profile_values[PROFILE_CUSTOM][PROFILE_ROWS] = {
	/* Performance: 50%, Low, Small, Quarter, Third, Every 6th, no sun rays;
	no object shadows, 4 lights, Performance effects, half the particles,
	Adaptive AI, sound every 2nd frame */
	{ "0.5", "0.5", "8", "4", "3", "6", "0", "0", "4", "1", "2", "0", "2" },
	/* Balanced (the release's defaults): 75%, Low, Small, Quarter, Third,
	Every 3rd, sun rays; shadows near only, 8 lights, Full effects and
	particles, Adaptive AI, sound every 2nd frame */
	{ "0.75", "0.5", "8", "4", "3", "3", "1", "1", "8", "2", "1", "0", "2" },
	/* Quality, the Xbox's: 100%, High, Off, Every tick, Full, Every tick,
	sun rays; all shadows, lights, effects and particles, AI every tick,
	sound every frame */
	{ "1", "1", "0", "1", "1", "1", "1", "2", "0", "2", "1", "1", "1" },
};

/* the variables the performance logging switch sets, and their values */
static const char *const performance_log_variables[3][2] = {
	{ "HALO_FRAME_TIMING", "300" },
	{ "HALO_RENDER_PROFILE", "1" },
	{ "HALO_TICK_PROFILE", "1" },
};

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* (the netcode is the distributed one, the only one network version 9
	plays: a match's client once could not reach its own host because the
	Vita refused a sendto on a connected datagram socket, fixed in
	vita_net.c) */
};

static int panel_open;
static unsigned long long both_since, last_move, last_shown;
static unsigned long previous_buttons;
/* the buttons down when the panel closed: the game has them only once
they are let go (the cross that opened System Link is not the game's A) */
static unsigned long held_after_close;
static int restart_pending;
/* the code screen (or the public games) came from Join a game with the
connection Online: a code taken goes on to Join a game's steps */
static int code_for_join;

/* the tab shown, the page of it shown (the tab's own, or a page one of its
rows opened), the line chosen on each page, and the screens an action
opens */
static int tab, page;
static int page_selected[PAGE_COUNT];
enum
{
	SCREEN_LIST,
	SCREEN_CODE,
	SCREEN_BROWSE,
	SCREEN_DELETE,
	SCREEN_GUIDE,
};
static int screen;
/* the code being typed (eight characters of P2P_CODE_ALPHABET) and the
character the cursor is on */
static char code_typed[P2P_CODE_LENGTH_TYPED + 1] = "AAAAAAAA";
static int code_cursor;
/* the public games as last shown (OpenCE's server browser: p2p.h's
p2p_lobby_entry, every text printable ASCII), and the one chosen */
#define BROWSE_LINES 8
static struct p2p_lobby_entry browse_entries[BROWSE_LINES];
static int browse_count, browse_selected;
/* the chosen game asks for its password: the keyboard is up for it */
static int browse_join_pending;
/* a line about the last action (a code looked up ...), shown in the help
line for a few seconds */
static char notice[80];
static unsigned long long notice_until;
/* the network the game started with (HALO_VITA_NETWORK at load): what
internet or ad hoc play can do this session */
static char running_network[8] = "wifi";
/* the Vita's user name (the system's), which other players see */
static char vita_name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE + 1];
/* Host a game or Join a game: the guide's action (ACTION_HOST or
ACTION_JOIN); whether the game was asked for its System Link screen and
when; why it did not open ("" while nothing went wrong); and the request to
make once the ad hoc dialog has joined the room's group (0: none) */
static int guide_action, guide_waiting, adhoc_pending;
static unsigned long long guide_sent;
static char guide_problem[64];

/* ---------- the Play page's lines of text

The lobby name and the password (KIND_TEXT rows): typed on the system's
keyboard (vita_ime.c), kept as their printable ASCII characters, no longer
than PLAY_TEXT_LENGTH (the game lists' name field holds 15), in the
environment and in settings.txt. The lobby name is the Vita's user name
until one is typed (or "Halo" when the user name has no such character);
it is saved only once typed or loaded, so a new user name shows through.
An empty password is none. */

#define PLAY_TEXT_LENGTH 15

static struct play_text
{
	const char *variable;
	char value[PLAY_TEXT_LENGTH + 1];
	/* typed, or named by settings.txt: saved */
	int named;
} play_texts[] = {
	{ "HALO_NET_LOBBY_NAME", "", 0 },
	{ "HALO_NET_LOBBY_PASSWORD", "", 0 },
};

#define PLAY_TEXT_COUNT ((int)(sizeof(play_texts) / sizeof(play_texts[0])))

/* the keyboard's row while it is open (the setting), else NULL */
static const struct setting *ime_setting;

/* ---------- modded maps */

#define MAXIMUM_MAPS 64
#define MAP_NAME_SIZE 48
/* the map lines shown at once (the list scrolls) */
#define MAP_LINES 8

enum
{
	MAP_XBOX,
	MAP_CUSTOM_EDITION,
	MAP_OPENSAUCE,
	MAP_OTHER,
};

struct map_entry
{
	char name[MAP_NAME_SIZE];
	char extension[8];
	unsigned long long size;
	int format;
};

static struct map_entry maps[MAXIMUM_MAPS];
static int map_count, map_scroll;
/* the Custom Edition resource maps missing from the folder ("" when all
are there), and whether any map listed is a Custom Edition one */
static char maps_missing[64];
static int maps_have_custom_edition;
/* a Custom Edition installer to take them from: vita_ce_installer_state's
(0 none, 1 there, 2 being read) and its file name */
static int maps_installer_state;
static char maps_installer[64];
/* the maps turned off: their names, commas between (HALO_MAPS_DISABLED) */
static char maps_disabled[1024];

/* the Xbox's own maps, and the Custom Edition resource maps: never listed */
static const char *const stock_maps[] = {
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"ui", "bitmaps", "sounds", "loc",
};

/* ---------- the report */

enum
{
	REPORT_IDLE,
	REPORT_SAVING,
	REPORT_SAVED,
	REPORT_FAILED,
};

static volatile int report_state;
static char report_path[96];
static int report_files;
static unsigned char report_buffer[64 * 1024];

static unsigned long long now_us(void)
{
	return sceKernelGetProcessTimeWide();
}

/* a line on the last action, shown in the help line for a few seconds */
static void set_notice(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(notice, sizeof(notice), format, arguments);
	va_end(arguments);
	notice_until = now_us() + 4000000ULL;
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

			/* (Render resolution's Dynamic is no number's nearest) */
			if (!strcmp(setting->values[index], "dynamic"))
				continue;

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
		if (settings[index].variable && strcmp(settings[index].variable, variable) == 0)
			return &settings[index];
	return NULL;
}

static int choice_of(const char *variable)
{
	struct setting const *setting = setting_named(variable);

	return setting ? setting->choice : 0;
}

/* the network chosen in the panel (this session's, running_network, until
a restart) */
static const char *chosen_network(void)
{
	const struct setting *setting = setting_named("HALO_VITA_NETWORK");

	return setting->values[setting->choice];
}

/* ---------- the Play page's listing

The hosted game as others see it (its name, most players, public or not,
password) and the public games, over internet play's calls (p2p.h): OpenCE's
server browser, ported (signed listings, passwords, the map, rules and
players; entries by the host's key, id, joined by it with a password). */


static struct play_text *play_text_named(const char *variable)
{
	int index;

	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		if (!strcmp(play_texts[index].variable, variable))
			return &play_texts[index];
	return NULL;
}

/* text as the Play page keeps it: its printable ASCII characters, the
spaces at either end left out, PLAY_TEXT_LENGTH at most */
static void play_sanitize(char *out, int size, const char *text)
{
	int length = 0, limit = size - 1 < PLAY_TEXT_LENGTH ? size - 1 : PLAY_TEXT_LENGTH;

	for (; text && *text && length < limit; text++)
		if ((unsigned char)*text >= 0x20 && (unsigned char)*text <= 0x7E && !(*text == ' ' && length == 0))
			out[length++] = *text;
	while (length > 0 && out[length - 1] == ' ')
		length--;
	out[length] = 0;
}

static void play_listing_set_name(const char *name)
{
	p2p_lobby_set_name(name);
}

static void play_listing_set_public(int listed)
{
	p2p_lobby_set_public(listed);
}

/* (a co-op game's own: network.coop_public) */
static void play_listing_set_coop_public(int listed)
{
	p2p_lobby_set_coop_public(listed);
}

/* (internet play works the password's key out on its own thread, once it
runs) */
static void play_listing_set_password(const char *password)
{
	p2p_lobby_set_password(password);
}

/* joins a public game (with the password typed, for a locked one) */
static void play_join_entry(const struct p2p_lobby_entry *entry, const char *password)
{
	p2p_lobby_join(entry->id, password ? password : "");
}

/* how the last join of a public game went: -1 the password was wrong, -2
the game is gone, else 0 */
static int play_join_state(void)
{
	int state = p2p_lobby_join_state();

	return state == P2P_LOBBY_JOIN_WRONG_PASSWORD ? -1 : state == P2P_LOBBY_JOIN_GONE ? -2 : 0;
}

/* the Play page's rows that apply: internet play's while the connection
is Online (this session's or the one chosen), the password while the game
is public too */
static int network_is(const char *network)
{
	return !strcmp(running_network, network) || !strcmp(chosen_network(), network);
}

/* whether a row is shown: the ad hoc room while the connection is Ad hoc
(chosen or this session's), the Play page's internet play rows while it is
Online (network_is), its password while the game is public too; Dynamic
minimum while Render resolution is Dynamic (each keeps its value
meanwhile) */
static int setting_shown(const struct setting *setting)
{
	if ((setting->variable && !strcmp(setting->variable, "HALO_ADHOC_ROOM")) || setting->action == ACTION_ADHOC_JOIN)
		return network_is("adhoc");
	if (setting->action == ACTION_CE_EXTRACT)
		return maps_installer_state == 1;
	/* (a code typed in: online, on a Vita whose game has not got the PC
	menus' Direct Link, menu_tags.c) */
	if (setting->action == ACTION_JOIN_CODE)
		return network_is("online") && halo_pc_menus_state != 1;
	if (setting->action == ACTION_BROWSE ||
		(setting->variable && !strcmp(setting->variable, "HALO_NET_HOST_PUBLIC")))
		return network_is("online");
	if (setting->variable && !strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD"))
		return network_is("online") && choice_of("HALO_NET_HOST_PUBLIC");
	if (setting->variable && !strcmp(setting->variable, "HALO_DYNAMIC_RES_MIN"))
	{
		const struct setting *scale = setting_named("HALO_RENDER_SCALE");

		return !strcmp(scale->values[scale->choice], "dynamic");
	}
	return 1;
}

/* the last choice left and right reach: not the Profile row's Custom (read,
not chosen) */
static int choice_last(const struct setting *setting)
{
	if (setting == &settings[0])
		return PROFILE_CUSTOM - 1;
	return setting->count - 1;
}

/* a setting's value as the environment variable (or variables) it is */
static void apply_value(const struct setting *setting)
{
	const char *value = setting->values[setting->choice];

	if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
	{
		int index;

		/* (on: env.txt's own values stand, HALO_TICK_PROFILE=3 ...) */
		for (index = 0; index < 3; index++)
			if (value[0])
				setenv(performance_log_variables[index][0], performance_log_variables[index][1], 0);
			else
				unsetenv(performance_log_variables[index][0]);
	}
	if (setting->dev && !value[0])
		unsetenv(setting->variable);
	else
		setenv(setting->variable, value, 1);
}

/* a Play page text set (typed, loaded or a test command's): kept
sanitized, in the environment, and at once where it shows (the lobby name
in the hosted game's listing: the game itself reads the environment each
frame of its lobby, network_server_manager.c) */
static void play_text_set(struct play_text *text, const char *value, int named)
{
	play_sanitize(text->value, sizeof(text->value), value);
	text->named = named;
	if (!strcmp(text->variable, "HALO_NET_LOBBY_NAME") && !text->value[0])
	{
		/* (the default: the Vita's user name, else "Halo") */
		play_sanitize(text->value, sizeof(text->value), vita_name);
		if (!text->value[0])
			snprintf(text->value, sizeof(text->value), "Halo");
		text->named = 0;
	}
	setenv(text->variable, text->value, 1);
	if (!strcmp(text->variable, "HALO_NET_LOBBY_NAME"))
		play_listing_set_name(text->value);
	else
		play_listing_set_password(text->value);
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
		apply_value(setting);
	}
	return restart;
}

/* settings.txt written anew: into a file of its own, then put in place of
the old one (a file opened for writing is not cut short on Vita3K, which
left the end of a longer old file after a shorter new one) */
static void save(void)
{
	FILE *file;
	int index, ok;

	remove(SETTINGS_FILE ".new");
	file = fopen(SETTINGS_FILE ".new", "w");
	if (!file)
		return;
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE && !(settings[index].dev && settings[index].choice == 0))
			fprintf(file, "%s=%s\n", settings[index].variable, settings[index].values[settings[index].choice]);
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		if (play_texts[index].named && play_texts[index].value[0])
			fprintf(file, "%s=%s\n", play_texts[index].variable, play_texts[index].value);
	if (maps_disabled[0])
		fprintf(file, "HALO_MAPS_DISABLED=%s\n", maps_disabled);
	ok = fclose(file) == 0;
	if (ok)
	{
		remove(SETTINGS_FILE);
		rename(SETTINGS_FILE ".new", SETTINGS_FILE);
	}
}

/* the chosen network as the platform layer's settings (port_config.c):
internet play on, without UPnP, which the Vita does not have
(vita_stubs.c); or ad hoc play, which needs no internet */
static void apply_network(void)
{
	const char *network = getenv("HALO_VITA_NETWORK");

	snprintf(running_network, sizeof(running_network), "%s", network ? network : "wifi");
	if (!strcmp(running_network, "online"))
	{
		setenv("HALO_NET_ONLINE", "true", 1);
		setenv("HALO_NET_ALLOW_UPNP", "false", 0);
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		setenv("HALO_NET_ADHOC", "true", 1);
	}
	{
		/* a player with a default profile (no name) goes by the Vita's user
		name in network games (network_game_local_player_name), and it is the
		lobby name's default (play_text_set) */
		char name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE + 1];

		memset(name, 0, sizeof(name));
		if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)name, sizeof(name) - 1) >= 0 &&
			name[0])
		{
			setenv("HALO_NET_PLAYER_NAME", name, 0);
			snprintf(vita_name, sizeof(vita_name), "%s", name);
		}
	}
	{
		char line[96];

		snprintf(line, sizeof(line), "vita: network %s (settings panel, Multiplayer: Connection)", running_network);
		vita_host_log(line);
	}
}

/* (in halo.log near its top: the dev switches on, if any - a log from a
tester's run in test mode says so) */
static void log_dev_switches(void)
{
	char message[400];
	int length = 0, index;

	for (index = 0; index < SETTING_COUNT && length < (int)sizeof(message); index++)
	{
		const struct setting *setting = &settings[index];

		if (!setting->dev || setting->choice == 0)
			continue;
		if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
		{
			int variable;

			length += snprintf(message + length, sizeof(message) - length, " %s=1 (", PERFORMANCE_LOG);
			for (variable = 0; variable < 3 && length < (int)sizeof(message); variable++)
			{
				const char *value = getenv(performance_log_variables[variable][0]);

				length += snprintf(message + length, sizeof(message) - length, "%s%s=%s", variable ? " " : "",
					performance_log_variables[variable][0], value ? value : "");
			}
			if (length < (int)sizeof(message))
				length += snprintf(message + length, sizeof(message) - length, ")");
		}
		else
		{
			const char *value = getenv(setting->variable);

			length += snprintf(message + length, sizeof(message) - length, " %s=%s", setting->variable,
				value ? value : setting->values[setting->choice]);
		}
	}
	if (length)
	{
		char line[460];

		snprintf(line, sizeof(line), "settings: TEST MODE, dev switches on:%s", message);
		vita_host_log(line);
	}
}

int vita_settings_set(const char *variable, const char *value);

void vita_settings_load(void)
{
	extern int (*halo_test_setting_hook)(const char *variable, const char *value);

	FILE *file = fopen(SETTINGS_FILE, "r");
	/* (a long line: the maps turned off) */
	char line[1200];
	int index;
	/* the dev switches settings.txt names (the others keep env.txt's) */
	char dev_saved[SETTING_COUNT];
	/* the rows settings.txt names, and the profile it was saved with */
	char named[SETTING_COUNT];
	int saved_profile = -1;
	/* the Play page's texts as env.txt and settings.txt give them, and
	whether settings.txt named them */
	char texts[PLAY_TEXT_COUNT][PLAY_TEXT_LENGTH + 1];
	char texts_named[PLAY_TEXT_COUNT];
	/* (1.0.3's Online games row, saved as HALO_NET_LOBBY_PUBLIC: the
	Visibility row's choice when settings.txt has none of its own) */
	const char *old_public = NULL;
	char old_public_value[8];

	memset(dev_saved, 0, sizeof(dev_saved));
	memset(named, 0, sizeof(named));
	if (!shipped_kept)
	{
		for (index = 0; index < SETTING_COUNT; index++)
			shipped_choice[index] = settings[index].choice;
		shipped_kept = 1;
	}
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
		struct setting *setting = &settings[index];
		const char *value = setting->variable ? getenv(setting->variable) : NULL;

		if (setting->kind != KIND_CHOICE)
			continue;
		if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
		{
			int variable;

			/* (on when env.txt turns on any of its three) */
			for (variable = 0; variable < 3; variable++)
			{
				const char *timing = getenv(performance_log_variables[variable][0]);

				if (timing && atoi(timing))
					setting->choice = 1;
			}
		}
		else if (value && strcmp(setting->variable, "HALO_PROFILE") != 0)
			setting->choice = find_choice(setting, value);
	}
	{
		const char *disabled = getenv("HALO_MAPS_DISABLED");

		snprintf(maps_disabled, sizeof(maps_disabled), "%s", disabled ? disabled : "");
	}
	memset(texts_named, 0, sizeof(texts_named));
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
	{
		const char *value = getenv(play_texts[index].variable);

		/* (env.txt's: a starting value, not saved) */
		play_sanitize(texts[index], sizeof(texts[index]), value ? value : "");
	}
	/* (env.txt's 1.0.3 variable, without the new one) */
	if (getenv("HALO_NET_LOBBY_PUBLIC") && !getenv("HALO_NET_HOST_PUBLIC"))
	{
		struct setting *visibility = setting_named("HALO_NET_HOST_PUBLIC");

		visibility->choice = find_choice(visibility, getenv("HALO_NET_LOBBY_PUBLIC"));
	}
	if (file)
	{
		while (fgets(line, sizeof(line), file))
		{
			char *equals = strchr(line, '=');
			char *end = line + strlen(line);
			struct setting *setting;

			while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
				*--end = 0;
			if (!equals)
				continue;
			*equals = 0;
			if (strcmp(line, "HALO_MAPS_DISABLED") == 0)
			{
				snprintf(maps_disabled, sizeof(maps_disabled), "%s", equals + 1);
				continue;
			}
			if (play_text_named(line))
			{
				int text = (int)(play_text_named(line) - play_texts);

				play_sanitize(texts[text], sizeof(texts[text]), equals + 1);
				texts_named[text] = 1;
				continue;
			}
			if (strcmp(line, "HALO_NET_LOBBY_PUBLIC") == 0)
			{
				snprintf(old_public_value, sizeof(old_public_value), "%s", equals + 1);
				old_public = old_public_value;
				continue;
			}
			/* (1.0.3's co-op rows: every game this Vita hosted became that
			campaign level; co-op is now hosted from the Campaign screen's
			difficulty, Y. An old choice loads as Off: not set, and gone
			from settings.txt at the next save) */
			if (strcmp(line, "HALO_NET_COOP_LEVEL") == 0 || strcmp(line, "HALO_NET_COOP_DIFFICULTY") == 0)
			{
				if (strcmp(line, "HALO_NET_COOP_LEVEL") == 0 && equals[1])
				{
					char message[160];

					snprintf(message, sizeof(message), "settings: the old co-op setting (%.16s) is off: co-op is hosted "
						"from Campaign (Y on the difficulty)", equals + 1);
					vita_host_log(message);
				}
				continue;
			}
			/* (the profile line is what the rows were: worked out again) */
			setting = setting_named(line);
			if (setting && setting->kind == KIND_CHOICE && strcmp(line, "HALO_PROFILE") == 0)
			{
				for (index = 0; index < PROFILE_CUSTOM; index++)
					if (strcasecmp(equals + 1, setting->values[index]) == 0)
						saved_profile = index;
			}
			else if (setting && setting->kind == KIND_CHOICE)
			{
				setting->choice = find_choice(setting, equals + 1);
				named[setting - settings] = 1;
				if (setting->dev)
					dev_saved[setting - settings] = 1;
			}
		}
		fclose(file);
	}
	if (old_public && !named[setting_named("HALO_NET_HOST_PUBLIC") - settings])
	{
		struct setting *visibility = setting_named("HALO_NET_HOST_PUBLIC");

		visibility->choice = find_choice(visibility, old_public);
	}
	if (saved_profile >= 0)
	{
		int row;

		/* a row the saved profile sets that settings.txt does not name
		(one added since: Sun rays, in 1.1) takes the profile's value,
		unless env.txt names it */
		for (row = 0; row < PROFILE_ROWS; row++)
		{
			struct setting *setting = setting_named(profile_variables[row]);

			if (setting && !named[setting - settings] && !getenv(setting->variable))
				setting->choice = find_choice(setting, profile_values[saved_profile][row]);
		}
	}
	/* (the profile is what the rows are: Custom when env.txt or the panel
	set them apart from every profile) */
	setting_named("HALO_PROFILE")->choice = matching_profile();
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE && (!settings[index].dev || dev_saved[index]))
			apply_value(&settings[index]);
	if (maps_disabled[0])
		setenv("HALO_MAPS_DISABLED", maps_disabled, 1);
	else
		unsetenv("HALO_MAPS_DISABLED");
	for (index = 0; index < (int)(sizeof(fixed_defaults) / sizeof(fixed_defaults[0])); index++)
		setenv(fixed_defaults[index][0], fixed_defaults[index][1], 0);
	halo_test_setting_hook = vita_settings_set;
	{
		/* (in halo.log: the profile and the rows it sets, the picture's) */
		char message[600];
		int length = snprintf(message, sizeof(message), "settings: profile %s:", settings[0].names[settings[0].choice]);

		for (index = 0; index < PROFILE_ROWS && length < (int)sizeof(message); index++)
			length += snprintf(message + length, sizeof(message) - length, " %s=%s", profile_variables[index],
				getenv(profile_variables[index]));
		if (length < (int)sizeof(message))
			snprintf(message + length, sizeof(message) - length, " HALO_DISPLAY_WIDTH=%s HALO_UPSCALE_FILTER=%s",
				getenv("HALO_DISPLAY_WIDTH"), getenv("HALO_UPSCALE_FILTER"));
		vita_host_log(message);
	}
	log_dev_switches();
	if (maps_disabled[0])
	{
		char message[1100];

		snprintf(message, sizeof(message), "settings: maps turned off: %s", maps_disabled);
		vita_host_log(message);
	}
	apply_network();
	/* (after apply_network: the lobby name's default is the user name) */
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		play_text_set(&play_texts[index], texts[index], texts_named[index]);
}

/* ---------- modded maps */

static const char *maps_directory(void)
{
	const char *directory = getenv("HALO_MAPS_ROOT");

	return directory && directory[0] ? directory : DATA_DIRECTORY "/maps";
}

static int map_disabled(const char *name)
{
	const char *list = maps_disabled;
	size_t length = strlen(name);

	while (*list)
	{
		size_t entry = strcspn(list, ",");

		if (entry == length && strncasecmp(list, name, length) == 0)
			return 1;
		list += entry;
		if (*list == ',')
			list++;
	}
	return 0;
}

/* turns the map `name` off or on: in the list, the environment and
settings.txt */
static void map_set_disabled(const char *name, int disabled)
{
	char list[sizeof(maps_disabled)];
	const char *entry = maps_disabled;
	int length = 0;

	if (map_disabled(name) == disabled)
		return;
	/* (the list again without the name, then with it at the end) */
	list[0] = 0;
	while (*entry)
	{
		size_t size = strcspn(entry, ",");

		if (size && !(size == strlen(name) && strncasecmp(entry, name, size) == 0) &&
			length + (int)size + 2 < (int)sizeof(list))
			length += snprintf(list + length, sizeof(list) - length, "%s%.*s", length ? "," : "", (int)size, entry);
		entry += size;
		if (*entry == ',')
			entry++;
	}
	if (disabled && length + (int)strlen(name) + 2 < (int)sizeof(list))
		snprintf(list + length, sizeof(list) - length, "%s%s", length ? "," : "", name);
	memcpy(maps_disabled, list, sizeof(maps_disabled));
	if (maps_disabled[0])
		setenv("HALO_MAPS_DISABLED", maps_disabled, 1);
	else
		unsetenv("HALO_MAPS_DISABLED");
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: map %s turned %s", name, disabled ? "off" : "on");
		vita_host_log(line);
	}
}

static int map_compare(const void *first, const void *second)
{
	const struct map_entry *a = first, *b = second;
	int order = strcasecmp(a->name, b->name);

	return order ? order : strcasecmp(a->extension, b->extension);
}

static int file_exists(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0;
}

/* the format of a map file by its header: the cache signature 'head' and
its version (5 the Xbox's, 609 Custom Edition's) */
static int map_format(const char *path, const char *extension)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	size_t got = 0;
	unsigned long version;

	if (file)
	{
		got = fread(header, 1, sizeof(header), file);
		fclose(file);
	}
	if (got != sizeof(header) || memcmp(header, "daeh", 4) != 0)
		return MAP_OTHER;
	version = header[4] | (header[5] << 8) | ((unsigned long)header[6] << 16) | ((unsigned long)header[7] << 24);
	if (version == 5)
		return MAP_XBOX;
	if (version == 609)
		return strcasecmp(extension, "yelo") == 0 ? MAP_OPENSAUCE : MAP_CUSTOM_EDITION;
	return MAP_OTHER;
}

/* the maps folder looked through again: the maps that are not the Xbox's */
static void maps_scan(void)
{
	const char *directory = maps_directory();
	DIR *folder = opendir(directory);
	struct dirent *entry;
	static const char *const resource_maps[] = { "bitmaps", "sounds", "loc" };
	int index;

	map_count = 0;
	maps_have_custom_edition = 0;
	while (folder && (entry = readdir(folder)) != NULL && map_count < MAXIMUM_MAPS)
	{
		const char *dot = strrchr(entry->d_name, '.');
		struct map_entry *map = &maps[map_count];
		char path[512];
		struct stat status;
		int stock = 0;

		if (!dot || dot == entry->d_name || (strcasecmp(dot, ".map") != 0 && strcasecmp(dot, ".yelo") != 0) ||
			(size_t)(dot - entry->d_name) >= sizeof(map->name))
			continue;
		snprintf(map->name, sizeof(map->name), "%.*s", (int)(dot - entry->d_name), entry->d_name);
		snprintf(map->extension, sizeof(map->extension), "%s", dot + 1);
		for (index = 0; index < (int)(sizeof(stock_maps) / sizeof(stock_maps[0])); index++)
			if (strcasecmp(map->name, stock_maps[index]) == 0)
				stock = 1;
		if (stock)
			continue;
		snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
		map->size = stat(path, &status) == 0 ? (unsigned long long)status.st_size : 0;
		map->format = map_format(path, map->extension);
		if (map->format == MAP_CUSTOM_EDITION || map->format == MAP_OPENSAUCE)
			maps_have_custom_edition = 1;
		map_count++;
	}
	if (folder)
		closedir(folder);
	qsort(maps, (size_t)map_count, sizeof(maps[0]), map_compare);
	maps_missing[0] = 0;
	for (index = 0; index < 3; index++)
	{
		char path[256];
		size_t used = strlen(maps_missing);

		snprintf(path, sizeof(path), "%s/%s.map", directory, resource_maps[index]);
		if (!file_exists(path))
			snprintf(maps_missing + used, sizeof(maps_missing) - used, "%s%s.map", used ? " " : "",
				resource_maps[index]);
	}
	if (map_scroll >= map_count)
		map_scroll = 0;
	maps_installer_state = maps_missing[0] ? vita_ce_installer_state(maps_installer, sizeof(maps_installer)) : 0;
}

static void size_text(char *text, int size, unsigned long long bytes)
{
	if (bytes >= 1024ULL * 1024ULL)
		snprintf(text, (size_t)size, "%llu.%llu MB", bytes >> 20, ((bytes & 0xFFFFFULL) * 10) >> 20);
	else
		snprintf(text, (size_t)size, "%llu KB", (bytes + 1023) >> 10);
}

static const char *map_format_name(int format)
{
	return format == MAP_XBOX ? "Xbox" : format == MAP_CUSTOM_EDITION ? "CE" : format == MAP_OPENSAUCE ? "CE+OS" : "?";
}

/* deletes a map file, with its picture and description beside it */
static void map_delete(const struct map_entry *map)
{
	const char *directory = maps_directory();
	char path[256];
	int other = 0, index;

	snprintf(path, sizeof(path), "%s/%s.%s", directory, map->name, map->extension);
	if (remove(path) != 0)
	{
		set_notice("Could not delete %.40s.%s", map->name, map->extension);
		return;
	}
	/* (and a download of it that was cut off, kept to go on: map_share.c) */
	snprintf(path, sizeof(path), "%s/%s.download", directory, map->name);
	remove(path);
	snprintf(path, sizeof(path), "%s/%s.resume", directory, map->name);
	remove(path);
	/* (the picture and description stay while a .map or .yelo of the name
	is left) */
	for (index = 0; index < map_count; index++)
		if (&maps[index] != map && strcasecmp(maps[index].name, map->name) == 0)
			other = 1;
	if (!other)
	{
		snprintf(path, sizeof(path), "%s/%s.bmp", directory, map->name);
		remove(path);
		snprintf(path, sizeof(path), "%s/%s.txt", directory, map->name);
		remove(path);
		map_set_disabled(map->name, 0);
	}
	set_notice("Deleted %.40s.%s", map->name, map->extension);
	{
		char line[160];

		snprintf(line, sizeof(line), "settings: map %s.%s deleted", map->name, map->extension);
		vita_host_log(line);
	}
}

/* ---------- the report */

/* copies a file into the report's folder; nonzero if it was there */
static int report_copy(const char *from, const char *folder, const char *name)
{
	char path[256];
	FILE *input = fopen(from, "rb"), *output;
	size_t got;
	int ok = 1;

	if (!input)
		return 0;
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	output = fopen(path, "wb");
	if (!output)
	{
		fclose(input);
		return 0;
	}
	while ((got = fread(report_buffer, 1, sizeof(report_buffer), input)) > 0)
		if (fwrite(report_buffer, 1, got, output) != got)
		{
			ok = 0;
			break;
		}
	fclose(input);
	if (fclose(output) != 0)
		ok = 0;
	return ok;
}

/* the report, on a thread of its own (a crash dump is tens of megabytes) */
static void report_thread(void *argument)
{
	static const char *const files[] = { "halo.log", "halo-prev.log", "settings.txt", "env.txt" };
	SceDateTime time;
	char path[512], newest[512], newest_name[256];
	int index, count = 0;
	DIR *folder;
	struct dirent *entry;
	long long newest_time = -1;

	(void)argument;
	memset(&time, 0, sizeof(time));
	sceRtcGetCurrentClockLocalTime(&time);
	snprintf(report_path, sizeof(report_path), DATA_DIRECTORY "/report-%04u%02u%02u-%02u%02u%02u",
		(unsigned)time.year, (unsigned)time.month, (unsigned)time.day, (unsigned)time.hour, (unsigned)time.minute,
		(unsigned)time.second);
	mkdir(report_path, 0777);
	for (index = 0; index < (int)(sizeof(files) / sizeof(files[0])); index++)
	{
		snprintf(path, sizeof(path), DATA_DIRECTORY "/%s", files[index]);
		count += report_copy(path, report_path, files[index]);
	}
	/* the newest crash dump */
	newest[0] = newest_name[0] = 0;
	folder = opendir(DUMP_DIRECTORY);
	while (folder && (entry = readdir(folder)) != NULL)
	{
		struct stat status;
		size_t length = strlen(entry->d_name);

		if (length < 16 || strncmp(entry->d_name, "psp2core", 8) != 0 ||
			strcasecmp(entry->d_name + length - 8, ".psp2dmp") != 0)
			continue;
		snprintf(path, sizeof(path), DUMP_DIRECTORY "/%s", entry->d_name);
		if (stat(path, &status) == 0 && ((long long)status.st_mtime > newest_time ||
			((long long)status.st_mtime == newest_time && strcmp(entry->d_name, newest_name) > 0)))
		{
			newest_time = (long long)status.st_mtime;
			snprintf(newest, sizeof(newest), "%s", path);
			snprintf(newest_name, sizeof(newest_name), "%s", entry->d_name);
		}
	}
	if (folder)
		closedir(folder);
	if (newest[0])
		count += report_copy(newest, report_path, newest_name);
	report_files = count;
	{
		char line[200];

		snprintf(line, sizeof(line), "settings: report saved to %s (%d files%s)", report_path, count,
			newest[0] ? ", with the newest crash dump" : ", no crash dump");
		vita_host_log(line);
	}
	__atomic_store_n(&report_state, count ? REPORT_SAVED : REPORT_FAILED, __ATOMIC_RELEASE);
}

static void report_start(void)
{
	if (__atomic_load_n(&report_state, __ATOMIC_ACQUIRE) == REPORT_SAVING)
		return;
	__atomic_store_n(&report_state, REPORT_SAVING, __ATOMIC_RELEASE);
	if (vita_host_thread_start("halo report", report_thread, NULL, -1) < 0)
	{
		__atomic_store_n(&report_state, REPORT_FAILED, __ATOMIC_RELEASE);
		set_notice("Could not start the report");
	}
}

/* ---------- the lines of a tab */

enum
{
	LINE_SETTING,
	LINE_MAP,
	/* (a line that is not chosen: a warning, a note) */
	LINE_INFO,
};

struct line
{
	int type;
	int index;
	char text[80];
};

#define MAXIMUM_LINES (SETTING_COUNT + MAXIMUM_MAPS + 4)

/* the tabs shown: Dev once its switch is on */
static int tab_shown(int index)
{
	return index != TAB_DEV || choice_of("HALO_DEV_SETTINGS");
}

/* the lines of the page shown: its rows, then the gyroscope's line (Gyro
settings) or the maps (Modded maps) */
static int page_lines(struct line *lines)
{
	int count = 0, index;

	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].page == page && setting_shown(&settings[index]))
		{
			lines[count].type = LINE_SETTING;
			lines[count++].index = index;
		}
	if (page == PAGE_GYRO)
	{
		/* (the gyroscope's rates now: redrawn twice a second) */
		lines[count].type = LINE_INFO;
		vita_gyro_status(lines[count++].text, sizeof(lines[0].text));
	}
	if (page == PAGE_MAPS)
	{
		if (maps_missing[0] && (choice_of("HALO_CUSTOM_EDITION") || maps_have_custom_edition || maps_installer_state))
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "!Missing: %s", maps_missing);
			/* (where they come from: the Halo CE installer copied in does) */
			lines[count].type = LINE_INFO;
			if (maps_installer_state == 2)
				snprintf(lines[count++].text, sizeof(lines[0].text), "!Taking them from %.28s", maps_installer);
			else if (maps_installer_state == 1)
				snprintf(lines[count++].text, sizeof(lines[0].text), "!Extract PC files takes them from the installer");
			else
			{
				snprintf(lines[count++].text, sizeof(lines[0].text), "!Copy them in, or the Halo CE installer");
				lines[count].type = LINE_INFO;
				snprintf(lines[count++].text, sizeof(lines[0].text), "!(halocesetup*.exe) to ux0:data/haloce-vita");
			}
		}
		if (!map_count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "No custom maps in the maps folder");
		}
		for (index = 0; index < map_count; index++)
		{
			lines[count].type = LINE_MAP;
			lines[count++].index = index;
		}
	}
	return count;
}

/* what the game is doing with other machines (system_link_shortcut.c) */
static int multiplayer_state(void)
{
	return __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
}

/* this Vita's name, and its address as the other Vitas reach it */
static void vita_line(char *text, int size)
{
	unsigned int address = (unsigned int)halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS];
	const unsigned char *bytes = (const unsigned char *)&address;
	char name[20];

	snprintf(name, sizeof(name), "%.16s", vita_name[0] ? vita_name : "(no name)");
	if (!strcmp(running_network, "adhoc"))
		snprintf(text, (size_t)size, "This Vita: %s, ad hoc room %d", name, choice_of("HALO_ADHOC_ROOM") + 1);
	else if (address)
		snprintf(text, (size_t)size, "This Vita: %s  %u.%u.%u.%u", name, bytes[0], bytes[1], bytes[2], bytes[3]);
	else if (multiplayer_state() == SYSTEM_LINK_STATE_STARTING)
		snprintf(text, (size_t)size, "This Vita: %s", name);
	else
		snprintf(text, (size_t)size, "This Vita: %s, no Wi-Fi network", name);
}

/* one line on the network game: none, looking for games, hosting ... */
static void game_line(char *text, int size)
{
	int machines = halo_multiplayer_status[SYSTEM_LINK_STATUS_MACHINES];
	int games = halo_multiplayer_status[SYSTEM_LINK_STATUS_GAMES];
	int maximum = halo_multiplayer_status[SYSTEM_LINK_STATUS_MAXIMUM];

	switch (multiplayer_state())
	{
	case SYSTEM_LINK_STATE_SEARCHING:
		if (games)
			snprintf(text, (size_t)size, "Looking for games: %d found", games);
		else
			snprintf(text, (size_t)size, "Looking for games: none yet");
		break;
	case SYSTEM_LINK_STATE_JOINING:
		snprintf(text, (size_t)size, "Joining a game...");
		break;
	case SYSTEM_LINK_STATE_HOSTING:
		/* (and the most players the game takes, once the game says) */
		if (machines > 1 && maximum > 0)
			snprintf(text, (size_t)size, "Hosting: %d of %d players in the lobby",
				halo_multiplayer_status[SYSTEM_LINK_STATUS_PLAYERS], maximum);
		else if (machines > 1)
			snprintf(text, (size_t)size, "Hosting: %d Vitas in the lobby", machines);
		else if (maximum > 0)
			snprintf(text, (size_t)size, "Hosting: waiting for players (max %d)", maximum);
		else
			snprintf(text, (size_t)size, "Hosting: waiting for players");
		break;
	case SYSTEM_LINK_STATE_LOBBY:
		snprintf(text, (size_t)size, "In a lobby: %d Vitas, the host starts", machines);
		break;
	case SYSTEM_LINK_STATE_IN_GAME:
		snprintf(text, (size_t)size, "In a game: %d Vitas%s", machines,
			halo_multiplayer_status[SYSTEM_LINK_STATUS_HOST] ? " (you host)" : "");
		break;
	case SYSTEM_LINK_STATE_PLAYING:
		snprintf(text, (size_t)size, "Playing: quit the level to host or join");
		break;
	default:
		snprintf(text, (size_t)size, "No game yet: host one or join one");
		break;
	}
}

/* one short line on internet or ad hoc play this session ("" on Wi-Fi) */
static void status_line(char *text, int size)
{
	char code[P2P_CODE_SIZE];
	char detail[96];

	text[0] = 0;
	if (!strcmp(running_network, "online"))
	{
		if (p2p_hosting_code(code, sizeof(code)))
			snprintf(text, (size_t)size, "Your code: %s%s", code, choice_of("HALO_NET_HOST_PUBLIC") ? " (public)" : "");
		else
		{
			p2p_status(detail, sizeof(detail));
			snprintf(text, (size_t)size, "Online: %.26s", detail);
		}
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		int state = vita_adhoc_state(detail, sizeof(detail));

		if (state == 2)
		{
			char group[64];

			p2p_adhoc_status(group, sizeof(group));
			snprintf(text, (size_t)size, "Ad hoc: %.27s", group);
		}
		else
		{
			snprintf(text, (size_t)size, "%s", state == 1 ? "Ad hoc: joining..." : state == -1 ? "Ad hoc: did not join" :
				"Ad hoc: not in a group");
		}
	}
}

/* the Connection row's help, by its choice */
static const char *connection_help(const struct setting *setting)
{
	const char *value = setting->values[setting->choice];

	return !strcmp(value, "adhoc") ? "No router: Vitas side by side (experimental)" : !strcmp(value, "online") ?
		"Internet play by code (experimental)" : "Vitas on one Wi-Fi network play together";
}

/* ---------- PlayStation's terms

With the Button icons PlayStation the panel says what each Xbox button does
(in Halo's default controller layout, as the Button layout's help always
did) where it named the Xbox button: the Button layout's rows, the touch
zones' choices and diagram, a few help lines; and the Vita button where it
named the one the menus' fixed layout puts an Xbox button on (the steps of
Host a game and Join a game: Cross for A) */

static int playstation_terms(void)
{
	/* (the owner's choice: the Button icons change the game's icons only;
	the panel keeps the Xbox controller's names, A B X Y, Black, White ...,
	with either) */
	return 0;
}

/* the Xbox buttons by what they do (VITA_XBOX_*'s order) */
static const char *const xbox_actions[VITA_XBOX_COUNT] = { VITA_XBOX_ACTION_NAMES };

/* a row's label and help in PlayStation's terms: by its variable, or its
label for a row that opens a page */
static const struct
{
	const char *key;
	const char *label;
	const char *help;
} playstation_rows[] = {
	{ "HALO_XBOX_A", "Jump", "Jump (in the menus Cross stays Accept)" },
	{ "HALO_XBOX_B", "Melee", "Melee (in the menus Circle stays Back)" },
	{ "HALO_XBOX_X", "Action, reload", "Action, reload (in play only)" },
	{ "HALO_XBOX_Y", "Switch weapon", "Switch weapon (in play only)" },
	{ "HALO_XBOX_BLACK", "Switch grenades", "Switch grenades (in play only)" },
	{ "HALO_XBOX_WHITE", "Flashlight", "Flashlight (in play only)" },
	{ "HALO_XBOX_LEFT_TRIGGER", "Throw grenade", "Throw a grenade (in play only)" },
	{ "HALO_XBOX_RIGHT_TRIGGER", "Fire", "Fire (in play only)" },
	{ "HALO_XBOX_LEFT_STICK", "Crouch", "Crouch (in play only)" },
	{ "HALO_XBOX_RIGHT_STICK", "Zoom", "Zoom (in play only)" },
	{ "HALO_XBOX_BACK", "Scoreboard", "Scoreboard (in the menus Select stays Back)" },
	{ "HALO_CROUCH_TOGGLE", NULL, "A press crouches, the next stands (Toggle)" },
	{ "HALO_DEBUG_CAMERA", NULL, "Hold Black 1 s: follow, orbit, then a flying camera" },
	{ "Button layout", NULL, "Which Vita button each action is on, in play" },
	{ "Touch zones", NULL, "Front and rear touch zones as actions" },
};

static int playstation_row(const struct setting *setting)
{
	const char *key = setting->variable ? setting->variable : setting->label;
	int index;

	if (!playstation_terms())
		return -1;
	for (index = 0; index < (int)(sizeof(playstation_rows) / sizeof(playstation_rows[0])); index++)
		if (!strcmp(playstation_rows[index].key, key))
			return index;
	return -1;
}

static const char *setting_label(const struct setting *setting)
{
	int row = playstation_row(setting);

	return row >= 0 && playstation_rows[row].label ? playstation_rows[row].label : setting->label;
}

/* Map downloads' help, by choice */
static const char *const map_downloads_help[3] = {
	"Asks before a host's map comes; public games warn",
	"Asks, but not in games from the public lobby",
	"No downloads: copy the host's map in yourself",
};

static const char *setting_help(const struct setting *setting)
{
	static char camera_help[64];
	int row = playstation_row(setting);

	if (setting->variable && !strcmp(setting->variable, "HALO_MAP_SHARE_FROM") && setting->choice >= 0 &&
		setting->choice < 3)
		return map_downloads_help[setting->choice];

	/* (the debug camera's Black: the Vita button it is on) */
	if (row >= 0 && !strcmp(playstation_rows[row].key, "HALO_DEBUG_CAMERA"))
	{
		const struct setting *black = setting_named("HALO_XBOX_BLACK");

		if (black && strcmp(black->values[black->choice], "none"))
		{
			snprintf(camera_help, sizeof(camera_help), "Hold %s 1 s: follow, orbit, then a flying camera",
				black->names[black->choice]);
			return camera_help;
		}
	}
	return row >= 0 ? playstation_rows[row].help : setting->help;
}

/* the name of a row's choice: a touch zone's Xbox button by what it does */
static const char *setting_value_name(const struct setting *setting)
{
	if (setting->page == PAGE_TOUCH && setting->values[0] && !strcmp(setting->values[0], "off") &&
		playstation_terms() && setting->choice < VITA_XBOX_COUNT)
		return xbox_actions[setting->choice];
	return setting->names[setting->choice];
}

/* an Xbox face button in the menus (A B X Y), as the guide names it: the
Xbox's name, or the Vita button the menus put it on */
static const char *menu_button(char xbox)
{
	static const char *const vita_names[4] = { "Cross", "Circle", "Square", "Triangle" };
	static const char *const xbox_names[4] = { "A", "B", "X", "Y" };
	const char *letters = "ABXY";
	int index = (int)(strchr(letters, xbox) - letters);

	return playstation_terms() ? vita_names[index] : xbox_names[index];
}

/* the longer line at the bottom: what the selected line does, or what the
last action did */
static void help_line(char *text, int size, const struct line *line)
{
	char detail[96];
	const struct setting *setting = line && line->type == LINE_SETTING ? &settings[line->index] : NULL;
	int state = __atomic_load_n(&report_state, __ATOMIC_ACQUIRE);

	if (notice[0] && now_us() < notice_until)
		snprintf(text, (size_t)size, "%s", notice);
	else if (halo_screen_restart_needed())
		snprintf(text, (size_t)size, "Restart the game for this change");
	else if (restart_pending && setting && setting->restart)
		snprintf(text, (size_t)size, "Restart the game for this change");
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_SAVING)
		snprintf(text, (size_t)size, "Saving the report...");
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_SAVED)
		snprintf(text, (size_t)size, "Saved: %.56s", report_path);
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_FAILED)
		snprintf(text, (size_t)size, "Could not save the report");
	else if (line && line->type == LINE_MAP)
		snprintf(text, (size_t)size, "%s: on or off, or delete it",
			maps[line->index].format == MAP_XBOX ? "Xbox map" : maps[line->index].format == MAP_OTHER ?
			"Not a known map" : "PC map (needs PC maps On)");
	else if (!setting)
		text[0] = 0;
	else if (!strcmp(running_network, "online") && (setting->action == ACTION_JOIN_CODE ||
		setting->action == ACTION_BROWSE || (setting->variable && !strcmp(setting->variable, "HALO_NET_HOST_PUBLIC"))) &&
		p2p_status(detail, sizeof(detail)))
		snprintf(text, (size_t)size, "%.60s", detail);
	/* (ad hoc: the group is joined first, when not in it) */
	else if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2 &&
		(setting->action == ACTION_HOST || setting->action == ACTION_JOIN || setting->action == ACTION_CAMPAIGN ||
			setting->action == ACTION_ADHOC_JOIN))
		snprintf(text, (size_t)size, "First the system's dialog joins ad hoc room %d", choice_of("HALO_ADHOC_ROOM") + 1);
	else if (setting->variable && !strcmp(setting->variable, "HALO_VITA_NETWORK"))
		snprintf(text, (size_t)size, "%s", connection_help(setting));
	else
		snprintf(text, (size_t)size, "%s", setting_help(setting));
}

/* the tab bar: '\t', the tabs shown with '|' between, '*' before this one */
static int tab_bar(char *text, int size)
{
	int length = snprintf(text, (size_t)size, "\t"), index, first = 1;

	for (index = 0; index < TAB_COUNT && length < size; index++)
		if (tab_shown(index))
		{
			length += snprintf(text + length, (size_t)(size - length), "%s%s%s", first ? "" : "|",
				index == tab ? "*" : "", pages[index].name);
			first = 0;
		}
	return length;
}

/* the touch zones' diagram (vita_gxm.c menu_build draws it): a line of
'\x01', a character per zone in vita_controls.h's order (S the row's zone,
s while Off, A a zone set to an Xbox button, - one Off), a space, and a
character per zone for its Xbox button ('a' + the VITA_XBOX_* index);
nothing for a row that is not a zone's */
static void touch_diagram(char *text, int size, const struct setting *chosen)
{
	char zones[2 * VITA_ZONE_COUNT + 2];
	int zone, is_zone = 0;

	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
	{
		const struct setting *setting = setting_named(vita_touch_variables[zone]);
		int choice = setting ? setting->choice : 0;

		if (setting == chosen)
		{
			is_zone = 1;
			zones[zone] = choice ? 'S' : 's';
		}
		else
			zones[zone] = choice ? 'A' : '-';
		/* (capitals: the actions' short names, PlayStation's terms) */
		zones[VITA_ZONE_COUNT + 1 + zone] = (char)((playstation_terms() ? 'A' : 'a') + (choice < VITA_XBOX_COUNT ?
			choice : 0));
	}
	zones[VITA_ZONE_COUNT] = ' ';
	zones[2 * VITA_ZONE_COUNT + 1] = 0;
	if (is_zone)
		snprintf(text, (size_t)size, "\n\x01%s", zones);
}

/* the panel's buttons for the line chosen, in one line under its help */
static void footer_line(char *text, int size, const struct line *line)
{
	const struct setting *setting = line && line->type == LINE_SETTING ? &settings[line->index] : NULL;
	const char *middle = "";

	if (line && line->type == LINE_MAP)
		middle = "Left/right: on/off   Square: delete   ";
	else if (setting && setting->kind == KIND_ACTION)
		middle = "Cross: select   ";
	else if (setting && setting->kind == KIND_TEXT)
		middle = "Cross: type   ";
	else if (setting)
		middle = "Left/right: change   ";
	snprintf(text, (size_t)size, "L/R: tabs   %sCircle: %s", middle, page < TAB_COUNT ? "close" : "back");
}

/* what a row that opens a page says of it ("" for nothing) */
static void page_summary(char *text, int size, const struct setting *setting)
{
	int index, set = 0, moved = 0;

	text[0] = 0;
	switch (setting->opens)
	{
	case PAGE_BUTTONS:
		for (index = 0; index < SETTING_COUNT; index++)
			if (settings[index].page == PAGE_BUTTONS && settings[index].choice != shipped_choice[index] &&
				strcmp(settings[index].variable, "HALO_BUTTON_ICONS"))
				moved = 1;
		snprintf(text, (size_t)size, "%s", moved ? "Custom" : "As shipped");
		break;
	case PAGE_TOUCH:
		for (index = 0; index < SETTING_COUNT; index++)
			if (settings[index].page == PAGE_TOUCH && settings[index].choice)
				set++;
		if (set)
			snprintf(text, (size_t)size, "%d on", set);
		else
			snprintf(text, (size_t)size, "Off");
		break;
	case PAGE_MAPS:
		snprintf(text, (size_t)size, map_count == 1 ? "1 map" : "%d maps", map_count);
		break;
	}
}

/* the page shown, as vita_gxm.c menu_build draws it: the tab bar ('\t'),
a page's title ('\x03') under it, a row per line (its label, '\x02', its
value: the values make a column), the lines that are not rows ('\x04': the
game's state, the gyroscope's), an empty line for a gap, then the chosen
row's help ('\x05') and the panel's buttons ('\x06') at the bottom */
static void show_list(void)
{
	char text[2048];
	struct line lines[MAXIMUM_LINES];
	int count = page_lines(lines), length, index, number = 0, highlighted = 0;
	int *selected = &page_selected[page];
	char status[64], help[96];

	if (*selected >= count)
		*selected = count - 1;
	if (*selected < 0)
		*selected = 0;
	/* (a line that is not a row is not chosen) */
	if (count && lines[*selected].type == LINE_INFO)
		for (index = 0; index < count; index++)
			if (lines[index].type != LINE_INFO)
			{
				*selected = index;
				break;
			}
	length = tab_bar(text, sizeof(text));
	if (page >= TAB_COUNT && length < (int)sizeof(text))
	{
		length += snprintf(text + length, sizeof(text) - length, "\n\x03%s > %s", pages[tab].name, pages[page].name);
		number++;
	}
	/* (the map lines scroll: MAP_LINES at once, the chosen one among them) */
	if (page == PAGE_MAPS && count && lines[*selected].type == LINE_MAP)
	{
		int map = lines[*selected].index;

		if (map < map_scroll)
			map_scroll = map;
		if (map >= map_scroll + MAP_LINES)
			map_scroll = map - MAP_LINES + 1;
	}
	for (index = 0; index < count && length < (int)sizeof(text); index++)
	{
		const struct line *line = &lines[index];

		if (line->type == LINE_MAP && (line->index < map_scroll || line->index >= map_scroll + MAP_LINES))
			continue;
		/* (Modded maps: a gap between its switch and the maps) */
		if (page == PAGE_MAPS && line->type != LINE_SETTING && index && lines[index - 1].type == LINE_SETTING)
		{
			length += snprintf(text + length, sizeof(text) - length, "\n");
			number++;
		}
		number++;
		if (index == *selected)
			highlighted = number;
		if (line->type == LINE_INFO)
			length += snprintf(text + length, sizeof(text) - length, "\n%s%s", line->text[0] == '!' ? "" : "\x04",
				line->text);
		else if (line->type == LINE_MAP)
		{
			const struct map_entry *map = &maps[line->index];
			char size_name[24], name[24];

			size_text(size_name, sizeof(size_name), map->size);
			/* (the .yelo of a name: marked *) */
			snprintf(name, sizeof(name), "%.19s%s", map->name, strcasecmp(map->extension, "yelo") ? "" : "*");
			length += snprintf(text + length, sizeof(text) - length, "\n%s\x02  %-3s %8s  %s", name,
				map_disabled(map->name) ? "Off" : "On", size_name, map_format_name(map->format));
		}
		else
		{
			const struct setting *setting = &settings[line->index];

			if (setting->kind == KIND_TEXT)
			{
				const struct play_text *value = play_text_named(setting->variable);

				/* (the password hidden) */
				length += snprintf(text + length, sizeof(text) - length, "\n%s\x02  %s", setting_label(setting),
					!value->value[0] ? "none" : !strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD") ? "set" : value->value);
			}
			else if (setting->kind == KIND_ACTION)
			{
				char summary[24];

				page_summary(summary, sizeof(summary), setting);
				/* (two spaces first: the summary, or the >, where the other
				rows' values start, after their "< ") */
				length += snprintf(text + length, sizeof(text) - length, "\n%s\x02  %s%s>", setting_label(setting), summary,
					summary[0] ? "  " : "");
			}
			else
			{
				int last = choice_last(setting);

				/* (a row that applies after a restart: marked *) */
				length += snprintf(text + length, sizeof(text) - length, "\n%s%s\x02%c %s %c", setting_label(setting),
					setting->restart ? "*" : "", setting->choice > 0 ? '<' : ' ', setting_value_name(setting),
					setting->choice < last ? '>' : ' ');
			}
		}
	}
	if (page == PAGE_MAPS && map_count > MAP_LINES && length < (int)sizeof(text))
		length += snprintf(text + length, sizeof(text) - length, "\n\x04(maps %d-%d of %d)", map_scroll + 1,
			map_scroll + MAP_LINES < map_count ? map_scroll + MAP_LINES : map_count, map_count);
	/* (Multiplayer, after a gap: this Vita, the network game, internet or
	ad hoc play; the Play page: the last two) */
	if ((page == TAB_MULTIPLAYER || page == PAGE_PLAY) && length < (int)sizeof(text))
	{
		length += snprintf(text + length, sizeof(text) - length, "\n");
		if (page == TAB_MULTIPLAYER)
		{
			vita_line(status, sizeof(status));
			length += snprintf(text + length, sizeof(text) - length, "\n\x04%s", status);
			/* (the game's Multiplayer menu without OpenCE's screens) */
			if (halo_pc_menus_state < 0 && length < (int)sizeof(text))
				length += snprintf(text + length, sizeof(text) - length,
					"\n\x04Online menus need bitmaps.map, loc.map: README");
		}
		game_line(status, sizeof(status));
		if (length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n\x04%s", status);
		status_line(status, sizeof(status));
		if (status[0] && length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n\x04%s", status);
	}
	help_line(help, sizeof(help), count ? &lines[*selected] : NULL);
	if (length < (int)sizeof(text))
		length += snprintf(text + length, sizeof(text) - length, "\n\x05%s", help);
	footer_line(help, sizeof(help), count ? &lines[*selected] : NULL);
	if (length < (int)sizeof(text))
		length += snprintf(text + length, sizeof(text) - length, "\n\x06%s", help);
	/* the Touch zones page: the zones drawn beside the rows */
	if (count && lines[*selected].type == LINE_SETTING && length < (int)sizeof(text))
		touch_diagram(text + length, (int)sizeof(text) - length, &settings[lines[*selected].index]);
	vgxm_menu_set(text, highlighted);
}

static void show_code(void)
{
	char text[512], letters[32], cursor[32];
	int index, column = 0;

	/* "A B C D - E F G H", and a caret under the cursor's */
	for (index = 0; index < P2P_CODE_LENGTH_TYPED; index++)
	{
		if (index == 4)
		{
			letters[column] = '-';
			cursor[column++] = ' ';
			letters[column] = ' ';
			cursor[column++] = ' ';
		}
		letters[column] = code_typed[index];
		cursor[column++] = index == code_cursor ? '^' : ' ';
		letters[column] = ' ';
		cursor[column++] = ' ';
	}
	letters[column] = cursor[column] = 0;
	snprintf(text, sizeof(text), "%s\n\n    %s\n    %s\n%s\nUp/down: letter  Left/right: move\nCross: join  Circle: back",
		code_for_join ? "JOIN WITH A CODE\n(the host's Multiplayer tab shows it)" : "JOIN WITH A CODE", letters,
		cursor, strcmp(running_network, "online") ? "Connection must be Online (restart)" : "");
	vgxm_menu_set(text, 2);
}

/* the public games as the server browser lists them (one line each: the
name, the players, the map, then [pw] for a password and PC for a Halo PC
map), the chosen one's Rules and Players lines below, in the overlay's own
font: no art */
static void show_browse(void)
{
	char text[2048];
	int length, index;
	char detail[64];

	length = snprintf(text, sizeof(text), "PUBLIC GAMES");
	if (strcmp(running_network, "online"))
		length += snprintf(text + length, sizeof(text) - length, "\nConnection must be Online (restart)");
	else if (!browse_count)
	{
		p2p_status(detail, sizeof(detail));
		length += snprintf(text + length, sizeof(text) - length, "\nLooking for games (%.24s)", detail);
	}
	/* (a game a line: its name, players of most, map, [pw] for a password,
	PC for a Halo PC map) */
	for (index = 0; index < browse_count && length < (int)sizeof(text); index++)
	{
		const struct p2p_lobby_entry *entry = &browse_entries[index];
		char row[64];
		int end = snprintf(row, sizeof(row), "%-15.15s %2d/%-2d %-14.14s%s%s", entry->name, entry->players,
			entry->maximum, entry->map, entry->locked ? " [pw]" : "", entry->pc_map ? " PC" : "");

		while (end > 0 && row[end - 1] == ' ')
			row[--end] = 0;
		length += snprintf(text + length, sizeof(text) - length, "\n%s", row);
	}
	/* (the chosen game's Rules and Players lines; one that could not be
	joined this run says so) */
	if (browse_selected < browse_count && length < (int)sizeof(text))
	{
		const struct p2p_lobby_entry *entry = &browse_entries[browse_selected];

		if (entry->rules[0])
			length += snprintf(text + length, sizeof(text) - length, "\n%s%.*s", entry->failed ? "FAILED: " : "",
				entry->failed ? 38 : 46, entry->rules);
		if (entry->players_line[0] && length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n%.46s", entry->players_line);
	}
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\nCross: join   Square: refresh   Circle: back");
	vgxm_menu_set(text, browse_count ? browse_selected + 1 : 0);
}

/* the map chosen on the Modded maps page, or NULL */
static struct map_entry *selected_map(void)
{
	struct line lines[MAXIMUM_LINES];
	int count;

	if (page != PAGE_MAPS)
		return NULL;
	count = page_lines(lines);
	if (page_selected[PAGE_MAPS] < count && lines[page_selected[PAGE_MAPS]].type == LINE_MAP)
		return &maps[lines[page_selected[PAGE_MAPS]].index];
	return NULL;
}

static void show_delete(void)
{
	char text[512], size_name[16];
	const struct map_entry *map = selected_map();

	if (!map)
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	size_text(size_name, sizeof(size_name), map->size);
	snprintf(text, sizeof(text), "DELETE MAP\n\n%.40s.%s  (%s)\nand its picture and description, if any.\n\n"
		"It cannot be undone.\nCross: delete   O: keep", map->name, map->extension, size_name);
	vgxm_menu_set(text, -1);
}

/* why Host or Join cannot open System Link now (NULL: it can) */
static const char *guide_blocker(void)
{
	int state = multiplayer_state();

	if (strcmp(chosen_network(), running_network))
		return "Restart the game first: Connection changed";
	if (state == SYSTEM_LINK_STATE_PLAYING)
		return "Leave the level first: Start, then Quit";
	if (state == SYSTEM_LINK_STATE_IN_GAME)
		return "In a game: Start, then Quit, to leave it";
	if (state == SYSTEM_LINK_STATE_HOSTING)
		return "Already hosting: the game's lobby is open";
	if (state == SYSTEM_LINK_STATE_JOINING || state == SYSTEM_LINK_STATE_LOBBY)
		return "Already in a lobby: B there leaves it";
	if (strcmp(running_network, "adhoc") && state != SYSTEM_LINK_STATE_STARTING &&
		!halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS])
		return "No Wi-Fi: connect the Vita to a network";
	return NULL;
}

/* the game's answer, as the guide says it */
static const char *guide_answer_text(int answer)
{
	switch (answer)
	{
	case SYSTEM_LINK_ANSWER_IN_PLAY:
		return "Leave the level first: Start, then Quit";
	case SYSTEM_LINK_ANSWER_IN_LOBBY:
		return "Already in a lobby: B there leaves it";
	case SYSTEM_LINK_ANSWER_NO_NETWORK:
		return strcmp(running_network, "adhoc") ? "No Wi-Fi: connect the Vita to a network" :
			"Not in the ad hoc group: join it first";
	default:
		return "The game could not open it (see its message)";
	}
}

/* Host a game, Join a game: the steps, as the game's menus name the
buttons (the Xbox's, or with the Button icons PlayStation the Vita's), then
cross opens the game's System Link screen */
static void show_guide(void)
{
	char text[1536], line[64];
	int length, step = 1, host = guide_action == ACTION_HOST;
	int adhoc = !strcmp(running_network, "adhoc"), online = !strcmp(running_network, "online");
	const char *blocker = guide_blocker();

	vita_line(line, sizeof(line));
	length = snprintf(text, sizeof(text), "%s\n\n%s", host ? "HOST A GAME" : "JOIN A GAME", line);
	if (adhoc && vita_adhoc_state(NULL, 0) != 2)
		length += snprintf(text + length, sizeof(text) - length, "\n%d The system's dialog joins ad hoc room %d", step++,
			choice_of("HALO_ADHOC_ROOM") + 1);
	length += snprintf(text + length, sizeof(text) - length,
		"\n%d The game's System Link screen opens\n%d %s to join if asked, %s on a profile,%s%s again", step, step + 1,
		menu_button('A'), menu_button('A'), playstation_terms() ? "\n  " : " ", menu_button('A'));
	step += 2;
	if (host)
	{
		length += snprintf(text + length, sizeof(text) - length,
			"\n%d SYSTEM LINK GAMES: %s creates a game\n%d %s on a map, %s on a game type", step, menu_button('Y'),
			step + 1, menu_button('A'), menu_button('A'));
		step += 2;
		/* (the Custom Edition maps are in the map list with PC maps on) */
		if (!choice_of("HALO_CUSTOM_EDITION"))
			length += snprintf(text + length, sizeof(text) - length, "\n  (PC maps: Modded maps, PC maps On)");
		/* (the game as others see it: the Play page's rows) */
		length += snprintf(text + length, sizeof(text) - length, "\n  As \"%s\", %s players at most",
			play_text_named("HALO_NET_LOBBY_NAME")->value, setting_named("HALO_NET_MAX_PLAYERS")->names[choice_of(
			"HALO_NET_MAX_PLAYERS")]);
		length += snprintf(text + length, sizeof(text) - length,
			"\n%d Wait in the lobby for the others to\n  join; %s there starts the game sooner", step, menu_button('A'));
	}
	else
	{
		length += snprintf(text + length, sizeof(text) - length,
			"\n%d SYSTEM LINK GAMES: %s on the host's game\n  (the games %s show there)\n%d Wait in the lobby for the"
			" host to start\n  A map you lack comes from the host", step, menu_button('A'),
			adhoc ? "in the room" : online ? "you are connected to" : "on this network", step + 1);
	}
	/* (online: how the code's lookup goes; System Link lists the host's
	game once connected) */
	if (online && !host)
	{
		char detail[96];

		p2p_status(detail, sizeof(detail));
		/* (its first clause: "connected to the host", "code ABCD-EFGH
		found", "no game has code ABCD-EFGH", in the line's 46) */
		detail[strcspn(detail, ":;(")] = 0;
		while (detail[0] && detail[strlen(detail) - 1] == ' ')
			detail[strlen(detail) - 1] = 0;
		length += snprintf(text + length, sizeof(text) - length, "\nOnline: %.38s", detail);
		/* (a public game's password, when the listing says it was wrong) */
		if (play_join_state() == -1)
			length += snprintf(text + length, sizeof(text) - length, "\n  The password was wrong: back, and again");
		else if (play_join_state() == -2)
			length += snprintf(text + length, sizeof(text) - length, "\n  The game is gone from the public games");
		else if (!strstr(detail, "connected to the host"))
			length += snprintf(text + length, sizeof(text) - length, "\n  Wait for \"connected to the host\"");
	}
	else if (online)
		length += snprintf(text + length, sizeof(text) - length,
			"\nOnline: your code shows on the Play page\n  once the game is made; tell it to the others");
	/* (the Xbox's names, and which Vita button each is: the game's own
	menus show the Xbox's icons) */
	if (!playstation_terms())
		length += snprintf(text + length, sizeof(text) - length, "\nMenus: A Cross, B Circle, X Square, Y Triangle");
	if (guide_waiting)
		length += snprintf(text + length, sizeof(text) - length, "\n\nOpening System Link...\nCircle: back");
	else if (guide_problem[0] || blocker)
		length += snprintf(text + length, sizeof(text) - length, "\n\n!%s\nCircle: back",
			guide_problem[0] ? guide_problem : blocker);
	else
		length += snprintf(text + length, sizeof(text) - length, "\n\nCross: open System Link   Circle: back");
	vgxm_menu_set(text, -1);
}

static void show(void)
{
	last_shown = now_us();
	if (screen == SCREEN_CODE)
		show_code();
	else if (screen == SCREEN_BROWSE)
		show_browse();
	else if (screen == SCREEN_DELETE)
		show_delete();
	else if (screen == SCREEN_GUIDE)
		show_guide();
	else
		show_list();
}

static void change(struct setting *setting, int step)
{
	int choice = setting->choice + step;

	if (choice < 0 || choice >= setting->count)
		return;
	/* (Custom is not chosen: the Profile row reads it when a row it sets
	is changed) */
	if (strcmp(setting->variable, "HALO_PROFILE") == 0 && choice == PROFILE_CUSTOM)
		return;
	setting->choice = choice;
	apply_value(setting);
	/* (a row that applies after a restart says so on its help line from
	now on, and every line does for a few seconds) */
	if (setting->restart)
	{
		restart_pending = 1;
		set_notice("Restart the game for this change");
	}
	if (strcmp(setting->variable, "HALO_PROFILE") == 0)
	{
		/* (a profile sets its rows; Custom leaves them as they are) */
		if (apply_profile(choice))
		{
			restart_pending = 1;
			set_notice("Restart the game for this change");
		}
	}
	else
	{
		/* (a row a profile sets, changed: the profile it now matches, or
		Custom) */
		struct setting *profile = setting_named("HALO_PROFILE");

		profile->choice = matching_profile();
		apply_value(profile);
	}
	if (strcmp(setting->variable, "XV_FPS") == 0)
		vgxm_overlay_enable(atoi(setting->values[choice]));
	if (strcmp(setting->variable, "HALO_UPSCALE_FILTER") == 0)
		vgxm_upscale_filter_set(choice);
	/* (listed or not takes effect at once, also while hosting) */
	if (strcmp(setting->variable, "HALO_NET_HOST_PUBLIC") == 0)
		play_listing_set_public(choice);
	if (strcmp(setting->variable, "HALO_NET_COOP_PUBLIC") == 0)
		play_listing_set_coop_public(choice);
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	if (setting->dev)
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: dev switch %s=%s (settings panel)", setting->variable,
			setting->values[choice][0] ? setting->values[choice] : "(default)");
		vita_host_log(line);
	}
}

/* a row set as the panel sets it, its profile worked out again (applied,
saved); 0 if there is no such row. The game's Server Setup sets the hosted
game's settings so (port/linux/game/menu_functions.c), and (debug)
HALO_TEST_COMMANDS "@set VARIABLE value" (main.c) */
int vita_settings_set(const char *variable, const char *value)
{
	struct setting *setting = setting_named(variable);

	/* (a Play page text: as typed) */
	if (setting && setting->kind == KIND_TEXT)
	{
		char line[96];

		play_text_set(play_text_named(variable), value, 1);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
		save();
		/* (a password is never written to halo.log) */
		if (!strcmp(variable, "HALO_NET_LOBBY_PASSWORD"))
			snprintf(line, sizeof(line), "settings: password %s (the game)",
				play_text_named(variable)->value[0] ? "set" : "none");
		else
			snprintf(line, sizeof(line), "settings: %s=%s (the game)", variable, play_text_named(variable)->value);
		vita_host_log(line);
		return 1;
	}
	if (!setting || setting->kind != KIND_CHOICE)
		return 0;
	change(setting, find_choice(setting, value) - setting->choice);
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: %s=%s (the game)", variable, setting->values[setting->choice]);
		vita_host_log(line);
	}
	return 1;
}

/* the name of what a guide's action does, for a message over the game */
static const char *guide_title(int action)
{
	return action == ACTION_HOST ? "Host a game" : action == ACTION_CAMPAIGN ? "Host co-op campaign" : "Join a game";
}

/* asks the game for its System Link screen, or Campaign's (the answer:
guide_poll) */
static void guide_request(void)
{
	char line[96];

	guide_problem[0] = 0;
	halo_system_link_answer = SYSTEM_LINK_ANSWER_NONE;
	__atomic_store_n(&halo_system_link_request, guide_action == ACTION_HOST ? SYSTEM_LINK_REQUEST_HOST :
		guide_action == ACTION_CAMPAIGN ? SYSTEM_LINK_REQUEST_CAMPAIGN : SYSTEM_LINK_REQUEST_JOIN, __ATOMIC_RELEASE);
	guide_waiting = 1;
	guide_sent = now_us();
	if (guide_action == ACTION_CAMPAIGN)
		snprintf(line, sizeof(line), "settings: host co-op campaign: asked the game for its Campaign screen (%s)",
			running_network);
	else
		snprintf(line, sizeof(line), "settings: %s a game: asked the game for its System Link screen (%s)",
			guide_action == ACTION_HOST ? "host" : "join", running_network);
	vita_host_log(line);
}

static void close_panel(void)
{
	if (screen == SCREEN_BROWSE)
		p2p_lobby_browse(0);
	panel_open = 0;
	screen = SCREEN_LIST;
	/* (it opens again on the tab's own page) */
	page = tab;
	held_after_close = previous_buttons;
	vgxm_menu_set(NULL, 0);
}

/* Publish from the game/event thread; only the input thread writes the menu. */
static pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;
static char pending_message[2048];
static int message_pending, message_visible;
/* map sharing's question (port/linux/game/map_share.c): cross yes, circle
no; -1 until answered */
static char pending_question[2048];
static int question_pending, question_visible, question_withdrawn;
static volatile int question_result = -1;
/* map sharing's progress: redrawn when it changes; circle cancels */
static char pending_progress[2048];
static int progress_changed, progress_visible, progress_hide;
static volatile int progress_cancel;

/* `title`, `text` and `footer` in lines of the overlay's width */
static size_t format_box(char *formatted, size_t size, const char *title, const char *text, const char *footer)
{
    size_t used = 0;
    int column = 0;
    const char *parts[] = {title, "\n\n", text, footer};
    for (int part = 0; part < 4; ++part)
    {
        const char *cursor = parts[part];
        while (*cursor && used < size - 1)
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
                if (used >= size - 1) break;
                /* (a space the line ended at does not start the next) */
                if (ch == ' ') continue;
            }
            formatted[used++] = ch;
            column = ch == '\n' ? 0 : column + 1;
        }
    }
    formatted[used] = 0;
    return used;
}

void vita_settings_message(const char *title, const char *text)
{
    char formatted[2048];
    size_t used = format_box(formatted, sizeof(formatted), title, text, "\n\nCross / Circle: close");

    pthread_mutex_lock(&message_lock);
    memcpy(pending_message, formatted, used + 1);
    message_pending = 1;
    pthread_mutex_unlock(&message_lock);
}

/* a yes or no question over the game (NULL withdraws it) */
void vita_settings_question(const char *title, const char *text)
{
    char formatted[2048];
    size_t used = text ? format_box(formatted, sizeof(formatted), title ? title : "", text,
        "\n\nCross: yes     Circle: no") : 0;

    pthread_mutex_lock(&message_lock);
    question_result = -1;
    if (text)
    {
        memcpy(pending_question, formatted, used + 1);
        question_pending = 1;
        question_withdrawn = 0;
    }
    else
    {
        question_pending = 0;
        question_withdrawn = 1;
    }
    pthread_mutex_unlock(&message_lock);
}

int vita_settings_question_answer(void)
{
    return question_result;
}

/* a progress line over the game (NULL hides it), which circle cancels */
void vita_settings_progress(const char *title, const char *text)
{
    char formatted[2048];
    size_t used = text ? format_box(formatted, sizeof(formatted), title ? title : "", text,
        "\n\nCircle: cancel") : 0;

    pthread_mutex_lock(&message_lock);
    if (text)
    {
        if (!progress_visible && !progress_changed)
            progress_cancel = 0;
        memcpy(pending_progress, formatted, used + 1);
        progress_changed = 1;
        progress_hide = 0;
    }
    else
    {
        progress_changed = 0;
        progress_hide = 1;
    }
    pthread_mutex_unlock(&message_lock);
}

int vita_settings_progress_cancelled(void)
{
    return progress_cancel;
}

/* (the input thread) the question and the progress line, below a message:
nonzero when one holds the buttons */
static int question_progress_input(unsigned long pressed)
{
    int shown_now = 0;

    pthread_mutex_lock(&message_lock);
    if (question_withdrawn)
    {
        question_withdrawn = 0;
        if (question_visible)
        {
            question_visible = 0;
            vgxm_menu_set(NULL, 0);
        }
    }
    if (question_pending)
    {
        vgxm_menu_set(pending_question, -1);
        question_pending = 0;
        question_visible = 1;
        shown_now = 1;
        panel_open = 0;
    }
    if (progress_hide)
    {
        progress_hide = 0;
        if (progress_visible && !question_visible)
            vgxm_menu_set(NULL, 0);
        progress_visible = 0;
    }
    if (progress_changed && !question_visible)
    {
        vgxm_menu_set(pending_progress, -1);
        progress_changed = 0;
        progress_visible = 1;
        panel_open = 0;
    }
    pthread_mutex_unlock(&message_lock);

    if (question_visible)
    {
        if (!shown_now && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE)))
        {
            question_result = (pressed & VITA_BUTTON_CROSS) ? 1 : 0;
            question_visible = 0;
            vgxm_menu_set(NULL, 0);
        }
        return 1;
    }
    if (progress_visible)
    {
        if (pressed & VITA_BUTTON_CIRCLE)
            progress_cancel = 1;
        return 1;
    }
    return 0;
}

/* the Controls tab's rows as shipped, its pages' too (but the gyro's rows
and Show dev settings): the touch zones Off, the buttons in Xita's layout,
look and crouch as they were */
static void reset_controls(void)
{
	int index;

	for (index = 0; index < SETTING_COUNT; index++)
	{
		struct setting *setting = &settings[index];

		if (pages[setting->page].tab != TAB_CONTROLS || setting->kind != KIND_CHOICE ||
			strcmp(setting->variable, "HALO_DEV_SETTINGS") == 0 || strncmp(setting->variable, "HALO_GYRO", 9) == 0 ||
			strcmp(setting->variable, "HALO_BUTTON_ICONS") == 0)
			continue;
		setting->choice = shipped_choice[index];
		apply_value(setting);
	}
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	set_notice("Controls as shipped");
	vita_host_log("settings: controls reset (settings panel)");
}

static void act(const struct setting *setting)
{
	switch (setting->action)
	{
	/* (online: a game joined by its code, or picked from the public games,
	then Join's steps with how the lookup goes, as Join a game was in 1.0.3) */
	case ACTION_JOIN_CODE:
		guide_action = ACTION_JOIN;
		guide_problem[0] = 0;
		code_for_join = 1;
		screen = guide_blocker() ? SCREEN_GUIDE : SCREEN_CODE;
		break;
	case ACTION_BROWSE:
		guide_action = ACTION_JOIN;
		guide_problem[0] = 0;
		code_for_join = 1;
		if (guide_blocker())
		{
			screen = SCREEN_GUIDE;
			break;
		}
		screen = SCREEN_BROWSE;
		browse_count = browse_selected = 0;
		p2p_lobby_browse(1);
		break;
	/* (ad hoc: the room's group joined through the system's dialog, the
	panel closed meanwhile; the game's System Link screen then finds the
	others) */
	case ACTION_ADHOC_JOIN:
		if (strcmp(running_network, "adhoc"))
			set_notice("Restart the game for Ad hoc first");
		else if (vita_adhoc_state(NULL, 0) == 2)
			set_notice("In room %d's group already", choice_of("HALO_ADHOC_ROOM") + 1);
		else
		{
			close_panel();
			vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
			vita_host_log("settings: ad hoc: joining the room's group (settings panel)");
		}
		break;
	case ACTION_SAVE_REPORT:
		report_start();
		break;
	case ACTION_RESET_CONTROLS:
		reset_controls();
		break;
	case ACTION_PAGE:
		page = setting->opens;
		if (page == PAGE_MAPS)
			maps_scan();
		break;
	/* (its progress line then takes the screen) */
	case ACTION_CE_EXTRACT:
		if (vita_ce_installer_start() == 0)
			set_notice("Taking them from %.28s", maps_installer);
		else
			set_notice("No installer, or the files are there");
		maps_scan();
		break;
	case ACTION_HOST:
	case ACTION_JOIN:
		guide_action = setting->action;
		guide_problem[0] = 0;
		code_for_join = 0;
		screen = SCREEN_GUIDE;
		break;
	/* (the Campaign screen at once: Y on its difficulty hosts co-op; with
	Ad hoc, the room's group joined first, guide_input's way) */
	case ACTION_CAMPAIGN:
		guide_action = ACTION_CAMPAIGN;
		guide_problem[0] = 0;
		if (guide_blocker())
		{
			set_notice("%s", guide_blocker());
			break;
		}
		if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2)
		{
			adhoc_pending = ACTION_CAMPAIGN;
			close_panel();
			vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
			return;
		}
		guide_request();
		break;
	}
}

/* (every frame) the game's answer to guide_request: opened, the panel
closes; not, the guide says why (a message over the game when the panel
is closed: the ad hoc dialog came first). The game answers within a
frame or two: one that has not in 3 s (busy loading) is asked no more */
static void guide_poll(unsigned long long now)
{
	const char *problem = NULL;

	if (!guide_waiting)
		return;
	if (__atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE) == SYSTEM_LINK_REQUEST_NONE)
	{
		int answer = halo_system_link_answer;

		guide_waiting = 0;
		if (answer == SYSTEM_LINK_ANSWER_OPENED)
		{
			if (panel_open)
				close_panel();
			return;
		}
		problem = guide_answer_text(answer);
	}
	else if (now - guide_sent > 3000000ULL)
	{
		int request = __atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE);

		/* (unless the game took it meanwhile: then its answer, next frame) */
		if (request == SYSTEM_LINK_REQUEST_NONE || !__atomic_compare_exchange_n(&halo_system_link_request, &request,
			SYSTEM_LINK_REQUEST_NONE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return;
		guide_waiting = 0;
		problem = "The game did not answer: try again";
	}
	else
		return;
	snprintf(guide_problem, sizeof(guide_problem), "%s", problem);
	/* (the guide redrawn at once) */
	last_shown = 0;
	/* (Host co-op campaign has no screen of its own: the help line says it) */
	if (panel_open && guide_action == ACTION_CAMPAIGN)
		set_notice("%s", problem);
	else if (!panel_open || screen != SCREEN_GUIDE)
		vita_settings_message(guide_title(guide_action), problem);
}

/* the guide's buttons: cross opens System Link (with Ad hoc, after the
system's dialog has joined the room's group), circle goes back */
static void guide_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		/* (an answer still to come closes nothing: it is kept, unshown) */
		screen = SCREEN_LIST;
		guide_problem[0] = 0;
		return;
	}
	if (!(pressed & VITA_BUTTON_CROSS) || guide_waiting)
		return;
	guide_problem[0] = 0;
	if (guide_blocker())
		return;
	if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2)
	{
		/* (the panel closes: the system's dialog takes the screen) */
		adhoc_pending = guide_action;
		close_panel();
		vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
		return;
	}
	guide_request();
}

/* the code screen's buttons */
static void code_input(unsigned long pressed, unsigned long buttons, unsigned long long now)
{
	static const char alphabet[] = P2P_CODE_ALPHABET;
	int step = 0;

	if (pressed & VITA_BUTTON_CIRCLE)
	{
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_CROSS)
	{
		char code[P2P_CODE_SIZE];

		snprintf(code, sizeof(code), "%.4s-%.4s", code_typed, code_typed + 4);
		if (strcmp(running_network, "online"))
			set_notice("Connection must be Online (restart)");
		else
		{
			p2p_join_code(code);
			set_notice("Looking up %s...", code);
		}
		screen = code_for_join && !strcmp(running_network, "online") ? SCREEN_GUIDE : SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_LEFT)
		code_cursor = (code_cursor + P2P_CODE_LENGTH_TYPED - 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_RIGHT)
		code_cursor = (code_cursor + 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_UP)
		step = 1;
	else if (pressed & VITA_BUTTON_DOWN)
		step = -1;
	else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 150000)
		step = (buttons & VITA_BUTTON_UP) ? 1 : -1;
	if (step)
	{
		const char *at = strchr(alphabet, code_typed[code_cursor]);
		int position = at ? (int)(at - alphabet) : 0;
		int size = (int)sizeof(alphabet) - 1;

		code_typed[code_cursor] = alphabet[(position + step + size) % size];
		last_move = now;
	}
}

/* the public lobby screen's buttons */
static void browse_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		p2p_lobby_browse(0);
		screen = SCREEN_LIST;
		return;
	}
	if ((pressed & VITA_BUTTON_UP) && browse_selected > 0)
		browse_selected--;
	if ((pressed & VITA_BUTTON_DOWN) && browse_selected < browse_count - 1)
		browse_selected++;
	/* (every host asked to publish again) */
	if (pressed & VITA_BUTTON_SQUARE)
		p2p_lobby_refresh();
	if ((pressed & VITA_BUTTON_CROSS) && browse_selected < browse_count)
	{
		const struct p2p_lobby_entry *entry = &browse_entries[browse_selected];

		/* (a locked game: its password first, on the system's keyboard) */
		if (entry->locked)
		{
			browse_join_pending = 1;
			if (vita_ime_open("Password", "", PLAY_TEXT_LENGTH, 1) == 0)
			{
				vgxm_menu_set(NULL, 0);
				return;
			}
			browse_join_pending = 0;
		}
		play_join_entry(entry, NULL);
		set_notice("Joining %.20s...", entry->name);
		p2p_lobby_browse(0);
		screen = SCREEN_GUIDE;
	}
}

/* the delete confirmation's buttons */
static void delete_input(unsigned long pressed)
{
	struct map_entry *map = selected_map();

	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if ((pressed & VITA_BUTTON_CROSS) && map)
	{
		if (halo_cache_map_in_use(map->name))
			set_notice("%.30s is in play: leave it first", map->name);
		else
		{
			map_delete(map);
			maps_scan();
		}
		screen = SCREEN_LIST;
	}
}

/* the public games again (not this machine's own game); the chosen one
stays chosen while the list changes under it */
static void browse_refresh(void)
{
	struct p2p_lobby_entry entry;
	char selected[sizeof(entry.id)] = "";
	int index;

	if (browse_selected < browse_count)
		memcpy(selected, browse_entries[browse_selected].id, sizeof(selected));
	browse_count = 0;
	for (index = 0; browse_count < BROWSE_LINES && p2p_lobby_entry(index, &entry); index++)
	{
		if (entry.own)
			continue;
		if (selected[0] && !strcmp(entry.id, selected))
			browse_selected = browse_count;
		browse_entries[browse_count++] = entry;
	}
	if (browse_selected >= browse_count)
		browse_selected = browse_count ? browse_count - 1 : 0;
}

/* a Play page text row: the system's keyboard opened on its text
(nonzero if it opened) */
static int text_edit(const struct setting *setting)
{
	const struct play_text *text = play_text_named(setting->variable);
	int password = !strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD");

	if (vita_ime_open(password ? "Password (empty: none)" : "Lobby name", text->value, PLAY_TEXT_LENGTH, password) != 0)
	{
		set_notice("The keyboard did not open");
		return 0;
	}
	ime_setting = setting;
	vgxm_menu_set(NULL, 0);
	return 1;
}

/* (each frame while the keyboard is up) nonzero while it is: the text
typed goes to its row (or the password to the chosen public game's join),
and the panel is back */
static int ime_input(void)
{
	char typed[64];
	int result;

	if (!ime_setting && !browse_join_pending)
		return 0;
	result = vita_ime_poll(typed, sizeof(typed));
	if (result == 0)
		return 1;
	if (ime_setting)
	{
		struct play_text *text = play_text_named(ime_setting->variable);

		if (result > 0)
		{
			char line[96];

			play_text_set(text, typed, 1);
			__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
			save();
			if (!strcmp(text->variable, "HALO_NET_LOBBY_PASSWORD"))
				snprintf(line, sizeof(line), "settings: password %s (settings panel)", text->value[0] ? "set" : "none");
			else
				snprintf(line, sizeof(line), "settings: %s=%s (settings panel)", text->variable, text->value);
			vita_host_log(line);
		}
		ime_setting = NULL;
	}
	else
	{
		browse_join_pending = 0;
		if (result > 0 && browse_selected < browse_count)
		{
			play_join_entry(&browse_entries[browse_selected], typed);
			set_notice("Joining %.20s...", browse_entries[browse_selected].name);
			p2p_lobby_browse(0);
			screen = SCREEN_GUIDE;
		}
	}
	if (panel_open)
		show();
	return 1;
}

/* (each frame) a line of text the game's menus asked for
(system_link_shortcut.h, port/linux/game/menu_functions.c): the system's
keyboard opened on it, and the text typed handed back; nonzero while the
keyboard is up (the game then gets no buttons) */
static int game_text_input(void)
{
	int state = __atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE);
	char typed[HALO_TEXT_INPUT_SIZE];
	int result;

	if (state == HALO_TEXT_INPUT_REQUESTED)
	{
		/* (not over the panel: the game's menus are not taking presses then) */
		if (panel_open)
			return 0;
		halo_text_input_title[HALO_TEXT_INPUT_SIZE - 1] = 0;
		halo_text_input_text[HALO_TEXT_INPUT_SIZE - 1] = 0;
		if (vita_ime_open(halo_text_input_title, halo_text_input_text, halo_text_input_maximum,
			halo_text_input_password) != 0)
		{
			vita_host_log("ime: the game's typing did not open");
			__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_CANCELLED, __ATOMIC_RELEASE);
			return 0;
		}
		__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_OPEN, __ATOMIC_RELEASE);
		return 1;
	}
	if (state != HALO_TEXT_INPUT_OPEN)
		return 0;
	result = vita_ime_poll(typed, sizeof(typed));
	if (result == 0)
		return 1;
	if (result > 0)
		snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", typed);
	__atomic_store_n(&halo_text_input_state, result > 0 ? HALO_TEXT_INPUT_DONE : HALO_TEXT_INPUT_CANCELLED,
		__ATOMIC_RELEASE);
	/* (the presses that closed the keyboard are not the game's) */
	held_after_close = previous_buttons;
	return 1;
}

/* to the next tab shown, or the one before */
static void tab_step(int step)
{
	int next = tab;

	do
		next = (next + step + TAB_COUNT) % TAB_COUNT;
	while (!tab_shown(next));
	tab = page = next;
	notice[0] = 0;
	/* (Multiplayer's Modded maps row says how many) */
	if (tab == TAB_MULTIPLAYER)
		maps_scan();
}

int vita_settings_input(const struct vita_host_pad *pad)
{
	unsigned long buttons = pad->buttons;
	unsigned long pressed = buttons & ~previous_buttons;
	int both = (buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_START);
	unsigned long long now = now_us();
    int message_opened = 0;

	previous_buttons = buttons;
	held_after_close &= buttons;
	if (held_after_close && !panel_open)
		return 1;
	/* (the system's ad hoc dialog reads the pad itself: the game must not
	act on the same presses) */
	if (vita_adhoc_state(NULL, 0) == 1)
		return 1;
	/* (nor while the system's keyboard is up) */
	if (ime_input())
		return 1;
	/* (the game's menus' own typing, the same keyboard: OpenCE's multiplayer
	screens) */
	if (game_text_input())
		return 1;
	/* (Host or Join with Ad hoc: the dialog done, System Link next, or a
	message saying it did not join) */
	if (adhoc_pending)
	{
		if (vita_adhoc_state(NULL, 0) == 2)
		{
			guide_action = adhoc_pending;
			guide_request();
		}
		else
			vita_settings_message(guide_title(adhoc_pending),
				"The Vita did not join the ad hoc group. Try again, or another Ad hoc dialog way (Dev tab).");
		adhoc_pending = 0;
	}
	guide_poll(now);
	/* (the game's answer closed the panel just now: the buttons still held,
	the cross that asked included, are not the game's) */
	if (held_after_close && !panel_open)
		return 1;
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
            /* (what the message covered comes back) */
            pthread_mutex_lock(&message_lock);
            if (question_visible)
            {
                question_visible = 0;
                question_pending = 1;
            }
            else if (progress_visible)
                progress_changed = 1;
            pthread_mutex_unlock(&message_lock);
        }
        return 1;
    }
    if (question_progress_input(pressed))
    {
        both_since = 0;
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
				if (!tab_shown(tab))
					tab = page = TAB_GRAPHICS;
				if (tab == TAB_MULTIPLAYER)
					maps_scan();
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
	if (screen == SCREEN_CODE)
	{
		code_input(pressed, buttons, now);
		show();
		return 1;
	}
	if (screen == SCREEN_BROWSE)
	{
		browse_input(pressed);
		/* (the keyboard took the screen for a password) */
		if (browse_join_pending)
			return 1;
		/* (the list as it fills, twice a second) */
		if (screen == SCREEN_BROWSE && now - last_shown > 500000)
			browse_refresh();
		if (pressed || now - last_shown > 500000)
			show();
		return 1;
	}
	if (screen == SCREEN_DELETE)
	{
		delete_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_GUIDE)
	{
		guide_input(pressed);
		/* (redrawn twice a second: the game's state, its answer) */
		if (panel_open && (pressed || now - last_shown > 500000))
			show();
		return 1;
	}
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		/* (a page a row opened: back to its tab, on that row) */
		if (page != tab)
		{
			page = tab;
			notice[0] = 0;
			show();
		}
		else
			close_panel();
		return 1;
	}
	if (pressed & (VITA_BUTTON_L | VITA_BUTTON_R))
	{
		tab_step((pressed & VITA_BUTTON_L) ? -1 : 1);
		show();
		return 1;
	}
	{
		/* up and down step over the lines that can be chosen, and repeat
		while held */
		struct line lines[MAXIMUM_LINES];
		int count = page_lines(lines);
		int *selected = &page_selected[page];
		int step = 0;

		if (pressed & VITA_BUTTON_UP)
			step = -1;
		else if (pressed & VITA_BUTTON_DOWN)
			step = 1;
		else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 250000)
			step = (buttons & VITA_BUTTON_UP) ? -1 : 1;
		if (*selected >= count)
			*selected = count - 1;
		if (step && count)
		{
			int tries;

			for (tries = 0; tries < count; tries++)
			{
				*selected = (*selected + step + count) % count;
				if (lines[*selected].type != LINE_INFO)
					break;
			}
			last_move = now;
		}
		if (count && (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE)))
		{
			struct line *line = &lines[*selected];

			if (line->type == LINE_MAP)
			{
				struct map_entry *map = &maps[line->index];

				if (pressed & VITA_BUTTON_SQUARE)
					screen = SCREEN_DELETE;
				else if (pressed & VITA_BUTTON_LEFT)
					map_set_disabled(map->name, 1);
				else if (pressed & VITA_BUTTON_RIGHT)
					map_set_disabled(map->name, 0);
				else
					map_set_disabled(map->name, !map_disabled(map->name));
			}
			else if (line->type == LINE_SETTING && settings[line->index].kind == KIND_TEXT)
			{
				/* (the system's keyboard: the panel hides meanwhile, and the
				text comes back in ime_input) */
				if ((pressed & VITA_BUTTON_CROSS) && text_edit(&settings[line->index]))
					return 1;
			}
			else if (line->type == LINE_SETTING && settings[line->index].kind == KIND_ACTION)
			{
				if (pressed & (VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS))
				{
					notice[0] = 0;
					act(&settings[line->index]);
					if (!panel_open)
						return 1;
				}
			}
			else if (line->type == LINE_SETTING && !(pressed & VITA_BUTTON_SQUARE))
			{
				struct setting *setting = &settings[line->index];
				int step = (pressed & VITA_BUTTON_LEFT) ? -1 : 1;

				if (setting->choice + step <= choice_last(setting))
					change(setting, step);
			}
		}
	}
	/* (redrawn on a press, and every half second for the status line: a
	code appears when the game starts hosting; the report's progress) */
	if (pressed || now - last_shown > 500000)
		show();
	return 1;
}
