/*
P2P_FILESHARE.INC

File Share with friends (p2p_fileshare.h), part of p2p_friends.c (its
helpers, its lock and its state; the constants and structures:
p2p_fileshare_types.inc). The design, the messages, the limits and the
threats: docs/fileshare-friends.md.

Every message is a friends' pair message, read by pair_heard as presence
is (the key that opened it is the sender's, the recipient this machine),
then here: a friend's budget, the signature, the replay window, then the
message's own checks. Nothing here touches a file but the marks
(friends/fileshare.dat): the game reads and saves the variants
(port/linux/game/fileshare_friends.c, through the jobs).

(debug, not in release builds) HALO_TEST_FILESHARE: the tests' steps of
this side (fileshare_test_parse below; the game's side reads its own of the
same variable).
*/

/* ---------- small helpers */

static int fileshare_sharing_on(void)
{
	return setting_on("friends.fileshare", "HALO_FRIENDS_FILESHARE");
}

static int fileshare_kind_valid(int kind)
{
	return kind == P2P_FILESHARE_KIND_MAP_VARIANT || kind == P2P_FILESHARE_KIND_GAME_VARIANT;
}

/* a size a kind may have (the cap: never more than P2P_FILESHARE_MAXIMUM_BYTES) */
static int fileshare_size_valid(int kind, unsigned long size)
{
	if (kind == P2P_FILESHARE_KIND_GAME_VARIANT)
		return size == P2P_FILESHARE_GAME_VARIANT_BYTES;
	return kind == P2P_FILESHARE_KIND_MAP_VARIANT && size >= FILES_MAP_VARIANT_MINIMUM_BYTES &&
		size <= P2P_FILESHARE_MAXIMUM_BYTES;
}

/* a map's short name ([a-z0-9_], 1 to 31) */
static int fileshare_map_name_valid(const char *name)
{
	int length = 0;

	for (; name[length]; length++)
		if (!((name[length] >= 'a' && name[length] <= 'z') || (name[length] >= '0' && name[length] <= '9') ||
			name[length] == '_'))
		{
			return 0;
		}
	return length >= 1 && length <= FILES_MAP_LENGTH;
}

/* a mark's names: a map variant's map and <stem>.hmv (the stem 1 to 28 of
[a-z0-9_-], forge_files.h), a game variant's saved game folder (1 to 39 of
[A-Za-z0-9_-]) and no map */
static int fileshare_names_valid(int kind, const char *map, const char *file)
{
	int length = (int)strlen(file), index;

	if (kind == P2P_FILESHARE_KIND_MAP_VARIANT)
	{
		if (!fileshare_map_name_valid(map) || length < 5 || length > 28 + 4 || strcmp(file + length - 4, ".hmv"))
			return 0;
		for (index = 0; index < length - 4; index++)
			if (!((file[index] >= 'a' && file[index] <= 'z') || (file[index] >= '0' && file[index] <= '9') ||
				file[index] == '_' || file[index] == '-'))
			{
				return 0;
			}
		return 1;
	}
	if (kind != P2P_FILESHARE_KIND_GAME_VARIANT || map[0] || length < 1 || length >= P2P_FILESHARE_FILE_SIZE)
		return 0;
	for (index = 0; index < length; index++)
		if (!((file[index] >= 'a' && file[index] <= 'z') || (file[index] >= 'A' && file[index] <= 'Z') ||
			(file[index] >= '0' && file[index] <= '9') || file[index] == '_' || file[index] == '-'))
		{
			return 0;
		}
	return 1;
}

static int fileshare_chunk_size(unsigned long size, int index)
{
	int count = (int)((size + FILES_CHUNK_BYTES - 1) / FILES_CHUNK_BYTES);

	if (index < 0 || index >= count)
		return 0;
	return index < count - 1 ? FILES_CHUNK_BYTES : (int)(size - (unsigned long)index * FILES_CHUNK_BYTES);
}

static int bit_get(const unsigned char *mask, int index)
{
	return (mask[index >> 3] >> (index & 7)) & 1;
}

static void bit_set(unsigned char *mask, int index, int on)
{
	if (on)
		mask[index >> 3] |= (unsigned char)(1 << (index & 7));
	else
		mask[index >> 3] &= (unsigned char)~(1 << (index & 7));
}

static void fileshare_status(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(friends->files.status, sizeof(friends->files.status), format, arguments);
	va_end(arguments);
	friends->files.status_serial++;
	platform_log("fileshare: %s", friends->files.status);
}

static const char *fileshare_end_text(int reason)
{
	switch (reason)
	{
	case FILES_END_SAVED:
		return "saved";
	case FILES_END_DECLINED:
		return "declined";
	case FILES_END_CANCELLED:
		return "cancelled";
	case FILES_END_NOT_SHARING:
		return "not sharing";
	case FILES_END_NOT_SHARED:
		return "not shared";
	case FILES_END_BUSY:
		return "busy";
	case FILES_END_TOO_LARGE:
		return "too large";
	case FILES_END_REFUSED:
		return "refused by the validator";
	case FILES_END_TOO_SOON:
		return "too soon";
	}
	return "?";
}

/* a friend's name, or their code's start */
static void fileshare_friend_name(const unsigned char *key, char *name)
{
	struct friend_record *record = find_friend(key);

	if (record && record->name[0])
		snprintf(name, P2P_FRIEND_NAME_SIZE, "%s", record->name);
	else
	{
		char code[P2P_FRIEND_CODE_LENGTH + 1];

		code_of_key(key, code);
		snprintf(name, P2P_FRIEND_NAME_SIZE, "%.4s-%.4s", code, code + 4);
	}
}

/* (friends_lock held) whether this machine may send anything of file share:
DONE, or why not */
static int fileshare_gate(void)
{
	if (!friends_on())
		return P2P_FILESHARE_OFFLINE;
	if (friends->appear_offline || appear_offline_now())
		return P2P_FILESHARE_APPEARING_OFFLINE;
	if (!fileshare_sharing_on())
		return P2P_FILESHARE_SHARING_OFF;
	return P2P_FILESHARE_DONE;
}

/* a friend by their entry's id */
static struct friend_record *fileshare_friend_of(const char *friend_id)
{
	int index;

	if (!friend_id || !entry_key(friend_id, P2P_FRIEND_KIND_FRIEND, &index))
		return NULL;
	return &friends->friends[index];
}

/* ---------- the messages' bodies */

static int fileshare_body_encode(const struct message *message, unsigned char *bytes)
{
	int size = FILES_ID_LENGTH, index;

	memcpy(bytes, message->files_id, FILES_ID_LENGTH);
	switch (message->type)
	{
	case TYPE_FILES_ASK:
		break;
	case TYPE_FILES_LIST:
		bytes[size++] = (unsigned char)message->files_page;
		bytes[size++] = (unsigned char)message->files_pages;
		bytes[size++] = (unsigned char)message->files_count;
		for (index = 0; index < message->files_count; index++)
		{
			const struct files_entry *entry = &message->files_entries[index];

			bytes[size++] = (unsigned char)entry->kind;
			memcpy(bytes + size, entry->file, FILES_FILE_ID_LENGTH);
			size += FILES_FILE_ID_LENGTH;
			put_u32(bytes + size, entry->size);
			size += 4;
			size += put_text(bytes + size, entry->map, FILES_MAP_LENGTH);
			size += put_text(bytes + size, entry->title, FILES_TITLE_LENGTH);
		}
		break;
	case TYPE_FILES_GET:
		memcpy(bytes + size, message->files_file, FILES_FILE_ID_LENGTH);
		size += FILES_FILE_ID_LENGTH;
		break;
	case TYPE_FILES_OFFER:
		bytes[size++] = (unsigned char)message->files_flags;
		bytes[size++] = (unsigned char)message->files_kind;
		put_u32(bytes + size, message->files_size);
		size += 4;
		memcpy(bytes + size, message->files_digest, P2P_SHA256_SIZE);
		size += P2P_SHA256_SIZE;
		size += put_text(bytes + size, message->files_map, FILES_MAP_LENGTH);
		size += put_text(bytes + size, message->files_title, FILES_TITLE_LENGTH);
		break;
	case TYPE_FILES_PULL:
		put_u16(bytes + size, (unsigned int)message->files_first);
		size += 2;
		bytes[size++] = (unsigned char)message->files_count;
		break;
	case TYPE_FILES_CHUNK:
		put_u16(bytes + size, (unsigned int)message->files_first);
		size += 2;
		memcpy(bytes + size, message->files_data, (size_t)message->files_data_size);
		size += message->files_data_size;
		break;
	case TYPE_FILES_END:
		bytes[size++] = (unsigned char)message->files_reason;
		break;
	}
	return size;
}

