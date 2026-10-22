/*
FORGE_MARKERS.INC

Forge's spawn markers (included by forge.c, after forge_net.inc, for its
statics): where a placed character is placed again - its placed spot, the
home a new round and its RESPAWN put it at - shown to the monitor as a small
ghosted marker in its AI team's colour, as Halo 3 shows its spawn points,
and taken, moved and let go like an object, or its character's properties
opened from it (Square while holding it). Moving it moves where that
character respawns (its home, saved in the map variant as its record's
place); the character itself stays where it is until it is placed again.

The marker is drawn (forge_marker_draw: the game's debug-geometry shaders, a
colour a vertex, blended), never an object: nothing of it is networked or
saved but the home it stands for, which the host's entries already carry to
every joiner. A joiner's move or property change from a marker is a request
(_forge_net_request_set or _forge_net_request_property with
FORGE_NET_FLAG_SPAWN and the character's id) the host checks and makes, to a
host whose hello says FORGE_NET_HOST_VERSION_SPAWNS; an older host is not
asked. Shown only while the player is the monitor in a Forge game: never in
a map variant played.

struct forge_marker and forge_marker_draw are every kind's (a character's
spawn now; the player's spawn points to come), the kind setting the shape's
size and its colour's source.
*/

/* ---------- constants */

/* the marker: its disc and pillar (world units); where the crosshair takes
it, forge.c's FORGE_MARKER_PICK_RADIUS */
#define FORGE_MARKER_DISC_RADIUS 0.22f
#define FORGE_MARKER_PILLAR_RADIUS 0.045f
#define FORGE_MARKER_HEIGHT 0.55f
#define FORGE_MARKER_SIDES 10
/* a marker let go stands this far over the ground under it */
#define FORGE_MARKER_CLEARANCE 0.25f
/* the most drawn a frame (a triangle budget: each is some 70) */
#define FORGE_MARKER_MOST_DRAWN 64
#define FORGE_MARKER_TRIANGLES 80

enum
{
	_forge_marker_ai_spawn = 0,
	/* (the player's spawn points: another branch's) */
	_forge_marker_player_spawn,
	NUMBER_OF_FORGE_MARKER_KINDS
};

/* ---------- structures */

struct forge_marker
{
	short kind;
	real_point3d position;
	real_vector3d forward;
	/* its colour */
	real red;
	real green;
	real blue;
	/* under the crosshair, or held: brighter */
	boolean lit;
};

/* ---------- private code */

/* a character's AI team's colour (FORGE_AI_TEAM_*: its own team's when
DEFAULT) */
static void forge_marker_team_color(
	struct forge_placed const *placed,
	struct forge_marker *marker)
{
	static real const colors[NUMBER_OF_FORGE_AI_TEAMS][3] =
	{
		{ 0.85f, 0.85f, 0.85f }, /* default: its own (below) */
		{ 0.30f, 0.60f, 1.00f }, /* the player's */
		{ 0.35f, 0.90f, 0.45f }, /* human */
		{ 0.75f, 0.40f, 1.00f }, /* covenant */
		{ 0.95f, 0.70f, 0.30f }, /* flood */
		{ 1.00f, 0.90f, 0.30f }, /* sentinel */
	};
	short team = (short)(placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS);

	/* (DEFAULT: the colour of the team the character fights for: its
	unit's, dead or alive) */
	if (team == FORGE_AI_TEAM_DEFAULT)
	{
		short engine_team = NONE;

		if (forge_object_alive(placed->object_index))
			engine_team = forge_ai_team_index(placed->ai_team, placed->object_index);
		else if (tag_get_group_tag(placed->tag_index) == ACTOR_VARIANT_DEFINITION_TAG &&
			actor_variant_definition_get(placed->tag_index)->unit_reference.index != NONE)
		{
			engine_team = unit_definition_get(actor_variant_definition_get(placed->tag_index)->unit_reference.index)->unit.default_team;
		}
		switch (engine_team)
		{
		case _game_team_player: team = FORGE_AI_TEAM_PLAYER; break;
		case _game_team_human: team = FORGE_AI_TEAM_HUMAN; break;
		case _game_team_covenant: team = FORGE_AI_TEAM_COVENANT; break;
		case _game_team_flood: team = FORGE_AI_TEAM_FLOOD; break;
		case _game_team_sentinel: team = FORGE_AI_TEAM_SENTINEL; break;
		}
	}
	marker->red = colors[team][0];
	marker->green = colors[team][1];
	marker->blue = colors[team][2];
}

