# TLSF

Two-Level Segregated Fit memory allocator, version 3.1, by Matthew Conte,
BSD 3-Clause licence (the notice at the top of `tlsf.h`).

Upstream: https://github.com/mattconte/tlsf, commit
`deff9ab509341f264addbd3c8ada533678591905` (2020-03-29). `tlsf.c` (SHA-256
`2a0f8cfc9cfe6114ccdc6cf22339059440b16f1149b5107bead4ae4c3a0d50e2`) and
`tlsf.h` (SHA-256
`f7f73c48810ba60203095667c226e5a600a6ea0f69afba48efff6efbaa628d4f`) are kept,
unchanged.

The Vita's shader compiler (SceShaccCg) allocates from a TLSF heap in a
memory block of its own (`port/vita/host/vita_gxm.c`, the shader
compiler's heap) instead of the C heap the game shares: an allocation it
cannot have returns NULL there, which the compiler takes as a failed
compile, where SceLibKernel's mspace stopped the process (Vita3K, Oct 8
2026: a recursive lock, then sceClibAbort). It is built with `NDEBUG`.
