/*
NET_FUZZ_VOICE.C

A fuzz target for voice chat's rules (port/linux/game/voice_protocol.c) and
its decoder: a machine's frames to the host and the host's relay as they
come off the wire (_distributed_message_voice and _voice_relay's entries,
after the distributed netcode's header), the jitter buffer and the talkers
over any steps and any clock, and the Opus decoder (port/third_party/opus)
fed every packet the checks pass. What is read must be what may be used:
a message read whole or not at all, each frame of its count, its size
within VOICE_MAXIMUM_FRAME_BYTES, its flags known, an Opus packet of one 20
ms mono frame, the same bytes again when written; the jitter buffer never
holds more than its slots nor gives a frame of more bytes than a frame may
have; the talkers never more than VOICE_MAXIMUM_TALKERS; the decoder gives a
frame of 320 samples or an error, never more.
The sending, relaying and playing (voice.c, voice_audio.c) are not here.

An input is a list of records, each a kind, a 16-bit length and that many
bytes:
  0  a machine's message: its count, then its entries
  1  a relay of the host's: its count, then its entries
  2  the jitter buffer: steps of five bytes (put or take, the sequence's two
     bytes, the size, the clock's step), on one buffer
  3  the talkers: steps of two bytes (the player, the clock's step)
  4  a packet for the decoder (decoded if the checks pass it, then lost and
     recovered from it)
Built as net_fuzz_chat.c is, with the codec, by run_net_fuzz_test.sh.
*/

#include "../../linux/game/voice_protocol.c"

#include <opus.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static OpusDecoder *fuzz_decoder;

static void fuzz_frames(const unsigned char *data, int size, int maximum)
{
	struct voice_frame frames[VOICE_MAXIMUM_RELAY_FRAMES];
	unsigned char written[VOICE_MAXIMUM_RELAY_FRAMES * (VOICE_FRAME_HEADER_BYTES + VOICE_MAXIMUM_FRAME_BYTES)];
	int count, index, length = 0;

	if (size < 1)
		return;
	count = voice_frames_read(data + 1, data + size, data[0], frames, maximum);
	if (count < 0)
		return;
	if (count != data[0] || count > maximum)
		abort();
	for (index = 0; index < count; index++)
	{
		int bytes;

		if (frames[index].size < 1 || frames[index].size > VOICE_MAXIMUM_FRAME_BYTES ||
			(frames[index].flags & ~VOICE_VALID_FLAGS) || !voice_opus_packet_valid(frames[index].data, frames[index].size))
		{
			abort();
		}
		bytes = voice_frame_write(written + length, (int)sizeof(written) - length, &frames[index]);
		if (bytes != VOICE_FRAME_HEADER_BYTES + frames[index].size)
			abort();
		length += bytes;
	}
	/* (read whole: what was read, written again, is the message) */
	if (length != size - 1 || memcmp(written, data + 1, (size_t)length))
		abort();
}

static void fuzz_jitter(const unsigned char *data, int size)
{
	struct voice_jitter jitter;
	uint32_t now = 0;
	int offset;

	voice_jitter_reset(&jitter);
	for (offset = 0; offset + 5 <= size; offset += 5)
	{
		uint16_t sequence = (uint16_t)(data[offset + 1] | data[offset + 2] << 8);
		int frame_size = data[offset + 3];

		now += (uint32_t)data[offset + 4] * 3u;
		if (data[offset] & 1)
		{
			unsigned char frame[VOICE_MAXIMUM_FRAME_BYTES + 1];

			memset(frame, 9 << 3, sizeof(frame));
			voice_jitter_put(&jitter, sequence, frame, frame_size, now);
		}
		else
		{
			unsigned char out[VOICE_MAXIMUM_FRAME_BYTES];
			int out_size = -1;
			int result = voice_jitter_take(&jitter, now, out, &out_size);

			if (out_size < 0 || out_size > VOICE_MAXIMUM_FRAME_BYTES)
				abort();
			if (result == _voice_jitter_frame && out_size < 1)
				abort();
			if (result == _voice_jitter_none && out_size)
				abort();
		}
		if (voice_jitter_count(&jitter) > VOICE_JITTER_SLOTS)
			abort();
	}
}

static void fuzz_talkers(const unsigned char *data, int size)
{
	struct voice_talkers talkers;
	uint32_t now = 0;
	int offset;

	voice_talkers_reset(&talkers);
	for (offset = 0; offset + 2 <= size; offset += 2)
	{
		now += (uint32_t)data[offset + 1] * 7u;
		voice_talkers_admit(&talkers, (int8_t)data[offset], now);
		if (voice_talkers_count(&talkers, now) > VOICE_MAXIMUM_TALKERS)
			abort();
	}
}

