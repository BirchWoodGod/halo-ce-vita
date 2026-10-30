/*
FORGE_PRECISION.INC

Forge's precision tools (included by forge.c, after forge_physics.inc, for
its statics), 1.1.2:

- NUDGE: L held while holding an object is rotate mode; L + D-pad up (the
  right stick's click; the console's "forge nudgemode") flips it to nudge
  mode and back. In nudge mode a flick of the left stick moves the object
  one step along the ground - up away from the camera, down toward it,
  across to its side -, a flick of the right stick up or down one step up
  or down (the mouse's look up and down too), and D-pad right (White, the
  keyboard's Q) steps the step's size: 5, 10, 25 or 1 cm (5 at first).
  Along the camera's own axes; with SNAP on, along the grid's (the world's
  axis nearest the camera's).
  The nudge is an offset kept while the object is held (taken again with
  the camera, the object keeps it) and where it is let go; a new hold starts
  with none. The HUD says the mode and its step.
- MAGNETS (on unless turned off: the settings panel's Forge page, the
  console's "forge magnets"): a building block held near another block of
  the layout, with SNAP off, has its faces put flush against that block's -
  side by side, stacked, or edges lined up - when one is within
  FORGE_MAGNET_RANGE (about 45 cm) of the other's, along each world axis
  (the blocks' extents as turned: forge_blocks.c forge_blocks_extents); a
  nudge after it moves it from there.
- POSITION: the properties window's readout of where the object or marker
  held is, in metres (a world unit is 3.048 m).

Network Forge needs nothing of its own: a joiner's held object moves where
its own placing puts it, nudged and drawn by the magnets, and its moves are
the requests the host checks as ever (forge_net_protocol.c: in the world,
every number finite).
*/

/* ---------- constants */

static char const *const forge_nudge_log_names[NUMBER_OF_FORGE_NUDGES] =
{
	"ahead", "back", "left", "right", "up", "down",
};

/* ---------- private code */

static real forge_nudge_step_world(
	void)
{
	short index = (short)PIN(forge_globals.nudge_step, 0, (short)NUMBEROF(forge_nudge_steps) - 1);

	return (real)forge_nudge_steps[index] * 0.01f / FORGE_METRES_PER_WORLD_UNIT;
}

/* the nudge's ground axes: the camera's ahead and left, along the ground
(snapping: the world's axes nearest them) */
static void forge_nudge_axes(
	real_vector3d *ahead,
	real_vector3d *left)
{
	real_point3d position;
	real_vector3d forward;

	if (!forge_camera_get(&position, &forward))
		forward = *global_forward3d;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	if (forge_globals.snap)
	{
		if (forge_absolute(forward.i) >= forge_absolute(forward.j))
		{
			forward.i = forward.i < 0.f ? -1.f : 1.f;
			forward.j = 0.f;
		}
		else
		{
			forward.j = forward.j < 0.f ? -1.f : 1.f;
			forward.i = 0.f;
		}
	}
	*ahead = forward;
	left->i = -forward.j;
	left->j = forward.i;
	left->k = 0.f;
}

/* the object held nudged one step (_forge_nudge_*) */
static void forge_nudge(
	short direction)
{
	real_vector3d ahead, left, along;
	real step = forge_nudge_step_world();

	if (!object_try_and_get(forge_globals.held_object_index) || direction < 0 || direction >= NUMBER_OF_FORGE_NUDGES)
		return;
	forge_nudge_axes(&ahead, &left);
	switch (direction)
	{
	case _forge_nudge_ahead: along = ahead; break;
	case _forge_nudge_back: along.i = -ahead.i; along.j = -ahead.j; along.k = 0.f; break;
	case _forge_nudge_left: along = left; break;
	case _forge_nudge_right: along.i = -left.i; along.j = -left.j; along.k = 0.f; break;
	case _forge_nudge_up: along = *global_up3d; break;
	default: along.i = 0.f; along.j = 0.f; along.k = -1.f; break;
	}
	forge_globals.held_nudge.i += along.i * step;
	forge_globals.held_nudge.j += along.j * step;
	forge_globals.held_nudge.k += along.k * step;
	platform_log("forge: nudged %s by %d cm (%.4f world units): the hold's offset %.4f %.4f %.4f",
		forge_nudge_log_names[direction], forge_nudge_steps[forge_globals.nudge_step], step, forge_globals.held_nudge.i,
		forge_globals.held_nudge.j, forge_globals.held_nudge.k);
}

/* (each frame of nudge mode: L held while holding an object, its mode
flipped) the sticks' flicks nudge it */
static void forge_nudge_update(
	struct forge_pad const *pad)
{
	real sticks[3];
	short axis;

