# Forge for Halo CE Vita (design)

Date: 2026-10-18. Status: phase (a) built as version 1 (below, "Version 1
as built"), on the owner's decisions of Oct 18; phase (b) built (below,
"Phase (b) as built": map variants saved and loaded, File Share, undo,
snapping, object properties); the rest a draft.
Part 2 of 3: (1) Halo 3-style menus (`docs/halo3-menus`,
`2026-10-18-halo3-menus-design.md`), (2) Forge, (3) File Share.

## Intent and why

The owner (Oct 18): "I imagine forge mode on the vita to be like halo reach.
It should let me do any map and spawn literally anything I want. From
building objects to vehicles, guns, and ai." And: "First as a sandbox to
test then we add the features to save and share to friends."

Halo CE never had Forge. Halo 3 brought it, Reach made it a level editor.
On the Vita, Forge gives Halo CE a second life: the same few maps, played
in new ways, made by the players on the console itself, with no PC, no
editing kit and no modding tools. It also uses what this port already has:
the menu kit (part 1), bots, map sharing, the distributed netcode, the tag
validator.

## Goals

1. **A monitor in any level.** Fly through walls, look anywhere, Halo 3's
   monitor controls fitted to the Vita's buttons and touch screens.
2. **Spawn anything the map has**: vehicles, weapons, equipment, scenery,
   machines, AI characters (each with a free actor, fighting as the
   map's own do). Grab, move, rotate, duplicate, delete.
3. **An object browser** in the Halo 3 / Halo Online look (the approved
   mockup `h3-look-v6.html` B), by category.
4. **Then objects from other maps** (the hard part, assessed below): a
   Hunter in Blood Gulch, Flood on the beach.
5. **Then layouts**: saved per map, loaded later, driving the netgame's
   spawns (player spawns, weapon and vehicle spawns, flags, hills), and
   shared with friends (part 3, File Share), every file from another player
   validated before the game reads it.
6. Works on the Vita first, at the Vita's frame rate, within its memory.
   PC and Linux get it too (same code).

## Non-goals (for now)

- Editing the level's structure (the BSP: terrain, walls). Forge places
  objects, as Halo 3's does.
- Scripting (Halo script) in layouts. A layout is data, never code.
- Live co-editing (several monitors in one game) before phase (c). Halo 3
  had it; it waits for the single-player loop to be right. *Built Oct 20:
  "Network Forge as built" below.*
- Custom Edition / MCC map content in the palette. Retail Xbox maps first;
  CE maps follow the same rules later (they are already loaded and
  validated by `custom_edition_cache.c` and `tag_validate.c`).

## Anti-goals (a failure even if it works)

- **Shipping Bungie or Microsoft data.** No tags, bitmaps, models or sounds
  from the maps in the repository, in a layout, or sent over the network.
  Anything cross-map is built on the player's own Vita from the player's
  own retail maps, as the shader cache and the CE installer are. The menu
  art follows part 1's rule: our own PNGs, Halo Online and Halo 3
  captures as references only.
- **A file that can hurt the player who loads it.** A layout from another
  player is untrusted input, the same as a map from `map_share.c` or a
  packet: bounded sizes, every count and index checked, every tag named by
  path and resolved against an allow-list, every float finite and inside
  the map. A crash on a crafted layout is a security bug, not a bug.
- **Breaking the game when Forge is off.** Nothing changes until Forge is
  asked for (the spike: no line, no state, no input taken).
- **A sandbox that drops the frame rate in normal play**, or memory taken
  from campaign maps that have none to give (they have under 1 MB of tag
  cache free: below).

## What exists to reuse

Searched Oct 18: upstream OpenCE (`OpenCommunityEdition/OpenCE`, CC0; our
clones under `~/github/vitaprojects/opence`), and the Halo CE community.

| Project | What it has | Licence | Use |
|---|---|---|---|
| OpenCE (upstream, `7524ba6`) | no Forge, no object spawner, no monitor | CC0 | nothing to port. Its CE map loader, tag validator and map sharing (already ported here) are what Forge stands on |
| Forge CE / Forge Island (Sledmine, github.com/Sledmine/Forge) | Chimera (client) + SAPP (server) Lua Forge: monitor mode, pick and place, rotate by yaw/pitch/roll steps, `.fmap` JSON layouts keyed by tag path, "reflection" of forge objects into the scenario's netgame spawn tables, rcon sync, a store pushed to joiners | **none stated** (no LICENSE file, so all rights reserved) | **ideas only, no code.** Its lessons are taken below: tag paths (not indices) in the file, objects of unknown tags skipped, spawns reflected into netgame tables, one pre-built Forge map (Forge Island) holding every forge object |
| lua-blam, Mimic (Sledmine) | memory access for Chimera/SAPP; object/AI sync | none stated | ideas only |
| Chalwk's HALO-SCRIPT-PROJECTS | SAPP Lua: object spawner (tag class, path, x y z, rotation, respawn time, radius), item spawner, custom spawns | MIT | ideas only: SAPP's API is not ours. Its spawner's record (class, path, place, rotation, respawn, radius) matches the layout record below |
| Halo 3, Reach, Halo Online (ElDewrito) | the reference look and controls | proprietary | references only, outside the repo (`~/halo-test-out/ho-shots/`) |

The engine itself has most of the pieces (found for the spike):
`object_placement_data_new` / `object_new` / `object_delete` /
`object_set_position` (`source/objects/objects.c`), the cheats' object
spawning (`cheat_all_vehicles`, `source/game/cheats.c`), the beta's flying
camera (`source/camera/flying_camera.c`, the director's `debug_camera_load`),
`ai_attach_free` (`ai_scripting_attach_free`, `source/ai/ai_script.c`),
`hud_print_message`, and the port's tag schema (`port/linux/game/tag_schema*.c`),
which lists every reference, block and data of every tag group.

## Phases

**(a) Local sandbox.** Monitor mode in any level, local game only.
Spawn the loaded map's objects, grab, move, rotate, duplicate, delete. AI
characters with free actors. The object browser. No saving. Budget limits
on. This is the spike made real, with the browser and the controls below.

**(b) Save and load layouts per map.** A layout file (format below) per
map, kept on the memory card (`ux0:data/haloce-vita/forge/<map>/` as
built). A Forge lobby
(map picker, layout picker) from the Halo 3 menu kit. "Load layout" starts
the map with it applied. Undo/redo. Grid and angle snapping. Object
properties (team, respawn time, spawn at start). Objects from other maps
arrive here, as "palette packs" (assessed below), on multiplayer maps
first.

**(c) Layouts as netgame variants, and sharing.** A layout can carry the
netgame's spawns (player spawns per team and game type, weapon and
equipment spawns with respawn times, vehicle spawns, flag and ball spots,
hills, teleporter pairs), read by the game engine in place of (or added
to) the scenario's. A host sends its layout to joiners in the lobby as
`map_share.c` sends a map. File Share (part 3) shows, saves, deletes and
shares them. Every received layout passes the validator before anything
reads it.

## The monitor: camera and controls

The spike's monitor (proven on the harness): the player's unit stays where
it stood, hidden and invulnerable, its controls not read; the director's
flying camera flies from the player's eyes by the monitor's controls; on
leaving, the player stands where the monitor was, as Halo 3 drops its
Spartan. For the real thing the unit should also leave the world (no
collision, not a target): Halo 3 swaps bipeds; ours can disconnect the unit
from the map (`object_disconnect_from_map`) and reconnect it at the
monitor's place.

Halo 3's diagram (the owner's list: fly up/down, thrust, grab, rotate,
delete, item menu, drop/pick up), mapped to the Vita. The Vita has no stick
clicks and no second pair of shoulder buttons, so the table uses the
port's default layout (Cross = A, Circle = B, Square = X, Triangle = Y,
L/R = the triggers, D-pad down = left stick click, D-pad up = right stick
click, D-pad left/right = Black/White). Halo 3's own buttons are written as
the owner's diagram has them, **to be checked against it** (open question
1).

### Buttons

| Action | Halo 3 (360) | Vita | PC (keyboard and mouse) |
|---|---|---|---|
| Fly (move) | left stick | left stick | W A S D |
| Look | right stick | right stick | mouse |
| Fly up | A (hold) | Cross (hold) | Space |
| Fly down | left stick click (hold) | D-pad down (hold) | C / left Ctrl |
| Thrust (4x speed) | hold a trigger while flying | L (hold, nothing held) | Shift |
| Grab / drop the object under the crosshair | right trigger | R | left mouse button |
| Rotate the held object | hold, then right stick | L (hold) + right stick left/right | right mouse button + mouse |
| Push / pull the held object | | L (hold) + right stick up/down | mouse wheel |
| Delete (held, or under the crosshair) | Y | Triangle | Delete |
| Item menu (object browser) | X | Square (nothing aimed at) | Q |
| Edit the object under the crosshair (team, respawn, properties) | X on an object | Square (aiming at one) | Q on an object |
| Duplicate the object under the crosshair | (Reach) | Circle | middle mouse button |
| Monitor / player | D-pad up | Select + D-pad up (the owner, Oct 18: D-pad up stays zoom) | Tab, or F1 + Z |
| Speed (slow, normal, fast), magnets / grid snap | (Halo Online) | D-pad right / D-pad left | 1 / 2 |
| Pause, Forge settings, save layout | Start | Start | Esc |
| Scoreboard / budget | Back | Select | F1 |

The spike implements: fly (left stick, Cross, D-pad down), look, thrust on L,
R grab/drop, L + right stick rotate and push/pull, Triangle delete, Square
spawn the palette's current object, D-pad left/right step the palette
(instead of speed and snap), D-pad up leave. Entering is the console's
`forge` command; the D-pad up toggle comes with the Forge game type, so
zoom (D-pad up's job in play) is untouched elsewhere.

### Touch (the Vita's own)

| Touch | Monitor, nothing held | Holding an object | Object browser |
|---|---|---|---|
| Front tap | select the object under the finger (a pick ray from the touch point, not the crosshair) | drop it there | pick the row / tab |
| Front drag (one finger) | | slide it along the ground under the finger | scroll the list |
| Front two-finger twist | | rotate about up (yaw), snapping to the angle step | |
| Front pinch | | push / pull | |
| Rear drag | look (fine) | pitch / roll the object (fine) | |
| Rear touch zones | as the player's Touch zones settings, off by default in Forge | | |
| Front top-right corner | toggle grid snap ("Magnets Enabled", Halo Online's top-left notice) | | |

The rear pad's guard (`HALO_TOUCH_REAR_GUARD`) applies, so a resting hand
does not rotate anything.

## The object browser

The approved mockup (`h3-look-v6.html` B): the game behind, a Forge HUD
top-left (map, budget used / budget), the object to place as a blue ghost,
and a side window on the right: title strip, kind tabs, a breadcrumb, the
list with the orange selection bar and a count per row ("4 placed"), a
help pane ("From the Library. The first one loads about 6 MB; the rest
are free."), and the button strip (Place, Back, Hide, Search).

Tabs and categories (each category lists only what the palette holds):

| Tab | Categories | Tag groups |
|---|---|---|
| Blocks | Walls, Ramps, Bridges, Platforms, Cover, Rocks, Trees | `scen` (the map's scenery; forge blocks would be our own models: open question 6) |
| Vehicles | Human, Covenant | `vehi` |
| Weapons | Human, Covenant, Grenades, Power weapons | `weap`, and grenades' `eqip` |
| Equipment | Power-ups (overshield, camo, health), Ammo | `eqip` |
| Characters | Marines, Covenant (Grunts, Jackals, Elites, Hunters), Flood, Sentinels | `actv` (each spawns its unit with a free actor), `bipd` |
| Scenery | Props, Machines, Lights, Teleporters | `scen`, `mach`, `ctrl`, `lifi` |
| Game | Player spawns, Weapon spawns, Vehicle spawns, Flags, Hills, Teleporters, Kill zones | layout records with no object of their own (phase c) |

The palette's names come from the tag's path (`characters\hunter\hunter` ->
"Hunter"), with a small table of our own friendlier names. Cinematic tags
(`cinematics\...`, `levels\a10\devices\...`, the Pelican's cinematic
variants) are left out by a deny-list; the spike lists them all.

The browser is drawn by the settings panel's GXM overlay (part 1's
`vita_gxm.c` `menu_build`, in its Halo 3 look), not by widgets: it opens
over a running game without pausing it, as Halo 3's does. The Forge lobby,
the map picker and File Share are the menu kit's XML widget screens.

## How spawning works in the engine (from the spike)

- **An object of the loaded map:** `object_placement_data_new(&p, tag, NONE)`,
  position and facing, `_new_object_never_automatically_delete_bit` (so the
  garbage collector leaves a placed weapon alone), then `object_new(&p)`.
  Exactly what `cheat_all_vehicles` does. Works for `vehi`, `weap`, `eqip`,
  `scen`, `bipd`, `mach`, `ctrl`.
- **An AI character:** an actor variant (`actv`) names its unit
  (`unit_reference`); spawn that biped, then `ai_scripting_attach_free(unit,
  actv)` makes a free actor (no encounter, no squad). Proven on b30: a
  Hunter, an armored marine and an Elite commander spawned with actors, and
  they fought each other within seconds (screenshots). Vehicle crew variants
  (`warthog gunner`, `ghost pilot`) name no unit. The real thing places
  **squads**: a spawner record (variant, count, team, respawn) rather than
  single bipeds, as Halo Online and Reach do, and later firing positions.
- **AI on multiplayer maps is not proven.** Blood Gulch has pathfinding data
  (4916 pathfinding surfaces in its BSP), but its only actor variants are
  vehicle crew. Attaching one to the cyborg biped made no actor. It needs a
  real character, which means cross-map content. To test once a pack loads.
- **Moving:** `object_set_position` each frame while held, velocities zeroed;
  on drop, `_object_at_rest_bit` cleared so a vehicle falls and settles by
  the game's own physics (the spike's Warthog does).
- **Picking:** the nearest object whose bounding sphere the camera's line
  meets in front of the structure (`collision_test_vector` against the
  structure only). A collision vector against objects misses weapons and
  equipment on the ground, which have no collision model.
- **Threads:** the monitor runs on the main thread after the cameras
  update and before the tick thread starts (`main.c`), as the test commands
  do, so no object is touched while the tick runs.
- **Limits:** the port's object pool is 8192 objects and 8 MB
  (`halo_port_capacity.h`, Xbox 2048 and 1 MB), 256 actors. Forge's budget
  (open question 4) sits well inside it, sized for the Vita's frame rate,
  not its memory.
- **Network:** a spawned object is an ordinary object. In phase (c) a
  layout is applied identically on every machine at the map's start (the
  objects placed with the scenario's), so the distributed netcode
  (`network_objects.c`) sees them as the map's own. Live editing in a
  network game waits.

## "Anything from any map": the technical assessment

### What an object needs

An object is a tree of tags. Measured with the spike's `forge deps` (a walk
of every reference in the tag schema, transitively):

| Object (map) | Tags | Tag data (tag cache) | Bitmaps' pixels + sounds' samples (map file) | Groups (largest) |
|---|---|---|---|---|
| Warthog (bloodgulch) | 285 | 289 KB | 2.0 MB | 72 `bitm`, 61 `effe`, 53 `part`, 29 `snd!`, `mode`, `coll`, `antr`, `phys`, its gun's `weap` and `proj`, the gunner's `actv` |
| Rocket launcher (bloodgulch) | 197 | 243 KB | 1.0 MB | `bitm`, `snd!`, `effe`, `part`, `antr` (112 KB), `proj` |
| Teleporter base (bloodgulch) | 23 | 10 KB | none | `mode`, `bitm`, `senv` |
| Ghost (b30) | 286 | 266 KB | 1.7 MB | as the Warthog |
| Hunter (b30) | 200 | 657 KB | 3.5 MB | `antr` 442 KB, 69 `snd!`, `actr`, `actv`, `udlg` (dialogue), its fuel rod |
| Elite commander, energy sword (b30) | 384 | 1.27 MB | 5.2 MB | two `antr` (929 KB), 133 `snd!` (dialogue), 76 `bitm` |

So the classes that must come along are: the object's own group and its
parents' (`obje`, `unit`, `vehi`...), `mode` (models), shaders (`soso`,
`schi`, `senv`, `sgla`, `smet`, `sotr`), `bitm`, `antr` (animations, the
largest single tags), `coll`, `phys`/`pphy`, `snd!`, `lsnd`, `effe`, `part`,
`pctl`, `cont`, `deca`, `ligh`, `lens`, `glw!`, `jpt!`, `foot`, `wphi`/`unhi`
(HUD), `trak`, `ant!`, `mgs2`, `spla`; for a weapon its `proj`; for a
character its `actr`, `actv`, `udlg` and its weapon's whole tree.

