#!/usr/bin/env python3
"""Write the chat filter's word list (port/vita/app0/chat/filter.txt).

The in-game chat (port/linux/game/game_chat.c) masks slurs and hate speech
in what other players say, on the machine that shows it
(port/linux/src/chat_filter.c). Its words are a data file shipped in the VPK
(and beside the PC build), made by this tool from OpenCE's streamer mode
table: the SLURS group of OpenCE's port/linux/game/text_filter_terms.inc and
its allowed words (Scunthorpe), as of the commit before OpenCE's licence
changed (4b09168e^, when OpenCE was CC0). That table is OpenCE's review of
Surge AI's profanity list ("The Obscenity List", MIT; the notice is in the
file's header): each base word kept or dropped, filed in a group, with where
it may be found (inside a word, at a word's start, or as the whole word).
Only the SLURS group is taken (racial and ethnic slurs, sexual orientation and
gender slurs, disability slurs, the hate phrases OpenCE's review added):
swearing and insults are not this filter's (the owner: "keep it focused on
slurs/hate speech").

The words are ROT13'd in the file, as OpenCE's review.txt keeps them, so the
file is no readable list; the allowed words are plain.

    python3 tools/chat_filter_words.py --opence ~/github/vitaprojects/opence/opence-contrib

checks the OpenCE checkout has the commit, reads the table there with git
show and writes the file again (--check: says whether the file is as it
would be written, and changes nothing).
"""

import argparse
import codecs
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/vita/app0/chat/filter.txt"
# (OpenCE's last CC0 commit: its parent of the AGPL switch)
COMMIT = "4b09168e^"
TERMS = "port/linux/game/text_filter_terms.inc"
RULES = {"_text_filter_anywhere": "anywhere", "_text_filter_start": "start", "_text_filter_whole": "whole"}
# (ROT13'd) the SLURS group's words left out here: in a Halo game "flamer" is
# the flamethrower (Halo PC's), said far more often than the slur
EXCLUDED = {"synzre", "synzref"}

HEADER = """\
# The in-game chat's filter (port/linux/src/chat_filter.c): slurs and hate
# speech masked as **** in what other players say, on the machine that shows
# it (Settings > Multiplayer > Chat > Chat filter; on by default). Written by
# tools/chat_filter_words.py; do not edit by hand.
#
# A line is a rule and a word:
#   whole <word>     only as a whole word (or with "s" / "es" after it)
#   start <word>     at a word's start
#   anywhere <word>  inside any word
#   allow <word>     never masked, nor any part of it (Scunthorpe)
# The words of the first three are ROT13'd (tr 'A-Za-z' 'N-ZA-Mn-za-m'), so
# this file is no readable list. Letters a-z only; the matcher reads leet
# (0 o, 1 i/l, 3 e, @ a, $ s...), spacing, punctuation and repeated letters.
#
# Source: OpenCE's streamer mode table (github.com/OpenCommunityEdition/OpenCE,
# port/linux/game/text_filter_terms.inc and port/assets/text_filter, commit
# {commit}, CC0 1.0), its SLURS group alone (but "flamer": in Halo, the
# flamethrower). That table is OpenCE's review of
# Surge AI's profanity list "The Obscenity List" (profanity_en.csv, SHA-256
# ca4ff7d713a8e6f6ec2616f489cd09f6fb96b8b3af404a207783be58a91f3ede), whose
# notice follows:
#
# MIT License
#
# Copyright (c) Surge AI
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""


def read_table(checkout: Path, commit: str) -> str:
    result = subprocess.run(["git", "-C", str(checkout), "show", f"{commit}:{TERMS}"], capture_output=True,
                            text=True, check=False)
    if result.returncode:
        sys.exit(f"chat_filter_words: {checkout} has no {commit}:{TERMS}: {result.stderr.strip()}")
    return result.stdout


def resolve(checkout: Path, commit: str) -> str:
    result = subprocess.run(["git", "-C", str(checkout), "rev-parse", "--short=12", commit], capture_output=True,
                            text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else commit


def decode(escaped: str, key: int) -> str:
    raw = codecs.escape_decode(escaped.encode())[0]
    return bytes(byte ^ key for byte in raw).decode("ascii")


def build(table: str, commit: str) -> str:
    key_match = re.search(r"#define\s+TEXT_FILTER_TERM_KEY\s+(0x[0-9A-Fa-f]+|\d+)", table)
    if not key_match:
        sys.exit("chat_filter_words: no TEXT_FILTER_TERM_KEY in the table")
    key = int(key_match.group(1), 0)
    terms = []
    for match in re.finditer(r'\{\s*(_text_filter_\w+)\s*,\s*(\d+)\s*,\s*(_text_filter_\w+)\s*,\s*"([^"]*)"\s*\}',
                             table):
        group, _severity, rule, letters = match.groups()
        if group != "_text_filter_slurs":
            continue
        word = decode(letters, key)
        if not re.fullmatch(r"[a-z]{2,40}", word) or rule not in RULES:
            sys.exit(f"chat_filter_words: a term the tool cannot read: {rule}")
        if codecs.encode(word, "rot13") in EXCLUDED:
            continue
        terms.append((RULES[rule], codecs.encode(word, "rot13")))
    allowed_block = re.search(r"text_filter_allowed_words\[\]\s*=\s*\{(.*?)\};", table, re.S)
    allowed = re.findall(r'"([a-z]+)"', allowed_block.group(1)) if allowed_block else []
    if len(terms) < 50:
        sys.exit(f"chat_filter_words: only {len(terms)} slurs found: the table's layout changed?")
    lines = [HEADER.format(commit=commit)]
    order = {"whole": 0, "start": 1, "anywhere": 2}
    for rule, word in sorted(set(terms), key=lambda term: (order[term[0]], term[1])):
        lines.append(f"{rule} {word}\n")
    for word in sorted(set(allowed)):
        lines.append(f"allow {word}\n")
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--opence", required=True, help="an OpenCE git checkout holding the commit")
    parser.add_argument("--commit", default=COMMIT, help=f"the commit to read (default {COMMIT})")
    parser.add_argument("--check", action="store_true", help="only say whether the file is up to date")
    args = parser.parse_args()
    checkout = Path(args.opence).expanduser()
    text = build(read_table(checkout, args.commit), resolve(checkout, args.commit))
    if args.check:
        current = OUTPUT.read_text(encoding="ascii") if OUTPUT.is_file() else ""
        if current != text:
            print(f"{OUTPUT.relative_to(ROOT)} is not as the table makes it", file=sys.stderr)
            return 1
        print(f"{OUTPUT.relative_to(ROOT)} is up to date")
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text, encoding="ascii")
    count = sum(1 for line in text.splitlines() if line and not line.startswith(("#", "allow")))
    print(f"wrote {OUTPUT.relative_to(ROOT)}: {count} words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
