/*
NET_FUZZ_MAIN.C

The network fuzz targets (net_fuzz_*.c) without libFuzzer: runs each file
named, or each file in each folder named, through the target once (the
cases kept in net_fuzz_cases/, and any that a fuzzing run found), after
the target's own checks (net_fuzz_checks, if it has them). Built with
AddressSanitizer and UBSan by run_net_fuzz_test.sh, so a case that reads
or writes out of bounds, or overflows, fails the run.

Built as the platform layer's tests are (gcc, -m32), and linked with a
target built as the game is.
*/

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
/* the target's checks: the number that failed */
int net_fuzz_checks(void) __attribute__((weak));

/* a target's seed for the fuzzer, written to folder/name */
void net_fuzz_write_seed(const char *folder, const char *name, const void *data, unsigned long size)
{
	char path[4096];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(data, 1, size, file);
		fclose(file);
	}
}

static int run_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	static uint8_t data[1 << 20];
	size_t size;

	if (!file)
	{
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}
	size = fread(data, 1, sizeof(data), file);
	fclose(file);
	LLVMFuzzerTestOneInput(data, size);
	return 0;
}

int main(int argc, char **argv)
{
	int failures = 0, cases = 0;
	int index;

	if (net_fuzz_checks)
		failures += net_fuzz_checks();
	for (index = 1; index < argc; index++)
	{
		struct stat status;

		if (stat(argv[index], &status) == 0 && S_ISDIR(status.st_mode))
		{
			DIR *directory = opendir(argv[index]);
			struct dirent *entry;

			while (directory && (entry = readdir(directory)))
			{
				char path[4096];

				if (entry->d_name[0] == '.')
					continue;
				snprintf(path, sizeof(path), "%s/%s", argv[index], entry->d_name);
				failures += run_file(path);
				cases++;
			}
			if (directory)
				closedir(directory);
		}
		else
		{
			failures += run_file(argv[index]);
			cases++;
		}
	}
	printf("%s: %d cases, %d failures\n", argv[0], cases, failures);
	return failures != 0;
}
