The Halo 3-style menus (app0:menus/halo3/)

With Menu style Halo 3 (settings panel: Graphics > Screen; HALO_MENU_STYLE=halo3)
these screens stand for the Xbox's own, over the same animated Halo ring:
- main.xml: the main menu (ui\shell\main_menu\main_menu) - START SOLO GAME,
  CAMPAIGN, MULTIPLAYER, FORGE and FILE SHARE (both "in a later update" for
  now), CUSTOMIZATION, Settings on Start, the HALO title, the player's
  nameplate and the live servers (ONLINE NOW).
- campaign.xml: the campaign lobby (it stands for the level select the
  campaign's profile screen opens) with its Select Mission and Select
  Difficulty pickers.
- multiplayer.xml: the multiplayer lobby (it stands for the Xbox's Multiplayer
  screen, as OpenCE's multiplayer.xml does with Menu style Halo CE):
  MULTIPLAYER LOBBY with NETWORK (ONLINE: internet play; SYSTEM LINK, or AD HOC
  with the connection Ad hoc; OFFLINE: split screen), GAME and MAP (pickers:
  the game type and map the Xbox's lists open on after START GAME), START GAME
  (ONLINE: Create Game; SYSTEM LINK: the profile, then the new game's map list;
  OFFLINE: split screen's), EDIT GAME TYPES (the Xbox's), BROWSE GAMES (ONLINE:
  the server browser, CUSTOM GAMES; SYSTEM LINK: the System Link list) and JOIN
  WITH CODE; the map's picture, "<game> on <map>", "1 Player (N max)" and the
  nameplate. The browser, Create Game and JOIN WITH CODE run OpenCE's screens'
  functions (menu_functions.c "port browser/setup/code ...") under the same
  widget names; the browser's rows show a VITA or PC badge with a lock for a
  password. When internet play is off, ONLINE's rows open a dialog saying how to
  turn it on.
- password.xml: the password screen of a game with one (it stands for OpenCE's,
  pc\join\password\screen, which the browser opens).
Each <menus root=... replaces="<the Xbox screen>"> names the screen it stands
for (or OpenCE's, for the password screen); port/linux/game/menu_tags.c opens it in that one's place while Menu style
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
  The server browser's badges (badge_*.png: the words VITA and PC on a plate,
  a padlock beside them) and the value arrows (arrow_*.png) are drawn there
  too, no maker's logo. The look follows Halo 3's screens; nothing of Bungie's, Microsoft's or
  Halo Online's is in this folder.
- The level pictures, the difficulty emblems, the multiplayer maps' pictures
  (mp_map_grafix) and descriptions are ui.map's own (<frame map=...>,
  string_list="ui\...").
- The game's menus draw a picture 1:1, cropped to its widget, or stretched
  when the widget is larger (ui_widget.c draw_bitmap_in_rect): each PNG is at
  most its widget's size, and a widget draws a line of text only when the
  font's line fits its height.

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create
h3mptouch.
