# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic input tests; no game files, downloaded tools or physical FTP."""
import hashlib
import json
from pathlib import Path
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
    files = pc.stage(xbox, source, ce, output)
    assert not (output/pc.INCOMPLETE).exists()
    assert (output/'maps/ui.map').read_bytes() == fakes.xbox_tree()['maps']['ui.map']
    for name, data in fakes.xbox_tree()['maps'].items():
        assert (output/'maps'/name).read_bytes() == data
    for name in ('bitmaps', 'sounds', 'loc'):
        assert (output/'maps'/(name+'.map')).read_bytes() == (ce/(name+'.map')).read_bytes()
    retail = output/'retail_pc'
    assert len(list((retail/'maps').glob('pc_*.map'))) == 19
    assert (retail/'maps/pc_bloodgulch.map').read_bytes() == (source/'Maps/bloodgulch.map').read_bytes()
    assert (retail/'resources/bitmaps.map').read_bytes() == (source/'Maps/bitmaps.map').read_bytes()
    assert not (retail/'maps/bitmaps.map').exists()
    assert not (retail/'maps/bloodgulch.map').exists()
    assert len(list((retail/'previews').glob('pc_*.bmp'))) == 6
    for row in files:
        path = output/row['file']
        assert path.stat().st_size == row['bytes']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
    manifest = json.loads((output/'pc-content-manifest.json').read_text())
    assert manifest['format'] == 2 and manifest['offline_only']
    assert manifest['retail_upload_enabled'] is False
    profile = json.loads((retail/'content.json').read_text())
    assert profile['offline_only'] and profile['runtime_integrated'] is False
    assert profile['maps'][0]['name'] == 'pc_beavercreek'
    places = {(i.folder, i.name) for i in hcv.staged_uploads(str(output))}
    assert ('ux0:data/haloce-vita/maps', 'bloodgulch.map') in places
    assert all('retail_pc' not in folder and not name.startswith('pc_') for folder, name in places)
    assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in before.items())
    with pytest.raises(hcv.InstallerError, match='new output'):
        pc.stage(xbox, source, ce, output)


def test_stock_upload_matches_standard_installer_exactly(tmp_path):
    xbox, source, ce = inputs(tmp_path)
    baseline = tmp_path/'baseline'
    hcv.extract_xbox_files(str(xbox), str(baseline))
    hcv.copy_pc_maps_from_folder(str(ce), str(baseline))
    output = tmp_path/'with-retail'
    pc.stage(xbox, source, ce, output)
    def planned(directory):
        return {(item.folder, item.name): hashlib.sha256(Path(item.local).read_bytes()).hexdigest()
                for item in hcv.staged_uploads(str(directory))}
    assert planned(output) == planned(baseline)


def test_retail_campaign_rejected_before_creating_output(tmp_path):
    xbox, source, ce = inputs(tmp_path)
    output = tmp_path/'output'
    with pytest.raises(hcv.InstallerError, match='campaign'):
        pc.stage(xbox, source, ce, output, campaign=True)
    assert not output.exists()


@pytest.mark.parametrize('manifest', [None, {}, {'format': 1}, [],
    {'format': 2, 'offline_only': False, 'retail_upload_enabled': False},
    {'format': 2, 'offline_only': True, 'retail_upload_enabled': True}])
def test_legacy_or_unrecognized_profiles_cannot_upload(tmp_path, manifest):
    (tmp_path/'retail_pc').mkdir()
    if manifest is not None:
        (tmp_path/'pc-content-manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(hcv.InstallerError, match='unsafe or unsupported'):
        hcv.staged_uploads(str(tmp_path))


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
