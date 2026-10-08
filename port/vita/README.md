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
| `host_invite.txt` | online play: the invite of the game you host (a private access token; see Multiplayer) |
| `join_link.txt` | online play: an invite or code to join, taken and removed by the game (see Multiplayer) |

### Movies

The Xbox movies are Bink files, which the Vita cannot play. Convert them to
H.264 MP4 (any size up to 960x544, AAC audio) and put them in
`ux0:data/haloce-vita/movies/` with the same names: `intro.mp4`,
`credits.mp4`, `attract1.mp4` ... For example, with ffmpeg:

```
ffmpeg -i intro.bik -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
       -vf scale=640:-2 -c:a aac -b:a 128k intro.mp4
```

A movie without an MP4 is skipped, as the game skips a missing movie. (The
game looks for `data/bink/<name>.bik` first; the port creates an empty one
for each MP4 at start-up.)

A movie is scaled to fill the screen at the shape its file gives (letter-
or pillarboxed only as that needs): the Xbox's 4:3 movies fill the height,
16:9 ones the width, whether made 640x360, 848x480, 960x544 or 640x480 with
ffmpeg `-aspect 16:9`. The rows the decoder adds to fill its last 16-pixel
macroblock (640x360 decodes to 640x368) are cut by the picture size in the
file. `HALO_MOVIE_ASPECT=16:9` in `env.txt` forces a shape for files without
one; `HALO_MOVIE_DUMP=n` writes the n-th frame as the decoder gave it
(`movie_nv12.raw`) and as converted (`movie_rgb.raw`).

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

In the menus the D-pad moves the selection. The settings panel's Controls
tab has two pages laid out as the Xbox controller: Button layout puts each
Xbox button on another Vita button (in play only: the menus keep this
layout), and Touch zones makes each touch zone (below) press an Xbox
button. Button layout's Button icons (Xbox by default) set to PlayStation
makes the game's button icons show the Vita button each Xbox button is on
now (below).

**Touch zones** (`port/vita/host/vita_controls.c`; positions in the
screen's 960 x 544 pixels, the rear pad scaled to the same as seen from the
front):

| Zone | Where | Counts |
| --- | --- | --- |
| Touch top left | front, 0-160 x 0-120 | at once |
| Touch top right | front, 800-960 x 0-120 | at once |
| Touch left edge | front, 0-120 x 190-400 (beside the D-pad, above the motion tracker) | at once |
| Touch right edge | front, 840-960 x 190-400 (beside the face buttons) | at once |
| Rear touch left | rear, 0-440 (all of its height), less the guard's border | once held the guard's time |
| Rear touch right | rear, 520-960, less the guard's border | once held the guard's time |

A finger counts for the zone it came down in for as long as it stays down;
one that comes down outside every zone does nothing, and each finger counts
on its own. The rear pad's guard (Rear touch guard, `HALO_TOUCH_REAR_GUARD`)
keeps the hands holding the Vita from pressing anything: a finger that comes
down within its border of the pad's edges (all four) never counts, even slid
inwards, and one further in counts once held its time. On hardware a grip
resting on the pad pressed a rear zone's button with the old 0.1 s and no
border.

| Rear touch guard | Border | Hold |
| --- | --- | --- |
| Off | none | 0.1 s |
| Light | 48 px | 0.15 s |
| Normal (default) | 96 px | 0.25 s |
| Strong | 144 px | 0.4 s |

