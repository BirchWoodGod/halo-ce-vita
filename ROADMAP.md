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

## Next: 1.1.0: multiplayer, custom maps and a steady 30 fps

- **Online play between Vitas**: host a public lobby or a private one with
  a short code, join from the settings panel. Built on iamhaller's
  networking work. Vita to Vita only for now.
- **Ad hoc play** between Vitas without a router.
- **QR code invites**: the host shows its lobby code as a QR code, and the
  joiner scans it with the Vita's camera.
- **A relay** for networks that cannot connect to each other directly.
- **A steady 30 fps**, including the biggest fights.
- **Campaign co-op over the network**: play the campaign together on up to
  four Vitas over Wi-Fi, online or ad hoc, built on halo-ce-universal's network
  co-op (the Xbox only had split-screen co-op on one console).
- **Custom maps**: play community-made maps, Xbox ones and Halo PC / Custom
  Edition ones, and download a host's map from its Vita when you join.
- **Easier Custom Edition setup**: the game picks out the `bitmaps.map`,
  `sounds.map` and `loc.map` that Custom Edition maps need from your own
  Halo Custom Edition installer or Halo MCC folder copied to the Vita, so
  no unpacking on a PC. The files themselves are never included.
- **Settings panel in tabs**, with a Modded maps tab, touch zones and
  remappable buttons in Xbox controller terms, and dynamic resolution.

## Next: 1.1.1

- **Game chat**: quick-chat phrases and typed messages in the lobby and in
  game, with mute.
- **Split screen on the PS TV**: more than one player on one PS TV with
  DualShock 3 / DualShock 4 controllers.

## Later

- **Loading**: a level start without the remaining few seconds of waiting.

## Help wanted

Bug reports with crash dumps are the most useful contribution: see
[Reporting a crash](README.md#reporting-a-crash-or-a-problem). Pull
requests are welcome, especially for performance (the render on the
Vita's first core), the open issues above, and testing multiplayer with
two Vitas.
