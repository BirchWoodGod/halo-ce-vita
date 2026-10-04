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

## Next: 1.0.3 (in beta)

Pre-releases are on the [releases page](https://github.com/BirchWoodGod/halo-ce-vita/releases).
Beta 1 is out; beta 2 fixes most of what testers reported:

- **Crashes and freezes** found in players' crash dumps (dropping a weapon,
  HUD warning sounds in fights, leaving a vehicle, a texture cache lock).
- **Checkpoints** that stopped coming after some saves, and missing
  marine reinforcements.
- **No more freezes reaching a new area**: the graphics shaders ship
  precompiled.
- **Graphics:** black camouflaged Elites, black scopes, flickering glass
  and effects late in a session, white placeholders on the HUD, the black
  screen between sections, the missing dropship in a cutscene, smoother
  weapon motion.
- **Heavy fights:** more speed in the biggest battles.
- Still being looked at: trees and Covenant shields flickering at the
  screen's edges, water on The Silent Cartographer.

## 1.1.0: multiplayer

- **Online play between Vitas**: host a public lobby or a private one with
  a short code, join from the settings panel. Built on iamhaller's
  networking work. Vita to Vita only for now.
- **Ad hoc play** between Vitas without a router.
- **QR code invites**: the host shows its lobby code as a QR code, and the
  joiner scans it with the Vita's camera.
- **A relay** for networks that cannot connect to each other directly.

## Later

- **Campaign co-op over the network**: play the campaign together on two
  Vitas, online or ad hoc. The Xbox game only had split-screen co-op on
  one console, so this is new work for the port.
- **A steady 30 fps** in the biggest fights.
- **Loading**: a level start without the remaining few seconds of waiting.

## Help wanted

Bug reports with crash dumps are the most useful contribution: see
[Reporting a crash](README.md#reporting-a-crash-or-a-problem). Pull
requests are welcome, especially for performance (the render on the
Vita's first core), the open issues above, and testing multiplayer with
two Vitas.
