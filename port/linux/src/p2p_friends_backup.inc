/*
P2P_FRIENDS_BACKUP.INC

The friend identity's backup (p2p_friends.h), part of p2p_friends.c, which
includes it at its end: this machine's friend identity (the Ed25519 seed its
key and so its friend code come from) and, if chosen, its friends list put
in a file sealed with a password, and restored from one on this machine or
another. The design, its threat model and what two machines with one
identity do: docs/superpowers/specs/2026-10-21-friends-backup-design.md.

- The file (<name>.hfb in the game's own folder: ux0:data/haloce-vita on the
  Vita, the save folder on a PC), BACKUP_FILE_SIZE bytes whatever the list
  holds (its size tells nothing of it):
    header, the AEAD's additional data (60 bytes): "HFBK", its version (1),
      its key derivation (1: Argon2id), its flags (1: the friends list is in
      it), 0, Argon2id's memory in KiB blocks and its passes (big-endian
      u32s), the salt (16 random bytes), the nonce (24 random bytes), the
      contents' size (u32);
    the contents sealed with XChaCha20-Poly1305 (Monocypher's), then its
      16-byte tag.
  The contents: "HFBP", their version (1), the flags again, 0, 0, the seed
  (32), its public key (32), the epoch of the run that wrote it (u64), when
  it was written (Unix time, u64), the friends list's size and marks.dat's
  (u32s), then friends.dat's and marks.dat's own bytes (each with its
  SHA-256, read as their files are: list_decode, marks_decode), zeros to the
  end.
- The password's key: Argon2id, 8 MiB and five passes (no less than OWASP's
  7 MiB and five), a 16-byte salt of its own each time: about a second on a
  Vita, where a guess costs the same. A file asking for less (or for more
  than a Vita spares: 32 MiB, 32 passes) is refused before any work.
- Every check before anything changes: the sizes, the header, the tag (a
  wrong password and an altered file look the same: the AEAD's tag), the
  contents' version, the seed giving the key the file names, the list and
  marks read into a state of their own as load reads them. Restoring keeps
  the identity it replaces (identity.dat, friends.dat, marks.dat as they
  are) in friends/replaced-<date>/ first; the new files are written list
  first, identity last, the old ones put back if one fails; a save of the
  old list waiting meanwhile is not written (files_generation).
- The restored identity's run is newer than the file's and than this
  machine's own (its epoch: friends take its messages, and drop those of a
  run older than it as replays).

The password is copied once into the job, wiped as soon as the key is made;
the key, the contents and the job's copies wiped as they are let go;
nothing of them is logged. The work runs on a thread of its own (the job's
state under backup_lock, taken alone or after friends_lock, never before
it); what touches friends' state runs on the caller's thread under
friends_lock.
*/

enum
{
	BACKUP_VERSION = 1,
	BACKUP_KDF_ARGON2ID = 1,
	BACKUP_FLAG_LIST = 1,
	/* Argon2id: its memory (KiB blocks) and passes as written, and what a
	file may ask (memory times passes no less than written) */
	BACKUP_BLOCKS = 8192,
	BACKUP_PASSES = 5,
	BACKUP_MINIMUM_BLOCKS = 8192,
	BACKUP_MAXIMUM_BLOCKS = 32768,
	BACKUP_MINIMUM_PASSES = 3,
	BACKUP_MAXIMUM_PASSES = 32,
	BACKUP_SALT_SIZE = 16,
	BACKUP_NONCE_SIZE = 24,
	BACKUP_TAG_SIZE = 16,
	BACKUP_HEADER_SIZE = 60,
	BACKUP_PLAIN_HEADER_SIZE = 96,
	BACKUP_PLAIN_SIZE = BACKUP_PLAIN_HEADER_SIZE + MAXIMUM_LIST_SIZE + MAXIMUM_MARKS_SIZE,
	BACKUP_FILE_SIZE = BACKUP_HEADER_SIZE + BACKUP_PLAIN_SIZE + BACKUP_TAG_SIZE,

	BACKUP_EXPORT = 1,
	BACKUP_RESTORE = 2,
	/* (milliseconds) a backup opened and neither confirmed nor cancelled
	(its menu left) is let go, wiped, after this long */
	BACKUP_READY_LIFETIME = 180000,
};

/* the job: one at a time */
static pthread_mutex_t backup_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	int state;
	/* its thread runs (or a restore is being confirmed) */
	int running;
	int operation;
	char path[PATH_SIZE];
	char password[P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM + 1];
	int password_size;
	int flags;
	/* the contents: to seal (export), or opened (restore, READY) */
	unsigned char *plain;
	/* (READY) its code and the friends in its list (-1 none); when it was
	opened */
	char code[P2P_FRIEND_CODE_SIZE];
	int friend_count;
	unsigned long ready_time;
	/* (SAVED) where it went */
	char said[PATH_SIZE];
} backup;

static void backup_wipe(void *bytes, int size)
{
	p2p_wipe(bytes, size);
}

/* the folder backups go to and come from */
static int backup_folder(char *path, int size)
{
#if defined(HALO_VITA) && !defined(P2P_FRIENDS_TEST)
	return snprintf(path, (size_t)size, "ux0:data/haloce-vita") < size;
#else
	return snprintf(path, (size_t)size, "%s", platform_save_root()) < size;
#endif
}