/* a body read: 0 if it is not one, whole, within every bound (what each
field may say for this transfer is the handler's) */
static int fileshare_body_decode(struct message *message, const unsigned char *bytes, int offset, int body_end)
{
	int index;

	if (offset + FILES_ID_LENGTH > body_end)
		return 0;
	memcpy(message->files_id, bytes + offset, FILES_ID_LENGTH);
	offset += FILES_ID_LENGTH;
	switch (message->type)
	{
	case TYPE_FILES_ASK:
		return offset == body_end;
	case TYPE_FILES_LIST:
		if (offset + 3 > body_end)
			return 0;
		message->files_page = bytes[offset];
		message->files_pages = bytes[offset + 1];
		message->files_count = bytes[offset + 2];
		offset += 3;
		if (message->files_pages < 1 || message->files_pages > FILES_PAGES_MAXIMUM ||
			message->files_page >= message->files_pages || message->files_count > FILES_PAGE_ENTRIES)
		{
			return 0;
		}
		for (index = 0; index < message->files_count; index++)
		{
			struct files_entry *entry = &message->files_entries[index];

			if (offset + 1 + FILES_FILE_ID_LENGTH + 4 > body_end)
				return 0;
			entry->kind = bytes[offset++];
			if (!fileshare_kind_valid(entry->kind))
				return 0;
			memcpy(entry->file, bytes + offset, FILES_FILE_ID_LENGTH);
			offset += FILES_FILE_ID_LENGTH;
			entry->size = get_u32(bytes + offset);
			offset += 4;
			if (!get_text(bytes, body_end, &offset, entry->map, FILES_MAP_LENGTH, 0) ||
				!get_text(bytes, body_end, &offset, entry->title, FILES_TITLE_LENGTH, 0))
			{
				return 0;
			}
		}
		return offset == body_end;
	case TYPE_FILES_GET:
		if (offset + FILES_FILE_ID_LENGTH != body_end)
			return 0;
		memcpy(message->files_file, bytes + offset, FILES_FILE_ID_LENGTH);
		return 1;
	case TYPE_FILES_OFFER:
		if (offset + 2 + 4 + P2P_SHA256_SIZE > body_end)
			return 0;
		message->files_flags = bytes[offset];
		message->files_kind = bytes[offset + 1];
		if ((message->files_flags & ~FILES_OFFER_UNASKED) || !fileshare_kind_valid(message->files_kind))
			return 0;
		message->files_size = get_u32(bytes + offset + 2);
		memcpy(message->files_digest, bytes + offset + 6, P2P_SHA256_SIZE);
		offset += 6 + P2P_SHA256_SIZE;
		if (!get_text(bytes, body_end, &offset, message->files_map, FILES_MAP_LENGTH, 0) ||
			!get_text(bytes, body_end, &offset, message->files_title, FILES_TITLE_LENGTH, 0))
		{
			return 0;
		}
		return offset == body_end;
	case TYPE_FILES_PULL:
		if (offset + 3 != body_end)
			return 0;
		message->files_first = (int)get_u16(bytes + offset);
		message->files_count = bytes[offset + 2];
		return message->files_count >= 1 && message->files_count <= FILES_PULL_MAXIMUM &&
			message->files_first + message->files_count <= FILES_MAXIMUM_CHUNKS;
	case TYPE_FILES_CHUNK:
		if (offset + 2 >= body_end)
			return 0;
		message->files_first = (int)get_u16(bytes + offset);
		offset += 2;
		message->files_data_size = body_end - offset;
		if (message->files_first >= FILES_MAXIMUM_CHUNKS || message->files_data_size > FILES_CHUNK_BYTES)
			return 0;
		memcpy(message->files_data, bytes + offset, (size_t)message->files_data_size);
		return 1;
	case TYPE_FILES_END:
		if (offset + 1 != body_end)
			return 0;
		message->files_reason = bytes[offset];
		return message->files_reason < FILES_END_REASON_COUNT;
	}
	return 0;
}

/* a file-share message to a friend (its id): 0 if it could not be queued */
static int fileshare_send(struct friend_record *record, struct message *message, const unsigned char *id)
{
	memcpy(message->files_id, id, FILES_ID_LENGTH);
	return send_pair(record, message);
}

static void fileshare_send_end(struct friend_record *record, const unsigned char *id, int reason)
{
	struct message message;

	if (!record)
		return;
	message_start(&message, TYPE_FILES_END, record->key);
	message.files_reason = reason;
	fileshare_send(record, &message, id);
}

static int fileshare_queue_room(void)
{
	return friends->outbound_count <= MAXIMUM_OUTBOUND - FILES_QUEUE_FREE;
}

/* ---------- the marks (friends/fileshare.dat) */

static int fileshare_marks_encode(unsigned char *bytes)
{
	int size = 0, index;

	memcpy(bytes, "HFSM", 4);
	size = 4;
	bytes[size++] = FILES_MARKS_VERSION;
	bytes[size++] = (unsigned char)friends->files.mark_count;
	for (index = 0; index < friends->files.mark_count; index++)
	{
		const struct files_mark *mark = &friends->files.marks[index];

		bytes[size++] = (unsigned char)mark->kind;
		size += put_text(bytes + size, mark->map, FILES_MAP_LENGTH);
		size += put_text(bytes + size, mark->file, P2P_FILESHARE_FILE_SIZE - 1);
	}
	p2p_sha256(bytes, size, bytes + size);
	return size + P2P_SHA256_SIZE;
}

/* a marks file read: 0 if it fails any check (then none are used) */
static int fileshare_marks_decode(const unsigned char *bytes, int size)
{
	struct files_mark marks[P2P_FILESHARE_SHARED_MAXIMUM];
	unsigned char digest[P2P_SHA256_SIZE];
	int count, index, offset = FILES_MARKS_HEADER_SIZE, other;

	if (size < FILES_MARKS_HEADER_SIZE + P2P_SHA256_SIZE || size > FILES_MARKS_MAXIMUM_SIZE ||
		memcmp(bytes, "HFSM", 4) || bytes[4] != FILES_MARKS_VERSION)
	{
		return 0;
	}
	p2p_sha256(bytes, size - P2P_SHA256_SIZE, digest);
	if (!p2p_equal(digest, bytes + size - P2P_SHA256_SIZE, P2P_SHA256_SIZE))
		return 0;
	count = bytes[5];
	if (count > P2P_FILESHARE_SHARED_MAXIMUM)
		return 0;
	for (index = 0; index < count; index++)
	{
		int length;

		if (offset + 2 > size - P2P_SHA256_SIZE)
			return 0;
		marks[index].kind = bytes[offset++];
		/* (names as written: exact, never cleaned into another) */
		length = bytes[offset];
		if (length > FILES_MAP_LENGTH || offset + 1 + length > size - P2P_SHA256_SIZE)
			return 0;
		memcpy(marks[index].map, bytes + offset + 1, (size_t)length);
		marks[index].map[length] = 0;
		offset += 1 + length;
		if (offset >= size - P2P_SHA256_SIZE)
			return 0;
		length = bytes[offset];
		if (length >= P2P_FILESHARE_FILE_SIZE || offset + 1 + length > size - P2P_SHA256_SIZE)
			return 0;
		memcpy(marks[index].file, bytes + offset + 1, (size_t)length);
		marks[index].file[length] = 0;
		offset += 1 + length;
		if (!fileshare_names_valid(marks[index].kind, marks[index].map, marks[index].file))
		{
			return 0;
		}
		for (other = 0; other < index; other++)
			if (marks[other].kind == marks[index].kind && !strcmp(marks[other].map, marks[index].map) &&
				!strcmp(marks[other].file, marks[index].file))
			{
				return 0;
			}
	}
	if (offset != size - P2P_SHA256_SIZE)
		return 0;
	memcpy(friends->files.marks, marks, sizeof(marks[0]) * (size_t)count);
	friends->files.mark_count = count;
	return 1;
}

/* (friends_lock held) the marks read once */
static void fileshare_marks_load(void)
{
	unsigned char bytes[FILES_MARKS_MAXIMUM_SIZE + 1];
	char path[PATH_SIZE];
	int size;

	if (friends->files.marks_loaded)
		return;
	friends->files.marks_loaded = 1;
	friends->files.mark_count = 0;
	if (!file_path("fileshare.dat", "", path, sizeof(path)))
		return;
	size = file_read(path, bytes, sizeof(bytes));
	if (size < 0)
		return;
	if (!fileshare_marks_decode(bytes, size))
	{
		friends->files.mark_count = 0;
		platform_log("fileshare: friends/fileshare.dat failed its checks: no file is marked shared");
	}
}

static int fileshare_mark_find(int kind, const char *map, const char *file)
{
	int index;

	for (index = 0; index < friends->files.mark_count; index++)
		if (friends->files.marks[index].kind == kind && !strcmp(friends->files.marks[index].map, map) &&
			!strcmp(friends->files.marks[index].file, file))
		{
			return index;
		}
	return -1;
}

/* ---------- transfers ended */

static void fileshare_wipe(unsigned char **bytes, int size)
{
	if (*bytes)
	{
		memset(*bytes, 0, (size_t)size);
		free(*bytes);
		*bytes = NULL;
	}
}

static void fileshare_upload_end(struct files_upload *upload)
{
	fileshare_wipe(&upload->bytes, upload->size);
	memset(upload, 0, sizeof(*upload));
}

static void fileshare_download_end(void)
{
	struct files_download *download = &friends->files.download;

	fileshare_wipe(&download->bytes, (int)download->size);
	memset(download, 0, sizeof(*download));
}

/* everything with a key (a friend removed or blocked) dropped, nothing sent */
static void fileshare_friend_gone(const unsigned char *key)
{
	struct files_state *files = &friends->files;
	int index;

	for (index = 0; index < FILES_UPLOADS; index++)
		if (files->uploads[index].state && p2p_equal(files->uploads[index].key, key, P2P_KEY_SIZE))
			fileshare_upload_end(&files->uploads[index]);
	if (files->download.state && p2p_equal(files->download.key, key, P2P_KEY_SIZE))
		fileshare_download_end();
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
		if (files->offers[index].active && p2p_equal(files->offers[index].key, key, P2P_KEY_SIZE))
			memset(&files->offers[index], 0, sizeof(files->offers[index]));
	for (index = 0; index < FILES_PENDING_ASKS; index++)
		if (files->asks[index].active && p2p_equal(files->asks[index].key, key, P2P_KEY_SIZE))
			files->asks[index].active = 0;
	if (files->browse.state && p2p_equal(files->browse.key, key, P2P_KEY_SIZE))
		memset(&files->browse, 0, sizeof(files->browse));
}

