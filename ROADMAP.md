# Roadmap

Where Halo CE for PS Vita is and where it is going. Dates are aims, not
promises: every change is tested on a real Vita before it ships.

## Released

### 1.0 (October 1, 2026)

The whole campaign and the multiplayer maps on the Vita, built natively
from the decompilation of the Xbox game.

### 1.0.1

Crash fixes for explosions, grenades, particles and decals; no more
flickering models; lens flares; skipping cutscenes; working multiplayer
(the Vita's network stack refused the game's player updates); checkpoints
without the freeze; a crouch toggle; an FPS counter switch.

### 1.0.2

Textures stream in instead of freezing the game; faster loading; much
cheaper big fights (simulation of many soldiers and sounds); the
flashlight and dynamic lights now light the level; steady bloom and lens
flares; saves that keep working across updates.

### 1.0.2.1 and 1.0.2.2

Security fixes: the Xbox's debug console no longer listens on the network,
and three bugs in the original LAN multiplayer code are closed.

### 1.0.3 (October 6, 2026)

Big fights run much better (about 25 fps on The Silent Cartographer's
beach with the defaults, 28 with the Performance profile); shaders ship
precompiled, so reaching a new area no longer freezes; graphics profiles,
a 4:3 option and Sharp upscaling, all applied without a restart; crash
fixes from players' dumps (dropped weapons, HUD sounds, vehicles); fixed
checkpoints; Warthog windshields, energy shields, camouflage, scopes and
water drawn correctly.

### 1.1.0 (October 14, 2026): multiplayer, co-op and custom maps

- **Online play between Vitas** with OpenCE's in-game multiplayer menus: a
  server browser of public games (with passwords), Join by code, and
  Create Game with Server Setup. Needs Halo PC's `bitmaps.map`,
  `sounds.map` and `loc.map`. Built on iamhaller's networking work. Vita to
  Vita only.
- **Ad hoc** play between Vitas (no router), next to **Same Wi-Fi** (system link).
- **Campaign co-op over the network** for two Vitas (three or four
  experimental), Private or Public, built on OpenCE's network co-op (the Xbox only had split-screen
  co-op on one console).
- **Custom maps**: Xbox maps, and Halo PC / Custom Edition multiplayer and
  campaign maps; a joiner downloads the host's map in the lobby.
- **Easier Custom Edition setup**: the game takes the three files out of
  your own Halo Custom Edition installer copied to the Vita, and restarts
  itself; or copy them from Halo MCC or a Custom Edition install. The files
  themselves are never included.
- **A relay** for networks that cannot connect to each other directly (none
  set by default).
- **Graphics settings** after Bruno Santana's build (shadows, lights,
  effects, particles, AI think rate, sound updates) in the profiles,
  **frame interpolation** up to 60 fps, the **fourth CPU core** with
  CapUnlocker, and dynamic resolution.
- **Settings panel in tabs**, remappable buttons and touch zones in Xbox
  controller terms, gyro aiming and PlayStation button icons.
- **Movies optional**, and a **Windows install tool**.
- Fixes for #30 (16:9 scopes), #31 (sun glow), #32 (muzzle flash) and #33
  (The Maw's armoury: shadows, and the cloaked Flood's slowdown).

## Next: 1.1.1 (insider builds now, public beta next)

