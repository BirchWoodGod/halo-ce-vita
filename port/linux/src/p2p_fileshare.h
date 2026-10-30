/*
P2P_FILESHARE.H

File Share with friends (p2p_fileshare.inc, part of p2p_friends.c, as the
party is; the design, its messages, limits, validation and threats:
docs/fileshare-friends.md). Halo 3's way: a friend's File Share browsed
(the map variants and game variants they mark shared), one downloaded with
the player's yes, and a file sent to a friend, who is asked first.

Friends only (accepted friends: never a blocked player, a stranger or a
party member who is not a friend), variants only (a Forge map variant, a
game variant; never a map or any other file), at most
P2P_FILESHARE_MAXIMUM_BYTES. Everything travels through the friends'
channel: the brokers, sealed for the pair and signed with the friend
identity, replay protected. Nothing is sent or answered while this machine
appears offline or has Share with friends (friends.fileshare) off.

This unit knows no file: the game's main loop (port/linux/game/
fileshare_friends.c) gives it this machine's list (p2p_fileshare_set_local),
reads a file it asks for (a job: P2P_FILESHARE_JOB_LOAD) through the game's
own loader and validator, and checks and saves a file received (a job:
P2P_FILESHARE_JOB_SAVE) through the validator, after the player said yes.

Every call takes the friends' lock (briefly) and may come from any thread.
Plain ints and chars only: the Vita's settings panel (another ABI) calls
these too.
*/

#ifndef __HALO_LINUX_P2P_FILESHARE_H
#define __HALO_LINUX_P2P_FILESHARE_H

#include "p2p_friends.h"

enum
{
	/* the kinds of file shared (the only ones) */
	P2P_FILESHARE_KIND_MAP_VARIANT = 1,
	P2P_FILESHARE_KIND_GAME_VARIANT = 2,
	/* the most bytes of a map variant (forge_files.h FORGE_FILE_MAXIMUM_SIZE),
	and a game variant's (struct game_variant, as blam.lst holds it) */
	P2P_FILESHARE_MAXIMUM_BYTES = 65536,
	P2P_FILESHARE_GAME_VARIANT_BYTES = 0x68,
	/* the files marked shared at most, and this machine's list's most */
	P2P_FILESHARE_SHARED_MAXIMUM = 32,
	P2P_FILESHARE_LOCAL_MAXIMUM = 160,
	/* a map's short name, a file's name (a map variant's <stem>.hmv; a game
	variant's saved game folder), a title, each with its terminator */
	P2P_FILESHARE_MAP_SIZE = 32,
	P2P_FILESHARE_FILE_SIZE = 40,
	P2P_FILESHARE_TITLE_SIZE = 32,
	/* a file's id (the first 8 bytes of its SHA-256) in hex, an offer's
	id in hex, with terminators */
	P2P_FILESHARE_ID_SIZE = 17,
	P2P_FILESHARE_DIGEST_SIZE = 32,
	/* the offers waiting for the player's answer at most */
	P2P_FILESHARE_OFFERS_MAXIMUM = 4,
	P2P_FILESHARE_TEXT_SIZE = 160,
};

/* a file of this machine's (the game's list: every map variant of its own,
every game variant of its player's own) */
struct p2p_fileshare_local
{
	int kind;
	/* a map variant's map (bloodgulch); "" for a game variant */
	char map[P2P_FILESHARE_MAP_SIZE];
	/* a map variant's file name; a game variant's saved game folder's */
	char file[P2P_FILESHARE_FILE_SIZE];
	/* its title (UTF-8), as File Share shows it */
	char title[P2P_FILESHARE_TITLE_SIZE];
	int size;
	/* refused by the game's validator (never offered) */
	int damaged;
	/* (filled by p2p_fileshare_local) marked shared */
	int shared;
	/* (the game: of a shared file it read and checked) its SHA-256 */
	int hashed;
	unsigned char digest[P2P_FILESHARE_DIGEST_SIZE];
};

