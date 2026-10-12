#!/bin/bash
# A Custom Edition map's long Ogg Vorbis music plays whole on the Linux
# harness (gxm-null), and loops where it ends (custom_edition_sounds.c's
# chains; port/linux/src/ogg_sound.c's pieces). The sound cache gives a
# permutation 1 MB of Xbox ADPCM at most (OGG_SOUND_MAXIMUM_ADPCM_BYTES), and
# a longer sound was cut off there: rainbow-road's music (sound\russian-mix\
# i-love-felicia-l, one Ogg Vorbis permutation of 222.6 s of 22 kHz stereo,
# 5.4 MB of ADPCM) went back to its start after 42.3 s (932032 frames). As
# the map loads, such a permutation is now made a chain of permutations of
# ~256 KB, as Halo's own music is, each link decoded as the sound manager
# asks for it, taking up the decoding where the link before kept it.
#
# With HALO_FIXED_TICK (frames drive the mixer, a tick's audio each, and the
# sound cache's loads are done before the sound manager looks) each case
# plays the music past its whole length, the logs of the decoder
# (HALO_OGG_TRACE), the channels (HALO_SOUND_TRACE) and the mixer
# (HALO_AUDIO_PACKET_TRACE) are read, and:
#   - the map log says the permutation is played as a chain of K links whose
#     frames are the stream's whole length (its last Ogg page's granule
#     position, read from the map here) in whole blocks;
#   - each link was decoded once, in order, from the frame the one before
#     ended at, links 2 to K taking up the decoding the one before kept, and
#     the frames decoded add up to the stream's to the frame;
#   - the mixer played every byte of every link;
#   - looping: the channel queued links 1 to K in order, and link 1 again
#     only after link K (the loop point is the music's end, not 42 s in);
#     impulse (a script's sound_impulse_start, "@sound"): links 1 to K in
#     order, then nothing (the Xbox played an impulse sound's first
#     permutation only);
#   - the map's going logs the links decoded, no permutation failed to
#     decode or be measured, and no assertion or exception was logged.
#
#   run_ce_long_music_test.sh [looping] [impulse]   (default both)
#   HALO_TEST_VITA          the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA          a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_CE_MAP        the Custom Edition map (default rainbow-road.map in
#                           HALO_TEST_CE_MAPS, default ../custom-maps-dl beside
#                           the tree)
#   HALO_TEST_CE_SOUND      its long sound (default sound\russian-mix\i-love-felicia-l)
#   HALO_TEST_CE_RESOURCES  Halo PC's bitmaps.map, sounds.map and loc.map
#                           (default the data's maps folder, else Steam's MCC
#                           halo1/maps/custom_edition)
#   HALO_TEST_SECONDS       the most each case runs (default 300)
#   HALO_TEST_OUT           where the logs go (deleted after a pass when the
#                           test made it; HALO_TEST_KEEP=1 keeps them)
# Without the map or the resource maps (none are in the repository) it says
# SKIP and passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
map_file=${HALO_TEST_CE_MAP:-$ce_maps/rainbow-road.map}
sound=${HALO_TEST_CE_SOUND:-'sound\russian-mix\i-love-felicia-l'}
seconds=${HALO_TEST_SECONDS:-300}
cases=${*:-looping impulse}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_ce_long_music_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
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
if [ ! -e "$map_file" ]; then
	echo "SKIP: no $map_file (HALO_TEST_CE_MAP)"
	exit 0
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
map=$(basename "$map_file" .map)
mkdir -p "$out"

