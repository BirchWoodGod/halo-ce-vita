#!/usr/bin/env python3
"""The wire contract test: this tree's network wire against a pinned OpenCE.

Compares, with no game data and no network:
- the Bungie message table (source/networking/network_messages.c): every
  message type's name and number, and the bytes each encodes to from the same
  structure (port/vita/tests/wire_contract_messages.c, built against each
  tree's packet table and encoder);
- the distributed netcode's message kinds (port/linux/game/network_distributed.h);
- the game settings record (struct network_game) and the gametype's PC options
  (struct game_variant_options): layouts as clang lays them out, enum values,
  the bytes the record goes on the wire as and what decoding takes and refuses
  (port/vita/tests/wire_contract_record.c, port/linux/game/network_game_record.c);
- the advertisement (message_server_game_advertise): layout and reserved-byte
  constants;
- the session limits and capacities (halo_port_limits.h, halo_port_capacity.h);
- signalling, the server browser's listing and the join token, by what the
  Vita build's sources hold (presence checks: their behaviour is the netns
  tests');
and the legacy Vita wire (network version 18, betas 1-4) against the tree
that wire was released from (LEGACY_COMMIT): its record must stay byte for byte.

Reports each gap of docs/crossplay.md section 3.2 by its number (G1..G16) as
PASS, FAIL, MANUAL (no automatic check: the task that closes it brings its
own test) or N/A, beside the baseline checks (S*: what is already the same,
L*: the legacy wire), which must always pass. EXPECTED_OPEN lists the gaps
not yet closed: the run fails when a baseline check fails, when a gap not in
the list fails, or when a gap in it passes (it was closed: take it out of the
list). That makes it a gate as the crossplay tasks land.

Needs the Linux build configured in this tree (build.ninja: its compiler and
the game's flags; build/linux/halo_msvc_semantics.h), a clone of OpenCE that
has OPENCE_COMMIT (exported with git archive, never written to), and git.

  wire_contract.py [--opence DIR] [--work DIR] [--expected-open G2,G3,...]
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "port/vita/tests"

# the pinned upstream (docs/crossplay.md: OpenCE 8102e711, build-206, network
# version 29; ChupathingyCE 7d6d8f24, 0.7.5b, announces the same)
OPENCE_COMMIT = "8102e711"
# the tree the legacy Vita wire was released from: next-1.1 as 1.1.0-beta.4
# was built (network version 18)
LEGACY_COMMIT = "50ae7395"

# the gaps of docs/crossplay.md section 3.2 not yet closed (G6 has no
# automatic check yet: MANUAL, listed so the list says what is open). Closed
# by T4/T5 (feat/xp-wire-switch): G2, G3, G4, G5, G15, and G12/G13 as
# "silent toward PC machines" (T6-lite: this build's extensions never sent on
# the crossplay wire; the capability handshake proper is T6's). G14 by the PC
# co-op work (feat/xp-coop): judged by what a client indexes with a host's
# numbers, docs/crossplay.md 6.2
EXPECTED_OPEN = {"G6", "G9", "G10", "G11"}

GAP_TITLES = {
	"G1": "game settings record (13,120 bytes, game_variant_options)",
	"G2": "advertisement flags (0x02 in progress, not Vita)",
	"G3": "join token (no Vita XOR on the crossplay wire)",
	"G4": "signalling (hceu, MESSAGE_VERSION 4)",
	"G5": "server browser listing (format 1, hceu-lobby-1)",
	"G6": "map.version and CE map names",
	"G7": "v24 PC vehicle set",
	"G8": "v19 co-op flags (cooperative_flags)",
	"G9": "v10 pings (distributed 19)",
	"G10": "votekick (distributed 78/79)",
	"G11": "voice (distributed 80-82, none at 78/79)",
	"G12": "our Bungie messages 35+ (capability gated)",
	"G13": "our tunnel type 6 (capability gated)",
	"G14": "capacities (halo_port_capacity.h)",
	"G15": "network version (OpenCE's announced)",
	"G16": "WebRTC (not planned)",
}

# the probes' C library: the game's headers name printf and fopen the
# platform layer's (port/linux/include/stdio.h), here the C library's (weak:
# the network fuzz targets' stand-ins, net_fuzz_game.h, define some)
LIBC_REFERENCES = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define WEAK __attribute__((weak))
WEAK int halo_linux_vsnprintf(char *b, size_t n, const char *f, va_list a) { return vsnprintf(b, n, f, a); }
WEAK int halo_linux_snprintf(char *b, size_t n, const char *f, ...)
{ va_list a; int r; va_start(a, f); r = vsnprintf(b, n, f, a); va_end(a); return r; }
WEAK int halo_linux_vfprintf(FILE *s, const char *f, va_list a) { return vfprintf(s, f, a); }
WEAK int halo_linux_fprintf(FILE *s, const char *f, ...)
{ va_list a; int r; va_start(a, f); r = vfprintf(s, f, a); va_end(a); return r; }
WEAK int halo_linux_vprintf(const char *f, va_list a) { return vprintf(f, a); }
WEAK int halo_linux_printf(const char *f, ...)
{ va_list a; int r; va_start(a, f); r = vprintf(f, a); va_end(a); return r; }
WEAK FILE *halo_linux_fopen(const char *p, const char *m) { return fopen(p, m); }
WEAK void *csmemset(void *b, long c, unsigned long n) { return memset(b, (int)c, n); }
WEAK void *csmemcpy(void *d, const void *s, unsigned long n) { return memcpy(d, s, n); }
WEAK long csmemcmp(const void *a, const void *b, unsigned long n) { return memcmp(a, b, n); }
void *wire_contract_libc[] = { (void *)fscanf, (void *)fclose, (void *)malloc, (void *)free, (void *)memcmp,
	(void *)memcpy, (void *)memset, (void *)memmove, (void *)abort, (void *)getenv, (void *)fwrite,
	(void *)fflush, (void *)fork, (void *)pipe, (void *)read, (void *)write, (void *)close, (void *)waitpid,
	(void *)_exit };
"""

