# dedicatedcompat: can public 1.1.0 play the s8 Infection server?

`run_netns_online_test.sh dedicatedcompat` (or `dedicated_compat`) starts this
tree's dedicated server with Infection, 16 players and `sv_bots_fill 1`, then
has one **public v1.1.0** client and one **insider.21** client join it by its
code and play a round. Everything runs in private network namespaces with
the test's own MQTT broker, STUN server and relay. Nothing reaches the
internet. The code is in `netns_dedicated_compat.sh` (the run) and
`netns_compat_check.py` (the checks).

## Prerequisites (once)

Three Linux builds, all `--linux-net-vita`, i686, with the local 32-bit SDL3.
The clang wrapper adds `-I/-L/-rpath` for `../sdl3-i686`.

```sh
H=~/github/vitaprojects/halo-ce
W=~/halo-test-out/s8compat/tools/clang-sdl32   # exec clang -I$H/sdl3-i686/include -L$H/sdl3-i686/lib -Wl,-rpath,$H/sdl3-i686/lib "$@"
git -C $H/wt-insider21 worktree add --detach ../wt-v110client v1.1.0     # e1bafe52, the public release
git -C $H/wt-insider21 worktree add --detach ../wt-i21client 99bd7d99    # insider.21
for w in wt-v110client wt-i21client; do
  (cd $H/$w && python3 configure.py --linux-cc $W --lto off --pgo off --linux-net-vita &&
   sed -i "s|^  libs = -lSDL3 -lm -lpthread\$|  libs = -L$H/sdl3-i686/lib -lSDL3 -lm -lpthread|" build.ninja &&
   ninja build/linux/halo)
done
# the server, from the tree under test (configure the same way, then):
ninja build/linux/halo-server
```

v1.1.0 needs no test hook. Its network test (`HALO_NETWORK_TEST=join-code:`,
`HALO_TEST_INPUT=bot:N`, `HALO_NETWORK_TEST_SHOOT`) and its per-second
`network test: tick` lines (every player's team, weapons and its own player)
are the same as insider.21's.

## Run

```sh
cd <tree under test>
LD_LIBRARY_PATH=~/github/vitaprojects/halo-ce/sdl3-i686/lib \
HALO_TEST_OUT=~/halo-test-out/s8compat/run-$(date +%H%M) HALO_TEST_CPUS="0-7 8-15 16-23" \
  port/vita/tests/run_netns_online_test.sh dedicatedcompat
```

It takes about 5.5 minutes. The output ends with one line per check
(`[PASS]`, `[FAIL]`, `[WARN]`, `[NOT RUN]`) and a summary. The logs are kept
on a failure, or always with `HALO_TEST_KEEP=1`: `server/run.log`,
`v110/run.log`, `insider/run.log`.

Settings: `HALO_TEST_COMPAT_V110` and `HALO_TEST_COMPAT_NEW` (the client
binaries; the defaults are `../wt-v110client` and `../wt-i21client`),
`HALO_TEST_SERVER`, `HALO_TEST_BOTS_MAX` (16), `HALO_TEST_COMPAT_CYCLE`
(`prisoner infection`; several games are split by `;`, for example
`prisoner infection;chillout infection` with `HALO_TEST_SECONDS=520`),
`HALO_TEST_COMPAT_MINUTES` (3), `HALO_TEST_SERVER_INIT` (your own init.txt),
`HALO_TEST_COMPAT_LENIENT=1` (a check the run never reached is a warning).

## What it checks

- Both players joined, and no one was refused (Infection refusal, full
  game, too many from one address).
- Once settled, the bots keep the game at max-1, and it is never full with
  bots in it.
- The round begins and ends with a winner. Each client sees the end, and the
  insider's winner is the server's.
- No crash, assertion, dashboard (`XLaunchNewImage`) or lost connection on
  the server or either client. Each client exits 0.
- The 1.1.0 client plays with others for 40 s or more and sees the full game
  (15 players).
- The 1.1.0 player is **killed by a zombie**, and its own team switches from
  human (t1) to zombie (t0) on its side. Once it is a zombie it holds only the
  skull. The skull is the one weapon the zombies hold on the clients.
- Every player the server turned is a zombie on each client, and the two
  clients agree on every player's team second by second (no desync).

The game decides whether the 1.1.0 player gets infected. If it never is,
those checks say `NOT RUN` and the test fails. Run it again, or use two
games in the cycle.
