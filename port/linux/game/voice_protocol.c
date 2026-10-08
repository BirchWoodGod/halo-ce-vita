/*
VOICE_PROTOCOL.C

Voice chat's rules, apart from the game (voice_protocol.h).
*/

#include "voice_protocol.h"

#include <string.h>

/* ---------- private code */

/* the sequence's distance past the jitter buffer's next (negative: before
it), wrapping as the 16-bit sequence does */
static int sequence_distance(
	uint16_t sequence,
	uint16_t next)
{
	return (int)(int16_t)(uint16_t)(sequence - next);
}

/* ---------- public code */

int voice_opus_packet_valid(
	uint8_t const *data,
	int size)
{
	int configuration;

	if (!data || size < 1 || size > VOICE_MAXIMUM_FRAME_BYTES)
		return 0;
	/* (code 0: one frame; mono) */
	if ((data[0] & 3) != 0 || (data[0] & 4) != 0)
		return 0;
	configuration = data[0] >> 3;
	/* (20 ms: SILK's configurations 1, 5, 9; the hybrid's 13 and 15;
	CELT's 19, 23, 27, 31) */
	if (configuration < 12)
		return (configuration & 3) == 1;
	if (configuration < 16)
		return (configuration & 1) == 1;
	return (configuration & 3) == 3;
}

int voice_frames_read(
	uint8_t const *entries,
	uint8_t const *end,
	int count,
	struct voice_frame *frames,
	int maximum)
{
	int index;

	if (!entries || !end || end < entries || count <= 0 || count > maximum)
		return -1;
	for (index = 0; index < count; index++)
	{
		struct voice_frame *frame = &frames[index];

		if (end - entries < VOICE_FRAME_HEADER_BYTES)
			return -1;
		frame->who = entries[0];
		frame->flags = entries[1];
		frame->sequence = (uint16_t)(entries[2] | entries[3] << 8);
		frame->size = entries[4];
		entries += VOICE_FRAME_HEADER_BYTES;
		if ((frame->flags & ~VOICE_VALID_FLAGS) || frame->size < 1 || frame->size > VOICE_MAXIMUM_FRAME_BYTES ||
			end - entries < frame->size)
		{
			return -1;
		}
		memcpy(frame->data, entries, frame->size);
		entries += frame->size;
		if (!voice_opus_packet_valid(frame->data, frame->size))
			return -1;
	}
	/* (nothing left over) */
	return entries == end ? count : -1;
}

int voice_frame_write(
	uint8_t *destination,
	int room,
	struct voice_frame const *frame)
{
	int size = VOICE_FRAME_HEADER_BYTES + frame->size;

	if (room < size || (frame->flags & ~VOICE_VALID_FLAGS) || !voice_opus_packet_valid(frame->data, frame->size))
		return 0;
	destination[0] = frame->who;
	destination[1] = frame->flags;
	destination[2] = (uint8_t)(frame->sequence & 0xFF);
	destination[3] = (uint8_t)(frame->sequence >> 8);
	destination[4] = frame->size;
	memcpy(destination + VOICE_FRAME_HEADER_BYTES, frame->data, frame->size);
	return size;
}

void voice_jitter_reset(
	struct voice_jitter *jitter)
{
	memset(jitter, 0, sizeof(*jitter));
}

int voice_jitter_count(
	struct voice_jitter const *jitter)
{
	int count = 0;
	int index;

	for (index = 0; index < VOICE_JITTER_SLOTS; index++)
		count += jitter->slots[index].present != 0;
	return count;
}

/* (a talker's buffer dropped: what it held is counted as skipped, its
statistics kept) */
static void jitter_restart(
	struct voice_jitter *jitter)
{
	int index;

	for (index = 0; index < VOICE_JITTER_SLOTS; index++)
	{
		if (jitter->slots[index].present)
			jitter->skipped++;
		jitter->slots[index].present = 0;
	}
	jitter->started = 0;
	jitter->playing = 0;
}

