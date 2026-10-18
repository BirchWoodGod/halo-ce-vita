The Halo 3-style menus (app0:menus/halo3/)

With Menu style Halo 3 (settings panel: Graphics > Screen; HALO_MENU_STYLE=halo3)
these screens stand for the Xbox's own, over the same animated Halo ring:
- main.xml: the main menu (ui\shell\main_menu\main_menu) -
  CAMPAIGN, MULTIPLAYER, FORGE (its lobby, below), FILE SHARE ("in a later
  update" for now), MOD MANAGER (mods.xml, below), CUSTOMIZATION, Settings on
  Start, the HALO title, the
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
  VIBRATION, the Xbox profile's fields, and MODEL (after COLOR: the Spartan,
  or a player model found in the player's Halo Custom Edition maps,
  port/linux/game/player_models.c, built the first time it is shown, played
  in offline multiplayer games and Forge; kept in the port's own
  t:\player_models.txt, not the profile) - with the scene packs' Spartan at
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
- mods.xml: the MOD MANAGER the main menu opens, beside FILE SHARE (where
  Halo 3 keeps the player's content; it covers campaign and multiplayer
  content alike, so no lobby holds it): Halo 3's file list of the player's
  custom maps (the maps folder's, not the Xbox's own) and MCC mods (the mods
  folder's), each its name, type, size and ON / OFF / FAILED (the game's own
  checks: port/linux/game/mod_manager.c), the chosen one's modes, check,
  needs and source under the list, the space used and free at the top right.
  Cross turns one off or on (out of every map list, the bots' playlist and
  map sharing; kept on the memory card), Triangle opens its details, Square
  asks before deleting it (NO, KEEP IT first). The settings panel's
  Multiplayer > Modded maps > Mod manager does the same.
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
(ring_loop), scene="<point> x y z yaw pitch [ticks [fov]]" writes that camera
(world units and degrees; fov its field of view, the game's own 70 when it is
left out or 0) into one of ui.map's cutscene camera points that its script
never flies (load_campaign, settings, multiplayer, new_campaign) and cuts
there, ring_loop asleep (menu_functions.c h3 scenes; menus.scenes,
HALO_MENU_SCENES=0 turns them off). The campaign lobby, the multiplayer lobby
and the Forge lobby have theirs; the screens opened from them keep it. SELECT
PROFILE and CUSTOMIZATION's profile list have the editor's (customize.xml).

What stands in a lobby's scene, as Halo 3's lobbies have their scenes, is said
by the screen's scene_object= (port/linux/game/menu_scene_objects.c, whose
comment has the whole syntax), one entry after another, ';' between them:

  <pack>[:<tag>] [<distance> <right> <up> <facing>] [<option>=<value> ...]

an object of a scene pack (app0:packs/<pack>.txt: its recipe's tag whose name
ends in <tag>, '_' for a space) placed in the camera's frame (world units:
along the view, right of it, up in the picture; facing in degrees from facing
the camera, or the entry it looks at), and its options: pitch= and roll=
(degrees: a Warthog's nose up), color= (a multiplayer colour, 0 white, or
profile), weapon=[<pack>:]<tag> (in its hands), anim= (its own animation named
after its seat and weapon, "airborne", "idle", or whole with '_' for spaces),
frame= (that frame held; without it looped), stance=crouch, in=<entry>
seat=<label> (seated in an earlier entry's vehicle as the scripts'
unit_enter_vehicle seats one: driver, gunner, passenger), look=<entry> (turned
to it), suspension= (0, hanging as in the air, to 1). A vehicle in a scene is
held still, its physics off (vehicles.c): no dust, no skid, no crash. The
objects stand upright in the picture whatever the camera's pitch and are lit
by a light of their own (a key from the upper left, a cool rim, an ambient:
ui.map's space has no lightmap under them).
- campaign.xml: the Master Chief at the wheel of a Warthog caught mid-jump,
  its nose up and wheels hanging, a Marine on its gun and one beside him
  (menu_campaign: from the player's a30, b30, b40 or a10).
- multiplayer.xml: three white Spartans - jumping with a rocket launcher (a
  held frame of its airborne animation), holding the flag, crouching with a
  sniper rifle (menu_multiplayer: from the player's multiplayer maps).
- forge_lobby.xml: 343 Guilty Spark looking at a Warthog (menu_monitor: from
  the player's c20, c10, c40 or d40; the Warthog menu_campaign's).
- main.xml: the ring's own flight, nothing in it.
The packs are built on the machine from the player's own maps the first time a
lobby wants them (port/linux/game/scene_packs.c), into the save folder's
t/packs, checked as any pack. A scene is made whole once all its packs are
loaded, or not at all: no source map (a scene's pack names the maps it may be
built from), HALO_SCENE_PACKS=0, the scenes off or Menu style Halo CE, and the
lobby has no scene; nothing else changes.

SELECT PROFILE's card (and CUSTOMIZATION's list's) shows the focused profile's
Spartan in the panel's window (profiles_panel.png), in the profile's colour and
the player model chosen for it, following the focus; CUSTOMIZATION's editor
shows the same Spartan, recoloured live as COLOR changes
(menu_scene_spartan_show/_color/_hide, halo_menus.h; menu_functions.c
h3_profiles_spartan, h3_custom_spartan_preview). Without a pack, or with the
scenes off, the colour's swatch stays.

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create
h3mptouch h3coop h3coopguide h3scenes h3lobbyscenes h3profiles h3proftouch h3customize h3models
h3damaged h3mods h3modspicker h3modstouch; the packs': port/vita/tests/run_scene_pack_test.sh; Forge's:
port/vita/tests/run_forge_test.sh, port/vita/tests/run_forge_files_test.sh;
co-op between two machines: port/vita/tests/run_netns_online_test.sh h3coopmenu.
