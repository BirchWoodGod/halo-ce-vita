/*
VOICE_AUDIO.C

Voice chat's audio (voice_link.h): the microphone, the Opus codec and
playback. The game's side (port/linux/game/voice.c) sends and receives the
frames; nothing here touches the network.

- The microphone is SDL's recording device at 16 kHz mono (on the Vita
  SDL's own sceAudioIn port, SCE_AUDIO_IN_PORT_TYPE_VOICE: the built-in
  microphone, or a headset's), opened only while this machine may send
  (push to talk held, or open mic on) and closed a second after: nothing is
  recorded otherwise. With push to talk, what was recorded while the button
  was held is sent and nothing after; with open mic, a 20 ms frame is sent
  when it is louder than the level chosen (and for VOICE_OPEN_HANG_FRAMES
  after, so words' ends are not cut).
- The codec (libopus, port/third_party/opus: 16 kbps, VoIP, in-band
  redundancy for 10% loss) runs on a thread of its own, never the game's:
  on the Vita the fourth core when Fourth core helpers is All async and the
  system allows it (vita_fourth_core.c), else wherever the system puts it,
  below the game's threads' priority. Complexity 1 on the Vita (an encode
  costs about 2 ms of a Cortex-A9 for 20 ms of voice), 5 elsewhere
  (HALO_VOICE_COMPLEXITY). It sleeps while voice is not in use.
- Each talker's frames go through a jitter buffer (voice_protocol.c) into a
  ring of decoded samples, which the sound mixer (dsound_sdl.c) adds to
  its mix, at the volume chosen, before its limiter, so voice is part of
  the game's sound (and of HALO_AUDIO_DUMP's). A missing frame is
  concealed, from the next frame's redundancy when it has come.
- (debug) HALO_TEST_VOICE_MIC stands in for the microphone: "tone" (a
  440 Hz tone, "tone:HZ" another), or a WAV file (16-bit PCM), played in a
  loop at the pace a microphone would give it, for automated tests on
  machines with none (run_netns_online_test.sh voice).
- halo.log has, every ten seconds while voice is in use and something
  happened, what was encoded and played, with the codec's time a frame.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "voice_link.h"
#include "../game/voice_protocol.h"

#include <SDL3/SDL.h>
#include <opus.h>

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- constants */

enum
{
	/* frames this machine encoded waiting for the game to send them (the
	oldest dropped past it: the game is not sending) */
	OUTBOX_FRAMES = 16,
	/* frames from the network waiting for the codec thread */
	INBOX_FRAMES = 96,
	/* a talker's decoded samples (16 kHz): the ring, and how much the
	codec keeps ahead of the mixer */
	PLAYOUT_RING_SAMPLES = 4096,
	PLAYOUT_TARGET_SAMPLES = 2 * VOICE_FRAME_SAMPLES,
	/* open mic: frames sent after the voice falls under the level */
	VOICE_OPEN_HANG_FRAMES = 20,
	/* the microphone closed this long after sending stops */
	MICROPHONE_CLOSE_MILLISECONDS = 1000,
	/* the codec thread's pace while voice is in use */
	CODEC_PERIOD_MILLISECONDS = 5,
	/* halo.log's statistics */
	STATISTICS_MILLISECONDS = 10000,
	/* the codec thread's stack (Opus's scratch space is on it: voice's
	encoding and decoding run in 32 KB, measured; SDL's calls have the rest) */
	CODEC_STACK_BYTES = 128 * 1024,
};

/* ---------- structures */

struct encoded_frame
{
	unsigned short sequence;
	unsigned char size;
	unsigned char data[VOICE_MAXIMUM_FRAME_BYTES];
};

struct received_frame
{
	unsigned char talker;
	unsigned char size;
	unsigned short sequence;
	unsigned char data[VOICE_MAXIMUM_FRAME_BYTES];
};

struct talker
{
	/* (the codec thread's) */
	struct voice_jitter jitter;
	OpusDecoder *decoder;
	int active;
	unsigned long played;
	unsigned long concealed;
	unsigned long recovered;
	/* the decoded samples' energy since the last statistics (their level) */
	double energy;
	unsigned long energy_samples;
	/* the ring: the codec thread writes, the mixer reads */
	short ring[PLAYOUT_RING_SAMPLES];
	volatile unsigned int written;
	volatile unsigned int read;
	/* the game let the talker go: the mixer drops what the ring holds */
	volatile int flush;
	/* (the mixer's) the samples interpolated between: 16 kHz to 48 */
	float previous;
	float current;
	int phase;
};

