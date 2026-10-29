# Linux

`ninja linux` compiles the game with clang for 32-bit x86 Linux. The result
is a native executable, `build/linux/halo`. The game shows its graphics with
OpenGL 4.5. It plays sound through SDL3. It accepts keyboard, mouse and
gamepad input.

The game is 32-bit code because its data (tags, cache files, saved games)
contains 32-bit pointers, as on the Xbox.

## Requirements

You do not need the Xbox SDK. The declarations that the game uses are in
`port/include/xdk`.

To build:

- Python and ninja.
- clang. The option `--linux-cc` of `configure.py` selects a different
  compiler.
- The 32-bit glibc development files: `lib32-glibc` on Arch Linux,
  `gcc-multilib` and `libc6-dev-i386` on Debian and Ubuntu.
- The 32-bit SDL3: `lib32-sdl3` on Arch Linux, `libsdl3-dev:i386` on Debian
  and Ubuntu.

To start the game:

- The 32-bit OpenGL libraries (`lib32-mesa`).
- The 32-bit PipeWire or PulseAudio client libraries (`lib32-pipewire` or
  `lib32-libpulse`).

## Build the game

1. Go to the root folder of the repository.
2. Enter `python configure.py`.
3. Enter `ninja linux`.

## Start the game

Enter `build/linux/halo`.

The game data is the folder that contains `maps/`, from an Xbox disc image
of any version of the game. The game looks for this folder in this
sequence:

1. `paths.data` in `config.toml`.
2. The current folder.
3. The folder of the executable.
4. `assets/` in the current folder, and `assets/` in the repository that
   contains the executable.

### Halo: The Master Chief Collection

You do not need an install tool for MCC. At each start the game finds the
Halo content folder: the folder of your own Halo: The Master Chief
Collection that contains `halo1`. The game only reads this folder. It
does not copy or change MCC's files. The game looks in this sequence and
uses the first folder that it finds:

1. `paths.content` in `config.toml` (`HALO_CONTENT_ROOT`). `"auto"` (the
   default) or empty: continue with step 2. `"none"`: no MCC. A folder:
   MCC's folder, its `halo1` or `halo1/maps` folder, or a Steam library
   that contains MCC. If the folder is gone, or it contains no Halo CE map
   of MCC, the log tells you and the game continues with step 2.
2. `mcc/` in the data root.
3. Steam's libraries. On Linux, in `~/.local/share/Steam`, `~/.steam/steam`,
   `~/.steam/root`, `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`
   (Flatpak) and `~/snap/steam/common/.local/share/Steam` (Snap): each
   library that `steamapps/libraryfolders.vdf` (or
   `config/libraryfolders.vdf`) lists, then the Steam folder itself. On
   Windows: the Steam folder of the registry (`HKCU\Software\Valve\Steam`
   `SteamPath`, `HKLM\SOFTWARE\WOW6432Node\Valve\Steam` and
   `HKLM\SOFTWARE\Valve\Steam` `InstallPath`), `Steam` in Program Files,
   `SteamLibrary`, `Steam` and `Games\Steam` on each fixed drive, then the
   uninstall entry `Steam App 976730` (`InstallLocation`) and the Xbox
   app's `<drive>:\XboxGames\Halo The Master Chief Collection\Content`.
   In each library: `steamapps/common/Halo The Master Chief Collection`.

A folder is MCC's only if `halo1/maps` contains a Halo CE stock map
(`bloodgulch.map`, `a10.map` and the others) with MCC's cache header
(`head`, version 13). The log (`content:` lines) shows each library that
the game examined and the folder that it found.

If the game finds MCC but no `maps/` folder, it does not ask for a disc
image. It uses the folder of the executable as the data root, and makes an
empty `maps/` folder there. The menus then come from MCC's `ui.map`, and
the Halo PC files from MCC's `halo1/maps/custom_edition`.

If the game finds no MCC (with `"auto"`), the log tells you, and the main
menu shows "Halo MCC not found" (the Halo 3 menu: below the title; the
original menu: in the corner, in turn with the version).

The Xbox `maps/` folder and MCC can be used together. Priority, from first
to last: the settings in `config.toml` (`paths.data`, `paths.content`,
`paths.menu_source`), then the files of the Xbox `maps/` folder (`ui.map`
for the menus, the Halo PC files), then the files of MCC. MCC's levels are
always added to the lists. Refer to `docs/mcc_cea_maps.md`.

### No game data

If the game finds no data and no MCC, it asks for an Xbox disc image
(`.xiso` or `.iso`). This occurs at the first start:

- Select "No" to stop the game.
- Select "Yes" to open a file picker. Select the disc image. The game copies
  `maps/` and `default.xbe` next to the executable and shows the progress.

