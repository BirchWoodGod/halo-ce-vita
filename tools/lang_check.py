#!/usr/bin/env python3
"""Check the port's translations (port/vita/app0/lang/<code>.txt, read by
port/linux/src/lang.c) against the English strings the code marks.

Every string literal given to T(), N_() or TW(L...) in port/ and source/
is a key (adjacent literals joined, C escapes undone, as the compiler
does), and so is the text of each <string text="..."> and each piece of a
strings="A|B" attribute of the PC menus' XML (port/vita/app0/menus, which
port/linux/src/menu_files.c translates as it reads them). The settings
panel's rows (vita_settings.c's settings[] table: labels, values' names,
help lines) are checked by vita_settings_test.c, which asks the panel
itself.

Each language file must have an entry for every key; an entry's printf
conversions (and Halo's %a-button icon tokens) must be its English's, in
the same order; and every character must be one the settings panel's
font draws (port/vita/host/overlay_font.h: printable ASCII and the Latin-1
letters, ¿ ¡ « » ß º ª). Exit status 1 when any file falls short.

  python3 tools/lang_check.py [--root .] [--list] [--unused] [--missing CODE]

--list prints the keys found (for a new language file: tools/lang_check.py
--missing fr writes the missing entries of fr.txt, English for English, to
standard output, ready to translate).
"""

import argparse
import re
import sys
from pathlib import Path

LANG_DIR = Path("port/vita/app0/lang")
MENUS_DIR = Path("port/vita/app0/menus")
SCAN_DIRS = [Path("port"), Path("source")]
SKIP_PARTS = {"build", "third_party", "tests"}

# the characters the overlay font draws (overlay_font.h overlay_glyph)
DRAWN = set(chr(c) for c in range(32, 127)) | set("\r\n") | set(
    "ÀÁÂÃÄÅÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝàáâãäåçèéêëìíîïðñòóôõöøùúûüýÿ¡ª«º»¿ß")

STRING = r'(?:L?"(?:[^"\\\n]|\\.)*"\s*)+'
CALL = re.compile(r'(?<![A-Za-z0-9_])(T|N_|TW)\(\s*(' + STRING + r')\s*\)')


def unescape(literal: str) -> str:
    """the text of adjacent C string literals"""
    out = []
    for part in re.findall(r'L?"((?:[^"\\\n]|\\.)*)"', literal):
        index = 0
        while index < len(part):
            ch = part[index]
            if ch != "\\":
                out.append(ch)
                index += 1
                continue
            nxt = part[index + 1]
            simple = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'", "a": "\a", "0": "\0"}
            if nxt == "x":
                match = re.match(r"[0-9a-fA-F]+", part[index + 2:])
                out.append(chr(int(match.group(0), 16)))
                index += 2 + len(match.group(0))
            elif nxt in "01234567" and re.match(r"[0-7]{1,3}", part[index + 1:]):
                match = re.match(r"[0-7]{1,3}", part[index + 1:])
                out.append(chr(int(match.group(0), 8)))
                index += 1 + len(match.group(0))
            else:
                out.append(simple.get(nxt, nxt))
                index += 2
    # (the source is UTF-8: a literal's bytes are its text)
    return "".join(out)


def strip_comments(text: str) -> str:
    """the source with its comments blanked (the lines kept)"""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:[^\"\\\n]|\\.)*\"|'(?:[^'\\\n]|\\.)*'",
                  lambda m: m.group(0) if m.group(0)[0] in "\"'" else blank(m), text, flags=re.S)


def source_keys(root: Path):
    """{key: [where, ...]} of the T(), N_() and TW() literals"""
    keys = {}
    for top in SCAN_DIRS:
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in (".c", ".h") or SKIP_PARTS & set(path.relative_to(root).parts):
                continue
            text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
            if "T(" not in text and "N_(" not in text and "TW(" not in text:
                continue
            for match in CALL.finditer(text):
                key = unescape(match.group(2))
                if not re.search(r"[A-Za-z]", key):
                    continue
                line = text.count("\n", 0, match.start()) + 1
                keys.setdefault(key, []).append(f"{path.relative_to(root)}:{line}")
    return keys


def xml_keys(root: Path):
    """the PC menus' texts (menu_files.c translates each as it reads it: the
    attribute's text, its \\n made a new line)"""
    keys = {}
    for path in sorted((root / MENUS_DIR).glob("*.xml")):
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r'<string\s+text="([^"]*)"', text):
            key = xml_text(match.group(1))
            if re.search(r"[A-Za-z]", key):
                keys.setdefault(key, []).append(f"{path.relative_to(root)}:{text.count(chr(10), 0, match.start()) + 1}")
        for match in re.finditer(r'\sstrings="([^"]*)"', text):
            for piece in match.group(1).split("|"):
                key = xml_text(piece)
                if re.search(r"[A-Za-z]", key):
                    keys.setdefault(key, []).append(
                        f"{path.relative_to(root)}:{text.count(chr(10), 0, match.start()) + 1}")
    return keys


