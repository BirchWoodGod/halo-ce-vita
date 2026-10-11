/*
NET_FUZZ_TWIN.H

In-game mutation fuzzing of the game's message handlers (tests only: built
with HALO_NET_FUZZ_TWIN defined, never in release builds; port/linux/
README.md's network tests). After a real message from another machine is
handled, with HALO_NET_FUZZ_RATE=N one in N is handled again as a mutated
copy (a twin: bits flipped, bytes set to extremes, cut short, stretched, its
packet type changed), its result ignored, so that the handlers (the game's
messages, the distributed netcode's, map sharing's) meet malformed input in
a running game: with its objects, players and state, which the stand-alone
fuzz targets (port/vita/tests/net_fuzz_*.c) cannot give them. Meant for
AddressSanitizer builds in the netns tests (run_netns_crossplay_test.sh,
run_netns_online_test.sh): the last twins are written to HALO_NET_FUZZ_DUMP
when the process dies under a sanitizer, and every 1024 twins.

With HALO_NET_FUZZ_CAPTURE=folder, the first few of each kind of what a
client takes from a host are written there as the fuzz targets' seeds
(net_fuzz_capture): the game's messages (as net_fuzz_messages.c's inputs),
the distributed netcode's, the game settings record (as
net_fuzz_variant_options.c's), signalling's ACCEPT and the server browser's
listings (as net_fuzz_signal.c's and p2p_lobby_fuzz.c's): seeds from real
traffic, a PC host's in the crossplay tests.

First written for the network hardening audit (triage, Oct 7 2026) and kept
out of the tree then; committed for the crossplay security pass
(docs/crossplay.md, T9), compiled out of every other build.
*/

#ifndef __NET_FUZZ_TWIN_H
#define __NET_FUZZ_TWIN_H

#if defined(HALO_NET_FUZZ_TWIN) && !defined(HALO_RELEASE)
#define NET_FUZZ_TWIN_ENABLED 1

enum
{
	/* a client's message on the host, the host's on a client, a datagram on
	the host */
	_net_fuzz_twin_host_from_client,
	_net_fuzz_twin_client_from_host,
	_net_fuzz_twin_host_datagram,
};

/* TRUE with a twin of the message (size bytes) in twin (buffer_size bytes
at most), its size in twin_size; FALSE (most messages, and always without
HALO_NET_FUZZ_RATE) for none */
int net_fuzz_twin(void const *message, unsigned short size, unsigned short buffer_size, void *twin,
	unsigned short *twin_size, int direction);

enum
{
	_net_fuzz_capture_packet,
	_net_fuzz_capture_data,
	_net_fuzz_capture_record,
	_net_fuzz_capture_accept,
	_net_fuzz_capture_listing,
	NUMBER_OF_NET_FUZZ_CAPTURES
};

/* (HALO_NET_FUZZ_CAPTURE set) the prefix and the bytes written as a file
of the kind and key (0-255), the first few of each */
void net_fuzz_capture(int kind, int key, void const *prefix, int prefix_size, void const *bytes, int size);
#endif

#endif
