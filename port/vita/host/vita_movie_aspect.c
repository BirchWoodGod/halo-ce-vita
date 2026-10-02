/*
VITA_MOVIE_ASPECT.C

The shape a movie is meant to be shown at, from its MP4 file: the Vita's
video player gives the frame's size in pixels, and an MP4 may store its
pictures squeezed ("anamorphic": 640x480 pixels shown 16:9, as ffmpeg's
-aspect 16:9 writes them). The display size is in the video track's header
(tkhd: the width and height a player shows, 16.16 fixed point), and the
pixels' own shape in the sample description's pasp box. Plain C and stdio,
so the host tests can build it (port/vita/tests).
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
};

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
					walk_boxes(body + entry + 8 + 78, entry_size - 8 - 78, search, depth + 1);
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

float vita_movie_file_aspect(const char *path, unsigned long width, unsigned long height, const char **source)
{
	struct aspect_search search;
	unsigned char header[16];
	FILE *file;
	long offset = 0;
	float aspect = 0.0f;

	*source = "its size";
	if (!width || !height)
		return 0.0f;
	memset(&search, 0, sizeof(search));
	file = fopen(path, "rb");
	if (!file)
		return (float)width / (float)height;
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
				walk_boxes(body, body_size, &search, 0);
			free(body);
			break;
		}
		offset += (long)box_size;
	}
	fclose(file);
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
