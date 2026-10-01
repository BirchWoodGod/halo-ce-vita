# Halo: Combat Evolved for the PS Vita

A native PlayStation Vita port of **Halo: Combat Evolved**, built from the
decompilation of the Xbox game. It is not an emulator: the game's own code
is compiled for the Vita's ARM processor, and its Direct3D rendering is
translated to the Vita's GPU.

**No game data is included.** You need your own Xbox copy of Halo: Combat
Evolved.

## What works

- The whole campaign from the menus, with checkpoints, saves and Save and
  Quit, cinematics, and the movies (converted to MP4, see below).
- Multiplayer maps on your own (split screen with one player), and system
  link over Wi-Fi with other Vitas or the Linux/Windows builds of the port.
- Profiles, controller settings and the game's settings menus.
- A settings panel for the Vita's quality and control options: hold
  **Select + Start** in game.
- 30 fps in cinematics and most of the campaign; the largest fights (The
  Silent Cartographer's beach) run in the high teens to low twenties.

## Install

You need a PS Vita or PS TV with HENkaku/Ensō (firmware 3.60 to 3.74),
VitaShell, and about 1.5 GB free on `ux0:`.

1. Install `halo.vpk` with VitaShell. The bubble is called **Halo CE**.
2. Copy the `maps` folder of your Xbox disc to `ux0:data/haloce-vita/maps/`.
   To get it from a disc image, `extract-xiso -x "Halo.iso"` and take the
   `maps` folder. All versions of the Xbox game work.
3. Start the game. The first load of each level takes a while: the game
   decompresses it into a cache file on the memory card.

Without the maps the game shows where to copy them and exits.

### Movies (optional)

The Xbox movies are Bink files, which the Vita cannot play. Convert them
(the disc's `bink` folder) to H.264 MP4 and put them in
`ux0:data/haloce-vita/movies/` under the same names (`intro.mp4`,
`credits.mp4`, `attract1.mp4` ...):

```
ffmpeg -i intro.bik -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
       -vf scale=640:-2 -c:a aac -b:a 128k intro.mp4
```

A movie without an MP4 is skipped, as the game skips a missing movie.

## Controls

| Vita | In game |
| --- | --- |
| Left stick / right stick | move / look |
| R / L | fire / throw grenade |
| Cross | jump |
| Circle | melee |
| Square | reload, action |
| Triangle | switch weapon |
| D-pad down / up | crouch / zoom |
| D-pad left / right | switch grenades / flashlight |
| Start | pause; skips a cinematic |
| Select | scoreboard |
| Select + Start (hold) | settings panel |

## Settings panel

Hold Select + Start for a second. Up and down choose a setting, left and
right change it, Circle closes the panel. Changes apply at once (the render
resolution after a restart) and are kept in `ux0:data/haloce-vita/settings.txt`.

The defaults favour frame rate: lower model detail at a distance, tiny
distant objects skipped, static props and object lighting updated less
often, and a 75% render resolution. Set model detail High, distant objects
Off, scenery and lighting to every tick and the resolution to 100% to see
the game exactly as on the Xbox.

## Building

You need [VitaSDK](https://vitasdk.org) (set `VITASDK` or install it in
`~/vitasdk`), clang 17 or newer, Python 3 and ninja.

```
python3 configure.py --linux-cc <clang for armhf Linux> --lto off --pgo off --portable --release
ninja vita
```

The results are `build/vita/eboot.bin` and `build/vita/halo.vpk`. The game
code is compiled by clang with the game's MSVC-like ABI, the Vita-side code
by VitaSDK's GCC. [port/vita/README.md](port/vita/README.md) has the
details: the layout of `port/vita`, testing in Vita3K and in a Linux build
of the Vita renderer, debug switches, and the files the game keeps on the
memory card.

## Contributing

Issues and pull requests are welcome. Open work:

- **Performance** in the biggest fights: the render on the first core is
  the limit.
- **Ad hoc multiplayer** between two Vitas without a router, then online
  play.
- **The retail loading screen**: the decompilation is of a pre-release
  build whose loading screen Bungie replaced before release.

When you report a problem, attach `ux0:data/haloce-vita/halo.log` and
`halo-prev.log` (the previous session's log, kept after a crash).

## Credits

This port stands on a lot of other people's work:

- **Bungie** made Halo: Combat Evolved. Halo is a trademark of Microsoft.
- **[punpckhdq/halo](https://github.com/punpckhdq/halo)**: the decompilation
  of the Xbox build 2342 (`cachebeta.exe`, SHA-256
  `4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`),
  including the decompiled Xbox libraries in `libs/` (Direct3D 8, the C
  runtime, XAPI, Bink).
- **[bnunu/halo-1](https://github.com/bnunu/halo-1)**: the fork of that
  decompilation the native port starts from.
- **[cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal)**:
  the native Linux, Windows and Android port this repository is built on:
  the platform layer, the OpenGL renderer the Vita renderer is modelled on,
  the distributed netcode, system link over the internet, and much more.
  Those platforms still build from this tree (`port/linux`, `port/windows`,
  `port/android`, each with its own README).
- **[Invader](https://github.com/SnowyMouse/invader)** by SnowyMouse: the
  tag definitions `tag_layouts.h` is generated from, which let the port
  relocate the maps' tags.
- **The Xita project**: the earlier work on running Halo on the Vita, whose
  findings (the register combiner translation, the GPU and threading
  lessons, the tools) went into this port.
- **PS Vita port**: BirchWoodGod.

Libraries and tools: [VitaSDK](https://vitasdk.org),
[SDL3](https://github.com/libsdl-org/SDL) (desktop builds),
[tomlc17](https://github.com/cktan/tomlc17),
[KCP](https://github.com/skywind3000/kcp),
[Mbed TLS](https://github.com/Mbed-TLS/mbedtls),
[miniupnpc](https://github.com/miniupnp/miniupnp),
[musl](https://musl.libc.org)'s math functions,
[extract-xiso](https://github.com/XboxDev/extract-xiso), and
[Vita3K](https://vita3k.org) for testing.

This project is not affiliated with or endorsed by Microsoft or Bungie,
and it contains no game assets.
