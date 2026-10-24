# Superseded aggregate proposal: follow-up work

The original #48 aggregate is retained as a review reference, not a merge
candidate. The maintainer's review identified stock-map/network compatibility,
campaign-save layout, and local bot-profile lifetime problems. The naming-only
cleanup on this branch does not resolve those behavior problems. Do not ship
the old aggregate or its replacement-map installer.

## Prioritized independent PRs

- #49: standalone Vita atomic runtime, with host concurrency checks and an
  explicit on-device acceptance gate.
- #50: isolated owner-input installer, preserving stock Xbox/CE staging and
  naming retail maps `pc_*` in a separate profile. Retail upload/play remains
  disabled until a reviewed offline runtime integration exists.
- The maintainer is integrating the v7-only loader directly; no duplicate
  loader PR is proposed.

Both independent branches start from current upstream main. They contain no
extra CI workflow or dependencies on this branch's gameplay additions.

## Remaining work before any further gameplay PR

1. Compare each feature with `next`; use its existing PC vehicles, banshees,
   bots and gametype menus instead of proposing overlapping implementations.
2. Retail content needs alias/resource/cache namespace routing and offline
   guards for host, join, map sharing and playlist paths. Metadata alone is
   insufficient. Preserve Xbox maps and standard CE resources.
3. Rework campaign placement without changing checkpoint/game-state allocation
   layout. Old campaign saves must load unchanged; restore/revert/BSP transitions
   also need tests. Campaign additions remain withheld until this is proven.
4. Any optional local profiles must be cleared at teardown and every nonlocal
   transition, with regression coverage for local match -> hosted online match.
5. Consider opt-in visual effects separately only after checking `next` for
   overlap and measuring device rendering/performance regressions.

The remaining module, symbol, test, action and environment-switch names now
describe their purpose (`pc_content`, `pc_vehicle_fleet`, `local_bot_profiles`,
`combat_fx_options`). Personal prefixes and the added CI workflow were removed.
Private personal-build branches and existing device installations are untouched.

## Historical implementation notes

The notes below describe the original aggregate and its earlier tests. They
are not current integration instructions or acceptance of its known blockers.

This source-only series targets `BirchWoodGod/halo-ce-vita` main
`61debf05e6e31c8583f172e0992c0a0c632a9e3a`. kaboggs adapted the local
PC-content, bot-profile and combat-detail features, retaining existing notices.
It preserves upstream title ID, art, data directory, updater and release URLs.
No private development history or custom branding is part of this series.

## Review and application order

1. Validated retail version-7 cache acceptance and bitmap group-offset fix,
   with synthetic regressions. PC bitmap addresses remain absolute.
2. Sized/generic Vita atomic runtime and shared unaligned synchronization;
   fixes SDK linkage without unaligned ARM exclusive accesses.
3. Opt-in local gameplay and menu changes, with portable scheduler/profile/FX
   tests. Module names now describe the integration units' purpose.
4. Owner-input PC folder/ISO staging, separate CE menu resources, generated
   local previews, safe upload enumeration and synthetic installer tests.
5. This handoff and a read-only CI job that tests the source and builds a VPK.

The branch descends directly from the pinned upstream base; it can be merged or
cherry-picked in this order. Loader/atomic commits can be reviewed independently.
Use ordinary `git cherry-pick` or `git am` on exported patches. No retail inputs
are required for CI or compilation. Maintainers choose whether to enable or
rename the optional gameplay integration before release.

## Features and limits

