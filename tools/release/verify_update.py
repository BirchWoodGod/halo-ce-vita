#!/usr/bin/env python3
"""Check a release's update files as the Vita does (before uploading them,
or after downloading them back from the release).

  python3 tools/release/verify_update.py --dir release-files/ --version 1.1.0-beta.4 [--public HEX]

--dir holds the VPK the manifest names (halo-ce-vita-<version>-vita.vpk, or
halo.vpk), halo-update.txt and halo-update.txt.sig; the public key is
vita_update_key.h's unless --public gives one. Exit status 0 when the
signature is the key's, the manifest names this version and title and one
of those two names, and that file's size and SHA-512 are the manifest's.

--pc checks the PC builds' files instead: halo-update-pc.txt, its signature,
and each archive it names, as Setup does.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--public", help="the public key, 64 hexadecimal digits (default: vita_update_key.h's)")
    parser.add_argument("--pc", action="store_true", help="the PC builds' manifest, not the Vita's")
    arguments = parser.parse_args()
    folder = Path(arguments.dir)
    version = arguments.version[1:] if arguments.version.startswith("v") else arguments.version
    public = (arguments.public or common.source_public_key() or "").lower()
    if len(public) != 64 or public == common.PLACEHOLDER:
        common.fail("no public key (vita_update_key.h holds the placeholder): give --public")
    text = (folder / (common.PC_MANIFEST if arguments.pc else common.MANIFEST)).read_bytes()
    signature_text = (folder / (common.PC_SIGNATURE if arguments.pc else common.SIGNATURE)).read_text(
        encoding="ascii").rstrip("\r\n")
    try:
        signature = bytes.fromhex(signature_text)
    except ValueError:
        common.fail("the signature file is not hexadecimal")
    if not common.ed25519.verify(bytes.fromhex(public), text, signature):
        common.fail("the manifest's signature is not this key's")
    if arguments.pc:
        verify_pc(folder, text, version, public)
        return
    fields = common.parse_manifest(text.decode("ascii", "replace"))
    if not fields:
        common.fail("the manifest is malformed")
    if fields["version"] != version:
        common.fail(f"the manifest is for {fields['version']}, not {version}")
    if fields["file"] not in (common.vpk_name(version), common.LEGACY_VPK):
        common.fail(f"the manifest names {fields['file']}, not {common.vpk_name(version)} or {common.LEGACY_VPK}")
    vpk = folder / fields["file"]
    if not vpk.is_file():
        common.fail(f"{vpk} is missing (the file the manifest names)")
    size, sha512 = common.file_digest(vpk)
    if int(fields["size"]) != size or fields["sha512"] != sha512:
        common.fail(f"{fields['file']} is not the file the manifest signs")
    title = common.vpk_title(vpk)
    if title != fields["title"]:
        common.fail(f"the VPK's title {title} is not the manifest's {fields['title']}")
    print(f"OK: {version}, {title}, {size} bytes, signed by {public}")


def verify_pc(folder, text, version, public):
    """the PC builds' manifest, its signature already checked"""
    parsed = common.parse_pc_manifest(text.decode("ascii", "replace"))
    if not parsed:
        common.fail("the PC manifest is malformed")
    manifest_version, rows = parsed
    if manifest_version != version:
        common.fail(f"the PC manifest is for {manifest_version}, not {version}")
    for system, (file, size, sha512) in rows.items():
        if file != common.pc_name(version, system):
            common.fail(f"the PC manifest names {file}, not {common.pc_name(version, system)}")
        if not (folder / file).is_file():
            common.fail(f"{folder / file} is missing (a file the PC manifest names)")
        if common.file_digest(folder / file) != (size, sha512):
            common.fail(f"{file} is not the file the PC manifest signs")
        common.check_pc_archive(folder / file, system)
    print(f"OK: {version}, PC builds {', '.join(rows)}, signed by {public}")


if __name__ == "__main__":
    main()