/* ---------- prototypes */

#ifdef HALO_VITA
int vita_host_fourth_core_join(const char *role, int level) __attribute__((weak));
#endif

/* ---------- globals */

volatile int halo_voice_status[HALO_VOICE_STATUS_COUNT];
volatile int halo_voice_talk_held;

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	int started;

	/* what the game asks (voice_audio_update) */
	volatile int active;
	volatile int capture;
	volatile int open_mic;
	volatile int level_dbfs;
	volatile int volume;

	/* (under lock) */
	struct encoded_frame outbox[OUTBOX_FRAMES];
	int outbox_first;
	int outbox_count;
	struct received_frame inbox[INBOX_FRAMES];
	int inbox_first;
	int inbox_count;
	unsigned long inbox_dropped;
	volatile int forget[VOICE_MAXIMUM_PLAYERS];

	/* (the codec thread's) */
	OpusEncoder *encoder;
	SDL_AudioStream *microphone;
	int microphone_paused;
	unsigned long long microphone_idle_since;
	short capture_buffer[VOICE_FRAME_SAMPLES];
	int capture_count;
	int capturing;
	int open_hang;
	unsigned short sequence;
	int complexity;
	/* the test microphone */
	int test_checked;
	int test_tone_hz;
	short *test_samples;
	unsigned long test_sample_count;
	unsigned long test_position;
	unsigned long long test_started;
	unsigned long long test_given;

	struct talker talkers[VOICE_MAXIMUM_PLAYERS];

	volatile int sending;
	volatile int level;

	/* statistics */
	unsigned long encoded;
	unsigned long encoded_bytes;
	unsigned long long encode_ns;
	unsigned long long encode_worst_ns;
	unsigned long decoded;
	unsigned long long decode_ns;
	unsigned long long decode_worst_ns;
	unsigned long long statistics_time;
	unsigned long outbox_dropped;
} voice_audio = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* ---------- private code */

static unsigned long long now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000000ULL + (unsigned long long)now.tv_nsec;
}

static unsigned long long now_ms(void)
{
	return SDL_GetTicks();
}

static int frame_level(const short *samples, int count)
{
	double energy = 0.0;
	int index;

	for (index = 0; index < count; index++)
		energy += (double)samples[index] * samples[index];
	energy /= count > 0 ? count : 1;
	if (energy < 1.0)
		return -99;
	return (int)floor(10.0 * log10(energy / (32768.0 * 32768.0)));
}

