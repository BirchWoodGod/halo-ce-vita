/*
FORGE_STORE.H

Forge's map variants on the memory card (forge_store.c): the Forge folder
(ux0:data/haloce-vita/forge/ on the Vita, <save root>/forge/ elsewhere), a
folder in it for each map (its short name: bloodgulch), and in that the map
variants (<name>.hmv, port/linux/game/forge_files.h). Only names this port
makes are ever opened there: a map folder's name is checked as a map's, a
file's as a map variant's (forge_file_name_valid), and every file read goes
through the validator (forge_file_decode) with its folder's map expected,
before anything is taken from it. A file is written whole or not at all: to
a temporary file first, read back and checked, then renamed over the old
one.
*/

#ifndef __FORGE_STORE_H
#define __FORGE_STORE_H

#include "../game/forge_files.h"

/* ---------- constants */

/* the most map variants listed (each map's, or all of them) */
#define FORGE_STORE_MAXIMUM_LISTED 128
/* a message's room */
#define FORGE_STORE_WHY_SIZE 160

/* ---------- structures */

/* a map variant found */
struct forge_store_entry
{
	char map_name[FORGE_FILE_MAP_NAME_SIZE];
	char file_name[FORGE_FILE_NAME_SIZE];
	char title[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char description[FORGE_FILE_DESCRIPTION_SIZE];
	char date[FORGE_FILE_DATE_SIZE];
	/* its records (objects), its packs */
	short objects;
	short packs;
	/* refused by the validator: listed so that it can be deleted, never
	read further */
	int damaged;
	unsigned long size;
};

/* ---------- prototypes/FORGE_STORE.C */

/* the Forge folder, or a map's folder in it (map_name not NULL), as a path
of this machine; the folders made when make is nonzero. 0 if the name is
not a map's or the path does not fit */
int forge_store_folder(
	char const *map_name,
	int make,
	char *path,
	int size);

/* the map variants of a map's folder (map_name), or of every map's
(NULL): at most capacity, by map then title; how many */
int forge_store_list(
	char const *map_name,
	struct forge_store_entry *entries,
	int capacity);

/* a map variant read and checked (its map expected); 1, or 0 with why */
int forge_store_load(
	char const *map_name,
	char const *file_name,
	struct forge_file_layout *layout,
	char *why,
	int why_size);

/* a layout written to its map's folder: as file_name (replacing it), or,
when file_name is NULL or empty, under a name made from its title that no
other file has; the name in saved_name. 1, or 0 with why (nothing left
behind: the old file, if any, as it was) */
int forge_store_save(
	struct forge_file_layout const *layout,
	char const *file_name,
	char *saved_name,
	int saved_size,
	char *why,
	int why_size);

/* a map variant deleted; 1 if it was */
int forge_store_delete(
	char const *map_name,
	char const *file_name);

/* a map variant's title changed (its file name kept); 1, or 0 with why */
int forge_store_rename(
	char const *map_name,
	char const *file_name,
	char const *title,
	char *why,
	int why_size);

/* whether a map variant's file is there */
int forge_store_exists(
	char const *map_name,
	char const *file_name);

/* the date now, "YYYY-MM-DD HH:MM" */
void forge_store_date_now(
	char *date,
	int size);

#endif
