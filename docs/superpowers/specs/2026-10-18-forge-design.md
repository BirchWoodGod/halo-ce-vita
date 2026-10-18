# Forge for Halo CE Vita (design)

Date: 2026-10-18. Status: phase (a) built as version 1 (below, "Version 1
as built"), on the owner's decisions of Oct 18; the rest a draft.
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
  had it; it waits for the single-player loop to be right.
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
map, kept on the memory card (`ux0:data/halo/forge/<map>/`). A Forge lobby
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
| Turn the held object | L + right stick across | right mouse button / G + mouse |
| Push / pull it | L + right stick up/down | mouse wheel (or G + ...) |
| Delete (held, or under the crosshair) | Triangle | Delete |
| Object browser | Square | E or R |
| Copy (held or under the crosshair) | Circle | F |
| Set upright | D-pad left | X |
| Movement speed (slow, normal, fast) | D-pad right | Q |
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

### Left for phase (b), save and share

- The layout file and its validator (above): the session's objects are a
  bounded table already (`forge_placed`, 256 entries: object, palette tag,
  whether a character) - a layout is that table's tag paths, places and
  facings; nothing reads a file yet.
- A layout picker in the lobby, load on START FORGE, save from the Forge
  menu (`ux0:data/halo/forge/<map>/`), names typed on the Vita's keyboard.
- Object properties (team, respawn, place at start), undo, grid and angle
  snapping, touch controls, the blue ghost.
- File Share (part 3) and sending layouts in the lobby; packs for other
  maps' objects; campaign levels in the lobby (the console's monitor works
  there already); our own building blocks.

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
7. **Sharing.** Friends only (a code, the Discord, a file on the memory
   card), or a public list on our server? Phase (c) assumes friends and
   lobbies; a public list needs moderation and is part 3's question.
8. **Custom Edition maps** in Forge: same rules, later?
9. **Name and file extension** for layouts (`.forge`? Halo 3 called them
   "map variants").

## Risks

- Pack building and loading touch tag relocation and validation, the code
  every security fix of the last months went into. It gets its own fuzzing
  and ASan runs before it is on by default.
- AI characters are the frame rate's biggest cost on the Vita (the AI
  benchmarks in `triage/`). A budget per layout, enforced on load.
- Objects held through walls end up inside the structure. On drop,
  `object_force_inside_bsp` with the monitor's position as the known good
  point, as the game does for a stuck object.
