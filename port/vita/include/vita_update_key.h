/*
VITA_UPDATE_KEY.H

The Ed25519 public key whose signature an update must carry
(vita_update_verify.h) before the game keeps it for VitaShell. Only its
owner's private key, kept offline, signs a release (tools/release/README.md).

The owner's key, made with tools/release/make_update_key.py on Oct 11 2026;
its private half is kept offline (never in git, never in CI). A build given
64 zeros instead (the tests' placeholder builds) downloads no update ("no
update key in this build").

(test builds give HALO_UPDATE_PUBLIC_KEY on the command line instead: the
tests a throwaway key made at test time, configure.py --vita-update-key a
hardware test's; never a release.)
*/

#ifndef VITA_UPDATE_KEY_H
#define VITA_UPDATE_KEY_H

#ifndef HALO_UPDATE_PUBLIC_KEY
#define HALO_UPDATE_PUBLIC_KEY "65b2979d2f239b9530c649a4761e99f8ebbc2fd39a45e246f694d9950f16f537"
#endif

#endif
