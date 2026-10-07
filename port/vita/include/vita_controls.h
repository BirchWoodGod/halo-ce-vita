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

/* a touch on the rear pad counts once held its guard's hold time (a brush
of the fingers holding the Vita does not); a front one at once. This is
the hold of the guard Off (as before the guard) */
#define VITA_TOUCH_REAR_HOLD_US 100000ULL

/* the rear pad's guard (the panel's Rear touch guard,
HALO_TOUCH_REAR_GUARD): the hands holding the Vita rest on the pad's
border, so a finger that comes down within `edge` pixels of it never counts
(not even slid inwards), and one further in counts once held hold_ms. Off
is the pad as before the guard: no border, 0.1 s */
enum
{
	VITA_REAR_GUARD_OFF,
	VITA_REAR_GUARD_LIGHT,
	VITA_REAR_GUARD_NORMAL,
	VITA_REAR_GUARD_STRONG,
	VITA_REAR_GUARD_COUNT
};

#define VITA_REAR_GUARD_DEFAULT VITA_REAR_GUARD_NORMAL
/* (settings.txt's values and the panel's names, in the enum's order) */
#define VITA_REAR_GUARD_VALUES "off", "light", "normal", "strong"
#define VITA_REAR_GUARD_NAMES "Off", "Light", "Normal", "Strong"

struct vita_rear_guard
{
	/* the border, in the screen's pixels (VITA_TOUCH_WIDTH x HEIGHT), on
	all four sides; the hold, in milliseconds */
	int edge;
	int hold_ms;
};

extern const struct vita_rear_guard vita_rear_guards[VITA_REAR_GUARD_COUNT];

/* the guard a settings value names; VITA_REAR_GUARD_DEFAULT if none */
int vita_rear_guard_named(const char *value);

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
outside every zone or in the rear pad's guarded border, so it never
counts) and since when; and the rear pad's guard (a VITA_REAR_GUARD_*, set
by the caller: zeroed, Off) */
struct vita_touch_tracker
{
	struct
	{
		int used;
		int panel, id, zone;
		unsigned long long since;
	} fingers[VITA_TOUCH_TRACKED];
	int rear_guard;
};

/* the zones held this frame (a bit per zone): each finger counts for the
zone it started in, while it stays down, once held long enough for its
panel (the rear: its guard's hold, and never one that started in the
guard's border); several fingers each count on their own. *started (if
given) is how many fingers came down this frame */
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

/* the Xbox buttons by what they do in Halo's default controller layout, as
the panel names them with the Button icons PlayStation (below), and in two
letters */
#define VITA_XBOX_ACTION_NAMES "Off", "Jump", "Melee", "Action", "Switch weapon", "Switch grenades", "Flashlight", \
	"Throw grenade", "Fire", "Crouch", "Zoom", "Scoreboard"
#define VITA_XBOX_ACTION_SHORT_NAMES "", "Jp", "Me", "Ac", "Sw", "Sg", "Fl", "Gr", "Fi", "Cr", "Zm", "Sc"

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

/* ---------- button icons

The panel's Button icons (HALO_BUTTON_ICONS, Controls > Button layout):
Xbox, the default, leaves the game's button icons as they are; PlayStation
shows in their place the Vita button each Xbox button is on now, the
mapping above's (in the menus the fixed layout, in play the Button layout
and the touch zones), drawn by the game's HUD (source/interface/hud_draw.c)
from these */
enum
{
	/* on no Vita button (Black in the menus, a button set to None and no
	zone): the Xbox's icon stays */
	VITA_GLYPH_NONE,
	VITA_GLYPH_CROSS,
	VITA_GLYPH_CIRCLE,
	VITA_GLYPH_SQUARE,
	VITA_GLYPH_TRIANGLE,
	VITA_GLYPH_L,
	VITA_GLYPH_R,
	VITA_GLYPH_UP,
	VITA_GLYPH_DOWN,
	VITA_GLYPH_LEFT,
	VITA_GLYPH_RIGHT,
	VITA_GLYPH_START,
	VITA_GLYPH_SELECT,
	/* the touch zones, in VITA_ZONE_*'s order */
	VITA_GLYPH_TOUCH_TOP_LEFT,
	VITA_GLYPH_TOUCH_TOP_RIGHT,
	VITA_GLYPH_TOUCH_LEFT_EDGE,
	VITA_GLYPH_TOUCH_RIGHT_EDGE,
	VITA_GLYPH_TOUCH_REAR_LEFT,
	VITA_GLYPH_TOUCH_REAR_RIGHT,
	VITA_GLYPH_COUNT
};

#define VITA_BUTTON_ICONS_VALUES "xbox", "playstation"
#define VITA_BUTTON_ICONS_NAMES "Xbox", "PlayStation"

/* whether HALO_BUTTON_ICONS asks for PlayStation's (Xbox if unset) */
int vita_button_icons_playstation(void);

/* the glyph of the Vita button an Xbox controller button is on: the
button as the game numbers it (A B X Y Black White, the left and right
triggers, the D-pad's up down left right, Start, Back, the left and right
sticks' clicks: 0 to 15), in the menus or in play. A button on a Vita
button is that button, else the first touch zone set to it, else none (in
play the D-pad is none: the game reads it only in the menus) */
int vita_button_glyph(const struct vita_controls_config *config, int gamepad_button, int menus);

