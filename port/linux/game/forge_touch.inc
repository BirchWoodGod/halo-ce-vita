/*
FORGE_TOUCH.INC

The Vita's front screen in Forge's monitor (included by forge.c; on by
default, the panel's Touch in Forge, HALO_FORGE_TOUCH: vita_pad.c). The
fingers' gestures (forge_touch.c) do what the pad does, by the pad's own
calls, so a network game's client asks the host, undo keeps it, and nothing
new goes on the wire:
- drag an object (or a spawn marker): it is grabbed (forge_grab, as R) and
  stays under the finger at its distance, the camera free to move; lifted,
  it is dropped (forge_drop, as R). A second finger dragged meanwhile turns
  it;
- tap an object: it is selected, its name on the HUD; a tap on nothing, or
  on another object, moves on. A drag that starts on no object turns the
  object selected: grabbed where it is, turned, dropped when lifted. The
  HUD's label says what is selected;
- holding an object with R: a drag turns it, as L + the right stick (across
  yaws, up and down pitch), in the steps of rotate mode's snapping. Roll
  stays on the pad.
The rear pad is not used: the hands holding the Vita rest there
(vita_controls.c's Rear touch guard), and the front screen does it all. The
front screen's touch zones are off while the monitor flies (vita_pad.c), and
a finger that comes down in one counts for nothing here (the HUD's corners,
and where a thumb holding the Vita can land).

The finger's ray: forge_camera_get gives it as the camera's forward while
forge_touch_aim is on, so that the crosshair's own pick, the hold and a
marker's hold need no second version. The view's tangents are the whole
picture's: main.c set_window_camera_values's vertical field of view is that
of the window, 0.85 of the viewport (compute_window_bounds), so across the
viewport they come to 0.75 * tan(field of view / 2) up and down, wider by
the Hor+ columns of a wide screen, the Field of view setting's scale on both
(run_forge_test.sh touch checks a held object against
render_camera_world_to_screen, at 640 and the Vita's 848 wide). */

#define FORGE_TOUCH_TURN (_pi / 320.f)
/* a drag this far is a step, with rotate mode's snapping on */
#define FORGE_TOUCH_TURN_STEP 30.f

enum
{
	_forge_touch_none,
	/* in the finger's hand */
	_forge_touch_carry,
	/* the object selected, grabbed where it is and turned */
	_forge_touch_turn_selected,
	/* held by R: the finger only turns it */
	_forge_touch_turn_held,
	/* lifted before the host's answer: dropped when it comes */
	_forge_touch_release
};

real field_of_view_first_person_scale(short local_player_index);
long halo_linux_forge_touch(struct forge_touch_finger *fingers, long maximum);

static boolean forge_touch_tangents(
	real *horizontal,
	real *vertical)
{
	struct observer_result const *camera = observer_get_camera(forge_globals.local_player_index);

	if (!camera)
		return FALSE;
	*vertical = 0.75f * tangent(camera->field_of_view * 0.5f) *
		field_of_view_first_person_scale(forge_globals.local_player_index);
	*horizontal = *vertical * (real)halo_screen_width() / 480.f;
	return TRUE;
}

/* the camera's line through a place on the screen, as tangents across the
view's middle */
static boolean forge_touch_view_place(
	short x,
	short y,
	real *across,
	real *up)
{
	real horizontal, vertical;

	if (!forge_touch_tangents(&horizontal, &vertical))
		return FALSE;
	*across = (real)(x - 320) / ((real)halo_screen_width() * 0.5f) * horizontal;
	*up = (real)(240 - y) / 240.f * vertical;
	return TRUE;
}

/* (forge_camera_get) the forward that place on the view looks along */
static void forge_touch_ray(
	real across,
	real up,
	real_vector3d *forward)
{
	struct observer_result const *camera = observer_get_camera(forge_globals.local_player_index);
	real_vector3d right;

	if (!camera)
		return;
	cross_product3d(&camera->forward, &camera->up, &right);
	forward->i = camera->forward.i + right.i * across + camera->up.i * up;
	forward->j = camera->forward.j + right.j * across + camera->up.j * up;
	forward->k = camera->forward.k + right.k * across + camera->up.k * up;
	normalize3d(forward);
}