/* every transfer dropped, nothing sent (friends off, appearing offline,
sharing off) */
static void fileshare_stop_all(const char *why)
{
	struct files_state *files = &friends->files;
	int index, any = files->download.state != FILES_IDLE;

	for (index = 0; index < FILES_UPLOADS; index++)
	{
		any |= files->uploads[index].state != FILES_IDLE;
		fileshare_upload_end(&files->uploads[index]);
	}
	fileshare_download_end();
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
	{
		any |= files->offers[index].active;
		memset(&files->offers[index], 0, sizeof(files->offers[index]));
	}
	for (index = 0; index < FILES_PENDING_ASKS; index++)
		files->asks[index].active = 0;
	if (files->browse.state == P2P_FILESHARE_BROWSE_ASKING)
		files->browse.state = P2P_FILESHARE_BROWSE_NO_ANSWER;
	if (any)
		fileshare_status(T("File transfers stopped: %s"), why);
}

/* ---------- this machine's list */

static const struct p2p_fileshare_local *fileshare_local_find(int kind, const char *map, const char *file)
{
	int index;

	for (index = 0; index < friends->files.local_count; index++)
	{
		const struct p2p_fileshare_local *entry = &friends->files.local[index];

		if (entry->kind == kind && !strcmp(entry->map, map) && !strcmp(entry->file, file))
			return entry;
	}
	return NULL;
}

/* a shared file of the list by its id (its SHA-256's start) */
static const struct p2p_fileshare_local *fileshare_shared_by_id(const unsigned char *file)
{
	int index;

	for (index = 0; index < friends->files.local_count; index++)
	{
		const struct p2p_fileshare_local *entry = &friends->files.local[index];

		if (entry->hashed && !entry->damaged && fileshare_mark_find(entry->kind, entry->map, entry->file) >= 0 &&
			fileshare_size_valid(entry->kind, (unsigned long)entry->size) &&
			p2p_equal(entry->digest, file, FILES_FILE_ID_LENGTH))
		{
			return entry;
		}
	}
	return NULL;
}

/* an ask answered: the shared files in pages */
static int fileshare_answer_ask(struct files_pending_ask *ask)
{
	struct friend_record *record = find_friend(ask->key);
	struct files_entry entries[P2P_FILESHARE_SHARED_MAXIMUM];
	int count = 0, index, pages, page, first;
	int page_first[FILES_PAGES_MAXIMUM + 1];
	char name[P2P_FRIEND_NAME_SIZE];

	if (!record)
		return 1;
	if (friends->outbound_count > MAXIMUM_OUTBOUND - FILES_QUEUE_FREE - FILES_PAGES_MAXIMUM)
		return 0;
	for (index = 0; index < friends->files.local_count && count < P2P_FILESHARE_SHARED_MAXIMUM; index++)
	{
		const struct p2p_fileshare_local *local = &friends->files.local[index];

		if (!local->hashed || local->damaged || fileshare_mark_find(local->kind, local->map, local->file) < 0 ||
			!fileshare_size_valid(local->kind, (unsigned long)local->size))
		{
			continue;
		}
		entries[count].kind = local->kind;
		memcpy(entries[count].file, local->digest, FILES_FILE_ID_LENGTH);
		entries[count].size = (unsigned long)local->size;
		clean_text(entries[count].map, FILES_MAP_LENGTH, (const unsigned char *)local->map, (int)strlen(local->map), 0);
		clean_text(entries[count].title, FILES_TITLE_LENGTH, (const unsigned char *)local->title,
			(int)strlen(local->title), 0);
		count++;
	}
	/* (packed into pages that fit a message: past FILES_PAGES_MAXIMUM, the
	rest left out) */
	pages = 0;
	first = 0;
	while (pages < FILES_PAGES_MAXIMUM)
	{
		int size = FILES_ID_LENGTH + 3, at = first;

		while (at < count && at - first < FILES_PAGE_ENTRIES)
		{
			int entry_size = 1 + FILES_FILE_ID_LENGTH + 4 + 1 + (int)strlen(entries[at].map) + 1 +
				(int)strlen(entries[at].title);

			if (size + entry_size > MAXIMUM_BODY_SIZE)
				break;
			size += entry_size;
			at++;
		}
		page_first[pages++] = first;
		first = at;
		if (first >= count)
			break;
	}
	page_first[pages] = first;
	for (page = 0; page < pages; page++)
	{
		struct message message;

		message_start(&message, TYPE_FILES_LIST, record->key);
		message.files_page = page;
		message.files_pages = pages;
		message.files_count = page_first[page + 1] - page_first[page];
		memcpy(message.files_entries, entries + page_first[page], sizeof(entries[0]) * (size_t)message.files_count);
		fileshare_send(record, &message, ask->id);
	}
	fileshare_friend_name(ask->key, name);
	platform_log("fileshare: %s browsed this machine's File Share: %d shared files in %d pages", name,
		page_first[pages], pages);
	return 1;
}

/* ---------- what is heard */

static void fileshare_ask_heard(struct friend_record *record, const struct message *message)
{
	struct files_state *files = &friends->files;
	int index, slot = -1;

	if (!fileshare_sharing_on())
	{
		fileshare_send_end(record, message->files_id, FILES_END_NOT_SHARING);
		platform_log("fileshare: %s asked for this machine's File Share: not sharing", record->name);
		return;
	}
	if (record->files.ask_heard_time && !elapsed(record->files.ask_heard_time, FILES_ASK_INTERVAL))
	{
		friends->dropped_rate++;
		return;
	}
	record->files.ask_heard_time = now_stamp();
	for (index = 0; index < FILES_PENDING_ASKS; index++)
	{
		if (files->asks[index].active && p2p_equal(files->asks[index].key, record->key, P2P_KEY_SIZE))
			slot = index;
		else if (!files->asks[index].active && slot < 0)
			slot = index;
	}
	if (slot < 0)
	{
		friends->dropped_rate++;
		return;
	}
	files->asks[slot].active = 1;
	memcpy(files->asks[slot].key, record->key, P2P_KEY_SIZE);
	memcpy(files->asks[slot].id, message->files_id, FILES_ID_LENGTH);
	files->asks[slot].time = now_stamp();
	files->asks[slot].wait_list = !files->local_given || elapsed(files->local_time, FILES_LOCAL_FRESH);
	if (files->asks[slot].wait_list)
		files->local_wanted = 1;
}

static void fileshare_list_heard(struct friend_record *record, const struct message *message)
{
	struct files_browse *browse = &friends->files.browse;
	int index;

	if (browse->state != P2P_FILESHARE_BROWSE_ASKING || !p2p_equal(browse->key, record->key, P2P_KEY_SIZE) ||
		!p2p_equal(browse->id, message->files_id, FILES_ID_LENGTH) ||
		(browse->pages && browse->pages != message->files_pages) || (browse->pages_heard >> message->files_page & 1))
	{
		friends->dropped_forged++;
		return;
	}
	browse->pages = message->files_pages;
	browse->pages_heard |= 1u << message->files_page;
	for (index = 0; index < message->files_count && browse->count < P2P_FILESHARE_SHARED_MAXIMUM; index++)
	{
		const struct files_entry *entry = &message->files_entries[index];

		/* (a kind's size, within the cap: else left out) */
		if (!fileshare_size_valid(entry->kind, entry->size))
			continue;
		browse->entries[browse->count++] = *entry;
	}
	if (browse->pages_heard == (1u << browse->pages) - 1)
	{
		browse->state = P2P_FILESHARE_BROWSE_LISTED;
		platform_log("fileshare: %s's File Share: %d files", record->name, browse->count);
	}
}

static void fileshare_get_heard(struct friend_record *record, const struct message *message)
{
	struct files_state *files = &friends->files;
	const struct p2p_fileshare_local *entry;
	struct files_upload *upload = NULL;
	int index;

	if (!fileshare_sharing_on())
	{
		fileshare_send_end(record, message->files_id, FILES_END_NOT_SHARING);
		return;
	}
	if (record->files.get_heard_time && !elapsed(record->files.get_heard_time, FILES_GET_INTERVAL))
	{
		fileshare_send_end(record, message->files_id, FILES_END_TOO_SOON);
		return;
	}
	record->files.get_heard_time = now_stamp();
	entry = fileshare_shared_by_id(message->files_file);
	if (!entry)
	{
		fileshare_send_end(record, message->files_id, FILES_END_NOT_SHARED);
		platform_log("fileshare: %s asked for a file not shared (or changed since listed)", record->name);
		return;
	}
	for (index = 0; index < FILES_UPLOADS; index++)
	{
		if (files->uploads[index].state && p2p_equal(files->uploads[index].key, record->key, P2P_KEY_SIZE))
		{
			upload = NULL;
			break;
		}
		if (!files->uploads[index].state && !upload)
			upload = &files->uploads[index];
	}
	if (!upload)
	{
		fileshare_send_end(record, message->files_id, FILES_END_BUSY);
		return;
	}
	memset(upload, 0, sizeof(*upload));
	upload->state = FILES_LOADING;
	memcpy(upload->key, record->key, P2P_KEY_SIZE);
	memcpy(upload->id, message->files_id, FILES_ID_LENGTH);
	upload->kind = entry->kind;
	snprintf(upload->map, sizeof(upload->map), "%s", entry->map);
	snprintf(upload->file, sizeof(upload->file), "%s", entry->file);
	snprintf(upload->title, sizeof(upload->title), "%s", entry->title);
	upload->has_digest = 1;
	memcpy(upload->digest, entry->digest, P2P_SHA256_SIZE);
	upload->job = ++files->job_serial;
	upload->started_time = now_stamp();
	platform_log("fileshare: %s is downloading \"%s\" (%s)", record->name, entry->title,
		p2p_fileshare_kind_text(entry->kind));
}

