/*
SYSTEM_LINK_SHORTCUT.H

The Vita settings panel's Play page (hosting, joining, co-op from the
Campaign screen), and what it says about the network game (port/vita/host/vita_settings.c;
the game's side: port/linux/game/system_link_shortcut.c). The two share a
few ints, the panel's side being another ABI: plain ints only.
*/

#ifndef __HALO_LINUX_SYSTEM_LINK_SHORTCUT_H
#define __HALO_LINUX_SYSTEM_LINK_SHORTCUT_H

/* halo_system_link_request: the panel sets it, the main loop answers in
halo_system_link_answer and sets it back to SYSTEM_LINK_REQUEST_NONE */
enum
{
	SYSTEM_LINK_REQUEST_NONE,
	/* the game's System Link screen, to host a game or to join one (the
	same screen: its list of games, where Y creates one) */
	SYSTEM_LINK_REQUEST_HOST,
	SYSTEM_LINK_REQUEST_JOIN,
	/* the Campaign screen (its profiles, then the levels and the
	difficulty, where Y hosts co-op: coop_menu.c), with the main menu
	behind it: the Play page's Host co-op campaign */
	SYSTEM_LINK_REQUEST_CAMPAIGN,
};

enum
{
	SYSTEM_LINK_ANSWER_NONE,
	/* the System Link screen is open, at its first step (press A to join);
	or the Campaign screen, at its profiles */
	SYSTEM_LINK_ANSWER_OPENED,
	/* not at the menus: a level is being played */
	SYSTEM_LINK_ANSWER_IN_PLAY,
	/* already hosting a game, or in another's lobby */
	SYSTEM_LINK_ANSWER_IN_LOBBY,
	/* no network (the game's own check: no address) */
	SYSTEM_LINK_ANSWER_NO_NETWORK,
	/* the menus would not open (the game said why) */
	SYSTEM_LINK_ANSWER_UNAVAILABLE,
};

/* halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE] */
enum
{
	/* the main loop has not run yet */
	SYSTEM_LINK_STATE_STARTING,
	/* a level, campaign or split screen */
	SYSTEM_LINK_STATE_PLAYING,
	/* the menus, no network game */
	SYSTEM_LINK_STATE_MENUS,
	/* the System Link list, looking for games */
	SYSTEM_LINK_STATE_SEARCHING,
	/* joining the game picked in the list */
	SYSTEM_LINK_STATE_JOINING,
	/* this machine's game's lobby (it hosts) */
	SYSTEM_LINK_STATE_HOSTING,
	/* another machine's game's lobby */
	SYSTEM_LINK_STATE_LOBBY,
	/* a network game being played */
	SYSTEM_LINK_STATE_IN_GAME,
};

enum
{
	SYSTEM_LINK_STATUS_STATE,
	/* the machines and players of the network game (lobby or played) */
	SYSTEM_LINK_STATUS_MACHINES,
	SYSTEM_LINK_STATUS_PLAYERS,
	/* the games the System Link list shows (SEARCHING) */
	SYSTEM_LINK_STATUS_GAMES,
	/* nonzero if this machine hosts the network game played (IN_GAME) */
	SYSTEM_LINK_STATUS_HOST,
	/* this machine's system link address (network byte order; 0: none),
	as other machines reach it */
	SYSTEM_LINK_STATUS_ADDRESS,
	/* the most players the network game takes (its host's Max players; 2
	in co-op) */
	SYSTEM_LINK_STATUS_MAXIMUM,
	SYSTEM_LINK_STATUS_COUNT
};

