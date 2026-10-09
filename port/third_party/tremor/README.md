# Tremor

Xiph.Org's integer Ogg Vorbis decoder ("libvorbisidec"), BSD 3-Clause
licence (`COPYING`). It decodes Halo Custom Edition maps' Ogg Vorbis sound
permutations, which the game's sound cache then holds as Xbox ADPCM
(`port/linux/src/ogg_sound.c`, `port/linux/game/custom_edition_sounds.c`):
no floating point, which suits the Vita's Cortex-A9, and maintained with
libvorbis's fixes. It reads Ogg pages with libogg
(`port/third_party/libogg`). stb_vorbis was the other candidate; its last
release (1.22, 2021) carries the out-of-bounds writes in its stream setup
reported in 2023 (CVE-2023-45675 to 45682), which Debian's tracker lists
as waiting for an upstream fix, and Custom Edition maps come from
strangers.

Upstream: https://gitlab.xiph.org/xiph/tremor, commit
`820fb3237ea81af44c9cc468c8b4e20128e3e5ad` (2025-04-03). The decoder's
sources are kept: not `vorbisfile.c` (the game decodes from its own reads,
`ogg_sound.c`), nor the build files, examples and documentation.

SHA-256 of the files as upstream has them:

    5376bf8a9d8001e10e5475b072d95de5c465de9c372ec070fad97752cf5fdef3  block.c
    a43f0fb5326f946dc939eff82b0aeb4c6c70277ee500391be78f3e26054e212b  codebook.c (changed, below)
    c16d7c9069c7d56d6980ecda63db0513733fba30fa6d847a7d452117fbb024f5  floor0.c
    0cb8475515ebdec5920a6abcb78a74fee14fceb39e65d1a035f60aab88568aa7  floor1.c
    1100fa7f2c4d57f9069a4e5ecfe74344ac3f8358fff2b3c031fd0cdcf668cafd  info.c (changed, below)
    eee800dcdd2359871ae0553158701e800169d6fc8296926d7cfb8815f945fb4f  mapping0.c
    bf9b1f02c8a02bae6787d30329d657d4f7159e786e2971bf987adfb30e4950f0  mdct.c
    a38fcf3d102cc6e015792158b3b3204e71280dcb6b8f5e2401c7c9765791b0f8  registry.c
    9a01a2663e5b5b1413225f49e91cc14e7abb1fa9502a956e68ec8b26519c38ae  res012.c
    6e1236616f4f2d4f9628c9fa64d8c5b80980ac365068bff2d212fbeb847a2252  sharedbook.c (changed, below)
    e871d0ca46a01b38888e9f6c31dda950e22d1352b3c17ca92b20b9eb1a8c9e96  synthesis.c
    3c21289ea24858990497f1e920844e215a26c857741da82c414ee81bb1d4908a  window.c
    bccde5bccd95e2f6765c78605e04205f34d2f270a7e8445034d7d1912d7960e0  asm_arm.h
    e9db3e511267398a8a1704b86da78fc1625bd9b271eeb02d977f4f458b453b24  backends.h
    e4463b2acde3701abe36c85df04c8f050e56dadfbfb74ea9a3185620f14a0d60  block.h
    c9b4a1a7d68e285aec265fc36c3b459f46d5f66d1e255f5f86284e01f67bacb9  codebook.h
    c0cc6d9362a3dbf1c55b83064c55385ac937983c9875b1237a5d3373018b2026  codec_internal.h
    bbf025d8f5b531e31baa73caa82f9e64aebc54caba3a832fb2ea49b2730d3be5  ivorbiscodec.h
    12e9af6ce501a3ad824a63480f225ca87aa2f0a3352c8a7063d47c658edff9de  lsp_lookup.h
    989aadfd8852177ba9997d20150fd523255090dd4f035412638390d50c33a4d7  mdct.h
    270d0ec53d9a48e78eeee98abe73fb0999fe23cc9a897f0caaa8279150f4e785  mdct_lookup.h
    bf348705d1c542f4a9be8b414444b6454717b58027fad75facb32f600bdfdc47  misc.h
    66f59b4193207adb13386ddacebeba1a314d2ca20e485c45de37171e490072c0  os.h
    e77e683e5adafc5ca76141f9a7e23e83ee14ad30520b08795c31f117c410f86a  registry.h
    f9475a1e82bd389106319b1aa9f90fab67198a96f339bebec890dc392fa28e1a  window.h
    94f0b69c82daf25fb58f743452e543b8c80b6f6144254f9b6c93f452ba1a419b  window_lookup.h
    d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814  COPYING

Three files are changed (`port.patch` is the difference; their SHA-256 as
kept: `codebook.c`
`ab3d780770f0fc593bbaa1413f140711e622609b904ea5de8a6401e102eec5ed`,
`info.c` `9c3e85ad89f242c240f2d376c72cf831eb0ed43b6b2413fd077c4ae827d1bfab`,
`sharedbook.c`
`972fd0b054cbf1a428d44b49b9409831dbad8e0605054d09c90ecf1d1fa73551`):

- `sharedbook.c`, `vorbis_book_init_decode`: the two arrays of a codebook's
  used entries come from the allocator, not `alloca`: a stream can declare
  a codebook of up to 2^24 entries, which would have been that much stack
  (8 bytes each). The allocator is bounded (below), so the arrays are too.
- `info.c`, the identification header: a header cut short reads -1 for the
  block sizes' exponents, which were shifted by it (undefined); it is now
  refused, as a block size out of range was.
- `sharedbook.c`, `_book_unquantize`, and `codebook.c`'s four vector
  decoders: a stream's values can ask for shifts past the 32-bit word
  (undefined, and not the same on x86 and ARM); they are capped. Only
  crafted streams reach either: the samples of valid streams are the same.

Tremor allocates through `_ogg_malloc` and the rest, which the port's
`libogg/include/ogg/os_types.h` makes the decoder's own bounded allocator
(`ogg_sound.c`: a TLSF heap in working memory the caller gives, and an
allocation that does not fit ends the decoding, as Tremor does not check
its allocations). Its `alloca` calls left are sized by the stream's
channels (the port takes one or two) and by Vorbis's largest block (32 KB
of stack at most on a 32-bit machine).

It is built with `-DNDEBUG -O2` and the game's `-fwrapv` and
`-fno-strict-aliasing`, without its ARM assembly (`_ARM_ASSEM_`);
`tools/linux_build.py`'s `ogg_sound_sources` lists the files.
`port/vita/tests/run_ogg_sound_test.sh` tests and fuzzes it.
