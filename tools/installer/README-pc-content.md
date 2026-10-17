# Retail PC content staging (experimental)

This optional command-line extension stages your own Xbox and retail Halo PC
files. It includes no game data and downloads nothing. Run it from this source
checkout with Python 3.9 or later; it needs only the standard library.

```powershell
python tools/installer/halo_pc_content.py --xbox "Xbox-Halo.iso" --pc "Halo-PC/Maps" --menu "Halo-CE/Maps" --out "New-Vita-PC-files" --campaign
```

- `--xbox`: your Xbox ISO/XISO or extracted game folder. Supplies the Xbox
  executable and menu, and campaign maps when `--campaign` is omitted.
- `--pc`: your installed **retail** Halo PC Maps directory, its parent game
  directory, or a plain ISO9660 image containing loose version-7 maps. The
  nineteen multiplayer maps and retail bitmap/sound resources are required.
  Custom Edition maps, MCC-native maps and renamed maps are rejected here.
- `--menu`: your Custom Edition resource-map directory (including MCC's
  `halo1/maps/custom_edition`) or your CE installer. Existing CE extraction
  code reads a cabinet; it never executes the installer.
- `--out`: a **new** directory. Existing staging folders and installations are
  refused. A failure leaves an incomplete marker and blocks upload; use a new
  directory after fixing the input.
- `--campaign`: also stage the ten retail PC campaign maps. This is experimental;
  opening-level operation does not establish complete campaign compatibility.

Packed retail CAB/MSI install discs, UDF-only images, split BIN/CUE images,
Joliet-only layouts and multi-extent ISO files are unsupported: use installed
retail files instead. Setup, autorun, executables and scripts on the selected
media are never launched; the ISO is not mounted. No DRM bypass is provided.

The command keeps retail gameplay resources at `maps/`, CE menu bitmaps and
strings at `maps/menu_resources/`, and extracts six original map previews locally
as BMPs. The Xbox `ui.map` remains the menu map. A local JSON manifest records
each output's size and SHA-256. Inputs are read-only; PC copies are re-read and
hash checked before atomic replacement inside the fresh staging directory.

Back up the Vita's data and saves before replacing an installed layout. Inspect
the staging manifest, open VitaShell FTP on a trusted local network, then use
the existing upload command:

```powershell
python tools/installer/halo_ce_vita_installer.py --out "New-Vita-PC-files" upload 192.168.1.20 --replace
```

This targets the upstream game's `ux0:data/haloce-vita`. `--replace` explicitly
replaces matching target filenames, including stock multiplayer maps with their
PC versions. It does not back up the device. Do not point it at a personal fork
with a different data directory. FTP is unencrypted; no automatic upload occurs
while staging. The upstream uploader checks sizes, not remote full-file hashes.

Install the matching rebuilt VPK using VitaShell. Enable **PC maps**, then use
**Edit Gametypes → Item Options** for **All+PC** or PC weapon additions. Keep the
staging directory private: it contains your game files and derived previews.

The ISO/folder reader and its synthetic tests adapt the CC0 source in
[JackTulli/halo-64 at 45a76cdf](https://github.com/JackTulli/halo-64/tree/45a76cdfae43782835ce64cc4947e4178caaecd6).
Its notice is retained in `pc_map_source.py` and its license in
`licenses/PC_INPUT_CC0.txt`. The Vita orchestration and existing installer use
the repository's GPL-3.0-only license. No new production Python dependencies,
converter binaries, codecs or fonts are bundled.