/* forge_camera_get looks along the place, or the camera's own way again */
static void forge_touch_aim(
	boolean on,
	real across,
	real up)
{
	forge_globals.touch_aiming = on;
	forge_globals.touch_aim_across = across;
	forge_globals.touch_aim_up = up;
}

/* what is held now: where it is, seen from the camera, the hold's place and
distance: it stays where it is until the finger moves */
static void forge_touch_hold_bind(
	void)
{
	struct observer_result const *camera = observer_get_camera(forge_globals.local_player_index);
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);
	real_point3d point;
	real_vector3d to_point, right;
	real along;

	if (!camera)
		return;
	if (object)
		point = object->object.position;
	else if (forge_globals.held_marker_id)
		point = forge_globals.held_marker_position;
	else
		return;
	to_point.i = point.x - camera->position.x;
	to_point.j = point.y - camera->position.y;
	to_point.k = point.z - camera->position.z;
	along = dot_product3d(&to_point, &camera->forward);
	if (along > 0.1f)
	{
		cross_product3d(&camera->forward, &camera->up, &right);
		forge_globals.touch_hold_across = dot_product3d(&to_point, &right) / along;
		forge_globals.touch_hold_up = dot_product3d(&to_point, &camera->up) / along;
	}
	forge_globals.held_distance = PIN(square_root(dot_product3d(&to_point, &to_point)), FORGE_MINIMUM_HOLD_DISTANCE,
		FORGE_MAXIMUM_HOLD_DISTANCE);
	forge_globals.touch_waiting = FALSE;
}

/* the object held turned by a finger's move, in rotate mode's snapping */
static void forge_touch_turn(
	real across,
	real down)
{
	real step = DEGREES_TO_RADIANS((real)forge_rotate_snaps[forge_globals.rotate_snap]);
	real moves[2];
	short axes[2] = { _forge_axis_yaw, _forge_axis_pitch };
	short index;

	if (!object_try_and_get(forge_globals.held_object_index))
		return;
	/* (the side facing the camera goes the finger's way: right, against
	the right stick's turn; up, as the stick's) */
	moves[0] = across;
	moves[1] = -down;
	for (index = 0; index < 2; index++)
	{
		if (step <= 0.f)
		{
			forge_rotate_held(axes[index], moves[index] * FORGE_TOUCH_TURN);
			continue;
		}
		forge_globals.touch_turn[index] += moves[index];
		while (forge_globals.touch_turn[index] >= FORGE_TOUCH_TURN_STEP)
		{
			forge_globals.touch_turn[index] -= FORGE_TOUCH_TURN_STEP;
			forge_rotate_held(axes[index], step);
		}
		while (forge_globals.touch_turn[index] <= -FORGE_TOUCH_TURN_STEP)
		{
			forge_globals.touch_turn[index] += FORGE_TOUCH_TURN_STEP;
			forge_rotate_held(axes[index], -step);
		}
	}
	if (across != 0.f || down != 0.f)
		forge_globals.touch_turning = TRUE;
}

static void forge_touch_end(
	void)
{
	forge_globals.touch_mode = _forge_touch_none;
	forge_globals.touch_waiting = FALSE;
	forge_globals.touch_turn[0] = forge_globals.touch_turn[1] = 0.f;
}

/* (leaving the monitor, a new session) nothing of the fingers' left */
static void forge_touch_reset(
	void)
{
	forge_touch_end();
	forge_touch_aim(FALSE, 0.f, 0.f);
	forge_globals.touch_selected = NONE;
	forge_globals.test_touch_count = 0;
	csmemset(&forge_globals.touch_gesture, 0, sizeof(forge_globals.touch_gesture));
}

/* what a finger holds let go, the pad's way */
static void forge_touch_let_go(
	void)
{
	if (forge_globals.held_marker_id)
		forge_marker_drop();
	else if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
}

