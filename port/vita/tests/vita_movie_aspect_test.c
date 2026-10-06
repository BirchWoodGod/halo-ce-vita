/*
VITA_MOVIE_ASPECT_TEST.C

A desktop test of port/vita/host/vita_movie_aspect.c: prints the display
shape it finds for each MP4 named on the command line, and with
"<file>=<expected>" checks it; "@<player>" adds a player aspect ratio for
vita_movie_choose_aspect (run_vita_movie_aspect_test.sh makes the files
with ffmpeg). "size:<file>=<w>x<h>" checks the picture size the file's
sample description gives, "visible:<dw>x<dh>,<pw>x<ph>=<w>x<h>" what
vita_movie_visible_size keeps of a decoded frame, and
"pitch:<gray file>:<w>x<h>@<pitch>=<expected>" the row pitch
vita_movie_detect_pitch finds in a luma plane laid out with rows <pitch>
bytes apart (0 expected: too flat to decide).
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_host.h"

int main(int argc, char **argv)
{
	int index, failures = 0;

	for (index = 1; index < argc; index++)
	{
		char path[1024];
		char *equals;
		unsigned long width, height;
		const char *source;
		float aspect, expected = 0.0f, player = 0.0f;
		char *at;

		if (!strncmp(argv[index], "pitch:", 6))
		{
			unsigned long frame_width, frame_height, pitch, expected_pitch, room, row, found;
			unsigned char *gray, *plane;
			FILE *file;
			int decided, ok;

			snprintf(path, sizeof(path), "%s", argv[index] + 6);
			equals = strrchr(path, '=');
			at = strrchr(path, ':');
			if (!equals || !at || sscanf(at + 1, "%lux%lu@%lu", &frame_width, &frame_height, &pitch) != 3)
			{
				fprintf(stderr, "bad check %s\n", argv[index]);
				return 2;
			}
			expected_pitch = strtoul(equals + 1, NULL, 10);
			*at = 0;
			room = 2048UL * 1088 * 3 / 2;
			gray = malloc(frame_width * frame_height);
			plane = malloc(room);
			file = fopen(path, "rb");
			if (!gray || !plane || !file || fread(gray, 1, frame_width * frame_height, file) != frame_width * frame_height)
			{
				fprintf(stderr, "cannot read %s\n", path);
				return 2;
			}
			fclose(file);
			/* (the padding and what follows: noise, as a decoder may leave) */
			for (row = 0; row < room; row++)
				plane[row] = (unsigned char)(rand() >> 7);
			for (row = 0; row < frame_height; row++)
				memcpy(plane + row * pitch, gray + row * frame_width, frame_width);
			found = vita_movie_detect_pitch(plane, room, frame_width, frame_height, &decided);
			ok = decided ? found == expected_pitch : expected_pitch == 0;
			printf("%s: %lu%s - expected %lu: %s\n", argv[index], found, decided ? "" : " (undecided)", expected_pitch,
				ok ? "ok" : "FAILED");
			failures += !ok;
			free(gray);
			free(plane);
			continue;
		}
		if (!strncmp(argv[index], "size:", 5) || !strncmp(argv[index], "visible:", 8))
		{
			unsigned long expected_width = 0, expected_height = 0, got_width = 0, got_height = 0;
			int ok;

			snprintf(path, sizeof(path), "%s", strchr(argv[index], ':') + 1);
			equals = strrchr(path, '=');
			if (!equals || sscanf(equals + 1, "%lux%lu", &expected_width, &expected_height) != 2)
			{
				fprintf(stderr, "bad check %s\n", argv[index]);
				return 2;
			}
			*equals = 0;
			if (argv[index][0] == 's')
				vita_movie_file_picture_size(path, &got_width, &got_height);
			else
			{
				unsigned long decoded_width, decoded_height, picture_width, picture_height;

				if (sscanf(path, "%lux%lu,%lux%lu", &decoded_width, &decoded_height, &picture_width, &picture_height) != 4)
				{
					fprintf(stderr, "bad check %s\n", argv[index]);
					return 2;
				}
				vita_movie_visible_size(decoded_width, decoded_height, picture_width, picture_height, &got_width, &got_height);
			}
			ok = got_width == expected_width && got_height == expected_height;
			printf("%s: %lux%lu - expected %lux%lu: %s\n", argv[index], got_width, got_height, expected_width, expected_height,
				ok ? "ok" : "FAILED");
			failures += !ok;
			continue;
		}
		/* <file>:<width>x<height>[@<player>][=<expected>] */
		snprintf(path, sizeof(path), "%s", argv[index]);
		equals = strchr(path, '=');
		if (equals)
		{
			*equals = 0;
			expected = (float)atof(equals + 1);
		}
		at = strrchr(path, '@');
		if (at && at > strrchr(path, '/'))
		{
			*at = 0;
			player = (float)atof(at + 1);
		}
		if (!strrchr(path, ':') || sscanf(strrchr(path, ':') + 1, "%lux%lu", &width, &height) != 2)
		{
			fprintf(stderr, "usage: %s file.mp4:<w>x<h>[@<player>][=<aspect>] ...\n", argv[0]);
			return 2;
		}
		*strrchr(path, ':') = 0;
		aspect = vita_movie_file_aspect(path, width, height, &source);
		if (player > 0.0f)
			aspect = vita_movie_choose_aspect(aspect, &source, width, height, player);
		printf("%s (%lux%lu", path, width, height);
		if (player > 0.0f)
			printf(", player %.3f", (double)player);
		printf("): %.3f from %s", (double)aspect, source);
		if (equals)
		{
			int ok = fabsf(aspect - expected) < 0.01f;

			printf(" - expected %.3f: %s", (double)expected, ok ? "ok" : "FAILED");
			failures += !ok;
		}
		printf("\n");
	}
	return failures ? 1 : 0;
}
