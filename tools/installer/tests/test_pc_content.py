# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic input tests; no game files, downloaded tools or physical FTP."""
import hashlib
import json
import struct

import pytest
import fakes
from tools.installer import halo_pc_content as pc
from test_pc_map_source import map_bytes, resource_bytes

hcv = pc.installer


def menu_bitmap():
    # Original data layout, invented white pixels: no commercial image fixture.
    width, height = 140, 116
    tag = bytearray(0x68 + 21*48)
    struct.pack_into('<II', tag, 0x60, 21, 0x68)
    raw = struct.pack('<HHI', 0xffff, 0, 0) * (width*height//16)
    pixel_at = 16 + len(tag)
    for n in range(21):
        struct.pack_into('<Ihhhhh', tag, 0x68+n*48, 0, width, height, 1, 0, 14)
        struct.pack_into('<II', tag, 0x68+n*48+0x18, pixel_at, len(raw))
    name = b'ui\\shell\\bitmaps\\mp_map_grafix\0'
    names_at = pixel_at + len(raw)
    return (struct.pack('<4I', 1, names_at, names_at+len(name), 1) + tag + raw +
            name + struct.pack('<III', 0, len(tag), 16))


def inputs(tmp_path):
    xbox = tmp_path/'xbox.iso'
    fakes.make_xiso(str(xbox), fakes.xbox_tree())
    maps = tmp_path/'PC/Maps'
    maps.mkdir(parents=True)
    for name in pc.MP_MAPS + pc.CAMPAIGN_MAPS:
        (maps/(name+'.map')).write_bytes(map_bytes(name))
    for name, kind in [('bitmaps', 1), ('sounds', 2)]:
        (maps/(name+'.map')).write_bytes(resource_bytes(kind))
    ce = tmp_path/'CE'
    ce.mkdir()
    (ce/'bitmaps.map').write_bytes(menu_bitmap())
    (ce/'sounds.map').write_bytes(resource_bytes(2))
    (ce/'loc.map').write_bytes(resource_bytes(3))
    return xbox, maps.parent, ce


def test_complete_staging_isolated_resources_and_upload_layout(tmp_path):
    xbox, source, ce = inputs(tmp_path)
    before = {p: hashlib.sha256(p.read_bytes()).digest() for p in tmp_path.rglob('*') if p.is_file()}
    output = tmp_path/'new-output'
    files = pc.stage(xbox, source, ce, output, campaign=True)
    assert not (output/pc.INCOMPLETE).exists()
    assert (output/'maps/ui.map').read_bytes() == fakes.xbox_tree()['maps']['ui.map']
    assert (output/'maps/a10.map').read_bytes() == (source/'Maps/a10.map').read_bytes()
    assert (output/'maps/bitmaps.map').read_bytes() != (output/'maps/menu_resources/bitmaps.map').read_bytes()
    assert len(list((output/'maps').glob('*.bmp'))) == 6
    for row in files:
        path = output/row['file']
        assert path.stat().st_size == row['bytes']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
    assert json.loads((output/'pc-content-manifest.json').read_text())['retail_campaign']
    places = {(i.folder, i.name) for i in hcv.staged_uploads(str(output))}
    assert ('ux0:data/haloce-vita/maps/menu_resources', 'bitmaps.map') in places
    assert ('ux0:data/haloce-vita/maps', 'timberland.bmp') in places
    assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in before.items())
    with pytest.raises(hcv.InstallerError, match='new output'):
        pc.stage(xbox, source, ce, output)


def test_failure_remains_marked_and_cannot_upload(tmp_path):
    xbox, source, ce = inputs(tmp_path)
    (ce/'bitmaps.map').write_bytes(resource_bytes(1))
    output = tmp_path/'incomplete'
    with pytest.raises(hcv.InstallerError, match='previews'):
        pc.stage(xbox, source, ce, output)
    assert (output/pc.INCOMPLETE).exists()
    with pytest.raises(hcv.InstallerError, match='incomplete'):
        hcv.staged_uploads(str(output))


@pytest.mark.parametrize('mutation', ['index', 'count', 'pixels', 'dimensions'])
def test_preview_bounds(tmp_path, mutation):
    data = bytearray(menu_bitmap())
    if mutation == 'index': struct.pack_into('<I', data, 8, len(data)+1)
    if mutation == 'count': struct.pack_into('<I', data, 16+0x60, 0xffffffff)
    if mutation == 'pixels': struct.pack_into('<I', data, 16+0x68+13*48+0x18, len(data)+1)
    if mutation == 'dimensions': struct.pack_into('<h', data, 16+0x68+13*48+4, 32767)
    source = tmp_path/'bitmaps.map'
    source.write_bytes(data)
    with pytest.raises(hcv.InstallerError):
        pc.extract_previews(source, tmp_path)
