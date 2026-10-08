/*
VOICE_PROTOCOL.H

The rules of voice chat (voice.c, voice_audio.c), testable and fuzzable
without the game (port/vita/tests/voice_test.c, net_fuzz_voice.c), as
chat_protocol.h's are for game chat.

A player's voice goes to the host in 20 ms Opus frames (16 kHz, mono), a
few at a time in an unreliable message of the distributed netcode
(_distributed_message_voice, network_distributed.h), and the host, which
names who said them from that machine's own players (a joiner never names
anyone), passes each on (_distributed_message_voice_relay) to every other
machine, or to those with a player of the sender's team. Nothing of voice
travels on a reliable connection: a frame lost is lost, a late one dropped,
and nothing waits for one.

Every field from the network is checked here before use, on the host and
again on every machine that plays it (a host is a stranger too):
- a message's frames are counted (at most VOICE_MAXIMUM_FRAMES_PER_MESSAGE
  from a machine, VOICE_MAXIMUM_RELAY_FRAMES from the host) and each is
  read no further than the message's end; a message with anything wrong in
  it (a count past its frames, bytes left over, a flag not known, a frame
  of no bytes or of more than VOICE_MAXIMUM_FRAME_BYTES) is dropped whole;
- a frame is an Opus packet of exactly one 20 ms mono frame (its first
  byte, the TOC: code 0, stereo off, a 20 ms configuration), nothing else,
  so the decoder is only ever given a bounded packet of the shape voice
  chat sends;
- a machine's frames are limited (chat_bucket: VOICE_MACHINE_BURST at once,
  then one every VOICE_MACHINE_REFILL_MILLISECONDS: a talker's 50 a second
  and some jitter), and so are the host's relaying as a whole and what a
  machine plays of its host's;
- at most VOICE_MAXIMUM_TALKERS players are passed on at once (the first to
  talk; voice_talkers), so a crowd cannot make the host send more than that.

The jitter buffer (voice_jitter) orders a talker's frames by their
sequence number, holds VOICE_JITTER_START_FRAMES before playing, drops a
frame later than the one playing or one already had, and says when a frame
is missing (the decoder then conceals it, from the next frame's redundancy
when it has it: Opus's in-band FEC).
*/

#ifndef __VOICE_PROTOCOL_H
#define __VOICE_PROTOCOL_H

#include <stdint.h>

#include "chat_protocol.h"

/* ---------- constants */

/* the codec's: 16 kHz mono, 20 ms frames */
#define VOICE_SAMPLE_RATE 16000
#define VOICE_FRAME_SAMPLES 320
#define VOICE_FRAME_MILLISECONDS 20
/* the most bytes a frame may have (the encoder is held to
VOICE_ENCODE_MAXIMUM_BYTES: 16 kbps is 40) */
#define VOICE_MAXIMUM_FRAME_BYTES 80
#define VOICE_ENCODE_MAXIMUM_BYTES 60
/* the bitrate asked of the encoder */
#define VOICE_BITRATE 16000

/* a machine's message to the host: its frames since the last tick (a 30 Hz
tick has one or two; more after a hitch) */
#define VOICE_MAXIMUM_FRAMES_PER_MESSAGE 4
/* a relay of the host's: the talkers' frames since its last tick */
#define VOICE_MAXIMUM_RELAY_FRAMES 12

/* a machine's frames (the host): VOICE_MACHINE_BURST at once, then one
every VOICE_MACHINE_REFILL_MILLISECONDS (a talker sends one every 20) */
#define VOICE_MACHINE_BURST 12
#define VOICE_MACHINE_REFILL_MILLISECONDS 18
/* the host's relaying as a whole: its talkers' together */
#define VOICE_HOST_BURST (VOICE_MACHINE_BURST * VOICE_MAXIMUM_TALKERS)
#define VOICE_HOST_REFILL_MILLISECONDS 4
/* what a machine plays of what its host relays: the same */
#define VOICE_PLAYED_BURST VOICE_HOST_BURST
#define VOICE_PLAYED_REFILL_MILLISECONDS VOICE_HOST_REFILL_MILLISECONDS

/* the players passed on at once, and how long a talker keeps its place
after its last frame */
#define VOICE_MAXIMUM_TALKERS 4
#define VOICE_TALKER_HOLD_MILLISECONDS 400
/* players a machine tells apart (the game's: players.h) */
#define VOICE_MAXIMUM_PLAYERS 16

