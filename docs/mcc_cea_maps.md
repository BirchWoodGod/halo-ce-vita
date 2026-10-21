# Halo: CE Anniversary (MCC) maps

Halo: The Master Chief Collection's Halo 1 maps (Halo: Combat Evolved
Anniversary, "CEA") are cache version 13. Steam Workshop campaign mods ship
them: Ruby's Rebalanced Halo: CE (Workshop 2915693904, by Ruby_of_Blue) is ten
of them, `maps/a10.map` to `maps/d40.map`. This build plays them two ways:

- **natively, from the mods folder** (branch `feat/mcc-native`): the player
  copies the mod's folder to the Vita as Steam keeps it, and the game loads
  its version 13 maps as they are, making at load what the converter makes
  (below: [Playing a mod](#playing-a-mod-natively)). No PC is needed.
- **converted** (branch `feat/mcc-maps`): `port/tools/mcc_map_convert.py`
  rebuilds each map as a Halo Custom Edition cache (version 609) with
  [Invader](https://github.com/SnowyMouse/invader), and the Custom Edition
  loader ([custom_edition_caches.md](custom_edition_caches.md)) plays it
  ([Converting](#converting)).

No map, tag or other mod data is in this repository: players copy or
convert their own.

MCC's own multiplayer maps (Halo PC's: Blood Gulch's Banshees, the Rocket
Warthog, gun turrets, the fuel rod) convert too, for local games:
[MCC's PC multiplayer maps](#mccs-pc-multiplayer-maps) (branch
`feat/mcc-pc-maps`).

## Playing a mod natively

Copy the mod's whole folder (Steam keeps it in
`steamapps/workshop/content/976730/<workshop id>`) into the data root's
`mods` folder, under a short name of your own:

```
ux0:data/haloce-vita/
  maps/              the Xbox maps (as before; nothing of the mod goes here)
  mods/
    ruby/            the mod's folder, renamed (any name; see below)
      maps/a10.map ... maps/d40.map     the Halo: CE Anniversary maps (needed)
      CampaignInfo.json                 the campaign's title and order (optional)
      campaign/a10.json ...             each level's title, description, picture (optional)
      assets/*.png                      the pictures they name (optional)
```

Turn PC maps on, then MCC mods (Settings > Multiplayer > Modded maps; the
MCC mods row shows once the mods folder has a folder in it; on a PC,
`game.mcc_mods = true` with `game.custom_edition`). MCC mods is
experimental and off by default: off, the mods folder is not looked at
and no mod map loads. The mod manager (the Halo 3 menus' MOD MANAGER, the
panel's Modded maps > Mod manager: `port/linux/game/mod_manager.c`) lists
each mod, on or off, with the same checks, whether or not MCC mods is on,
and turns one off (`HALO_MODS_DISABLED`, the mods' keys: left out of the
level list, kept in its folder) or deletes its folder. The campaign's
level list then shows the mod's maps after the ten levels (and any Custom
Edition campaign maps), in the order its `CampaignInfo.json` plays them,
by their titles ("The Pillar of Autumn"), described by the mod's title and
the level's description, with the level's thumbnail as its picture (a
`<map>.bmp` beside the map takes its place). Choose one, then the
difficulty: it plays alone, offline. Won, a level goes on to the next one
its campaign lists, on the same difficulty. There is no profile progress
for them, as for Custom Edition campaign maps; Save and Quit (Halo PC's
pause menu's QUIT) writes the last checkpoint to a save of the custom
campaign maps' own, `savegame_custom.bin` beside the profile's
`savegame.bin` (which it leaves alone), and the same map started again
on the same difficulty resumes from it. Halo PC's pause menu has no SAVE
LAST CHECKPOINT (a function of Halo PC's own, which did nothing here): its
QUIT saves.

- **The folder's name** is the mod's key: in lower case, its letters,
  digits and `-` only, at most 12 (`ruby`; Steam's `2915693904` works too,
  as it is). A map's name is the key, `_` and its file's name (`ruby_a10`),
  at most 25 characters, and its level name `mods
uby_a10`: no stock
  level, Custom Edition map or network game is named so, so a mod's `a10`
  is never taken for the stock one. A map named as a file of the maps
  folder (a converted `ruby_a10.map`) is left out: the maps folder's
  plays. 16 mods and 64 maps at most are listed.
- **Bitmaps and sounds**: a map that keeps them all in itself (classic-only
  maps, as the Ruby maps are) needs nothing else. A map that keeps their
  pixels and samples in MCC's resource maps reads them from a
  `bitmaps.map` and `sounds.map` in the mod's own `maps` folder, beside it
  (never the maps folder's, which are Halo PC's): each bitmap's pixels
  must be one of `bitmaps.map`'s items, by offset and size, or the map is
  refused (as it is when the file is missing); a sound whose samples are
  not one of `sounds.map`'s items, or whose mod has no `sounds.map`, plays
  nothing (MCC keeps its own sounds in FMOD banks, so a map built against
  MCC's shared files has none of them here).
  A map that takes whole tags from them is refused. MCC's own campaign
  maps load so, with MCC's `halo1/maps/bitmaps.map` beside them (a test
  of this loader, below: their sounds are silent).
- `debug.txt` says what was found (`mods: 10 maps of 1 mods ...`), why a
  map was left out, and what the load made of each (`custom edition:
  Halo: CE Anniversary: ...`).

### Offline only

Online security comes first: a version 13 map is a new kind of file, and
nothing of it reaches another player, in either direction.

- It is loaded **only from a mod's folder** (where map sharing never
  writes: downloads go to the maps folder and take versions 5 and 609
  only), **only of a solo scenario**, and **only for a game of this
  machine's own** (`game_connection` local: never a network game's, hosted
  or joined, nor the dedicated server's). A version 13 file in the maps
  folder is named and refused ("plays one only from a mod's folder"), and
  a file of any other kind in a mod's folder is not loaded.
- The mods folder is not even looked in unless MCC mods is on (off by
  default), for any name a game of another machine gives (a host's map, a
  film's: `custom_edition_map_path` gets its mods argument only when the
  connection is local), nor ever by the dedicated server.
- It has a format of its own (`_cache_file_format_mcc_cea_cache`), which
  every check of the Custom Edition format leaves out: network identities
  (`custom_edition_cache_map_identity`), join checks
  (`custom_edition_maps_loadable`), map sharing
  (`custom_edition_maps_shareable` says no to a mod map) and the hosting
  of co-op (Y on the difficulty list is refused for a mod map, and logged)
  never see one.
- Every field is untrusted. The header, tag index, scenario and structure
  BSP references are checked before anything is allocated; every tag
  instance, name, block and data field as a Custom Edition cache's are;
  each structure BSP, its CEA header and every material's vertex range in
  the file as the map loads, and again when the BSP is read (the BSP must
  be the one checked: its file range, size and vertex size; and each
  vertex's vectors must compress); the tags again after they are moved
  (`tag_validate.c`), and each BSP as it loads. The mod's JSON and PNG
  files are read by bounded readers (`mod_campaign_files.c`,
  `png_files.c`) of names taken only of the mod's own folders. Nothing in
  a mod is run but its scripts, by the game's interpreter, after
  `custom_edition_scripts.c` has checked and renamed every call.
- The loader, the fixes and the two readers have fuzz-style tests (every
  truncation and seeded random corruptions, under AddressSanitizer and
  undefined-behaviour trapping: [Tested](#tested)).

### What the game makes of a version 13 map

As it loads (`cache_file_formats.c`, `custom_edition_cache.c`,
`custom_edition_geometry.c`), in its own copy of the tags, what
`mcc_map_convert.py` makes for Invader:

| MCC has | This build takes | At load |
| --- | --- | --- |
| tag data linked to 0x50000000, 64 MB of room | a tag cache of its own | the pointers moved there (`tag_relocate.c`), as a Custom Edition map's on the Vita, now in every build |
| structure BSPs linked to the top of the 64 MB | one at a time, in room after the tags | each BSP's reference moved to that room; as it is read, its own pointers moved from where MCC links it (`halo_tag_relocate_linked_structure_bsp_rebase`) |
| BSP vertices outside the BSP, in the file (the CEA BSP header) | uncompressed vertices to compress | read from the file as the BSP loads, a megabyte at a time, each range and vector checked |
| 32767 script syntax nodes, with holes (Ruby a10 uses nodes to 19722) | 19001 | packed, every link renumbered (more than 19001 in use refuses the map) |
| more than 512 object names (b40 513, c40 and d40 531) | 512 | names past 512 that scripts, AI commands or conversations name moved into the last unnamed places below 512 (c40 16, d40 3), the rest dropped (19 on c40 and d40); more than 512 named refuses the map |
| CEA's bitmap flags (environment) | the first eight | cleared (Ruby a10: 339 bitmaps) |
| a DXT bitmap not marked compressed (the HUD wraith icon) | marked | marked, its mipmaps cut to those its pixels hold whole |
| indices out of range, enumerators past their values | | none or valid ones, as any map's (`tag_validate.c`) |
| a sixth `weapon_type` | 5 values | 0 (`tag_validate.c`: the rocket warthog's gun) |
| `mcc_mission_segment` | no such function | a call gives true, as in MCC, and does nothing else (`custom_edition_scripts.c`) |
| `player_effect_set_max_vibrate` | `player_effect_set_max_rumble` | called by that name |
| two objects of one name | | the first placed has it (`objects.c`) |
| more than 48 object lists in one update (b40, c20) | | collected between scripts (`feat/mcc-maps`) |
| a HUD laid out on a canvas twice the Xbox's ([The HUD](#the-hud)) | the Xbox's 640x480 | placements, margins and digit metrics halved, bitmaps drawn at a quarter (also converted maps) |

The rest is a Custom Edition map's: shaders renumbered, Ogg Vorbis sounds
decoded as they load, models converted, HUD scales halved, linear sprites
(`custom_edition_cache_convert`, `custom_edition_cache.c`).

### The HUD

MCC lays the HUD out on a canvas twice the Xbox's 640x480, and its HUD
tags (MCC's own `ui\hud\*`, which Ruby's maps keep) are made for it:
every offset in the unit, weapon and grenade HUD interfaces and the HUD
globals is twice the Xbox b30's (the shield panel meter 14,0 against 7,0,
the health panel 72,34 against 36,17, the grenade count 164,2 against
82,1, the HUD messages 0,130 against 0,60, the damage indicators 20
against 10), the digits' metrics are twice theirs (18 screen width against
9), and the bitmaps the placements draw are four times the Xbox's with
Halo PC's *half HUD scale* flag (`hud_unit_backgrounds` 512x128 against
128x32, `hud_reticles` 512 against 128, the digits 72x48 against 18x12, the
HUD message icons 486x104 against 122x26). Halo PC's own maps draw on the
Xbox's canvas, so this build drew such a HUD twice its size and pushed in
from the corners (the owner's Vita, October 10 2026, Ruby's b30 converted).

As the map loads, a HUD globals tag with MCC's remaps of HUD bitmaps to its
remastered ones (Invader's *anniversary hud remaps*, MCC's alone: 14 in
each Ruby map, natively and converted, and in MCC's own b30; the Xbox's and
Halo PC's tags leave the place zero) marks the HUD as MCC's
(`hud_mcc_canvas` in `cache_file_report`, "the HUD is Halo: CE
Anniversary's" in the load log), and `mcc_hud_placements_convert` brings it
to the Xbox's canvas: each placement's offset halved, its size halved and
halved again by its bitmap's half HUD scale; a placement with the *use high
res scale* flag keeps its offset (MCC reads the flag, made for Halo PC, as
an offset on the Xbox's canvas and doubles it: Invader's
`hud_interface.cpp`) and loses the flag; the waypoint and damage indicator
margins and the digits' metrics halved. What no placement sizes,
`hud_mcc_bitmap_scale` draws: the digits and the HUD messages' icons at a
quarter of their bitmaps, the waypoint arrows, the damage indicators and
the scopes' masks (a weapon HUD's screen effect: the sniper rifle's
1024x512, the pistol's 512), which are only twice the Xbox's with the half
HUD scale, at half. MCC's meters carry its *use xbox shading* flag and are
laid out as the Xbox's (the order they fill in luminance, their shape in
alpha), so they keep their channels where a Halo PC meter's are swapped
(`custom_edition_bitmaps.c` `hud_meter_bitmap_add`; swapped, the shield,
health and rounds drew full above three quarters and empty below, and
seemed not to change: the owner's Vita, October 10 2026), and a weapon HUD
element's own anchor, MCC's (in its header's padding: the sniper rifle's
range ladders, anchored to the centre where its HUD anchors the top left),
is followed for the corners and the centre. Ruby's HUD then draws at the
Xbox b30's sizes and places, the motion sensor 10 further in from the
corner (MCC's own margin); `port/vita/tests/run_hud_layout_test.sh` checks
it, natively and converted. Not followed: MCC's anchors at the edges'
centres (none in Ruby's HUD) and its remastered HUD bitmaps (the remaps);
a HUD bitmap drawn at a quarter of its size, point sampled as the Xbox's
meters are, is a little rougher than MCC's.

## How version 13 differs from 609

What this engine meets, found on the ten Ruby maps (October 2026,
mod 3.1.0, build string `01.03.43.0000`) with Invader 0.55 and the loader's
report tool:

| | Custom Edition (609) | CEA (13) |
| --- | --- | --- |
| Header | `head`/`foot`, as 609 | the same fields; a flags word at 0x68: 1 uses MCC's `bitmaps.map`, 2 its sounds cache, 4 classic only (the Ruby maps: 7) |
| Tag data address | 0x40440000 | 0x50000000 |
| Tag space | 23 MB (OpenSauce: 34.5 MB) | 64 MB (the tag data 13.8-20.3 MB; each structure BSP linked to the top) |
| Structure BSP | its header's vertex buffer fields zero; each material's uncompressed vertices in the BSP | the header's second and third words say where in the file the BSP's vertices are and how many bytes; each material's rendered and lightmap vertices at offsets into those (its vertex buffers' offsets), its vertex data empty (Invader's `ScenarioStructureBSPCompiledHeaderCEA`); the BSPs 40 KB-5.5 MB without them, their vertices 0-8 MB |
| Script syntax nodes | 19001 (OpenSauce 28501) | 32767, with holes (a10: 18480 in use up to node 19722) |
| File size limit | 384 MB | 2 GB (this build loads one of at most 1 GB: `MCC_CEA_CACHE_FILE_MAXIMUM_BYTES`) |
| Compression | none | none for these maps (file size = the header's) |
| Bitmaps and sounds | in the map, or in `bitmaps.map`/`sounds.map` (Halo PC's) | in the map, or in MCC's shared caches; the Ruby maps keep all theirs (Invader: "External tags: None"), so MCC's own files are not needed; a map that keeps pixels or samples outside reads them from its mod's own (above) |
| Bitmap formats | DXT1-5 and the uncompressed ones | BC7 added (none in the Ruby maps) |
| Sounds | Xbox ADPCM, Ogg Vorbis, PCM | the same (Ruby a10: 1101 Ogg Vorbis sounds, 5 PCM permutations) |
| Script functions | Halo PC's | `mcc_mission_segment` (MCC's mission statistics; 13-34 calls a map) and other MCC additions (none used by the Ruby maps) |
| Object names | 512 | more (Ruby b40 513, c40 and d40 531) |
| Animations a graph | 256 (tool limit) | more (Ruby's cyborg: 300; this build's Custom Edition check allows it) |
| `weapon_type` | 5 values | a sixth (rwarthog gun, fuel rod gun): the tag check makes it 0 |
| Anniversary graphics | none | remastered models, bitmaps and sounds, used when the classic-only flag is clear; the Ruby maps set it: classic only |

Invader refuses tags MCC plays, and the conversion makes these right (the
native load makes the same, or the game takes them as they are: [What the
game makes of a version 13 map](#what-the-game-makes-of-a-version-13-map)):

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
- `mcc_mission_segment` calls: `true`, what MCC gives (its maps test it
  before a cinematic's pause) and this build's script loader makes of one.

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
caches (not classic only) needs `--mcc-maps` and `--sound-maps`: [MCC's PC
multiplayer maps](#mccs-pc-multiplayer-maps).

## Converted maps on the Vita

Copy the `.map`, `.txt` and `.bmp` files to `ux0:data/haloce-vita/maps/`
beside the stock maps (Halo PC's `bitmaps.map`, `sounds.map` and `loc.map`
must be there too, as for any Custom Edition map, and PC maps on). They
are listed in Campaign after the ten levels, by their pictures and titles;
the stock `a10.map` ... stay as they are. A converted level won goes on to
the next of its prefix (`ruby_a10` to `ruby_a30`: `custom_edition_maps_campaign_next_level`),
on the same difficulty; there is no profile progress for them, and Save
and Quit writes the custom campaign maps' own save (`savegame_custom.bin`,
the stock campaign's kept), which the map resumes from.

## MCC's PC multiplayer maps

MCC's `halo1/maps` holds Halo PC's seven multiplayer maps as version 13
caches: `bloodgulch`, `dangercanyon`, `deathisland`, `gephyrophobia`,
`icefields`, `infinity` and `timberland` (none of the Xbox's; the Vita's
Xbox maps have no Banshee, Rocket Warthog, gun turret or fuel rod in
multiplayer: triage/mp-banshee-fuelrod-1.1.1.md). They are not classic
maps: each keeps its bitmaps (445-494) and sounds (333-340) in MCC's
resource maps, some 800 external tags, which the converter refused. It
takes them now (`feat/mcc-pc-maps`):

```
python3 port/tools/mcc_map_convert.py --invader <invader build> \
    --output <folder> --prefix pc_ \
    --mcc-maps "<MCC>/halo1/maps" --sound-maps <the Xbox maps folder> \
    "<MCC>/halo1/maps/bloodgulch.map" ...
```

- **Bitmaps** from MCC's `halo1/maps/bitmaps.map` (`--mcc-maps`): the
  folder invader-extract reads holds it alone.
- **Sounds**: MCC keeps no `sounds.map` (its sounds are in its FMOD banks).
  Given Halo PC's `sounds.map` (MCC's `custom_edition` folder) in its place,
  Invader read MCC's offsets in it and extracted noise for 160 of Blood
  Gulch's sounds (an RMS of 22860 against the Xbox tag's 2227, correlation
  0.03): never do that. The converter takes each such sound's tag, by
  path, from the player's Xbox maps (`--sound-maps`, multiplayer maps
  first; Xbox ADPCM, which the game plays without decoding): 323-330 a
  map. Halo PC added 16 the Xbox maps lack (`PC_ONLY_SOUNDS`): the fuel
  rod's explosion, overheat, melee and impact, the rocket launcher's reload
  and the menu's back and cursor are given the Xbox sound most like them
  (the campaign fuel rod's explosion, the plasma rifle's overheat, the
  rocket launcher's melee and impact, ...); the flamethrower's burnout,
  empty and impact, the hit "ting", and a few ambiences (Timberland's
  stream and waterfall, Infinity's beam emitter, an electric hum) are made
  silent, a tenth of a second of the class named. Each is listed in the
  map's `.log`.
- A multiplayer scenario may have no scripts. The rest is as for a classic
  map (one lens flare reflection fixed a map).

The seven take 51 s on a desktop (the Xbox maps' sounds extracted once a
run) and are 96-110 MB each (every bitmap and sound in the map: they rely
on no resource map), 6.9-11.1 MiB of tag space (of 23); `map_validate
--strict` under AddressSanitizer: 0 corrections. They are Custom Edition
multiplayer maps like any other in the maps folder (PC maps on; Halo PC's
`bitmaps.map`, `sounds.map` and `loc.map` there as for any).

### Vehicles and weapons in local games

Their placements (October 2026):

| Map | Banshees | Gun turrets | Fuel rods | Other vehicles |
| --- | --- | --- | --- | --- |
| Blood Gulch | 2, allowed only | - | 1 | Warthog and Rocket Warthog a team by default; more of them, Ghosts and Scorpions allowed |
| Danger Canyon | - | - | 2 | Ghosts, Warthogs, Rocket Warthogs by default; Scorpions allowed |
| Death Island | 4 by default | 2 | 3 | |
| Gephyrophobia | 4 by default | 4 | 2 | |
| Ice Fields | - | - | 2 | |
| Infinity | 8 (6 in Slayer) | 8 | 4 | |
| Timberland | - | - | 2 | |

A Custom Edition map places the vehicles whose spawn flags name the game
type by default (custom_edition_objects.c), and of those an Xbox variant's
default set keeps Warthogs, Ghosts and Scorpions
(`game_engine_vehicle_placement_allowed`, OpenCE's rule), so Halo PC's
maps lose their Banshees, Rocket Warthogs and turrets. **PC map vehicles**
(`game.pc_map_vehicles`, HALO_PC_MAP_VEHICLES; the Vita's Settings >
Multiplayer > Modded maps, shown while PC maps is on) changes that in a
game of this machine's alone (split screen or offline, with or without
bots: `network_game_is_splitscreen_local`):

- **Xbox** (the default): as before;
- **PC**: every type the placements' default flags name, as Halo PC's
  default set (Death Island's, Gephyrophobia's and Infinity's Banshees and
  turrets, the Rocket Warthogs);
- **Allowed**: also every placement the map allows in the game type (Blood
  Gulch's two Banshees, and its every Ghost and Scorpion: 28 vehicles).

PC and Allowed take the place of the Xbox variant's vehicle set (Slayer's
own is Warthogs alone), as a PC host's set does; a variant with no
vehicles keeps none.

Every machine of a network game places a map's vehicles itself and no
wire carries this choice, so a network game's vehicles (hosted, joined,
the dedicated server's) are never changed by it, and nothing on any wire
changes. The bots drive the ground vehicles and man the turrets as on any
map; a Banshee is the players' (the bots fly none).

The fuel rod (Halo PC's `weapons\plasma_cannon`) needs nothing: it is past
the Xbox's weapon list in the PC globals (index 14), so a game's normal
weapon set leaves it where the map lays it (the other sets remap it as any
weapon); Halo PC's flamethrower is a rocket launcher, as the Xbox game
makes it (`game_engine_remap_weapon`).

`port/vita/tests/run_mcc_pc_maps_test.sh` (HALO_TEST_MCC_PC_MAPS, or MCC's
folder and Invader to convert them first) plays each in a local Slayer
game with four bots and Allowed, and Blood Gulch with Xbox and PC
(October 13 2026, gxm-null harness; ranges over three or four runs: the
scripted player is no pilot, and a Banshee that meets a wall climbs and
gets no further, so a flight is 10 units off and 1 up, or 2 up):

| Game | Placed | The player's Banshee flight |
| --- | --- | --- |
| Blood Gulch, Allowed | 2 Banshees, 1 fuel rod lying | 38.7-72.7 units, up 1.7-3.9 |
| Death Island | 4 Banshees, 3 fuel rods | 7.9-91.5 units, up 2.6-2.8 |
| Gephyrophobia | 4 Banshees, 2 fuel rods | 59.9-91.5 units, up 2.5-2.9 |
| Infinity | 6 Banshees, 4 fuel rods | 5.1-58.0 units, up 2.5-8.8 (it starts in a base's hangar) |
| Danger Canyon, Ice Fields, Timberland | no Banshee; 2 fuel rods each | - |
| Blood Gulch, Xbox | 2 Warthogs, no Rocket Warthog or Banshee | - |
| Blood Gulch, PC | 2 Warthogs, 2 Rocket Warthogs, no Banshee | - |

`map_validate --fuzz` (AddressSanitizer, undefined-behaviour trapping,
32-bit) on pc_bloodgulch, pc_deathisland, pc_infinity and pc_timberland:
6400 runs, no crash. The game reads them with the Custom Edition loader
alone (no new parser); the converter, which reads MCC's files through
Invader, runs on the player's PC.

Not tried on a Vita: their load, memory (96-110 MB files whose tags are
7-11 MB) and frame rate.

**Network**: a converted map is an ordinary Custom Edition map: listed in
the multiplayer map list with PC maps on, hostable in a system link or
online game (where PC map vehicles is Xbox), and offered to a joiner who
lacks it by map sharing (`custom_edition_maps_shareable` refuses only stock
names, mods folder maps and disabled maps), as Ruby's converted campaign
maps already are. Whether MCC-derived maps may be shared so is the owner's
to decide; nothing here changes it.

## Measured (gxm-null harness, x86)

### Native against converted (October 10 2026)

Ruby's ten maps from the Steam folder (linked read-only as `mods/ruby`)
against the same maps converted (`ruby_*.map`), the two runs beside each
other on one machine (`run_mcc_native_test.sh`, `run_mcc_maps_test.sh`: 120
s of fixed ticks, the first cinematics skipped, deathless):

| Map | Tag cache native | converted | Ticks (both) | ms a tick native (max) | converted (max) |
| --- | --- | --- | --- | --- | --- |
| a10 | 18.4 MB | 25.0 MB | 6844 | 0.490 (0.89) | 0.485 (0.82) |
| a30 | 18.6 | 23.0 | 6839 | 0.463 (0.81) | 0.479 (1.05) |
| a50 | 18.2 | 23.0 | 6898 | 0.593 (0.89) | 0.597 (0.82) |
| b30 | 18.6 | 23.0 | 6897 / 6898 | 1.706 (2.36) | 1.732 (2.34) |
| b40 | 19.8 | 23.0 | 6898 | 0.350 (0.43) | 0.369 (0.43) |
| c10 | 20.3 | 23.5 | 6897 | 0.351 (0.73) | 0.351 (0.69) |
| c20 | 22.3 | 28.4 | 6839 | 0.418 (0.59) | 0.415 (0.54) |
| c40 | 24.2 | 27.6 | 6838 | 0.387 (0.42) | 0.386 (0.42) |
| d20 | 22.1 | 27.75 | 6838 | 0.599 (0.62) | 0.635 (0.69) |
| d40 | 25.9 | 33.6 | 6838 | 0.143 (0.16) | 0.145 (0.16) |

- **The same game.** Every map ends both runs with the same objects, of
  every type (a10: 480; a50: 1122; d40: 462), and the same ticks; no
  assertion or exception in either. The a10 encounter walk of
  `feat/mcc-maps` (shoot, cafeteria, airlock, flank, loop, knot, three
  structure BSP switches): 600 objects against 601 at the end (an
  equipment), the same bipeds, the tick 0.21-0.70 ms in both.
- **Tick cost**: the same, within the runs' noise (the tags are the same
  tags; the native load's work is all done as the map loads).
- **Memory**: the tag cache 3.2-7.7 MB smaller natively (it holds no
  structure BSP vertices: those are read from the file as each BSP loads,
  a megabyte at a time, into the compressed vertices both paths keep); the
  C heap after load 170 KB smaller (no resource maps' indices); the memory
  window the same. With the Vita's memory made tight (HALO_CDRAM_RESERVE_MB=60
  so the texture pool lives in user RAM, and HALO_USER_FREE_MB 48 MB less
  each path's tag cache, as if the Vita had 48 MB of user RAM before the
  tag cache), the a10 encounter walk decoded 663 textures natively with no
  pressure, where the converted map, its tag cache 6.6 MB larger, thrashed
  the pool: 215799 decodes, 7049 segments recycled, 15301 textures drawn
  smaller.
- **Loading**: a10 natively, from the tags read to the models converted,
  ~75 ms here; each structure BSP's vertices (up to 8 MB in the file) read
  as the BSP loads.
- A C heap that grows by ~25 MB over the first map played and stays there
  is the harness's with any map (the stock a10 too), not the mods'.

### Tick and render cost (October 11 2026, feat/mcc-110)

gxm-null harness on x86, threaded tick, fixed ticks, the scripted player
deathless, `HALO_TICK_PROFILE=1`; the Vita's profile switches (static
scenery, LOD 0.5, lighting every third tick ...). Mean of the 10 s
windows' ms a tick (the game's own tick, not the frame), and the render
thread's recording:

| Run | Tick (ms) | p90 window | Render (ms) | Bipeds at the end |
| --- | --- | --- | --- | --- |
| stock a10, 180 s, base (`feat/crossplay` 326330d7), three runs | 0.264 / 0.257 / 0.269 | 0.29 | 0.52-0.55 | 45 |
| stock a10, this branch, two clean runs | 0.267 / 0.250 | 0.27-0.30 | 0.52 | 45 |
| stock b30, 180 s, base / this branch | 0.724 / 0.707 | | 0.98 / 1.01 | 69 |
| Ruby's b30 (converted), 180 s | 0.759 | | 1.11 | 114 |
| stock c10 / Ruby's c10, 180 s | 0.483 / 0.442 | | 0.79 / 0.66 | 11 / 17 |
| shader tour of b30 (8 s a stop, 420 s): stock | 0.529 | 0.97 | 0.52 | 69 |
| the same: Ruby's b30 | 1.345 | 2.34 | 1.11 | 114 |
| the same: MCC's own b30 natively (version 13) | 0.501 | 1.00 | 0.56 | 69 |
| shader tour of a50: stock / Ruby's | 0.358 / 0.235 | 1.07 / 0.74 | 0.40 / 0.27 | 32 / 42 |

- Stock maps: unchanged by the branch (a10 and b30 within the runs'
  noise; the same objects at the end).
- The version 13 loader costs nothing once a map is loaded: MCC's own b30
  played natively ticks as the Xbox b30 does.
- Ruby's maps are heavier where its encounters are bigger: its b30, with
  45 more bipeds as the tour wakes them, costs 2.5 times the stock tick and
  twice the recording; its a50 and c10 do not (the tours stop at each
  map's own cutscene flags, so they are not the same walk). On the owner's
  Vita (October 10, converted maps) the mod averaged ~28 fps over 16
  minutes; the harness says that is the content's cost on the tick and
  render threads, not the loader's, and nothing in this branch makes it
  cheaper. Scaled by the stock map's own cost (the harness is not the
  Vita, but both threads' work grows alike): Ruby's b30 fights take ~2.4
  times stock b30's tick at the 90th percentile and twice its recording,
  so where the stock level holds 30 fps on a Vita those fights fall well
  below it, as the owner saw.

### The converted maps (feat/mcc-maps)

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

Not measured on a Vita: user RAM with a 33.6 MB tag cache and the tick
cost of the larger encounters.

### Sounds: the sound cache file

Every Ruby map's sounds are Ogg Vorbis (as MCC's own are): 991-1123 sounds
a map (d20 257), 5030-5811 permutations (d20 1638), 47-61 MB of streams
that are 130-180 MB of Xbox ADPCM (`cache_file_report`'s
`sound_permutations_ogg_vorbis`; ten maps 493 MB of Ogg Vorbis, 1416 MB of
ADPCM). A stock map's Xbox ADPCM is read and played as it is; an Ogg Vorbis
permutation was decoded each time the sound cache loaded it. On the owner's
Vita (October 10, converted ruby_b30, 80 s): 103 decoded in 19.6 s of the
fourth core, which the map reader, the cache file thread, the map
decompression and the mixer share, and frames of 0.6-1.0 s where the game
waited for the tick while the decoder ran: the tick slept (91 ms of run
time in a 1078 ms wait) while the decoder had the fourth core to itself
(99% busy, the cache file thread and map reader starved: reads of 42 KB took
600 ms). At the other helpers' priority the decoder kept the core (the Vita
does not share a core between threads of one priority), and the tick could
wait on it two ways: deleting a sound cache block whose decoding ran, which
waited for the decoder's next read of the stream, and the map read lock
(`custom_edition_cache_read`), which the decoder held for its reads and the
tick takes for the textures it predicts. The harness, whose scheduler does
share a core, shows neither for long; the frame-hitch line now names the
lock's waits ("custom map read lock wait") for the Vita to say.

Now each permutation's ADPCM is kept in the map's sound cache file
(`mods/<mod>/cache/<map>.sndcache`, or `sound_cache/<map>.sndcache` for a
converted map in `maps`; docs/custom_edition_caches.md) and read from there
the next time, the file filled in the background at the lowest priority,
the deleting thread never waits, and the decoder reads with handles of its
own. A full file is the map's ADPCM: 133 MB for b30 (1.4 GB for the ten);
memory: the decoder's 450 KB block grows by the table, 38 bytes a
permutation (203 KB for b30), taken as the map loads.

gxm-null harness, b30 from the mods folder, the beach walk, threaded tick,
real time, the decoder made 60 times slower (`HALO_OGG_DECODE_SLOWDOWN`,
the Vita's: the owner's 190 ms a permutation against 3 ms here) and the
memory card's speed (`HALO_IO_THROTTLE_KBPS=10000`):

| b30, ~80 s of play | decoded | read from the file | tick waits over 100 ms |
| --- | --- | --- | --- |
| before | 249 | - | 1 (126 ms) |
| first play, file empty | 162 | 56 (permutations the sound cache had let go) | 0 |
| first play, no background filling | 151 | 64 | 0 |
| second play, file full | 0 | 245: 14 MB in 2.7 s of the throttled card | 0 |

Without the slowdown the background fills the whole b30 file in under a
minute; decoding every permutation of b30 (7490 s of audio) takes 8.9 s on
the desktop (847 times real time) and 26.6 s on a Raspberry Pi 4 (282
times), the ADPCM identical to the game's to the byte on both (the file's
CRC-32s); a Vita's A9, some 6-8 times slower than the Pi's A72, would take
3-4 minutes of its fourth core, which the background spreads over the time
the core is idle.

The converter could transcode instead (port/tools/mcc_map_convert.py,
nothing done yet): the converted maps would hold the ADPCM themselves
(+87 MB for b30, 221 to 308 MB; +923 MB for the ten) and never decode, at
8.9 s a map of a desktop's time; it needs a native tool of ours (Tremor and
xbox_adpcm_encoder.c) or Invader's invader-recover and invader-sound, which
rebuild sound tags from recovered audio (their gains and skip fractions not
kept as the map has them).

## GulchCE, compared (October 11 2026)

[GulchCE](https://github.com/chestahh/GulchCE) (chestahh, CC0, an OpenCE
fork) plays version 13 maps too, natively, on desktop Windows (its own
docs call Linux and Android gameplay unverified), with its own adapter
(`port/linux/game/mcc_*.c`, about 8500 lines written October 9-11 2026).
Compared at its commit 56f598a, feature by feature, for what this build
lacks and could take on a Vita (CC0 code may come into this GPL tree;
every piece taken names GulchCE where it lands). None of its MCC art
(its `mcc_*_butn*.png` button glyphs, generated pause menu art) is taken.

| Area | GulchCE | This build | Taken / kept |
| --- | --- | --- | --- |
| Memory | a fixed 64 MB window at the maps' 0x50000000; every bitmap decoded and kept in memory (522 MB for Mercury Falling), every sound decoded to ADPCM at load (100+ MB) | tags moved into a tag cache of at most 34.5 MB, textures streamed through the texture cache, sounds transcoded lazily into the mod's sound cache file | kept: GulchCE's model cannot fit a Vita |
| Header, layout | solo and multiplayer scenarios, any 64 KB tag base, compressed length ignored | solo only, tag base 0x50000000, flags 1-4, a file of at most 1 GB, fuzzed | kept (multiplayer v13 maps would be offline-only anyway) |
| Resource packages | a package folder's `bitmaps.map`/`sounds.map`, items matched by offset, name and size, a stale offset moved to a unique name | refused | taken: a mod's own `bitmaps.map`/`sounds.map` beside its maps, each range one of their items by offset and size; a missing sound silent; no name matching or moving (the files are the mod's) |
| Recordings | 0xFF00 weapon indices made none | passed through: a release build read the unit's inventory 256 entries before it | taken, for every map |
| Cutscene cameras | field of view past 90 degrees made 90 | passed through: the scripted camera asserts | taken, for every map |
| Model shaders | MCC's "use xbox multipurpose channel order" flag keeps the masks' channels | every multipurpose map reordered as Halo PC's: MCC's flagged ones drew wrong | taken |
| Script parameters | runs them (its own runtime, 32767 nodes) | would have run them as globals | refused instead (no map examined has one) |
| `mcc_mission_segment` | true | false (the pause before each cinematic skipped) | taken: true |
| `player_effect_set_max_vibrate` | `player_effect_set_max_rumble` | nothing | taken |
| Other MCC script functions | `objects_distance_to_*`, breadcrumb nav points, gravity, effects gain, ~30 in all | nothing (a call gives its type's default) | left: MCC's own maps call the distance functions only from their breadcrumb nav point scripts; Ruby's maps call none |
| Weapon export values 17/18 | 15/16 | 15/16 already (from DamnationCE) | - |
| HUD | converted for every v13 map; edge-centre anchors 5-8 | converted when the HUD globals carry MCC's remaps (MCC's maps and Ruby's do); anchors past 4 the top left | left: no MCC or Ruby HUD uses anchors 5-8 (Combat Revolved does) |
| Bitmaps | BC7 decoded; other sizes than powers of two in DXT made linear ARGB | refused | left: a BC7 transcode would be disk-cached work on a 444 MHz core, ARGB four times the memory |
| Geometry | broken model tangents rebuilt | such a model refuses the map | left (only Mercury and ML's pelican need it) |
| Checkpoints, grenades 3/4, pause menus | extra checkpoint state, grenade side tables (36 engine hooks), generated pause menus | none | left: large, and not needed by the maps tested |
| Campaign flow | levels chosen by hand | `CampaignInfo.json` order, titles, pictures, next level when won | kept |
| Network | mod maps hosted for co-op and multiplayer | never on any wire | kept: never |
| Parsers | bounded reads, some allocations sized by the file (a BSP up to 64 MB, sounds up to 256 MB) | bounded reads, allocations capped by the tag cache, seeded corruptions under AddressSanitizer | kept; what was taken is fuzzed the same way |

## Tested

- `python -m pytest tools/test_mcc_cea_caches.py` (33 tests, made-up
  caches, the report tool `cache_file_report` under AddressSanitizer and
  undefined-behaviour trapping): the compact layout, the references moved,
  the refusal where the caller does not allow a version 13 cache
  (`--no-mcc-cea`), node packing and every link renumbered, names moved
  and placements and references renumbered, the bitmaps' flags and
  mipmaps, 21 malformed caches each refused with its reason, a tag of
  MCC's resource maps, a tag cache too large, every truncation of the
  header and tag data, and 400 seeded corruptions of the header, tag
  index, scenario, tag data, structure BSP and vertices (sampled evidence,
  not proof).
- `tools/test_mod_campaign_files.py` (22) and `tools/test_png_files.py`
  (19): the campaign files and the pictures, kinds refused, every
  truncation and 300 seeded corruptions each, under AddressSanitizer.
- `cache_file_report` on Ruby's ten maps, under AddressSanitizer: all load
  and are fixed (`script_nodes_packed`, `object_names_moved`, ...).
- `port/vita/tests/run_mcc_native_test.sh`: the ten maps from the mods
  folder, 120 s each, a won map's next, and a version 13 map in the maps
  folder refused; the campaign's level list driven by scripted presses
  (HALO_TEST_PAD): the ten listed after the stock levels, the eleventh
  entry plays `mods\ruby_a10`, Y on its difficulty refuses co-op.
- Unchanged: `tools/test_cache_file_formats.py` (155), the converted maps
  (`run_mcc_maps_test.sh`), the stock a10 and b30, a Custom Edition map
  with its tags moved on x86 (`HALO_CUSTOM_EDITION_RELOCATE=1`, refused
  before), `run_bots_test.sh slayer`, `run_netns_online_test.sh code` and
  `lobby` (a `--linux-net-vita` build), `run_map_share_test.sh`,
  `run_net_fuzz_test.sh`, and the Vita build.

- October 11 2026 (`feat/mcc-110`, the four branches on `feat/crossplay`):
  `test_mcc_cea_caches.py` 40 (also scripts with parameters refused, a
  mod's `bitmaps.map` read by item, offsets not an item's refused, 300
  seeded corruptions of the `bitmaps.map`), `test_scenario_validation.py`
  9 (recordings' weapon indices, camera fields of view, through
  `map_validate` on a converted map), `test_cache_file_formats.py` 158,
  `test_mcc_map_convert.py` 15; MCC's own ten campaign maps played
  natively as a mod (`run_mcc_native_test.sh` with
  `HALO_TEST_MCC_MOD` a folder linking MCC's `halo1/maps/*.map` and its
  `bitmaps.map`, read-only: no Ruby map was at hand): all ten load and run
  120 s, a10 won goes on to a30, a version 13 map in the maps folder is
  refused, and with MCC mods off the mods folder is not looked at;
  `run_hud_layout_test.sh` with MCC's b30 natively and Ruby's converted
  b30; the regression suites (netns code, coop, lobby, menus, swatmixed,
  dedicated and mapmid, map sharing, crossplay strict with crossmaps,
  settings, p2p lobby, language, network fuzz, Ogg sounds).

- The security review of October 11 2026 (fuzzing under AddressSanitizer
  and undefined-behaviour trapping, 32-bit): `map_validate`, which now
  loads version 13 maps as the game does, 416000 runs on MCC's a10, a30,
  b30, b40, c40 and d20 with words changed at random; 60000 seeded
  corruptions of made-up caches through `cache_file_report`; 4 million of
  the BSP vertex window reader; libFuzzer on the PNG reader (57.8
  million), the campaign files' JSON reader (67.9 million) and the sound
  cache file reader (8.9 million): no crash; one validator fix (a model
  part naming nodes of a model of none) and the game's zlib's double free
  (CVE-2002-0059), which map sharing reached, fixed.
- The shipped shader programs hold every program a shader tour of
  Ruby's ten maps (219) and of MCC's own ten played natively (215) drew.

Not tried on a Vita: the mods folder's listing and pictures, a version 13
map's load (its user RAM: 18-26 MB of tags, a megabyte while a BSP
loads), and its BSP switches with their vertices read from the memory
card.
