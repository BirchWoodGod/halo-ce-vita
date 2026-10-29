/*
P2P_FILESHARE_TYPES.INC

File Share with friends' constants and state (p2p_fileshare.h; its code
p2p_fileshare.inc), part of p2p_friends.c: included before the friends'
own state, which holds this one (the tests swap a machine's whole state).
The messages, limits and threats: docs/fileshare-friends.md.
*/

enum
{
	/* the pair messages' types (after the party's) */
	TYPE_FILES_ASK = 14,
	TYPE_FILES_LIST = 15,
	TYPE_FILES_GET = 16,
	TYPE_FILES_OFFER = 17,
	TYPE_FILES_PULL = 18,
	TYPE_FILES_CHUNK = 19,
	TYPE_FILES_END = 20,

	FILES_ID_LENGTH = 8,
	FILES_FILE_ID_LENGTH = 8,
	FILES_MAP_LENGTH = P2P_FILESHARE_MAP_SIZE - 1,
	FILES_TITLE_LENGTH = P2P_FILESHARE_TITLE_SIZE - 1,
	/* a chunk's bytes: the largest that keeps a chunk's message within the
	brokers' room (P2P_FRIENDS_PAYLOAD_ROOM): id, index, data */
	FILES_CHUNK_BYTES = 288,
	FILES_MAXIMUM_CHUNKS = (P2P_FILESHARE_MAXIMUM_BYTES + FILES_CHUNK_BYTES - 1) / FILES_CHUNK_BYTES,
	FILES_CHUNK_MASK_BYTES = (FILES_MAXIMUM_CHUNKS + 7) / 8,
	FILES_CHUNK_BODY_SIZE = FILES_ID_LENGTH + 2 + FILES_CHUNK_BYTES,
	/* a map variant's least bytes (its header: forge_files.h) */
	FILES_MAP_VARIANT_MINIMUM_BYTES = 64,
	/* a pull's most chunks; a list's page's most entries, and pages */
	FILES_PULL_MAXIMUM = 8,
	FILES_PAGE_ENTRIES = 8,
	FILES_PAGES_MAXIMUM = 8,
	/* an entry of a list: kind, file id, size, map, title */
	FILES_ENTRY_MAXIMUM_SIZE = 1 + FILES_FILE_ID_LENGTH + 4 + 1 + FILES_MAP_LENGTH + 1 + FILES_TITLE_LENGTH,
	/* an offer flag: sent unasked ("send to friend") */
	FILES_OFFER_UNASKED = 1,
	/* uploads at once (one a friend), asks waiting for this machine's list */
	FILES_UPLOADS = 2,
	FILES_PENDING_ASKS = 4,

	/* the marks file: "HFSM", its version, the count, the records (kind, map,
	file), its SHA-256 */
	FILES_MARKS_VERSION = 1,
	FILES_MARKS_HEADER_SIZE = 4 + 1 + 1,
	FILES_MARK_RECORD_MAXIMUM_SIZE = 1 + 1 + FILES_MAP_LENGTH + 1 + (P2P_FILESHARE_FILE_SIZE - 1),
	FILES_MARKS_MAXIMUM_SIZE = FILES_MARKS_HEADER_SIZE + P2P_FILESHARE_SHARED_MAXIMUM * FILES_MARK_RECORD_MAXIMUM_SIZE +
		P2P_SHA256_SIZE,

	/* milliseconds: a friend's file-share messages (at most this many at
	once, one more each interval, before the signature's work) */
	FILES_BUDGET = 48,
	FILES_BUDGET_INTERVAL = 40,
	/* asks sent a friend, and answered; downloads asked; offers sent unasked
	and heard (a friend's each) */
	FILES_ASK_INTERVAL = 5000,
	FILES_GET_INTERVAL = 5000,
	FILES_OFFER_INTERVAL = 15000,
	/* an offer waiting for the player's answer */
	FILES_OFFER_LIFETIME = 120000,
	/* a transfer's pulls served (an upload's: at most this many at once, one
	more each interval), the receiver's own pace, its pulls again for the
	chunks missing, a stall, the most a transfer may take */
	FILES_PULL_BUDGET = 6,
	FILES_PULL_BUDGET_INTERVAL = 500,
	FILES_PULL_INTERVAL = 500,
	FILES_REPULL_TIME = 4000,
	FILES_STALL_TIME = 20000,
	FILES_TRANSFER_TIME = 180000,
	/* an ask or a download with no answer */
	FILES_ANSWER_TIME = 15000,
	/* an ask waits this long at most for this machine's list read again;
	the list is read again for an ask when older than this */
	FILES_ASK_WAIT = 5000,
	FILES_LOCAL_FRESH = 10000,
	/* the game's work on a job (a load, a save) at most */
	FILES_JOB_TIME = 30000,
	/* chunks are queued only while the outgoing queue has this many places
	free; this many signed in one pass at most */
	FILES_QUEUE_FREE = 24,
	FILES_SENDS_PER_PASS = 8,
};

