# Optional local bot profiles

This independent follow-up to #48 uses the existing upstream bots, menus,
teams, weapons, vehicle logic and simulation. It adds no vehicle/gametype
implementation or retail content dependency and allocates no checkpoint state.

In the Vita multiplayer panel, **Ghost profiles** is off by default. Select
10% for the requested rare setting: each special profile gets its own encounter
roll once per local countdown. Two bot slots are needed for both to appear at
once. Sparse bot slots are mapped to the actual reserved machine indices.

The two optional profiles keep their scoreboard names, bright-green/teal color
preferences in non-team games, per-spawn camouflage rolls and the first
profile's aim/burst adjustments. These additions run only in Split Screen;
upstream still chooses the team colors. **Profile colors: Original** leaves
upstream's primary color choices intact.

The server stores the original name/color fields before applying a profile.
It restores those fields and clears profile state at the next countdown,
return to pregame, server disposal, map teardown, and before remote connections
are enabled. The restore operation leaves team/controller/lobby fields alone.
Every reader and application path also checks current Split Screen mode, so
a mode change cannot enable special skills or scoreboard substitution online.
This is static session state, outside the saved-game allocator and wire layout.

Host regressions execute the actual profile module and the production countdown,
cleanup and remote-accept callback bodies with minimal game-field stubs. They
cover repeated local -> online cycles, restoration before the remote-network
callback, an abrupt mode change, sparse slots, invalid settings/indices, human
name exclusion and color opt-out. The separate selection regression checks
100,000 seeded matches. These tests do not establish the Vita ABI or real-game
rendering, networking, powerup timing or complete session lifecycle behavior.

Run the host checks with a C compiler on PATH:

```sh
python -m pytest -q tools/test_local_bot_profile_lifecycle.py
cc -std=c99 -O2 -Wall -Wextra -Werror port/vita/tests/local_bot_profile_rules_test.c -o /tmp/profile-rules
/tmp/profile-rules
```

Before integration, build with VitaSDK and test on-device: Off leaves the normal
bots unchanged; Always with two bots shows both profiles; team colors hold;
names, colors and abilities reset after a local game followed by online hosting
or joining; returning to Split Screen gives a fresh roll; repeated cancel/retry,
playlist rotation and map teardown do not leak state. No new device pass is
claimed for this independent split. Existing campaign saves and online wire
structures are not changed.

The new Vita contribution uses the repository's GPL-3.0-only license. It contains
source and synthetic tests, with no game data or additional CI workflow.
