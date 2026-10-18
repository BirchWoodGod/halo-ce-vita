The Halo 3-style menus (app0:menus/halo3/)

With Menu style Halo 3 (settings panel: Graphics > Screen; HALO_MENU_STYLE=halo3)
these screens stand for the Xbox's own, over the same animated Halo ring:
- main.xml: the main menu (ui\shell\main_menu\main_menu) - START SOLO GAME,
  CAMPAIGN, MULTIPLAYER, FORGE (its lobby, below), FILE SHARE ("in a later
  update" for now), CUSTOMIZATION, Settings on Start, the HALO title, the
  player's nameplate and the live servers (ONLINE NOW).
- campaign.xml: the campaign lobby (it stands for the level select the
  campaign's profile screen opens) with its Select Mission and Select
  Difficulty pickers.
- forge_lobby.xml: the Forge lobby FORGE opens, with its Select Map picker
  (the multiplayer maps of the maps folder) and START FORGE, which starts a
  local game of the map in Forge mode (port/linux/game/forge.c).
In each level's map (ingame.txt):
- pause.xml: the pause screen (ui\shell\solo_game\pause_game\pause_game).
- forge.xml: Forge's screens over the game: the object browser (Square, as
  the monitor), the Forge menu (Start in a Forge game) and its question
  before leaving.
Each <menus root=... replaces="<the Xbox screen>"> names the screen it stands
for; port/linux/game/menu_tags.c opens it in that one's place while Menu style
is Halo 3. Like OpenCE's screens in the folder above, they are built when
ui.map loads and need the Halo PC (Custom Edition) files in the maps folder;
their pictures are read the first time one opens, and if they cannot be read
the game's own screens open.

What is here
- The PNGs and the fonts (font_*.bin) are this project's own, written by
  tools/halo3_menu_art.py (python3 tools/halo3_menu_art.py
  port/vita/app0/menus/halo3): the panels, the orange bars, the strips, the
  PlayStation button icons, the nameplates, the HALO title, and three fonts
  from Saira Semi Condensed (SIL Open Font License, tools/fonts/OFL.txt).
  The look follows Halo 3's screens; nothing of Bungie's, Microsoft's or
  Halo Online's is in this folder.
- The level pictures and the difficulty emblems are ui.map's own
  (<frame map=...>).
- The game's menus draw a picture 1:1, cropped to its widget, or stretched
  when the widget is larger (ui_widget.c draw_bitmap_in_rect): each PNG is at
  most its widget's size, and a widget draws a line of text only when the
  font's line fits its height. A PNG's width and height are powers of two
  (menu_tags.c refuses another: its texture would be read wrong).

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause; Forge's:
port/vita/tests/run_forge_test.sh.
