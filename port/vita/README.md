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
| `shaders/` | shaders the Vita compiled (made on first use) |
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
| Multiplayer | | opens the Multiplayer page (see Multiplayer) |

The choices are saved in `ux0:data/haloce-vita/settings.txt`.

## Multiplayer

- **Split screen** needs two players, and the Vita has one controller.
- **System link** works over Wi-Fi: every machine on the same network running
  this port (another Vita, or the Linux/Windows build) can host or join.
- **Online** and **ad hoc** play are new and **not yet tested on a Vita**;
  they are off unless chosen. Both end in the game's own System Link
  screens: the other machine's game shows in the list, and you host or
  join as on a local network.

The settings panel's last line, **Multiplayer**, opens a page of its own
(Circle goes back):

| Line | What it does |
| --- | --- |
| Network | **Wi-Fi** (the default): system link on this network. **Online**: internet play, with the Linux/Windows builds and other Vitas. **Ad hoc**: Vitas nearby, without a router. Applies after a restart. |
| Online games | **Private**: others join with your code. **Public**: your games are also listed for anyone to join. |
| Join with a code | Type another player's code with the D-pad (up and down change a letter, left and right move, Cross joins). |
| Browse public games | The games listed in the public lobby; Cross joins one. |
| Ad hoc room | 1 to 4: Vitas in the same room play together. |
| Ad hoc dialog | Which mode the system's ad hoc dialog uses: Connect (try first), Create or Join. |
| Join / Leave ad hoc group | Opens the system's ad hoc dialog (the panel closes), or leaves the group. |

A line under them says what is happening: your code while you host, the
lookup of a code, the connection, or the ad hoc group.

**Online.** Set Network to Online and restart. To host, create a game in
Multiplayer, System Link as usual: the Multiplayer page then shows **Your
code: ABCD-EFGH** (the code stays the same until you quit). Tell it to the
others; with Online games set to Public, your game is also listed. To
join, type the code under Join with a code (or pick the game under Browse
public games), wait for "connected to the host", then open Multiplayer,
System Link: the host's game is in the list. A Linux or Windows copy of
the port shows its code in its log next to its invite link, and joins a
code copied to the clipboard (with its dash) or given on its command line.

Internet play has no server of its own: the machines find each other
through public MQTT brokers and connect directly (UDP hole punching).
That fails between two networks whose NATs both give each destination its
own port (some mobile and company networks), unless one router forwards
the port. The Vita can ask its router for that (UPnP) but does not until
it has been seen working: `HALO_NET_ALLOW_UPNP=true` in `env.txt` turns
it on. A code is a convenience, not a password: anyone who guesses it can
join that game.

**Ad hoc.** Set Network to Ad hoc on every Vita and restart. Choose the
same Ad hoc room on each, then Join ad hoc group: the system's dialog
joins (or makes) the room's group. When the line under the page says
another machine is in the group, one Vita creates a System Link game and
the others find it in their lists. (Ad hoc carries the game the way online
play does, over the group instead of the internet.)

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
| `HALO_NET_TRACE=1` | every socket's calls, failures and traffic every 2 s, and the network library's free memory, in `halo.log` ("net trace:"); runs the socket self-test at start-up too |
| `HALO_NET_SELFTEST=1` | the socket layer's loopback and broadcast behaviour, logged at start-up |
| `HALO_NET_ALLOW_UPNP=true` | online play may ask the router to forward its port (UPnP; off on the Vita until tested) |
| `HALO_NET_BROKERS`, `HALO_NET_STUN` | the signalling brokers and STUN servers online play uses (`host:port`, commas between) |
| `HALO_ADHOC_GROUP=NAME` | the ad hoc group's name (up to 8 letters) in place of the room's |
| `HALO_ADHOC_ID_TYPE=0` | the ad hoc libraries' product ID type (default 1, reserved, as vitaQuake) |
| `HALO_ADHOC_PROBE=1` | logs what the Vita's ad hoc libraries do without joining a group |

## Layout of port/vita

| Path | What |
| --- | --- |
| `host/` | the Vita side, built with VitaSDK's GCC: start-up, GXM, controls, movies (AvPlayer), sockets (sceNet), the settings panel |
| `platform/` | built with the game's ABI: the Direct3D device (`d3d8_gxm.c`), the NV2A register combiner and vertex program translators (`nv2a_psh_cg.c`, `nv2a_vsh_cg.c`), texture decoding, Bink |
| `include/` | the boundary between the two (`vita_host.h`, `vita_gxm.h`) |
| `null/` | the do-nothing GXM for the Linux test build |
| `sce_sys/` | LiveArea images |

## Help wanted

- **Testing online and ad hoc play** on Vitas (see Multiplayer): send
  `halo.log` from each machine, with `HALO_NET_TRACE=1` in `env.txt`.
  Ad hoc play joins its group through the system's network check dialog
  in a PSP ad hoc mode (the SDK names no call that joins one directly; the
  dialog is how vitaQuake does it), then carries the game over PDP
  datagrams; which dialog mode two Vitas need is not known yet.
- **Performance** in heavy fights (the render on the first core is the limit;
  see the timing lines).
- **DXT1 mip chains**: Vita3K shows rainbow noise on some DXT1 textures'
  smaller levels (`HALO_DXT_NOCHAIN_KIND=1` turns their chains off).
- **Movies**: the colour conversion and upload cost ~20 ms a frame; a GPU
  conversion would free the CPU.