def xml_text(attribute: str) -> str:
    for entity, ch in (("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&apos;", "'"), ("&amp;", "&")):
        attribute = attribute.replace(entity, ch)
    return attribute.replace("\\n", "\n")


def conversions(text: str):
    """printf's conversions as written, in order (lang.c format_conversions)"""
    found = []
    index = 0
    while True:
        index = text.find("%", index)
        if index < 0:
            return found
        start = index
        index += 1
        if index < len(text) and text[index] == "%":
            index += 1
            continue
        match = re.match(r"[-+ #0]*[0-9*]*(?:\.[0-9*]*)?[hlLqjzt]*.?", text[index:], re.S)
        index += len(match.group(0))
        found.append(text[start:index])


def c_unescape_line(text: str) -> str:
    out, index = [], 0
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            out.append({"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}.get(text[index + 1], None) or "\0BAD")
            index += 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


ENTRY = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*(?:#.*)?$')
NAME = re.compile(r'^\s*@name\s*=\s*"((?:[^"\\]|\\.)*)"\s*$')


def read_language(path: Path):
    """(name, {english: (translation, line)}, [problems]) as lang.c reads it"""
    entries, problems, name = {}, [], None
    for number, line in enumerate(path.read_text(encoding="utf-8").lstrip("\ufeff").split("\n"), 1):
        line = line.rstrip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = NAME.match(line)
        if match:
            name = c_unescape_line(match.group(1))
            continue
        match = ENTRY.match(line)
        if not match:
            problems.append(f"{path.name}:{number}: not \"English\" = \"translation\"")
            continue
        english, text = c_unescape_line(match.group(1)), c_unescape_line(match.group(2))
        if "\0BAD" in english + text:
            problems.append(f"{path.name}:{number}: an escape lang.c does not read (\\n \\r \\t \\\" \\\\ only)")
            continue
        if english in entries:
            problems.append(f"{path.name}:{number}: a second line for \"{english[:40]}\"")
        if conversions(english) != conversions(text):
            problems.append(f"{path.name}:{number}: its % conversions {conversions(text)} are not the English's "
                            f"{conversions(english)}: \"{english[:50]}\"")
        bad = sorted(set(ch for ch in text if ch not in DRAWN))
        if bad:
            problems.append(f"{path.name}:{number}: characters the panel's font does not draw: "
                            f"{' '.join(repr(ch) for ch in bad)} in \"{text[:50]}\"")
        entries[english] = (text, number)
    if name is None:
        problems.append(f"{path.name}: no @name line")
    return name, entries, problems


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--list", action="store_true", help="print the keys found")
    parser.add_argument("--unused", action="store_true", help="also list each file's entries no key has")
    parser.add_argument("--missing", metavar="CODE", help="print CODE.txt's missing entries, ready to translate")
    args = parser.parse_args()
    root = args.root
    keys = source_keys(root)
    for key, where in xml_keys(root).items():
        keys.setdefault(key, []).extend(where)
    if args.list:
        for key in sorted(keys):
            print(f'"{escape(key)}"  # {keys[key][0]}')
        return 0
    if args.missing:
        path = root / LANG_DIR / f"{args.missing}.txt"
        entries = read_language(path)[1] if path.exists() else {}
        for key in sorted(keys, key=lambda k: keys[k][0]):
            if key not in entries:
                print(f'# {keys[key][0]}\n"{escape(key)}" = "{escape(key)}"')
        return 0
    files = sorted((root / LANG_DIR).glob("*.txt"))
    failed = 0
    print(f"lang_check: {len(keys)} strings marked for translation (T, N_, TW, the PC menus' XML)")
    for path in files:
        name, entries, problems = read_language(path)
        missing = [key for key in keys if key not in entries]
        for key in sorted(missing, key=lambda k: keys[k][0]):
            problems.append(f"{path.name}: no entry for \"{escape(key)[:70]}\" ({keys[key][0]})")
        status = "FAIL" if problems else "PASS"
        print(f"{status} {path.name} ({name}): {len(entries)} entries, {len(keys) - len(missing)} of {len(keys)} "
              f"marked strings translated")
        for problem in problems:
            print(f"  {problem}")
        if args.unused:
            for english in sorted(set(entries) - set(keys)):
                print(f"  (not a marked string here: \"{escape(english)[:60]}\" - a settings panel row's?)")
        failed += bool(problems)
    if not files:
        print("lang_check: no language files")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
