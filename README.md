# Halo: Combat Evolved for the PS Vita

A native PlayStation Vita port of **Halo: Combat Evolved**, built from the
decompilation of the Xbox game. It is not an emulator: the game's own code
is compiled for the Vita's ARM processor, and its Direct3D rendering is
translated to the Vita's GPU.

**No game data is included.** You need your own Xbox copy of Halo: Combat
Evolved.

![A Warthog on The Silent Cartographer's beach, on a PS Vita](docs/screenshots/warthog-beach.png)

| | | |
| --- | --- | --- |
| ![Two Pelicans over the sea in The Silent Cartographer's opening](docs/screenshots/pelicans.png) | ![Landing on The Silent Cartographer's beach](docs/screenshots/beach-landing.png) | ![Covenant at a Blood Gulch base](docs/screenshots/blood-gulch.png) |

*Screenshots taken on a PS Vita.*

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

### What you need

- A PS Vita or PS TV with HENkaku/Ensō (firmware 3.60 to 3.74) and
  [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases).
- About 1.5 GB free on `ux0:` (the game keeps decompressed copies of the
  levels it loads).
- Your own **Xbox** copy of Halo: Combat Evolved (the disc, or an image of
  it). All versions of the Xbox game work. The PC version's maps do not.

### Steps

1. **Download `halo.vpk`** from the
   [latest release](https://github.com/BirchWoodGod/halo-ce-vita/releases/latest).
2. **Install it.** Copy the VPK to the Vita (VitaShell's USB or FTP mode),
   open it in VitaShell and confirm. The bubble is called **Halo CE**.
3. **Get the game files from your disc.** From an image,
   [extract-xiso](https://github.com/XboxDev/extract-xiso) unpacks it:
   `extract-xiso -x "Halo.iso"`. You need two things from it: the `maps`
   folder and `default.xbe`.
4. **Copy them to the Vita**, with VitaShell's USB or FTP mode:

   ```
   ux0:data/haloce-vita/maps/         <- the whole maps folder (ui.map, a10.map, bloodgulch.map ...)
   ux0:data/haloce-vita/default.xbe   <- the loading screen's picture is read from it
   ```

5. **Start the game.** The first load of each level takes a while: the
   game writes a cache file for it to the memory card.

Without the maps the game shows where to copy them and exits.

### Updating

Install the new `halo.vpk` over the old one. Your maps, saves and settings
in `ux0:data/haloce-vita/` are kept.

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

### Saving

Checkpoints are kept with **Save and Quit** from the pause menu; choose the
campaign again to resume. Closing the game from the home screen keeps the
levels you reached, but not the checkpoint, as on the Xbox.

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

### What you need

- Linux (or WSL on Windows) with **Python 3**, **ninja** and **clang 17 or
  newer** (the game code is compiled by clang with the game's MSVC-like ABI).
- **[VitaSDK](https://vitasdk.org)** for the Vita side (compiled by its
  GCC) and the packaging tools. Install it with
  [vdpm](https://github.com/vitasdk/vdpm) and set `VITASDK` (or put it in
  `~/vitasdk`).

For example, on Ubuntu or Debian:

```
sudo apt install git python3 ninja-build clang lld curl
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH        # (add both lines to ~/.bashrc)
git clone https://github.com/vitasdk/vdpm && cd vdpm
./bootstrap-vitasdk.sh && ./install-all.sh && cd ..
```

### Build

```
git clone https://github.com/BirchWoodGod/halo-ce-vita
cd halo-ce-vita
python3 configure.py --lto off --pgo off --portable --release
ninja vita
```

The results are `build/vita/eboot.bin` and `build/vita/halo.vpk`. Install
the VPK as above, or, with an FTP server running on the Vita (VitaShell's
SELECT), replace just the executable:

```
curl -T build/vita/eboot.bin ftp://<vita address>:1337/ux0:/app/HCEV00001/eboot.bin
```

Run `configure.py` again after adding a source file or changing anything in
`port/vita/sce_sys` (the LiveArea images and the title).
[port/vita/README.md](port/vita/README.md) has the details: the layout of
`port/vita`, testing in [Vita3K](https://vita3k.org) and in a Linux build of
the Vita renderer, debug switches, and the files the game keeps on the
memory card.

## Contributing

Issues and pull requests are welcome. Open work:

- **Performance** in the biggest fights: the render on the first core is
  the limit.
- **Ad hoc multiplayer** between two Vitas without a router, then online
  play.
- **Draw count**: objects are drawn one part at a time (about 270 of the
  ~360 draws in a big fight); batching them would help the most.

### Reporting a crash or a problem

Open an [issue](https://github.com/BirchWoodGod/halo-ce-vita/issues) with
what you were doing (level, place, weapon, vehicle) and these files from
the memory card (VitaShell's FTP or USB mode):

- `ux0:data/haloce-vita/halo.log` and `halo-prev.log`: the port's logs of
  this and the previous session (the previous one is the crashed one after
  a restart).
- After a crash, the newest `ux0:data/psp2core-....psp2dmp`: the crash
  dump. Leave the Vita alone for a minute after a crash so it finishes
  writing it (a dump still being written ends in `.tmp`).
- `ux0:data/haloce-vita/data/debug.txt`: the game's own log.

The logs hold no personal data beyond your profile names.

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
  tag definitions `port/linux/src/tag_layouts.h` is generated from (by
  `tools/gen_tag_layouts.py`), which let the port relocate the maps' tags.
- **[Xita](https://github.com/Xita-Project/xita)**: the earlier work on running Halo on the Vita, whose
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

## License

This port is licensed under the **GNU General Public License, version 3
only** ([LICENSE](LICENSE)), because `port/linux/src/tag_layouts.h` is
generated from Invader's GPL-3.0 tag definitions. To regenerate it, clone
Invader into `invader/` (or set `INVADER=<path>`) and run
`python3 tools/gen_tag_layouts.py port/linux/src/tag_layouts.h`.

The decompilation and the halo-ce-universal port this builds on are
dedicated to the public domain under CC0 1.0
([LICENSES/CC0-1.0.txt](LICENSES/CC0-1.0.txt)); the bundled libraries keep
their own licenses. The license covers this code only: Halo's maps,
executable and other game content belong to their owners and are not
included.

This project is not affiliated with or endorsed by Microsoft or Bungie,
and it contains no game assets.