The loading screen takes its picture and its hum from the disc's
`default.xbe` (the retail Xbox game has them in the executable, not in
`maps/`). Put the file in the data root, next to `maps/`, or in the folder
above it. Without it, the loading screen stays dark and silent.

The game writes the copy to `maps.partial`. When the copy is complete, the
game changes the name to `maps`. If the copy stops before it is complete,
the game asks for the disc image again at the next start.

## Files and folders

| Xbox drive | Folder |
| --- | --- |
| `d:\` | The data root: the folder that contains `maps/`. |
| `z:\` | `z/` in the save root. This folder contains the cache (approximately 800 MB of map data) and the saved games. |
| `u:\` | `u/` in the save root. This folder contains the user data. |

The save root is `paths.saves` in `config.toml`. If that setting is empty,
the save root is `$XDG_DATA_HOME/halo-linux` (usually
`~/.local/share/halo-linux`).

The game makes the folders when it needs them. Names of files and folders
are not case-sensitive, as on the Xbox.

These files are in the data root:

| File | Contents |
| --- | --- |
| `debug.txt` | The log of the game. At start-up, the game shows the data root in the terminal. |
| `init.txt` | Console commands that the game does at start-up. For example, `map_name levels\a10\a10` starts the first campaign level. |

The settings are in `config.toml` next to the executable. Refer to
"Settings".

If the game stops because of a fatal signal, it writes the address and a
backtrace to the standard error. To find the function at the address, enter
`addr2line -e build/linux/halo <address>`.

## Controls

The keyboard and the mouse operate controller 1. The game adds the input of
the first gamepad to controller 1. The other gamepads operate controllers 2
to 4.

In play the keyboard and the mouse operate the player's actions
(`src/key_bindings.c`, `include/halo_key_bindings.h`), each bound to keys,
mouse buttons or the wheel in `config.toml`'s `[controls.keyboard]` (up to
four each, names as SDL gives them: `"Left Ctrl"`, `"Mouse Right"`,
`"Wheel Up"`). The defaults are Halo MCC's PC Halo: CE layout. An action
that is one of the game's controls presses the controller button that the
profile's button layout puts that control on, so a button layout never
moves a key.

| Action (`config.toml` key) | Default | Controller (standard layout) |
| --- | --- | --- |
| `move_forward`, `move_backward`, `strafe_left`, `strafe_right` | W, S, A, D | left stick |
| (aim) | mouse | (direct aim) |
| `fire` | left mouse button | right trigger |
| `zoom` | right mouse button, Z | right stick click |
| `throw_grenade` | G | left trigger |
| `melee` | F, mouse button 4 | B |
| `action` | E | X (never reloads when there is nothing to act on) |
| `reload` | R | (reload alone) |
| `switch_weapon` | Tab, mouse wheel | Y |
| `switch_grenade` | H, middle mouse button | black |
| `flashlight` | Q | white |
| `jump` | space | A |
| `crouch` | left ctrl, C | left stick click (also while walking) |
| `scoreboard` | backspace | back |
| (pause) | escape | start |
| (fixed) | arrow keys, F1, enter | D-pad, back, A (unless bound) |
| \` | | open the developer console |
| F12 | | release or capture the mouse |
| F11 | | change between fullscreen and window |

The menus' **KEYBOARD & MOUSE** page (`game/controls_menu.c`; Halo 3
menus: CUSTOMIZATION, a profile; Halo CE menus: SETTINGS, a profile;
ElDewrito menus: CUSTOMIZATION's CONTROLS tab) lists the actions with their
first and second keys, and the mouse's `input.mouse_sensitivity`,
`input.invert_mouse` and `input.raw_mouse`. A (or a click) asks for the
key: the next key or mouse button is bound (Escape cancels, Delete clears);
a key that another action has is swapped with it, and the page says what
the other action has now. X clears the chosen key, Y (or RESET TO MCC
DEFAULTS) puts every key back. Each change is written to `config.toml` at
once. A `config.toml` of an earlier version is migrated: an action at an
earlier version's default (the fixed layout up to 1.1.1-insider.24) gets
the MCC default; an action you set yourself is kept, and a default gives
up a key that you bound to another action.

One movement of the mouse wheel changes the weapon one time. A second
movement after a short pause changes it again.

In the menus, the mouse moves a pointer:

- The item below the pointer gets the focus.
- A left click selects the item. On a setting with values, a click on the
  left or right half changes the value. On a button in the key of a screen
  (for example "B = Back"), a click pushes that button.
- A right click goes back.
- The mouse wheel moves through the items.

The keyboard also operates the menus, with fixed keys: the arrow keys (or
W A S D) move, Enter accepts, Escape (or Backspace, F) goes back, space is
the controller's start, E is X, Tab Y, Q white, X black, G the left
trigger, F1 back. In
play, Escape opens the pause menu; Escape in the pause menu closes it.
When the game continues, the mouse aims again. A mouse button that you hold from the menu does not fire until
you push it again.

The prompts and the menus show the keys and the mouse buttons bound (the
first with a keycap) while you play with the keyboard and the mouse, and a gamepad's buttons
once you use a gamepad (`display.button_icons` in "Settings").

### Typing

When a menu asks for text (a new profile's name, a password, a game's code,
a lobby's name, a friend's code, the party chat, a map variant's name), a
text box opens over the game (`src/pc_text_entry.c`, drawn by
`game/pc_text_entry_overlay.c`). While it is open, the game gets no keys,
no clicks and no gamepad buttons.

| Keyboard | |
| --- | --- |
| letters, digits, punctuation | type |
| Backspace, Delete | erase before or after the caret |
| Left, Right, Home, End | move the caret |
| Ctrl+V, Shift+Insert | paste |
| Enter | accept |
| Escape | cancel |

With a gamepad, the box shows an on-screen keyboard (it shows when the
gamepad was the last device you used, or when you push a gamepad button):

| Gamepad | |
| --- | --- |
| D-pad, left stick | choose a key (hold to repeat) |
| A | push the key (ABC: capitals, #+=: symbols, SPACE, DELETE, DONE) |
| B | erase |
| X | space |
| left / right shoulder | capitals / symbols |
| left / right trigger | move the caret |
| Start | accept |
| Y | cancel |

The text keeps only the characters that the game's text can show
(printable ASCII). The game drops other characters (letters with accents,
emoji). The box shows how many characters you typed and how many the field
takes; a password shows as stars.

## Settings

The settings are in `config.toml` next to the executable
(`build/linux/config.toml`). At the first start, the game writes the file
with the default values and a comment for each setting. To get the default
values again, delete the file.

The game reads the file one time, at start-up. If a key is not correct, or
a value has the wrong type, the game writes the line to the log and uses the
default value.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.fullscreen` | `true` | `HALO_FULLSCREEN` | `true`: fullscreen at the resolution of the display. The picture has 480 lines of the game and the width of the display. `false`: a window with the 640x480 picture of the Xbox. F11 changes between the two. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | The size of the window, as a multiple of 640x480. You can change the size of the window. |
| `display.vsync` | `true` | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Refer to "Frame rate". |
| `display.first_person_legs` | `false` | `HALO_FIRST_PERSON_LEGS` | `true`: in first person, your own body is drawn from the waist down, so you see your legs when you look down, moving as everyone else sees them. Not in a vehicle, zoomed, in a cutscene or in third person. Only the picture changes: hit boxes, the camera and the network are as before. From OpenCE. |
| `display.ragdolls` | `true` | `HALO_RAGDOLLS` | `true`: a character killed falls limp, thrown by the blow that killed it, and comes to rest where it lands, instead of its death animation's last pose laid on the ground. Only the picture changes: the body's place, hit boxes, saved games and the network are as before, and each machine simulates its own. Four fall at once at most (`HALO_RAGDOLLS_ACTIVE`), twelve are kept. The idea is from jojoxdk's Halo 1 MCC Box3D Physics Mod. |
| `display.button_icons` | `"auto"` | `HALO_BUTTON_ICONS` | The button icons of the prompts, the menus' hints and the help lines (the HUD's "Hold E to pick up", the Halo 3 menus' strips, the Halo CE menus' keys, the Forge menu's controls). `"auto"`: the keyboard and mouse's keys and buttons while you play with them, a gamepad's buttons once you use one (a PlayStation pad's, by SDL's gamepad type, the PlayStation's; any other the Xbox's), and back to the keyboard's at the next key or mouse use, as Halo PC did; the keyboard's before any input, unless a gamepad is connected at the start. `"keyboard"`, `"xbox"` or `"playstation"`: always those. The variable is read each time (a test's `@set HALO_BUTTON_ICONS ...` is live). The keys are the ones in "Controls" (in the menus, where the mouse moves the pointer: Enter, Escape, G, and space for start; the right trigger has no key there and keeps the Xbox's icon). Our own art: `tools/halo3_menu_art.py` draws the keycaps (Saira Semi Condensed, SIL OFL), the mouse and the Xbox buttons into `menus/halo3/key_atlas.png` and `xbox_atlas.png`; without them, words ("Space", "left click"). Not on the Vita, which keeps its own Button icons. |
| `graphics.particles` | `"full"` | `HALO_PARTICLE_BUDGET` | The particles that the effects of a fight make. `"full"`: all, as on the Xbox. `"reduced"` or `"low"`: fewer sparks, smoke, dust and debris, and they go sooner (in multiplayer, out of view, after a sixth of a second; `"low"` a tenth). Muzzle flashes, flashes, your own weapon and the effects of scripts and cutscenes are not changed. |
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `input.mouse_sensitivity` | `1.0` | `HALO_MOUSE_SENSITIVITY` | The multiplier for the mouse aim. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.raw_mouse` | `true` | `HALO_MOUSE_RAW` | `true`: the mouse's own movement aims (raw input). `false`: with the system's pointer speed and acceleration (SDL's `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE`). |
| `controls.keyboard.<action>` | MCC's (see "Controls") | `HALO_KEY_<ACTION>` | The keys of each action, separated by commas ("Controls"). The KEYBOARD & MOUSE page writes them. |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `paths.content` | `"auto"` | `HALO_CONTENT_ROOT` | The Halo MCC folder. `"auto"`: the game finds it. `"none"`: no MCC. A folder: that folder. Refer to "Halo: The Master Chief Collection". |
| `network.netcode` | `"distributed"` | `HALO_NETCODE` | Each machine moves its own player at once, and the host makes the decisions (refer to `NETCODE.md`). The only netcode network version 9 plays; kept so older settings files load. |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.chat` | `true` | `HALO_CHAT` | The in-game chat (`port/linux/game/game_chat.c`): lines to everyone, the team or the party, from the quick chat wheel (Back + D-pad left in play; F1 + Left on the keyboard: up and down a phrase or Type a message, left and right or the shoulders the channel, A sends, Y types, B closes) or typed. OpenCE's text chat on the wire: its players and the Vitas read each other's lines. `false`: no chat over the game, none sent. |
| `network.chat_filter` | `true` | `HALO_CHAT_FILTER` | Slurs and hate speech in other players' chat lines (PC players' too) shown as `****`, however they are spelled; a player it catches three times in five minutes muted here. Changes only what this player sees. The words: `port/vita/app0/chat/filter.txt`. |
| `network.join_from_clipboard` | `true` | `HALO_NET_JOIN_FROM_CLIPBOARD` | `true`: when the game comes to the front, it joins the game of an invite link on the clipboard. |
| `network.tunnel_port` | `0` | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. `0`: the game selects a port. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.signalling_brokers` | three public brokers | `HALO_NET_BROKERS` | The public MQTT brokers (`host:port`, with commas between them) that let the machines of an invite find each other. A broker whose plain port fails is tried at once over MQTT over TLS on 8883 (`host:port/tlsport` names another TLS port, `/0` none). |
| `network.brokers_ca_file` | `""` | `HALO_NET_BROKERS_CA_FILE` | A PEM file of more certificate authorities for the brokers' TLS (the automated tests' own brokers). Empty: only the authorities built in (`port/linux/src/broker_roots.h`). |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a game that this machine hosts for internet play is public, listed in the server browser of everyone. `false`: it is private, and only the players with its code or invite link can join it. Server Setup's visibility writes its choice here. |
| `network.coop_public` | `false` | `HALO_NET_COOP_PUBLIC` | The same for an online co-op game, which is private unless you select public: `network.host_public` does not change it. X on the waiting screen of co-op hosted from the campaign's menus writes its choice here. A private co-op game is joined by its code. |
| `network.coop_players` | `16` (Vita: `2`; `3` and `4` experimental) | `HALO_NET_COOP_PLAYERS` | The most players that a co-op game this machine hosts takes, from `2`. The host runs the AI and the scripts of the campaign for all the players. |
| `network.coop_enemies_mode` | `"per_player"` | `HALO_NET_COOP_ENEMIES_MODE` | Online co-op's extra enemies: `"none"`; `"per_player"`, each squad of enemies that a level places grows by `network.coop_enemies` for each player past the first; or `"multiplier"`, each squad is `network.coop_enemies_multiplier` times as large, for any number of players. The extra enemies stand around the squad's places, and those that a dropship has no seats for drop out of it after its passengers. EXTRA ENEMIES in co-op's Server Setup writes its choice here. |
| `network.coop_enemies` | `50` | `HALO_NET_COOP_ENEMIES` | The extra enemies per player, a percentage from `25` to `200`: for each player past the first, each squad of enemies gets this much of itself more (`100`: as many again, so four players meet four times the squad), up to 8 times the squad however many players there are. PER PLAYER in co-op's Server Setup writes its choice here. |
| `network.coop_enemies_multiplier` | `2` | `HALO_NET_COOP_ENEMIES_MULTIPLIER` | The static multiplier of the enemies, `2` to `32`: each squad of enemies is this many times as large. MULTIPLIER in co-op's Server Setup writes its choice here. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
| `discord.application_id` | the application of the project | `HALO_DISCORD_APPLICATION` | The Discord application for invites. Empty: no Discord. |
| `menus.style` | `"halo3"` | `HALO_MENU_STYLE` | The menus: `"halo3"`, the Halo 3-style screens (the main menu, the campaign lobby and its pickers, the multiplayer lobby, its server browser, Create Game, code and password screens, the pause screen, the settings panel on the Vita) over the same animated Halo ring (`port/vita/app0/menus/halo3`, which need the Halo PC files as OpenCE's screens do). Read each time a screen opens. |
| `menus.scenes` | `true` | `HALO_MENU_SCENES` | With the Halo 3 menus, a camera of its own behind each lobby (the campaign, multiplayer and Forge lobbies, their `scene=` in `port/vita/app0/menus/halo3`: one of ui.map's camera points, our numbers written into it), the main menu the ring's own flight; `0` or `false` keeps the ring's flight behind every screen. |
| `menus.player_models` | `false` | `HALO_PLAYER_MODELS` | Player models from your Halo Custom Edition maps (CUSTOMIZATION's MODEL, `port/linux/game/player_models.c`). Off in 1.1.1 until Halo 2's models come: every player is the Spartan, no MODEL rows, the maps folder not looked through for models, a saved choice in `t:\player_models.txt` ignored (kept); `1` or `true` turns them on (testing). |
| `update.auto` | `true` | `HALO_UPDATE_AUTO` | `true`: at start-up, the game looks for a new version. Refer to "Updates". `false`: the game does not look. |
| `debug.update_answer` | `""` | `HALO_UPDATE_ANSWER` | The answer to the update question, for automatic tests: `yes`, `no` or `never`. Empty: the game asks. |
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds. `0`: never. |
| (none) | | `HALO_MENUS_DUMP` | A screen's tag path (`ui\shell\main_menu\main_menu`): its widget tree as ui.map has it is logged when it opens (types, places, pictures, text, data inputs, what each button runs and opens, children). What a screen of the menus' XML standing for it must do. |
| (none, not in release builds) | | `HALO_TEST_BROWSER_GAMES` | `N`: N made-up games after the server browser's own (not joinable), for the browser's tests. |
| (none, not in release builds) | | `HALO_TEST_BROWSER_PC` | `1`: with `HALO_TEST_BROWSER_GAMES`, every second made-up game a PC game and every third, from the third, with a password (both platforms' badges, the password screen). |
| (none, not in release builds) | | `HALO_TEST_TAPS` | `ms:x,y ...`: taps (the pointer there and a click, as the Vita's touch screen's) at those places of the menus' 640x480, that many milliseconds after the menus first ask. |
| (none, not in release builds) | | `HALO_TEST_BUTTON_GLYPHS` | `g,g,...`: with the PlayStation's or the keyboard's icons (`display.button_icons`), the glyphs of the game's buttons 0, 1, 2 ... in place of the layout's, by number (`VITA_GLYPH_*` of `port/vita/include/vita_controls.h`, or `HALO_GLYPH_KEY_FIRST` + a `halo_key` of `port/linux/include/halo_button_icons.h`: `64` is SPACE's keycap), so a screen's few icons can show the others (L, START, the D-pad, the wider keycaps ...). |
| (none, not in release builds) | | `HALO_TEST_GAMEPAD_TYPE` | `playstation`: the scripted pad's presses (`HALO_TEST_PAD`, `@aim pad`) are a PlayStation pad's for `display.button_icons` `"auto"`; else an Xbox pad's. A `kb` step of `HALO_TEST_PAD` is a key pressed. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.texture_log`, `debug.gl_debug`, `debug.texture_no_cache` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_TEXTURE_LOG`, `HALO_GL_DEBUG`, `HALO_TEXTURE_NO_CACHE` | Tools to find problems in the graphics: counts for each frame, all the GL state of one frame, the GLSL code, the textures. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with a GLSL expression (for example `t0.rgb`). |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_shoot`, `debug.network_test_vehicle`, `debug.network_test_pickup`, `debug.test_input` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_PICKUP`, `HALO_TEST_INPUT` | Automatic tests of system link (`game/network_test.c`). Refer to `NETCODE.md`. |
| `debug.network_latency`, `debug.network_loss`, `debug.network_corrupt`, `debug.network_corrupt_stream`, `debug.network_corrupt_after` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | The game holds all the data that it receives for this number of milliseconds, ignores this percentage of the datagrams, and damages this percentage of the datagrams it receives, and this percentage of its reads of streams, at random (bytes changed, cut short, stretched or replaced), from this many seconds after the start. Use the first two to test the netcode as on the internet, and the others to test that nothing another machine sends can crash the game (a damaged stream is closed, so a little goes a long way; a host's messages to its own client are damaged too, so start damaging once the game has started). |

With Mesa drivers, the game sends its GL calls through the GL thread of
Mesa. To stop this, set the environment variable `mesa_glthread=false`.

## Updates

This project's releases (`github.com/BirchWoodGod/halo-ce-vita/releases`,
tagged `v<version>`) carry the PC build as `halo-ce-vita-<version>-pc-linux.tar.gz`
and `halo-ce-vita-<version>-pc-windows.zip` (made by `tools/package_pc.py`),
beside the Vita's `halo-ce-vita-<version>-vita.vpk`. To update, download the
file for your platform and unpack it over the old folder: `config.toml`,
the game data and the saves stay.

The PC build does not look for updates by itself (nothing asks the network
unless the player does) and never replaces itself: watch the releases page.
An update check run by the tests (`@update`) names the file for this
platform in `halo.log` (`update: ... is available ...:
halo-ce-vita-<version>-pc-linux.tar.gz from ...`).

The upstream port's self-updater (`src/updater.c`: `update.auto`, the
question "Do you want to update?") follows the upstream port's numbered
builds (`build-<number>` releases of `cybersecurity/halo-ce-universal`).
Only the upstream repository's CI gives a build its number
(`tools/ci_build.py`); this project's builds have none, so they never ask.

## Frame rate

The game calculates its world at 30 Hz, as on the Xbox. On the Xbox, the
game showed one frame for each calculation (tick). This port shows one frame
for each refresh of the display, for example at 60, 120 or 240 Hz.

Each frame shows the world between the last two ticks
(`game/render_interpolation.c`):

- After each tick, the game keeps the camera, the position of each part of
  each object, and the first-person weapon.
- Each frame mixes the last two ticks. The mix agrees with the time since
  the last tick.
- Rotations use quaternions. Positions and scales are linear.
- A teleport, a respawn or a cut of the camera does not mix. It jumps.

Thus the frames are one tick (33 ms) after the calculation. The calculation
does not change.

To get 30 frames each second, set `display.interpolation = false`.

To see the frame rate:

1. Push \` to open the developer console.
2. Enter `display_framerate true`.

The frame rate shows at the bottom right of the screen. It is the mean over
half a second.

## System link

The Xbox game lets 16 players on 4 machines play a system link game. This
port lets up to 128 players on up to 128 machines play. Each machine can
have up to 4 players (split screen).

- `include/halo_port_limits.h` sets the limits.
- `include/halo_port_capacity.h` sets the memory for the limits. The game
  state is 16 MB at `0x81A00000` (3.3 MB on the Xbox). The pools of objects,
  effects, particles, contrails, lights and sounds are also larger.
- The byte-matching build keeps the limits of the Xbox. All the changes are
  in `#ifdef HALO_LINUX`.

