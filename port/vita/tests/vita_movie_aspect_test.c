/*
VITA_MOVIE_ASPECT_TEST.C

A desktop test of port/vita/host/vita_movie_aspect.c: prints the display
shape it finds for each MP4 named on the command line, and with
"<file>=<expected>" checks it; "@<player>" adds a player aspect ratio for
vita_movie_choose_aspect (run_vita_movie_aspect_test.sh makes the files
with ffmpeg).
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
