#!/usr/bin/env python3
"""The tag validator's check of recorded animations (port/linux/game/
tag_schema_scenario.c, recorded_animation_check), run by tools/map_validate.c
on a copy of a Halo Custom Edition map whose recorded animations are changed.

A weapon index of a recorded animation (its unit control's, at byte 4 of
the stream, and each weapon index event's) that is neither one of a unit's
four weapons nor none (0xFFFF) is made none: Halo: CE Anniversary maps carry
0xFF00 (GulchCE's mcc_recordings.c found it on Solar Flare), which a release
build took for weapon -256 of the unit's inventory. A recording of the stock
indices is left as it is; a stream cut short is not played.

No map is in the repository: HALO_TEST_CE_MAP names a Custom Edition map with
recorded animations (default Ruby's converted c10, whose recordings have weapon index events, from the October 2026 run,
~/halo-test-out/mcc-maps.DcyKlC/conv2/ruby_c10.map), HALO_TEST_CE_RESOURCES
the folder with Halo PC's bitmaps.map, sounds.map and loc.map (default
Steam's MCC halo1/maps/custom_edition); without them the tests skip.
"""

import os
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TAG_BASE = 0x40440000
# struct scenario's recorded animations block; struct
# recorded_animation_definition's size and fields (scenario_definitions.h,
# recorded_animation_definitions.h: 32-bit layout)
SCENARIO_RECORDED_ANIMATIONS = 0x36C
RECORDED_ANIMATION_SIZE = 0x40
RECORDED_ANIMATION_VERSION = 0x20
RECORDED_ANIMATION_CONTROL_VERSION = 0x22
RECORDED_ANIMATION_STREAM = 0x2C
UNIT_CONTROL_SIZE = 52


def paths():
    validator = Path(os.environ.get("HALO_TEST_MAP_VALIDATE", ROOT / "build/linux/map_validate"))
    game_map = Path(os.environ.get(
        "HALO_TEST_CE_MAP", Path.home() / "halo-test-out/mcc-maps.DcyKlC/conv2/ruby_c10.map"))
    resources = Path(os.environ.get(
        "HALO_TEST_CE_RESOURCES",
        Path.home() / ".local/share/Steam/steamapps/common/Halo The Master Chief Collection/halo1/maps/custom_edition"))
    if not validator.is_file():
        pytest.skip(f"no {validator} (ninja linux)")
    if not game_map.is_file() or not all((resources / f"{n}.map").is_file() for n in ("bitmaps", "sounds", "loc")):
        pytest.skip("no Custom Edition map and resource maps (HALO_TEST_CE_MAP, HALO_TEST_CE_RESOURCES)")
    return validator, game_map, resources


def recordings(data):
    """(file offset of each recorded animation's stream, its size, version,
    unit control version) of a Custom Edition cache's scenario"""
    tag_data_offset = struct.unpack_from("<I", data, 0x10)[0]

    def file_offset(address):
        return tag_data_offset + address - TAG_BASE

    tags_address, scenario_id, _, tag_count = struct.unpack_from("<IIII", data, tag_data_offset)
    scenario_address = None
    for index in range(tag_count):
        entry = file_offset(tags_address) + index * 32
        if struct.unpack_from("<I", data, entry + 12)[0] == scenario_id:
            scenario_address = struct.unpack_from("<I", data, entry + 20)[0]
    assert scenario_address is not None
    count, address = struct.unpack_from("<II", data, file_offset(scenario_address) + SCENARIO_RECORDED_ANIMATIONS)
    found = []
    for index in range(count):
        element = file_offset(address) + index * RECORDED_ANIMATION_SIZE
        version, control_version = struct.unpack_from("<hh", data, element + RECORDED_ANIMATION_VERSION)
        size = struct.unpack_from("<I", data, element + RECORDED_ANIMATION_STREAM)[0]
        stream_address = struct.unpack_from("<I", data, element + RECORDED_ANIMATION_STREAM + 12)[0]
        found.append((file_offset(stream_address), size, version, control_version, element))
    return found


def event_size(event_type):
    """recorded_event_size (version 4, after the header)"""
    if event_type < 2:
        return 0
    if event_type < 4:
        return 1
    if event_type < 6:
        return 2
    if event_type < 7:
        return 8
    if event_type < 15:
        return 2
    return 4


def weapon_events(data, stream, size, control_version):
    """the file offsets of a version 4 stream's weapon index events' words"""
    offset = UNIT_CONTROL_SIZE + sum(4 if index == 1 else 2 for index in range(1, control_version)) + 12
    found = []
    while offset < size:
        header = data[stream + offset]
        event_type = header >> 2
        offset += 1 if (header & 3) < 2 else (header & 3)
        if event_type == 1:
            break
        if event_type == 5:
            found.append(stream + offset)
        offset += event_size(event_type)
    return found


def validate(validator, resources, path):
    result = subprocess.run([str(validator), "--maps", str(resources), str(path)],
                            capture_output=True, text=True, timeout=300)
    return result.returncode, result.stdout + result.stderr


@pytest.fixture(scope="module")
def setting():
    validator, game_map, resources = paths()
    data = game_map.read_bytes()
    found = [r for r in recordings(data) if r[2] == 4 and r[1] > UNIT_CONTROL_SIZE + 12]
    if not found:
        pytest.skip(f"{game_map.name} has no recorded animation of version 4")
    return validator, game_map, resources, data, found


def test_the_maps_recordings_need_no_correction(setting):
    validator, game_map, resources, data, found = setting
    code, output = validate(validator, resources, game_map)
    assert "weapon indices of no weapon" not in output
    assert code == 0, output[-2000:]


def test_a_unit_controls_weapon_index_of_no_weapon_is_made_none(setting, tmp_path):
    validator, game_map, resources, data, found = setting
    changed = bytearray(data)
    stream = found[0][0]
    struct.pack_into("<H", changed, stream + 4, 0xFF00)
    copy = tmp_path / game_map.name
    copy.write_bytes(changed)
    code, output = validate(validator, resources, copy)
    assert code == 0, output[-2000:]
    assert "has 1 weapon indices of no weapon: none" in output


def test_weapon_index_events_of_no_weapon_are_made_none(setting, tmp_path):
    validator, game_map, resources, data, found = setting
    changed = bytearray(data)
    events = []
    for stream, size, _, control_version, _ in found:
        events = weapon_events(data, stream, size, control_version)
        if events:
            break
    if not events:
        pytest.skip("no weapon index event in the map's recordings")
    for event in events:
        struct.pack_into("<H", changed, event, 0xFF00)
    copy = tmp_path / game_map.name
    copy.write_bytes(changed)
    code, output = validate(validator, resources, copy)
    assert code == 0, output[-2000:]
    assert f"has {len(events)} weapon indices of no weapon: none" in output


def test_a_stream_cut_inside_its_unit_control_is_not_played(setting, tmp_path):
    validator, game_map, resources, data, found = setting
    changed = bytearray(data)
    element = found[0][4]
    struct.pack_into("<I", changed, element + RECORDED_ANIMATION_STREAM, 40)
    copy = tmp_path / game_map.name
    copy.write_bytes(changed)
    code, output = validate(validator, resources, copy)
    assert code == 0, output[-2000:]
    assert "ends inside its unit's control: not played" in output