Obey these rules:

- All the machines in a game must use a build with the same limits.
- The port uses protocol version 2. It does not see the Xbox game or older
  builds of the port. They do not see the port.

These are the differences from the Xbox:

- The host waits up to 60 seconds (15 seconds on the Xbox) for the other
  machines to load the map.
- If a machine does not read the messages of the host for two seconds, the
  host removes it from the game.
- The saved games contain all the game state. Thus a saved game is 16 MB.
  Saved games from older builds of the port do not operate.
- In campaign and in games of up to 16 players, the game removes garbage
  (bodies, dropped weapons) as on the Xbox. In larger games, it keeps more
  garbage, in proportion to the players.
- The lobby shows the local machine and the first three remote machines.
  The other machines are also in the game.
- In free-for-all games, each player is a team.

Linux, Windows and Android machines can play in the same game. With the
netcode `"lockstep"`, each machine must calculate the same floating-point
results. Thus all the ports:

- Compile without fused multiply-add (`-ffp-contract=off`).
- Use the math functions of musl (`port/include/halo_math.h`,
  `port/third_party/musl-math`), not the math functions of the system.

### Play on one computer

More than one copy of the game can play on one computer. Each copy must
have a different loopback address. A copy with an address gets no
broadcasts. Thus each copy must send its broadcasts to the other copies.

