# Halo 3-style menus for Halo CE Vita (design)

Date: 2026-10-18. Status: approved mockups, design for review.
Part 1 of 3: (1) Halo 3-style menus, (2) Forge sandbox, (3) File Share.
Forge and File Share each get their own design later. Where this document
mentions them, it is only so the menu kit can carry them.

## Why

The owner wants Halo CE on the Vita to feel like Halo 3 on the Xbox 360:
- the main menu column;
- the campaign lobby with its nameplates;
- the orange selection bar;
- the Forge and File Share screens.

Players who prefer the classic look must keep it. The menus are also the
groundwork Forge and File Share are built on, since both need list, tab,
picture and lobby screens that Halo CE's menus don't have.

## Goals

1. A **Halo 3 menu kit**: one look and one set of building blocks, used
   by every new screen.
2. A **Menu style** setting with two values: **Halo 3** (the default
   once it ships) and **Halo CE**, the menus exactly as they are today.
   It changes live, at the main menu, with no restart.
3. Halo 3-style versions of four screens:
   - the main menu;
   - the campaign lobby;
   - settings;
   - the multiplayer entry point (Multiplayer / Online), reusing the
     existing OpenCE multiplayer screens' functions.
4. **Background (the owner's words): it stays Halo CE's animated Halo
   ring.** The ui.map main-menu scene keeps rendering exactly as today.
   The new screens draw over it. No other background art is shipped.

## Non-goals (for this part)

- Forge itself and File Share's saving and sharing (parts 2 and 3). The
  main menu shows **Forge** and **File Share** items. Until those parts
  ship, the items are hidden, not shown greyed out or broken.
- In-game HUD changes. HUD and pause menu stay Halo CE's.
- A 3D Spartan in the campaign lobby. The mockup shows one, standing
  like Chief and the Arbiter in Halo 3's lobby, but rendering the player
  biped in the menu is its own task. It comes after this part. Until
  then that area shows the animated ring.

## Anti-goals (would make it a failure even if it works)

- Shipping Bungie, Microsoft or Halo Online art, fonts or text. The
  look is recreated with our own PNGs and a free lookalike font. Halo
  Online's files and Halo 3 screenshots are **references only** and live
  outside the repo (`~/halo-test-out/ho-shots/`, `~/Downloads/`). The
  same goes for twist84's Halo 3 recomp: read it to understand behaviour,
  never copy from it.
- Breaking the classic menus. With Menu style = Halo CE, every screen,
  flow and test behaves exactly as on dev-1.1.1.
- A slower main menu, or more memory at the main menu than the Vita can
  spare. Today's main menu load time and the ui.map memory budget are the
  bar.
- Screens that don't fit the Vita's 960×544, or text too small to read on
  the Vita's 5-inch screen.

## The look (from the approved mockups)

The approved mockups are in
`~/github/vitaprojects/halo-ce/forge-design/.superpowers/brainstorm/84745-1792297867/content/`:
- `main-campaign-v4.html`: main menu and campaign lobby, each beside the
  Halo 3 screenshot it copies;
- `settings-campaign-v2.html`: settings A;
- `h3-look-v6.html`: File Share and the Forge browser, for parts 2 and 3.

Values taken from Halo Online's own stylesheets (its settings screen and
Forge window):

