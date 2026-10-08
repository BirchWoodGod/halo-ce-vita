# halo-relay: internet play's relay (README.md). `make` builds it; `make test`
# runs its checks and fuzzes its packet reader with ASan and UBSan
# (port/vita/tests/run_relay_test.sh does the same).
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
LDFLAGS += -pie -Wl,-z,relro,-z,now
MONOCYPHER = ../third_party/monocypher
SOURCES = main.c relay.c $(MONOCYPHER)/monocypher.c

halo-relay: $(SOURCES) relay.h ../linux/src/p2p_relay_protocol.h
	$(CC) $(CFLAGS) -I$(MONOCYPHER) -o $@ $(SOURCES) $(LDFLAGS)

test:
	../vita/tests/run_relay_test.sh

clean:
	rm -f halo-relay

.PHONY: test clean