For a host and two clients, enter these commands in three terminals:

```sh
HALO_NET_ADDRESS=127.0.0.200 HALO_NET_BROADCAST=127.0.0.201,127.0.0.202 build/linux/halo
HALO_NET_ADDRESS=127.0.0.201 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
HALO_NET_ADDRESS=127.0.0.202 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
```

Do not give 127.0.0.1 to a copy. Each copy gets to its own address through
127.0.0.1. Linux and Windows send all of 127.0.0.0/8 to the loopback
interface.

### Test with many machines

`tools/system_link_bots.py` adds simple machines to a game. Each machine has
one player. The machines obey the system link protocol and send input, but
they do not calculate the game.

1. Start a game on the host.
2. Enter `python tools/system_link_bots.py --host 127.0.0.200 --machines 127 --start`.

Each machine uses its own loopback address, from 127.0.0.2. The option
`--start` starts the game when all the machines are in the lobby. If the
host has no `network.address`, do not give `--host`.

## Internet play

Machines with an invite link can play system link on the internet. This
project has no server.

When a copy of the game starts to host a system link game, it makes an
invite link: `halo://join/<44 hexadecimal digits>`. The game writes the link
to the standard error and puts it on the clipboard.

To join a game, do one of these steps:

- Open the link. The game is the handler of `halo://` links. If the game
  already operates, the new copy gives the link to it and stops.
