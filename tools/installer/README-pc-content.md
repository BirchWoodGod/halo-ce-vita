# Isolated retail PC content staging

This installer-only follow-up to #48 stages your own Xbox, Custom Edition
resources, and retail Halo PC multiplayer files. It includes no game data and
downloads nothing. Run it with Python 3.9 or later and the standard library.

```powershell
python tools/installer/halo_pc_content.py --xbox "Xbox-Halo.iso" --pc "Halo-PC/Maps" --menu "Halo-CE/Maps" --out "New-Vita-PC-files"
```

- `--xbox`: your Xbox ISO/XISO or extracted game folder. Supplies the Xbox
  executable and all stock maps, including campaign and multiplayer maps.
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

Retail campaign replacement is not supported. Campaign maps remain the Xbox
ones. No game code, game-state allocations, saves, settings or bot profiles are
changed by this PR.

Packed retail CAB/MSI install discs, UDF-only images, split BIN/CUE images,
Joliet-only layouts and multi-extent ISO files are unsupported: use installed
retail files instead. Setup, autorun, executables and scripts on the selected
media are never launched; the ISO is not mounted. No DRM bypass is provided.

The standard `default.xbe` and `maps/` output is byte-for-byte the upstream
Xbox + CE-resource staging layout. Retail files have a separate namespace:

| Path | Content |
| --- | --- |
| `maps/` | Unchanged Xbox maps and standard CE resources |
| `retail_pc/maps/pc_bloodgulch.map`, etc. | Nineteen retail multiplayer caches with distinct filenames |
| `retail_pc/resources/` | Retail `bitmaps.map` and `sounds.map`; never copied over the standard resources |
| `retail_pc/previews/pc_*.bmp` | Six previews extracted locally from the owner's CE bitmap resource |
| `retail_pc/content.json` | Alias-to-original cache-name mapping and offline-only policy |
| `pc-content-manifest.json` | Output sizes and SHA-256, without private input paths |

The map payload/header is unchanged; the alias is only a distinct filename.
Inputs are read-only; PC copies are re-read and hash checked before publication
inside the fresh staging directory. Output remains private game data.

The existing uploader deliberately excludes the entire `retail_pc/` folder.
It uploads only the standard files, exactly as it does without this extension:

```powershell
python tools/installer/halo_ce_vita_installer.py --out "New-Vita-PC-files" upload vita.local
```

This targets `ux0:data/haloce-vita`. Existing replacement/skip behavior is the
upstream uploader's behavior; it does not back up the device. FTP is unencrypted
and its verification is size-based. No automatic upload occurs while staging.
Incomplete staging, the old #48 format-1 replacement layout, and unrecognized
retail-profile manifests are refused.

Retail play is intentionally not enabled here. A separate runtime PR must
resolve `pc_` aliases and retail resource paths, isolate converted caches, use
the v7-only loader, and reject these maps from online hosting/joining, map share,
and online playlists before retail upload is enabled. The manifest's
`offline_only` field declares that contract; metadata alone is not a network
guard. Do not manually copy the profile into the standard maps directory.

This split has no dependency on the original #48 gameplay code, no extra CI
workflow, and no 3DS gameplay source. The maintainer is handling the v7 loader
separately. Standard files can be staged/uploaded now; the retail profile is
preparation for that later, offline-only runtime integration.

The ISO/folder reader and its synthetic tests adapt the CC0 source in
[JackTulli/halo-64 at 45a76cdf](https://github.com/JackTulli/halo-64/tree/45a76cdfae43782835ce64cc4947e4178caaecd6).
Its notice is retained in `pc_map_source.py` and its license in
`licenses/PC_INPUT_CC0.txt`. The Vita orchestration and existing installer use
the repository's GPL-3.0-only license. No new production Python dependencies,
converter binaries, codecs or fonts are bundled.
