# libogg

Xiph.Org's Ogg bitstream library, BSD 3-Clause licence (`COPYING`): the
pages and packets Tremor (`port/third_party/tremor`) decodes Halo Custom
Edition maps' Ogg Vorbis sounds from (`port/linux/src/ogg_sound.c`).

Upstream: https://gitlab.xiph.org/xiph/ogg, commit
`06a5e0262cdc28aa4ae6797627a783b5010440f0` (2026-03-02; release 1.3.6 and
seven commits, the last a fix of `ogg_stream_iovecin`, which only encoding
uses). Kept unchanged:

    3f851d6dfa660bcc220343d0444252f49935d2819d2b9ddfcf56b6bd8f4ff0dc  src/framing.c
    753d2ace9337bcb8c9ef23f42d630186f55f75a0d962ee79b3e7d87a614c147c  src/bitwise.c
    bbeb9f7e2ef15925400943a16ea9e3ce6aaf2f84a1e74b69b33e53a5c4693023  src/crctable.h
    aad86109c1fdb377738675a63e0b19859e06ada25ae5c59c290ca7263d1dc4ca  include/ogg/ogg.h
    d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814  COPYING

`include/ogg/os_types.h` is the port's, in place of upstream's (SHA-256
`3bbab6a5d31e3c25c6080252b042afb87028e3ffff100e3e0285648b250b70f4`) and of
the `config_types.h` its build generates: the integer types from
`<stdint.h>`, and `_ogg_malloc`, `_ogg_calloc`, `_ogg_realloc` and
`_ogg_free` (libogg's and Tremor's every allocation) sent to the decoder's
bounded allocator in `ogg_sound.c` rather than the C heap.