- Copy the link (or the 44 digits) and go to the game.
- Enter `halo <link>`.
- Accept a Discord invite. Refer to "Discord".

When the machines connect, the game of the host shows in Multiplayer,
System Link. Join the game as on a local network. System link on a local
network does not need an invite.

### Security

Only machines with the invite can find the game:

- The link contains the identifier of the host and a random 16-byte token.
- The machines exchange their addresses through public MQTT brokers
  (`network.signalling_brokers`). The topics are HMACs of the token. A key
  from the token encrypts and authenticates the messages
  (`src/p2p_signal.c`, `src/p2p_crypto.c`).
- A key from the host encrypts and authenticates each packet between two
  machines.
- An invite operates while the copy of the game that made it operates.

### Connection

Each machine gets its public address from public STUN servers. Then the two
machines send packets to each other until the packets get through (UDP hole
punching). There is no relay.

Some networks give a different port for each destination (for example some
mobile and company networks). Two machines behind such networks cannot
connect. To connect, forward `network.tunnel_port` on the router of one of
the machines.

The game can ask the router to forward the port (UPnP,
`src/posix_upnp.c`, with `port/third_party/miniupnpc`):

- The host asks its router when a player uses its invite.
- A player that joins asks its router when it does not reach the host in
  5 seconds.
