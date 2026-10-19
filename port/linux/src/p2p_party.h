/*
P2P_PARTY.H

The party (p2p_party.inc, part of p2p_friends.c, whose head says its
messages, keys, limits and threat model): up
to eight players who move from game to game together, Halo 3's way. A
leader invites friends (only accepted friends, never a blocked player); a
member may leave at any time, the leader may remove one, and when the
leader leaves, quits or goes offline the longest-standing member leads.
When the leader hosts a game the members join its lobby by themselves; when
the leader joins another's game they are told where (a FOLLOW) and join it
behind, through the game's own join and its checks (map, password,
identity, room): a member who cannot get in is told why and stays in the
party. Party chat: short text messages, typed or chosen from presets,
end-to-end sealed and signed for the members alone.

Everything travels through the friends' channel (the signalling brokers,
sealed per pair and signed with each machine's friend identity, replay
protected): nothing of it is a game message, and the network version does
not change. Appearing offline, a machine sends and takes nothing of a
party (it leaves the one it was in).

Every call takes the friends' lock (briefly) and may come from any thread.
Plain ints and chars only: the Vita's settings panel (another ABI) calls
these too.
*/

#ifndef __HALO_LINUX_P2P_PARTY_H
#define __HALO_LINUX_P2P_PARTY_H

#include "p2p_friends.h"

enum
{
	P2P_PARTY_MAXIMUM = 8,
	/* a chat message's characters at most (printable ASCII) and its
	terminator */
	P2P_PARTY_CHAT_LENGTH = 120,
	P2P_PARTY_CHAT_SIZE = P2P_PARTY_CHAT_LENGTH + 1,
	/* the chat lines kept (the newest) */
	P2P_PARTY_CHAT_LINES = 24,
	/* a member's name as shown: a friend's as the friends list keeps it,
	another's with their code's first characters after it ("Bob #K7QX") */
	P2P_PARTY_NAME_SIZE = P2P_FRIEND_NAME_SIZE + 8,
	P2P_PARTY_NOTE_SIZE = 64,
	/* the quick chat presets (p2p_party_chat_preset_text) */
	P2P_PARTY_PRESETS = 8,
};

/* a member as the lists show it */
struct p2p_party_member
{
	/* what p2p_party_kick and p2p_party_mute take */
	char id[P2P_FRIEND_ID_SIZE];
	char name[P2P_PARTY_NAME_SIZE];
	char code[P2P_FRIEND_CODE_SIZE];
	int leader;
	/* this machine's own */
	int self;
	/* on this machine's friends list (the name its own), muted here, blocked
	here (nothing of theirs shown or sent), heard from of late */
	int is_friend;
	int muted;
	int blocked;
	int online;
	/* what they said of themselves: in a game, following, why they could not
	follow ("Couldn't follow: the game is full") */
	char note[P2P_PARTY_NOTE_SIZE];
};

