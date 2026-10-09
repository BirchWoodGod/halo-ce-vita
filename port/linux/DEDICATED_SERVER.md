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
  A Vita that joins while a game is ending, or while the scores are shown,
  waits and lands in the next game's lobby.
- Ends a game that nobody is in after 30 seconds (`sv_end_empty`) and
  waits in its lobby (the scores shown only 3 seconds, to nobody). While
  nobody is connected, it uses almost no CPU.
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

## Download a release build

Each release from 1.1.0-beta.2 has the server ready to run, on its
[releases page](https://github.com/BirchWoodGod/halo-ce-vita/releases), as
two downloads:

| Download | For |
| --- | --- |
| `halo-ce-vita-server-<version>-linux-x86.tar.gz` | A 64-bit PC or VPS: Debian 12 or newer, Ubuntu 22.04 or newer, or another distribution with glibc 2.36 or newer. A 32-bit x86 program. |
| `halo-ce-vita-server-<version>-linux-armhf.tar.gz` | A Raspberry Pi 4 (or another ARMv8 Pi: 3, 5) with Raspberry Pi OS 12 ("Bookworm") or 13 ("Trixie"), 32-bit or 64-bit. A 32-bit ARM program. |

The program needs only glibc: SDL3 is linked into it. On a 64-bit system,
add the 32-bit glibc once:

```
sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install libc6:i386     # PC, VPS
sudo dpkg --add-architecture armhf && sudo apt update && sudo apt install libc6:armhf   # 64-bit Raspberry Pi OS
```

Each download holds `halo-server`, `README-SERVER.md` (a quick start),
this guide, `init.txt.example`, `examples/` (Slayer, Big Team, Oddball and
King of the Hill servers on ports 2302 to 2305), `systemd/` (the units of
"As a service" and "Several servers on one VPS", and `halo-servers`), the
licences (`LICENSE`, the GPL 3, and `licenses/`, SDL3's and the other
libraries') and `SHA256SUMS`. No game data: copy the `maps` folder of your
own Xbox game into the server's folder ("The server's folder"). The
release's `SHA256SUMS` has the downloads' checksums:

```
sha256sum -c SHA256SUMS --ignore-missing
tar xzf halo-ce-vita-server-<version>-linux-x86.tar.gz
cd halo-ce-vita-server-<version>-linux-x86 && ./halo-server -help
```

Then follow "Run it on a PC" or "Run it on a Raspberry Pi 4" with that
`halo-server` in place of `build/linux/halo-server`.

The downloads are built by `.github/workflows/dedicated-server.yml` (on a
release's tag, or by hand) with `tools/package_server.py`, which builds the
same packages on any Linux PC with clang, lld, cmake and ninja: it compiles
the server against Debian 12's own glibc and libgcc (fetched, checked
against their pinned checksums), with SDL3 3.2 built as a static, headless
library, checks that the programs need no glibc symbol newer than 2.36 and
no library but glibc's, and starts each package's server in Debian 12's
userland (bubblewrap, or a `debian:bookworm` container; the ARM one under
`qemu-arm`):

```
python3 tools/package_server.py --version 1.1.0-beta.2 --fetch-qemu
```

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

The server for a Raspberry Pi is a 32-bit ARM (armhf) program. Built with
Arm's toolchain as below, it runs on Raspberry Pi OS 13 ("Trixie") or newer
(its glibc is 2.38); the release build ("Download a release build") runs on
12 ("Bookworm") too. On a 64-bit Raspberry Pi OS, add
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
| `saves/` | The game's cache and saves. The map cache's files say 750 MB (the Xbox's six cache slots), but only the maps copied into them take room: about 160 MB for a multiplayer cycle (the menu and three maps, decompressed), up to 550 MB more with co-op (two campaign levels). |

The server needs `ui.map` (the game starts in its main menu) and the
multiplayer maps in its cycle. Allow about 1 GB of disk for `saves/`
besides the maps. Several servers on one machine can share one map cache
instead (`sv_map_cache`, see "Several servers on one VPS").

## Start

```
halo-server [-path DIR] [-exec FILE] [-port N] [-gameport N] [-mapcache DIR]
```

| Option | What it does |
| --- | --- |
| `-path DIR` | The server's folder (default: the working directory). |
| `-exec FILE` | The commands to run at the start (default: `init.txt` in the server's folder). |
| `-port N` | Internet play's UDP port, the one to forward (default 2302). |
| `-gameport N` | The game's ports on this machine, N and N+1 (`sv_game_port`; default 5150 for port 2302, 5152 for 2303, and so on). Never forwarded. |
| `-mapcache DIR` | A map cache that several servers share (`sv_map_cache`). |
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
| `sv_postgame <seconds>` | How long the scores are shown (default 10; 3 when nobody is in the game). |
| `sv_end_empty <seconds>` | A game that nobody is in ends after this (default 30; 0: never). |
| `sv_coop <level> [difficulty]` | Co-op on a campaign level (`a10`, `a30`...; difficulty 0 to 3, default 1) instead of the cycle; the server runs the AI and the scripts. `init.txt` only. |
| `sv_map_download <0\|1>` | 1: Vitas without a custom map of the cycle may download it from the server, in the lobby. 0 (the default): no downloads. |
| `sv_port <port>` | Internet play's UDP port (default 2302). `init.txt` only. |
| `sv_game_port <port>` | The game's own ports on this machine: this one (the host's) and the next (its client's). The default is 5150 for `sv_port` 2302, 5152 for 2303, 5154 for 2304 and so on up to 2401 (5150 past that), so servers on 2302, 2303... never take each other's. Only a server on 5150 is found on its LAN (see "Several servers on one VPS"). Not 5149 or 5151. `init.txt` only. |
| `sv_map_cache <folder>` | A map cache folder that several servers share: each map is decompressed into it once, by the first server that plays it, and read by all (a path relative to the server's folder, or a full one). `init.txt` only. |
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

- Any Pi 4 has the memory: the server used 92-95 MB with four players in
  multiplayer and about 117 MB in co-op. The game's map cache in `saves/`
  takes about 800 MB of storage.
  Keep the server's folder on the SD card or, better, a USB SSD.
- Connect the Pi by Ethernet, not Wi-Fi.
- On an AMD Ryzen 9 7950X the server used 1.8% of one core with four
  players on Blood Gulch (shooting each other, too), 1.2% in co-op on The
  Truth and Reconciliation with one player, and 0.3% with nobody connected.
  A Pi 4 core is about four to six times slower for this code: expect about
  10% of one core with four players, more with sixteen, and 1 to 2% while
  nobody is connected. (Measured on the PC only.)

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
- The server offers the address and port the internet sees it at (learned
  from STUN). A router that keeps the port when it forwards (most do) needs
  nothing more. A router that changes the port of what goes out (a
  "symmetric" NAT) hides the forwarded port: give the server its address
  with `sv_public_address <your public IP>:2302`.
- A relay (`sv_relay`, `port/relay`) is only for players whose network
  cannot reach the server directly. With the port forwarded, players
  connect directly, even players behind a symmetric NAT (a mobile network):
  in the tests, Vitas behind symmetric NATs connected straight to a server
  whose port was forwarded; with the server's router symmetric as well, they
  went through the relay until `sv_public_address` named the forwarded
  port, and then connected directly.
- On a LAN, Vitas with Connection set to System Link find the server in
  their System Link list without the internet (the game's ports 5150 and
  5151 on the LAN). Only a server on those ports is found that way: see
  "Several servers on one VPS".

## Several servers on one VPS

One machine can run several servers, each with its own name, cycle and
players. Each needs its own folder and two things of its own:

- **Its internet play port** (`sv_port`): 2302, 2303, 2304... Forward and
  open each one (UDP), and nothing else.
- **Its game ports** (`sv_game_port`): the game's own ports, 5150 (the
  host's) and 5151 (its client's), which every copy of the game binds. A
  second server on 5150 cannot host ("network: could not host a game (the
  game's network ports in use?)"). Each server takes two others on its
  machine, by default from its `sv_port`: 5150 and 5151 for 2302, 5152 and
  5153 for 2303, 5154 and 5155 for 2304, 5156 and 5157 for 2305, so four
  servers on 2302 to 2305 need nothing more. `sv_game_port` sets them
  (`sv_game_port 5170`: 5170 and 5171). These ports never leave the
  machine: they are not forwarded.

The Vitas know nothing of this: they still send to 5150 and 5151. The
server swaps the numbers where its traffic meets its machine (its sockets)
and internet play's tunnel, so Vitas join a server on any ports by its code
and from the server browser, as before, and no network message changed
(network version 18).

On the servers' own LAN, Vitas with Connection set to System Link reach the
game's ports themselves: they find and join only the server on 5150 (the
one on 2302). The others do not answer there and do not advertise
themselves on the LAN (they would be joined at 5150, the first server):
players on that LAN join them by their code or from the server browser, as
from anywhere else. On a VPS nobody is on the LAN, and nothing changes.

### A shared map cache

The game plays an Xbox map from a decompressed copy in its cache (six
fixed slots in `saves/`; the files say 750 MB, but take what was copied:
about 160 MB for a multiplayer cycle). With `sv_map_cache <folder>`,
servers share one cache instead: each map is decompressed once, into a file
of its own in that folder (`bloodgulch-035e2da3-02a46800.map`: its name,
checksum and length), and every server reads that file. Their own caches
then stay empty (24 KB on disk each).

- A shared copy is written under a temporary name, synced, then renamed:
  a file with its name is always whole. The server writing it holds a lock
  (`<file>.lock`); a server that wants the same map meanwhile waits for it
  (and writes it itself if the first one stops). Copies are never changed
  once made.
- A copy is only read. The folder may be made read-only (or another user's)
  once it holds the cycles' maps: maps it lacks are then copied into each
  server's own cache, as without one.
- A map without a checksum (Invader writes none) cannot be told from
  another version of it and is not shared.
- Each map's copy is kept: a cycle of more than three multiplayer maps no
  longer decompresses each map again when it comes round (the slots are
  only three). The folder grows to the sum of the cycles' maps, about 40
  to 46 MB each, plus 32 MB for `ui.map`. Delete its files while no server
  runs to make room or after changing a map.

What it saves depends on how much the cycles share. Measured
(`run_netns_online_test.sh dedicatedmulti`): three servers playing Blood
Gulch and Chill Out took 155 MB of map cache each (465 MB), and 155 MB
together with `sv_map_cache`. Four servers whose cycles have 13 different
maps between them take about 170 MB each (670 MB) on their own and about
580 MB shared: little less, but no map is decompressed again at its turn.

### On a VPS, with systemd

`port/linux/halo-server@.service` runs one server per folder in
`/opt/halo/servers` (`halo-server@s1` for `/opt/halo/servers/s1`), all as
the user `halo`, with `/opt/halo/mapcache` as their shared map cache
(unless a server's `init.txt` names another) and the protections of
`halo-server.service`. The program is `/opt/halo/bin/halo-server`, and
`/opt/halo/lib` holds the 32-bit SDL3 (and the 32-bit libraries it needs)
on a system without them (a release build needs none: SDL3 is inside it).

Four servers:

```
sudo useradd --system --home-dir /opt/halo --shell /usr/sbin/nologin halo
sudo install -D -m 755 build/linux/halo-server /opt/halo/bin/halo-server
sudo install -d -o halo -g halo -m 750 /opt/halo/mapcache
for n in 1 2 3 4; do
	sudo install -d -o halo -g halo -m 750 /opt/halo/servers/s$n
	sudo ln -s /opt/halo/maps /opt/halo/servers/s$n/maps    # the maps, once
	sudo install -o halo -g halo -m 640 init-s$n.txt /opt/halo/servers/s$n/init.txt   # (a release's examples/)
done
sudo install -m 644 port/linux/halo-server@.service /etc/systemd/system/
sudo install -m 755 tools/halo-servers /usr/local/bin/halo-servers
sudo systemctl daemon-reload
sudo halo-servers enable all
sudo halo-servers start all
```

Each `init.txt` has its own `sv_name`, cycle and `sv_port` (2302, 2303,
2304, 2305); give each `sv_game_port` only to move it from the default. The
maps folder (`/opt/halo/maps`, readable by `halo`) can be one for all.
Open UDP 2302 to 2305 in the firewall (`sudo ufw allow 2302:2305/udp`).

`tools/halo-servers` manages them:

```
$ halo-servers list
SERVER       STATE      PORT   GAME-PORTS  NAME
s1           active     2302   5150-5151   My Slayer Server
s2           active     2303   5152-5153   My Big Team Server
...
$ sudo halo-servers status
s1: active; playing, slayer on prisoner; 3 players (Vita1, Bob, Ann); code ABCD-EFGH
s2: active; lobby, team_slayer on bloodgulch; 0 players; code JKLM-NPQR
...
$ sudo halo-servers restart s2
$ sudo halo-servers logs s1 -f
```

`status` reads each server's log since it last started (the code, the game,
who joined and left). The logs are the journal's (`journalctl -u
halo-server@s1`), which journald keeps and rotates (`SystemMaxUse=` in
`/etc/systemd/journald.conf`; a server writes a few lines a game). Reading
them needs root or the `systemd-journal` group; starting and stopping,
root.

Each server used 92-117 MB of memory (the unit allows 600 MB) and 2% of a
PC core with four players: a VPS with 2 GB and two cores holds four with
room to spare.

The single-server unit (`halo-server.service`, `/var/lib/halo-server`) is
unchanged; a server moved from it to `/opt/halo/servers` keeps its
`init.txt` and `bans.txt`.

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
- `dedicatedcoop`: co-op on The Truth and Reconciliation with two Vitas,
  and the empty round ended.
- `dedicatedmulti`: three servers on one machine (internet play's ports
  2302 to 2304; game ports from `sv_port` and from `sv_game_port`), sharing
  a map cache: each hosts and is listed, two Vitas join each (by its code,
  and from the server browser by its name), a Vita on their LAN joins the
  first one alone, and each map is decompressed once
  (`HALO_TEST_MULTI_SHARED=0`: each server its own cache, to compare).

`HALO_TEST_SYMMETRIC_NAT=joiners` (or `all`, with
`HALO_TEST_SERVER_PUBLIC=1`) makes the routers' NAT symmetric.

```
python3 configure.py --linux-d3d gxm-null --linux-net-vita --lto off --pgo off
ninja linux linux-server
HALO_TEST_DATA=/path/to/data port/vita/tests/run_netns_online_test.sh dedicated
```

`HALO_TEST_SERVER_STATS=1` records the server's CPU time and memory every
5 seconds in `server/stats.log`.