	sticks[0] = pad->sticks[_gamepad_stick_left][1];
	sticks[1] = pad->sticks[_gamepad_stick_left][0];
	sticks[2] = pad->sticks[_gamepad_stick_right][1];
	for (axis = 0; axis < 3; axis++)
	{
		if (forge_globals.nudge_flicked[axis])
		{
			if (forge_absolute(sticks[axis]) < FORGE_ROTATE_REARM)
				forge_globals.nudge_flicked[axis] = FALSE;
			continue;
		}
		if (forge_absolute(sticks[axis]) > FORGE_ROTATE_FLICK)
		{
			forge_globals.nudge_flicked[axis] = TRUE;
			if (axis == 0)
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_ahead : _forge_nudge_back);
			else if (axis == 1)
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_right : _forge_nudge_left);
			else
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_up : _forge_nudge_down);
		}
	}
	/* (the mouse's look up and down: a step each FORGE_ROTATE_MOUSE_STEP) */
	forge_globals.rotate_mouse[_forge_axis_pitch] += forge_globals.pending_mouse_pitch;
	while (forge_globals.rotate_mouse[_forge_axis_pitch] >= FORGE_ROTATE_MOUSE_STEP)
	{
		forge_globals.rotate_mouse[_forge_axis_pitch] -= FORGE_ROTATE_MOUSE_STEP;
		forge_nudge(_forge_nudge_up);
	}
	while (forge_globals.rotate_mouse[_forge_axis_pitch] <= -FORGE_ROTATE_MOUSE_STEP)
	{
		forge_globals.rotate_mouse[_forge_axis_pitch] += FORGE_ROTATE_MOUSE_STEP;
		forge_nudge(_forge_nudge_down);
	}
}

/* the nudge step stepped (D-pad right in nudge mode), said */
static void forge_nudge_step_next(
	void)
{
	forge_globals.nudge_step = (short)((forge_globals.nudge_step + 1) % (short)NUMBEROF(forge_nudge_steps));
	forge_notice(T("Nudge: %d cm"), forge_nudge_steps[forge_globals.nudge_step]);
	platform_log("forge: nudge step %d cm", forge_nudge_steps[forge_globals.nudge_step]);
}

/* (forge_held_object_place) a block held drawn flush to the layout's
blocks near it: along each world axis, the nearest of its faces' offsets to
another block's faces (side by side, or the same side lined up) within
FORGE_MAGNET_RANGE, where the two overlap or nearly along the other axes */
static void forge_magnets_apply(
	long object_index,
	real_point3d *position,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	real_vector3d half;
	real best[3];
	long best_object[3];
	short index;
	short axis;

	if (forge_globals.magnets_off || forge_globals.snap || !forge_blocks_extents(object_index, forward, up, &half))
		return;
	for (axis = 0; axis < 3; axis++)
	{
		best[axis] = FORGE_MAGNET_RANGE;
		best_object[axis] = NONE;
	}
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct object_datum *other;
		real_vector3d other_half;
		short other_axis;
		boolean near = TRUE;

		if (placed->object_index == object_index || !forge_object_alive(placed->object_index))
			continue;
		other = object_get(placed->object_index);
		if (distance_squared3d(&other->object.position, position) > FORGE_MAGNET_REACH * FORGE_MAGNET_REACH ||
			!forge_blocks_extents(placed->object_index, &other->object.forward, &other->object.up, &other_half))
		{
			continue;
		}
		/* (the boxes near each other on every axis) */
		for (other_axis = 0; other_axis < 3; other_axis++)
		{
			if (forge_absolute(position->n[other_axis] - other->object.position.n[other_axis]) >
				half.n[other_axis] + other_half.n[other_axis] + FORGE_MAGNET_RANGE)
			{
				near = FALSE;
			}
		}
		if (!near)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			real ours[2], theirs[2];
			short a, b;

			ours[0] = position->n[axis] - half.n[axis];
			ours[1] = position->n[axis] + half.n[axis];
			theirs[0] = other->object.position.n[axis] - other_half.n[axis];
			theirs[1] = other->object.position.n[axis] + other_half.n[axis];
			for (a = 0; a < 2; a++)
			{
				for (b = 0; b < 2; b++)
				{
					real offset = theirs[b] - ours[a];

					if (forge_absolute(offset) < forge_absolute(best[axis]) ||
						(best_object[axis] == NONE && forge_absolute(offset) < FORGE_MAGNET_RANGE))
					{
						best[axis] = offset;
						best_object[axis] = placed->object_index;
					}
				}
			}
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		if (best_object[axis] != NONE)
			position->n[axis] += best[axis];
	}
	if (best_object[0] != NONE || best_object[1] != NONE || best_object[2] != NONE)
	{
		if (!forge_globals.magnet_said)
		{
			platform_log("forge: magnets: drawn %.3f %.3f %.3f to the blocks near it, at %.3f %.3f %.3f",
				best_object[0] != NONE ? best[0] : 0.f, best_object[1] != NONE ? best[1] : 0.f,
				best_object[2] != NONE ? best[2] : 0.f, position->x, position->y, position->z);
		}
		forge_globals.magnet_said = TRUE;
	}
	else
	{
		forge_globals.magnet_said = FALSE;
	}
}

/* ---------- public code (the settings panel's Forge page, vita_settings.c:
plain types) */

int forge_panel_magnets(
	void)
{
	return !forge_globals.magnets_off ? 1 : 0;
}

void forge_panel_magnets_set(
	int on)
{
	forge_globals.magnets_off = on == 0;
	platform_log("forge: magnets %s (settings panel)", !forge_globals.magnets_off ? "on" : "off");
}

int forge_panel_nudge_centimetres(
	void)
{
	return forge_nudge_steps[PIN(forge_globals.nudge_step, 0, (short)NUMBEROF(forge_nudge_steps) - 1)];
}

void forge_panel_nudge_step(
	int step)
{
	short count = (short)NUMBEROF(forge_nudge_steps);

	forge_globals.nudge_step = (short)((forge_globals.nudge_step + (step < 0 ? -1 : 1) + count) % count);
	platform_log("forge: nudge step %d cm (settings panel)", forge_nudge_steps[forge_globals.nudge_step]);
}
