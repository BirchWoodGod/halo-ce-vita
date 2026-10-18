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
  ONLINE CO-OP (internet play: Connection Online) or SYSTEM LINK CO-OP (AD
  HOC CO-OP with the connection Ad hoc) makes START GAME host it as co-op,
  as the Xbox's difficulty list's Y did, and open the game's lobby, the
  waiting screen; the others join from MULTIPLAYER (BROWSE GAMES, JOIN WITH
  CODE).
- profiles.xml: SELECT PROFILE, standing for the Xbox's campaign profile
  screen (ui\shell\main_menu\load_campaign_player_profile, which CAMPAIGN
  opens; profiles_coop.xml for the screen inside it, which the settings
  panel's Host co-op campaign opens): the profiles, each with its colour,
  then CREATE NEW PROFILE (its name typed on the Vita's keyboard); the
  chosen one's card (colour, name, current level, difficulty, controls, as
  the Xbox's card). Cross plays it (the campaign lobby), Square deletes it
  after a question, Triangle makes a new one. A profile whose file is
  damaged (a 0-byte blam.sav) is said to be so and is not played, as the
  Xbox refuses it; a damaged saved game is said in the lobby.
- customize.xml: CUSTOMIZATION, standing for the Xbox's player profile
  settings (player_profile_select_screen and its edit screens): the
  profiles as above, then the chosen one's editor - PLAYER NAME, COLOR
  (the game's eighteen, or RANDOM: the host picks), STICK LAYOUT, BUTTON
  LAYOUT, LOOK SENSITIVITY, INVERT LOOK, INVERT FLIGHT, AUTO-CENTER,
  VIBRATION, the Xbox profile's fields - with the scene packs' Spartan at
  the right, in the profile's colour and recoloured live as COLOR changes,
  in front of the editor's own camera (menu_functions.c
  h3_custom_spartan_preview; the colour's swatch instead when there is no
  pack or the scenes are off). Circle saves the changes to the profile, as
  the Xbox's SAVE CHANGES did. The settings panel's Controls tab keeps the
  PS Vita's own (look speed, invert, which Vita button is each Xbox button):
  they act on top of the profile's, and the help says so.
  Both read and write the profiles with the Xbox screens' own functions
  (blam.sav, SaveMeta.xbx): menu_functions.c "port h3 profile/custom ...".
- forge_lobby.xml: the Forge lobby FORGE opens, with its Select Map picker
  (the multiplayer maps of the maps folder) and START FORGE, which starts a
  local game of the map in Forge mode (port/linux/game/forge.c).
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
  glyph_atlas.png is no screen's: the same button icons at twice their size
  (and SELECT and the D-pad's directions), which the game draws in place of
  the Xbox's in play and in the Halo CE menus when the settings panel's
  Button icons is PlayStation (source/interface/hud_draw.c).
  The server browser's badges (badge_*.png: the words VITA and PC on a plate,
  a padlock beside them) and the value arrows (arrow_*.png) are drawn there
  too, no maker's logo. The profiles' colour swatches (swatch_*.png: the
  game's eighteen player colours, and swatch_random.png for none) too. The look follows Halo 3's screens; nothing of Bungie's, Microsoft's or
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

Characters - PARKED for now (the owner, beta.7: "the Spartans look off, later"):
the lobbies stand no Spartans; the code, the scene packs and the recipes stay,
and CUSTOMIZATION's editor still shows its preview Spartan. To bring them back,
put these attributes back on the lobby screens (as last tuned), and run
run_menus_test.sh h3characters again (it skips itself while they are out):
  campaign.xml     h3/campaign/screen  scene_object="menu_spartan 1.15 0.38 -0.6 -15; menu_spartan 0.95 0.43 -0.6 20"
  multiplayer.xml  h3/mp/screen        scene_object="menu_spartan 0.9 0.30 -0.6 20"
  forge_lobby.xml  h3/forge/lobby      scene_object="menu_spartan 0.9 0.30 -0.6 20"

A screen with a scene= may stand characters in front of its camera,
scene_object="<scene pack> <distance> <right> <up> <facing> [; ...]" (world units
in the camera's frame, degrees turned from facing it): the pack's first tag (a
biped) holding its weapon, in the player's multiplayer colour
(port/linux/game/menu_scene_objects.c). The pack, menu_spartan
(app0:packs/menu_spartan.txt: the multiplayer cyborg and its assault rifle), is
built on the machine from the player's own multiplayer map the first time a
lobby wants it (port/linux/game/scene_packs.c), into the save folder's t/packs.
The campaign lobby had two, the multiplayer and Forge lobbies one. No pack, no
multiplayer map, HALO_SCENE_PACKS=0 or the scenes off: no character, nothing
else changes. menu_scene_spartan_show/_color/_hide (halo_menus.h) put the same
Spartan in a scene, in a colour: CUSTOMIZATION's editor shows it. The
characters stand upright in the picture whatever the camera's pitch (placed
in the camera's frame: "up" is up in the picture), in a calm stance (weapon
low, head to the camera) and lit by a light of their own (a key from the
upper left, a cool rim, an ambient: ui.map's space has no lightmap under
them). There is no floor: the lobbies' framing cuts them at the shins.

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create
h3mptouch h3coop h3coopguide h3scenes (h3characters: parked) h3profiles h3proftouch h3customize
h3damaged; the packs': port/vita/tests/run_scene_pack_test.sh; Forge's: port/vita/tests/run_forge_test.sh;
co-op between two machines: port/vita/tests/run_netns_online_test.sh h3coopmenu.