A finger coming down counts as input for the screen's dimming.
`HALO_PAD_FILE` (debug, `vita_input.c`) takes `tl tr el er rl rr` for the
zones (touched at their middle, inside every guard's border).

**Gyro aiming** (`vita_controls.c`, `vita_input.c`; Controls, Gyro aiming
and the Gyro settings page, Off by default): the gyroscope's samples (`sceMotionGetSensorState`, every sample
since the last frame, radians a second about the Vita's axes) go through a
filter - the bias learnt from each second the Vita lies still (no axis
moving 2 deg/s, the accelerometer 0.03 g; drift of a resting Vita), small
motion smoothed (40 ms below 5 deg/s, none from 15 deg/s: hand tremor), a
0.75 deg/s deadzone - and are integrated into angles. Held in landscape,
the Vita's y axis (turning it left) is yaw left, x (its top edge towards
you) pitch up, z (steering left) yaw with Gyro turning Tilt. The angles
reach the game's look code as a direct change of facing, as mouse aim does
on the desktop (`xinput_sdl.c` `halo_linux_mouse_look`,
`source/game/player_control.c`): no stick acceleration, no clipping at full
deflection, divided by the zoom like the stick. The game takes them once a
frame while the player can look (not paused, not in a cinematic); what
turns in the menus, with the panel open or before a zoom (While zoomed) or a
button press (While holding) is dropped. The gyroscope is never input for
the screen's dimming. `HALO_PAD_FILE` takes `gx=N gy=N gz=N` (degrees a
second about x, y, z while the step is held, in place of the sensor;
`gy=60:1000` turns the view left for a second), `HALO_GYRO_SIM=x,y,z` the
same all the time, and `HALO_GYRO_LOG=1` logs the yaw and pitch the game
took (debug).

## Settings panel

Hold **Select + Start** for about a second, in play or in the menus. **L and
R** switch between four tabs, **Graphics**, **Controls**, **Audio** and
**Multiplayer** (and **Dev**, once Show dev settings is on); up and down
choose a line, left and right change it, Cross opens a line marked `>` (a
page of rarely changed rows, or an action), and Circle goes back from a
page or closes the panel. Each row's value is in a column of its own, the
chosen row's help is under the rows and the panel's buttons on the line
below. The game does not see the buttons while the panel is open. Rows
marked `*` apply after a restart. Which page shows a row changes nothing
of `settings.txt`: a file from an older version loads as it is.

**Graphics**

