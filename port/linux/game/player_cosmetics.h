/*
PLAYER_COSMETICS.H

(port) The platform's own cosmetics: more armour colours than the game's
eighteen and a visor colour, saved in the profile's file (player_profile.c's
extension, after the stock checksum: a stock or older build reads nothing of
it) and sent between this project's builds in network_player.icon_index
(players.h NETWORK_PLAYER_COSMETICS_*), never on the crossplay wire. Every
other machine sees the nearest of the eighteen (primary_color_index stays
one of them) and the stock gold visor.

Colours are numbered as the game's: 0-17 the game's own (profile_color_table),
18 on this platform's extension (PLAYER_COSMETICS_*). Visors: 0 the stock
gold (nothing changed), 1 on a tint.
*/

#ifndef __PLAYER_COSMETICS_H
#define __PLAYER_COSMETICS_H

#include "cseries/cseries.h"
#include "math/real_math.h"

enum
{
	PLAYER_COSMETICS_CLASSIC_COLORS = 18,
	PLAYER_COSMETICS_EXTENDED_COLORS = 24,
	PLAYER_COSMETICS_COLORS = PLAYER_COSMETICS_CLASSIC_COLORS + PLAYER_COSMETICS_EXTENDED_COLORS,
	PLAYER_COSMETICS_VISORS = 12,
};

/* a colour (0 - PLAYER_COSMETICS_COLORS-1): its 0xRRGGBB, its name
(English, for T()), the nearest of the game's eighteen */
unsigned long player_cosmetics_color_rgb(short color);
char const *player_cosmetics_color_name(short color);
short player_cosmetics_nearest_classic(short color);

/* a visor (0 - PLAYER_COSMETICS_VISORS-1): its 0xRRGGBB and name */
unsigned long player_cosmetics_visor_rgb(short visor);
char const *player_cosmetics_visor_name(short visor);

/* a profile's armour colour (NONE: random; the extension's when it has one,
else the stock primary_color_index) and visor (0 stock) */
short player_cosmetics_profile_color(long profile_index, short primary_color_index);
short player_cosmetics_profile_visor(long profile_index);
/* saved (the profile's file rewritten with them: the colour's nearest
classic is the profile's primary_color_index, set by the caller) */
boolean player_cosmetics_profile_save(long profile_index, short color, short visor);

/* network_player.icon_index for a player: the flame's claim and the
cosmetics (a colour 18+, a visor 1+), the stock values when there is
nothing extra */
short player_cosmetics_icon_encode(boolean flame_claim, short color, short visor);
boolean player_cosmetics_icon_flame(short icon_index);
/* the extended colour carried (NONE: none) and the visor (0 stock) */
short player_cosmetics_icon_color(short icon_index);
short player_cosmetics_icon_visor(short icon_index);
/* the value a host keeps: a well-formed one, else NONE */
short player_cosmetics_icon_clean(short icon_index);

/* (game_engine.c) a player's extended armour colour (NONE: the record's
stock one): this machine's players' from their profiles, the others' from
what their machines sent */
short player_cosmetics_player_color(long player_index);

/* (render_objects.c) the visor tint the object is drawn with, set around
its model's draw (rasterizer_xbox_models.c reads it) */
void player_cosmetics_render_object_begin(long object_index);
void player_cosmetics_render_object_end(void);
/* (rasterizer_xbox_models.c) the stock cyborg's visor shader drawn now:
its tint, else FALSE */
boolean player_cosmetics_visor_tint(void const *shader_model, long reflection_cube_map_index,
	long base_map_index, real_rgb_color *tint);

/* (menu_functions.c) the Customization preview's visor, NONE for the
profile's */
void player_cosmetics_preview_visor(short visor);

/* (the settings panel, port/vita/host/vita_settings.c: the first local
player's profile's colour - 0 random, then colour + 1 - and visor, chosen
from the panel's thread, taken at the game's frame: player_cosmetics_update,
main.c) */
int player_cosmetics_panel_color_count(void);
char const *player_cosmetics_panel_color_name(int index);
int player_cosmetics_panel_visor_count(void);
char const *player_cosmetics_panel_visor_name(int index);
int player_cosmetics_panel_color_chosen(void);
int player_cosmetics_panel_visor_chosen(void);
void player_cosmetics_panel_choose_color(int index);
void player_cosmetics_panel_choose_visor(int index);
void player_cosmetics_update(void);

#endif
