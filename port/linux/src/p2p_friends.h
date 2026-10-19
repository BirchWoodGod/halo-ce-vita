/*
P2P_FRIENDS.H

Friends (p2p_friends.c; docs/superpowers/specs/2026-10-18-friends-design.md):
each machine has a friend code (ABCD-EFGH-JKLM) from a key of its own that
lasts (its friend identity, kept in the save area); typing someone's code
sends them a friend request, which they accept or decline. Friends see each
other's presence (online or not, and what they are doing), join a friend's
game that can be joined in one press, and invite a friend into the game they
host. Everything between friends goes through internet play's signalling
brokers, sealed for the friend alone and signed with the identity.

Every call takes the friends' lock (briefly; never internet play's while it
holds it) and may come from any thread. Plain ints and chars only: the
Vita's settings panel (another ABI) calls these too.
*/

#ifndef __HALO_LINUX_P2P_FRIENDS_H
#define __HALO_LINUX_P2P_FRIENDS_H

enum
{
	/* "ABCD-EFGH-JKLM" and a terminator */
	P2P_FRIEND_CODE_SIZE = 15,
	/* the code's characters (P2P_CODE_ALPHABET), without the dashes */
	P2P_FRIEND_CODE_LENGTH = 12,
	/* a name as shown (printable ASCII) and its terminator */
	P2P_FRIEND_NAME_SIZE = 17,
	/* an entry's handle: 16 hex digits and a terminator */
	P2P_FRIEND_ID_SIZE = 17,
	/* "Slayer on Blood Gulch", "Co-op: The Pillar of Autumn, Legendary" */
	P2P_FRIEND_ACTIVITY_SIZE = 64,
	P2P_FRIENDS_MAXIMUM = 100,
	P2P_FRIEND_REQUESTS_MAXIMUM = 16,
	P2P_FRIEND_OUTGOING_MAXIMUM = 16,
	P2P_FRIEND_BLOCKED_MAXIMUM = 64,
	P2P_FRIEND_INVITES_MAXIMUM = 8,
	/* every entry p2p_friends_list may give */
	P2P_FRIEND_ENTRIES_MAXIMUM = P2P_FRIENDS_MAXIMUM + P2P_FRIEND_REQUESTS_MAXIMUM + P2P_FRIEND_OUTGOING_MAXIMUM +
		P2P_FRIEND_BLOCKED_MAXIMUM + P2P_FRIEND_INVITES_MAXIMUM,
};

/* what an entry is, in the order the lists show them */
enum
{
	/* a friend request to this machine */
	P2P_FRIEND_KIND_REQUEST,
	/* a friend's invite into their game */
	P2P_FRIEND_KIND_INVITE,
	P2P_FRIEND_KIND_FRIEND,
	/* a request this machine sent, not answered yet */
	P2P_FRIEND_KIND_OUTGOING,
	P2P_FRIEND_KIND_BLOCKED,
	P2P_FRIEND_KIND_COUNT
};

/* what a friend is doing (their presence's) */
enum
{
	P2P_FRIEND_STATE_OFFLINE,
	P2P_FRIEND_STATE_MENUS,
	P2P_FRIEND_STATE_MULTIPLAYER,
	P2P_FRIEND_STATE_COOP,
	P2P_FRIEND_STATE_CAMPAIGN,
	P2P_FRIEND_STATE_FORGE,
	P2P_FRIEND_STATE_COUNT
};