### What maps already share

The same walk, compared by tag path with each map's tag list:

- The Warthog's 285 tags are **all already in** chillout, boardingaction,
  a10, b30 and d40. The multiplayer maps checked (bloodgulch, chillout,
  boardingaction) each carry the multiplayer vehicles (Warthog, Ghost,
  Scorpion), 15 weapon and 14 equipment tags, whatever the map places
  (the game variant may turn any of them on). So vehicles and weapons on a
  multiplayer map cost nothing: phase (a) already offers them. (To check
  on the other multiplayer maps; the Banshee is not among them.)
- c20 (the Library) lacks 120 of the Warthog's 285 (46 effects, 19 bitmaps,
  11 sounds, shaders, HUD).
- The Hunter on Blood Gulch lacks 115 of its 200 (57 sounds, 16 bitmaps, 11
  effects, its models and animations).

### The probe: the Warthog from bloodgulch into another map

The Warthog is already in nearly every map, so the probe asks what it
would take where it is missing (c20, the Library), and does the same for
a Hunter on Blood Gulch:

- **c20 lacks 120 of the Warthog's 285 tags: 198 KB of tag data and 1.1
  MB of pixels and samples.** c20 has 4.8 MB of tag cache free, so it fits
  without a larger cache.
- **Blood Gulch lacks 115 of the Hunter's 200: 618 KB of tag data and 3.1
  MB of pixels and samples.** 13.7 MB free: fits many times over.
- The Warthog's 285 tags have the same sizes, tag for tag, in bloodgulch,
  a10 and b30. That supports deduplicating by path for the retail maps
  (sizes, not contents, compared: a pack build should compare a hash of
  each tag's bytes, pointers masked).

What the import itself needs, from the code:

1. **Read the source map's tags.** The port already decompresses every
   Xbox map into the cache partition and reads its tag data at load
   (`cache_files.c`); a second, read-only reader of another map's header
   and tag data is new but small.
2. **Find each tag's bytes.** The schema walker (the spike's
   `forge deps`) already visits every root, block and data of a tag; it
   would collect their extents instead of summing them.
3. **Copy and relocate.** Copy the missing tags into free tag cache,
   rewrite every block and data address and every reference's name
   pointer by the move, and every tag index (`tag_reference.index`, the
   schema's tag index fields) to the target's new or existing tag. The
   tag instance table (`global_tag_instances`, inside the target's tag
   data) has no room for more tags: it needs a larger table of our own.
   `tag_relocate.c` does the address part for a whole map at load.
4. **Reset runtime fields.** The schema's reset fields (texture cache
   handles, sound cache slots, predicted resources) as a map load leaves
   them, then the validator over the new tags (`tag_validate.c`).
5. **Read their pixels and samples from the source file.** A bitmap's or
   sound's file offset points into its own map: the texture and sound
   caches must know which file, as a Custom Edition map's offsets already
   count in one space of several files (`custom_edition_cache.c`).
6. **Before the map's objects are placed**, never mid-game (option C's
   problems), and identically on every machine of a network game.

It is not cheap: 3 and 5 touch the loader and the caches, and it is the
pack loader (option B) under another name. The probe stops at findings.

### The memory budget

The tag cache is 22 MB (`TAG_CACHE_SIZE`): the map's tag data, then the
structure BSP loaded after it. Bitmaps and sounds stream through the
texture cache (22 MB) and the sound cache (4 MB) from the map file, so an
imported object's pixels and samples cost contention, not a reservation.
Measured (`forge tags`):

| Map | Tag data | Largest BSP | Tag cache free |
|---|---|---|---|
| bloodgulch | 7.0 MB | 1.2 MB | **13.7 MB** |
| chillout | 6.7 MB | 0.5 MB | **14.7 MB** |
| sidewinder | 7.0 MB | 1.9 MB | **13.1 MB** |
| a10 | 13.5 MB | 8.2 MB | 0.3 MB |
| a30 | 14.6 MB | 6.8 MB | 0.5 MB |
| b30 | 15.0 MB | 6.3 MB | 0.7 MB |
| b40 | 16.2 MB | 5.6 MB | 0.2 MB |
| c10 | 15.8 MB | 5.7 MB | 0.4 MB |
| c20 | 8.5 MB | 8.6 MB | 4.8 MB |
| d40 | 14.3 MB | 7.5 MB | 0.1 MB |

Multiplayer maps have 13 to 15 MB of tag cache free: room for every
Covenant and Flood character at once (a character is 0.7 to 1.3 MB of tag
data, shared tags counted once). Campaign maps have almost none: importing
there means a larger tag cache, as the port already gives Custom Edition
maps (23 MB, 34.5 MB with OpenSauce's upgrade:
`CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED`), paid from the Vita's main
memory for every level. On the Vita that is the risk to measure first
(open question 5).

### Options

**A. The loaded map only.** Free, done by the spike. On multiplayer maps
that is already every multiplayer vehicle, weapon and power-up, plus the
map's scenery.

**B. Palette packs, built on the Vita, loaded with the map (recommended).**
A pack is a curated set of objects ("Covenant", "Flood", "Marines",
"Forerunner props", "Vehicles: Banshee, Wraith, Scorpion"), each listed by
tag path and source map. The first time a pack is wanted, the game builds
it on the Vita from the player's own retail maps (as the shader cache and
the CE installer are built): it walks each object's closure (the spike's
walker), copies those tags, their bitmaps' pixels and sounds' samples into
one pack file on the memory card, with pointers made offsets (as a cache
file's are). A layout names the packs it uses. When the map loads, the
loader appends the pack's tags after the map's in the tag cache, rebases
its tag indices and pointers (the port's `tag_relocate.c` already relocates
a whole map), resolves duplicates by path to the map's own copy, runs the
tag validator on them (`tag_validate.c`, as for a CE map), and reads their
pixels and samples from the pack file through one more file range, as a
Custom Edition map's offsets already count in several files
(`custom_edition_cache.c`). Pros: nothing of Bungie's is shipped or sent
(a layout names packs, the packs are built locally, a peer compares a pack
by its recipe's hash); the tag set is fixed when the map starts, so the
network, saved games and the game's runtime tag fields (sound cache slots,
texture handles, predicted resources) behave as for any map; memory known
before loading. Cons: building a pack takes a minute and needs the source
maps present; on campaign maps it needs the larger tag cache.

**C. Live import while playing.** Pick a Hunter in the browser and the
game opens b30, walks the closure, copies and relocates it into free tag
cache, mid-game. Rejected for now: per-tag partial relocation is new code
in the most dangerous place (every pointer of every tag), runtime fields
in those tags would have to be initialised as a map load does, the
memory card's reads would stall the frame, two machines would have to
import in step, a revert or a saved game would have to bring the tags
back, and tag cache fragments would never be returned. Pack loading (B)
is the same machinery done once at a safe point.

**D. Pre-baked "forge maps" shipped with the game** (Forge CE's Forge
Island). Ruled out: they would be Bungie's data. A map **we** make (our own
BSP and blocks) is a separate, later project (open question 6).

**Recommendation:** A for phase (a); B for phase (b), on multiplayer maps
only, starting with one pack (Covenant characters: the AI-on-MP test). The
pack builder and loader are the riskiest new code in Forge and get the
fuzzing the map loader got. Campaign maps get packs only once the larger
tag cache's cost on the Vita is measured. C is not built.

## The layout file

Binary, little-endian, fixed-size records, so the parser is small and
fuzzable (the port's `map_share_protocol.c` and its `net_fuzz_*` are the
model). Tag paths, never tag indices (Forge CE's lesson: a path survives
another map version; an object whose tag is missing is skipped, said in
the log, and counted on screen).

```
header (64 bytes)
  magic        "HFRG"              4
  version      u16 (1)             2
  header_size  u16 (64)            2
  flags        u32                 4   (bit 0: has netgame spawns)
  map_name     char[32]            32  (the scenario's short name: bloodgulch)
  map_hash     u32                 4   (the map's tag data checksum; a
                                        mismatch warns, does not refuse)
  pack_count   u16                 2
  path_count   u16                 2
  record_count u16                 2
  meta_size    u16                 2   (title, author, description: UTF-8)
  payload_size u32                 4
  checksum     u32                 4   (CRC-32 of everything after the header)
packs        pack_count  x { recipe hash u32[2], name char[24] }
paths        path_count  x { group tag u32, length u8, path char[length] }
records      record_count x 48 bytes {
  path index u16, kind u8 (object, squad, player spawn, weapon spawn,
  vehicle spawn, flag, hill, teleporter in/out, kill zone),
  team i8, position f32[3], yaw/pitch/roll f32[3], respawn s u16,
  count u8 (a squad's), game type mask u16, variant u8, flags u8,
  reserved u8[13] (zero) }
meta         meta_size bytes
```

As built (phase b, `port/linux/game/forge_files.c`), with these
adjustments, each for a reason:

- **The name and extension.** Halo 3's "map variants" in the UI; the file
  is `<name>.hmv` ("Halo map variant"), the magic stays `HFRG`.
- **The meta** is four UTF-8 texts, each ended by a zero byte and filling
  `meta_size` exactly: title (at most 31 bytes, not empty), author (31),
  description (127) and the date saved (`YYYY-MM-DD HH:MM` or empty). The
  design named three; File Share shows the date, which a file's own time
  would lose when the file is sent to a friend.
- **Kinds.** Teleporter in and out are two kinds (ten in all: object,
  squad, player spawn, weapon spawn, vehicle spawn, flag, hill, teleporter
  in, teleporter out, kill zone), so a record is one end of a pair. A kind
  that names an object (object, squad, weapon spawn, vehicle spawn) needs
  a path of a group it takes (a squad an `actv`, a weapon spawn `weap` or
  `eqip`, a vehicle spawn `vehi`); the others name none: path index
  `0xFFFF`.
- **Flags.** Header bit 0 must say exactly whether any record is a
  netgame's (not an object); a record's flags have bit 0, placed at the
  start; every other bit of both is zero.
- **Version 2** (Oct 18 2026): a character's AI team and AI off (the
  first reserved byte, flag bit 1; "AI teams, AI on/off and NEW ROUND as
  built" below). Version 1 is read as ever.
