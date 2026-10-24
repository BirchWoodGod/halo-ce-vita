# Optional Vita combat visuals

This independent follow-up to #48 keeps the upstream particle budget, particle
density and tiny-decal controls. Two separate opt-in rows appear on the existing
graphics detail page; both default to their original/budgeted behavior.

- **Ribbon tracers: Enhanced** makes the authored assault-rifle and sniper
  ribbons easier to see at Vita resolution. Width floors are bounded; the
  short rifle transition lasts several visual ticks and a two-point rifle
  ribbon keeps a visible tail. Other contrails use their authored behavior.
- **Combat detail: Preserved** exempts matching weapon/impact/shield/blood/flame/
  scorch effects from the reduced particle emission/drawing and tiny-decal
  filters. Existing pools still bound allocations. More particles and marks
  cost performance; ambient effects keep their normal budget controls.

No new textures, sounds, effects, map data or game assets are bundled. The game
uses the owner's existing authored effects. Projectile movement, collision,
damage, gameplay tick rate, save layout, wire layout and content paths are not
changed. The setting changes contrail visual time, not projectile flight time.
No PC map loader or custom gameplay implementation is required.

`HALO_COMBAT_TRACERS=0` and `HALO_COMBAT_FX=0` are the defaults. The host helper
regression covers unset/off/on switches, effect classification, unrelated effect
exclusion, bounded width floors and the short ribbon transition. It does not
validate real Vita rendering/performance or compile the complete game.

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror \
  port/vita/tests/combat_fx_options_test.c -o /tmp/combat-fx
/tmp/combat-fx
```

Before integration, build with VitaSDK and measure on-device: compare both
settings off to upstream; test AR/sniper ribbons at near/far distances; verify
blood, impact marks, flamethrower flames and scorch marks under reduced budgets;
check ambient effects still thin; measure crowded fights, memory and frame time;
confirm toggling back restores the original presentation. This independent
split has no new target build or device/performance acceptance yet.

New helpers/tests use the repository's GPL-3.0-only license. No extra CI workflow
or binary release is included.