# the sound's longest permutation, as the map has it: its name, and the
# frames its Ogg Vorbis stream makes at the sound's rate (the granule
# position of the stream's last page with one; xbox_adpcm_rate_frames)
expected=$(python3 -I - "$map_file" "$resources/sounds.map" "$sound" <<'EOF'
import struct, sys
path, sounds_path, wanted = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(path, 'rb').read()
u32 = lambda d, o: struct.unpack_from('<I', d, o)[0]
s16 = lambda d, o: struct.unpack_from('<h', d, o)[0]
tag_data = u32(data, 0x10)
address = lambda pointer: tag_data + pointer - 0x40440000
instances, count = u32(data, tag_data), u32(data, tag_data + 12)
for index in range(count):
    entry = address(instances) + index * 32
    if data[entry:entry + 4] != b'!dns' or u32(data, entry + 24):
        continue
    name_at = address(u32(data, entry + 16))
    if data[name_at:data.index(b'\0', name_at)].decode('latin1').lower() != wanted.lower():
        continue
    sound = address(u32(data, entry + 20))
    rate = 44100 if s16(data, sound + 6) == 1 else 22050
    channels = 2 if s16(data, sound + 0x6C) == 1 else 1
    if channels == 1 and rate == 44100:
        rate = 22050
    best = None
    for range_index in range(u32(data, sound + 0x98)):
        pitch_range = address(u32(data, sound + 0x9C)) + range_index * 0x48
        for permutation_index in range(u32(data, pitch_range + 0x3C)):
            permutation = address(u32(data, pitch_range + 0x40)) + permutation_index * 0x7C
            size, flags, offset = u32(data, permutation + 0x40), u32(data, permutation + 0x44), u32(data, permutation + 0x48)
            if best is None or size > best[1]:
                best = (data[permutation:permutation + 32].split(b'\0')[0].decode('latin1'), size, flags, offset)
    name, size, flags, offset = best
    stream = (open(sounds_path, 'rb').read() if flags & 1 else data)[offset:offset + size]
    serial = u32(stream, 14)
    stream_rate = u32(stream, 28 + 12) & 0x7FFFFFFF
    frames = None
    at = stream.rfind(b'OggS')
    while at >= 0:
        granule = struct.unpack_from('<q', stream, at + 6)[0]
        if u32(stream, at + 14) == serial and granule >= 0:
            frames = granule
            break
        at = stream.rfind(b'OggS', 0, at)
    if stream_rate == rate:
        output = frames
    elif stream_rate == 2 * rate:
        output = (frames + 1) // 2
    else:
        output = (frames * rate + stream_rate - 1) // stream_rate
    print(name, output, channels, rate, sep='\t')
    break
EOF
)
perm_name=$(echo "$expected" | cut -f1)
expected_frames=$(echo "$expected" | cut -f2)
if [ -z "$expected_frames" ]; then
	echo "FAIL: no Ogg Vorbis sound $sound in $map_file (HALO_TEST_CE_SOUND)"
	exit 1
fi
echo "  $map: $sound ($perm_name): its stream makes $expected_frames frames ($(echo "$expected" | cut -f3) ch $(echo "$expected" | cut -f4) Hz)"