/* the object the finger is on, NONE if none; *marker the spawn marker it
is on instead (NONE if none), by the rule R has: a marker before what stands
on it, never one behind a wall */
static long forge_touch_pick(
	short x,
	short y,
	long *marker)
{
	real across, up;
	real object_distance = FORGE_PICK_RANGE;
	real marker_distance = FORGE_PICK_RANGE;
	long object_index;
	long marker_id;

	*marker = NONE;
	if (!forge_touch_view_place(x, y, &across, &up))
		return NONE;
	forge_touch_aim(TRUE, across, up);
	object_index = forge_object_under_crosshair_at(&object_distance);
	marker_id = forge_marker_under_crosshair(&marker_distance);
	forge_touch_aim(FALSE, 0.f, 0.f);
	if (marker_id != NONE && marker_distance <= object_distance + FORGE_MARKER_PICK_RADIUS)
	{
		*marker = marker_id;
		return NONE;
	}
	return object_index;
}

static void forge_touch_drag_begin(
	short x,
	short y)
{
	long marker;
	long object_index;

	/* (R's: the finger turns it) */
	if (object_try_and_get(forge_globals.held_object_index))
	{
		forge_globals.touch_mode = _forge_touch_turn_held;
		return;
	}
	if (forge_globals.held_marker_id)
		return;
	object_index = forge_touch_pick(x, y, &marker);
	forge_globals.touch_hold_across = forge_globals.touch_hold_up = 0.f;
	forge_touch_view_place(x, y, &forge_globals.touch_hold_across, &forge_globals.touch_hold_up);
	if (marker != NONE)
	{
		real marker_distance = FORGE_PICK_RANGE;

		forge_touch_aim(TRUE, forge_globals.touch_hold_across, forge_globals.touch_hold_up);
		forge_marker_under_crosshair(&marker_distance);
		forge_marker_grab(marker, marker_distance);
		forge_touch_aim(FALSE, 0.f, 0.f);
		forge_globals.touch_mode = _forge_touch_carry;
		forge_globals.touch_waiting = TRUE;
		platform_log("forge touch: dragging a marker");
	}
	else if (object_index != NONE)
	{
		forge_globals.touch_selected = object_index;
		forge_grab(object_index);
		forge_globals.touch_mode = _forge_touch_carry;
		forge_globals.touch_waiting = TRUE;
		platform_log("forge touch: dragging an object");
	}
	else if (object_try_and_get(forge_globals.touch_selected))
	{
		forge_grab(forge_globals.touch_selected);
		forge_globals.touch_mode = _forge_touch_turn_selected;
		forge_globals.touch_waiting = TRUE;
		platform_log("forge touch: turning the object selected");
	}
}

