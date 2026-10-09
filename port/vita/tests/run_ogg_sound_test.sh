#!/bin/sh
# Builds and runs the tests of Halo Custom Edition's Ogg Vorbis sounds made
# Xbox ADPCM (port/linux/src/ogg_sound.c, with port/third_party/tremor,
# libogg and tlsf), each with AddressSanitizer and UBSan, 32-bit as the Vita
# and the Linux game are where the compiler can (else as the host is), and
# with the game's -fwrapv:
#   ogg_sound_test.c  the streams in ogg_sound_cases decoded and played as
#                     the mixer decodes ADPCM, lengths, channels, refusals,
#                     every cut and changed byte; with Halo PC's sounds.map
#                     (HALO_TEST_CE_RESOURCES, as run_ce_map_memory_test.sh
#                     finds it; the player's own, not in this repository)
#                     every Ogg Vorbis permutation in it
#   ogg_sound_fuzz.c  OGG_FUZZ_ITERATIONS (default 20000) changed streams
#                     from a fixed seed; with OGG_FUZZ_SECONDS=n, also n
#                     seconds of libFuzzer (64-bit; clang), what it finds in
#                     OGG_FUZZ_OUT
# Tremor shifts negative values left on purpose (its fixed point), which
# UBSan's shift-base check is told to let be in the vendored code.
#   CC  the compiler (default clang if there is one, else cc)
# The streams in ogg_sound_cases were made with FFmpeg's libvorbis, quality
# 0, from its sine source: 0.5 s of a 440 Hz sine at half scale (660 Hz on
# the right), e.g. for sine_44100_2.ogg
#   ffmpeg -f lavfi -i sine=frequency=440:sample_rate=44100:duration=0.5
#     -f lavfi -i sine=frequency=660:sample_rate=44100:duration=0.5
#     -filter_complex "[0][1]join=inputs=2:channel_layout=stereo,volume=4"
#     -c:a libvorbis -q:a 0 sine_44100_2.ogg
# and for the mono ones the 440 Hz sine alone ("-af volume=4 -ac 1").
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/ogg_sound_test.$$
cc=${CC:-$(command -v clang >/dev/null 2>&1 && echo clang || echo cc)}
mkdir -p "$out/obj"
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
flags="-g -O1 -fwrapv -fno-strict-aliasing -I$root/port/third_party/libogg/include -I$root/port/linux/src"
vendored="$root/port/third_party/tremor/block.c $root/port/third_party/tremor/codebook.c
	$root/port/third_party/tremor/floor0.c $root/port/third_party/tremor/floor1.c $root/port/third_party/tremor/info.c
	$root/port/third_party/tremor/mapping0.c $root/port/third_party/tremor/mdct.c $root/port/third_party/tremor/registry.c
	$root/port/third_party/tremor/res012.c $root/port/third_party/tremor/sharedbook.c
	$root/port/third_party/tremor/synthesis.c $root/port/third_party/tremor/window.c
	$root/port/third_party/libogg/src/framing.c $root/port/third_party/libogg/src/bitwise.c
	$root/port/third_party/tlsf/tlsf.c"
bits=-m32
echo 'int main(void) { return 0; }' > "$out/probe.c"
$cc $bits $sanitize "$out/probe.c" -o "$out/probe" 2>/dev/null && "$out/probe" || bits=
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
export OGG_SOUND_CASES="$here/ogg_sound_cases"
status=0

# (the vendored code once, with the sanitizers but shift-base)
objects=
for source in $vendored; do
	object="$out/obj/$(basename "$source" .c).o"
	$cc $bits $sanitize -fno-sanitize=shift-base $flags -DNDEBUG -w -c "$source" -o "$object"
	objects="$objects $object"
done

resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="${HALO_TEST_DATA:-$root/../data2276}/maps"
	[ -e "$resources/sounds.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
$cc $bits $sanitize $flags -Wall "$here/ogg_sound_test.c" "$root/port/linux/src/ogg_sound.c" "$root/port/linux/src/xbox_adpcm_encoder.c" $objects -lm \
	-o "$out/ogg_sound_test"
if [ -e "$resources/sounds.map" ]; then
	(cd "$out" && ./ogg_sound_test --resource-map "$resources/sounds.map") || status=1
else
	echo "(no Halo PC sounds.map: HALO_TEST_CE_RESOURCES)"
	(cd "$out" && ./ogg_sound_test) || status=1
fi
$cc $bits $sanitize $flags -Wall "$here/ogg_sound_fuzz.c" "$root/port/linux/src/ogg_sound.c" "$root/port/linux/src/xbox_adpcm_encoder.c" $objects -lm \
	-o "$out/ogg_sound_fuzz"
(cd "$out" && ./ogg_sound_fuzz) || status=1

seconds=${OGG_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	found=${OGG_FUZZ_OUT:-$out/found}
	mkdir -p "$found/corpus" "$out/obj64"
	objects64=
	for source in $vendored; do
		object="$out/obj64/$(basename "$source" .c).o"
		clang -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all $flags \
			-DNDEBUG -w -c "$source" -o "$object"
		objects64="$objects64 $object"
	done
	# (the seeds: the streams as the target takes them, a first byte of
	# good checksums and the whole output before each)
	for name in sine_22050_1 sine_22050_2 sine_44100_1 sine_44100_2; do
		case $name in
		sine_22050_1) first='\210' ;; sine_22050_2) first='\211' ;; sine_44100_1) first='\212' ;; *) first='\213' ;;
		esac
		{ printf "$first"; cat "$here/ogg_sound_cases/$name.ogg"; } > "$found/corpus/seed_$name"
	done
	clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -DOGG_FUZZ_LIBFUZZER $flags \
		"$here/ogg_sound_fuzz.c" "$root/port/linux/src/ogg_sound.c" "$root/port/linux/src/xbox_adpcm_encoder.c" \
		$objects64 -lm -o "$out/ogg_sound_libfuzzer"
	(cd "$out" && ./ogg_sound_libfuzzer -max_total_time="$seconds" -max_len=131072 -print_final_stats=1 \
		-artifact_prefix="$found/" "$found/corpus" > "$found/fuzz.log" 2>&1) || status=1
	grep -E 'stat::number_of_executed_units|stat::peak_rss|cov:.*ft:' "$found/fuzz.log" | tail -3 || true
	ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
fi

[ "$status" = 0 ] && rm -rf "$out"
exit $status