| Setting | Default | What it does |
| --- | --- | --- |
| Profile | Balanced | sets the rows marked (P) at once: Performance, Balanced, Quality; Custom when they match none |
| Render resolution (P) | 75% | the 3D view's resolution (applies at once); Dynamic: lowered in heavy scenes, from the Dynamic minimum up to 100% |
| Dynamic minimum | 50% | shown with Render resolution Dynamic (the resolution follows the graphics chip's load, frame by frame, up to 100%): the lowest it goes |
| Aspect ratio | 16:9 | 4:3: the Xbox's framing, with black bars (applies at once) |
| Upscale filter | Smooth | Sharp: crisp pixels |
| Frame limit | 30 FPS | the most frames shown a second |
| FPS counter | Off | the game's own frame counter, bottom right |
| Smooth weapon motion | On | the first-person weapon blended between game ticks |
| Advanced > | | the page below |

Graphics, **Advanced**:

| Setting | Default | What it does |
| --- | --- | --- |
| Model detail (P) | Low | level of detail of characters, vehicles and props |
| Hide distant objects (P) | Small | skips objects that cover only a few pixels |
| Scenery updates (P) | Quarter | how often static props are updated |
| Object lighting (P) | Third | how often object lighting is recomputed |
| Sun rays (P) | On | the sun's glow and light shafts outdoors (Off in Performance: seven small extra scenes a frame for the graphics chip while the sun is in view) |
| Distant AI | Every tick | Half: enemies more than 20 world units from every player recheck what they can see every other tick (a CPU saving in big fights; they react a tick later on average). In no profile yet: being tried |
| Tiny decals | Shown | Skipped: bullet holes and marks under 2 pixels across are not made. In no profile yet: being tried |

**Controls**

| Setting | Default | What it does |
| --- | --- | --- |
| Look sensitivity | 100% | right stick turning speed |
| Invert look | No | reverses the right stick's up and down |
| Crouch | Toggle | the left stick click (crouch): a press crouches and the next stands (Hold: crouch while held), from its Vita button or a touch zone |
| Gyro aiming | Off | turning the Vita aims, with the right stick: On, While zoomed, While holding (the Gyro button) |
| Rear touch guard | Normal | the rear zones ignore a touch that starts near the pad's edges (where the hands holding the Vita rest) and count once held: Off (no border, 0.1 s), Light, Normal (96 px, 0.25 s), Strong (144 px, 0.4 s) |
| Button layout > | As shipped | Button icons (Xbox; PlayStation: below), then A, B, X, Y, Black, White, Left trigger, Right trigger, Left stick click, Right stick click, Back: the Vita button of each in play (Cross, Circle, Square, Triangle, D-pad left, D-pad right, L, R, D-pad down, D-pad up, Select as shipped; None: no button; the help line says what Halo does with it); a Vita button on two Xbox buttons presses both. The row says Custom once one is moved |
| Touch zones > | Off | Touch top left, top right, left edge, right edge, Rear touch left, right: the Xbox button each zone presses (A, B, X, Y, Black, White, Left trigger, Right trigger, Left stick (click), Right stick (click) or Back). The row says how many are on |
| Gyro settings > | | Gyro button (L: While holding aims while held; in play it then does nothing else), Gyro sensitivity (1.5x; 0.5x-3x, 1x turns the view as far as the Vita turns, less while zoomed), Gyro vertical (Normal: tilt the top edge towards you to look up; Inverted), Gyro turning (Turn (yaw), or Tilt (roll) it like a wheel) |
| Advanced > | | Stick deadzone (Off; raise it if the sticks drift), Reset controls (look, crouch, deadzone, buttons and touch zones as shipped; gyro aiming, its settings, Button icons and Show dev settings stay), Show dev settings (Off; shows the Dev tab) |

**Button icons** (`HALO_BUTTON_ICONS`, xbox or playstation; live, kept by
Reset controls): with PlayStation each of the game's button icons - the
HUD's prompts (`source/interface/hud_messaging.c`), the icons in the menus'
and the HUD's text and the menus' button hints (`ui_widget.c`) - shows the
Vita button its Xbox button is on now (`vita_button_glyph` in
`port/vita/host/vita_controls.c`): in play the Button layout's, else the
first touch zone set to it; in the menus the fixed layout (A Cross, B
Circle, X Square, Y Triangle, the triggers L and R, Back Select). Cross,
Circle, Square and Triangle are drawn in code (`source/interface/hud_draw.c`:
two strokes, a ring, a square, a triangle, of the game's own white bitmap),
centred in the Xbox icon's sprite and as tall as its button, in the
PlayStation's own colours (Cross blue, Circle red, Square pink, Triangle
green); L, R, START, SELECT, the D-pad ("D-pad down") and the
touch zones ("rear touch left") are words in the game's font. An Xbox
button on no Vita button and no zone keeps its Xbox icon; so do the
sticks' icons (move, look). The settings panel keeps the Xbox controller's
names (A, B, X, Y, Black, White ...) with either setting: the Button icons
change the game's icons only.

The Touch zones page draws the front screen and the rear pad beside the
rows: the chosen row's zone blue (dark blue while Off), the zones set to an
Xbox button grey. The Gyro settings page has a line with the gyroscope's
rates now (degrees a second: yaw, pitch, roll) and "learnt" once the Vita
has lain still for a second.

**Audio**

| Setting | Default | What it does |
| --- | --- | --- |
| Sound voices * | Original | the most positional sounds playing at once (Original: 46); the game's own priorities pick which; faster with fewer, but sound playback feeds back into the game (the AI drifts from the original's choices) |
| Sound occlusion (P) | Every 3rd | how often a sound's muffling behind walls is rechecked while it and the camera stay put (Every tick: the original) |