int p2p_friends_backup_name(const char *typed, char *name)
{
	int length = 0;
	const char *end;

	if (!typed)
		return 0;
	while (*typed == ' ')
		typed++;
	end = typed + strlen(typed);
	while (end > typed && end[-1] == ' ')
		end--;
	if (end - typed > 4 && !strncasecmp(end - 4, ".hfb", 4))
		end -= 4;
	for (; typed < end; typed++)
	{
		char character = *typed;

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_') ||
			length >= P2P_FRIENDS_BACKUP_NAME_SIZE - 1)
		{
			name[0] = 0;
			return 0;
		}
		name[length++] = character;
	}
	name[length] = 0;
	return length > 0;
}

/* a password as the key takes it: printable ASCII, the spaces at either end
dropped (the menus' typing drops them); its length, or -1 (a character past
printable ASCII) or -2 (too long) */
static int backup_password_take(const char *password, char *taken)
{
	const char *end;
	int length = 0;

	if (!password)
		password = "";
	while (*password == ' ')
		password++;
	end = password + strlen(password);
	while (end > password && end[-1] == ' ')
		end--;
	for (; password < end; password++)
	{
		if ((unsigned char)*password < 0x20 || (unsigned char)*password > 0x7E)
			return -1;
		if (length >= P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM)
			return -2;
		if (taken)
			taken[length] = *password;
		length++;
	}
	if (taken)
		taken[length] = 0;
	return length;
}

int p2p_friends_backup_password_check(const char *password)
{
	int length = backup_password_take(password, NULL);

	if (length < 0)
		return P2P_FRIENDS_BACKUP_BAD_PASSWORD;
	return length < P2P_FRIENDS_BACKUP_PASSWORD_MINIMUM ? P2P_FRIENDS_BACKUP_SHORT_PASSWORD : P2P_FRIENDS_BACKUP_IDLE;
}

static void backup_header(unsigned char *header, int flags, unsigned long blocks, unsigned long passes,
	const unsigned char *salt, const unsigned char *nonce, int plain_size)
{
	memcpy(header, "HFBK", 4);
	header[4] = BACKUP_VERSION;
	header[5] = BACKUP_KDF_ARGON2ID;
	header[6] = (unsigned char)flags;
	header[7] = 0;
	put_u32(header + 8, blocks);
	put_u32(header + 12, passes);
	memcpy(header + 16, salt, BACKUP_SALT_SIZE);
	memcpy(header + 32, nonce, BACKUP_NONCE_SIZE);
	put_u32(header + 56, (unsigned long)plain_size);
}

/* a file's bytes (BACKUP_FILE_SIZE) from its contents, sealed with a
password's key made with these memory and passes: 1, or 0 (no memory) */
static int backup_seal_file(const unsigned char *plain, int flags, const char *password, int password_size,
	unsigned long blocks, unsigned long passes, unsigned char *file)
{
	unsigned char salt[BACKUP_SALT_SIZE], nonce[BACKUP_NONCE_SIZE], key[P2P_PASSWORD_KEY_SIZE];

	posix_random_bytes(salt, sizeof(salt));
	posix_random_bytes(nonce, sizeof(nonce));
	if (!p2p_backup_key(password, password_size, salt, sizeof(salt), blocks, passes, key))
		return 0;
	backup_header(file, flags, blocks, passes, salt, nonce, BACKUP_PLAIN_SIZE);
	p2p_backup_seal(key, nonce, file, BACKUP_HEADER_SIZE, plain, BACKUP_PLAIN_SIZE, file + BACKUP_HEADER_SIZE);
	backup_wipe(key, sizeof(key));
	return 1;
}

/* a file's header checked (the sizes, its version, what its key asks):
P2P_FRIENDS_BACKUP_IDLE (fine) or why not; its flags, memory, passes and
contents' size */
static int backup_header_check(const unsigned char *file, int size, int *flags, unsigned long *blocks,
	unsigned long *passes, int *plain_size)
{
	if (size < 5 || memcmp(file, "HFBK", 4))
		return P2P_FRIENDS_BACKUP_DAMAGED;
	if (file[4] > BACKUP_VERSION)
		return P2P_FRIENDS_BACKUP_NEWER;
	if (file[4] != BACKUP_VERSION || size < BACKUP_HEADER_SIZE)
		return P2P_FRIENDS_BACKUP_DAMAGED;
	if (file[5] != BACKUP_KDF_ARGON2ID)
		return P2P_FRIENDS_BACKUP_NEWER;
	*flags = file[6];
	*blocks = get_u32(file + 8);
	*passes = get_u32(file + 12);
	*plain_size = (int)(get_u32(file + 56) & 0x7FFFFFFFUL);
	if ((*flags & ~BACKUP_FLAG_LIST) || file[7] || *blocks < BACKUP_MINIMUM_BLOCKS || *blocks > BACKUP_MAXIMUM_BLOCKS ||
		*passes < BACKUP_MINIMUM_PASSES || *passes > BACKUP_MAXIMUM_PASSES ||
		(unsigned long long)*blocks * *passes < (unsigned long long)BACKUP_BLOCKS * BACKUP_PASSES ||
		*plain_size < BACKUP_PLAIN_HEADER_SIZE || *plain_size > BACKUP_PLAIN_SIZE ||
		size != BACKUP_HEADER_SIZE + *plain_size + BACKUP_TAG_SIZE)
	{
		return P2P_FRIENDS_BACKUP_DAMAGED;
	}
	return P2P_FRIENDS_BACKUP_IDLE;
}