void voice_jitter_put(
	struct voice_jitter *jitter,
	uint16_t sequence,
	uint8_t const *data,
	int size,
	uint32_t now_milliseconds)
{
	struct voice_jitter_slot *slot;
	int distance;

	if (size < 1 || size > VOICE_MAXIMUM_FRAME_BYTES)
		return;
	if (!jitter->started)
	{
		jitter->started = 1;
		jitter->playing = 0;
		jitter->next = sequence;
		jitter->first_milliseconds = now_milliseconds;
	}
	distance = sequence_distance(sequence, jitter->next);
	if (distance < 0)
	{
		int index;
		int span = 0;

		/* (late, while playing: dropped; before playing, an earlier frame
		come after a later one: the start moves back to it, if what is held
		still fits) */
		if (jitter->playing)
		{
			jitter->late++;
			return;
		}
		for (index = 0; index < VOICE_JITTER_SLOTS; index++)
		{
			if (jitter->slots[index].present)
			{
				int held = sequence_distance(jitter->slots[index].sequence, sequence);

				if (held > span)
					span = held;
			}
		}
		if (span >= VOICE_JITTER_SLOTS)
		{
			jitter->late++;
			return;
		}
		jitter->next = sequence;
		distance = 0;
	}
	else if (distance >= VOICE_JITTER_SLOTS)
	{
		/* (far ahead: the talker started again after a gap - the sequence
		counts on - or this machine missed more than the buffer holds) */
		jitter_restart(jitter);
		jitter->started = 1;
		jitter->next = sequence;
		jitter->first_milliseconds = now_milliseconds;
	}
	slot = &jitter->slots[sequence % VOICE_JITTER_SLOTS];
	if (slot->present && slot->sequence == sequence)
	{
		jitter->duplicates++;
		return;
	}
	slot->present = 1;
	slot->sequence = sequence;
	slot->size = (uint8_t)size;
	memcpy(slot->data, data, (size_t)size);
}

int voice_jitter_take(
	struct voice_jitter *jitter,
	uint32_t now_milliseconds,
	uint8_t *data,
	int *size)
{
	struct voice_jitter_slot *slot;
	int count;

	*size = 0;
	if (!jitter->started)
		return _voice_jitter_none;
	count = voice_jitter_count(jitter);
	if (!jitter->playing)
	{
		if (count < VOICE_JITTER_START_FRAMES &&
			!(count > 0 && now_milliseconds - jitter->first_milliseconds >= VOICE_JITTER_START_MILLISECONDS))
		{
			return _voice_jitter_none;
		}
		jitter->playing = 1;
	}
	slot = &jitter->slots[jitter->next % VOICE_JITTER_SLOTS];
	if (slot->present && slot->sequence == jitter->next)
	{
		memcpy(data, slot->data, slot->size);
		*size = slot->size;
		slot->present = 0;
		jitter->next++;
		jitter->frames++;
		return _voice_jitter_frame;
	}
	/* (nothing more held: the talker stopped, or this machine ran dry; it
	starts again with the next frame, after its own wait) */
	if (!count)
	{
		jitter->started = 0;
		jitter->playing = 0;
		return _voice_jitter_none;
	}
	/* (missing, with later ones held: concealed, from the next one's
	redundancy when it is there) */
	if (slot->present)
	{
		/* (a frame of another sequence in its place: older than next,
		never given; dropped) */
		slot->present = 0;
		jitter->skipped++;
	}
	jitter->next++;
	jitter->lost++;
	slot = &jitter->slots[jitter->next % VOICE_JITTER_SLOTS];
	if (slot->present && slot->sequence == jitter->next)
	{
		memcpy(data, slot->data, slot->size);
		*size = slot->size;
	}
	return _voice_jitter_lost;
}

void voice_talkers_reset(
	struct voice_talkers *talkers)
{
	int index;

	for (index = 0; index < VOICE_MAXIMUM_TALKERS; index++)
	{
		talkers->player[index] = -1;
		talkers->last_milliseconds[index] = 0;
	}
}

int voice_talkers_admit(
	struct voice_talkers *talkers,
	int player,
	uint32_t now_milliseconds)
{
	int index;
	int free_index = -1;

	if (player < 0 || player >= VOICE_MAXIMUM_PLAYERS)
		return 0;
	for (index = 0; index < VOICE_MAXIMUM_TALKERS; index++)
	{
		if (talkers->player[index] == player)
		{
			talkers->last_milliseconds[index] = now_milliseconds;
			return 1;
		}
		if (free_index < 0 && (talkers->player[index] < 0 ||
			now_milliseconds - talkers->last_milliseconds[index] > VOICE_TALKER_HOLD_MILLISECONDS))
		{
			free_index = index;
		}
	}
	if (free_index < 0)
		return 0;
	talkers->player[free_index] = (int16_t)player;
	talkers->last_milliseconds[free_index] = now_milliseconds;
	return 1;
}

int voice_talkers_count(
	struct voice_talkers const *talkers,
	uint32_t now_milliseconds)
{
	int index;
	int count = 0;

	for (index = 0; index < VOICE_MAXIMUM_TALKERS; index++)
	{
		if (talkers->player[index] >= 0 &&
			now_milliseconds - talkers->last_milliseconds[index] <= VOICE_TALKER_HOLD_MILLISECONDS)
		{
			count++;
		}
	}
	return count;
}