**Multiplayer**: see [Multiplayer](#multiplayer).

Multiplayer's **Modded maps** page lists the maps in the maps folder that
are not the Xbox's own: name, size, kind (**Xbox** for a modded or newly built Xbox map, **CE**
for a Halo Custom Edition one, **CE+OS** for an OpenSauce `.yelo`, marked
`*`) and On or Off. Left and right turn a map off or on: an Off map stays in
the folder but is left out of the multiplayer map list (`HALO_MAPS_DISABLED`
in `settings.txt`; a game on a map you turned off may not be joinable).
Square deletes a map, with its `.bmp` picture and `.txt` description, after
asking (not the map being played). **PC maps** (Off by default,
experimental) puts the Custom Edition maps in the map list; the page warns
when the Custom Edition resource maps they need (`bitmaps.map`,
`sounds.map`, `loc.map`) are not in the maps folder. **Map downloads**
decides what happens when you join a game on a custom map you lack:
**Ask** (the default) asks whether to download it from the host, and in a
game joined from the public lobby warns that the host is a stranger (only
accept maps from players you trust); **Not public games** asks only in
games joined with a code, on the same Wi-Fi or ad hoc; **Never** offers no
downloads (`HALO_MAP_SHARE_FROM`: `ask`, `private`, `never`).

**Dev** (once Show dev settings is on): debug switches for testers. Each
is an `env.txt` variable; a switch is saved in `settings.txt` only while it
is on, and off, `env.txt`'s value (or the default) applies. While any is on,
`halo.log` says `settings: TEST MODE, dev switches on: ...` near its top.

