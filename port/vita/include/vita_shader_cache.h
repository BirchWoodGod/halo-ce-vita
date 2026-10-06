/*
The formats of the Vita's compiled shader programs on the memory card
(ux0:data/haloce-vita/shaders) and in the VPK (app0:shaders.pak), and the
checks a file must pass before one of its programs is handed to the GPU.
Plain C, no SDK: port/vita/host/vita_gxm.c reads and writes the files,
tools/vita_shader_pack.py makes the pack, port/vita/tests/vita_shader_cache_test.c
tests the checks on the desktop.

A program is looked up by the hash of its Cg (vshc_source_hash). Two ids
say what else made it:
- the compile id: the compiler's settings (vita_gxm.c's shader_compile_settings,
  hashed as text) and these formats' version. Programs made with other
  settings are other programs, so a pack or a cache file with another
  compile id is never used.
- the generator id: a hash, taken at build time, of the sources that write
  the Cg (tools/vita_shader_generator_id.py: nv2a_psh_cg.c, nv2a_vsh_cg.c,
  vita_xgpu.h). The memory card's cache is emptied when it changes (issue
  #28: a cache an older build left was used by a newer one). The pack is
  matched by the compile id only: each of its programs is bound to the exact
  Cg it was compiled from (the hash) and to its bytes (a checksum), so a
  generator change only leaves some of its programs unasked for.

Cache file <hash>.gxp: a 40-byte header ("HCEVSHC2", compile id, generator
id, the Cg's hash, the program's size and FNV-1a 32 checksum), then the GXP
program. The cache's id file (VSHC_ID_FILE_NAME) holds both ids as text.
Pack: a 32-byte header ("HCEVSHP2", count, 0, compile id, generator id of
the build it was made for, for the log only), per program 24 bytes (hash,
offset, size, checksum, 0) sorted by hash, then the programs, each 16-byte
aligned. Little endian, as the Vita.
*/
#ifndef VITA_SHADER_CACHE_H
#define VITA_SHADER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define VSHC_MAGIC "HCEVSHC2"
#define VSHC_HEADER_SIZE 40
#define VSHP_MAGIC "HCEVSHP2"
#define VSHP_HEADER_SIZE 32
#define VSHP_ENTRY_SIZE 24
#define VSHC_ID_FILE_NAME "cache.id"
/* (bumped when a format above changes; part of the compile id) */
#define VSHC_FORMAT_VERSION 2

enum vshc_result
{
	VSHC_OK = 0,
	VSHC_SHORT,         /* shorter than a header, or than the size it gives */
	VSHC_NOT_OURS,      /* no magic: an older build's raw program, or not a cache file */
	VSHC_OTHER_COMPILE, /* made with other compiler settings */
	VSHC_OTHER_GENERATOR, /* made by a build with other Cg generator sources */
	VSHC_OTHER_SOURCE,  /* the program of another Cg than its name says */
	VSHC_BAD_SIZE,      /* the file's size is not the header's */
	VSHC_BAD_CHECKSUM,  /* the program's bytes are not the ones written */
	VSHC_NOT_PROGRAM,   /* not a whole GXP program */
};

struct vshc_ids
{
	uint64_t compile_id, generator_id;
};

/* FNV-1a 64 over the Cg, the basis xored with the kind (1: fragment) */
uint64_t vshc_source_hash(const char *source, int fragment);
/* FNV-1a 32 over the program's bytes */
uint32_t vshc_checksum(const void *data, size_t size);
/* the compile id for the settings' text (vita_gxm.c) */
uint64_t vshc_compile_id(const char *settings);
/* nonzero if data is a whole GXP program of size bytes (magic, size field) */
int vshc_gxp_whole(const void *data, size_t size);
/* what a result means, for the log */
const char *vshc_result_name(enum vshc_result result);

/* the header for a program (VSHC_HEADER_SIZE bytes into header) */
void vshc_header_make(unsigned char *header, const struct vshc_ids *ids, uint64_t hash, const void *program,
	uint32_t program_size);
/* the header read from a cache file of file_size bytes, checked against
the ids and the hash its name gives; VSHC_OK: *program_size is the size of
the program that follows it (file_size - VSHC_HEADER_SIZE) */
enum vshc_result vshc_header_check(const unsigned char *header, size_t file_size, const struct vshc_ids *ids,
	uint64_t hash, uint32_t *program_size);
/* the program read after a checked header: its checksum and GXP size */
enum vshc_result vshc_program_check(const unsigned char *header, const void *program, uint32_t program_size);
/* both, for a whole cache file in one buffer; VSHC_OK: *program points into it */
enum vshc_result vshc_file_check(const void *data, size_t size, const struct vshc_ids *ids, uint64_t hash,
	const void **program, uint32_t *program_size);