/* (debug) HALO_TEST_VOICE_MIC: a tone, or a WAV file's samples as 16 kHz
mono */
static void test_microphone_read(void)
{
	const char *setting = getenv("HALO_TEST_VOICE_MIC");

	voice_audio.test_checked = 1;
	if (!setting || !*setting)
		return;
	if (!strncmp(setting, "tone", 4))
	{
		voice_audio.test_tone_hz = setting[4] == ':' ? atoi(setting + 5) : 440;
		if (voice_audio.test_tone_hz <= 0 || voice_audio.test_tone_hz >= VOICE_SAMPLE_RATE / 2)
			voice_audio.test_tone_hz = 440;
		platform_log("voice: the microphone is a %d Hz tone (HALO_TEST_VOICE_MIC)", voice_audio.test_tone_hz);
		return;
	}
	{
		FILE *file = fopen(setting, "rb");
		unsigned char header[12];
		unsigned long rate = 0, channels = 0, bits = 0;

		if (!file || fread(header, 1, 12, file) != 12 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4))
		{
			platform_log("voice: HALO_TEST_VOICE_MIC %s is not a WAV file; the microphone is a tone", setting);
			voice_audio.test_tone_hz = 440;
			if (file)
				fclose(file);
			return;
		}
		for (;;)
		{
			unsigned char chunk[8];
			unsigned long size;

			if (fread(chunk, 1, 8, file) != 8)
				break;
			size = chunk[4] | chunk[5] << 8 | (unsigned long)chunk[6] << 16 | (unsigned long)chunk[7] << 24;
			if (!memcmp(chunk, "fmt ", 4) && size >= 16)
			{
				unsigned char format[16];

				if (fread(format, 1, 16, file) != 16)
					break;
				channels = format[2] | format[3] << 8;
				rate = format[4] | format[5] << 8 | (unsigned long)format[6] << 16 | (unsigned long)format[7] << 24;
				bits = format[14] | format[15] << 8;
				fseek(file, (long)(size - 16 + (size & 1)), SEEK_CUR);
			}
			else if (!memcmp(chunk, "data", 4) && bits == 16 && channels >= 1 && channels <= 8 && rate >= 8000 &&
				rate <= 192000 && size <= 64UL * 1024 * 1024)
			{
				short *raw = malloc(size);
				unsigned long frames, index, count;

				if (!raw || fread(raw, 1, size, file) != size)
				{
					free(raw);
					break;
				}
				frames = size / (2 * channels);
				count = (unsigned long)((unsigned long long)frames * VOICE_SAMPLE_RATE / rate);
				voice_audio.test_samples = malloc(sizeof(short) * (count ? count : 1));
				for (index = 0; voice_audio.test_samples && index < count; index++)
					voice_audio.test_samples[index] = raw[(unsigned long)((unsigned long long)index * rate /
						VOICE_SAMPLE_RATE) * channels];
				voice_audio.test_sample_count = voice_audio.test_samples ? count : 0;
				free(raw);
				break;
			}
			else
			{
				fseek(file, (long)(size + (size & 1)), SEEK_CUR);
			}
		}
		fclose(file);
		if (voice_audio.test_sample_count)
			platform_log("voice: the microphone is %s (%lu ms, HALO_TEST_VOICE_MIC)", setting,
				voice_audio.test_sample_count * 1000 / VOICE_SAMPLE_RATE);
		else
		{
			platform_log("voice: HALO_TEST_VOICE_MIC %s has no 16-bit samples; the microphone is a tone", setting);
			voice_audio.test_tone_hz = 440;
		}
	}
}

static int test_microphone(void)
{
	if (!voice_audio.test_checked)
		test_microphone_read();
	return voice_audio.test_tone_hz || voice_audio.test_sample_count;
}

/* the test microphone's samples due since it started, up to maximum */
static int test_microphone_samples(short *samples, int maximum)
{
	unsigned long long due;
	int count = 0;

	if (!voice_audio.test_started)
	{
		voice_audio.test_started = now_ms();
		voice_audio.test_given = 0;
	}
	due = (now_ms() - voice_audio.test_started) * VOICE_SAMPLE_RATE / 1000;
	while (voice_audio.test_given < due && count < maximum)
	{
		if (voice_audio.test_sample_count)
		{
			samples[count] = voice_audio.test_samples[voice_audio.test_position++ % voice_audio.test_sample_count];
		}
		else
		{
			/* (-12 dBFS, its loudness rising and falling as a voice's) */
			double t = (double)voice_audio.test_position++ / VOICE_SAMPLE_RATE;
			double envelope = 0.6 + 0.4 * sin(2.0 * 3.14159265358979 * 3.0 * t);

			samples[count] = (short)(8000.0 * envelope * sin(2.0 * 3.14159265358979 * voice_audio.test_tone_hz * t));
		}
		count++;
		voice_audio.test_given++;
	}
	return count;
}

#ifdef HALO_ANDROID
/* (the Android guest reaches SDL through the host's imports, which have no
recording device: no microphone there, others' voices played all the same) */
#define SDL_GetAudioStreamData(stream, data, length) ((void)(stream), (void)(data), (void)(length), 0)
#define SDL_ClearAudioStream(stream) ((void)(stream), true)
#define SDL_PauseAudioStreamDevice(stream) ((void)(stream), true)
#define SDL_DestroyAudioStream(stream) ((void)(stream))
#define SDL_OpenAudioDeviceStream(device, spec, callback, userdata) \
	((void)(device), (void)(spec), (void)(callback), (void)(userdata), (SDL_AudioStream *)NULL)
#endif

static void microphone_close(void)
{
	if (voice_audio.microphone)
	{
		SDL_DestroyAudioStream(voice_audio.microphone);
		voice_audio.microphone = NULL;
		platform_log("voice: the microphone closed");
	}
	voice_audio.test_started = 0;
}

