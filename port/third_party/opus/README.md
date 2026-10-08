# Opus

The Opus audio codec's reference implementation (libopus), by Xiph.Org,
Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO,
Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla and Amazon,
under the BSD-style licence in `COPYING` (authors: `AUTHORS`).

Upstream: https://opus-codec.org/, release 1.6.1
(`opus-1.6.1.tar.gz` from https://downloads.xiph.org/releases/opus/,
SHA-256 `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1`,
as Xiph's `SHA256SUMS.txt` lists it; the version VitaSDK's own libopus has).

Only what voice chat (`port/linux/src/voice_audio.c`) builds is here, copied
unchanged: `include/` (the API), `celt/` and `silk/` with SILK's
fixed-point half (`silk/fixed/`), and `src/`'s single-stream encoder and
decoder (`opus.c`, `opus_encoder.c`, `opus_decoder.c`, `extensions.c`,
`repacketizer.c`). Left out: the SIMD and assembly variants (x86, ARM, MIPS),
the floating-point SILK, the encoder's float analysis, multistream and
projection (ambisonics), the neural network features of 1.5 and later
(`dnn/`: deep PLC, DRED, OSCE; all off by default), the tests, the
documentation and the build systems, and the headers nothing built includes.

Built the same way on every port (`tools/linux_build.py`'s `OPUS_FLAGS`):
`FIXED_POINT` with `DISABLE_FLOAT_API` (16-bit samples in and out: on the
Vita's Cortex-A9 the fixed-point build encodes 10-30% faster than the
floating-point one at voice chat's settings, measured as Cortex-A9 code on
a Raspberry Pi 4), `VAR_ARRAYS` (its scratch space on the calling thread's
stack: voice chat's codec thread has a stack of its own for it, 128 KB,
of which encoding and decoding at 16 kHz take under 32 KB), its
fixed-point arithmetic wrapping (`-fwrapv`: a malformed packet can
overflow a product in SILK's decoder, which is noise rather than undefined
behaviour; the network fuzz target found one) and the C code alone, no
run-time CPU detection.
