#!/usr/bin/env python3
"""
MCC_SOUND_CHECK.PY

Checks, against the player's own MCC files, what the harness played of MCC's
sound effects (run_mcc_sound_test.sh). Nothing of MCC's is written anywhere.

  mcc_sound_check.py events RUN_LOG MCC_SOUND_DIR TAG...

RUN_LOG is a harness log of a run with HALO_TEST_COMMANDS "@sound <tag>" (one
per TAG, in order), HALO_SOUND_TRACE=1 and HALO_AUDIO_PACKET_TRACE=1. For each
tag the sound manager queued a permutation (main.c's @sound; "sound trace:
channel N queue <permutation> (next -1, <bytes> bytes)") and the mixer played
its packets (consecutive places in the sound cache, <bytes> bytes in all;
"mixer: played <frames> frames of a adpcm <n>-channel packet at <place>
(<bytes> bytes), rms <rms>"). This finds that permutation's stream in the
bank (the sound list: the tag's path, sfx for old_sfx, or its path and a
pitch range's name; the stream named for the permutation), decodes the stream
here (the Xbox's IMA ADPCM), and requires the mixer to have played the same
number of frames, and at the same RMS (within 1.5%, the packets' rms being
rounded).
"""
import math
import re
import struct
import sys

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
        963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
        24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima(predictor, index, nibbles):
    out = []
    for nibble in nibbles:
        step = STEP[index]
        delta = step >> 3
        if nibble & 1:
            delta += step >> 2
        if nibble & 2:
            delta += step >> 1
        if nibble & 4:
            delta += step
        predictor = predictor - delta if nibble & 8 else predictor + delta
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX[nibble & 7]))
        out.append(predictor)
    return out


def decode(data, channels):
    """Xbox IMA ADPCM: 36 bytes a 64 frames for one channel; two channels' 4-byte headers, then 8 groups of 4 bytes each."""
    out = [[] for _ in range(channels)]
    step = 36 * channels
    for block in range(0, len(data) - step + 1, step):
        for c in range(channels):
            predictor, index = struct.unpack_from('<hB', data, block + 4 * c)
            nibbles = []
            if channels == 1:
                for byte in data[block + 4:block + 36]:
                    nibbles += [byte & 15, byte >> 4]
            else:
                for group in range(8):
                    at = block + 8 + group * 8 + c * 4
                    for byte in data[at:at + 4]:
                        nibbles += [byte & 15, byte >> 4]
            out[c] += ima(predictor, index, nibbles)
    return out