/* the microphone open and recording; 0 if there is none */
static int microphone_open(void)
{
	SDL_AudioSpec spec;

	if (test_microphone())
		return 1;
	if (voice_audio.microphone)
	{
		if (voice_audio.microphone_paused)
		{
			SDL_ClearAudioStream(voice_audio.microphone);
			SDL_ResumeAudioStreamDevice(voice_audio.microphone);
			voice_audio.microphone_paused = 0;
		}
		return 1;
	}
	{
		static unsigned long long failed_at;

		/* (none, or refused: asked again at most every 5 s) */
		if (failed_at && now_ms() - failed_at < 5000)
			return 0;
		if (!platform_sdl_initialize())
		{
			failed_at = now_ms();
			return 0;
		}
		spec.format = SDL_AUDIO_S16;
		spec.channels = 1;
		spec.freq = VOICE_SAMPLE_RATE;
		voice_audio.microphone = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
		if (!voice_audio.microphone)
		{
			if (!failed_at)
				platform_log("voice: no microphone (%s)", SDL_GetError());
			failed_at = now_ms();
			return 0;
		}
		failed_at = 0;
	}
	SDL_ResumeAudioStreamDevice(voice_audio.microphone);
	voice_audio.microphone_paused = 0;
	platform_log("voice: the microphone is open");
	return 1;
}

/* recording stopped: what the device held is dropped */
static void microphone_pause(void)
{
	if (voice_audio.microphone && !voice_audio.microphone_paused)
	{
		SDL_PauseAudioStreamDevice(voice_audio.microphone);
		SDL_ClearAudioStream(voice_audio.microphone);
		voice_audio.microphone_paused = 1;
	}
	voice_audio.test_started = 0;
}

static int microphone_samples(short *samples, int maximum)
{
	int bytes;

	if (test_microphone())
		return test_microphone_samples(samples, maximum);
	if (!voice_audio.microphone)
		return 0;
	bytes = SDL_GetAudioStreamData(voice_audio.microphone, samples, maximum * (int)sizeof(short));
	return bytes > 0 ? bytes / (int)sizeof(short) : 0;
}

static int encoder_ready(void)
{
	int error = 0;

	if (voice_audio.encoder)
		return 1;
	voice_audio.encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
	if (!voice_audio.encoder)
	{
		platform_log("voice: the encoder could not be made (%d)", error);
		return 0;
	}
	{
		const char *setting = getenv("HALO_VOICE_COMPLEXITY");

#ifdef HALO_VITA
		voice_audio.complexity = 1;
#else
		voice_audio.complexity = 5;
#endif
		if (setting && *setting)
			voice_audio.complexity = atoi(setting) < 0 ? 0 : atoi(setting) > 10 ? 10 : atoi(setting);
	}
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_BITRATE(VOICE_BITRATE));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_COMPLEXITY(voice_audio.complexity));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_VBR_CONSTRAINT(1));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	opus_encoder_ctl(voice_audio.encoder, OPUS_SET_DTX(0));
	platform_log("voice: Opus 1.6.1 (fixed point), 16 kHz mono, %d kbps, complexity %d, in-band FEC",
		VOICE_BITRATE / 1000, voice_audio.complexity);
	return 1;
}

/* a frame of the microphone's encoded and queued for the game */
static void encode_frame(const short *samples)
{
	struct encoded_frame frame;
	unsigned long long started = now_ns(), spent;
	int size;

	if (!encoder_ready())
		return;
	size = opus_encode(voice_audio.encoder, samples, VOICE_FRAME_SAMPLES, frame.data, VOICE_ENCODE_MAXIMUM_BYTES);
	spent = now_ns() - started;
	voice_audio.encode_ns += spent;
	if (spent > voice_audio.encode_worst_ns)
		voice_audio.encode_worst_ns = spent;
	/* (a frame the encoder made of nothing but its TOC, or a shape voice
	does not send, is not sent) */
	if (size < 2 || !voice_opus_packet_valid(frame.data, size))
		return;
	frame.size = (unsigned char)size;
	frame.sequence = voice_audio.sequence++;
	voice_audio.encoded++;
	voice_audio.encoded_bytes += (unsigned long)size;
	pthread_mutex_lock(&voice_audio.lock);
	if (voice_audio.outbox_count == OUTBOX_FRAMES)
	{
		voice_audio.outbox_first = (voice_audio.outbox_first + 1) % OUTBOX_FRAMES;
		voice_audio.outbox_count--;
		voice_audio.outbox_dropped++;
	}
	voice_audio.outbox[(voice_audio.outbox_first + voice_audio.outbox_count++) % OUTBOX_FRAMES] = frame;
	pthread_mutex_unlock(&voice_audio.lock);
}

