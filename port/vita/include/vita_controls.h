/*
VITA_CONTROLS.H

The mapping layer between the Vita's controls and the Xbox controller the
game reads (port/vita/host/vita_controls.c): the touch zones on the front
screen and the rear pad, and what the settings panel's Controls tab sets
(the Xbox button each zone presses, the Vita button of each Xbox button).
Nothing here touches the SDK, so the desktop test
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

/* ---------- the Xbox controller's buttons */

/* what a touch zone presses, and what each remappable row is: the Xbox
controller's buttons (Start stays on Start). A to the right trigger are in
XINPUT_GAMEPAD_A..RIGHT_TRIGGER's order, so VITA_XBOX_A + n is analog
button n */
enum
{
	VITA_XBOX_OFF,
	VITA_XBOX_A,
	VITA_XBOX_B,
	VITA_XBOX_X,
	VITA_XBOX_Y,
	VITA_XBOX_BLACK,
	VITA_XBOX_WHITE,
	VITA_XBOX_LEFT_TRIGGER,
	VITA_XBOX_RIGHT_TRIGGER,
	VITA_XBOX_LEFT_STICK,
	VITA_XBOX_RIGHT_STICK,
	VITA_XBOX_BACK,
	VITA_XBOX_COUNT
};

/* the Xbox buttons as settings.txt values and as the panel shows them (in
the enum's order; macros, for the panel's row initialisers) */
#define VITA_XBOX_VALUES "off", "a", "b", "x", "y", "black", "white", "lt", "rt", "ls", "rs", "back"
#define VITA_XBOX_NAMES "Off", "A", "B", "X", "Y", "Black", "White", "Left trigger", "Right trigger", \
	"Left stick", "Right stick", "Back"

/* (two letters each, for the panel's zone diagram) */
#define VITA_XBOX_SHORT_NAMES "", "A", "B", "X", "Y", "Bl", "Wh", "LT", "RT", "LS", "RS", "Bk"

/* the Xbox button a settings value names; VITA_XBOX_OFF if none */
int vita_xbox_named(const char *value);

/* the Vita buttons an Xbox button can be put on (in play), as settings.txt
values and as the panel shows them; "none" leaves it on no button */
#define VITA_BUTTON_CHOICES 12
#define VITA_BUTTON_VALUES "cross", "circle", "square", "triangle", "l", "r", "up", "down", "left", "right", \
	"select", "none"
#define VITA_BUTTON_NAMES "Cross", "Circle", "Square", "Triangle", "L", "R", "D-pad up", "D-pad down", \
	"D-pad left", "D-pad right", "Select", "None"

/* the Vita button (a VITA_BUTTON_* bit, 0 for none) a settings value names,
or `fallback` if it names none */
unsigned long vita_button_named(const char *value, unsigned long fallback);

/* the settings variable of each zone's Xbox button, and of each Xbox
button's Vita button (NULL for Off) */
extern const char *const vita_touch_variables[VITA_ZONE_COUNT];
extern const char *const vita_xbox_variables[VITA_XBOX_COUNT];
/* each Xbox button's Vita button as shipped (the README's layout) */
extern const unsigned long vita_xbox_defaults[VITA_XBOX_COUNT];

/* ---------- the mapping */

struct vita_controls_config
{
	/* each zone's Xbox button, and each Xbox button's Vita button */
	int zone_xbox[VITA_ZONE_COUNT];
	unsigned long xbox_button[VITA_XBOX_COUNT];
	/* HALO_CROUCH_TOGGLE */
	int crouch_toggle;
};

/* the zones' Xbox buttons and the Xbox buttons' Vita buttons from the
environment (the panel's settings), defaults for what is unset */
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
Cross and Circle A and B) and the touch zones do nothing; in play each Xbox
button is its Vita button's or a zone's (the left stick's click through the
crouch toggle) */
void vita_controls_map(const struct vita_controls_config *config, struct vita_controls_state *state,
	unsigned long buttons, unsigned long touch, int menus, struct vita_controls_output *output);

#endif