static void fuzz_decode(const unsigned char *data, int size)
{
	opus_int16 pcm[VOICE_FRAME_SAMPLES * 2];
	int result;

	if (!voice_opus_packet_valid(data, size))
		return;
	if (!fuzz_decoder)
	{
		int error = 0;

		fuzz_decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
		if (!fuzz_decoder)
			abort();
	}
	/* (what the codec thread does: the frame, then as a lost frame's
	redundancy, then a lost frame's concealment) */
	result = opus_decode(fuzz_decoder, data, size, pcm, VOICE_FRAME_SAMPLES, 0);
	if (result != VOICE_FRAME_SAMPLES && result >= 0)
		abort();
	result = opus_decode(fuzz_decoder, data, size, pcm, VOICE_FRAME_SAMPLES, 1);
	if (result != VOICE_FRAME_SAMPLES && result >= 0)
		abort();
	result = opus_decode(fuzz_decoder, NULL, 0, pcm, VOICE_FRAME_SAMPLES, 0);
	if (result != VOICE_FRAME_SAMPLES)
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t offset = 0;

	while (offset + 3 <= size)
	{
		int kind = data[offset] % 5;
		int length = data[offset + 1] | (data[offset + 2] << 8);

		offset += 3;
		if ((size_t)length > size - offset)
			length = (int)(size - offset);
		switch (kind)
		{
		case 0: fuzz_frames(data + offset, length, VOICE_MAXIMUM_FRAMES_PER_MESSAGE); break;
		case 1: fuzz_frames(data + offset, length, VOICE_MAXIMUM_RELAY_FRAMES); break;
		case 2: fuzz_jitter(data + offset, length); break;
		case 3: fuzz_talkers(data + offset, length); break;
		default: fuzz_decode(data + offset, length); break;
		}
		offset += (size_t)length;
	}
	return 0;
}

/* (NET_FUZZ_SEED_DIR set: a record of each kind written there as seeds) */
void net_fuzz_write_seed(char const *folder, char const *name, void const *data, unsigned long size);

int net_fuzz_checks(void)
{
	char const *folder = getenv("NET_FUZZ_SEED_DIR");
	unsigned char seed[1024];
	int failures = 0;
	int length, index;

	if (!folder)
		return 0;
	/* (a message of two frames, and a relay of three, from a real encoder's
	shape: SILK wideband 20 ms TOCs) */
	for (index = 0; index < 2; index++)
	{
		int frames = index ? 3 : 2, frame;

		seed[0] = (unsigned char)index;
		length = 3;
		seed[length++] = (unsigned char)frames;
		for (frame = 0; frame < frames; frame++)
		{
			struct voice_frame entry;
			int byte;

			memset(&entry, 0, sizeof(entry));
			entry.who = (uint8_t)(index ? frame + 1 : 0);
			entry.flags = (uint8_t)(frame == 1);
			entry.sequence = (uint16_t)(65535 + frame);
			entry.size = (uint8_t)(28 + frame * 5);
			entry.data[0] = 9 << 3;
			for (byte = 1; byte < entry.size; byte++)
				entry.data[byte] = (uint8_t)(byte * 37 + frame);
			length += voice_frame_write(seed + length, (int)sizeof(seed) - length, &entry);
		}
		seed[1] = (unsigned char)((length - 3) & 0xFF);
		seed[2] = (unsigned char)((length - 3) >> 8);
		net_fuzz_write_seed(folder, index ? "relay" : "frames", seed, (unsigned long)length);
	}
	{
		static unsigned char const jitter[] =
		{
			2, 40, 0,
			1, 10, 0, 30, 10,
			1, 12, 0, 30, 10,
			1, 11, 0, 30, 10,
			0, 0, 0, 0, 30,
			0, 0, 0, 0, 7,
			1, 9, 0, 30, 1,
			1, 255, 255, 80, 1,
			0, 0, 0, 0, 7,
		};
		static unsigned char const talkers[] =
		{
			3, 14, 0,
			0, 1, 1, 1, 2, 1, 3, 1, 4, 1, 15, 200, 16, 1,
		};

		net_fuzz_write_seed(folder, "jitter", jitter, sizeof(jitter));
		net_fuzz_write_seed(folder, "talkers", talkers, sizeof(talkers));
	}
	{
		/* (a real frame of the encoder as voice chat sets it up) */
		int error = 0;
		OpusEncoder *encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
		opus_int16 pcm[VOICE_FRAME_SAMPLES];
		int size = 0, frame;

		if (!encoder)
			return 1;
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(VOICE_BITRATE));
		opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
		opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
		for (frame = 0; frame < 5; frame++)
		{
			for (index = 0; index < VOICE_FRAME_SAMPLES; index++)
				pcm[index] = (opus_int16)((index * 97 + frame * 13) % 4000 - 2000);
			size = opus_encode(encoder, pcm, VOICE_FRAME_SAMPLES, seed + 3, VOICE_ENCODE_MAXIMUM_BYTES);
		}
		opus_encoder_destroy(encoder);
		if (size <= 0 || !voice_opus_packet_valid(seed + 3, size))
			failures++;
		seed[0] = 4;
		seed[1] = (unsigned char)size;
		seed[2] = 0;
		net_fuzz_write_seed(folder, "packet", seed, (unsigned long)(3 + (size > 0 ? size : 0)));
	}
	return failures;
}
