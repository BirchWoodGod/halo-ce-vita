The Halo 3-style menus (app0:menus/halo3/)

With Menu style Halo 3 (settings panel: Graphics > Screen; HALO_MENU_STYLE=halo3)
these screens stand for the Xbox's own, over the same animated Halo ring:
- main.xml: the main menu (ui\shell\main_menu\main_menu) -
  CAMPAIGN, MULTIPLAYER, FORGE (its lobby, below), FILE SHARE ("in a later
  update" for now), CUSTOMIZATION, Settings on Start, the HALO title, the
  player's nameplate and the live servers (ONLINE NOW).
- campaign.xml: the campaign lobby (it stands for the level select the
  campaign's profile screen opens) with its Select Network, Select Mission
  and Select Difficulty pickers. NETWORK: LOCAL plays the level here;
  ONLINE CO-OP (internet play: Connection Online), SYSTEM LINK CO-OP or AD
  HOC CO-OP (the ad hoc group joined first, network.xml) - the settings
  panel's Connection, switched live, its status under NETWORK - makes START
  GAME host it as co-op, as the Xbox's difficulty
  list's Y did, and open the game's lobby, the waiting screen; the others
  join from MULTIPLAYER (BROWSE GAMES, JOIN WITH CODE).
- forge_lobby.xml: the Forge lobby FORGE opens, with its Select Map picker
  (the multiplayer maps of the maps folder) and START FORGE, which starts a
  local game of the map in Forge mode (port/linux/game/forge.c).
- multiplayer.xml: the multiplayer lobby (it stands for the Xbox's Multiplayer
  screen, as OpenCE's multiplayer.xml does with Menu style Halo CE):
  MULTIPLAYER LOBBY with NETWORK (ONLINE: internet play; SYSTEM LINK: the Vitas
  on the same Wi-Fi; AD HOC: the Vitas near, the connection Ad hoc; OFFLINE:
  split screen), GAME and MAP (pickers: the game type and map the Xbox's lists
  open on after START GAME), START GAME (ONLINE: Create Game; SYSTEM LINK and
  AD HOC: the profile, then the new game's map list; OFFLINE: split screen's),
  EDIT GAME TYPES (the Xbox's), BROWSE GAMES (ONLINE: the server browser,
  CUSTOM GAMES; SYSTEM LINK and AD HOC: the profile, then the LAN browser,
  network.xml) and JOIN WITH CODE; ONLINE, SYSTEM LINK and AD HOC are the
  settings panel's Connection (port/linux/game/network_connection.c),
  switched live when chosen, its status in a line under NETWORK (Online,
  Connecting..., Same Wi-Fi, Ad hoc, No Wi-Fi); the map's picture, "<game> on <map>", "1 Player (N max)" and the
  nameplate. The browser, Create Game and JOIN WITH CODE run OpenCE's screens'
  functions (menu_functions.c "port browser/setup/code ...") under the same
  widget names; the browser's rows show a VITA or PC badge with a lock for a
  password. When internet play is off, ONLINE's rows open a dialog saying how to
  turn it on.
- network.xml: the lobby's local networks' screens - the LAN browser (SYSTEM
  LINK GAMES or AD HOC GAMES: the server browser's screen over the games the
  Xbox's System Link list would show, the game's own search and join; no
  padlocks, System Link has no passwords) and the joining dialog (AD HOC out
  of the ad hoc group: the system's dialog joins it, then what was asked
  goes on).
- password.xml: the password screen of a game with one (it stands for OpenCE's,
  pc\join\password\screen, which the browser opens).
In each level's map (ingame.txt):
- pause.xml: the pause screen (ui\shell\solo_game\pause_game\pause_game).
- forge.xml: Forge's screens over the game: the object browser (Square, as
  the monitor), the Forge menu (Start in a Forge game) and its question
  before leaving.
Each <menus root=... replaces="<the Xbox screen>"> names the screen it stands
for (or OpenCE's, for the password screen); port/linux/game/menu_tags.c
opens it in that one's place while Menu style is Halo 3. Like OpenCE's screens in the folder above, they are built when
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
  font's line fits its height. A PNG's width and height are powers of two
  (menu_tags.c refuses another: its texture would be read wrong).

Scenes: a screen's scene= puts a camera of its own behind it (Halo 3's
lobbies have one): scene="ring" (the main menu) is ui.map's own flight
(ring_loop), scene="<point> x y z yaw pitch [ticks]" writes that camera
(world units and degrees) into one of ui.map's cutscene camera points that
its script never flies (load_campaign, settings, multiplayer, new_campaign)
and glides there, ring_loop asleep (menu_functions.c h3 scenes; menus.scenes,
HALO_MENU_SCENES=0 turns them off). The campaign lobby, the multiplayer
lobby and the Forge lobby have theirs; the screens opened from them keep it.

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create
h3mptouch h3coop h3coopguide h3scenes h3lan h3adhoc h3adhoccoop; Forge's:
port/vita/tests/run_forge_test.sh; between two machines:
port/vita/tests/run_netns_online_test.sh h3coopmenu h3lan h3adhoc h3online
netswitch.
