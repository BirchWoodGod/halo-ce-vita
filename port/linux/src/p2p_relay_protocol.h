/*
P2P_RELAY_PROTOCOL.H

The messages between internet play's tunnel (p2p.c) and a relay
(port/relay), for two machines whose NATs keep them from reaching each
other directly. Both sides include this header, so it holds only plain
constants.

A relay never sees the game: what it carries are the tunnel's packets,
sealed with keys only the two machines have (p2p_crypto.c). It pairs the
two machines of a session that ask it for the same allocation (16 bytes
derived from the session's secret, which only those two machines can work
out: p2p.c's relay_allocation_id), one on the host's side and one on the
joiner's, and forwards a packet from either one to the other, and nothing
anywhere else.

Every message starts with P2P_RELAY_MAGIC (the tunnel's own start with
P2P_TUNNEL_MAGIC, STUN's with 0 or 1) and its type. All numbers are in
network byte order.

- ALLOCATE (a machine to the relay), P2P_RELAY_ALLOCATE_SIZE bytes: magic,
  type, version, role (0: the host's side, 1: the joiner's), the asker's
  nonce (P2P_RELAY_NONCE_SIZE: the answers carry it), a cookie
  (P2P_RELAY_COOKIE_SIZE; zeros until the relay gave one), the allocation
  (P2P_RELAY_ALLOCATION_SIZE), and zeros to the size. The padding makes
  every answer smaller than the request it answers, so no one can make the
  relay send more to an address than was sent to it (the address of a
  spoofed request).
- COOKIE (the relay's answer to an ALLOCATE without a valid cookie),
  P2P_RELAY_COOKIE_MESSAGE_SIZE bytes: magic, type, version, 0, the asker's
  nonce, and the cookie: a keyed hash of the asker's address and the time,
  which shows that the asker receives at that address. The relay keeps
  nothing until a request carries one.
- ALLOCATED (the relay's answer to an ALLOCATE with a valid cookie),
  P2P_RELAY_ALLOCATED_SIZE bytes: magic, type, version, a status
  (P2P_RELAY_WAITING, _READY, _BUSY, _TAKEN), the asker's nonce, the
  allocation's channel (4 bytes) and the seconds it may still last (4).
  Also sent once to the machine waiting when the other one arrives.
- DATA (a machine to the relay, and the relay to the other machine,
  byte for byte the same): magic, type, the channel, then a tunnel packet
  (P2P_TUNNEL_MAGIC first). The relay takes it only from one of the
  allocation's two addresses, and only while its rate allows.
*/

#ifndef __HALO_LINUX_P2P_RELAY_PROTOCOL_H
#define __HALO_LINUX_P2P_RELAY_PROTOCOL_H

enum
{
	P2P_RELAY_MAGIC = 0x6A,
	/* the tunnel's packets' first byte (p2p.c's TUNNEL_MAGIC) */
	P2P_TUNNEL_MAGIC = 0x69,
	P2P_RELAY_VERSION = 1,

	_relay_allocate = 1,
	_relay_cookie = 2,
	_relay_allocated = 3,
	_relay_data = 4,

	/* ALLOCATED's statuses: the other machine has not asked yet; both
	have (DATA flows); the relay has no room (its allocations, or those of
	the asker's address); the allocation's side is another address's */
	P2P_RELAY_WAITING = 0,
	P2P_RELAY_READY = 1,
	P2P_RELAY_BUSY = 2,
	P2P_RELAY_TAKEN = 3,

	P2P_RELAY_NONCE_SIZE = 8,
	P2P_RELAY_COOKIE_SIZE = 16,
	P2P_RELAY_ALLOCATION_SIZE = 16,
	P2P_RELAY_CHANNEL_SIZE = 4,

	/* where each field of an ALLOCATE starts */
	P2P_RELAY_ROLE_OFFSET = 3,
	P2P_RELAY_NONCE_OFFSET = 4,
	P2P_RELAY_COOKIE_OFFSET = P2P_RELAY_NONCE_OFFSET + P2P_RELAY_NONCE_SIZE,
	P2P_RELAY_ALLOCATION_OFFSET = P2P_RELAY_COOKIE_OFFSET + P2P_RELAY_COOKIE_SIZE,
	P2P_RELAY_ALLOCATE_SIZE = 64,

	/* COOKIE: the header, the nonce, the cookie */
	P2P_RELAY_COOKIE_MESSAGE_SIZE = 4 + P2P_RELAY_NONCE_SIZE + P2P_RELAY_COOKIE_SIZE,
	/* ALLOCATED: the header (its status at 3), the nonce, the channel, the
	seconds left */
	P2P_RELAY_CHANNEL_OFFSET = 4 + P2P_RELAY_NONCE_SIZE,
	P2P_RELAY_LIFETIME_OFFSET = P2P_RELAY_CHANNEL_OFFSET + P2P_RELAY_CHANNEL_SIZE,
	P2P_RELAY_ALLOCATED_SIZE = P2P_RELAY_LIFETIME_OFFSET + 4,

	/* DATA: the magic, the type and the channel, then the tunnel packet */
	P2P_RELAY_DATA_HEADER_SIZE = 2 + P2P_RELAY_CHANNEL_SIZE,
	/* a tunnel packet's least (its header, a byte, the tag: p2p.c) and a
	DATA message's most (a UDP payload in a 1500-byte IPv4 packet) */
	P2P_RELAY_MINIMUM_TUNNEL_PACKET = 1 + 6 + 8 + 1 + 16,
	P2P_RELAY_MAXIMUM_DATA_SIZE = 1472,
};

#endif
