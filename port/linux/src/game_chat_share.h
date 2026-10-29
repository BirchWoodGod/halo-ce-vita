/*
GAME_CHAT_SHARE.H

What the in-game chat (port/linux/game/game_chat.c) and the Vita's settings
panel (port/vita/host/vita_settings.c: SELECT + D-pad left, the Chat page)
share: plain ints and ASCII names only, the panel's side being another ABI.

- halo_game_chat_active: nonzero while this machine plays a network game
  (or one with a party: the party's channel), so SELECT + D-pad left is
  the chat's (its quick chat), not the Party page's.
- The players of the game, for the Chat page's mutes, published by the
  game's main loop twice a second: halo_chat_players_count -1 with no
  network game, the serial odd while they are written; each name ASCII and
  ended, and whether this machine mutes it.
- halo_chat_mute_request: the panel's (the index + 1 of a player it showed,
  | 0x100 to mute, 0 none), with the name it showed in
  halo_chat_mute_request_name (the game checks it is still that player's);
  the game sets it back to 0.
*/

#ifndef __HALO_LINUX_GAME_CHAT_SHARE_H
#define __HALO_LINUX_GAME_CHAT_SHARE_H

enum
{
	HALO_CHAT_PLAYERS_MAXIMUM = 16,
	HALO_CHAT_PLAYER_NAME_SIZE = 16,
	HALO_CHAT_MUTE_REQUEST_MUTE = 0x100,
};

extern volatile int halo_game_chat_active;
extern volatile int halo_chat_players_serial;
extern volatile int halo_chat_players_count;
extern volatile int halo_chat_players_muted[HALO_CHAT_PLAYERS_MAXIMUM];
/* (the player of this machine, never muted: marked) */
extern volatile int halo_chat_players_local[HALO_CHAT_PLAYERS_MAXIMUM];
extern char halo_chat_players_names[HALO_CHAT_PLAYERS_MAXIMUM][HALO_CHAT_PLAYER_NAME_SIZE];
extern volatile int halo_chat_mute_request;
extern char halo_chat_mute_request_name[HALO_CHAT_PLAYER_NAME_SIZE];

#endif
