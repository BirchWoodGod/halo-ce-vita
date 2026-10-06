/*
VITA_MOVIE_ASPECT.C

The shape a movie is meant to be shown at, from its MP4 file: the Vita's
video player gives the frame's size in pixels, and an MP4 may store its
pictures squeezed ("anamorphic": 640x480 pixels shown 16:9, as ffmpeg's
-aspect 16:9 writes them). The display size is in the video track's header
(tkhd: the width and height a player shows, 16.16 fixed point), and the
pixels' own shape in the sample description's pasp box. The sample
description also gives the picture's size, which the Vita's decoder pads to
whole 16-pixel macroblocks (a 640x360 movie comes out 640x368). Plain C and
stdio, so the host tests can build it (port/vita/tests).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"

/* (a moov box larger than this is not read: a long movie's sample tables) */
#define MAXIMUM_MOOV_BYTES (8UL * 1024 * 1024)

struct aspect_search
{
	/* the video track's header display size (16.16), the pasp spacing */
	unsigned long display_width, display_height;
	unsigned long track_width, track_height;
	int track_is_video;
	unsigned long pasp_h, pasp_v;
	int found_display, found_pasp;
	/* the visual sample entry's picture size */
	unsigned long picture_width, picture_height;
	int found_picture;
};

static unsigned long read16(const unsigned char *p)
{
	return ((unsigned long)p[0] << 8) | p[1];
}

static unsigned long read32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

/* the boxes in [data, data + size); depth guards against a malformed file */
static void walk_boxes(const unsigned char *data, unsigned long size, struct aspect_search *search, int depth)
{
	unsigned long offset = 0;

	if (depth > 12)
		return;
	while (offset + 8 <= size)
	{
		unsigned long box_size = read32(data + offset);
		const unsigned char *type = data + offset + 4;
		unsigned long header = 8;
		const unsigned char *body;
		unsigned long body_size;

		if (box_size == 1)
		{
			/* (a 64-bit size: only its low word can matter inside moov) */
			if (offset + 16 > size || read32(data + offset + 8))
				return;
			box_size = read32(data + offset + 12);
			header = 16;
		}
		else if (box_size == 0)
			box_size = size - offset;
		if (box_size < header || box_size > size - offset)
			return;
		body = data + offset + header;
		body_size = box_size - header;

		if (!memcmp(type, "trak", 4))
		{
			struct aspect_search track = *search;

			/* each track's header and handler are its own */
			track.track_width = track.track_height = 0;
			track.track_is_video = 0;
			track.found_pasp = 0;
			track.found_picture = 0;
			walk_boxes(body, body_size, &track, depth + 1);
			if (track.track_is_video && !search->found_display && track.track_width && track.track_height)
			{
				search->display_width = track.track_width;
				search->display_height = track.track_height;
				search->found_display = 1;
			}
			if (track.track_is_video && track.found_pasp && !search->found_pasp)
			{
				search->pasp_h = track.pasp_h;
				search->pasp_v = track.pasp_v;
				search->found_pasp = 1;
			}
			if (track.track_is_video && track.found_picture && !search->found_picture)
			{
				search->picture_width = track.picture_width;
				search->picture_height = track.picture_height;
				search->found_picture = 1;
			}
		}
		else if (!memcmp(type, "mdia", 4) || !memcmp(type, "minf", 4) || !memcmp(type, "stbl", 4))
			walk_boxes(body, body_size, search, depth + 1);
		else if (!memcmp(type, "tkhd", 4) && body_size >= 84)
		{
			/* version 1 has 64-bit times: the size is 12 bytes later */
			unsigned long at = body[0] == 1 ? 88 : 76;

			if (body_size >= at + 8)
			{
				search->track_width = read32(body + at);
				search->track_height = read32(body + at + 4);
			}
		}
		else if (!memcmp(type, "hdlr", 4) && body_size >= 12)
			search->track_is_video = !memcmp(body + 8, "vide", 4);
		else if (!memcmp(type, "stsd", 4) && body_size >= 8)
		{
			/* the sample entries; a visual one's boxes start 78 bytes in */
			unsigned long entry = 8;

			if (entry + 8 <= body_size)
			{
				unsigned long entry_size = read32(body + entry);

				if (entry_size >= 8 + 78 && entry_size <= body_size - entry)
				{
					/* (its width and height 24 bytes into the entry) */
					search->picture_width = read16(body + entry + 8 + 24);
					search->picture_height = read16(body + entry + 8 + 26);
					search->found_picture = search->picture_width && search->picture_height;
					walk_boxes(body + entry + 8 + 78, entry_size - 8 - 78, search, depth + 1);
				}
			}
		}
		else if (!memcmp(type, "pasp", 4) && body_size >= 8)
		{
			search->pasp_h = read32(body);
			search->pasp_v = read32(body + 4);
			search->found_pasp = search->pasp_h && search->pasp_v;
		}
		offset += box_size;
	}
}

