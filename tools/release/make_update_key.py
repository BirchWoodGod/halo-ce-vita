#!/usr/bin/env python3
"""Make the Vita updater's signing key, once (the owner, on their own
machine; tools/release/README.md).

  python3 tools/release/make_update_key.py --private ~/halo-update-key/halo-update.key

Writes a new Ed25519 private key to the file given (made with mode 600; an
existing file is never overwritten; a path inside a git working tree is
refused) and prints its public key, to paste into
port/vita/include/vita_update_key.h. Keep the private key file offline and
backed up (a lost key means players cannot update in-game to builds signed
with a new one: they install that build with VitaShell once); anyone who
has it can sign a build every updating Vita will install.
"""

import argparse
import os
import secrets
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--private", required=True, help="the private key file to make (outside any git tree)")
    parser.add_argument("--allow-git", action="store_true", help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    path = Path(arguments.private).expanduser()
    common.ed25519.self_test()
    if path.exists():
        common.fail(f"{path} exists: a key is never overwritten")
    if common.inside_git(path) and not arguments.allow_git:
        common.fail(f"{path} is inside a git working tree: keep the private key out of every repository")
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    seed = secrets.token_bytes(32)
    public = common.ed25519.public_key(seed).hex()
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="ascii") as file:
        file.write("# Halo CE for PS Vita: the updater's Ed25519 private key (seed).\n")
        file.write("# KEEP OFFLINE. Never commit, upload or put in CI. Back it up somewhere safe.\n")
        file.write(f"# public key: {public}\n")
        file.write(seed.hex() + "\n")
    print(f"private key written to {path} (mode 600). Keep it offline.")
    print()
    print("public key (paste into port/vita/include/vita_update_key.h, in place of the 64 zeros):")
    print()
    print(f'#define HALO_UPDATE_PUBLIC_KEY "{public}"')


if __name__ == "__main__":
    main()