| Switch | Variable | What it does |
| --- | --- | --- |
| Performance logging * | `HALO_FRAME_TIMING=300`, `HALO_RENDER_PROFILE=1`, `HALO_TICK_PROFILE=1` | where the frame and the tick go, in `halo.log` |
| Crash dump on hang | `HALO_HANG_CRASH=1` | a hang of 8 s (no frame presented) crashes on purpose, for a crash dump |
| FPS overlay | `XV_FPS` | Off, FPS only (2), Full (1): frames per second, game and render times, core load, render scale, GPU time, free video memory (VRAM; yellow while part of the texture cache is in main memory) |
| Debug camera | `HALO_DEBUG_CAMERA=1` | hold Black for a second: follow, orbit, then a flying camera |
| Ad hoc dialog | `HALO_ADHOC_DIALOG_MODE` | which mode the system's ad hoc dialog uses: Connect (try first), Create or Join |
| Save report | | copies `halo.log`, `halo-prev.log`, `settings.txt`, `env.txt` and the newest `ux0:data/psp2core-*.psp2dmp` into `ux0:data/haloce-vita/report-<date>/` and shows the folder |
| A/B switches > | | the four below, on a page of their own |
| GPU W clamp * | `HALO_GXM_WCLAMP=0` | A/B: models close to the camera dropping out (issue #9) |
| Target mip minimum * | `HALO_TARGET_CHAIN_MIN_SIZE` | A/B: the smallest mip level of render targets (32, 16 or 8 pixels) |
| Frame phase lock * | `HALO_FRAME_PHASE_LOCK=0` | A/B: 30 FPS frames kept between two ticks (Off: a fixed period) |
| Render target sync * | `HALO_GXM_RTT_SYNC=0` | A/B: a scene waits for a render target drawn just before it |

The choices are saved in `ux0:data/haloce-vita/settings.txt`.

## Multiplayer

**Vitas play only Vitas.** Online, ad hoc and system link games on the Vita
are between Vitas: a Vita does not list or join a PC's game, and the Linux,
Windows and Android builds do not list or join a Vita's (a Vita cannot keep
up with a PC host's game). Every machine needs the same version of this
port and Xbox maps of a supported build (NTSC 01.10.12.2276 or 01.08.15.1749,
PAL 01.01.14.2342); maps of another build cannot open the multiplayer menu.

- **Split screen** needs two players, and the Vita has one controller: a
  split screen game starts with you alone, to play the multiplayer maps on
  your own.
- **System link** over Wi-Fi: Vitas on the same network host and join each
  other's games under Multiplayer, System Link.
- **Ad hoc** (Vitas side by side, no router) and **online** (internet
  play) are **experimental**: each has a page in the Multiplayer tab while
  Connection is set to it.
  Both end in the game's own System Link screens: the other Vita's game
  shows in the list, and you host or join as on a local network.

**Hosting and joining.** Put the Vitas on the same Wi-Fi network, open the
settings panel (hold Select and Start) and go to **Multiplayer**:

1. **Host a game** (or **Join a game**) shows the steps; Cross opens the
   game's System Link screen for you.
2. Press A to join if asked, A on your profile, then A again.
3. **SYSTEM LINK GAMES** lists the games on the network. The host presses
   **Y** to create one (A on a map, A on a game type); the others press
   **A** on the host's game.
4. Everyone waits in the lobby; A there starts the game sooner. A custom
   map a joiner lacks comes from the host (the game asks, naming the host,
   and warns in a public game; Cross: yes; Modded maps, Map downloads),
   before the game starts: the host's game waits for the download, and its
   lobby shows the progress by the joiner's name. For a PC map, a joiner
   with PC maps off is asked to turn it on; a PC map also needs Custom
   Edition's `bitmaps.map`, `sounds.map` and `loc.map` in the maps folder
   (the game says which are missing). A joiner who cannot get the map is
   told why (an older host, a game already started: join in the lobby).
   A download that stops (cancelled, the connection lost) goes on from
   where it stopped when you join for the same map again.

The game's menus use the Xbox's buttons: A is Cross, B Circle, X Square, Y
Triangle, Back Select. The same screens are under Multiplayer, System Link
Play in the main menu. To host on a PC (Custom Edition) map, turn on PC maps
on the Modded maps page first.

The settings panel's **Multiplayer** tab:

| Line | What it does |
| --- | --- |
| Host a game / Join a game | The steps above, then the game's System Link screen (from the menus, outside a lobby). |
| Connection | **Same Wi-Fi** (the default): system link on this network. **Ad hoc** (experimental): Vitas side by side, without a router. **Online** (experimental): internet play. Applies after a restart. |
| Co-op > | **Co-op campaign**: **Off** (the default): the games you host are multiplayer. A level: the games you host are that campaign level played together (co-op, below). **Co-op difficulty**: Easy, Normal, Heroic or Legendary, for the co-op games you host. The row says the level. |
| Online games > | With Connection Online: the page below. |
| Ad hoc > | With Connection Ad hoc: **Ad hoc room** (1 to 4: Vitas in the same room play together), **Join / Leave ad hoc group** (opens the system's ad hoc dialog, the panel closing, or leaves the group). |
| Modded maps > | Your custom maps (above). |

Lines under them say your Vita's name and address and what the game is
doing (looking for games and how many it found, hosting and how many Vitas
are in, in a lobby, in a game), and with Online or Ad hoc, your code or the
ad hoc group.

Internet play's lines, on Multiplayer's **Online games** page (Connection
Online):

| Line | What it does |
| --- | --- |
| Online games | **Private**: others join with your code. **Public**: your games are also listed in the server browser for any Vita to join. |
| Join with a code | Type another player's code with the D-pad (up and down change a letter, left and right move, Cross joins). |
| Browse public games | The server browser: one line a game (its name, players, map, **[pw]** if it has a password, **PC** on a Halo PC map), and the chosen game's Rules ("Slayer to 50 on Blood Gulch") and Players ("5 of 16: name, name... +3 more") below. Cross joins it, Square refreshes the list. |

The ad hoc dialog's mode (Connect, Create or Join) is a Dev switch.

**Online.** Set Connection to Online and restart. To host, create a game (Host a game, or Multiplayer, System Link):
the Multiplayer tab then shows your code, **ABCD-EFGH** (it stays the same
until you quit the game). Tell it to the others; with Online games set to
Public, your game is also listed. To
join, choose **Join a game**: it asks for the host's code first (Triangle
there browses the public games instead), then shows the steps with how the
lookup goes. Once it says "connected to the host", Cross opens System Link,
and the host's game is in the list. Join with a code and Browse public
games on the Online games page do the same. A code is a convenience, not a
password: anyone who has it (or guesses it) can join that game.

The long invite still works too, as a fallback: the host's is written to
`ux0:data/haloce-vita/host_invite.txt` (a private access token: share it
only with the players you invite, and only while that game runs). To join
with one, copy it with VitaShell or FTP under a temporary name into
`ux0:data/haloce-vita/`, then rename it to `join_link.txt` (so the game
never reads half a file); the game takes and removes the file within a
second, and `halo.log` says what came of it. A file holding a code
(`ABCD-EFGH`) works the same way.

Internet play has no server of its own: the Vitas find each other through
public MQTT brokers and then connect directly (UDP hole punching). That can
fail between two networks whose NATs both give each destination its own port
(some mobile and company networks, double NAT): forwarding a UDP port on one
router helps (`HALO_NET_TUNNEL_PORT=<port>` in `env.txt`, the same port
forwarded to that Vita). There is no relay and no UPnP on the Vita yet.

What others can see: the public MQTT brokers and STUN servers (third
parties) see each Vita's public IP address while it plays online, and the
Vitas it plays with learn it too, as in any peer-to-peer game. Everything
sent through the brokers is encrypted with keys from the invite, so the
brokers learn no codes or invites, but anyone browsing the public games sees
a public game's name (your Vita's user name), its map, its rules, its
players' names and counts, and can join it. A public game's listing is
signed with the host's key (OpenCE's server browser), so no one else can
list, change or remove it; it never holds the game's code. A game with a
password lists its invite sealed with the password (Argon2id, about half a
second on a Vita to try one), so only those who know it can join from the
browser; a code still joins it. Private games are not listed. Vitas list on
their own topics, signed under their own label: a PC never sees a Vita's
public game, nor a Vita a PC's. A host can ban by device: the ID the
game sends is a keyed hash (HMAC) of the Vita's OpenPSID made for this game
only, not the OpenPSID itself.

