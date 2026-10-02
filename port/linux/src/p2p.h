/*
P2P.H

Internet play (p2p.c): machines that shared an invite reach each other's
system link games as if they were on one LAN. xnet.c routes the game's
traffic for them through here; sdl_platform.c passes invite links in and
out through the clipboard.

Addresses and ports are in network byte order.
*/

#ifndef __HALO_LINUX_P2P_H
#define __HALO_LINUX_P2P_H

/* starts internet play, if network.online is set, when the game starts
its networking; local_address is the address the game's sockets are
reached at (network.address, else 127.0.0.1) */
void p2p_initialize(unsigned long local_address);

/* on the desktop, before anything else: if this process was started with
an invite link (halo://join/...) and another copy of the game is running,
passes the link to it and returns nonzero (this one should quit) */
int p2p_hand_off_invite(void);

/* joins the game an invite link or code leads to; text may hold other
words around it. Returns nonzero if it held an invite */
int p2p_join_invite(const char *text);

/* this machine's identifier, which its XNADDR carries (6 bytes) */
const unsigned char *p2p_identifier(void);
/* the address the game reaches the machine with this identifier at, if it
is an internet play peer */
int p2p_peer_address(const unsigned char *identifier, unsigned long *address);

/* a destination the game sends to or connects to (stream: a TCP socket):
if it is a peer's address, the local address standing in for it */
int p2p_outgoing(int stream, unsigned long *address, unsigned short *port);
/* a source the game received from, accepted from or is connected to: if
it is one standing in for a peer, the peer's address */
int p2p_incoming(int stream, unsigned long *address, unsigned short *port);
/* the local addresses standing in for this port (a broadcast's) on every
peer; returns their count */
int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count);

/* the game listens (it is hosting) on socket, or closes a socket */
void p2p_socket_listening(int socket);
void p2p_socket_closed(int socket);

/* text for the clipboard (a new invite link), once; NULL if none. Called
from the main thread */
const char *p2p_take_clipboard_text(void);

/* ---------- short codes and the public lobby (p2p_signal.c), for a
machine with no clipboard (the Vita's settings panel) and for strangers.
Every call takes p2p's lock and may come from any thread; nothing waits on
the network. Plain ints and chars only: the Vita's host side (another ABI)
calls these too. */

enum
{
	/* "ABCD-EFGH" and a terminator */
	P2P_CODE_SIZE = 10,
	P2P_LOBBY_NAME_SIZE = 32,
};

/* the letters and digits of a code: no 0, 1, I or O, which read alike */
#define P2P_CODE_ALPHABET "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

struct p2p_lobby_entry
{
	char code[P2P_CODE_SIZE];
	char name[P2P_LOBBY_NAME_SIZE];
	int players, maximum;
	/* the same network version as this machine's (else joining fails) */
	int compatible;
	/* this machine's own game */
	int own;
};

/* joins the game of a code: "ABCD-EFGH", with or without the dash, any
case. Returns nonzero if it was a code (internet play must be on) */
int p2p_join_code(const char *code);
/* while this machine hosts with internet play on: copies its code (with
the dash) and returns nonzero */
int p2p_hosting_code(char *code, int size);
/* lists this machine's game in the public lobby while it hosts (or not);
the name is what others see (network.lobby_name otherwise) */
void p2p_lobby_set_public(int listed);
void p2p_lobby_set_name(const char *name);
/* browsing the public lobby: on or off, and the index-th entry (nonzero if
there is one) */
void p2p_lobby_browse(int on);
int p2p_lobby_entry(int index, struct p2p_lobby_entry *entry);
/* one line on what internet play is doing (for a menu); returns nonzero
if internet play runs */
int p2p_status(char *text, int size);
/* the same for ad hoc play (p2p_adhoc.c): in a group or not, with how many
other machines; returns nonzero if ad hoc play is on */
int p2p_adhoc_status(char *text, int size);

#endif
