/*
CHAT_LINK.H

Game chat's menu on the Vita (port/vita/host/vita_settings.c: Back + Y in
a network game's lobby or in the game) and the game's side of chat
(port/linux/game/chat.c), which share these. The two are built with other
compilers and ABIs: plain ints and chars only. Both run on the game's main
thread (the menu in the pad's reading, the game in its main loop).
*/

#ifndef __HALO_LINUX_CHAT_LINK_H
#define __HALO_LINUX_CHAT_LINK_H

enum
{
	/* the players listed for muting (the others of the game; on the Vita
	at most 15 others) */
	HALO_CHAT_PLAYERS = 16,
	HALO_CHAT_NAME_SIZE = 16,
	/* a typed line: 80 characters and its end, rounded up */
	HALO_CHAT_TEXT_SIZE = 84,
	HALO_CHAT_TEXT_LENGTH = 80,
};

/* the settings panel's "Game chat" (HALO_CHAT): on, quick chat only, off */
enum
{
	HALO_CHAT_MODE_ON,
	HALO_CHAT_MODE_QUICK,
	HALO_CHAT_MODE_OFF,
};

/* what the game says of chat now (halo_chat_status), each frame */
enum
{
	/* nonzero in a network game's lobby or in the game (with chat not Off) */
	HALO_CHAT_STATUS_AVAILABLE,
	/* nonzero if the game has teams (team chat) */
	HALO_CHAT_STATUS_TEAMS,
	/* a HALO_CHAT_MODE_* */
	HALO_CHAT_STATUS_MODE,
	/* how many of halo_chat_player_names are set */
	HALO_CHAT_STATUS_PLAYERS,
	/* milliseconds until this machine may send a line (0: now) */
	HALO_CHAT_STATUS_WAIT,
	/* nonzero if this machine hosts the game (its Game chat and its mutes
	are the game's) */
	HALO_CHAT_STATUS_HOST,
	HALO_CHAT_STATUS_COUNT
};

/* what the menu asks of the game (halo_chat_request), which takes it at
its next frame and sets it back to NONE */
enum
{
	HALO_CHAT_REQUEST_NONE,
	/* the phrase halo_chat_request_value */
	HALO_CHAT_REQUEST_QUICK,
	/* the line in halo_chat_request_text */
	HALO_CHAT_REQUEST_TYPED,
	/* the player named in halo_chat_request_text muted, or heard again */
	HALO_CHAT_REQUEST_MUTE,
	HALO_CHAT_REQUEST_UNMUTE,
};

extern volatile int halo_chat_status[HALO_CHAT_STATUS_COUNT];
extern char halo_chat_player_names[HALO_CHAT_PLAYERS][HALO_CHAT_NAME_SIZE];
extern volatile int halo_chat_player_muted[HALO_CHAT_PLAYERS];
extern volatile int halo_chat_request;
extern volatile int halo_chat_request_value;
/* nonzero: the request's line to the sender's team only */
extern volatile int halo_chat_request_team;
extern char halo_chat_request_text[HALO_CHAT_TEXT_SIZE];

/* the quick chat phrases as the menu lists them (chat_protocol.c's, by
their number) */
#define HALO_CHAT_PHRASES "Need backup", "Enemy spotted", "Follow me", "On my way", "Thanks", "Good game", "Yes", "No"
#define HALO_CHAT_PHRASE_COUNT 8

#endif