- The forwarded port is one more address that the machine gives to the
  other machine.
- The forward has a duration of one hour. The game makes it longer while
  it operates, and removes it when the game stops.
- UPnP does not help behind a second NAT, for example the NAT of a mobile
  network provider. Then the router has a private address, and the game
  does not ask.

To stop all UPnP requests, set `network.allow_upnp` to `false`.

In the game, each machine has an address in 100.64.0.0/10:

- `src/xnet.c` sends the traffic of the game to such an address through
  local sockets on 127.0.0.1 (or `network.address`).
- `src/p2p.c` sends that traffic through one UDP socket. UDP datagrams go
  as they are. TCP connections go as KCP streams (`port/third_party/kcp`).
- The broadcasts of the game go to all the machines. Thus the game of the
  host shows on the other machines.

### Discord

If the Discord desktop client operates, the game of the host shows in
Discord (through the application of `discord.application_id`). The activity
has a private party with the invite as its join secret. The host can send
the invite with the invite button of Discord. When a person accepts it, that
person joins the game. If the game does not operate, Discord starts it.

## What operates

| Area | Status |
| --- | --- |
| Game code | All 466 C files of the game. The changes are in "Game source changes". |
| Graphics | Direct3D 8 on OpenGL 4.5 core through SDL3 (`src/d3d8_gl.c`). The port translates the NV2A vertex shaders and register combiners to GLSL. It decodes all the Xbox texture formats. The vertex and index buffers come from a GL copy of the Xbox memory. |
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz, with volume, pitch, mix bins, distance, stereo pan, occlusion and obstruction. There is no Doppler effect, no cones and no reverb. |
| Input | XInput on SDL3 (`src/xinput_sdl.c`): keyboard, mouse, gamepads with rumble, and the debug keyboard for the console. |
| Files | The Win32 file functions and the MSVC file functions on POSIX, with the translation of Xbox paths. |
| Threads | Threads, events, mutexes, critical sections, interlocked operations and alertable waits. |
| Memory | The port reserves the Xbox memory at `0x80000000`. Thus the game gets the fixed addresses that it expects. |
| Saved games | The Xbox `UDATA` layout, with SHA-1 signatures. |
| Networking | Winsock on BSD sockets. System link on a local network and on the internet. |
| Bink video | Not available. The game skips the movies. |