/* the contents opened checked, as far as they can be without friends'
state (the list and marks are read as load reads them when confirmed):
P2P_FRIENDS_BACKUP_IDLE (fine) or why not; the friends in the list (-1: no
list) */
static int backup_plain_check(const unsigned char *plain, int plain_size, int flags, int *friend_count)
{
	unsigned char key[P2P_KEY_SIZE], x25519[P2P_KEY_SIZE];
	unsigned long list_size, marks_size;
	int index;

	if (memcmp(plain, "HFBP", 4))
		return P2P_FRIENDS_BACKUP_DAMAGED;
	if (plain[4] > BACKUP_VERSION)
		return P2P_FRIENDS_BACKUP_NEWER;
	if (plain[4] != BACKUP_VERSION || plain[5] != flags || plain[6] || plain[7])
		return P2P_FRIENDS_BACKUP_DAMAGED;
	p2p_ed25519_public(plain + 8, key, NULL);
	if (!p2p_equal(key, plain + 40, P2P_KEY_SIZE) || !p2p_ed25519_to_x25519(key, x25519))
		return P2P_FRIENDS_BACKUP_DAMAGED;
	list_size = get_u32(plain + 88);
	marks_size = get_u32(plain + 92);
	if (list_size > MAXIMUM_LIST_SIZE || marks_size > MAXIMUM_MARKS_SIZE ||
		BACKUP_PLAIN_HEADER_SIZE + list_size + marks_size > (unsigned long)plain_size)
	{
		return P2P_FRIENDS_BACKUP_DAMAGED;
	}
	if (!(flags & BACKUP_FLAG_LIST))
	{
		if (list_size || marks_size)
			return P2P_FRIENDS_BACKUP_DAMAGED;
		*friend_count = -1;
	}
	else
	{
		if (list_size < LIST_HEADER_SIZE + P2P_SHA256_SIZE || memcmp(plain + BACKUP_PLAIN_HEADER_SIZE, "HFRL", 4))
			return P2P_FRIENDS_BACKUP_DAMAGED;
		*friend_count = plain[BACKUP_PLAIN_HEADER_SIZE + 5];
	}
	for (index = BACKUP_PLAIN_HEADER_SIZE + (int)(list_size + marks_size); index < plain_size; index++)
		if (plain[index])
			return P2P_FRIENDS_BACKUP_DAMAGED;
	return P2P_FRIENDS_BACKUP_IDLE;
}

/* a file opened with a password: P2P_FRIENDS_BACKUP_READY and its contents
(BACKUP_PLAIN_SIZE bytes, zeros past its own size) and their flags and
friends, or why not (plain then holds nothing of it) */
static int backup_open_file(const unsigned char *file, int size, const char *password, int password_size,
	unsigned char *plain, int *flags, int *friend_count)
{
	unsigned char key[P2P_PASSWORD_KEY_SIZE];
	unsigned long blocks, passes;
	int plain_size, outcome;

	outcome = backup_header_check(file, size, flags, &blocks, &passes, &plain_size);
	if (outcome != P2P_FRIENDS_BACKUP_IDLE)
		return outcome;
	if (!p2p_backup_key(password, password_size, file + 16, BACKUP_SALT_SIZE, blocks, passes, key))
		return P2P_FRIENDS_BACKUP_NO_MEMORY;
	memset(plain, 0, BACKUP_PLAIN_SIZE);
	outcome = p2p_backup_open(key, file + 32, file, BACKUP_HEADER_SIZE, file + BACKUP_HEADER_SIZE, plain_size, plain) ?
		backup_plain_check(plain, plain_size, *flags, friend_count) : P2P_FRIENDS_BACKUP_WRONG_PASSWORD;
	backup_wipe(key, sizeof(key));
	if (outcome != P2P_FRIENDS_BACKUP_IDLE)
	{
		backup_wipe(plain, BACKUP_PLAIN_SIZE);
		return outcome;
	}
	return P2P_FRIENDS_BACKUP_READY;
}

/* bytes written to a path as file_put writes friends' files (a .tmp
written, read back and compared, the old file renamed .old, the new one
renamed in, the old one deleted; whatever fails, the old file is where it
was) */
static int backup_path_put(const char *path, const unsigned char *bytes, int size)
{
	char temporary[PATH_SIZE + 8], old[PATH_SIZE + 8];
	unsigned char *check;
	int replaced = 0, result;

	if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary) ||
		snprintf(old, sizeof(old), "%s.old", path) >= (int)sizeof(old))
	{
		return 0;
	}
	check = malloc((size_t)size + 1);
	if (!check)
		return 0;
	remove(temporary);
	if (!file_write(temporary, bytes, size))
	{
		free(check);
		return 0;
	}
	result = file_read(temporary, check, size + 1);
	if (result != size || memcmp(check, bytes, (size_t)size))
	{
		free(check);
		remove(temporary);
		return 0;
	}
	free(check);
	if (file_exists(path))
	{
		remove(old);
		if (rename(path, old) != 0)
		{
			remove(temporary);
			return 0;
		}
		replaced = 1;
	}
	if (rename(temporary, path) != 0)
	{
		remove(temporary);
		if (replaced)
			rename(old, path);
		return 0;
	}
	if (replaced)
		remove(old);
	return 1;
}