# ---------- trees


class Tree:
	def __init__(self, name, path):
		self.name = name
		self.path = Path(path)

	def text(self, relative):
		file = self.path / relative
		return file.read_text(errors="replace") if file.exists() else ""


def export_tree(repository, commit, destination):
	"""The commit's files, from the repository's objects (the clone is only read)."""
	destination.mkdir(parents=True, exist_ok=True)
	archive = subprocess.run(["git", "-C", str(repository), "archive", commit], check=True,
		capture_output=True).stdout
	subprocess.run(["tar", "-x", "-C", str(destination)], input=archive, check=True)


def generate_semantics(tree):
	output = tree.path / "build/linux/halo_msvc_semantics.h"
	if output.exists():
		return
	output.parent.mkdir(parents=True, exist_ok=True)
	subprocess.run([sys.executable, "-I", "tools/linux_msvc_semantics.py", "--output", str(output),
		"--all-inlines", "--tags", "source", "--inlines", "source", "--inlines", "port/include/xdk"],
		cwd=tree.path, check=True, stdout=subprocess.DEVNULL)


def game_flags():
	"""The game's compile flags (network_messages.c's in this tree's build.ninja),
	relative to a tree's root; DEBUG, its assertions fatal."""
	ninja = (ROOT / "build.ninja").read_text()
	compiler = re.search(r"^linux_cc = (.*)$", ninja, re.M)
	block = re.search(r"^build build/linux/obj/source/networking/network_messages\.o:.*?\n  cflags = (.*?)(?<!\$)\n",
		ninja, re.S | re.M)
	if not compiler or not block:
		sys.exit("FAIL no Linux build configured in this tree (build.ninja)")
	flags = re.sub(r"\$\n\s*", " ", block.group(1))
	words = [word for word in shlex.split(flags) if word not in ("-march=native",)]
	words = ["-O1" if word == "-O2" else word for word in words]
	return compiler.group(1).strip(), words


class Builder:
	def __init__(self, work):
		self.work = work
		self.cc, self.flags = game_flags()

	def libc_object(self):
		"""An object built as plain C that refers to the C library's functions the
		probes call: the game's semantics header makes the game's references to
		them weak, and with only weak references they would not be linked."""
		obj = self.work / "libc_references.o"
		if not obj.exists():
			source = self.work / "libc_references.c"
			source.write_text(LIBC_REFERENCES)
			subprocess.run([self.cc, "-m32", "-c", str(source), "-o", str(obj)], check=True)
		return str(obj)

	def compile_run(self, tree, name, source, extra_sources=(), defines=(), arguments=(), syntax_only=False,
		extra_flags=()):
		"""Builds source (and extra sources) with the game's flags in the tree,
		and runs it; its standard output (or, syntax_only, clang's)."""
		output = self.work / f"{tree.name}.{name}"
		command = [self.cc, *self.flags, *extra_flags, *[f"-D{define}" for define in defines]]
		if syntax_only:
			result = subprocess.run(command + ["-fsyntax-only", "-Xclang", "-fdump-record-layouts", str(source)],
				cwd=tree.path, capture_output=True, text=True)
			if result.returncode != 0:
				raise RuntimeError(f"{tree.name}: {name} did not compile:\n{result.stderr[-3000:]}")
			return result.stdout
		objects = []
		for index, path in enumerate([source, *extra_sources]):
			obj = self.work / f"{tree.name}.{name}.{index}.o"
			result = subprocess.run(command + ["-c", str(path), "-o", str(obj)], cwd=tree.path,
				capture_output=True, text=True)
			if result.returncode != 0:
				raise RuntimeError(f"{tree.name}: {name} ({path}) did not compile:\n{result.stderr[-3000:]}")
			objects.append(str(obj))
		result = subprocess.run([self.cc, "-m32", "-no-pie", "-Wl,--gc-sections", "-Wl,--warn-unresolved-symbols",
			*objects, self.libc_object(), "-lm", "-o", str(output)], capture_output=True, text=True)
		if result.returncode != 0:
			raise RuntimeError(f"{tree.name}: {name} did not link:\n{result.stderr[-3000:]}")
		result = subprocess.run([str(output), *arguments], capture_output=True, text=True)
		if result.returncode not in (0, 1):
			raise RuntimeError(f"{tree.name}: {name} exited {result.returncode}:\n{result.stdout[-2000:]}"
				f"{result.stderr[-2000:]}")
		return result.stdout


# ---------- what the trees declare


def strip_comments(text):
	text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
	return re.sub(r"//[^\n]*", " ", text)


def enumerators(text, pattern):
	"""The enumerators matching pattern in text's enums."""
	names = []
	for body in re.findall(r"\benum\b[^{};]*\{(.*?)\}", strip_comments(text), re.S):
		for entry in body.split(","):
			match = re.match(r"\s*([A-Za-z_]\w*)", entry)
			if match and re.fullmatch(pattern, match.group(1)) and match.group(1) not in names:
				names.append(match.group(1))
	return names


def macros(text, pattern):
	names = [name for name in re.findall(r"^\s*#\s*define\s+(\w+)(?!\()\s", text, re.M) if re.fullmatch(pattern, name)]
	return list(dict.fromkeys(names))