## How the port operates

### The compiler

`tools/linux_build.py` compiles the game with clang and these options, which
give the ABI of the MSVC compiler:

- `--target=i686-linux-gnu`: 32-bit x86.
- `-fms-extensions`: the MSVC extensions.
- `-fshort-wchar`: 16-bit `wchar_t`.
- `-malign-double`: 8-byte alignment of 64-bit members.
- `-fcommon`: tentative definitions, as in C89.

glibc gives only ISO C (`__STRICT_ANSI__`). Thus POSIX names, for example
`random`, do not conflict with the names of the game.

These files supply the MSVC functions that clang does not have:

| File | Contents |
| --- | --- |
| `include/halo_linux_prefix.h` | The first header of each file: the architecture macros of the SDK, MSVC `__inline`, SEH keywords, `__declspec(selectany)`. |
| `include/` | Headers that add MSVC names to the C runtime headers. |
| `port/include/xdk` | The Xbox SDK declarations. The compiler reads this folder after all the other folders. |
| `tools/linux_msvc_semantics.py` | Makes a header that declares each struct tag at file scope, as MSVC does. It also makes the header inline functions weak, as the COMDAT functions of MSVC. `game/msvc_comdat.c` gives one external copy of each. |
| `include/halo_linux_winsock_names.h` | Gives new names to the Winsock functions of the SDK. Thus they do not link to the glibc functions with the same names. |
| `include/halo_linux_source_fixups.h` | Repairs one declaration conflict (`rasterizer_debug_drawing_begin`). |

