# Releases and the in-game update download's signatures

From 1.1.0-beta.4 the Vita can download a newer version from the settings
panel (General > Check for updates, then press it again). It keeps only a
`halo.vpk` whose manifest is signed by **the owner's Ed25519 key**, in
`ux0:data/haloce-vita/update/halo-<version>.vpk`, for the player to install
with VitaShell (the game is a safe homebrew, `vita-make-fself -s`: it cannot
write its own folder, so it installs nothing itself). The game holds the
public half of the key (`port/vita/include/vita_update_key.h`). The private
key never goes into git, GitHub, CI or anyone else's hands: whoever holds it
can make every updating Vita offer their file as the next Halo.

## Release files

From 1.1.1 a release's files are named for the project, the version and
the platform:

| asset | what | made by |
| --- | --- | --- |
| `halo-ce-vita-<version>-vita.vpk` | the Vita's game | `ninja vita`, then `sign_update.py` copies it under this name |
| `halo-ce-vita-<version>-pc-windows.zip` | the PC build, Windows | `tools/package_pc.py windows` (CI's Windows job, artifact `halo-ce-vita-pc-windows`) |
| `halo-ce-vita-<version>-pc-linux.tar.gz` | the PC build, Linux | `tools/package_pc.py linux` (CI's Linux job, artifact `halo-ce-vita-pc-linux`) |
| `halo-update.txt` | the Vita updater's manifest: version, title ID, file, size, SHA-512 of the VPK | `sign_update.py` |
| `halo-update.txt.sig` | the manifest's Ed25519 signature (128 hex digits) | `sign_update.py` |
| `halo.vpk` | the same VPK under its old name, **1.1.1 releases only** | `sign_update.py --legacy-name` |

The Vita's updater (from 1.1.1) downloads the file the signed manifest
names, `halo-ce-vita-<version>-vita.vpk` or `halo.vpk`, and no other. The
builds before 1.1.1 ask for `halo.vpk` alone and refuse a manifest naming
anything else, so the 1.1.1 releases (betas included) are signed with
`--legacy-name`: the manifest names `halo.vpk`, uploaded beside the new
name. From 1.1.2 on, sign without it (a 1.1.0 Vita then gets "the manifest
names another file" and installs by hand, as before the updater).

The PC build is packed from a build configured with `--linux-net-vita` (it
plays with the Vitas; `package_pc.py` refuses one without it):

```
python3 configure.py --lto off --pgo off --portable --release --linux-net-vita
ninja linux
python3 tools/package_pc.py linux       # dist/halo-ce-vita-<version>-pc-linux.tar.gz
```

The PC files are not signed for an updater: the PC build never downloads
itself (port/linux/README.md, "Updates").

A release without the manifest and signature still installs with VitaShell
as before; the in-game download refuses it ("the server answered 404").

## Once: make the key

On your own machine (not a server, not CI), outside every git checkout:

```
python3 tools/release/make_update_key.py --private ~/halo-update-key/halo-update.key
```

It writes the private key (mode 600; it never overwrites a file and refuses
a path inside a git working tree) and prints a line such as

```
#define HALO_UPDATE_PUBLIC_KEY "1a16ebc3...0fe4"
```

Paste that into `port/vita/include/vita_update_key.h` in place of the 64
zeros (the placeholder) and commit that change: the public key is not a
secret. Until then every build refuses to download updates ("no update key
in this build") and the panel points players at the releases page.

Keep the private key file:

- offline when not signing (an encrypted USB stick, or an encrypted disk);
- backed up (a second copy, also offline). If it is lost, make a new key and
  ship its public key in a release; players install that one from the
  releases page, and download in-game again from then on;
- never in a repository, a cloud folder, a chat, an issue, or a CI secret.
  `*.key` is in `.gitignore` as a safety net only.

If the key is ever exposed: make a new one, ship a release with the new
public key, tell players to get that release from the releases page (a
build trusts only its own key), and say so in the release notes.

The tools need only Python 3 (Ed25519 is in `ed25519_rfc8032.py`, RFC 8032's
reference code, checked against RFC 8032's vectors before each use and
against the game's Monocypher by `port/vita/tests/run_update_download_test.sh`).

## Each release: sign it

1. Build the release as usual (`python3 configure.py --lto off --pgo off
   --portable --release`, `ninja vita`) with the version in
   `port/vita/include/vita_version.h` and the real public key in
   `vita_update_key.h` - **without** any of the test options below.
2. Sign the exact VPK you will upload, on your machine, with the key
   plugged in (`--legacy-name` for the 1.1.1 releases: "Release files"):

   ```
   python3 tools/release/sign_update.py --key ~/halo-update-key/halo-update.key \
       --vpk build/vita/halo.vpk --version 1.1.1-beta.1 --out release-files/ --legacy-name
   ```

   `release-files/` then holds `halo-ce-vita-1.1.1-beta.1-vita.vpk`
   (and `halo.vpk`, with `--legacy-name`), `halo-update.txt` and
   `halo-update.txt.sig`.

   It refuses a version that is not a tag's (`x.y.z`, `x.y.z-beta.n` or
   `x.y.z-rc.n`), a file that is not a VPK or is over 64 MB, and a test
   build of this tree (unless `--test-build`); it warns if the version is
   not the source's or the key is not the one in `vita_update_key.h`.
3. Check what you will upload (optional, the same checks the Vita makes):

   ```
   python3 tools/release/verify_update.py --dir release-files/ --version 1.1.1-beta.1
   ```

4. Unplug the key. Upload everything in `release-files/`, and the PC
   files, to the release tagged `v1.1.1-beta.1` (the tag must
   be the version with a `v`: the game asks for exactly that tag's files and
   refuses a manifest naming another version, or one not newer than the
   running build).

Do not re-upload a different VPK without signing again: the Vita
refuses a file whose SHA-512 is not the manifest's.

## CI

CI never signs and never holds the private key. GitHub Actions builds the
desktop ports; Vita releases are built and signed on the owner's machine.
If a workflow is ever added that publishes Vita releases, it publishes the
two signature files the owner made locally, never a key.

## What the Vita checks (port/vita/host/vita_update_verify.c, vita_update_download.c)

- A release build downloads only from
  `https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v<version>/`
  (a fixed URL, the version a release tag's) and asks only that
  repository's releases API; it follows GitHub's redirect only to
  `release-assets.githubusercontent.com` or `objects.githubusercontent.com`,
  over TLS checked against the authorities built into the game
  (`port/linux/src/update_roots.h`).
- The manifest's signature first, with the built-in key; then its form, that
  its version is the tag's and newer than the running build, the title
  (`HCEV00001`), the file (`halo-ce-vita-<version>-vita.vpk`, or `halo.vpk`;
  before 1.1.1 `halo.vpk` alone) and a size of at most 64 MB.
- The card's free space; `halo.vpk` downloaded to that size at most (resumed
  if cut), its size and SHA-512 the manifest's. Only then is it kept, as
  `update/halo-<version>.vpk` beside its signed manifest, older kept ones
  removed. A refusal keeps nothing of it.
- At each start, a kept VPK is checked again (signature, version newer than
  the running build, SHA-512) before the panel and the main menu point at
  it; one that is no newer (installed) or fails is removed.

## Test builds (never released)

`configure.py` has three options for test builds only; with none of them
the build is a release build:

| option | what |
| --- | --- |
| `--vita-update-test-sources` | defines `HALO_UPDATE_TEST_SOURCES`: the build takes `HALO_UPDATE_URL` (the releases list: `https://api.github.com/...` or a loopback `http://127.x`) and `HALO_UPDATE_DOWNLOAD_BASE` (`https://github.com/<owner>/<repo>/releases/download` or a loopback `http://127.x`) from `env.txt`; a release build ignores both and logs it |
| `--vita-update-key HEX` | the build's public key, in place of `vita_update_key.h`'s |
| `--vita-version VERSION` | the version the build says it is, in place of `vita_version.h`'s: the Vita's and the PC builds' (Linux, Windows) alike; `tools/package_pc.py` names its files for it |

`build.ninja` then says "TEST BUILD of the updater", and `sign_update.py`
refuses that tree's `build/vita/halo.vpk` without `--test-build`. Files a
test build downloads must still be signed by its key.

### Making test builds A (1.1.0-beta.3) and B (1.1.0-beta.4)

With a throwaway key (`python3 tools/release/make_update_key.py --private
~/halo-test-key/test.key`; the public key is on its `# public key:` line):

```
KEY=<the test public key, 64 hex digits>
python3 configure.py --lto off --pgo off --portable --release \
    --vita-update-test-sources --vita-update-key $KEY --vita-version 1.1.0-beta.3
ninja vita && cp build/vita/halo.vpk A.vpk
python3 configure.py --lto off --pgo off --portable --release \
    --vita-update-test-sources --vita-update-key $KEY --vita-version 1.1.0-beta.4
ninja vita && cp build/vita/halo.vpk B.vpk
python3 tools/release/sign_update.py --key ~/halo-test-key/test.key --vpk B.vpk \
    --version 1.1.0-beta.4 --out B-files/
python3 configure.py --lto off --pgo off --portable --release   # back to a release configuration
```

(`B.vpk` is a copy outside `build/`, so `--test-build` is not needed.)

### Testing on a Vita without touching the public releases

1. Create a throwaway **public** repository, say `BirchWoodGod/halo-update-test`
   (release downloads of a private repository need a token, which the game
   never sends), and a pre-release `v1.1.0-beta.4` in it with `B.vpk`
   uploaded as `halo.vpk`, `B-files/halo-update.txt` and
   `B-files/halo-update.txt.sig`.
2. Install `A.vpk` with VitaShell. Add to `ux0:data/haloce-vita/env.txt`:

   ```
   HALO_UPDATE_URL=https://api.github.com/repos/BirchWoodGod/halo-update-test/releases?per_page=6
   HALO_UPDATE_DOWNLOAD_BASE=https://github.com/BirchWoodGod/halo-update-test/releases/download
   ```

3. Run the checklist below, then remove the two lines, install the real
   release with VitaShell, and delete the test repository and the test key.

### Hardware checklist

| # | do | expect (screen; `halo.log`) |
| --- | --- | --- |
| 1 | start A; General > Check for updates | "Update available: 1.1.0-beta.4", "Press again to download it" in amber |
| 2 | press it, Cross, Circle while "Downloading" shows | "Stopped. Nothing was kept."; `ux0:data/haloce-vita/update/` empty |
| 3 | press again, Cross; turn Wi-Fi off halfway | a message (connection); `update/halo.vpk.part` kept; Wi-Fi on, press again: it resumes (`trying again`, or a shorter download) |
| 4 | press again, Cross, let it finish (also try it during a campaign level: frame time should not change) | progress: manifest, MB, checking; "1.1.0-beta.4 downloaded and verified. Install it with VitaShell: ux0:data/haloce-vita/update/halo-1.1.0-beta.4.vpk"; `update: manifest for 1.1.0-beta.4 signed by the update key`, `halo.vpk matches its signed SHA-512`, `downloaded and verified: install ... with VitaShell` |
| 5 | look at the panel and the main menu; restart A | the panel: "VitaShell: ux0:data/haloce-vita/update/" and "halo-1.1.0-beta.4.vpk (verified, to install)"; the main menu's corner "To install: 1.1.0-beta.4"; after the restart the same (`update: 1.1.0-beta.4 is downloaded and verified` at start) |
| 6 | press Check for updates twice | the message with the path again; nothing downloaded again |
| 7 | install that VPK with VitaShell, start the game | General shows 1.1.0-beta.4; at start `halo-1.1.0-beta.4.vpk is no newer than this build (installed): removed`; `update/` empty; saves, settings, maps as before |
| 8 | reinstall A; in the test release replace `halo.vpk` with another file (do not sign again); download | "refused or failed: the download is not the file the manifest signs. Nothing was kept." |
| 9 | reinstall A, download B again; with VitaShell append a byte to the kept VPK; restart | at start `failed its check ... removed`; no "To install" line |
| 10 | a release build (no test options) with the two `env.txt` lines | `update: HALO_UPDATE_URL is for test builds: GitHub's is asked`; it looks at BirchWoodGod/halo-ce-vita only |
| 11 | a build with the placeholder key | the download refused: "This build cannot check updates (it has no update key)..." |

What these settle, which the Linux tests cannot: the Vita's TLS to
`release-assets.githubusercontent.com` (Let's Encrypt; the ISRG roots added
to `update_roots.h`), that a safe homebrew writes `ux0:data/haloce-vita/update`
and VitaShell installs the file from there, that the download costs the game
no frames, and how long each stage takes on the Vita.
