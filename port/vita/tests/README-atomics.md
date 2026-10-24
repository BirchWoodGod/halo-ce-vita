# Vita atomic runtime acceptance

This independent follow-up to #48 supplies the compiler-runtime entry points
emitted for packed 16/32-bit atomics. It replaces the old generic-only runtime
in `vita_stubs.c`. The SDK GCC builds `vita_atomics.c` automatically through
the existing `port/vita/host/*.c` build glob; game code still uses Clang's packed
ABI. No map loader, gameplay, save layout, CI workflow or 3DS source is included.

Aligned 16/32-bit operations use the compiler builtins and interoperate with
inline SDK operations. Sized and generic operations on the same unaligned
16/32-bit object share one fallback lock. Other generic sizes also use that
lock. As with normal atomic APIs, callers must use one consistent object size;
overlapping accesses of different widths are not supported. The fallback is
blocking, not lock-free, and must not be called from a reentrant interrupt or
signal handler. Its ordering is conservatively stronger than requested.

Run the host ABI/concurrency regression with GCC or Clang:

```sh
cc -std=c99 -O2 -Wall -Wextra -Werror -pthread \
  port/vita/host/vita_atomics.c port/vita/tests/vita_atomics_test.c \
  -o /tmp/vita-atomics-test
/tmp/vita-atomics-test
```

The test exercises aligned/unaligned 16/32-bit operations, generic sizes and
mixed sized/generic contention. Host results do not establish ARM/device
correctness. Build with the repository's normal Vita build, then check the
resulting host object for unexpected recursive `__atomic_*` library calls.

Before merging, on-device acceptance remains required:

- Run the ABI/concurrency regression using the Vita SDK's pthread support or
  an equivalent device thread harness; record device, firmware and SDK version.
- Start a campaign from an existing save and exercise effects/lighting; return
  to the menu and reload. This PR itself makes no save-format changes.
- Run a local bot match, then return to the menu and host/join a stock online
  match on a matching upstream build; check for hangs and lighting regressions.

No new on-device pass is claimed for this isolated branch. Keep its PR as a
draft until the maintainer's requested device testing has been completed.