/* ---------- gyro aiming

The Vita's gyroscope (sceMotion, vita_input.c) turns the view as the Vita
turns. Its axes, the Vita held in landscape facing the player: x along the
screen's long edge (to the right), y along the short edge (up), z out of
the screen (towards the player); angular velocity in radians a second,
positive counter-clockwise looking down the axis. So, held upright:
turning the Vita left (yaw) is +y, tilting its top edge towards the player
(the view looking up, as if seen through the Vita) is +x, turning it like a
steering wheel to the left (roll) is +z.

Each sample goes through vita_gyro_filter_sample (the bias learnt while the
Vita lies still, smoothing of small motion, a deadzone) into an angle;
vita_gyro_accumulate turns the angles of a frame into the game's yaw (left
positive) and pitch (up positive), which vita_gyro_take hands to the game's
look code once a frame (port/linux/src/xinput_sdl.c halo_linux_mouse_look:
added to the facing change directly, like mouse aim, so no stick
acceleration, no clipping at full deflection). */

enum
{
	VITA_GYRO_OFF,
	VITA_GYRO_ON,
	/* only while the weapon is zoomed */
	VITA_GYRO_ZOOMED,
	/* only while the gyro button is held */
	VITA_GYRO_HOLD,
	VITA_GYRO_MODES
};

enum
{
	VITA_GYRO_TURN_YAW,
	VITA_GYRO_TURN_ROLL,
};

/* the settings (the panel's Controls tab and Gyro settings page): HALO_GYRO off/on/zoomed/hold,
HALO_GYRO_BUTTON (a VITA_BUTTON_VALUES name; default l), HALO_GYRO_SENS
(percent, 1:1 at 100; default 150), HALO_GYRO_INVERT_Y 0/1, HALO_GYRO_TURN
yaw/roll */
#define VITA_GYRO_DEFAULT_SENS 150

struct vita_gyro_config
{
	int mode;
	unsigned long button;
	/* view radians per radian the Vita turns */
	float sensitivity;
	int invert_y;
	int turn;
};

void vita_gyro_config_load(struct vita_gyro_config *config);

/* rates below this (radians a second, after the bias: 0.75 degrees) are
taken as none, and above it reduced by it */
#define VITA_GYRO_DEADZONE 0.01309f
/* small motion is smoothed (hand tremor): fully below the first rate, not
at all above the second (radians a second: 5 and 15 degrees), with this
time constant (seconds) */
#define VITA_GYRO_SMOOTH_LOW 0.08727f
#define VITA_GYRO_SMOOTH_HIGH 0.26180f
#define VITA_GYRO_SMOOTH_TIME 0.040f
/* still (the bias learnt): a second in which no axis' rate moves more than
2 degrees a second (from lowest to highest), the accelerometer no more than
0.03 g, and the mean rate is under 6 degrees a second */
#define VITA_GYRO_STILL_TIME 1.0f
#define VITA_GYRO_STILL_SPREAD 0.03491f
#define VITA_GYRO_STILL_ACCEL 0.03f
#define VITA_GYRO_STILL_MEAN 0.10472f
/* a gap longer than this (seconds) between two samples integrates nothing */
#define VITA_GYRO_MAXIMUM_GAP 0.05f

struct vita_gyro_filter
{
	/* the bias (radians a second per axis), and how many still seconds
	it was learnt from */
	float bias[3];
	int still_count;
	/* the current still window */
	float window_time;
	float window_sum[3];
	float window_low[3], window_high[3];
	float accel_low[3], accel_high[3];
	/* the smoothed rate (after the bias) and the last rate given out (after
	the deadzone), for the panel's line */
	float smoothed[3];
	float rate[3];
};

/* one sample: rate (radians a second, the Vita's axes), accel (g; NULL for
none), dt (seconds since the previous sample); calibrate 0 for a sample
that is not the sensor's (the pad script's): it never teaches the bias.
Adds the angle turned (radians per axis) to angle[] */
void vita_gyro_filter_sample(struct vita_gyro_filter *filter, const float rate[3], const float accel[3], float dt,
	int calibrate, float angle[3]);

/* whether the gyro aims now, before the zoom (vita_gyro_take): not in the
menus, and in hold mode only while the button is held */
int vita_gyro_active(const struct vita_gyro_config *config, unsigned long buttons, int menus);

/* the yaw and pitch (the game's radians) for the angles the Vita turned */
void vita_gyro_look(const struct vita_gyro_config *config, const float angle[3], float *yaw, float *pitch);

/* what has turned since the game last took it */
struct vita_gyro_state
{
	float yaw, pitch;
	int active;
	/* pad reads since the game last took it: more than a few (a
	cinematic, the game paused) and what turned meanwhile is dropped */
	int unconsumed;
};

#define VITA_GYRO_MAXIMUM_UNCONSUMED 4

/* a pad read's angles; active is vita_gyro_active's answer. Inactive (or
just becoming active) drops what was waiting: the aim starts afresh */
void vita_gyro_accumulate(struct vita_gyro_state *state, const struct vita_gyro_config *config,
	const float angle[3], int active);

/* the yaw and pitch since the last call, zero and 0 if none (and dropped
in zoomed mode while not zoomed) */
int vita_gyro_take(struct vita_gyro_state *state, const struct vita_gyro_config *config, int zoomed, float *yaw,
	float *pitch);

#endif
