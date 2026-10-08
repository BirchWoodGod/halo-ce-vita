/*
POSIX_FILES_TEST.C

A desktop test of the read-only bit the game clears on every file it
deletes (file_delete, source/tag_files/files_windows.c: SetFileAttributesA
then DeleteFileA; posix_set_read_only in port/linux/src/posix_files.c),
built twice by run_posix_files_test.sh with chmod refusing everything
(EINVAL), as the Vita's newlib chmod does:

  linux  a file that can be written is left alone (no chmod), so it is
         deleted; one made read-only is made writable, and read-only
         again, as before (chmod let through for those)
  vita   posix_files.c as the Vita builds it, the card's sceIoGetstat and
         sceIoChstat stood in for: a file that can be written is left
         alone and deleted; a write bit the card will not change does
         not keep a file from being deleted, but setting one it will not
         is an error; a file that is not there is ENOENT; nothing found
         is called read-only (newlib's stat has no permission bits)

Every file_delete failed on the Vita with error 0x57 (ERROR_INVALID_PARAMETER,
chmod's EINVAL) before: 26 default playlists at every start, the datastore
and the movie folder's files.

Run port/vita/tests/run_posix_files_test.sh.
*/

#ifdef __vita__
/* (first, as posix_files.c has it) */
#include <psp2/io/stat.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix.h"

static int failures;
/* (volatile: the wrapped chmod reads and counts them behind the compiler's back) */
static volatile int chmod_calls, chmod_allowed;

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* (-Wl,--wrap=chmod) the Vita's chmod: refused */
int __real_chmod(const char *path, mode_t mode);
int __wrap_chmod(const char *path, mode_t mode)
{
	chmod_calls++;
	if (chmod_allowed)
		return __real_chmod(path, mode);
	errno = EINVAL;
	return -1;
}

static void make_file(const char *path, mode_t mode)
{
	FILE *file = fopen(path, "wb");

	fputs("blam", file);
	fclose(file);
	chmod_allowed = 1;
	chmod(path, mode);
	chmod_allowed = 0;
	chmod_calls = 0;
}

#ifdef __vita__
/* the card: a file's write bit as the card keeps it, and whether it lets
sceIoChstat change it */
static volatile int card_writable = 1, card_changes = 1, chstat_calls;

int sceIoGetstat(const char *file, SceIoStat *information)
{
	struct stat st;

	if (stat(file, &st) != 0)
		return (int)(0x80010000u | (unsigned)errno);
	memset(information, 0, sizeof(*information));
	information->st_mode = SCE_S_IFREG | SCE_S_IRUSR | SCE_S_IRSYS | SCE_S_IWSYS | (card_writable ? SCE_S_IWUSR : 0);
	information->st_size = st.st_size;
	return 0;
}

int sceIoChstat(const char *file, SceIoStat *information, int bits)
{
	(void)file;
	chstat_calls++;
	/* (the type kept, as the card wants it) */
	if (!card_changes || bits != SCE_CST_MODE || (information->st_mode & SCE_S_IFMT) != SCE_S_IFREG)
		return (int)0x80010016u;
	card_writable = (information->st_mode & SCE_S_IWUSR) != 0;
	return 0;
}

static void test(const char *folder)
{
	char path[1024];
	struct posix_file_information information;

	printf("--- vita\n");
	snprintf(path, sizeof(path), "%s/blam.lst", folder);

	make_file(path, 0644);
	card_writable = 1;
	chstat_calls = 0;
	check(posix_set_read_only(path, 0) == 0, "a file that can be written: its bit cleared");
	check(chstat_calls == 0 && chmod_calls == 0, "a file that can be written: left alone");
	check(unlink(path) == 0, "then deleted");

	make_file(path, 0644);
	card_writable = 0;
	card_changes = 0;
	check(posix_set_read_only(path, 0) == 0, "a write bit the card will not set: not in the way");
	check(chmod_calls == 0, "newlib's chmod not called");
	check(unlink(path) == 0, "then deleted");

	make_file(path, 0644);
	card_writable = 0;
	card_changes = 1;
	check(posix_set_read_only(path, 0) == 0 && card_writable, "a read-only file made writable on the card");
	errno = 0;
	card_changes = 0;
	check(posix_set_read_only(path, 1) == -1 && errno == EINVAL, "a read-only bit the card will not set: an error");
	card_changes = 1;
	check(posix_set_read_only(path, 1) == 0 && !card_writable, "made read-only on the card");
	check(posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_read_only),
		"nothing called read-only from newlib's stat");
	unlink(path);

	errno = 0;
	check(posix_set_read_only(path, 0) == -1 && errno == ENOENT, "a file that is not there: ENOENT");
}
#else
static void test(const char *folder)
{
	char path[1024];
	struct stat st;
	struct posix_file_information information;

	printf("--- linux\n");
	snprintf(path, sizeof(path), "%s/blam.lst", folder);

	make_file(path, 0644);
	check(posix_set_read_only(path, 0) == 0, "a file that can be written: its bit cleared");
	check(chmod_calls == 0, "a file that can be written: left alone");
	check(unlink(path) == 0, "then deleted");

	make_file(path, 0444);
	chmod_allowed = 1;
	check(posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_read_only), "0444 is read-only");
	check(posix_set_read_only(path, 0) == 0 && chmod_calls == 1, "a read-only file made writable");
	check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644, "0444 to 0644");
	check(posix_set_read_only(path, 1) == 0 && stat(path, &st) == 0 && (st.st_mode & 0777) == 0444,
		"made read-only again: 0444");
	chmod_allowed = 0;
	errno = 0;
	check(posix_set_read_only(path, 0) == -1 && errno == EINVAL, "a change chmod refuses: its error");
	chmod_allowed = 1;
	chmod(path, 0644);
	chmod_allowed = 0;
	unlink(path);

	errno = 0;
	check(posix_set_read_only(path, 0) == -1 && errno == ENOENT, "a file that is not there: ENOENT");
}
#endif

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "posix_files_test FOLDER\n");
		return 2;
	}
	test(argv[1]);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
