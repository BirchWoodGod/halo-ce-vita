# Releases and the in-game updater's signatures

From 1.1.0-beta.4 the Vita can install a newer version from the settings
panel (General > Check for updates, then press it again). It installs only a
`halo.vpk` whose manifest is signed by **the owner's Ed25519 key**; the game
holds the public half (`port/vita/include/vita_update_key.h`). The private
key never goes into git, GitHub, CI or anyone else's hands: whoever holds it
can make every updating Vita run their code.

A release that players can install in-game carries three assets:

| asset | what |
| --- | --- |
| `halo.vpk` | the game, as before |
| `halo-update.txt` | the manifest: version, title ID, file, size, SHA-512 of `halo.vpk` |
| `halo-update.txt.sig` | the manifest's Ed25519 signature (128 hex digits) |

A release without the two extra files still works as before for VitaShell
installs; the in-game updater refuses it ("the server answered 404").

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
secret. Until then every build refuses to update itself ("no update key in
this build") and the panel points players at the releases page.

Keep the private key file:

- offline when not signing (an encrypted USB stick, or an encrypted disk);
- backed up (a second copy, also offline). If it is lost, make a new key,
  ship its public key in a release, and tell players to install that one
  release with VitaShell; from then on they update in-game again;
- never in a repository, a cloud folder, a chat, an issue, or a CI secret.
  `*.key` is in `.gitignore` as a safety net only.

If the key is ever exposed: make a new one, ship a release with the new
public key (players install it with VitaShell, since a build only trusts its
own key), and say so in the release notes.

The tools need only Python 3 (Ed25519 is in `ed25519_rfc8032.py`, RFC 8032's
reference code, checked against RFC 8032's vectors before each use and
against the game's Monocypher by `port/vita/tests/run_update_install_test.sh`).

## Each release: sign it

1. Build the release as usual (`ninja vita`) with the version in
   `port/vita/include/vita_version.h` and the real public key in
   `vita_update_key.h`.
2. Sign the exact `halo.vpk` you will upload, on your machine, with the key
   plugged in:

   ```
   python3 tools/release/sign_update.py --key ~/halo-update-key/halo-update.key \
       --vpk build/vita/halo.vpk --version 1.1.0-beta.4 --out release-files/
   ```

   It refuses a version that is not a tag's (`x.y.z` or `x.y.z-beta.n`), a
   file that is not a VPK or is over 64 MB, and warns if the version is not
   the source's or the key is not the one in `vita_update_key.h`.
3. Check what you will upload (optional, the same checks the Vita makes):

   ```
   cp build/vita/halo.vpk release-files/
   python3 tools/release/verify_update.py --dir release-files/ --version 1.1.0-beta.4
   ```

4. Unplug the key. Upload `halo.vpk`, `halo-update.txt` and
   `halo-update.txt.sig` to the release tagged `v1.1.0-beta.4` (the tag must
   be the version with a `v`: the game asks for exactly that tag's files and
   refuses a manifest naming another version, or one not newer than the
   running build).

Do not re-upload a different `halo.vpk` without signing again: the Vita
refuses a file whose SHA-512 is not the manifest's.

## CI

CI never signs and never holds the private key. GitHub Actions builds the
desktop ports; Vita releases are built and signed on the owner's machine.
If a workflow is ever added that publishes Vita releases, it publishes the
two signature files the owner made locally (for example as a draft release's
assets the owner uploads), never a key.

## What the Vita checks (port/vita/host/vita_update_verify.c, vita_update_install.c)

- It downloads only from
  `https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v<version>/`
  (a fixed URL, the version a release tag's), follows GitHub's redirect only
  to `release-assets.githubusercontent.com` or `objects.githubusercontent.com`
  over TLS checked against the authorities built into the game
  (`port/linux/src/update_roots.h`).
- The manifest's signature first, with the built-in key; then its form, that
  its version is the tag's and newer than the running build, the title
  (`HCEV00001`), the file (`halo.vpk`) and a size of at most 64 MB.
- `halo.vpk` downloaded to that size at most (resumed if cut), its size and
  SHA-512 the manifest's; unpacked into `ux0:data/haloce-vita/update/stage`
  with every name checked (no `..`, no absolute path, nothing in
  `sce_sys/package` or `sce_pfs`), sizes and CRCs checked, and its
  `param.sfo`'s title the running game's.
- Then the game's files in `ux0:app/HCEV00001` are replaced with a journal
  (any failure, crash or power cut is undone at once or at the next start),
  and the game restarts into the new version. The data folder (saves,
  `settings.txt`, `config.toml`, maps, movies) is not touched.

Writing `ux0:app` needs HENkaku's **Enable Unsafe Homebrew**, as VitaShell
needs; the game says so if it is off.

## Testing on a Vita without touching the public releases

Builds take two debug settings from `ux0:data/haloce-vita/env.txt`, so a
**test build signed with a test key** can update itself from a throwaway
repository of the owner's, never from `BirchWoodGod/halo-ce-vita`:

- `HALO_UPDATE_URL`: the update check's releases list (already there:
  `https://api.github.com/...` or a loopback `http://127.x.x.x`);
- `HALO_UPDATE_DOWNLOAD_BASE`: where the three files are, only
  `https://github.com/<owner>/<repository>/releases/download` or a loopback
  `http://127.x.x.x...`. The files must still be signed by the key built into
  the game: with a release build these settings can only fetch releases
  signed by the real key.

Steps (the owner; nothing here is committed):

1. Create a throwaway **public** repository, say `BirchWoodGod/halo-update-test`
   (release downloads of a private repository need a token, which the game
   never sends).
2. Make a test key outside git:
   `python3 tools/release/make_update_key.py --private ~/halo-test-key/test.key`.
3. Paste its public key into `port/vita/include/vita_update_key.h` (local
   edit only). Build A: `ninja vita` as the tree is (`1.1.0-beta.3`); keep
   `build/vita/halo.vpk` as `A.vpk`. Build B: also set `HALO_VITA_VERSION`
   to `"1.1.0-beta.4"` in `vita_version.h` (local edit), `ninja vita`, keep
   it as `B.vpk`. Then `git checkout port/vita/include/vita_update_key.h
   port/vita/include/vita_version.h`.
4. Sign B with the test key:
   `python3 tools/release/sign_update.py --key ~/halo-test-key/test.key --vpk B.vpk --version 1.1.0-beta.4 --out B-files/`
   and create a release `v1.1.0-beta.4` (pre-release) in the test repository
   with `B.vpk` uploaded as `halo.vpk`, `halo-update.txt` and
   `halo-update.txt.sig`.
5. Install `A.vpk` with VitaShell. Add to `ux0:data/haloce-vita/env.txt`:

   ```
   HALO_UPDATE_URL=https://api.github.com/repos/BirchWoodGod/halo-update-test/releases?per_page=6
   HALO_UPDATE_DOWNLOAD_BASE=https://github.com/BirchWoodGod/halo-update-test/releases/download
   ```

6. Run the hardware checklist below, then remove the two lines, reinstall the
   real release with VitaShell, and delete the test repository and test key.

### Hardware checklist

| # | do | expect (screen; `halo.log`) |
| --- | --- | --- |
| 1 | HENkaku Settings: Enable Unsafe Homebrew **off**; start A; General > Check for updates, then press it again, Cross | message naming Enable Unsafe Homebrew; `update: refused: cannot write in ux0:app/HCEV00001 (unsafe homebrew is off)`; the game unchanged |
| 2 | turn it **on**, restart A, start a campaign level, open the panel, press Check for updates twice | "Updates install from the main menu..." |
| 3 | back at the main menu: Check for updates | "Update available: 1.1.0-beta.4" and "Press again to download and install it" in amber |
| 4 | press it, Cross, Circle while "Downloading" shows | "Stopped. Nothing was installed."; no `halo.vpk.part` in `ux0:data/haloce-vita/update/` |
| 5 | press again, Cross; turn Wi-Fi off halfway through the download | a message (connection); `halo.vpk.part` kept; turning Wi-Fi on and pressing again resumes (`trying again` or a shorter download) |
| 6 | press again, Cross, let it run | progress: manifest, MB, checking, unpacking, installing; "Restarting..."; the game comes back as 1.1.0-beta.4 (General shows the version); `update: ... signed by the update key`, `matches its signed SHA-512`, `installed (N files)`, `restarting into it` (in `halo-prev.log` after the restart) |
| 7 | after 6 | saves, `settings.txt`, `config.toml`, `maps/`, `movies/` as before; `ux0:data/haloce-vita/update/` empty; the LiveArea bubble starts the game; `ux0:app/HCEV00001/sce_sys/package/` untouched (VitaShell) |
| 8 | reinstall A; in the test release, replace `halo.vpk` with another file (do not sign again); update | "refused or failed: the download is not the file the manifest signs"; A unchanged |
| 9 | (optional, a small risk to the card) during "Installing", hold the power button to turn the Vita off | at the next start `update: an install cut short was undone (...)`; the game is A and plays |
| 10 | a build with the placeholder key (the tree as committed before the real key is pasted) | "This build cannot install updates itself (it has no update key)..." |

What these settle, which the Linux tests cannot: that HENkaku's safe mode
refuses the write probe in `ux0:app` (and unsafe mode allows it), that the
running game's files can be renamed or overwritten, that
`sceAppMgrLoadExec("app0:eboot.bin")` starts the new eboot, the Vita's TLS to
`release-assets.githubusercontent.com` (Let's Encrypt, ISRG roots added to
`update_roots.h`), and the time each stage takes on the Vita.
