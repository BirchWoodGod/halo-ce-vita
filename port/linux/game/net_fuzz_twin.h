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
#endif

#endif
