"""Tests for loading Halo: CE Anniversary caches (MCC's version 13) natively
(port/linux/game/cache_file_formats.c; docs/mcc_cea_maps.md).

As tools/test_cache_file_formats.py does for Custom Edition caches, these
build complete but tiny version 13 caches in memory (no game data is needed
or stored) and run the report tool port/tools/cache_file_report.c on them:
compiled with warnings as errors and undefined behaviour trapping, and here
also with AddressSanitizer when the compiler has it, so a crash, an
out-of-bounds read or an undefined operation on malformed input fails the
test that caused it.

Covered: the layout this build gives such a cache (its tag data at
0x50000000, its structure BSPs moved to the room after it, their vertices
left in the file), what mcc_cea_cache_fix makes of MCC's larger arrays
(script syntax nodes packed, object names past 512 moved or dropped, MCC's
bitmap flags), the refusal of a version 13 cache where the game does not
allow one, every check of the loader with a malformed case, and sampled
robustness: every truncation of a valid cache's header and tag data, and
seeded random corruptions of its header, tag data, structure BSP and
vertices, each of which must end in a clean verdict.
"""
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "port/linux/game/cache_file_formats.c"
TOOL = ROOT / "port/tools/cache_file_report.c"

BASE = 0x50000000
TAG_SPACE = 0x04000000
HEADER_BYTES = 0x800
NONE = 0xFFFFFFFF
ALIGNMENT = 0x10000

STRICT_FLAGS = ["-Wall", "-Wextra", "-Wpedantic", "-Werror"]
UB_TRAP_FLAGS = ["-fsanitize=undefined", "-fsanitize-trap=undefined"]

# the scenario's fields (scenario_definitions.h, as cache_file_formats.c has them)
SCENARIO_BYTES = 0x5B0
OBJECT_NAMES = 0x204
SCENERY = 0x210
COMMAND_LISTS = 0x438
CONVERSATIONS = 0x468
SYNTAX_DATA = 0x474
STRINGS = 0x488
SCRIPTS = 0x49C
GLOBALS = 0x4A8
STRUCTURE_BSPS = 0x5A4
MCC_NODES = 32767
GAME_NODES = 19001


def code(text):
    return struct.unpack(">I", text.encode("latin-1"))[0]


def find_clang():
    for candidate in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


