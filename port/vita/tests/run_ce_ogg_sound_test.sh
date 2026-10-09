#!/bin/bash
# Halo Custom Edition maps' Ogg Vorbis and 16-bit PCM sounds on the Linux
# harness (port/linux/game/custom_edition_sounds.c, port/linux/src/ogg_sound.c,
# xbox_adpcm_encoder.c): each map below is loaded, a few of its Ogg Vorbis
# and 16-bit PCM sounds played through
# HALO_TEST_COMMANDS' "@sound" (main.c: as sound_impulse_start plays a sound
# with no object), and the map left for the main menu. With HALO_OGG_TRACE
# the decoder logs each permutation it decodes or encodes (its frames, its
# RMS, its Xbox ADPCM's size and place in the sound cache) and with
# HALO_AUDIO_PACKET_TRACE the mixer each packet it plays out (its place,
# size and RMS: dsound_sdl.c). Each sound must decode to non-silent PCM
# (RMS 300 or more of 32767), and the mixer must play all of its ADPCM, and
# hear it (RMS 100 or more); a 16-bit PCM permutation's ADPCM must play at
# its PCM's RMS within 3% (read as Xbox ADPCM, as it was before, the scarab
# bolt's PCM played as noise); no permutation may fail to decode or to be
# measured, the decoder must give its memory back as the map goes, and no
# assertion or exception be logged.
#   extinction                 (Custom Edition maps: HaloMaps) the
#                              announcer's "slayer", held by Halo PC's
#                              sounds.map (22 kHz stereo), and the map's own
#                              gunfire_sounds\fire2 (44 kHz stereo), whose
#                              buffer sizes give their lengths; and its 16-bit
#                              PCM scarab bolt_impact (22 kHz stereo, 34880
#                              frames) and newghost fire (44 kHz stereo) in
#                              Xbox ADPCM sounds; and the spectre's open, a
#                              44 kHz mono Xbox ADPCM sound the game refused
#                              (mono sounds at 22 kHz only), now decoded and
#                              taken at 22 kHz as it loads; its ADPCM must
#                              play at the halved samples' RMS within 3%, and
#                              no sound may be refused as "not a mono 22k"
#   Covenant_V_Marines_Beta_5  a protected map: every tag is named
#                              <protected> and the buffer sizes are
#                              scrambled, so the lengths come from the
#                              streams' last pages (the 300th and 600th
#                              sound, 22 kHz mono), and the 1052nd, a 16-bit
#                              PCM one (22 kHz mono)
#
#   run_ce_ogg_sound_test.sh
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAPS       the Custom Edition maps (default ../custom-maps-dl
#                           beside the tree)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_OUT           where the logs go (kept)
# Without either map or the resource maps (none are in the repository) it
# says SKIP and passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ce_ogg_sound_test.$$}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }

if [ ! -x "$binary" ]; then
	echo "no harness at $binary (HALO_TEST_VITA)"
	exit 2
fi
if [ ! -e "$data/maps/ui.map" ]; then
	echo "no maps (ui) in $data/maps (HALO_TEST_DATA)"
	exit 2
fi
resources=${HALO_TEST_CE_RESOURCES:-}
if [ -z "$resources" ]; then
	resources="$data/maps"
	[ -e "$resources/sounds.map" ] ||
		resources="$HOME/.local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"
fi
for name in bitmaps sounds loc; do
	if [ ! -e "$resources/$name.map" ]; then
		echo "SKIP: no Halo PC $name.map (HALO_TEST_CE_RESOURCES)"
		exit 0
	fi
done

run() { # MAP SOUND_COMMANDS EXPECTED_SOUNDS
	local map=$1 sounds=$2 expected=$3 log commands
	if [ ! -e "$ce_maps/$map.map" ]; then
		echo "SKIP ($map): not in $ce_maps (HALO_TEST_CE_MAPS)"
		return
	fi
	rm -rf "$out/$map"
	mkdir -p "$out/$map/data/maps" "$out/$map/save" "$out/$map/bin"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$out/$map/data/maps/"
	done
	ln -sfn "$(cd "$ce_maps" && pwd)/$map.map" "$out/$map/data/maps/$map.map"
	for name in bitmaps sounds loc; do
		ln -sfn "$(cd "$resources" && pwd)/$name.map" "$out/$map/data/maps/$name.map"
	done
	cp "$binary" "$out/$map/bin/halo"
	commands="M240:map_name levels\\test\\$map\\$map;${sounds}L420:@menu;"
	log="$out/$map/run.log"
	(cd "$out/$map" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$map/data" \
		HALO_SAVE_ROOT="$out/$map/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER=45 HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_CUSTOM_EDITION=1 HALO_WINDOW_FLOOR_KB=22528 HALO_OGG_TRACE=1 HALO_AUDIO_PACKET_TRACE=1 \
		"HALO_TEST_COMMANDS=$commands" timeout -k 5 165 "$out/$map/bin/halo" > "$log" 2>&1)
	local code=$?
	rm -rf "$out/$map/bin"
	[ "$code" = 0 ] || fail "$map" "the harness exited with $code ($log)"
	grep -aE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" | head -3 | sed 's/^/  /'
	grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" && fail "$map" "an assertion or exception ($log)"
	grep -aE "does not decode|has no length|not in the map" "$log" "$out/$map/data/debug.txt" 2>/dev/null | head -3 |
		sed 's/^/  /'
	grep -aqE "does not decode|has no length|not in the map" "$log" "$out/$map/data/debug.txt" 2>/dev/null &&
		fail "$map" "a sound was not found, measured or decoded ($log)"
	grep -aq "not a mono 22k compressed sound" "$log" "$out/$map/data/debug.txt" 2>/dev/null &&
		fail "$map" "a sound was refused as not a mono 22 kHz one ($log)"
	grep -aq "Ogg Vorbis permutations decoded with the map" "$log" ||
		fail "$map" "the decoder did not stop as the map went ($log)"
	# each decoded permutation, and the mixer's packets from its ADPCM
	awk -v map="$map" -v expected="$expected" '
		function hex(text,   value, index_, digit) {
			value = 0
			sub(/^0x/, "", text)
			for (index_ = 1; index_ <= length(text); index_++) {
				digit = index("0123456789abcdef", tolower(substr(text, index_, 1))) - 1
				value = value * 16 + digit
			}
			return value
		}
		function finish() {
			if (current == "") return
			played_rms = played_frames ? sqrt(played_squares / played_frames) : 0
			printf "  %s: %s%s, %d frames, rms %d, %s in %s ms; the mixer played %d of its %d bytes, rms %d\n",
				map, kind == "pcm" ? "16-bit PCM " : kind == "adpcm" ? "44 kHz Xbox ADPCM " : "", current, frames, rms,
				kind == "pcm" ? "encoded" : kind == "adpcm" ? "taken at 22 kHz" : "decoded", ms,
				played, bytes, played_rms
			if (rms < 300) { printf "FAIL (%s): %s decoded to silence (rms %d)\n", map, current, rms; bad = 1 }
			if (played != bytes) { printf "FAIL (%s): the mixer played %d of %s'"'"'s %d bytes\n", map, played, current, bytes; bad = 1 }
			if (played_rms < 100) { printf "FAIL (%s): the mixer played %s as silence (rms %d)\n", map, current, played_rms; bad = 1 }
			if (kind != "ogg" && (played_rms < rms * 0.97 || played_rms > rms * 1.03)) {
				printf "FAIL (%s): the mixer played %s %s at rms %d, not its rms %d\n", map, kind, current, played_rms, rms
				bad = 1
			}
			count++
			current = ""
		}
		/(ogg|pcm) sound: .* of .*: / && / frames / {
			finish()
			line = $0
			kind = line ~ /adpcm sound: / ? "adpcm" : line ~ /pcm sound: / ? "pcm" : "ogg"
			sub(/.*(ogg|pcm) sound: /, "", line)
			current = line; sub(/: .*/, "", current)
			status_ = line; sub(/^[^:]*: /, "", status_); sub(/,.*/, "", status_)
			if (status_ != "decoded") { printf "FAIL (%s): %s: %s\n", map, current, status_; bad = 1 }
			frames = line; sub(/^[^,]*, /, "", frames); sub(/ frames.*/, "", frames)
			rms = line; sub(/.*, rms /, "", rms); sub(/ .*/, "", rms)
			ms = line; sub(/.*peak [0-9-]*, /, "", ms); sub(/ ms.*/, "", ms)
			bytes = line; sub(/.*adpcm /, "", bytes); sub(/ bytes.*/, "", bytes)
			# (numbers, not strings: "2739" < 300 compares as text)
			frames += 0; rms += 0; bytes += 0
			start = line; sub(/.* at /, "", start); start = hex(start)
			played = 0; played_squares = 0; played_frames = 0
			next
		}
		/test command at tick/ { finish() }
		/mixer: played / && current != "" {
			line = $0
			address = line; sub(/.* packet at /, "", address); sub(/ .*/, "", address); address = hex(address)
			if (address >= start && address < start + bytes) {
				size = line; sub(/.*\(/, "", size); sub(/ bytes.*/, "", size)
				packet_frames = line; sub(/.*mixer: played /, "", packet_frames); sub(/ frames.*/, "", packet_frames)
				packet_rms = line; sub(/.*rms /, "", packet_rms)
				played += size
				played_squares += packet_rms * packet_rms * packet_frames
				played_frames += packet_frames
			}
		}
		END {
			finish()
			if (count < expected) { printf "FAIL (%s): %d permutations decoded, not %d\n", map, count, expected; bad = 1 }
			exit bad
		}' "$log" || status=1
	grep -a "Ogg Vorbis permutations decoded with the map" "$log" | sed 's/^halo-linux: /  /'
}

mkdir -p "$out"
run extinction 'L60:@sound sound\dialog\multiplayer1\slayer;L150:@sound sound\sfx\weapons\gunfire_sounds\fire2;L250:@sound vehicles\scarab\bolt\bolt_impact;L330:@sound vehicles\newghost\sounds\fire;L380:@sound vehicles\spectre\open;' 5
run Covenant_V_Marines_Beta_5 'L60:@sound <protected> 300;L150:@sound <protected> 600;L250:@sound <protected> 1052;' 3
if [ "$status" = 0 ]; then
	echo "PASS: Custom Edition Ogg Vorbis and 16-bit PCM sounds decoded and played ($out)"
fi
exit $status
