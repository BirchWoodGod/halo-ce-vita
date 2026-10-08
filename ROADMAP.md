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

## In beta: 1.1.0: multiplayer, co-op, chat and custom maps

The 1.1.0 betas are on the
[releases page](https://github.com/BirchWoodGod/halo-ce-vita/releases) as
pre-releases (network version 19: every Vita in a game needs the same
version).

- **Online play between Vitas** with OpenCE's in-game multiplayer menus: a
  server browser of public games (with passwords), Join by code, and
  Create Game with Server Setup. Needs Halo PC's `bitmaps.map`,
  `sounds.map` and `loc.map`. Built on iamhaller's networking work. Vita to
  Vita only.
- **Same Wi-Fi** (system link) and **ad hoc** play between Vitas.
- **Game chat**: quick-chat phrases and typed messages in the lobby and in
  game (Back + Y), with mutes and the host's On / Quick chat only / Off.
- **Split screen on the PS TV**: up to four players on one PS TV with
  DualShock 3 / DualShock 4 controllers, in multiplayer games and in the
  campaign for two.
- **Campaign co-op over the network** for up to four Vitas, Private or
  Public, built on OpenCE's network co-op (the Xbox only had split-screen
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

### Coming in the later 1.1.0 betas

- **Voice chat** in network games.
- **A faster Quality profile**: the Xbox's full detail at a better frame
  rate.

## Later

- **A steady 30 fps** in the biggest fights.
- **QR code invites**: the host shows its lobby code as a QR code, and the
  joiner scans it with the Vita's camera.
- **Loading**: a level start without the remaining few seconds of waiting.

## Help wanted

Bug reports with crash dumps are the most useful contribution: see
[Reporting a crash](README.md#reporting-a-crash-or-a-problem). Pull
requests are welcome, especially for performance (the render on the
Vita's first core), the open issues above, and testing multiplayer and
co-op with two or more Vitas.
