OpenCE's multiplayer screens (app0:menus/)

When the maps folder holds Halo PC's (Halo Custom Edition's) bitmaps.map and
loc.map, the main menu's Multiplayer opens these screens instead of the Xbox's
Multiplayer screen: the PC menus' Multiplayer screen (JOIN GAME: INTERNET, LAN,
JOIN BY CODE; CREATE GAME: INTERNET, LAN; CO-OP CAMPAIGN, SPLIT SCREEN, EDIT
GAMETYPES), the Server Browser with its password screen, Server Setup (Create
Game > Internet) and Direct Link (join by code). Without those files, or if
anything here or in them cannot be read, the Xbox's menus are as ever.

What ships here, and what comes from the player
- The XML (menus.txt lists the files): the screens' layout, after OpenCE's PC
  menus (OpenCommunityEdition/OpenCE, CC0: c9ee319a and its port_settings.py
  screens). Its own lines of text (descriptions, help, labels such as JOIN BY
  CODE and VISIBILITY) are this port's and OpenCE's.
- header_server_browser.png, header_password.png, header_direct_link.png:
  OpenCE's own art (its port_svg headers, CC0, by MrBruh, the titles in its
  OpenCE-Regular font, SIL OFL), drawn at 512x64.
- Every Bungie picture and line of text is the player's own: <bitmap
  resource="ui\..."> is read from the maps folder's bitmaps.map, <strings
  resource="ui\..."> from its loc.map (both Halo Custom Edition resource maps,
  as in Halo: MCC's halo1/maps/custom_edition or a Custom Edition install). The
  fonts and the button key are the Xbox ui.map's. Nothing of Bungie's is in
  this folder.

The format is OpenCE's (port/assets/menus/README.md in its repository), read
by port/linux/src/menu_files.c and built by port/linux/game/menu_tags.c, with
resource= added on <bitmap> and <strings>; the functions the screens run are
port/linux/game/menu_functions.c's.

Menu style Halo CE's additions (classic/)
- The new features in the original menus' look: the Xbox screens of ui.map
  (and of a level's map: the multiplayer pause) with the port's rows, built as
  OpenCE's are. A <widget extends="ui\..."> is a copy of the map's widget's
  look (its picture, font, colour, bounds and children) with what the element
  gives; with classic_patch="true" it is the map's widget itself while Menu
  style is Halo CE, and the map's own again while it is Halo 3 (menu_tags.c
  swaps the tag's definition as each screen opens), so the Xbox screen keeps
  its name, history and look and the Halo 3 menus are as they were.
- main.xml: the main menu's FORGE, FRIENDS (its PARTY row; the friend code
  and its backup), FILE SHARE and MOD MANAGER under SETTINGS, each opening the
  port's screen (halo3/forge_lobby.xml, friends.xml, fileshare.xml, mods.xml).
- profile.xml: SETTINGS' profile settings' MORE OPTIONS (the player model,
  the flaming helmet and the rest: halo3/customize.xml's editor).
- pregame.xml: the network game's lobby, Y PLAYERS and the right trigger's
  LOBBY OPTIONS (PLAYERS, INVITE FRIENDS, MAP VARIANT).
- pause.xml (ingame.txt): the multiplayer pause's PLAYERS and PARTY.
- OpenCE's Multiplayer screen's GAMETYPES PLUS (multiplayer.xml: the
  Halo 3 GAME TYPES, Infection's rules too) and Server Setup's BOTS
  (setup.xml) are theirs.
- No Bungie picture or line of text is added: the new rows' words are the
  port's (translated, lang/), drawn in the map's own fonts or as pictures of
  the port's own (tools/classic_menu_art.py, Saira Semi Condensed, SIL OFL):
  the main menu's new rows and LOBBY OPTIONS's heading in the look of the
  game's, one set per language - in English the main menu's rows are made,
  as the menus load, of the very letters of the player's ui.map's own
  CAMPAIGN, MULTIPLAYER, SETTINGS and GAME DEMOS (port/linux/game/
  menu_words.c, kept in the data folder; the pictures here the fallback) (classic/en/, classic/es/: "{lang}" in a
  <frame png=...> is the language chosen's, English's when it has none), and
  the multiplayer pause's box (classic/pausebox.png); the other pictures are
  the map's own or the player's Halo PC data's, as above.

Menu style Classic+'s screens for the port's features (classic/forge.xml, ...)
- A widget named cp/<name> stands for the Halo 3 set's h3/<name> while Menu
  style is Classic+ (menu_tags.c menu_tags_online, as online/<name> does for
  ElDewrito): the same rows, found by their names by menu_functions.c's
  functions, laid out as the Xbox's Multiplayer screen and OpenCE's - the
  heading, the rows at the left in ui.map's large font, the description's
  panel at the right, the button key. The pictures and fonts are the
  player's (ui.map, Halo PC's bitmaps.map: art.xml); the headings are the
  port's words drawn in the game's look (tools/classic_menu_art.py).
- Menu style Classic (HALO_MENU_STYLE=original) shows none of this: the
  Xbox menus as they shipped; the settings panel has the port's features.