/* a line of text the game's menus want typed (OpenCE's multiplayer screens,
port/linux/game/menu_functions.c: a lobby name, a password, a code): the
game fills halo_text_input_title, _text (the text to start from),
_maximum and _password and sets halo_text_input_state to REQUESTED; the
Vita's host opens the system's keyboard on it (vita_ime.c), the game
getting no buttons meanwhile, and answers DONE (the text typed in
halo_text_input_text, printable ASCII) or CANCELLED. Elsewhere the game
answers itself (HALO_TEST_TEXT_INPUT) */
enum
{
	HALO_TEXT_INPUT_IDLE,
	HALO_TEXT_INPUT_REQUESTED,
	HALO_TEXT_INPUT_OPEN,
	HALO_TEXT_INPUT_DONE,
	HALO_TEXT_INPUT_CANCELLED,
	HALO_TEXT_INPUT_SIZE = 64,
};

extern volatile int halo_text_input_state;
/* nonzero when the game's menus ask for the settings panel (the Halo 3 main
menu's Start, "port settings panel"): the Vita's host opens it and clears it */
extern volatile int halo_settings_panel_request;
extern char halo_text_input_title[HALO_TEXT_INPUT_SIZE];
extern char halo_text_input_text[HALO_TEXT_INPUT_SIZE];
extern int halo_text_input_maximum;
extern int halo_text_input_password;

/* whether the game's Multiplayer menu has OpenCE's screens (menu_tags.c,
set as ui.map loads): 1 yes (Halo PC's bitmaps.map and loc.map are there),
-1 no (the Xbox's), 0 not known yet */
extern volatile int halo_pc_menus_state;

extern volatile int halo_system_link_request;
extern volatile int halo_system_link_answer;
extern volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];

/* the connection the game plays over (port/linux/game/network_connection.c):
the settings panel's Connection (HALO_VITA_NETWORK), which the Halo 3
lobbies' NETWORK sets too, switched live at the menus. The game publishes
what it runs (halo_network_connection: HALO_NETWORK_*, -1 before its first
frame) and how it goes (halo_network_phase), which the panel shows as its
own (running_network) */
enum
{
	HALO_NETWORK_WIFI,
	HALO_NETWORK_ADHOC,
	HALO_NETWORK_ONLINE,
};

enum
{
	/* set up (Online: a broker reached) */
	HALO_NETWORK_PHASE_READY,
	/* Online: internet play started, no broker reached yet */
	HALO_NETWORK_PHASE_CONNECTING,
	/* the setting changed; the switch waits for the menus with no network
	game */
	HALO_NETWORK_PHASE_WAITING,
	/* Online: no broker reached (the setting went back to Same Wi-Fi) */
	HALO_NETWORK_PHASE_FAILED,
};

extern volatile int halo_network_connection;
extern volatile int halo_network_phase;

/* ad hoc play's group (network_connection.c, menu_functions.c: the lobbies'
AD HOC and AD HOC CO-OP). halo_adhoc_request: the game sets it, the Vita's
host takes it and sets it back to HALO_ADHOC_REQUEST_NONE -
- JOIN: the ad hoc room's group joined through the system's dialog, in the
  settings panel's room and dialog way, as the panel's Host and Join do
  before System Link (with the connection Ad hoc, outside a group);
- LEAVE: the group left (the connection left Ad hoc: the Vita's Wi-Fi back).
halo_adhoc_group: the host's word on the group, every frame. Elsewhere (no
Vita host) the game answers itself (system_link_shortcut.c: an emulated
group, HALO_NET_ADHOC_EMULATE, is in it from the start; HALO_ADHOC_TEST,
test builds, answers a join) */
enum
{
	HALO_ADHOC_REQUEST_NONE,
	HALO_ADHOC_REQUEST_JOIN,
	HALO_ADHOC_REQUEST_LEAVE,
};

enum
{
	/* the connection is not Ad hoc */
	HALO_ADHOC_GROUP_OFF = -2,
	/* the system's dialog did not join one (cancelled, or failed) */
	HALO_ADHOC_GROUP_FAILED = -1,
	HALO_ADHOC_GROUP_NONE = 0,
	/* the system's dialog is up */
	HALO_ADHOC_GROUP_JOINING = 1,
	HALO_ADHOC_GROUP_IN = 2,
};

extern volatile int halo_adhoc_request;
extern volatile int halo_adhoc_group;

#endif