/* the next chunks pulled for the download (from the first missing) */
static void fileshare_pull(struct friend_record *record)
{
	struct files_download *download = &friends->files.download;
	struct message message;
	int first = 0, count;

	while (first < download->chunk_count && bit_get(download->have, first))
		first++;
	if (first >= download->chunk_count)
		return;
	count = download->chunk_count - first;
	if (count > FILES_PULL_MAXIMUM)
		count = FILES_PULL_MAXIMUM;
	message_start(&message, TYPE_FILES_PULL, record->key);
	message.files_first = first;
	message.files_count = count;
	if (fileshare_send(record, &message, download->id))
	{
		download->pull_first = first;
		download->pull_count = count;
		download->pulled_time = now_stamp();
	}
}

/* a download begun: its chunks' room, the first pull */
static int fileshare_download_begin(struct friend_record *record)
{
	struct files_download *download = &friends->files.download;

	download->chunk_count = (int)((download->size + FILES_CHUNK_BYTES - 1) / FILES_CHUNK_BYTES);
	download->bytes = malloc((size_t)download->size);
	if (!download->bytes)
		return 0;
	memset(download->bytes, 0, (size_t)download->size);
	memset(download->have, 0, sizeof(download->have));
	download->have_count = 0;
	download->state = FILES_PULLING;
	download->progress_time = now_stamp();
	snprintf(download->friend_name, sizeof(download->friend_name), "%s", record->name);
	fileshare_pull(record);
	return 1;
}

static void fileshare_offer_heard(struct friend_record *record, const struct message *message)
{
	struct files_state *files = &friends->files;
	struct files_download *download = &files->download;
	int index, slot = -1;

	if (!(message->files_flags & FILES_OFFER_UNASKED))
	{
		/* (the answer to this machine's download) */
		if (download->state != FILES_GETTING || !p2p_equal(download->key, record->key, P2P_KEY_SIZE) ||
			!p2p_equal(download->id, message->files_id, FILES_ID_LENGTH))
		{
			friends->dropped_forged++;
			return;
		}
		if (message->files_kind != download->kind || message->files_size != download->size ||
			!p2p_equal(message->files_digest, download->file, FILES_FILE_ID_LENGTH) ||
			!fileshare_size_valid(message->files_kind, message->files_size))
		{
			fileshare_send_end(record, message->files_id, FILES_END_CANCELLED);
			fileshare_status(T("%s's file is not the one listed: not downloaded"), record->name);
			fileshare_download_end();
			return;
		}
		memcpy(download->digest, message->files_digest, P2P_SHA256_SIZE);
		if (!fileshare_download_begin(record))
		{
			fileshare_send_end(record, message->files_id, FILES_END_CANCELLED);
			fileshare_download_end();
		}
		return;
	}
	/* (a friend's "send to friend": asked of the player, nothing pulled
	before the answer) */
	if (!fileshare_sharing_on())
	{
		fileshare_send_end(record, message->files_id, FILES_END_NOT_SHARING);
		platform_log("fileshare: %s offered a file: not taken (Share with friends is off)", record->name);
		return;
	}
	if (record->files.offer_heard_time && !elapsed(record->files.offer_heard_time, FILES_OFFER_INTERVAL))
	{
		friends->dropped_rate++;
		return;
	}
	record->files.offer_heard_time = now_stamp();
	if (!fileshare_size_valid(message->files_kind, message->files_size))
	{
		fileshare_send_end(record, message->files_id, FILES_END_TOO_LARGE);
		platform_log("fileshare: refused %s's offer of \"%s\": a %s of %lu bytes (the most: %d)", record->name,
			message->files_title, p2p_fileshare_kind_text(message->files_kind), message->files_size,
			message->files_kind == P2P_FILESHARE_KIND_GAME_VARIANT ? P2P_FILESHARE_GAME_VARIANT_BYTES :
			P2P_FILESHARE_MAXIMUM_BYTES);
		return;
	}
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
	{
		if (files->offers[index].active && p2p_equal(files->offers[index].key, record->key, P2P_KEY_SIZE))
		{
			slot = -2;
			break;
		}
		if (!files->offers[index].active && slot == -1)
			slot = index;
	}
	if (slot < 0)
	{
		fileshare_send_end(record, message->files_id, FILES_END_BUSY);
		return;
	}
	memset(&files->offers[slot], 0, sizeof(files->offers[slot]));
	files->offers[slot].active = 1;
	memcpy(files->offers[slot].key, record->key, P2P_KEY_SIZE);
	memcpy(files->offers[slot].id, message->files_id, FILES_ID_LENGTH);
	files->offers[slot].kind = message->files_kind;
	files->offers[slot].size = message->files_size;
	memcpy(files->offers[slot].digest, message->files_digest, P2P_SHA256_SIZE);
	snprintf(files->offers[slot].map, sizeof(files->offers[slot].map), "%s", message->files_map);
	snprintf(files->offers[slot].title, sizeof(files->offers[slot].title), "%s", message->files_title);
	files->offers[slot].time = now_stamp();
	platform_log("fileshare: %s offered a %s: \"%s\" (%s, %lu bytes)", record->name,
		p2p_fileshare_kind_text(message->files_kind), message->files_title, message->files_map[0] ? message->files_map :
		"any map", message->files_size);
}

static void fileshare_pull_heard(struct friend_record *record, const struct message *message)
{
	int index, chunk;

	for (index = 0; index < FILES_UPLOADS; index++)
	{
		struct files_upload *upload = &friends->files.uploads[index];

		if (upload->state != FILES_OFFERED || !p2p_equal(upload->key, record->key, P2P_KEY_SIZE) ||
			!p2p_equal(upload->id, message->files_id, FILES_ID_LENGTH))
		{
			continue;
		}
		if (message->files_first + message->files_count > upload->chunk_count ||
			!budget_left(&upload->pull_budget, FILES_PULL_BUDGET, FILES_PULL_BUDGET_INTERVAL, 1))
		{
			friends->dropped_rate++;
			return;
		}
		if (upload->unasked && !upload->heard_time)
			platform_log("fileshare: %s accepted \"%s\"", record->name, upload->title);
		upload->heard_time = now_stamp();
		for (chunk = message->files_first; chunk < message->files_first + message->files_count; chunk++)
			bit_set(upload->wanted, chunk, 1);
		return;
	}
	friends->dropped_forged++;
}

static void fileshare_chunk_heard(struct friend_record *record, const struct message *message)
{
	struct files_download *download = &friends->files.download;
	int index = message->files_first;

	if (download->state != FILES_PULLING || !p2p_equal(download->key, record->key, P2P_KEY_SIZE) ||
		!p2p_equal(download->id, message->files_id, FILES_ID_LENGTH))
	{
		friends->dropped_forged++;
		return;
	}
	if (index >= download->chunk_count || message->files_data_size != fileshare_chunk_size(download->size, index))
	{
		/* (a chunk past the size the offer said, or of another size: the
		transfer is not what was offered) */
		fileshare_send_end(record, download->id, FILES_END_CANCELLED);
		fileshare_status(T("\"%s\" from %s was not what was offered: not saved"), download->title, record->name);
		fileshare_download_end();
		return;
	}
	if (bit_get(download->have, index))
		return;
	memcpy(download->bytes + (size_t)index * FILES_CHUNK_BYTES, message->files_data, (size_t)message->files_data_size);
	bit_set(download->have, index, 1);
	download->have_count++;
	download->progress_time = now_stamp();
	if (download->have_count < download->chunk_count)
	{
		/* (the window had: the next) */
		int at, window_done = 1;

		for (at = download->pull_first; at < download->pull_first + download->pull_count; at++)
			window_done &= bit_get(download->have, at);
		if (window_done)
			fileshare_pull(record);
		return;
	}
	{
		unsigned char digest[P2P_SHA256_SIZE];

		p2p_sha256(download->bytes, (int)download->size, digest);
		if (!p2p_equal(digest, download->digest, P2P_SHA256_SIZE))
		{
			fileshare_send_end(record, download->id, FILES_END_REFUSED);
			fileshare_status(T("\"%s\" from %s arrived damaged: not saved"), download->title, record->name);
			fileshare_download_end();
			return;
		}
	}
	download->state = FILES_SAVING;
	download->job = ++friends->files.job_serial;
	download->job_taken = 0;
	download->progress_time = now_stamp();
	platform_log("fileshare: \"%s\" from %s arrived whole (%lu bytes, its SHA-256 the offer's): checking it",
		download->title, record->name, download->size);
}

static void fileshare_end_heard(struct friend_record *record, const struct message *message)
{
	struct files_state *files = &friends->files;
	int index, reason = message->files_reason;

	if (files->browse.state == P2P_FILESHARE_BROWSE_ASKING && p2p_equal(files->browse.key, record->key, P2P_KEY_SIZE) &&
		p2p_equal(files->browse.id, message->files_id, FILES_ID_LENGTH))
	{
		files->browse.state = reason == FILES_END_NOT_SHARING ? P2P_FILESHARE_BROWSE_NOT_SHARING :
			reason == FILES_END_TOO_SOON ? P2P_FILESHARE_BROWSE_TOO_SOON : P2P_FILESHARE_BROWSE_NO_ANSWER;
		platform_log("fileshare: %s's File Share: %s", record->name, fileshare_end_text(reason));
		return;
	}
	if (files->download.state && p2p_equal(files->download.key, record->key, P2P_KEY_SIZE) &&
		p2p_equal(files->download.id, message->files_id, FILES_ID_LENGTH))
	{
		if (files->download.state != FILES_SAVING)
		{
			fileshare_status(T("%s stopped sending \"%s\" (%s)"), record->name, files->download.title,
				fileshare_end_text(reason));
			fileshare_download_end();
		}
		return;
	}
	for (index = 0; index < FILES_UPLOADS; index++)
	{
		struct files_upload *upload = &files->uploads[index];

		if (!upload->state || !p2p_equal(upload->key, record->key, P2P_KEY_SIZE) ||
			!p2p_equal(upload->id, message->files_id, FILES_ID_LENGTH))
		{
			continue;
		}
		if (reason == FILES_END_SAVED)
			fileshare_status(T("%s saved \"%s\""), record->name, upload->title);
		else if (reason == FILES_END_DECLINED)
			fileshare_status(T("%s declined \"%s\""), record->name, upload->title);
		else if (reason == FILES_END_REFUSED)
			fileshare_status(T("%s's game refused \"%s\""), record->name, upload->title);
		else
			fileshare_status(T("\"%s\" not sent to %s (%s)"), upload->title, record->name, fileshare_end_text(reason));
		fileshare_upload_end(upload);
		return;
	}
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
		if (files->offers[index].active && p2p_equal(files->offers[index].key, record->key, P2P_KEY_SIZE) &&
			p2p_equal(files->offers[index].id, message->files_id, FILES_ID_LENGTH))
		{
			memset(&files->offers[index], 0, sizeof(files->offers[index]));
			return;
		}
}

