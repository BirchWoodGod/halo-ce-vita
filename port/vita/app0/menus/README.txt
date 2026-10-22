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
  port's (translated, lang/), drawn in the map's own fonts; the pictures are
  the map's own or the player's Halo PC data's, as above.
