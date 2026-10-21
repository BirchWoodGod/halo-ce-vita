#!/usr/bin/env python3
"""Make or change the signed tester list (the flaming helmet's: who may wear
it), on the owner's machine, with the release key (make_update_key.py's,
the key updates are signed with). tools/release/README.md, "Tester list",
says the whole of it; port/linux/src/tester_list.h the file's form.

  # show a list (its version, testers, whether its signature is the owner's)
  python3 tools/release/sign_testers.py --list port/vita/app0/testers.txt --show

  # add a tester from the tester-id.txt their game wrote (Vita:
  # ux0:data/haloce-vita/friends/tester-id.txt; PC: the save folder's
  # friends/tester-id.txt), or by key, or from your own friends.dat by
  # their name or friend code (you are friends with them)
  python3 tools/release/sign_testers.py --key ~/halo-update-key/halo-update.key \\
      --list port/vita/app0/testers.txt --add-id tester-id.txt [--expires 2027-01-31]
  ...  --add-key <64 hex digits> --label Bob
  ...  --friends-dat /path/to/friends.dat --add-friend ABCD-EFGH-JKLM

  # take a tester off (by label, key or code): the new version revokes them
  ...  --remove Bob

Every change writes the list again with its version one higher (or
--version N, which must be higher), signs it and checks it as the game
does. A list made with another key than port/vita/include/vita_update_key.h's
HALO_RELEASE_PUBLIC_KEY is refused unless --test (the tests' throwaway keys).
The private key never goes into git: the list does (port/vita/app0/testers.txt
ships in the VPK and beside the PC executable), and a newer list can be
given to testers to drop into ux0:data/haloce-vita/testers.txt (the PC's
save folder's testers.txt) before a release carries it.
"""

import argparse
import calendar
import hashlib
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402

MAGIC = "halo-ce-vita-testers 1"
ID_MAGIC = "halo-ce-vita-tester-id 1"
MAXIMUM_ENTRIES = 256
LABEL = re.compile(r"[A-Za-z0-9._-]{1,32}")
CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"


def friend_code(key):
    """p2p_friends.c's code_of_key: ABCD-EFGH-JKLM"""
    digest = hashlib.sha256(b"hcev-friend-code-1" + key).digest()
    bits = int.from_bytes(digest[:8], "big") >> 4
    code = "".join(CODE_ALPHABET[(bits >> (5 * (11 - index))) & 31] for index in range(12))
    return f"{code[0:4]}-{code[4:8]}-{code[8:12]}"


def parse_list(data, public):
    """(version, [(key bytes, expires, label)]) of a list's bytes, checked as
    port/linux/src/tester_list.c checks it (public: the key it must be
    signed with), or fails"""
    match = re.fullmatch(rb"(.*)signature ([0-9a-f]{128})\n", data, re.S)
    if not match:
        common.fail("not a tester list: no signature line")
    signed, signature = match.group(1), bytes.fromhex(match.group(2).decode())
    if not common.ed25519.verify(public, signed, signature):
        common.fail("the list's signature is not this key's")
    lines = signed.decode("ascii").split("\n")
    if len(lines) < 3 or lines[0] != MAGIC or lines[-1] != "" or not re.fullmatch(r"version [1-9][0-9]{0,9}", lines[1]):
        common.fail("not a tester list (its first two lines)")
    version = int(lines[1].split()[1])
    entries = []
    for line in lines[2:-1]:
        fields = re.fullmatch(r"tester ([0-9a-f]{64}) (0|[1-9][0-9]{0,11}) ([A-Za-z0-9._-]{1,32})", line)
        if not fields:
            common.fail(f"not a tester line: {line!r}")
        entries.append((bytes.fromhex(fields.group(1)), int(fields.group(2)), fields.group(3)))
    if version > 0xFFFFFFFF or len(entries) > MAXIMUM_ENTRIES:
        common.fail("the list is out of range")
    return version, entries


def list_text(version, entries):
    lines = [MAGIC, f"version {version}"]
    lines += [f"tester {key.hex()} {expires} {label}" for key, expires, label in entries]
    return ("\n".join(lines) + "\n").encode("ascii")


def parse_expiry(text):
    """0 (never), a Unix second, or YYYY-MM-DD (the end of that day, UTC)"""
    if text in (None, "", "never", "0"):
        return 0
    if re.fullmatch(r"[1-9][0-9]{0,11}", text):
        return int(text)
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}", text):
        return calendar.timegm(time.strptime(text, "%Y-%m-%d")) + 86400
    common.fail(f"--expires {text}: never, a Unix second or YYYY-MM-DD")


def read_tester_id(path):
    """(key, label) of a tester-id.txt the game wrote"""
    try:
        text = Path(path).read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        common.fail(f"cannot read {path}: {error}")
    fields = dict(line.split(" ", 1) for line in text.splitlines()[1:] if " " in line)
    if not text.startswith(ID_MAGIC + "\n") or not re.fullmatch(r"[0-9a-f]{64}", fields.get("key", "")):
        common.fail(f"{path} is not a tester-id.txt (the game's friends/tester-id.txt)")
    key = bytes.fromhex(fields["key"])
    name = re.sub(r"[^A-Za-z0-9._-]", "", fields.get("name", ""))[:20]
    return key, (name + "-" if name else "") + friend_code(key).replace("-", "")


