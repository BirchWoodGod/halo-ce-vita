#!/usr/bin/env python3
"""
mcc_map_convert.py: Halo: CE Anniversary maps (Halo MCC's cache version 13,
such as a Steam Workshop campaign mod's maps) rebuilt as Halo Custom Edition
caches (version 609) that this port's Custom Edition loader plays
(docs/mcc_cea_maps.md).

The work is Invader's (github.com/SnowyMouse/invader, GPL-3.0): this script
only drives its tools and makes the few changes Invader's Custom Edition
build refuses an MCC map's tags without:

1. invader-extract takes every tag out of the MCC map (a classic map keeps
   all its bitmaps and sounds in itself: no MCC resource maps are read);
2. the scenario's scripts, which invader-extract decompiles into the
   scenario, are written out as a source file, with the calls of MCC's own
   script functions (mcc_mission_segment, which marks a mission's segments
   for MCC's statistics) made constants: `true`, what it gives in MCC (its
   maps test it before a cinematic's pause) and this port's script loader
   makes of it;
3. a lens flare reflection naming a bitmap past the end of its flare's
   bitmap (MCC draws none) is given the first, other indices past the end
   of what they index are made none (invader-bludgeon invalid-indices, as
   this port's tag check makes them in any map), a DXT bitmap not marked
   compressed is marked so (in a copy of the map: invader-extract refuses
   it, and its mipmaps cut to those its pixel data holds whole), a HUD
   element naming a sequence its bitmap lacks is given no bitmap (the game
   draws neither), invalid enumerators are made valid ones (invader-bludgeon
   invalid-enums), of the scenario's objects sharing a name, all but the
   first lose it (Invader refuses the scenario), and a scenario with more
   object names than the game's 512 (MCC has room for more) keeps those its
   scripts, AI conversations and command lists name and loses others past
   512 (the objects so named are named nothing; nothing names them);
4. invader-build builds a Custom Edition cache from them, its scripts
   compiled from that source, under a new scenario name (the map file's,
   `--prefix` and the map's name: ruby_a10), so that it is not taken for
   the stock level and Invader leaves its tags as they are (Invader changes
   a few weapon and projectile values of maps it builds under a stock map's
   name), with the tag space the map needs: an MCC map may need more than
   Custom Edition's 23 MB, up to 1.5 times that, as OpenSauce's memory
   upgrades allow, which this port reads from the map's structure BSP
   addresses.

The mod's files are only read, by Invader. Nothing they hold is run: the
scripts are recompiled from their source by Invader's script compiler and
run by the game's own interpreter.

    mcc_map_convert.py --invader DIR --output DIR [--prefix ruby_] MAP.map...

--invader   a folder with invader-extract, invader-build, invader-edit and
            invader-info (an Invader build, 0.55 or later)
--output    where the Custom Edition maps go, named <prefix><map>.map
--work      the extracted tags (default: a folder in --output, deleted
            once a map is built unless --keep-work)
"""

import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

# MCC's script functions that Custom Edition lacks (Invader's riat
# definitions: in "mcc-cea" but not "gbx-custom"), with what a call becomes
MCC_ONLY_FUNCTIONS = {
	# (boolean mcc_mission_segment <string>)
	"mcc_mission_segment": "true",
}

# Custom Edition's tag space, and the most this port gives a map (OpenSauce's
# memory upgrades: 1.5 times it; cache_file_formats.h)
CUSTOM_EDITION_TAG_SPACE = 0x1700000
MAXIMUM_TAG_SPACE = 0x2280000
TAG_SPACE_GRANULARITY = 0x10000

# a stock map's name: Invader alters some tags when building one
STOCK_SCENARIO_NAMES = {
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
	"damnation", "dangercanyon", "deathisland", "gephyrophobia", "hangemhigh",
	"icefields", "infinity", "longest", "prisoner", "putput", "ratrace",
	"sidewinder", "timberland", "ui", "wizard",
}

# the longest map name this port lists (custom_edition_maps.c)
MAXIMUM_MAP_NAME_LENGTH = 25


class ConversionError(Exception):
	pass


