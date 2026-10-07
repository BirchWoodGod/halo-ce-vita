/*
SYSTEM_LINK_SHORTCUT.H

The Vita settings panel's Host a game and Join a game, and what its
Multiplayer tab says about the network game (port/vita/host/vita_settings.c;
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
};

enum
{
	SYSTEM_LINK_ANSWER_NONE,
	/* the System Link screen is open, at its first step (press A to join) */
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
	SYSTEM_LINK_STATUS_COUNT
};

extern volatile int halo_system_link_request;
extern volatile int halo_system_link_answer;
extern volatile int halo_multiplayer_status[SYSTEM_LINK_STATUS_COUNT];

#endif
