# Dedicated server

A dedicated server hosts online games for Halo CE for PS Vita on a PC or a
Raspberry Pi. It runs the game's host (the simulation, the AI, the scores)
without a window, sound or player of its own. Vitas join it as they join a
Vita's game: by its code, from the server browser, or on its LAN. The game
goes on when players leave, and the host is not a Vita, so the players'
Vitas only run their own part of the game.

The server is the game compiled again as a host that can do nothing else
(`port/linux/game/dedicated_server.c`). It uses the netcode and the
messages of the Vitas' version: a server built from this tree plays with
Vitas of network version 18 (Halo CE for PS Vita 1.1.0).

## What it does

- Hosts one game at a time, with up to 16 players.
- Plays a map cycle (`sv_mapcycle_add`). A game ends at its score, at
  the time limit (`sv_timelimit`) or with `sv_end_game`. The scores are
  shown, then the next game of the cycle starts in the lobby.
- Starts a game when the first player is in (`sv_minplayers`), after a
  short countdown (`sv_start_delay`). Others join the game in progress.
- Ends a game that nobody is in after 30 seconds (`sv_end_empty`) and
  waits in its lobby. While nobody is connected, it uses almost no CPU.
- Is listed in the server browser (public by default) or is joined only by
  its code (`sv_public 0`), with or without a password.
- Takes commands from `init.txt` at the start, and from its console (the
  terminal it runs in).

What it does not do:

- PCs cannot join it. The server is on the Vitas' side of the "Vitas play
  only Vitas" line (see "Vitas only" below).
- It has no remote console (no rcon). Commands are typed on the machine
  that runs the server.
- It does not share maps unless you allow it (`sv_map_download`).
- Players in the lobby cannot see `sv_say` messages. They see them in a
  game.

## Build

The server is built from this repository with the same tools as the game.
No game data is needed to build it.

### On a PC (Linux, x86)

The game is 32-bit code (its data has 32-bit pointers), so the server is a
32-bit program. You need what `port/linux/README.md` lists to build the
Linux game: Python, ninja, clang, lld, the 32-bit glibc development files
and the 32-bit SDL3 (`lib32-sdl3` on Arch Linux, `libsdl3-dev:i386` on
Debian and Ubuntu). The server does not open a window or play sound, but it
uses SDL3 for its threads and clock.

```
python3 configure.py --portable --release --lto off --pgo off
ninja linux-server
```

The result is `build/linux/halo-server`. `--portable` makes code for any
x86-64 processor. Leave it out to build for the machine you build on.

The machine that runs the server needs the 32-bit glibc and SDL3 (the same
packages, without `-dev` where your distribution splits them).

### For a Raspberry Pi 4

The server for a Raspberry Pi is a 32-bit ARM (armhf) program. It runs on
Raspberry Pi OS 13 ("Trixie") or newer. On a 64-bit Raspberry Pi OS, add
the armhf libraries once:
`sudo dpkg --add-architecture armhf && sudo apt update && sudo apt install libc6:armhf libsdl3-0:armhf`.

`tools/linux_armhf_cc.sh` takes the place of clang in the build:

- **On the Pi itself** (a 32-bit Raspberry Pi OS with `clang`, `lld`,
  `ninja-build`, `python3` and `libsdl3-dev`):

  ```
  python3 configure.py --linux-cc tools/linux_armhf_cc.sh --portable --release --lto off --pgo off
  ninja linux-server
  ```

  The build takes a long time on a Pi. Building on a PC is faster.

- **From a PC:** get Arm's GNU toolchain for `arm-none-linux-gnueabihf`
  (13.3 or newer; its glibc must not be newer than the Pi's) and build SDL3
  with it as a static library (CMake: `-DSDL_STATIC=ON -DSDL_SHARED=OFF
  -DSDL_X11=OFF -DSDL_WAYLAND=OFF`, installed into a folder of its own).
  Then:

  ```
  export ARM_GNU_TOOLCHAIN=/path/to/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf
  export SDL3_ARMHF=/path/to/sdl3-armhf
  python3 configure.py --linux-cc tools/linux_armhf_cc.sh --portable --release --lto off --pgo off
  ninja linux-server
  ```

  Copy `build/linux/halo-server` to the Pi. SDL3 is inside it.

## The server's folder

The server keeps everything in one folder, its data root. Give it with
`-path`, or start the server in that folder.

| File | What it is |
| --- | --- |
| `maps/` | The game data: the `maps` folder of your own Xbox copy of the game (`bloodgulch.map`, `ui.map` and the rest). Custom maps go here too. Nothing of Bungie's is in this repository. |
| `init.txt` | The server's commands, run at the start. |
| `config.toml` | The game's settings, written with the defaults at the first start. The server sets most of what it needs with its commands. |
| `bans.txt` | The bans, one line each. The server reads it at every join. |
| `cheaters.txt` | Players the host dropped for cheating (speed hacks). |
| `debug.txt` | The game's log. |
| `saves/` | The game's cache and saves. It grows to about 800 MB (the Xbox's map cache). |