/* the microphone's samples, while this machine may send */
static void capture_update(void)
{
	int capture = __atomic_load_n(&voice_audio.capture, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&voice_audio.active, __ATOMIC_ACQUIRE);

	if (!capture)
	{
		if (voice_audio.capturing)
		{
			short samples[VOICE_FRAME_SAMPLES];
			int count;

			/* (push to talk let go: what was recorded while it was held is
			sent, the last frame filled out with silence, and nothing more) */
			while ((count = microphone_samples(samples, VOICE_FRAME_SAMPLES - voice_audio.capture_count)) > 0)
			{
				memcpy(voice_audio.capture_buffer + voice_audio.capture_count, samples, sizeof(short) * (size_t)count);
				voice_audio.capture_count += count;
				if (voice_audio.capture_count == VOICE_FRAME_SAMPLES)
				{
					if (!voice_audio.open_mic)
						encode_frame(voice_audio.capture_buffer);
					voice_audio.capture_count = 0;
				}
			}
			if (voice_audio.capture_count && !voice_audio.open_mic)
			{
				memset(voice_audio.capture_buffer + voice_audio.capture_count, 0,
					sizeof(short) * (size_t)(VOICE_FRAME_SAMPLES - voice_audio.capture_count));
				encode_frame(voice_audio.capture_buffer);
			}
			voice_audio.capture_count = 0;
			voice_audio.capturing = 0;
			voice_audio.open_hang = 0;
			microphone_pause();
			voice_audio.microphone_idle_since = now_ms();
			__atomic_store_n(&voice_audio.sending, 0, __ATOMIC_RELEASE);
			__atomic_store_n(&voice_audio.level, -99, __ATOMIC_RELEASE);
		}
		if (voice_audio.microphone && now_ms() - voice_audio.microphone_idle_since >= MICROPHONE_CLOSE_MILLISECONDS)
			microphone_close();
		return;
	}
	if (!voice_audio.capturing)
	{
		if (!microphone_open())
			return;
		voice_audio.capturing = 1;
		voice_audio.capture_count = 0;
		voice_audio.open_hang = 0;
		/* (a new talk spurt: the encoder starts afresh) */
		if (voice_audio.encoder)
			opus_encoder_ctl(voice_audio.encoder, OPUS_RESET_STATE);
		__atomic_store_n(&voice_audio.sending, !voice_audio.open_mic, __ATOMIC_RELEASE);
	}
	for (;;)
	{
		int count = microphone_samples(voice_audio.capture_buffer + voice_audio.capture_count,
			VOICE_FRAME_SAMPLES - voice_audio.capture_count);

		if (count <= 0)
			break;
		voice_audio.capture_count += count;
		if (voice_audio.capture_count < VOICE_FRAME_SAMPLES)
			continue;
		voice_audio.capture_count = 0;
		{
			int level = frame_level(voice_audio.capture_buffer, VOICE_FRAME_SAMPLES);
			int send = 1;

			__atomic_store_n(&voice_audio.level, level, __ATOMIC_RELEASE);
			if (__atomic_load_n(&voice_audio.open_mic, __ATOMIC_ACQUIRE))
			{
				if (level >= __atomic_load_n(&voice_audio.level_dbfs, __ATOMIC_ACQUIRE))
					voice_audio.open_hang = VOICE_OPEN_HANG_FRAMES;
				else if (voice_audio.open_hang > 0)
					voice_audio.open_hang--;
				send = voice_audio.open_hang > 0;
				if (send && !voice_audio.sending && voice_audio.encoder)
					opus_encoder_ctl(voice_audio.encoder, OPUS_RESET_STATE);
				__atomic_store_n(&voice_audio.sending, send, __ATOMIC_RELEASE);
			}
			if (send)
				encode_frame(voice_audio.capture_buffer);
		}
	}
}

static unsigned int ring_fill(const struct talker *talker)
{
	return __atomic_load_n(&talker->written, __ATOMIC_ACQUIRE) - __atomic_load_n(&talker->read, __ATOMIC_ACQUIRE);
}

