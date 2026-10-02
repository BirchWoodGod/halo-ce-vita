/*
VITA_MOVIE_ASPECT_TEST.C

A desktop test of port/vita/host/vita_movie_aspect.c: prints the display
shape it finds for each MP4 named on the command line, and with
"<file>=<expected>" checks it (run_vita_movie_aspect_test.sh makes the files
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
		float aspect, expected = 0.0f;

		/* <file>:<width>x<height>[=<expected>] */
		snprintf(path, sizeof(path), "%s", argv[index]);
		equals = strchr(path, '=');
		if (equals)
		{
			*equals = 0;
			expected = (float)atof(equals + 1);
		}
		if (!strrchr(path, ':') || sscanf(strrchr(path, ':') + 1, "%lux%lu", &width, &height) != 2)
		{
			fprintf(stderr, "usage: %s file.mp4:<w>x<h>[=<aspect>] ...\n", argv[0]);
			return 2;
		}
		*strrchr(path, ':') = 0;
		aspect = vita_movie_file_aspect(path, width, height, &source);
		printf("%s (%lux%lu): %.3f from %s", path, width, height, (double)aspect, source);
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
