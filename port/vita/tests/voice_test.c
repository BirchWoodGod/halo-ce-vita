/*
VOICE_TEST.C

Desktop test of voice chat's rules (port/linux/game/voice_protocol.c) and
codec settings: the Opus packets accepted (one 20 ms mono frame, nothing
else), messages' frames as every field from the wire could be (counts,
sizes, flags, bytes left over or missing), the jitter buffer (order, loss
and its concealment from the next frame, late and repeated frames, a talker
that stops and starts again, the sequence wrapping), the talkers passed on
at once, a machine's frame limit at a talker's pace, and the codec as voice
chat sets it up (port/linux/src/voice_audio.c: 16 kHz mono VoIP at 16 kbps
with in-band FEC), whose every packet must pass the host's checks, with the
time it takes a frame.
run_voice_test.sh builds it 32-bit, with AddressSanitizer and UBSan, with
the vendored libopus (port/third_party/opus).
*/

#include "voice_protocol.h"

#include <opus.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
static int checks;

#define CHECK(condition) do { checks++; if (!(condition)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void test_packets(void)
{
	uint8_t packet[VOICE_MAXIMUM_FRAME_BYTES + 1];
	int configuration;

	memset(packet, 0x55, sizeof(packet));
	for (configuration = 0; configuration < 32; configuration++)
	{
		int twenty = configuration < 12 ? (configuration & 3) == 1 : configuration < 16 ? (configuration & 1) == 1 :
			(configuration & 3) == 3;

		packet[0] = (uint8_t)(configuration << 3);
		CHECK(voice_opus_packet_valid(packet, 40) == twenty);
		/* (stereo, or more than one frame: never) */
		packet[0] = (uint8_t)(configuration << 3 | 4);
		CHECK(!voice_opus_packet_valid(packet, 40));
		packet[0] = (uint8_t)(configuration << 3 | 1);
		CHECK(!voice_opus_packet_valid(packet, 40));
		packet[0] = (uint8_t)(configuration << 3 | 3);
		CHECK(!voice_opus_packet_valid(packet, 40));
	}
	packet[0] = 9 << 3;
	CHECK(voice_opus_packet_valid(packet, 1));
	CHECK(voice_opus_packet_valid(packet, VOICE_MAXIMUM_FRAME_BYTES));
	CHECK(!voice_opus_packet_valid(packet, VOICE_MAXIMUM_FRAME_BYTES + 1));
	CHECK(!voice_opus_packet_valid(packet, 0));
	CHECK(!voice_opus_packet_valid(packet, -1));
	CHECK(!voice_opus_packet_valid(NULL, 10));
}

static struct voice_frame make_frame(int who, int flags, int sequence, int size)
{
	struct voice_frame frame;
	int index;

	memset(&frame, 0, sizeof(frame));
	frame.who = (uint8_t)who;
	frame.flags = (uint8_t)flags;
	frame.sequence = (uint16_t)sequence;
	frame.size = (uint8_t)size;
	frame.data[0] = 9 << 3;
	for (index = 1; index < size; index++)
		frame.data[index] = (uint8_t)(sequence * 7 + index);
	return frame;
}

