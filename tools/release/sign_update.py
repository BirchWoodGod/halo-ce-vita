#!/usr/bin/env python3
"""Sign a release's halo.vpk for the Vita's in-game updater (the owner, on
their own machine, after building the release; tools/release/README.md).

  python3 tools/release/sign_update.py --key ~/halo-update-key/halo-update.key \\
      --vpk build/vita/halo.vpk --version 1.1.0-beta.4 --out release-files/

Writes halo-update.txt (the manifest: version, title, file, size, SHA-512)
and halo-update.txt.sig (its Ed25519 signature, hexadecimal) into --out,
then checks them as the game does. Upload both beside halo.vpk to the
release tagged v<version>; the game refuses the update without them, or if
halo.vpk is not the file they name.

Refuses a version that is not a release tag's (x.y.z or x.y.z-beta.n), a VPK
that is not one (no eboot.bin, no param.sfo) or is larger than 64 MB, a
title other than the one in the VPK, and warns when the version is not the
source's (vita_version.h) or the key is not the one in vita_update_key.h.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", required=True, help="the private key file (make_update_key.py)")
    parser.add_argument("--vpk", required=True, help="the release's halo.vpk")
    parser.add_argument("--version", required=True, help="the release's version: its tag without the v")
    parser.add_argument("--out", required=True, help="the folder for halo-update.txt and halo-update.txt.sig")
    parser.add_argument("--title", help="the title ID (default: the VPK's param.sfo's)")
    arguments = parser.parse_args()
    common.ed25519.self_test()
    version = arguments.version[1:] if arguments.version.startswith("v") else arguments.version
    if not common.TAG.fullmatch(version):
        common.fail(f"{version} is not a release version (x.y.z or x.y.z-beta.n)")
    vpk = Path(arguments.vpk)
    title = common.vpk_title(vpk)
    if not title or not common.TITLE.fullmatch(title):
        common.fail(f"{vpk}'s param.sfo has no usable TITLE_ID ({title})")
    if arguments.title and arguments.title != title:
        common.fail(f"the VPK's title is {title}, not {arguments.title}")
    size, sha512 = common.file_digest(vpk)
    if not 0 < size <= common.VPK_MAXIMUM:
        common.fail(f"{vpk} is {size} bytes: the game takes 1 byte to 64 MB")
    seed = common.read_private_key(arguments.key)
    public = common.ed25519.public_key(seed).hex()
    source_key = common.source_public_key()
    if source_key == common.PLACEHOLDER:
        print("warning: vita_update_key.h still holds the placeholder: builds from this source refuse updates",
              file=sys.stderr)
    elif source_key and source_key != public:
        print(f"warning: this key ({public[:16]}...) is not vita_update_key.h's ({source_key[:16]}...)",
              file=sys.stderr)
    source_version = common.source_version()
    if source_version and source_version != version:
        print(f"warning: the source's version is {source_version}, not {version}: is the VPK this release's?",
              file=sys.stderr)
    text = common.manifest_text(version, title, size, sha512).encode("ascii")
    signature = common.ed25519.sign(seed, text)
    if not common.ed25519.verify(bytes.fromhex(public), text, signature):
        common.fail("the signature does not verify (this should not happen)")
    out = Path(arguments.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / common.MANIFEST).write_bytes(text)
    (out / common.SIGNATURE).write_text(signature.hex() + "\n", encoding="ascii")
    print(text.decode("ascii"), end="")
    print(f"signed by {public}")
    print(f"upload {out / common.MANIFEST}, {out / common.SIGNATURE} and {vpk} (as {common.VPK}) to the release v{version}")


if __name__ == "__main__":
    main()