struct p2p_friend_entry
{
	/* what p2p_friends_act takes (with the kind) */
	char id[P2P_FRIEND_ID_SIZE];
	int kind;
	/* the name they chose (a request sent: their code) */
	char name[P2P_FRIEND_NAME_SIZE];
	/* their code ("ABCD-EFGH-JKLM"; a request sent: the code typed) */
	char code[P2P_FRIEND_CODE_SIZE];
	/* a friend: online now; what they do; their game joinable (one press);
	whether this machine could invite them now (hosting a game that can
	take them) */
	int online;
	int state;
	int joinable;
	int invitable;
	/* (joinable) their game has a password: joining asks for it */
	int locked;
	/* (a friend online, or an invite) whether their game is on the crossplay
	wire (network_wire.h: a Vita's game PC players can join too, or a PC
	host's they joined): the lists tag it VITA+PC, else VITA */
	int crossplay;
	/* (a friend online, or an invite) the players in their game and the most */
	int players, maximum;
	/* the line the lists show: a friend's doing ("Offline" when not
	online), an invite's game, "Wants to be your friend", "Request sent",
	"Blocked" (in the language chosen) */
	char activity[P2P_FRIEND_ACTIVITY_SIZE];
	/* (a friend online in a party: p2p_party.h) its size, and "In Bob's
	party (3/8)" ("" none) */
	int party_size;
	char party[P2P_FRIEND_ACTIVITY_SIZE];
};

/* this machine's code (with its dashes): 0 if the identity could not be
made (no random bytes: never), else nonzero */
int p2p_friends_code(char *code, int size);

/* every entry, in the order shown: requests (newest first), invites, the
friends online (by name), the friends offline (by name), the requests sent,
the blocked; how many (at most maximum_count) */
int p2p_friends_list(struct p2p_friend_entry *entries, int maximum_count);
/* the friends, and of them those online now (if not NULL) */
int p2p_friends_count(int *online);

/* sends a friend request to a code (any case, with or without dashes or
spaces) */
enum
{
	P2P_FRIENDS_ADD_SENT,
	P2P_FRIENDS_ADD_NOT_A_CODE,
	P2P_FRIENDS_ADD_OWN_CODE,
	P2P_FRIENDS_ADD_ALREADY_FRIENDS,
	P2P_FRIENDS_ADD_ALREADY_SENT,
	P2P_FRIENDS_ADD_FULL,
	/* their request came in already: accepted instead */
	P2P_FRIENDS_ADD_ACCEPTED,
};
int p2p_friends_add(const char *code);

/* what can be done to an entry (by its id and kind) */
enum
{
	/* a request: become friends */
	P2P_FRIENDS_ACCEPT,
	/* a request: no (nothing is sent; its sender muted a week) */
	P2P_FRIENDS_DECLINE,
	/* a request, a friend, an invite's friend: blocked */
	P2P_FRIENDS_BLOCK,
	P2P_FRIENDS_UNBLOCK,
	/* a friend: no longer */
	P2P_FRIENDS_REMOVE,
	/* a request sent: taken back */
	P2P_FRIENDS_CANCEL,
	/* a friend: join their game (it must be joinable) */
	P2P_FRIENDS_JOIN,
	/* a friend: invite them into the game this machine hosts */
	P2P_FRIENDS_INVITE,
	/* an invite: join its game, or let it go */
	P2P_FRIENDS_INVITE_ACCEPT,
	P2P_FRIENDS_INVITE_IGNORE,
};
/* the outcome */
enum
{
	P2P_FRIENDS_DONE,
	/* no such entry (gone meanwhile) */
	P2P_FRIENDS_GONE,
	/* not for that kind of entry */
	P2P_FRIENDS_NOT_ALLOWED,
	/* internet play is off: go online first */
	P2P_FRIENDS_OFFLINE,
	/* join: their game is not joinable now; invite: this machine hosts no
	game a friend can be invited into (a password's included) */
	P2P_FRIENDS_NOT_JOINABLE,
	/* invite: one was sent to them a moment ago */
	P2P_FRIENDS_TOO_SOON,
	/* the list is full */
	P2P_FRIENDS_FULL,
	/* (a player's options) this machine appears offline: nothing asked */
	P2P_FRIENDS_APPEARING_OFFLINE,
	/* join: their game has a password - p2p_friends_join_password with it
	(an invite into it joins without) */
	P2P_FRIENDS_PASSWORD,
};
int p2p_friends_act(const char *id, int kind, int action);
/* a friend's game with a password joined with it (its presence's sealed
token opened on the lobby's thread: p2p_lobby_join_state says how it goes,
P2P_LOBBY_JOIN_WRONG_PASSWORD a wrong one) */
int p2p_friends_join_password(const char *id, const char *password);
/* the outcome as the player reads it (in the language chosen) */
const char *p2p_friends_outcome_text(int outcome);
const char *p2p_friends_add_text(int result);