ENUM_SOURCES = {
	"messages": ("source/networking/network_messages.h", r"_message_\w+"),
	"distributed": ("port/linux/game/network_distributed.h", r"(_distributed_message_\w+|NUMBER_OF_DISTRIBUTED_MESSAGES)"),
	"options": ("source/game/game_engine.h",
		r"(_friendly_fire_\w+|_radar_players_\w+|_loadout_\w+|_variant_vehicle_\w+|VARIANT_VEHICLE_SET_\w+|"
		r"MAXIMUM_VARIANT_VEHICLE_COUNT|NUMBER_OF_(FRIENDLY_FIRE_MODES|RADAR_PLAYERS|LOADOUTS|LOADOUT_WEAPONS|"
		r"VARIANT_VEHICLES))"),
	"cooperative": ("source/networking/network_game_manager.h", r"_network_game_cooperative_\w+"),
}
MACRO_SOURCES = [("port/linux/include/halo_port_limits.h", r"HALO_PORT_\w+"),
	("port/linux/include/halo_port_capacity.h", r"HALO_PORT_\w+"),
	# (the wire switch's, docs/crossplay.md T4: the crossplay wire's version)
	("port/linux/include/network_wire.h", r"HALO_PORT_\w+")]


def declared_values(builder, tree):
	"""{group: {name: value}} for the enums and macros above, as the tree's
	compiler sees them."""
	lines = ['#include "cseries.h"', '#include "game/game_engine.h"', '#include "networking/network_game_manager.h"',
		'#include "networking/network_messages.h"']
	if (tree.path / "port/linux/game/network_distributed.h").exists():
		lines.append('#include "network_distributed.h"')
	if (tree.path / "port/linux/include/network_wire.h").exists():
		lines.append('#include "network_wire.h"')
	lines += ["#include <stdio.h>", "int main(void)", "{"]
	for group, (relative, pattern) in ENUM_SOURCES.items():
		for name in enumerators(tree.text(relative), pattern):
			lines.append(f'\tprintf("{group} {name} %ld\\n", (long)({name}));')
	for relative, pattern in MACRO_SOURCES:
		for name in macros(tree.text(relative), pattern):
			lines.append(f"#if defined({name})")
			lines.append(f'\tprintf("macro {name} %ld\\n", (long)({name}));')
			lines.append("#endif")
	lines += ["\treturn 0;", "}"]
	source = builder.work / f"{tree.name}.declared.c"
	source.write_text("\n".join(lines) + "\n")
	values = {}
	for line in builder.compile_run(tree, "declared", source).splitlines():
		group, name, value = line.split()
		values.setdefault(group, {})[name] = int(value)
	return values


# ---------- record layouts


def record_layouts(text):
	"""{record name: (size, [(offset, type, field)])} of clang's dump: the
	top-level fields of each record."""
	layouts = {}
	for block in text.split("*** Dumping AST Record Layout")[1:]:
		lines = block.strip("\n").splitlines()
		head = re.match(r"\s*0 \| (?:struct|union) (\w+)", lines[0]) if lines else None
		if not head:
			continue
		fields = []
		size = None
		for line in lines[1:]:
			match = re.match(r"\s*(\d+)(?::\d+-\d+)? \|   (\S.*?) (\w+)$", line)
			if match:
				fields.append((int(match.group(1)), match.group(2), match.group(3)))
				continue
			match = re.search(r"\[sizeof=(\d+)", line)
			if match:
				size = int(match.group(1))
				break
		layouts.setdefault(head.group(1), (size, fields))
	return layouts


def layouts_of(builder, tree):
	source = builder.work / f"{tree.name}.layouts.c"
	has_options = "struct game_variant_options\n{" in tree.text("source/game/game_engine.h")
	source.write_text("\n".join([
		'#include "cseries.h"', '#include "game/game_engine.h"', '#include "networking/network_game_manager.h"',
		"int wire_contract_sizes[] = { sizeof(struct network_game), sizeof(struct game_variant)",
		", sizeof(struct game_variant_options)" if has_options else "", " };", ""]))
	layouts = record_layouts(builder.compile_run(tree, "layouts", source, syntax_only=True))
	# (the advertisement: declared in the client's message handler)
	handler = tree.path / "source/networking/network_client_message_handler.c"
	layouts.update({name: value for name, value in record_layouts(
		builder.compile_run(tree, "handler_layouts", handler, syntax_only=True)).items()
		if name.startswith("message_server_game_advertise")})
	return layouts


def describe_layout(layout):
	size, fields = layout
	return f"{size} bytes: " + ", ".join(f"{field}@0x{offset:X}" for offset, _, field in fields)


# ---------- the record probe


OPTION_VALUES = {
	"time_limit": "15", "friendly_fire": "2", "friendly_fire_penalty": "10", "vehicle_respawn_time": "60",
	"auto_team_balance": "1", "radar_players": "1", "loadout": "1", "primary_weapon": "3", "secondary_weapon": "9",
	"no_map_weapons": "1",
}


