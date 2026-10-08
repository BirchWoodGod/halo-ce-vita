/*
VOICE.H

Voice chat in network games (voice.c; its rules: voice_protocol.h; its
audio: port/linux/src/voice_audio.c).
*/

#ifndef __VOICE_H
#define __VOICE_H
#pragma once

#include "cseries.h"

/* each frame, from the main loop (between ticks, with chat_update): the
settings, push to talk, the test script (HALO_TEST_VOICE), the audio side */
void voice_update(void);

/* who is talking and this machine's microphone, over the game (interface.c's
fullscreen overlays, with chat's lines) */
void voice_draw(void);

/* (network_distributed.c, after each tick, last) this machine's frames to
the host; the host's relaying of the tick's frames to the machines */
void voice_network_tick(void);

/* (network_distributed.c) the least bytes a frame takes in a message */
word voice_minimum_entry_size(void);
/* (the host) a client machine's _distributed_message_voice */
void voice_handle_frames(long machine_index, byte const *entries, byte const *end, short count);
/* (a client) the host's _distributed_message_voice_relay */
void voice_handle_relay(byte const *entries, byte const *end, short count);

#endif // __VOICE_H
