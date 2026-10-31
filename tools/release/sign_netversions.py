#!/usr/bin/env python3
"""Make or change the signed crossplay version table (which PC network
versions the game joins and by which built-in rules), on the owner's
machine, with the release key (make_update_key.py's, the key updates and the
tester list are signed with). tools/release/README.md, "Crossplay version
table", says the whole of it; port/linux/src/netversion_table.h the file's
form.

  # show a table (its version, rows, what this source makes of each, and
  # whether its signature is the owner's)
  python3 tools/release/sign_netversions.py --table release-files/netversions.txt --show

  # OpenCE raised to 40 and a 40 game plays as a 39 game does (checked with
  # the crossplay tests): join 40 by 39's rules
  python3 tools/release/sign_netversions.py --key /path/to/halo-update.key \\
      --table release-files/netversions.txt --join 40:39

  # a version known not to work, with the reason the browser shows
  ...  --refuse 41 "OpenCE 41 changed the game settings; wait for an update"

  # take a row off (the version is then the built-in list's again)
  ...  --remove 40

Every change writes the table again with its version one higher (or
--version N, which must be higher), signs it into netversions.txt.sig beside
it and checks both as the game does. The game takes a table only when its
version is higher than the one it has, so a table's version only goes up.
Rules (--join's second number) must be the network version of a wire this
source has built in (port/linux/include/network_wire.h's
HALO_PORT_CROSSPLAY_*NETWORK_VERSION: 29, 32, 38, 39, 40, 41); --allow-newer-rules
takes another (a newer build's wire: older builds then show "needs a newer
Halo CE for PS Vita"). A table made with another key than
port/vita/include/vita_update_key.h's HALO_RELEASE_PUBLIC_KEY is refused
unless --test (the tests' throwaway keys). The private key's path is only
read, never printed or copied. Upload netversions.txt and netversions.txt.sig
to the "crossplay-table" release (README.md says how).
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402

MAGIC = "halo-ce-vita-netversions 1"
HOST_VERSION = 32  # (network_wire.h's HALO_PORT_CROSSPLAY_NETWORK_VERSION: what a Vita hosting for everyone announces)
SIGNATURE_SUFFIX = ".sig"
MAXIMUM_ROWS = 64
MAXIMUM_SIZE = 27 + 19 + MAXIMUM_ROWS * (13 + 64 + 1)
LEGACY_VERSION = 18
REASON = re.compile(r"[A-Za-z0-9.,:;!?'()/+\- ]{1,64}")
NUMBER = r"[1-9][0-9]{0,4}"
WIRE_HEADER = common.REPOSITORY / "port/linux/include/network_wire.h"


def built_in_rules():
    """the crossplay versions this source has rules for (network_wire.h)"""
    try:
        text = WIRE_HEADER.read_text(encoding="utf-8")
    except OSError:
        common.fail(f"cannot read {WIRE_HEADER}")
    versions = sorted({int(value) for value in
                       re.findall(r"#define HALO_PORT_CROSSPLAY_\w*NETWORK_VERSION (\d+)", text)})
    if not versions:
        common.fail(f"no crossplay versions in {WIRE_HEADER}")
    return versions


def reason_valid(text):
    return bool(REASON.fullmatch(text)) and not text.startswith(" ") and not text.endswith(" ")


def parse_table(data, signature_text, public):
    """(version, {network version: ("join", rules) or ("refuse", reason)}) of
    a table's bytes and its signature file's, checked as
    port/linux/src/netversion_table.c checks them, or fails"""
    if len(data) > MAXIMUM_SIZE:
        common.fail("the table is larger than any table can be")
    match = re.fullmatch(rb"([0-9a-f]{128})(\r?\n)?", signature_text)
    if not match:
        common.fail("not a table's signature file (128 lowercase hexadecimal digits)")
    if not common.ed25519.verify(public, data, bytes.fromhex(match.group(1).decode())):
        common.fail("the table's signature is not this key's")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        common.fail("not a crossplay version table (not ASCII)")
    lines = text.split("\n")
    if len(lines) < 3 or lines[0] != MAGIC or lines[-1] != "" or not re.fullmatch(r"version [1-9][0-9]{0,9}", lines[1]):
        common.fail("not a crossplay version table (its first two lines)")
    version = int(lines[1].split()[1])
    rows, previous = {}, 0
    for line in lines[2:-1]:
        join = re.fullmatch(rf"join ({NUMBER}) rules ({NUMBER})", line)
        refuse = re.fullmatch(rf"refuse ({NUMBER}) (.+)", line)
        if join:
            number, row = int(join.group(1)), ("join", int(join.group(2)))
            if row[1] > 0xFFFF:
                common.fail(f"not a row: {line!r}")
        elif refuse and reason_valid(refuse.group(2)):
            number, row = int(refuse.group(1)), ("refuse", refuse.group(2))
        else:
            common.fail(f"not a row: {line!r}")
        if number > 0xFFFF or number <= previous:
            common.fail(f"the rows are not in order of their versions, or out of range: {line!r}")
        previous = number
        rows[number] = row
    if version > 0xFFFFFFFF or len(rows) > MAXIMUM_ROWS:
        common.fail("the table is out of range")
    return version, rows


def table_text(version, rows):
    lines = [MAGIC, f"version {version}"]
    for number in sorted(rows):
        kind, value = rows[number]
        lines.append(f"join {number} rules {value}" if kind == "join" else f"refuse {number} {value}")
    return ("\n".join(lines) + "\n").encode("ascii")


def describe(rows, rules):
    """each row, and what a build of this source makes of it"""
    out = []
    for number in sorted(rows):
        kind, value = rows[number]
        if number == LEGACY_VERSION:
            note = "the Vitas' own version: ignored"
        elif kind == "refuse":
            note = "not joined" + (" (a built-in version: a kill switch)" if number in rules else "")
        elif number in rules and value != number:
            note = f"ignored: {number} is built in with its own rules"
        elif value not in rules:
            note = f"not joined by this source's builds (no rules of {value}): \"needs a newer Halo CE for PS Vita\""
        else:
            note = f"joined, played by {value}'s rules"
        out.append(f"  {kind} {number} {'rules ' + str(value) if kind == 'join' else value}: {note}")
    joined = sorted({version for version in rules if rows.get(version, ("join",))[0] != "refuse"} |
                    {number for number, (kind, value) in rows.items()
                     if kind == "join" and value in rules and number not in rules and number != LEGACY_VERSION})
    out.append(f"  the crossplay wire joins: {', '.join(map(str, joined)) or 'none'} (built in: "
               f"{', '.join(map(str, rules))})")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--table", required=True, help="the table to make or change (netversions.txt; its signature "
                        "is written beside it, netversions.txt.sig)")
    parser.add_argument("--key", help="the private key file (make_update_key.py); not needed for --show")
    parser.add_argument("--show", action="store_true", help="print the table and check its signature")
    parser.add_argument("--join", action="append", default=[], metavar="VERSION:RULES",
                        help="join this network version, played by the built-in version RULES's rules")
    parser.add_argument("--refuse", action="append", nargs=2, default=[], metavar=("VERSION", "REASON"),
                        help="refuse this network version, the reason shown in the browser (1-64 of A-Z a-z 0-9 "
                        "space . , : ; ! ? ' ( ) / + -)")
    parser.add_argument("--remove", action="append", default=[], type=int, metavar="VERSION", help="take a row off")
    parser.add_argument("--allow-newer-rules", action="store_true",
                        help="--join with rules this source does not have (a newer build's wire)")
    parser.add_argument("--version", type=int, help="the new version (default: the old one plus 1)")
    parser.add_argument("--public", help="--show without --key: the key the table is checked with (default: the "
                        "source's HALO_RELEASE_PUBLIC_KEY)")
    parser.add_argument("--test", action="store_true", help="a TEST key's table (the automated tests')")
    arguments = parser.parse_args()
    common.ed25519.self_test()
    path = Path(arguments.table)
    signature_path = path.with_name(path.name + SIGNATURE_SUFFIX)
    rules = built_in_rules()

    if arguments.key:
        seed = common.read_private_key(arguments.key)
        public = common.ed25519.public_key(seed)
        if not arguments.test and public.hex() != common.source_public_key():
            common.fail("this key is not port/vita/include/vita_update_key.h's HALO_RELEASE_PUBLIC_KEY (the "
                        "owner's): the game would refuse the table (--test for a test key)")
    else:
        seed = None
        public = bytes.fromhex(arguments.public or common.source_public_key() or common.PLACEHOLDER)

    if path.exists():
        if not signature_path.exists():
            common.fail(f"{path} has no {signature_path.name} beside it")
        version, rows = parse_table(path.read_bytes(), signature_path.read_bytes(), public)
    else:
        version, rows = 0, {}

    changing = arguments.join or arguments.refuse or arguments.remove or arguments.version
    if arguments.show:
        print(f"{path}: version {version}, {len(rows)} rows, signature good" if version else
              f"{path}: no table yet (the built-in list alone)")
        print("\n".join(describe(rows, rules)))
        if not changing:
            return
    if seed is None:
        common.fail("--key is needed to change the table")

    for item in arguments.join:
        match = re.fullmatch(rf"({NUMBER}):({NUMBER})", item)
        if not match or int(match.group(1)) > 0xFFFF or int(match.group(2)) > 0xFFFF:
            common.fail(f"--join {item}: VERSION:RULES, two network versions (40:39)")
        number, value = int(match.group(1)), int(match.group(2))
        if number == LEGACY_VERSION:
            common.fail(f"--join {number}: the Vitas' own network version, never a crossplay one")
        if number in rules:
            common.fail(f"--join {number}: built in with its own rules already (a row for it would be ignored)")
        if value not in rules and not arguments.allow_newer_rules:
            common.fail(f"--join {item}: {value} is not rules this source has ({', '.join(map(str, rules))}); "
                        "--allow-newer-rules for a newer build's")
        rows[number] = ("join", value)
        print(f"join {number}, played by {value}'s rules")
    for number_text, reason in arguments.refuse:
        if not re.fullmatch(NUMBER, number_text) or int(number_text) > 0xFFFF:
            common.fail(f"--refuse {number_text}: a network version (1-65535)")
        if not reason_valid(reason):
            common.fail(f"--refuse {number_text}: the reason {reason!r} must be 1-64 of A-Z a-z 0-9 space "
                        ". , : ; ! ? ' ( ) / + - (no space first or last)")
        number = int(number_text)
        if number == LEGACY_VERSION:
            common.fail(f"--refuse {number}: the Vitas' own network version, not the table's")
        if number == HOST_VERSION:
            common.fail(f"--refuse {number}: what every Vita's game for everyone announces: Vitas could no longer "
                        "join each other's games")
        rows[number] = ("refuse", reason)
        print(f"refuse {number}: {reason}" + (" (a built-in version: its games are no longer joined)"
                                             if number in rules else ""))
    for number in arguments.remove:
        if number not in rows:
            common.fail(f"--remove {number}: no such row")
        del rows[number]
        print(f"removed the row of {number}")
    if len(rows) > MAXIMUM_ROWS:
        common.fail(f"more than {MAXIMUM_ROWS} rows")
    new_version = arguments.version if arguments.version is not None else version + 1
    if new_version <= version or new_version > 0xFFFFFFFF:
        common.fail(f"--version {new_version} must be higher than {version} (and at most 4294967295)")
    body = table_text(new_version, rows)
    if len(body) > MAXIMUM_SIZE:
        common.fail("the table would be larger than the game takes")
    signature = common.ed25519.sign(seed, body)
    signature_text = (signature.hex() + "\n").encode("ascii")
    check_version, check_rows = parse_table(body, signature_text, public)
    assert check_version == new_version and check_rows == rows
    path.parent.mkdir(parents=True, exist_ok=True)
    for target, data in ((path, body), (signature_path, signature_text)):
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_bytes(data)
        temporary.replace(target)
    print(f"{path}: version {new_version}, {len(rows)} rows, signed and checked ({signature_path.name} beside it)")
    print("\n".join(describe(rows, rules)))


if __name__ == "__main__":
    main()