| Element | Value |
|---|---|
| Panel gradient | `rgba(27,41,70,.95)` → `rgba(24,39,69,.9)` at 20% → `rgba(6,16,37,.9)` |
| Title and button strips | near-black, `rgba(2,5,10,.88)`, with a 1 px rule in `rgba(160,180,205,.55)` fading at both ends |
| Selection bar | `linear-gradient(90deg, rgba(200,120,40,.55) 0%, transparent 150%)`; Halo 3's main-menu column uses a fuller orange `#b8743a` → `#8a5a3a` |
| Text | primary `hsl(0,0%,68%)`, selected `hsl(0,0%,86–92%)`, values ice blue `#77879A`, selected values `hsl(213,30%,80%)` |
| Type | uppercase condensed sans for items and titles (Halo 3 uses Conduit ITC; we use a free lookalike, see below); mixed case for help text and button labels |
| Buttons | PlayStation glyphs: dark round buttons with coloured symbols (✕ blue, ○ red, △ green, □ pink) and the label beside them, in the bottom strip |
| Nameplate | a solid colour bar (the profile's colour), emblem square on the left, rank badge on the right, and Halo 3's spinning ring left of the bar while busy |

The screens:

- **Main menu (Halo 3's own layout):**
  - a translucent column lower-left, starting at about 55% of the
    screen's height;
  - its items: START SOLO GAME (orange when highlighted), a rule, then
    Campaign, Multiplayer, Online, Forge, File Share, with Settings on
    the Start button at the column's foot;
  - a large glowing HALO title on the right with "Combat Evolved" under
    it;
  - top-right, "1 Player", then the player's nameplate;
  - under that, "ONLINE NOW" and up to three live servers as nameplates
    (name · map players/max, ping). They come from the existing server
    list.
  - Online and the server rows are hidden when Connection is Offline.
- **Campaign lobby (Halo 3's, near one-to-one):**
  - a left column titled CAMPAIGN LOBBY, with the options Switch Lobby,
    Network: Local/Online, Mission: <level>, Difficulty: <difficulty> and
    Start Game;
  - then a "□ Edit Campaign Options" strip and the status line ("Ready" /
    "This party is local to this PS Vita", or the co-op lobby's state);
  - the mission's picture with the difficulty emblem over it and a
    loading bar under it, captioned "<Mission> on <Difficulty> at
    Mission Start" (or "at <checkpoint>" when continuing);
  - bottom strip: Settings, Select, Leave;
  - right side: "N Player(s) (4 max)" and a nameplate for each player
    (co-op is up to four);
  - Mission and Difficulty each open a picker in Halo 3's Select Map
    style (list, picture, description). Levels not unlocked aren't
    offered.
- **Settings (mockup A):**
  - the title strip SETTINGS with the nameplate at its right;
  - tabs General / Graphics / Controls / Audio / Multiplayer (plus Dev
    when shown), with L/R hints;
  - small section headings, and rows with values in ice blue;
  - the highlighted row shows "‹ value ›";
  - a side panel with the row's name, its value large, a cost bar where
    the row has a performance cost, and the help text;
  - bottom strip: Change, Defaults, Tabs, Back.
  - It shows today's settings, so `vita_settings.c`'s rows, pages and
    environment variables stay the source of truth. Only the drawing and
    the layout change.

## How (decided)

- **Built on the menus we already have.** dev-1.1.1 already turns
  OpenCE-format XML (`port/vita/app0/menus/*.xml`, CC0) into Halo CE
  widget tags when ui.map loads (`port/linux/game/menu_tags.c`,
  `menu_files.c`, `menu_functions.c`), with our own PNG frames. The
  game's widget code then runs them. The Halo 3 screens are more XML
  screens plus PNG art in the same system, standing in for the Xbox
  screens they replace, the way `multiplayer.xml` stands in for the
  Multiplayer screen today. That keeps the animated ring (ui.map's scene
  is untouched) and reuses widget navigation, sounds and history.
- **Menu style picks which set opens.** `menu_tags_screen` (the hook
  that already swaps screens) consults the setting. Halo CE means the
  hook leaves the game's screens alone.
- **Settings in the new style** is the one screen not made of widgets:
  the settings panel is drawn by our own GXM overlay (`vita_gxm.c`
  `menu_build`, a bitmap font). The Halo 3 settings screen is that same
  panel with a new layout and drawing (panel, strips, bar, side panel,
  glyphs), chosen by Menu style. Its rows and logic are not forked.
- **Our own art only.** Gradients, strips, bars, glyphs, nameplates,
  emblem frames and difficulty emblems are drawn by us as PNGs (or by
  code in the overlay), and committed under `port/vita/app0/menus/`.
  Mission pictures come from the game's own campaign level images (the
  Xbox menus' level previews in ui.map), not from screenshots we ship.
- **A free condensed font** in Halo 3's style (open licence, for example
  from Google Fonts' Saira or Barlow Condensed family; the builder picks
  and records the licence). It's built into the widgets' font form and
  the overlay's bitmap font.

## Left to the builder

- Which free font, and how it becomes a Halo CE font tag (menu_tags.c
  builds bitmaps and string lists today, not fonts), or whether the new
  screens use a converted bitmap font instead.
- How data the widgets don't have reaches them: the live servers, the
  player count and nameplates, the mission picture, "at Mission Start"
  versus a checkpoint. These are game data inputs, as the multiplayer
  screen's `port mp update desc` does today.
- The exact pixel layout within Halo CE's 640×480 widget space, scaled
  to the Vita's screen, matching the mockups.
- How the settings overlay's new drawing is split from the old, so the
  classic panel stays byte-for-byte the same when Menu style is Halo CE.
- Tests: the existing menus test (`run_menus_test.sh`) for both styles,
  and `vita_settings_test` for the new row.

## Constraints

- Vita: 960×544, one GPU, the ui.map memory budget at the main menu.
  The new screens' PNGs load when a screen first opens, as the
  multiplayer screens' do, not as ui.map loads.
- The OpenCE menu XML format and its CC0 code stay compatible with
  OpenCE, so improvements can go back upstream.
- PC and Linux builds share the code. Menu style works there too.
- Language: every new string goes through `T()` and lang files (es.txt
  today).

## Out of scope, noted for later parts

- **Forge (part 2):**
  - the Forge lobby and map picker, from the Halo Online captures in
    `~/halo-test-out/ho-shots/`;
  - monitor controls copied from Halo 3's diagram (fly up/down, thrust,
    grab, rotate, delete, item menu, drop/pick up), mapped to the Vita;
  - the object browser (`h3-look-v6.html` B);
  - objects and AI from any map: the hard part, loading tags across map
    files.
- **File Share (part 3):** `h3-look-v6.html` A, then saving and loading,
  sharing, and the security checks on files from other players.
