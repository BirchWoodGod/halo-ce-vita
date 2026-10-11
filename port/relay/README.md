# halo-relay

A relay for internet play. Some pairs of networks can't connect two machines
directly, for example when both are behind symmetric NAT, double NAT or a
mobile carrier's NAT. For those pairs, the relay passes the game's tunnel
packets between the two machines. It runs on any Linux machine with a public
IPv4 address and one open UDP port, such as a Raspberry Pi at home with the
port forwarded, or a small VPS.

- **It sees no game content.** The tunnel's packets are encrypted end to end
  with keys that only the two machines hold (`port/linux/src/p2p_crypto.c`).
  The relay forwards them byte for byte and can't read or change them.
- **It's not an open proxy.** It forwards only between the two machines of
  one game session. Both machines must ask for the same allocation, using an
  identifier derived from their session's secret, which only they can work
  out. It never sends anything to an address that hasn't shown it receives
  there.
- **It's only a fallback.** The game always tries a direct connection first.
  It asks a relay only when there's still no two-way direct path 8 seconds
  after a player starts connecting. While a game goes through the relay,
  the game keeps trying the direct addresses for 5 minutes and moves back
  to a direct path as soon as one answers.

The protocol is in `port/linux/src/p2p_relay_protocol.h`, the rules in
`relay.c`, and the threat model in the project's `triage/relay-status.md`.

## Build

You need a C compiler and make. Nothing else, apart from Monocypher, which
is in this repository:

```sh
cd port/relay
make            # builds ./halo-relay
make test       # its checks, and its packet reader fuzzed with ASan and UBSan (needs clang)
```

## Run

```sh
./halo-relay                     # UDP 47320 on every IPv4 address
./halo-relay --port 47320 --total-mbit 20
```

| Option | Default | What it does |
| --- | --- | --- |
| `--bind ADDRESS` | 0.0.0.0 | The IPv4 address to listen on |
| `--port PORT` | 47320 | The UDP port |
| `--allocations N` | 256 | The most games (relayed player pairs) at once |
| `--per-address N` | 16 | The most allocations one IPv4 address can be in. A host whose players all come through the relay uses one allocation per player. |
| `--rate-kbit N` | 4000 | One allocation's rate, both directions together |
| `--burst-kb N` | 256 | One allocation's burst |
| `--packet-rate N` | 1000 | One allocation's packets per second |
| `--max-mb N` | 1024 | One allocation's total traffic, in megabytes (0 for no limit) |
| `--total-mbit N` | 0 (no limit) | All allocations' rate together. Set it below your upload speed. |
| `--lifetime-min N` | 360 | An allocation's longest life, in minutes |
| `--idle-sec N` | 60 | An allocation closes when both of its machines have been silent this long |
| `--quiet` | | Logs only the summaries |

The relay prints one line when an allocation opens, when its pair is
complete, and when it closes (with its bytes), plus a summary every 10
minutes and on `SIGUSR1`. The log never contains an IP address. It shows
a tag instead: a hash of the address, keyed with a random key that's
generated at each start and never stored. Tags tell the machines apart
within one run, but a tag can't be traced back to an address or matched
across restarts. Addresses are kept in memory only while their allocation
is open.

### As a service (systemd)

`halo-relay.service` is a hardened unit. It runs as a dynamic user with no
privileges, no writable filesystem, IPv4 sockets only and 64 MB of memory:

```sh
sudo install -m 755 halo-relay /usr/local/bin/halo-relay
sudo install -m 644 halo-relay.service /etc/systemd/system/halo-relay.service
sudo systemctl daemon-reload
sudo systemctl enable --now halo-relay
journalctl -u halo-relay -f
```

Open UDP 47320 in the machine's firewall. For example:

- with ufw: `sudo ufw allow 47320/udp`
- with nftables: `nft add rule inet filter input udp dport 47320 accept`

## Where to host it

- **Raspberry Pi at home.** Forward UDP 47320 on the router to the Pi. The
  relay's traffic counts against the home connection's upload: every byte it
  receives from one player, it sends to the other.
- **VPS.** Any small Linux VPS works: the relay needs about 1 MB of memory
  and hardly any CPU. Open UDP 47320 in the provider's firewall too.

### Bandwidth

Measured in `port/vita/tests/run_netns_online_test.sh relay`, one joiner
playing a host's Slayer game through the relay used about **130 kbit/s
with both directions added together**, about 1 MB a minute. The relay
receives that much and sends the same amount, so it needs about 130 kbit/s
of upload and 130 kbit/s of download per relayed player. A full Vita game
(16 players) in which every joiner comes through the relay needs at least
2 Mbit/s each way, and more in practice, because a bigger game sends each
player more. A custom map that a host shares with a relayed joiner
moves at up to `--rate-kbit` while it downloads. Most players connect
directly and never touch the relay.

## Telling the game about it

On a Vita, either:

- put the relay in `relays.txt` beside `brokers.txt` (`app0:relays.txt` in
  the game's package, which is empty by default), one `host:port` per line,
  at most 2 relays; or
- add `HALO_NET_RELAYS=relay.example.org:47320` to `env.txt`.

On Linux and Windows builds, put `relays.txt` beside `config.toml`, or set
`network.relays`. `network.allow_relay = false` turns relaying off.

The two players don't need to name the same relay. Each machine offers its
own relays when the players connect, and both use the host's relays first,
then the joiner's. One player having a relay is enough.
