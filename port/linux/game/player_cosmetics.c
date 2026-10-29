/*
PLAYER_COSMETICS.C

(port) The platform's own armour colours and visor colours: see
player_cosmetics.h. The colours are plain RGB values of this project's
choosing (tools/online_menu_art.py draws the same ones as the menus'
swatches: keep the two tables alike).

- Saved: player_profile.c's extension of the profile's file (two bytes after
  the stock checksum, with a mark of their own); the stock record keeps the
  nearest of the game's eighteen, so a build without this reads a good
  profile in that colour and the stock gold visor.
- Sent: network_player.icon_index, which the game never read and the record
  carries whole (the flaming helmet's claim's field: players.h). A player
  with nothing extra sends the stock values (NONE, or the flame's own claim
  value, as before); one with an extended colour or a visor sends
  NETWORK_PLAYER_COSMETICS_MARK and the values. Builds before this one read
  neither (their hosts keep NONE in its place): the nearest colour, gold.
  Never on the crossplay wire (network_client_manager.c): OpenCE and other
  PC builds see the nearest colour and the gold visor.
- Drawn: the armour through the game's own change colour
  (player_profile_get_rgb_color knows the extended colours); the visor
  through the stock cyborg visor shader's reflection tint (its gold is the
  reflection's: rasterizer_xbox_models.c), a constant of the stock pixel
  shader - no new shader program (the Vita's shaders.pak is unchanged).
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "units/units.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_files.h"
#include "player_cosmetics.h"
long menu_scene_spartan_object(void);
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);
#ifdef HALO_LINUX
boolean player_profile_extension_get(long player_profile_index, byte *color_plus_one, byte *visor);
boolean player_profile_extension_set(long player_profile_index, byte color_plus_one, byte visor);
#endif

/* (the game's eighteen, player_profile.c profile_color_table's, then the
platform's) */
static struct
{
	unsigned long rgb;
	char const *name;
} const player_cosmetics_colors[PLAYER_COSMETICS_COLORS] =
{
	{ 0xFFFFFF, "WHITE" }, { 0x000000, "BLACK" }, { 0xFE0000, "RED" }, { 0x0201E3, "BLUE" },
	{ 0x707E71, "GRAY" }, { 0xFFFF01, "YELLOW" }, { 0x00FF01, "GREEN" }, { 0xFF56B9, "PINK" },
	{ 0xAB10F4, "PURPLE" }, { 0x01FFFF, "CYAN" }, { 0x6493ED, "COBALT" }, { 0xFF7F00, "ORANGE" },
	{ 0x1ECC91, "TEAL" }, { 0x006401, "SAGE" }, { 0x603814, "BROWN" }, { 0xC69C6C, "TAN" },
	{ 0x9D0B0E, "MAROON" }, { 0xF5999E, "SALMON" },
	/* (the platform's) */
	{ 0x4A5560, "SLATE" }, { 0x8A96A0, "STEEL" }, { 0x2B2D30, "CHARCOAL" }, { 0xC0C4C8, "SILVER" },
	{ 0x1B2A5C, "NAVY" }, { 0x2E2A7A, "INDIGO" }, { 0x6FB7E8, "SKY" }, { 0xBFE6F2, "ICE" },
	{ 0x0F5E5E, "DEEP TEAL" }, { 0x8FE3B5, "MINT" }, { 0x3E5B2A, "MOSS" }, { 0x5B6324, "OLIVE" },
	{ 0x9BD13A, "LIME" }, { 0xD8C79A, "SAND" }, { 0xC9A43A, "GOLD" }, { 0xE0A21A, "AMBER" },
	{ 0xA4441C, "RUST" }, { 0x4A3020, "COCOA" }, { 0xB0182A, "CRIMSON" }, { 0x5E1224, "WINE" },
	{ 0xD46A8C, "ROSE" }, { 0x5E2A62, "PLUM" }, { 0x6A3FB8, "VIOLET" }, { 0xA897D8, "LAVENDER" },
};

