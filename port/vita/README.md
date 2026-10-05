# Halo: Combat Evolved on the PlayStation Vita

The PS Vita build of the decompilation port. The game code is the same as
the Linux, Windows and Android builds; `port/vita` adds a Direct3D device on
the Vita's GPU (GXM), the controls, movies, sockets and the settings panel.

The port does not include any game data. You need your own copy of Halo:
Combat Evolved for the Xbox.

## Requirements

- A PS Vita or PS TV on firmware 3.60 to 3.74 with HENkaku/Ensō and VitaShell.
- The maps must be present: without `ui.map` the game shows where to copy
  them and exits.
- About 1.5 GB free on `ux0:`. The game decompresses the maps it loads into
  cache files on the memory card (up to about 765 MB).
- The Xbox game's `maps` folder (`ui.map`, `bloodgulch.map`, `a10.map` ...).
- Optional: the intro and credits movies converted to MP4 (see Movies).

## Installing

1. Install `halo.vpk` with VitaShell. The bubble is called **Halo CE**
   (title ID `HCEV00001`).
2. Copy the Xbox game's maps to `ux0:data/haloce-vita/maps/` (the whole
   `maps` folder), and the disc's `default.xbe` to `ux0:data/haloce-vita/`:
   the loading screen takes its picture from the executable, and stays dark
   without it. (A copy that [Xita](https://github.com/Xita-Project/xita)'s installer put in
   `ux0:data/xita/haloce/maps/` is used if that folder is missing, with the
   `default.xbe` next to it.)
3. Start the game. The first load of each map takes a while: the game
   writes its cache file to the card.

The game keeps its files in `ux0:data/haloce-vita/`:

| Path | What |
| --- | --- |
| `maps/` | the Xbox maps |
| `default.xbe` | the Xbox executable (the loading screen's picture) |
| `data/` | settings (`config.toml`), `init.txt`, the game's log (`debug.txt`) |
| `saves/` | profiles and saved games |
| `movies/` | the movies as MP4 (optional) |
| `shaders/` | shaders the Vita compiled itself: those the VPK does not ship (made on first use, in the background) |
| `settings.txt` | the settings panel's choices |
| `halo.log` | the port's log |
| `env.txt` | optional debug switches, one `NAME=value` per line |

### Movies

The Xbox movies are Bink files, which the Vita cannot play. Convert them to
H.264 MP4 (640x480 or smaller, AAC audio) and put them in
`ux0:data/haloce-vita/movies/` with the same names: `intro.mp4`,
`credits.mp4`, `attract1.mp4` ... For example, with ffmpeg:

```
ffmpeg -i intro.bik -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
       -vf scale=640:-2 -c:a aac -b:a 128k intro.mp4
```

A movie without an MP4 is skipped, as the game skips a missing movie. (The
game looks for `data/bink/<name>.bik` first; the port creates an empty one
for each MP4 at start-up.)

A movie is shown at the shape its file gives: one made 16:9 at 640x480
(ffmpeg `-aspect 16:9`) fills the screen's width, the Xbox's are 4:3.
`HALO_MOVIE_ASPECT=16:9` in `env.txt` forces a shape for files without one.

## Controls

| Vita | Xbox | In play |
| --- | --- | --- |
| Cross | A | jump, accept |
| Circle | B | melee, back |
| Square | X | reload / action |
| Triangle | Y | switch weapon |
| L | left trigger | throw grenade |
| R | right trigger | fire |
| Left stick | left stick | move |
| Right stick | right stick | look |
| D-pad down | left stick click | crouch |
| D-pad up | right stick click | zoom |
| D-pad left | Black | switch grenades |
| D-pad right | White | flashlight |
| Start | Start | pause; skips a cinematic |
| Select | Back | scoreboard |

In the menus the D-pad moves the selection.

## Settings panel

Hold **Select + Start** for about a second, in play or in the menus. Up and
down choose a setting, left and right (or Cross) change it, and Circle closes
the panel. The game does not see the buttons while the panel is open.

| Setting | Default | What it does |
| --- | --- | --- |
| Performance overlay | Off | frames per second, game and render times, core load |
| FPS counter | Off | the game's own frame counter, bottom right |
| Frame limit | 30 FPS | the most frames shown a second |
| Render resolution | 75% | the 3D view's resolution (applies after a restart) |
| Model detail | Low | level of detail of characters, vehicles and props |
| Hide distant objects | Small | skips objects that cover only a few pixels |
| Scenery updates | Quarter | how often static props are updated |
| Object lighting | Third | how often object lighting is recomputed |
| Sound voices | Original | the most positional sounds playing at once (Original: 46); the game's own priorities pick which; faster with fewer, but sound playback feeds back into the game (the AI drifts from the original's choices); applies after a restart |
| Sound occlusion | Every 3rd | how often a sound's muffling behind walls is rechecked while it and the camera stay put (Every tick: the original) |
| Look sensitivity | 100% | right stick turning speed |
| Crouch | Toggle | D-pad down crouches and the next press stands (Hold: crouch while held) |
| Invert look | No | reverses the right stick's up and down |
| Stick deadzone | Off | raise it if the sticks drift |

The choices are saved in `ux0:data/haloce-vita/settings.txt`.

## Multiplayer

- **Split screen** needs two players, and the Vita has one controller.
- **System link** over Wi-Fi between Vitas on the same network is in but not
  yet tested with two consoles. Play against PCs is not supported yet.
- **Ad hoc** (Vita to Vita without a router) and online play are not done yet.

## Building

You need:

- [VitaSDK](https://vitasdk.org) (set `VITASDK`, or install it in `~/vitasdk`).
- clang 17 or newer (the game code is compiled by clang with the game's
  MSVC-like ABI; the Vita-side code by VitaSDK's GCC).
- Python 3 and ninja.

```
python3 configure.py --lto off --pgo off --portable --release
ninja vita
```

(`configure.py` also writes the Linux build graph; `--linux-cc` picks its
compiler, and `ninja vita` builds only the Vita part.)

The results are `build/vita/eboot.bin` and `build/vita/halo.vpk`. Run
`configure.py` again after adding a source file or changing anything in
`port/vita/sce_sys` (the LiveArea images and the title).

### Testing without a Vita

- [Vita3K](https://vita3k.org) runs the build. Install the VPK, or copy
  `eboot.bin` into `ux0/app/HCEV00001/`. Vita3K hides some hardware
  problems (it reads render targets and mip chains its own way), so check
  graphics changes on a Vita too.
- `configure.py --linux-d3d gxm-null` builds the Linux game with the Vita's
  Direct3D device over a GPU that draws nothing. It runs on x86 and ARM Linux
  and measures the Vita render path's CPU cost without the hardware.

### The shipped shaders

The GPU programs are Cg the port writes from the game's combiner and vertex
program states, compiled on the device by SceShaccCg (0.6-1.5 s each on the
hardware). `port/vita/app0/shaders.pak`, in the VPK, holds the ones the
levels and multiplayer maps make, compiled ahead; `tools/vita_shader_pack.py`
says how it is made again after the Cg generators (`nv2a_psh_cg.c`,
`nv2a_vsh_cg.c`) change: collect the sources with the gxm-null build
(`HALO_SHADER_COLLECT`, `HALO_SHADER_TOUR`), compile them on Vita3K
(`HALO_SHADER_PRECOMPILE`), pack them. A program the pack misses is compiled
in the background on the device and kept in `shaders/`.

### Debug switches

`env.txt` takes the platform layer's and the port's environment variables.
Useful ones:

| Switch | What |
| --- | --- |
| `HALO_FRAME_TIMING=300` | a timing line every 300 frames in `halo.log` |
| `HALO_RENDER_PROFILE=1`, `HALO_TICK_PROFILE=1` | where the frame and the tick go |
| `HALO_GPU_STATS=1` | draws, uniforms, ring use per frame |
| `HALO_SCREENSHOT_DIR=ux0:data/haloce-vita/shots`, `HALO_SCREENSHOT_EVERY=n` | a BMP every n frames (at 100% resolution) |
| `HALO_NO_MOVIES=1`, `HALO_NO_AUDIO=1` | skip movies or sound |
| `HALO_DXT_MIPS=0` | compressed textures without their mip chains |
| `HALO_HEARTBEAT=1` | a line every 2 s in `heartbeat.txt` (is the game still running?) |
| `HALO_STARTUP_CHECKS=1` | the clocks and the cost of basic operations, logged at start-up |
| `HALO_ADHOC_PROBE=1` | logs what the Vita's ad hoc libraries do (see Help wanted) |
| `HALO_SHADER_ASYNC=0` | a shader that is neither shipped nor cached is compiled while the game waits (the default compiles it in the background and skips its draws until it is ready) |
| `HALO_SHADER_PACK=0` | ignore the shipped shaders (`app0:shaders.pak`) |
| `HALO_NET_PROFILE=1`, `HALO_NET_TRACE=1` | where a network game's frame goes; what its sockets do |
| `HALO_NET_CATCH_UP_TICKS=n` | the most ticks a frame of a System Link or online game runs to catch up with real time (default 2; 30 = beta.1's pacing) |
| `HALO_TIME_CODE=1` | draw the tick count that some cutscene scripts turn on (`time_code_show`) in the top-left corner, as the beta did |

## Layout of port/vita

| Path | What |
| --- | --- |
| `host/` | the Vita side, built with VitaSDK's GCC: start-up, GXM, controls, movies (AvPlayer), sockets (sceNet), the settings panel |
| `platform/` | built with the game's ABI: the Direct3D device (`d3d8_gxm.c`), the NV2A register combiner and vertex program translators (`nv2a_psh_cg.c`, `nv2a_vsh_cg.c`), texture decoding, Bink |
| `include/` | the boundary between the two (`vita_host.h`, `vita_gxm.h`) |
| `null/` | the do-nothing GXM for the Linux test build |
| `sce_sys/` | LiveArea images |

## Help wanted

- **Ad hoc multiplayer** so two Vitas can play without a router, then online
  play. The SDK names no call that joins an ad hoc group: with
  `HALO_ADHOC_PROBE=1` the PSP-style ad hoc libraries start, but the Vita
  stays out of any group (peer-to-peer sockets work over Wi-Fi).
- **Performance** in heavy fights (the render on the first core is the limit;
  see the timing lines).
- **DXT1 mip chains**: Vita3K shows rainbow noise on some DXT1 textures'
  smaller levels (`HALO_DXT_NOCHAIN_KIND=1` turns their chains off).
- **Movies**: the colour conversion and upload cost ~20 ms a frame; a GPU
  conversion would free the CPU.