/* (the job's thread) the file sealed and written, or opened */
static void *backup_thread(void *unused)
{
	unsigned char *file = malloc(BACKUP_FILE_SIZE + 1);
	int outcome, flags = 0, friend_count = -1, size;

	(void)unused;
	/* (the job's fields are the thread's while it runs: the calls that
	start a job refuse while it does, and the readers read only what is set
	before its state, under backup_lock) */
	if (!file)
		outcome = P2P_FRIENDS_BACKUP_NO_MEMORY;
	else if (backup.operation == BACKUP_EXPORT)
	{
		if (!backup_seal_file(backup.plain, backup.flags, backup.password, backup.password_size, BACKUP_BLOCKS,
			BACKUP_PASSES, file))
		{
			outcome = P2P_FRIENDS_BACKUP_NO_MEMORY;
		}
		else
			outcome = backup_path_put(backup.path, file, BACKUP_FILE_SIZE) ? P2P_FRIENDS_BACKUP_SAVED :
				P2P_FRIENDS_BACKUP_WRITE_FAILED;
		backup_wipe(backup.plain, BACKUP_PLAIN_SIZE);
	}
	else
	{
		size = file_read(backup.path, file, BACKUP_FILE_SIZE + 1);
		outcome = size < 0 ? P2P_FRIENDS_BACKUP_NO_FILE : backup_open_file(file, size, backup.password,
			backup.password_size, backup.plain, &flags, &friend_count);
		if (outcome == P2P_FRIENDS_BACKUP_READY)
		{
			char code[P2P_FRIEND_CODE_LENGTH + 1];

			code_of_key(backup.plain + 40, code);
			code_shown(code, backup.code, sizeof(backup.code));
			backup.friend_count = friend_count;
			backup.flags = flags;
			backup.ready_time = now_stamp();
		}
	}
	backup_wipe(backup.password, sizeof(backup.password));
	backup.password_size = 0;
	free(file);
	if (outcome == P2P_FRIENDS_BACKUP_SAVED)
		platform_log("friends: backup: written to %s (%s)", backup.said, backup.flags & BACKUP_FLAG_LIST ?
			"the friend code and the friends list" : "the friend code alone");
	else if (outcome == P2P_FRIENDS_BACKUP_READY)
		platform_log("friends: backup: %s opened: the friend code %s, %d friends (%s)", backup.said, backup.code,
			friend_count < 0 ? 0 : friend_count, friend_count < 0 ? "no list in it" : "its list");
	else
		platform_log("friends: backup: %s: %s", backup.said, p2p_friends_backup_text(outcome));
	pthread_mutex_lock(&backup_lock);
	if (outcome != P2P_FRIENDS_BACKUP_READY)
	{
		free(backup.plain);
		backup.plain = NULL;
	}
	backup.state = outcome;
	backup.running = 0;
	pthread_mutex_unlock(&backup_lock);
	return NULL;
}

/* (under backup_lock) what a job held let go */
static void backup_clear(void)
{
	if (backup.plain)
	{
		backup_wipe(backup.plain, BACKUP_PLAIN_SIZE);
		free(backup.plain);
		backup.plain = NULL;
	}
	backup_wipe(backup.password, sizeof(backup.password));
	backup.password_size = 0;
	backup.code[0] = 0;
	backup.friend_count = -1;
}

/* (under backup_lock) a backup opened long ago and left let go */
static void backup_expire_locked(void)
{
	if (!backup.running && backup.state == P2P_FRIENDS_BACKUP_READY && elapsed(backup.ready_time, BACKUP_READY_LIFETIME))
	{
		backup_clear();
		backup.state = P2P_FRIENDS_BACKUP_IDLE;
		platform_log("friends: backup: the backup opened and left let go (not restored)");
	}
}

/* (internet play's thread, each pass) the same, taking backup_lock */
static void backup_expire(void)
{
	pthread_mutex_lock(&backup_lock);
	backup_expire_locked();
	pthread_mutex_unlock(&backup_lock);
}

/* a job started (its fields set: the path, the password, the contents):
P2P_FRIENDS_BACKUP_WORKING or why not. Takes backup_lock */
static int backup_start(int operation, const char *name, const char *password, unsigned char *plain, int flags)
{
	char folder[PATH_SIZE], typed[P2P_FRIENDS_BACKUP_NAME_SIZE];
	pthread_attr_t attributes;
	pthread_t thread;
	int started;

	pthread_mutex_lock(&backup_lock);
	if (backup.running)
	{
		pthread_mutex_unlock(&backup_lock);
		return P2P_FRIENDS_BACKUP_BUSY;
	}
	/* (one opened and not confirmed, or an outcome not read: let go) */
	backup_clear();
	if (!p2p_friends_backup_name(name, typed) || !backup_folder(folder, sizeof(folder)) ||
		snprintf(backup.path, sizeof(backup.path), "%s/%s.hfb", folder, typed) >= (int)sizeof(backup.path))
	{
		pthread_mutex_unlock(&backup_lock);
		return P2P_FRIENDS_BACKUP_BAD_NAME;
	}
	snprintf(backup.said, sizeof(backup.said), "%s", backup.path);
	backup.password_size = backup_password_take(password, backup.password);
	backup.operation = operation;
	backup.flags = flags;
	backup.plain = plain;
	backup.state = P2P_FRIENDS_BACKUP_WORKING;
	backup.running = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	started = pthread_create(&thread, &attributes, backup_thread, NULL) == 0;
	pthread_attr_destroy(&attributes);
	if (!started)
	{
		backup.plain = NULL;
		backup.running = 0;
		backup.state = P2P_FRIENDS_BACKUP_IDLE;
		backup_wipe(backup.password, sizeof(backup.password));
		backup.password_size = 0;
		pthread_mutex_unlock(&backup_lock);
		return P2P_FRIENDS_BACKUP_NO_MEMORY;
	}
	pthread_mutex_unlock(&backup_lock);
	return P2P_FRIENDS_BACKUP_WORKING;
}

