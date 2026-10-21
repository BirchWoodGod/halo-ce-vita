/*
GAME_CHAT.H

The in-game chat (game_chat.c): what the netcode, the main loop, the
overlays, the controllers and the menus ask of it.
*/

#ifndef __GAME_CHAT_H
#define __GAME_CHAT_H
#pragma once

/* the chat's controls, from the Vita's buttons (vita_pad.c) or an Xbox pad
and the keyboard (xinput_sdl.c) */
enum
{
	GAME_CHAT_PAD_UP = 1 << 0,
	GAME_CHAT_PAD_DOWN = 1 << 1,
	GAME_CHAT_PAD_LEFT = 1 << 2,
	GAME_CHAT_PAD_RIGHT = 1 << 3,
	/* send the phrase chosen (Cross, A) */
	GAME_CHAT_PAD_ACCEPT = 1 << 4,
	/* close (Circle, B) */
	GAME_CHAT_PAD_BACK = 1 << 5,
	/* type a line (Triangle, Y) */
	GAME_CHAT_PAD_TYPE = 1 << 6,
	/* the channel before and after (L and R; the shoulders, the triggers) */
	GAME_CHAT_PAD_PREVIOUS = 1 << 7,
	GAME_CHAT_PAD_NEXT = 1 << 8,
	/* the combo held: SELECT + D-pad left (Back + D-pad left) */
	GAME_CHAT_PAD_COMBO = 1 << 9,
};

/* (network_distributed.c) a message of the chat's kinds (87-89): from a
client machine (machine_index) on the host, from the host (NONE) on a
client; from_stream when it came over a connection's stream, not a
datagram nor a batch. Checked here, the game in progress */
void game_chat_handle_message(long machine_index, int kind, int count, void const *entries, unsigned long size,
	int from_stream);

/* (main.c) every frame: the host's settings sent, the party's lines read, a
typed line taken, the panel's mutes, the tests' script */
void game_chat_update(void);

/* (interface.c, over the whole screen) the lines, the quick chat wheel */
void game_chat_draw(void);

/* the chat's controls this frame (GAME_CHAT_PAD_*), now in milliseconds:
nonzero when the chat takes them (the wheel is open: the game sees none of
the buttons; the sticks stay the game's) */
int game_chat_pad(unsigned int buttons, unsigned long now);

/* whether the chat's wheel is open (its controls the chat's) */
int game_chat_wheel_open(void);

/* (the PLAYERS screen, the settings panel) a player muted on this machine by
name, or unmuted; whether they are */
int game_chat_mute_name(char const *name, int muted);
int game_chat_name_muted(char const *name);

/* a line said, as the wheel or the keyboard says it: the channel
(game_chat_protocol.h GAME_CHAT_CHANNEL_ALL, _TEAM, _PARTY), the text;
nonzero if it went */
int game_chat_say(int channel, char const *text);

#endif