| Area | Behavior | Acceptance |
| --- | --- | --- |
| Retail caches | Existing validated PC conversion also accepts version 7; normalize stale bitmap-group offsets | 29 owner campaign/MP maps converted; synthetic regressions; Timberland freeze reproduction resolved on Vita |
| Menus | Isolate CE menu resources from retail gameplay resources; six locally extracted previews | Join/Create menus and Timberland preview confirmed; actual internet play unverified |
| Gametypes | Native Item Options adds All+PC, Banshees, Rocket Warthogs, Normal + PC, Flamethrower and Fuel Rod choices | Saved selection and automatic All+PC spawns on Blood Gulch confirmed |
| Automatic fleet | One tracked vehicle per type/team, adopts live native vehicles, ground/headroom checks, bounded creation and respawn | Real scheduler exercised with a fake engine; other maps and individual vehicle operation remain unverified |
| Manual tools | Eight real loaded weapon/vehicle definitions, tick-thread placement and status | Flamethrower spawn/fire/flames/scorch confirmed on Timberland |
| Combat detail | Optional rifle ribbons/sniper smoke and combat particle/decal preservation | Portable helper tests; full visual/performance matrix pending |
| Rare bot profiles | Optional independent match chances, camouflage/combat behavior, full scoreboard names and FFA color preferences | Probability/uniqueness tests; special-profile hardware checks pending |
| Campaign | Optional four loaded-asset pickup placements; no general asset importer | Opening retail campaign movement/look confirmed; added pickups/full campaign pending |

Automatic vehicles apply only to local/Split Screen games on Blood Gulch,
Sidewinder, Death Island, Danger Canyon, Ice Fields, Infinity, Timberland and
Gephyrophobia. Existing local player and loaded-tag checks are required. Small
arenas receive no added vehicles. Up to twelve tracked vehicles and four extra
pickups are scheduled; this is not a cap on all native map objects. Live driven
vehicles are retained; destroyed/collected slots wait at least 30 seconds.

The scheduler and remapper read the **loaded scenario** tag name, not the solo
menu's stale selection. A regression leaves solo a10 selected while loading full
Blood Gulch/Death Island paths. Bounded logs report selected flags, anchors,
successful spawns and blocked ground.

Native game-variant size and field ranges are unchanged. Reserved flag bits
24..27 carry the local choices; original builds ignore them and see the base
native set. The shipped Team Ball preset's `0xffffffe3` filler value is explicitly
normalized before interpreting/editing additions. Unknown unrelated bits are
otherwise preserved. Crossplay weapon substitution and remote-client mutation
guards remain in place. This does not establish mixed-build networking support.

Upstream bots, particle budget, PC-map enablement and local weapon/campaign
extras keep conservative defaults; special profiles and enhanced combat FX are
opt-in here. The separate private development build uses personal defaults and
its own app/data identity. Its hardware results exercise the same loader and
core gameplay code, not a fully qualified upstream package.

## Reproduce checks

See `.github/workflows/pc-content.yml`: compile/run the four C helper tests,
run cache/BMP/scheduler tests and the installer suite, then configure and build
with the pinned VitaSDK/SDL setup. The cache reader runs with undefined-behavior
trapping. Installer tests use invented maps, resource records and ISO images;
they do not embed commercial fixtures. Windows may skip symbolic-link cases;
Linux CI exercises them.

[Installer instructions](../tools/installer/README-pc-content.md) describe the
input formats, new-output requirement, manifest, incomplete marker and explicit
upload. The command-line extension is included as readable Python source; this
series does not claim a newly packaged Windows executable or GUI wizard step.

## Security and rights scope

The contribution review covers newly reachable source commits and blobs,
asset/secret/path exclusion, new input parsers, bounded allocations/indices,
subprocess boundaries, local-only mutation, compiler atomics and added tests.
The new staging/ISO modules introduce no downloader, subprocess, executable
payload or production dependency. Existing FTP/CE extraction code is reused;
the complete inherited game/installer/dependency tree is not certified secure.

No maps, ISO/XBE/VPK files, extracted textures/models/audio/tags, preview pixels,
saves, SDKs, executables, private machine paths or private bubble art are added.
Source strings naming game tags and four placement points are functional
integration data, not bundled game assets. All generated previews stay local.
Original notices and GPL-3.0-only remain; the adapted CC0 reader/test provenance
and complete CC0 text accompany it. No code from the N64 renderer or conversion
pipeline is copied.

This is a technical asset-exclusion and license/provenance review, **not legal
clearance** for inherited decompilation, trademarks or users' input files. The
[GPL terms](https://www.gnu.org/licenses/gpl-3.0.html) cover rights the contributors
can grant; they do not license third-party game data. Microsoft's
[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules) are not
blanket reverse-engineering permission. No Microsoft endorsement is claimed.
Users must have the necessary rights to their files. No DRM bypass is supplied.
