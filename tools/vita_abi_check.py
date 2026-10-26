#!/usr/bin/env python3
"""Find the Vita build's calls that only work under the x86 calling convention.

The game's C is C89 with warnings off: a call with no prototype in scope is
an implicit `int f()`, and a local prototype may disagree with the
definition. On x86 every argument is a stack slot, so that still works. The
Vita is hard-float ARM: reals travel in VFP registers, but a variadic or
unprototyped call passes them in the core registers, so the callee reads
garbage (the 1.1.1 RC's crash: platform_log implicitly declared in
tick_thread.c, a %s after a %f taking a stray word as its string).

This compiles every C unit of `ninja vita` to LLVM IR with its own flags
(from `ninja -t commands`) and runs tools/android_abi_check.py's comparison
over them: each external declaration against its definition, reporting the
ones whose register classes or variadic-ness differ. Run it from a tree
configured for the Vita (configure.py, then `ninja vita` once is not
needed: only the compile commands are read):

    python3 tools/vita_abi_check.py [--out DIR] [--jobs N]

Exit status 1 when a mismatch is found that tools/vita_abi_allow.txt does
not list (the decompiled sources' own, checked harmless there); 0 otherwise.
"""

import argparse
import concurrent.futures
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import android_abi_check  # noqa: E402

ALLOW = HERE / "vita_abi_allow.txt"


def compile_commands(target: str):
    text = subprocess.run(["ninja", "-t", "commands", target], check=True, capture_output=True, text=True).stdout
    for line in text.splitlines():
        words = shlex.split(line)
        if not words or Path(words[0]).name not in ("clang", "clang-cl") or "-c" not in words:
            continue
        source = next((w for w in words if w.endswith(".c") and not w.startswith("-")), None)
        if source and "/third_party/" not in source:
            yield source, words


def to_ir(job):
    source, words, out = job
    command, skip = [], False
    for word in words:
        if skip:
            skip = False
            continue
        if word in ("-MF", "-o"):
            skip = True
            continue
        if word in ("-MMD", "-c") or word.startswith("-flto"):
            continue
        command.append(word)
    ll = out / (source.replace("/", "__") + ".ll")
    result = subprocess.run(command + ["-S", "-emit-llvm", "-o", str(ll)], capture_output=True, text=True)
    return ll if result.returncode == 0 else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", default="build/vita/halo.elf")
    parser.add_argument("--out", type=Path, help="where the .ll files go (default: a temporary folder)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    temporary = None
    out = args.out
    if not out:
        temporary = tempfile.TemporaryDirectory(prefix="vita_abi_check.")
        out = Path(temporary.name)
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(source, words, out) for source, words in compile_commands(args.target)]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(to_ir, jobs))
    files = [ll for ll in results if ll]
    print(f"vita_abi_check: {len(files)} of {len(jobs)} units compiled to IR", file=sys.stderr)
    failed = [job[0] for job, ll in zip(jobs, results) if not ll]
    if failed:
        # (a unit left out could hide a call: the check is not done)
        print("vita_abi_check: these did not compile: " + " ".join(failed), file=sys.stderr)
        return 2
    report = subprocess.run([sys.executable, str(HERE / "android_abi_check.py"), *map(str, files)],
                            capture_output=True, text=True)
    # (android_abi_check.py's report: "UNSAFE <name>", then its definition's
    # and the declaration's lines)
    blocks, current = [], None
    for line in report.stdout.splitlines():
        if line.startswith(("UNSAFE ", "benign ")):
            current = [line]
            blocks.append(current)
        elif line.startswith("    ") and current is not None:
            current.append(line)
    allowed = set()
    if ALLOW.is_file():
        allowed = {line.split("#")[0].strip() for line in ALLOW.read_text().splitlines()} - {""}
    new = 0
    for block in blocks:
        name = block[0].split()[1]
        listed = name in allowed
        new += 0 if listed else 1
        print(("  (allowed) " if listed else "! ") + "\n    ".join(block))
    if temporary:
        temporary.cleanup()
    print(f"vita_abi_check: {len(blocks)} unsafe declarations, {new} not in {ALLOW.name}", file=sys.stderr)
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
