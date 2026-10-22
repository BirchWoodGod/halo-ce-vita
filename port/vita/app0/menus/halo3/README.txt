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
  ONLINE CO-OP (internet play: Connection Online), SYSTEM LINK CO-OP or AD
  HOC CO-OP (the ad hoc group joined first, network.xml) - the settings
  panel's Connection, switched live, its status under NETWORK - makes START
  GAME host it as co-op, as the Xbox's difficulty
  list's Y did, and open the game's lobby, the waiting screen; the others
  join from MULTIPLAYER (BROWSE GAMES, JOIN WITH CODE).
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
  MULTIPLAYER LOBBY with NETWORK (ONLINE: internet play; SYSTEM LINK: the Vitas
  on the same Wi-Fi; AD HOC: the Vitas near, the connection Ad hoc; OFFLINE:
  split screen), GAME and MAP (pickers: the game type and map START GAME
  hosts with), START GAME (ONLINE, SYSTEM LINK and AD HOC: the profile, then
  the game hosted on the lobby's GAME and MAP as the Xbox's lists would set
  them, and its lobby, pregame.xml - no map or game type list; OFFLINE: split
  screen's), GAME OPTIONS on ONLINE (Square: Create Game's name, max
  players, public or private and password, the settings panel's Play page's
  too, which START GAME hosts with),
  EDIT GAME TYPES (gametypes.xml, below), BROWSE GAMES (the profile, then the games
  browser, network.xml: ONLINE the System Link and the online games in one
  list) and JOIN WITH CODE; ONLINE, SYSTEM LINK and AD HOC are the
  settings panel's Connection (port/linux/game/network_connection.c),
  switched live when chosen, its status in a line under NETWORK (Online,
  Connecting..., Same Wi-Fi, Ad hoc, No Wi-Fi); the map's picture, "<game> on <map>", "1 Player (N max)" and the
  nameplate.
  The lobby is live (menu_functions.c h3_live, Halo 3's model): on ONLINE,
  SYSTEM LINK or AD HOC, with a profile (the nameplate's, or the one chosen
  this run) and the network ready (Online's broker reached, the Wi-Fi up,
  in the ad hoc group), its game is hosted the moment the lobby is up and
  its scene stands, with its GAME and MAP: friends, the party, a code, the
  games browser (a public game) join it while the host chooses, through
  every check a join has. The lobby shows its players (PLAYERS n/max, the
  game's lobby's rows) instead of the nameplate; GAME and MAP changed go to
  every joiner's lobby as the Xbox sent a game's settings between two
  games (every build's client, 1.1.0's too); GAME OPTIONS' name and most
  players at once, public or private and the password its listing. L opens
  INVITE FRIENDS online (friends.xml). START GAME opens the game's lobby
  on that game at once (no profile asked, no other server), its countdown
  begun once the game can start (it waits for every machine's map, map
  sharing included); B there goes back to the lobby, the game kept, the
  countdown stopped. B on the lobby, the main menu, another NETWORK, BROWSE
  GAMES or JOIN WITH CODE close it: its joiners are told the host left.
  Not live (START GAME hosts as before): OFFLINE, the network not ready,
  crossplay on, no profile at all, a map not played with others. The browser, Create Game and JOIN WITH CODE run OpenCE's screens'
  functions (menu_functions.c "port browser/setup/code ...") under the same
  widget names; the browser's rows show a VITA or PC badge with a lock for a
  password. When internet play is off, ONLINE's rows open a dialog saying how to
  turn it on.
- gametypes.xml: GAME TYPES, the lobby's EDIT GAME TYPES (it stands for the
  Xbox's game type list, playlist_select\gametype_select_screen, and its edit
  screens): the player's saved game types (z:\saved\playlists' files, as
  the Xbox's list reads them), then the game's own; CREATE NEW GAME TYPE and
  Triangle make one from a base (one of the game's own); Cross on one opens
  its options - EDIT, COPY, RENAME (its name typed), DELETE (after a
  question); the game's own have EDIT and COPY, which make one of the
  player's -, Square deletes it. The editor: NAME, GAME (Slayer, CTF,
  Oddball, King of the Hill, Race), then the settings in Halo 3's groups -
  GENERAL (score to win, lives), RESPAWN (respawn time, its growth, suicide
  penalty), PLAYER (shields, maximum health, invisible players, odd man out,
  starting equipment, objective indicator, players on radar, friend
  indicators), WEAPONS AND VEHICLES (weapon set, infinite grenades, vehicle
  set), TEAM (teams, Race's team scoring) and the game's own (Slayer's death
  bonus, kill penalty, kill in order; CTF's assault, single flag, flag must
  reset, flag at home to score; Oddball's ball type, balls, random start,
  speed and traits; King's moving hill; Race's type) -, each value stepped
  through the values the Xbox's edit screens offer; SAVE writes it with the
  game's own functions (playlist_profile_new, playlist_profile_save: the
  same file, checksum and SaveMeta.xbx name), the lobby's GAME then on it;
  Circle with changes asks whether to save them. A game type holds no time
  limit, friendly fire or betrayal penalty (a PC host's options), and the
  help says so. A game type of the player's whose file fails its checksum
  (damaged, or edited outside the game) is DAMAGED in the list - EDIT, COPY
  and RENAME refused, Square deletes it - and left out of the lobby's GAME.
  Menu style Halo CE keeps the Xbox's screens.
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
- friends.xml: FRIENDS (the main menu's last item; Triangle in the
  multiplayer, campaign and Forge lobbies), Halo 3's friends list over
  port/linux/src/p2p_friends.h: this Vita's friend code at the top, ADD
  FRIEND (a code typed on the Vita's keyboard), APPEAR OFFLINE and FRIENDS
  CAN JOIN (the settings panel's rows), then the requests, the invites, the
  friends (online first, what each is doing - "Slayer on Blood Gulch",
  "Co-op: The Pillar of Autumn, Legendary", "In Forge" - and JOIN when their
  game can be joined), the requests sent and the blocked players. Cross does
  a row's first thing (Accept, Join, type a code), Square its second
  (Decline, Invite, Ignore, Cancel), Triangle its options (Remove, Block,
  Unblock) after Halo 3's question; a join goes on as JOIN WITH CODE's.
  Each lobby shows a FRIENDS panel at the right, under the players: who is
  online and what they are doing, and the requests and invites waiting
  (with no one to show, the friends list's line across the panel, in two
  lines when one is too narrow). A
  friend's invite asks at the menus with the game's question (Accept the
  invite and join now?). The settings panel's Multiplayer > Friends does the
  same for the original menus.
- players.xml: PLAYERS, Halo 3's roster of the network game this Vita plays
  (Triangle in the game's lobby, the waiting screen; on the carnage report;
  with the game's own scoreboard up, Back held - with the Halo 3 scoreboard
  Triangle opens that instead, below -): each player's name, team and what
  friends knows of them. Cross on another machine's player: its options,
  ADD FRIEND (a friend request, no code typed: the two machines exchange
  friend cards over internet play first, only when the other player has
  friends on and does not appear offline) and BLOCK. Listed in menus.txt and
  ingame.txt, so it uses only what both sets have.
- network.xml: the games browser (BROWSE GAMES, after the profile: the
  server browser's screen over the games the Xbox's System Link list would
  show, the game's own search and join - SYSTEM LINK GAMES, AD HOC GAMES -
  and, with the connection Online, the online list's public games too, in
  one list, CUSTOM GAMES, the System Link ones marked [LAN]; padlocks for
  the online games' passwords, System Link has none) and the joining dialog
  (AD HOC out of the ad hoc group: the system's dialog joins it, then what
  was asked goes on).
- Nothing is asked twice: once a profile is chosen alone on SELECT PROFILE
  (or the campaign's), a network game's SELECT PROFILE is not shown again
  in that run (menu_functions.c h3_session: the profile set at once, as its
  Cross would, its checks kept; asked again if the profile is gone or taken,
  and split screen's always asks), and the games a code, a friend, an
  invite or the party join are joined from the games browser (network.xml),
  not the Xbox's System Link list.
- join.xml: SELECT PROFILE for a multiplayer game, standing for the Xbox's
  four-way "press START to join" screen of System Link (START GAME and BROWSE
  GAMES on every network, Create Game, JOIN WITH CODE, the panel's Join a
  game; join_split.xml for split screen's): four players' plates, one a
  controller's - Cross joins, left and right choose the profile, Cross sets
  it (the Xbox's functions and checks), Cross once all are ready goes on;
  Circle un-readies, leaves, then goes back.
- pregame.xml: the pre-game lobby, standing for the Xbox's ENLISTED PLAYERS
  (co-op's waiting screen and multiplayer's): the mission's picture, name
  and difficulty, or the map's and "<game type> on <map>"; the status and
  countdown; the code, large, online; public or private and Square's hint;
  the players in their colours with their teams. Its buttons are the Xbox
  screen's own functions; L, the host's online, INVITE FRIENDS.
- friends.xml's INVITE FRIENDS (L in the live lobby and the game's lobby,
  the host's online): the friends online, Cross sends the chosen one the
  friends' invite into this Vita's game (sealed for them alone, one a
  friend a while; a game with a password or full takes none, said); the
  settings panel's Friends page does the same with Square on a friend.
- password.xml: the password screen of a game with one (it stands for OpenCE's,
  pc\join\password\screen, which the browser opens).
In each level's map (ingame.txt):
- pause.xml: the pause screen (ui\shell\solo_game\pause_game\pause_game).
- mppause.xml: the multiplayer pause screen of a player alone on their
  machine (ui\shell\multiplayer_game\pause_game\1p_pause_game; split
  screen's 2p and 4p screens, each player's own, stay the Xbox's), in
  pause.xml's look: the game type and map in the title strip, the score to
  win, RESUME, PLAYERS (players.xml), SETTINGS (the profile's controls as
  CUSTOMIZATION's, saved on Circle and played at once, and ALL SETTINGS:
  the settings panel), PARTY in a party (the settings panel's Party page),
  LEAVE GAME for a machine that joined or END GAME for the host's, each
  asked first, then the Xbox's own quit ("mp game player quit", whatever
  the network: system link, online, a dedicated server's or a PC's game).
  It never pauses the game, as the Xbox's does not; no CHANGE TEAMS (the
  Xbox changes teams in the game's lobby only); a Forge game's Start opens
  the Forge menu instead (menu_tags.c).
- forge.xml: Forge's screens over the game: the object browser (Square, as
  the monitor), the Forge menu (Start in a Forge game) and its question
  before leaving.
- carnage.xml: the CARNAGE REPORT, Halo 3's results at a multiplayer game's
  end (local, System Link, ad hoc and online; every game type), opened over
  the game once its scores come up, in the place of the game's own report
  (game_engine.c game_engine_post_rasterize_post_game, drawn with Menu style
  Halo CE, or menus.carnage_report "classic" - the settings panel's Game >
  Menus > Carnage report Classic): the winner, the players
  ranked as the game ranks them (a team game's under their team's row - its
  score, its players' sums -, the winning team first), each on a nameplate
  in their colour or their team's, this machine's rows lit, the winners
  marked. L and R: SUMMARY (the game type's score - kills, flags, the
  ball's or the hill's time, laps -, kills, deaths, assists, +/-) and
  DETAILS (suicides, betrayals in a team game, the game type's own - flag
  grabs and returns, kills with the ball and ball carriers killed, the best
  lap -, K/D); up and down scroll a long list. Cross: the host goes on to
  the lobby with everyone (by itself after 30 s on the report); a machine
  that joined waits for the host, Circle leaves the game (asked first).
  Triangle: PLAYERS. Its numbers are the game's own record
  (port/linux/game/carnage_report.c), on a joined machine the host's as the
  distributed netcode sends them: every machine shows the same; nothing is
  counted for it but betrayals, which are a teammate's death only now (a
  player's own was counted as one too: game_statistics.c).
- scoreboard.xml: the SCOREBOARD, Halo 3's, in the place of the game's own
  scores while Back (the Vita's SELECT) is held in a multiplayer game
  (menu_functions.c "port h3 scoreboard"; menus.scoreboard "classic" - the
  settings panel's Game > Menus > Scoreboard Halo CE - or Menu style Halo CE:
  the game's own): local and split screen, System Link, ad hoc, online, a
  dedicated server's and a PC host's games, each local player's own in their
  view (split screen's the smaller scoreboard_split). Held, it is drawn over
  the game without being open (ui_widget.c ui_widget_port_held_screen_show):
  the player plays on, no button is taken. It is a compact panel at the top
  of the screen (about 45% of its width), as Halo 3's: "Players" and "Score"
  over the rows; a team game's teams each a translucent band of the team's
  colour with its rank, name and score (the leading team first), its players
  under it in the same colour; a free for all's players in one translucent
  list, as the game ranks them. A row: the emblem (the player's colour,
  their team's in a team game), the name, the tags - the host's crown, the
  party's mark, the tester's small flame (only where the flame itself shows:
  its proof checked here), BOT on a bot's, the ping's bars (the latency
  meter's; the host's table on a joined machine) - and the score; this
  machine's rows outlined in yellow; the dead and those out of lives dimmed;
  the score to win and the time left over it, "Hold [Triangle] to activate"
  under it; no kills, deaths or assists held. All sixteen players are held
  (eighteen compact rows with a team game's two bands); with more, the leaders
  and the player's own row and "+N more - Hold [Triangle] to open". Opened it
  is the same panel a little wider, with kills, deaths and assists, the game
  type on the map, a small selector row under it (the emblem and name of the
  player Cross acts on; the D-pad's up and down arrows) and the buttons in one
  light line. Triangle while it is held (one player on the
  machine) opens it as a screen: the
  D-pad, the stick, L and R, a drag on the touch screen scroll it, Cross (a
  tap) on a player opens their options - MUTE CHAT, ADD FRIEND (VIEW FRIEND
  for a friend), KICK for the game's host alone (network_anticheat_drop,
  which refuses any machine not hosting; a dedicated server's players kick no
  one, its admin does) -, Circle or Back again closes it. Its rows are read a
  few times a second and its text set only when they change.
In both (menus.txt and ingame.txt):
- dialog.xml: the Halo 3 dialog, every message box and question the game
  shows - a title, the text, up to three choices in rows, Cross Select and
  Circle Back in the strip (the button icons setting's), over what is behind,
  dimmed (dim.png). dialog_ui.xml (ui.map) and dialog_ingame.xml (a level's
  map) make its screens stand for the Xbox's boxes (ui\shell\error\...): the
  errors (display_error: connection lost, the host's game closed, join
  failures, damaged files, profile and save errors, storage full, the port's
  own error texts), the errors that end the game (CONTINUE or QUIT GAME),
  the pause screen's REVERT TO CHECKPOINT and RESTART MISSION questions
  (split screen's halves and co-op's QUIT too), SAVE AND QUIT's "Saving",
  the main menu's "Saving" and "Creating your profile", the Xbox's delete
  profile and delete game type questions; their handlers are the Xbox
  boxes'. The platform's own message and yes or no question (map sharing,
  the friend and party invites, the crossplay check, Forge's notes) open it
  too (menu_functions.c h3_dialog_port_update), over the screen showing or
  the game. "port h3 dialog" gives each its title and text, the port's own
  words in the player's language. Split screen's quarter screens' errors
  stay the Xbox's. Menu style Halo CE keeps the Xbox's boxes and the
  platform's.
Each <menus root=... replaces="<the Xbox screen>"> (or <screen root=...
replaces=...>, several in a file) names the screen it stands
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
  key_atlas.png and xbox_atlas.png are the desktop build's only (left out of
  the Vita's package: tools/vita_build.py): the keyboard's keycaps, the mouse and the Xbox controller's
  buttons, which the PC shows in their place, the Halo 3 menus' hints among
  them, with display.button_icons (port/linux/include/halo_button_icons.h
  lists the keys).
  The server browser's badges (badge_*.png: the words VITA and PC on a plate,
  a padlock beside them) and the value arrows (arrow_*.png) are drawn there
  too, no maker's logo. The profiles' colour swatches (swatch_*.png: the
  game's eighteen player colours, and swatch_random.png for none) and the
  carnage report's winners' mark (carnage_won.png, a gold chevron) too. The look follows Halo 3's screens; nothing of Bungie's, Microsoft's or
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
and the Forge lobby have theirs; the screens opened from them keep it, the
game's lobby (pregame.xml) of a game hosted or joined from one too: a network
game's lobby stops the main menu's ticks, so the scene stands there as it was,
whole (a scene not made by then is none until the game is over). SELECT
PROFILE and CUSTOMIZATION's profile list have the editor's (customize.xml).

What stands in a lobby's scene, as Halo 3's lobbies have their scenes, is said
by the screen's scene_object= (port/linux/game/menu_scene_objects.c, whose
comment has the whole syntax), one entry after another, ';' between them:

  <pack>[:<tag>] [<distance> <right> <up> <facing>] [<option>=<value> ...]

an object of a scene pack (app0:packs/<pack>.txt: its recipe's tag whose name
ends in <tag>, '_' for a space) placed in the camera's frame (world units:
along the view, right of it, up in the picture; facing in degrees from facing
the camera, or the entry it looks at), and its options: pitch= and roll=
(degrees: a Warthog's nose up; banked to its left), color= (a multiplayer colour, 0 white, or
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
  its nose up and wheels hanging, banked to its left so that the Chief shows,
  a Marine on its gun and one beside him
  (menu_campaign: from the player's a30, b30, b40 or a10).
- multiplayer.xml: three white Spartans - jumping with a rocket launcher (a
  held frame of its airborne animation), holding the flag, crouching with a
  sniper rifle (menu_multiplayer: from the player's multiplayer maps).
- forge_lobby.xml: 343 Guilty Spark behind a Warthog, looking at it (menu_monitor: from
  the player's c20, c10, c40 or d40; the Warthog menu_campaign's).
- main.xml: the ring's own flight, nothing in it.
The packs are built on the machine from the player's own maps, into the save
folder's t/packs, checked as any pack, all of it on the packs' worker thread
(port/linux/game/scene_packs.c: the game thread only puts a checked pack in);
once the main menu has rested 2 s every scene's pack and the card's are
prebuilt in the background and loaded (HALO_MENU_PREBUILD=0: built the first
time a lobby wants them). A scene is shown once its packs' textures are read.
The multiplayer scene's flag flies in a breeze of the scene's
(HALO_MENU_FLAG_BREEZE). A scene is made whole once all its packs are
loaded, or not at all: no source map (a scene's pack names the maps it may be
built from), HALO_SCENE_PACKS=0, the scenes off or Menu style Halo CE, and the
lobby has no scene; nothing else changes.

SELECT PROFILE's card (and CUSTOMIZATION's list's) shows the focused profile's
Spartan in the panel's window (profiles_panel.png), in the profile's colour and
the player model chosen for it, following the focus; CUSTOMIZATION's editor
shows the same Spartan, recoloured live as COLOR changes
(menu_scene_spartan_show/_color/_hide, halo_menus.h; menu_functions.c
h3_profiles_spartan, h3_custom_spartan_preview). Without a pack, or with the
scenes off, the colour's swatch stays. A model is taken once the choice has
rested on it 300 ms (stepping through MODEL loads none of the models passed),
and the pack of the one left is let go (scene_packs_release), so any number
of models can be looked at in one visit.

Touch: the Vita's front screen points in the menus (a finger highlights, a tap
presses A on the item or the button of a key); the button icons are named as
the game's keys (a_butn, b_butn, start_butn) for that.

Tests: port/vita/tests/run_menu_speed_test.sh (no stall, the prebuild);
port/vita/tests/run_menus_test.sh h3probe h3classic h3xbox h3broken
h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create
h3mptouch h3coop h3coopguide h3scenes h3lobbyscenes h3modelsmany h3profiles h3proftouch h3customize h3models
h3nameplate h3damaged h3mods h3modspicker h3modstouch h3mpcustom h3lan h3adhoc h3adhoccoop h3oddrows h3friends h3friendspanel
h3friendsfit h3mppause h3carnage h3scoreboard; the packs':
port/vita/tests/run_scene_pack_test.sh; Forge's: port/vita/tests/run_forge_test.sh,
port/vita/tests/run_forge_files_test.sh; between two machines:
port/vita/tests/run_netns_online_test.sh h3coopmenu h3lan h3adhoc h3online h3mppause carnage h3scoreboard netswitch, and friends'
friends friendsdecline friendsblock friendsinvite friendsoffline friendsforged.