/* a talker's 20 ms decoded into its ring (data NULL: concealed) */
static void decode_into(int index, const unsigned char *data, int size, int fec)
{
	struct talker *talker = &voice_audio.talkers[index];
	short samples[VOICE_FRAME_SAMPLES];
	unsigned long long started;
	unsigned int written;
	int count, sample;

	if (!talker->decoder)
	{
		int error = 0;

		talker->decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
		if (!talker->decoder)
			return;
	}
	started = now_ns();
	count = opus_decode(talker->decoder, data, data ? size : 0, samples, VOICE_FRAME_SAMPLES, fec);
	{
		unsigned long long spent = now_ns() - started;

		voice_audio.decode_ns += spent;
		if (spent > voice_audio.decode_worst_ns)
			voice_audio.decode_worst_ns = spent;
		voice_audio.decoded++;
	}
	if (count != VOICE_FRAME_SAMPLES)
		return;
	written = talker->written;
	if (written - __atomic_load_n(&talker->read, __ATOMIC_ACQUIRE) + VOICE_FRAME_SAMPLES > PLAYOUT_RING_SAMPLES)
		return;
	for (sample = 0; sample < VOICE_FRAME_SAMPLES; sample++)
	{
		talker->ring[(written + (unsigned int)sample) % PLAYOUT_RING_SAMPLES] = samples[sample];
		talker->energy += (double)samples[sample] * samples[sample];
	}
	talker->energy_samples += VOICE_FRAME_SAMPLES;
	__atomic_store_n(&talker->written, written + VOICE_FRAME_SAMPLES, __ATOMIC_RELEASE);
}

/* the talkers' frames played: kept PLAYOUT_TARGET_SAMPLES ahead of the
mixer */
static void playout_update(void)
{
	unsigned int now = (unsigned int)now_ms();
	int index;

	for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
	{
		struct talker *talker = &voice_audio.talkers[index];

		if (__atomic_exchange_n(&voice_audio.forget[index], 0, __ATOMIC_ACQ_REL))
		{
			voice_jitter_reset(&talker->jitter);
			if (talker->decoder)
				opus_decoder_ctl(talker->decoder, OPUS_RESET_STATE);
			__atomic_store_n(&talker->flush, 1, __ATOMIC_RELEASE);
			talker->active = 0;
			continue;
		}
		if (!talker->jitter.started)
			continue;
		while (ring_fill(talker) < PLAYOUT_TARGET_SAMPLES)
		{
			unsigned char data[VOICE_MAXIMUM_FRAME_BYTES];
			int size = 0;
			int result = voice_jitter_take(&talker->jitter, now, data, &size);

			if (result == _voice_jitter_frame)
			{
				if (!talker->active && talker->decoder)
					opus_decoder_ctl(talker->decoder, OPUS_RESET_STATE);
				talker->active = 1;
				decode_into(index, data, size, 0);
				talker->played++;
			}
			else if (result == _voice_jitter_lost)
			{
				/* (the next frame's redundancy, else the decoder's guess) */
				decode_into(index, size ? data : NULL, size, size != 0);
				talker->concealed++;
				talker->recovered += size != 0;
			}
			else
			{
				talker->active = 0;
				break;
			}
		}
	}
}

static void statistics_update(int force)
{
	unsigned long long now = now_ms();
	int index;

	if (!voice_audio.statistics_time)
		voice_audio.statistics_time = now;
	if (!force && now - voice_audio.statistics_time < STATISTICS_MILLISECONDS)
		return;
	if (voice_audio.encoded || voice_audio.decoded)
	{
		platform_log("voice: encoded %lu frames (%lu bytes, %.2f ms a frame, worst %.2f; %lu not sent), decoded %lu "
			"(%.3f ms a frame, worst %.2f) over %.1f s", voice_audio.encoded, voice_audio.encoded_bytes,
			voice_audio.encoded ? (double)voice_audio.encode_ns / 1e6 / voice_audio.encoded : 0.0,
			(double)voice_audio.encode_worst_ns / 1e6, voice_audio.outbox_dropped, voice_audio.decoded,
			voice_audio.decoded ? (double)voice_audio.decode_ns / 1e6 / voice_audio.decoded : 0.0,
			(double)voice_audio.decode_worst_ns / 1e6, (double)(now - voice_audio.statistics_time) / 1000.0);
	}
	for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
	{
		struct talker *talker = &voice_audio.talkers[index];
		struct voice_jitter *jitter = &talker->jitter;

		if (!talker->played && !talker->concealed && !jitter->late && !jitter->duplicates)
			continue;
		platform_log("voice: talker %d played %lu frames, concealed %lu (%lu from redundancy), late %lu, "
			"repeated %lu, skipped %lu, level %d dBFS", index, talker->played, talker->concealed, talker->recovered,
			(unsigned long)jitter->late, (unsigned long)jitter->duplicates, (unsigned long)jitter->skipped,
			talker->energy_samples && talker->energy >= talker->energy_samples ?
				(int)floor(10.0 * log10(talker->energy / talker->energy_samples / (32768.0 * 32768.0))) : -99);
		talker->played = talker->concealed = talker->recovered = 0;
		talker->energy = 0.0;
		talker->energy_samples = 0;
		jitter->late = jitter->duplicates = jitter->skipped = 0;
	}
	voice_audio.encoded = voice_audio.encoded_bytes = voice_audio.decoded = voice_audio.outbox_dropped = 0;
	voice_audio.encode_ns = voice_audio.encode_worst_ns = voice_audio.decode_ns = voice_audio.decode_worst_ns = 0;
	voice_audio.statistics_time = now;
}