class Bank:
    def __init__(self, directory):
        self.file = open(directory + '/sounds_adpcm.fsb', 'rb')
        header = self.file.read(60)
        magic, version, count, sh_bytes, nt_bytes, data_bytes, mode = struct.unpack('<4s6I', header[:28])
        assert magic == b'FSB5' and version == 1 and mode == 7
        table = self.file.read(sh_bytes)
        names = self.file.read(nt_bytes)
        self.data0 = 60 + sh_bytes + nt_bytes
        self.streams = []
        at = 0
        for i in range(count):
            word, = struct.unpack_from('<Q', table, at)
            at += 8
            extra = word & 1
            while extra:
                flag, = struct.unpack_from('<I', table, at)
                extra = flag & 1
                at += 4 + ((flag >> 1) & 0xFFFFFF)
            self.streams.append((((word >> 7) & 0x7FFFFFF) << 5, [1, 2, 6, 8][(word >> 5) & 3], word >> 34))
        offsets = struct.unpack_from('<%dI' % count, names, 0)
        self.names = [names[o:names.index(b'\0', o)].decode('latin1') for o in offsets]
        lst = open(directory + '/lst/sounds_adpcm.lst.bin', 'rb').read()
        version, records = struct.unpack_from('<II', lst, 0)
        assert version == 1
        at = 8
        self.records = {}
        for i in range(records):
            length, = struct.unpack_from('<I', lst, at)
            path = lst[at + 4:at + 4 + length - 1].decode('latin1').lower()
            first, streams = struct.unpack_from('<II', lst, at + 4 + length)
            at += 4 + length + 8
            self.records[path] = (first, streams)
        assert at == len(lst)

    def pcm(self, index):
        offset, channels, samples = self.streams[index]
        self.file.seek(self.data0 + offset)
        return decode(self.file.read((samples + 63) // 64 * 36 * channels), channels), samples

    def find(self, tag, permutation):
        """streams of the tag whose name is the permutation's"""
        tag = tag.lower().replace('/', '\\')
        candidates = [tag]
        if tag.startswith('sound\\sfx\\'):
            candidates.append('sound\\old_sfx\\' + tag[10:])
        found = []
        for candidate in candidates:
            for path, (first, count) in self.records.items():
                if path == candidate or path.startswith(candidate + '\\'):
                    for i in range(first, first + count):
                        if re.fullmatch(re.escape(permutation.lower()) + r'_\d+_\d+', self.names[i].lower()):
                            found.append(i)
        return found


def events(log_path, bank, tags):
    lines = open(log_path, errors='replace').read().splitlines()
    failures = 0
    cursor = 0
    for tag in tags:
        while cursor < len(lines) and ('test command: sound ' + tag) not in lines[cursor]:
            cursor += 1
        if cursor >= len(lines):
            print('FAIL: the log has no "test command: sound %s"' % tag)
            return 1
        queue = None
        for j in range(cursor, len(lines)):
            m = re.search(r'sound trace: channel \d+ queue (.*?) \(next -?\d+, (\d+) bytes\)', lines[j])
            if m:
                queue = (j, m.group(1), int(m.group(2)))
                break
        if not queue:
            print('FAIL: %s: no sound was queued' % tag)
            failures += 1
            continue
        j, permutation, size = queue
        streams = bank.find(tag, permutation)
        if not streams:
            print('FAIL: %s: the bank has no stream for permutation "%s"' % (tag, permutation))
            failures += 1
            continue
        packets = []
        for line in lines[j:j + 4000]:
            m = re.search(r'mixer: played (\d+) frames of a adpcm (\d)-channel packet at 0x([0-9a-f]+) \((\d+) bytes\), rms (\d+)', line)
            if m:
                packets.append(tuple(int(m.group(i), 16 if i == 3 else 10) for i in range(1, 6)))
        # the permutation's packets: places one after another (other voices' packets come between), `size` bytes in all
        by_place = {}
        for packet in packets:
            by_place.setdefault(packet[2], packet)
        run = None
        for start in range(len(packets)):
            total, at, frames, energy, first = 0, packets[start][2], 0, 0.0, packets[start]
            while at in by_place and total + by_place[at][3] <= size:
                packet = by_place[at]
                total += packet[3]
                at += packet[3]
                frames += packet[0]
                energy += packet[4] ** 2 * packet[0] * packet[1]
                if total == size:
                    break
            if total == size:
                run = (frames, math.sqrt(energy / (frames * first[1])), first[1])
                break
        if not run:
            print('FAIL: %s: the mixer did not play %d bytes of "%s" from consecutive places' % (tag, size, permutation))
            failures += 1
            continue
        best = None
        for index in streams:
            pcm, samples = bank.pcm(index)
            channels = len(pcm)
            frames = (samples + 63) // 64 * 64
            rms = math.sqrt(sum(v * v for c in pcm for v in c) / (len(pcm[0]) * channels))
            if channels == run[2] and frames == run[0] and abs(rms - run[1]) <= max(2.0, 0.015 * rms):
                best = (index, frames, rms, channels)
                break
        if not best:
            print('FAIL: %s "%s": mixer played %d frames at rms %.0f (%d ch); no stream of the bank matches' % (tag, permutation, run[0], run[1], run[2]))
            failures += 1
        else:
            print('ok: %s "%s" = stream %d (%s): %d frames, %d channel(s), rms %.0f (mixer %.0f)' % (
                tag, permutation, best[0], bank.names[best[0]], best[1], best[3], best[2], run[1]))
        cursor += 1
    return 1 if failures else 0


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == 'events':
        return events(sys.argv[2], Bank(sys.argv[3]), sys.argv[4:])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
