#!/usr/bin/env python3
"""Sign a release's VPK for the Vita's in-game updater, and its PC builds for
the installer's download (the owner, on their own machine, after building the
release; tools/release/README.md).

  python3 tools/release/sign_update.py --key ~/halo-update-key/halo-update.key \\
      --vpk build/vita/halo.vpk --version 1.1.1-beta.1 --out release-files/ [--legacy-name] \\
      [--pc-windows dist/halo-ce-vita-1.1.1-beta.1-pc-windows.zip] \\
      [--pc-linux dist/halo-ce-vita-1.1.1-beta.1-pc-linux.tar.gz]

Copies the VPK into --out as halo-ce-vita-<version>-vita.vpk, the release's
name for it, and writes halo-update.txt (the manifest: version, title, file,
size, SHA-512) and halo-update.txt.sig (its Ed25519 signature, hexadecimal)
beside it, then checks them as the game does. Upload everything in --out to
the release tagged v<version>; the game refuses the update without them, or
if the VPK is not the file they name.

--legacy-name (the 1.1.1 releases, betas included): the manifest names
halo.vpk, the name the builds before 1.1.1 download, and --out gets the VPK
under that name too; builds from 1.1.1 take either name.

Refuses a version that is not a release tag's (x.y.z, x.y.z-beta.n or x.y.z-rc.n), a VPK
that is not one (no eboot.bin, no param.sfo) or is larger than 64 MB, a
title other than the one in the VPK, and warns when the version is not the
source's (vita_version.h) or the key is not the one in vita_update_key.h.

--pc-windows, --pc-linux (either, both; without --vpk they alone are signed):
copies each archive into --out under the release's name and writes
halo-update-pc.txt (version, then each archive's file, size and SHA-512) and
halo-update-pc.txt.sig, signed with the same key. Setup's PC download refuses
a release without them. Refuses a file that is not this system's PC build or
is larger than 1 GB.
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_signing as common  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", required=True, help="the private key file (make_update_key.py)")
    parser.add_argument("--vpk", help="the release's halo.vpk")
    parser.add_argument("--pc-windows", help="the release's Windows PC build (tools/package_pc.py windows)")
    parser.add_argument("--pc-linux", help="the release's Linux PC build (tools/package_pc.py linux)")
    parser.add_argument("--version", required=True, help="the release's version: its tag without the v")
    parser.add_argument("--out", required=True, help="the folder for halo-update.txt and halo-update.txt.sig")
    parser.add_argument("--title", help="the title ID (default: the VPK's param.sfo's)")
    parser.add_argument("--legacy-name", action="store_true",
                        help="the manifest names halo.vpk (what builds before 1.1.1 download), uploaded beside "
                        "halo-ce-vita-<version>-vita.vpk")
    parser.add_argument("--test-build", action="store_true",
                        help="sign a TEST build (configure.py --vita-update-test-sources/--vita-update-key/--vita-version)")
    arguments = parser.parse_args()
    common.ed25519.self_test()
    version = arguments.version[1:] if arguments.version.startswith("v") else arguments.version
    if not common.TAG.fullmatch(version):
        common.fail(f"{version} is not a release version (x.y.z, x.y.z-beta.n or x.y.z-rc.n)")
    if not arguments.vpk and not arguments.pc_windows and not arguments.pc_linux:
        common.fail("nothing to sign: give --vpk, --pc-windows or --pc-linux")
    # (everything checked before the key is read)
    pc_rows = []
    for system, path in (("windows", arguments.pc_windows), ("linux", arguments.pc_linux)):
        if path:
            common.check_pc_archive(path, system)
            size, sha512 = common.file_digest(path)
            if not 0 < size <= common.PC_MAXIMUM:
                common.fail(f"{path} is {size} bytes: Setup takes 1 byte to 1 GB")
            pc_rows.append((system, Path(path), size, sha512))
    vpk = Path(arguments.vpk) if arguments.vpk else None
    title = size = sha512 = None
    if vpk:
        # (a test build of this tree is never signed as a release by mistake)
        ninja = common.REPOSITORY / "build.ninja"
        if (vpk.resolve() == (common.REPOSITORY / "build/vita/halo.vpk").resolve() and ninja.is_file() and
                "TEST BUILD of the updater" in ninja.read_text(encoding="utf-8", errors="replace") and
                not arguments.test_build):
            common.fail(f"{vpk} is a TEST build (configure.py's --vita-update-* / --vita-version): reconfigure without "
                        "them for a release, or pass --test-build to sign it for a test")
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
    out = Path(arguments.out)
    out.mkdir(parents=True, exist_ok=True)
    uploads = []
    if vpk:
        file = common.LEGACY_VPK if arguments.legacy_name else common.vpk_name(version)
        text = common.manifest_text(version, title, size, sha512, file).encode("ascii")
        write_signed(out, common.MANIFEST, common.SIGNATURE, text, seed, public)
        # (the VPK under the release's names: its own, and halo.vpk too when the
        # manifest names that)
        names = [common.vpk_name(version)] + ([common.LEGACY_VPK] if arguments.legacy_name else [])
        for name in names:
            if (out / name).resolve() != vpk.resolve():
                shutil.copyfile(vpk, out / name)
        print(text.decode("ascii"), end="")
        uploads += names + [common.MANIFEST, common.SIGNATURE]
    if pc_rows:
        rows = [(system, common.pc_name(version, system), size, sha512) for system, _, size, sha512 in pc_rows]
        text = common.pc_manifest_text(version, rows).encode("ascii")
        write_signed(out, common.PC_MANIFEST, common.PC_SIGNATURE, text, seed, public)
        for (_, path, _, _), (_, name, _, _) in zip(pc_rows, rows):
            if (out / name).resolve() != path.resolve():
                shutil.copyfile(path, out / name)
        print(text.decode("ascii"), end="")
        uploads += [name for _, name, _, _ in rows] + [common.PC_MANIFEST, common.PC_SIGNATURE]
    print(f"signed by {public}")
    print(f"upload {', '.join(uploads)} (all in {out}) to the release v{version}")


def write_signed(out, manifest, signature_file, text, seed, public):
    """a manifest and its signature file in out, checked as written"""
    signature = common.ed25519.sign(seed, text)
    if not common.ed25519.verify(bytes.fromhex(public), text, signature):
        common.fail("the signature does not verify (this should not happen)")
    (out / manifest).write_bytes(text)
    (out / signature_file).write_text(signature.hex() + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