Testers in the [Discord](https://discord.gg/Ed7sbCgAF5) play the insider
builds first. The public 1.1.1 beta comes out once every problem they
reported is fixed and the features under "Before the public beta" are in.

### Already in the insider builds

- **Halo 3-style menus**: main menu, campaign and multiplayer lobbies, a
  live multiplayer lobby, SELECT PROFILE, new pause menus. The classic
  menus stay as a setting, and every option stays in the settings panel.
- **Friends, party (up to 8) and party chat**: friend codes, invites,
  joining a friend's game without a code.
- **Forge**: fly as the Monitor, place, move and rotate objects, AI
  (Covenant, Flood, Sentinels, Marines), Banshees, Wraiths, Shades and the
  flyable Pelican on every map, building blocks, saved map variants, and
  multiplayer Forge for up to 8.
- **File Share and a mod manager**, and MCC campaign mods (offline).
- **Crossplay with PC**: one browser for Vita and PC games, WHO CAN JOIN
  for hosts, OpenCE's newer network versions, and host-side anti-cheat.
- **System Link and Ad hoc browsers**, and Ad hoc co-op.
- **PC builds** of this port (Linux and Windows) with keyboard and mouse.
- Ragdolls, movies fixed, and a sharp interface layer.

### Before the public beta

- Every bug reported on the insider builds fixed (joining, map downloads,
  the server browser, freezes, the Customization preview, checkpoints).
- **Game type editor** in the Halo 3 style (being built).
- **Carnage report** after a game, Halo 3 style.
- **Halo 3-style message boxes** and campaign prompts.
- **"Classic plus"**: the new features on the classic menus too.
- **Renamed custom maps** (like `cursed-bloodgulch`) in the map lists.
- **Friends backup**: your friend identity saved with a password.

## Later

### Birch's ideas

- **A steady 30 fps** in the biggest fights, and the Quality profile at
  30 fps there.
- **Better performance on Custom Edition maps.**
- **QR code invites**: the host shows its lobby code as a QR code, and the
  joiner scans it with the Vita's camera.
- **Loading**: a level start without the remaining few seconds of waiting.
- **Text chat in games** (voice chat is likely too heavy for the Vita).
- **Split screen** on the PS TV.
- **Halo Reach-style menus** ("could be another thing to add in the
  future").
- **More armour and models** for Customization (artists wanted).
- **Sharing Forge variants with friends** through File Share.
- **Forge on the classic menus** too, not only the Halo 3 ones (OpenCE is
  adding Forge as well).
- **AI respawning** in Forge.
- **Forge's AI using the multiplayer bots' logic**: campaign characters
  (Elites, Grunts, Marines) playing the game type like the bots do.
- **A Warzone-style mode** (as in Halo 5): two teams fighting each other
  and the AI, with AI defending objectives (Hunters guarding a base,
  bosses worth points) set up in Forge.
- **Forge variants with AI played with the multiplayer bots.**
- **Multiplayer lobbies with bots** on the Vita: bots filling the empty
  slots in games you host for other players.
- **"VITA+"**: the VITA PC tag renamed if Android and iOS builds come.

### Claude's suggestions

Claude is the AI that helps build the port.

- **Game logic speed-ups** seen in other Halo CE projects: collision tests
  in one pass, sound occlusion that stops at the first hit, light passes
  limited to their area.
- **More online hardening**: more of the host's own checks on what
  players send.
- **The Windows build tested on real Windows** before it's called
  supported (it's tested under Wine so far).
- **Controls you can find**: zoom labelled "Right stick (zoom)" on the
  Buttons and Touch zones pages, since players looked for it.
- **Leftovers kept out of Forge**: Bungie's unfinished gravity rifle (it
  has no model and does no damage).

### Asked for by testers

| Request | Asked by | Birch's answer | Status |
|---|---|---|---|
| Field of view setting | DestinyTellsFacts | "I will add the fov btw, its on the list" | Planned |
| The original loading screens with their sound, and a fast loading option | xal | "Yeah they are broken right now, trying to bring them back" / "that loading screen is essential" | Planned |
| The 1999 reveal-trailer Master Chief armour | Wonka | "It's on the way. If anyone is good at modeling please hit me up!" | Needs a modeler |
| Zoom on the rear touchpad or the D-pad | BlazeRed | "There should be a remapping section and touch controls mapping" | Already there (zoom is "Right stick"); a clearer label is planned |
| Zoom on Select | Gold Yoshi | (xal: select + down, Controls tab) | Already possible |
| Aim assist off while using the gyro | Boopop | "I can add that option" | Done (insider.15) |
| A restart prompt after switching crossplay | LadyCassie | "Im actually adding a feature to be able to change the network settings live" | Done (no switch, no restart) |
| Linux build with bots | Helldiver450, Bruno Santana | "Comes with everything the vita version comes with and forge" | Done (insider.17) |
| Show renamed custom maps (`cursed-bloodgulch`) in the menus | Grunt pirate | Made a must for the beta | Before the public beta |
| Halo Reach-style menus | NXBLE | "maybe another time. If someone else makes it that'll help me a lot" | Later, help wanted |
| Aim assist on/off for the stick | xal | Not answered yet | Open |
| The stick's ramp-up as an option, dead zone and response curve settings | DestinyTellsFacts, xal, Wonka | Not answered yet | Open |
| Touchscreen aiming | xal | Not answered yet | Open |
| Skulls (campaign modifiers) | Wonka | "Skulls would be fun" | Planned |
| A "potato" graphics preset (flat lighting, no shadows, no filtering) | Helldiver450 | Not answered yet | Open |
| Dark armour colours easier to see in dark levels | DestinyTellsFacts | Not answered yet | Open |
| European (PAL) game files: Spanish, Italian, French, German | sobredosis | Not answered yet | Open (sobredosis wrote a guide) |

## Help wanted

Bug reports with crash dumps are the most useful contribution: see
[Reporting a crash](README.md#reporting-a-crash-or-a-problem). Pull
requests are welcome, especially for performance (the render on the
Vita's first core), the open issues above, and testing multiplayer and
co-op with two or more Vitas.
