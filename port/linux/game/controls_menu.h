/*
CONTROLS_MENU.H

The menus' KEYBOARD & MOUSE page (controls_menu.c): the PC builds' keyboard
and mouse bindings (port/linux/include/halo_key_bindings.h) and the
mouse's settings, in each menu style's look (Halo CE: menus/classic/
keys.xml; Halo 3: halo3/keys.xml; Online: online/keys.xml), one set of
functions behind them. Its functions are the menus' own (menu_tags.c finds
them by name after port_function_names, from CONTROLS_MENU_FUNCTION_FIRST;
menu_functions.c hands them here), so the page adds no entry to the
menus' long lists.
*/

#ifndef CONTROLS_MENU_H
#define CONTROLS_MENU_H

struct widget_instance;
struct event_record;

/* the page's functions' indices, past menu_functions.c's (relative to
PC_MENU_FUNCTION_BASE) */
#define CONTROLS_MENU_FUNCTION_FIRST 2048

/* the event handler functions' and game data inputs' names (NULL past
them), and an index for a name (-1 for none) */
char const *controls_menu_function_name(long index);
char const *controls_menu_game_data_input_name(long index);
long controls_menu_function_index(char const *name);
long controls_menu_game_data_input_index(char const *name);

/* (menu_functions.c) the event handler function index (from 0) run on
widget: what a menu function returns (FALSE: no screen opens or closes
after it) */
unsigned char controls_menu_event(struct widget_instance *widget, struct event_record *event, long index,
	unsigned char *widget_deleted);
/* (menu_functions.c) the game data input index (from 0) on widget */
void controls_menu_game_data(struct widget_instance *widget, long index);

/* whether this build has the page (a PC's: the desktop's keyboard and
mouse); the entries to it are hidden elsewhere (port keys entry) */
int controls_menu_available(void);

#endif
