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