/* a friend's file-share message (pair_heard opened it with their key, its
sender theirs): their budget, the signature, replays, then its own checks */
static void fileshare_pair_heard(struct friend_record *record, const struct message *message, const unsigned char *plain,
	int size)
{
	if (!p2p_equal(message->sender, record->key, P2P_KEY_SIZE) ||
		!p2p_equal(message->recipient, friends->key, P2P_KEY_SIZE))
	{
		friends->dropped_forged++;
		return;
	}
	/* (appearing offline, or sharing off for what is not an ask: nothing
	answered, nothing taken - an ask, a download or an offer is answered
	"not sharing" while sharing is off and this machine shows online) */
	if (friends->appear_offline || appear_offline_now())
		return;
	if (!budget_left(&record->files.budget, FILES_BUDGET, FILES_BUDGET_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	if (!plain_verify(plain, size))
	{
		friends->dropped_forged++;
		platform_log("fileshare: dropped a message from %s whose signature is not theirs", record->name);
		return;
	}
	/* (a copy through another broker: counted, not logged) */
	if (!replay_check(record, message->epoch, message->counter))
	{
		friends->dropped_replays++;
		return;
	}
	record->heard_time = now_stamp();
	switch (message->type)
	{
	case TYPE_FILES_ASK:
		fileshare_ask_heard(record, message);
		break;
	case TYPE_FILES_LIST:
		fileshare_list_heard(record, message);
		break;
	case TYPE_FILES_GET:
		fileshare_get_heard(record, message);
		break;
	case TYPE_FILES_OFFER:
		fileshare_offer_heard(record, message);
		break;
	case TYPE_FILES_PULL:
		fileshare_pull_heard(record, message);
		break;
	case TYPE_FILES_CHUNK:
		fileshare_chunk_heard(record, message);
		break;
	case TYPE_FILES_END:
		fileshare_end_heard(record, message);
		break;
	}
}

/* ---------- the tests' steps (debug) */

#ifndef HALO_RELEASE
/* HALO_TEST_FILESHARE (this side's; the game's side reads its own):
key-file=PATH (this machine's friend key written there, hex),
stranger-ask=S:PATH (S seconds in, a FILES_ASK sealed and signed as if this
machine were the friend of the key in PATH: a stranger's, or a blocked
player's), bad=S:WHAT (up to four; S seconds in, to the first friend online: oversize, an
offer of a map variant past the cap; forged, an offer sealed for the pair but
signed by another key; sender, one signed by another key that names itself
the sender), sharing-off-at=S (Share with friends turned off) */
static void fileshare_test_parse(void)
{
	const char *setting = getenv("HALO_TEST_FILESHARE");
	char copy[1024];
	char *step, *save_pointer = NULL;
	struct files_state *files = &friends->files;

	files->test_parsed = 1;
	files->test_stranger_at = files->test_sharing_off_at = -1;
	if (!setting || !setting[0])
		return;
	snprintf(copy, sizeof(copy), "%s", setting);
	for (step = strtok_r(copy, ",", &save_pointer); step; step = strtok_r(NULL, ",", &save_pointer))
	{
		if (!strncmp(step, "key-file=", 9))
			snprintf(files->test_key_file, sizeof(files->test_key_file), "%s", step + 9);
		else if (!strncmp(step, "stranger-ask=", 13) && strchr(step, ':'))
		{
			files->test_stranger_at = atol(step + 13);
			snprintf(files->test_stranger_file, sizeof(files->test_stranger_file), "%s", strchr(step, ':') + 1);
		}
		else if (!strncmp(step, "bad=", 4) && strchr(step, ':') && files->test_bad_count < 4)
		{
			files->test_bad_at[files->test_bad_count] = atol(step + 4);
			snprintf(files->test_bad[files->test_bad_count], sizeof(files->test_bad[0]), "%s", strchr(step, ':') + 1);
			files->test_bad_count++;
		}
		else if (!strncmp(step, "sharing-off-at=", 15))
			files->test_sharing_off_at = atol(step + 15);
	}
}

static int fileshare_hex_key(const char *text, unsigned char *key)
{
	int index;

	for (index = 0; index < P2P_KEY_SIZE; index++)
	{
		unsigned int value;

		if (sscanf(text + index * 2, "%2x", &value) != 1)
			return 0;
		key[index] = (unsigned char)value;
	}
	return 1;
}

static void fileshare_test_steps(void)
{
	struct files_state *files = &friends->files;
	int index;

	if (!files->test_parsed)
		fileshare_test_parse();
	if (files->test_key_file[0])
	{
		char hex[2 * P2P_KEY_SIZE + 2];
		FILE *file;

		p2p_hex(friends->key, P2P_KEY_SIZE, hex);
		strcat(hex, "\n");
		file = fopen(files->test_key_file, "wb");
		if (file)
		{
			fwrite(hex, 1, strlen(hex), file);
			fclose(file);
		}
		files->test_key_file[0] = 0;
	}
	if (test_due(files->test_sharing_off_at) && !files->test_sharing_off_done)
	{
		files->test_sharing_off_done = 1;
		setenv("HALO_FRIENDS_FILESHARE", "false", 1);
		platform_log("fileshare: test: Share with friends turned off");
	}
	if (test_due(files->test_stranger_at) && !files->test_stranger_done)
	{
		char text[2 * P2P_KEY_SIZE + 8];
		FILE *file = fopen(files->test_stranger_file, "rb");
		struct friend_record record;
		struct message message;
		unsigned char id[FILES_ID_LENGTH];

		memset(text, 0, sizeof(text));
		if (file)
		{
			size_t got = fread(text, 1, sizeof(text) - 1, file);

			(void)got;
			fclose(file);
		}
		memset(&record, 0, sizeof(record));
		if (fileshare_hex_key(text, record.key))
		{
			files->test_stranger_done = 1;
			posix_random_bytes(id, sizeof(id));
			message_start(&message, TYPE_FILES_ASK, record.key);
			platform_log("fileshare: test: an ask sent, sealed and signed, to a key that is not a friend's: %s",
				fileshare_send(&record, &message, id) ? "queued" : "not queued");
		}
	}
	if (files->test_bads_done < files->test_bad_count && test_due(files->test_bad_at[files->test_bads_done]))
	{
		struct friend_record *record = NULL;
		const char *bad = files->test_bad[files->test_bads_done];

		for (index = 0; index < friends->friend_count && !record; index++)
			if (friends->friends[index].presence.online && friend_keys(&friends->friends[index]))
				record = &friends->friends[index];
		if (record)
		{
			struct message message;
			unsigned char plain[MAXIMUM_PLAIN_SIZE], payload[MAXIMUM_PAYLOAD_SIZE];
			unsigned char saved_seed[P2P_SEED_SIZE], saved_key[P2P_KEY_SIZE];
			char topic[TOPIC_LENGTH];
			int size;

			files->test_bads_done++;
			message_start(&message, TYPE_FILES_OFFER, record->key);
			posix_random_bytes(message.files_id, FILES_ID_LENGTH);
			message.files_flags = FILES_OFFER_UNASKED;
			message.files_kind = P2P_FILESHARE_KIND_MAP_VARIANT;
			message.files_size = !strcmp(bad, "oversize") ? 70000 : 4096;
			posix_random_bytes(message.files_digest, P2P_SHA256_SIZE);
			snprintf(message.files_map, sizeof(message.files_map), "bloodgulch");
			snprintf(message.files_title, sizeof(message.files_title), "Bad %s", bad);
			if (!strcmp(bad, "oversize"))
				fileshare_send(record, &message, message.files_id);
			else
			{
				/* (forged: sealed for the pair, signed by another key; sender:
				signed by another key that names itself the sender) */
				memcpy(saved_seed, friends->seed, sizeof(saved_seed));
				memcpy(saved_key, friends->key, sizeof(saved_key));
				posix_random_bytes(friends->seed, sizeof(friends->seed));
				if (!strcmp(bad, "sender"))
					p2p_ed25519_public(friends->seed, friends->key, NULL);
				size = plain_encode(&message, plain);
				memcpy(friends->seed, saved_seed, sizeof(saved_seed));
				memcpy(friends->key, saved_key, sizeof(saved_key));
				size = envelope_seal(KIND_PAIR, record->send_key, plain, size, payload);
				mailbox_topic(record->key, topic);
				outbound_add(topic, payload, size, 0, NULL);
			}
			platform_log("fileshare: test: a bad offer (%s) sent to %s", bad, record->name);
		}
	}
}
#endif

/* ---------- each pass of internet play's thread (friends_lock held) */

static void fileshare_update(void)
{
	struct files_state *files = &friends->files;
	int index, sends = 0, gate;

#ifndef HALO_RELEASE
	fileshare_test_steps();
#endif
	fileshare_marks_load();
	gate = fileshare_gate();
	if (gate != P2P_FILESHARE_DONE)
	{
		/* (nothing goes on, nothing waits to be answered: friends off,
		appearing offline, sharing off) */
		if (files->download.state || files->uploads[0].state || files->uploads[1].state || files->offers[0].active ||
			files->offers[1].active || files->offers[2].active || files->offers[3].active)
		{
			fileshare_stop_all(p2p_fileshare_outcome_text(gate));
		}
		for (index = 0; index < FILES_PENDING_ASKS; index++)
			files->asks[index].active = 0;
		return;
	}
	/* friends' asks: answered once this machine's list is read again (or it
	has waited long enough) */
	for (index = 0; index < FILES_PENDING_ASKS; index++)
	{
		struct files_pending_ask *ask = &files->asks[index];

		if (!ask->active)
			continue;
		if (!ask->wait_list || (files->local_given && (int)(files->local_time - ask->time) >= 0 && !files->local_wanted) ||
			elapsed(ask->time, FILES_ASK_WAIT))
		{
			if (fileshare_answer_ask(ask))
				ask->active = 0;
		}
	}
	/* uploads: a load not done, the chunks pulled, stalls */
	for (index = 0; index < FILES_UPLOADS; index++)
	{
		struct files_upload *upload = &files->uploads[index];
		struct friend_record *record = upload->state ? find_friend(upload->key) : NULL;

		if (!upload->state)
			continue;
		if (!record)
		{
			fileshare_upload_end(upload);
			continue;
		}
		if (upload->state == FILES_LOADING)
		{
			if (elapsed(upload->started_time, FILES_JOB_TIME))
			{
				fileshare_send_end(record, upload->id, FILES_END_NOT_SHARED);
				fileshare_upload_end(upload);
			}
			continue;
		}
		if ((!upload->heard_time && elapsed(upload->started_time, upload->unasked ?
			FILES_OFFER_LIFETIME + FILES_ANSWER_TIME : FILES_ANSWER_TIME)) ||
			(upload->heard_time && elapsed(upload->heard_time, FILES_STALL_TIME)) ||
			elapsed(upload->started_time, FILES_TRANSFER_TIME + (upload->unasked ? FILES_OFFER_LIFETIME : 0)))
		{
			fileshare_status(T("\"%s\" not sent: %s stopped answering"), upload->title, record->name);
			fileshare_upload_end(upload);
			continue;
		}
		{
			int chunk;

			for (chunk = 0; chunk < upload->chunk_count && sends < FILES_SENDS_PER_PASS && fileshare_queue_room(); chunk++)
			{
				struct message message;

				if (!bit_get(upload->wanted, chunk))
					continue;
				message_start(&message, TYPE_FILES_CHUNK, record->key);
				message.files_first = chunk;
				message.files_data_size = fileshare_chunk_size((unsigned long)upload->size, chunk);
				memcpy(message.files_data, upload->bytes + (size_t)chunk * FILES_CHUNK_BYTES,
					(size_t)message.files_data_size);
				if (!fileshare_send(record, &message, upload->id))
					break;
				bit_set(upload->wanted, chunk, 0);
				sends++;
			}
		}
	}
	/* the download: its answer awaited, its chunks pulled again, stalls */
	if (files->download.state)
	{
		struct files_download *download = &files->download;
		struct friend_record *record = find_friend(download->key);

		if (!record)
			fileshare_download_end();
		else if (download->state == FILES_GETTING && elapsed(download->started_time, FILES_ANSWER_TIME))
		{
			fileshare_status(T("%s didn't answer: \"%s\" not downloaded"), record->name, download->title);
			fileshare_download_end();
		}
		else if (download->state == FILES_PULLING && (elapsed(download->progress_time, FILES_STALL_TIME) ||
			elapsed(download->started_time, FILES_TRANSFER_TIME)))
		{
			fileshare_send_end(record, download->id, FILES_END_CANCELLED);
			fileshare_status(T("\"%s\" from %s stopped coming: not saved"), download->title, record->name);
			fileshare_download_end();
		}
		else if (download->state == FILES_PULLING && elapsed(download->pulled_time, FILES_REPULL_TIME) &&
			elapsed(download->progress_time, FILES_PULL_INTERVAL))
		{
			fileshare_pull(record);
		}
		else if (download->state == FILES_SAVING && elapsed(download->progress_time, FILES_JOB_TIME))
		{
			fileshare_send_end(record, download->id, FILES_END_REFUSED);
			fileshare_status(T("\"%s\" from %s couldn't be saved now"), download->title, record->name);
			fileshare_download_end();
		}
	}
	/* offers unanswered: declined */
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
	{
		struct files_offer *offer = &files->offers[index];

		if (offer->active && elapsed(offer->time, FILES_OFFER_LIFETIME))
		{
			struct friend_record *record = find_friend(offer->key);

			fileshare_send_end(record, offer->id, FILES_END_DECLINED);
			platform_log("fileshare: the offer of \"%s\" went unanswered: declined", offer->title);
			memset(offer, 0, sizeof(*offer));
		}
	}
	if (files->browse.state == P2P_FILESHARE_BROWSE_ASKING && elapsed(files->browse.time, FILES_ANSWER_TIME))
	{
		files->browse.state = P2P_FILESHARE_BROWSE_NO_ANSWER;
		platform_log("fileshare: no answer to the browse");
	}
}

/* ---------- the game's side (p2p_fileshare.h) */

int p2p_fileshare_sharing(void)
{
	return fileshare_sharing_on();
}

int p2p_fileshare_marked(int kind, const char *map, const char *file)
{
	int marked;

	pthread_mutex_lock(&friends_lock);
	fileshare_marks_load();
	marked = map && file && fileshare_mark_find(kind, map, file) >= 0;
	pthread_mutex_unlock(&friends_lock);
	return marked;
}

int p2p_fileshare_mark(int kind, const char *map, const char *file, int shared)
{
	unsigned char bytes[FILES_MARKS_MAXIMUM_SIZE];
	int size, index, written;

	if (!map || !file || !fileshare_names_valid(kind, map, file))
		return P2P_FILESHARE_NOT_ALLOWED;
	pthread_mutex_lock(&friends_lock);
	fileshare_marks_load();
	index = fileshare_mark_find(kind, map, file);
	if ((index >= 0) == (shared != 0))
	{
		pthread_mutex_unlock(&friends_lock);
		return P2P_FILESHARE_DONE;
	}
	if (shared)
	{
		if (friends->files.mark_count >= P2P_FILESHARE_SHARED_MAXIMUM)
		{
			pthread_mutex_unlock(&friends_lock);
			return P2P_FILESHARE_FULL;
		}
		index = friends->files.mark_count++;
		friends->files.marks[index].kind = kind;
		snprintf(friends->files.marks[index].map, sizeof(friends->files.marks[index].map), "%s", map);
		snprintf(friends->files.marks[index].file, sizeof(friends->files.marks[index].file), "%s", file);
	}
	else
	{
		memmove(&friends->files.marks[index], &friends->files.marks[index + 1],
			sizeof(friends->files.marks[0]) * (size_t)(friends->files.mark_count - index - 1));
		friends->files.mark_count--;
	}
	friends->files.local_wanted = 1;
	size = fileshare_marks_encode(bytes);
	pthread_mutex_unlock(&friends_lock);
	pthread_mutex_lock(&friends_file_lock);
	written = file_put("fileshare.dat", bytes, size);
	pthread_mutex_unlock(&friends_file_lock);
	platform_log("fileshare: %s %s%s%s %s shared%s", p2p_fileshare_kind_text(kind), map, map[0] ? "/" : "", file,
		shared ? "now" : "no longer", written ? "" : " (the marks couldn't be written: this run only)");
	return P2P_FILESHARE_DONE;
}

void p2p_fileshare_set_local(const struct p2p_fileshare_local *entries, int count)
{
	pthread_mutex_lock(&friends_lock);
	if (count < 0)
		count = 0;
	if (count > P2P_FILESHARE_LOCAL_MAXIMUM)
		count = P2P_FILESHARE_LOCAL_MAXIMUM;
	memcpy(friends->files.local, entries, sizeof(entries[0]) * (size_t)count);
	friends->files.local_count = count;
	friends->files.local_time = now_stamp();
	friends->files.local_given = 1;
	pthread_mutex_unlock(&friends_lock);
}

int p2p_fileshare_local(struct p2p_fileshare_local *entries, int maximum_count)
{
	int index, count;

	pthread_mutex_lock(&friends_lock);
	fileshare_marks_load();
	count = friends->files.local_count < maximum_count ? friends->files.local_count : maximum_count;
	for (index = 0; index < count; index++)
	{
		entries[index] = friends->files.local[index];
		entries[index].shared = fileshare_mark_find(entries[index].kind, entries[index].map, entries[index].file) >= 0;
	}
	pthread_mutex_unlock(&friends_lock);
	return count;
}

int p2p_fileshare_local_wanted(void)
{
	int wanted;

	pthread_mutex_lock(&friends_lock);
	wanted = friends->files.local_wanted || !friends->files.local_given;
	friends->files.local_wanted = 0;
	pthread_mutex_unlock(&friends_lock);
	return wanted;
}

void p2p_fileshare_refresh(void)
{
	pthread_mutex_lock(&friends_lock);
	friends->files.local_wanted = 1;
	pthread_mutex_unlock(&friends_lock);
}

int p2p_fileshare_browse(const char *friend_id)
{
	struct files_browse *browse = &friends->files.browse;
	struct friend_record *record;
	struct message message;
	int outcome;

	pthread_mutex_lock(&friends_lock);
	outcome = fileshare_gate();
	record = fileshare_friend_of(friend_id);
	if (outcome == P2P_FILESHARE_DONE && !record)
		outcome = P2P_FILESHARE_GONE;
	else if (outcome == P2P_FILESHARE_DONE && !record->presence.online)
		outcome = P2P_FILESHARE_FRIEND_OFFLINE;
	else if (outcome == P2P_FILESHARE_DONE && record->files.ask_sent_time &&
		!elapsed(record->files.ask_sent_time, FILES_ASK_INTERVAL))
	{
		outcome = P2P_FILESHARE_TOO_SOON;
	}
	if (outcome == P2P_FILESHARE_DONE)
	{
		memset(browse, 0, sizeof(*browse));
		browse->state = P2P_FILESHARE_BROWSE_ASKING;
		memcpy(browse->key, record->key, P2P_KEY_SIZE);
		posix_random_bytes(browse->id, FILES_ID_LENGTH);
		browse->time = now_stamp();
		record->files.ask_sent_time = browse->time;
		message_start(&message, TYPE_FILES_ASK, record->key);
		if (!fileshare_send(record, &message, browse->id))
		{
			browse->state = P2P_FILESHARE_BROWSE_NONE;
			outcome = P2P_FILESHARE_BUSY;
		}
		else
			platform_log("fileshare: asked %s for their File Share", record->name);
	}
	pthread_mutex_unlock(&friends_lock);
	return outcome;
}

int p2p_fileshare_browse_state(char *friend_name, char *friend_id, struct p2p_fileshare_remote *entries,
	int maximum_count, int *count)
{
	struct files_browse *browse = &friends->files.browse;
	int state, index, shown = 0;

	pthread_mutex_lock(&friends_lock);
	state = browse->state;
	if (friend_name)
	{
		friend_name[0] = 0;
		if (state)
			fileshare_friend_name(browse->key, friend_name);
	}
	if (friend_id)
	{
		friend_id[0] = 0;
		if (state)
			key_id(browse->key, friend_id);
	}
	for (index = 0; state == P2P_FILESHARE_BROWSE_LISTED && entries && index < browse->count && shown < maximum_count;
		index++)
	{
		struct p2p_fileshare_remote *entry = &entries[shown++];

		p2p_hex(browse->entries[index].file, FILES_FILE_ID_LENGTH, entry->id);
		entry->kind = browse->entries[index].kind;
		snprintf(entry->map, sizeof(entry->map), "%s", browse->entries[index].map);
		snprintf(entry->title, sizeof(entry->title), "%s", browse->entries[index].title);
		entry->size = (int)browse->entries[index].size;
	}
	if (count)
		*count = shown;
	pthread_mutex_unlock(&friends_lock);
	return state;
}

void p2p_fileshare_browse_close(void)
{
	pthread_mutex_lock(&friends_lock);
	memset(&friends->files.browse, 0, sizeof(friends->files.browse));
	pthread_mutex_unlock(&friends_lock);
}

int p2p_fileshare_download(const char *file_id)
{
	struct files_state *files = &friends->files;
	struct files_browse *browse = &files->browse;
	struct friend_record *record = NULL;
	const struct files_entry *entry = NULL;
	struct message message;
	char each[P2P_FILESHARE_ID_SIZE];
	int outcome, index;

	pthread_mutex_lock(&friends_lock);
	outcome = fileshare_gate();
	if (outcome == P2P_FILESHARE_DONE)
	{
		record = browse->state == P2P_FILESHARE_BROWSE_LISTED ? find_friend(browse->key) : NULL;
		for (index = 0; record && file_id && index < browse->count && !entry; index++)
		{
			p2p_hex(browse->entries[index].file, FILES_FILE_ID_LENGTH, each);
			if (!strcmp(each, file_id))
				entry = &browse->entries[index];
		}
		if (!entry)
			outcome = P2P_FILESHARE_GONE;
		else if (!record->presence.online)
			outcome = P2P_FILESHARE_FRIEND_OFFLINE;
		else if (files->download.state)
			outcome = P2P_FILESHARE_BUSY;
		else if (record->files.get_sent_time && !elapsed(record->files.get_sent_time, FILES_GET_INTERVAL))
			outcome = P2P_FILESHARE_TOO_SOON;
		else if (!fileshare_size_valid(entry->kind, entry->size))
			outcome = P2P_FILESHARE_TOO_LARGE;
	}
	if (outcome == P2P_FILESHARE_DONE)
	{
		struct files_download *download = &files->download;

		memset(download, 0, sizeof(*download));
		download->state = FILES_GETTING;
		memcpy(download->key, record->key, P2P_KEY_SIZE);
		posix_random_bytes(download->id, FILES_ID_LENGTH);
		download->kind = entry->kind;
		download->size = entry->size;
		memcpy(download->file, entry->file, FILES_FILE_ID_LENGTH);
		snprintf(download->map, sizeof(download->map), "%s", entry->map);
		snprintf(download->title, sizeof(download->title), "%s", entry->title);
		snprintf(download->friend_name, sizeof(download->friend_name), "%s", record->name);
		download->started_time = download->progress_time = now_stamp();
		record->files.get_sent_time = download->started_time;
		message_start(&message, TYPE_FILES_GET, record->key);
		memcpy(message.files_file, entry->file, FILES_FILE_ID_LENGTH);
		if (!fileshare_send(record, &message, download->id))
		{
			memset(download, 0, sizeof(*download));
			outcome = P2P_FILESHARE_BUSY;
		}
		else
			fileshare_status(T("Downloading \"%s\" from %s..."), download->title, record->name);
	}
	pthread_mutex_unlock(&friends_lock);
	return outcome;
}

int p2p_fileshare_send(const char *friend_id, int kind, const char *map, const char *file)
{
	struct files_state *files = &friends->files;
	struct friend_record *record;
	struct files_upload *upload = NULL;
	int outcome, index;

	if (!map || !file || !fileshare_names_valid(kind, map, file))
		return P2P_FILESHARE_NOT_ALLOWED;
	pthread_mutex_lock(&friends_lock);
	outcome = fileshare_gate();
	record = fileshare_friend_of(friend_id);
	if (outcome == P2P_FILESHARE_DONE && !record)
		outcome = P2P_FILESHARE_GONE;
	else if (outcome == P2P_FILESHARE_DONE && !record->presence.online)
		outcome = P2P_FILESHARE_FRIEND_OFFLINE;
	else if (outcome == P2P_FILESHARE_DONE && record->files.offer_sent_time &&
		!elapsed(record->files.offer_sent_time, FILES_OFFER_INTERVAL))
	{
		outcome = P2P_FILESHARE_TOO_SOON;
	}
	for (index = 0; outcome == P2P_FILESHARE_DONE && index < FILES_UPLOADS; index++)
	{
		if (files->uploads[index].state && p2p_equal(files->uploads[index].key, record->key, P2P_KEY_SIZE))
		{
			outcome = P2P_FILESHARE_BUSY;
			upload = NULL;
		}
		else if (!files->uploads[index].state && !upload)
			upload = &files->uploads[index];
	}
	if (outcome == P2P_FILESHARE_DONE && !upload)
		outcome = P2P_FILESHARE_BUSY;
	if (outcome == P2P_FILESHARE_DONE)
	{
		const struct p2p_fileshare_local *local = fileshare_local_find(kind, map, file);

		memset(upload, 0, sizeof(*upload));
		upload->state = FILES_LOADING;
		upload->unasked = 1;
		memcpy(upload->key, record->key, P2P_KEY_SIZE);
		posix_random_bytes(upload->id, FILES_ID_LENGTH);
		upload->kind = kind;
		snprintf(upload->map, sizeof(upload->map), "%s", map);
		snprintf(upload->file, sizeof(upload->file), "%s", file);
		snprintf(upload->title, sizeof(upload->title), "%s", local ? local->title : file);
		upload->job = ++files->job_serial;
		upload->started_time = now_stamp();
		record->files.offer_sent_time = upload->started_time;
		platform_log("fileshare: sending %s %s%s%s to %s", p2p_fileshare_kind_text(kind), map, map[0] ? "/" : "", file,
			record->name);
	}
	pthread_mutex_unlock(&friends_lock);
	return outcome;
}

int p2p_fileshare_take_offer(struct p2p_fileshare_offer *offer)
{
	struct files_state *files = &friends->files;
	int index, found = -1;

	pthread_mutex_lock(&friends_lock);
	for (index = 0; index < P2P_FILESHARE_OFFERS_MAXIMUM; index++)
		if (files->offers[index].active && !files->offers[index].asked &&
			(found < 0 || (int)(files->offers[index].time - files->offers[found].time) < 0))
		{
			found = index;
		}
	if (found >= 0)
	{
		struct files_offer *each = &files->offers[found];

		each->asked = 1;
		p2p_hex(each->id, FILES_ID_LENGTH, offer->id);
		fileshare_friend_name(each->key, offer->friend_name);
		offer->kind = each->kind;
		snprintf(offer->map, sizeof(offer->map), "%s", each->map);
		snprintf(offer->title, sizeof(offer->title), "%s", each->title);
		offer->size = (int)each->size;
	}
	pthread_mutex_unlock(&friends_lock);
	return found >= 0;
}

int p2p_fileshare_answer(const char *offer_id, int accept)
{
	struct files_state *files = &friends->files;
	struct files_offer *offer = NULL;
	struct friend_record *record = NULL;
	char each[P2P_FILESHARE_ID_SIZE];
	int index, outcome;

	pthread_mutex_lock(&friends_lock);
	for (index = 0; offer_id && index < P2P_FILESHARE_OFFERS_MAXIMUM && !offer; index++)
	{
		p2p_hex(files->offers[index].id, FILES_ID_LENGTH, each);
		if (files->offers[index].active && !strcmp(each, offer_id))
			offer = &files->offers[index];
	}
	if (offer)
		record = find_friend(offer->key);
	outcome = !offer || !record ? P2P_FILESHARE_GONE : fileshare_gate();
	if (outcome == P2P_FILESHARE_DONE && !accept)
	{
		fileshare_send_end(record, offer->id, FILES_END_DECLINED);
		platform_log("fileshare: declined %s's \"%s\"", record->name, offer->title);
		memset(offer, 0, sizeof(*offer));
	}
	else if (outcome == P2P_FILESHARE_DONE && files->download.state)
		outcome = P2P_FILESHARE_BUSY;
	else if (outcome == P2P_FILESHARE_DONE)
	{
		struct files_download *download = &files->download;

		memset(download, 0, sizeof(*download));
		download->unasked = 1;
		memcpy(download->key, offer->key, P2P_KEY_SIZE);
		memcpy(download->id, offer->id, FILES_ID_LENGTH);
		download->kind = offer->kind;
		download->size = offer->size;
		memcpy(download->digest, offer->digest, P2P_SHA256_SIZE);
		snprintf(download->map, sizeof(download->map), "%s", offer->map);
		snprintf(download->title, sizeof(download->title), "%s", offer->title);
		download->started_time = now_stamp();
		memset(offer, 0, sizeof(*offer));
		if (!fileshare_download_begin(record))
		{
			fileshare_send_end(record, download->id, FILES_END_CANCELLED);
			fileshare_download_end();
			outcome = P2P_FILESHARE_BUSY;
		}
		else
			fileshare_status(T("Downloading \"%s\" from %s..."), download->title, record->name);
	}
	pthread_mutex_unlock(&friends_lock);
	return outcome;
}

unsigned long p2p_fileshare_status(char *text, int size)
{
	unsigned long serial;

	pthread_mutex_lock(&friends_lock);
	if (text && size > 0)
		snprintf(text, (size_t)size, "%s", friends->files.status);
	serial = friends->files.status_serial;
	pthread_mutex_unlock(&friends_lock);
	return serial;
}

int p2p_fileshare_progress(int *percent)
{
	struct files_state *files = &friends->files;
	int busy = 0, index;

	pthread_mutex_lock(&friends_lock);
	if (percent)
		*percent = 0;
	if (files->download.state == FILES_PULLING && files->download.chunk_count)
	{
		busy = 1;
		if (percent)
			*percent = files->download.have_count * 100 / files->download.chunk_count;
	}
	else if (files->download.state)
		busy = 1;
	for (index = 0; index < FILES_UPLOADS && !busy; index++)
		busy = files->uploads[index].state != FILES_IDLE;
	pthread_mutex_unlock(&friends_lock);
	return busy;
}

int p2p_fileshare_take_job(struct p2p_fileshare_job *job, unsigned char *bytes, int capacity)
{
	struct files_state *files = &friends->files;
	int index, type = P2P_FILESHARE_JOB_NONE;

	memset(job, 0, sizeof(*job));
	pthread_mutex_lock(&friends_lock);
	for (index = 0; index < FILES_UPLOADS && !type; index++)
	{
		struct files_upload *upload = &files->uploads[index];

		if (upload->state != FILES_LOADING || upload->job_taken)
			continue;
		upload->job_taken = 1;
		type = P2P_FILESHARE_JOB_LOAD;
		job->serial = upload->job;
		job->kind = upload->kind;
		snprintf(job->map, sizeof(job->map), "%s", upload->map);
		snprintf(job->file, sizeof(job->file), "%s", upload->file);
		job->has_digest = upload->has_digest;
		memcpy(job->digest, upload->digest, P2P_SHA256_SIZE);
		fileshare_friend_name(upload->key, job->friend_name);
		snprintf(job->title, sizeof(job->title), "%s", upload->title);
	}
	if (!type && files->download.state == FILES_SAVING && !files->download.job_taken &&
		bytes && capacity >= (int)files->download.size)
	{
		struct files_download *download = &files->download;

		download->job_taken = 1;
		type = P2P_FILESHARE_JOB_SAVE;
		job->serial = download->job;
		job->kind = download->kind;
		snprintf(job->map, sizeof(job->map), "%s", download->map);
		snprintf(job->friend_name, sizeof(job->friend_name), "%s", download->friend_name);
		snprintf(job->title, sizeof(job->title), "%s", download->title);
		job->size = (int)download->size;
		memcpy(job->digest, download->digest, P2P_SHA256_SIZE);
		job->has_digest = 1;
		memcpy(bytes, download->bytes, download->size);
	}
	job->type = type;
	pthread_mutex_unlock(&friends_lock);
	return type;
}

void p2p_fileshare_job_loaded(int serial, const unsigned char *bytes, int size, const char *title, const char *why)
{
	struct files_state *files = &friends->files;
	int index;

	pthread_mutex_lock(&friends_lock);
	for (index = 0; index < FILES_UPLOADS; index++)
	{
		struct files_upload *upload = &files->uploads[index];
		struct friend_record *record;
		unsigned char digest[P2P_SHA256_SIZE];
		struct message message;

		if (upload->state != FILES_LOADING || upload->job != serial)
			continue;
		record = find_friend(upload->key);
		if (!record)
		{
			fileshare_upload_end(upload);
			break;
		}
		if (size <= 0 || !bytes || !fileshare_size_valid(upload->kind, (unsigned long)size))
		{
			fileshare_send_end(record, upload->id, size > P2P_FILESHARE_MAXIMUM_BYTES ? FILES_END_TOO_LARGE :
				FILES_END_NOT_SHARED);
			fileshare_status(T("\"%s\" can't be sent: %s"), upload->title, why && why[0] ? why : T("it can't be read"));
			fileshare_upload_end(upload);
			break;
		}
		p2p_sha256(bytes, size, digest);
		if (upload->has_digest && !p2p_equal(digest, upload->digest, P2P_SHA256_SIZE))
		{
			/* (changed since it was listed: the friend asks again) */
			fileshare_send_end(record, upload->id, FILES_END_NOT_SHARED);
			platform_log("fileshare: \"%s\" changed since it was listed: not sent to %s", upload->title, record->name);
			files->local_wanted = 1;
			fileshare_upload_end(upload);
			break;
		}
		upload->bytes = malloc((size_t)size);
		if (!upload->bytes)
		{
			fileshare_send_end(record, upload->id, FILES_END_BUSY);
			fileshare_upload_end(upload);
			break;
		}
		memcpy(upload->bytes, bytes, (size_t)size);
		upload->size = size;
		upload->chunk_count = (size + FILES_CHUNK_BYTES - 1) / FILES_CHUNK_BYTES;
		memcpy(upload->digest, digest, P2P_SHA256_SIZE);
		if (title && title[0])
			snprintf(upload->title, sizeof(upload->title), "%s", title);
		message_start(&message, TYPE_FILES_OFFER, record->key);
		message.files_flags = upload->unasked ? FILES_OFFER_UNASKED : 0;
		message.files_kind = upload->kind;
		message.files_size = (unsigned long)size;
		memcpy(message.files_digest, digest, P2P_SHA256_SIZE);
		clean_text(message.files_map, FILES_MAP_LENGTH, (const unsigned char *)upload->map, (int)strlen(upload->map), 0);
		clean_text(message.files_title, FILES_TITLE_LENGTH, (const unsigned char *)upload->title,
			(int)strlen(upload->title), 0);
		if (!fileshare_send(record, &message, upload->id))
		{
			fileshare_upload_end(upload);
			break;
		}
		upload->state = FILES_OFFERED;
		/* (the friend's first pull is their yes: heard_time 0 until then) */
		upload->started_time = now_stamp();
		upload->heard_time = 0;
		if (upload->unasked)
			fileshare_status(T("Offered \"%s\" to %s: waiting for their answer"), upload->title, record->name);
		break;
	}
	pthread_mutex_unlock(&friends_lock);
}

void p2p_fileshare_job_saved(int serial, int saved, const char *text)
{
	struct files_download *download = &friends->files.download;

	pthread_mutex_lock(&friends_lock);
	if (download->state == FILES_SAVING && download->job == serial)
	{
		struct friend_record *record = find_friend(download->key);

		fileshare_send_end(record, download->id, saved ? FILES_END_SAVED : FILES_END_REFUSED);
		if (saved)
			fileshare_status(download->kind == P2P_FILESHARE_KIND_GAME_VARIANT ?
				T("Saved \"%s\" from %s to your game types") : T("Saved \"%s\" from %s to your File Share"),
				text && text[0] ? text : download->title, download->friend_name);
		else
			fileshare_status(T("\"%s\" from %s was refused: %s"), download->title, download->friend_name,
				text && text[0] ? text : T("it didn't pass the checks"));
		fileshare_download_end();
	}
	pthread_mutex_unlock(&friends_lock);
}

void p2p_fileshare_digest(const unsigned char *bytes, int size, unsigned char *digest)
{
	p2p_sha256(bytes, size, digest);
}

const char *p2p_fileshare_outcome_text(int outcome)
{
	switch (outcome)
	{
	case P2P_FILESHARE_DONE:
		return T("Done");
	case P2P_FILESHARE_GONE:
		return T("That's gone now");
	case P2P_FILESHARE_OFFLINE:
		return T("Go online to share files with friends");
	case P2P_FILESHARE_APPEARING_OFFLINE:
		return T("You're appearing offline");
	case P2P_FILESHARE_SHARING_OFF:
		return T("Share with friends is off");
	case P2P_FILESHARE_FRIEND_OFFLINE:
		return T("Your friend is offline");
	case P2P_FILESHARE_BUSY:
		return T("A file is on its way already");
	case P2P_FILESHARE_TOO_SOON:
		return T("Wait a moment and try again");
	case P2P_FILESHARE_FULL:
		return T("You share 32 files already");
	case P2P_FILESHARE_NOT_ALLOWED:
		return T("That file can't be shared");
	case P2P_FILESHARE_TOO_LARGE:
		return T("That file is too large");
	}
	return "";
}

const char *p2p_fileshare_kind_text(int kind)
{
	return kind == P2P_FILESHARE_KIND_GAME_VARIANT ? T("game variant") : T("map variant");
}
