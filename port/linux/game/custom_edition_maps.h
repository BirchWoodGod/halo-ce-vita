/*
CUSTOM_EDITION_MAPS.H

The Halo Custom Edition maps in the menus of the native builds. With the
game.custom_edition setting on, the multiplayer level list
(source/interface/ui_widget_event_handler_functions.c) offers the Custom
Edition multiplayer maps in the maps folder after the Xbox levels, and the
campaign's level list the Custom Edition campaign maps (solo scenarios)
after its ten levels, played alone or as network co-op. The menus
that show a level (ui_widget_game_data_input_functions.c) know a level by an
index into the strings and frames of their own tags; these maps get display
indices beyond those, and text_group.c and ui_widget.c ask this unit for
their names, descriptions and pictures (custom_edition_maps.c).
*/

#ifndef __CUSTOM_EDITION_MAPS_H
#define __CUSTOM_EDITION_MAPS_H

/* ---------- constants */

/* the most maps of both kinds looked for, and the entries of the campaign's
level list: its ten levels, then the campaign maps
(ui_widget_event_handler_functions.c single_player_level_data) */
#define CUSTOM_EDITION_MAPS_MAXIMUM 128
#define CUSTOM_EDITION_MAPS_CAMPAIGN_LIST_ENTRIES (10 + CUSTOM_EDITION_MAPS_MAXIMUM)

/* ---------- structures */

struct bitmap_data;

/* ---------- prototypes/CUSTOM_EDITION_MAPS.C */

/* Looks for the Custom Edition multiplayer maps anew and returns the levels
the level list offers: the `xbox_level_count` levels of `xbox_levels`, then
those maps in the order of their names; `*level_count` is how many there
are. */
char **custom_edition_maps_level_list(
	char **xbox_levels,
	short xbox_level_count,
	short *level_count);

/* The display index of level `level_index` of the latest level list: an
Xbox level's own index, or a Custom Edition map's display index. */
short custom_edition_maps_level_display_index(
	short level_index);

/* The display index of the Custom Edition map the level name `level_name`
(a network game's map name) names, or NONE when it names none of the maps
found; the maps are looked for the first time this is asked. */
short custom_edition_maps_display_index(
	char const *level_name);

/* A Custom Edition map's name and description, for the display index
`display_index`, or NULL when that is no map's (text_group.c). */
wchar_t *custom_edition_maps_name(
	short display_index);
wchar_t *custom_edition_maps_description(
	short display_index);

/* What a bitmap widget showing frame `*frame_index` of the bitmap tag
`bitmap_tag_index` draws instead (ui_widget.c): when that is the level
pictures' tag and the frame a Custom Edition map's display index, the map's
picture, to be drawn over the whole widget, or NULL with `*frame_index` made
the unknown level's frame when the map has no picture. NULL, the frame
unchanged, for any other frame or tag. */
struct bitmap_data *custom_edition_maps_picture(
	long bitmap_tag_index,
	short *frame_index);

/* What a multiplayer host sends with a level so that its players can tell
whether they have its copy (the network game's map version, which the Xbox
left 0): 0 for an Xbox level, whose copies differ by region and play
together, else custom_edition_cache_map_identity's (0: no such map); also
for a Halo PC map named as an Xbox level when PC maps is on, which this
machine then plays instead of that level. */
unsigned long custom_edition_maps_network_identity(
	char const *level_name);

/* Whether a player may play the host's level `level_name`, of which the host
sent `host_identity`: an Xbox level always (unless the host plays a Halo PC
map of its name: as a custom map then); a custom map when this machine
has it and, if the host said which copy, that copy (*missing: not at all).
Logs why not. */
boolean custom_edition_maps_host_copy_matches(
	char const *level_name,
	unsigned long host_identity,
	boolean *missing);

/* What custom_edition_maps_loadable says of a level this machine has */
enum custom_edition_maps_load
{
	_custom_edition_maps_loadable = 0,
	/* a Custom Edition map, with PC maps (HALO_CUSTOM_EDITION) off */
	_custom_edition_maps_needs_pc_maps,
	/* a Custom Edition map whose resource maps are not in the maps folder */
	_custom_edition_maps_needs_resource_maps,
	/* no map file, or one that is neither: an Xbox cache that is not a
	multiplayer map the cache partition takes (custom_edition_cache.c) */
	_custom_edition_maps_not_loadable,
};

/* Whether this machine can load the level `level_name` (a network game's)
as it is now: an Xbox level always; a custom map when its file is an Xbox
multiplayer map, or a Custom Edition map with PC maps on and the resource
maps it takes tags from in the maps folder (those missing are then named in
`missing`, `missing_size` characters). A map turned off on the Modded maps
page still loads. A game whose map cannot be loaded stops as a damaged disc
(cache_files.c): the multiplayer join checks this first
(network_client_manager.c, map_share.c). */
short custom_edition_maps_loadable(
	char const *level_name,
	char *missing,
	long missing_size);

/* Whether this machine cannot play the Xbox level `level_name` that the host
plays as one (`host_identity` 0: custom_edition_maps_network_identity): its
file missing, cut short, damaged, too big, or a Halo PC map of that name
(cache_files_xbox_map_problem), which precaching it would have stopped at as
a damaged disc. TRUE then, with `why` for the player; FALSE for any other
level, and for a Halo PC map the host plays under an Xbox level's name
(custom_edition_maps_host_copy_matches and custom_edition_maps_loadable
decide those). */
boolean custom_edition_maps_stock_problem(
	char const *level_name,
	unsigned long host_identity,
	char *why,
	long why_size);

/* The title of the level `level_name` for the player: an Xbox level's as the
menus have it ("Battle Creek"), else its map's file name. */
char const *custom_edition_maps_level_title(
	char const *level_name);

/* Makes the next question about the maps look for them anew (a map was
downloaded into the folder: map_share.c). */
void custom_edition_maps_look_again(
	void);
/* forgets what the map file `name` (.map or .yelo) was found to be, whose
file a download has just replaced */
void custom_edition_maps_file_forget(
	char const *name);

/* The campaign's level list: looks for the maps anew and returns how many
Custom Edition campaign maps there are, in the order of their names; then
the level name and the display index (for the list's names, pictures and
descriptions) of the one at `campaign_index`, NULL and NONE past them. */
short custom_edition_maps_campaigns_find(
	void);
char const *custom_edition_maps_campaign_level_name(
	short campaign_index);
short custom_edition_maps_campaign_display_index(
	short campaign_index);

/* Whether `level_name` is a campaign level's: one of the campaign's ten, or
a Custom Edition campaign map's (custom_maps\<name>, custom_edition_level_name:
no file is looked at, so a client without the map knows it too). A network
game on one with no game engine is co-op. */
boolean custom_edition_maps_campaign_level(
	char const *level_name);

/* Whether a host may send its copy of the level `level_name` to a joiner
(map_share.c): a custom map it has, which is in its level list (on, and one
this machine plays: an Xbox multiplayer map, or a Custom Edition one with
the setting on), never one of the Xbox's own levels. */
boolean custom_edition_maps_shareable(
	char const *level_name);

#endif