/* (0: the stock gold, drawn as the shader has it) */
static struct
{
	unsigned long rgb;
	char const *name;
} const player_cosmetics_visors[PLAYER_COSMETICS_VISORS] =
{
	{ 0xFFFFFF, "GOLD" }, { 0xC8D0D8, "SILVER" }, { 0x3A78FF, "BLUE" }, { 0x40E070, "GREEN" },
	{ 0xFF3030, "RED" }, { 0xA050FF, "PURPLE" }, { 0x101010, "BLACK" }, { 0xFF8A20, "ORANGE" },
	{ 0x30E0E0, "CYAN" }, { 0xFF60B0, "PINK" }, { 0xF0F0F0, "WHITE" }, { 0xE8D040, "YELLOW" },
};

typedef char player_cosmetics_icon_bits_check[PLAYER_COSMETICS_EXTENDED_COLORS < 128 &&
	PLAYER_COSMETICS_VISORS <= 16 ? 1 : -1];

unsigned long player_cosmetics_color_rgb(short color)
{
	return color >= 0 && color < PLAYER_COSMETICS_COLORS ? player_cosmetics_colors[color].rgb : 0xFFFFFF;
}

char const *player_cosmetics_color_name(short color)
{
	return color >= 0 && color < PLAYER_COSMETICS_COLORS ? player_cosmetics_colors[color].name : "RANDOM";
}

short player_cosmetics_nearest_classic(short color)
{
	unsigned long rgb;
	long best_distance = 0x7FFFFFFF;
	short best = 0, index;

	if (color < PLAYER_COSMETICS_CLASSIC_COLORS)
		return color;
	if (color >= PLAYER_COSMETICS_COLORS)
		return NONE;
	rgb = player_cosmetics_colors[color].rgb;
	for (index = 0; index < PLAYER_COSMETICS_CLASSIC_COLORS; index++)
	{
		unsigned long other = player_cosmetics_colors[index].rgb;
		long dr = (long)((rgb >> 16) & 0xFF) - (long)((other >> 16) & 0xFF);
		long dg = (long)((rgb >> 8) & 0xFF) - (long)((other >> 8) & 0xFF);
		long db = (long)(rgb & 0xFF) - (long)(other & 0xFF);
		/* (a weighted distance: green counts most to the eye) */
		long distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db;

		if (distance < best_distance)
		{
			best_distance = distance;
			best = index;
		}
	}
	return best;
}

unsigned long player_cosmetics_visor_rgb(short visor)
{
	return visor > 0 && visor < PLAYER_COSMETICS_VISORS ? player_cosmetics_visors[visor].rgb : 0xFFFFFF;
}

char const *player_cosmetics_visor_name(short visor)
{
	return visor >= 0 && visor < PLAYER_COSMETICS_VISORS ? player_cosmetics_visors[visor].name : "GOLD";
}

short player_cosmetics_profile_color(long profile_index, short primary_color_index)
{
#ifdef HALO_LINUX
	byte color = 0, visor = 0;

	if (profile_index != NONE && player_profile_extension_get(profile_index, &color, &visor) && color > 0 &&
		color - 1 >= PLAYER_COSMETICS_CLASSIC_COLORS && color - 1 < PLAYER_COSMETICS_COLORS &&
		player_cosmetics_nearest_classic((short)(color - 1)) == primary_color_index)
	{
		/* (the extension's colour while the stock record still has its
		nearest: a build without it that changed the colour wins) */
		return (short)(color - 1);
	}
#else
	(void)profile_index;
#endif
	return primary_color_index;
}

short player_cosmetics_profile_visor(long profile_index)
{
#ifdef HALO_LINUX
	byte color = 0, visor = 0;

	if (profile_index != NONE && player_profile_extension_get(profile_index, &color, &visor) &&
		visor < PLAYER_COSMETICS_VISORS)
	{
		return visor;
	}
#else
	(void)profile_index;
#endif
	return 0;
}