def run(arguments, log, cwd=None, check=True):
	"""runs an Invader tool, its output appended to `log`"""
	with open(log, "a", encoding="utf-8", errors="replace") as file:
		file.write("$ " + " ".join(arguments) + "\n")
		file.flush()
		result = subprocess.run(arguments, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		output = result.stdout.decode("utf-8", "replace")
		file.write(output)
	if check and result.returncode != 0:
		raise ConversionError(f"{os.path.basename(arguments[0])} failed (exit {result.returncode}); see {log}")
	return output


def tool(invader, name):
	path = os.path.join(invader, name)
	if not os.access(path, os.X_OK):
		raise ConversionError(f"no {name} in {invader}")
	return path


def map_info(invader, path, kind, log):
	return run([tool(invader, "invader-info"), "-T", kind, path], log).strip()


# a scenario tag's source file (Invader's tag files: big-endian; a 32-byte
# name, then its source's size and 16 bytes the game does not keep)
SOURCE_FILE_BYTES = 52
SOURCE_FILE_NAME_BYTES = 32
MAXIMUM_SOURCE_FILES = 64


def scenario_source(scenario_tag):
	"""the script source files invader-extract put in the scenario tag: each
	decompiled file begins with its banner (";*" and a line of '='), and
	the block of source files (their names and sizes) comes right before
	the first's text, the texts one after another"""
	with open(scenario_tag, "rb") as file:
		data = file.read()
	banner = re.search(rb";\*\r?\n={16}", data)
	if not banner:
		return []
	start = banner.start()
	# (the most files whose names, sizes and banners all agree: fewer would
	# take the last files' sizes for the first's)
	found = []
	for count in range(1, MAXIMUM_SOURCE_FILES + 1):
		array = start - count * SOURCE_FILE_BYTES
		if array < 0:
			break
		sizes = []
		for index in range(count):
			element = array + index * SOURCE_FILE_BYTES
			name = data[element:element + SOURCE_FILE_NAME_BYTES].split(b"\0")[0]
			if not re.fullmatch(rb"[\x20-\x7e]+", name):
				break
			sizes.append(struct.unpack_from(">i", data, element + SOURCE_FILE_NAME_BYTES)[0])
		if len(sizes) != count or min(sizes) <= 0 or start + sum(sizes) > len(data):
			continue
		texts = []
		offset = start
		for size in sizes:
			text = data[offset:offset + size]
			if not text.startswith(b";*"):
				break
			texts.append(text.rstrip(b"\0").decode("latin-1"))
			offset += size
		if len(texts) == count:
			found = texts
	return found


def make_mcc_calls_constant(source):
	"""each call of one of MCC's own functions made its constant; returns
	the source and how many were changed"""
	changed = 0
	for name, constant in MCC_ONLY_FUNCTIONS.items():
		# (their arguments are a constant: a string or a name, no calls)
		pattern = re.compile(r"\(\s*" + re.escape(name) + r"(\s+[^()]*)?\)")
		source, count = pattern.subn(constant, source)
		changed += count
	return source, changed


MCC_CEA_VERSION = 13
MCC_CEA_TAG_BASE = 0x50000000
BITMAP_GROUP = b"mtib"
BITMAP_DATA_REFLEXIVE_OFFSET = 0x60
BITMAP_DATA_BYTES = 0x30
BITMAP_DATA_FLAGS_OFFSET = 0x0E
BITMAP_DATA_COMPRESSED_FLAG = 0x2
BITMAP_DATA_MIPMAP_COUNT_OFFSET = 0x14
BITMAP_DATA_PIXEL_DATA_SIZE_OFFSET = 0x1C
# DXT1, DXT3, DXT5: their bits a pixel
DXT_FORMATS = {14: 4, 15: 8, 16: 8}
BITMAP_TYPE_CUBE_MAP = 2


def dxt_bytes(width, height, depth, mipmap_count, bits, bitmap_type):
	"""a DXT bitmap's pixel data size as Invader computes it (whole 4x4
	blocks, every mipmap down to 1x1 counted as one block)"""
	size = 0
	faces = 6 if bitmap_type == BITMAP_TYPE_CUBE_MAP else 1
	width, height = max(width, 4), max(height, 4)
	for _ in range(mipmap_count + 1):
		size += ((width + 3) // 4 * 4) * ((height + 3) // 4 * 4) * depth * faces * bits // 8
		width, height, depth = max(width // 2, 1), max(height // 2, 1), max(depth // 2, 1)
	return size


def unmarked_dxt_bitmaps(path):
	"""the DXT bitmaps an MCC map does not mark compressed, as (file offset
	of the bitmap data, the mipmap count its pixel data holds whole): MCC's
	tools measured their pixels uncompressed, without whole blocks for the
	smallest mipmaps, which Invader then finds missing (every read
	bounds-checked: the map is untrusted)"""
	with open(path, "rb") as file:
		data = file.read()

	def unpack(format, offset):
		if offset < 0 or offset + struct.calcsize(format) > len(data):
			raise ConversionError(f"{path}: a tag lies outside the map")
		return struct.unpack_from(format, data, offset)

	if unpack("<4sI", 0) != (b"daeh", MCC_CEA_VERSION):
		raise ConversionError(f"{path}: not a cache version {MCC_CEA_VERSION} map")
	tag_data_offset, tag_data_size = unpack("<II", 0x10)

	def file_offset(address, size):
		offset = address - MCC_CEA_TAG_BASE
		if offset < 0 or offset + size > tag_data_size:
			raise ConversionError(f"{path}: a tag lies outside its tag data")
		return tag_data_offset + offset

	tag_array, _, _, tag_count = unpack("<IIII", tag_data_offset)
	offsets = []
	for index in range(tag_count):
		group, _, _, _, _, address, indexed = unpack("<4sIIIIII", file_offset(tag_array + index * 32, 32))
		if group != BITMAP_GROUP or indexed:
			continue
		count, elements = unpack("<iI", file_offset(address + BITMAP_DATA_REFLEXIVE_OFFSET, 8))
		for element in range(max(count, 0)):
			offset = file_offset(elements + element * BITMAP_DATA_BYTES, BITMAP_DATA_BYTES)
			width, height, depth, bitmap_type, bitmap_format, flags = unpack("<HHHHHH", offset + 4)
			if bitmap_format not in DXT_FORMATS or flags & BITMAP_DATA_COMPRESSED_FLAG:
				continue
			mipmap_count, = unpack("<H", offset + BITMAP_DATA_MIPMAP_COUNT_OFFSET)
			pixel_bytes, = unpack("<I", offset + BITMAP_DATA_PIXEL_DATA_SIZE_OFFSET)
			while mipmap_count > 0 and dxt_bytes(width, height, max(depth, 1), mipmap_count,
					DXT_FORMATS[bitmap_format], bitmap_type) > pixel_bytes:
				mipmap_count -= 1
			offsets.append((offset, mipmap_count))
	return offsets


def mark_dxt_bitmaps_compressed(path, bitmaps, copy):
	shutil.copyfile(path, copy)
	with open(copy, "r+b") as file:
		for offset, mipmap_count in bitmaps:
			file.seek(offset + BITMAP_DATA_FLAGS_OFFSET)
			flags, = struct.unpack("<H", file.read(2))
			file.seek(offset + BITMAP_DATA_FLAGS_OFFSET)
			file.write(struct.pack("<H", flags | BITMAP_DATA_COMPRESSED_FLAG))
			file.seek(offset + BITMAP_DATA_MIPMAP_COUNT_OFFSET)
			file.write(struct.pack("<H", mipmap_count))


# the scenario's object blocks, by the name Invader's messages give them
OBJECT_BLOCKS = {
	"Biped": "bipeds", "Vehicle": "vehicles", "Weapon": "weapons",
	"Equipment": "equipment", "Scenery": "scenery", "Machine": "machines",
	"Control": "controls", "Light fixture": "light_fixtures",
	"Sound scenery": "sound_scenery",
}
NO_NAME = 65535


def unshare_object_names(invader, tags, scenario, build_output, log):
	"""of each set of objects Invader found sharing a name, all but the
	first are given none; returns how many"""
	changed = 0
	scenario_tag = scenario + ".scenario"
	for found in re.findall(r"objects with this name: \[([^\]]*)\]", build_output):
		objects = re.findall(r"([A-Za-z ]+?) #(\d+)", found)
		for kind, index in objects[1:]:
			block = OBJECT_BLOCKS.get(kind.strip())
			if not block:
				raise ConversionError(f"objects named alike of an unknown kind ({kind}); see {log}")
			edit(invader, tags, ["-S", f"{block}[{index}].name", str(NO_NAME), scenario_tag], log)
			changed += 1
	return changed


def hide_missing_hud_sequences(invader, tags, build_output, log):
	"""a HUD element naming a sequence its bitmap lacks, which the game does
	not draw (hud_draw.c), is given no bitmap, which it does not draw either
	(Invader refuses the one and takes the other); returns how many"""
	changed = 0
	text = " ".join(build_output.split())
	for element, tag in re.findall(
			r"Sequence #\d+ in '[^']*' referenced by ([a-z ]+?) is out of bounds \(>= \d+\) \.\.\.in (.+?\.[a-z_]+)(?= |$)",
			text):
		base = element.replace(" ", "_")
		for key in (base + "_interface_bitmap", base + "_meter_bitmap"):
			if "rror" not in edit(invader, tags, ["-S", key, "", tag], log, check=False):
				changed += 1
				break
		else:
			raise ConversionError(f"a HUD element ({element}) names a sequence its bitmap lacks; see {log}")
	return changed


# the game's room for object names (MAXIMUM_OBJECT_NAMES_PER_SCENARIO), where
# MCC has more
MAXIMUM_OBJECT_NAMES = 512
# the scenario's fields naming an object name by its index: the placed
# objects' own names, and the references to one
OBJECT_NAME_DEFINITIONS = set(OBJECT_BLOCKS.values())
OBJECT_NAME_REFERENCES = {
	("ai_conversations", "participants", "use_this_object"),
	("ai_conversations", "participants", "set_new_name"),
	("command_lists", "commands", "object_name"),
}
LISTING_LINE = re.compile(r"^ ((?:[ \u2502]   )*)[\u251c\u2514]\u2500\u2500(\S+?)(?: \((.*?)\))?\s{2,}(\S.*)$")


def scenario_listing(invader, tags, scenario_tag, log):
	"""every element field of the scenario as (path, value): invader-edit's
	listing, its tree made keys ("scenery[3].name")"""
	output = edit(invader, tags, ["-L", scenario_tag], log + ".listing")
	os.remove(log + ".listing")
	fields = []
	stack = []
	for line in output.splitlines():
		if not line.startswith((" \u251c", " \u2514", " \u2502", "  ")):
			stack = []
			continue
		match = LISTING_LINE.match(line)
		if not match:
			continue
		depth = len(match.group(1)) // 4
		name, value, kind = match.group(2), match.group(3), match.group(4).strip()
		del stack[depth:]
		stack.append(name if kind == "struct" else None)
		if kind not in ("struct", "array"):
			path = ".".join(part for part in stack[:-1] if part) + "." + name
			fields.append((path, value))
	return fields


def fit_object_names(invader, tags, scenario, scripts, log):
	"""a scenario with more object names than the game has room for keeps
	those its scripts, AI conversations and command lists name first and
	loses the rest past the room (the objects so named are named nothing);
	returns how many it lost"""
	scenario_tag = scenario + ".scenario"
	fields = scenario_listing(invader, tags, scenario_tag, log)
	names = [value for path, value in fields if re.fullmatch(r"object_names\[\d+\]\.name", path)]
	if len(names) <= MAXIMUM_OBJECT_NAMES:
		return 0
	tokens = set()
	for text in scripts:
		tokens.update(re.findall(r"[A-Za-z0-9_\-]+", text))
	referenced = set(index for index, name in enumerate(names) if name in tokens)
	definitions = []
	references = []
	for path, value in fields:
		parts = tuple(re.sub(r"\[\d+\]", "", part) for part in path.split("."))
		if value is None or not value.isdigit():
			continue
		if len(parts) == 2 and parts[0] in OBJECT_NAME_DEFINITIONS and parts[1] == "name":
			definitions.append((path, int(value)))
		elif parts in OBJECT_NAME_REFERENCES:
			references.append((path, int(value)))
			if int(value) < len(names):
				referenced.add(int(value))
	if len(referenced) > MAXIMUM_OBJECT_NAMES:
		raise ConversionError(f"its scripts name {len(referenced)} objects, more than the game's {MAXIMUM_OBJECT_NAMES}")
	order = sorted(referenced) + [index for index in range(len(names)) if index not in referenced]
	order = order[:MAXIMUM_OBJECT_NAMES]
	new_index = {old: new for new, old in enumerate(order)}
	arguments = []
	for index, old in enumerate(order):
		arguments += ["-S", f"object_names[{index}].name", names[old]]
	for path, value in definitions + references:
		if value < len(names):
			arguments += ["-S", path, str(new_index.get(value, NO_NAME))]
	edit(invader, tags, arguments + [scenario_tag], log)
	# (-n: Invader keeps the names block from being edited, as its build
	# makes it; the build still checks every name is used once)
	edit(invader, tags, ["-n", "-E", f"object_names[{MAXIMUM_OBJECT_NAMES}-{len(names) - 1}]", scenario_tag], log)
	return len(names) - MAXIMUM_OBJECT_NAMES


def edit(invader, tags, arguments, log, check=True):
	return run([tool(invader, "invader-edit"), "-t", tags] + arguments, log, check=check)


def fix_lens_flares(invader, tags, log):
	"""a reflection naming a bitmap its flare's bitmap lacks is given the
	first (Invader refuses the map; MCC draws nothing there)"""
	fixed = 0
	for directory, _, files in os.walk(tags):
		for name in files:
			if not name.endswith(".lens_flare"):
				continue
			relative = os.path.relpath(os.path.join(directory, name), tags)
			bitmap = edit(invader, tags, ["-G", "bitmap", relative], log, check=False).strip()
			reflections = edit(invader, tags, ["-C", "reflections", relative], log, check=False).strip()
			if not reflections.isdigit() or not bitmap or bitmap.lower() in ("null", "(null)"):
				continue
			bitmap_path = bitmap.replace("\\", "/")
			if not bitmap_path.endswith(".bitmap"):
				bitmap_path += ".bitmap"
			count = edit(invader, tags, ["-C", "bitmap_data", bitmap_path], log, check=False).strip()
			if not count.isdigit():
				continue
			for index in range(int(reflections)):
				key = f"reflections[{index}].bitmap_index"
				value = edit(invader, tags, ["-G", key, relative], log, check=False).strip()
				if value.lstrip("-").isdigit() and int(value) >= int(count):
					edit(invader, tags, ["-S", key, "0", relative], log)
					fixed += 1
	return fixed


def build(invader, tags, data, scenario, name, tag_space, output, log, check=True):
	return run([
		tool(invader, "invader-build"),
		"-g", "gbx-custom",
		"-S", "data", "-d", data,
		"-t", tags,
		"-N", name,
		"-T", str(tag_space),
		"-o", output,
		scenario,
	], log, check=check)


def tag_space_used(build_output):
	"""the tag space Invader's build said it used, in bytes"""
	match = re.search(r"Tag space:\s+([0-9.]+)\s*/\s*([0-9.]+)\s*MiB", build_output)
	if not match:
		raise ConversionError("invader-build did not say how much tag space the map uses")
	return float(match.group(1)) * 1024 * 1024


# the menus' level pictures (custom_edition_maps.c): up to this many pixels a
# side, the middle 140x114 shown
MAXIMUM_PICTURE_SIZE = 8192
# a description's lines, as the Xbox levels' are
DESCRIPTION_LINE_LENGTH = 22


def png_pixels(path):
	"""an 8-bit RGB or RGBA, non-interlaced PNG's size and its rows of RGB
	pixels (the mod's pictures), or None for any other"""
	with open(path, "rb") as file:
		data = file.read()
	if data[:8] != b"\x89PNG\r\n\x1a\n":
		return None
	offset = 8
	header = None
	compressed = b""
	while offset + 8 <= len(data):
		length, kind = struct.unpack_from(">I4s", data, offset)
		body = data[offset + 8:offset + 8 + length]
		if len(body) != length:
			return None
		if kind == b"IHDR" and length == 13:
			header = struct.unpack(">IIBBBBB", body)
		elif kind == b"IDAT":
			compressed += body
		elif kind == b"IEND":
			break
		offset += 12 + length
	if not header:
		return None
	width, height, depth, color, _, _, interlace = header
	channels = {2: 3, 6: 4}.get(color)
	if depth != 8 or not channels or interlace or not (0 < width <= MAXIMUM_PICTURE_SIZE) or not (0 < height <= MAXIMUM_PICTURE_SIZE):
		return None
	stride = width * channels
	decompressor = zlib.decompressobj()
	raw = decompressor.decompress(compressed, (stride + 1) * height)
	if len(raw) != (stride + 1) * height:
		return None
	rows = []
	previous = bytearray(stride)
	for y in range(height):
		kind = raw[y * (stride + 1)]
		row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
		for x in range(stride):
			left = row[x - channels] if x >= channels else 0
			up = previous[x]
			corner = previous[x - channels] if x >= channels else 0
			if kind == 1:
				row[x] = (row[x] + left) & 0xFF
			elif kind == 2:
				row[x] = (row[x] + up) & 0xFF
			elif kind == 3:
				row[x] = (row[x] + (left + up) // 2) & 0xFF
			elif kind == 4:
				estimate = left + up - corner
				distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
				row[x] = (row[x] + (left, up, corner)[distances.index(min(distances))]) & 0xFF
			elif kind != 0:
				return None
		rows.append(bytes(b for x in range(0, stride, channels) for b in row[x:x + 3]))
		previous = row
	return width, height, rows


def write_bmp(path, width, height, rows):
	"""a 24-bit Windows bitmap (the menus read one, custom_edition_maps.c)"""
	padding = (4 - width * 3 % 4) % 4
	body = bytearray()
	for row in reversed(rows):
		for x in range(0, width * 3, 3):
			body += bytes((row[x + 2], row[x + 1], row[x]))
		body += bytes(padding)
	header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
	info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0)
	with open(path, "wb") as file:
		file.write(header + info + body)


def wrapped(text, length):
	lines = []
	for word in text.split():
		if lines and len(lines[-1]) + 1 + len(word) <= length:
			lines[-1] += " " + word
		else:
			lines.append(word)
	return lines


def write_level_text(mod, stem, output, name):
	"""the menus' picture and description of a map (<name>.bmp, <name>.txt,
	custom_edition_maps.c) from the mod's campaign files: the map's title
	and description, and its thumbnail; nothing when the mod has none"""
	campaign = os.path.join(mod, "campaign")
	if not os.path.isdir(campaign):
		return False
	for entry in sorted(os.listdir(campaign)):
		if not entry.lower().endswith(".json"):
			continue
		try:
			with open(os.path.join(campaign, entry), encoding="utf-8-sig") as file:
				info = json.load(file)
		except (OSError, ValueError):
			continue
		if not isinstance(info, dict) or str(info.get("ScenarioFile", "")).lower() != stem.lower():
			continue
		title = str((info.get("Title") or {}).get("Neutral", "")).strip()
		description = str((info.get("Description") or {}).get("Neutral", "")).strip()
		lines = wrapped(title, DESCRIPTION_LINE_LENGTH) + [""] + wrapped(description, DESCRIPTION_LINE_LENGTH)
		with open(os.path.join(output, name + ".txt"), "w", encoding="latin-1", errors="replace") as file:
			file.write("\n".join(lines[:8]) + "\n")
		images = info.get("Images") or {}
		for key in ("Thumbnail", "Large"):
			picture = str(images.get(key, "")).replace("\\", "/")
			# (only a file of the mod's own folder)
			full = os.path.realpath(os.path.join(mod, picture))
			if picture and full.startswith(os.path.realpath(mod) + os.sep) and os.path.isfile(full):
				pixels = png_pixels(full)
				if pixels:
					write_bmp(os.path.join(output, name + ".bmp"), *pixels)
					break
		return True
	return False


def convert(arguments, path):
	invader = arguments.invader
	stem = os.path.splitext(os.path.basename(path))[0]
	name = (arguments.prefix + stem).lower()
	if not re.fullmatch(r"[a-z0-9_\-]+", name) or len(name) > MAXIMUM_MAP_NAME_LENGTH:
		raise ConversionError(f"'{name}': a map name of letters, digits, '_' and '-', at most {MAXIMUM_MAP_NAME_LENGTH} characters")
	if name in STOCK_SCENARIO_NAMES:
		raise ConversionError(f"'{name}' is a stock map's name: give a --prefix")
	output = os.path.join(arguments.output, name + ".map")
	work = os.path.join(arguments.work, name)
	if os.path.exists(work):
		shutil.rmtree(work)
	tags = os.path.join(work, "tags")
	data = os.path.join(work, "data")
	os.makedirs(tags)
	log = os.path.join(arguments.output, name + ".log")
	if os.path.exists(log):
		os.remove(log)

	engine = map_info(invader, path, "engine", log)
	if "Anniversary" not in engine and "MCC" not in engine:
		raise ConversionError(f"{path}: not a Halo: CE Anniversary map ({engine})")
	if map_info(invader, path, "external_tags_count", log) not in ("0", ""):
		# (a map that takes tags from MCC's bitmaps.map and sounds.map:
		# invader-extract would need them, -m)
		raise ConversionError(f"{path}: takes tags from MCC's resource maps; only maps that keep their own (classic maps) are converted")
	scenario = map_info(invader, path, "scenario_path", log)

	unmarked = unmarked_dxt_bitmaps(path)
	source_map = path
	if unmarked:
		source_map = os.path.join(work, os.path.basename(path))
		mark_dxt_bitmaps_compressed(path, unmarked, source_map)

	print(f"{stem}: extracting the tags", flush=True)
	extracted = run([tool(invader, "invader-extract"), "-G", "-n", "-t", tags, source_map], log)
	if source_map != path:
		os.remove(source_map)
	if "Failed to extract" in extracted:
		raise ConversionError(f"invader-extract could not extract every tag; see {log}")

	print(f"{stem}: the scripts", flush=True)
	scenario_tag = os.path.join(tags, scenario.replace("\\", "/") + ".scenario")
	texts = scenario_source(scenario_tag)
	if not texts:
		raise ConversionError(f"{path}: no script source in its scenario")
	scripts = os.path.join(data, os.path.dirname(scenario.replace("\\", "/")), "scripts")
	os.makedirs(scripts)
	changed = 0
	for index, text in enumerate(texts):
		text, count = make_mcc_calls_constant(text)
		changed += count
		# (Invader's script compiler reads UTF-8; the game's strings are
		# Latin-1, a print's text now and then)
		with open(os.path.join(scripts, f"{stem}_{index}.hsc"), "w", encoding="utf-8", newline="") as file:
			file.write(text)

	print(f"{stem}: the tags Invader refuses", flush=True)
	lens_flares = fix_lens_flares(invader, tags, log)
	for kind in ("invalid-indices", "invalid-enums"):
		run([tool(invader, "invader-bludgeon"), "-t", tags, "-T", kind, "-b", "*"], log)
	names_lost = fit_object_names(invader, tags, scenario, texts, log)

	print(f"{stem}: building {name}.map", flush=True)
	names_unshared = 0
	for attempt in range(64):
		trial = build(invader, tags, data, scenario, name, MAXIMUM_TAG_SPACE * 2, output, log, check=False)
		if "Failed to compile" not in trial:
			break
		unshared = unshare_object_names(invader, tags, scenario, trial, log)
		unshared += hide_missing_hud_sequences(invader, tags, trial, log)
		if not unshared:
			raise ConversionError(f"invader-build refused the tags; see {log}")
		names_unshared += unshared
	else:
		raise ConversionError(f"invader-build still refuses the tags; see {log}")
	used = tag_space_used(trial)
	tag_space = max(CUSTOM_EDITION_TAG_SPACE,
		int(math.ceil((used + 4096) / TAG_SPACE_GRANULARITY)) * TAG_SPACE_GRANULARITY)
	if tag_space > MAXIMUM_TAG_SPACE:
		os.remove(output)
		raise ConversionError(f"needs {used / 1048576:.2f} MiB of tag space, more than the {MAXIMUM_TAG_SPACE / 1048576:.1f} MiB this port gives a map")
	result = build(invader, tags, data, scenario, name, tag_space, output, log)
	summary = re.search(r"Tag space:.*", result)
	print(f"{stem}: {name}.map built: {changed} MCC script calls made constant, "
		f"{lens_flares} lens flare reflections fixed, {len(unmarked)} DXT bitmaps marked compressed, "
		f"{names_unshared} shared object names and missing HUD sequences removed, "
		f"{names_lost} object names past the game's {MAXIMUM_OBJECT_NAMES} unnamed, "
		f"{summary.group(0) if summary else ''}", flush=True)
	# (the mod's folder: the maps folder's parent, as Steam keeps it)
	write_level_text(os.path.dirname(os.path.dirname(os.path.abspath(path))), stem, arguments.output, name)
	if not arguments.keep_work:
		shutil.rmtree(work)
	return output


def main():
	parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
	parser.add_argument("--invader", required=True)
	parser.add_argument("--output", required=True)
	parser.add_argument("--prefix", default="")
	parser.add_argument("--work")
	parser.add_argument("--keep-work", action="store_true")
	parser.add_argument("maps", nargs="+")
	arguments = parser.parse_args()
	os.makedirs(arguments.output, exist_ok=True)
	if not arguments.work:
		arguments.work = tempfile.mkdtemp(prefix="mcc_map_convert.", dir=arguments.output)
	failed = 0
	for path in arguments.maps:
		try:
			convert(arguments, path)
		except (ConversionError, OSError) as exception:
			print(f"{path}: {exception}", file=sys.stderr, flush=True)
			failed += 1
	if not arguments.keep_work and os.path.isdir(arguments.work) and not os.listdir(arguments.work):
		os.rmdir(arguments.work)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