`tools/linux_link_check.py` stops the link if a weak reference has no
definition. Without this check, the linker gives the reference the address
0.

### The platform layer (`src/`)

- The files `posix_*.c` use glibc. The compiler uses the ABI of the host
  for these files, because some glibc structures have a different layout
  with `-malign-double`.
- The other files include the SDK declarations through `platform.h`. Thus
  the compiler examines each definition against the SDK prototype.
- `src/halo_linker_common.c` gives weak storage for some globals of the
  January link, and for `fast_ftol_C` and `main_crash`.
- `main/d3d_intimacy.cpp` reads a private structure of the Xbox Direct3D.
  The Linux build does not use this file. `src/d3d8_gl.c` gives
  `d3d_find_flipcount`.
- The build returns small structures and unions in registers
  (`-freg-struct-return`), as on Win32.

### Game source changes

Five files of the game have changes for clang. These changes do not change
the MSVC objects: a comparison of all 612 C objects showed no difference in
code or data.

| File | Change |
| --- | --- |
| `cseries/cseries.c` | The naked function `stristr` uses `[ebp+8]` and `[ebp+12]` for its parameters. |
| `bitmaps/bitmap_drawing.c` | `*((word *)p)++` is now `*(*(word **)&p)++`. |
| `rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.c` | `&(T *)x` is now `(T **)&x`. |
| `hs/hs.c` | Local prototypes that did not agree with `ai_script.h` are removed. |
| `units/vehicles.c` | The local prototype of `unit_update_animation` uses the type of `units.h`. |

`math/real_math.h` had a copy of `plane2d_from_points` that did not agree
with the function in `effects/decals.c`. clang used the copy, and parts of
levels were not visible. The copy now agrees with the function.

Other changes are in `#ifdef HALO_LINUX`. All the native ports define
`HALO_LINUX`. The byte-matching build does not define it.

| File | Change |
| --- | --- |
| `scenario/scenario.c` | The BSP connection tables have names, not MSVC offsets. |
| `rasterizer/xbox/rasterizer_xbox_environment_fog.c` | A local pointer gets its value from the file-scope array with the same name. |
| `game/player_control.c` | The mouse aims the player on controller 1 directly. |
| `sound/game_sound.c` | The game calculates the obstruction of each sound one time for each tick, not for each frame. |
| `cseries/errors.c` | `debug.txt` stays open between lines. |
| `networking/`, `game/`, `interface/`, `bungie_net/network/` and the pools of objects, effects and sounds | The system link limits and the memory for them. |
| `game/`, `objects/`, `units/`, `networking/` | The distributed netcode. Refer to `NETCODE.md`. |
| `units/units.c`, `objects/damage.c`, `game/game_time.c`, `objects/objects.c` | A body that dies, and the blow that killed it, are told to `game/ragdolls.c`, which steps the dead bodies' ragdolls after each game tick and lets them go on a new map (`display.ragdolls`). The ragdolls' poses reach the render through `game/render_interpolation.c`'s copies of the tick's poses. |
| `render/render_objects.c`, `objects/objects.c` | In first person, your own body is drawn from the waist down with the pose `game/first_person_legs.c` makes (`display.first_person_legs`, from OpenCE); the waist it found and the mesh `game/object_mesh.c` read are forgotten on a new map. |

The x86 inline assembly of the game has C replacements in
`#ifdef HALO_LINUX`. Thus the compiler can optimize that code for each
processor:

| File | Assembly | Replacement |
| --- | --- | --- |
| `cseries/cseries.h` | x87 `fistp` (`fast_ftol`) | `__builtin_rint` |
| `bitmaps/bitmaps_inlines.h` | x87 conversions | C conversions |
| `math/matrix_math.c` | SSE `matrix4x3_multiply` | a C loop |
| `effects/decals.c` | an x87 conversion | a C conversion |
| `cseries/profile.c` | `rdtsc` | `QueryPerformanceCounter` |
| `cseries/cseries.c` | naked `stristr` | a C `stristr` |
| `cseries/stack_walk_windows.c` | a read of EBP | `__builtin_frame_address` |
| `interface/hud_draw.c` | a read of `[ebp+4]` | `__builtin_return_address(1)` |
| `bink/bink_playback.c` | `int 3` | `__builtin_trap` |

The x87 control and status words (`_control87`, `_statusfp`, `_clearfp` in
`src/msvc_crt.c`) use `fenv.h`. On Android, they use the FPCR and FPSR.