static void *codec_thread(void *parameter)
{
	int was_active = 0;

	(void)parameter;
#ifdef HALO_VITA
	if (vita_host_fourth_core_join)
		vita_host_fourth_core_join("voice codec", 2);
#endif
	for (;;)
	{
		int active;
		int index;

		pthread_mutex_lock(&voice_audio.lock);
		while (!voice_audio.active && !voice_audio.capturing && !voice_audio.inbox_count && !voice_audio.microphone)
		{
			if (was_active)
			{
				was_active = 0;
				pthread_mutex_unlock(&voice_audio.lock);
				statistics_update(1);
				pthread_mutex_lock(&voice_audio.lock);
				continue;
			}
			pthread_cond_wait(&voice_audio.wake, &voice_audio.lock);
		}
		active = voice_audio.active;
		/* (the frames that came, into their talkers' jitter buffers) */
		while (voice_audio.inbox_count)
		{
			struct received_frame *frame = &voice_audio.inbox[voice_audio.inbox_first];

			voice_audio.inbox_first = (voice_audio.inbox_first + 1) % INBOX_FRAMES;
			voice_audio.inbox_count--;
			if (active && frame->talker < VOICE_MAXIMUM_PLAYERS)
				voice_jitter_put(&voice_audio.talkers[frame->talker].jitter, frame->sequence, frame->data,
					frame->size, (unsigned int)now_ms());
		}
		pthread_mutex_unlock(&voice_audio.lock);

		if (!active)
		{
			/* (voice let go: every talker too) */
			for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
				if (voice_audio.talkers[index].jitter.started || voice_audio.talkers[index].active)
					__atomic_store_n(&voice_audio.forget[index], 1, __ATOMIC_RELEASE);
		}
		was_active |= active;
		capture_update();
		playout_update();
		statistics_update(0);
		SDL_Delay(CODEC_PERIOD_MILLISECONDS);
	}
	return NULL;
}

static void codec_start(void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (voice_audio.started)
		return;
	voice_audio.started = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, CODEC_STACK_BYTES);
	if (pthread_create(&thread, &attributes, codec_thread, NULL) != 0)
		platform_log("voice: the codec thread could not start");
	else
		pthread_detach(thread);
	pthread_attr_destroy(&attributes);
}

/* ---------- public code */

void voice_audio_update(
	int active,
	int capture,
	int open_mic,
	int level_dbfs,
	int volume)
{
	int changed;

	if (!active && !voice_audio.started)
		return;
	codec_start();
	changed = active != voice_audio.active || (capture && active) != voice_audio.capture;
	__atomic_store_n(&voice_audio.open_mic, open_mic != 0, __ATOMIC_RELEASE);
	__atomic_store_n(&voice_audio.level_dbfs, level_dbfs, __ATOMIC_RELEASE);
	__atomic_store_n(&voice_audio.volume, volume < 0 ? 0 : volume > 100 ? 100 : volume, __ATOMIC_RELEASE);
	__atomic_store_n(&voice_audio.capture, capture && active, __ATOMIC_RELEASE);
	if (changed)
	{
		pthread_mutex_lock(&voice_audio.lock);
		voice_audio.active = active != 0;
		if (!active)
		{
			voice_audio.outbox_count = 0;
			voice_audio.inbox_count = 0;
		}
		pthread_cond_signal(&voice_audio.wake);
		pthread_mutex_unlock(&voice_audio.lock);
	}
}