/* the checks every start makes first: P2P_FRIENDS_BACKUP_IDLE (fine) or
why not */
static int backup_start_check(const char *name, const char *password)
{
	char typed[P2P_FRIENDS_BACKUP_NAME_SIZE];
	int outcome = p2p_friends_backup_password_check(password);

	if (!p2p_friends_backup_name(name, typed))
		return P2P_FRIENDS_BACKUP_BAD_NAME;
	return outcome;
}

int p2p_friends_backup_export(const char *name, const char *password, int with_list)
{
	unsigned char *plain;
	int outcome = backup_start_check(name, password), flags = with_list ? BACKUP_FLAG_LIST : 0;
	unsigned long list_size = 0, marks_size = 0;

	if (outcome != P2P_FRIENDS_BACKUP_IDLE)
		return outcome;
	plain = calloc(1, BACKUP_PLAIN_SIZE);
	if (!plain)
		return P2P_FRIENDS_BACKUP_NO_MEMORY;
	pthread_mutex_lock(&friends_lock);
	load();
	memcpy(plain, "HFBP", 4);
	plain[4] = BACKUP_VERSION;
	plain[5] = (unsigned char)flags;
	memcpy(plain + 8, friends->seed, P2P_SEED_SIZE);
	memcpy(plain + 40, friends->key, P2P_KEY_SIZE);
	put_u64(plain + 72, friends->epoch);
	put_u64(plain + 80, unix_now());
	if (with_list)
	{
		list_size = (unsigned long)list_encode(plain + BACKUP_PLAIN_HEADER_SIZE);
		marks_size = (unsigned long)marks_encode(plain + BACKUP_PLAIN_HEADER_SIZE + list_size);
	}
	put_u32(plain + 88, list_size);
	put_u32(plain + 92, marks_size);
	pthread_mutex_unlock(&friends_lock);
	outcome = backup_start(BACKUP_EXPORT, name, password, plain, flags);
	if (outcome != P2P_FRIENDS_BACKUP_WORKING)
	{
		backup_wipe(plain, BACKUP_PLAIN_SIZE);
		free(plain);
	}
	return outcome;
}

int p2p_friends_backup_restore(const char *name, const char *password)
{
	unsigned char *plain;
	int outcome = backup_start_check(name, password);

	/* (a password shorter than a backup's: none opens with it, said as such) */
	if (outcome == P2P_FRIENDS_BACKUP_SHORT_PASSWORD)
		return P2P_FRIENDS_BACKUP_WRONG_PASSWORD;
	if (outcome != P2P_FRIENDS_BACKUP_IDLE)
		return outcome;
	plain = calloc(1, BACKUP_PLAIN_SIZE);
	if (!plain)
		return P2P_FRIENDS_BACKUP_NO_MEMORY;
	outcome = backup_start(BACKUP_RESTORE, name, password, plain, 0);
	if (outcome != P2P_FRIENDS_BACKUP_WORKING)
		free(plain);
	return outcome;
}

int p2p_friends_backup_state(char *text, int size)
{
	int state;

	pthread_mutex_lock(&backup_lock);
	backup_expire_locked();
	state = backup.state;
	if (text && size > 0)
	{
		if (state == P2P_FRIENDS_BACKUP_SAVED)
			snprintf(text, (size_t)size, T("Saved to %s"), backup.said);
		else
			snprintf(text, (size_t)size, "%s", p2p_friends_backup_text(state));
	}
	/* (an outcome: given once) */
	if (state != P2P_FRIENDS_BACKUP_WORKING && state != P2P_FRIENDS_BACKUP_READY)
		backup.state = P2P_FRIENDS_BACKUP_IDLE;
	pthread_mutex_unlock(&backup_lock);
	return state;
}

int p2p_friends_backup_found(char *code, char *current_code, int code_size, int *friend_count)
{
	int found;

	pthread_mutex_lock(&backup_lock);
	backup_expire_locked();
	found = backup.state == P2P_FRIENDS_BACKUP_READY && backup.plain && !backup.running;
	if (found)
	{
		if (code)
			snprintf(code, (size_t)code_size, "%s", backup.code);
		if (friend_count)
			*friend_count = backup.friend_count;
	}
	pthread_mutex_unlock(&backup_lock);
	if (found && current_code)
		p2p_friends_code(current_code, code_size);
	return found;
}

void p2p_friends_backup_cancel(void)
{
	pthread_mutex_lock(&backup_lock);
	if (!backup.running)
	{
		if (backup.state == P2P_FRIENDS_BACKUP_READY)
			platform_log("friends: backup: the backup opened let go (not restored)");
		backup_clear();
		if (backup.state == P2P_FRIENDS_BACKUP_READY)
			backup.state = P2P_FRIENDS_BACKUP_IDLE;
	}
	pthread_mutex_unlock(&backup_lock);
}