/* the jitter buffer: frames held, and how many before it plays */
#define VOICE_JITTER_SLOTS 16
#define VOICE_JITTER_START_FRAMES 3
/* (a frame's wait for the start: it plays what it has after this long) */
#define VOICE_JITTER_START_MILLISECONDS 80

/* a frame's flags */
enum
{
	/* to the sender's team only (in a game with teams: else everyone) */
	_voice_flag_team_bit,
	NUMBER_OF_VOICE_FLAGS
};

#define VOICE_VALID_FLAGS ((1 << NUMBER_OF_VOICE_FLAGS) - 1)

/* what a jitter buffer gives for the next 20 ms */
enum
{
	/* nothing to play (not started, or the talker stopped) */
	_voice_jitter_none,
	/* the frame */
	_voice_jitter_frame,
	/* the frame is missing: conceal it (with the next frame's redundancy
	when there is one: fec) */
	_voice_jitter_lost,
};

/* ---------- structures */

/* a frame as it comes off the wire, checked: who (a machine's controller
to the host, the game's player index from it), its flags, sequence and
bytes */
struct voice_frame
{
	uint8_t who;
	uint8_t flags;
	uint16_t sequence;
	uint8_t size;
	uint8_t data[VOICE_MAXIMUM_FRAME_BYTES];
};

/* each frame on the wire: who, flags, the sequence (little endian) and the
size, then the size's bytes */
#define VOICE_FRAME_HEADER_BYTES 5

struct voice_jitter_slot
{
	uint16_t sequence;
	uint8_t present;
	uint8_t size;
	uint8_t data[VOICE_MAXIMUM_FRAME_BYTES];
};

struct voice_jitter
{
	struct voice_jitter_slot slots[VOICE_JITTER_SLOTS];
	/* playing: the next sequence to give */
	uint16_t next;
	uint8_t started;
	uint8_t playing;
	/* (not playing) when its first frame came */
	uint32_t first_milliseconds;
	/* frames given, concealed, dropped late, dropped as had already, and
	those it gave up waiting for (a gap past the slots) */
	uint32_t frames;
	uint32_t lost;
	uint32_t late;
	uint32_t duplicates;
	uint32_t skipped;
};

/* (the host) the players passed on now, the first to talk first */
struct voice_talkers
{
	int16_t player[VOICE_MAXIMUM_TALKERS];
	uint32_t last_milliseconds[VOICE_MAXIMUM_TALKERS];
};

/* ---------- prototypes */

/* whether the bytes are an Opus packet of one 20 ms mono frame (its TOC:
code 0, no stereo, a configuration of 20 ms), 1 to VOICE_MAXIMUM_FRAME_BYTES
long */
int voice_opus_packet_valid(uint8_t const *data, int size);

/* a message's frames (count of them, the bytes from entries to end) read
into frames (maximum of them): their number, or -1 if anything is wrong
(the whole message is then dropped) */
int voice_frames_read(uint8_t const *entries, uint8_t const *end, int count, struct voice_frame *frames, int maximum);
/* a frame onto the end of a message being written (room bytes left): the
bytes it took, 0 if it does not fit or is not valid */
int voice_frame_write(uint8_t *destination, int room, struct voice_frame const *frame);

/* the jitter buffer: emptied; a frame put in (its bytes checked already);
what to play for the next 20 ms into frame (fec: the frame after a lost
one, its redundancy, when it is there) */
void voice_jitter_reset(struct voice_jitter *jitter);
void voice_jitter_put(struct voice_jitter *jitter, uint16_t sequence, uint8_t const *data, int size,
	uint32_t now_milliseconds);
int voice_jitter_take(struct voice_jitter *jitter, uint32_t now_milliseconds, uint8_t *data, int *size);
/* frames held */
int voice_jitter_count(struct voice_jitter const *jitter);

/* (the host) whether the player may be passed on now (a place kept or
free; it is then the player's for VOICE_TALKER_HOLD_MILLISECONDS more) */
void voice_talkers_reset(struct voice_talkers *talkers);
int voice_talkers_admit(struct voice_talkers *talkers, int player, uint32_t now_milliseconds);
/* how many are talking now */
int voice_talkers_count(struct voice_talkers const *talkers, uint32_t now_milliseconds);

#endif