boolean player_cosmetics_profile_save(long profile_index, short color, short visor)
{
#ifdef HALO_LINUX
	byte color_byte = (byte)(color >= PLAYER_COSMETICS_CLASSIC_COLORS && color < PLAYER_COSMETICS_COLORS ?
		color + 1 : 0);
	byte visor_byte = (byte)(visor > 0 && visor < PLAYER_COSMETICS_VISORS ? visor : 0);
	boolean saved;

	if (profile_index == NONE)
		return FALSE;
	saved = player_profile_extension_set(profile_index, color_byte, visor_byte);
	platform_log("cosmetics: profile 0x%lX: colour %d (%s), visor %d (%s) %s", (unsigned long)profile_index, color,
		player_cosmetics_color_name(color), visor, player_cosmetics_visor_name(visor), saved ? "saved" : "NOT saved");
	return saved;
#else
	(void)profile_index;
	(void)color;
	(void)visor;
	return FALSE;
#endif
}

short player_cosmetics_icon_encode(boolean flame_claim, short color, short visor)
{
	unsigned short extended = (unsigned short)(color >= PLAYER_COSMETICS_CLASSIC_COLORS &&
		color < PLAYER_COSMETICS_COLORS ? color - PLAYER_COSMETICS_CLASSIC_COLORS + 1 : 0);
	unsigned short tint = (unsigned short)(visor > 0 && visor < PLAYER_COSMETICS_VISORS ? visor : 0);

	if (!extended && !tint)
		return flame_claim ? NETWORK_PLAYER_FLAME_CLAIM : NONE;
	return (short)(NETWORK_PLAYER_COSMETICS_MARK | (flame_claim ? NETWORK_PLAYER_COSMETICS_FLAME_BIT : 0) |
		(tint << NETWORK_PLAYER_COSMETICS_VISOR_SHIFT) | extended);
}

static boolean icon_marked(short icon_index)
{
	return ((unsigned short)icon_index & NETWORK_PLAYER_COSMETICS_MARK_MASK) == NETWORK_PLAYER_COSMETICS_MARK;
}

boolean player_cosmetics_icon_flame(short icon_index)
{
	return icon_index == NETWORK_PLAYER_FLAME_CLAIM ||
		(icon_marked(icon_index) && ((unsigned short)icon_index & NETWORK_PLAYER_COSMETICS_FLAME_BIT));
}

short player_cosmetics_icon_color(short icon_index)
{
	short extended;

	if (!icon_marked(icon_index))
		return NONE;
	extended = (short)((unsigned short)icon_index & NETWORK_PLAYER_COSMETICS_COLOR_MASK);
	return extended > 0 && extended <= PLAYER_COSMETICS_EXTENDED_COLORS ?
		(short)(PLAYER_COSMETICS_CLASSIC_COLORS + extended - 1) : NONE;
}

short player_cosmetics_icon_visor(short icon_index)
{
	short visor;

	if (!icon_marked(icon_index))
		return 0;
	visor = (short)(((unsigned short)icon_index >> NETWORK_PLAYER_COSMETICS_VISOR_SHIFT) & 0xF);
	return visor < PLAYER_COSMETICS_VISORS ? visor : 0;
}

short player_cosmetics_icon_clean(short icon_index)
{
	if (icon_index == NETWORK_PLAYER_FLAME_CLAIM)
		return icon_index;
	if (!icon_marked(icon_index))
		return NONE;
	return player_cosmetics_icon_encode(player_cosmetics_icon_flame(icon_index), player_cosmetics_icon_color(icon_index),
		player_cosmetics_icon_visor(icon_index));
}