/* the id file's text for the ids (at most 64 bytes with the NUL) */
void vshc_id_text(char *text, size_t size, const struct vshc_ids *ids);
/* nonzero if the id file's contents (not NUL-terminated) are the ids' */
int vshc_id_matches(const char *contents, size_t size, const struct vshc_ids *ids);
/* nonzero if a file name is one the cache writes (<16 hex>.gxp or .tmp) or
the id file: the only files the cache ever removes */
int vshc_cache_file_name(const char *name);

/* ---------- another build's cache: moved aside at start-up, removed later

Emptying another build's cache file by file at start-up took 65 s on a
Vita's memory card (632 files, sceIoRemove ~0.1 s each; v1.1.0, Oct 6 2026),
a black screen all the while. Instead the folder is renamed, in one
operation, to <name>.old-<n> and a new empty folder made in its place
(vshc_retire); a low-priority thread removes the old folders later, once the
game is up and while it reads no files (vita_gxm.c, vshc_sweep): only the
names the cache writes (vshc_cache_file_name), then the folder if that left
it empty, so a folder holding anything else stays. If the rename fails, the
files stay where they are, the folder is marked (VSHC_SWEEP_FILE_NAME) and
swept in place: a program file is removed only if its header is not this
build's (cache_read refuses such a file anyway). An interrupted clean-up
goes on at the next start. The file system is reached through vshc_fs, so
the desktop test runs all of this on a folder of its own. */

#define VSHC_SWEEP_FILE_NAME "sweep.pending"
#define VSHC_OLD_FOLDER_SUFFIX ".old-"
#define VSHC_OLD_FOLDERS_MAX 99

struct vshc_fs
{
	void *context;
	/* each 0 on success, negative on failure */
	int (*rename)(void *context, const char *from, const char *to);
	int (*make_directory)(void *context, const char *path);
	int (*remove)(void *context, const char *path);
	int (*remove_directory)(void *context, const char *path);
	/* writes the text as the whole file */
	int (*write_text)(void *context, const char *path, const char *text);
	/* nonzero if anything is at the path */
	int (*exists)(void *context, const char *path);
	/* the first size bytes of the file: the count read, negative on failure */
	int (*read_head)(void *context, const char *path, void *buffer, unsigned int size);
	/* each entry's name in the folder (not . or ..) to each; negative if it cannot be listed */
	int (*list)(void *context, const char *path, void (*each)(void *argument, const char *name), void *argument);
};

enum vshc_retire_result
{
	VSHC_RETIRE_RENAMED,  /* moved to <name>.old-<n>, an empty folder made in its place */
	VSHC_RETIRE_IN_PLACE, /* the rename failed: marked to be swept in place */
	VSHC_RETIRE_FAILED,   /* neither: the files stay (each still refused when read) */
};

/* parent/name, another build's cache folder, out of the way; *index: the n
of the folder it was renamed to */
enum vshc_retire_result vshc_retire(const struct vshc_fs *fs, const char *parent, const char *name, int *index);
/* n if entry is <name>.old-<n> (1..VSHC_OLD_FOLDERS_MAX), else 0 */
int vshc_old_folder_index(const char *entry, const char *name);
/* nonzero if vshc_sweep has anything to do: an old folder in parent, or
parent/name marked */
int vshc_sweep_pending(const struct vshc_fs *fs, const char *parent, const char *name);

struct vshc_sweep_counts
{
	unsigned long removed;      /* files removed */
	unsigned long folders;      /* old folders removed */
	unsigned long left;         /* files left: not the cache's, or not removable */
};

/* the clean-up: every <name>.old-<n> folder in parent emptied of the cache's
files and removed, and parent/name swept in place if marked (keeping the
files whose header carries ids). pace(argument) is called before each
removal and stops the clean-up by returning 0 (the next start goes on).
1 when nothing is left to do, 0 if stopped or something stays. */
int vshc_sweep(const struct vshc_fs *fs, const char *parent, const char *name, const struct vshc_ids *ids,
	int (*pace)(void *argument), void *argument, struct vshc_sweep_counts *counts);

struct vshp_pack
{
	const unsigned char *data;
	size_t size;
	unsigned int count;
	uint64_t compile_id, generator_id;
};

/* checks a pack read whole into data (8-byte aligned); VSHC_OK fills pack */
enum vshc_result vshp_open(struct vshp_pack *pack, const void *data, size_t size, uint64_t compile_id);
/* the program for the hash in an opened pack (its bytes checked), or NULL */
const void *vshp_find(const struct vshp_pack *pack, uint64_t hash, uint32_t *program_size);

#endif
