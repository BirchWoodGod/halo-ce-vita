/*
COOP_MENU.H

Co-op from the campaign's own menus: the text the menus show of it
(coop_menu.c).
*/

#ifndef __HALO_LINUX_COOP_MENU_H
#define __HALO_LINUX_COOP_MENU_H

struct network_game;

/* the level's name (map_list, on one line) and the difficulty's
(difficulty_names), as the campaign's menus name them; empty if none */
void coop_menu_level_name(char const *map_name, wchar_t *text, int count);
void coop_menu_difficulty_name(short difficulty, wchar_t *text, int count);
/* "<host>: <level> (<difficulty>)", a co-op game in the game lists */
void coop_menu_game_description(wchar_t const *host, char const *map_name, short difficulty, wchar_t *text,
	int count);
/* fits the text to width as the widget draws it: cut short with "...", or
(scroll) scrolled along over time, a part that fits */
struct widget_instance;
void coop_menu_fit_line(wchar_t *text, short width, struct widget_instance *widget, boolean scroll);
/* whether a network game is co-op: no game engine, a campaign level */
boolean coop_menu_game_is_cooperative(struct network_game const *game);
/* whether this build's maps play with others (as the menus' own check) */
boolean coop_menu_available(void);
/* the text drawn over the screen whose tag this is, if any (render_ui_widgets) */
void coop_menu_render(long screen_tag_index);

#endif