- **Ranges.** Team -1 (neutral) to 7, respawn at most 600 s, count at most
  16 (an object's 0 or 1), game types within the five (mask `0x1F`),
  variant at most 15; positions within +-5000 (the BSP's world bounds,
  with a 10 world unit margin, are checked when the variant is applied:
  the validator has no map), angles within +-pi.
- **A pack** is `{ recipe hash u32[2], name char[24] }` as designed, the
  name 1 to 24 of `[a-z0-9_ -]` with zeros after it. Version 1 writes none
  (packs are another branch's), and reads them as the format says.
- **A path** may not appear twice (same group and path), nor have an empty
  part (`\\`) or end with `\`.

### Validation (every layout, ours too)

Checked, in this order, before the game sees a byte, in a pure C module
with no game dependencies (fuzzed as the network messages are):

- the whole file at most 64 KB; header size, version and magic exact;
  `payload_size` equal to what the counts make; the checksum;
- counts within limits: paths <= 256, packs <= 8, records <= the Forge
  budget's object limit (open question 4), meta <= 2 KB;
- every path: length 1 to 127, characters `[a-z0-9_ \\-.]` only, no `..`,
  no leading `\`; its group one of the forge-able groups (`vehi`, `weap`,
  `eqip`, `scen`, `bipd`, `mach`, `ctrl`, `lifi`, `actv`), never `scnr`,
  `sbsp`, `hsc` or anything else; resolved by name against the loaded map
  and its packs, the tag's group checked again, else the record skipped;
- every record: path index < path_count; kind and team in range; every
  float finite, positions inside the BSP's world bounds (and the flying
  camera's +-5000 assert), angles reduced to +-pi; counts and respawn times
  capped; reserved bytes zero;
- strings (title, author) valid UTF-8, terminated, shown through the
  font's own glyph check, never used in a path or a format string;
- a layout from another player is never written outside the Forge folder,
  its file name made by us (a hash), not taken from the sender.

Applied only after every check passed: a layout is applied whole or not
at all. A record whose tag is missing (a pack not built) is skipped, not
an error.

### Netgame spawns (phase c)

The scenario's netgame blocks (`netgame_flags`, `netgame_equipment`, the
player starting locations, the vehicles' multiplayer spawns) are fixed-size
in a cache file (Forge CE reuses disabled slots for this). Natively we do
better: the game engine's spawn queries read the scenario's entries and
then the layout's (or only the layout's, a per-layout choice), from the
port's own tables, built when the layout is applied. A host sends its
layout (and the pack recipes it needs) in the lobby through the map sharing
channel (`map_share.c`: rate limits, resume, SHA-256), and each joiner
validates it as above before the countdown can end.

## What the Halo 3 menu kit gives Forge

From part 1 (`2026-10-18-halo3-menus-design.md`):

- the look: panel gradient, title and button strips, the orange selection
  bar, ice-blue values, PlayStation glyphs in the bottom strip, the free
  condensed font;
- list, tab and picture-and-detail screens as XML widget screens (the
  Forge lobby, the map picker, the layout picker, File Share), and
  Halo 3's "Select Map" picker pattern (list, picture, description);
- the main menu's **Forge** and **File Share** items, hidden until these
  parts ship;
- the settings panel's GXM overlay in the Halo 3 look, which the object
  browser and the object properties window reuse over the running game;
- nameplates (an author's name on a layout).

Forge adds: the in-game Forge HUD (top-left map name, budget, notices such
as "Magnets Enabled", "Movement Speed: Fast"), the blue ghost of the object
to place, the browser window, and the object properties window.

## The spike

Branch `feat/forge-sandbox`, `port/linux/game/forge.c` (labelled a spike).
Nothing changes until the console's `forge` command, and then only in a
local game.

- `forge` / `forge on|off`: the monitor (hidden, invulnerable unit; the
  flying camera; the player back at the camera on leaving).
- `forge spawn <name>`, `forge grab`, `forge drop`, `forge turn <deg>`,
  `forge distance <wu>`, `forge delete`, `forge list [group]`,
  `forge camera x y z yaw pitch`, `forge where`.
- The controller in monitor mode, as the table above says (spike subset).
- Probes: `forge deps <name> [> file]` (the closure walk and sizes),
  `forge tags <file>` (a map's tags and its tag cache room).
- `forge press` / `forge stick`: the controller for tests.

Proven on the harness (`port/vita/tests/run_forge_test.sh`, screenshots in
`~/halo-test-out/forge-spike-20261018/`): Blood Gulch, a Warthog spawned,
turned 90 degrees, carried with the camera, dropped (it falls and lands); a
rocket launcher and a teleporter spawned; the teleporter grabbed, moved,
deleted; the palette stepped and spawned from with the controller's
buttons; the player back by the monitor's button. b30, a Hunter, a marine,
an Elite commander (each with an actor) and a Ghost spawned; the
characters fought. With no command, no Forge line at all.

Not in the spike: the browser, touch, PC mouse look in the monitor, the
unit taken out of the world, snapping, undo, budget, saving, anything
networked, anything cross-map. It was not built or run for the Vita.

## Version 1 as built (Oct 18 2026)

Branch `feat/forge` (the spike merged into `docs/halo3-menus`). The owner's
decisions: multiplayer maps first, the map's own objects only, a
monitor/Spartan toggle (D-pad up, then Select + D-pad up), a budget of 256 objects and 32 AI,
our own building blocks later, Halo 3's look; save and share later.

- **Entry.** The main menu's FORGE opens the Forge lobby
  (`halo3/forge_lobby.xml`): FORGE LOBBY, Network: Local, Map (Select Map:
  the Xbox's 13 multiplayer maps whose files are in the maps folder, each
  with ui.map's own picture and description), START FORGE; the map's
  picture with our Forge emblem, "Edit Objects on <map>". FILE SHARE keeps
  its "coming later" dialog.
- **The game.** START FORGE starts a local game (`forge_game_start`):
  free for all **Slayer**, score to win 999, respawn at once, no suicide
  penalty, no time limit (the variant options' default), the map's own
  weapons and vehicles; `main_disallow_persistent_storage`, so no saved
  game is read or written. Why Slayer: a game engine has to run on a
  multiplayer map for the player to respawn (no game engine means the
  campaign's death and revert), Slayer's only score is a player's kill of
  a player (alone, only a suicide's -1; an AI's kills are not a player's),
  so 999 is never reached, and it places nothing of its own (no flags,
  balls or hills). The session is the requested map's (`forge_map_loaded`);
  the main menu or another map ends it.
- **Monitor and Spartan.** As the spike, plus: Select + D-pad up (Back +
  the right stick's click; first D-pad up alone, changed by the owner the
  same day so that D-pad up stays zoom) flips both ways
  (Spartan at the monitor's place; the monitor from the Spartan's eyes);
  a respawned unit is hidden as the last; zoom is undone for the monitor.
  The Vita's crouch toggle does not latch D-pad down, and the touch zones
  are off, while the monitor flies (`vita_pad.c`).
- **The object browser** (`halo3/forge.xml`, in every level's map):
  Halo Online's window at the right over the running game (not paused):
  OBJECT BROWSER and the budget in the title strip, a crumb (OBJECTS »
  VEHICLES), the categories (Vehicles, Weapons with the grenades,
  Equipment, Scenery with machines and controls, Characters where the map
  has actor variants that name a unit: none on a multiplayer map), then
  each category's objects by readable name (our own names for the
  multiplayer maps' objects, translated; else the tag's name as words) and
  the count placed, a pane, ✕ Place ○ Back L R Category. A on an object
  closes it and puts the object in the monitor's hand. Not offered: a
  vehicle's gun, the flag and the ball, anything of a cinematic's. A
  placed object is what was chosen: the game type's remap (a multiplayer
  game's flamethrower made a rocket launcher, a vehicle set) is skipped
  for Forge's placements (`objects.c`, `forge_placing_exact`).
- **The Forge menu** (Start in a Forge game; a multiplayer map has no
  pause screen of its own for a game alone): RESUME, PLAY AS SPARTAN /
  MONITOR, LEAVE FORGE (asked first: nothing is saved yet), the controls.
- **The HUD** (monitor only): "FORGE · <map>", the budget, notices
  ("Movement Speed: Fast", "Placed Warthog", refusals) at the left, the
  crosshair "( + )", "[ + ]" with the name of the object under it (ice
  blue) or held (orange). The object under the crosshair is the first the
  camera's line meets by collision model, or for a weapon or equipment by
  its bounding sphere.
- **Budget.** Objects this session placed that still exist (256), and the
  characters among them still alive (32); past either, the choice is
  refused with a notice. The map's own objects do not count.

### Controls as built

| Action | Vita | PC (keyboard and mouse) |
|---|---|---|
| Fly | left stick | W A S D |
| Look | right stick (and the gyro if on) | mouse |
| Fly up / down (held) | Cross / D-pad down | Space / C or left Ctrl |
| Faster (4x) | L held, nothing held | Shift, or right mouse button / G |
| Grab / drop | R | left mouse button |
| Rotate the held object (rotate mode) | hold L: right stick across yaws, up/down pitches; with R held too, across rolls | hold right mouse button / G: mouse across yaws, up/down pitches; with the left button too, across rolls |
| Push / pull it | L + left stick up/down | mouse wheel, or right button + W/S |
| Rotation snap (Off, 15, 45, 90) | L + D-pad right | right button + Q |
| Delete (held, or under the crosshair) | Triangle | Delete |
| Object browser | Square | E or R |
| Copy (held or under the crosshair) | Circle | F |
| Set upright | D-pad left | X |
| Movement speed (slow, normal, fast) | D-pad right (L not held) | Q |
| Monitor / Spartan | Select + D-pad up, together | Tab, or F1 + Z |
| Forge menu | Start | Esc |
| In the browser | D-pad / ✕ / ○ / L R | arrows or the mouse / Enter / F / G, left mouse |

The combo is Back + the right stick's click in the Xbox's terms (the Vita's
Select + D-pad up by default, mirroring Select + D-pad down for the settings
panel). In a Forge game `xinput_sdl.c` (`forge_combo_filter`) keeps it from
the game while it is held and until both buttons are let go, so it neither
zooms nor shows the scoreboard; Back alone reaches the game after 200 ms
(the scoreboard, held), the click alone is the Spartan's zoom, and nothing
for the monitor.

The Vita column is the default Button layout's; a remapped Xbox button
moves with it (Forge reads the Xbox buttons). The PC column is the
keyboard's controller (`xinput_sdl.c`) plus Shift, Delete, Tab and the
wheel, which the monitor reads itself (`halo_linux_forge_keys`): the wheel
and Tab press Y in play, never a delete here.

### Tests

`port/vita/tests/run_forge_test.sh` (v1: the whole loop by the
controller, from the main menu to leaving; budget; b30's characters and
the AI budget; off) and the menus test's `h3touch` (FORGE opens the
lobby). `HALO_TEST_PAD` gained `level` (wait for a level), `@name`
(a screenshot) and stick steps for it.

## Phase (b) as built (Oct 18 2026)

Branch `feat/forge-files` (from `integration/1.1.1-beta.4`). The owner's
calls on the open questions: multiplayer maps first, sharing with friends
and lobbies only (no public list, no server upload), "map variants" as
Halo 3 named them.

- **The file and its validator** (`port/linux/game/forge_files.c`, pure
  C, no game dependency): the format above, version 1, `.hmv`. Every file,
  ours too, passes `forge_file_decode` whole before anything reads it:
  the checks of "Validation" in their order, each count bounded before it
  is used, each float's bits looked at before it is a float, the layout
  handed back only when every check passed (a refused file leaves an empty
  layout). The writer runs the same checks (a layout that would not be
  read back is not written).
- **The store** (`port/linux/src/forge_store.c`, the platform layer: this
  machine's paths): `ux0:data/haloce-vita/forge/<map>/<name>.hmv` on the
  Vita, `<save root>/forge/<map>/` on Linux and PC. Only a folder whose
  name is a map's (`[a-z0-9_]`) and a file whose name is a map variant's
  (`[a-z0-9_-]{1,28}.hmv`) are ever opened; each is read through the
  validator with its folder's map expected (another map's is refused). A
  save writes `<name>.hmv.tmp`, checks every write and the close, reads it
  back through the validator, and renames it in (the old file renamed
  `.old` first, deleted after: the Vita does not rename over a file);
  whatever fails, the temporary file is deleted and the old one stays.
  The card must have the file and 256 KB free first. A save's name is made
  from its title (letters and digits, `_` for the rest, numbered past the
  names there); the session's own variant's file is replaced when saved
  under its title.
- **Saving** (the Forge menu's SAVE MAP VARIANT): the name typed on the
  Vita's keyboard (the variant's own title, else "<Map> Variant"); where
  no keyboard is (a PC release build) it saves under that name at once.
  The session's placed objects are saved (`forge_placed`: tag path and
  group, place, facing as yaw, pitch and roll, team, respawn, flags), the
  author the profile's name, the date. What came of it is said under the
  menu ("Saved "Gulch Test": 2 objects.", "The memory card is full.
  Nothing was saved.", "Couldn't write it (is the card full?). Nothing was
  saved."). The map's own objects are not saved (as the budget does not
  count them): deleting one of the map's is not kept.
- **Loading**, Halo 3's way (the owner, Oct 18): the lobby's MAP row
  opens SELECT MAP, and choosing a map shows that map's variants in the
  same picker: the map itself first (the original), then the player's
  saved map variants of it (rows that scroll, the triggers a page), each
  with its objects, author, date and description; B goes back to the
  maps. The row then reads "MAP: BLOOD GULCH - MY FORT". Damaged files
  are not offered (File Share lists and deletes them). The Halo 3
  multiplayer lobby's MAP picker still lists the maps only: map variants
  are not playable in custom games until phase (c), so it offers only
  what can be played. START FORGE reads and checks the variant again first (one that
  cannot be read starts nothing: the lobby says why), and the session
  places it as it starts: each path found among the palette's tags (the
  browser's allow-list: nothing else is ever placed), the group checked,
  each place inside the structure's world bounds, within the budget. A
  record whose tag the map does not have, outside the world, or past the
  budget is skipped and counted ("Map variant X: 5 placed, 1 skipped"),
  never fatal; another map copy (the hash) is logged.
- **Undo and redo** (the Forge menu, with how many steps each has): 32
  steps, an object placed (its drop), deleted, moved or turned. Objects
  are named by a placed id that an undo gives back; a step that cannot be
  done any more is said and skipped. `object_delete` only marks an object
  (a paused game deletes it later), so such objects count as gone.
- **Snapping** (the Forge menu's SNAP): the object held shown and let go
  on a 0.25 world unit grid along the ground, turned in 15 degree steps.
- **Object properties** (Square while holding an object you placed): a
  window at the right, team (neutral, red, blue), respawn (never to 180
  s), placed at the start; saved in the record. They change nothing in a
  Forge game yet: they are for phase (c)'s netgame variants.
- **File Share** (the main menu's FILE SHARE, `halo3/fileshare.xml`):
  MAP VARIANTS (every map's, by map, the chosen one's picture, map,
  objects, author, date) and GAME VARIANTS (the Xbox's game types,
  listed only: they are made in MULTIPLAYER, GAME), L and R or left and
  right between them. A opens the Forge lobby on the variant's map with it
  chosen, Square renames it (the keyboard; the file name stays), Triangle
  deletes it, asked first. Touch as the other Halo 3 screens (a row, the
  button strip's labels).
- **The original menus** (the owner: "keep everything in the overlay menu
  in case people want the og menu"): with Menu style Halo CE the settings
  panel's Game tab ends with **Forge and File Share**, a page of its own:
  Map (the multiplayer maps in the maps folder), Map variant (None or one
  of that map's), Start Forge (asks the game, at its main menu, as the
  lobby's START FORGE: `SYSTEM_LINK_REQUEST_FORGE`, then closes the
  panel; in a level or a lobby it says to leave first), and under File
  Share every map variant (the first 32, six at a time), Square deleting
  one after a question. With Menu style Halo 3 the row is hidden (the
  Halo 3 main menu has FORGE and FILE SHARE); on the classic panel the
  Game tab's first headings give way to fit it, as they already do for the
  update check's lines (the Multiplayer tab has no room left).
- **Not built:** the blue ghost (the held object is the object itself:
  tinting or a translucent pass needs a render path of its own, and the
  debug-geometry lines the engine has are untested on the Vita's GXM);
  touch gestures in the monitor; netgame spawns; sharing (below).

### Scene packs in Forge as built

The lobby's EXTRA OBJECTS row (between MAP and START FORGE) chooses the
packs a session brings (`forge.c forge_packs`, in load order):

| Pack | Objects | Built from | Default | Tags | Tag cache | Pixels + samples (file) | Build (x86) |
|---|---|---|---|---|---|---|---|
| covenant_vehicles | Banshee, Wraith, Shade | b40, c40 | on | 430 | 857 KB | 6.3 MB (7.2 MB) | 1.3 s |
| dropships | Pelican, Covenant dropship | b30, a50 | on | 158 | 656 KB | 3.6 MB (4.2 MB) | 1.0 s |
| covenant_ai | 6 Covenant actor variants (10 offered with their kin) | b40 | asked for | 939 | 4964 KB | 30 MB (35 MB) | 1.4 s |

- Built in the background while the lobby is open (`scene_packs_prebuild`:
  built, not loaded with ui.map), its progress on the row's page and in the
  lobby's note; START FORGE refuses while a pack it brings is building (on
  the Vita a b40 build is tens of seconds: inflating b40 twice). Built once:
  the file stays in `t:\packs` until the recipe, the builder or the source
  map changes.
- Loaded as the Forge map loads, before its objects, in that order while
  the tag cache has room; one that does not fit is left out with the
  loader's reason, in the log and on the HUD. A pack whose objects the map
  has already is not loaded. The browser offers each object once (a pack's
  copy of a map's tag is the map's), the actor variants in an AI category.
- Map variants name the packs their records' tags come from (recipe name
  and hash); loading one brings them with the lobby's.
- Every Xbox multiplayer map carries the Warthog, Ghost and Scorpion tags
  (any game type may turn them on), so Forge offers them on every map from
  the map itself; none has the Rocket Warthog (Halo PC's), and packs are
  built from Xbox maps only: no multiplayer vehicles pack.
- The Covenant dropship places and stays as a large prop. The Pelican was
  one too (no player animation enters its `P-driver` seat, and of its
  riders' seats only `P-riderLF` and `P-riderRF`); since Oct 19 the
  recipe's `tune flyable_pelican` line (scene_pack.c, the builder's own
  tunings) makes it one players fly when the pack is built: the driver's
  seat takes the player's `W-driver` animations at the Pelican's own
  `driver enter` marker (before its nose, on the ground: walk up and press
  the action button; the seat stays invisible, as the campaign has it),
  the riders' seats the player had no animations for take their side's
  front ones (ten riders; the cargo seat left as it is), and our own speed
  (0.9 a tick at most, 0.045 more a tick; the campaign's 0.5 and 0.03,
  Extinction's 0.95 and 0.05). Nothing of Extinction is needed or used.
  With Extinction there too, both are offered: "Pelican" (the campaign's)
  and "Extinction Pelican" (its guns, co-pilot and eleven riders).
  `run_forge_test.sh pelican`.
- The settings panel's Forge route brings the packs on by default; choosing
  them there is left for later.
- Every multiplayer map with all three packs (run_forge_test.sh packmaps,
  Oct 18): each loads all three, 6478 KB of the tag cache held by packs,
  8837 KB free after them at the tightest (Blood Gulch; Sidewinder 8857 KB),
  9180 KB at the roomiest (Chiron TL-34); the browser offers 8 vehicles and
  10 AI on every map. So the Covenant AI would fit on by default; it stays
  asked for (its 35 MB file, its b40 build time on the Vita, and the AI's
  frame cost there).
- On the Vita, still to measure: a b40 pack's build (it inflates b40, 225
  MB, twice: tens of seconds), the AI pack's 35 MB on the memory card and
  4.9 MB of tag cache, and the frame cost of several AI (on the x86 harness
  a dozen fighting Covenant took the tick from 0.14 to 0.18 ms).
- GPU programs (the owner's Vita compiles none: they must be in
  shaders.pak): no new program written. Collected on the gxm-null harness
  (HALO_SHADER_COLLECT) through run_forge_test.sh's v1, packs, smallmap and
  panel cases and checked (tools/vita_shader_pack.py --check): every program
  the packs' Banshee, Pelican and Elite draw with is in the pack. One is
  not, in every Forge game and in none without Forge (a plain Blood Gulch
  slayer game: 64 of 64): 75acc2fa621a81e3.fp, one projected texture stage,
  most likely phase (a)'s HUD text (forge_draw, unchanged here). To add on
  Vita3K with the next pack (HALO_SHADER_PRECOMPILE).
- The gxm-null harness does not link on this tree's base (the menus' touch,
  891c2f77: vita_touch_pointer, vita_host_front_touches and
  vita_settings_panel_open have no Linux side); the collection above linked
  a throwaway stub of the three.

### Phase (b) tests

`port/vita/tests/run_forge_files_test.sh` (ASan and UBSan): the writer's
refusals, a round trip, a layout at every limit (within 64 KB), every
truncation, every bit flipped (refused but for the map hash's four bytes,
which only warn), huge counts, counts past the bytes, a payload size past
the file, bad paths (`..`, a leading `\`, capitals, `%`, a zero inside,
255 long, empty), scenario and script groups, the wrong map, every
record field out of range, bad meta, and 200000 random cases (half with
the checksum made right again so the checks after it are reached): no
crash, and every file taken writes back as itself. `run_forge_test.sh`:
`files`, `variants` (save, replace, load, a missing tag, damaged files,
a full card over the session's own file and as a new one), `edit` (undo,
redo, snapping, properties, the file read back with
`forge_files_test --dump`), `fileshare` and `fstouch`; v1, budget, b30
and off as before.

### Sharing in the lobby (phase c's transport): designed, not built

Not built in this branch, for two reasons: there is no network Forge game
yet (the lobby's NETWORK is LOCAL; a variant sent to a joiner could only be
saved, not played together), and any new lobby message is a network
protocol change (a network version bump that splits players from every
build before it, crossplay with PC included), which is the owner's call to
make with a release, not a branch's. The design, for when it is:

- **Who and when.** A host in a custom game lobby (and a Forge lobby
  once it has NETWORK) whose game uses a map variant offers it to each
  joiner in the pregame lobby; nothing is offered in a public game unless
  the joiner's Map downloads setting allows it (`HALO_MAP_SHARE_FROM`,
  as for maps). No public list, no server.
- **The channel.** `map_share.c`'s, on the connection to the host (its
  reliable stream), as a second kind of file: the query names a map
  variant by its map, its SHA-256 and its size; the host offers it or
  refuses; the joiner's player is asked ("<host> shares the map variant
  <title> (Blood Gulch, 12 objects). Save it?", the title shown through
  the font's glyph check, never in a path); then start, data, ack, done
  (SHA-256) as for a map. A variant is at most 64 KB, so one window holds
  it: no resume, no deflate, the in-game limits always.
- **The wire.** One new capability bit (`map variant`, after
  `in_progress`) in the query's and start's reason, so a host or joiner
  without it is refused as an older build is today; the query's name field
  carries `<map>/<hash>` instead of a map name only when the bit is set.
  That keeps the three messages of network version 18; if the reviewer
  wants a message of its own instead, the network version goes up by one
  and `network_wire.c`'s table and the wire contract test say which.
- **What the joiner keeps.** The bytes go to memory (at most 64 KB, the
  size offered checked first), never straight to the card. Only when all
  of them are there, the SHA-256 is the host's and `forge_file_decode`
  passes with the joiner's own map expected, is the file written, through
  `forge_store_save`'s temporary file and rename, under a name made by us
  (`forge_file_name_received`: `r<hash>.hmv`, never the sender's), in the
  joiner's own map folder; a file of that name already there is the same
  variant (the same hash) and is kept, never overwritten; one of another
  hash with the same title is saved beside it. A host that sends more than
  it offered, a bad hash or a refused file ends the transfer and is said.
- **Tests.** The protocol's new fields in `map_share_test.c` and
  `net_fuzz_map_share.c`; `run_netns_online_test.sh` gains a case: a host
  with a variant, a joiner that saves it, the file byte for byte the
  host's, and a joiner whose copy is damaged in flight refusing it.

### Left for later

- Netgame spawns (phase c): the layout's player, weapon and vehicle
  spawns, flags and hills read by the game engine; the properties (team,
  respawn, place at start) then act.
- Sharing in the lobby (above), and a network Forge game.
- The blue ghost, touch gestures in the monitor, packs for other maps'
  objects (another branch), campaign levels in the lobby, our own
  building blocks.

## Scene packs as built (Oct 18 2026)

Branch `feat/scene-packs`: option B's builder and loader, as infrastructure
of their own (`port/linux/game/scene_pack.c`: the pack, its builder, its
checks, no game state; `scene_packs.c`: a pack in the running game), first
used by the Halo 3 lobbies' Spartans (`menu_scene_objects.c`), and proven on
Forge's two cases: the Covenant's campaign vehicles and characters in Blood
Gulch.

- **Recipes** are our own text (`port/vita/app0/packs/*.txt`): `pack <name>`,
  `map <name>...` (the first of them in the maps folder is used), `tag
  <group> <path>` (the pack's first tags, in order: its objects), `strip
  <group>` and `strip <group> <path>` (left out: their references name
  none). menu_spartan (the multiplayer cyborg and its assault rifle, sounds
  and effects stripped), covenant_vehicles (Banshee, Wraith, Shade: b40 or
  c40), covenant_ai (grunts, jackals, elites, a hunter: b40, b30 or a50).
- **The file** (`t:\packs\<name>.pack`, the save root's t folder): a 128-byte
  header (sizes, the CRCs of the header and both sections, the recipe's hash,
  the builder's version, the source map's identity: its header and size),
  the tags laid out as a map's tag data (a tag header with no scenario, the
  table, names, the models' vertex and index buffers' headers, the tags, the
  buffers' bytes; linked at 0x5C000000), then the bitmaps' pixels and the
  sounds' samples (file offsets from 0x70000000).
- **The builder** inflates the source map to the end of its tag data
  (into the tag cache's free room, not the C heap), walks the recipe's tags
  and everything they need (every reference the tag layouts know, every tag
  index the schemas know), copies each tag and the models' buffers with every
  pointer the layouts know made the pack's (or none), then inflates the map
  again for the pixels and samples, in the file's order. Built lazily on a
  thread of its own the first time a pack is wanted, again whenever the
  recipe, the builder or the source map changes.
- **The checks** before anything reads a pack: the header bounded (counts,
  sizes, every section where it says, nothing past the file's end, its CRC),
  both sections' CRCs, then the tags moved by tag_relocate.c's walk with every
  pointer followed only into the pack and every reference checked against its
  own table (`halo_tag_relocate_pack`), then the tag validator over them with
  the pack as the only region and its data as the only file range, no
  scenario, structure bsp or globals allowed, and **no correction allowed**
  (`tag_validate_pack_tags`). A pack that fails is deleted and built once more,
  then given up for the map. The map's own walk and check state are kept, so
  its structure bsps load as before. `build/linux/scene_pack_test` builds,
  checks and fuzzes packs alone (cut short, grown, bytes changed with and
  without their CRCs made right, pointers made to point past the pack, counts
  made huge, header fields changed): every copy is refused or, if still a
  pack, loads clean twice; none crashes, also with the pack code, the walk and
the validator built with AddressSanitizer (which found one: a crafted root at
a pack's very end read past it, fixed in tag_relocate.c for maps too).
- **Loading** (between frames): the tags read right after the map's tags (the
  structure bsp loads at the top of the tag cache; the room between is the
  packs'), given tag indices after the table's, their file offsets moved to a
  range of the pack's own (four packs at once), their buffers registered, the
  table grown as menu_tags.c grows it; the bytes are reserved (a bsp's load
  does not clear them, its check does not take them for scratch, and a bsp that
  would overlap them is refused). Reads of a pack's pixels and samples are
  served from its file. Everything goes with the map.

Measured on the Linux harness (x86; the Vita's CPU is some 15-25 times
slower at this kind of work):

| Pack | Tags | Tag cache | Pixels + samples | File | Build | Load (checks included) | C heap |
|---|---|---|---|---|---|---|---|
| menu_spartan (bloodgulch) | 35 | 1137 KB | 868 KB | 2.0 MB | 130 ms, 49 MB inflated | 1.3 ms | +46 KB |
| covenant_vehicles (b40) | 430 | 857 KB | 6.3 MB | 7.2 MB | 1.35 s, 449 MB inflated | 4.3 ms | +72 KB |
| covenant_ai (b40) | 939 | 4964 KB | 30 MB | 35 MB | 1.36 s, 450 MB inflated | 18 ms | +88 KB |

In Blood Gulch (13.7 MB of tag cache free) covenant_ai leaves 9 MB free; at
ui.map menu_spartan leaves 19.8 MB. The C heap pays only the grown tag table
(32 bytes a tag) and the build's working memory; the texture and sound caches
read the pixels and samples as for any map. A campaign map's pack read from
b40 inflates b40 twice (~225 MB each): on the Vita that is tens of seconds,
the build's cost to bring down next (an index of inflate restart points, or
the cache partition's inflated copy of the map when there is one).

Shown in the harness (`port/vita/tests/run_scene_pack_test.sh`): Forge's
palette lists the pack's Banshee, Wraith and Shade and its ten Covenant actor
variants in Blood Gulch as soon as the pack loads (its palette is the loaded
tags'); a Banshee placed by Forge is ridden, flown up (60 world units) and
fired; an elite placed by Forge shoots the player down.

What Forge should call (nothing of Forge's was changed):
- `scene_packs_want(name)` from the lobby (builds on a thread; its state:
  building, ready, loaded, failed) and `scene_packs_load_now(name)` once the
  map has loaded, before its objects are placed (map load: forge_map_loaded);
  then rebuild the palette (`forge_palette_build`), which picks up the pack's
  objects by group as the map's;
- `scene_packs_roots(name, ...)` for a pack's own objects (the recipe's tags)
  and `scene_packs_tag(name, group, path)` for one by name; their categories
  are their groups' (vehi, weap, eqip, scen, actv...), as the browser's;
- still to come: a layout naming the packs it needs (by recipe name and hash),
  the same packs loaded on every machine of a network game, duplicates by path
  resolved to the map's own copy, and campaign maps (which need the larger tag
  cache).

## AI teams, AI on/off and NEW ROUND as built (Oct 18 2026)

Branch `feat/forge-ai-round` (from `fix/forge-fly-pitch`, insider.8 and the
monitor's fly fix). The owner, on insider.8: "a team function for the AI",
"enable or disable AI brain too", "start a new round in Forge mode".

- **The AI team** (the properties window's AI TEAM row, a character's only;
  `forge_files.h` `FORGE_AI_TEAM_*`), by the engine's own switches: the
  unit's `owner_team_index` and its actor's `meta.team_index`, as the
  scripts' `ai_set_team` gives them, then `ai_update_team_status` so every
  actor sees friend and foe again. The teams are the engine's
  (`game.h _game_team_*`); the friendly table is the diagonal only (no
  allegiance is made), so each team fights every other:
  - DEFAULT: the character's own (its unit's default team: the Covenant's
    for the Covenant AI pack's, the humans' for a marine; a unit of no AI
    team gets the engine's unused team 6: it fights everyone but its own
    kind). Shown as "DEFAULT (COVENANT)". Applied as it is placed (in a
    multiplayer game the engine gives a unit no team at all, which made
    two placed elites fight each other before);
  - PLAYER: the local player's own team (in a free for all game every
    player has one: team 0 in a Forge game): it fights for the player,
    never against, and fights every AI team;
  - HUMAN, COVENANT, FLOOD, SENTINEL: the AI teams, each the enemy of the
    others and of the player (a campaign map's own script may ally the
    humans with the player, as b30's does).
- **AI on/off** (the AI row, a character's only): off is the engine's
  `ai_braindead` (`actor_braindead`): the character stands frozen as placed
  (thinks, moves, fights not, as a cinematic's do; it can still be hurt);
  on again, it fights. The Forge menu's ALL AI flips every placed
  character (all off if any is on, else all on); kept per character.
- **NEW ROUND** (the Forge menu, Halo 3's Restart Round): asked, the menu
  closes; with the game running again `main_reset_map` resets the map as
  the engine's own map_reset does (the map's own objects as the scenario
  has them, its destroyed vehicles whole, the players created and spawned
  again, the scores and game time from zero). As the game state goes
  (`forge_map_resetting`, main.c) the layout is taken; after it, once the
  player has spawned, it is placed again (`forge_variant_apply`), each
  object at its placed spot, the characters alive with their AI settings,
  and the player is the monitor again where it flew (or the Spartan at a
  spawn point, if he was the Spartan). The console's `map_reset` keeps the
  layout the same way. The undo history is cleared (its steps named
  objects of the old round).
  - **The layout** is the editor's, not play's: each placed object keeps its
    placed spot (`forge_placed.home`), followed while it is held and for
    3 s after the editor lets it go, moves, turns or undoes it (it falls,
    settles; a character with its AI on only its fall), never by play (a
    Warthog driven or blown away, an elite walking). An object gone in
    play (equipment picked up, a grenade thrown, an object destroyed)
    keeps its entry with no object and comes back with the next round;
    only the editor's delete or undo takes an entry out. So the budget
    counts the layout: its objects, and its characters dead or alive.
  - **An unsaved layout** is kept: NEW ROUND places the session's layout
    as it is now (saved or not; nothing is written or read). The map's own
    objects come back as the map has them, as a save does not keep them
    either. A save is made from the placed spots too (a vehicle driven
    elsewhere is saved where it was placed).
- **Map variants, version 2** (`forge_files.c`): a record's first reserved
  byte is a character's AI team and flag bit 1 its AI off, both allowed
  only on an `actv` record (a character), the team below
  `NUMBER_OF_FORGE_AI_TEAMS`, and both zero in version 1, which is still
  read. A layout is written as the lowest version that holds it (version
  1 unless a character has an AI setting), so variants without AI settings
  stay readable by insider.8; a file read writes back as its own version.
  The fuzzer runs from both versions' samples; every bit of the version 2
  sample flipped is refused but for the map hash and the characters' AI
  fields (which have other good values, each checked in range).

### Tests

`run_forge_test.sh`: `aiteam` (an Elite on the player's team never hurts
the Spartan beside it; on the Covenant's it shoots his shield and body
down), `aioff` (AI off: never hurts him, stands still; ALL AI turns it on,
it fights), `round` (a Warthog shoved and wrecked, an Elite killed, an
overshield taken out of the world: after NEW ROUND all three whole at their
placed spots, the player respawned, the monitor where it flew), `aisave`
(saved as version 2 with a Flood Elite AI off and an Elite of the player's
team, the dump checked, started again from the panel's route: as saved);
v1's pad walks the two new rows. `run_forge_files_test.sh`: version 2's
round trip, refusals and fuzzing. The console's `forge aiteam`, `brain`,
`allai`, `ai`, `objects`, `round`, and the tests' `kill`, `destroy`,
`shove`.

## ALL AI fixed, and the other AI as packs (as built, Oct 18 2026)

Branch `feat/forge-ai-more` (from insider.10, `96dedf28`). The owner, on
insider.10: "The ALL AI off button doesn't work too well, teams work, we
should add the other AI."

### What was wrong with ALL AI

Found on the harness (`forge ai` now logs each character's aim, facing, actor
mode, weapon, body and shield, and the shots characters fire):

- **A frozen character turned back.** `actor_braindead` freezes a unit with
  the facing and aim it has at that moment (its desired vectors). The editor
  then turning or carrying it changed only the object: once let go it turned
  back to the old facing and kept aiming at where its target had been. Now
  every frame (`forge_ai_hold`) a placed character with its AI off whose
  facing the editor changed is given that facing (aim and look straight
  ahead), and its actor is made braindead again if anything woke it.
- **New characters ignored it.** ALL AI only flipped the characters already
  placed; one placed after ALL AI OFF fought at once. The choice is now a
  setting (`forge_globals.ai_all_off`): a character placed while it is off
  has its AI off; a map variant read sets it (off when every character in
  it is off); NEW ROUND keeps it.
- **The row lied.** It showed ON whenever any character was on, so with one
  turned on by hand it said ON over a mostly frozen layout, and pressing it
  froze everything. The row now says ON, OFF or MIXED; A (or left/right)
  turns all off from ON or MIXED, all on from OFF.
- **Characters the game spawns** (a carrier's infection forms, below) are
  not the layout's: in a Forge game they follow the setting too.
- **Characters were not given their variant.** `ai_attach_free` gives a unit
  an actor but not its variant's weapon, grenades, vitality, colours or
  camouflage: an Elite Major Needler held a plasma rifle, a stealth elite
  was not cloaked. `actor_customize_unit` (as `actor_place` does for a map's
  own characters) is now called as a character is placed.

Measured (`run_forge_test.sh allai`): four Covenant fighting the Spartan,
ALL AI off: over 10 s (44 readings) none moved more than 5 cm (idle
animation), none changed its aim, none fired; the Elite turned 90 degrees by
the editor kept its new facing; an Elite placed then came with its AI off;
MIXED, then OFF, then ON: they fight again (11 shots, then 84).

### The AI packs

| Pack | Characters (with their majors) | Source map | Tags | Tag cache | Pixels + samples (file) | Build (x86) |
|---|---|---|---|---|---|---|
| covenant_ai | + grunt minor needler, stealth elite (and major), elite commander energy sword: 14 | b40 (b30, a50) | 975 | 5187 KB (+223) | 30.3 MB (35.4 MB) | 1.4 s |
| flood_ai | infection form, carrier form, Flood elite (plasma rifle, shotgun), Flood human (assault rifle, shotgun): 6 | c20 (d20, c40, d40) | 581 | 2997 KB | 10.5 MB (13.4 MB) | 0.58 s |
| sentinel_ai | sentinel, sentinel major: 2 | c20 (c40) | 97 | 544 KB | 1.4 MB (1.9 MB) | 0.56 s |
| marine_ai | marine (assault rifle), armored marine (assault rifle, sniper rifle), their majors: 6 | a30 | 528 | 3041 KB | 8.9 MB (11.8 MB) | 0.97 s |

- The Library (c20) is the smallest campaign map (135 MB inflated against
  b40's 266 MB): the Flood and Sentinel packs read it twice in about a
  third of a b40 build's time. The Maw has the shielded sentinels, but
  neither the plain sentinel nor all the Flood's variants. The Enforcer is
  Halo 2's: Halo CE has none.
- marine_ai strips the marines' dialogue (`strip udlg`): with it the pack is
  843 sounds, 60 MB of samples and a 65 MB file (64 MB is a pack's limit);
  without it 12 MB. They fight silently.
- Sergeant Johnson is left out: Halo CE has him only as The Maw's cinematic
  biped (`cinematics\johnson\johnson`), with no actor variant.
- Each is opt-in in the lobby's EXTRA OBJECTS, after the Flyable Pelican
  (load order: the packs on by default first). `MAXIMUM_LOADED_SCENE_PACKS`
  is 8 (seven Forge packs and a player model's).
- Default teams are the units' own: the Flood's, the Sentinels', the
  humans'. In a Forge game the humans are the local player's allies, as
  every campaign script makes them (`ai_allegiance player human`; made
  again after a new round): marines fight for the player; HUMAN in the AI
  TEAM row is now an ally of the player too.
- A Flood infection form is a swarm actor, which `ai_attach_free` refuses:
  Forge gives it a swarm of its own (`actor_create_for_unit` with no
  encounter and a squad no encounterless actor has, so it never joins
  another placed swarm, whose AI settings are its own).
- A carrier's infection forms: `actors_spawn_from_unit` spawned a unit's
  actors only for an actor of an encounter. In a multiplayer game an
  encounterless actor's now spawn too (port change in `actors.c`), of no
  encounter, one swarm of their own; a campaign game is unchanged.
- The browser's AI category lists the characters by side (the unit's
  default team): Covenant, Flood, Sentinels, Humans, each row's side shown
  in its count column until one is placed. Readable names for the Flood's
  and the armored marines' (`forge_names`).
- The weapons category no longer offers a character's own weapon (the
  sentinel's beam, `characters\...`) nor a pack's weapon with no
  first-person model (the commander's energy sword, the hunter's fuel rod:
  a player would hold nothing); a map's own are offered as before. A map
  variant with a hunter fuel rod placed skips it.

### The tag cache: what fits on Blood Gulch

Blood Gulch is the tightest multiplayer map. The packs' room was measured
from the end of the map's tags to the end of the tag cache while a map
loaded: the scenario is not the game's yet when Forge's packs load, so the
structure bsp's place was not known and its 1266 KB were counted as free.
With every pack on, the packs took that room and the bsp was then refused:
"Couldn't load Blood Gulch: this map file is damaged". `tag_cache_room` now
finds the map's scenario tag itself; the earlier free figures (8837 KB
after the three first packs) were 1266 KB too high.

`run_forge_test.sh aipacks` (the Flyable Pelican from extinction.map):

| Packs on | Loaded | Held | Free after |
|---|---|---|---|
| the defaults (vehicles, dropships, Flyable Pelican) | all | 2547 KB | 11502 KB |
| defaults + Covenant AI | all | 7734 KB | 6315 KB |
| defaults + Flood AI | all | 5544 KB | 8505 KB |
| defaults + Sentinel AI | all | 3091 KB | 10958 KB |
| defaults + Marine AI | all | 5588 KB | 8461 KB |
| the four AI packs alone | all | 11770 KB | 2279 KB |
| every pack | all but Marine AI (left out, said) | 11276 KB | 2773 KB |

The room is 14049 KB in all. Any three AI packs fit with the defaults
(Covenant, Flood and Marine leave 277 KB); all four do only with the
defaults off: with them the last one asked for (Marine AI, in load order)
is 267 KB short and left out with the notice.

### GPU programs

No program written. Collected on the gxm-null harness (HALO_SHADER_COLLECT)
through `flood`, `floodcov`, `sentinels`, `marines`, `allai` and `aivariant`
and checked (`tools/vita_shader_pack.py --check`): 107 of 108 in
shaders.pak; lacking `ef7ede1093531c6c.fp` (one texture stage, fogged),
drawn in the Flood and the Sentinel cases (both c20 packs): to compile on
Vita3K (HALO_SHADER_PRECOMPILE) and add. The Covenant AI cases (`aioff`,
`aiteam`) lack one already in insider.10: `e990fac489771925.fp`.

### Tests

`run_forge_test.sh`: `allai` (above), `allaimenu` (the row by the pad:
MIXED, OFF, ON, screenshots), `flood` (infection form, carrier and combat
forms attack the Spartan; the carrier bursts, its infection forms live and
ALL AI freezes them), `floodcov` (the Flood and the Covenant fight),
`sentinels` (sentinels attack the Flood), `marines` (marines never hurt the
Spartan; with the Covenant there they shoot it), `aivariant` (a variant with
one character of each AI pack saved: it names the four packs; started again
with none chosen it brings them and places all four as saved), `aipacks`
(not by default: the table above). `run_scene_pack_test.sh tool`: the four AI
packs built, loaded with no correction and fuzzed.

## Packs from Halo Custom Edition maps (as built, Oct 18 2026)

Branch `feat/ce-packs` (from `integration/1.1.1-beta.8`). The owner's asks:
"Make the Pelican flyable. You can pull it from Extinction" and "custom player
models from Halo Custom Edition maps". Both are scene packs built from Custom
Edition maps, which every player has downloaded from the internet: untrusted.

### The builder (`port/linux/game/scene_pack_ce.c`)

- **Recipes** name a Custom Edition map instead of Xbox maps: `cemap <file>
  <size> <checksum>` (the file in the maps folder, without `.map`, its size in
  bytes and its header's checksum field, which together say which version of
  it). The first named map there of that identity is used, with the maps
  folder's `bitmaps.map`, `sounds.map` and `loc.map` (the ones the map takes
  tags from must be there). `unit multiplayer` is a root of its own: the map's
  globals' multiplayer biped, whatever its name (a protected map's tags are all
  named `<protected>`; the pack names them `protected\<map>\<group> <index>`).
  A recipe has map or cemap lines, not both; the hash takes the cemap lines and
  the Custom Edition builder's version (`ce1`).
- **Missing or another version**: the pack is unavailable and says why, never an
  error: "extinction.map (Halo Custom Edition) is not in the maps folder", "your
  extinction.map is another version of it than the one it is built from", "the
  maps folder has no bitmaps.map (Halo Custom Edition's), which X.map needs".
  Forge's EXTRA OBJECTS shows it ("Unavailable: ..."), a Forge game leaves the
  pack out without a word on the HUD.
- **The checks, in the game's Custom Edition loader's order**
  (`custom_edition_cache.c`), on the builder's thread:
  1. `cache_file_identify`: every header field against the file, and the
     recipe's identity;
  2. `custom_edition_cache_load_tags` (new, `cache_file_formats.c`): the load of
     a map's tags with every check of `custom_edition_cache_load` (tag index,
     every instance, name and address, every resource map's tag, every
     bitmap's pixels and sound's samples in their file, every model part's
     geometry in the model data) but the structure BSPs' own (each reference's
     tag, file range and place in the tag cache checked; the BSP is not read)
     and the header checksum (not taken), into a buffer of the tag data and
     resource tags only (the tag cache's free room when it has 4 MB more than
     the tag data, else the heap);
  3. `custom_edition_cache_combine_resource_offsets`,
     `custom_edition_cache_convert`;
  4. the tags moved where they are (`halo_tag_relocate_linked_tags_isolated`)
     and checked against their groups' schemas
     (`tag_validate_custom_edition_tags_isolated`), as the loader does: both
     leave the game's walk and check states as they were (the map loaded
     meanwhile keeps its structure bsps' walk), and both hold a lock that every
     public walk of `tag_relocate.c` and `tag_validate.c` now holds (one walk
     at a time, whichever thread); the source's messages go to the log only;
  5. as the builder takes each tag (the closure, as for an Xbox map), what
     Custom Edition holds otherwise is made the Xbox's, with the game's own
     checks: a **gbxmodel**'s parts (0x84 bytes) repacked as this build's (0x68)
     where they are, each part's indices checked (shader, nodes, parts, local
     node table), its vertices read from the model data, checked (nodes, the
     vectors the compressor takes, no NaN), their nodes made the model's,
     compressed as `rasterizer_geometry_compress_vertices` does (here without
     the game: the tool links none of it) into the pack's buffers, its strip
     checked and copied; a model of 44 nodes or more is refused (the game's
     Custom Edition maps draw such a model a part's nodes at a time, which a
     pack's are not). A **bitmap** is checked as `custom_edition_bitmaps.c`
     checks one drawable, written from its second level when it is a large 2D
     texture (as the game draws a Custom Edition map's, 128 KB), and laid out
     as `rasterizer_xbox_bitmap_rebuild_hardware_format` lays it out (every
     level swizzled, cube maps face by face, linear rows padded to 64 bytes,
     each face to 128); a model shader's multipurpose map and a HUD meter that
     Halo PC keeps in other channels than this build reads them from have their
     channels moved (decompressed from DXT to 32-bit colour first: the channels
     move between DXT's colour and alpha); the cached flag a cache file's tool
     sets is kept (the texture cache loads no bitmap without it). A sound
     **permutation** of Ogg Vorbis, 16-bit PCM or a halved 44 kHz mono sound is
     encoded as Xbox ADPCM (`ogg_sound.c`, `xbox_adpcm_encoder.c`), at most
     1 MB of it; a stream that cannot be decoded is silence. The pixels and
     samples are written to `<pack>.data`, then the pack is put together
     (header, tags, data);
  6. the pack is checked as any pack when it is loaded (`scene_pack_load`: its
     tags moved by the walk with every reference checked against its own table,
     then the tag validator with **no correction allowed**). A reference's group
     is made its tag's (a gbxmodel's `mod2` is a `mode`, a `scex` a `schi`).
- **Thread safety.** `cache_file_formats.c`'s address globals are read only
  while a Custom Edition map is loaded, which never overlaps a pack's build
  (packs are not wanted beside a Custom Edition map, and a map's unload stops
  and joins the build before the next loads).
- **Fuzzing.** `scene_pack_test cefuzz <recipe> <maps> <n> [seed]`: the source
  map's bytes changed as the builder reads them (never on disk: a read hook of
  the tool's) - words of the roots of the pack's tags, of the tag index and
  instances, bytes of the model data, pixels and samples a clean build read, of
  the tag data and of the header -, each build refused or its pack refused or
  loaded clean twice; never a crash or hang. 600 builds (seed 7): the build
  refused 205; 395 built, all 395 loaded clean twice; 0 failures. The packs
  built are fuzzed as any (`scene_pack_test fuzz`, 3000 copies of ce_pelican:
  0 failures).

Measured (x86 harness, the files cached):

| Pack | Source | Tags | Tag cache | Pixels + samples | File | Build |
|---|---|---|---|---|---|---|
| ce_pelican | extinction (3620 tags, 12.2 MB loaded) | 316 | 1032 KB | 7.9 MB | 8.9 MB | 40-135 ms |
| player model: Spartan (Extinction) | extinction | 391 | 1824 KB | 13.4 MB | 15.2 MB | 85 ms |
| player model: Covenant V Marines Beta 5 (protected) | Covenant_V_Marines_Beta_5 | 374 | 1812 KB | 12.4 MB | 14.2 MB | 250 ms |

On the Vita, still to measure: the build (the map's 12 MB of tag data read
from the memory card, Ogg Vorbis decoding of the pack's sounds); its scratch
(16 MB of the tag cache's free room: there in ui.map, in a Forge map only
when few packs are loaded, else the heap, which may refuse it: the pack is
then unavailable with the reason, built again in the lobby).

### The flyable Pelican

- **Source:** Extinction (`extinction.map`, 97 888 771 bytes, checksum
  c5372723), a Halo Custom Edition map by (DA)Ender and the Halo 2 Scarab
  Conversion Team (2006; its readme: "This map is unprotected. Feel free to
  ... reuse"). Its `vehicles\epelican\epelican` is a player vehicle: type
  human plane, seat 0 `W-driver` (driver, gunner, third-person camera), seat 1
  a co-pilot's, eleven rider seats, a sniper's. So no fallback was needed (the
  b30 Pelican made flyable at build time stays a design: not built).
- **Recipe** `app0:packs/ce_pelican.txt`, the Forge lobby's fourth EXTRA OBJECTS
  row "Flyable Pelican", on by default, beside the b30 one (still "Pelican",
  a prop); the browser names it "Flyable Pelican" (`forge_names`).
- **How it flies** (the map's own physics: Halo's human plane,
  `update_human_plane_physics`): left stick forward is thrust along where it
  faces, the view's pitch lifts it or brings it down; let go, it hovers and
  sinks slowly (its hover lift is 0.975 g at no throttle). In Blood Gulch it
  climbs to the map's vehicle ceiling (about 66 world units) and comes down on
  any ground: shown on the harness (run_scene_pack_test.sh cepelican: from 2
  up to 37, down to 9.6 on a ledge of the canyon, at rest), passengers boarded
  (`pack board 4`: seats 1 to 4) and aboard before and after. Its textures,
  the H2-style hull, its engines' glow draw as on Extinction. Collisions: it
  rests on terrain and ledges with its hull, nothing seen through it; a
  climb into the canyon wall kills its riders (a vehicle collision, as any).
- **Not checked:** its guns (the driver seat is the gunner), the HUD (the
  vehicle's unit HUD and the gun's weapon HUD come with it, their meters'
  channels moved), sounds on hardware.

### Player models from Custom Edition maps (`port/linux/game/player_models.c`)

- **The scan:** every Custom Edition multiplayer map of the maps folder (not
  one the mod manager turned off), read without being loaded
  (`scene_pack_ce_multiplayer_unit`: a few reads of its tag index, each within
  its tag data): its globals' multiplayer unit, when it is a biped held by the
  map whose tag or model is not the stock cyborg's (`characters\cyborg_mp\cyborg_mp`,
  `characters\cyborg\cyborg`; a protected map's names say nothing, so its
  biped is listed), and of 43 nodes or fewer. Each is a pack of its own
  (`pm_<hash of its map's name and identity>`, its recipe made by the game:
  `scene_packs_recipe_register`), built the first time it is shown or played.
  Found on this machine: **Spartan (Extinction)** (`characters\spartan\spartan_mp`,
  model `characters\scyborg\scyborg`, 19 nodes) and **Covenant V Marines Beta
  5** (protected; an Elite, 26 nodes). The folder is listed first, then each
  map read (the game's `find_files` is one listing for the whole game).
- **CUSTOMIZATION's MODEL** (after COLOR): SPARTAN, then each model; the
  preview builds the pack the first time ("Building the model... n%" under it),
  then stands the model in the profile's colour holding the menu Spartan's
  rifle, a taller one further away (by its eyes' height over the Spartan's 0.62).
  Circle keeps the choice. The settings panel's Game tab has **Player model**
  (shown while one is found), the same choice for the first player's profile:
  the original menus' players have it too.
- **Kept** in `t:\player_models.txt` (the save folder's t, beside the packs):
  a line a profile, its folder's name and the model's map, size and checksum
  (a choice keeps to its map; a map gone or of another version chooses the
  Spartan), written by way of a temporary file. Not in the profile: `blam.sav`
  is the Xbox's profile, whose layout has no room for it (and a PC or an older
  build reads the same files).
- **Played:** as a map's tags load (`cache_files.c`), in a local game (the
  connection local) of a multiplayer scenario, the first local player's
  profile (the one in use, else player 1's last, else the only one) has its
  model's pack loaded (after Forge's) and spawns as its biped
  (`players.c player_spawn`, a game engine's game) in the profile's colour:
  Forge (the Spartan when the monitor is let go), split screen, games with
  bots. Bots, the other local players and the map's own scripts keep the map's
  unit. Shown on the harness (run_scene_pack_test.sh cemodels: a Forge game,
  "the first local player plays as Spartan (Extinction)"), and the protected
  map's Elite in Blood Gulch (its initial weapon, a plasma rifle, given as a
  unit's are).
- **Left out:** the campaign (its maps have no tag cache to spare for a pack,
  "The memory budget", and its scripts and cutscenes name the cyborg); a
  Custom Edition multiplayer map (packs are not loaded beside one); network
  games (below).
- **First-person:** the arms are the globals' (unchanged); the camera is the
  model's own eye height; first-person legs draw the model's legs (seen on the
  harness). A biped's own initial weapons come with it, as with any unit (the
  Extinction Spartan's plasma pistol, the Elite's plasma rifle), beside the
  game type's starting weapons.

### What a network game would need (not built)

The model a player plays as is an object every machine must create the same
way (the host creates units, `players.c`; clients get its tag index in the
object's creation message): today a network game always uses the map's own
unit. For a player's model online:
1. **every machine has the pack**: the same recipe (its hash, which names the
   map file and its identity) built locally from its own copy of that Custom
   Edition map - nothing of the map is sent; a machine without the map cannot
   show that player as the model;
2. **a per-player capability check**: each joiner says which model packs it can
   load (recipe hashes) in the join exchange, the host says which it loads for
   the game, and only a player whose model every machine has gets it (the
   others the stock biped); a new capability bit, as the map share's, or a
   network version bump (the owner's call with a release: it splits players
   from every older build, crossplay with PC included);
3. **loaded before the map's objects on every machine, in the same order**,
   so the packs' tag indices match (the game's table grows by load order:
   `scene_packs.c`), or tag paths sent instead of indices for pack tags;
4. **validation as map share's**: a joiner never trusts a host's pack or
   recipe - it builds from its own map, checks the pack as any, and refuses
   (stock model) on any mismatch; a host never accepts a client's claim of a
   model without the same.
This is the "same packs loaded on every machine of a network game" still to
come for Forge, and should be built once for both.

### GPU programs

New models may draw with programs `shaders.pak` lacks (the owner's Vita has
no shader compiler; no GPU program was written here). Collected on the
gxm-null harness (HALO_SHADER_COLLECT) through the Pelican's flight (76
programs: all in the pack), a Forge game with each player model played or
stood in the world (82), the CUSTOMIZATION preview of both (23), and a Forge
game with neither (86: all in the pack), then checked
(`tools/vita_shader_pack.py --check`): one program is not in the pack,
**9d464b6e348ed572.fp** (two texture stages), drawn by the Extinction
Spartan's model in play and in the preview, in no run without it. To compile
and add with the next pack (HALO_SHADER_PRECOMPILE on Vita3K); until then a
Vita without the compiler skips the draws that need it (d3d8_gxm.c's
skipped_no_program): those parts of that model are missing.

## Menus that never stall (as built, Oct 18 2026)

Branch `feat/menu-speed`, from the owner's insider.11 test: the lobbies
"took a hot minute" the first time, SELECT PROFILE froze while it loaded the
card's Spartan, and the multiplayer scene's flag had no cloth on the Vita.

### Nothing heavy on the game thread

Every check, build, read and validation of a pack runs on the packs' worker
thread (`scene_packs.c`), below the game's threads. A pack wanted now is
built if it must be, then read into the tag cache room's start and checked
there as any pack is - the file opened once (`scene_pack_open_file`,
`scene_pack_load_file`): its tags read once and checked as read, its data's
CRC through the handle that serves its pixels later, so what is checked is
what is used - and given the tag indices, salt and file offsets the game's
table will give it ("staged"). The top of the next frame only puts it in:
the table grown, its buffers registered, other indices given if the table
changed meanwhile (`scene_pack_rebase_again`): **0.01 ms** on x86, logged
as "the game thread's part". A pack's bitmaps are read by the cache file
thread, as a map's are (a texture read on the render's thread held its frame
for the memory card's time), and a scene waits for its packs' textures
before it is shown (`scene_packs_textures_ready`: no stand-ins), then pops
in (a fade would need blended variants of the opaque shaders: new GPU
programs). The game thread's time in the packs' work is logged from 16 ms.

Measured on the Linux harness with the packs' own reads and writes slowed
to a slow card's 2 MB/s (`HALO_TEST_SLOW_IO=2048`), no pack built:

| | before (insider.11) | after |
|---|---|---|
| menu_spartan's load on the game thread | 982 ms | 0.01 ms |
| menu_campaign's | 3013 ms | 0.01 ms |
| menu_multiplayer's | 1688 ms | 0.01 ms |
| menu_monitor's | 160 ms | 0.01 ms |
| longest frame at the menus | 992 ms | 33.5 ms (the 30 fps cap) |

### Prebuilt at the main menu

Once the Halo 3 main menu has rested 2 s, every pack the menus'
`scene_object=` lines name (and their weapons'), menu_spartan and the first
profile's player model's are prebuilt (`menu_functions.c` h3 prebuild,
`scene_packs_prebuild`), then loaded, the card's first: SELECT PROFILE shows
its Spartan at once and a lobby its scene as soon as its textures are read.
The four take 2.6 MB of the tag cache, which leaves the 16.6 MB a campaign
map's build needs (b40's tag data). A prebuild's job is paced (1 ms each
128 KB read, and it waits while the game's own reads are pending), paused
while a map is copied to the cache partition, in a network game and away
from the main menu, and stops to make way for any pack wanted now (built
again later). `HALO_MENU_PREBUILD=0` turns it off.

### Faster builds

- **The cache partition's copy.** A build reads the game's decompressed copy
  of its map (`z:\cacheNNN.map`, `cache_files_cached_copy_open`) in place,
  seeking to what it wants, when that copy's header is the maps folder's
  map's byte for byte and has a checksum (as the game itself keeps a copy);
  the header and the cache file's copy generation are checked again after
  each read, and a copy begun into it meanwhile makes the build start again
  from the map. A recipe's map read in place is preferred to its others.
  menu_campaign from a30: 224 MB inflated in 682 ms before, **17.6 MB read in
  12.8 ms** from the copy (x86, the tool); in the game, paced, 125 ms against
  4.2 s.
- **One read for several packs** (`scene_pack_build_many`): the source's tag
  data read once, then the union of every pack's pixels and samples, each
  piece written to its pack as it goes by. menu_spartan and menu_multiplayer
  from Blood Gulch: 163 ms together against 128 + 155 ms (x86). A pack wanted
  now chooses its map alone (a larger shared map would delay it), and takes
  the waiting builds that name the same map with it.
- **Smaller packs.** Recipes' new `keep <graph>|<animation>` lines keep those
  animations of a graph whole; its others are kept without frames (no nodes,
  as the tag check makes an animation it refuses: their bytes are not in the
  pack). The menu recipes keep the poses their scenes play (found by logging
  every animation the lobbies, the card and the preview used; the lobbies'
  screenshots are as before):

  | Pack | tags before | after | file before | after |
  |---|---|---|---|---|
  | menu_spartan | 1137 KB | 382 KB | 2006 KB | 1251 KB |
  | menu_multiplayer | 1433 KB | 703 KB | 3446 KB | 2716 KB |
  | menu_campaign | 3075 KB | 1459 KB | 6152 KB | 4535 KB |
  | menu_monitor | 84 KB | 84 KB | 325 KB | 325 KB |

  Sounds were stripped already. The textures' top levels were tried (a
  recipe line leaving out the levels above 256): menu_spartan's pixels 868 to
  484 KB, menu_campaign's 3076 to 2436, menu_multiplayer's 2012 to 1372 - but
  at the Vita's 960x544 the GL screenshots of the card and the lobbies
  changed (up to ~2000 pixels a frame by 8 levels or more: the scenes do
  sample their textures' top levels), so it was left out. The models' far
  LODs were not stripped: a few tens of KB a model, for a remapping of every
  region's permutations the tag check would have to pass uncorrected.

### A pack's data checked once (insider.12's Forge start)

A tester's Forge game on Blood Gulch with all seven packs on ended in the
Vita's watchdog crash (HALO_HANG_CRASH=1): the map's load
(`scene_packs_load_now`, on the game thread, as Forge needs the packs before
its objects are placed) read each pack's whole data again just for its CRC -
covenant_ai 36 MB, flood_ai 13.7, ce_pelican 8.9 - at ~3 MB/s from the card,
no present for over 8 s. Now a pack's data (pixels and samples) is read whole
once: at its first load, or in the background by a prebuild's job (the Forge
lobby prebuilds its packs, the main menu its own), a record of the file then
checked is kept beside it (`t:\packs\<name>.pack.ok`: its size and time as
the file system gives them, and its header's CRCs). A later load of the same
file reads and checks its tags as ever (their CRC, their move, the tag
check) but not its data again. A file written again (a build, a copy, a
damage that rewrites it) has another time or size and is checked whole; a
damaged one is refused, deleted and built again (`run_scene_pack_test.sh
verified`). What a record cannot see is a file changed in place with its
time set back: its pixels or samples would be drawn or played wrong, never
read as structure (the tags are checked at every load). The Vita's watchdog
also counts progress the game thread waits on (`halo_progress_heartbeat`: a
pack read at a map's load, a map's copy into the cache partition), so a long
load that is reading is not taken for a hang. A Custom Edition pack whose map
is turned off in the settings' Modded maps fails at once, said why (the
Forge lobby offered Flyable Pelican as Ready with extinction turned off, and
would have loaded it).

The same tester's Forge lobby drew frames of ~100 ms for seconds at a time
while its seven packs were prebuilt: on insider.12 a pack's build ran at the
game threads' priority, its textures were read on the render's thread as
they were first drawn, and the lobby scene's packs (menu_campaign,
menu_monitor) were loaded on the game thread; all three are off the game's
threads now (the worker below them, paced, waiting while the game reads;
bitmaps by the cache file thread). Not measured on a Vita.

### The multiplayer scene's flag

Its draw was never missing: on the gxm-null harness the cloth is drawn every
frame (360 triangles, alpha blended, its vertex alpha 1, texture alpha from
flags_blue, DXT3) with programs shaders.pak has - all 18 of the scene's,
the cloth's fragment program `e24186ce9800f90d.fp` among them. Held still
in ui.map's space (no wind, no movement) the cloth hung limp down the pole,
seen edge on: a strip a few pixels wide on the harness at every turbulence
seed tried, nothing on the Vita. A flag held in a menu scene now flies in a
steady breeze along its camera's right (`menu_scene_objects_flag_wind`, a
force bringing the cloth's points to 2.5 world units a second against the
air's friction, `flags.c`), streaming across the picture face on and
rippling. `HALO_MENU_FLAG_BREEZE` sets the speed (0: limp, as before). Flags
in a game are as they were. No new GPU program.

### Tests

`port/vita/tests/run_menu_speed_test.sh`: `cold` (no pack built, no
prebuild, slow I/O: each load's game thread part under 5 ms, no pack work of
50 ms on the game thread, the longest frame at the menus under 100 ms, every
scene made, the flag flying), `prebuild` (everything built and loaded at the
main menu, Blood Gulch's two packs in one read, the visits build and read
nothing), `cached` (a30 played, then menu_campaign read in place).

## The AI counter and CLEAR AI (as built, Oct 19 2026)

Branch `feat/forge-rotate-ai` (from `feat/party`, `02dbe938`). The owner:
"Spawning in flood breaks the ai counter, it stays stuck after killing them
so we might need a way to have a clear all for ai".

**Why it stuck.** A placed character keeps its place in the AI budget
(`FORGE_MAXIMUM_AI`, 32) after it dies, by design: NEW ROUND brings it back,
and must have room for it. The Flood's bodies vanish (an infection form
pops, a carrier bursts), so nothing was left to aim at and delete, and the
HUD counted the dead with the living.

**Now.**

- The HUD counts the living, the dead apart: `Objects 33/256, AI 27/32,
  5 back next round`. The browser's pane says the same ("Dead AI come back
  next round.", and "Clear dead AI to place more." once the budget is full).
- The dead still hold their places (the next round must have room for
  them): a character past the budget is refused with "AI budget full: 32
  characters, 5 of them dead. Clear dead AI to place more."
- The Forge menu (Start, any Menu style) has **CLEAR DEAD AI (n)**, which
  takes the dead out of the layout at once, and **CLEAR ALL AI (n)**, asked
  first (`h3/forge/clearai/screen`), which takes every placed character out
  of the layout and the world, alive or dead. Neither comes back with NEW
  ROUND nor in a map variant saved after; the undo history's steps that
  name them go too (nothing brings a cleared character back). The settings
  panel's Forge and File Share page (the Halo CE Menu style's) has the same
  two rows while a Forge game is played (`forge_panel_ai_counts`,
  `forge_panel_clear_ai`), Clear all AI asked first.
- The Flood: a combat form feigning death (dead bit, `feign_death_timer`
  running: it gets back up as the same unit, `unit_port_resurrect`) is
  counted alive, never cleared as dead; what the game spawns (a carrier's
  infection forms, a body the Flood took) is never the layout's, never
  counted. A placed carrier that bursts, or an infection form that pops,
  is dead (its object gone) until cleared or the next round.

Console: `forge clearai dead|all`; `forge budget` logs the living, the dead
and the HUD's line; `forge ai` says `down` for a combat form feigning death.

Tests (`run_forge_test.sh`): `clearai` (the Flood AI pack fills the AI
budget; a carrier destroyed, an infection form and two combat forms killed;
the HUD's numbers, the refusal, CLEAR DEAD AI, another placed, NEW ROUND
with only what remains, CLEAR ALL AI, a map variant saved after holding the
Warthog alone) and `clearaimenu` (the Forge menu's two rows by the pad, the
question's B and A); `run_vita_settings_test.sh` the panel's rows.

## Rotation, version 2 (as built, Oct 19 2026)

The owner: "we need a better or different way of rotating the objects".
Before, L held turned the right stick to the held object: across turned it
about the up axis only, up and down pushed it away and pulled it nearer.

**Rotate mode** (holding an object, L held; the PC's right mouse button or
G): the camera stays still, and the right stick (the mouse; the Vita's gyro
when it is on) turns the object relative to the camera's view: across yaws
it about the world's up, up and down pitch it about the camera's left along
the ground (pushing up tilts its top away from you whichever way you face),
and with R held as well (the PC's left mouse button) across rolls it about
the camera's forward along the ground (its top to the right). R in rotate
mode never drops the object; hold L first, then R. The left stick's up and
down (the PC's W and S) push the object away and pull it nearer; the wheel
still does at any time.

**Snapping**: L + D-pad right (the PC's right button + Q) steps rotate
mode's snap through Off, 15, 45 and 90 degrees (D-pad right alone stays the
movement speed). Snapped, each flick of the stick (past 0.6, let back under
0.3) turns the object one step, and so does every 0.1 radian of the mouse's
look; the pitch and roll axes are then the world's axis nearest the
camera's, so the steps keep the object square with the map. The HUD under
the held object's name says the snapping ("Rotation snap: 45°") and, while
rotating, its yaw, pitch and roll.

**Exact angles**: the properties window (Square while holding an object you
placed) has YAW, PITCH and ROLL (left and right step each to the next
whole step: the snapping's, or 15 degrees with none; yaw and roll round
the circle, pitch from -90 to 90) and RESET ROTATION (all three none: facing
+x, upright). D-pad left still sets the object upright, facing as it faces.
(At a pitch of 90 the yaw is not defined: stepping yaw there turns the
object about its own forward, as gimbal lock has it.)

**Saved**: a map variant's record already kept yaw, pitch and roll
(`forge_angles_from_facing`, `forge_facing_from_angles`: forward and up
both); nothing in the file changed, old variants load as before, and a
pitched or rolled object comes back so after a save and load and after NEW
ROUND. Scenery stays as turned; a vehicle, a crate with physics or a
character falls and settles as the game's physics have it (a Warthog lands
on its wheels, a biped stands up), and its placed spot follows where it
comes to rest, as before.

Console: `forge property yaw|pitch|roll [-]`, `forge property resetrot`,
`forge look <yaw> <pitch>` (the mouse's look as the keyboard's port gives
it, for the tests); `forge where` logs the held object's forward, up and
angles, the snapping and whether it rotates; `forge objects` each placed
facing and up. Test: `run_forge_test.sh rotate` (a boulder: snapping to 45,
a flick of yaw, pitch and roll, R not dropping it, the exact angles, the
mouse's yaw and roll, then dropped, saved, a new round and the variant
loaded in another run, the facing checked at each).

## Building blocks as built (Oct 19 2026)

Branch `feat/forge-blocks` (from insider.15). The owner: "the building blocks
of a map", Halo 3 Forge style. Halo CE's maps have none (their walls and
floors are the structure bsp's), and nothing of Bungie's may be shipped, so
the game makes its own: blocks, walls, platforms, bridges, columns, ramps
and a corner, scenery objects with real collision, textured with the
player's own map's bitmaps, made the same on every machine.

### The set

| Block | Size (x by y by z, world units) | Kind | Material |
|---|---|---|---|
| Small Block, Block, Large Block | 0.5, 1, 2 cubes | box | walls |
| Small Wall, Wall, Large Wall | 1 x 0.25 x 1, 2 x 0.25 x 1, 4 x 0.25 x 2 | box | walls |
| Small Platform, Platform, Large Platform | 1 x 1, 2 x 2, 4 x 4, 0.25 thick | box | floors |
| Bridge, Long Bridge | 4 x 1, 8 x 1, 0.25 thick | box | floors |
| Column, Tall Column | 0.5 x 0.5 x 2, x 4 | box | walls |
| Small Ramp, Ramp | 1 x 1 x 1, 2 x 2 x 2 (45 degrees) | ramp | floors |
| Vehicle Ramp | 4 x 2 x 1 (14 degrees) | ramp | floors |
| Corner | 1 x 1 x 1, an L of two walls 0.25 thick | corner | walls |

Every size is a multiple of Forge's grid (0.25), the origin at the middle,
a ramp rising toward its +x. Tag paths `forge\blocks\<name>` (the same on
every map), their names translated (forge_blocks.c).

### How they are made (`port/linux/game/scene_pack_blocks.c`)

A scene pack whose recipe has the new `blocks` line. Its recipe is the game's
(forge_blocks.c registers `pack blocks_<map>`, `map <map>`, `blocks` for the
Forge map), so each map's blocks wear that map's bitmaps and the pack is
built once a map (`t:\packs\blocks_<map>.pack`, 0.4-0.7 MB, 56-57 tags, 67 KB
of tag cache; built in 3 ms from the cache partition's copy of the map on the
harness, about 160 ms when it inflates the map; the Forge lobby asks for the
chosen map's in the background).

- The builder reads the source map's tag data as for any pack; the
  generator then lays out its own tags as more of the source's (three a
  block - scenery, model, collision model - and two model shaders) at
  addresses of their own (`SCENE_PACK_BLOCKS_ADDRESS`, past any map's tag
  cache), which `source_at`, `source_instance` and `source_tag` take as the
  map's: the closure, the copy, the links, the bitmaps' pixels are the
  builder's as ever, the parts' vertices and strips are made buffers (as a
  Custom Edition model's), and the pack is checked as any (no correction
  allowed: every map's passes).
- **Textures:** two model shaders of our own (not alpha tested, no
  reflection, no multipurpose map): the base map and detail map (primary,
  else secondary, else micro) of the source's environment shader that reads
  most like a wall (metal, panels; one of the map's own level's shaders,
  `levels\...\<map>\`, first: Blood Gulch's `cap_metal`), and of the one most
  like a floor (plates, flat metal), another bitmap than the walls' when one
  reads well enough; with no environment shader, a model shader's base map.
  Chosen by the names' words in the source's order: the same on every
  machine. Every read of the source is bounded.
- **Models:** one node, one region, one geometry of one part (one draw call
  a block): flat faces, 24 vertices and 12 triangles a box (a corner 46 and
  24), compressed as `rasterizer_geometry_compress_vertices` compresses them,
  one strip; the texture coordinates world units over 1 (blocks side by side
  continue one another), the model's base map scale a power of two.
- **Collision:** one material (thick metal: its bullet holes and sounds), one
  region, no vitality (nothing damages a block), one bsp. A convex piece's
  faces are a chain of nodes (each face's plane; in front of it a leaf naming
  its surface; behind the last, solid), the corner's two pieces split first
  by the plane between them (the leaves on each side naming the other
  piece's face on it where it shows). Each surface's ring of edges counter-
  clockwise seen from outside, each edge shared with the surface on its
  other side (collision_bsp.c walks the rings; collision_features.c takes the
  convex edges and the vertices). Vectors (shots, vehicles' mass points) and
  spheres (bipeds) meet the faces as the structure's.
- **Determinism:** the shapes are exact (quarters of a world unit, the
  slopes' normals constants, the radii in 64ths found by whole numbers);
  nothing but the source's tags and the tables decides a byte. Built twice,
  the same bytes (and the recipe's hash carries the generator's version).
  A multiplayer Forge game's machines, each with the same map, make the same
  pack; the blocks are ordinary objects of their tag paths, loaded first
  among Forge's packs so that their indices match wherever the same packs are
  chosen.

### In Forge

- **BLOCKS** in the object browser (halo3/forge.xml; the classic menus play
  Forge with the same browser), after AI, the set in its order.
  `forge list blocks` on the console.
- **Grid snap** (SNAP on): a block held has its corner, not its middle, on
  the 0.25 grid along each world axis as it is turned, up and down too
  (forge_blocks_snap), so blocks of any size meet edge to edge and stack;
  rotate mode's 15/45/90 snapping turns them square.
- Placed, moved, turned, deleted, copied, counted in the budget and saved in
  map variants as any object (nothing in the file changed: old variants load
  as before; a variant's blocks are found again in the map's own pack).
- forge.c changes are hooks only: the category, the readable name, the snap,
  the pack's load before the others, and two console words (`forge snap
  [on|off]`, `forge blocks ray|projectiles` for the tests).

### GPU programs

No new program: collected on the gxm-null harness (HALO_SHADER_COLLECT) with
blocks placed in view on all thirteen multiplayer maps, every program is in
`shaders.pak` (94 of 94; on Blood Gulch the blocks add none to the game's
own 60): a model shader with a base and a detail map is one the maps' own
objects draw with.

### Security

The generator reads the player's map, anyone's file: every read is bounded
(the instances, names, references, roots, bitmaps), and what it makes goes
through the builder and the pack checks as any pack. `scene_pack_test
blocksfuzz <maps> <map> <n> [seed]` damages the source's tag data where the
blocks read it (shaders' and bitmaps' instances, roots and first bitmaps,
any word) and builds again: 1000 builds (seed 11) and 400 with
AddressSanitizer (seed 23), each refused or loaded clean twice, no crash.
`scene_pack_test fuzz` over a blocks pack: 3000 copies, 0 failures.

### Tests

`run_forge_test.sh blocks`: the tool's packs (Blood Gulch twice, the same
bytes; Chill Out), the structure placed with SNAP (each block where asked),
the Spartan standing on the bridge (z 2.5) and walking up a ramp, a Warthog
driven up the vehicle ramp and off it, a plasma bolt stopping at a large
wall (gone at its face; with no wall it goes on), the projectiles' line
meeting the wall (thick metal), the map variant saved and loaded again (10
placed, 0 skipped), the browser by the pad (BLOCKS, a wall placed).

### Not built

- AI pathfinding around blocks (no pathfinding spheres: characters walk into
  them and are stopped by the physics).
- More shapes (curves, windows, doors, a corner ramp), more materials than
  two a map, the blue ghost.
- A network Forge game's joiners load the blocks as the host does only where
  the multiplayer Forge branch calls forge_blocks_load (forge_packs_load).

## Network Forge as built (Oct 20 2026)

Branch `feat/mp-forge-sync` (from insider.15, insider.16 merged). The owner's
decisions: up to 8 players; the host authoritative (every edit a request the
host checks, makes and sends on; the first grab wins); each player a monitor
who can be the Spartan; late joiners get the current layout; the host's map
variant and packs carry over (a joiner builds them from its own maps, or is
told what is missing and forges without those objects); an optional
capability, no network version bump; host-only NEW ROUND, CLEAR AI, ALL AI;
the host saves its map variant, a joiner a copy; undo per player.

- **Capability** (`forge_net_protocol.h`, `forge_net.inc`): a Forge host says
  hello to each machine that joins (the Bungie message `_message_server_forge`,
  type 39, past upstream's: its version, session, packs and map variant's
  title), again each second; the joiner answers (`_message_client_forge`, 38:
  its version, capabilities, the packs it can build). A machine that does not
  answer in 6 s (an older build drops type 39 unread) or answers another version
  is refused from this game alone ("too old", which an older build says as a
  failed join), as is a ninth player ("full"). Nothing changes in a game that is
  not Forge; never on the crossplay wire. The wire contract's G12 lists the two
  types as ours, sent only by `network_game_client_send_to_server`.
- **In game**, four distributed kinds of their own (40-43, clear of upstream's
  19 and 78-86; a build without them drops them): a client's requests (reliable,
  over its stream only), the host's updates, palette and layout. Each request
  is checked field by field (`forge_net_request_valid`: op, flags, every float's
  bits, positions within +-5000, directions unit and square, property and step
  ranges, the tag below the host's palette), against the sender's replay window
  (16-bit sequence) and rate buckets (90 requests a second, 12 spawns at once
  then 4 a second), then by the game (the object there, not a player's unit,
  not the map's own scenery - nothing would show the others its move -, not
  held by another, the tag one the host's browser offers, the budget, inside
  the structure's world). Dropped requests are counted per machine and logged;
  refusals are codes the client words in its own language.
- **What the clients see**: the host compares its layout (`forge_placed`) with
  what it sent each frame: an entry per object (id, tag as the host's palette
  index, object, holder, placed spot, properties), its removal, and where the
  objects the netcode does not move are (scenery, machines, controls, light
  fixtures, the generated building blocks: each client makes its own copy by
  tag path). Units, vehicles, weapons and equipment are the netcode's (made at
  the host's index everywhere); a pack's tag index is translated by path on the
  client (`forge_net_client_tag`, network_objects.c), so packs in another order,
  or one missing, never make the wrong object. The palette is sent by tag path
  and group and resolved on the client against its own browser (its allow
  list).
- **Late join**: a client that has loaded asks for the state (sync): the
  palette, the layout as a map variant's bytes (`forge_variant_capture`), read
  on the client through `forge_file_decode` with its own map expected, each
  record's id, object and holder, the end; then the changes.
- **Monitors**: each machine's monitor flies its own camera and tells the host
  where five times a second; the host tells everyone; others' monitors are drawn
  as "( O )" and the player's name, their units hidden (and unhurt on the host).
  An object a client holds is moved by it and its moves sent fifteen times a
  second; the netcode leaves its place to that client meanwhile.
- **NEW ROUND** online is a round of its own (the engine's map reset is a local
  game's): every hold let go, the layout's objects placed again where the editor
  put them. Host only, as CLEAR AI and ALL AI; a client's menu says so.
- **Lobby**: the Forge lobby's NETWORK row (LOCAL by default; SYSTEM LINK,
  ONLINE, AD HOC) makes it live as the multiplayer lobby: its game hosted at
  once, private by default online, friends, party, codes, INVITE FRIENDS (L) and
  the browser join it, its players listed (n/8), START FORGE takes everyone in.
  The settings panel's Forge page has Network for the Halo CE menus' players
  (the System Link screen's profile, then the game's lobby). Joiners prebuild
  the host's packs and the map's blocks in the lobby.
- **Tests**: `run_netns_online_test.sh forge` (two machines: edits both ways,
  grabs refused both ways, a client's Elite run by the host, undo, junk
  requests, NEW ROUND, saves), `forge3` (a late joiner given the layout, its
  object seen by all, leaving while holding), `forgeold` (insider.15 and
  `HALO_FORGE_NET=0` refused, then playing Slayer), `forgeleave` (the host
  leaves), `h3forge` (the lobby by the controller), `forgepanel` (the panel's
  route); `run_net_fuzz_test.sh` target `forge` (checks and libFuzzer).
- **Open**: the map's own scenery is not editable online; Halo 3's monitor
  model is a text marker; ONLINE and AD HOC Forge lobbies are not run in netns
  (the multiplayer lobby's paths, shared); a host changing its packs after a
  joiner answered is not told to it again.

## Open questions for the owner

1. **The controls.** Is the button table right against your Halo 3
   diagram? In particular: what are Halo 3's thrust and rotate buttons,
   and is D-pad up for monitor/player OK in a Forge game (zoom moves to a
   rear touch zone there)?
2. **Monitor or Spartan in the sandbox first?** Halo 3 lets you play the
   layout as a Spartan in the same session. Phase (a) assumes yes.
3. **Campaign levels or multiplayer maps first?** Multiplayer maps have
   the memory for packs and are where layouts become game variants;
   campaign levels have the AI. The recommendation is multiplayer first,
   campaign later with a measured tag cache.
4. **The budget.** Halo 3: 640 items per map and a cost per item. What
   limit on the Vita (frame rate, not memory, is the limit: AI characters
   cost the most)? Proposed: 256 objects, 32 AI characters, and a cost
   per object shown in the browser.
5. **Packs' memory.** Is spending up to ~12 MB more main memory on a
   campaign level's tag cache acceptable, if measured fine on the Vita?
   (Multiplayer maps need none.)
6. **Forge blocks.** Halo 3 and Reach have walls, ramps and platforms made
   for Forge. Halo CE has none. Ours would have to be new models we make
   (CC0 or our own), shipped as our own pack. Wanted, and when?
   *Decided (Oct 19): wanted; made by the game, textured with the map's own
   bitmaps ("Building blocks as built").*
7. **Sharing.** Friends only (a code, the Discord, a file on the memory
   card), or a public list on our server? Phase (c) assumes friends and
   lobbies; a public list needs moderation and is part 3's question.
   *Decided (Oct 18): friends and lobbies only, no public list or server
   upload.*
8. **Custom Edition maps** in Forge: same rules, later?
9. **Name and file extension** for layouts (`.forge`? Halo 3 called them
   "map variants"). *Decided (Oct 18): "map variants" in the UI, files
   `.hmv`.*

## Risks

- Pack building and loading touch tag relocation and validation, the code
  every security fix of the last months went into. It gets its own fuzzing
  and ASan runs before it is on by default.
- AI characters are the frame rate's biggest cost on the Vita (the AI
  benchmarks in `triage/`). A budget per layout, enforced on load.
- Objects held through walls end up inside the structure. On drop,
  `object_force_inside_bsp` with the monitor's position as the known good
  point, as the game does for a stuck object.
