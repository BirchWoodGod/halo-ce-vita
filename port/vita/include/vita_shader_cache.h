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
