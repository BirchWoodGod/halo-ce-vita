/*
VITA_UPDATE_KEY.H

The Ed25519 public key whose signature an update must carry
(vita_update_verify.h). Only its owner's private key, kept offline, signs a
release (tools/release/README.md).

>>> PLACEHOLDER <<<
This is not a key: 64 zeros. A build with it refuses every update ("no
update key in this build") and the panel says to install new versions with
VitaShell. To ship updating builds, the owner runs once, on their own
machine:

	python3 tools/release/make_update_key.py --private ~/halo-update-key/halo-update.key

keeps the private key file offline (never in git, never in CI), and pastes
the public key it prints here, in place of the zeros.

(tests build with HALO_UPDATE_PUBLIC_KEY given on the command line: a
throwaway key made at test time.)
*/

#ifndef VITA_UPDATE_KEY_H
#define VITA_UPDATE_KEY_H

#ifndef HALO_UPDATE_PUBLIC_KEY
#define HALO_UPDATE_PUBLIC_KEY "0000000000000000000000000000000000000000000000000000000000000000"
#endif

#endif