short player_cosmetics_player_color(long player_index)
{
	struct player_datum *player = player_data && player_index != NONE ? player_try_and_get(player_index) : NULL;
	short color;

	if (!player)
		return NONE;
	if (player->local_player_index != NONE)
	{
		long profile_index = player_ui_get_active_player_profile_index(player->local_player_index);
		struct player_profile profile;

		if (profile_index == NONE || !player_profile_get(profile_index, &profile))
			return NONE;
		color = player_cosmetics_profile_color(profile_index, profile.primary_color_index);
		/* (the profile's own colour where the record has it: the game's
		random one kept when the profile has none) */
		return color >= PLAYER_COSMETICS_CLASSIC_COLORS &&
			player_cosmetics_nearest_classic(color) == player->network_player_data.primary_color_index ? color : NONE;
	}
	return player_cosmetics_icon_color(player->network_player_data.icon_index);
}

/* ---------- the visor drawn */

static struct
{
	/* the object drawn now, its visor (0 none) */
	short visor;
	/* the preview's visor asked (NONE: the profile's) */
	short preview_visor;
	boolean preview_set;
	/* the stock visor shaders seen: their shader's address and whether it
	is one */
	void const *shaders[64];
	long shader_maps[64][2];
	boolean visor_shader[64];
	long shader_count;
} player_cosmetics_render = { 0, NONE, FALSE };

void player_cosmetics_preview_visor(short visor)
{
	player_cosmetics_render.preview_visor = visor;
	player_cosmetics_render.preview_set = visor != NONE;
}

static short object_visor(long object_index)
{
	struct unit_datum *unit = unit_try_and_get(object_index);
	struct player_datum *player;
	long player_index;

	if (object_index != NONE && object_index == menu_scene_spartan_object())
	{
		if (player_cosmetics_render.preview_set)
			return player_cosmetics_render.preview_visor;
		return player_cosmetics_profile_visor(player_ui_get_active_player_profile_index(0));
	}
	if (!unit)
		return 0;
	player_index = unit->unit.player_index;
	if (player_index == NONE || !player_data || !(player = player_try_and_get(player_index)))
		return 0;
	/* (this machine's own players: their profiles'; the others': what
	their machines sent) */
	if (player->local_player_index != NONE)
		return player_cosmetics_profile_visor(player_ui_get_active_player_profile_index(player->local_player_index));
	return player_cosmetics_icon_visor(player->network_player_data.icon_index);
}

void player_cosmetics_render_object_begin(long object_index)
{
	player_cosmetics_render.visor = object_visor(object_index);
}

void player_cosmetics_render_object_end(void)
{
	player_cosmetics_render.visor = 0;
}

boolean player_cosmetics_visor_tint(void const *shader_model, long reflection_cube_map_index, long base_map_index,
	real_rgb_color *tint)
{
	unsigned long rgb;
	long index;
	boolean visor_shader = FALSE;

	if (player_cosmetics_render.visor <= 0)
		return FALSE;
	for (index = 0; index < player_cosmetics_render.shader_count; index++)
	{
		if (player_cosmetics_render.shaders[index] == shader_model &&
			player_cosmetics_render.shader_maps[index][0] == reflection_cube_map_index &&
			player_cosmetics_render.shader_maps[index][1] == base_map_index)
		{
			visor_shader = player_cosmetics_render.visor_shader[index];
			break;
		}
	}
	if (index == player_cosmetics_render.shader_count)
	{
		/* (the stock cyborg's visor: its base map the cyborg's, its cube
		map the mirror surface's) */
		char const *cube = reflection_cube_map_index != NONE ? tag_get_name(reflection_cube_map_index) : NULL;
		char const *base = base_map_index != NONE ? tag_get_name(base_map_index) : NULL;

		visor_shader = cube && base && strstr(cube, "cyborg") && strstr(cube, "mirror surface") &&
			!strcmp(base, "characters\\cyborg\\bitmaps\\cyborg");
		/* (full: begun again - a new map's) */
		if (player_cosmetics_render.shader_count == 64)
			player_cosmetics_render.shader_count = 0;
		{
			player_cosmetics_render.shaders[player_cosmetics_render.shader_count] = shader_model;
			player_cosmetics_render.shader_maps[player_cosmetics_render.shader_count][0] = reflection_cube_map_index;
			player_cosmetics_render.shader_maps[player_cosmetics_render.shader_count][1] = base_map_index;
			player_cosmetics_render.visor_shader[player_cosmetics_render.shader_count] = visor_shader;
			player_cosmetics_render.shader_count++;
		}
	}
	if (!visor_shader)
		return FALSE;
	rgb = player_cosmetics_visor_rgb(player_cosmetics_render.visor);
	tint->red = ((rgb >> 16) & 0xFF) / 255.f;
	tint->green = ((rgb >> 8) & 0xFF) / 255.f;
	tint->blue = (rgb & 0xFF) / 255.f;
	return TRUE;
}

