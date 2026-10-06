/*
VITA_CONTROLS.H

The mapping layer between the Vita's controls and the Xbox controller the
game reads (port/vita/host/vita_controls.c): the touch zones on the front
screen and the rear pad, the game actions a zone or a button can be set to
in the settings panel's Controls tab, and the Xbox controller's buttons
those actions press. Nothing here touches the SDK, so the desktop test
(port/vita/tests/vita_controls_test.c) builds it as it is. Like
vita_host.h, the structures are of 32-bit scalars only: the platform layer
(clang, the game's ABI) and the host (VitaSDK's GCC) both use them.
*/

#ifndef __HALO_VITA_CONTROLS_H
#define __HALO_VITA_CONTROLS_H

/* ---------- touch zones */

enum
{
	VITA_TOUCH_FRONT,
	VITA_TOUCH_REAR,
};

/* the zones, in the order of their bits in vita_host_pad.touch */
enum
{
	VITA_ZONE_TOP_LEFT,
	VITA_ZONE_TOP_RIGHT,
	VITA_ZONE_LEFT_EDGE,
	VITA_ZONE_RIGHT_EDGE,
	VITA_ZONE_REAR_LEFT,
	VITA_ZONE_REAR_RIGHT,
	VITA_ZONE_COUNT
};

/* a touch on the rear pad counts once held this long (a brush of the
fingers holding the Vita does not); a front one at once */
#define VITA_TOUCH_REAR_HOLD_US 100000ULL

/* both panels' positions are in the screen's pixels, 960 x 544 (the rear
pad scaled to the same, as seen from the front) */
#define VITA_TOUCH_WIDTH 960
#define VITA_TOUCH_HEIGHT 544

struct vita_touch_zone
{
	int panel;
	/* the rectangle, right and bottom excluded */
	int left, top, right, bottom;
	/* the pad script's name (vita_input.c HALO_PAD_FILE) */
	const char *script_name;
};

extern const struct vita_touch_zone vita_touch_zones[VITA_ZONE_COUNT];

/* the zone a position on a panel is in, or -1 */
int vita_touch_zone_at(int panel, int x, int y);

/* one finger on a panel as the panel reports it this frame */
struct vita_touch_contact
{
	int panel;
	int id;
	int x, y;
};

#define VITA_TOUCH_TRACKED 16

/* the fingers followed from frame to frame: the zone each started in (-1:
outside every zone, so it never counts) and since when */
struct vita_touch_tracker
{
	struct
	{
		int used;
		int panel, id, zone;
		unsigned long long since;
	} fingers[VITA_TOUCH_TRACKED];
};

/* the zones held this frame (a bit per zone): each finger counts for the
zone it started in, while it stays down, once held long enough for its
panel; several fingers each count on their own. *started (if given) is
how many fingers came down this frame */
unsigned long vita_touch_update(struct vita_touch_tracker *tracker, const struct vita_touch_contact *contacts,
	int count, unsigned long long now_us, int *started);

/* ---------- actions */

enum
{
	VITA_ACTION_OFF,
	VITA_ACTION_MELEE,
	VITA_ACTION_GRENADE,
	VITA_ACTION_FLASHLIGHT,
	VITA_ACTION_ZOOM,
	VITA_ACTION_CROUCH,
	VITA_ACTION_RELOAD,
	VITA_ACTION_SWITCH_WEAPON,
	VITA_ACTION_SWITCH_GRENADE,
	VITA_ACTION_JUMP,
	VITA_ACTION_SCOREBOARD,
	VITA_ACTION_FIRE,
	VITA_ACTION_COUNT
};

/* the actions as settings.txt values and as the panel shows them (in the
enum's order; macros, for the panel's row initialisers) */
#define VITA_ACTION_VALUES "off", "melee", "grenade", "flashlight", "zoom", "crouch", "reload", "weapon", \
	"grenade_switch", "jump", "scoreboard", "fire"
#define VITA_ACTION_NAMES "Off", "Melee", "Throw grenade", "Flashlight", "Zoom", "Crouch", "Reload/action", \
	"Switch weapon", "Switch grenade", "Jump", "Scoreboard", "Fire"

/* the action a settings value names; VITA_ACTION_OFF if none */
int vita_action_named(const char *value);

/* the buttons an action can be put on (in play), as settings.txt values and
as the panel shows them; "none" leaves the action on no button */
#define VITA_BUTTON_CHOICES 11
#define VITA_BUTTON_VALUES "cross", "circle", "square", "triangle", "l", "r", "up", "down", "left", "right", "none"
#define VITA_BUTTON_NAMES "Cross", "Circle", "Square", "Triangle", "L", "R", "D-pad up", "D-pad down", \
	"D-pad left", "D-pad right", "None"

/* the button (a VITA_BUTTON_* bit, 0 for none) a settings value names, or
`fallback` if it names none */
unsigned long vita_button_named(const char *value, unsigned long fallback);

/* the settings variable of each zone's action, and of each remappable
action's button (NULL: the action has no row, Off and Scoreboard: Select
is always the scoreboard) */
extern const char *const vita_touch_variables[VITA_ZONE_COUNT];
extern const char *const vita_button_variables[VITA_ACTION_COUNT];
/* each action's button as shipped (the README's layout) */
extern const unsigned long vita_button_defaults[VITA_ACTION_COUNT];

/* ---------- the mapping */

struct vita_controls_config
{
	int zone_action[VITA_ZONE_COUNT];
	unsigned long action_button[VITA_ACTION_COUNT];
	/* HALO_CROUCH_TOGGLE */
	int crouch_toggle;
};

/* the zones' actions and the actions' buttons from the environment (the
panel's settings), defaults for what is unset */
void vita_controls_config_load(struct vita_controls_config *config);

/* what the mapping remembers between frames (the crouch toggle) */
struct vita_controls_state
{
	int crouched;
	int crouch_was_down;
};

/* the Xbox controller's digital buttons (XINPUT_GAMEPAD_*) and its analog
buttons by index (A B X Y BLACK WHITE, left and right triggers) */
#define VITA_PAD_DPAD_UP 0x0001
#define VITA_PAD_DPAD_DOWN 0x0002
#define VITA_PAD_DPAD_LEFT 0x0004
#define VITA_PAD_DPAD_RIGHT 0x0008
#define VITA_PAD_START 0x0010
#define VITA_PAD_BACK 0x0020
#define VITA_PAD_LEFT_THUMB 0x0040
#define VITA_PAD_RIGHT_THUMB 0x0080

struct vita_controls_output
{
	unsigned long digital;
	unsigned char analog[8];
};

/* the Xbox controller's buttons for the Vita's buttons and the touch zones
held. In the game's menus the layout is fixed (the D-pad is the D-pad,
Cross and Circle A and B) and the touch zones do nothing; in play each
action is its button's or a zone's */
void vita_controls_map(const struct vita_controls_config *config, struct vita_controls_state *state,
	unsigned long buttons, unsigned long touch, int menus, struct vita_controls_output *output);

#endif