/* a file of a friend's File Share (their list) */
struct p2p_fileshare_remote
{
	char id[P2P_FILESHARE_ID_SIZE];
	int kind;
	char map[P2P_FILESHARE_MAP_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	int size;
};

/* a friend's offer (their "send to friend"), for the question */
struct p2p_fileshare_offer
{
	char id[P2P_FILESHARE_ID_SIZE];
	char friend_name[P2P_FRIEND_NAME_SIZE];
	int kind;
	char map[P2P_FILESHARE_MAP_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	int size;
};

/* the outcome of an action */
enum
{
	P2P_FILESHARE_DONE,
	/* no such friend, file or offer (gone meanwhile) */
	P2P_FILESHARE_GONE,
	/* internet play or friends are off */
	P2P_FILESHARE_OFFLINE,
	P2P_FILESHARE_APPEARING_OFFLINE,
	/* Share with friends is off */
	P2P_FILESHARE_SHARING_OFF,
	/* the friend shows offline */
	P2P_FILESHARE_FRIEND_OFFLINE,
	/* one is under way already (a download; two uploads, one a friend) */
	P2P_FILESHARE_BUSY,
	P2P_FILESHARE_TOO_SOON,
	/* the marks are full */
	P2P_FILESHARE_FULL,
	/* not a file that can be shared (a kind, a name) */
	P2P_FILESHARE_NOT_ALLOWED,
	P2P_FILESHARE_TOO_LARGE,
	P2P_FILESHARE_OUTCOME_COUNT
};

/* Share with friends (friends.fileshare): friends may browse the files
marked shared, and files are sent and received (asked each time) */
int p2p_fileshare_sharing(void);

/* ---------- this machine's files */

/* whether a file is marked shared */
int p2p_fileshare_marked(int kind, const char *map, const char *file);
/* a file marked shared, or not (kept in friends/fileshare.dat): an outcome */
int p2p_fileshare_mark(int kind, const char *map, const char *file, int shared);
/* (the game) this machine's list, read again: every file of its own, the
shared ones read, checked and hashed */
void p2p_fileshare_set_local(const struct p2p_fileshare_local *entries, int count);
/* the list as the game last gave it, each with its mark: how many */
int p2p_fileshare_local(struct p2p_fileshare_local *entries, int maximum_count);
/* (the game, each frame) nonzero when the list should be read again (its
marks changed, a friend asked and it is old, the menus asked); the ask
taken */
int p2p_fileshare_local_wanted(void);
/* (the menus, the settings panel) the list read again soon */
void p2p_fileshare_refresh(void);

/* ---------- a friend's File Share */

/* a friend's (their entry's id, p2p_friends.h) File Share asked for: an
outcome */
int p2p_fileshare_browse(const char *friend_id);
/* where the browse is */
enum
{
	P2P_FILESHARE_BROWSE_NONE,
	P2P_FILESHARE_BROWSE_ASKING,
	P2P_FILESHARE_BROWSE_LISTED,
	/* their Share with friends is off */
	P2P_FILESHARE_BROWSE_NOT_SHARING,
	/* asked too soon after the last */
	P2P_FILESHARE_BROWSE_TOO_SOON,
	/* no answer (offline, appearing offline, an older build) */
	P2P_FILESHARE_BROWSE_NO_ANSWER,
};
/* the browse's state, its friend's name and id, and their files (at most
maximum_count; *count of them) */
int p2p_fileshare_browse_state(char *friend_name, char *friend_id, struct p2p_fileshare_remote *entries,
	int maximum_count, int *count);
/* the browse let go */
void p2p_fileshare_browse_close(void);
/* a file of the friend's list (its id) downloaded: the player said yes to
the question. An outcome */
int p2p_fileshare_download(const char *file_id);

/* ---------- send to friend */

/* one of this machine's files sent to a friend (their entry's id): they
are asked, then it goes. An outcome */
int p2p_fileshare_send(const char *friend_id, int kind, const char *map, const char *file);

/* the oldest offer not asked about yet, for the question: nonzero if there
is one (then marked asked: it waits for the answer until it goes, after
two minutes) */
int p2p_fileshare_take_offer(struct p2p_fileshare_offer *offer);
/* an offer answered: accepted (it is downloaded, then checked and saved)
or declined. An outcome */
int p2p_fileshare_answer(const char *offer_id, int accept);

/* what the transfers came to, as the player reads it ("Saving \"Tower\"
from Bob: 40%", "Bob declined \"Tower\"", ...): the line, and a number
that changes with it (0: nothing yet) */
unsigned long p2p_fileshare_status(char *text, int size);
/* a download's or an upload's progress: nonzero while one is under way,
percent of it */
int p2p_fileshare_progress(int *percent);

/* ---------- the game's jobs */

enum
{
	P2P_FILESHARE_JOB_NONE,
	/* a file of this machine's read, checked and its bytes given back:
	p2p_fileshare_job_loaded */
	P2P_FILESHARE_JOB_LOAD,
	/* a file received (whole, its SHA-256 the offer's) checked by the
	validator and saved: p2p_fileshare_job_saved */
	P2P_FILESHARE_JOB_SAVE,
};
struct p2p_fileshare_job
{
	int serial;
	int type;
	int kind;
	char map[P2P_FILESHARE_MAP_SIZE];
	char file[P2P_FILESHARE_FILE_SIZE];
	/* (a load for a friend's download: the SHA-256 it was listed with,
	which its bytes must still have) */
	int has_digest;
	unsigned char digest[P2P_FILESHARE_DIGEST_SIZE];
	/* (a save) whose, its title as offered, its size (its bytes copied to
	the caller's) */
	char friend_name[P2P_FRIEND_NAME_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	int size;
};
/* the next job, if any (a save's bytes into bytes, capacity at least
P2P_FILESHARE_MAXIMUM_BYTES): its type, 0 none */
int p2p_fileshare_take_job(struct p2p_fileshare_job *job, unsigned char *bytes, int capacity);
/* a load done: the file's bytes and title (size 0: it could not be read
or did not pass, why) */
void p2p_fileshare_job_loaded(int serial, const unsigned char *bytes, int size, const char *title, const char *why);
/* a save done: saved (its title as saved), or refused (why) */
void p2p_fileshare_job_saved(int serial, int saved, const char *text);

/* the SHA-256 of bytes (the game's list's digests) */
void p2p_fileshare_digest(const unsigned char *bytes, int size, unsigned char *digest);
/* an outcome as the player reads it (in the language chosen) */
const char *p2p_fileshare_outcome_text(int outcome);
/* a kind's name ("map variant", "game variant") */
const char *p2p_fileshare_kind_text(int kind);

#endif