int voice_audio_take_frame(
	unsigned char *data,
	int *size,
	unsigned short *sequence)
{
	int taken = 0;

	if (!voice_audio.started)
		return 0;
	pthread_mutex_lock(&voice_audio.lock);
	if (voice_audio.outbox_count)
	{
		struct encoded_frame *frame = &voice_audio.outbox[voice_audio.outbox_first];

		memcpy(data, frame->data, frame->size);
		*size = frame->size;
		*sequence = frame->sequence;
		voice_audio.outbox_first = (voice_audio.outbox_first + 1) % OUTBOX_FRAMES;
		voice_audio.outbox_count--;
		taken = 1;
	}
	pthread_mutex_unlock(&voice_audio.lock);
	return taken;
}

void voice_audio_play_frame(
	int talker,
	unsigned short sequence,
	const unsigned char *data,
	int size)
{
	if (!voice_audio.started || talker < 0 || talker >= VOICE_MAXIMUM_PLAYERS || size < 1 ||
		size > VOICE_MAXIMUM_FRAME_BYTES || !voice_opus_packet_valid(data, size))
	{
		return;
	}
	pthread_mutex_lock(&voice_audio.lock);
	if (voice_audio.active)
	{
		struct received_frame *frame;

		if (voice_audio.inbox_count == INBOX_FRAMES)
		{
			voice_audio.inbox_first = (voice_audio.inbox_first + 1) % INBOX_FRAMES;
			voice_audio.inbox_count--;
			voice_audio.inbox_dropped++;
		}
		frame = &voice_audio.inbox[(voice_audio.inbox_first + voice_audio.inbox_count++) % INBOX_FRAMES];
		frame->talker = (unsigned char)talker;
		frame->sequence = sequence;
		frame->size = (unsigned char)size;
		memcpy(frame->data, data, (size_t)size);
		pthread_cond_signal(&voice_audio.wake);
	}
	pthread_mutex_unlock(&voice_audio.lock);
}

void voice_audio_forget_talker(
	int talker)
{
	if (voice_audio.started && talker >= 0 && talker < VOICE_MAXIMUM_PLAYERS)
		__atomic_store_n(&voice_audio.forget[talker], 1, __ATOMIC_RELEASE);
}

int voice_audio_sending(void)
{
	return __atomic_load_n(&voice_audio.sending, __ATOMIC_ACQUIRE);
}

int voice_audio_level(void)
{
	return __atomic_load_n(&voice_audio.level, __ATOMIC_ACQUIRE);
}

void voice_audio_mix(
	float *output,
	unsigned long frames)
{
	float gain;
	int index;

	if (!voice_audio.started)
		return;
	/* (80, the default, plays a voice as it came; 100 a quarter louder) */
	gain = (float)__atomic_load_n(&voice_audio.volume, __ATOMIC_ACQUIRE) / 80.0f / 32768.0f;
	for (index = 0; index < VOICE_MAXIMUM_PLAYERS; index++)
	{
		struct talker *talker = &voice_audio.talkers[index];
		unsigned int read = talker->read;
		unsigned int written = __atomic_load_n(&talker->written, __ATOMIC_ACQUIRE);
		unsigned long frame;

		if (__atomic_load_n(&talker->flush, __ATOMIC_ACQUIRE))
		{
			read = written;
			talker->previous = talker->current = 0.0f;
			talker->phase = 0;
			__atomic_store_n(&talker->read, read, __ATOMIC_RELEASE);
			__atomic_store_n(&talker->flush, 0, __ATOMIC_RELEASE);
			continue;
		}
		if (read == written && talker->phase == 0)
			continue;
		/* (16 kHz to 48: each sample three times, a straight line between
		one and the next) */
		for (frame = 0; frame < frames; frame++)
		{
			float value;

			if (talker->phase == 0)
			{
				if (read == written)
					break;
				talker->previous = talker->current;
				talker->current = (float)talker->ring[read % PLAYOUT_RING_SAMPLES];
				read++;
			}
			value = (talker->previous + (talker->current - talker->previous) * (float)talker->phase / 3.0f) * gain;
			talker->phase = (talker->phase + 1) % 3;
			output[frame * 2] += value;
			output[frame * 2 + 1] += value;
		}
		__atomic_store_n(&talker->read, read, __ATOMIC_RELEASE);
	}
}
