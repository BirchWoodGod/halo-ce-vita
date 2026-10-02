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

## Next: 1.0.3 (planned for next week)

- **Flashlight:** characters lit by the flashlight flicker black on some
  frames.
- **Disappearing objects:** Master Chief vanishing in the Pillar of
  Autumn's cryo tube when you look down; trees flickering at the edges of
  the screen; some Covenant cover only visible from some angles.
- **Grass and terrain** look noisy up close.
- **Multiplayer:** a per-frame network cost of about 10 ms, and freezes of
  a second or so during matches.
- **Heavy fights:** the render is now the limit at the peak of the biggest
  fights (objects, particles, visibility and shadows).
- **Polish from player reports:** 16:9 movies shown at their own aspect
  ratio, the main menu's music, the debug build number on screen, letters
  on the name-entry keyboard, slightly distorted sound.

## Later

- **Online play between Vitas**: host a public lobby or a private one with
  a short code, join from the settings panel. Built and tested on PC,
  waiting for hardware tests. Vita to Vita only for now.
- **Ad hoc play** between Vitas without a router.
- **A relay** for networks that cannot connect to each other directly.
- **A steady 30 fps** in the biggest fights.
- **Loading**: a level start without the remaining few seconds of waiting.

## Help wanted

Bug reports with crash dumps are the most useful contribution: see
[Reporting a crash](README.md#reporting-a-crash-or-a-problem). Pull
requests are welcome, especially for performance (the render on the
Vita's first core), the open issues above, and testing multiplayer with
two Vitas.