def fill_header(builder, tree, layouts):
	"""wire_contract_fill() for the tree's record (wire_contract_record.c)."""
	size, fields = layouts["network_game"]
	lines = ["static void wire_contract_fill(struct network_game *g)", "{", "\tmemset(g, 0, sizeof(*g));"]
	for _, _, field in fields:
		if field == "cooperative_flags":
			lines.append("\tg->cooperative_flags = 1;")
		elif field.startswith("__padding"):
			lines.append(f"\tmemset(&g->{field}, 0, sizeof(g->{field}));")
		elif field == "variant_options":
			for _, _, option in layouts["game_variant_options"][1]:
				if option == "vehicle_set":
					lines.append(f"\tg->variant_options.vehicle_set[0] = VARIANT_VEHICLE_SET_PC;")
					lines.append(f"\tg->variant_options.vehicle_set[1] = VARIANT_VEHICLE_SET_CUSTOM;")
				elif option == "vehicle_counts":
					lines.append("\t{ int side, type; for (side = 0; side < 2; side++) for (type = 0; type < "
						"NUMBER_OF_VARIANT_VEHICLES; type++) g->variant_options.vehicle_counts[side][type] = "
						"(byte)((side * 3 + type) % (MAXIMUM_VARIANT_VEHICLE_COUNT + 1)); }")
				else:
					lines.append(f"\tg->variant_options.{option} = {OPTION_VALUES.get(option, '0')};")
		else:
			seed = zlib.crc32(field.encode()) & 0xFF
			lines.append(f"\twire_contract_pattern(&g->{field}, sizeof(g->{field}), {seed}u);")
	lines += ["}", ""]
	header = builder.work / f"{tree.name}.fill.h"
	header.write_text("\n".join(lines))
	return header


def record_bytes(builder, tree, layouts):
	header = fill_header(builder, tree, layouts)
	return builder.compile_run(tree, "record", TESTS / "wire_contract_record.c",
		defines=[f'WIRE_CONTRACT_FILL_HEADER="{header}"']).strip()


def record_checks(builder, ours, layouts, opence_hex, legacy_hex):
	header = fill_header(builder, ours, layouts)
	(builder.work / "opence.record.hex").write_text(opence_hex)
	(builder.work / "legacy.record.hex").write_text(legacy_hex)
	output = builder.compile_run(ours, "record_checks", TESTS / "wire_contract_record.c",
		extra_sources=[ROOT / "port/linux/game/network_game_record.c"],
		defines=[f'WIRE_CONTRACT_FILL_HEADER="{header}"', "WIRE_CONTRACT_OURS"],
		extra_flags=[f"-I{TESTS}"],
		arguments=[str(builder.work / "opence.record.hex"), str(builder.work / "legacy.record.hex")])
	checks = {}
	notes = []
	for line in output.splitlines():
		match = re.match(r"CHECK (\S+) (PASS|FAIL) ?(.*)", line)
		if match:
			checks[match.group(1)] = (match.group(2) == "PASS", match.group(3))
		elif line.startswith("record sizes"):
			notes.append(line)
	return checks, notes


# ---------- the message probe


def message_lines(builder, tree):
	names = enumerators(tree.text("source/networking/network_messages.h"), r"_message_\w+")
	sizes = builder.work / f"{tree.name}.sizes.h"
	sizes.write_text("static long const wire_contract_sizes[] =\n{\n" +
		"".join(f"\tsizeof({name[1:]}),\n" for name in names) + "};\n")
	sources = [ROOT / "source/memory/data_packet_groups.c", ROOT / "source/memory/data_packets.c",
		ROOT / "source/memory/data_encoding.c", ROOT / "source/memory/byte_swapping.c",
		ROOT / "source/bungie_net/common/message_header.c"]
	# (each tree's own encoder)
	sources = [tree.path / path.relative_to(ROOT) for path in sources]
	output = builder.compile_run(tree, "messages", TESTS / "wire_contract_messages.c", extra_sources=sources,
		defines=[f'WIRE_CONTRACT_SIZES_HEADER="{sizes}"',
			f'WIRE_CONTRACT_MESSAGES_SOURCE="{tree.path / "source/networking/network_messages.c"}"'],
		extra_flags=[f"-I{TESTS}"])
	encoded = {}
	for line in output.splitlines():
		words = line.split()
		if len(words) == 4:
			encoded[int(words[0])] = (int(words[1]), words[2], words[3])
	return names, encoded


# ---------- the checks


class Report:
	def __init__(self):
		self.gaps = {}
		self.baseline = []

	def gap(self, number, status, detail):
		self.gaps[number] = (status, detail)

	def base(self, name, passed, detail):
		self.baseline.append((name, passed, detail))


def compare_values(ours, theirs, names):
	"""(missing, different) of names between two {name: value}."""
	missing = [name for name in names if name in theirs and name not in ours]
	different = [f"{name} {ours[name]} vs {theirs[name]}" for name in names
		if name in theirs and name in ours and ours[name] != theirs[name]]
	return missing, different


def source_function(text, needle):
	"""The function (from the line before its opening brace to its end) whose
	body holds needle, or ''."""
	index = text.find(needle)
	if index < 0:
		return ""
	start = text.rfind("\n{", 0, index)
	start = text.rfind("\n", 0, start - 1) if start > 0 else 0
	end = text.find("\n}", index)
	return text[start:end if end > 0 else len(text)]


def preprocessed(builder, tree, relative, vita=True):
	"""A platform layer source as the Vita build's compiler sees it."""
	command = [builder.cc, "-m32", "-E", "-w", "-DHALO_LINUX_PLATFORM_LAYER", "-D_GNU_SOURCE",
		"-include", "port/linux/include/halo_linux_prefix.h", "-Iport/linux/src", "-Iport/linux/include",
		"-Iport/linux/game", "-Iport/third_party/kcp", "-Iport/third_party/monocypher", "-Isource",
		"-Isource/cseries", "-idirafter", "port/include/xdk", str(relative)]
	if vita:
		command[3:3] = ["-DHALO_VITA", "-DHALO_NET_AS_VITA=1"]
	result = subprocess.run(command, cwd=tree.path, capture_output=True, text=True)
	return result.stdout if result.returncode == 0 else ""