run() { # CASE
	local name=$1 dir="$out/$1" log commands
	rm -rf "$dir"
	mkdir -p "$dir/data/maps" "$dir/save" "$dir/bin"
	for file in "$(cd "$data" && pwd)"/maps/*.map; do
		ln -sfn "$file" "$dir/data/maps/"
	done
	ln -sfn "$(cd "$(dirname "$map_file")" && pwd)/$map.map" "$dir/data/maps/$map.map"
	for file in bitmaps sounds loc; do
		ln -sfn "$(cd "$resources" && pwd)/$file.map" "$dir/data/maps/$file.map"
	done
	cp "$binary" "$dir/bin/halo"
	# (the music's whole length and a fifth, at 30 ticks a second, then
	# the main menu, which ends the map's decodings, then out)
	local ticks=$((expected_frames * 30 / $(echo "$expected" | cut -f4) * 6 / 5 + 300))
	if [ "$name" = looping ]; then
		# (a game: the player in the level, whose cluster's background
		# sound is the music)
		printf 'game_variant slayer\nmap_name levels\\test\\%s\\%s\n' "$map" "$map" > "$dir/data/init.txt"
		commands="M$ticks:@menu;M60:@exit;"
	else
		# (the map alone, no game: its music unplayed but by the command)
		commands="M240:map_name levels\\test\\$map\\$map;L60:@sound $sound;M$ticks:@menu;M60:@exit;"
	fi
	log="$dir/run.log"
	(cd "$dir" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$dir/data" \
		HALO_SAVE_ROOT="$dir/save" HALO_NO_VSYNC=1 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false HALO_FIXED_TICK=1 HALO_SKIP_DRAWS=1 \
		HALO_CUSTOM_EDITION=1 HALO_WINDOW_FLOOR_KB=22528 HALO_OGG_TRACE=1 HALO_SOUND_TRACE=1 HALO_AUDIO_PACKET_TRACE=1 \
		"HALO_TEST_COMMANDS=$commands" timeout -k 5 $((seconds + 120)) "$dir/bin/halo" > "$log" 2>&1)
	local code=$?
	rm -rf "$dir/bin" "$dir/save"
	[ "$code" = 0 ] || fail "$name" "the harness exited with $code ($log)"
	grep -aE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" | head -3 | sed 's/^/  /'
	grep -aqE "ASSERT|assertion|EXCEPTION|halt_and_catch_fire" "$log" && fail "$name" "an assertion or exception ($log)"
	grep -aE "does not decode|has no length|not in the map|no room for the chains" "$log" "$dir/data/debug.txt" 2>/dev/null |
		head -3 | sed 's/^/  /'
	grep -aqE "does not decode|has no length|not in the map|no room for the chains" "$log" "$dir/data/debug.txt" 2>/dev/null &&
		fail "$name" "a sound was not found, measured or decoded ($log)"
	grep -a "played as a chain" "$log" | sed 's/^halo-linux: /  /'
	grep -a "Ogg Vorbis permutations decoded with the map" "$log" | sed 's/^halo-linux: /  /'
	grep -aq "Ogg Vorbis permutations decoded with the map" "$log" ||
		fail "$name" "the map did not go, or its decoder did not stop ($log)"
	# (the names through the environment: awk -v reads their backslashes
	# as escapes)
	TEST_SOUND="$sound" TEST_PERMUTATION="$perm_name" awk -v case_name="$name" -v expected="$expected_frames" \
		-v channels="$(echo "$expected" | cut -f3)" '
		function hex(text,   value, index_, digit) {
			value = 0
			sub(/^0x/, "", text)
			for (index_ = 1; index_ <= length(text); index_++) {
				digit = index("0123456789abcdef", tolower(substr(text, index_, 1))) - 1
				value = value * 16 + digit
			}
			return value
		}
		function bad(text) { printf "FAIL (%s): %s\n", case_name, text; failed = 1 }
		# (the mixer plays a permutation in packets of 4096 frames from its
		# start: dsound_sdl.c)
		BEGIN { sound = ENVIRON["TEST_SOUND"]; perm = ENVIRON["TEST_PERMUTATION"]; packet_bytes = 4096 / 64 * 36 * channels }
		# the chain the map was given
		index($0, "custom edition: " sound " (") && /played as a chain of/ {
			line = $0
			sub(/.* is /, "", line); chain_frames = line; sub(/ frames.*/, "", chain_frames)
			line = $0; sub(/.*a chain of /, "", line); links = line; sub(/ permutations.*/, "", links)
			chain_frames += 0; links += 0
			next
		}
		# a link decoded: (in the first pass) its place in the chain, from
		# where, how, and the block it went into
		index($0, "ogg sound: " perm " of " sound ": ") && / link [0-9]+ of / {
			line = $0
			number = line; sub(/.* link /, "", number); sub(/ of .*/, "", number); number += 0
			first = line; sub(/.* from frame /, "", first); sub(/,.*/, "", first); first += 0
			frames = line; sub(/^[^,]*, /, "", frames); sub(/ frames.*/, "", frames); frames += 0
			bytes = line; sub(/.*adpcm /, "", bytes); sub(/ bytes.*/, "", bytes); bytes += 0
			start = line; sub(/.* bytes at /, "", start); sub(/,.*/, "", start); start = hex(start)
			resumed = line ~ /, resumed skipping 0 frames/
			if (line !~ /: decoded, /) bad("link " number " did not decode: " line)
			decodes++
			if (decodes <= links) {
				if (number != decodes) bad("link " number " decoded where link " decodes " was due")
				if (first != next_first) bad("link " number " from frame " first ", not " next_first)
				if (number > 1 && !resumed) bad("link " number " was decoded from the stream'"'"'s start, not taken up")
				total += frames
				next_first = first + bytes / (36 * channels) * 64
				link_bytes[number] = bytes
			}
			# (the block: the mixer'"'"'s packets in it are this link'"'"'s until
			# another decoding overlaps it)
			for (other in range_start)
				if (range_start[other] < start + bytes && start < range_start[other] + range_bytes[other])
					delete range_start[other]
			range_start[decodes] = start; range_bytes[decodes] = bytes; range_link[decodes] = number
			next
		}
		/mixer: played / {
			line = $0
			address = line; sub(/.* packet at /, "", address); sub(/ .*/, "", address); address = hex(address)
			size = line; sub(/.*\(/, "", size); sub(/ bytes.*/, "", size); size += 0
			# (the first pass: subscripts are strings, "3" > "22"; each of the
			# link'"'"'s packets once, at its place from the link'"'"'s start: another
			# sound read into the block after it is not the link'"'"'s)
			for (other in range_start)
				if (address >= range_start[other] && address < range_start[other] + range_bytes[other]) {
					offset = address - range_start[other]
					if (other + 0 <= links && offset % packet_bytes == 0 && !((other, offset) in seen)) {
						seen[other, offset] = 1
						played[range_link[other]] += size
					}
					break
				}
			next
		}
		# the channel: the links queued, by the one each is followed by
		index($0, "sound trace: channel ") && index($0, " queue " perm " (next ") {
			line = $0
			following = line; sub(/.* \(next /, "", following); sub(/,.*/, "", following)
			queued[++queue_count] = following + 0
			next
		}
		END {
			if (!links) { bad("the sound was not made a chain"); exit 1 }
			expected_blocks = int((expected + 63) / 64)
			if (chain_frames != expected_blocks * 64)
				bad("the chain is " chain_frames " frames, not the stream'"'"'s " expected " in whole blocks")
			if (decodes < links) bad(decodes " links decoded of " links)
			if (total != expected) bad("the links decoded " total " frames, not the stream'"'"'s " expected)
			for (number = 1; number <= links; number++)
				if (played[number] != link_bytes[number])
					bad("the mixer played " played[number] + 0 " of link " number "'"'"'s " link_bytes[number] + 0 " bytes")
			# links 1..K queued in order (next 1, 2, .., K-1, then -1)
			for (number = 1; number <= links; number++) {
				want = number < links ? number : -1
				if (queued[number] != want) { bad("queued " number ": followed by " queued[number] ", not " want); break }
			}
			if (case_name == "looping" && queue_count <= links)
				bad("the music did not come back to its start after its end (" queue_count " queued)")
			if (case_name == "looping" && queue_count > links && queued[links + 1] != 1)
				bad("after its end the music went on with a link followed by " queued[links + 1] ", not its first")
			if (case_name == "impulse" && queue_count != links)
				bad("the impulse queued " queue_count " links, not " links)
			printf "  %s: %d links decoded in order (%d frames, the stream'"'"'s %d), the mixer played each whole; " \
				"%d queued%s\n", case_name, links, total, expected, queue_count,
				case_name == "looping" ? ", the first again after the last" : ""
			exit failed
		}' "$log" || status=1
}

for case_name in $cases; do
	case $case_name in
	looping|impulse) run "$case_name" ;;
	*) fail "$case_name" "no such case (looping, impulse)" ;;
	esac
done
if [ "$status" = 0 ]; then
	echo "PASS: $map's $sound plays whole, as a chain of permutations ($out)"
fi
test_out_done $status
exit $status
