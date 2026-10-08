# Halo CE for PS Vita: dedicated server

This is the dedicated server of Halo CE for PS Vita, ready to run. It hosts
online games that Vitas join by its code, from the server browser, or on
its LAN. It runs on Linux:

- `linux-x86`: a 32-bit x86 program, for 64-bit PCs and VPSes (Debian 12 or
  newer, Ubuntu 22.04 or newer, and other distributions with glibc 2.36 or
  newer and its 32-bit libraries).
- `linux-armhf`: a 32-bit ARM program, for a Raspberry Pi 4 (Raspberry Pi
  OS 12 "Bookworm" or 13 "Trixie", 32-bit or 64-bit). It is built for the
  ARMv8 processors of the Pi 3, 4 and 5, not the Pi 1, 2 or Zero.

The server needs nothing but glibc: SDL3 is inside the program. The full
guide is `DEDICATED_SERVER.md`, next to this file, also at
<https://github.com/BirchWoodGod/halo-ce-vita/blob/main/port/linux/DEDICATED_SERVER.md>.

**No game data is included.** Copy the `maps` folder of your own Xbox copy
of Halo: Combat Evolved (`ui.map`, `bloodgulch.map` and the rest).

## Quick start

1. Check the download, and unpack it:

   ```
   sha256sum -c SHA256SUMS --ignore-missing     # (the release's SHA256SUMS, next to the tarball)
   tar xzf halo-ce-vita-server-*-linux-*.tar.gz
   cd halo-ce-vita-server-*/
   sha256sum -c SHA256SUMS                     # (the files inside)
   ```

2. Add the 32-bit glibc, once, if your system is 64-bit:

   - PC or VPS (Debian, Ubuntu):
     `sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install libc6:i386`
   - Raspberry Pi with the 64-bit Raspberry Pi OS:
     `sudo dpkg --add-architecture armhf && sudo apt update && sudo apt install libc6:armhf`
   - A 32-bit Raspberry Pi OS needs nothing.

   `./halo-server -help` then shows the options. (On a Pi 5 with the 64-bit
   Raspberry Pi OS, an "Exec format error" means its kernel has 16 KB
   pages, which runs no 32-bit program: `kernel=kernel8.img` in
   `/boot/firmware/config.txt` and a reboot select the 4 KB one.)

3. Make the server's folder, with your maps and a configuration:

   ```
   mkdir -p ~/halo-server
   cp -r /path/to/your/xbox/maps ~/halo-server/maps
   cp init.txt.example ~/halo-server/init.txt     # or one of examples/
   nano ~/halo-server/init.txt                    # sv_name, the map cycle...
   ```

4. Start it:

   ```
   ./halo-server -path ~/halo-server
   ```

   It shows the code Vitas join with (`hosting; Vitas join with the code
   ABCD-EFGH`). Type `help` for the commands, `quit` to stop.

5. Forward the UDP port (2302 by default, `sv_port`) on your router to this
   machine, and open it in the firewall (`sudo ufw allow 2302/udp`). No other
   port is needed from the internet.

## What is in this folder

