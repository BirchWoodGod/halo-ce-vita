/*
VOICE_LINK.H

What voice chat's three parts share: the game's side (port/linux/game/
voice.c: the network, the host's relaying, who is talking), the audio side
(port/linux/src/voice_audio.c: the microphone, the Opus codec on a thread
of its own, playback in the sound mixer) and the controls (the Vita's
push-to-talk, port/vita/host/vita_settings.c; the desktop's, xinput_sdl.c).
They are built with other compilers and ABIs: plain ints and chars only.

The settings (environment variables, the Vita's settings panel's
Multiplayer tab and settings.txt; on a desktop config.toml's network.voice*):
- HALO_VOICE: "ptt" (push to talk, the default), "open" (open mic: sends
  while the microphone hears more than HALO_VOICE_LEVEL), "off" (neither
  sends nor plays). This machine never sends without one of the first two,
  and with push to talk only while the button is held.
- HALO_VOICE_VOLUME: 0 to 100, how loud others are played (80).
- HALO_VOICE_LEVEL: open mic's threshold: "low", "medium" (default),
  "high" (a quiet voice sends at low; at high only a near one).
- HALO_VOICE_HOST: the games this machine hosts: "private" (the default:
  voice in games that are not listed in the public games - system link, ad
  hoc, join by code, private co-op - and none in a listed one), "on", "off".
*/

#ifndef __HALO_LINUX_VOICE_LINK_H
#define __HALO_LINUX_VOICE_LINK_H

/* HALO_VOICE */
enum
{
	HALO_VOICE_MODE_PUSH,
	HALO_VOICE_MODE_OPEN,
	HALO_VOICE_MODE_OFF,
};

/* HALO_VOICE_HOST */
enum
{
	HALO_VOICE_HOST_PRIVATE,
	HALO_VOICE_HOST_ON,
	HALO_VOICE_HOST_OFF,
};

/* what the game says of voice now (halo_voice_status), each frame */
enum
{
	/* nonzero in a network game being played, with voice not Off: the
	push-to-talk combination is voice's (else the buttons are the game's) */
	HALO_VOICE_STATUS_AVAILABLE,
	/* nonzero while this machine sends (the microphone is live) */
	HALO_VOICE_STATUS_SENDING,
	HALO_VOICE_STATUS_COUNT
};

extern volatile int halo_voice_status[HALO_VOICE_STATUS_COUNT];
/* the controls: nonzero while push to talk is held (the Vita's Back + left
trigger, a desktop's V or Back + left trigger); the game reads it each
frame */
extern volatile int halo_voice_talk_held;

/* ---------- the audio side (voice_audio.c), called from the game's main
thread but for voice_audio_mix (the sound mixer's thread) */

/* each frame: whether voice is in use (a game with voice; else the
microphone closes, the codec thread sleeps and the talkers are let go),
whether this machine may send now (push to talk held, or open mic on; the
microphone is open only then), open mic (sends only what is louder than
level_dbfs), the playback volume (0 to 100) */
void voice_audio_update(int active, int capture, int open_mic, int level_dbfs, int volume);
/* a frame this machine encoded, oldest first: 1 with its bytes (size), its
sequence; 0 when none is waiting */
int voice_audio_take_frame(unsigned char *data, int *size, unsigned short *sequence);
/* a talker's frame (checked: voice_protocol.c) for playing; talker 0 to
VOICE_MAXIMUM_PLAYERS - 1 (a player index) */
void voice_audio_play_frame(int talker, unsigned short sequence, const unsigned char *data, int size);
/* a talker let go (left, muted): what it held is dropped */
void voice_audio_forget_talker(int talker);
/* whether this machine is sending now: what the microphone hears passes
open mic's level, or push to talk is held */
int voice_audio_sending(void);
/* (statistics, halo.log) the microphone's level (dBFS, -99 silent) */
int voice_audio_level(void);
/* (the sound mixer, dsound_sdl.c) the talkers into the mix, 48 kHz stereo
float, before its limiter */
void voice_audio_mix(float *output, unsigned long frames);

#endif