static unsigned long forge_marker_pixel(
	struct forge_marker const *marker,
	real light,
	real alpha)
{
	real boost = marker->lit ? 1.35f : 1.f;
	unsigned long r = (unsigned long)(PIN(marker->red * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long g = (unsigned long)(PIN(marker->green * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long b = (unsigned long)(PIN(marker->blue * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long a = (unsigned long)(PIN(alpha, 0.f, 1.f) * 255.f + 0.5f);

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static void forge_marker_triangle(
	struct forge_net_orb_vertex *vertices,
	long *count,
	real_point3d const *a,
	real_point3d const *b,
	real_point3d const *c,
	unsigned long color)
{
	if (*count + 3 > FORGE_MARKER_TRIANGLES * 3)
		return;
	vertices[*count].point = *a;
	vertices[(*count)++].color = color;
	vertices[*count].point = *b;
	vertices[(*count)++].color = color;
	vertices[*count].point = *c;
	vertices[(*count)++].color = color;
}

/* ---------- public code (forge.c's) */

/* a marker drawn in the 3D (render.c's hook, after the objects): a disc on
the ground, an arrow on it where it faces, a pillar and a diamond over it;
blended, the walls in front hiding it */
static void forge_marker_draw(
	struct forge_marker const *marker)
{
	static struct forge_net_orb_vertex vertices[FORGE_MARKER_TRIANGLES * 3];
	real alpha = marker->lit ? 0.85f : 0.55f;
	real_vector3d forward = marker->forward;
	real_vector3d left;
	real_point3d center = marker->position;
	real_point3d top;
	long count = 0;
	long buffer;
	short side;

	forward.k = 0.f;
	if (normalize3d(&forward) == 0.f)
		forward = *global_forward3d;
	left.i = -forward.j;
	left.j = forward.i;
	left.k = 0.f;
	center.z += 0.02f;
	top = center;
	top.z += FORGE_MARKER_HEIGHT;
	for (side = 0; side < FORGE_MARKER_SIDES; side++)
	{
		real angle0 = (real)side * (real)(2.0 * 3.14159265358979) / (real)FORGE_MARKER_SIDES;
		real angle1 = (real)(side + 1) * (real)(2.0 * 3.14159265358979) / (real)FORGE_MARKER_SIDES;
		real_point3d disc0, disc1, low0, low1, high0, high1;

		disc0.x = center.x + cosf(angle0) * FORGE_MARKER_DISC_RADIUS;
		disc0.y = center.y + sinf(angle0) * FORGE_MARKER_DISC_RADIUS;
		disc0.z = center.z;
		disc1.x = center.x + cosf(angle1) * FORGE_MARKER_DISC_RADIUS;
		disc1.y = center.y + sinf(angle1) * FORGE_MARKER_DISC_RADIUS;
		disc1.z = center.z;
		/* (the disc) */
		forge_marker_triangle(vertices, &count, &center, &disc0, &disc1, forge_marker_pixel(marker, 0.7f, alpha * 0.8f));
		/* (the pillar) */
		low0.x = center.x + cosf(angle0) * FORGE_MARKER_PILLAR_RADIUS;
		low0.y = center.y + sinf(angle0) * FORGE_MARKER_PILLAR_RADIUS;
		low0.z = center.z;
		low1.x = center.x + cosf(angle1) * FORGE_MARKER_PILLAR_RADIUS;
		low1.y = center.y + sinf(angle1) * FORGE_MARKER_PILLAR_RADIUS;
		low1.z = center.z;
		high0 = low0;
		high0.z = top.z;
		high1 = low1;
		high1.z = top.z;
		forge_marker_triangle(vertices, &count, &low0, &low1, &high1,
			forge_marker_pixel(marker, 0.65f + 0.35f * (real)fabsf(cosf(angle0)), alpha));
		forge_marker_triangle(vertices, &count, &low0, &high1, &high0,
			forge_marker_pixel(marker, 0.65f + 0.35f * (real)fabsf(cosf(angle0)), alpha));
	}
	/* (the arrow on the disc: where the character faces) */
	{
		real_point3d tip, back_left, back_right;

		tip.x = center.x + forward.i * FORGE_MARKER_DISC_RADIUS * 1.7f;
		tip.y = center.y + forward.j * FORGE_MARKER_DISC_RADIUS * 1.7f;
		tip.z = center.z + 0.01f;
		back_left.x = center.x + left.i * FORGE_MARKER_DISC_RADIUS * 0.6f;
		back_left.y = center.y + left.j * FORGE_MARKER_DISC_RADIUS * 0.6f;
		back_left.z = tip.z;
		back_right.x = center.x - left.i * FORGE_MARKER_DISC_RADIUS * 0.6f;
		back_right.y = center.y - left.j * FORGE_MARKER_DISC_RADIUS * 0.6f;
		back_right.z = tip.z;
		forge_marker_triangle(vertices, &count, &tip, &back_left, &back_right, forge_marker_pixel(marker, 1.f, alpha));
	}
	/* (the diamond over it) */
	{
		real_point3d up_point = top, down_point = top, around[4];
		short corner;

		up_point.z += 0.12f;
		down_point.z -= 0.08f;
		for (corner = 0; corner < 4; corner++)
		{
			real angle = (real)corner * (real)(3.14159265358979 / 2.0);
			real_vector3d direction;

			direction.i = forward.i * cosf(angle) + left.i * sinf(angle);
			direction.j = forward.j * cosf(angle) + left.j * sinf(angle);
			around[corner] = top;
			around[corner].x += direction.i * 0.09f;
			around[corner].y += direction.j * 0.09f;
		}
		for (corner = 0; corner < 4; corner++)
		{
			short next = (short)((corner + 1) % 4);

			forge_marker_triangle(vertices, &count, &up_point, &around[corner], &around[next],
				forge_marker_pixel(marker, corner % 2 ? 1.f : 0.8f, alpha));
			forge_marker_triangle(vertices, &count, &down_point, &around[next], &around[corner],
				forge_marker_pixel(marker, corner % 2 ? 0.6f : 0.5f, alpha));
		}
	}
	if (count < 3)
		return;
	buffer = rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug, count);
	if (buffer == NONE)
		return;
	{
		void *locked;

		rasterizer_globals.current_lock_operation = _rasterizer_lock_debug;
		locked = rasterizer_dynamic_vertices_lock(buffer);
		if (locked)
		{
			csmemcpy(locked, vertices, (size_t)count * sizeof(vertices[0]));
			rasterizer_dynamic_vertices_unlock(buffer);
			rasterizer_debug_drawing_begin(FALSE);
			rasterizer_draw_dynamic_vertices(0, count / 3, buffer, 3);
			rasterizer_debug_drawing_end();
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
	}
	rasterizer_dynamic_vertices_delete(buffer);
}

/* where a character's marker stands and faces: its home (the one held: where
it is held) */
static boolean forge_marker_of_placed(
	struct forge_placed const *placed,
	real_point3d *position,
	real_vector3d *forward)
{
	if (!placed->ai || !placed->has_home)
		return FALSE;
	if (placed->id == forge_globals.held_marker_id)
	{
		*position = forge_globals.held_marker_position;
		*forward = forge_globals.held_forward;
		return TRUE;
	}
	*position = placed->home.position;
	*forward = placed->home.forward;
	return TRUE;
}

/* the placed entry of an id, NULL */
static struct forge_placed *forge_placed_of_id(
	long id)
{
	short index;

	if (id == NONE || id == 0)
		return NULL;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].id == id)
			return &forge_globals.placed[index];
	}
	return NULL;
}

/* whether the markers are shown: the monitor's, in a Forge game (not a map
variant played) */
static boolean forge_markers_shown(
	void)
{
	return forge_globals.active && forge_globals.game_active && !forge_globals.playing;
}

/* the marker the crosshair is on (its character's id; NONE), and how far
along the crosshair's line */
static long forge_marker_under_crosshair(
	real *distance)
{
	real_point3d position;
	real_vector3d forward;
	real nearest = FORGE_PICK_RANGE;
	long nearest_id = NONE;
	short index;

	if (!forge_markers_shown() || !forge_camera_get(&position, &forward))
		return NONE;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		real_point3d at;
		real_vector3d facing;
		real_vector3d to_center;
		real along;
		real across_squared;
		real entry;

		if (placed->id == forge_globals.held_marker_id || !forge_marker_of_placed(placed, &at, &facing))
			continue;
		at.z += FORGE_MARKER_HEIGHT * 0.5f;
		to_center.i = at.x - position.x;
		to_center.j = at.y - position.y;
		to_center.k = at.z - position.z;
		along = dot_product3d(&to_center, &forward);
		across_squared = dot_product3d(&to_center, &to_center) - along * along;
		if (along < 0.f || across_squared > FORGE_MARKER_PICK_RADIUS * FORGE_MARKER_PICK_RADIUS)
			continue;
		entry = along - square_root(FORGE_MARKER_PICK_RADIUS * FORGE_MARKER_PICK_RADIUS - across_squared);
		if (entry < nearest)
		{
			nearest = entry;
			nearest_id = placed->id;
		}
	}
	if (distance)
		*distance = nearest;
	return nearest_id;
}

/* (render.c's hook) every character's marker */
static void forge_markers_render(
	void)
{
	long under;
	short index;
	short drawn = 0;

	if (!forge_markers_shown())
	{
		forge_globals.markers_drawn = 0;
		return;
	}
	under = forge_globals.held_marker_id ? NONE : forge_marker_under_crosshair(NULL);
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS && drawn < FORGE_MARKER_MOST_DRAWN; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct forge_marker marker;
		real_vector3d to_marker;

		csmemset(&marker, 0, sizeof(marker));
		marker.kind = _forge_marker_ai_spawn;
		if (!forge_marker_of_placed(placed, &marker.position, &marker.forward))
			continue;
		to_marker.i = marker.position.x - render.camera.position.x;
		to_marker.j = marker.position.y - render.camera.position.y;
		to_marker.k = marker.position.z - render.camera.position.z;
		if (dot_product3d(&to_marker, &render.camera.forward) < -FORGE_MARKER_HEIGHT ||
			!render_frustum_sphere_visible(&render.frustum, &marker.position, FORGE_MARKER_HEIGHT))
		{
			continue;
		}
		forge_marker_team_color(placed, &marker);
		marker.lit = placed->id == under || placed->id == forge_globals.held_marker_id;
		forge_marker_draw(&marker);
		drawn++;
	}
	forge_globals.markers_drawn = drawn;
}

/* a character's marker taken (R on it): held where it is along the
crosshair, its facing the character's home's */
static void forge_marker_grab(
	long id,
	real distance)
{
	struct forge_placed *placed = forge_placed_of_id(id);
	char name[FORGE_NAME_SIZE];

	if (!placed || !placed->ai)
		return;
	/* (not one another player holds the character of) */
	if (placed->object_index != NONE && forge_net_held_by_other(placed->object_index))
	{
		forge_notice("%s", T("Someone is holding its character"));
		return;
	}
	forge_globals.held_marker_id = id;
	forge_globals.held_marker_position = placed->home.position;
	forge_globals.held_forward = placed->home.forward;
	forge_globals.held_up = placed->home.up;
	forge_globals.held_distance = PIN(distance + FORGE_MARKER_PICK_RADIUS, FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
	forge_tag_readable_name(placed->tag_index, name, sizeof(name));
	platform_log("forge: grabbed the spawn marker of %s (#%ld) at %.2f %.2f %.2f", name, id, placed->home.position.x,
		placed->home.position.y, placed->home.position.z);
}

/* (each update while one is held) where the camera holds it: on the ground
under the crosshair's point, as an object let go would fall */
static void forge_marker_held_update(
	void)
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d down;
	struct collision_result collision;
	struct forge_placed *placed = forge_placed_of_id(forge_globals.held_marker_id);

	if (!placed)
	{
		forge_globals.held_marker_id = 0;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	/* (along the crosshair as far as it is held, or to the ground or wall
	the crosshair meets first) */
	{
		real_vector3d along;

		along.i = forward.i * forge_globals.held_distance;
		along.j = forward.j * forge_globals.held_distance;
		along.k = forward.k * forge_globals.held_distance;
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&position, &along, NONE, &collision))
		{
			real t = MAX(collision.t * forge_globals.held_distance - 0.1f, 0.f);

			position.x += forward.i * t;
			position.y += forward.j * t;
			position.z += forward.k * t;
		}
		else
		{
			position.x += along.i;
			position.y += along.j;
			position.z += along.k;
		}
	}
	{
		real_vector3d facing = forge_globals.held_forward;
		real_vector3d up = forge_globals.held_up;

		forge_snapped(&position, &facing, &up);
	}
	/* (stood on what is under it, within a few metres) */
	down.i = 0.f;
	down.j = 0.f;
	down.k = -4.f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &position,
		&down, NONE, &collision))
	{
		/* (a little over it: a character placed there stands on it, never
		in it) */
		position.z += down.k * collision.t + FORGE_MARKER_CLEARANCE;
	}
	forge_globals.held_marker_position = position;
}