static void test_messages(void)
{
	uint8_t message[1024];
	struct voice_frame frames[VOICE_MAXIMUM_RELAY_FRAMES];
	struct voice_frame frame;
	int size = 0, index, written;

	for (index = 0; index < 3; index++)
	{
		frame = make_frame(index, index == 1 ? 1 << _voice_flag_team_bit : 0, 65534 + index, 20 + index * 15);
		written = voice_frame_write(message + size, (int)sizeof(message) - size, &frame);
		CHECK(written == VOICE_FRAME_HEADER_BYTES + frame.size);
		size += written;
	}
	CHECK(voice_frames_read(message, message + size, 3, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == 3);
	CHECK(frames[0].who == 0 && frames[1].who == 1 && frames[2].who == 2);
	CHECK(frames[0].sequence == 65534 && frames[1].sequence == 65535 && frames[2].sequence == 0);
	CHECK(frames[1].flags == 1 && frames[0].flags == 0);
	CHECK(frames[2].size == 50 && !memcmp(frames[2].data, make_frame(2, 0, 0, 50).data, 50));
	/* (the count says more or fewer frames than there are, bytes over or
	short: the whole message dropped) */
	CHECK(voice_frames_read(message, message + size, 4, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size, 2, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size - 1, 3, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size + 1, 3, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size, 0, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size, -1, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message, 1, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message + size, message, 1, frames, VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	/* (more than a message may carry) */
	size = 0;
	for (index = 0; index < VOICE_MAXIMUM_FRAMES_PER_MESSAGE + 1; index++)
	{
		frame = make_frame(0, 0, index, 10);
		size += voice_frame_write(message + size, (int)sizeof(message) - size, &frame);
	}
	CHECK(voice_frames_read(message, message + size, VOICE_MAXIMUM_FRAMES_PER_MESSAGE + 1, frames,
		VOICE_MAXIMUM_FRAMES_PER_MESSAGE) == -1);
	CHECK(voice_frames_read(message, message + size, VOICE_MAXIMUM_FRAMES_PER_MESSAGE + 1, frames,
		VOICE_MAXIMUM_RELAY_FRAMES) == VOICE_MAXIMUM_FRAMES_PER_MESSAGE + 1);
	/* (a field off: a flag not known, a size of 0 or past the most, a frame
	not of voice's shape) */
	frame = make_frame(0, 0, 1, 10);
	size = voice_frame_write(message, (int)sizeof(message), &frame);
	message[1] = 2;
	CHECK(voice_frames_read(message, message + size, 1, frames, 1) == -1);
	message[1] = 0x80;
	CHECK(voice_frames_read(message, message + size, 1, frames, 1) == -1);
	message[1] = 0;
	message[4] = 0;
	CHECK(voice_frames_read(message, message + VOICE_FRAME_HEADER_BYTES, 1, frames, 1) == -1);
	message[4] = VOICE_MAXIMUM_FRAME_BYTES + 1;
	memset(message + VOICE_FRAME_HEADER_BYTES, 9 << 3, VOICE_MAXIMUM_FRAME_BYTES + 1);
	CHECK(voice_frames_read(message, message + VOICE_FRAME_HEADER_BYTES + VOICE_MAXIMUM_FRAME_BYTES + 1, 1, frames, 1) ==
		-1);
	message[4] = 10;
	message[VOICE_FRAME_HEADER_BYTES] = 9 << 3 | 4;
	CHECK(voice_frames_read(message, message + VOICE_FRAME_HEADER_BYTES + 10, 1, frames, 1) == -1);
	message[VOICE_FRAME_HEADER_BYTES] = 8 << 3;
	CHECK(voice_frames_read(message, message + VOICE_FRAME_HEADER_BYTES + 10, 1, frames, 1) == -1);
	message[VOICE_FRAME_HEADER_BYTES] = 9 << 3;
	CHECK(voice_frames_read(message, message + VOICE_FRAME_HEADER_BYTES + 10, 1, frames, 1) == 1);
	/* (the writer refuses what the reader would) */
	frame = make_frame(0, 4, 1, 10);
	CHECK(voice_frame_write(message, (int)sizeof(message), &frame) == 0);
	frame = make_frame(0, 0, 1, 10);
	CHECK(voice_frame_write(message, VOICE_FRAME_HEADER_BYTES + 9, &frame) == 0);
	frame.data[0] = 0x0C;
	CHECK(voice_frame_write(message, (int)sizeof(message), &frame) == 0);
}

static int take(struct voice_jitter *jitter, uint32_t now, int *sequence_out)
{
	uint8_t data[VOICE_MAXIMUM_FRAME_BYTES];
	int size = 0;
	int result = voice_jitter_take(jitter, now, data, &size);

	*sequence_out = size ? data[1] : -1;
	return result;
}

static void put(struct voice_jitter *jitter, int sequence, uint32_t now)
{
	struct voice_frame frame = make_frame(0, 0, sequence, 12);

	/* (byte 1 tells the frame apart: its sequence) */
	frame.data[1] = (uint8_t)sequence;
	voice_jitter_put(jitter, (uint16_t)sequence, frame.data, frame.size, now);
}

static void test_jitter(void)
{
	struct voice_jitter jitter;
	int got, index;

	voice_jitter_reset(&jitter);
	CHECK(take(&jitter, 0, &got) == _voice_jitter_none);
	/* (in order: it waits for VOICE_JITTER_START_FRAMES, then plays them) */
	put(&jitter, 10, 0);
	put(&jitter, 11, 20);
	CHECK(take(&jitter, 20, &got) == _voice_jitter_none);
	put(&jitter, 12, 40);
	CHECK(take(&jitter, 40, &got) == _voice_jitter_frame && got == 10);
	CHECK(take(&jitter, 60, &got) == _voice_jitter_frame && got == 11);
	/* (out of order: put back in order) */
	put(&jitter, 14, 60);
	put(&jitter, 13, 61);
	CHECK(take(&jitter, 80, &got) == _voice_jitter_frame && got == 12);
	CHECK(take(&jitter, 100, &got) == _voice_jitter_frame && got == 13);
	CHECK(take(&jitter, 120, &got) == _voice_jitter_frame && got == 14);
	/* (lost: concealed from the next frame's redundancy) */
	put(&jitter, 16, 130);
	put(&jitter, 17, 131);
	CHECK(take(&jitter, 140, &got) == _voice_jitter_lost && got == 16);
	CHECK(jitter.lost == 1);
	CHECK(take(&jitter, 160, &got) == _voice_jitter_frame && got == 16);
	/* (the lost frame come late, and one already had: dropped) */
	put(&jitter, 15, 161);
	put(&jitter, 17, 162);
	CHECK(jitter.late == 1 && jitter.duplicates == 1);
	CHECK(take(&jitter, 180, &got) == _voice_jitter_frame && got == 17);
	/* (two lost in a row, the second with nothing after it to recover
	from... but one later: concealed twice) */
	put(&jitter, 20, 185);
	CHECK(take(&jitter, 200, &got) == _voice_jitter_lost && got == -1);
	CHECK(take(&jitter, 220, &got) == _voice_jitter_lost && got == 20);
	CHECK(take(&jitter, 240, &got) == _voice_jitter_frame && got == 20);
	/* (the talker stops: nothing more, and it starts again after a wait) */
	CHECK(take(&jitter, 260, &got) == _voice_jitter_none);
	CHECK(!jitter.started);
	put(&jitter, 40, 1000);
	CHECK(take(&jitter, 1000, &got) == _voice_jitter_none);
	CHECK(take(&jitter, 1000 + VOICE_JITTER_START_MILLISECONDS, &got) == _voice_jitter_frame && got == 40);
	/* (far ahead: a new start, what was held dropped) */
	voice_jitter_reset(&jitter);
	put(&jitter, 100, 0);
	put(&jitter, 101, 0);
	put(&jitter, 100 + VOICE_JITTER_SLOTS + 5, 10);
	CHECK(jitter.skipped == 2 && voice_jitter_count(&jitter) == 1 && jitter.next == 100 + VOICE_JITTER_SLOTS + 5);
	/* (an earlier frame before playing starts: the start moves back) */
	voice_jitter_reset(&jitter);
	put(&jitter, 50, 0);
	put(&jitter, 49, 1);
	put(&jitter, 51, 2);
	CHECK(take(&jitter, 2, &got) == _voice_jitter_frame && got == 49);
	/* (the sequence wraps) */
	voice_jitter_reset(&jitter);
	put(&jitter, 65534, 0);
	put(&jitter, 65535, 0);
	put(&jitter, 0, 0);
	put(&jitter, 1, 0);
	CHECK(take(&jitter, 0, &got) == _voice_jitter_frame && got == (65534 & 0xFF));
	CHECK(take(&jitter, 20, &got) == _voice_jitter_frame && got == 0xFF);
	CHECK(take(&jitter, 40, &got) == _voice_jitter_frame && got == 0);
	CHECK(take(&jitter, 60, &got) == _voice_jitter_frame && got == 1);
	/* (any order of any sequences: never more than the slots, never a
	frame given twice or older than one given) */
	voice_jitter_reset(&jitter);
	{
		unsigned seed = 7;
		int last = -1, given = 0;

		for (index = 0; index < 20000; index++)
		{
			int result;

			seed = seed * 1103515245u + 12345u;
			put(&jitter, (int)((index / 2 + (int)((seed >> 16) % 9) - 4) & 0xFFFF), (uint32_t)index * 10);
			CHECK(voice_jitter_count(&jitter) <= VOICE_JITTER_SLOTS);
			if (index % 2)
			{
				uint8_t data[VOICE_MAXIMUM_FRAME_BYTES];
				int size;

				result = voice_jitter_take(&jitter, (uint32_t)index * 10, data, &size);
				if (result == _voice_jitter_frame)
				{
					given++;
					if (last >= 0 && jitter.playing)
						CHECK(data[1] != (uint8_t)last || size == 0);
					last = data[1];
				}
			}
		}
		CHECK(given > 1000);
	}
}

static void test_talkers(void)
{
	struct voice_talkers talkers;
	int player;

	voice_talkers_reset(&talkers);
	for (player = 0; player < VOICE_MAXIMUM_TALKERS; player++)
		CHECK(voice_talkers_admit(&talkers, player, 1000));
	CHECK(voice_talkers_count(&talkers, 1000) == VOICE_MAXIMUM_TALKERS);
	/* (one more waits; those talking keep their places) */
	CHECK(!voice_talkers_admit(&talkers, VOICE_MAXIMUM_TALKERS, 1100));
	CHECK(voice_talkers_admit(&talkers, 0, 1200));
	/* (a talker quiet past the hold gives its place up) */
	CHECK(voice_talkers_admit(&talkers, VOICE_MAXIMUM_TALKERS, 1000 + VOICE_TALKER_HOLD_MILLISECONDS + 1));
	CHECK(voice_talkers_count(&talkers, 1000 + VOICE_TALKER_HOLD_MILLISECONDS + 1) == 2);
	/* (no player past the game's) */
	CHECK(!voice_talkers_admit(&talkers, -1, 5000));
	CHECK(!voice_talkers_admit(&talkers, VOICE_MAXIMUM_PLAYERS, 5000));
	CHECK(!voice_talkers_admit(&talkers, 255, 5000));
}

static void test_limits(void)
{
	struct chat_bucket bucket;
	int taken = 0, index;

	/* (a talker's 50 frames a second, with jitter, all pass) */
	memset(&bucket, 0, sizeof(bucket));
	for (index = 0; index < 500; index++)
	{
		uint32_t now = 10000u + (uint32_t)index * 20u + (uint32_t)((index * 37) % 15);

		taken += chat_bucket_take(&bucket, now, VOICE_MACHINE_BURST, VOICE_MACHINE_REFILL_MILLISECONDS);
	}
	CHECK(taken == 500);
	/* (a burst of 40 at once after a hitch: up to the burst) */
	taken = 0;
	for (index = 0; index < 40; index++)
		taken += chat_bucket_take(&bucket, 30000u, VOICE_MACHINE_BURST, VOICE_MACHINE_REFILL_MILLISECONDS);
	CHECK(taken <= VOICE_MACHINE_BURST);
	/* (a flood: no more than the pace) */
	memset(&bucket, 0, sizeof(bucket));
	taken = 0;
	for (index = 0; index < 10000; index++)
		taken += chat_bucket_take(&bucket, 50000u + (uint32_t)index / 10u, VOICE_MACHINE_BURST,
			VOICE_MACHINE_REFILL_MILLISECONDS);
	CHECK(taken <= VOICE_MACHINE_BURST + 1000 / VOICE_MACHINE_REFILL_MILLISECONDS + 1);
	/* (the most a machine can make the host send on, a second: its frames
	at their biggest) */
	CHECK((VOICE_MACHINE_BURST + 1000 / VOICE_MACHINE_REFILL_MILLISECONDS) * (VOICE_FRAME_HEADER_BYTES +
		VOICE_MAXIMUM_FRAME_BYTES) * 8 / 1000 < 64);
}

static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void test_codec(void)
{
	int error = 0;
	OpusEncoder *encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
	OpusDecoder *decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
	short pcm[VOICE_FRAME_SAMPLES], out[VOICE_FRAME_SAMPLES];
	uint8_t packet[VOICE_MAXIMUM_FRAME_BYTES];
	long bytes = 0, frames = 0, valid = 0;
	double encode = 0.0, decode = 0.0, energy_in = 0.0, energy_out = 0.0;
	int complexity, frame, sample;

	CHECK(encoder && decoder);
	if (!encoder || !decoder)
		return;
	for (complexity = 0; complexity <= 10; complexity += 5)
	{
		opus_encoder_ctl(encoder, OPUS_RESET_STATE);
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(VOICE_BITRATE));
		opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(complexity < 10 ? complexity : 1));
		opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
		opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
		opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(1));
		opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
		opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
		opus_encoder_ctl(encoder, OPUS_SET_DTX(0));
		bytes = frames = valid = 0;
		encode = decode = energy_in = energy_out = 0.0;
		for (frame = 0; frame < 500; frame++)
		{
			double started;
			int size, decoded;

			for (sample = 0; sample < VOICE_FRAME_SAMPLES; sample++)
			{
				double t = (double)(frame * VOICE_FRAME_SAMPLES + sample) / VOICE_SAMPLE_RATE;

				pcm[sample] = (short)(8000.0 * (0.6 + 0.4 * sin(2.0 * M_PI * 3.0 * t)) * sin(2.0 * M_PI * 440.0 * t));
				energy_in += (double)pcm[sample] * pcm[sample];
			}
			started = now_ms();
			size = opus_encode(encoder, pcm, VOICE_FRAME_SAMPLES, packet, VOICE_ENCODE_MAXIMUM_BYTES);
			encode += now_ms() - started;
			CHECK(size > 2 && size <= VOICE_ENCODE_MAXIMUM_BYTES);
			valid += voice_opus_packet_valid(packet, size);
			bytes += size;
			frames++;
			started = now_ms();
			/* (every tenth lost: concealed, or recovered from the next) */
			decoded = frame % 10 == 5 ? opus_decode(decoder, NULL, 0, out, VOICE_FRAME_SAMPLES, 0) :
				opus_decode(decoder, packet, size, out, VOICE_FRAME_SAMPLES, 0);
			decode += now_ms() - started;
			CHECK(decoded == VOICE_FRAME_SAMPLES);
			for (sample = 0; sample < VOICE_FRAME_SAMPLES && frame >= 10; sample++)
				energy_out += (double)out[sample] * out[sample];
		}
		/* (every packet of the encoder as voice chat sets it up passes the
		host's checks, near the bitrate asked, and the tone comes back at
		about its level) */
		CHECK(valid == frames);
		CHECK(bytes * 8 / (frames * VOICE_FRAME_MILLISECONDS) <= VOICE_BITRATE / 1000 + 4);
		CHECK(energy_out > energy_in * 0.25 && energy_out < energy_in * 2.0);
		printf("  codec, complexity %d: %.1f bytes a frame (%.1f kbps), encode %.3f ms, decode %.3f ms a frame "
			"(this machine)\n", complexity < 10 ? complexity : 1, (double)bytes / frames,
			bytes * 8.0 / (frames * VOICE_FRAME_MILLISECONDS), encode / frames, decode / frames);
	}
	/* (the decoder fed what the checks pass, junk after a valid TOC: it
	decodes or refuses, within its frame, never past it) */
	{
		unsigned seed = 99;
		int round;

		for (round = 0; round < 20000; round++)
		{
			int size = 1 + (int)((seed >> 16) % VOICE_MAXIMUM_FRAME_BYTES);
			int index, result;

			for (index = 0; index < size; index++)
			{
				seed = seed * 1103515245u + 12345u;
				packet[index] = (uint8_t)(seed >> 16);
			}
			if (!voice_opus_packet_valid(packet, size))
				continue;
			memset(out, 0, sizeof(out));
			result = opus_decode(decoder, packet, size, out, VOICE_FRAME_SAMPLES, 0);
			CHECK(result == VOICE_FRAME_SAMPLES || result < 0);
		}
	}
	opus_encoder_destroy(encoder);
	opus_decoder_destroy(decoder);
}

int main(void)
{
	test_packets();
	test_messages();
	test_jitter();
	test_talkers();
	test_limits();
	test_codec();
	printf("%s voice_test: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