/* ---------- the settings panel's rows (port/vita/host/vita_settings.c) */

static struct
{
	/* the first local player's profile's choices (colour + 1, 0 random;
	visor), as last read; a choice asked (-2: none), taken by the game's
	frame (player_cosmetics_update) */
	int volatile color_chosen;
	int volatile visor_chosen;
	int volatile color_request;
	int volatile visor_request;
} player_cosmetics_panel = { 0, 0, -2, -2 };

int player_cosmetics_panel_color_count(void)
{
	return PLAYER_COSMETICS_COLORS + 1;
}

char const *player_cosmetics_panel_color_name(int index)
{
	return index <= 0 ? "RANDOM" : player_cosmetics_color_name((short)(index - 1));
}

int player_cosmetics_panel_visor_count(void)
{
	return PLAYER_COSMETICS_VISORS;
}

char const *player_cosmetics_panel_visor_name(int index)
{
	return player_cosmetics_visor_name((short)index);
}

int player_cosmetics_panel_color_chosen(void)
{
	return player_cosmetics_panel.color_chosen;
}

int player_cosmetics_panel_visor_chosen(void)
{
	return player_cosmetics_panel.visor_chosen;
}

void player_cosmetics_panel_choose_color(int index)
{
	player_cosmetics_panel.color_chosen = index;
	player_cosmetics_panel.color_request = index;
}

void player_cosmetics_panel_choose_visor(int index)
{
	player_cosmetics_panel.visor_chosen = index;
	player_cosmetics_panel.visor_request = index;
}

static long panel_profile(void)
{
	long profile_index = player_ui_get_active_player_profile_index(0);

	return profile_index != NONE ? profile_index : player_ui_get_player1_last_used_profile_index();
}

void player_cosmetics_update(void)
{
	int color_request = player_cosmetics_panel.color_request;
	int visor_request = player_cosmetics_panel.visor_request;
	long profile_index = panel_profile();
	struct player_profile profile;
	short color, visor;

	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
		return;
	color = player_cosmetics_profile_color(profile_index, profile.primary_color_index);
	visor = player_cosmetics_profile_visor(profile_index);
	if (color_request != -2 || visor_request != -2)
	{
		player_cosmetics_panel.color_request = -2;
		player_cosmetics_panel.visor_request = -2;
		if (color_request != -2)
			color = (short)(color_request - 1 >= 0 && color_request - 1 < PLAYER_COSMETICS_COLORS ? color_request - 1 : NONE);
		if (visor_request != -2)
			visor = (short)(visor_request >= 0 && visor_request < PLAYER_COSMETICS_VISORS ? visor_request : 0);
		/* (the record's colour the nearest of the eighteen, the rest the
		file's extension; the profile in use then the saved one) */
		profile.primary_color_index = color == NONE ? NONE : player_cosmetics_nearest_classic(color);
		player_profile_save(profile_index, &profile);
		player_cosmetics_profile_save(profile_index, color, visor);
		if (player_ui_get_active_player_profile_index(0) == profile_index)
			player_ui_set_active_player_profile(0, profile_index, &profile);
		platform_log("cosmetics: the settings panel chose colour %d, visor %d", color, visor);
	}
	player_cosmetics_panel.color_chosen = color + 1;
	player_cosmetics_panel.visor_chosen = visor;
}