/* a party invite to this machine, for the question */
struct p2p_party_invite
{
	char id[P2P_FRIEND_ID_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	int size;
};

/* where the leader went, for this machine to follow (the game's side joins
it: p2p_party_follow_join) */
struct p2p_party_follow
{
	unsigned long serial;
	char leader[P2P_FRIEND_NAME_SIZE];
	/* the leader hosts it (else joined it) */
	int hosting;
	/* P2P_FRIEND_STATE_*, its map (scenario name) and game type, a Halo PC
	map, the players and most */
	int state;
	char map[33];
	char gametype[25];
	int pc_map;
	int players, maximum;
	char activity[P2P_FRIEND_ACTIVITY_SIZE];
};

/* the outcome of a party action */
enum
{
	P2P_PARTY_DONE,
	/* no such friend or member (gone meanwhile) */
	P2P_PARTY_GONE,
	/* only the leader may do that */
	P2P_PARTY_NOT_LEADER,
	/* only a friend may be invited (never a blocked player) */
	P2P_PARTY_NOT_FRIEND,
	/* internet play or friends are off: go online first */
	P2P_PARTY_OFFLINE,
	P2P_PARTY_APPEARING_OFFLINE,
	P2P_PARTY_FULL,
	P2P_PARTY_ALREADY_IN,
	P2P_PARTY_TOO_SOON,
	P2P_PARTY_NOT_IN_PARTY,
	/* a friend offline now */
	P2P_PARTY_FRIEND_OFFLINE,
	/* (chat) nothing left to send once cleaned, or too many at once */
	P2P_PARTY_EMPTY,
	P2P_PARTY_SLOW_DOWN,
};

/* what came of a follow (a member's, told to the party) */
enum
{
	P2P_PARTY_FOLLOW_NONE,
	P2P_PARTY_FOLLOW_JOINING,
	P2P_PARTY_FOLLOW_THERE,
	/* in another game: following once back at the menus */
	P2P_PARTY_FOLLOW_WAITING,
	P2P_PARTY_FOLLOW_PASSWORD,
	P2P_PARTY_FOLLOW_FULL,
	P2P_PARTY_FOLLOW_NO_MAP,
	/* another version of the game, or the crossplay wire's */
	P2P_PARTY_FOLLOW_VERSION,
	/* the game cannot be reached through internet play (a PC game, system
	link) */
	P2P_PARTY_FOLLOW_UNREACHABLE,
	P2P_PARTY_FOLLOW_FAILED,
	P2P_PARTY_FOLLOW_COUNT
};

/* the members (in the party's order: the longest-standing first, the
leader marked), none when not in a party; how many */
int p2p_party_members(struct p2p_party_member *members, int maximum_count);
/* one line: "Party: 3 of 8 (Alpha leads)", "Not in a party", "Go online
to make a party" (in the language chosen); returns the party's size (0: in
none) */
int p2p_party_status(char *text, int size);
/* whether this machine leads a party (of two or more) */
int p2p_party_leading(void);

/* invites a friend (by their entry's id, p2p_friends.h): a party is made
with this machine its leader if there is none */
int p2p_party_invite(const char *friend_id);
/* whether a friend can be invited now (their entry's id) */
int p2p_party_invitable(const char *friend_id);
/* the party's state of a friend: 0 none, 1 a member, 2 invited (waiting) */
int p2p_party_friend_state(const char *friend_id);
int p2p_party_leave(void);
/* (the leader) a member out of the party */
int p2p_party_kick(const char *member_id);
/* a member's chat and notes hidden here (or shown again); never told */
int p2p_party_mute(const char *member_id, int muted);
const char *p2p_party_outcome_text(int outcome);

/* the oldest party invite not asked about yet, for the question (then
marked asked; it lasts two minutes); its answer */
int p2p_party_take_invite(struct p2p_party_invite *invite);
int p2p_party_answer(const char *invite_id, int accept);
/* the party invites waiting (the lists') */
int p2p_party_invites(struct p2p_party_invite *invites, int maximum_count);

/* (a member) a follow not yet joined: nonzero and what it is */
int p2p_party_follow_pending(struct p2p_party_follow *follow);
/* joins it (the game's side, at the menus: the System Link screen next as
an invite's join): the outcome (P2P_PARTY_FOLLOW_*) */
int p2p_party_follow_join(unsigned long serial);
/* what came of it, told to the party (P2P_PARTY_FOLLOW_*) */
void p2p_party_follow_result(unsigned long serial, int result);
const char *p2p_party_follow_text(int result);

/* (the leader, before joining another's game) nonzero, and the warning,
if the game has fewer open slots than the party has players */
int p2p_party_join_warning(int players, int maximum, char *text, int size);

/* ---------- chat */

/* a line kept: who said it (as the lists show them: a member's name), what,
and its number (rising; 0: none) */
struct p2p_party_chat_line
{
	unsigned long number;
	char name[P2P_PARTY_NAME_SIZE];
	char text[P2P_PARTY_CHAT_SIZE];
	/* this machine's own, or the party's word (a member joined or left) */
	int self;
	int notice;
};
/* sends text typed (cleaned: printable ASCII the font draws, no control
characters, at most P2P_PARTY_CHAT_LENGTH) */
int p2p_party_chat_send(const char *text);
/* sends a preset (0 to P2P_PARTY_PRESETS - 1) */
int p2p_party_chat_preset(int preset);
const char *p2p_party_chat_preset_text(int preset);
/* the lines newer than after (0: all kept), oldest first; how many */
int p2p_party_chat_lines(struct p2p_party_chat_line *lines, int maximum_count, unsigned long after);
/* the newest line's number (0: none) */
unsigned long p2p_party_chat_newest(void);
/* text cleaned as chat sends it (the tests') */
int p2p_party_chat_clean(char *destination, int size, const char *source);

#endif