The server needs `ui.map` (the game starts in its main menu) and the
multiplayer maps in its cycle. Allow about 1 GB of disk for `saves/`
besides the maps.

## Start

```
halo-server [-path DIR] [-exec FILE] [-port N]
```

| Option | What it does |
| --- | --- |
| `-path DIR` | The server's folder (default: the working directory). |
| `-exec FILE` | The commands to run at the start (default: `init.txt` in the server's folder). |
| `-port N` | Internet play's UDP port, the one to forward (default 2302). |
| `-help` | Shows the options. |

The server writes its messages to the terminal: the code that Vitas join
with, the players who join and leave, and the games. Type `help` for the
commands. `quit`, Ctrl+C or `systemctl stop` stops the server and removes
its game from the server browser.

## init.txt

`init.txt` holds one command on each line, as Halo PC's dedicated server
(`haloceded`) takes them. Lines that start with `#`, `;` or `//` are
comments. A command that the server does not know is reported and
ignored. The server never runs a script, a program or a shell command from
this file or from its console.

```
# my server
sv_name "Vita Blood Gulch"
sv_maxplayers 12
sv_public 1
sv_password ""
sv_mapcycle_add bloodgulch ctf
sv_mapcycle_add chillout slayer
sv_mapcycle_add hangemhigh team_slayer
sv_timelimit 15
sv_scorelimit 0
sv_minplayers 1
sv_port 2302
```

### Commands

These commands work in `init.txt` and on the console. The names are
`haloceded`'s where the meaning is the same.

| Command | What it does |
| --- | --- |
| `sv_name <name>` | The name in the server browser (up to 32 characters; the Vitas' System Link list shows the first 16). |
| `sv_maxplayers <2-16>` | The most players. |
| `sv_password [password]` | A password for joining from the server browser. Without a password, there is none. The code always joins. |
| `sv_public <0\|1>` | 1 (the default): listed in the server browser. 0: joined only by its code. |
| `sv_mapcycle_add <map> <gametype>` | Adds a game to the cycle. The map is a file name in `maps/` without `.map`. |
| `sv_mapcycle_del <#>` | Takes a game out of the cycle. |
| `sv_mapcycle` | Lists the cycle. |
| `sv_mapcycle_clear` | Empties the cycle. An empty cycle plays Blood Gulch slayer. |
| `sv_mapcycle_begin` | Starts the cycle again from its first game. |
| `sv_map <map> <gametype>` | Plays that game next, at once. Then the cycle continues. |
| `sv_map_next` | Ends the game. The cycle's next game follows. |
| `sv_map_reset` | Ends the game and plays it again. |
| `sv_end_game` | Ends the game (its scores, then the next game). |
| `sv_timelimit <minutes>` | The longest a game lasts. 0 (the default): until its score. |
| `sv_scorelimit <score>` | The score that wins. 0 (the default): the gametype's. |
| `sv_minplayers <n>` | The players a game needs to start (default 1). |
| `sv_start_delay <seconds>` | The lobby's countdown once they are in (default 10). |
| `sv_postgame <seconds>` | How long the scores are shown (default 10). |
| `sv_end_empty <seconds>` | A game that nobody is in ends after this (default 30; 0: never). |
| `sv_coop <level> [difficulty]` | Co-op on a campaign level (`a10`, `a30`...; difficulty 0 to 3, default 1) instead of the cycle. `init.txt` only. |
| `sv_map_download <0\|1>` | 1: Vitas without a custom map of the cycle may download it from the server, in the lobby. 0 (the default): no downloads. |
| `sv_port <port>` | Internet play's UDP port (default 2302). `init.txt` only. |
| `sv_public_address <ip>[:port]` | The address the internet reaches the server at, if it cannot find it itself. `init.txt` only. |
| `sv_relay <host:port>` | A relay (`port/relay`) for players that cannot reach the server directly. `init.txt` only; up to 2. |
| `sv_players` | The players: their number, name, team, address and hardware id. |
| `sv_status` | The server's name, code, game and players. |
| `sv_kick <#\|name>` | Removes a player. They can join again. |
| `sv_ban <#\|name>` | Removes a player and bans their address and hardware id (a line in `bans.txt`). |
| `sv_ban_ip <a.b.c.d>` | Bans an address. |
| `sv_banlist` | Lists the bans, numbered. |
| `sv_unban <#>` | Removes a ban. |
| `sv_say <text>` | Shows a line, "Server: text", on every player's screen (in a game). |
| `quit` | Stops the server. |

The gametypes are the game's own: `slayer`, `team_slayer`, `ctf`,
`ironctf`, `king`, `team_king`, `oddball`, `team_oddball`, `race`,
`team_race`, `rally`, `elimination`, `stalker` and `accumulation`
(Halo PC's `ffa` and `koth` are taken too).

Halo CE (Custom Edition) maps need `game.custom_edition = true` in the
server's `config.toml` and the Halo PC resource maps (`bitmaps.map`,
`sounds.map`, `loc.map`) in `maps/`, as on the Vita.

## Run it on a PC

1. Make the server's folder and put the game's `maps` folder in it:

   ```
   mkdir -p ~/halo-server
   cp -r /path/to/your/xbox/maps ~/halo-server/maps
   ```

2. Write `~/halo-server/init.txt` (see above).
3. Start the server:

   ```
   build/linux/halo-server -path ~/halo-server
   ```

4. Forward UDP port 2302 on your router to the PC (see below), and let it
   through the PC's firewall.

The server shows its code, for example `hosting; Vitas join with the code
ABCD-EFGH`. Vitas join with that code (Join by code, or the settings
panel's Join with a code), or from the server browser if the server is
public. The code changes each time the server starts.

### As a service (systemd)

`port/linux/halo-server.service` runs the server as an unprivileged user,
with the protections systemd offers. The server's folder is
`/var/lib/halo-server` and the program is in `/opt/halo-server`:

```
sudo useradd --system --home-dir /var/lib/halo-server --shell /usr/sbin/nologin halo-server
sudo install -d -o halo-server -g halo-server -m 750 /var/lib/halo-server
sudo cp -r /path/to/your/xbox/maps /var/lib/halo-server/maps
sudo install -o halo-server -g halo-server -m 640 init.txt /var/lib/halo-server/init.txt
sudo install -D -m 755 build/linux/halo-server /opt/halo-server/halo-server
sudo install -m 644 port/linux/halo-server.service /etc/systemd/system/halo-server.service
sudo systemctl daemon-reload
sudo systemctl enable --now halo-server
journalctl -u halo-server -f
```

`journalctl` shows the code. A service has no console: change `init.txt`
and restart the service (`sudo systemctl restart halo-server`), or run the
server in a terminal multiplexer (`tmux`, `screen`) as the `halo-server`
user to type commands.

## Run it on a Raspberry Pi 4

The steps are the same as on a PC, with the Pi's build of the server.

- Use a Pi 4 with 2 GB of memory or more. The server uses about 100 MB of
  memory, and the game's map cache in `saves/` about 800 MB of storage.
  Keep the server's folder on the SD card or, better, a USB SSD.
- Connect the Pi by Ethernet, not Wi-Fi.
- The server with four players used under 2% of one core of an AMD Ryzen 9
  7950X. A Pi 4 core is about five times slower for this code: expect about
  10% of one core with four players, more with sixteen, and well under 2%
  while nobody is connected. (Measured on the PC only: see the commit that
  added the server.)

## Port forwarding

Vitas reach the server through internet play's UDP port, 2302 by default
(`-port`, `sv_port`). Forward that UDP port on your router to the machine
that runs the server, and allow it in that machine's firewall. No other
port is needed from the internet.

- The server and the players find each other through public MQTT brokers
  (`brokers.txt`, as the Vitas do). The server connects out to them; nothing
  comes in from them.
- If the router uses UPnP, the server can ask it for the forwarding itself
  (`network.allow_upnp` in `config.toml`, on by default) when a player
  connects. A fixed forwarding is more reliable.
- If the server cannot work out its public address (some double NAT), give
  it with `sv_public_address`.
- A relay (`sv_relay`, `port/relay`) is only for players whose network
  cannot reach the server directly. With the port forwarded, players
  connect directly.
- On a LAN, Vitas with Connection set to System Link find the server in
  their System Link list without the internet (the game's ports 5150 and
  5151 on the LAN).

## Vitas only

Vitas play only Vitas. The server is built on the Vitas' side of that line
(`HALO_NET_AS_VITA`), so:

- It advertises itself as a Vita's game (`HALO_PORT_ADVERTISED_VITA_FLAG`),
  on the Vitas' signalling topics (`hcev`), and its server browser listing
  is signed under the Vitas' label. PC builds of the game do not find its
  code, do not list it, and ignore it on a LAN.
- It accepts only machines that join with the Vitas' join token. A PC build
  that tries to join is refused (`run_netns_online_test.sh dedicatedpc`
  tests this with a PC build that ignores the line).
- It cannot be used to play: its own client joins only its own game, it
  ignores codes and invites, and it has no player. It cannot pass a PC off
  as a Vita.

Nothing changes for games that Vitas host.

The line is the same as between Vitas and PCs today: it keeps ordinary PC
builds out. The source code is public, so a modified build can present the
Vitas' token, as it can to a Vita's game. The server's other defences
(below) apply to every client.

## Security

The server is on the internet, so it is built to expect hostile traffic:

- **Nothing runs from outside.** The server takes commands only from
  `init.txt` and its own console (standard input). There is no remote
  console, no telnet console (compiled out), and no shell or script from
  any file or client.
- **No file names from clients.** A client never names a file. Map sharing
  sends only the map being played, by the server's own path, and only if
  `sv_map_download 1` and the map is in the cycle. Downloads happen only in
  the lobby, at a limited rate.
- **Limits per address:**
  - 6 connections to the game a minute from one address (by the player's
    real address, through internet play). More are refused.
  - 8 machines connected at once from one address (players behind one
    router share it).
  - 2000 packets a second from one player. The rest are dropped.
  - The signalling's limits (answers per broker and per second, players
    reaching the server at once) and the game's (2 connections from one
    address that have not joined, a join within 10 seconds, a player's hits
    paid for and checked) apply as for any host.
- **Host-authoritative game.** The server decides damage, deaths, scores
  and pickups. It checks the movements and hits that clients report, and
  drops a machine whose game runs faster than time (a speed hack): see
  `port/linux/NETCODE.md`.
- **Bans persist.** `sv_ban` and the speed-hack check add a line to
  `bans.txt` with the player's address (`ip=`) and hardware id (`hwid=`).
  The server reads the file at every join, also after a restart. Both are
  what the player's machine reports, and players behind one router share
  an address.
- **Encrypted transport.** Internet play's traffic is encrypted and
  authenticated per session (`port/linux/src/p2p.c`); the server browser's
  listings are signed.
- **Fuzzed.** `port/vita/tests/run_net_fuzz_test.sh` fuzzes the tunnel,
  the signalling, the game's message decoder and map sharing's messages,
  also built as the server's code (`NET_FUZZ_DEDICATED=1`), and
  `debug.network_corrupt` damages what a running server receives.

### Run it as an unprivileged user

Never run the server as root. The systemd unit
(`port/linux/halo-server.service`) runs it as the `halo-server` user, and
also:

- `NoNewPrivileges=yes`, no capabilities (`CapabilityBoundingSet=`).
- `ProtectSystem=strict` (the system read-only), `ReadWritePaths` only the
  server's folder, `ProtectHome=yes`, `PrivateTmp=yes`,
  `PrivateDevices=yes`.
- Kernel tunables, modules, logs, control groups and the clock protected.
- Network families limited to IPv4, UNIX and netlink sockets.
- `MemoryDenyWriteExecute=yes`, `LockPersonality=yes`,
  `RestrictNamespaces=yes`, `RestrictRealtime=yes`, `RestrictSUIDSGID=yes`.
- A system call filter (`@system-service`, without `@privileged`), and
  memory and task limits.

Without systemd, run the server as an ordinary user with no other
privileges, and give that user write access to the server's folder only.

## Testing

`port/vita/tests/run_netns_online_test.sh` runs the server and Linux builds
standing in for Vitas in network namespaces, with no internet:

- `dedicated`: the server behind a NAT with its port forwarded, three Vitas
  (by code, from the server browser, and one that leaves and joins again),
  two maps of the cycle, `sv_players` and `sv_say`.
- `dedicatedpc`: PC builds are refused (by code, browsing, on the LAN, and
  one that tries anyway).
- `dedicatedban`: `sv_ban`, a restart, and `sv_unban`.

```
python3 configure.py --linux-d3d gxm-null --linux-net-vita --lto off --pgo off
ninja linux linux-server
HALO_TEST_DATA=/path/to/data port/vita/tests/run_netns_online_test.sh dedicated
```

`HALO_TEST_SERVER_STATS=1` records the server's CPU time and memory every
5 seconds in `server/stats.log`.