/* (forge_update, each frame of the monitor) a menu open has the fingers:
what a finger carries waits with it (its properties may be what is open),
and is let go after, the finger being gone */
static void forge_touch_update(
	boolean menu_open)
{
	struct forge_touch_finger fingers[FORGE_TOUCH_MAXIMUM_FINGERS];
	struct forge_touch_events events;
	long count;
	boolean holding;

	forge_globals.touch_turning = FALSE;
	if (menu_open)
		return;
	count = halo_linux_forge_touch(fingers, FORGE_TOUCH_MAXIMUM_FINGERS);
	if (forge_globals.test_touch_count > 0)
	{
		short index;

		count = MIN(forge_globals.test_touch_count, FORGE_TOUCH_MAXIMUM_FINGERS);
		for (index = 0; index < count; index++)
			fingers[index] = forge_globals.test_touch[index];
	}
	forge_touch_gesture_update(&forge_globals.touch_gesture, fingers, (int)count, system_milliseconds(), &events);
	if (events.drag_began && forge_globals.touch_mode == _forge_touch_none)
		forge_touch_drag_begin(events.began_x, events.began_y);
	holding = object_try_and_get(forge_globals.held_object_index) != NULL || forge_globals.held_marker_id != 0;
	if (forge_globals.touch_mode != _forge_touch_none)
	{
		if (forge_globals.touch_mode != _forge_touch_turn_held && forge_globals.touch_waiting)
		{
			/* (a network game's client: the host's answer; refused, or not
			a client's, nothing) */
			if (holding)
				forge_touch_hold_bind();
			else if (!forge_net_grab_waiting())
				forge_touch_end();
		}
		else if (!holding)
		{
			/* (dropped by the pad, deleted, taken back by the host) */
			forge_touch_end();
		}
		if (forge_globals.touch_mode == _forge_touch_release && holding && !forge_globals.touch_waiting)
		{
			forge_touch_let_go();
			forge_touch_end();
		}
	}
	if (events.tap && !holding)
	{
		long marker;

		forge_globals.touch_selected = forge_touch_pick(events.tap_x, events.tap_y, &marker);
		if (forge_globals.touch_selected != NONE)
		{
			char name[FORGE_NAME_SIZE];

			forge_object_name(forge_globals.touch_selected, name, sizeof(name));
			platform_log("forge touch: selected %s", name);
		}
		else
			platform_log("forge touch: selected nothing");
	}
	if (events.dragging && !forge_globals.touch_waiting)
	{
		if (forge_globals.touch_mode == _forge_touch_carry)
		{
			real horizontal, vertical;

			if (forge_touch_tangents(&horizontal, &vertical))
			{
				forge_globals.touch_hold_across += (real)events.move_x / ((real)halo_screen_width() * 0.5f) * horizontal;
				forge_globals.touch_hold_up -= (real)events.move_y / 240.f * vertical;
			}
			forge_touch_turn((real)events.second_x, (real)events.second_y);
		}
		else if (forge_globals.touch_mode == _forge_touch_turn_selected ||
			forge_globals.touch_mode == _forge_touch_turn_held)
		{
			forge_touch_turn((real)events.move_x, (real)events.move_y);
		}
	}
	if (events.drag_ended)
	{
		if (forge_globals.touch_mode == _forge_touch_carry || forge_globals.touch_mode == _forge_touch_turn_selected)
		{
			if (forge_globals.touch_waiting)
			{
				forge_globals.touch_mode = _forge_touch_release;
			}
			else
			{
				forge_touch_let_go();
				forge_touch_end();
			}
		}
		else if (forge_globals.touch_mode == _forge_touch_turn_held)
		{
			forge_touch_end();
		}
	}
	/* (the selection gone with its object, or moved on by the pad) */
	if (forge_globals.touch_selected != NONE && !object_try_and_get(forge_globals.touch_selected))
		forge_globals.touch_selected = NONE;
}

/* (forge_update) the hold's line this frame: the finger's place while it
carries or turns what it grabbed, the camera's own else */
static void forge_touch_hold_aim(
	boolean on)
{
	boolean fingers = forge_globals.touch_mode == _forge_touch_carry ||
		forge_globals.touch_mode == _forge_touch_turn_selected || forge_globals.touch_mode == _forge_touch_release;

	forge_touch_aim(on && fingers && !forge_globals.touch_waiting, forge_globals.touch_hold_across,
		forge_globals.touch_hold_up);
}

/* (forge_draw, after the console's forge touch probe) where the renderer
shows the object held, in the menus' 640x480: the test's check of the
finger's line against the view as it is drawn */
static void forge_touch_probe(
	void)
{
	struct object_datum *held = object_try_and_get(forge_globals.held_object_index);
	real_point2d screen;

	if (!forge_globals.test_touch_probe)
		return;
	forge_globals.test_touch_probe = FALSE;
	if (held && render_camera_world_to_screen(&render.camera, &render.frustum, &held->object.position, &screen))
		platform_log("forge touch: held object on screen at %d,%d", (int)screen.x - (int)render.camera.viewport_bounds.x0 -
			(int)(halo_screen_width() - 640) / 2, (int)screen.y - (int)render.camera.viewport_bounds.y0);
}
