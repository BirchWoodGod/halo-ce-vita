/*
FIELD_OF_VIEW.C

The player's field of view (display.field_of_view, HALO_FIELD_OF_VIEW; the
Vita's settings panel: Video > Screen > Field of view), port only, local
only: nothing here is sent over the network or changes the game's state.

The number is the game's own field of view, in Halo's degrees: the Xbox's 70
(a unit's camera field of view, the biped's) is the stock view, and a larger
one widens it as Halo PC's does. The game turns its field of view into the
view's vertical half-angle as main.c set_window_camera_values does:

    tan(vertical / 2) = 0.75 * 0.85 * tan(field_of_view / 2)

so at 4:3 the view's horizontal tangent is 0.85 * tan(field_of_view / 2) and
the setting scales that tangent by tan(setting / 2) / tan(70 / 2). At 16:9
the picture is wider by the side strips the port adds (Hor+: 848 columns of
the same 480 lines), as at 70, so 16:9 at 70 is exactly the view as before.

Only the windows of a local player in first or third person are widened
(not cinematics, the scripted camera, the editor's or the menus'). A split
screen's views are each their player's own (each its own zoom).

Zoom: a weapon's zoomed field of view is the unit's divided by the zoom's
magnification (weapons.c weapon_get_field_of_view). Zoomed views stay the
stock ones, whatever the setting: the widening fades out as the view closes
in on the weapon's first zoom level (during the zoom's 0.18 s transition) and
is none at or past it, so a scope shows exactly what it showed.

The first-person weapon is drawn with its own depth range already
(rasterizer_set_frustum_z with the first-person clip distances); while it is,
the projection's x and y scales are put back to the stock field of view
(field_of_view_first_person_scale), so the gun keeps its size and shape at
any field of view.

The HUD's nav points and waypoints are placed through the drawn frustum
(render_camera_world_to_screen) and the crosshair is the view's centre, so
both follow; aim assist works in angles from the unit's aim, not the screen.
*/

#include "cseries.h"
#include "real_math.h"
#include "camera/director.h"
#include "game/player_control.h"
#include "items/weapons.h"
#include "units/units.h"
#include "units/unit_definitions.h"

#include <math.h>
#include <stdlib.h>

/* port/linux/src/port_config.c */
long config_integer(const char *name);
extern volatile unsigned long halo_settings_generation;

enum
{
	FIELD_OF_VIEW_LOCAL_PLAYERS = 4,
	FIELD_OF_VIEW_STOCK = 70,
	FIELD_OF_VIEW_MINIMUM = 50,
	FIELD_OF_VIEW_MAXIMUM = 120,
};

/* the last widening each local player's window was drawn with (the
first-person weapon's projection undoes it) */
static real field_of_view_scales[FIELD_OF_VIEW_LOCAL_PLAYERS] = { 1.0f, 1.0f, 1.0f, 1.0f };

/* the setting, in Halo's degrees (70: the stock view) */
long field_of_view_setting(
	void)
{
	static long degrees = -1;
	static unsigned long settings_seen;

	/* (the environment first, as first_person_legs_enabled: the Vita's
	settings panel sets the variable, live) */
	if (degrees < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_FIELD_OF_VIEW");
		long value;

		settings_seen = halo_settings_generation;
		value = setting && *setting ? strtol(setting, NULL, 10) : config_integer("display.field_of_view");
		degrees = value >= FIELD_OF_VIEW_MINIMUM && value <= FIELD_OF_VIEW_MAXIMUM ? value : FIELD_OF_VIEW_STOCK;
	}
	return degrees;
}

/* how much the setting widens the view's tangent (1: the stock view) */
static real field_of_view_setting_scale(
	void)
{
	long degrees = field_of_view_setting();

	if (degrees == FIELD_OF_VIEW_STOCK)
		return 1.0f;
	return (real)(tan(degrees * 0.5 * M_PI / 180.0) / tan(FIELD_OF_VIEW_STOCK * 0.5 * M_PI / 180.0));
}

/* the local player's unzoomed field of view and its weapon's first zoom
level's (0: the weapon does not zoom, or there is no unit) */
static real field_of_view_unzoomed(
	short local_player_index,
	real *first_zoom)
{
	long unit_index = player_control_get_unit_index(local_player_index);
	real unzoomed = DEGREES_TO_RADIANS((real)FIELD_OF_VIEW_STOCK);

	*first_zoom = 0.0f;
	if (unit_index != NONE)
	{
		struct unit_datum *unit = unit_get(unit_index);
		struct unit_definition *definition = unit_definition_get(unit->definition_index);
		long weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);

		unzoomed = definition->unit.camera_field_of_view;
		if (weapon_index != NONE)
		{
			real zoomed = weapon_get_field_of_view(weapon_index, unzoomed, 0);

			if (zoomed < unzoomed)
				*first_zoom = zoomed;
		}
	}
	return unzoomed;
}

/* the window's view tangent's widening for this local player's camera
(observer_field_of_view: the observer's, the game's own), remembered for its
first-person weapon; 1 for a window that is not a player's first or third
person view */
real field_of_view_world_scale(
	short local_player_index,
	real observer_field_of_view)
{
	real scale = 1.0f;

	if (local_player_index >= 0 && local_player_index < FIELD_OF_VIEW_LOCAL_PLAYERS)
	{
		director_perspective perspective = director_get_perspective(local_player_index);

		scale = field_of_view_setting_scale();
		if (scale != 1.0f &&
			perspective != _director_perspective_first_person &&
			perspective != _director_perspective_third_person)
			scale = 1.0f;
		if (scale != 1.0f)
		{
			real first_zoom;
			real unzoomed = field_of_view_unzoomed(local_player_index, &first_zoom);

			/* (zoomed: the stock view; on the way, between) */
			if (first_zoom > 0.0f && unzoomed > first_zoom)
			{
				real t = (observer_field_of_view - first_zoom) / (unzoomed - first_zoom);

				t = PIN(t, 0.0f, 1.0f);
				scale = 1.0f + (scale - 1.0f) * t;
			}
		}
		field_of_view_scales[local_player_index] = scale;
	}
	return scale;
}

/* the widening the local player's window is drawn with (1: none) */
real field_of_view_first_person_scale(
	short local_player_index)
{
	if (local_player_index >= 0 && local_player_index < FIELD_OF_VIEW_LOCAL_PLAYERS)
		return field_of_view_scales[local_player_index];
	return 1.0f;
}
