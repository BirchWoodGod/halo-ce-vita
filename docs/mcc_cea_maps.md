# Halo: CE Anniversary (MCC) maps

Halo: The Master Chief Collection's Halo 1 maps (Halo: Combat Evolved
Anniversary, "CEA") are cache version 13. Steam Workshop campaign mods ship
them: Ruby's Rebalanced Halo: CE (Workshop 2915693904, by Ruby_of_Blue) is ten
of them, `maps/a10.map` to `maps/d40.map`. This build does not load version
13. `port/tools/mcc_map_convert.py` rebuilds such a map as a Halo Custom
Edition cache (version 609) with [Invader](https://github.com/SnowyMouse/invader),
and the Custom Edition loader ([custom_edition_caches.md](custom_edition_caches.md))
plays it. Nothing changes at run time for version 13: the game never reads
an MCC map, so the files it loads are the same kind it already checks.

No map, tag or other mod data is in this repository: players convert their
own copy.

## How version 13 differs from 609

What this engine meets, found on the ten Ruby maps (October 2026,
mod 3.1.0, build string `01.03.43.0000`) with Invader 0.55 and the loader's
report tool:

| | Custom Edition (609) | CEA (13) |
| --- | --- | --- |
| Header | `head`/`foot`, as 609 | the same fields; a flags word at 0x68: 1 uses MCC's `bitmaps.map`, 2 its sounds cache, 4 classic only (the Ruby maps: 7) |
| Tag data address | 0x40440000 | 0x50000000 |
| Tag space | 23 MB (OpenSauce: 34.5 MB) | 64 MB |
| File size limit | 384 MB | 2 GB |
| Compression | none | none for these maps (file size = the header's) |
| Bitmaps and sounds | in the map, or in `bitmaps.map`/`sounds.map` (Halo PC's) | in the map, or in MCC's shared caches; the Ruby maps keep all theirs (Invader: "External tags: None"), so MCC's own files are not needed |
| Bitmap formats | DXT1-5 and the uncompressed ones | BC7 added (none in the Ruby maps) |
| Sounds | Xbox ADPCM, Ogg Vorbis, PCM | the same (Ruby a10: 1101 Ogg Vorbis sounds, 5 PCM permutations) |
| Script functions | Halo PC's | `mcc_mission_segment` (MCC's mission statistics; 13-34 calls a map) and other MCC additions (none used by the Ruby maps) |
| Object names | 512 | more (Ruby b40 513, c40 and d40 531) |
| Animations a graph | 256 (tool limit) | more (Ruby's cyborg: 300; this build's Custom Edition check allows it) |
| `weapon_type` | 5 values | a sixth (rwarthog gun, fuel rod gun): the tag check makes it 0 |
| Anniversary graphics | none | remastered models, bitmaps and sounds, used when the classic-only flag is clear; the Ruby maps set it: classic only |

Invader refuses tags MCC plays, and the conversion makes these right:

- a lens flare reflection naming a bitmap its flare's bitmap lacks (the
  Scorpion's headlights: index 2 of 1): given the first;
- indices past what they index (command list atoms' points, an effect's
  location): none, as this build's tag check makes them in any map
  (invader-bludgeon `invalid-indices`); enumerators past their values
  (`motion_sensor_blip_size` 30062): valid ones (`invalid-enums`);
- HUD wraith icon (`ui\hud\bitmaps\combined\wraith`, DXT1 not marked
  compressed, its smallest mipmaps cut short): marked, mipmaps cut to the
  whole ones (in a copy of the map, before extraction);
- a HUD element naming a sequence its bitmap lacks (`ui\hud\shadow`'s health
  panel background): no bitmap (the game draws neither);
- two objects of one name: the second unnamed;
- more than 512 object names: those the scripts, AI conversations and
  command lists name come first, the rest past 512 are dropped (their
  objects unnamed; nothing names them);
- `mcc_mission_segment` calls: `(not true)`, what this build's script loader
  makes of a call it lacks.

What the converted map needs of this build (branch `feat/mcc-maps`):

- **more tag space** than 23 MB (Ruby a10 25, c10 23.5, d20 27.75, c40 27.6,
  c20 28.4, d40 33.6 MB): the loader takes what the map's structure BSPs
  reach, up to 34.5 MB, as for OpenSauce's memory upgrades
  (`custom_edition_structure_bsp_tag_cache_bytes`). Invader's `-O` would fit
  23 MB by sharing identical data between tags, which the tag check
  refuses (a converted shader or HUD scale would be converted twice);
- **linear sprites**: MCC's HUD message icons are unswizzled bitmaps of any
  size, which Invader leaves unmarked; the loader marks them linear, and
  the texture cache gives linear textures their padded row pitch;
- **more object lists**: Ruby b40 and c20 make more than 48 in one script
  update; the unreferenced ones are now collected between scripts once
  three quarters are taken.

Lost or changed against MCC: the Anniversary graphics and sounds (not in
these maps), MCC's mission segments (statistics), the sixth weapon type,
19 unused object names on c40 and d40, Halo PC's
`player_effect_set_max_vibrate` (no rumble limit), and whatever MCC's
engine does differently from the Xbox game this build is. Invader changes a
few weapon values of maps built under a stock map's name (Custom Edition's
multiplayer values for the rocket warthog and the Ghost's and Banshee's
bolts); the converted maps have their own names, so Ruby's values stay. The
singleplayer pistol and plasma rifle accuracy patches Invader reapplies
from the scenario's flag, as MCC applies them.

## Converting

```
python3 port/tools/mcc_map_convert.py --invader <invader build> \
    --output <folder> --prefix ruby_ <mod>/maps/*.map
```

`--invader` is a folder with `invader-extract`, `invader-build`,
`invader-edit`, `invader-bludgeon` and `invader-info` (Invader 0.55 or
later; Windows builds exist). Each map becomes `<prefix><name>.map`
(`ruby_a10.map`) with `<prefix><name>.txt` and `.bmp`, its title,
description and picture from the mod's `campaign/*.json` and thumbnails;
`<prefix><name>.log` is Invader's output. The ten Ruby maps take 36 s on
a desktop (four at a time), 2.5 GB. The mod's files are only read, by
Invader; its scripts are recompiled from their source by Invader's compiler
and run by the game's interpreter. A map that takes tags from MCC's shared
caches (not classic only) is refused.

## On the Vita

Copy the `.map`, `.txt` and `.bmp` files to `ux0:data/haloce-vita/maps/`
beside the stock maps (Halo PC's `bitmaps.map`, `sounds.map` and `loc.map`
must be there too, as for any Custom Edition map, and PC maps on). They
are listed in Campaign after the ten levels, by their pictures and titles;
the stock `a10.map` ... stay as they are. A converted level won goes on to
the next of its prefix (`ruby_a10` to `ruby_a30`: `custom_edition_maps_campaign_next_level`),
on the same difficulty; there is no campaign save or profile progress for
them (Save and Quit keeps the stock campaign's).

## Measured (gxm-null harness, x86)

- All ten load and run 11000-14000 ticks with their cinematics skipped;
  `port/vita/tests/run_mcc_maps_test.sh` checks it (`HALO_TEST_MCC_MAPS`).
- a10, the stock walk through shoot, cafeteria, airlock, flank, loop and
  knot (heroic, fixed tick): objects 603 against 546, bipeds 133 against
  110 at the end; tick 0.29-0.61 against 0.15-0.45 ms in the first
  encounters (+20-50%), 0.82-0.89 against 0.80-0.97 ms in the knot.
- Texture pool (HALO_CDRAM_RESERVE_MB=4): a10 38 MB decoded against 50 MB
  stock, no pressure (the Custom Edition path draws 98 large textures from
  their second level); d40 17.7 against 13.5 MB, no pressure.
- C heap after load: 39.5 MB (a10) against 47 MB stock; the tag cache is a
  block of its own (25-33.6 MB of user RAM on the Vita), where an Xbox
  map's tags are in the game's window.

Not measured on a Vita: user RAM with a 33.6 MB tag cache, Ogg Vorbis
decoding of ~1000 sounds a map, and the tick cost of the larger encounters.