| File | What it is |
| --- | --- |
| `halo-server` | The server. |
| `init.txt.example` | A commented `init.txt`: the server's name, players, map cycle, port. |
| `examples/` | Four servers' `init.txt`: Slayer, Big Team (Team Slayer, 16 players), Oddball and King of the Hill, on ports 2302 to 2305. |
| `DEDICATED_SERVER.md` | The full guide: every command, port forwarding, Raspberry Pi notes, security. |
| `systemd/` | Optional: systemd units to run one server, or several, as services, and `halo-servers` to manage them (below). |
| `LICENSE`, `licenses/` | The GNU GPL version 3 (the server), and the licences of the code inside it (SDL3's zlib licence, mbed TLS, zlib, expat and the rest). |
| `SHA256SUMS` | The checksums of these files. |

## Run it as a service (optional)

`systemd/halo-server.service` runs one server as the unprivileged user
`halo-server`, with systemd's protections, from `/opt/halo-server` and the
folder `/var/lib/halo-server`:

```
sudo useradd --system --home-dir /var/lib/halo-server --shell /usr/sbin/nologin halo-server
sudo install -d -o halo-server -g halo-server -m 750 /var/lib/halo-server
sudo cp -r /path/to/your/xbox/maps /var/lib/halo-server/maps
sudo install -o halo-server -g halo-server -m 640 init.txt.example /var/lib/halo-server/init.txt
sudo install -D -m 755 halo-server /opt/halo-server/halo-server
sudo install -m 644 systemd/halo-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now halo-server
journalctl -u halo-server -f        # the code Vitas join with
```

Edit `/var/lib/halo-server/init.txt` and `sudo systemctl restart halo-server`
to change it.

## Several servers on one machine (optional)

Each server has its own folder and its own internet play port (`sv_port`:
2302, 2303...); its game ports follow from that (5150 and 5151 for 2302,
5152 and 5153 for 2303...). `systemd/halo-server@.service` runs one server
per folder in `/opt/halo/servers`, and `systemd/halo-servers` lists,
starts, stops and shows them. Four servers from `examples/`:

```
sudo useradd --system --home-dir /opt/halo --shell /usr/sbin/nologin halo
sudo install -D -m 755 halo-server /opt/halo/bin/halo-server
sudo install -d -o halo -g halo -m 750 /opt/halo/mapcache
sudo cp -r /path/to/your/xbox/maps /opt/halo/maps          # once, for all of them
for game in slayer bigteam oddball koth; do
  sudo install -d -o halo -g halo -m 750 /opt/halo/servers/$game
  sudo ln -s /opt/halo/maps /opt/halo/servers/$game/maps
  sudo install -o halo -g halo -m 640 examples/init-$game.txt /opt/halo/servers/$game/init.txt
done
sudo install -m 644 systemd/halo-server@.service /etc/systemd/system/
sudo install -m 755 systemd/halo-servers /usr/local/bin/halo-servers
sudo systemctl daemon-reload
sudo halo-servers enable all
sudo halo-servers start all
sudo ufw allow 2302:2305/udp
sudo halo-servers status          # each one's code, game and players
```

Give each server a name of its own (`sv_name` in its `init.txt`). The
servers share one map cache (`/opt/halo/mapcache`). `DEDICATED_SERVER.md`,
"Several servers on one VPS", has the details.

### Each server in a network namespace

`systemd/netns/` is the first public servers' set-up, from before servers
had game ports of their own: `halo-netns.sh` makes a network namespace per
server (as root, with iproute2 and iptables), forwards that server's UDP
port into it, and `halo-server-ns@.service` runs server `n` from
`/opt/halo/servers/s<n>` (its `sv_port` 2301 + n) inside namespace `hs<n>`.
This build does not need it; it is there for machines set up that way.

```
sudo install -D -m 755 systemd/netns/halo-netns.sh /opt/halo/halo-netns.sh
sudo install -m 644 systemd/netns/halo-netns@.service systemd/netns/halo-server-ns@.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now halo-server-ns@1      # (and @2, @3...)
```

## Security

- **Never run the server as root.** Use the systemd units (an unprivileged
  user, nothing writable but the server's folder), or an ordinary user with
  write access to the server's folder only.
- **Open only the server's UDP ports** (2302, or one per server) in the
  firewall and on the router. The server needs nothing else from the
  internet: it connects out to the matchmaking brokers and STUN servers.
- The server takes commands only from `init.txt` and its own console. It has
  no remote console, and never runs a program or a script.
- `sv_map_download` is off by default: no files leave the server unless you
  allow the map cycle's custom maps.
- Check the downloads against `SHA256SUMS`.

`DEDICATED_SERVER.md`, "Security", says what the server does against hostile
traffic.

## Licence

Halo CE for PS Vita's code is free software under the GNU General Public
License version 3 only (`LICENSE`); its source is at
<https://github.com/BirchWoodGod/halo-ce-vita>. `licenses/` holds the notices
of the libraries built into the server. Halo and its data are Microsoft's;
nothing of the game's data is in this package.