@pytest.fixture(scope="session")
def report_tool(tmp_path_factory):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed to build the report tool")
    folder = tmp_path_factory.mktemp("mcc-cea-report")
    output = folder / ("cache_file_report.exe" if sys.platform == "win32" else "cache_file_report")
    errors = []
    # with AddressSanitizer where it links, else as tools/test_cache_file_formats.py builds it
    for extra in (["-fsanitize=address", "-fno-omit-frame-pointer"], []):
        command = [clang, "-std=c99", *STRICT_FLAGS, *UB_TRAP_FLAGS, *extra, "-O1", "-g",
                   f"-I{MODULE.parent}", str(MODULE), str(TOOL), "-o", str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            return output
        errors.append(result.stdout + result.stderr)
    pytest.fail("could not build the report tool:\n" + "\n".join(errors))


def run_report(tool, *arguments):
    result = subprocess.run([str(tool), *map(str, arguments)], capture_output=True, text=True, errors="replace",
                            env={"ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1"})
    # 0: loaded, 1: refused; anything else is a crash, a sanitizer's report
    # or an undefined-behaviour trap
    assert result.returncode in (0, 1), (result.returncode, result.stdout[-2000:], result.stderr[-4000:])
    fields = {}
    for line in result.stdout.splitlines():
        key, _, value = line.partition(": ")
        fields.setdefault(key, value)
    return result.returncode, fields


class Blob:
    def __init__(self, base):
        self.base = base
        self.bytes = bytearray()

    def reserve(self, size, align=4):
        while len(self.bytes) % align:
            self.bytes.append(0)
        address = self.base + len(self.bytes)
        self.bytes.extend(bytes(size))
        return address

    def add(self, data, align=4):
        address = self.reserve(len(data), align)
        self.put(address, data)
        return address

    def put(self, address, data):
        offset = address - self.base
        self.bytes[offset:offset + len(data)] = data

    def u16(self, address, value):
        struct.pack_into("<H", self.bytes, address - self.base, value & 0xFFFF)

    def u32(self, address, value):
        struct.pack_into("<I", self.bytes, address - self.base, value & 0xFFFFFFFF)

    def block(self, address, count, element_address):
        self.u32(address, count)
        self.u32(address + 4, element_address if count else 0)
        self.u32(address + 8, 0)

    def data(self, address, size, data_address):
        for index, value in enumerate((size, 0, 0, data_address, 0)):
            self.u32(address + index * 4, value)


def ident(node_index):
    """a node's identifier (its datum's salt): 16 bits, never 0"""
    return 0x4000 | (node_index % 0x3FFF + 1)


def datum(node_index):
    return ident(node_index) << 16 | node_index


def node_bytes(identifier, constant_type, node_type, flags, next_index, data):
    return struct.pack("<hhhhIiI", identifier, constant_type, node_type, flags, next_index & 0xFFFFFFFF, 0,
                       data & 0xFFFFFFFF)


class Cache:
    """A complete, minimal Halo: CE Anniversary cache: a scenario with one
    or more structure BSPs (CEA headers, their vertices in the file), MCC's
    script node array, object names, a bitmap; every option changes one
    thing from the valid default, and `where` records the file offsets of
    the fields tests corrupt."""

    def __init__(self, bsp_sizes=(0x1000,), name_count=4, name_references=(), script_nodes=None,
                 bitmap=None, flags=7, scenario_type=0, external_tag=False, material=None):
        self.bsp_sizes = bsp_sizes
        self.name_count = name_count
        # (kind, index): "script", "command" or "conversation" naming an object name
        self.name_references = name_references
        # (count, valid indices, extra links): MCC's array of 32767 nodes
        self.script_nodes = script_nodes
        # (width, height, format, flags, mipmaps, pixel bytes)
        self.bitmap = bitmap
        self.flags = flags
        self.scenario_type = scenario_type
        self.external_tag = external_tag
        self.material = material or {}
        self.where = {}

    def build(self):
        tags = [("scnr", "test\\scenario"), ("bitm", "test\\bitmap")]
        bsp_tag_indices = []
        for index in range(len(self.bsp_sizes)):
            bsp_tag_indices.append(len(tags))
            tags.append(("sbsp", f"test\\bsp {index}"))
        salt = 0xE000

        # the file: header, each BSP's vertices then the BSP, model data, pixels, tag data
        vertex_count = self.material.get("vertex_count", 3)
        vertices = bytearray()
        for _ in range(vertex_count):
            vertices += struct.pack("<3f9f2f", 0.0, 0.0, 0.0, *self.material.get("normal", (0.0, 0.0, 1.0)),
                                    1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)
        for _ in range(vertex_count):
            vertices += struct.pack("<3f2f", 0.0, 0.0, 1.0, 0.0, 0.0)
        data = bytearray(HEADER_BYTES)
        bsp_records = []
        for index, size in enumerate(self.bsp_sizes):
            vertices_offset = len(data)
            data += vertices
            address = BASE + TAG_SPACE - size - index * 0x10000
            bsp_offset = len(data)
            data += self.structure_bsp(size, address, len(vertices), vertices_offset, vertex_count)
            bsp_records.append((bsp_offset, size, address, vertices_offset))
        model_offset = len(data)
        model_data = bytes(64)
        data += model_data
        pixels_offset = len(data)
        pixel_bytes = self.bitmap[5] if self.bitmap else 32
        data += bytes(pixel_bytes)
        tag_data_offset = len(data)

        tag_data = Blob(BASE)
        index = tag_data.reserve(0x28)
        instances = tag_data.reserve(len(tags) * 0x20)
        tag_data.u32(index + 0x00, instances)
        tag_data.u32(index + 0x04, salt << 16)
        tag_data.u32(index + 0x0C, len(tags))
        tag_data.u32(index + 0x14, model_offset)
        tag_data.u32(index + 0x1C, 32)
        tag_data.u32(index + 0x20, len(model_data))
        tag_data.u32(index + 0x24, code("tags"))
        for tag_index, (group, name) in enumerate(tags):
            instance = instances + tag_index * 0x20
            tag_data.u32(instance, code(group))
            tag_data.u32(instance + 0x04, NONE)
            tag_data.u32(instance + 0x08, NONE)
            tag_data.u32(instance + 0x0C, ((salt + tag_index) << 16) | tag_index)
            tag_data.u32(instance + 0x10, tag_data.add(name.encode("latin-1") + b"\0", align=1))
        if self.external_tag:
            tag_data.u32(instances + 1 * 0x20 + 0x18, 1)

        scenario = tag_data.reserve(SCENARIO_BYTES)
        references = tag_data.reserve(0x20 * len(self.bsp_sizes))
        tag_data.block(scenario + STRUCTURE_BSPS, len(self.bsp_sizes), references)
        for bsp_index, (bsp_offset, size, address, _) in enumerate(bsp_records):
            element = references + bsp_index * 0x20
            tag_index = bsp_tag_indices[bsp_index]
            tag_data.u32(element + 0x00, bsp_offset)
            tag_data.u32(element + 0x04, size)
            tag_data.u32(element + 0x08, address)
            tag_data.u32(element + 0x10, code("sbsp"))
            tag_data.u32(element + 0x1C, ((salt + tag_index) << 16) | tag_index)

        # object names, each placing a piece of scenery of its own
        names = tag_data.reserve(0x24 * self.name_count)
        tag_data.block(scenario + OBJECT_NAMES, self.name_count, names)
        for name_index in range(self.name_count):
            tag_data.put(names + name_index * 0x24, f"name{name_index}".encode() + b"\0")
            struct.pack_into("<hh", tag_data.bytes, names + name_index * 0x24 + 0x20 - BASE, 6, name_index)
        scenery = tag_data.reserve(0x48 * self.name_count)
        tag_data.block(scenario + SCENERY, self.name_count, scenery)
        for name_index in range(self.name_count):
            tag_data.u16(scenery + name_index * 0x48 + 2, name_index)
        self.addresses = {"names": names, "scenery": scenery}
        command_names = [index for kind, index in self.name_references if kind == "command"]
        conversation_names = [index for kind, index in self.name_references if kind == "conversation"]
        script_names = [index for kind, index in self.name_references if kind == "script"]
        if command_names:
            lists = tag_data.reserve(0x60)
            commands = tag_data.reserve(0x20 * len(command_names))
            tag_data.block(scenario + COMMAND_LISTS, 1, lists)
            tag_data.block(lists + 0x30, len(command_names), commands)
            for command_index, name_index in enumerate(command_names):
                tag_data.u16(commands + command_index * 0x20 + 0x18, name_index)
            self.addresses["commands"] = commands
        if conversation_names:
            conversations = tag_data.reserve(0x74)
            participants = tag_data.reserve(0x54 * len(conversation_names))
            tag_data.block(scenario + CONVERSATIONS, 1, conversations)
            tag_data.block(conversations + 0x50, len(conversation_names), participants)
            for participant_index, name_index in enumerate(conversation_names):
                tag_data.u16(participants + participant_index * 0x54 + 0x08, name_index)
                tag_data.u16(participants + participant_index * 0x54 + 0x0A, 0xFFFF)
            self.addresses["participants"] = participants

        # MCC's script node array: `count` nodes, those of `valid` in use
        if self.script_nodes is not None or script_names:
            count, valid, links = self.script_nodes or (len(script_names) + 2, None, {})
            valid = list(range(count)) if valid is None else list(valid)
            array = tag_data.reserve(0x38 + MCC_NODES * 20)
            tag_data.put(array, b"script node\0")
            struct.pack_into("<hhBBHIhhhhI", tag_data.bytes, array + 0x20 - BASE, MCC_NODES, 20, 1, 0, 0,
                             code("d@t@"), 0, count, len(valid), 0, 0x38)
            tag_data.data(scenario + SYNTAX_DATA, 0x38 + MCC_NODES * 20, array)
            nodes = array + 0x38
            for position, node_index in enumerate(valid):
                identifier = ident(node_index)
                next_index = links.get(node_index, NONE)
                if position < len(script_names):
                    # a constant naming an object name (object_name, 39; primitive)
                    payload = node_bytes(identifier, 39, 39, 1 | 8, next_index, script_names[position])
                else:
                    # a call (not primitive) whose first node is the next valid one
                    following = valid[position + 1] if position + 1 < len(valid) else None
                    first = datum(following) if following is not None else NONE
                    payload = node_bytes(identifier, 0, 1, 8, next_index, first)
                tag_data.put(nodes + node_index * 20, payload)
            script = tag_data.reserve(0x5C)
            tag_data.block(scenario + SCRIPTS, 1, script)
            root = valid[-1]
            tag_data.u32(script + 0x24, datum(root))
            global_ = tag_data.reserve(0x5C)
            tag_data.block(scenario + GLOBALS, 1, global_)
            first = valid[0]
            tag_data.u32(global_ + 0x28, datum(first))
            self.addresses.update(array=array, script=script, global_=global_)

        # a bitmap kept in the map
        group = tag_data.reserve(0x6C)
        bitmap = tag_data.reserve(0x30)
        tag_data.block(group + 0x60, 1, bitmap)
        tag_data.u32(bitmap + 0x18, pixels_offset)
        tag_data.u32(bitmap + 0x1C, pixel_bytes)
        if self.bitmap:
            width, height, bitmap_format, flags, mipmaps, _ = self.bitmap
            for offset, value in ((0x04, width), (0x06, height), (0x08, 1), (0x0C, bitmap_format),
                                  (0x0E, flags), (0x14, mipmaps)):
                tag_data.u16(bitmap + offset, value)
        self.addresses["bitmap"] = bitmap
        for tag_index, address in ((0, scenario), (1, group)):
            tag_data.u32(instances + tag_index * 0x20 + 0x14, address)

        data += tag_data.bytes
        struct.pack_into("<IiIIII", data, 0, code("head"), 13, len(data), 0, tag_data_offset, len(tag_data.bytes))
        data[0x20:0x24] = b"test"
        data[0x40:0x4E] = b"01.03.43.0000\0"
        struct.pack_into("<hxxIH", data, 0x60, self.scenario_type, 0x12345678, self.flags)
        struct.pack_into("<I", data, 0x7FC, code("foot"))
        self.where = {
            "tag_data": tag_data_offset,
            "scenario": tag_data_offset + (scenario - BASE),
            "references": tag_data_offset + (references - BASE),
            "bsp": bsp_records[0][0] if bsp_records else None,
            "vertices": bsp_records[0][3] if bsp_records else None,
            "bitmap": tag_data_offset + (bitmap - BASE),
        }
        self.tag_data_size = len(tag_data.bytes)
        self.bsp_records = bsp_records
        return bytes(data)

    def structure_bsp(self, size, address, vertices_size, vertices_offset, vertex_count):
        options = self.material
        bsp = bytearray(size)
        struct.pack_into("<IIIIII", bsp, 0, address + 0x18, vertices_size, vertices_offset, 0, 0, code("sbsp"))
        structure, lightmap, material = 0x18, 0x2A0, 0x2C0
        struct.pack_into("<iII", bsp, structure + 0x104, 1, address + lightmap, 0)
        struct.pack_into("<h", bsp, lightmap, 0)
        struct.pack_into("<iII", bsp, lightmap + 0x14, 1, address + material, 0)
        struct.pack_into("<h", bsp, material + 0xB0, options.get("vertex_type", 0))
        struct.pack_into("<iI", bsp, material + 0xB4, vertex_count, options.get("vertices_offset", 0))
        struct.pack_into("<h", bsp, material + 0xC4, 2)
        struct.pack_into("<iI", bsp, material + 0xC8, options.get("lightmap_vertex_count", vertex_count),
                         options.get("lightmap_vertices_offset", vertex_count * 56))
        struct.pack_into("<i", bsp, material + 0xD8, options.get("uncompressed_size", 0))
        return bsp

    def write(self, folder, name="test.map"):
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / name
        path.write_bytes(self.build())
        return path


def patched(path, offset, fmt, value):
    data = bytearray(path.read_bytes())
    struct.pack_into(fmt, data, offset, value)
    path.write_bytes(data)
    return path


def dumped(tool, path, folder):
    dump = folder / "tags.bin"
    returncode, report = run_report(tool, "--dump-tags", dump, path)
    assert returncode == 0, report
    return report, dump.read_bytes()


def at(tags, address, fmt):
    return struct.unpack_from(fmt, tags, address - BASE)


# ---------- supported layout


def test_minimal_cache_loads_into_a_compact_tag_cache(report_tool, tmp_path):
    cache = Cache(bsp_sizes=(0x1000, 0x3000, 0x2000))
    path = cache.write(tmp_path)
    returncode, report = run_report(report_tool, path)
    assert returncode == 0, report
    assert report["format"] == "Halo: CE Anniversary cache"
    assert report["load"] == "ok" and report["convert"] == "ok"
    slot = (cache.tag_data_size + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
    # the tag data, then room for the largest structure BSP
    assert int(report["tag_cache_bytes"], 16) == slot + ALIGNMENT
    assert int(report["mcc_cea_structure_bsp_address"], 16) == BASE + slot
    for index, (offset, size, address, vertices_offset) in enumerate(cache.bsp_records):
        assert report[f"mcc_cea_structure_bsp.{index}"] == \
            f"0x{offset:x}+0x{size:x} linked to 0x{address:08x}, vertices 0x{vertices_offset:x}+0x{3 * 76:x}"
    assert report["structure_bsp_materials_checked"] == "3"


def test_structure_bsp_references_are_moved_to_the_room_after_the_tags(report_tool, tmp_path):
    cache = Cache(bsp_sizes=(0x1000, 0x2000))
    path = cache.write(tmp_path)
    report, tags = dumped(report_tool, path, tmp_path)
    references = cache.where["references"] - cache.where["tag_data"] + BASE
    slot = int(report["mcc_cea_structure_bsp_address"], 16)
    assert at(tags, references + 8, "<I")[0] == slot
    assert at(tags, references + 0x20 + 8, "<I")[0] == slot


def test_a_version_13_cache_is_refused_where_the_game_does_not_allow_one(report_tool, tmp_path):
    path = Cache().write(tmp_path)
    returncode, report = run_report(report_tool, "--no-mcc-cea", path)
    assert returncode == 1
    assert report["format"] == "Halo: CE Anniversary cache"
    assert report["load"] == "the cache version is not one this build knows"


def test_script_nodes_are_packed_into_this_builds_array(report_tool, tmp_path):
    # 20000 nodes, more than this build's 19001, of which every other one up
    # to 2000 is a hole: 19000 in use
    count = 20000
    valid = [index for index in range(count) if index >= 2000 or index % 2]
    links = {valid[0]: datum(valid[1]), valid[1]: NONE}
    cache = Cache(script_nodes=(count, valid, links))
    path = cache.write(tmp_path)
    report, tags = dumped(report_tool, path, tmp_path)
    assert report["script_nodes_packed"] == str(len(valid))
    array = cache.addresses["array"]
    maximum, element_size = at(tags, array + 0x20, "<hh")
    first_free, node_count, actual_count = at(tags, array + 0x2C, "<hhh")
    assert (maximum, element_size) == (GAME_NODES, 20)
    assert node_count == actual_count == first_free == len(valid)
    scenario = cache.where["scenario"] - cache.where["tag_data"] + BASE
    assert at(tags, scenario + SYNTAX_DATA, "<i")[0] == 0x38 + GAME_NODES * 20
    nodes = array + 0x38

    def node(index):
        return at(tags, nodes + index * 20, "<hhhhIiI")

    # each node moved to its place among those in use, its identifier kept
    for new_index in (0, 1, 500, len(valid) - 1):
        assert node(new_index)[0] == ident(valid[new_index]) - 0x10000 * (ident(valid[new_index]) >= 0x8000)
    # a next link, and a call's first node, given the new index
    assert node(0)[4] == ident(valid[1]) << 16 | 1
    assert node(5)[6] == ident(valid[6]) << 16 | 6
    # the script's root and the global's initializer
    assert at(tags, cache.addresses["script"] + 0x24, "<I")[0] == ident(valid[-1]) << 16 | (len(valid) - 1)
    assert at(tags, cache.addresses["global_"] + 0x28, "<I")[0] == ident(valid[0]) << 16
    # nothing past them
    assert node(len(valid))[0] == 0


def test_more_script_nodes_than_this_build_has_are_refused(report_tool, tmp_path):
    path = Cache(script_nodes=(GAME_NODES + 1, None, {})).write(tmp_path)
    returncode, report = run_report(report_tool, path)
    assert returncode == 1
    assert report["mcc_cea_fix"].startswith("the scenario's scripts use more syntax nodes")


def test_named_object_names_past_512_take_the_places_of_unnamed_ones(report_tool, tmp_path):
    references = [("script", 520), ("command", 530), ("conversation", 515), ("script", 3), ("command", 511)]
    cache = Cache(name_count=531, name_references=references)
    path = cache.write(tmp_path)
    report, tags = dumped(report_tool, path, tmp_path)
    assert report["object_names_moved"] == "3"
    assert report["object_names_dropped"] == "19"
    scenario = cache.where["scenario"] - cache.where["tag_data"] + BASE
    assert at(tags, scenario + OBJECT_NAMES, "<i")[0] == 512
    names = cache.addresses["names"]
    scenery = cache.addresses["scenery"]

    def name(index):
        return tags[names - BASE + index * 0x24:names - BASE + index * 0x24 + 32].split(b"\0")[0].decode()

    # the last unnamed places below 512 (511 is named) take 515, 520 and 530
    assert name(510) == "name515" and name(509) == "name520" and name(508) == "name530"
    assert name(511) == "name511" and name(3) == "name3"
    # the placements: the moved names' at their new places, the displaced
    # and dropped ones none
    assert at(tags, scenery + 515 * 0x48 + 2, "<h")[0] == 510
    assert at(tags, scenery + 530 * 0x48 + 2, "<h")[0] == 508
    assert at(tags, scenery + 510 * 0x48 + 2, "<h")[0] == -1
    assert at(tags, scenery + 525 * 0x48 + 2, "<h")[0] == -1
    assert at(tags, scenery + 3 * 0x48 + 2, "<h")[0] == 3
    # the references
    nodes = cache.addresses["array"] + 0x38
    assert at(tags, nodes + 0 * 20 + 0x10, "<h")[0] == 509
    assert at(tags, nodes + 1 * 20 + 0x10, "<h")[0] == 3
    assert at(tags, cache.addresses["commands"] + 0x18, "<h")[0] == 508
    assert at(tags, cache.addresses["commands"] + 0x20 + 0x18, "<h")[0] == 511
    assert at(tags, cache.addresses["participants"] + 0x08, "<h")[0] == 510


def test_more_than_512_named_object_names_are_refused(report_tool, tmp_path):
    references = [("command", index) for index in range(513)]
    path = Cache(name_count=600, name_references=references).write(tmp_path)
    returncode, report = run_report(report_tool, path)
    assert returncode == 1
    assert report["mcc_cea_fix"] == "the scenario names more objects than this build has room for"


def test_a_dxt_bitmap_not_marked_compressed_is_marked_and_mcc_flags_cleared(report_tool, tmp_path):
    # DXT1 64x64, six mipmaps claimed, pixels for the first three (2048 + 512 + 128)
    cache = Cache(bitmap=(64, 64, 14, 0x201, 6, 2688))
    path = cache.write(tmp_path)
    report, tags = dumped(report_tool, path, tmp_path)
    assert report["bitmaps_marked_compressed"] == "1"
    assert report["bitmaps_flags_cleared"] == "1"
    flags, = at(tags, cache.addresses["bitmap"] + 0x0E, "<H")
    mipmaps, = at(tags, cache.addresses["bitmap"] + 0x14, "<H")
    assert flags & 2 and not flags & 0xFE00
    assert mipmaps == 2


# ---------- malformed input


def corrupt_header_flags(cache, path):
    return patched(path, 0x68, "<H", 0x8)


def corrupt_compressed(cache, path):
    return patched(path, 0x0C, "<I", 0x1000)


def corrupt_file_length(cache, path):
    return patched(path, 0x08, "<I", path.stat().st_size + 1)


def corrupt_tag_data_range(cache, path):
    return patched(path, 0x14, "<I", path.stat().st_size)


def corrupt_tag_data_address(cache, path):
    return patched(path, cache.where["tag_data"], "<I", BASE + 0x1000)


def corrupt_tag_count(cache, path):
    return patched(path, cache.where["tag_data"] + 0x0C, "<i", 0x7FFFFFFF)


def corrupt_scenario_handle(cache, path):
    return patched(path, cache.where["tag_data"] + 0x04, "<I", 0xE0000040)


def corrupt_bsp_count(cache, path):
    return patched(path, cache.where["scenario"] + STRUCTURE_BSPS, "<i", 33)


def corrupt_bsp_below_tag_data(cache, path):
    return patched(path, cache.where["references"] + 8, "<I", BASE + 0x100)


def corrupt_bsp_past_tag_space(cache, path):
    return patched(path, cache.where["references"] + 8, "<I", BASE + TAG_SPACE - 0x10)


def corrupt_bsp_size(cache, path):
    return patched(path, cache.where["references"] + 4, "<i", 0x7FFFFFF0)


def corrupt_bsp_file_offset(cache, path):
    return patched(path, cache.where["references"], "<i", path.stat().st_size - 0x10)


def corrupt_bsp_signature(cache, path):
    return patched(path, cache.where["bsp"] + 0x14, "<I", code("xbsp"))


def corrupt_bsp_pointer(cache, path):
    return patched(path, cache.where["bsp"], "<I", 0x12345678)


def corrupt_bsp_vertices_range(cache, path):
    return patched(path, cache.where["bsp"] + 8, "<I", path.stat().st_size - 4)


def corrupt_material_vertices_offset(cache, path):
    return patched(path, cache.where["bsp"] + 0x2C0 + 0xB8, "<I", 0x100)


def corrupt_material_lightmap_offset(cache, path):
    return patched(path, cache.where["bsp"] + 0x2C0 + 0xCC, "<I", 0xFFFFFFF0)


def corrupt_material_count(cache, path):
    return patched(path, cache.where["bsp"] + 0x2C0 + 0xB4, "<i", 0x10000)


def corrupt_material_uncompressed(cache, path):
    return patched(path, cache.where["bsp"] + 0x2C0 + 0xD8, "<i", 228)


def corrupt_lightmaps_block(cache, path):
    return patched(path, cache.where["bsp"] + 0x18 + 0x104, "<i", 0x01000000)


def corrupt_bitmap_pixels(cache, path):
    return patched(path, cache.where["bitmap"] + 0x18, "<I", path.stat().st_size)


MALFORMED = {
    "header flags": (corrupt_header_flags, "identify", "the Halo: CE Anniversary header is not valid (flags or tag data address)"),
    "compressed": (corrupt_compressed, "identify", "the cache is compressed, which Custom Edition caches never are"),
    "file length": (corrupt_file_length, "identify", "the file length in the header does not fit the file or the size limit"),
    "tag data range": (corrupt_tag_data_range, "identify", "the tag data range in the header does not fit the file or the tag cache"),
    "tag data address": (corrupt_tag_data_address, "identify", "the Halo: CE Anniversary header is not valid (flags or tag data address)"),
    "tag count": (corrupt_tag_count, "identify", "the tag instances do not fit in the tag data"),
    "scenario": (corrupt_scenario_handle, "identify", "the scenario tag is missing, misplaced or not a scenario"),
    "bsp count": (corrupt_bsp_count, "identify", "the scenario's structure BSP block is not valid"),
    "bsp below tag data": (corrupt_bsp_below_tag_data, "identify", "a structure BSP does not fit in the file or in the tag cache"),
    "bsp past tag space": (corrupt_bsp_past_tag_space, "identify", "a structure BSP does not fit in the file or in the tag cache"),
    "bsp size": (corrupt_bsp_size, "identify", "a structure BSP does not fit in the file or in the tag cache"),
    "bsp file offset": (corrupt_bsp_file_offset, "load", "a structure BSP does not fit in the file or in the tag cache"),
    "bsp signature": (corrupt_bsp_signature, "load", "a structure BSP header is not valid"),
    "bsp pointer": (corrupt_bsp_pointer, "load", "a structure BSP header is not valid"),
    "bsp vertices": (corrupt_bsp_vertices_range, "load", "a structure BSP header is not valid"),
    "material vertices": (corrupt_material_vertices_offset, "load", "a structure BSP's lightmaps or their materials do not have the documented layout"),
    "material lightmap vertices": (corrupt_material_lightmap_offset, "load", "a structure BSP's lightmaps or their materials do not have the documented layout"),
    "material count": (corrupt_material_count, "load", "a structure BSP's lightmaps or their materials do not have the documented layout"),
    "material vertex data": (corrupt_material_uncompressed, "load", "a structure BSP's lightmaps or their materials do not have the documented layout"),
    "lightmaps": (corrupt_lightmaps_block, "load", "a structure BSP's lightmaps or their materials do not have the documented layout"),
    "bitmap pixels": (corrupt_bitmap_pixels, "load", "bitmap pixels or sound samples lie outside their file"),
}


@pytest.mark.parametrize("case", sorted(MALFORMED))
def test_malformed_caches_are_refused(report_tool, tmp_path, case):
    corrupt, key, expected = MALFORMED[case]
    cache = Cache()
    path = corrupt(cache, cache.write(tmp_path))
    returncode, report = run_report(report_tool, path)
    assert returncode == 1
    assert report[key] == expected, report


def test_a_tag_held_by_one_of_mccs_resource_maps_is_refused(report_tool, tmp_path):
    path = Cache(external_tag=True).write(tmp_path)
    returncode, report = run_report(report_tool, path)
    assert returncode == 1
    assert report["load"] == "a tag of a group that resource maps never hold is marked as held by one"


def test_a_tag_cache_larger_than_this_build_gives_is_refused(report_tool, tmp_path):
    # a structure BSP of 35 MB: more than the 34.5 MB this build gives a map
    cache = Cache(bsp_sizes=(0x2300000,))
    path = cache.write(tmp_path)
    returncode, report = run_report(report_tool, path)
    assert returncode == 1
    assert report["identify"] == "the tag data and the tags held by resource maps do not fit below the structure BSP"


# ---------- sampled robustness


def test_every_truncation_of_the_header_and_tag_data_is_refused_cleanly(report_tool, tmp_path):
    cache = Cache(bsp_sizes=(0x1000, 0x2000), name_count=520, name_references=[("script", 515)],
                  bitmap=(64, 64, 14, 0x200, 6, 2688))
    pristine = cache.build()
    lengths = sorted(set(list(range(0, HEADER_BYTES + 0x40, 0x10)) +
                         list(range(cache.where["tag_data"], len(pristine), max(1, cache.tag_data_size // 300)))))
    for length in lengths:
        path = tmp_path / "truncated.map"
        path.write_bytes(pristine[:length])
        returncode, report = run_report(report_tool, path)
        assert returncode == 1, length


def test_seeded_corruption_never_crashes_the_loader(report_tool, tmp_path):
    """Sampled robustness, not proof: random bytes and words written over the
    header, the tag data (index, instances, scenario, names, script nodes),
    the structure BSP and its vertices of a valid cache, each of which must
    end in a clean verdict (exit 0 or 1, checked by run_report, under
    AddressSanitizer and undefined-behaviour trapping)."""
    cache = Cache(bsp_sizes=(0x1000, 0x2000), name_count=520,
                  name_references=[("script", 515), ("command", 517), ("conversation", 3)],
                  bitmap=(64, 64, 14, 0x200, 6, 2688))
    pristine = cache.build()
    regions = [
        (0, HEADER_BYTES),
        (cache.where["tag_data"], cache.where["tag_data"] + 0x28 + 0x20 * 4),
        (cache.where["scenario"], cache.where["scenario"] + SCENARIO_BYTES),
        (cache.where["tag_data"], len(pristine)),
        (cache.where["bsp"], cache.where["bsp"] + 0x400),
        (cache.where["vertices"], cache.where["vertices"] + 3 * 76),
    ]
    interesting = [0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFF, BASE, BASE + 0x28, BASE + TAG_SPACE - 4,
                   0x40440000, 0x10000]
    generator = random.Random(20261010)
    for trial in range(400):
        data = bytearray(pristine)
        for _ in range(generator.randint(1, 6)):
            start, end = generator.choice(regions)
            offset = generator.randrange(start, end - 4)
            if generator.random() < 0.5:
                data[offset] = generator.randrange(256)
            else:
                struct.pack_into("<I", data, offset & ~3, generator.choice(interesting))
        path = tmp_path / f"trial{trial % 8}.map"
        path.write_bytes(data)
        run_report(report_tool, path)