/* an END's reason */
enum
{
	FILES_END_SAVED = 0,
	FILES_END_DECLINED,
	FILES_END_CANCELLED,
	FILES_END_NOT_SHARING,
	FILES_END_NOT_SHARED,
	FILES_END_BUSY,
	FILES_END_TOO_LARGE,
	FILES_END_REFUSED,
	FILES_END_TOO_SOON,
	FILES_END_REASON_COUNT
};

/* where a transfer is */
enum
{
	FILES_IDLE = 0,
	/* (an upload) the game reads the file */
	FILES_LOADING,
	/* (an upload) offered: chunks sent as they are pulled */
	FILES_OFFERED,
	/* (a download) asked for, the offer awaited */
	FILES_GETTING,
	/* (a download) chunks pulled */
	FILES_PULLING,
	/* (a download) whole: the game checks and saves it */
	FILES_SAVING,
};

/* a list entry as a message carries it */
struct files_entry
{
	int kind;
	unsigned char file[FILES_FILE_ID_LENGTH];
	unsigned long size;
	char map[FILES_MAP_LENGTH + 1];
	char title[FILES_TITLE_LENGTH + 1];
};

struct files_mark
{
	int kind;
	char map[P2P_FILESHARE_MAP_SIZE];
	char file[P2P_FILESHARE_FILE_SIZE];
};

/* a file going to a friend */
struct files_upload
{
	int state;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char id[FILES_ID_LENGTH];
	int unasked;
	int kind;
	char map[P2P_FILESHARE_MAP_SIZE];
	char file[P2P_FILESHARE_FILE_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	/* (a friend's download) the SHA-256 it was listed with */
	int has_digest;
	unsigned char digest[P2P_SHA256_SIZE];
	/* the game's load (its job's serial; handed to the game) */
	int job;
	int job_taken;
	unsigned char *bytes;
	int size;
	int chunk_count;
	/* the chunks pulled and not sent yet */
	unsigned char wanted[FILES_CHUNK_MASK_BYTES];
	struct budget pull_budget;
	unsigned long started_time, heard_time;
};

/* the file coming to this machine (one at a time) */
struct files_download
{
	int state;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char id[FILES_ID_LENGTH];
	int unasked;
	int kind;
	unsigned long size;
	unsigned char digest[P2P_SHA256_SIZE];
	/* (a download from a list) the entry's file id */
	unsigned char file[FILES_FILE_ID_LENGTH];
	char map[P2P_FILESHARE_MAP_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	char friend_name[P2P_FRIEND_NAME_SIZE];
	unsigned char *bytes;
	unsigned char have[FILES_CHUNK_MASK_BYTES];
	int have_count;
	int chunk_count;
	/* the chunks last pulled */
	int pull_first, pull_count;
	int job;
	int job_taken;
	unsigned long started_time, progress_time, pulled_time;
};

/* a friend's offer waiting for the player */
struct files_offer
{
	int active;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char id[FILES_ID_LENGTH];
	int kind;
	unsigned long size;
	unsigned char digest[P2P_SHA256_SIZE];
	char map[P2P_FILESHARE_MAP_SIZE];
	char title[P2P_FILESHARE_TITLE_SIZE];
	unsigned long time;
	int asked;
};

/* a friend's ask, answered once this machine's list is read again */
struct files_pending_ask
{
	int active;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char id[FILES_ID_LENGTH];
	unsigned long time;
	/* the list was old as it came: answered once it is read again */
	int wait_list;
};

/* a friend's File Share looked at */
struct files_browse
{
	int state;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char id[FILES_ID_LENGTH];
	unsigned long time;
	int pages;
	unsigned int pages_heard;
	struct files_entry entries[P2P_FILESHARE_SHARED_MAXIMUM];
	int count;
};

struct files_state
{
	int marks_loaded;
	struct files_mark marks[P2P_FILESHARE_SHARED_MAXIMUM];
	int mark_count;
	/* this machine's list (the game's), when given; read again wanted */
	struct p2p_fileshare_local local[P2P_FILESHARE_LOCAL_MAXIMUM];
	int local_count;
	unsigned long local_time;
	int local_given;
	int local_wanted;
	struct files_upload uploads[FILES_UPLOADS];
	struct files_download download;
	struct files_offer offers[P2P_FILESHARE_OFFERS_MAXIMUM];
	struct files_pending_ask asks[FILES_PENDING_ASKS];
	struct files_browse browse;
	int job_serial;
	/* what the transfers came to (p2p_fileshare_status) */
	char status[P2P_FILESHARE_TEXT_SIZE];
	unsigned long status_serial;
	/* (debug) the tests' steps (HALO_TEST_FILESHARE: p2p_fileshare.inc's) */
	int test_parsed;
	char test_key_file[PATH_SIZE];
	char test_stranger_file[PATH_SIZE];
	long test_stranger_at, test_bad_at, test_sharing_off_at;
	char test_bad[16];
	int test_stranger_done, test_bad_done, test_sharing_off_done;
};

/* a friend's file-share bookkeeping (in their friend_record) */
struct files_friend
{
	struct budget budget;
	unsigned long ask_sent_time, ask_heard_time, get_sent_time, get_heard_time, offer_sent_time, offer_heard_time;
};