/* the marker held let go: its character's home there (a network game's
client asks the host; one of an older host keeps none) */
static void forge_marker_drop(
	void)
{
	struct forge_placed *placed = forge_placed_of_id(forge_globals.held_marker_id);
	long id = forge_globals.held_marker_id;
	struct forge_transform home;

	forge_globals.held_marker_id = 0;
	if (!placed)
		return;
	home.position = forge_globals.held_marker_position;
	home.forward = forge_globals.held_forward;
	home.up = forge_globals.held_up;
	if (forge_net_client())
	{
		forge_net_client_spawn_move(id, &home);
		return;
	}
	forge_marker_home_set(placed, &home);
}

/* (forge_marker_drop; a machine's request, forge_net.inc) a character's
home set from its marker: where it is placed again from now on, saved so */
static void forge_marker_home_set(
	struct forge_placed *placed,
	struct forge_transform const *home)
{
	char name[FORGE_NAME_SIZE];

	placed->home = *home;
	placed->has_home = TRUE;
	/* (not where its living body is followed to as it settles) */
	placed->settle_until = system_milliseconds();
	forge_tag_readable_name(placed->tag_index, name, sizeof(name));
	platform_log("forge: the spawn marker of %s (#%ld) moved to %.2f %.2f %.2f facing %.2f %.2f %.2f", name, placed->id,
		home->position.x, home->position.y, home->position.z, home->forward.i, home->forward.j, home->forward.k);
}

/* (render.c, each player's window, after the objects) Forge's drawn things:
the characters' spawn markers (the monitor's), the other players' monitors
(a network game's) */
void forge_render_world(
	void)
{
	forge_markers_render();
	forge_net_render_monitors();
}
