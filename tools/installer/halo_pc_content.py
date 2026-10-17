#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# PC-content staging integration by kaboggs, October 2026.
"""Stage the player's own Xbox, retail PC and CE menu files; no downloads.

Never mounts an ISO, executes setup/autorun, or modifies source files.
The output must be new. A failed staging directory is marked incomplete;
upload refuses it until all files and previews have passed validation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile

try:
    from . import halo_ce_vita_installer as installer
    from .pc_map_source import PCInputError, PCMapSource
except ImportError:
    import halo_ce_vita_installer as installer
    from pc_map_source import PCInputError, PCMapSource

MP_MAPS = ('beavercreek', 'bloodgulch', 'boardingaction', 'carousel', 'chillout',
           'damnation', 'dangercanyon', 'deathisland', 'gephyrophobia', 'hangemhigh',
           'icefields', 'infinity', 'longest', 'prisoner', 'putput', 'ratrace',
           'sidewinder', 'timberland', 'wizard')
CAMPAIGN_MAPS = ('a10', 'a30', 'a50', 'b30', 'b40', 'c10', 'c20', 'c40', 'd20', 'd40')
PREVIEWS = {'icefields': 13, 'deathisland': 14, 'dangercanyon': 15,
            'infinity': 16, 'timberland': 17, 'gephyrophobia': 18}
INCOMPLETE = '.pc-content-incomplete'


def _fail(message):
    raise installer.InstallerError(message)


def _read(stream, offset, size, limit):
    if offset < 0 or size < 0 or offset + size > limit:
        _fail('CE preview data is outside the resource file.')
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        _fail('CE preview data is truncated.')
    return data


def _preview_tag(stream, limit):
    kind, names_at, index_at, count = struct.unpack('<4I', _read(stream, 0, 16, limit))
    if (kind != 1 or not 16 <= names_at <= index_at <= limit or
            not 1 <= count <= 1000000 or index_at + count * 12 != limit or
            index_at - names_at > 32 << 20):
        _fail('Invalid CE bitmap resource index.')
    names = _read(stream, names_at, index_at - names_at, limit)
    entries = _read(stream, index_at, count * 12, limit)
    found = None
    for name_at, size, offset in struct.iter_unpack('<III', entries):
        end = names.find(b'\0', name_at)
        if (name_at >= len(names) or end < name_at or
                (name_at and names[name_at - 1] != 0) or offset < 16 or offset + size > names_at):
            _fail('CE resource index contains invalid bounds or names.')
        if names[name_at:end] == b'ui\\shell\\bitmaps\\mp_map_grafix':
            if found is not None or not 0x68 <= size <= 1 << 20:
                _fail('Duplicate or oversized CE preview tag.')
            found = _read(stream, offset, size, limit)
    if found is None:
        _fail('CE menu previews were not found. Select original CE menu resources.')
    return found


def _rgb565(value):
    r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _decode_dxt1(raw, width, height):
    if (width < 140 or height < 114 or width > 1024 or height > 1024 or
            width % 4 or height % 4 or len(raw) < width * height // 2):
        _fail('Unsupported CE preview dimensions or pixel length.')
    pixels = bytearray(width * height * 3)
    cursor = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            first, second, bits = struct.unpack_from('<HHI', raw, cursor)
            cursor += 8
            a, b = _rgb565(first), _rgb565(second)
            colors = [a, b]
            if first > second:
                colors += [tuple((2*a[c]+b[c])//3 for c in range(3)),
                           tuple((a[c]+2*b[c])//3 for c in range(3))]
            else:
                colors += [tuple((a[c]+b[c])//2 for c in range(3)), (0, 0, 0)]
            for y in range(4):
                for x in range(4):
                    at = ((by+y)*width+bx+x)*3
                    pixels[at:at+3] = bytes(colors[(bits >> (2*(y*4+x))) & 3])
    return pixels


def _bmp(rgb, source_width):
    # Match the original widget's 140x114 visible area, omitting texture padding.
    width, height, stride = 140, 114, 420
    data = bytearray()
    for y in range(height - 1, -1, -1):
        row = rgb[y*source_width*3:y*source_width*3+width*3]
        for x in range(0, len(row), 3):
            data += row[x:x+3][::-1]
    return (struct.pack('<2sIHHI', b'BM', 54+len(data), 0, 0, 54) +
            struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, stride*height, 0, 0, 0, 0) + data)


def extract_previews(bitmaps, output):
    bitmaps, output = Path(bitmaps), Path(output)
    limit = bitmaps.stat().st_size
    if not 16 <= limit <= 512 << 20:
        _fail('CE bitmap resource length is unsupported.')
    with bitmaps.open('rb') as stream:
        tag = _preview_tag(stream, limit)
        count, address = struct.unpack_from('<II', tag, 0x60)
        if count != 21 or address < 0x68 or address + count*48 > len(tag):
            _fail('Unsupported CE preview bitmap table.')
        for name, frame in PREVIEWS.items():
            bitmap = tag[address+frame*48:address+(frame+1)*48]
            _, width, height, depth, kind, format_ = struct.unpack_from('<Ihhhhh', bitmap)
            offset, size = struct.unpack_from('<II', bitmap, 0x18)
            if (depth, kind, format_) != (1, 0, 14) or size > 2 << 20:
                _fail('Unsupported CE preview format.')
            rgb = _decode_dxt1(_read(stream, offset, size, limit), width, height)
            target = installer.safe_join(str(output), name + '.bmp')
            with open(target, 'xb') as destination:
                destination.write(_bmp(rgb, width))


def stage(xbox, pc, menu, output, campaign=False):
    source = PCMapSource(pc)
    names = list(MP_MAPS) + ['bitmaps', 'sounds']
    if campaign:
        names += list(CAMPAIGN_MAPS)
    source.validate(names)  # Read/validate before creating any output.
    output = Path(output).expanduser().absolute()
    if output.exists() or output.is_symlink():
        _fail('Choose a new output directory; existing files are never replaced.')
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.parent.resolve() != output.parent:
        _fail('Output parent must not contain symbolic links or aliases.')
    output.mkdir()
    marker = output / INCOMPLETE
    marker.write_text('Incomplete PC content staging. Do not upload.\n', encoding='utf-8')
    installer.extract_xbox_files(str(xbox), str(output))
    source.provide(names, output / 'maps')
    # Retail resources serve gameplay; CE resources are isolated for the menus.
    with tempfile.TemporaryDirectory(prefix='halo-menu-', dir=str(output)) as temporary:
        if Path(menu).is_file():
            installer.extract_pc_maps_from_installer(str(menu), temporary)
        else:
            installer.copy_pc_maps_from_folder(str(menu), temporary)
        ce = Path(temporary) / 'maps'
        destination = output / 'maps/menu_resources'
        destination.mkdir()
        for name in ('bitmaps.map', 'loc.map'):
            shutil.copyfile(ce / name, destination / name)
        shutil.copyfile(ce / 'loc.map', output / 'maps/loc.map')
        extract_previews(ce / 'bitmaps.map', output / 'maps')
    files = []
    for path in sorted(output.rglob('*')):
        if path.is_file() and path != marker:
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(4 << 20), b''):
                    digest.update(block)
            files.append({'file': path.relative_to(output).as_posix(),
                          'bytes': path.stat().st_size, 'sha256': digest.hexdigest()})
    (output / 'pc-content-manifest.json').write_text(json.dumps(
        {'format': 1, 'retail_campaign': campaign, 'files': files}, indent=2) + '\n', encoding='utf-8')
    marker.unlink()
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbox', required=True, help='Your Xbox ISO/XISO or extracted game folder')
    parser.add_argument('--pc', required=True, help='Your retail PC Maps folder or loose-map ISO9660 image')
    parser.add_argument('--menu', required=True, help='Your CE resource folder or CE installer (read only)')
    parser.add_argument('--out', required=True, help='New staging directory, never an existing installation')
    parser.add_argument('--campaign', action='store_true', help='Also use the ten retail PC campaign maps')
    args = parser.parse_args(argv)
    try:
        files = stage(args.xbox, args.pc, args.menu, args.out, args.campaign)
    except (installer.InstallerError, PCInputError, OSError) as error:
        parser.exit(1, 'Staging failed: %s\n' % error)
    print('Validated %d files. Keep this staging output private; it contains your game data.' % len(files))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