/* the oldest invite not asked about yet, for the pop-up: nonzero if there is
one (then marked asked: the lists keep it until answered or old) */
int p2p_friends_take_invite(struct p2p_friend_entry *entry);

/* ---------- the game's players (a player's options in the lobbies, the
scoreboard and the carnage report): their friend cards are asked for over
internet play's tunnel, answered only by a machine whose player has friends
on and does not appear offline */

/* what an ask is for, and a player's state as the options show it */
enum
{
	P2P_FRIENDS_PLAYER_ADD = 1,
	P2P_FRIENDS_PLAYER_BLOCK,
	/* (a friend's player invited to the party once their card says who
	they are: p2p_party.h) */
	P2P_FRIENDS_PLAYER_PARTY,
};
enum
{
	P2P_FRIENDS_PLAYER_NONE,
	/* (the machine's own players) */
	P2P_FRIENDS_PLAYER_THIS_VITA,
	/* asked for its card, waiting */
	P2P_FRIENDS_PLAYER_ASKING,
	/* no answer: friends off, appearing offline, or an older build */
	P2P_FRIENDS_PLAYER_NOT_TAKING,
	P2P_FRIENDS_PLAYER_FRIEND,
	P2P_FRIENDS_PLAYER_REQUEST_SENT,
	P2P_FRIENDS_PLAYER_ASKED_YOU,
	P2P_FRIENDS_PLAYER_BLOCKED,
};
/* the game this machine plays (the game's main loop, each second): its
machine's index (-1: in no network game), whether it hosts, and (a host's)
each machine's address as internet play has it (count of them; network or
host byte order, a virtual address of internet play's or 0) */
void p2p_friends_set_game(int machine, int hosting, const unsigned long *addresses, int count);
/* asks a machine's player for their friend card, to add them (a friend
request to their code once it comes) or to block them: P2P_FRIENDS_DONE
(asked, or done at once with the card already had), _GONE (no such
machine), _OFFLINE (internet play or friends off), _APPEARING_OFFLINE,
_TOO_SOON */
int p2p_friends_player_ask(int machine, int intent);
/* a machine's player's state (P2P_FRIENDS_PLAYER_*) */
int p2p_friends_player_state(int machine);
/* a machine's player, if their card is a friend's: nonzero and the
friend's entry id (the party invites them: p2p_party.h) */
int p2p_friends_player_friend_id(int machine, char *id);

/* one line for the lists' top: "2 of 5 friends online", "Go online to see
your friends", "Appearing offline" (in the language chosen); returns
nonzero if friends' presence is being exchanged now */
int p2p_friends_status(char *text, int size);

/* the player's name as friends see it (the game's first profile's; empty
for the lobby name, else "Player") */
void p2p_friends_set_name(const char *name);

/* what this machine is doing, for its presence (the game's main loop, as it
changes; calling it with the same again costs little): a state
(P2P_FRIEND_STATE_*), the map (its scenario's name: "bloodgulch", "a10"),
the game type's name, its engine, a co-op or campaign game's difficulty (0
to 3; -1 none), whether the map is a Halo PC one, and the players in the
game and the most it takes */
void p2p_friends_set_activity(int state, const char *map, const char *gametype, int engine, int difficulty,
	int pc_map, int players, int maximum);

/* the activity line of a state and its game, as the lists show a friend's
(a game type with no name: its engine's; text holds P2P_FRIEND_ACTIVITY_SIZE) */
void p2p_friends_activity_text(char *text, int size, int state, const char *map, const char *gametype, int engine,
	int difficulty, int pc_map);

#endif