def run(arguments):
	work = Path(arguments.work) if arguments.work else Path(tempfile.mkdtemp(prefix="wire_contract."))
	work.mkdir(parents=True, exist_ok=True)
	builder = Builder(work)
	ours = Tree("ours", ROOT)
	opence = Tree("opence", work / "opence")
	legacy = Tree("legacy", work / "legacy")
	if not opence.path.exists():
		export_tree(arguments.opence, OPENCE_COMMIT, opence.path)
	if not legacy.path.exists():
		export_tree(ROOT, LEGACY_COMMIT, legacy.path)
	for tree in (opence, legacy):
		generate_semantics(tree)
	if not (ROOT / "build/linux/halo_msvc_semantics.h").exists():
		subprocess.run(["ninja", "build/linux/halo_msvc_semantics.h"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)

	report = Report()
	values = {tree.name: declared_values(builder, tree) for tree in (ours, opence, legacy)}
	layouts = {tree.name: layouts_of(builder, tree) for tree in (ours, opence, legacy)}
	ov, tv = values["ours"], values["opence"]
	ol, tl, ll = layouts["ours"], layouts["opence"], layouts["legacy"]

	# --- S1: the Bungie messages both have: names, numbers, bytes
	our_names, our_encoded = message_lines(builder, ours)
	their_names, their_encoded = message_lines(builder, opence)
	problems = []
	for number, name in enumerate(their_names):
		if number >= len(our_names) or our_names[number] != name:
			problems.append(f"type {number} is {name} upstream, {our_names[number] if number < len(our_names) else 'none'} here")
		elif our_encoded.get(number) != their_encoded.get(number):
			problems.append(f"{name} encodes differently ({our_encoded.get(number, ('?',))[0]} vs "
				f"{their_encoded.get(number, ('?',))[0]} bytes of structure)")
	report.base("S1 Bungie messages 0-%d: numbers and encoded bytes" % (len(their_names) - 1), not problems,
		"; ".join(problems) or f"{len(their_names)} types identical")
	extra = our_names[len(their_names):]
	# (this build's own messages are sent only through
	# network_game_client_send_to_server, which the wire switch silences on
	# the crossplay wire; the host side never runs there: a Vita hosts no PC)
	sender = source_function(ours.text("source/networking/network_client_manager.c"),
		"boolean network_game_client_send_to_server(")
	silenced = bool(re.search(r"network_wire_crossplay\(\)", sender))
	report.gap("G12", "PASS" if not extra or silenced else "MANUAL",
		("ours only: " + ", ".join(f"{len(their_names) + i} {n[9:]}" for i, n in enumerate(extra)) +
			(", never sent on the crossplay wire (network_game_client_send_to_server)" if silenced else
			" (gated by the capability handshake: T6)")) if extra else "no messages past upstream's")

	# --- distributed kinds
	ours_kinds, their_kinds = dict(ov.get("distributed", {})), dict(tv.get("distributed", {}))
	our_kind_count = ours_kinds.pop("NUMBER_OF_DISTRIBUTED_MESSAGES", None)
	their_kinds.pop("NUMBER_OF_DISTRIBUTED_MESSAGES", None)
	shared = [name for name in their_kinds if name in ours_kinds]
	different = [f"{name} {ours_kinds[name]} vs {their_kinds[name]}" for name in shared if ours_kinds[name] != their_kinds[name]]
	report.base("S2 distributed kinds both have: numbers", not different,
		"; ".join(different) or f"{len(shared)} kinds identical")

	def kinds_gap(number, names, values_wanted):
		missing = [n for n in names if n not in ours_kinds]
		wrong = [f"{n} {ours_kinds[n]} (upstream {their_kinds.get(n)})" for n in names
			if n in ours_kinds and ours_kinds[n] != their_kinds.get(n)]
		taken = [f"{v} is our {n}" for n, v in ours_kinds.items() if v in values_wanted and n not in names]
		problems = (["missing " + ", ".join(missing)] if missing else []) + wrong + taken
		report.gap(number, "FAIL" if problems else "PASS", "; ".join(problems) or "as upstream")

	kinds_gap("G9", ["_distributed_message_pings"], {their_kinds.get("_distributed_message_pings")})
	kinds_gap("G10", ["_distributed_message_votekick", "_distributed_message_votekick_status"],
		{their_kinds.get("_distributed_message_votekick"), their_kinds.get("_distributed_message_votekick_status")})
	voice = ["_distributed_message_voice_up", "_distributed_message_voice_down", "_distributed_message_voice_config"]
	kinds_gap("G11", voice, {their_kinds.get(n) for n in voice} |
		{their_kinds.get("_distributed_message_votekick"), their_kinds.get("_distributed_message_votekick_status")})

	# --- S6: upstream's map sharing (its network version 27: kinds 83-86),
	# which this build does not play: none of ours at those numbers, and all
	# past our kinds, so the bounds check drops them as unknown, in the lobby
	# (no game in progress) and in a game alike
	share = {n: v for n, v in their_kinds.items() if n.startswith("_distributed_message_map_share")}
	handler = source_function(ours.text("port/linux/game/network_distributed.c"),
		"void network_distributed_handle_message(")
	problems = [f"{v} is our {n}" for n, v in ours_kinds.items() if v in share.values()]
	if our_kind_count is None or any(v < our_kind_count for v in share.values()):
		problems.append(f"upstream's {sorted(share.values())} not past our {our_kind_count} kinds")
	if not re.search(r"header\.type\s*>=\s*NUMBER_OF_DISTRIBUTED_MESSAGES", handler) or \
		not re.search(r"!game_in_progress\(\)", handler):
		problems.append("network_distributed_handle_message has no bounds or lobby check")
	report.base("S6 upstream's map sharing kinds dropped here", bool(share) and not problems,
		"; ".join(problems) or (f"{', '.join(f'{v} {n[21:]}' for n, v in sorted(share.items(), key=lambda i: i[1]))}: "
			f"past our {our_kind_count} kinds, dropped as unknown" if share else "upstream has none (not v27+?)"))
	# --- S7: this build's map sharing messages (Bungie 36/37) from a host:
	# ignored on the crossplay wire (a PC host never sends them; one that did
	# would be a stranger feeding the downloader)
	handler_text = ours.text("source/networking/network_client_message_handler.c")
	answer = handler_text[handler_text.find("case _message_server_map_download_answer:"):]
	answer = answer[:answer.find("default:")] if "case _message_server_map_download_answer:" in handler_text else ""
	report.base("S7 our map sharing messages ignored on the crossplay wire", "network_wire_crossplay()" in answer,
		"a host's map download answer/data dropped with Crossplay on" if "network_wire_crossplay()" in answer else
		"network_client_message_handler.c takes them whatever the wire")

	# --- G1, G7, G8: the record
	problems = []
	for record in ("network_game", "game_variant_options"):
		if record not in ol:
			problems.append(f"no struct {record}")
		elif ol[record] != tl.get(record):
			problems.append(f"struct {record} laid out {describe_layout(ol[record])}; upstream "
				f"{describe_layout(tl[record])}")
	option_names = list(tv.get("options", {}))
	missing, different = compare_values(ov.get("options", {}), tv.get("options", {}), option_names)
	if missing:
		problems.append("option enums missing: " + ", ".join(missing))
	problems += ["option enum " + d for d in different]
	checks, notes = {}, []
	if "network_game" in tl and "game_variant_options" in tl:
		opence_hex = record_bytes(builder, opence, tl)
		legacy_hex = record_bytes(builder, legacy, ll)
		if "game_variant_options" in ol:
			checks, notes = record_checks(builder, ours, ol, opence_hex, legacy_hex)
	record_problems = [f"{name}: {detail}" for name, (passed, detail) in checks.items()
		if not passed and name != "legacy_encode" and name != "legacy_decode"]
	if not checks:
		record_problems.append("the record probe did not run (no game_variant_options here)")
	report.gap("G1", "FAIL" if problems or record_problems else "PASS",
		"; ".join(problems + record_problems) or
		f"layouts as upstream, {sum(1 for name, (passed, _) in checks.items() if passed and name.startswith('refuse'))}"
		f" refusals, " + "; ".join(notes))
	pc_set = "VARIANT_VEHICLE_SET_PC"
	uses_pc = pc_set in ours.text("source/game/game_engine.c")
	if pc_set not in ov.get("options", {}) or ov["options"][pc_set] != tv["options"].get(pc_set) or not uses_pc:
		report.gap("G7", "FAIL", f"{pc_set} {'missing' if pc_set not in ov.get('options', {}) else ov['options'][pc_set]}"
			f" (upstream {tv['options'].get(pc_set)}){'' if uses_pc else ', not placed by game_engine.c'}")
	else:
		report.gap("G7", "PASS", f"{pc_set} = 0x{ov['options'][pc_set]:X}, placed by game_engine.c")
	ng = dict((field, offset) for offset, _, field in ol.get("network_game", (0, []))[1])
	tg = dict((field, offset) for offset, _, field in tl.get("network_game", (0, []))[1])
	coop = "_network_game_cooperative_no_player_collisions_bit"
	if ng.get("cooperative_flags") == tg.get("cooperative_flags") and ov.get("cooperative", {}).get(coop) == \
		tv.get("cooperative", {}).get(coop) and ng.get("cooperative_flags") is not None:
		report.gap("G8", "PASS", f"cooperative_flags at 0x{ng['cooperative_flags']:X}, bit {ov['cooperative'][coop]} "
			"(carried and checked; PLAYER COLLISIONS off played: network_coop_player_collisions)")
	else:
		report.gap("G8", "FAIL", f"cooperative_flags at {ng.get('cooperative_flags')} (upstream {tg.get('cooperative_flags')})")

	# --- L1, L2: the legacy wire
	legacy_problems = []
	if ll.get("network_game") is None:
		legacy_problems.append("no record in the legacy tree")
	elif "legacy_encode" in checks:
		for name in ("legacy_encode", "legacy_decode"):
			if not checks[name][0]:
				legacy_problems.append(f"{name}: {checks[name][1]}")
	else:
		legacy_problems.append("the record probe did not run")
	report.base(f"L1 legacy record ({LEGACY_COMMIT}, betas 1-4): bytes on the wire", not legacy_problems,
		"; ".join(legacy_problems) or f"{ll['network_game'][0]} bytes, byte for byte, both ways")
	missing, different = compare_values(ov.get("messages", {}), values["legacy"].get("messages", {}),
		list(values["legacy"].get("messages", {})))
	legacy_messages = [f"type {n}" for n in missing] + different
	report.base("L2 legacy Bungie message numbers", not legacy_messages, "; ".join(legacy_messages) or "unchanged")

	# --- S3: the advertisement
	adv = "message_server_game_advertise"
	problems = []
	if ol.get(adv) != tl.get(adv):
		problems.append(f"laid out {describe_layout(ol[adv]) if adv in ol else 'nowhere'}; upstream "
			f"{describe_layout(tl[adv]) if adv in tl else 'nowhere'}")
	for name in ("HALO_PORT_ADVERTISED_VERSION_OFFSET", "HALO_PORT_ADVERTISED_FLAGS_OFFSET",
		"HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG"):
		if ov["macro"].get(name) != tv["macro"].get(name):
			problems.append(f"{name} {ov['macro'].get(name)} vs {tv['macro'].get(name)}")
	report.base("S3 advertisement layout and reserved bytes", not problems, "; ".join(problems) or "identical")
	flag = "HALO_PORT_ADVERTISED_IN_PROGRESS_FLAG"
	if ov["macro"].get(flag) == tv["macro"].get(flag):
		report.gap("G2", "PASS", f"{flag} = 0x{ov['macro'][flag]:02X}")
	else:
		meanings = ", ".join(f"{n[len('HALO_PORT_ADVERTISED_'):]} 0x{v:02X}" for n, v in ov["macro"].items()
			if n.startswith("HALO_PORT_ADVERTISED_") and n.endswith("_FLAG"))
		report.gap("G2", "FAIL", f"no {flag} (0x{tv['macro'].get(flag, 0):02X} upstream); ours: {meanings}")

	# --- S4: the session's shape
	shape = ["HALO_PORT_MAXIMUM_NETWORK_PLAYERS", "HALO_PORT_MAXIMUM_NETWORK_MACHINES",
		"HALO_PORT_NETWORK_MACHINE_SIZE", "HALO_PORT_NETWORK_PLAYER_SIZE", "HALO_PORT_NETWORK_GAME_MESSAGE_VERSION",
		"HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE", "HALO_PORT_NETWORK_PACKET_SIZE",
		"HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE", "HALO_PORT_NETWORK_GAME_SIZE",
		"HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET", "HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET"]
	missing, different = compare_values(ov["macro"], tv["macro"], shape)
	report.base("S4 session limits and record constants", not missing and not different,
		"; ".join(["missing " + ", ".join(missing)] if missing else [] + different) or
		f"{len(shape)} constants identical")

	# --- G14: capacities, judged by what a client indexes with a host's
	# numbers (docs/crossplay.md 6.2). A client runs no AI and no scripts:
	# the actor, prop and swarm pools, script object lists and the game state
	# they make are the host's only, and caches, effects and widgets are each
	# machine's own. Objects (named by datum index) must match, the object
	# memory must hold what the host's does, and the client's table of the
	# AI units it drives (network_actor_table.h) the host's actors and the
	# units its cutscenes drive (upstream: network_actors.c's actors + 32)
	capacity = macros(opence.text("port/linux/include/halo_port_capacity.h"), r"HALO_PORT_\w+")
	host_only = {"HALO_PORT_MAXIMUM_ACTORS", "HALO_PORT_MAXIMUM_PROPS", "HALO_PORT_MAXIMUM_SWARMS",
		"HALO_PORT_MAXIMUM_SWARM_COMPONENTS", "HALO_PORT_MAXIMUM_OBJECT_LISTS_PER_MAP",
		"HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP", "HALO_PORT_GAME_STATE_CPU_SIZE", "HALO_PORT_GAME_STATE_SIZE"}
	problems, notes = [], []
	for name, rule in (("HALO_PORT_MAXIMUM_OBJECTS_PER_MAP", "=="), ("HALO_PORT_OBJECT_MEMORY_POOL_SIZE", ">=")):
		theirs, ours_value = tv["macro"].get(name), ov["macro"].get(name)
		if theirs is None:
			continue
		if ours_value is None or (ours_value != theirs if rule == "==" else ours_value < theirs):
			problems.append(f"{name} {ours_value} vs {theirs} (a client indexes it with the host's numbers)")
	table = ROOT / "port/linux/game/network_actor_table.h"
	table_capacity = None
	if table.exists():
		match = re.search(r"#define NETWORK_ACTOR_TABLE_CAPACITY \(?\s*(\d+)\s*(?:\+\s*(\d+))?\s*\)?", table.read_text())
		if match:
			table_capacity = int(match.group(1)) + int(match.group(2) or 0)
	upstream_actors = tv["macro"].get("HALO_PORT_MAXIMUM_ACTORS")
	if upstream_actors is not None:
		if table_capacity is None or table_capacity < upstream_actors + 32:
			problems.append(f"the client's AI unit table {table_capacity} vs the host's {upstream_actors} + 32")
		else:
			notes.append(f"client AI table {table_capacity} (host {upstream_actors} + 32)")
	missing, different = compare_values(ov["macro"], tv["macro"], capacity)
	host_different = [d for d in different if d.split()[0] in host_only]
	if host_different:
		notes.append("host-only " + "; ".join(host_different))
	local = [d.split()[0] for d in different if d.split()[0] not in host_only and
		d.split()[0] not in ("HALO_PORT_MAXIMUM_OBJECTS_PER_MAP", "HALO_PORT_OBJECT_MEMORY_POOL_SIZE")]
	local += [name for name in missing if name not in host_only]
	if local:
		notes.append("each machine's own " + ", ".join(local))
	report.gap("G14", "FAIL" if problems else "PASS", "; ".join(problems + notes) or "identical")

	# --- G15: the version
	announced = tv["macro"].get("HALO_PORT_NETWORK_VERSION")
	ours_versions = {n: v for n, v in ov["macro"].items() if re.fullmatch(r"HALO_PORT_\w*NETWORK_VERSION\w*", n)}
	if announced in ours_versions.values():
		report.gap("G15", "PASS", f"announces {announced}")
	else:
		report.gap("G15", "FAIL", f"upstream {announced}; ours " +
			", ".join(f"{n} {v}" for n, v in ours_versions.items()))

	# --- G3: the join token
	server = ours.text("source/networking/network_server_manager.c")
	token = source_function(server, "Vita-only game!")
	if not token:
		report.gap("G3", "PASS", "no Vita XOR")
	elif re.search(r"crossplay|network_game_record_format_current|network_wire", token, re.I):
		report.gap("G3", "PASS", "the Vita XOR is chosen by the wire")
	else:
		report.gap("G3", "FAIL", "the Vita XOR applies whatever the wire (network_server_manager.c)")

	# --- G4, G5: signalling and the listing, in the Vita build
	signal = preprocessed(builder, ours, "port/linux/src/p2p_signal.c")
	their_signal = preprocessed(builder, opence, "port/linux/src/p2p_signal.c", vita=False)
	their_version = re.search(r"MESSAGE_VERSION\s*=\s*(\d+)", their_signal)
	versions = [int(v) for v in re.findall(r"MESSAGE_VERSION\w*\s*=\s*(\d+)", signal)]
	topic = '"hceu" "/3/' in signal or '"hceu/3/' in signal
	if not signal:
		report.gap("G4", "FAIL", "p2p_signal.c did not preprocess")
	elif topic and their_version and int(their_version.group(1)) in versions:
		report.gap("G4", "PASS", f"hceu topics and MESSAGE_VERSION {their_version.group(1)} in the Vita build")
	else:
		report.gap("G4", "FAIL", f"Vita build: hceu topics {'yes' if topic else 'no'}, MESSAGE_VERSION "
			f"{versions} (upstream {their_version.group(1) if their_version else '?'})")
	lobby = preprocessed(builder, ours, "port/linux/src/p2p_lobby.c")
	formats = [int(v) for v in re.findall(r"LISTING_FORMAT\w*\s*=\s*(\d+)", lobby)]
	label = "hceu-lobby-1" in lobby
	if label and 1 in formats:
		report.gap("G5", "PASS", "format 1 and hceu-lobby-1 in the Vita build")
	else:
		report.gap("G5", "FAIL", f"Vita build: listing formats {formats}, hceu-lobby-1 {'yes' if label else 'no'}")

	# --- the rest
	report.gap("G6", "MANUAL", "map identity on the wire (since v29 custom_maps\\<name> and the hash's first 32 bits): T3's own tests "
		"(run_crossplay_maps_test.sh, netns crossmaps)")
	p2p = ours.text("port/linux/src/p2p.c")
	ping_send = source_function(p2p, "inner[0] = _packet_ping_table;")
	ping_take = source_function(p2p, "ping_table_received(peer, inner + 1")
	if "network_wire_crossplay()" in ping_send and "network_wire_crossplay()" in ping_take:
		report.gap("G13", "PASS", "tunnel type 6 (ping table) neither sent nor read on the crossplay wire")
	else:
		report.gap("G13", "MANUAL", "tunnel type 6 (ping table): gated by T6, its own test")
	report.gap("G16", "N/A", "WebRTC is not planned on the Vita (announce kind none: T5)")
	return report, work


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--opence", default=os.environ.get("OPENCE_DIR",
		str(Path.home() / "halo-test-out/xpv29.M6zO/OpenCE")), help="a clone of OpenCE with OPENCE_COMMIT")
	parser.add_argument("--work", help="where to build (default: a new folder under TMPDIR, deleted on a pass)")
	parser.add_argument("--expected-open", help="the gaps not yet closed, comma-separated (default: EXPECTED_OPEN)")
	parser.add_argument("--keep", action="store_true", help="keep the work folder")
	arguments = parser.parse_args()
	expected = set(arguments.expected_open.split(",")) if arguments.expected_open is not None else EXPECTED_OPEN
	expected.discard("")
	made_work = arguments.work is None
	try:
		report, work = run(arguments)
	except (RuntimeError, subprocess.CalledProcessError) as failure:
		print(f"FAIL the wire contract test could not run: {failure}")
		return 1

	ok = True
	print(f"wire contract: this tree against OpenCE {OPENCE_COMMIT} and the legacy wire's {LEGACY_COMMIT}")
	print("\nbaseline (must pass)")
	for name, passed, detail in report.baseline:
		print(f"  {'PASS' if passed else 'FAIL'}  {name}: {detail}")
		ok = ok and passed
	print("\ngaps (docs/crossplay.md 3.2)")
	print(f"  {'gap':<4} {'status':<6} {'expected':<8} {'verdict':<11} detail")
	for number in sorted(GAP_TITLES, key=lambda gap: int(gap[1:])):
		status, detail = report.gaps.get(number, ("FAIL", "not checked"))
		want_open = number in expected
		if status in ("N/A",):
			verdict = "ok"
		elif status == "MANUAL":
			verdict = "ok (manual)" if want_open else "UNEXPECTED"
		elif status == "PASS":
			verdict = "CLOSED NOW" if want_open else "ok"
		else:
			verdict = "ok (open)" if want_open else "REGRESSED"
		if verdict in ("UNEXPECTED", "CLOSED NOW", "REGRESSED"):
			ok = False
		print(f"  {number:<4} {status:<6} {'open' if want_open else 'closed':<8} {verdict:<11} "
			f"{GAP_TITLES[number]}: {detail}")
	closed = sorted((n for n, (s, _) in report.gaps.items() if s == "PASS"), key=lambda gap: int(gap[1:]))
	print(f"\n{len(closed)} of {len(GAP_TITLES)} gaps closed: {', '.join(closed)}")
	print("PASS wire contract (as expected)" if ok else
		"FAIL wire contract: a baseline check failed, a closed gap regressed, or an expected-open gap passed "
		"(take it out of EXPECTED_OPEN in tools/wire_contract.py)")
	if ok and made_work and not arguments.keep:
		shutil.rmtree(work, ignore_errors=True)
	elif not ok or arguments.keep:
		print(f"work in {work}")
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