**Co-op (experimental).** Play the campaign together on two Vitas, by
system link, online or ad hoc. The host starts it from the Campaign menu:
Campaign, a profile, a level and a difficulty as for single player, then on
the difficulty screen **Y** (Play co-op) instead of **A** (which plays alone,
as ever). The game's lobby opens as the waiting screen: "Waiting for your
partner", with the level, the difficulty, this Vita's name and how the game
is reached (system link, online and its code, or ad hoc); **B** cancels and
goes back to the difficulty screen. The partner opens the System Link screen
(the settings panel's Join a game on the Multiplayer tab, or Multiplayer,
System Link), where the game is listed as "<host>: <level> (<difficulty>)"
(with the level and "Co-op" and the difficulty beside it), and joins it with
**A**. Once the partner is in, the level starts after a few seconds (the
host's **A** sooner). The old way still works: the Multiplayer tab's Co-op
campaign and Co-op difficulty make any game the host creates in System Link
that level. The Xbox game had co-op only in split screen; this is upstream
halo-ce-universal's network co-op (credited in the main README):

- Two players. The host's Vita runs the level's scripts and its AI for both,
  and a Vita has no time to spare for more, so a co-op game takes two and
  upstream's extra enemies are off.
- Cutscenes are skipped by vote: press **Start** in one, and it is skipped
  once both Vitas have (the screen shows the count).
- A loading zone into a part of the level the team has not been in brings
  the other player along. Going back to a part already visited needs the
  team there (both players at the loading zone, or near it); one held back
  is told so.
- A dead player watches the other and comes back beside them once it is
  safe. With both dead, both come back where they were at the last
  checkpoint (the level is not reverted); checkpoints show on both Vitas.
- A level won ends the round, and the lobby's next game is the campaign's
  next level (after The Maw, The Pillar of Autumn). Started from the
  Campaign menu, the lobby opens on it with no map to pick, and it starts
  after ten seconds (the host's **A** sooner; **B** ends the co-op game).
- Co-op leaves single player's save alone: a network game neither resumes
  nor writes it, and Save and Quit in co-op only leaves the game. Levels
  finished in co-op count as finished in the profile, as in the Xbox's
  split screen co-op.
- Every Vita needs this version (network version 17): a 1.1.0 build tells
  the player to update.

**Ad hoc.** Set Connection to Ad hoc on every Vita and restart. Choose the
same Ad hoc room on each, then Host a game or Join a game: the system's
dialog joins (or makes) the room's group first, then the System Link screen
opens (Join ad hoc group does the first part alone). When the line under
the page says another machine is in the group, one Vita creates a System
Link game and the others find it in their lists. Ad hoc carries the game the way online play does,
over the group instead of the internet; nothing goes to the internet.

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
in the background on the device and kept in `shaders/`. The VPK gets a copy
of the pack stamped with the build's generator id (made again when that id
changes; the build notes it when the pack in the tree was made for another
generator: `vita_shader_pack.py --check` tells how many collected sources it
still has). After an update, another build's `shaders/` is renamed to
`shaders.old-<n>` at start-up and removed by a low-priority thread once the
game is up, while it reads no files (`gxm: old shader cache clean-up` in the
log; an interrupted one goes on at the next start).

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
| `HALO_NET_PROFILE=1` | the networked frame's steps and the socket queries, timed |
| `HALO_NET_TUNNEL_PORT=<port>` | online play's UDP port (to forward it on the router) |
| `HALO_NET_BROKERS`, `HALO_NET_STUN` | the signalling brokers and STUN servers online play uses (`host:port`, commas between) |
| `HALO_ADHOC_GROUP=NAME` | the ad hoc group's name (up to 8 letters) in place of the room's |
| `HALO_ADHOC_ID_TYPE=0` | the ad hoc libraries' product ID type (default 1, reserved, as vitaQuake) |
| `HALO_ADHOC_PROBE=1` | logs what the Vita's ad hoc libraries do without joining a group |
| `HALO_SHADER_ASYNC=0` | a shader that is neither shipped nor cached is compiled while the game waits (the default compiles it in the background and skips its draws until it is ready) |
| `HALO_SHADER_PACK=0` | ignore the shipped shaders (`app0:shaders.pak`) |
| `HALO_SHADER_SWEEP=0` | leave another build's old shader cache folders (`shaders.old-<n>`) on the memory card |
| `HALO_NET_PROFILE=1`, `HALO_NET_TRACE=1` | where a network game's frame goes; what its sockets do |
| `HALO_NET_CATCH_UP_TICKS=n` | the most ticks a frame of a System Link or online game runs to catch up with real time (default 2; 30 = beta.1's pacing) |
| `HALO_TICK_OVERLAP=1`, `=0` | when a tick takes longer than 33 ms and longer than the frame's render, a frame runs one tick instead of two while that keeps at least four fifths of the game's speed (frames come up to twice as often); unset: on in a multiplayer game nobody else plays in (a local game, or a System Link host no other machine joined), off in the campaign and in games with other machines; `tick pacing:` lines in `halo.log` |
| `HALO_TICK_CATCH_UP=0` | one tick a frame at most in a local game and, now, for a System Link host no other machine joined: the game plays slower instead of the frame rate halving |
| `HALO_DECAL_CACHE_WINDOW=n` | a new decal takes the least recently drawn room among the n decals after the last one made instead of searching all 2048 (default 64; 0 = the whole cache, as the Xbox) |
| `HALO_DECAL_MIN_PIXELS=n` | decals that would be under n pixels across from every local view are not made (default 0: off) |
| `HALO_AI_PERCEPTION_LOD=n` | actors farther than n world units from every player refresh their props' status (line of sight) every other tick; player props as before (default 0: off) |
| `HALO_TIME_CODE=1` | draw the tick count that some cutscene scripts turn on (`time_code_show`) in the top-left corner, as the beta did |
| `HALO_NET_COOP_LEAD_TICKS=n` | a co-op client runs no tick while it is more than n ticks ahead of its host's latest, so it plays at a slow host's pace (default 6; 0 = never) |
| `HALO_NET_SYNC_TRACE=1` | each correction (what made it, the object, how far off) and every 30 s the bytes sent of each message type, in `debug.txt` ("net sync:") |

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