def friends_dat(path):
    """[(key, name, code)] of the friends in a friends.dat (p2p_friends.c's
    list_encode: "HFRL", version 1, six counts, then each friend's key,
    name, friendship, since, epoch, counter)"""
    data = Path(path).read_bytes()
    if len(data) < 11 or data[:4] != b"HFRL" or data[4] != 1:
        common.fail(f"{path} is not a friends.dat")
    count, offset, friends = data[5], 11, []
    for _ in range(count):
        record = data[offset:offset + 85]
        if len(record) < 85:
            common.fail(f"{path} is cut short")
        key, length = record[:32], min(record[32], 16)
        name = record[33:33 + length].decode("ascii", "replace")
        friends.append((key, name, friend_code(key)))
        offset += 85
    return friends


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", required=True, help="the list to make or change (port/vita/app0/testers.txt)")
    parser.add_argument("--key", help="the private key file (make_update_key.py); not needed for --show")
    parser.add_argument("--show", action="store_true", help="print the list and check its signature")
    parser.add_argument("--add-id", action="append", default=[], help="a tester's tester-id.txt")
    parser.add_argument("--add-key", action="append", default=[], help="a tester's friend identity (64 hex digits)")
    parser.add_argument("--label", help="(with --add-key) who it is: 1-32 of A-Z a-z 0-9 . _ -")
    parser.add_argument("--friends-dat", help="your own friends.dat, for --add-friend")
    parser.add_argument("--add-friend", action="append", default=[], help="a friend's name or code (--friends-dat)")
    parser.add_argument("--expires", help="the added testers' end: never (default), YYYY-MM-DD or a Unix second")
    parser.add_argument("--remove", action="append", default=[], help="a tester's label, key or friend code")
    parser.add_argument("--version", type=int, help="the new version (default: the old one plus 1)")
    parser.add_argument("--public", help="--show without --key: the key the list is checked with (default: the "
                        "source's HALO_RELEASE_PUBLIC_KEY)")
    parser.add_argument("--test", action="store_true", help="a TEST key's list (the automated tests')")
    arguments = parser.parse_args()
    common.ed25519.self_test()
    path = Path(arguments.list)

    if arguments.key:
        seed = common.read_private_key(arguments.key)
        public = common.ed25519.public_key(seed)
        source = common.source_public_key()
        if not arguments.test and public.hex() != source:
            common.fail("this key is not port/vita/include/vita_update_key.h's HALO_RELEASE_PUBLIC_KEY (the "
                        "owner's): the game would refuse the list (--test for a test key)")
    else:
        seed = None
        public = bytes.fromhex(arguments.public or common.source_public_key() or common.PLACEHOLDER)

    if path.exists():
        version, entries = parse_list(path.read_bytes(), public)
    else:
        version, entries = 0, []

    if arguments.show:
        print(f"{path}: version {version}, {len(entries)} testers, signature good")
        now = int(time.time())
        for key, expires, label in entries:
            state = "never ends" if not expires else (
                f"ends {time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime(expires))}" +
                (" (EXPIRED)" if expires <= now else ""))
            print(f"  {label:32} {friend_code(key)}  {key.hex()}  {state}")
        if not (arguments.add_id or arguments.add_key or arguments.add_friend or arguments.remove or
                arguments.version):
            return

    if seed is None:
        common.fail("--key is needed to change the list")
    expires = parse_expiry(arguments.expires)
    added = []
    for item in arguments.add_id:
        added.append(read_tester_id(item))
    for item in arguments.add_key:
        if not re.fullmatch(r"[0-9a-fA-F]{64}", item):
            common.fail(f"--add-key {item}: 64 hexadecimal digits")
        key = bytes.fromhex(item)
        added.append((key, arguments.label or friend_code(key).replace("-", "")))
    if arguments.add_friend:
        if not arguments.friends_dat:
            common.fail("--add-friend needs --friends-dat")
        friends = friends_dat(arguments.friends_dat)
        for item in arguments.add_friend:
            wanted = item.upper().replace("-", "").replace(" ", "")
            found = [f for f in friends if f[2].replace("-", "") == wanted or f[1] == item]
            if len(found) != 1:
                common.fail(f"--add-friend {item}: {'no' if not found else 'more than one'} friend by that name or "
                            "code in " + arguments.friends_dat)
            key, name, code = found[0]
            label = re.sub(r"[^A-Za-z0-9._-]", "", name)[:20]
            added.append((key, (label + "-" if label else "") + code.replace("-", "")))
    for key, label in added:
        if not LABEL.fullmatch(label):
            common.fail(f"label {label!r}: 1-32 of A-Z a-z 0-9 . _ -")
        if not common.ed25519._decompress(key):
            common.fail(f"{key.hex()} is not an Ed25519 public key")
        entries = [entry for entry in entries if entry[0] != key]
        entries.append((key, expires, label))
        print(f"added {label} ({friend_code(key)}){', ends ' + str(expires) if expires else ''}")
    for item in arguments.remove:
        wanted = item.upper().replace("-", "")
        kept = [entry for entry in entries if not (entry[2] == item or entry[0].hex() == item.lower() or
                                                   friend_code(entry[0]).replace("-", "") == wanted)]
        if len(kept) == len(entries):
            common.fail(f"--remove {item}: no such tester")
        print(f"removed {len(entries) - len(kept)} ({item})")
        entries = kept
    if len(entries) > MAXIMUM_ENTRIES:
        common.fail(f"more than {MAXIMUM_ENTRIES} testers")
    new_version = arguments.version if arguments.version is not None else version + 1
    if new_version <= version or new_version > 0xFFFFFFFF:
        common.fail(f"--version {new_version} must be higher than {version} (and at most 4294967295)")
    body = list_text(new_version, entries)
    signature = common.ed25519.sign(seed, body)
    data = body + b"signature " + signature.hex().encode() + b"\n"
    check_version, check_entries = parse_list(data, public)
    assert check_version == new_version and len(check_entries) == len(entries)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)
    print(f"{path}: version {new_version}, {len(entries)} testers, signed and checked")


if __name__ == "__main__":
    main()