/* the moov box's video track into search; 0 when the file cannot be read */
static int read_movie(const char *path, struct aspect_search *search)
{
	unsigned char header[16];
	FILE *file;
	long offset = 0;

	memset(search, 0, sizeof(*search));
	file = fopen(path, "rb");
	if (!file)
		return 0;
	/* the top level boxes, to moov (often at the end of the file) */
	while (fseek(file, offset, SEEK_SET) == 0 && fread(header, 1, 8, file) == 8)
	{
		unsigned long box_size = read32(header);
		unsigned long header_size = 8;

		if (box_size == 1)
		{
			if (fread(header + 8, 1, 8, file) != 8 || read32(header + 8))
				break;
			box_size = read32(header + 12);
			header_size = 16;
		}
		if (box_size < header_size)
			break;
		if (!memcmp(header + 4, "moov", 4))
		{
			unsigned long body_size = box_size - header_size;
			unsigned char *body = body_size <= MAXIMUM_MOOV_BYTES ? malloc(body_size) : NULL;

			if (body && fread(body, 1, body_size, file) == body_size)
				walk_boxes(body, body_size, search, 0);
			free(body);
			break;
		}
		offset += (long)box_size;
	}
	fclose(file);
	return 1;
}

float vita_movie_file_aspect(const char *path, unsigned long width, unsigned long height, const char **source)
{
	struct aspect_search search;
	float aspect = 0.0f;

	*source = "its size";
	if (!width || !height)
		return 0.0f;
	if (!read_movie(path, &search))
		return (float)width / (float)height;
	if (search.found_display && search.display_height)
	{
		aspect = (float)search.display_width / (float)search.display_height;
		*source = "the track header";
	}
	else if (search.found_pasp)
	{
		aspect = (float)width * (float)search.pasp_h / ((float)height * (float)search.pasp_v);
		*source = "the pixel aspect";
	}
	if (aspect < 0.5f || aspect > 4.0f)
	{
		aspect = (float)width / (float)height;
		*source = "its size";
	}
	return aspect;
}

float vita_movie_choose_aspect(float file_aspect, const char **source, unsigned long width, unsigned long height,
	float player_aspect)
{
	float pixels = width && height ? (float)width / (float)height : 0.0f;

	/* (issue #8: a 960x544 copy flagged 16:9, setdar=16/9, has a track header
	of 967x544 - 1.778, within 1% of its pixels' 1.765 - and was then shown
	at the player's aspect ratio, 4:3 on the Vita. The track header is the
	file's own word on its shape: the player's is used only without it) */
	if (strcmp(*source, "its size") || !pixels)
		return file_aspect;
	if (player_aspect > 0.5f && player_aspect < 4.0f && (player_aspect < pixels * 0.99f || player_aspect > pixels * 1.01f))
	{
		*source = "the player";
		return player_aspect;
	}
	return file_aspect;
}

int vita_movie_file_picture_size(const char *path, unsigned long *width, unsigned long *height)
{
	struct aspect_search search;

	*width = *height = 0;
	if (!read_movie(path, &search) || !search.found_picture)
		return 0;
	*width = search.picture_width;
	*height = search.picture_height;
	return 1;
}

void vita_movie_visible_size(unsigned long decoded_width, unsigned long decoded_height, unsigned long picture_width,
	unsigned long picture_height, unsigned long *width, unsigned long *height)
{
	/* (only the decoder's padding to whole macroblocks is cut: a picture
	size that does not fit inside the frame, or is 16 or more pixels less,
	says something else - a scaled or anamorphic entry - and is ignored) */
	*width = decoded_width;
	*height = decoded_height;
	if (picture_width && picture_width <= decoded_width && decoded_width - picture_width < 16)
		*width = picture_width;
	if (picture_height && picture_height <= decoded_height && decoded_height - picture_height < 16)
		*height = picture_height;
}

/* the summed difference between sampled rows y and y + 1 of a luma plane
read pitch bytes apart, and (horizontal) the summed difference along them */
static unsigned long row_difference(const unsigned char *luma, unsigned long pitch, unsigned long width,
	unsigned long height, unsigned long *horizontal)
{
	unsigned long sample, x, sum = 0, along = 0;

	for (sample = 0; sample < 24; sample++)
	{
		unsigned long y = sample * (height - 2) / 23;
		const unsigned char *row = luma + y * pitch, *next = row + pitch;

		for (x = 0; x + 4 < width; x += 4)
		{
			sum += (unsigned long)abs((int)row[x] - (int)next[x]);
			along += (unsigned long)abs((int)row[x] - (int)row[x + 4]);
		}
	}
	if (horizontal)
		*horizontal = along;
	return sum;
}

unsigned long vita_movie_detect_pitch(const unsigned char *luma, unsigned long room, unsigned long width,
	unsigned long height, int *decided)
{
	/* the candidates: the width padded to 16 (what the decoder is assumed
	to write), 32, 64, 128 and 256 pixels; one is taken only when its rows
	follow on from each other clearly better (by a quarter) than those of
	the assumed one. A flat picture (the intro's first second is white)
	says nothing: *decided stays 0 and the next frame is asked */
	static const unsigned long alignments[] = { 16, 32, 64, 128, 256 };
	unsigned long assumed = (width + 15) & ~15UL, best = assumed, best_cost, horizontal, index;

	*decided = 0;
	if (!luma || width < 16 || height < 16)
		return assumed;
	best_cost = row_difference(luma, assumed, width, height, &horizontal);
	if (horizontal < 24 * (width / 4) * 2)
		return assumed;
	*decided = 1;
	for (index = 1; index < sizeof(alignments) / sizeof(alignments[0]); index++)
	{
		unsigned long pitch = (width + alignments[index] - 1) & ~(alignments[index] - 1), cost;

		if (pitch == best || pitch * ((height + 15) & ~15UL) * 3 / 2 > room)
			continue;
		cost = row_difference(luma, pitch, width, height, NULL);
		if (cost * 4 < best_cost * 3)
		{
			best = pitch;
			best_cost = cost;
		}
	}
	return best;
}