/* (under friends_lock) the identity replaced kept as its files are, in
friends/replaced-<date>/: 1, or 0 (nothing to keep it in) */
static int backup_keep_replaced(const unsigned char *identity, int identity_size, const unsigned char *list,
	int list_size, const unsigned char *marks, int marks_size, char *kept, int kept_size)
{
	char folder[PATH_SIZE], name[64], path[PATH_SIZE];
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	int attempt, written;

	if (!friends_folder(folder, sizeof(folder)))
		return 0;
	posix_make_directory(folder);
	for (attempt = 0; attempt < 100; attempt++)
	{
		if (local && local->tm_year >= 100 && local->tm_year <= 8099)
			snprintf(name, sizeof(name), "replaced-%04d%02d%02d-%02d%02d%02d", local->tm_year + 1900, local->tm_mon + 1,
				local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
		else
			snprintf(name, sizeof(name), "replaced-%llu", unix_now());
		if (attempt)
			snprintf(name + strlen(name), sizeof(name) - strlen(name), "-%d", attempt + 1);
		if (!file_path(name, "/identity.dat", path, sizeof(path)))
			return 0;
		if (!file_exists(path))
			break;
	}
	if (attempt >= 100 || !file_path(name, "", path, sizeof(path)))
		return 0;
	posix_make_directory(path);
	snprintf(path, sizeof(path), "%s/identity.dat", name);
	written = file_put(path, identity, identity_size);
	snprintf(path, sizeof(path), "%s/friends.dat", name);
	written = written && file_put(path, list, list_size);
	snprintf(path, sizeof(path), "%s/marks.dat", name);
	written = written && file_put(path, marks, marks_size);
	if (written)
		snprintf(kept, (size_t)kept_size, "friends/%s", name);
	return written;
}

/* (under friends_lock) the restored identity and lists (a state read as
load reads the files) made this machine's, its run's epoch given: the old
identity's friends told it went offline, its party left, what was between
it and others let go */
static void backup_take(const struct friends_state *restored, unsigned long long epoch)
{
	int index;

	if (friends->party.active)
		party_leave_now(friends_on(), T("your friend code was restored"));
	if (friends_on() && friends->presence_online && friends->friend_count)
	{
		presence_sign(0, 0);
		for (index = 0; index < friends->friend_count; index++)
			presence_send(&friends->friends[index]);
	}
	memcpy(friends->seed, restored->seed, sizeof(friends->seed));
	memcpy(friends->key, restored->key, sizeof(friends->key));
	memcpy(friends->x25519_secret, restored->x25519_secret, sizeof(friends->x25519_secret));
	memcpy(friends->x25519_public, restored->x25519_public, sizeof(friends->x25519_public));
	memcpy(friends->code, restored->code, sizeof(friends->code));
	memcpy(friends->inbox_topic, restored->inbox_topic, sizeof(friends->inbox_topic));
	memcpy(friends->mailbox_topic, restored->mailbox_topic, sizeof(friends->mailbox_topic));
	memcpy(friends->inbox_key, restored->inbox_key, sizeof(friends->inbox_key));
	memcpy(friends->code_recipient, restored->code_recipient, sizeof(friends->code_recipient));
	friends->has_identity = 1;
	friends->epoch = epoch;
	friends->counter = 0;
	friends->epoch_written = 1;
	/* the lists */
	memcpy(friends->friends, restored->friends, sizeof(friends->friends));
	friends->friend_count = restored->friend_count;
	memcpy(friends->requests, restored->requests, sizeof(friends->requests));
	friends->request_count = restored->request_count;
	for (index = 0; index < friends->request_count; index++)
		friends->requests[index].heard_time = now_stamp();
	memcpy(friends->outgoing, restored->outgoing, sizeof(friends->outgoing));
	friends->outgoing_count = restored->outgoing_count;
	memcpy(friends->blocked, restored->blocked, sizeof(friends->blocked));
	friends->blocked_count = restored->blocked_count;
	memcpy(friends->tombstones, restored->tombstones, sizeof(friends->tombstones));
	friends->tombstone_count = restored->tombstone_count;
	memcpy(friends->muted, restored->muted, sizeof(friends->muted));
	friends->muted_count = restored->muted_count;
	memcpy(friends->marks, restored->marks, sizeof(friends->marks));
	friends->mark_count = restored->mark_count;
	/* what was the old identity's: its invites, its party, the requests
	heard for it, what waited to be read, its presence */
	friends->invite_count = 0;
	memset(&friends->party, 0, sizeof(friends->party));
	friends->party_invite_count = 0;
	memset(friends->party_seen, 0, sizeof(friends->party_seen));
	friends->chat_count = 0;
	memset(friends->seen, 0, sizeof(friends->seen));
	friends->seen_next = 0;
	friends->inbound_count = 0;
	friends->card_action = 0;
	friends->presence_size = 0;
	friends->presence_online = 0;
	friends->presence_joinable = 0;
	friends->presence_want_reply = 0;
	friends->activity_changed = 1;
	friends->test_last_sent_size = 0;
	/* (its files written already) */
	friends->dirty = 0;
	friends->counters_dirty = 0;
	friends->saved_time = friends->counters_saved_time = now_stamp();
}

/* (under friends_lock) the contents opened (checked: backup_plain_check)
read into a state of their own as load reads the files - the identity, and
the list and marks (each key once, never the identity's own): 1, or 0 if
they are not what this build writes */
static int backup_read_plain(const unsigned char *plain, int flags, struct friends_state *restored)
{
	struct friends_state *live = friends;
	unsigned long list_size = get_u32(plain + 88), marks_size = get_u32(plain + 92);
	int read;

	if (list_size > MAXIMUM_LIST_SIZE || marks_size > MAXIMUM_MARKS_SIZE)
		return 0;
	friends = restored;
	memcpy(friends->seed, plain + 8, P2P_SEED_SIZE);
	identity_from_seed();
	read = p2p_equal(friends->key, plain + 40, P2P_KEY_SIZE);
	if (read && (flags & BACKUP_FLAG_LIST))
		read = list_decode(plain + BACKUP_PLAIN_HEADER_SIZE, (int)list_size) &&
			(!marks_size || marks_decode(plain + BACKUP_PLAIN_HEADER_SIZE + list_size, (int)marks_size));
	friends = live;
	return read;
}

/* (under friends_lock) the contents opened made this machine's identity:
P2P_FRIENDS_BACKUP_RESTORED or why not (nothing changed then) */
static int backup_apply(const unsigned char *plain, int flags)
{
	struct friends_state *live = friends, *restored = calloc(1, sizeof(*restored));
	unsigned char *bytes = malloc(2 * (IDENTITY_SIZE + MAXIMUM_LIST_SIZE + MAXIMUM_MARKS_SIZE));
	unsigned char *old_identity, *old_list, *old_marks, *new_identity, *new_list, *new_marks;
	int old_identity_size, old_list_size, old_marks_size, new_identity_size = 0, new_list_size = 0, new_marks_size = 0;
	unsigned long long epoch;
	char kept[PATH_SIZE], shown[P2P_FRIEND_CODE_SIZE];
	int read, outcome = P2P_FRIENDS_BACKUP_RESTORED;

	if (!restored || !bytes)
	{
		free(restored);
		free(bytes);
		return P2P_FRIENDS_BACKUP_NO_MEMORY;
	}
	old_identity = bytes;
	old_list = old_identity + IDENTITY_SIZE;
	old_marks = old_list + MAXIMUM_LIST_SIZE;
	new_identity = old_marks + MAXIMUM_MARKS_SIZE;
	new_list = new_identity + IDENTITY_SIZE;
	new_marks = new_list + MAXIMUM_LIST_SIZE;
	read = backup_read_plain(plain, flags, restored);
	/* (its run: newer than the file's, and than this machine's own if it is
	the same identity, whose friends have heard this run) */
	epoch = get_u64(plain + 72) + 1;
	if (epoch < unix_now())
		epoch = unix_now();
	if (p2p_equal(restored->key, live->key, P2P_KEY_SIZE) && epoch <= live->epoch)
		epoch = live->epoch + 1;
	if (read)
	{
		friends = restored;
		new_identity_size = identity_encode(restored->seed, epoch, new_identity);
		new_list_size = list_encode(new_list);
		new_marks_size = marks_encode(new_marks);
	}
	friends = live;
	if (!read)
	{
		outcome = P2P_FRIENDS_BACKUP_DAMAGED;
		goto done;
	}
	old_identity_size = identity_encode(friends->seed, friends->epoch, old_identity);
	old_list_size = list_encode(old_list);
	old_marks_size = marks_encode(old_marks);
	/* the identity replaced kept, then the new files written: the list
	first, the identity last; the old ones back if one fails */
	pthread_mutex_lock(&friends_file_lock);
	if (!backup_keep_replaced(old_identity, old_identity_size, old_list, old_list_size, old_marks, old_marks_size, kept,
		sizeof(kept)))
	{
		outcome = P2P_FRIENDS_BACKUP_WRITE_FAILED;
	}
	else if (!file_put("friends.dat", new_list, new_list_size))
		outcome = P2P_FRIENDS_BACKUP_WRITE_FAILED;
	else if (!file_put("marks.dat", new_marks, new_marks_size))
	{
		file_put("friends.dat", old_list, old_list_size);
		outcome = P2P_FRIENDS_BACKUP_WRITE_FAILED;
	}
	else if (!file_put("identity.dat", new_identity, new_identity_size))
	{
		file_put("friends.dat", old_list, old_list_size);
		file_put("marks.dat", old_marks, old_marks_size);
		outcome = P2P_FRIENDS_BACKUP_WRITE_FAILED;
	}
	/* (a save of the old list waiting for the files meanwhile: not written) */
	friends->files_generation++;
	pthread_mutex_unlock(&friends_file_lock);
	if (outcome != P2P_FRIENDS_BACKUP_RESTORED)
		goto done;
	backup_take(restored, epoch);
	code_shown(friends->code, shown, sizeof(shown));
	platform_log("friends: backup: restored: this machine's friend code is now %s (%d friends, %d requests, %d sent, "
		"%d blocked; the one it replaced kept in %s)", shown, friends->friend_count, friends->request_count,
		friends->outgoing_count, friends->blocked_count, kept);
done:
	backup_wipe(restored, (int)sizeof(*restored));
	free(restored);
	backup_wipe(bytes, 2 * (IDENTITY_SIZE + MAXIMUM_LIST_SIZE + MAXIMUM_MARKS_SIZE));
	free(bytes);
	return outcome;
}

int p2p_friends_backup_confirm(void)
{
	unsigned char *plain;
	int flags, outcome;

	pthread_mutex_lock(&backup_lock);
	backup_expire_locked();
	if (backup.running || backup.state != P2P_FRIENDS_BACKUP_READY || !backup.plain)
	{
		pthread_mutex_unlock(&backup_lock);
		return P2P_FRIENDS_BACKUP_NOTHING;
	}
	/* (the job's while it is confirmed: no other starts) */
	plain = backup.plain;
	backup.plain = NULL;
	flags = backup.flags;
	backup.running = 1;
	backup.state = P2P_FRIENDS_BACKUP_WORKING;
	pthread_mutex_unlock(&backup_lock);
	pthread_mutex_lock(&friends_lock);
	load();
	outcome = backup_apply(plain, flags);
	pthread_mutex_unlock(&friends_lock);
	if (outcome != P2P_FRIENDS_BACKUP_RESTORED)
		platform_log("friends: backup: not restored: %s", p2p_friends_backup_text(outcome));
	backup_wipe(plain, BACKUP_PLAIN_SIZE);
	free(plain);
	pthread_mutex_lock(&backup_lock);
	backup_clear();
	backup.running = 0;
	backup.state = P2P_FRIENDS_BACKUP_IDLE;
	pthread_mutex_unlock(&backup_lock);
	return outcome;
}

const char *p2p_friends_backup_text(int outcome)
{
	switch (outcome)
	{
	case P2P_FRIENDS_BACKUP_WORKING:
		return T("Working on it: a few seconds...");
	case P2P_FRIENDS_BACKUP_SAVED:
		return T("Backup saved");
	case P2P_FRIENDS_BACKUP_READY:
		return T("Backup opened: confirm to restore it");
	case P2P_FRIENDS_BACKUP_RESTORED:
		return T("Friend code restored");
	case P2P_FRIENDS_BACKUP_BUSY:
		return T("A backup is already being worked on");
	case P2P_FRIENDS_BACKUP_SHORT_PASSWORD:
		return T("The password needs 8 characters or more");
	case P2P_FRIENDS_BACKUP_BAD_PASSWORD:
		return T("Use letters, digits and symbols (64 at most)");
	case P2P_FRIENDS_BACKUP_MISMATCH:
		return T("The passwords don't match");
	case P2P_FRIENDS_BACKUP_BAD_NAME:
		return T("A file name is letters, digits, - and _");
	case P2P_FRIENDS_BACKUP_NO_FILE:
		return T("No backup file with that name");
	case P2P_FRIENDS_BACKUP_WRONG_PASSWORD:
		return T("Wrong password, or the file was changed");
	case P2P_FRIENDS_BACKUP_DAMAGED:
		return T("That file is damaged, or not a backup");
	case P2P_FRIENDS_BACKUP_NEWER:
		return T("That backup is from a newer version");
	case P2P_FRIENDS_BACKUP_NO_MEMORY:
		return T("Not enough memory: try from the main menu");
	case P2P_FRIENDS_BACKUP_WRITE_FAILED:
		return T("Couldn't write the file (memory card full?)");
	case P2P_FRIENDS_BACKUP_NOTHING:
		return T("Nothing to restore");
	}
	return "";
}

#ifndef HALO_RELEASE
/* (debug: HALO_TEST_FRIENDS's backup=, backup-code= and restore= steps,
under friends_lock) a backup written, or one opened and confirmed, at its
second; how it went logged */
static void test_backup_steps(void)
{
	char name[P2P_FRIENDS_BACKUP_NAME_SIZE], password[P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM + 1];
	char text[PATH_SIZE + 64];
	int outcome, state, with_list = friends->test_backup_list;

	memcpy(name, friends->test_backup_name, sizeof(name));
	memcpy(password, friends->test_backup_password, sizeof(password));
	if (friends->test_backup_at >= 0 && !friends->test_backup_started && test_due(friends->test_backup_at))
	{
		friends->test_backup_started = 1;
		pthread_mutex_unlock(&friends_lock);
		outcome = p2p_friends_backup_export(name, password, with_list);
		platform_log("friends: test: backing up the friend code%s to %s: %s", with_list ? " and the list" : "", name,
			p2p_friends_backup_text(outcome));
		pthread_mutex_lock(&friends_lock);
		if (outcome != P2P_FRIENDS_BACKUP_WORKING)
			friends->test_backup_started = 2;
	}
	if (friends->test_restore_at >= 0 && !friends->test_restore_started && test_due(friends->test_restore_at))
	{
		friends->test_restore_started = 1;
		pthread_mutex_unlock(&friends_lock);
		outcome = p2p_friends_backup_restore(name, password);
		platform_log("friends: test: restoring the friend code from %s: %s", name, p2p_friends_backup_text(outcome));
		pthread_mutex_lock(&friends_lock);
		if (outcome != P2P_FRIENDS_BACKUP_WORKING)
			friends->test_restore_started = 2;
	}
	backup_wipe(password, sizeof(password));
	if (friends->test_backup_started != 1 && friends->test_restore_started != 1)
		return;
	pthread_mutex_unlock(&friends_lock);
	state = p2p_friends_backup_state(text, sizeof(text));
	if (state == P2P_FRIENDS_BACKUP_READY)
	{
		outcome = p2p_friends_backup_confirm();
		platform_log("friends: test: the backup confirmed: %s", p2p_friends_backup_text(outcome));
	}
	else if (state != P2P_FRIENDS_BACKUP_WORKING)
		platform_log("friends: test: the backup: %s", text);
	pthread_mutex_lock(&friends_lock);
	if (state != P2P_FRIENDS_BACKUP_WORKING)
	{
		if (friends->test_backup_started == 1)
			friends->test_backup_started = 2;
		if (friends->test_restore_started == 1)
			friends->test_restore_started = 2;
	}
}
#endif
