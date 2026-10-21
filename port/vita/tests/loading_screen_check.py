#!/usr/bin/env python3
"""loading_screen_check.py: run_loading_screen_test.sh's helpers.

  synthetic OUT        writes an Xbox executable of the retail one's structure
                       around the loading picture and chant (progress_bar.c,
                       retail_find_loading_image / retail_find_loading_tone):
                       the code that loads them, a 320x240 run-length picture
                       (a checkerboard, made here) and a few blocks of Xbox
                       ADPCM (a tone, made here). No retail byte in it.
  garbage OUT SIZE     an "XBEH" header and SIZE pseudo-random bytes (seeded)
  frames DIR SECONDS   the brightest of the screenshots written in the first
                       SECONDS (from the first one's time; the load's, before
                       the level's own frames): its name, the share of its
                       pixels lit (any channel over 40) and of them blue-lit
                       (blue over 40 and over red by 10)
  audio FILE SECONDS   the loudest quarter second (RMS) of a 48 kHz stereo
                       float dump (HALO_AUDIO_DUMP) in its first SECONDS
  png BMP PNG          a screenshot as a PNG (no other module needed)
"""
import array
import glob
import math
import os
import random
import struct
import sys
import zlib

BASE = 0x10000
SECTION_VA = 0x11000
SECTION_RAW = 0x1000


def synthetic(path):
    section = bytearray()

    def place(data):
        address = SECTION_VA + len(section)
        section.extend(data)
        while len(section) % 4:
            section.append(0)
        return address

    width_va = place(struct.pack('<h', 320))
    height_va = place(struct.pack('<h', 240))
    runs = bytearray()
    for y in range(240):
        x = 0
        while x < 320:
            count = min(15, 320 - x, 40 - x % 40)
            intensity = 15 if (x // 40 + y // 40) % 2 else 2
            runs.append(count << 4 | intensity)
            x += count
    runs_va = place(bytes(runs))
    tone = bytearray()
    for block in range(64):
        tone += struct.pack('<hBB', 0, 40, 0)
        tone += bytes([0x71 if (block + i) % 2 else 0xf9 for i in range(32)])
    tone_va = place(bytes(tone))
    # (the picture's loader, then the chant's SetBufferData and loop region,
    # 18 bytes apart as in the retail executable)
    code = b'\x0f\xbf\x05' + struct.pack('<I', height_va) + b'\x0f\xbf\x0d' + struct.pack('<I', width_va)
    code += b'\x68' + struct.pack('<I', len(runs)) + b'\x68' + struct.pack('<I', runs_va) + b'\x90' * 8
    code += b'\x68' + struct.pack('<I', len(tone)) + b'\x68' + struct.pack('<I', tone_va) + b'\x50\xe8\0\0\0\0'
    code += b'\x8b\x0e' + b'\x68' + struct.pack('<I', len(tone)) + b'\x6a\x00\x51\xe8\0\0\0\0' + b'\xc3'
    place(code)
    header = bytearray(SECTION_RAW)
    header[0:4] = b'XBEH'
    struct.pack_into('<I', header, 0x104, BASE)
    struct.pack_into('<I', header, 0x11c, 1)
    struct.pack_into('<I', header, 0x120, BASE + 0x200)
    struct.pack_into('<5I', header, 0x200, 0, SECTION_VA, len(section), SECTION_RAW, len(section))
    with open(path, 'wb') as file:
        file.write(bytes(header) + bytes(section))


def garbage(path, size):
    generator = random.Random(2276)
    with open(path, 'wb') as file:
        file.write(b'XBEH' + bytes(generator.getrandbits(8) for _ in range(size)))


def bmp_pixels(path):
    data = open(path, 'rb').read()
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    bits, = struct.unpack_from('<H', data, 28)
    step = bits // 8
    row = (width * step + 3) & ~3
    return data, offset, width, abs(height), height < 0, step, row


def frames(directory, seconds):
    best = None
    paths = sorted(glob.glob(os.path.join(directory, '*.bmp')))
    start = min((os.path.getmtime(path) for path in paths), default=0)
    for path in paths:
        if os.path.getmtime(path) > start + seconds:
            continue
        data, offset, width, height, _, step, row = bmp_pixels(path)
        lit = blue = samples = 0
        for y in range(0, height, 4):
            line = data[offset + y * row:offset + y * row + width * step]
            for x in range(0, width * step, step * 4):
                b, g, r = line[x], line[x + 1], line[x + 2]
                samples += 1
                if max(r, g, b) > 40:
                    lit += 1
                    if b > 40 and b > r + 10:
                        blue += 1
        entry = (lit / samples, blue / samples, os.path.basename(path))
        if not best or entry > best:
            best = entry
    if best:
        print('%s %.4f %.4f' % (best[2], best[0], best[1]))
    else:
        print('none 0 0')


def audio(path, seconds):
    samples = array.array('f')
    with open(path, 'rb') as file:
        samples.frombytes(file.read(int(48000 * 2 * 4 * seconds)))
    loudest = 0.0
    window = 48000 // 4 * 2
    for start in range(0, len(samples), window):
        part = samples[start:start + window]
        if part:
            loudest = max(loudest, math.sqrt(sum(value * value for value in part) / len(part)))
    print('%.5f' % loudest)


def png(source, destination):
    data, offset, width, height, top_down, step, row = bmp_pixels(source)
    raw = bytearray()
    for y in range(height):
        line = data[offset + (y if top_down else height - 1 - y) * row:][:width * step]
        raw.append(0)
        for x in range(0, width * step, step):
            raw += bytes((line[x + 2], line[x + 1], line[x]))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    with open(destination, 'wb') as file:
        file.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    command = sys.argv[1]
    if command == 'synthetic':
        synthetic(sys.argv[2])
    elif command == 'garbage':
        garbage(sys.argv[2], int(sys.argv[3]))
    elif command == 'frames':
        frames(sys.argv[2], float(sys.argv[3]))
    elif command == 'audio':
        audio(sys.argv[2], float(sys.argv[3]))
    elif command == 'png':
        png(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
