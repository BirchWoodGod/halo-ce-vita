/*
VITA_SETTINGS.C

The settings panel: press SELECT and D-pad down together (or hold SELECT
and START together for a moment, as before 1.1.0-beta.4), in play
or in the menus, and a panel over the game shows the Vita's settings in
tabs that L and R switch between, laid out as Halo Online's settings (Oct
2026): Game, Controls, Video, Sound and Multiplayer, and Dev once "Show
dev settings" (Game) is on; a tab's rows under headings (Interface, First
person, Updates...). No tab is longer than a screen: the rows few players
change open from a row marked > as a page of their own (Game's Menus: Menu
style and, with the Halo 3 menus, Carnage report; Video's Detail and
Performance; Controls' Button layout, Touch zones and Gyro settings;
Multiplayer's Play and Modded maps; Dev's A/B switches), and circle goes
back.
Up and down choose a line, left and right change it, cross does what an
action line says, circle (or SELECT + down again) closes the panel; the
game sees no buttons while it is open. Each row's value is drawn in a
column of its own (a '\x02' between label and value, vita_gxm.c
menu_build), the selected row's help on one line below the rows and the
panel's buttons on the line under it. Each setting is one of the
environment variables the port already reads (HALO_MODEL_LOD_SCALE...),
kept in ux0:data/haloce-vita/settings.txt (written by a thread of its own:
save) and set before the game starts;
which page shows a row changes nothing of that. A change
bumps halo_settings_generation, and the readers that can take a new value
mid-game read theirs again (the render resolution and the aspect ratio too:
the screen's targets are made again between two frames, d3d8_gxm.c
screen_settings_apply; if the memory for the new size cannot be had, the
panel says the change waits for a restart). Render resolution's Dynamic
lets the GPU's load set the scale, frame by frame, between the Dynamic
minimum (shown while Dynamic is chosen) and 100% (d3d8_gxm.c, the dynamic
resolution).

The release's defaults, the ones measured best on the Vita, are set here
too, under whatever env.txt and settings.txt say.

Everything the panel shows is in the Language row's language (Audio;
lang.c, port/vita/app0/lang: Automatic, the Vita's system language when
there is a file of it, else English), the row's labels, values and help
lines by their English (T() where they are drawn), its other lines and
messages by T() where they are made; a change of language is live. Lines
are measured in characters, not UTF-8's bytes (format_box, the tests'
menu_fits).

The Profile row at the top of Graphics sets the speed-related rows at once
(Performance, Balanced - the defaults - or Quality); it reads Custom when
those rows match none of them (the render resolution and its Dynamic
minimum, on the tab; the model detail, distant objects, shadows, lights,
effects, sun rays, particles and particle density, under Detail; the
scenery and lighting update rates and the AI's think rate, under
Performance; and Audio's sound updates and occlusion). All three
are Dynamic resolution, from 50%, 63% and 75%; Quality is otherwise the
Xbox game's every one but the AI's think rate (Adaptive). Rows marked *
apply after a restart (the sound voices, the network, most dev switches).

Game's Updates heading has the update check (update_check.h):
Check for updates, its first row, asks GitHub in the background and shows
its answer beside the build's version on the lines under it ("Version
1.1.0-beta.3 - Up to date"; a newer version in amber with where to get it),
where a player sees it as the tab opens; once it found one, the next press
downloads and checks it, and the lines say where it is kept for VitaShell
to install (vita_update.h: a build with the update key; one without says
where to get it). Then the rows players change (look,
crouch, gyro aiming, the rear touch guard) and three pages laid out as the
Xbox controller. Button
layout puts each Xbox button (A B X Y, Black, White, the triggers, the
sticks' clicks, Back) on a Vita button of the player's choosing, in play.
Touch zones: each zone (the front screen's top corners and its left and
right edges, the rear pad's halves; vita_controls.c says where they are
and when a finger counts; Rear touch guard keeps the hands holding the
Vita off the rear ones) presses an Xbox button, and the page draws the
zones beside its rows (touch_diagram). Button layout's first row, Button
icons, chooses the game's button icons: Xbox (the game's own; the default,
VITA_BUTTON_ICONS_DEFAULT in vita_controls.h) or PlayStation (the Halo 3
menus' icons), each icon then showing the Vita button its Xbox button is
on now (vita_controls.h, source/interface/hud_draw.c; live, kept by Reset
controls); with PlayStation the panel too names the Xbox buttons by what
they do (Jump, Melee...) and the guide's steps the Vita's buttons. Gyro settings: gyro aiming
(vita_controls.h) turns the view as the Vita turns, on top of the right
stick - Off (the default), On, While zoomed, or While holding the Gyro
button (which then does nothing else in play) - with its sensitivity (1x:
the view turns as far as the Vita), vertical direction and whether turning
is the Vita's yaw or its roll; a line shows the gyroscope's rates now and
whether its bias was learnt (it is, each time the Vita lies still for a
second). Advanced: the sticks' deadzone, Reset controls (look, crouch,
buttons and touch back as shipped; the gyro's rows, Show dev settings and
the Update channel stay), Show dev settings, then the Update channel that
Check for updates looks at (Experimental or Stable; a pre-release build's
Stable greyed out: choice_disabled).

Multiplayer is for playing with other Vitas: Connection, the ad hoc room
(with the connection Ad hoc), Join with a code (see below) and Modded maps,
then lines saying this Vita's name and address and what the game is doing:
looking for games and how many it found, hosting and how many players are
in (of the most the game takes), in another's lobby, in a game; and,
hosting online, the game's short code (ABCD-EFGH) for others to type in.
Hosting, joining and co-op are the game's own Multiplayer menu's: with Halo
PC's bitmaps.map and loc.map in the maps folder, OpenCE's screens (server
browser, Server Setup, Direct Link: port/linux/game/menu_functions.c), else
the Xbox's (System Link, where Y creates a game); then a line says "Online
menus need bitmaps.map, loc.map: README" (halo_pc_menus_state).

- "Connection" chooses how the Vitas reach each other (live: the game
  switches at its menus with no network game, network_connection.c; the
  Halo 3 lobbies' NETWORK is the same setting, and either shows the
  other's choice):
  Same Wi-Fi, the default, the system link that works on hardware; Ad hoc
  (p2p_adhoc.c, vita_net.c; not yet verified on hardware): Vitas side by
  side without a router, in the room chosen on the tab (the group joined
  through the system's ad hoc dialog when the game's System Link screen
  needs it); or Online (internet play, port/linux/src/p2p.c; experimental).
  The game asks for the same (Ad hoc chosen, or AD HOC's rows in the Halo 3
  lobbies: the dialog joins the room's group; leaving Ad hoc leaves it, the
  Wi-Fi back: game_adhoc_requests, system_link_shortcut.h). What the game
  runs is the panel's running_network (halo_network_connection).
- "Join with a code" (Online, on a Vita without the PC menus, whose Direct
  Link does it in the game) types the host's code in with the D-pad, then
  shows Join's steps with how the lookup goes, and Cross opens the game's
  System Link screen (port/linux/game/system_link_shortcut.c), where the
  host's game shows once reached. The steps name the buttons as the game's
  menus do, the Xbox's (A is Cross there, B Circle, X Square, Y Triangle).
- The hosted game's settings (HALO_NET_LOBBY_NAME, its printable ASCII, 15
  characters at most; none until one is typed: the game's own name, its
  player's profile's;
  HALO_NET_MAX_PLAYERS, 2 to 16; HALO_NET_HOST_PUBLIC, OpenCE's name;
  HALO_NET_LOBBY_PASSWORD) are the game's Server Setup's, which sets them as
  this panel sets a row (vita_settings_set) and settings.txt keeps them; the
  Play page that showed them is gone (its rows are kept on a page no row
  opens). So is co-op's own visibility (HALO_NET_COOP_PUBLIC, OpenCE's
  network.coop_public: Private unless chosen), which X on the waiting screen
  of co-op hosted online sets (port/linux/game/coop_menu.c). 1.0.3's
  "Online games" row (HALO_NET_LOBBY_PUBLIC) loads as Visibility; 1.0.3's Co-op page variables load as Off.
- "Online bots" (on the Vita and PC page; HALO_BOTS_ONLINE: Full, the default, Off, or 1 to 13 at
  most): a game this Vita hosts on the Online connection is filled with bots
  to its most players less one, a bot leaving for each player who joins (a
  party's members on one team; network_server_manager.c's bot fill); the
  Halo 3 GAME OPTIONS' BOTS sets it too. Never in Same Wi-Fi, ad hoc or
  Split Screen games.
- "Bots" (HALO_BOTS, Off or 1 to 15): computer players in the game's
  Multiplayer > Split Screen games (port/linux/game/bots.c), who join its
  lobby; with any, "Bot skill" (HALO_BOT_SKILL: Easy, Normal, Heroic,
  Legendary), "Bot teams" (HALO_BOT_TEAMS: in team games Even, on both
  teams, or Against you, all on the other team) and "Bot vehicles"
  (HALO_BOT_VEHICLES: On, the bots drive and ride the map's Warthogs, Ghosts
  and Scorpions, or Off, on foot), "Bot routes" (HALO_BOT_ROUTES: Direct,
  everyone the shortest way down the middle, as before; Lanes, the attackers
  spread over the map's side lanes; Mixed, a few of them;
  port/linux/game/bots_routes.c) and "Bot playlist" (HALO_BOT_PLAYLIST:
  Off; Small maps, Big maps, Everything or Custom, bots_playlist.txt in the
  data folder: after the scores the game goes on to the next map and
  gametype by itself, port/linux/game/bots_playlist.c) show below it. Live:
  the lobby and the bots follow them.

Multiplayer's Vita and PC page has an Infection page (below Players): it sets the game's own
Infection game type as this Vita hosts it, from any menus' game type list
(port/linux/game/infection.c; infection.* in config.toml): First zombies (one
in every 4 to 16 players, 8 by default), Zombie speed (100% to 150%, 115%),
Round time (the humans left then win: none, 3 to 30 minutes, 6), Zombie
respawn (3 to 15 seconds, 3), Zombie shields (On) and Tough zombies (half
the damage: Off). A game type saved with its own settings plays by those.

Multiplayer's Modded maps page lists the maps in the maps folder that are not the Xbox's own:
name, size, Xbox or Custom Edition (CE; CE+OS for OpenSauce's .yelo; CE SP
for a Custom Edition campaign map, which the Campaign's level list offers
after its ten levels), and whether it is on. Left and right turn one off or on (an off map stays in
the folder but leaves the level list: HALO_MAPS_DISABLED, read by
port/linux/game/custom_edition_maps.c each time the list opens); square
deletes one, with its picture and description, after a confirmation. The
"PC maps" switch (Custom Edition maps in the level list) is there too, with
a warning when the Custom Edition resource maps those need (bitmaps.map,
sounds.map, loc.map) are not in the folder (copy them in, or the Halo CE
installer: with one in ux0:data/haloce-vita, "Extract PC files" takes them
from it, vita_ce_installer.c), and "Map downloads": whether a
host's map is offered for download in a game joined without it (Ask, the
default, warns in a game joined from the public lobby; Not public games
asks only in the others; Never: port/linux/game/map_share.c).
The page's first row, "Mod manager", opens the mod manager's page (the
Halo 3 menus' MOD MANAGER's: port/linux/game/mod_manager.c, through
mod_manager_share.h): the maps folder's custom maps and the mods folder's
MCC mods, each with On, Off or Error (it failed the game's own checks), its
size and kind; the help line says why it failed or what it needs. Left and
right turn one off or on (a mod by its key: HALO_MODS_DISABLED, kept here as
HALO_MAPS_DISABLED is), cross shows its details, square deletes it after a
question; the game's main loop looks through the folders when the page
opens, and deletes (only inside our folders, never through a link). Circle
goes back to Modded maps.
Multiplayer's Friends page (port/linux/src/p2p_friends.h; its row in Join
with a code's place, which the PC menus' Direct Link leaves out, Online:
the tab is a screen's height) is the Halo 3
FRIENDS screen's for the original menus: this Vita's friend code, Add a
friend (the code typed with the D-pad, as Join with a code), Appear offline
(HALO_FRIENDS_APPEAR_OFFLINE) and Friends can join (HALO_FRIENDS_JOIN), then
the requests, invites, friends, requests sent and blocked players, each with
a word of its state, the chosen one's doing on the help line. Cross accepts,
joins or unblocks, square declines, invites, ignores or cancels, triangle
removes a friend or blocks a request after a question; a join goes on to
Join's steps.
"MCC mods" (HALO_MCC_MODS, experimental, Off by default; shown while the
mods folder has a folder in it) lets the Campaign's level list offer a
Halo: The Master Chief Collection mod's Halo: CE Anniversary maps from
ux0:data/haloce-vita/mods, played alone and offline
(port/linux/game/custom_edition_mods.c, docs/mcc_cea_maps.md).
"Halo content folder" (HALO_CONTENT_ROOT): where the player's own Halo: The
Master Chief Collection folder is (halo1 in it): Found by itself, the
default, ux0:data/haloce-vita/mcc, or ux0:, uma0: or imc0:data/mcc. Its
maps are listed with the others, as "(MCC)" levels (the campaign's after
its ten levels, the Halo 3 campaign lobby's missions MCC's where it has
them, the multiplayer maps first among the custom maps); there is no
switch, and without one the Xbox maps alone. The page's lines say where
the content is read from and what of it is played
(port/linux/game/mcc_content.c, docs/mcc_cea_maps.md).
"PC map vehicles" (HALO_PC_MAP_VEHICLES, shown while PC maps is on): a
Custom Edition map's vehicles in a game of this Vita's alone (split screen,
offline, with or without bots; never a network game's): Xbox, the default,
its Warthogs, Ghosts and Scorpions; PC, every vehicle it places, as Halo PC
(Banshees, Rocket Warthogs, gun turrets); Allowed, every one it allows in
the game type too (port/linux/game/custom_edition_objects.c).

Game's Forge and File Share page (its last row, shown with Menu style Halo
CE: the Halo 3 main menu has FORGE and FILE SHARE) reaches Forge and File
Share with the original menus (the owner: everything in the overlay, for the original menus too):
Map (the multiplayer maps in the maps folder: port/linux/game/forge.c),
Map variant (None, or one of that map's: port/linux/src/forge_store.c) and
Start Forge, which asks the game for the Forge game at its main menu
(SYSTEM_LINK_REQUEST_FORGE, system_link_shortcut.c) and closes the panel;
under File Share the map variants of every map (the first
FORGE_PANEL_MAXIMUM), square deleting one after a confirmation. While a
Forge game is played, Clear dead AI and Clear all AI (asked first) do what
the Forge menu's CLEAR DEAD AI and CLEAR ALL AI do, each with how many it
takes (forge.c forge_panel_ai_counts, forge_panel_clear_ai).

Dev holds a few switches for testers (each a debug environment variable;
most are read once, at start-up, and say so; the renderer's A/B ones on a
page of their own) and "Save report", which
copies halo.log, halo-prev.log, settings.txt, env.txt and the newest crash
dump into ux0:data/haloce-vita/report-<date>/ for sending. A dev switch is
saved in settings.txt only while it is on; off, env.txt's value (or the
default) applies. When any is on, halo.log says so near its top. "Ad hoc
dialog" (how the system's dialog joins a group) is there too, out of the
Multiplayer tab's way.
*/

#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <psp2/system_param.h>

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lang.h"
#include "forge_store.h"
#include "mod_manager_share.h"
#include "p2p.h"
#include "p2p_friends.h"
#include "p2p_party.h"
#include "game_chat_share.h"
#include "crossplay_guard.h"
#include "system_link_shortcut.h"
#include "update_check.h"
#include "vita_controls.h"
#include "vita_gxm.h"
#include "vita_host.h"
#include "vita_update.h"
#include "vita_update_verify.h"
#include "vita_version.h"

/* the wire of the game the Vita hosts or joins (port/linux/src/network_wire.c):
nonzero on the crossplay wire (a PC's game, or one hosted for Vitas and PC) */
int network_wire_crossplay(void);

#define DATA_DIRECTORY "ux0:data/haloce-vita"
#define SETTINGS_FILE DATA_DIRECTORY "/settings.txt"
/* settings.txt's last line (save_lines): the file was written whole */
#define SETTINGS_END "HALO_SETTINGS_END"
/* where the system writes its crash dumps (psp2core-*.psp2dmp) */
#define DUMP_DIRECTORY "ux0:data"
#define MAXIMUM_CHOICES 48
/* the language files (lang.c): in the game's package */
#define LANG_DIRECTORY "app0:lang"
/* a code's characters as typed (p2p.h shows them ABCD-EFGH) */
#define P2P_CODE_LENGTH_TYPED 8

/* read again by the readers that take a change mid-game (port_config.c) */
extern volatile unsigned long halo_settings_generation;
/* the render resolution or aspect ratio asked for could not be made
(d3d8_gxm.c screen_settings_apply): it waits for a restart */
int halo_screen_restart_needed(void);
/* whether the map file `name` (no extension) has its tags loaded
(cache_files.c): such a map is not deleted */
int halo_cache_map_in_use(const char *name);

/* the pages: the tabs L and R switch between, then the pages a row of a tab
opens (circle goes back to the tab) */
enum
{
	TAB_GAME,
	TAB_CONTROLS,
	TAB_GRAPHICS,
	TAB_AUDIO,
	TAB_MULTIPLAYER,
	TAB_DEV,
	TAB_COUNT,

	PAGE_GRAPHICS_DETAIL = TAB_COUNT,
	PAGE_GRAPHICS_PERFORMANCE,
	PAGE_BUTTONS,
	PAGE_TOUCH,
	PAGE_GYRO,
	PAGE_PLAY,
	PAGE_MAPS,
	PAGE_DEV_AB,
	PAGE_FORGE,
	/* (Modded maps' Mod manager: circle goes back to Modded maps) */
	PAGE_MODS,
	/* (Multiplayer's Friends: p2p_friends.h) */
	PAGE_FRIENDS,
	/* (Multiplayer's Party: p2p_party.h) */
	PAGE_PARTY,
	/* (Multiplayer's Vita and PC: the browsers' filter, who can join a game
	hosted - network_wire.h) */
	PAGE_CROSSPLAY,
	/* the players of the game this Vita hosts: kick, ban (the game's side:
	port/linux/game/network_anticheat.c) */
	PAGE_PLAYERS,
	/* (Multiplayer's Infection: the built-in Infection game type's settings,
	port/linux/game/infection.c) */
	PAGE_INFECTION,
	/* (Game's Menus: Menu style, the Halo 3 menus' Carnage report) */
	PAGE_MENUS,
	/* (Multiplayer > Friends > Chat: the in-game chat, its filter, the
	players muted: port/linux/game/game_chat.c, game_chat_share.h) */
	PAGE_CHAT,
	PAGE_COUNT
};

/* each page's name and the tab it is on (a tab's own: itself) */
static const struct
{
	const char *name;
	int tab;
} pages[PAGE_COUNT] = {
	{ "Game", TAB_GAME },
	{ "Controls", TAB_CONTROLS },
	{ "Video", TAB_GRAPHICS },
	{ "Sound", TAB_AUDIO },
	{ "Multiplayer", TAB_MULTIPLAYER },
	{ "Dev", TAB_DEV },
	{ "Detail", TAB_GRAPHICS },
	{ "Performance", TAB_GRAPHICS },
	{ "Button layout", TAB_CONTROLS },
	{ "Touch zones", TAB_CONTROLS },
	{ "Gyro settings", TAB_CONTROLS },
	{ "Play", TAB_MULTIPLAYER },
	{ "Modded maps", TAB_MULTIPLAYER },
	{ "A/B switches", TAB_DEV },
	{ "Forge and File Share", TAB_GAME },
	{ "Mod manager", TAB_MULTIPLAYER },
	{ "Friends", TAB_MULTIPLAYER },
	{ "Party", TAB_MULTIPLAYER },
	{ "Vita and PC", TAB_MULTIPLAYER },
	{ "Players", TAB_MULTIPLAYER },
	{ "Infection", TAB_MULTIPLAYER },
	{ "Menus", TAB_GAME },
	{ "Chat", TAB_MULTIPLAYER },
};
/* the headings over a tab's rows, as Halo Online's settings have them
(HUD, Interface...): each above the row it names, when that row is shown */
static const struct
{
	int page;
	const char *row;
	const char *name;
} headings[] = {
	{ TAB_GAME, "Menus", N_("Interface") },
	{ TAB_GAME, "First-person legs", N_("First person") },
	{ TAB_GAME, "Check for updates", N_("Updates") },
	{ TAB_CONTROLS, "Look sensitivity", N_("Look") },
	{ TAB_CONTROLS, "Crouch", N_("Buttons") },
	{ TAB_CONTROLS, "Gyro aiming", N_("Gyro") },
	{ TAB_CONTROLS, "Rear touch guard", N_("Touch") },
	{ TAB_GRAPHICS, "Profile", N_("Quality") },
	{ TAB_GRAPHICS, "Aspect ratio", N_("Screen") },
	{ TAB_GRAPHICS, "Detail", N_("More") },
	{ TAB_MULTIPLAYER, "Connection", N_("Network") },
	{ TAB_MULTIPLAYER, "Bots", N_("Bots") },
	{ TAB_MULTIPLAYER, "Co-op players", N_("Games") },
};

enum
{
	/* a line whose value left and right change (the default) */
	KIND_CHOICE,
	/* a line that does something when chosen (cross, or right) */
	KIND_ACTION,
	/* a line of text (the Play page's lobby name and password): cross
	opens the system's keyboard (vita_ime.c); kept in play_texts */
	KIND_TEXT,
};

enum
{
	ACTION_NONE,
	ACTION_JOIN_CODE,
	ACTION_BROWSE,
	ACTION_SAVE_REPORT,
	ACTION_RESET_CONTROLS,
	ACTION_HOST,
	ACTION_JOIN,
	/* the Campaign screen, where Y on the difficulty hosts co-op */
	ACTION_CAMPAIGN,
	/* the ad hoc room's group joined (the system's dialog), for the game's
	System Link screen */
	ACTION_ADHOC_JOIN,
	/* opens the page `opens` */
	ACTION_PAGE,
	/* the Custom Edition installer's maps taken (vita_ce_installer.c) */
	ACTION_CE_EXTRACT,
	/* a look for a newer version (update_notify.c), in the background */
	ACTION_UPDATE_CHECK,
	/* (the Friends page) a friend's code typed with the D-pad */
	ACTION_FRIEND_ADD,
	/* (the Friends page) the friend code backed up to a file with a
	password, or restored from one */
	ACTION_FRIEND_BACKUP,
	/* (the Party page) this Vita out of its party */
	ACTION_PARTY_LEAVE,
	/* (the Party page) a chat message typed on the system's keyboard */
	ACTION_PARTY_MESSAGE,
	/* (the Players page) the machines this host banned for the session, and
	those its anti-cheat dropped, let in again */
	ACTION_CLEAR_BANS,
	/* (the Players page) a bot more, or fewer, in the game this Vita hosts
	(its add_bot and drop_bot: network_server_manager.c) */
	ACTION_ADD_BOT,
	ACTION_DROP_BOT,
};

struct setting
{
	const char *label;
	const char *variable;
	int restart;
	int count;
	const char *values[MAXIMUM_CHOICES];
	const char *names[MAXIMUM_CHOICES];
	const char *help;
	int choice;
	/* the page the row is on (a TAB_* or PAGE_*) */
	int page;
	int kind;
	int action;
	/* a dev switch: its first choice is the default (an empty value: the
	variable unset), it is saved only when on, and env.txt's value stands
	while settings.txt does not name it */
	int dev;
	/* ACTION_PAGE: the page the row opens */
	int opens;
};

/* a row that opens a page of rows */
#define PAGE_ROW(label, help, page, opened) \
	{ label, NULL, 0, 0, { NULL }, { NULL }, help, 0, page, KIND_ACTION, ACTION_PAGE, 0, opened }

/* (the performance logging switch: the three timing variables at once) */
#define PERFORMANCE_LOG "HALO_PERF_LOG"

static struct setting settings[] = {
	/* (the Profile row is the first: settings[0]; Render resolution the
	second) */
	{ "Profile", "HALO_PROFILE", 0, 4, { "performance", "balanced", "quality", "custom" },
		{ "Performance", "Balanced", "Quality", "Custom" }, "Sets resolution, detail, update rates at once", 1,
		TAB_GRAPHICS },
	/* (Dynamic: the dynamic resolution, d3d8_gxm.c - the scale follows the
	GPU's load, from the Dynamic minimum up to 100%) */
	{ "Render resolution", "HALO_RENDER_SCALE", 0, 6, { "1", "0.875", "0.75", "0.625", "0.5", "dynamic" },
		{ "100%", "88%", "75%", "63%", "50%", "Dynamic" }, "Lower is faster, softer. Dynamic: by GPU load", 5,
		TAB_GRAPHICS },
	/* (shown while Render resolution is Dynamic: setting_shown) */
	{ "Dynamic minimum", "HALO_DYNAMIC_RES_MIN", 0, 3, { "0.5", "0.625", "0.75" }, { "50%", "63%", "75%" },
		"The lowest Dynamic goes in heavy scenes", 1, TAB_GRAPHICS },
	{ "Frame limit", "HALO_FRAME_CAP", 0, 3, { "30", "60", "0" }, { "30 FPS", "60 FPS", "Off" },
		"The most frames shown a second", 0, TAB_GRAPHICS },
	/* (port/linux/game/render_interpolation.c: frames drawn between the
	game's 30 ticks a second, blended; up to two a tick while both fit
	(main.c), else one; triple buffered (vita_gxm.c). On, it is the frame
	limit, and the weapon's blend is part of it. Live) */
	{ "Frame interpolation", "HALO_INTERPOLATION", 0, 2, { "false", "true" }, { "Off", "On" },
		"Up to 60 FPS: frames between the 30 Hz ticks", 0, TAB_GRAPHICS },
	{ "Aspect ratio", "HALO_DISPLAY_WIDTH", 0, 2, { "848", "640" }, { "16:9", "4:3" },
		"4:3: the Xbox's framing, black bars", 0, TAB_GRAPHICS },
	/* (port/linux/game/field_of_view.c: Halo's degrees, 70 the original
	view; wider at the sides, 16:9 adding its strips as at 70; zoomed views
	and the first-person weapon kept as they were. In no profile. Live) */
	{ "Field of view", "HALO_FIELD_OF_VIEW", 0, 7, { "70", "75", "80", "85", "90", "95", "100" },
		{ "70 (original)", "75", "80", "85", "90", "95", "100" },
		"Wider shows more, but more to draw can cost frame rate", 0, TAB_GRAPHICS },
	{ "Upscale filter", "HALO_UPSCALE_FILTER", 0, 2, { "0", "1" }, { "Smooth", "Sharp" },
		"Scaling to the screen: Sharp = crisp pixels", 0, TAB_GRAPHICS },
	/* (d3d8_gxm.c, Sharp interface: the HUD, the menus, their text, the
	screen flashes and the letterbox drawn at the display's 960x544, the 3D
	at the render resolution. On by default, in no profile. Live) */
	{ "Sharp interface", "HALO_SHARP_UI", 0, 2, { "1", "0" }, { "On", "Off" },
		"HUD, menus and text at full resolution", 0, TAB_GRAPHICS },
	PAGE_ROW("Detail", "Models, shadows, lights, effects, particles", TAB_GRAPHICS, PAGE_GRAPHICS_DETAIL),
	PAGE_ROW("Performance", "Update rates, AI think rate, fourth core helpers", TAB_GRAPHICS,
		PAGE_GRAPHICS_PERFORMANCE),
	{ "Model detail", "HALO_MODEL_LOD_SCALE", 0, 4, { "1", "0.75", "0.5", "0.35" },
		{ "High", "Medium", "Low", "Lowest" }, "Level of detail of characters and vehicles", 1,
		PAGE_GRAPHICS_DETAIL },
	{ "Hide distant objects", "HALO_MIN_OBJECT_PIXELS", 0, 4, { "0", "4", "8", "12" },
		{ "Off", "Tiny", "Small", "Medium" }, "Skip objects this small on screen", 2, PAGE_GRAPHICS_DETAIL },
	/* (Bruno Santana's settings, his variables and values, so his build's
	settings.txt carries over: render_objects.c's shadows - Near only for
	objects over 60 pixels across, the Xbox's 30 -, object_lights.c's
	dynamic lights cap, render.c's passes, render_particles.c's density) */
	{ "Object shadows", "HALO_VITA_SHADOWS", 0, 3, { "2", "1", "0" }, { "Full", "Near only", "Off" },
		"Shadows under characters and vehicles", 2, PAGE_GRAPHICS_DETAIL },
	{ "Dynamic lights", "HALO_MAX_SCENE_LIGHTS", 0, 4, { "0", "8", "4", "2" }, { "All", "8", "4", "2" },
		"The most lights from shots and blasts a frame", 2, PAGE_GRAPHICS_DETAIL },
	{ "Effects quality", "HALO_VITA_EFFECTS_QUALITY", 0, 3, { "2", "1", "0" },
		{ "Full", "Performance", "Minimal" }, "Performance: no reflections, specular, grass", 0,
		PAGE_GRAPHICS_DETAIL },
	/* (the sun's glow and rays, rasterizer_lights.c: seven small scenes a
	frame while the sun is in view) */
	{ "Sun rays", "HALO_SUN_RAYS", 0, 2, { "1", "0" }, { "On", "Off" },
		"The sun's glow and light shafts outdoors", 1, PAGE_GRAPHICS_DETAIL },
	/* (decals.c: decals under 2 pixels across are not made. Off as on the
	Xbox, in no profile yet: to be tried on hardware) */
	{ "Tiny decals", "HALO_DECAL_MIN_PIXELS", 0, 2, { "0", "2" }, { "Shown", "Skipped" },
		"Bullet holes too small to see", 0, PAGE_GRAPHICS_DETAIL },
	/* (the particle budget, port/linux/game/particle_budget.c: the
	particles a fight's effects make - an entry's count and a long life span
	cut, those too small to see at their distance not made; muzzle flashes,
	the first-person weapon's and scripted effects whole. Live; Reduced in
	every profile. Particle density, under it, draws fewer of those made) */
	{ "Particles", "HALO_PARTICLE_BUDGET", 0, 3, { "full", "reduced", "low" }, { "Full", "Reduced", "Low" },
		"Fewer sparks, smoke and debris in big fights", 1, PAGE_GRAPHICS_DETAIL },
	{ "Particle density", "HALO_PARTICLE_RENDER_DIVISOR", 0, 4, { "1", "2", "3", "4" },
		{ "Full", "Half", "Third", "Quarter" }, "Smoke, sparks, debris drawn (your gun's: all)", 0,
		PAGE_GRAPHICS_DETAIL },
	/* (port/linux/game/ragdolls.c: a dead body falls limp, a few at once
	simulated on the tick's thread, only the picture changes. On by
	default and in every profile. Live) */
	{ "Ragdolls", "HALO_RAGDOLLS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Dead bodies fall limp where they land", 1, PAGE_GRAPHICS_DETAIL },
	{ "Scenery updates", "HALO_SCENERY_UPDATE_DIVISOR", 0, 3, { "1", "2", "4" },
		{ "Every tick", "Half", "Quarter" }, "How often static props are updated", 2, PAGE_GRAPHICS_PERFORMANCE },
	{ "Object lighting", "HALO_LIGHTING_REFRESH_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Full", "Half", "Third" }, "How often object lighting is recomputed", 2, PAGE_GRAPHICS_PERFORMANCE },
	/* (actors.c, Bruno Santana's variable and values: how often an actor
	thinks - its perception, situation, emotions and decision -, its
	movement and firing every tick; Adaptive by its distance to the nearest
	player. 1.1's Distant AI row, HALO_AI_PERCEPTION_LOD, is gone: a
	settings.txt naming it loads as the profile has this row) */
	{ "AI think rate", "HALO_AI_THINK_DIVISOR", 0, 4, { "1", "0", "2", "3" },
		{ "Every tick", "Adaptive", "15 Hz", "10 Hz" }, "Adaptive: far-off enemies think less often", 1,
		PAGE_GRAPHICS_PERFORMANCE },
	/* (vita_fourth_core.c, with Bruno Santana's levels and variable, so his
	build's settings.txt carries over: Audio puts the sound mixer on the
	fourth core, All async the mixer, GXM's display queue, the cache file
	thread, the map decompression, the checkpoint writer, the shader
	compiler and clean-up and the log's writer. The system gives a game
	that core only with a kernel plugin such as CapUnlocker; halo.log says
	where each went. Off until the hardware says what it is worth; in no
	profile) */
	{ "Fourth core helpers", "HALO_CPU3_AUX", 1, 3, { "0", "1", "2" }, { "Off", "Audio", "All async" },
		"Needs CapUnlocker: audio, or all background work", 2, PAGE_GRAPHICS_PERFORMANCE },
	/* (d3d8_gxm.c clocks_governor, vita_clocks.c: the CPU and GPU clocks
	lowered after a second in the main menu, paused or on a loading screen,
	play's again at once; never below the clocks found at start, so an
	overclocking plugin's stand. On by default, in no profile. Live) */
	{ "Battery saver in menus", "HALO_CLOCK_GOVERNOR", 0, 2, { "1", "0" }, { "On", "Off" },
		"Lower clocks in the menus, paused and loading", 0, PAGE_GRAPHICS_PERFORMANCE },
	/* (the menus: Halo CE's own, or the Halo 3-style ones over the same
	animated Halo ring - port/vita/app0/menus/halo3, menu_tags.c, which
	reads it each time a screen opens: live) */
	PAGE_ROW("Menus", "Menu style, the carnage report and scoreboard", TAB_GAME, PAGE_MENUS),
	/* (ElDewrito, "online": the Halo 3 menus' flows in the online set's look,
	port/vita/app0/menus/online, menu_tags.c menu_tags_online - in place of
	the Halo 3 style since insider.25 (the owner): a saved "halo3" is
	ElDewrito (find_choice); HALO_MENU_HALO3=1 in env.txt still shows the
	Halo 3 set for a test (menu_files.c)) */
#ifndef VITA_SETTINGS_TEST_HALO3_STYLE
	/* (Classic, "original": the Xbox menus as they shipped, the port's
	features here alone; Classic+, "classic" - the Halo CE style before
	insider.25, so a saved one keeps its rows -: the Xbox menus with the
	port's rows and screens in their look. The owner, insider.25) */
	{ "Menu style", "HALO_MENU_STYLE", 0, 3, { "original", "classic", "online" }, { "Classic", "Classic+", "ElDewrito" },
		"ElDewrito: Halo Online style. Classic: as shipped. Classic+: with the new rows.", 2, PAGE_MENUS },
#else
	/* (vita_settings_test.c: the panel's every page as the hidden Halo 3
	style lays it out - its checks were written for it) */
	{ "Menu style", "HALO_MENU_STYLE", 0, 4, { "classic", "halo3", "online", "original" }, { "Halo CE", "Halo 3", "ElDewrito", "Classic" },
		"ElDewrito: Halo Online style. Halo 3: the ring. Halo CE: the original.", 1, PAGE_MENUS },
#endif
	/* (menu_functions.c h3_carnage_wanted, config.toml's menus.carnage_report:
	a multiplayer game's end, Halo 3's CARNAGE REPORT or the game's own;
	read when the scores come up: live. Shown with Menu style Halo 3 - Halo
	CE has its own two below -: setting_shown. On this page, with
	Menu style, as the Game tab is a screen's height) */
	{ "Carnage report", "HALO_CARNAGE_REPORT", 0, 2, { "classic", "halo3" }, { "Original", "ElDewrito" },
		"A multiplayer game's end: ElDewrito's report, or the original", 1, PAGE_MENUS },
	/* (menu_functions.c h3_scoreboard_wanted, config.toml's menus.scoreboard:
	the scores while Back is held in a multiplayer game, Halo 3's scoreboard
	or the game's own; read each frame: live. Shown with Menu style Halo 3, as
	Carnage report is - Halo CE has its own two below -: setting_shown) */
	{ "Scoreboard", "HALO_SCOREBOARD", 0, 2, { "classic", "halo3" }, { "Original", "ElDewrito" },
		"Scores in a game: ElDewrito's scoreboard, or the original", 1, PAGE_MENUS },
	/* (menu_backdrop.c, config.toml's menus.backdrop: the campaign level
	Halo behind the Online menus, made once from the player's a30.map; Off,
	the ring. Read each frame at the main menu: live. Shown with Menu style
	Online: setting_shown) */
	{ "Menu background", "HALO_MENU_BACKDROP", 0, 2, { "0", "1" }, { "Ring", "Halo (live)" },
		"ElDewrito menus: the level Halo behind them (your a30.map), or the ring", 1, PAGE_MENUS },
	/* (the same two for Menu style Halo CE - the owner, Oct 24: the original
	scores and report by default, Halo 3's as an option, so that the report's
	DETAILS and the scoreboard's player options (mute, add friend, kick) are
	reachable from the original menus too - menu_functions.c h3_carnage_wanted,
	h3_scoreboard_wanted; menus.classic_carnage_report, menus.classic_scoreboard.
	Shown with Menu style Halo CE: setting_shown. Live) */
	{ "Carnage report", "HALO_CLASSIC_CARNAGE_REPORT", 0, 2, { "classic", "halo3" }, { "Original", "ElDewrito" },
		"A multiplayer game's end: the original, or ElDewrito's report", 0, PAGE_MENUS },
	{ "Scoreboard", "HALO_CLASSIC_SCOREBOARD", 0, 2, { "classic", "halo3" }, { "Original", "ElDewrito" },
		"Scores in a game: the original, or ElDewrito's with player options", 0, PAGE_MENUS },
	/* (Forge and File Share for the original menus - the owner: everything
	in the overlay -: the Forge page, under Menus (Interface; it was Game's
	last row, under Updates, insider.21), shown with Menu style Halo CE; the
	Halo 3 main menu has FORGE and FILE SHARE: setting_shown) */
	PAGE_ROW("Forge and File Share", "Forge a map; your map variants (File Share)", TAB_GAME, PAGE_FORGE),
	/* (the game's button icons: the Xbox's, or the Vita button each is on,
	in the menus and in play: vita_controls.h; not part of the layout,
	which Reset controls and the page's summary leave out) */
	{ "Button icons", "HALO_BUTTON_ICONS", 0, 2, { VITA_BUTTON_ICONS_VALUES }, { VITA_BUTTON_ICONS_NAMES },
		"Halo's prompts: Xbox icons, or the Vita's", VITA_BUTTON_ICONS_DEFAULT, TAB_GAME },

	/* (the port's own text: this panel, its messages over the game, the
	PC menus' and co-op's lines; lang.c, port/vita/app0/lang. Its choices
	are Automatic, English and a language a file found: language_choices.
	The game's own language too (xbox_xapi.c XGetLanguage, at start): the
	Xbox's maps_es folder of a PAL disc's maps, read when there is one) */
	{ "Language", "HALO_LANGUAGE", 0, 2, { "auto", "en" }, { "Automatic", "English" },
		"This panel, the port's messages. Automatic: the Vita's", 0, TAB_GAME },
	{ "FPS counter", "HALO_FRAMERATE_COUNTER", 0, 2, { "0", "1" }, { "Off", "On" },
		"The game's frame counter, bottom right", 0, TAB_GAME },
	/* (port/linux/game/first_person_legs.c, from OpenCE: your own body
	drawn from the waist down in first person, one more model a frame on
	foot. On by default and in every profile. Live) */
	{ "First-person legs", "HALO_FIRST_PERSON_LEGS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Your legs when you look down, as others see them", 1, TAB_GAME },
	/* (port/linux/game/player_models.c: the first local player's profile's
	player model, from the player's Halo Custom Edition maps, as
	CUSTOMIZATION's MODEL chooses it - kept by the game for the profile, never
	in settings.txt; the choices filled when the panel opens: player_models_
	choices. Shown while the maps folder has one) */
	{ "Player model", "HALO_PLAYER_MODEL", 0, 1, { "0" }, { "Spartan" },
		"Offline: you, or a Custom Edition map's player", 0, TAB_GAME },
	/* (port/linux/game/player_cosmetics.c: the first local player's
	profile's armour colour - the game's eighteen and the platform's own -
	and visor colour, as CUSTOMIZATION's COLORS chooses them; kept in the
	profile, never in settings.txt; the choices filled when the panel opens:
	cosmetics_choices. On Game's Menus page: the Game tab has no room) */
	{ "Armor color", "HALO_ARMOR_COLOR", 0, 1, { "0" }, { "RANDOM" },
		"Other games see the nearest of the first eighteen", 0, PAGE_MENUS },
	{ "Visor color", "HALO_VISOR_COLOR", 0, 1, { "0" }, { "GOLD" },
		"Seen by this game's PS Vita and PC players; others see gold", 0, PAGE_MENUS },
	/* (port/linux/src/p2p_flame.inc: the tester's blue flaming helmet,
	shown only while this Vita's friend identity is on the owner's signed
	tester list, p2p_flame_listed; CUSTOMIZATION has it too. The next game
	joined) */
	{ "Flaming helmet", "HALO_FLAMING_HELMET", 0, 2, { "false", "true" }, { "Off", "On" },
		"Testers: your blue flame; other players' games check it's yours", 1, TAB_GAME },
	{ "Smooth weapon motion", "HALO_INTERPOLATE_FIRST_PERSON", 0, 2, { "1", "0" }, { "On", "Off" },
		"The weapon's animation blended between game ticks", 0, TAB_GAME },

	/* (Game, Updates: the update check, update_check.h - nothing asked of
	the network until Check for updates is pressed, nothing downloaded
	until it is pressed again with an update found, vita_update.h, and
	nothing installed: VitaShell does that; the
	build's version and the answer on the lines under it, page_lines - then
	the rows players change, then the pages laid out as the Xbox controller) */
	{ "Check for updates", NULL, 0, 0, { NULL }, { NULL }, "Asks GitHub for the newest version; again to download it",
		0, TAB_GAME, KIND_ACTION, ACTION_UPDATE_CHECK },
	/* (the channel Check for updates, on the tab, looks at: Experimental at
	pre-releases and releases, Stable at releases only; a pre-release build
	is on Experimental, its Stable greyed out (choice_disabled) and a saved
	Stable ignored (vita_settings_load); a release build starts on Stable.
	Rarely changed, so here rather than beside Check for updates) */
	{ "Update channel", "HALO_UPDATE_CHANNEL", 0, 2, { "experimental", "stable" }, { "Experimental", "Stable" },
		"Experimental: betas too. Stable: releases only", 1, TAB_GAME },
	{ "Show dev settings", "HALO_DEV_SETTINGS", 0, 2, { "0", "1" }, { "Off", "On" },
		"The Dev tab: switches for testers, Save report", 0, TAB_GAME },
	/* (menu_backdrop.c, config.toml's menus.backdrop_music: the menu music's
	level under the live backdrop - the level's ambience under it -; read
	each frame: live. Shown with Menu style ElDewrito: setting_shown) */
	{ "Menu music", "HALO_MENU_MUSIC", 0, 7, { "100", "70", "50", "40", "30", "20", "0" },
		{ "100%", "70%", "50%", "40%", "30%", "20%", "Off" },
		"Under the ElDewrito menus' live backdrop: the level's ambience comes through", 4, TAB_AUDIO },
	{ "Sound voices", "HALO_SOUND_CHANNELS", 1, 4, { "16", "24", "32", "0" },
		{ "16", "24", "32", "Original" }, "Fewer is faster; the AI then differs (after a restart)", 3, TAB_AUDIO },
	{ "Sound occlusion", "HALO_SOUND_OBSTRUCTION_TICKS", 0, 3, { "1", "3", "6" },
		{ "Every tick", "Every 3rd", "Every 6th" }, "How often muffling behind walls is rechecked", 1, TAB_AUDIO },
	/* (sound_manager.c, Bruno Santana's variable: the sound manager's work
	- positions, volumes, which sounds get the voices - every nth frame;
	the mixer plays on) */
	{ "Sound updates", "HALO_SOUND_MANAGER_DIVISOR", 0, 3, { "1", "2", "3" },
		{ "Every frame", "Every 2nd", "Every 3rd" }, "Sounds placed and started; lower is faster", 1, TAB_AUDIO },
	{ "Look sensitivity", "XV_LOOK_SENS", 0, 6, { "50", "75", "100", "125", "150", "200" },
		{ "50%", "75%", "100%", "125%", "150%", "200%" }, "Right stick turning speed", 2, TAB_CONTROLS },
	{ "Invert look", "XV_INVERT_Y", 0, 2, { "0", "1" }, { "No", "Yes" }, "Reverse the right stick's up and down", 0,
		TAB_CONTROLS },
	{ "Stick deadzone", "XV_DEADZONE", 0, 4, { "0", "5", "10", "15" }, { "Off", "5%", "10%", "15%" },
		"Raise if the sticks drift", 0, TAB_CONTROLS },
	{ "Crouch", "HALO_CROUCH_TOGGLE", 0, 2, { "1", "0" }, { "Toggle", "Hold" },
		"Left stick click: a press crouches, the next stands (Toggle)", 0, TAB_CONTROLS },
	PAGE_ROW("Button layout", "Which Vita button each Xbox button is on, in play", TAB_CONTROLS, PAGE_BUTTONS),
	{ "Reset controls", NULL, 0, 0, { NULL }, { NULL }, "Look, crouch, buttons, touch as shipped (gyro kept)", 0,
		TAB_CONTROLS, KIND_ACTION, ACTION_RESET_CONTROLS },
	/* (gyro aiming: vita_controls.h; its details on Gyro settings) */
	{ "Gyro aiming", "HALO_GYRO", 0, VITA_GYRO_MODES, { "off", "on", "zoomed", "hold" },
		{ "Off", "On", "While zoomed", "While holding" }, "Turn the Vita to aim, with the right stick", 0, TAB_CONTROLS },
	PAGE_ROW("Gyro settings", "Gyro button, sensitivity, direction", TAB_CONTROLS, PAGE_GYRO),
	/* (the rear zones' guard against the hands holding the Vita:
	vita_controls.c - the pad's border never counts, and a hold time) */
	{ "Rear touch guard", "HALO_TOUCH_REAR_GUARD", 0, VITA_REAR_GUARD_COUNT, { VITA_REAR_GUARD_VALUES },
		{ VITA_REAR_GUARD_NAMES }, "Rear pad: its edges (the grip) ignored, a hold time",
		VITA_REAR_GUARD_DEFAULT, TAB_CONTROLS },
	PAGE_ROW("Touch zones", "Front and rear touch zones as Xbox buttons", TAB_CONTROLS, PAGE_TOUCH),
	/* (the Vita button of each Xbox button, in play: vita_controls.c; each
	row says what its button does in play, as the touch zones' choices do:
	VITA_XBOX_NAMES) */
	{ "A (jump)", "HALO_XBOX_A", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"A: jump (in the menus Cross stays A)", 0, PAGE_BUTTONS },
	{ "B (melee)", "HALO_XBOX_B", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"B: melee (in the menus Circle stays B)", 1, PAGE_BUTTONS },
	{ "X (reload, action)", "HALO_XBOX_X", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"X: reload, action (in play only)", 2, PAGE_BUTTONS },
	{ "Y (switch weapon)", "HALO_XBOX_Y", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Y: switch weapon (in play only)", 3, PAGE_BUTTONS },
	{ "Black (grenade type)", "HALO_XBOX_BLACK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Black: switch grenades (in play only)", 8, PAGE_BUTTONS },
	{ "White (flashlight)", "HALO_XBOX_WHITE", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"White: flashlight (in play only)", 9, PAGE_BUTTONS },
	{ "Left trigger (grenade)", "HALO_XBOX_LEFT_TRIGGER", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Left trigger: throw a grenade (in play only)", 4, PAGE_BUTTONS },
	{ "Right trigger (fire)", "HALO_XBOX_RIGHT_TRIGGER", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Right trigger: fire (in play only)", 5, PAGE_BUTTONS },
	{ "Left stick (crouch)", "HALO_XBOX_LEFT_STICK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Left stick click: crouch (in play only)", 7, PAGE_BUTTONS },
	{ "Right stick (zoom)", "HALO_XBOX_RIGHT_STICK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Right stick click: zoom (in play only)", 6, PAGE_BUTTONS },
	{ "Back (scoreboard)", "HALO_XBOX_BACK", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"Back: scoreboard (in the menus Select stays Back)", 10, PAGE_BUTTONS },
	/* (the touch zones, in vita_controls.h's order: the Xbox button each
	presses) */
	{ "Touch top left", "HALO_TOUCH_TOP_LEFT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, top left corner: counts at once", 0, PAGE_TOUCH },
	{ "Touch top right", "HALO_TOUCH_TOP_RIGHT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, top right corner: counts at once", 0, PAGE_TOUCH },
	{ "Touch left edge", "HALO_TOUCH_LEFT_EDGE", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, left edge by the D-pad: counts at once", 0, PAGE_TOUCH },
	{ "Touch right edge", "HALO_TOUCH_RIGHT_EDGE", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Front screen, right edge by the buttons: counts at once", 0, PAGE_TOUCH },
	{ "Rear touch left", "HALO_TOUCH_REAR_LEFT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Rear pad, left half: held a moment (Rear touch guard)", 0, PAGE_TOUCH },
	{ "Rear touch right", "HALO_TOUCH_REAR_RIGHT", 0, VITA_XBOX_COUNT, { VITA_XBOX_VALUES }, { VITA_XBOX_NAMES },
		"Rear pad, right half: held a moment (Rear touch guard)", 0, PAGE_TOUCH },
	/* (the button's choices are the Xbox buttons' rows') */
	{ "Gyro button", "HALO_GYRO_BUTTON", 0, VITA_BUTTON_CHOICES, { VITA_BUTTON_VALUES }, { VITA_BUTTON_NAMES },
		"While holding: aims while held, nothing else in play", 4, PAGE_GYRO },
	{ "Gyro sensitivity", "HALO_GYRO_SENS", 0, 8, { "50", "75", "100", "125", "150", "200", "250", "300" },
		{ "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "2.5x", "3x" }, "1x: the view turns as far as the Vita does", 4,
		PAGE_GYRO },
	{ "Gyro vertical", "HALO_GYRO_INVERT_Y", 0, 2, { "0", "1" }, { "Normal", "Inverted" },
		"Normal: tilt the top edge towards you to look up", 0, PAGE_GYRO },
	{ "Gyro turning", "HALO_GYRO_TURN", 0, 2, { "yaw", "roll" }, { "Turn (yaw)", "Tilt (roll)" },
		"Turn the Vita left/right, or tilt it like a wheel", 0, PAGE_GYRO },
	/* (On: the stick's aim assist, its friction and adhesion, on the
	gyro's turns too: halo_linux_look_assist. Off, with the gyro on in any
	mode: no aim assist at all, the stick's look and the shots' autoaim
	included: halo_linux_aim_assist_off, vita_gyro_unassisted,
	source/game/player_control.c and aim_assist.c. Ignored with the gyro
	off) */
	{ "Gyro aim assist", "HALO_GYRO_ASSIST", 0, 2, { "0", "1" }, { "Off", "On" },
		"Off: no aim assist at all while the gyro is on", 1, PAGE_GYRO },

	/* (Multiplayer: the connection, the ad hoc room (Ad hoc), a code typed
	in (Online, for a Vita without the in-game PC menus: setting_shown), the
	modded maps. Hosting, joining and co-op are the game's own Multiplayer
	menu's: OpenCE's screens, menu_functions.c) */
	/* (Online: internet play, experimental. Live: the game switches to it
	at its menus, port/linux/game/network_connection.c; the Halo 3 lobbies'
	NETWORK sets the same row) */
	{ "Connection", "HALO_VITA_NETWORK", 0, 3, { "wifi", "adhoc", "online" }, { "Same Wi-Fi", "Ad hoc", "Online" },
		"Vitas on one Wi-Fi network play together", 0, TAB_MULTIPLAYER },
	/* (no switch since Oct 19 2026 (the owner): every game speaks its own
	protocol, docs/crossplay.md, network_wire.h. The browsers list the
	Vitas' games and OpenCE's (OpenCE, ChupathingyCE, DamnationCE hosts and
	servers) together, tagged: Browser shows narrows them,
	network.browser_filter, live. Who can join is the games this Vita hosts
	(the owner, Oct 20 2026): Vitas only (the Vitas' protocol; the port's PC
	build is refused), Vitas and Halo CE Vita PC (the same protocol, the PC
	build joins too), or everyone (OpenCE's protocol, listed for both),
	network.who_can_join, taken as a game is hosted; the Create Game
	screens' WHO CAN JOIN sets the same row. The retired Vitas / Vitas and PC
	(HALO_NET_HOST_CROSSPLAY false / true, in an older settings.txt or
	env.txt) loads as Vitas only / everyone: settings_load. Neither with Ad hoc, which no PC
	reaches: setting_shown. The old Crossplay row's choice, in an older
	settings.txt, becomes Browser shows: settings_load. On a page of their
	own, in the old row's place: the tab is a screen's height) */
	PAGE_ROW("Vita and PC", "Which games the browser lists; who joins yours", TAB_MULTIPLAYER, PAGE_CROSSPLAY),
	{ "Browser shows", "HALO_NET_BROWSER_FILTER", 0, 3, { "all", "vita", "pc" }, { "All games", "Vita games", "PC games" },
		"Vita and PC (OpenCE) games, each tagged", 0, PAGE_CROSSPLAY },
	{ "Who can join", "HALO_NET_WHO_CAN_JOIN", 0, 3, { "vita", "vita_pc", "everyone" },
		{ "Vita", "Vita PC", "Everyone" },
		"Vita PC: Vitas and Halo CE Vita on PC. Everyone: also OpenCE", 0, PAGE_CROSSPLAY },
	/* (OpenCE's network version 32: a host for everyone tells its PC
	players whether a mouse may have aim assist, network.block_mouse_aim_assist,
	read live; network_distributed.c sends it. Nothing else changes: the
	Vita's own aim assist is the Gyro page's) */
	{ "PC mouse aim assist", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST", 0, 2, { "false", "true" },
		{ "Their choice", "Off" }, "Everyone games: Off turns off OpenCE players' mouse aim assist", 0, PAGE_CROSSPLAY },
	/* (bots.online: the bots a game this Vita hosts Online fills its lobby
	with, to its most players less one, leaving as players join: Full by
	default; the Halo 3 GAME OPTIONS' BOTS sets it too. Live. On Vita and PC
	with the other rows of the games you host online, not on the tab: the
	Multiplayer tab is a screen's height. Its skill, vehicles and routes are
	the tab's Bot rows, shown while it fills) */
	{ "Online bots", "HALO_BOTS_ONLINE", 0, 10, { "full", "off", "1", "2", "3", "5", "7", "9", "11", "13" },
		{ "Full", "Off", "1", "2", "3", "5", "7", "9", "11", "13" },
		"Games you host Online: bots fill all but one slot", 0, PAGE_CROSSPLAY },
	{ "Ad hoc room", "HALO_ADHOC_ROOM", 0, 4, { "1", "2", "3", "4" }, { "1", "2", "3", "4" },
		"Vitas in the same room play together", 0, TAB_MULTIPLAYER },
	{ "Join the room", NULL, 0, 0, { NULL }, { NULL }, "Then Multiplayer, System Link (LAN) in the game", 0,
		TAB_MULTIPLAYER, KIND_ACTION, ACTION_ADHOC_JOIN },
	{ "Join with a code", NULL, 0, 0, { NULL }, { NULL }, "Type the code another player's game shows", 0,
		TAB_MULTIPLAYER, KIND_ACTION, ACTION_JOIN_CODE },
	/* (friends, port/linux/src/p2p_friends.c: the code, requests, who is
	online, Join, Invite; the Halo 3 menus' FRIENDS screen does the same) */
	PAGE_ROW("Friends", "Your friend code, requests, who is online, join, invite", TAB_MULTIPLAYER, PAGE_FRIENDS),
	/* (the party, port/linux/src/p2p_party.h: friends who play together, the
	leader's games followed; the Halo 3 menus' PARTY screen does the same.
	The Friends page's first row: the Multiplayer tab is a screen's height;
	SELECT + D-pad left opens it at once) */
	PAGE_ROW("Party", "Invite friends, follow the leader, leave", PAGE_FRIENDS, PAGE_PARTY),
	{ "Chat", "HALO_CHAT", 0, 2, { "1", "0" }, { "On", "Off" },
		"Off: no chat over the game, none sent from it", 0, PAGE_CHAT },
	{ "Chat filter", "HALO_CHAT_FILTER", 0, 2, { "1", "0" }, { "On", "Off" },
		"On: slurs others type shown as ****; only you see it", 0, PAGE_CHAT },
	/* (party chat: typed on the system's keyboard; quick chat is the page's
	line under it) */
	{ "Send a message", NULL, 0, 0, { NULL }, { NULL }, "Party chat: type a message to your party", 0,
		PAGE_PARTY, KIND_ACTION, ACTION_PARTY_MESSAGE },
	{ "Leave the party", NULL, 0, 0, { NULL }, { NULL }, "You stay where you are; the others go on together", 0,
		PAGE_PARTY, KIND_ACTION, ACTION_PARTY_LEAVE },
	{ "Add a friend", NULL, 0, 0, { NULL }, { NULL }, "Type a friend's code: they accept, then you're friends", 0,
		PAGE_FRIENDS, KIND_ACTION, ACTION_FRIEND_ADD },
	/* (this Vita's friend code, shown as the row's value; cross: the code
	kept in a file with a password, or restored from one on this Vita or
	another, p2p_friends_backup.inc; the Halo 3 menus' FRIEND CODE BACKUP
	does the same) */
	{ "Friend code", NULL, 0, 0, { NULL }, { NULL }, "Back it up to a file with a password, or restore it", 0,
		PAGE_FRIENDS, KIND_ACTION, ACTION_FRIEND_BACKUP },
	/* (the game this Vita hosts: its players kicked or banned, the host's
	anti-cheat; network_anticheat.c answers, system_link_shortcut.h) */
	/* (the in-game chat, port/linux/game/game_chat.c: SELECT + D-pad left in
	play opens its quick chat; its page: on or off, the slur filter, the
	players muted. Here, before the games' players: the Multiplayer tab and
	the Friends page are a screen's height) */
	PAGE_ROW("Chat", "In-game chat: on or off, the slur filter, mute players", PAGE_CROSSPLAY, PAGE_CHAT),
	PAGE_ROW("Players", "The game you host: kick or ban a player", PAGE_CROSSPLAY, PAGE_PLAYERS),
	/* (the game's own Infection game type as this Vita hosts it, in any
	menus' game type lists: port/linux/game/infection.c's infection.*
	settings, read as a game starts. Beside Players, the games you host's:
	the Multiplayer tab is a screen's height) */
	PAGE_ROW("Infection", "The game you host: Infection's zombies and round", PAGE_CROSSPLAY, PAGE_INFECTION),
	/* (network.anticheat: a machine that keeps sending what only a modified
	game sends dropped and kept out a while; read as each game starts) */
	{ "Anti-cheat", "HALO_NET_ANTICHEAT", 0, 2, { "true", "false" }, { "On", "Off" },
		"On: cheaters are dropped from your games a while", 0, PAGE_PLAYERS },
	{ "Clear bans", NULL, 0, 0, { NULL }, { NULL }, "Let banned and dropped players join again", 0,
		PAGE_PLAYERS, KIND_ACTION, ACTION_CLEAR_BANS },
	/* (bots in the game you host, mid-game too: a bot more or fewer than
	Online bots fills it with, joining or leaving at once) */
	{ "Add a bot", NULL, 0, 0, { NULL }, { NULL }, "The game you host: a bot joins, mid-game too", 0,
		PAGE_PLAYERS, KIND_ACTION, ACTION_ADD_BOT },
	{ "Drop a bot", NULL, 0, 0, { NULL }, { NULL }, "The game you host: a bot leaves", 0,
		PAGE_PLAYERS, KIND_ACTION, ACTION_DROP_BOT },
	/* (friends.appear_offline: nothing sent to friends; live) */
	{ "Appear offline", "HALO_FRIENDS_APPEAR_OFFLINE", 0, 2, { "false", "true" }, { "Off", "On" },
		"On: friends see you offline; you still see them", 0, PAGE_FRIENDS },
	/* (friends.join: a public game without a password joined from presence) */
	{ "Friends can join", "HALO_FRIENDS_JOIN", 0, 2, { "true", "false" }, { "On", "Off" },
		"Your games, public or private: friends join in one press", 0, PAGE_FRIENDS },
	/* (the offline bots: port/linux/game/bots.c; the skill and teams rows
	shown while there are any, setting_shown) */
	{ "Bots", "HALO_BOTS", 0, 11, { "0", "1", "2", "3", "4", "5", "6", "7", "9", "11", "15" },
		{ "Off", "1", "2", "3", "4", "5", "6", "7", "9", "11", "15" },
		"Computer players in Multiplayer > Split Screen", 0, TAB_MULTIPLAYER },
	{ "  Bot skill", "HALO_BOT_SKILL", 0, 4, { "easy", "normal", "heroic", "legendary" },
		{ "Easy", "Normal", "Heroic", "Legendary" }, "How soon bots react, how well they aim", 1, TAB_MULTIPLAYER },
	{ "  Bot teams", "HALO_BOT_TEAMS", 0, 2, { "even", "against" }, { "Even", "Against you" },
		"Team games: bots on both teams, or all against you", 0, TAB_MULTIPLAYER },
	{ "  Bot vehicles", "HALO_BOT_VEHICLES", 0, 2, { "1", "0" }, { "On", "Off" },
		"Bots drive and ride the map's vehicles", 0, TAB_MULTIPLAYER },
	/* (port/linux/game/bots_routes.c: Direct as before, everyone down the
	middle; Lanes and Mixed spread the attackers over the map's side lanes.
	Live: read each second) */
	{ "  Bot routes", "HALO_BOT_ROUTES", 0, 3, { "direct", "lanes", "mixed" }, { "Direct", "Lanes", "Mixed" },
		"Direct: all charge the middle. Lanes, Mixed: some flank by the sides", 0, TAB_MULTIPLAYER },
	/* (port/linux/game/bots_playlist.c: after the scores the next map and
	gametype by itself; Custom reads bots_playlist.txt in the data folder.
	Live: read at each game's scores) */
	{ "  Bot playlist", "HALO_BOT_PLAYLIST", 0, 5, { "off", "small", "big", "all", "custom" },
		{ "Off", "Small maps", "Big maps", "Everything", "Custom" },
		"Split Screen: next map and game by itself. Custom: bots_playlist.txt", 0, TAB_MULTIPLAYER },
	/* (network.coop_players: co-op games this Vita hosts take 2 players by
	default, the Xbox's; 3 and 4 are experimental, the host running the
	campaign's AI and scripts for everyone: network_server_manager.c) */
	{ "Co-op players", "HALO_NET_COOP_PLAYERS", 0, 3, { "2", "3", "4" }, { "2", "3 (experimental)", "4 (experimental)" },
		"Co-op you host. 3-4: your Vita may lag", 0, TAB_MULTIPLAYER },
	/* (network.remote_lod, objects.c's objects_remote_lod_skipped: in a game
	this Vita joined, another player out of view, on foot and doing nothing
	but moving, updated every other tick. Auto by default (the owner, Oct 11
	2026): in games of more than 16 players, which only crossplay's PC games
	are; a sixth to a third less of a 24-player game's tick. Live) */
	{ "Unseen players", "HALO_NET_REMOTE_LOD", 0, 3, { "auto", "1", "0" }, { "Auto", "Reduced", "Full" },
		"Reduced: smoother big games. Auto: over 16 players", 0, TAB_MULTIPLAYER },
	/* (the game's own Infection game type as this Vita hosts it, in any
	game's menus' game type lists: port/linux/game/infection.c's
	infection.* settings, read as a game starts. On a page of its own, its
	row on Vita and PC: the Multiplayer tab is a screen's height) */
	{ "First zombies", "HALO_INFECTION_ZOMBIES", 0, 6, { "4", "6", "8", "10", "12", "16" },
		{ "1 per 4 players", "1 per 6 players", "1 per 8 players", "1 per 10 players", "1 per 12 players",
		"1 per 16 players" }, "Zombies at the start, chosen at random (one at least)", 2, PAGE_INFECTION },
	{ "Zombie speed", "HALO_INFECTION_SPEED", 0, 6, { "100", "110", "115", "125", "135", "150" },
		{ "100%", "110%", "115%", "125%", "135%", "150%" }, "How fast zombies run; humans run at 100", 2,
		PAGE_INFECTION },
	{ "Round time", "HALO_INFECTION_TIME", 0, 9, { "0", "3", "5", "6", "8", "10", "15", "20", "30" },
		{ "No limit", "3 minutes", "5 minutes", "6 minutes", "8 minutes", "10 minutes", "15 minutes", "20 minutes",
		"30 minutes" }, "The humans still alive then win", 3, PAGE_INFECTION },
	{ "Zombie respawn", "HALO_INFECTION_RESPAWN", 0, 5, { "3", "5", "8", "10", "15" },
		{ "3 seconds", "5 seconds", "8 seconds", "10 seconds", "15 seconds" }, "How soon a dead zombie comes back", 0,
		PAGE_INFECTION },
	{ "Zombie shields", "HALO_INFECTION_SHIELDS", 0, 2, { "1", "0" }, { "On", "Off" },
		"Off: zombies have their body alone", 0, PAGE_INFECTION },
	{ "Tough zombies", "HALO_INFECTION_TOUGH", 0, 2, { "0", "1" }, { "Off", "On" },
		"On: zombies take half the damage", 0, PAGE_INFECTION },
	PAGE_ROW("Modded maps", "Custom maps: on, off, delete; PC maps", TAB_MULTIPLAYER, PAGE_MAPS),
	/* (the latency meter, port/linux/game/latency_meter.c: the round trip
	to the host at the top right of a network game and in the scoreboard's
	Ping column; the host's, its slowest player's) */
	{ "Latency meter", "HALO_LATENCY_METER", 0, 2, { "1", "0" }, { "On", "Off" },
		"Ping to the host in network games, top right", 0, TAB_MULTIPLAYER },

	/* (the hosted game's settings, which the game's Server Setup sets
	(vita_settings_set, menu_functions.c) and settings.txt keeps: on a page
	no row opens) */
	{ "  Lobby name", "HALO_NET_LOBBY_NAME", 0, 0, { NULL }, { NULL }, "The name others see for your game", 0,
		PAGE_PLAY, KIND_TEXT },
	/* (network_server_manager.c: the game's maximum_players; co-op takes
	network.coop_players, the Co-op players row) */
	{ "  Max players", "HALO_NET_MAX_PLAYERS", 0, 15,
		{ "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" },
		{ "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" },
		"The most players your games take (not co-op)", 14, PAGE_PLAY },
	/* (network.host_public, OpenCE's: p2p.c) */
	{ "  Visibility", "HALO_NET_HOST_PUBLIC", 0, 2, { "false", "true" }, { "Private", "Public" },
		"Private: others join by code. Public: listed too", 1, PAGE_PLAY },
	{ "  Password", "HALO_NET_LOBBY_PASSWORD", 0, 0, { NULL }, { NULL }, "A public game asks joiners for it", 0,
		PAGE_PLAY, KIND_TEXT },
	/* (network.coop_public, OpenCE's: co-op hosted online is private
	unless chosen with X on its waiting screen, which sets this row; it
	takes effect at once: port/linux/game/coop_menu.c) */
	{ "  Co-op visibility", "HALO_NET_COOP_PUBLIC", 0, 2, { "false", "true" }, { "Private", "Public" },
		"Co-op online: Private, by code; Public, listed too", 0, PAGE_PLAY },

	/* (custom maps: the Custom Edition maps join the multiplayer level
	list; off by default while their colours are wrong on the Vita; the
	maps themselves follow on the page, page_lines) */
	/* (the mod manager: the maps folder's custom maps and the mods folder's
	MCC mods with the game's own checks, turned off and on, deleted;
	port/linux/game/mod_manager.c answers, mod_manager_share.h) */
	PAGE_ROW("Mod manager", "Custom maps and MCC mods: check, on, off, delete", PAGE_MAPS, PAGE_MODS),
	{ "PC maps", "HALO_CUSTOM_EDITION", 0, 2, { "0", "1" }, { "Off", "On" },
		"Experimental: Custom Edition maps in the map list", 0, PAGE_MAPS },
	/* (the Halo content folder, port/linux/game/mcc_content.c: the
	player's own MCC folder, halo1 in it, its maps listed with the others;
	found by itself in ux0:data/haloce-vita/mcc, or one of these. No
	switch: the page's lines say where the content is read from) */
	{ "Halo content folder", "HALO_CONTENT_ROOT", 0, 4, { "auto", "ux0:data/mcc", "uma0:data/mcc", "imc0:data/mcc" },
		{ "Found by itself", "ux0:data/mcc", "uma0:data/mcc", "imc0:data/mcc" },
		"Your MCC folder with halo1 in it; only read. Default: haloce-vita/mcc", 0, PAGE_MAPS },
	/* (a mod's Halo: CE Anniversary maps in ux0:data/haloce-vita/mods,
	offered after the campaign's levels and played alone, offline:
	port/linux/game/custom_edition_mods.c; off by default, and shown only
	while the mods folder has a folder in it or the row is on:
	setting_shown) */
	{ "MCC mods", "HALO_MCC_MODS", 0, 2, { "0", "1" }, { "Off", "On" },
		"Experimental: mods folder's MCC maps, solo, offline", 0, PAGE_MAPS },
	/* (a Custom Edition map's vehicles in a local game: Halo PC's Banshees,
	Rocket Warthogs and gun turrets, port/linux/game/custom_edition_objects.c;
	shown while PC maps is on or the row is not Xbox: setting_shown) */
	{ "PC map vehicles", "HALO_PC_MAP_VEHICLES", 0, 3, { "xbox", "pc", "all" }, { "Xbox", "PC", "Allowed" },
		"Local games: PC maps' Banshees, turrets. Allowed: all the map allows", 0, PAGE_MAPS },
	/* (shown while a Custom Edition installer is in ux0:data/haloce-vita
	and the resource maps are missing: vita_ce_installer.c) */
	{ "Extract PC files", NULL, 0, 0, { NULL }, { NULL }, "The three files out of the Halo CE installer",
		0, PAGE_MAPS, KIND_ACTION, ACTION_CE_EXTRACT },
	/* (a host's offer of its map, in a game this Vita joined:
	port/linux/game/map_share.c; the help is each choice's own,
	map_downloads_help) */
	{ "Map downloads", "HALO_MAP_SHARE_FROM", 0, 3, { "ask", "private", "never" }, { "Ask", "Not public games", "Never" },
		"A host's map, when you join without it", 0, PAGE_MAPS },

	{ "Performance logging", PERFORMANCE_LOG, 1, 2, { "", "1" }, { "Off", "On" },
		"Frame, render and tick timing in halo.log (at start-up)", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Crash dump on hang", "HALO_HANG_CRASH", 0, 2, { "", "1" }, { "Off", "On" },
		"A hang of 8 s crashes for a dump (psp2core) to send", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "FPS overlay", "XV_FPS", 0, 3, { "0", "2", "1" }, { "Off", "FPS only", "Full" },
		"Top right. Full: tick and render times, the cores' load", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Debug camera", "HALO_DEBUG_CAMERA", 0, 2, { "0", "1" }, { "Off", "On" },
		"Hold Black 1 s: follow, orbit, then a flying camera", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	/* (vita_clocks.c vita_host_battery_log: the charge in halo.log once a
	minute; Auto, the variable unset: on in a pre-release build) */
	{ "Battery log", "HALO_BATTERY_LOG", 0, 3, { "", "1", "0" }, { "Auto", "On", "Off" },
		"Charge left in halo.log each minute. Auto: test builds", 0, TAB_DEV, KIND_CHOICE, ACTION_NONE, 1 },
	/* (the system's ad hoc dialog's way of joining, for testers) */
	{ "Ad hoc dialog", "HALO_ADHOC_DIALOG_MODE", 0, 3, { "0", "1", "2" }, { "Connect", "Create", "Join" },
		"How the system dialog joins: try Connect first", 0, TAB_DEV },
	{ "Save report", NULL, 0, 0, { NULL }, { NULL }, "Logs, settings and the newest crash dump in one folder", 0,
		TAB_DEV, KIND_ACTION, ACTION_SAVE_REPORT },
	PAGE_ROW("A/B switches", "Renderer switches a bug report may ask for", TAB_DEV, PAGE_DEV_AB),
	{ "GPU W clamp", "HALO_GXM_WCLAMP", 1, 2, { "", "0" }, { "Default", "Off" },
		"A/B: models close to the camera dropping out (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Target mip minimum", "HALO_TARGET_CHAIN_MIN_SIZE", 1, 3, { "", "16", "8" }, { "32 px", "16 px", "8 px" },
		"A/B: smallest mip level of render targets (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Frame phase lock", "HALO_FRAME_PHASE_LOCK", 1, 2, { "", "0" }, { "On", "Off" },
		"A/B: 30 FPS frames kept between two ticks (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
	{ "Render target sync", "HALO_GXM_RTT_SYNC", 1, 2, { "", "0" }, { "On", "Off" },
		"A/B: a scene waits for a target drawn before (at start-up)", 0, PAGE_DEV_AB, KIND_CHOICE, ACTION_NONE, 1 },
};

#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))

/* the page or tab a page's row is on (its own tab for a tab, or a page no
row opens) */
static int page_parent(int opened)
{
	int index;

	if (opened < TAB_COUNT)
		return opened;
	for (index = 0; index < (int)(sizeof(settings) / sizeof(settings[0])); index++)
	{
		if (settings[index].action == ACTION_PAGE && settings[index].opens == opened)
			return settings[index].page;
	}
	return TAB_GAME;
}

/* each row's choice as shipped (Reset controls), kept before anything is
loaded */
static int shipped_choice[SETTING_COUNT];
static int shipped_kept;

/* the profiles (the Profile row's first three choices): the value of each
row a profile sets. Balanced is the release's defaults (the rows' own).

Measured for 1.1.0-beta.4 (Oct 2026: the Vita's render path's CPU work
in the Linux build on a Raspberry Pi 4 and on x86, fixed ticks, a row at a
time from Performance; a10, b30, c10, 15-bot Blood Gulch CTF and Chill
Out): the old Balanced cost the Pi's CPU only 6-12% more than Performance
in the campaign (15-22% in the bots games, most of it Full particles, the
game's own work), yet the Vita ran it at 22-26 fps to Performance's 34. It
is the graphics chip's scenes: Object shadows (Near only: 1.5 more scenes
a frame on a10, 3 to 19 target changes a frame in a Chill Out fight) and
Sun rays (seven 64x64 scenes while the sun is in view) each end the main
scene and store it to memory, and halo.log's gxm line shows those frames
waiting in the display queue ("queue" 20-57 ms a frame at 15-26 fps; its
"waiting for the GPU" is measured after that wait, hence 0.00). So
Balanced adds only what makes no extra scene and little CPU (Pi +0-5%);
Quality spends its CPU (Pi +3-12%) and the old Balanced's scenes on what
shows most - shadows near, sun rays, High models, 8 lights - and not on
the rows that cost much and show little (Full particles in big fights,
sound occlusion every tick, all lights, Full shadows' extra targets,
scenery and lighting every tick: the old Quality) */
#define PROFILE_CUSTOM 3
#define PROFILE_ROWS 15

static const char *const profile_variables[PROFILE_ROWS] = {
	"HALO_RENDER_SCALE", "HALO_MODEL_LOD_SCALE", "HALO_MIN_OBJECT_PIXELS", "HALO_SCENERY_UPDATE_DIVISOR",
	"HALO_LIGHTING_REFRESH_DIVISOR", "HALO_SOUND_OBSTRUCTION_TICKS", "HALO_SUN_RAYS",
	"HALO_VITA_SHADOWS", "HALO_MAX_SCENE_LIGHTS", "HALO_VITA_EFFECTS_QUALITY", "HALO_PARTICLE_RENDER_DIVISOR",
	"HALO_AI_THINK_DIVISOR", "HALO_SOUND_MANAGER_DIVISOR", "HALO_PARTICLE_BUDGET", "HALO_DYNAMIC_RES_MIN",
};

/* (a settings.txt saved with a profile takes the profile's values as this
build has them, rows added or changed since: vita_settings_load) */
static const char *const profile_values[PROFILE_CUSTOM][PROFILE_ROWS] = {
	/* Performance: Dynamic resolution from 50%, Low, Small, Quarter, Third,
	Every 6th, no sun rays; no object shadows, 4 lights, Performance
	effects, half the particles drawn, Adaptive AI, sound every 2nd frame;
	Reduced particles */
	{ "dynamic", "0.5", "8", "4", "3", "6", "0", "0", "4", "1", "2", "0", "2", "reduced", "0.5" },
	/* Balanced (the release's defaults): Performance's scenes and nearly its
	CPU (Pi +1-5%), with Dynamic resolution from 63%, Medium models, sound
	occlusion every 3rd tick, Full effects (reflections, specular, grass)
	and every particle drawn: Small, Quarter, Third, no sun rays, no object
	shadows, 4 lights, Adaptive AI, sound every 2nd frame, Reduced
	particles */
	{ "dynamic", "0.75", "8", "4", "3", "3", "0", "0", "4", "2", "1", "0", "2", "reduced", "0.625" },
	/* Quality: Balanced with shadows near, sun rays, High models, 8 lights,
	Tiny, Half, Half and sound every frame (Pi CPU +3-12%, its scenes the
	old Balanced's); Dynamic resolution from 75%, sound occlusion every 3rd
	tick, Adaptive AI, Reduced particles (Full: +16% more in a 15-bot
	Chill Out fight, +30% in all). The Xbox's every row (Custom) was too
	slow on both chips */
	{ "dynamic", "1", "4", "2", "2", "3", "1", "1", "8", "2", "1", "0", "1", "reduced", "0.75" },
};

/* the variables the performance logging switch sets, and their values */
/* (HALO_LOAD_PROFILE: a frame over 100 ms names its waits for reads -
textures, sounds, the cache file thread - with halo.log's frame-hitch line,
the game thread's steps that frame) */
#define PERFORMANCE_LOG_VARIABLES 4
static const char *const performance_log_variables[PERFORMANCE_LOG_VARIABLES][2] = {
	{ "HALO_FRAME_TIMING", "300" },
	{ "HALO_RENDER_PROFILE", "1" },
	{ "HALO_TICK_PROFILE", "1" },
	{ "HALO_LOAD_PROFILE", "1" },
};

/* the release's fixed defaults (not in the panel) */
static const char *const fixed_defaults[][2] = {
	{ "HALO_TICK_THREAD", "1" },
	{ "HALO_NO_VSYNC", "1" },
	{ "HALO_STATIC_SCENERY", "1" },
	/* (the netcode is the distributed one, the only one network version 9
	plays: a match's client once could not reach its own host because the
	Vita refused a sendto on a connected datagram socket, fixed in
	vita_net.c) */
};

static int panel_open;
static unsigned long long both_since, last_move, last_shown;
static unsigned long previous_buttons;
/* the buttons down when the panel closed: the game has them only once
they are let go (the cross that opened System Link is not the game's A) */
static unsigned long held_after_close;
static int restart_pending;
/* port: the offer to restart at once (close_panel): 0 none made since the
last change that needs one, 2 asked and waiting, 1 answered (not asked
again until another such change). The game's main menu, idle, is where a
restart loses nothing (halo_main_menu_idle, source/main/main.c); the
restart itself is vita_host_restart_game's (vita_ce_installer.c) */
static int restart_offered;
int halo_main_menu_idle(void);
int vita_host_restart_game(void);
void vita_settings_question(const char *title, const char *text);
/* the code screen (or the public games) came from Join a game with the
connection Online: a code taken goes on to Join a game's steps */
static int code_for_join;

/* the tab shown, the page of it shown (the tab's own, or a page one of its
rows opened), the line chosen on each page, and the screens an action
opens */
static int tab, page;
static int page_selected[PAGE_COUNT];
enum
{
	SCREEN_LIST,
	SCREEN_CODE,
	SCREEN_BROWSE,
	SCREEN_DELETE,
	SCREEN_GUIDE,
	/* a large PC game chosen in the public games (crossplay_guard.h): join
	anyway? */
	SCREEN_LARGE,
	/* a map variant to delete (the Forge page's File Share) */
	SCREEN_FORGE_DELETE,
	/* the Forge game's characters all cleared: asked first */
	SCREEN_FORGE_CLEAR,
	/* the Mod manager page's chosen entry: its facts; its delete's
	question */
	SCREEN_MOD_DETAILS,
	SCREEN_MOD_DELETE,
	/* the Friends page: a friend's code typed; the question before removing
	or blocking one */
	SCREEN_FRIEND_CODE,
	SCREEN_FRIEND_CONFIRM,
	/* the Friends page's Friend code backup: its steps (friend_backup) */
	SCREEN_FRIEND_BACKUP,
	/* the Party page: the question before the leader removes a member */
	SCREEN_PARTY_CONFIRM,
	SCREEN_BAN_CONFIRM,
};
static int screen;
/* the code being typed (eight characters of P2P_CODE_ALPHABET) and the
character the cursor is on */
static char code_typed[P2P_CODE_LENGTH_TYPED + 1] = "AAAAAAAA";
static int code_cursor;
/* the public games as last shown (OpenCE's server browser: p2p.h's
p2p_lobby_entry, every text printable ASCII), and the one chosen */
#define BROWSE_LINES 8
static struct p2p_lobby_entry browse_entries[BROWSE_LINES];
static int browse_count, browse_selected;
/* the chosen game asks for its password: the keyboard is up for it */
static int browse_join_pending;
/* the large PC game asked about (SCREEN_LARGE) */
static struct p2p_lobby_entry browse_large;
/* a line about the last action (a code looked up ...), shown in the help
line for a few seconds */
static char notice[160];
static unsigned long long notice_until;
/* the network the game started with (HALO_VITA_NETWORK at load): what
internet or ad hoc play can do this session */
static char running_network[8] = "wifi";
/* the Vita's user name (the system's), which other players see */
static char vita_name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE + 1];
/* Host a game or Join a game: the guide's action (ACTION_HOST or
ACTION_JOIN); whether the game was asked for its System Link screen and
when; why it did not open ("" while nothing went wrong); and the request to
make once the ad hoc dialog has joined the room's group (0: none) */
static int guide_action, guide_waiting, adhoc_pending;
static unsigned long long guide_sent;
static char guide_problem[128];

/* ---------- the Play page's lines of text

The lobby name and the password (KIND_TEXT rows): typed on the system's
keyboard (vita_ime.c), kept as their printable ASCII characters, no longer
than PLAY_TEXT_LENGTH (the game lists' name field holds 15), in the
environment and in settings.txt. The lobby name is the Vita's user name
until one is typed (or "Halo" when the user name has no such character);
it is saved only once typed or loaded, so a new user name shows through.
An empty password is none. */

#define PLAY_TEXT_LENGTH 15

static struct play_text
{
	const char *variable;
	char value[PLAY_TEXT_LENGTH + 1];
	/* typed, or named by settings.txt: saved */
	int named;
} play_texts[] = {
	{ "HALO_NET_LOBBY_NAME", "", 0 },
	{ "HALO_NET_LOBBY_PASSWORD", "", 0 },
};

#define PLAY_TEXT_COUNT ((int)(sizeof(play_texts) / sizeof(play_texts[0])))

/* the keyboard's row while it is open (the setting), else NULL */
static const struct setting *ime_setting;

/* ---------- modded maps */

#define MAXIMUM_MAPS 64
#define MAP_NAME_SIZE 48
/* the map lines shown at once (the list scrolls) */
#define MAP_LINES 8

enum
{
	MAP_XBOX,
	MAP_CUSTOM_EDITION,
	MAP_OPENSAUCE,
	MAP_OTHER,
	/* a Custom Edition cache of a solo scenario: a campaign map (the
	Campaign's level list's, port/linux/game/custom_edition_maps.c) */
	MAP_CUSTOM_EDITION_CAMPAIGN,
};

struct map_entry
{
	char name[MAP_NAME_SIZE];
	char extension[8];
	unsigned long long size;
	int format;
};

static struct map_entry maps[MAXIMUM_MAPS];
static int map_count, map_scroll;
/* the Custom Edition resource maps missing from the folder ("" when all
are there), and whether any map listed is a Custom Edition one */
static char maps_missing[64];
static int maps_have_custom_edition;
/* a Custom Edition installer to take them from: vita_ce_installer_state's
(0 none, 1 there, 2 being read) and its file name */
static int maps_installer_state;
/* whether the mods folder (DATA_DIRECTORY/mods) has a folder in it: the
MCC mods row is shown then */
static int maps_mods_present;
/* (the Halo content folder: where it is read from, and Halo CE's maps in it,
-1 for no such folder) */
static char maps_content_folder[64];
static int maps_content_halo1;
static char maps_installer[64];
/* the maps turned off: their names, commas between (HALO_MAPS_DISABLED) */
static char maps_disabled[1024];

/* ---------- friends

The Friends page (Multiplayer > Friends; port/linux/src/p2p_friends.h): the
code, Add a friend (the code typed with the D-pad, as Join with a code),
Appear offline and Friends can join, then the entries (requests, invites,
friends online first, requests sent, blocked), each its name and a word of
its state, the chosen one's doing on the help line. Cross does an entry's
first thing (accept, join, unblock), square its second (decline, invite,
ignore, cancel), triangle removes a friend or blocks a request after a
question. A join (a friend's game, an invite) goes on as Join with a code's,
to the guide's steps. */
/* (the page's three rows, its three lines and these, with the line saying
which are shown, are a screen's height) */
#define FRIEND_LINES 4
static struct p2p_friend_entry friends_view[P2P_FRIEND_ENTRIES_MAXIMUM];
static int friends_count;
static int friends_scroll;
/* the code being typed (P2P_FRIEND_CODE_LENGTH characters) and its cursor */
static char friend_code_typed[P2P_FRIEND_CODE_LENGTH + 1] = "AAAAAAAAAAAA";
static int friend_code_cursor;
/* the question's entry and what Cross does there (P2P_FRIENDS_REMOVE or
P2P_FRIENDS_BLOCK) */
static struct p2p_friend_entry friend_confirm_entry;
static int friend_confirm_action;
/* Friend code backup (p2p_friends.h, p2p_friends_backup.inc): back up
(cross) or restore (square); the file's name, its password on the system's
keyboard (and again, to back up), with the friends list or not; the work
watched; a restore opened asks before it replaces anything. What was typed
is wiped once used */
enum
{
	FRIEND_BACKUP_MENU,
	FRIEND_BACKUP_NAME,
	FRIEND_BACKUP_PASSWORD,
	FRIEND_BACKUP_AGAIN,
	FRIEND_BACKUP_LIST,
	FRIEND_BACKUP_WORKING,
	FRIEND_BACKUP_CONFIRM,
	FRIEND_BACKUP_DONE,
};
static struct
{
	int step;
	int restore;
	/* the keyboard up for the step; to be opened at the next frame */
	int ime_pending;
	int ime_next;
	char name[P2P_FRIENDS_BACKUP_NAME_SIZE];
	char password[P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM + 1];
	/* (CONFIRM) the codes, the friends in it; (DONE) what came of it */
	char code[P2P_FRIEND_CODE_SIZE], current[P2P_FRIEND_CODE_SIZE];
	int friend_count;
	char said[256];
} friend_backup;

/* ---------- the party

The Party page (Multiplayer > Party; port/linux/src/p2p_party.h): Leave the
party, the party's word (its size and leader), then the party invites
waiting (cross joins the party, square says no), the members (each with a
word: leader, you, muted, why they could not follow), and the friends online
who can be invited (cross invites). Cross on a member mutes or unmutes them
(here only), triangle (the leader's) removes them after a question. */
#define PARTY_FRIEND_LINES 4
static struct p2p_party_member party_members_view[P2P_PARTY_MAXIMUM];
static int party_members_count;
static struct p2p_party_invite party_invites_view[P2P_FRIEND_INVITES_MAXIMUM];
static int party_invites_count;
/* the friends who can be invited (indices into friends_view) */
static int party_friends_view[P2P_FRIEND_ENTRIES_MAXIMUM];
static int party_friends_count;
static int party_friends_scroll;
static struct p2p_party_member party_confirm_member;
/* the quick chat preset shown on its line (left and right change it, cross
sends it); a message being typed for the party */
static int party_preset;
static int party_message_pending;
/* a friend's game with a password: the keyboard up for it (its friend's id
and name), then the join it opens */
static int friend_password_pending;
static char friend_password_id[P2P_FRIEND_ID_SIZE], friend_password_name[P2P_FRIEND_NAME_SIZE];
/* the chat lines the page shows (the newest) */
#define PARTY_CHAT_PAGE_LINES 3
/* (the party's leader) a friend's game warned of: fewer open slots than the
party; a second press joins it */
static char party_warned_id[P2P_FRIEND_ID_SIZE];
static unsigned long long party_warned_time;

/* ---------- the mod manager */

/* The Mod manager page (Multiplayer > Modded maps > Mod manager): what the
game's look found (mod_manager_share.h: the game's main loop answers the
panel's requests, mod_manager.c), each entry a line - its name, on or off
(or Error when it failed the game's checks), its size and kind - the chosen
one's check or needs on the help line. Left and right turn it off or on
(HALO_MAPS_DISABLED for a map, HALO_MODS_DISABLED for a mod: kept here, in
settings.txt), cross shows its details, square deletes it after a question
(the game deletes it: only inside our maps and mods folders). */
static struct halo_mods_inventory mods_view;
/* 0 not looked yet, 1 asked (the game's answer awaited), 2 shown */
static int mods_state;
static int mods_scroll;
/* the mods turned off: their keys, commas between (HALO_MODS_DISABLED) */
static char mods_disabled[1024];

/* the Xbox's own maps, and the Custom Edition resource maps: never listed */
static const char *const stock_maps[] = {
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"ui", "bitmaps", "sounds", "loc",
};

/* ---------- the Player model row (port/linux/game/player_models.c) */

/* the game's player models, the Spartan first (0): how many, each one's
name, the first local player's profile's choice; and the choice made (the
game takes it at the top of a frame, for that profile) */
int player_models_panel_count(void);
const char *player_models_panel_name(int index);
int player_models_panel_chosen(void);
void player_models_panel_choose(int index);

static char player_model_values[MAXIMUM_CHOICES][4];
static char player_model_names[MAXIMUM_CHOICES][48];

/* ---------- the Armor color and Visor color rows
(port/linux/game/player_cosmetics.c) */
int player_cosmetics_panel_color_count(void);
const char *player_cosmetics_panel_color_name(int index);
int player_cosmetics_panel_visor_count(void);
const char *player_cosmetics_panel_visor_name(int index);
int player_cosmetics_panel_color_chosen(void);
int player_cosmetics_panel_visor_chosen(void);
void player_cosmetics_panel_choose_color(int index);
void player_cosmetics_panel_choose_visor(int index);

static char cosmetics_values[2][MAXIMUM_CHOICES][4];

/* (the rows the profile keeps, not settings.txt) */
static int profile_row(const char *variable)
{
	return variable && (!strcmp(variable, "HALO_PLAYER_MODEL") || !strcmp(variable, "HALO_ARMOR_COLOR") ||
		!strcmp(variable, "HALO_VISOR_COLOR"));
}

/* ---------- Forge and File Share (the Forge page) */

/* forge.c's maps (the lobby's: the Xbox's multiplayer maps), by index: how
many, each one's name (English), short name (bloodgulch) and whether its
file is in the maps folder */
int forge_panel_map_count(void);
const char *forge_panel_map_name(int map_index);
const char *forge_panel_map_short(int map_index);
int forge_panel_map_available(int map_index);
/* the Forge game's placed characters alive and dead (FALSE: no Forge game
is played), and its dead or all of them cleared, at its next update */
int forge_panel_ai_counts(int *alive, int *dead);
void forge_panel_clear_ai(int all);
/* (system_link_shortcut.c) the Forge game asked for: its map's index and
its map variant's file name ("" none), with SYSTEM_LINK_REQUEST_FORGE */
extern volatile int halo_forge_request_map;
extern char halo_forge_request_variant[HALO_FORGE_VARIANT_SIZE];
/* ... and whether on the network (the connection the game runs: a network
Forge game others join; 0, a local game) */
extern volatile int halo_forge_request_network;

/* the map variants listed (File Share's), the ones shown at once */
#define FORGE_PANEL_MAXIMUM 32
#define FORGE_PANEL_LINES 6

enum
{
	FORGE_LINE_MAP,
	FORGE_LINE_VARIANT,
	/* (Local, or the network: the connection's, others joining) */
	FORGE_LINE_NETWORK,
	FORGE_LINE_START,
	/* (while a Forge game is played) */
	FORGE_LINE_CLEAR_DEAD,
	FORGE_LINE_CLEAR_ALL,
	FORGE_LINE_COUNT
};

static struct forge_store_entry forge_entries[FORGE_PANEL_MAXIMUM];
static int forge_entry_count, forge_scroll;
/* the map chosen (forge.c's index; -1: none in the maps folder), its
variant (0 none, else the nth of the map's that is not damaged), and the
variant asked about (SCREEN_FORGE_DELETE) */
static int forge_map = -1, forge_variant, forge_delete_index = -1;
/* (Network) Start Forge's game a network Forge game (1) or a local one (0) */
static int forge_network;

/* ---------- the report */

enum
{
	REPORT_IDLE,
	REPORT_SAVING,
	REPORT_SAVED,
	REPORT_FAILED,
};

static volatile int report_state;
static char report_path[96];
static int report_files;
static unsigned char report_buffer[64 * 1024];

static unsigned long long now_us(void)
{
	return sceKernelGetProcessTimeWide();
}

/* a line on the last action, shown in the help line for a few seconds */
static void set_notice(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(notice, sizeof(notice), format, arguments);
	va_end(arguments);
	notice_until = now_us() + 4000000ULL;
}

static int find_choice(const struct setting *setting, const char *value)
{
	int index;

	for (index = 0; index < setting->count; index++)
		if (strcmp(setting->values[index], value) == 0)
			return index;
	/* (Menu style Halo 3, saved by an older build: ElDewrito replaced it) */
	if (setting->variable && !strcmp(setting->variable, "HALO_MENU_STYLE") && !strcmp(value, "halo3"))
		return find_choice(setting, "online");
	/* (a value not on the list, from env.txt: the nearest by number) */
	{
		double wanted = atof(value), best_distance = 1e30;
		int best = setting->choice;

		for (index = 0; index < setting->count; index++)
		{
			double distance = atof(setting->values[index]) - wanted;

			/* (Render resolution's Dynamic is no number's nearest) */
			if (!strcmp(setting->values[index], "dynamic"))
				continue;

			if (distance < 0)
				distance = -distance;
			if (distance < best_distance)
			{
				best_distance = distance;
				best = index;
			}
		}
		return best;
	}
}

static struct setting *setting_named(const char *variable)
{
	int index;

	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].variable && strcmp(settings[index].variable, variable) == 0)
			return &settings[index];
	return NULL;
}

static int choice_of(const char *variable)
{
	struct setting const *setting = setting_named(variable);

	return setting ? setting->choice : 0;
}

/* the network chosen in the panel (this session's, running_network, until
a restart) */
static const char *chosen_network(void)
{
	const struct setting *setting = setting_named("HALO_VITA_NETWORK");

	return setting->values[setting->choice];
}

/* ---------- the language

The port's own text in the Language row's language (lang.c): each language
whose file is in app0:lang is a choice, under its own name, and Automatic
(the default) is the Vita's system language when it is one of them, else
English. A change is live: the panel, and every message after it, in the
new language at once. The game's own language (xbox_xapi.c XGetLanguage:
the map folder of a PAL disc's language, maps_es, and its movies) is the
one at start: HALO_LANGUAGE, and HALO_SYSTEM_LANGUAGE for Automatic. */

/* the Vita's system languages (SCE_SYSTEM_PARAM_LANG_*, 0 to 19) as
lang.c's codes */
static const char *const system_language_codes[] = {
	"ja", "en", "fr", "es", "de", "it", "nl", "pt", "ru", "ko", "zh-TW", "zh-CN", "fi", "sv", "da", "no", "pl",
	"pt-BR", "en", "tr",
};

static char system_language[8] = "en";
static int language_ready;

/* the Language row's choices: Automatic, then English and each language
found (lang_count's order) */
static void language_choices(void)
{
	struct setting *setting = setting_named("HALO_LANGUAGE");
	int index;

	for (index = 0; index < lang_count() && index + 1 < MAXIMUM_CHOICES; index++)
	{
		setting->values[index + 1] = lang_code(index);
		setting->names[index + 1] = lang_name(index);
	}
	setting->count = index + 1;
}

/* the language of the Language row's choice, chosen */
static void language_apply(void)
{
	const struct setting *setting = setting_named("HALO_LANGUAGE");
	const char *value = setting->values[setting->choice];

	lang_select(strcmp(value, "auto") ? value : system_language);
}

/* the language files read, and the Vita's language chosen (vita_main.c,
before anything is shown: the missing data screen's too); settings.txt's
choice follows in vita_settings_load */
void vita_settings_language_init(void)
{
	int value = -1;
	char line[160];
	int length, index;

	if (language_ready)
		return;
	language_ready = 1;
	lang_init(LANG_DIRECTORY);
	if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &value) >= 0 && value >= 0 &&
		value < (int)(sizeof(system_language_codes) / sizeof(system_language_codes[0])))
		snprintf(system_language, sizeof(system_language), "%s", system_language_codes[value]);
	setenv("HALO_SYSTEM_LANGUAGE", system_language, 1);
	lang_select(system_language);
	language_choices();
	length = snprintf(line, sizeof(line), "vita: system language %s (%d); the port's text in:", system_language,
		value);
	for (index = 0; index < lang_count() && length < (int)sizeof(line); index++)
		length += snprintf(line + length, sizeof(line) - (size_t)length, " %s", lang_code(index));
	vita_host_log(line);
	if (lang_problems(NULL, 0))
	{
		char problems[512];

		lang_problems(problems, sizeof(problems));
		snprintf(line, sizeof(line), "vita: language files: %.120s", problems);
		vita_host_log(line);
	}
}

/* ---------- the Play page's listing

The hosted game as others see it (its name, most players, public or not,
password) and the public games, over internet play's calls (p2p.h): OpenCE's
server browser, ported (signed listings, passwords, the map, rules and
players; entries by the host's key, id, joined by it with a password). */


static struct play_text *play_text_named(const char *variable)
{
	int index;

	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		if (!strcmp(play_texts[index].variable, variable))
			return &play_texts[index];
	return NULL;
}

/* text as the Play page keeps it: its printable ASCII characters, the
spaces at either end left out, PLAY_TEXT_LENGTH at most */
static void play_sanitize(char *out, int size, const char *text)
{
	int length = 0, limit = size - 1 < PLAY_TEXT_LENGTH ? size - 1 : PLAY_TEXT_LENGTH;

	for (; text && *text && length < limit; text++)
		if ((unsigned char)*text >= 0x20 && (unsigned char)*text <= 0x7E && !(*text == ' ' && length == 0))
			out[length++] = *text;
	while (length > 0 && out[length - 1] == ' ')
		length--;
	out[length] = 0;
}

static void play_listing_set_name(const char *name)
{
	p2p_lobby_set_name(name);
}

static void play_listing_set_public(int listed)
{
	p2p_lobby_set_public(listed);
}

/* (a co-op game's own: network.coop_public) */
static void play_listing_set_coop_public(int listed)
{
	p2p_lobby_set_coop_public(listed);
}

/* (internet play works the password's key out on its own thread, once it
runs) */
static void play_listing_set_password(const char *password)
{
	p2p_lobby_set_password(password);
}

/* joins a public game (with the password typed, for a locked one) */
static void play_join_entry(const struct p2p_lobby_entry *entry, const char *password)
{
	p2p_lobby_join(entry->id, password ? password : "");
}

/* how the last join of a public game went: -1 the password was wrong, -2
the game is gone, else 0 */
static int play_join_state(void)
{
	int state = p2p_lobby_join_state();

	return state == P2P_LOBBY_JOIN_WRONG_PASSWORD ? -1 : state == P2P_LOBBY_JOIN_GONE ? -2 : 0;
}

/* the Play page's rows that apply: internet play's while the connection
is Online (this session's or the one chosen), the password while the game
is public too */
static int network_is(const char *network)
{
	return !strcmp(running_network, network) || !strcmp(chosen_network(), network);
}

/* whether a row is shown: the ad hoc room while the connection is Ad hoc
(chosen or this session's), the Play page's internet play rows while it is
Online (network_is), its password while the game is public too; Dynamic
minimum while Render resolution is Dynamic (each keeps its value
meanwhile) */
static int classic_panel(void);
static int online_panel_style(void);
static int original_style(void);
static int pc_menus_shown(void);

static int setting_shown(const struct setting *setting)
{
	/* (Forge and File Share: the Halo 3 main menu has them; for the
	original menus, the Halo CE Menu style, the panel does) */
	if (setting->action == ACTION_PAGE && setting->opens == PAGE_FORGE)
		return classic_panel();
	/* (the Halo 3 menus' report: with Menu style Halo 3) */
	if (setting->variable && (!strcmp(setting->variable, "HALO_CARNAGE_REPORT") ||
		!strcmp(setting->variable, "HALO_SCOREBOARD")))
		return !classic_panel();
	/* (the live backdrop: the Online menus') */
	if (setting->variable && !strcmp(setting->variable, "HALO_MENU_BACKDROP"))
		return online_panel_style();
	/* (the menu music's level under the live backdrop: ElDewrito's) */
	if (setting->variable && !strcmp(setting->variable, "HALO_MENU_MUSIC"))
		return online_panel_style();
	/* (and Menu style Halo CE's own two) */
	if (setting->variable && (!strcmp(setting->variable, "HALO_CLASSIC_CARNAGE_REPORT") ||
		!strcmp(setting->variable, "HALO_CLASSIC_SCOREBOARD")))
		return classic_panel() && !original_style();
	if ((setting->variable && !strcmp(setting->variable, "HALO_ADHOC_ROOM")) || setting->action == ACTION_ADHOC_JOIN)
		return network_is("adhoc");
	if (setting->action == ACTION_PAGE && setting->opens == PAGE_CROSSPLAY)
		return !network_is("adhoc");
	if (setting->action == ACTION_CE_EXTRACT)
		return maps_installer_state == 1;
	if (setting->variable && !strcmp(setting->variable, "HALO_MCC_MODS"))
		return maps_mods_present || choice_of("HALO_MCC_MODS");
	if (setting->variable && !strcmp(setting->variable, "HALO_PC_MAP_VEHICLES"))
		return choice_of("HALO_CUSTOM_EDITION") || choice_of("HALO_PC_MAP_VEHICLES");
	if (setting->variable && !strcmp(setting->variable, "HALO_PLAYER_MODEL"))
		return setting->count > 1;
	/* (the tester's flaming helmet: a listed tester's alone) */
	if (setting->variable && !strcmp(setting->variable, "HALO_FLAMING_HELMET"))
		return p2p_flame_listed();
	/* (skill, vehicles and routes: the offline bots' and Online bots'; on
	the tab under Bots while there are any, and on Vita and PC under Online
	bots while it fills (page_lines puts them there): the tab has no room
	for them with Bots off. Teams and the playlist: Split Screen's alone) */
	if (setting->variable && (!strcmp(setting->variable, "HALO_BOT_SKILL") ||
		!strcmp(setting->variable, "HALO_BOT_VEHICLES") || !strcmp(setting->variable, "HALO_BOT_ROUTES")))
		return page == PAGE_CROSSPLAY ? choice_of("HALO_BOTS_ONLINE") != 1 : choice_of("HALO_BOTS") != 0;
	if (setting->variable && (!strcmp(setting->variable, "HALO_BOT_TEAMS") ||
		!strcmp(setting->variable, "HALO_BOT_PLAYLIST")))
		return choice_of("HALO_BOTS") != 0;
	/* (a code typed in: online, on a Vita whose game has not got the PC
	menus' Direct Link, menu_tags.c) */
	if (setting->action == ACTION_JOIN_CODE)
		return network_is("online") && !pc_menus_shown();
	/* (friends: their presence, joins and invites go through internet play;
	in the code row's place, which the PC menus' Direct Link leaves out: the
	tab is a screen's height, and a game with the PC menus is one whose
	menus join friends' games. Online chosen and running) */
	if (setting->action == ACTION_PAGE && setting->opens == PAGE_FRIENDS)
		return !strcmp(running_network, "online") && !strcmp(chosen_network(), "online") && (pc_menus_shown() || original_style());
	if (setting->action == ACTION_BROWSE)
		return network_is("online");
	/* (a network Forge game hosted: never listed, its lobby's only - no
	Visibility, no password) */
	if (setting->variable && !strcmp(setting->variable, "HALO_NET_HOST_PUBLIC"))
		return network_is("online") && !halo_forge_net_hosting;
	if (setting->variable && !strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD"))
		return network_is("online") && choice_of("HALO_NET_HOST_PUBLIC") && !halo_forge_net_hosting;
	if (setting->variable && !strcmp(setting->variable, "HALO_DYNAMIC_RES_MIN"))
	{
		const struct setting *scale = setting_named("HALO_RENDER_SCALE");

		return !strcmp(scale->values[scale->choice], "dynamic");
	}
	return 1;
}

/* the last choice left and right reach: not the Profile row's Custom (read,
not chosen) */
static int choice_last(const struct setting *setting)
{
	if (setting == &settings[0])
		return PROFILE_CUSTOM - 1;
	return setting->count - 1;
}

/* a choice this build does not offer, shown greyed out: Stable updates on
a pre-release build (update_check.h) */
static int choice_disabled(const struct setting *setting, int choice)
{
	return setting->variable && !strcmp(setting->variable, "HALO_UPDATE_CHANNEL") && choice >= 0 &&
		choice < setting->count && !strcmp(setting->values[choice], "stable") &&
		update_build_is_prerelease(HALO_VITA_VERSION);
}

/* a setting's value as the environment variable (or variables) it is */
static void apply_value(const struct setting *setting)
{
	const char *value = setting->values[setting->choice];

	/* (the profile's, which the game keeps: player_models.c,
player_cosmetics.c; changed only by a press, change) */
	if (profile_row(setting->variable))
		return;
	if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
	{
		int index;

		/* (on: env.txt's own values stand, HALO_TICK_PROFILE=3 ...) */
		for (index = 0; index < PERFORMANCE_LOG_VARIABLES; index++)
			if (value[0])
				setenv(performance_log_variables[index][0], performance_log_variables[index][1], 0);
			else
				unsetenv(performance_log_variables[index][0]);
	}
	if (setting->dev && !value[0])
		unsetenv(setting->variable);
	else
		setenv(setting->variable, value, 1);
}

/* a Play page text set (typed, loaded or a test command's): kept
sanitized, in the environment, and at once where it shows (the lobby name
in the hosted game's listing: the game itself reads the environment each
frame of its lobby, network_server_manager.c) */
static void play_text_set(struct play_text *text, const char *value, int named)
{
	play_sanitize(text->value, sizeof(text->value), value);
	text->named = named;
	if (!strcmp(text->variable, "HALO_NET_LOBBY_NAME") && !text->value[0])
	{
		/* (none: the game's own name, its player's profile's, as the
		player's name in the game and the pre-game lobby are - network.
		lobby_name empty; it was the Vita's user name, and an insider.11
		co-op lobby was "Listed as: user549" for the profile "birch") */
		text->named = 0;
	}
	setenv(text->variable, text->value, 1);
	if (!strcmp(text->variable, "HALO_NET_LOBBY_NAME"))
		play_listing_set_name(text->value);
	else
		play_listing_set_password(text->value);
}

/* the profile the rows match, or Custom */
static int matching_profile(void)
{
	int profile, row;

	for (profile = 0; profile < PROFILE_CUSTOM; profile++)
	{
		for (row = 0; row < PROFILE_ROWS; row++)
		{
			const struct setting *setting = setting_named(profile_variables[row]);

			if (!setting || strcmp(setting->values[setting->choice], profile_values[profile][row]) != 0)
				break;
		}
		if (row == PROFILE_ROWS)
			return profile;
	}
	return PROFILE_CUSTOM;
}

/* a profile's values set in its rows; nonzero if a row that waits for a
restart changed */
static int apply_profile(int profile)
{
	int row, restart = 0;

	if (profile < 0 || profile >= PROFILE_CUSTOM)
		return 0;
	for (row = 0; row < PROFILE_ROWS; row++)
	{
		struct setting *setting = setting_named(profile_variables[row]);
		int choice;

		if (!setting)
			continue;
		choice = find_choice(setting, profile_values[profile][row]);
		if (choice != setting->choice)
		{
			setting->choice = choice;
			if (setting->restart)
				restart = 1;
		}
		apply_value(setting);
	}
	return restart;
}

/* ---------- settings.txt

Written off the game thread (Oct 20 2026; BlazeRed, 1.1.0-beta.6: the game
froze changing the performance preset or the gyro). Each change wrote the
file where it was made - the panel's left and right and the game's own
rows (Server Setup, the lobbies' network) on the game thread - and the
memory card's fopen, fclose, remove and rename held the frame: halo.log on
the testers' Vitas has a hitch after every such save, nothing else in it
(no texture, no shader, no wait for the worker): Server Setup's four rows
one frame of 114 ms (a save each 27 ms apart), 549 ms with the card busy,
a panel change 238 ms. Now the change makes the file's text (memory only,
save_lines) and a thread of its own writes it: a change while it writes is
written after, the newest text only (a press each frame is written once
or twice), and a text the same as the last one asked for is not written at
all (the game sets its rows as they are each time Server Setup opens).
vita_settings_save_wait waits for the writer: before settings.txt is read
again and before the game restarts. The thread is made at the first save
after the memory window (vita_host_memory_window_made: nothing may be made
before it); until then, or if it cannot be made, the file is written in
place as before.

No pthread_cond_wait here (insider.19's crash, Oct 21 2026: LadyCassie,
xal and the owner, any setting changed - in the panel, the game's rows,
the main menu's Appear offline - and the game gone). The writer is a thread
of sceKernelCreateThread's (vita_host_thread_start_stack), and so are Save
report's and the CE installer's, which wait for it: vitasdk gives a thread
its pthread data only when pthread_create made it or it has called
pthread_create itself, and pthread_cond_wait, a cancellation point, reads
it - NULL, a data abort in pte_osSemaphoreCancellablePend (both core dumps:
thread "halo settings", R4 0, at its first wait). So the writer sleeps on a
kernel semaphore (save_wake) and the waits for it (vita_settings_save_wait,
a text too long for it) look every millisecond; save_lock, a mutex, is
not a cancellation point and is safe on any thread. */

/* the most the text is (some 9 KB with every map and mod turned off): one
longer is written in place */
#define SAVE_TEXT_SIZE 16384

/* where save_lines puts the text: the file, or memory */
struct save_output
{
	FILE *file;
	char *text;
	int size;
	int length;
};

static void save_put(struct save_output *output, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	if (output->file)
		vfprintf(output->file, format, arguments);
	else if (output->length < output->size)
	{
		int length = vsnprintf(output->text + output->length, (size_t)(output->size - output->length), format,
			arguments);

		output->length = length < 0 ? output->size : output->length + length;
	}
	va_end(arguments);
}

/* settings.txt's lines: the build, the rows, the Play page's texts, the
maps and mods turned off */
static void save_lines(struct save_output *output)
{
	int index;

	save_put(output, "HALO_SETTINGS_BUILD=%s\n", HALO_VITA_VERSION);
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE && !(settings[index].dev && settings[index].choice == 0) &&
			!profile_row(settings[index].variable))
		{
			save_put(output, "%s=%s\n", settings[index].variable, settings[index].values[settings[index].choice]);
		}
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		if (play_texts[index].named && play_texts[index].value[0])
			save_put(output, "%s=%s\n", play_texts[index].variable, play_texts[index].value);
	if (maps_disabled[0])
		save_put(output, "HALO_MAPS_DISABLED=%s\n", maps_disabled);
	if (mods_disabled[0])
		save_put(output, "HALO_MODS_DISABLED=%s\n", mods_disabled);
	/* (the last line: a file with it was written whole - settings_source) */
	save_put(output, SETTINGS_END "=1\n");
}

/* settings.txt written anew - the text, or with none the rows as they are
now: into a file of its own (settings.txt.new), flushed to the card, then
put in place of the old one, which is kept as settings.txt.bak (a file
opened for writing is not cut short on Vita3K, which left the end of a
longer old file after a shorter new one; the Vita's rename does not write
over a file). Whenever the game stops - a crash, the battery, the power
button held - one of the three is whole (settings_source reads the one
that is); nonzero if it was put in place */
static int save_file(const char *text)
{
	FILE *file;
	int ok, kept;

	remove(SETTINGS_FILE ".new");
	file = fopen(SETTINGS_FILE ".new", "w");
	if (!file)
		return 0;
	if (text)
		fputs(text, file);
	else
	{
		struct save_output output = { file, NULL, 0, 0 };

		save_lines(&output);
	}
	ok = fflush(file) == 0 && !ferror(file);
	/* (on the card before the old one is moved) */
	if (ok)
		fsync(fileno(file));
	ok = fclose(file) == 0 && ok;
	if (!ok)
	{
		remove(SETTINGS_FILE ".new");
		return 0;
	}
	remove(SETTINGS_FILE ".bak");
	kept = rename(SETTINGS_FILE, SETTINGS_FILE ".bak") == 0;
	if (!kept)
		remove(SETTINGS_FILE);
	ok = rename(SETTINGS_FILE ".new", SETTINGS_FILE) == 0;
	/* (not put in place: the old one back where it was) */
	if (!ok && kept)
		rename(SETTINGS_FILE ".bak", SETTINGS_FILE);
	return ok;
}

/* the writer's state (save_lock): the text made by the last save, the
newest one asked for and the one being written; how many were asked for and
written; the writer 0 not made yet, 1 running, -1 none (written in place);
the semaphore it sleeps on, signalled when a text is asked for */
static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static SceUID save_wake = -1;
static char *save_text_made, *save_text_asked, *save_text_writing;
static unsigned long save_asked, save_written;
static int save_writer;

/* (save_lock held) until the writer has written what was asked for: a look
each millisecond (any thread may wait so; see above) */
static void save_written_wait(void)
{
	while (save_writer > 0 && save_written != save_asked)
	{
		pthread_mutex_unlock(&save_lock);
		vita_host_sleep_us(1000);
		pthread_mutex_lock(&save_lock);
	}
}

static void save_thread(void *argument)
{
	(void)argument;
	pthread_mutex_lock(&save_lock);
	for (;;)
	{
		unsigned long asked, changes;
		unsigned long long started;
		int ok;

		while (save_written == save_asked)
		{
			pthread_mutex_unlock(&save_lock);
			sceKernelWaitSema(save_wake, 1, NULL);
			pthread_mutex_lock(&save_lock);
		}
		asked = save_asked;
		changes = save_asked - save_written;
		memcpy(save_text_writing, save_text_asked, strlen(save_text_asked) + 1);
		pthread_mutex_unlock(&save_lock);
		started = now_us();
		ok = save_file(save_text_writing);
		{
			/* (the card's time, in halo.log when it would have been a
			hitch: what the game thread no longer waits for) */
			unsigned long long took = now_us() - started;

			if (!ok || took >= 100000)
			{
				char line[128];

				snprintf(line, sizeof(line), "settings: settings.txt %s in %.1f ms off the game thread (%lu change%s)",
					ok ? "written" : "NOT written", (double)took / 1000.0, changes, changes == 1 ? "" : "s");
				vita_host_log(line);
			}
		}
		pthread_mutex_lock(&save_lock);
		/* (not written: the next save writes it, the same text or not) */
		if (!ok && save_asked == asked)
			save_text_asked[0] = 0;
		save_written = asked;
	}
}

/* (save_lock held) the writer made, at the first save after the memory
window; nonzero if it runs */
static int save_writer_ready(void)
{
	if (save_writer == 0 && vita_host_memory_window_made())
	{
		save_text_made = malloc(SAVE_TEXT_SIZE);
		save_text_asked = calloc(1, SAVE_TEXT_SIZE);
		save_text_writing = malloc(SAVE_TEXT_SIZE);
		save_writer = -1;
		/* (a signal while one is pending is refused: the writer looks at
		save_asked each time it wakes) */
		if (save_text_made && save_text_asked && save_text_writing)
			save_wake = sceKernelCreateSema("halo settings", 0, 0, 1, NULL);
		if (save_wake >= 0 &&
			vita_host_thread_start_stack("halo settings", save_thread, NULL, -1, 0x10000100, 64 * 1024) >= 0)
		{
			save_writer = 1;
		}
		else
		{
			if (save_wake >= 0)
				sceKernelDeleteSema(save_wake);
			save_wake = -1;
			free(save_text_made);
			free(save_text_asked);
			free(save_text_writing);
			save_text_made = save_text_asked = save_text_writing = NULL;
			vita_host_log("settings: no thread for settings.txt: written where it changes");
		}
	}
	return save_writer > 0;
}

/* settings.txt as the rows are now, written by the writer (or in place
before it exists) */
static void save(void)
{
	pthread_mutex_lock(&save_lock);
	if (save_writer_ready())
	{
		struct save_output output = { NULL, save_text_made, SAVE_TEXT_SIZE, 0 };

		save_lines(&output);
		if (output.length < SAVE_TEXT_SIZE)
		{
			/* (the same as the last one asked for: nothing to write) */
			if (strcmp(save_text_made, save_text_asked) != 0)
			{
				memcpy(save_text_asked, save_text_made, (size_t)output.length + 1);
				save_asked++;
				sceKernelSignalSema(save_wake, 1);
			}
			pthread_mutex_unlock(&save_lock);
			return;
		}
		/* (too long for the writer: in place, once it is idle, and the
		next save written whatever it is) */
		save_written_wait();
		save_text_asked[0] = 0;
	}
	save_file(NULL);
	pthread_mutex_unlock(&save_lock);
}

/* waits until settings.txt has what was last asked for */
void vita_settings_save_wait(void)
{
	pthread_mutex_lock(&save_lock);
	save_written_wait();
	pthread_mutex_unlock(&save_lock);
}

/* the chosen network as the platform layer's settings (port_config.c):
internet play on, without UPnP, which the Vita does not have
(vita_stubs.c); or ad hoc play, which needs no internet */
static void apply_network(void)
{
	const char *network = getenv("HALO_VITA_NETWORK");

	snprintf(running_network, sizeof(running_network), "%s", network ? network : "wifi");
	/* (UPnP whatever the connection: it may go Online later, live) */
	setenv("HALO_NET_ALLOW_UPNP", "false", 0);
	if (!strcmp(running_network, "online"))
	{
		setenv("HALO_NET_ONLINE", "true", 1);
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		setenv("HALO_NET_ADHOC", "true", 1);
	}
	{
		/* a player with a default profile (no name) goes by the Vita's user
		name in network games (network_game_local_player_name) */
		char name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE + 1];

		memset(name, 0, sizeof(name));
		if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)name, sizeof(name) - 1) >= 0 &&
			name[0])
		{
			setenv("HALO_NET_PLAYER_NAME", name, 0);
			snprintf(vita_name, sizeof(vita_name), "%s", name);
		}
	}
	{
		char line[96];

		snprintf(line, sizeof(line), "vita: network %s (settings panel, Multiplayer: Connection)", running_network);
		vita_host_log(line);
	}
}

/* (in halo.log near its top: the dev switches on, if any - a log from a
tester's run in test mode says so) */
static void log_dev_switches(void)
{
	char message[400];
	int length = 0, index;

	for (index = 0; index < SETTING_COUNT && length < (int)sizeof(message); index++)
	{
		const struct setting *setting = &settings[index];

		if (!setting->dev || setting->choice == 0)
			continue;
		if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
		{
			int variable;

			length += snprintf(message + length, sizeof(message) - length, " %s=1 (", PERFORMANCE_LOG);
			for (variable = 0; variable < PERFORMANCE_LOG_VARIABLES && length < (int)sizeof(message); variable++)
			{
				const char *value = getenv(performance_log_variables[variable][0]);

				length += snprintf(message + length, sizeof(message) - length, "%s%s=%s", variable ? " " : "",
					performance_log_variables[variable][0], value ? value : "");
			}
			if (length < (int)sizeof(message))
				length += snprintf(message + length, sizeof(message) - length, ")");
		}
		else
		{
			const char *value = getenv(setting->variable);

			length += snprintf(message + length, sizeof(message) - length, " %s=%s", setting->variable,
				value ? value : setting->values[setting->choice]);
		}
	}
	if (length)
	{
		char line[460];

		snprintf(line, sizeof(line), "settings: TEST MODE, dev switches on:%s", message);
		vita_host_log(line);
	}
}

/* ---------- settings.txt read safely

(Oct 21 2026, M4J0R_l-l0LL4ND, insider.19: the game crashed as a setting
changed, and every start after it took the whole Vita down, C0-15008-4.)
Nothing settings.txt holds reaches the game unchecked: a file cut short, of
garbage, or with a value no build wrote is read as far as it makes sense and
the rest is the defaults, said in halo.log. */

/* what a file of settings is: none, there but no line of settings in it
(garbage, empty), lines but not the last (an older build's - before
insider.21 none had it - or one cut short), or whole */
enum
{
	SETTINGS_SOURCE_NONE,
	SETTINGS_SOURCE_GARBAGE,
	SETTINGS_SOURCE_PARTIAL,
	SETTINGS_SOURCE_WHOLE
};

/* whether line (NAME=value, its end trimmed) is one a build of this game
could have written: a name of capitals, digits and '_' */
static int settings_line_named(const char *line)
{
	const char *at = line;

	while ((*at >= 'A' && *at <= 'Z') || (*at >= '0' && *at <= '9') || *at == '_')
		at++;
	return at > line && *at == '=';
}

static int settings_source_kind(const char *path)
{
	FILE *file = fopen(path, "r");
	char line[1200];
	int kind;

	if (!file)
		return SETTINGS_SOURCE_NONE;
	kind = SETTINGS_SOURCE_GARBAGE;
	while (fgets(line, sizeof(line), file))
	{
		if (!settings_line_named(line))
			continue;
		if (strncmp(line, SETTINGS_END "=", sizeof(SETTINGS_END)) == 0)
		{
			kind = SETTINGS_SOURCE_WHOLE;
			break;
		}
		kind = SETTINGS_SOURCE_PARTIAL;
	}
	fclose(file);
	return kind;
}

/* the file to read the settings from (NULL: none, the defaults): settings.txt
when it is whole or an older build's; else the last save not yet put in
place, or the one before it (settings.txt.new, .bak) - a game stopped while
it wrote the file, or a file damaged on the card */
static FILE *settings_source(void)
{
	static const char *const names[] = { "settings.txt.new", "settings.txt.bak" };
	static const char *const paths[] = { SETTINGS_FILE ".new", SETTINGS_FILE ".bak" };
	int kind = settings_source_kind(SETTINGS_FILE), index, best = -1, best_kind = SETTINGS_SOURCE_GARBAGE;
	char message[200];

	if (kind == SETTINGS_SOURCE_WHOLE)
		return fopen(SETTINGS_FILE, "r");
	if (kind == SETTINGS_SOURCE_PARTIAL)
	{
		vita_host_log("settings: settings.txt has no last line (an older build's, or cut short): what it has is read, "
			"the rest at the defaults");
		return fopen(SETTINGS_FILE, "r");
	}
	for (index = 0; index < 2; index++)
	{
		int other = settings_source_kind(paths[index]);

		/* (.new only whole: a part of it is a save that was stopped) */
		if (other > best_kind && (other == SETTINGS_SOURCE_WHOLE || index == 1))
		{
			best = index;
			best_kind = other;
		}
	}
	if (best >= 0)
	{
		snprintf(message, sizeof(message), "settings: settings.txt %s: read from %s%s",
			kind == SETTINGS_SOURCE_NONE ? "is not there" : "has no settings in it", names[best],
			best_kind == SETTINGS_SOURCE_WHOLE ? "" : " (cut short: the rest at the defaults)");
		vita_host_log(message);
		return fopen(paths[best], "r");
	}
	if (kind == SETTINGS_SOURCE_GARBAGE)
		vita_host_log("settings: settings.txt has no settings in it, and there is no copy: the defaults");
	return NULL;
}

/* (for halo.log: a value as the file has it, at most 24 printable
characters) */
static void settings_value_shown(char *shown, size_t size, const char *value)
{
	size_t index;

	for (index = 0; index + 1 < size && index < 24 && value[index]; index++)
		shown[index] = value[index] >= ' ' && value[index] < 127 ? value[index] : '?';
	shown[index] = 0;
}

/* settings.txt's value for a row: one of its choices, or a number between
its first and last numbers (the nearest one, as env.txt's: find_choice) -
written by an older build whose choices were others. Anything else (not a
number, out of the row's range, empty) is no choice: 0, and the row keeps
the one it has (env.txt's, or the default) */
static int settings_value_choice(const struct setting *setting, const char *value, int *choice)
{
	double number, lowest = HUGE_VAL, highest = -HUGE_VAL;
	char *end;
	int index;

	for (index = 0; index < setting->count; index++)
		if (strcmp(setting->values[index], value) == 0)
		{
			*choice = index;
			return 1;
		}
	/* (a number as a build writes one: digits, a point, a sign - not hex,
	exponents, spaces or "nan") */
	if (strspn(value, "-0123456789.") != strlen(value))
		return 0;
	number = strtod(value, &end);
	if (!value[0] || end == value || *end || !isfinite(number))
		return 0;
	for (index = 0; index < setting->count; index++)
	{
		double offered = strtod(setting->values[index], &end);

		if (!setting->values[index][0] || *end || !isfinite(offered))
			continue;
		lowest = offered < lowest ? offered : lowest;
		highest = offered > highest ? offered : highest;
	}
	if (lowest > highest || number < lowest || number > highest)
		return 0;
	*choice = find_choice(setting, value);
	return 1;
}

int vita_settings_set(const char *variable, const char *value);

void vita_settings_load(void)
{
	extern int (*halo_test_setting_hook)(const char *variable, const char *value);

	FILE *file;
	/* (a long line: the maps turned off) */
	char line[1200];
	int index;
	/* the dev switches settings.txt names (the others keep env.txt's) */
	char dev_saved[SETTING_COUNT];
	/* the rows settings.txt names, and the profile it was saved with */
	char named[SETTING_COUNT];
	int saved_profile = -1;
	/* (the build that wrote settings.txt: HALO_SETTINGS_BUILD, from 1.1.0) */
	char saved_build[24] = "";
	/* the Play page's texts as env.txt and settings.txt give them, and
	whether settings.txt named them */
	char texts[PLAY_TEXT_COUNT][PLAY_TEXT_LENGTH + 1];
	char texts_named[PLAY_TEXT_COUNT];
	/* (1.0.3's Online games row, saved as HALO_NET_LOBBY_PUBLIC: the
	Visibility row's choice when settings.txt has none of its own) */
	const char *old_public = NULL;
	char old_public_value[8];
	/* (the retired Crossplay switch's choice: -1 none) */
	int old_crossplay = -1;
	/* (the retired two-way Who can join, HALO_NET_HOST_CROSSPLAY: -1 none,
	0 Vitas, 1 Vitas and PC) */
	int old_host_crossplay = -1;

	/* (what the writer has yet to write first; then the file is what the
	rows are, and the next save is written whatever it is) */
	vita_settings_save_wait();
	pthread_mutex_lock(&save_lock);
	if (save_text_asked)
		save_text_asked[0] = 0;
	pthread_mutex_unlock(&save_lock);
	file = settings_source();
	vita_settings_language_init();
	memset(dev_saved, 0, sizeof(dev_saved));
	memset(named, 0, sizeof(named));
	/* (a pre-release build's Update channel: Experimental) */
	if (!shipped_kept && update_build_is_prerelease(HALO_VITA_VERSION))
		setting_named("HALO_UPDATE_CHANNEL")->choice = find_choice(setting_named("HALO_UPDATE_CHANNEL"), "experimental");
	if (!shipped_kept)
	{
		for (index = 0; index < SETTING_COUNT; index++)
			shipped_choice[index] = settings[index].choice;
		shipped_kept = 1;
	}
	/* env.txt (already read) gives a starting choice (HALO_PROFILE=
	performance, balanced or quality there: that profile's, under the rows
	env.txt names itself); settings.txt, the panel's own file, has the last
	word */
	{
		const char *profile = getenv("HALO_PROFILE");
		int profile_index;

		for (profile_index = 0; profile && profile_index < PROFILE_CUSTOM; profile_index++)
			if (strcasecmp(profile, settings[0].values[profile_index]) == 0)
			{
				int row;

				for (row = 0; row < PROFILE_ROWS; row++)
				{
					struct setting *setting = setting_named(profile_variables[row]);

					if (setting && !getenv(setting->variable))
						setting->choice = find_choice(setting, profile_values[profile_index][row]);
				}
			}
	}
	for (index = 0; index < SETTING_COUNT; index++)
	{
		struct setting *setting = &settings[index];
		const char *value = setting->variable ? getenv(setting->variable) : NULL;

		if (setting->kind != KIND_CHOICE)
			continue;
		if (strcmp(setting->variable, PERFORMANCE_LOG) == 0)
		{
			int variable;

			/* (on when env.txt turns on any of its variables) */
			for (variable = 0; variable < PERFORMANCE_LOG_VARIABLES; variable++)
			{
				const char *timing = getenv(performance_log_variables[variable][0]);

				if (timing && atoi(timing))
					setting->choice = 1;
			}
		}
		else if (value && strcmp(setting->variable, "HALO_PROFILE") != 0)
			setting->choice = find_choice(setting, value);
	}
	{
		const char *disabled = getenv("HALO_MAPS_DISABLED");

		snprintf(maps_disabled, sizeof(maps_disabled), "%s", disabled ? disabled : "");
		disabled = getenv("HALO_MODS_DISABLED");
		snprintf(mods_disabled, sizeof(mods_disabled), "%s", disabled ? disabled : "");
	}
	memset(texts_named, 0, sizeof(texts_named));
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
	{
		const char *value = getenv(play_texts[index].variable);

		/* (env.txt's: a starting value, not saved) */
		play_sanitize(texts[index], sizeof(texts[index]), value ? value : "");
	}
	/* (env.txt's 1.0.3 variable, without the new one) */
	if (getenv("HALO_NET_LOBBY_PUBLIC") && !getenv("HALO_NET_HOST_PUBLIC"))
	{
		struct setting *visibility = setting_named("HALO_NET_HOST_PUBLIC");

		visibility->choice = find_choice(visibility, getenv("HALO_NET_LOBBY_PUBLIC"));
	}
	if (file)
	{
		while (fgets(line, sizeof(line), file))
		{
			char *equals = strchr(line, '=');
			char *end = line + strlen(line);
			struct setting *setting;

			while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
				*--end = 0;
			if (!equals)
				continue;
			*equals = 0;
			if (strcmp(line, "HALO_MAPS_DISABLED") == 0)
			{
				snprintf(maps_disabled, sizeof(maps_disabled), "%s", equals + 1);
				continue;
			}
			if (strcmp(line, "HALO_MODS_DISABLED") == 0)
			{
				snprintf(mods_disabled, sizeof(mods_disabled), "%s", equals + 1);
				continue;
			}
			if (play_text_named(line))
			{
				int text = (int)(play_text_named(line) - play_texts);

				play_sanitize(texts[text], sizeof(texts[text]), equals + 1);
				texts_named[text] = 1;
				continue;
			}
			if (strcmp(line, SETTINGS_END) == 0)
				continue;
			if (strcmp(line, "HALO_SETTINGS_BUILD") == 0)
			{
				snprintf(saved_build, sizeof(saved_build), "%s", equals + 1);
				continue;
			}
			if (strcmp(line, "HALO_NET_LOBBY_PUBLIC") == 0)
			{
				snprintf(old_public_value, sizeof(old_public_value), "%s", equals + 1);
				old_public = old_public_value;
				continue;
			}
			/* (the Crossplay switch, retired Oct 19 2026: its choice becomes
			Browser shows, below; gone from settings.txt at the next save) */
			if (strcmp(line, "HALO_CROSSPLAY") == 0)
			{
				old_crossplay = !strcmp(equals + 1, "1") || !strcmp(equals + 1, "true") ? 1 : 0;
				continue;
			}
			/* (the two-way Who can join, retired Oct 20 2026: its choice
			becomes the three-way one's, below; gone from settings.txt at the
			next save) */
			if (strcmp(line, "HALO_NET_HOST_CROSSPLAY") == 0)
			{
				old_host_crossplay = !strcmp(equals + 1, "1") || !strcmp(equals + 1, "true") ? 1 : 0;
				continue;
			}
			/* (1.0.3's co-op rows: every game this Vita hosted became that
			campaign level; co-op is now hosted from the Campaign screen's
			difficulty, Y. An old choice loads as Off: not set, and gone
			from settings.txt at the next save) */
			if (strcmp(line, "HALO_NET_COOP_LEVEL") == 0 || strcmp(line, "HALO_NET_COOP_DIFFICULTY") == 0)
			{
				if (strcmp(line, "HALO_NET_COOP_LEVEL") == 0 && equals[1])
				{
					char message[160];

					snprintf(message, sizeof(message), "settings: the old co-op setting (%.16s) is off: co-op is hosted "
						"from Campaign (Y on the difficulty)", equals + 1);
					vita_host_log(message);
				}
				continue;
			}
			/* (the profile line is what the rows were: worked out again) */
			setting = setting_named(line);
			if (setting && setting->kind == KIND_CHOICE && strcmp(line, "HALO_PROFILE") == 0)
			{
				for (index = 0; index < PROFILE_CUSTOM; index++)
					if (strcasecmp(equals + 1, setting->values[index]) == 0)
						saved_profile = index;
			}
			else if (setting && setting->kind == KIND_CHOICE)
			{
				int choice;

				if (!settings_value_choice(setting, equals + 1, &choice))
				{
					char shown[32], message[200];

					settings_value_shown(shown, sizeof(shown), equals + 1);
					snprintf(message, sizeof(message), "settings: settings.txt's %s=%s is none of its choices: %s",
						setting->variable, shown, setting->values[setting->choice]);
					vita_host_log(message);
					continue;
				}
				setting->choice = choice;
				named[setting - settings] = 1;
				if (setting->dev)
					dev_saved[setting - settings] = 1;
			}
		}
		fclose(file);
	}
	/* a pre-release build saved Experimental as its Update channel, which
	was no choice (Stable greyed out): a release build starts such a
	settings.txt - no HALO_SETTINGS_BUILD, or a pre-release's - on Stable;
	one a release wrote keeps the player's choice */
	if (!update_build_is_prerelease(HALO_VITA_VERSION))
	{
		struct setting *channel = setting_named("HALO_UPDATE_CHANNEL");

		if (named[channel - settings] && strcmp(channel->values[channel->choice], "experimental") == 0 &&
			(!saved_build[0] || update_build_is_prerelease(saved_build)))
		{
			channel->choice = find_choice(channel, "stable");
			vita_host_log("settings: the Update channel a pre-release build saved (Experimental) starts on Stable "
				"in this release");
		}
	}
	if (old_public && !named[setting_named("HALO_NET_HOST_PUBLIC") - settings])
	{
		struct setting *visibility = setting_named("HALO_NET_HOST_PUBLIC");

		visibility->choice = find_choice(visibility, old_public);
	}
	/* (the retired Crossplay switch, settings.txt's or env.txt's, and no
	Browser shows saved yet: On, a player's own choice to play PCs, shows PC
	games, as it did; Off, as shipped, shows all, the Vitas' and PCs'
	together - the switch is gone, every game joined on its own protocol) */
	if (old_crossplay < 0 && getenv("HALO_CROSSPLAY"))
		old_crossplay = !strcmp(getenv("HALO_CROSSPLAY"), "1") || !strcmp(getenv("HALO_CROSSPLAY"), "true") ? 1 : 0;
	if (old_crossplay >= 0 && !named[setting_named("HALO_NET_BROWSER_FILTER") - settings])
	{
		struct setting *filter = setting_named("HALO_NET_BROWSER_FILTER");
		char message[160];

		filter->choice = find_choice(filter, old_crossplay ? "pc" : "all");
		snprintf(message, sizeof(message), "settings: the Crossplay switch is gone (Crossplay %s): Browser shows %s",
			old_crossplay ? "On" : "Off", old_crossplay ? "PC games" : "all games");
		vita_host_log(message);
	}
	unsetenv("HALO_CROSSPLAY");
	/* (the retired two-way Who can join, settings.txt's or env.txt's, and no
	three-way choice saved yet: Vitas stays Vitas only, Vitas and PC becomes
	Everyone - OpenCE's PC players, as it took) */
	if (old_host_crossplay < 0 && getenv("HALO_NET_HOST_CROSSPLAY") && getenv("HALO_NET_HOST_CROSSPLAY")[0])
		old_host_crossplay = !strcmp(getenv("HALO_NET_HOST_CROSSPLAY"), "1") ||
			!strcmp(getenv("HALO_NET_HOST_CROSSPLAY"), "true") ? 1 : 0;
	if (old_host_crossplay >= 0 && !named[setting_named("HALO_NET_WHO_CAN_JOIN") - settings] &&
		!(getenv("HALO_NET_WHO_CAN_JOIN") && getenv("HALO_NET_WHO_CAN_JOIN")[0]))
	{
		struct setting *who = setting_named("HALO_NET_WHO_CAN_JOIN");
		char message[160];

		who->choice = find_choice(who, old_host_crossplay ? "everyone" : "vita");
		snprintf(message, sizeof(message), "settings: Who can join has three choices now (it was %s): %s",
			old_host_crossplay ? "Vitas and PC" : "Vitas", old_host_crossplay ? "Everyone" : "Vita");
		vita_host_log(message);
	}
	unsetenv("HALO_NET_HOST_CROSSPLAY");
	if (saved_profile >= 0)
	{
		int row;

		/* the saved profile's rows take its values as this build has them,
		those added since (Sun rays, in 1.1; Particles, in 1.1.0-beta.4) and
		those changed (1.1.0-beta.4: Dynamic resolution in all three, with
		their minimums; Quality's Adaptive AI; then Balanced and Quality
		redone, above profile_values: a Balanced player's shadows, sun rays
		and 8 lights go, Medium models and Reduced particles come; Quality's
		Xbox rows become its own) -
		HALO_PROFILE is saved only
		while the rows match it (Custom otherwise), so a player's own
		choices are never taken back. A row env.txt names keeps what env.txt
		and settings.txt give it (settings.txt's last, as for any row) */
		for (row = 0; row < PROFILE_ROWS; row++)
		{
			struct setting *setting = setting_named(profile_variables[row]);

			if (setting && !getenv(setting->variable))
				setting->choice = find_choice(setting, profile_values[saved_profile][row]);
		}
	}
	/* (a choice this build does not offer, from settings.txt or env.txt -
	Stable on a pre-release build: the row's first one offered, said once) */
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE && choice_disabled(&settings[index], settings[index].choice))
		{
			struct setting *setting = &settings[index];
			char message[160];
			int choice;

			for (choice = 0; choice < setting->count && choice_disabled(setting, choice); choice++)
				;
			snprintf(message, sizeof(message), "settings: %s=%s is not offered by this build (%s): %s",
				setting->variable, setting->values[setting->choice], HALO_VITA_VERSION, setting->values[choice]);
			vita_host_log(message);
			setting->choice = choice;
		}
	/* (the profile is what the rows are: Custom when env.txt or the panel
	set them apart from every profile) */
	setting_named("HALO_PROFILE")->choice = matching_profile();
	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].kind == KIND_CHOICE && (!settings[index].dev || dev_saved[index]))
			apply_value(&settings[index]);
	if (maps_disabled[0])
		setenv("HALO_MAPS_DISABLED", maps_disabled, 1);
	else
		unsetenv("HALO_MAPS_DISABLED");
	if (mods_disabled[0])
		setenv("HALO_MODS_DISABLED", mods_disabled, 1);
	else
		unsetenv("HALO_MODS_DISABLED");
	for (index = 0; index < (int)(sizeof(fixed_defaults) / sizeof(fixed_defaults[0])); index++)
		setenv(fixed_defaults[index][0], fixed_defaults[index][1], 0);
	halo_test_setting_hook = vita_settings_set;
	language_apply();
	{
		char message[96];

		snprintf(message, sizeof(message), "settings: language %s (%s)", lang_current(),
			setting_named("HALO_LANGUAGE")->values[choice_of("HALO_LANGUAGE")]);
		vita_host_log(message);
	}
	{
		/* (in halo.log: the profile and the rows it sets, the picture's) */
		char message[600];
		int length = snprintf(message, sizeof(message), "settings: profile %s:", settings[0].names[settings[0].choice]);

		for (index = 0; index < PROFILE_ROWS && length < (int)sizeof(message); index++)
			length += snprintf(message + length, sizeof(message) - length, " %s=%s", profile_variables[index],
				getenv(profile_variables[index]));
		if (length < (int)sizeof(message))
			snprintf(message + length, sizeof(message) - length, " HALO_DISPLAY_WIDTH=%s HALO_UPSCALE_FILTER=%s",
				getenv("HALO_DISPLAY_WIDTH"), getenv("HALO_UPSCALE_FILTER"));
		vita_host_log(message);
	}
	log_dev_switches();
	if (maps_disabled[0])
	{
		char message[1100];

		snprintf(message, sizeof(message), "settings: maps turned off: %s", maps_disabled);
		vita_host_log(message);
	}
	if (mods_disabled[0])
	{
		char message[1100];

		snprintf(message, sizeof(message), "settings: mods turned off: %s", mods_disabled);
		vita_host_log(message);
	}
	apply_network();
	/* (the Play page's texts, settings.txt's: none is the game's own name) */
	for (index = 0; index < PLAY_TEXT_COUNT; index++)
		play_text_set(&play_texts[index], texts[index], texts_named[index]);
}

/* ---------- modded maps */

static const char *maps_directory(void)
{
	const char *directory = getenv("HALO_MAPS_ROOT");

	return directory && directory[0] ? directory : DATA_DIRECTORY "/maps";
}

static int map_disabled(const char *name)
{
	const char *list = maps_disabled;
	size_t length = strlen(name);

	while (*list)
	{
		size_t entry = strcspn(list, ",");

		if (entry == length && strncasecmp(list, name, length) == 0)
			return 1;
		list += entry;
		if (*list == ',')
			list++;
	}
	return 0;
}

/* turns the map `name` off or on: in the list, the environment and
settings.txt */
static void map_set_disabled(const char *name, int disabled)
{
	char list[sizeof(maps_disabled)];
	const char *entry = maps_disabled;
	int length = 0;

	if (map_disabled(name) == disabled)
		return;
	/* (the list again without the name, then with it at the end) */
	list[0] = 0;
	while (*entry)
	{
		size_t size = strcspn(entry, ",");

		if (size && !(size == strlen(name) && strncasecmp(entry, name, size) == 0) &&
			length + (int)size + 2 < (int)sizeof(list))
			length += snprintf(list + length, sizeof(list) - length, "%s%.*s", length ? "," : "", (int)size, entry);
		entry += size;
		if (*entry == ',')
			entry++;
	}
	if (disabled && length + (int)strlen(name) + 2 < (int)sizeof(list))
		snprintf(list + length, sizeof(list) - length, "%s%s", length ? "," : "", name);
	memcpy(maps_disabled, list, sizeof(maps_disabled));
	if (maps_disabled[0])
		setenv("HALO_MAPS_DISABLED", maps_disabled, 1);
	else
		unsetenv("HALO_MAPS_DISABLED");
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: map %s turned %s", name, disabled ? "off" : "on");
		vita_host_log(line);
	}
}

static int map_compare(const void *first, const void *second)
{
	const struct map_entry *a = first, *b = second;
	int order = strcasecmp(a->name, b->name);

	return order ? order : strcasecmp(a->extension, b->extension);
}

static int file_exists(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0;
}

/* the format of a map file by its header: the cache signature 'head' and
its version (5 the Xbox's, 609 Custom Edition's), and a Custom Edition
cache's scenario type (0x60: 0 solo, a campaign map) */
static int map_format(const char *path, const char *extension)
{
	unsigned char header[0x62];
	FILE *file = fopen(path, "rb");
	size_t got = 0;
	unsigned long version;

	if (file)
	{
		got = fread(header, 1, sizeof(header), file);
		fclose(file);
	}
	if (got != sizeof(header) || memcmp(header, "daeh", 4) != 0)
		return MAP_OTHER;
	version = header[4] | (header[5] << 8) | ((unsigned long)header[6] << 16) | ((unsigned long)header[7] << 24);
	if (version == 5)
		return MAP_XBOX;
	if (version == 609 && strcasecmp(extension, "yelo") != 0 && header[0x60] == 0 && header[0x61] == 0)
		return MAP_CUSTOM_EDITION_CAMPAIGN;
	if (version == 609)
		return strcasecmp(extension, "yelo") == 0 ? MAP_OPENSAUCE : MAP_CUSTOM_EDITION;
	return MAP_OTHER;
}

/* the maps folder looked through again: the maps that are not the Xbox's */
static void maps_scan(void)
{
	const char *directory = maps_directory();
	DIR *folder = opendir(directory);
	struct dirent *entry;
	static const char *const resource_maps[] = { "bitmaps", "sounds", "loc" };
	int index;

	map_count = 0;
	maps_have_custom_edition = 0;
	while (folder && (entry = readdir(folder)) != NULL && map_count < MAXIMUM_MAPS)
	{
		const char *dot = strrchr(entry->d_name, '.');
		struct map_entry *map = &maps[map_count];
		char path[512];
		struct stat status;
		int stock = 0;

		if (!dot || dot == entry->d_name || (strcasecmp(dot, ".map") != 0 && strcasecmp(dot, ".yelo") != 0) ||
			(size_t)(dot - entry->d_name) >= sizeof(map->name))
			continue;
		snprintf(map->name, sizeof(map->name), "%.*s", (int)(dot - entry->d_name), entry->d_name);
		snprintf(map->extension, sizeof(map->extension), "%s", dot + 1);
		for (index = 0; index < (int)(sizeof(stock_maps) / sizeof(stock_maps[0])); index++)
			if (strcasecmp(map->name, stock_maps[index]) == 0)
				stock = 1;
		if (stock)
			continue;
		snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
		/* (a folder named as a map is none: not listed, never opened) */
		if (stat(path, &status) != 0 || !S_ISREG(status.st_mode))
			continue;
		map->size = (unsigned long long)status.st_size;
		map->format = map_format(path, map->extension);
		if (map->format == MAP_CUSTOM_EDITION || map->format == MAP_OPENSAUCE || map->format == MAP_CUSTOM_EDITION_CAMPAIGN)
			maps_have_custom_edition = 1;
		map_count++;
	}
	if (folder)
		closedir(folder);
	qsort(maps, (size_t)map_count, sizeof(maps[0]), map_compare);
	maps_missing[0] = 0;
	for (index = 0; index < 3; index++)
	{
		char path[256];
		size_t used = strlen(maps_missing);

		snprintf(path, sizeof(path), "%s/%s.map", directory, resource_maps[index]);
		if (!file_exists(path))
			snprintf(maps_missing + used, sizeof(maps_missing) - used, "%s%s.map", used ? " " : "",
				resource_maps[index]);
	}
	if (map_scroll >= map_count)
		map_scroll = 0;
	maps_installer_state = maps_missing[0] ? vita_ce_installer_state(maps_installer, sizeof(maps_installer)) : 0;
	{
		/* (the Halo content folder, as the game finds it on the Vita:
		port/linux/src/xbox_files.c) */
		const struct setting *content = setting_named("HALO_CONTENT_ROOT");
		const char *chosen = content ? content->values[content->choice] : NULL;
		char halo1[96];
		DIR *maps_folder;

		snprintf(maps_content_folder, sizeof(maps_content_folder), "%s",
			chosen && *chosen && strcmp(chosen, "auto") ? chosen : DATA_DIRECTORY "/mcc");
		snprintf(halo1, sizeof(halo1), "%s/halo1/maps", maps_content_folder);
		maps_content_halo1 = -1;
		maps_folder = opendir(halo1);
		if (maps_folder)
		{
			maps_content_halo1 = 0;
			while ((entry = readdir(maps_folder)) != NULL)
			{
				size_t length = strlen(entry->d_name);

				if (length > 4 && !strcasecmp(entry->d_name + length - 4, ".map") &&
					strcasecmp(entry->d_name, "bitmaps.map") && strcasecmp(entry->d_name, "ui.map"))
					maps_content_halo1++;
			}
			closedir(maps_folder);
		}
	}
	maps_mods_present = 0;
	folder = opendir(DATA_DIRECTORY "/mods");
	while (folder && !maps_mods_present && (entry = readdir(folder)) != NULL)
	{
		char path[512];
		struct stat status;

		snprintf(path, sizeof(path), "%s/%s", DATA_DIRECTORY "/mods", entry->d_name);
		maps_mods_present = entry->d_name[0] != '.' && stat(path, &status) == 0 && S_ISDIR(status.st_mode);
	}
	if (folder)
		closedir(folder);
}

static void size_text(char *text, int size, unsigned long long bytes)
{
	/* (the memory card's free space: GB) */
	if (bytes >= 1024ULL * 1024ULL * 1024ULL)
		snprintf(text, (size_t)size, "%llu.%llu GB", bytes >> 30, ((bytes & 0x3FFFFFFFULL) * 10) >> 30);
	else if (bytes >= 1024ULL * 1024ULL)
		snprintf(text, (size_t)size, "%llu.%llu MB", bytes >> 20, ((bytes & 0xFFFFFULL) * 10) >> 20);
	else
		snprintf(text, (size_t)size, "%llu KB", (bytes + 1023) >> 10);
}

static const char *map_format_name(int format)
{
	return format == MAP_XBOX ? "Xbox" : format == MAP_CUSTOM_EDITION ? "CE" : format == MAP_OPENSAUCE ? "CE+OS" :
		format == MAP_CUSTOM_EDITION_CAMPAIGN ? "CE SP" : "?";
}

void map_checks_forget(void);

/* deletes a map file, with its picture and description beside it */
static void map_delete(const struct map_entry *map)
{
	const char *directory = maps_directory();
	char path[256];
	int other = 0, index;

	snprintf(path, sizeof(path), "%s/%s.%s", directory, map->name, map->extension);
	if (remove(path) != 0)
	{
		set_notice(T("Could not delete %.40s.%s"), map->name, map->extension);
		return;
	}
	/* (and a download of it that was cut off, kept to go on: map_share.c) */
	snprintf(path, sizeof(path), "%s/%s.download", directory, map->name);
	remove(path);
	snprintf(path, sizeof(path), "%s/%s.resume", directory, map->name);
	remove(path);
	/* (the picture and description stay while a .map or .yelo of the name
	is left) */
	for (index = 0; index < map_count; index++)
		if (&maps[index] != map && strcasecmp(maps[index].name, map->name) == 0)
			other = 1;
	if (!other)
	{
		snprintf(path, sizeof(path), "%s/%s.bmp", directory, map->name);
		remove(path);
		snprintf(path, sizeof(path), "%s/%s.txt", directory, map->name);
		remove(path);
		map_set_disabled(map->name, 0);
	}
	set_notice(T("Deleted %.40s.%s"), map->name, map->extension);
	/* (the menus' map lists look at the maps folder's files again:
	port/linux/game/map_checks.c) */
	map_checks_forget();
	{
		char line[160];

		snprintf(line, sizeof(line), "settings: map %s.%s deleted", map->name, map->extension);
		vita_host_log(line);
	}
}

/* ---------- the mod manager */

/* the name of the entry being deleted, for its notice */
static char mods_deleting[HALO_MODS_NAME_BYTES];

/* whether the comma list `list` names `name` (either case) */
static int name_listed(const char *list, const char *name)
{
	size_t length = strlen(name);

	while (length && *list)
	{
		size_t entry = strcspn(list, ",");

		if (entry == length && strncasecmp(list, name, length) == 0)
			return 1;
		list += entry;
		if (*list == ',')
			list++;
	}
	return 0;
}

/* a list as the game set it (its MOD MANAGER, vita_settings_set), kept as
settings.txt can keep it: printable ASCII but '%' */
static void list_sanitize(char *list, size_t size, const char *value)
{
	size_t length = 0;

	for (; value && *value && length + 1 < size; value++)
		if (*value >= ' ' && *value <= '~' && *value != '%')
			list[length++] = *value;
	list[length] = 0;
}

/* turns the mod of the key `key` off or on: in the list, the environment
and settings.txt (as map_set_disabled does a map) */
static void mod_set_disabled(const char *key, int disabled)
{
	char list[sizeof(mods_disabled)];
	const char *entry = mods_disabled;
	int length = 0;

	if (!key[0] || name_listed(mods_disabled, key) == disabled)
		return;
	list[0] = 0;
	while (*entry)
	{
		size_t size = strcspn(entry, ",");

		if (size && !(size == strlen(key) && strncasecmp(entry, key, size) == 0) &&
			length + (int)size + 2 < (int)sizeof(list))
			length += snprintf(list + length, sizeof(list) - length, "%s%.*s", length ? "," : "", (int)size, entry);
		entry += size;
		if (*entry == ',')
			entry++;
	}
	if (disabled && length + (int)strlen(key) + 2 < (int)sizeof(list))
		snprintf(list + length, sizeof(list) - length, "%s%s", length ? "," : "", key);
	memcpy(mods_disabled, list, sizeof(mods_disabled));
	if (mods_disabled[0])
		setenv("HALO_MODS_DISABLED", mods_disabled, 1);
	else
		unsetenv("HALO_MODS_DISABLED");
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: mod %s turned %s", key, disabled ? "off" : "on");
		vita_host_log(line);
	}
}

/* asks the game to look through the maps and mods folders (its answer:
mods_poll) */
static void mods_request_scan(void)
{
	/* (one being answered already: its answer is a look too) */
	if (__atomic_load_n(&halo_mods_request, __ATOMIC_ACQUIRE) == HALO_MODS_REQUEST_NONE)
	{
		halo_mods_answer = HALO_MODS_ANSWER_NONE;
		__atomic_store_n(&halo_mods_request, HALO_MODS_REQUEST_SCAN, __ATOMIC_RELEASE);
	}
	mods_state = 1;
	vita_host_log("settings: mod manager: asked the game to look");
}

/* asks the game to delete `entry`, then to look again */
static void mods_request_delete(const struct halo_mod_entry *entry)
{
	if (__atomic_load_n(&halo_mods_request, __ATOMIC_ACQUIRE) != HALO_MODS_REQUEST_NONE)
		return;
	snprintf(mods_deleting, sizeof(mods_deleting), "%s", entry->file);
	snprintf(halo_mods_target, sizeof(halo_mods_target), "%s", entry->file);
	halo_mods_target_kind = entry->kind;
	halo_mods_answer = HALO_MODS_ANSWER_NONE;
	__atomic_store_n(&halo_mods_request, HALO_MODS_REQUEST_DELETE, __ATOMIC_RELEASE);
	mods_state = 1;
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: mod manager: asked the game to delete %s", entry->file);
		vita_host_log(line);
	}
}

/* the game's answer, once it came: 1 when it did (the page is shown again) */
static int mods_poll(void)
{
	int answer, index;

	if (mods_state != 1 || __atomic_load_n(&halo_mods_request, __ATOMIC_ACQUIRE) != HALO_MODS_REQUEST_NONE)
		return 0;
	answer = halo_mods_answer;
	memcpy(&mods_view, &halo_mods_inventory, sizeof(mods_view));
	/* (the game's, another unit's: taken as no more than there is room for,
	each text ended) */
	if (mods_view.count < 0 || mods_view.count > HALO_MODS_MAXIMUM)
		mods_view.count = 0;
	for (index = 0; index < mods_view.count; index++)
	{
		struct halo_mod_entry *entry = &mods_view.entries[index];

		entry->name[sizeof(entry->name) - 1] = entry->file[sizeof(entry->file) - 1] = 0;
		entry->title[sizeof(entry->title) - 1] = 0;
		entry->why[sizeof(entry->why) - 1] = entry->needs[sizeof(entry->needs) - 1] = 0;
		/* (on or off as this panel's lists say now) */
		entry->enabled = !name_listed(entry->kind == HALO_MOD_KIND_MCC_MOD ? mods_disabled : maps_disabled, entry->name);
	}
	mods_state = 2;
	if (mods_scroll >= mods_view.count)
		mods_scroll = 0;
	if (answer == HALO_MODS_ANSWER_DELETED)
		set_notice(T("Deleted %.40s"), mods_deleting);
	else if (answer == HALO_MODS_ANSWER_IN_USE)
		set_notice(T("%.30s is in play: leave it first"), mods_deleting);
	else if (answer == HALO_MODS_ANSWER_FAILED)
		set_notice(T("Could not delete %.40s"), mods_deleting);
	{
		char line[96];

		snprintf(line, sizeof(line), "settings: mod manager: %d entries", mods_view.count);
		vita_host_log(line);
	}
	return 1;
}

/* an entry's kind, short (the line's column) and in words */
static const char *mod_kind_short(int kind)
{
	return kind == HALO_MOD_KIND_XBOX_MAP ? "Xbox" : kind == HALO_MOD_KIND_CE_MAP ? "CE" :
		kind == HALO_MOD_KIND_CE_CAMPAIGN ? "CE SP" : kind == HALO_MOD_KIND_OPENSAUCE ? "CE+OS" :
		kind == HALO_MOD_KIND_MCC_MOD ? "MCC" : "?";
}

static const char *mod_kind_name(int kind)
{
	return kind == HALO_MOD_KIND_XBOX_MAP ? T("Xbox map") : kind == HALO_MOD_KIND_CE_MAP ? T("PC map") :
		kind == HALO_MOD_KIND_CE_CAMPAIGN ? T("PC campaign map") : kind == HALO_MOD_KIND_OPENSAUCE ? T("OpenSauce map") :
		kind == HALO_MOD_KIND_MCC_MOD ? T("MCC mod") : T("Unknown file");
}

static void mod_modes_text(char *text, int size, int modes)
{
	int length = 0;

	text[0] = 0;
	if (modes & HALO_MOD_MODE_MULTIPLAYER)
		length += snprintf(text + length, (size_t)(size - length), "%s", T("Multiplayer"));
	if ((modes & HALO_MOD_MODE_CAMPAIGN) && length < size)
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Campaign"));
	if ((modes & HALO_MOD_MODE_COOP) && length < size)
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Co-op"));
	if ((modes & HALO_MOD_MODE_FORGE) && length < size)
		length += snprintf(text + length, (size_t)(size - length), "%s%s", length ? ", " : "", T("Forge"));
	if (!length)
		snprintf(text, (size_t)size, "%s", T("None"));
}

/* the entry's state on its line: On, Off, or Error (it failed the game's
checks) */
static const char *mod_state_name(const struct halo_mod_entry *entry)
{
	return !entry->valid ? T("Error") : entry->enabled ? T("On") : T("Off");
}

/* turns `entry` off or on (`disabled` -1: the other way) */
static void mod_entry_toggle(struct halo_mod_entry *entry, int disabled)
{
	if (!entry->deletable)
	{
		set_notice("%s", T("Its name isn't one this game takes: use VitaShell"));
		return;
	}
	if (disabled < 0)
		disabled = entry->enabled;
	if (entry->kind == HALO_MOD_KIND_MCC_MOD)
		mod_set_disabled(entry->name, disabled);
	else
		map_set_disabled(entry->name, disabled);
	entry->enabled = !name_listed(entry->kind == HALO_MOD_KIND_MCC_MOD ? mods_disabled : maps_disabled, entry->name);
}

/* ---------- the report */

/* copies a file into the report's folder; nonzero if it was there */
static int report_copy(const char *from, const char *folder, const char *name)
{
	char path[256];
	FILE *input = fopen(from, "rb"), *output;
	size_t got;
	int ok = 1;

	if (!input)
		return 0;
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	output = fopen(path, "wb");
	if (!output)
	{
		fclose(input);
		return 0;
	}
	while ((got = fread(report_buffer, 1, sizeof(report_buffer), input)) > 0)
		if (fwrite(report_buffer, 1, got, output) != got)
		{
			ok = 0;
			break;
		}
	fclose(input);
	if (fclose(output) != 0)
		ok = 0;
	return ok;
}

/* the report, on a thread of its own (a crash dump is tens of megabytes) */
static void report_thread(void *argument)
{
	static const char *const files[] = { "halo.log", "halo-prev.log", "settings.txt", "env.txt" };
	SceDateTime time;
	char path[512], newest[512], newest_name[256];
	int index, count = 0;
	DIR *folder;
	struct dirent *entry;
	long long newest_time = -1;

	(void)argument;
	/* (settings.txt as the last change has it, not half put in place) */
	vita_settings_save_wait();
	memset(&time, 0, sizeof(time));
	sceRtcGetCurrentClockLocalTime(&time);
	snprintf(report_path, sizeof(report_path), DATA_DIRECTORY "/report-%04u%02u%02u-%02u%02u%02u",
		(unsigned)time.year, (unsigned)time.month, (unsigned)time.day, (unsigned)time.hour, (unsigned)time.minute,
		(unsigned)time.second);
	mkdir(report_path, 0777);
	for (index = 0; index < (int)(sizeof(files) / sizeof(files[0])); index++)
	{
		snprintf(path, sizeof(path), DATA_DIRECTORY "/%s", files[index]);
		count += report_copy(path, report_path, files[index]);
	}
	/* the newest crash dump */
	newest[0] = newest_name[0] = 0;
	folder = opendir(DUMP_DIRECTORY);
	while (folder && (entry = readdir(folder)) != NULL)
	{
		struct stat status;
		size_t length = strlen(entry->d_name);

		if (length < 16 || strncmp(entry->d_name, "psp2core", 8) != 0 ||
			strcasecmp(entry->d_name + length - 8, ".psp2dmp") != 0)
			continue;
		snprintf(path, sizeof(path), DUMP_DIRECTORY "/%s", entry->d_name);
		if (stat(path, &status) == 0 && ((long long)status.st_mtime > newest_time ||
			((long long)status.st_mtime == newest_time && strcmp(entry->d_name, newest_name) > 0)))
		{
			newest_time = (long long)status.st_mtime;
			snprintf(newest, sizeof(newest), "%s", path);
			snprintf(newest_name, sizeof(newest_name), "%s", entry->d_name);
		}
	}
	if (folder)
		closedir(folder);
	if (newest[0])
		count += report_copy(newest, report_path, newest_name);
	report_files = count;
	{
		char line[200];

		snprintf(line, sizeof(line), "settings: report saved to %s (%d files%s)", report_path, count,
			newest[0] ? ", with the newest crash dump" : ", no crash dump");
		vita_host_log(line);
	}
	__atomic_store_n(&report_state, count ? REPORT_SAVED : REPORT_FAILED, __ATOMIC_RELEASE);
}

static void report_start(void)
{
	if (__atomic_load_n(&report_state, __ATOMIC_ACQUIRE) == REPORT_SAVING)
		return;
	__atomic_store_n(&report_state, REPORT_SAVING, __ATOMIC_RELEASE);
	if (vita_host_thread_start("halo report", report_thread, NULL, -1) < 0)
	{
		__atomic_store_n(&report_state, REPORT_FAILED, __ATOMIC_RELEASE);
		set_notice("%s", T("Could not start the report"));
	}
}

/* ---------- the lines of a tab */

enum
{
	LINE_SETTING,
	LINE_MAP,
	/* (the Forge page: its Map, Map variant, Start Forge (index), and a map
	variant of File Share's (index into forge_entries)) */
	LINE_FORGE,
	LINE_VARIANT,
	/* (the Mod manager page's entries: mods_view) */
	LINE_MOD,
	/* (the Friends page's entries: friends_view) */
	LINE_FRIEND,
	/* (the Party page's: party_members_view, party_invites_view, and the
	friends to invite, party_friends_view) */
	LINE_PARTY_MEMBER,
	LINE_PARTY_INVITE,
	LINE_PARTY_FRIEND,
	/* (the Party page's quick chat: its preset, party_preset) */
	LINE_PARTY_PRESET,
	/* (the Players page's players: host_players_view) */
	LINE_HOST_PLAYER,
	/* (the Chat page's: the game's players, chat_players_names) */
	LINE_CHAT_PLAYER,
	/* (a line that is not chosen: a warning, a note) */
	LINE_INFO,
	/* (a heading over rows: headings; not chosen either) */
	LINE_HEADING,
};

struct line
{
	int type;
	int index;
	char text[128];
};

/* (a page shows the maps, or the mod manager's entries: never both) */
#define MAXIMUM_LIST_LINES (MAXIMUM_MAPS > HALO_MODS_MAXIMUM ? MAXIMUM_MAPS : HALO_MODS_MAXIMUM)
#define MAXIMUM_LINES (SETTING_COUNT + MAXIMUM_LIST_LINES + FORGE_PANEL_MAXIMUM + FORGE_LINE_COUNT + 6 + \
	P2P_FRIEND_ENTRIES_MAXIMUM + \
	(int)(sizeof(headings) / sizeof(headings[0])))

/* a line that is not chosen (moving up and down passes it) */
static int line_passive(const struct line *line)
{
	return line->type == LINE_INFO || line->type == LINE_HEADING;
}

/* the tabs shown: Dev once its switch is on */
static int tab_shown(int index)
{
	return index != TAB_DEV || choice_of("HALO_DEV_SETTINGS");
}

/* port: the downloaded and verified VPK waiting to be installed with
VitaShell (vita_update.c), on two lines under the version in amber: the
folder, then the file (its name holds the version; a line is 46
characters); 1 when there is one */
static int update_ready_lines(struct line *lines, int *count)
{
	char version[UPDATE_VERSION_SIZE], path[512], folder[512];
	const char *slash;

	if (!vita_update_ready(version, sizeof(version), path, sizeof(path)))
		return 0;
	slash = strrchr(path, '/');
	snprintf(folder, sizeof(folder), "%.*s", slash ? (int)(slash - path + 1) : 0, path);
	lines[*count].type = LINE_INFO;
	lines[*count].text[0] = '!';
	snprintf(lines[(*count)++].text + 1, sizeof(lines[0].text) - 1, T("VitaShell: %s"), folder);
	lines[*count].type = LINE_INFO;
	lines[*count].text[0] = '!';
	snprintf(lines[(*count)++].text + 1, sizeof(lines[0].text) - 1, T("%s (verified, to install)"), slash ? slash + 1 : path);
	return 1;
}

/* the info lines under Check for updates (Game): the build's version
(for a report too: halo.log's first line has it) and the update check's
answer - on one line while both fit ("Version 1.1.0-beta.3 - Up to date"),
else the answer on a line of its own; a newer version in amber, on two lines
under the version, with where to get it (always this fixed address, never
one from the answer); their count */
static int update_lines(struct line *lines)
{
	char version[UPDATE_VERSION_SIZE], answer[96];
	int status = update_check_status(version, sizeof(version));
	int count = 0;

	lines[count].type = LINE_INFO;
	snprintf(lines[count++].text, sizeof(lines[0].text), T("Version %s"), HALO_VITA_VERSION);
	answer[0] = 0;
	switch (status)
	{
	case UPDATE_STATUS_CHECKING:
		snprintf(answer, sizeof(answer), "%s", T("Checking..."));
		break;
	case UPDATE_STATUS_UP_TO_DATE:
		snprintf(answer, sizeof(answer), "%s", T("Up to date"));
		break;
	case UPDATE_STATUS_AVAILABLE:
		/* port: a newer version downloaded already: where it is, below */
		if (update_ready_lines(lines, &count))
			return count;
		/* (in amber: '!') */
		lines[count].type = LINE_INFO;
		lines[count].text[0] = '!';
		snprintf(lines[count++].text + 1, sizeof(lines[0].text) - 1, T("Update available: %s"), version);
		/* port: a build with the update key downloads it on the next press
		(vita_update.c); one without says where to get it */
		lines[count].type = LINE_INFO;
		{
			unsigned char key[32];

			if (update_public_key(key))
				snprintf(lines[count++].text, sizeof(lines[0].text), "!%s", T("Press again to download it"));
			else
				snprintf(lines[count++].text, sizeof(lines[0].text), "!%s", UPDATE_RELEASES_PAGE);
		}
		return count;
	case UPDATE_STATUS_NO_CONNECTION:
		snprintf(answer, sizeof(answer), "%s", T("Couldn't check: no connection"));
		break;
	case UPDATE_STATUS_FAILED:
		snprintf(answer, sizeof(answer), "%s", T("Couldn't check right now"));
		break;
	}
	/* port: a downloaded update waiting to be installed, whatever the last
	look said */
	if (update_ready_lines(lines, &count))
		return count;
	if (!answer[0])
		return count;
	/* (46 characters a line: the panel's width, in characters, not UTF-8's
	bytes) */
	if (lang_utf8_length(lines[0].text) + 3 + lang_utf8_length(answer) <= 46)
	{
		size_t length = strlen(lines[0].text);

		snprintf(lines[0].text + length, sizeof(lines[0].text) - length, " - %s", answer);
		return count;
	}
	lines[count].type = LINE_INFO;
	if (status == UPDATE_STATUS_UP_TO_DATE)
		snprintf(lines[count++].text, sizeof(lines[0].text), T("Up to date (%s)"), version);
	else
		snprintf(lines[count++].text, sizeof(lines[0].text), "%s", answer);
	return count;
}

/* the map variants read again (the Forge page opened, one deleted), the
map chosen kept if it is still in the maps folder, else Blood Gulch or
the first that is */
static void forge_scan(void)
{
	int index, count = forge_panel_map_count();

	forge_entry_count = forge_store_list(NULL, forge_entries, FORGE_PANEL_MAXIMUM);
	if (forge_entry_count < 0)
		forge_entry_count = 0;
	if (forge_scroll >= forge_entry_count)
		forge_scroll = 0;
	if (forge_map < 0 || forge_map >= count || !forge_panel_map_available(forge_map))
	{
		forge_map = -1;
		for (index = 0; index < count; index++)
			if (forge_panel_map_available(index) && (forge_map < 0 || !strcmp(forge_panel_map_short(index), "bloodgulch")))
				forge_map = index;
		forge_variant = 0;
	}
}

/* the map chosen's map variants that can be played: how many, and the
entry of the nth (1 the first), NULL past them */
static const struct forge_store_entry *forge_map_variant(int number, int *count)
{
	const struct forge_store_entry *found = NULL;
	int index, seen = 0;

	for (index = 0; index < forge_entry_count; index++)
		if (forge_map >= 0 && !forge_entries[index].damaged &&
			!strcmp(forge_entries[index].map_name, forge_panel_map_short(forge_map)) && ++seen == number)
			found = &forge_entries[index];
	if (count)
		*count = seen;
	return found;
}

/* a map's name by its short name (bloodgulch: Blood Gulch) */
static const char *forge_map_title(const char *short_name)
{
	int index;

	for (index = 0; index < forge_panel_map_count(); index++)
		if (!strcmp(forge_panel_map_short(index), short_name))
			return T(forge_panel_map_name(index));
	return short_name;
}

static int multiplayer_state(void);
static void close_panel(void);

/* (Start Forge) the game asked for the Forge game, the panel closed; not
in a level or a network game's lobby */
static void forge_start(void)
{
	const struct forge_store_entry *variant = forge_variant ? forge_map_variant(forge_variant, NULL) : NULL;
	int state = multiplayer_state();

	if (forge_map < 0)
	{
		set_notice("%s", T("No multiplayer map is in the maps folder."));
		return;
	}
	if (state == SYSTEM_LINK_STATE_PLAYING)
	{
		set_notice("%s", T("Leave the level first: Start, then Quit"));
		return;
	}
	if (state == SYSTEM_LINK_STATE_JOINING || state == SYSTEM_LINK_STATE_HOSTING || state == SYSTEM_LINK_STATE_LOBBY)
	{
		set_notice("%s", T("Leave the network game's lobby first"));
		return;
	}
	halo_forge_request_map = forge_map;
	halo_forge_request_network = forge_network;
	snprintf(halo_forge_request_variant, sizeof(halo_forge_request_variant), "%s", variant ? variant->file_name : "");
	__atomic_store_n(&halo_system_link_request, SYSTEM_LINK_REQUEST_FORGE, __ATOMIC_RELEASE);
	vita_host_log("settings: Forge asked for (settings panel)");
	close_panel();
}

/* the Friends page's entries again (at most twice a second, unless asked
now: an action's outcome shows at once) */
static void friends_refresh(int now)
{
	static unsigned long long refreshed;
	unsigned long long time = now_us();

	if (!now && refreshed && time - refreshed < 500000ULL)
		return;
	refreshed = time;
	friends_count = p2p_friends_list(friends_view, P2P_FRIEND_ENTRIES_MAXIMUM);
	if (friends_count < 0 || friends_count > P2P_FRIEND_ENTRIES_MAXIMUM)
		friends_count = 0;
	if (friends_scroll >= friends_count)
		friends_scroll = 0;
}

/* the Party page's lines again (with the friends', at most twice a second) */
static void party_refresh(int now)
{
	int index;

	friends_refresh(now);
	party_members_count = p2p_party_members(party_members_view, P2P_PARTY_MAXIMUM);
	party_invites_count = p2p_party_invites(party_invites_view, P2P_FRIEND_INVITES_MAXIMUM);
	party_friends_count = 0;
	for (index = 0; index < friends_count; index++)
		if (friends_view[index].kind == P2P_FRIEND_KIND_FRIEND && friends_view[index].online &&
			p2p_party_friend_state(friends_view[index].id) != 1)
		{
			party_friends_view[party_friends_count++] = index;
		}
	if (party_friends_scroll >= party_friends_count)
		party_friends_scroll = 0;
}

/* a member's word, as its line's value */
static const char *party_member_word(const struct p2p_party_member *member)
{
	if (member->blocked)
		return T("Blocked");
	if (member->leader)
		return member->self ? T("You, leader") : T("Leader");
	if (member->self)
		return T("You");
	if (member->muted)
		return T("Muted");
	return member->online ? T("In party") : T("Not heard");
}

/* an entry's state in a word, as its line's value */
static const char *friend_state_word(const struct p2p_friend_entry *entry)
{
	switch (entry->kind)
	{
	case P2P_FRIEND_KIND_REQUEST:
		return T("Request");
	case P2P_FRIEND_KIND_INVITE:
		return T("Invitation");
	case P2P_FRIEND_KIND_FRIEND:
		return entry->joinable ? T("Joinable") : entry->online ? T("Online") : T("Offline");
	case P2P_FRIEND_KIND_OUTGOING:
		return T("Sent");
	}
	return T("Blocked");
}

/* the Players page's view of the hosted game's players (published by the
game's main loop: system_link_shortcut.h), read whole (the serial even and
the same before and after), and the panel's request's answer awaited */
static int host_players_count = -1;
static int host_players_machines[HALO_HOST_PLAYERS_MAXIMUM];
static char host_players_names[HALO_HOST_PLAYERS_MAXIMUM][HALO_HOST_PLAYER_NAME_SIZE];
static int host_session_bans;
static int host_player_asked;
static char host_player_asked_name[HALO_HOST_PLAYER_NAME_SIZE];

static void host_players_refresh(void)
{
	int tries;

	/* (the panel's last request answered: said) */
	if (host_player_asked && !__atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE))
	{
		int answer = __atomic_load_n(&halo_host_player_answer, __ATOMIC_ACQUIRE);

		if (host_player_asked == HALO_HOST_PLAYER_CLEAR_BANS)
			set_notice("%s", T("Banned players can join again"));
		else if (host_player_asked == HALO_HOST_PLAYER_ADD_BOT || host_player_asked == HALO_HOST_PLAYER_DROP_BOT)
			set_notice("%s", answer != HALO_HOST_ANSWER_DONE ? T("Only in a game you host") :
				host_player_asked == HALO_HOST_PLAYER_ADD_BOT ? T("A bot joins") : T("A bot leaves"));
		else if (answer != HALO_HOST_ANSWER_DONE)
			set_notice("%s", T("That player is not in the game"));
		else if (host_player_asked == HALO_HOST_PLAYER_KICK)
			set_notice(T("Kicked %s"), host_player_asked_name);
		else
			set_notice(T("Banned %s from your games"), host_player_asked_name);
		host_player_asked = 0;
	}
	for (tries = 0; tries < 4; tries++)
	{
		int serial = __atomic_load_n(&halo_host_players_serial, __ATOMIC_ACQUIRE), count, index;

		if (serial & 1)
			continue;
		count = halo_host_players_count;
		if (count > HALO_HOST_PLAYERS_MAXIMUM)
			count = HALO_HOST_PLAYERS_MAXIMUM;
		for (index = 0; index < count; index++)
		{
			host_players_machines[index] = halo_host_players_machines[index];
			memcpy(host_players_names[index], halo_host_players_names[index], HALO_HOST_PLAYER_NAME_SIZE);
			host_players_names[index][HALO_HOST_PLAYER_NAME_SIZE - 1] = 0;
		}
		host_session_bans = halo_host_session_bans;
		if (__atomic_load_n(&halo_host_players_serial, __ATOMIC_ACQUIRE) == serial)
		{
			host_players_count = count;
			return;
		}
	}
}

/* the Chat page's view of the game's players (published by the game:
game_chat_share.h), read whole as host_players_refresh reads its own */
static int chat_players_count = -1;
static int chat_players_muted[HALO_CHAT_PLAYERS_MAXIMUM];
static int chat_players_local[HALO_CHAT_PLAYERS_MAXIMUM];
static char chat_players_names[HALO_CHAT_PLAYERS_MAXIMUM][HALO_CHAT_PLAYER_NAME_SIZE];

static void chat_players_refresh(void)
{
	int tries;

	for (tries = 0; tries < 4; tries++)
	{
		int serial = __atomic_load_n(&halo_chat_players_serial, __ATOMIC_ACQUIRE), count, index;

		if (serial & 1)
			continue;
		count = halo_chat_players_count;
		if (count > HALO_CHAT_PLAYERS_MAXIMUM)
			count = HALO_CHAT_PLAYERS_MAXIMUM;
		for (index = 0; index < count; index++)
		{
			chat_players_muted[index] = halo_chat_players_muted[index];
			chat_players_local[index] = halo_chat_players_local[index];
			memcpy(chat_players_names[index], halo_chat_players_names[index], HALO_CHAT_PLAYER_NAME_SIZE);
			chat_players_names[index][HALO_CHAT_PLAYER_NAME_SIZE - 1] = 0;
		}
		if (__atomic_load_n(&halo_chat_players_serial, __ATOMIC_ACQUIRE) == serial)
		{
			chat_players_count = count;
			return;
		}
	}
}

/* a Chat page's player's line: Cross mutes them here, or unmutes them (the
game takes it at its next frame: never told to anyone) */
static void chat_player_input(const struct line *line, unsigned long pressed)
{
	int index = line->index;
	char log[96];

	if (!(pressed & VITA_BUTTON_CROSS) || index < 0 || index >= chat_players_count)
		return;
	if (chat_players_local[index])
	{
		set_notice("%s", T("A player of this PS Vita"));
		return;
	}
	if (__atomic_load_n(&halo_chat_mute_request, __ATOMIC_ACQUIRE))
		return;
	memcpy(halo_chat_mute_request_name, chat_players_names[index], HALO_CHAT_PLAYER_NAME_SIZE);
	__atomic_store_n(&halo_chat_mute_request, (index + 1) | (chat_players_muted[index] ? 0 : HALO_CHAT_MUTE_REQUEST_MUTE),
		__ATOMIC_RELEASE);
	/* (shown at once as it will be) */
	chat_players_muted[index] = !chat_players_muted[index];
	set_notice(chat_players_muted[index] ? T("%s muted: their chat is hidden here") : T("%s unmuted"),
		chat_players_names[index]);
	snprintf(log, sizeof(log), "settings: chat: %s %s", chat_players_names[index],
		chat_players_muted[index] ? "muted" : "unmuted");
	vita_host_log(log);
}

/* (SCREEN_BAN_CONFIRM) the player a ban for good waits on */
static int ban_confirm_index = -1;
static char ban_confirm_name[HALO_HOST_PLAYER_NAME_SIZE];
static int ban_confirm_machine;

/* a kick or ban asked of the game's main loop */
static void host_player_request(int how, const char *name, int machine)
{
	char log[128];

	host_player_asked = how;
	memcpy(host_player_asked_name, name, HALO_HOST_PLAYER_NAME_SIZE);
	__atomic_store_n(&halo_host_player_answer, HALO_HOST_ANSWER_NONE, __ATOMIC_RELEASE);
	__atomic_store_n(&halo_host_player_request, how << 8 | (machine & 0xFF), __ATOMIC_RELEASE);
	snprintf(log, sizeof(log), "settings: players: %s %s", how == HALO_HOST_PLAYER_KICK ? "kick" :
		how == HALO_HOST_PLAYER_BAN ? "ban (session)" : "ban (for good)", host_player_asked_name);
	vita_host_log(log);
}

/* a Players page's line's buttons: Cross kicks, Triangle bans for the
session, Square for good (bans.txt) once its question is answered */
static void host_player_input(const struct line *line, unsigned long pressed)
{
	int how = (pressed & VITA_BUTTON_CROSS) ? HALO_HOST_PLAYER_KICK : (pressed & VITA_BUTTON_TRIANGLE) ?
		HALO_HOST_PLAYER_BAN : (pressed & VITA_BUTTON_SQUARE) ? HALO_HOST_PLAYER_BAN_ALWAYS : 0;

	if (!how || line->index < 0 || line->index >= host_players_count ||
		__atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE))
	{
		return;
	}
	if (how == HALO_HOST_PLAYER_BAN_ALWAYS)
	{
		ban_confirm_index = line->index;
		memcpy(ban_confirm_name, host_players_names[line->index], HALO_HOST_PLAYER_NAME_SIZE);
		ban_confirm_name[HALO_HOST_PLAYER_NAME_SIZE - 1] = 0;
		ban_confirm_machine = host_players_machines[line->index];
		screen = SCREEN_BAN_CONFIRM;
		vita_host_log("settings: players: ban for good asked");
		return;
	}
	host_player_request(how, host_players_names[line->index], host_players_machines[line->index]);
}

/* the question before a ban for good (square) */
static void show_ban_confirm(void)
{
	char text[512];

	snprintf(text, sizeof(text), T("BAN FOR GOOD\n\n%.20s\n\nThey can't join your games again until you\nuse Clear bans.\n"
		"Cross: ban   O: back"), ban_confirm_name);
	vgxm_menu_set(text, -1);
}

/* the question's buttons */
static void ban_confirm_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if (pressed & VITA_BUTTON_CROSS)
	{
		screen = SCREEN_LIST;
		if (!__atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE))
			host_player_request(HALO_HOST_PLAYER_BAN_ALWAYS, ban_confirm_name, ban_confirm_machine);
	}
}

/* the lines of the page shown: its rows, each heading (headings) above the
row it names (Game's Check for updates with the version and the answer
under it), then the gyroscope's line (Gyro settings) or the maps (Modded
maps) */
static int page_lines(struct line *lines)
{
	int count = 0, index;

	if (page == PAGE_FRIENDS)
		friends_refresh(0);
	if (page == PAGE_PARTY)
		party_refresh(0);
	if (page == PAGE_PLAYERS)
		host_players_refresh();
	if (page == PAGE_CHAT)
		chat_players_refresh();

	for (index = 0; index < SETTING_COUNT; index++)
		if (settings[index].page == page && setting_shown(&settings[index]))
		{
			size_t heading;

			for (heading = 0; heading < sizeof(headings) / sizeof(headings[0]); heading++)
				if (headings[heading].page == page && !strcmp(headings[heading].row, settings[index].label))
				{
					lines[count].type = LINE_HEADING;
					snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T(headings[heading].name));
				}
			lines[count].type = LINE_SETTING;
			lines[count++].index = index;
			/* (Game: the version and the update check's answer right
			under Check for updates) */
			if (settings[index].action == ACTION_UPDATE_CHECK)
				count += update_lines(lines + count);
			/* (Vita and PC: the bots' skill, vehicles and routes - the
			tab's rows, also the Online bots' - under Online bots while it
			fills, setting_shown) */
			if (page == PAGE_CROSSPLAY && settings[index].variable &&
				!strcmp(settings[index].variable, "HALO_BOTS_ONLINE"))
			{
				int bot;

				for (bot = 0; bot < SETTING_COUNT; bot++)
					if (settings[bot].variable && (!strcmp(settings[bot].variable, "HALO_BOT_SKILL") ||
						!strcmp(settings[bot].variable, "HALO_BOT_VEHICLES") ||
						!strcmp(settings[bot].variable, "HALO_BOT_ROUTES")) && setting_shown(&settings[bot]))
					{
						lines[count].type = LINE_SETTING;
						lines[count++].index = bot;
					}
			}
		}
	if (page == PAGE_GYRO)
	{
		/* (the gyroscope's rates now: redrawn twice a second) */
		lines[count].type = LINE_INFO;
		vita_gyro_status(lines[count++].text, sizeof(lines[0].text));
	}
	if (page == PAGE_MAPS)
	{
		if (maps_missing[0] && (choice_of("HALO_CUSTOM_EDITION") || maps_have_custom_edition || maps_installer_state))
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count].text, sizeof(lines[0].text), "!");
			snprintf(lines[count].text + 1, sizeof(lines[0].text) - 1, T("Missing: %s"), maps_missing);
			count++;
			/* (where they come from: the Halo CE installer copied in does) */
			lines[count].type = LINE_INFO;
			lines[count].text[0] = '!';
			if (maps_installer_state == 2)
				snprintf(lines[count++].text + 1, sizeof(lines[0].text) - 1, T("Taking them from %.28s"), maps_installer);
			else if (maps_installer_state == 1)
				snprintf(lines[count++].text + 1, sizeof(lines[0].text) - 1, "%s",
					T("Extract PC files takes them from the installer"));
			else
			{
				snprintf(lines[count++].text + 1, sizeof(lines[0].text) - 1, "%s", T("Copy them in, or the Halo CE installer"));
				lines[count].type = LINE_INFO;
				lines[count].text[0] = '!';
				snprintf(lines[count++].text + 1, sizeof(lines[0].text) - 1, "%s",
					T("(halocesetup*.exe) to ux0:data/haloce-vita"));
			}
		}
		/* (where the content is read from: the Halo content folder, or the
		Xbox maps alone) */
		lines[count].type = LINE_INFO;
		if (maps_content_halo1 > 0)
		{
			snprintf(lines[count++].text, sizeof(lines[0].text), T("Content: %.30s"), maps_content_folder);
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), T("Halo CE: %d MCC maps (no sound yet)"), maps_content_halo1);
		}
		else
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Content: the Xbox maps (no MCC folder)"));
		if (!map_count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("No custom maps in the maps folder"));
		}
		for (index = 0; index < map_count; index++)
		{
			lines[count].type = LINE_MAP;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_FORGE)
	{
		int playing = forge_panel_ai_counts(NULL, NULL);

		for (index = 0; index < FORGE_LINE_COUNT; index++)
		{
			if (!playing && (index == FORGE_LINE_CLEAR_DEAD || index == FORGE_LINE_CLEAR_ALL))
				continue;
			lines[count].type = LINE_FORGE;
			lines[count++].index = index;
		}
		lines[count].type = LINE_HEADING;
		snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("File Share"));
		if (!forge_entry_count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("No map variants yet: save one in Forge"));
		}
		for (index = 0; index < forge_entry_count; index++)
		{
			lines[count].type = LINE_VARIANT;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_FRIENDS)
	{
		/* (the code: the Friend code row's value) */
		lines[count].type = LINE_INFO;
		p2p_friends_status(lines[count++].text, sizeof(lines[0].text));
		lines[count].type = LINE_INFO;
		snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Triangle: remove a friend, block a request"));
		if (!friends_count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Give your code to friends, or type theirs"));
		}
		for (index = 0; index < friends_count; index++)
		{
			lines[count].type = LINE_FRIEND;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_PLAYERS)
	{
		lines[count].type = LINE_INFO;
		if (host_players_count < 0)
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("You host no network game"));
		else if (!host_players_count)
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("No one else is in your game"));
		else
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Cross kicks, Triangle bans, Square for good"));
		if (host_session_bans)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), T("%d kept out of your games"), host_session_bans);
		}
		for (index = 0; index < host_players_count && index < HALO_HOST_PLAYERS_MAXIMUM; index++)
		{
			lines[count].type = LINE_HOST_PLAYER;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_CHAT)
	{
		lines[count].type = LINE_INFO;
		snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("In play: SELECT + D-pad left, quick chat"));
		lines[count].type = LINE_INFO;
		if (chat_players_count < 0)
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Mute players: in a network game"));
		else
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Cross mutes a player's chat here"));
		for (index = 0; index < chat_players_count && index < HALO_CHAT_PLAYERS_MAXIMUM; index++)
		{
			lines[count].type = LINE_CHAT_PLAYER;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_PARTY)
	{
		int in_party;

		lines[count].type = LINE_INFO;
		in_party = p2p_party_status(lines[count++].text, sizeof(lines[0].text)) > 1;
		/* (the chat: its newest lines, then quick chat) */
		if (in_party)
		{
			struct p2p_party_chat_line chat[P2P_PARTY_CHAT_LINES];
			int chat_count = p2p_party_chat_lines(chat, P2P_PARTY_CHAT_LINES, 0), first;

			first = chat_count > PARTY_CHAT_PAGE_LINES ? chat_count - PARTY_CHAT_PAGE_LINES : 0;
			for (index = first; index < chat_count; index++)
			{
				lines[count].type = LINE_INFO;
				if (chat[index].notice)
					snprintf(lines[count++].text, sizeof(lines[0].text), "%.100s", chat[index].text);
				else
					snprintf(lines[count++].text, sizeof(lines[0].text), "%.20s: %.100s", chat[index].name, chat[index].text);
			}
			lines[count].type = LINE_PARTY_PRESET;
			lines[count++].index = party_preset;
		}
		for (index = 0; index < party_invites_count; index++)
		{
			lines[count].type = LINE_PARTY_INVITE;
			lines[count++].index = index;
		}
		for (index = 0; index < party_members_count; index++)
		{
			lines[count].type = LINE_PARTY_MEMBER;
			lines[count++].index = index;
		}
		lines[count].type = LINE_HEADING;
		snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Invite a friend"));
		if (!party_friends_count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("No friend online to invite"));
		}
		for (index = 0; index < party_friends_count; index++)
		{
			lines[count].type = LINE_PARTY_FRIEND;
			lines[count++].index = index;
		}
	}
	if (page == PAGE_MODS)
	{
		/* (the space used and free, or the look awaited) */
		lines[count].type = LINE_INFO;
		if (mods_state != 2)
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("Checking the maps and mods..."));
		else
		{
			char used[24], free_space[24];

			size_text(used, sizeof(used), (unsigned long long)mods_view.used_kilobytes << 10);
			size_text(free_space, sizeof(free_space), (unsigned long long)mods_view.free_kilobytes << 10);
			snprintf(lines[count++].text, sizeof(lines[0].text), T("%d items: %s used, %s free"), mods_view.count, used,
				free_space);
		}
		if (mods_state == 2 && !mods_view.count)
		{
			lines[count].type = LINE_INFO;
			snprintf(lines[count++].text, sizeof(lines[0].text), "%s", T("No custom maps or mods"));
		}
		for (index = 0; mods_state == 2 && index < mods_view.count; index++)
		{
			lines[count].type = LINE_MOD;
			lines[count++].index = index;
		}
	}
	return count;
}

/* what the game is doing with other machines (system_link_shortcut.c) */
static int multiplayer_state(void)
{
	return __atomic_load_n(&halo_multiplayer_status[SYSTEM_LINK_STATUS_STATE], __ATOMIC_ACQUIRE);
}

/* this Vita's name, and its address as the other Vitas reach it */
static void vita_line(char *text, int size)
{
	unsigned int address = (unsigned int)halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS];
	const unsigned char *bytes = (const unsigned char *)&address;
	char name[20];

	snprintf(name, sizeof(name), "%.16s", vita_name[0] ? vita_name : T("(no name)"));
	if (!strcmp(running_network, "adhoc"))
		snprintf(text, (size_t)size, T("This Vita: %s, ad hoc room %d"), name, choice_of("HALO_ADHOC_ROOM") + 1);
	else if (address)
		snprintf(text, (size_t)size, T("This Vita: %s  %u.%u.%u.%u"), name, bytes[0], bytes[1], bytes[2], bytes[3]);
	else if (multiplayer_state() == SYSTEM_LINK_STATE_STARTING)
		snprintf(text, (size_t)size, T("This Vita: %s"), name);
	else
		snprintf(text, (size_t)size, T("This Vita: %s, no Wi-Fi network"), name);
}

/* one line on the network game: none, looking for games, hosting ... */
static void game_line(char *text, int size)
{
	int machines = halo_multiplayer_status[SYSTEM_LINK_STATUS_MACHINES];
	int games = halo_multiplayer_status[SYSTEM_LINK_STATUS_GAMES];
	int maximum = halo_multiplayer_status[SYSTEM_LINK_STATUS_MAXIMUM];

	switch (multiplayer_state())
	{
	case SYSTEM_LINK_STATE_SEARCHING:
		if (games)
			snprintf(text, (size_t)size, T("Looking for games: %d found"), games);
		else
			snprintf(text, (size_t)size, "%s", T("Looking for games: none yet"));
		break;
	case SYSTEM_LINK_STATE_JOINING:
		snprintf(text, (size_t)size, "%s", T("Joining a game..."));
		break;
	case SYSTEM_LINK_STATE_HOSTING:
		/* (and the most players the game takes, once the game says) */
		if (machines > 1 && maximum > 0)
			snprintf(text, (size_t)size, T("Hosting: %d of %d players in the lobby"),
				halo_multiplayer_status[SYSTEM_LINK_STATUS_PLAYERS], maximum);
		else if (machines > 1)
			snprintf(text, (size_t)size, T("Hosting: %d Vitas in the lobby"), machines);
		else if (maximum > 0)
			snprintf(text, (size_t)size, T("Hosting: waiting for players (max %d)"), maximum);
		else
			snprintf(text, (size_t)size, "%s", T("Hosting: waiting for players"));
		break;
	case SYSTEM_LINK_STATE_LOBBY:
		snprintf(text, (size_t)size, T("In a lobby: %d Vitas, the host starts"), machines);
		break;
	case SYSTEM_LINK_STATE_IN_GAME:
		snprintf(text, (size_t)size, T("In a game: %d Vitas%s"), machines,
			halo_multiplayer_status[SYSTEM_LINK_STATUS_HOST] ? T(" (you host)") : "");
		break;
	case SYSTEM_LINK_STATE_PLAYING:
		snprintf(text, (size_t)size, "%s", T("Playing: quit the level to host or join"));
		break;
	default:
		snprintf(text, (size_t)size, "%s", T("No game yet: host one or join one"));
		break;
	}
}

/* one short line on internet or ad hoc play this session ("" on Wi-Fi) */
static void status_line(char *text, int size)
{
	char code[P2P_CODE_SIZE];
	char detail[96];

	text[0] = 0;
	if (!strcmp(running_network, "online"))
	{
		if (p2p_hosting_code(code, sizeof(code)))
		{
			/* (a public game: whether the brokers hold its listing) */
			switch (p2p_lobby_hosting_status(detail, sizeof(detail)))
			{
			case P2P_LOBBY_HOSTING_LISTED:
				snprintf(text, (size_t)size, T("Your code: %s; %.30s"), code, detail);
				break;
			case P2P_LOBBY_HOSTING_UNREACHABLE:
				snprintf(text, (size_t)size, T("Your code: %s; not listed: no game list"), code);
				break;
			case P2P_LOBBY_HOSTING_PENDING:
				snprintf(text, (size_t)size, T("Your code: %s; getting listed..."), code);
				break;
			case P2P_LOBBY_HOSTING_PRIVATE:
				snprintf(text, (size_t)size, T("Your code: %s (private)"), code);
				break;
			default:
				snprintf(text, (size_t)size, T("Your code: %s%s"), code, choice_of("HALO_NET_HOST_PUBLIC") ? T(" (public)") :
					"");
				break;
			}
		}
		else
		{
			p2p_status_shown(detail, sizeof(detail));
			snprintf(text, (size_t)size, T("Online: %.26s"), detail);
		}
	}
	else if (!strcmp(running_network, "adhoc"))
	{
		int state = vita_adhoc_state(detail, sizeof(detail));

		if (state == 2)
		{
			char group[64];

			p2p_adhoc_status(group, sizeof(group));
			snprintf(text, (size_t)size, T("Ad hoc: %.27s"), group);
		}
		else
		{
			snprintf(text, (size_t)size, "%s", state == 1 ? T("Ad hoc: joining...") : state == -1 ? T("Ad hoc: did not join") :
				T("Ad hoc: not in a group"));
		}
	}
}

/* the Connection row's help, by its choice */
static const char *connection_help(const struct setting *setting)
{
	const char *value = setting->values[setting->choice];

	return !strcmp(value, "adhoc") ? T("No router: Vitas side by side (experimental)") : !strcmp(value, "online") ?
		T("Internet play by code (experimental)") : T("Vitas on one Wi-Fi network play together");
}

/* ---------- PlayStation's terms

With the Button icons PlayStation the panel says what each Xbox button does
(in Halo's default controller layout, as the Button layout's help always
did) where it named the Xbox button: the Button layout's rows, the touch
zones' choices and diagram, a few help lines; and the Vita button where it
named the one the menus' fixed layout puts an Xbox button on (the steps of
Host a game and Join a game: Cross for A) */

static int playstation_terms(void)
{
	/* (the owner's choice: the Button icons change the game's icons only;
	the panel keeps the Xbox controller's names, A B X Y, Black, White ...,
	with either) */
	return 0;
}

/* the Xbox buttons by what they do (VITA_XBOX_*'s order) */
static const char *const xbox_actions[VITA_XBOX_COUNT] = { VITA_XBOX_ACTION_NAMES };

/* a row's label and help in PlayStation's terms: by its variable, or its
label for a row that opens a page */
static const struct
{
	const char *key;
	const char *label;
	const char *help;
} playstation_rows[] = {
	{ "HALO_XBOX_A", "Jump", "Jump (in the menus Cross stays Accept)" },
	{ "HALO_XBOX_B", "Melee", "Melee (in the menus Circle stays Back)" },
	{ "HALO_XBOX_X", "Action, reload", "Action, reload (in play only)" },
	{ "HALO_XBOX_Y", "Switch weapon", "Switch weapon (in play only)" },
	{ "HALO_XBOX_BLACK", "Switch grenades", "Switch grenades (in play only)" },
	{ "HALO_XBOX_WHITE", "Flashlight", "Flashlight (in play only)" },
	{ "HALO_XBOX_LEFT_TRIGGER", "Throw grenade", "Throw a grenade (in play only)" },
	{ "HALO_XBOX_RIGHT_TRIGGER", "Fire", "Fire (in play only)" },
	{ "HALO_XBOX_LEFT_STICK", "Crouch", "Crouch (in play only)" },
	{ "HALO_XBOX_RIGHT_STICK", "Zoom", "Zoom (in play only)" },
	{ "HALO_XBOX_BACK", "Scoreboard", "Scoreboard (in the menus Select stays Back)" },
	{ "HALO_CROUCH_TOGGLE", NULL, "A press crouches, the next stands (Toggle)" },
	{ "HALO_DEBUG_CAMERA", NULL, "Hold Black 1 s: follow, orbit, then a flying camera" },
	{ "Button layout", NULL, "Which Vita button each action is on, in play" },
	{ "Touch zones", NULL, "Front and rear touch zones as actions" },
};

static int playstation_row(const struct setting *setting)
{
	const char *key = setting->variable ? setting->variable : setting->label;
	int index;

	if (!playstation_terms())
		return -1;
	for (index = 0; index < (int)(sizeof(playstation_rows) / sizeof(playstation_rows[0])); index++)
		if (!strcmp(playstation_rows[index].key, key))
			return index;
	return -1;
}

/* (each shown in the language chosen: lang.c) */
static const char *setting_label(const struct setting *setting)
{
	int row = playstation_row(setting);

	return T(row >= 0 && playstation_rows[row].label ? playstation_rows[row].label : setting->label);
}

/* Map downloads' help, by choice */
static const char *const map_downloads_help[3] = {
	N_("Asks before a host's map comes; public games warn"),
	N_("Asks, but not in games from the public lobby"),
	N_("No downloads: copy the host's map in yourself"),
};

static const char *setting_help(const struct setting *setting)
{
	static char camera_help[128];
	int row = playstation_row(setting);

	if (setting->variable && !strcmp(setting->variable, "HALO_MAP_SHARE_FROM") && setting->choice >= 0 &&
		setting->choice < 3)
		return T(map_downloads_help[setting->choice]);

	/* (the debug camera's Black: the Vita button it is on) */
	if (row >= 0 && !strcmp(playstation_rows[row].key, "HALO_DEBUG_CAMERA"))
	{
		const struct setting *black = setting_named("HALO_XBOX_BLACK");

		if (black && strcmp(black->values[black->choice], "none"))
		{
			snprintf(camera_help, sizeof(camera_help), T("Hold %s 1 s: follow, orbit, then a flying camera"),
				T(black->names[black->choice]));
			return camera_help;
		}
	}
	return T(row >= 0 ? playstation_rows[row].help : setting->help);
}

/* the name of a row's choice: a touch zone's Xbox button by what it does;
in the language chosen, but a language's own name (the Language row's) */
static const char *setting_value_name(const struct setting *setting)
{
	if (setting->page == PAGE_TOUCH && setting->values[0] && !strcmp(setting->values[0], "off") &&
		playstation_terms() && setting->choice < VITA_XBOX_COUNT)
		return T(xbox_actions[setting->choice]);
	if (setting->variable && !strcmp(setting->variable, "HALO_LANGUAGE") && setting->choice > 0)
		return setting->names[setting->choice];
	/* (a player model's name is its map's own) */
	if (setting->variable && !strcmp(setting->variable, "HALO_PLAYER_MODEL") && setting->choice > 0)
		return setting->names[setting->choice];
	return T(setting->names[setting->choice]);
}

/* an Xbox face button in the menus (A B X Y), as the guide names it: the
Xbox's name, or the Vita button the menus put it on */
static const char *menu_button(char xbox)
{
	static const char *const vita_names[4] = { "Cross", "Circle", "Square", "Triangle" };
	static const char *const xbox_names[4] = { "A", "B", "X", "Y" };
	const char *letters = "ABXY";
	int index = (int)(strchr(letters, xbox) - letters);

	return playstation_terms() ? T(vita_names[index]) : xbox_names[index];
}

/* the longer line at the bottom: what the selected line does, or what the
last action did */
static void help_line(char *text, int size, const struct line *line)
{
	char detail[96];
	const struct setting *setting = line && line->type == LINE_SETTING ? &settings[line->index] : NULL;
	int state = __atomic_load_n(&report_state, __ATOMIC_ACQUIRE);

	if (notice[0] && now_us() < notice_until)
		snprintf(text, (size_t)size, "%s", notice);
	else if (halo_screen_restart_needed())
		snprintf(text, (size_t)size, "%s", T("Restart the game for this change"));
	else if (restart_pending && setting && setting->restart)
		snprintf(text, (size_t)size, "%s", T("Restart the game for this change"));
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_SAVING)
		snprintf(text, (size_t)size, "%s", T("Saving the report..."));
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_SAVED)
		snprintf(text, (size_t)size, T("Saved: %.56s"), report_path);
	else if (setting && setting->action == ACTION_SAVE_REPORT && state == REPORT_FAILED)
		snprintf(text, (size_t)size, "%s", T("Could not save the report"));
	else if (line && line->type == LINE_MAP)
		snprintf(text, (size_t)size, T("%s: on or off, or delete it"),
			maps[line->index].format == MAP_XBOX ? T("Xbox map") : maps[line->index].format == MAP_OTHER ?
			T("Not a known map") : maps[line->index].format == MAP_CUSTOM_EDITION_CAMPAIGN ?
			T("PC campaign map, in Campaign's levels (needs PC maps On)") : T("PC map (needs PC maps On)"));
	else if (line && line->type == LINE_FORGE)
		snprintf(text, (size_t)size, "%s", line->index == FORGE_LINE_MAP ? T("The multiplayer map to edit") :
			line->index == FORGE_LINE_VARIANT ? T("Its objects placed as Forge starts") :
			line->index == FORGE_LINE_NETWORK ? T("Local: alone. Network: friends forge with you (8, never listed online)") :
			line->index == FORGE_LINE_CLEAR_DEAD ? T("Dead AI you placed, out of the layout: room for more") :
			line->index == FORGE_LINE_CLEAR_ALL ? T("Every AI you placed, alive or dead, out of the layout") :
			T("A local game: you fly as the monitor and place objects"));
	else if (line && line->type == LINE_VARIANT)
	{
		const struct forge_store_entry *entry = &forge_entries[line->index];

		if (entry->damaged)
			snprintf(text, (size_t)size, "%s", T("Damaged: it can't be used. Square deletes it"));
		else
			snprintf(text, (size_t)size, T("%d objects, saved %.16s"), entry->objects, entry->date[0] ? entry->date : "-");
	}
	else if (line && line->type == LINE_MOD)
	{
		const struct halo_mod_entry *entry = &mods_view.entries[line->index];

		if (!entry->valid)
			snprintf(text, (size_t)size, T("Failed: %.60s"), entry->why);
		else if (entry->needs[0])
			snprintf(text, (size_t)size, T("Needs: %.60s"), entry->needs);
		else
			snprintf(text, (size_t)size, T("%s: on or off, or delete it"), mod_kind_name(entry->kind));
	}
	else if (line && line->type == LINE_PARTY_MEMBER)
	{
		const struct p2p_party_member *member = &party_members_view[line->index];

		snprintf(text, (size_t)size, "%s: %.50s", member->code, member->note[0] ? member->note : member->is_friend ||
			member->self ? T("in your party") : T("not on your friends list"));
	}
	else if (line && line->type == LINE_PARTY_INVITE)
		snprintf(text, (size_t)size, T("%.16s's party, %d of %d: join to follow their games"),
			party_invites_view[line->index].name, party_invites_view[line->index].size, P2P_PARTY_MAXIMUM);
	else if (line && line->type == LINE_PARTY_FRIEND)
	{
		const struct p2p_friend_entry *entry = &friends_view[party_friends_view[line->index]];

		snprintf(text, (size_t)size, "%s: %.50s", entry->name, entry->party[0] ? entry->party : entry->activity);
	}
	else if (line && line->type == LINE_PARTY_PRESET)
		snprintf(text, (size_t)size, "%s", T("Said to your party at once, in their language"));
	else if (line && line->type == LINE_HOST_PLAYER)
		snprintf(text, (size_t)size, "%s", T("A ban keeps their PS Vita or PC out of your games"));
	else if (line && line->type == LINE_CHAT_PLAYER)
		snprintf(text, (size_t)size, "%s", T("Muted: their chat is hidden here; they're never told"));
	else if (line && line->type == LINE_FRIEND)
	{
		const struct p2p_friend_entry *entry = &friends_view[line->index];

		if (entry->kind == P2P_FRIEND_KIND_FRIEND && entry->online && entry->maximum)
			snprintf(text, (size_t)size, T("%s: %.40s, %d of %d"), entry->name, entry->activity, entry->players,
				entry->maximum);
		else
			snprintf(text, (size_t)size, "%s: %.50s", entry->kind == P2P_FRIEND_KIND_OUTGOING ? entry->code : entry->name,
				entry->activity);
	}
	else if (!setting)
		text[0] = 0;
	else if (!strcmp(running_network, "online") && (setting->action == ACTION_JOIN_CODE ||
		setting->action == ACTION_BROWSE || (setting->variable && !strcmp(setting->variable, "HALO_NET_HOST_PUBLIC"))) &&
		p2p_status_shown(detail, sizeof(detail)))
		snprintf(text, (size_t)size, "%.70s", detail);
	/* (ad hoc: the group is joined first, when not in it) */
	else if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2 &&
		(setting->action == ACTION_HOST || setting->action == ACTION_JOIN || setting->action == ACTION_CAMPAIGN ||
			setting->action == ACTION_ADHOC_JOIN))
		snprintf(text, (size_t)size, T("First the system's dialog joins ad hoc room %d"), choice_of("HALO_ADHOC_ROOM") + 1);
	else if (setting->variable && !strcmp(setting->variable, "HALO_VITA_NETWORK"))
		snprintf(text, (size_t)size, "%s", connection_help(setting));
	/* (a pre-release build: Experimental only, until the release) */
	else if (setting->variable && !strcmp(setting->variable, "HALO_UPDATE_CHANNEL") &&
		update_build_is_prerelease(HALO_VITA_VERSION) && update_version_release(HALO_VITA_VERSION, detail, sizeof(detail)))
		snprintf(text, (size_t)size, T("Stable updates arrive with %s"), detail);
	else
		snprintf(text, (size_t)size, "%s", setting_help(setting));
}

/* the tab bar: '\t', the tabs shown with '|' between, '*' before this one */
static int tab_bar(char *text, int size)
{
	int length = snprintf(text, (size_t)size, "\t"), index, first = 1;

	for (index = 0; index < TAB_COUNT && length < size; index++)
		if (tab_shown(index))
		{
			length += snprintf(text + length, (size_t)(size - length), "%s%s%s", first ? "" : "|",
				index == tab ? "*" : "", T(pages[index].name));
			first = 0;
		}
	return length;
}

/* the touch zones' diagram (vita_gxm.c menu_build draws it): a line of
'\x01', a character per zone in vita_controls.h's order (S the row's zone,
s while Off, A a zone set to an Xbox button, - one Off), a space, and a
character per zone for its Xbox button ('a' + the VITA_XBOX_* index);
nothing for a row that is not a zone's */
static void touch_diagram(char *text, int size, const struct setting *chosen)
{
	char zones[2 * VITA_ZONE_COUNT + 2];
	int zone, is_zone = 0;

	for (zone = 0; zone < VITA_ZONE_COUNT; zone++)
	{
		const struct setting *setting = setting_named(vita_touch_variables[zone]);
		int choice = setting ? setting->choice : 0;

		if (setting == chosen)
		{
			is_zone = 1;
			zones[zone] = choice ? 'S' : 's';
		}
		else
			zones[zone] = choice ? 'A' : '-';
		/* (capitals: the actions' short names, PlayStation's terms) */
		zones[VITA_ZONE_COUNT + 1 + zone] = (char)((playstation_terms() ? 'A' : 'a') + (choice < VITA_XBOX_COUNT ?
			choice : 0));
	}
	zones[VITA_ZONE_COUNT] = ' ';
	zones[2 * VITA_ZONE_COUNT + 1] = 0;
	if (is_zone)
		snprintf(text, (size_t)size, "\n\x01%s", zones);
}

/* the panel's buttons for the line chosen, in one line under its help */
static void footer_line(char *text, int size, const struct line *line)
{
	const struct setting *setting = line && line->type == LINE_SETTING ? &settings[line->index] : NULL;
	const char *middle = "";

	if (line && line->type == LINE_MAP)
		middle = T("Left/right: on/off   Square: delete   ");
	else if (line && line->type == LINE_VARIANT)
		middle = T("Square: delete   ");
	else if (line && line->type == LINE_FORGE)
		middle = line->index == FORGE_LINE_START || line->index == FORGE_LINE_CLEAR_DEAD ||
			line->index == FORGE_LINE_CLEAR_ALL ? T("Cross: select   ") : T("Left/right: change   ");
	else if (line && line->type == LINE_MOD)
		middle = T("Cross: details   Square: delete   ");
	else if (line && line->type == LINE_PARTY_MEMBER)
		middle = party_members_view[line->index].self ? "" : p2p_party_leading() ?
			T("Cross: mute  Triangle: remove  ") : T("Cross: mute   ");
	else if (line && line->type == LINE_PARTY_INVITE)
		middle = T("Cross: join party  Square: no  ");
	else if (line && line->type == LINE_PARTY_FRIEND)
		middle = T("Cross: invite to party   ");
	else if (line && line->type == LINE_PARTY_PRESET)
		middle = T("Left/right: choose  Cross: send  ");
	else if (line && line->type == LINE_HOST_PLAYER)
		middle = T("Cross: kick  Triangle: ban  ");
	else if (line && line->type == LINE_CHAT_PLAYER)
		middle = chat_players_local[line->index] ? "" : chat_players_muted[line->index] ? T("Cross: unmute  ") :
			T("Cross: mute  ");
	else if (line && line->type == LINE_FRIEND)
	{
		const struct p2p_friend_entry *entry = &friends_view[line->index];

		middle = entry->kind == P2P_FRIEND_KIND_REQUEST ? T("Cross: accept  Square: decline  ") :
			entry->kind == P2P_FRIEND_KIND_INVITE ? T("Cross: accept  Square: ignore  ") :
			entry->kind == P2P_FRIEND_KIND_FRIEND ? T("Cross: join  Square: invite  ") :
			entry->kind == P2P_FRIEND_KIND_OUTGOING ? T("Square: cancel   ") : T("Cross: unblock   ");
	}
	else if (setting && setting->kind == KIND_ACTION)
		middle = T("Cross: select   ");
	else if (setting && setting->kind == KIND_TEXT)
		middle = T("Cross: type   ");
	else if (setting)
		middle = T("Left/right: change   ");
	snprintf(text, (size_t)size, T("L/R: tabs   %sCircle: %s"), middle, page < TAB_COUNT ? T("close") : T("back"));
}

/* what a row that opens a page says of it ("" for nothing) */
static void page_summary(char *text, int size, const struct setting *setting)
{
	int index, set = 0, moved = 0;

	text[0] = 0;
	/* (the Friend code row: the code) */
	if (setting->action == ACTION_FRIEND_BACKUP)
	{
		p2p_friends_code(text, size);
		return;
	}
	switch (setting->opens)
	{
	case PAGE_BUTTONS:
		for (index = 0; index < SETTING_COUNT; index++)
			if (settings[index].page == PAGE_BUTTONS && settings[index].choice != shipped_choice[index] &&
				strcmp(settings[index].variable, "HALO_BUTTON_ICONS"))
				moved = 1;
		snprintf(text, (size_t)size, "%s", moved ? T("Custom") : T("As shipped"));
		break;
	case PAGE_MENUS:
	{
		/* (the menus chosen: Halo 3 or Halo CE) */
		const struct setting *style = setting_named("HALO_MENU_STYLE");

		snprintf(text, (size_t)size, "%s", style ? T(style->names[style->choice]) : "");
		break;
	}
	case PAGE_TOUCH:
		for (index = 0; index < SETTING_COUNT; index++)
			if (settings[index].page == PAGE_TOUCH && settings[index].choice)
				set++;
		if (set)
			snprintf(text, (size_t)size, T("%d on"), set);
		else
			snprintf(text, (size_t)size, "%s", T("Off"));
		break;
	case PAGE_FRIENDS:
	{
		int online = 0, requests = 0, index;

		friends_refresh(0);
		for (index = 0; index < friends_count; index++)
		{
			online += friends_view[index].kind == P2P_FRIEND_KIND_FRIEND && friends_view[index].online;
			requests += friends_view[index].kind == P2P_FRIEND_KIND_REQUEST ||
				friends_view[index].kind == P2P_FRIEND_KIND_INVITE;
		}
		if (requests)
			snprintf(text, (size_t)size, T("%d online, %d new"), online, requests);
		else
			snprintf(text, (size_t)size, T("%d online"), online);
		break;
	}
	case PAGE_PARTY:
	{
		char status[96];
		int size_now = p2p_party_status(status, sizeof(status));

		if (size_now)
			snprintf(text, (size_t)size, T("%d of %d"), size_now, P2P_PARTY_MAXIMUM);
		else
			snprintf(text, (size_t)size, "%s", T("None"));
		break;
	}
	case PAGE_MODS:
		/* (once looked: how many) */
		if (mods_state == 2)
			snprintf(text, (size_t)size, T("%d items"), mods_view.count);
		break;
	case PAGE_MAPS:
		if (map_count == 1)
			snprintf(text, (size_t)size, "%s", T("1 map"));
		else
			snprintf(text, (size_t)size, T("%d maps"), map_count);
		break;
	case PAGE_FORGE:
		if (forge_entry_count == 1)
			snprintf(text, (size_t)size, "%s", T("1 variant"));
		else
			snprintf(text, (size_t)size, T("%d variants"), forge_entry_count);
		break;
	}
}

/* the classic panel's lines fit its height (vita_gxm.c menu_build, at
960x544, which does not fit its rows to its height as the Halo 3 menus'
panel, h3_panel, does): the lines from 76 pixels down, 28 a row, 22 a
heading ('\x08'), 36 a page's title, 14 a gap; the chosen row's bar 22
pixels under its line's top, above the rule at 468 over the help and the
buttons */
static int classic_panel_fits(const char *text)
{
	const char *at = strchr(text, '\n');
	int y = 76, last = 0;

	while (at)
	{
		const char *line = at + 1;
		int bytes = (int)strcspn(line, "\n");

		if (line[0] == '\x01' || line[0] == '\x05' || line[0] == '\x06')
			;
		else if (line[0] == '\x03')
			y += 36;
		else if (line[0] == '\x08')
			y += 22;
		else if (!bytes)
			y += 14;
		else
		{
			last = y;
			y += 28;
		}
		at = line[bytes] ? line + bytes : NULL;
	}
	return last + 22 <= 468;
}

/* Menu style Online: its panel (vita_gxm.c online_panel) */
static int online_panel_style(void)
{
	const struct setting *style = setting_named("HALO_MENU_STYLE");

	return style && !strcmp(style->values[style->choice], "online");
}

/* the Halo CE menus chosen (Menu style): the classic panel draws the
settings (vita_gxm.c, halo_menu_style_halo3) */
static int classic_panel(void)
{
	const struct setting *style = setting_named("HALO_MENU_STYLE");

	return style && (!strcmp(style->values[style->choice], "classic") || !strcmp(style->values[style->choice], "original"));
}

/* Menu style Classic ("original"): the Xbox menus as they shipped - no
OpenCE Multiplayer screen, so its Direct Link and FRIENDS are not in the
menus: the panel has them */
static int original_style(void)
{
	const struct setting *style = setting_named("HALO_MENU_STYLE");

	return style && !strcmp(style->values[style->choice], "original");
}

/* the PC menus (OpenCE's Multiplayer screen: menu_tags.c) are what the
menus show: read, and not Menu style Classic */
static int pc_menus_shown(void)
{
	return halo_pc_menus_state == 1 && !original_style();
}

/* the page's text, as vita_gxm.c menu_build draws it: the tab bar ('\t'),
a page's title ('\x03') under it, a row per line (its label, '\x02', its
value: the values make a column), the lines that are not rows ('\x04': the
game's state, the gyroscope's), the headings over rows ('\x08') but the
first `skip`, an empty line for a gap, then the chosen
row's help ('\x05') and the panel's buttons ('\x06') at the bottom. The
chosen line's number on the panel, and in *headings the headings there are */
static int list_text(char *text, int size, const struct line *lines, int count, int selected, int skip, int *headings)
{
	int length, index, number = 0, highlighted = 0;
	char status[128], help[192];

	*headings = 0;
	length = tab_bar(text, size);
	if (page >= TAB_COUNT && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n\x03%s > %s", T(pages[tab].name), T(pages[page].name));
		number++;
	}
	for (index = 0; index < count && length < size; index++)
	{
		const struct line *line = &lines[index];

		if (line->type == LINE_MAP && (line->index < map_scroll || line->index >= map_scroll + MAP_LINES))
			continue;
		if (line->type == LINE_VARIANT && (line->index < forge_scroll || line->index >= forge_scroll + FORGE_PANEL_LINES))
			continue;
		if (line->type == LINE_MOD && (line->index < mods_scroll || line->index >= mods_scroll + MAP_LINES))
			continue;
		if (line->type == LINE_FRIEND && (line->index < friends_scroll || line->index >= friends_scroll + FRIEND_LINES))
			continue;
		if (line->type == LINE_PARTY_FRIEND && (line->index < party_friends_scroll ||
			line->index >= party_friends_scroll + PARTY_FRIEND_LINES))
		{
			continue;
		}
		/* (the classic panel short of room: the first headings left out) */
		if (line->type == LINE_HEADING && (*headings)++ < skip)
			continue;
		/* (Modded maps: a gap between its switch and the maps; the Forge page:
		before its File Share heading; the Mod manager: between its totals and
		the entries) */
		if (index && ((page == PAGE_MAPS && line->type != LINE_SETTING && lines[index - 1].type == LINE_SETTING) ||
			(page == PAGE_FORGE && line->type == LINE_HEADING) ||
			(page == PAGE_MODS && line->type == LINE_MOD && lines[index - 1].type == LINE_INFO) ||
			(page == PAGE_FRIENDS && line->type == LINE_INFO && lines[index - 1].type == LINE_SETTING)))
		{
			length += snprintf(text + length, (size_t)(size - length), "\n");
			number++;
		}
		number++;
		if (index == selected)
			highlighted = number;
		if (line->type == LINE_INFO)
			length += snprintf(text + length, (size_t)(size - length), "\n%s%s", line->text[0] == '!' ? "" : "\x04",
				line->text);
		else if (line->type == LINE_HEADING)
			length += snprintf(text + length, (size_t)(size - length), "\n\x08%s", line->text);
		else if (line->type == LINE_FORGE)
		{
			int variants = 0;
			const struct forge_store_entry *variant = forge_map_variant(forge_variant, &variants);

			if (line->index == FORGE_LINE_MAP)
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %s", T("Map"),
					forge_map >= 0 ? T(forge_panel_map_name(forge_map)) : T("none"));
			else if (line->index == FORGE_LINE_VARIANT)
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02%c %.24s %c", T("Map variant"),
					forge_variant > 0 ? '<' : ' ', variant ? variant->title : T("None"), forge_variant < variants ? '>' : ' ');
			else if (line->index == FORGE_LINE_NETWORK)
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02%c %s %c", T("Network"),
					forge_network ? '<' : ' ', !forge_network ? T("Local") : !strcmp(running_network, "online") ? T("Online") :
					!strcmp(running_network, "adhoc") ? T("Ad hoc") : T("Same Wi-Fi"), forge_network ? ' ' : '>');
			else if (line->index == FORGE_LINE_CLEAR_DEAD || line->index == FORGE_LINE_CLEAR_ALL)
			{
				int alive = 0, dead = 0;

				forge_panel_ai_counts(&alive, &dead);
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %d  >",
					line->index == FORGE_LINE_CLEAR_DEAD ? T("Clear dead AI") : T("Clear all AI"),
					line->index == FORGE_LINE_CLEAR_DEAD ? dead : alive + dead);
			}
			else
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  >", T("Start Forge"));
		}
		else if (line->type == LINE_VARIANT)
		{
			const struct forge_store_entry *entry = &forge_entries[line->index];

			length += snprintf(text + length, (size_t)(size - length), "\n%.22s\x02  %.16s", entry->damaged ?
				T("Damaged file") : entry->title, forge_map_title(entry->map_name));
		}
		else if (line->type == LINE_MAP)
		{
			const struct map_entry *map = &maps[line->index];
			char size_name[24], name[24];

			size_text(size_name, sizeof(size_name), map->size);
			/* (the .yelo of a name: marked *) */
			snprintf(name, sizeof(name), "%.19s%s", map->name, strcasecmp(map->extension, "yelo") ? "" : "*");
			const char *state = map_disabled(map->name) ? T("Off") : T("On");
			/* (on or off, three characters wide: a translation's by its
			characters, not its UTF-8 bytes) */
			int pad = 3 - lang_utf8_length(state);

			length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %s%*s %8s  %s", name, state,
				pad > 0 ? pad : 0, "", size_name, map_format_name(map->format));
		}
		else if (line->type == LINE_PARTY_MEMBER)
			length += snprintf(text + length, (size_t)(size - length), "\n%.20s\x02  %s",
				party_members_view[line->index].name, party_member_word(&party_members_view[line->index]));
		else if (line->type == LINE_PARTY_INVITE)
			length += snprintf(text + length, (size_t)(size - length), "\n%.20s\x02  %s",
				party_invites_view[line->index].name, T("Party invite"));
		else if (line->type == LINE_PARTY_FRIEND)
			length += snprintf(text + length, (size_t)(size - length), "\n%.20s\x02  %s",
				friends_view[party_friends_view[line->index]].name, T("Invite"));
		else if (line->type == LINE_PARTY_PRESET)
			length += snprintf(text + length, (size_t)(size - length), "\n%s\x02< %.24s >", T("Quick chat"),
				p2p_party_chat_preset_text(party_preset));
		else if (line->type == LINE_HOST_PLAYER)
			length += snprintf(text + length, (size_t)(size - length), "\n%.15s\x02  %s",
				host_players_names[line->index], T("In your game"));
		else if (line->type == LINE_CHAT_PLAYER)
			length += snprintf(text + length, (size_t)(size - length), "\n%.15s\x02  %s",
				chat_players_names[line->index], chat_players_local[line->index] ? T("You") :
				chat_players_muted[line->index] ? T("Muted") : T("Chat shown"));
		else if (line->type == LINE_FRIEND)
		{
			const struct p2p_friend_entry *entry = &friends_view[line->index];

			/* (the name in the label's column, a word of its state in the
			value's: the help line has the rest) */
			/* (a friend on this project's PC build: "(PC)" after the name) */
			if (entry->pc && entry->kind != P2P_FRIEND_KIND_OUTGOING)
				length += snprintf(text + length, (size_t)(size - length), "\n%.15s (PC)\x02  %s", entry->name,
					friend_state_word(entry));
			else
				length += snprintf(text + length, (size_t)(size - length), "\n%.20s\x02  %s",
					entry->kind == P2P_FRIEND_KIND_OUTGOING ? entry->code : entry->name, friend_state_word(entry));
		}
		else if (line->type == LINE_MOD)
		{
			const struct halo_mod_entry *entry = &mods_view.entries[line->index];
			char size_name[24], name[24];
			const char *state = mod_state_name(entry);
			/* (the state five characters wide, by its characters) */
			int pad = 5 - lang_utf8_length(state);

			size_text(size_name, sizeof(size_name), (unsigned long long)entry->kilobytes << 10);
			snprintf(name, sizeof(name), "%.20s", entry->kind == HALO_MOD_KIND_MCC_MOD ? entry->title : entry->file);
			length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %s%*s %8s  %s", name, state,
				pad > 0 ? pad : 0, "", size_name, mod_kind_short(entry->kind));
		}
		else
		{
			const struct setting *setting = &settings[line->index];

			if (setting->kind == KIND_TEXT)
			{
				const struct play_text *value = play_text_named(setting->variable);

				/* (the password hidden) */
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %s", setting_label(setting),
					!value->value[0] ? (!strcmp(setting->variable, "HALO_NET_LOBBY_NAME") ? T("your profile's name") : T("none")) :
					!strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD") ? T("set") : value->value);
			}
			else if (setting->kind == KIND_ACTION)
			{
				char summary[48];

				page_summary(summary, sizeof(summary), setting);
				/* (two spaces first: the summary, or the >, where the other
				rows' values start, after their "< ") */
				length += snprintf(text + length, (size_t)(size - length), "\n%s\x02  %s%s>", setting_label(setting), summary,
					summary[0] ? "  " : "");
			}
			else
			{
				int last = choice_last(setting), choice, before = 0, after = 0;

				/* (the arrows: a choice offered that way) */
				for (choice = 0; choice <= last; choice++)
					if (!choice_disabled(setting, choice))
					{
						before |= choice < setting->choice;
						after |= choice > setting->choice;
					}
				/* (a row that applies after a restart: marked *) */
				length += snprintf(text + length, (size_t)(size - length), "\n%s%s\x02%c %s %c", setting_label(setting),
					setting->restart ? "*" : "", before ? '<' : ' ', setting_value_name(setting), after ? '>' : ' ');
				/* (Menu style Online: where the choice stands among them,
				'\x0b' choice/last, for its panel's slider: vita_gxm.c
				online_panel; the other panels never see it) */
				if (online_panel_style() && last > 0)
					length += snprintf(text + length, (size_t)(size - length), "\x0b%d/%d", setting->choice, last);
				/* (the choices this build does not offer, greyed out after a
				'\x07', after the arrow's place: vita_gxm.c menu_row) */
				for (choice = 0; choice <= last && length < size; choice++)
					if (choice_disabled(setting, choice))
						length += snprintf(text + length, (size_t)(size - length), "\x07%s", T(setting->names[choice]));
			}
		}
	}
	if (page == PAGE_FORGE && forge_entry_count > FORGE_PANEL_LINES && length + 2 < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n\x04");
		length += snprintf(text + length, (size_t)(size - length), T("(variants %d-%d of %d)"), forge_scroll + 1,
			forge_scroll + FORGE_PANEL_LINES < forge_entry_count ? forge_scroll + FORGE_PANEL_LINES : forge_entry_count,
			forge_entry_count);
	}
	if (page == PAGE_FRIENDS && friends_count > FRIEND_LINES && length + 2 < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n\x04");
		length += snprintf(text + length, (size_t)(size - length), T("(%d-%d of %d)"), friends_scroll + 1,
			friends_scroll + FRIEND_LINES < friends_count ? friends_scroll + FRIEND_LINES : friends_count, friends_count);
	}
	if (page == PAGE_MODS && mods_state == 2 && mods_view.count > MAP_LINES && length + 2 < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n\x04");
		length += snprintf(text + length, (size_t)(size - length), T("(%d-%d of %d)"), mods_scroll + 1,
			mods_scroll + MAP_LINES < mods_view.count ? mods_scroll + MAP_LINES : mods_view.count, mods_view.count);
	}
	if (page == PAGE_MAPS && map_count > MAP_LINES && length + 2 < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n\x04");
		length += snprintf(text + length, (size_t)(size - length), T("(maps %d-%d of %d)"), map_scroll + 1,
			map_scroll + MAP_LINES < map_count ? map_scroll + MAP_LINES : map_count, map_count);
	}
	/* (Multiplayer, after a gap: this Vita, the network game, internet or
	ad hoc play; the Play page: the last two) */
	if ((page == TAB_MULTIPLAYER || page == PAGE_PLAY) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "\n");
		if (page == TAB_MULTIPLAYER)
		{
			vita_line(status, sizeof(status));
			length += snprintf(text + length, (size_t)(size - length), "\n\x04%s", status);
			/* (the game's Multiplayer menu without OpenCE's screens) */
			if (halo_pc_menus_state < 0 && length < size)
				length += snprintf(text + length, (size_t)(size - length), "\n\x04%s",
					T("Online menus need bitmaps.map, loc.map: README"));
		}
		game_line(status, sizeof(status));
		if (length < size)
			length += snprintf(text + length, (size_t)(size - length), "\n\x04%s", status);
		status_line(status, sizeof(status));
		if (status[0] && length < size)
			length += snprintf(text + length, (size_t)(size - length), "\n\x04%s", status);
		/* (hosting a game for Vitas and PC, online: its OpenCE join link, which
		PC players open as their own; the Vitas' code above: docs/crossplay.md
		T16) */
		{
			char invite[96];

			if (!strcmp(running_network, "online") && network_wire_crossplay() &&
				p2p_hosting_invite(invite, (int)sizeof(invite)) && strlen(invite) > 38 && length < size)
			{
				/* (the link in two lines of the panel's 46 characters) */
				length += snprintf(text + length, (size_t)(size - length), "\n\x04%s\n\x04%.38s\n\x04%s",
					T("PC players (OpenCE) join with:"), invite, invite + 38);
			}
		}
	}
	help_line(help, sizeof(help), count ? &lines[selected] : NULL);
	if (length < size)
		length += snprintf(text + length, (size_t)(size - length), "\n\x05%s", help);
	footer_line(help, sizeof(help), count ? &lines[selected] : NULL);
	if (length < size)
		length += snprintf(text + length, (size_t)(size - length), "\n\x06%s", help);
	/* the Touch zones page: the zones drawn beside the rows */
	if (count && lines[selected].type == LINE_SETTING && length < size)
		touch_diagram(text + length, size - length, &settings[lines[selected].index]);
	return highlighted;
}

/* the page shown (list_text). The classic panel, the Halo CE menus', does
not fit its rows to its height: while the lines would run past it, the
headings give way, the first first (the tab bar over its first rows) */
static void show_list(void)
{
	char text[3072];
	struct line lines[MAXIMUM_LINES];
	int count = page_lines(lines), index, highlighted, headings, skip;
	int *selected = &page_selected[page];

	if (*selected >= count)
		*selected = count - 1;
	if (*selected < 0)
		*selected = 0;
	/* (a line that is not a row is not chosen) */
	if (count && line_passive(&lines[*selected]))
		for (index = 0; index < count; index++)
			if (!line_passive(&lines[index]))
			{
				*selected = index;
				break;
			}
	/* (the map lines scroll: MAP_LINES at once, the chosen one among them) */
	if (page == PAGE_MAPS && count && lines[*selected].type == LINE_MAP)
	{
		int map = lines[*selected].index;

		if (map < map_scroll)
			map_scroll = map;
		if (map >= map_scroll + MAP_LINES)
			map_scroll = map - MAP_LINES + 1;
	}
	if (page == PAGE_FORGE && count && lines[*selected].type == LINE_VARIANT)
	{
		int variant = lines[*selected].index;

		if (variant < forge_scroll)
			forge_scroll = variant;
		if (variant >= forge_scroll + FORGE_PANEL_LINES)
			forge_scroll = variant - FORGE_PANEL_LINES + 1;
	}
	if (page == PAGE_MODS && count && lines[*selected].type == LINE_MOD)
	{
		int entry = lines[*selected].index;

		if (entry < mods_scroll)
			mods_scroll = entry;
		if (entry >= mods_scroll + MAP_LINES)
			mods_scroll = entry - MAP_LINES + 1;
	}
	if (page == PAGE_FRIENDS && count && lines[*selected].type == LINE_FRIEND)
	{
		int entry = lines[*selected].index;

		if (entry < friends_scroll)
			friends_scroll = entry;
		if (entry >= friends_scroll + FRIEND_LINES)
			friends_scroll = entry - FRIEND_LINES + 1;
	}
	if (page == PAGE_PARTY && count && lines[*selected].type == LINE_PARTY_FRIEND)
	{
		int entry = lines[*selected].index;

		if (entry < party_friends_scroll)
			party_friends_scroll = entry;
		if (entry >= party_friends_scroll + PARTY_FRIEND_LINES)
			party_friends_scroll = entry - PARTY_FRIEND_LINES + 1;
	}
	for (skip = 0; ; skip++)
	{
		highlighted = list_text(text, (int)sizeof(text), lines, count, *selected, skip, &headings);
		if (skip >= headings || !classic_panel() || classic_panel_fits(text))
			break;
	}
	vgxm_menu_set(text, highlighted);
}

static void show_code(void)
{
	char text[1024], letters[32], cursor[32];
	int index, column = 0;

	/* "A B C D - E F G H", and a caret under the cursor's */
	for (index = 0; index < P2P_CODE_LENGTH_TYPED; index++)
	{
		if (index == 4)
		{
			letters[column] = '-';
			cursor[column++] = ' ';
			letters[column] = ' ';
			cursor[column++] = ' ';
		}
		letters[column] = code_typed[index];
		cursor[column++] = index == code_cursor ? '^' : ' ';
		letters[column] = ' ';
		cursor[column++] = ' ';
	}
	letters[column] = cursor[column] = 0;
	snprintf(text, sizeof(text), "%s\n\n    %s\n    %s\n%s\n%s\n%s",
		code_for_join ? T("JOIN WITH A CODE\n(the host's Multiplayer tab shows it)") : T("JOIN WITH A CODE"), letters,
		cursor, strcmp(running_network, "online") ? T("Connection must be Online") : "",
		T("Up/down: letter  Left/right: move"), T("Cross: join  Circle: back"));
	vgxm_menu_set(text, 2);
}

/* a friend's code being typed: "A B C D - E F G H - J K L M", a caret under
the cursor's */
static void show_friend_code(void)
{
	char text[1024], letters[48], cursor[48];
	int index, column = 0;

	for (index = 0; index < P2P_FRIEND_CODE_LENGTH; index++)
	{
		if (index == 4 || index == 8)
		{
			letters[column] = '-';
			cursor[column++] = ' ';
			letters[column] = ' ';
			cursor[column++] = ' ';
		}
		letters[column] = friend_code_typed[index];
		cursor[column++] = index == friend_code_cursor ? '^' : ' ';
		letters[column] = ' ';
		cursor[column++] = ' ';
	}
	letters[column] = cursor[column] = 0;
	snprintf(text, sizeof(text), "%s\n\n    %s\n    %s\n%s\n%s\n%s", T("ADD A FRIEND\n(their Friends page shows their code)"),
		letters, cursor, strcmp(running_network, "online") ? T("Sent once the Connection is Online") : "",
		T("Up/down: letter  Left/right: move"), T("Cross: send  Circle: back"));
	vgxm_menu_set(text, 2);
}

/* Friend code backup's screen: its question, the work, the restore's
question, what came of it */
static void show_friend_backup(void)
{
	char text[1024], list[64];

	switch (friend_backup.step)
	{
	case FRIEND_BACKUP_LIST:
		snprintf(text, sizeof(text), "%s\n\n%s\n\n%s", T("BACK UP FRIEND CODE"),
			T("Put your friends list in the backup too?\nIt comes back with your code."),
			T("Cross: with the list   Square: code only\nCircle: cancel"));
		break;
	case FRIEND_BACKUP_WORKING:
		snprintf(text, sizeof(text), "%s\n\n%s", T("FRIEND CODE BACKUP"),
			p2p_friends_backup_text(P2P_FRIENDS_BACKUP_WORKING));
		break;
	case FRIEND_BACKUP_CONFIRM:
		if (friend_backup.friend_count < 0)
			snprintf(list, sizeof(list), "%s", T("the friend code alone"));
		else
			snprintf(list, sizeof(list), T("%d friends in it"), friend_backup.friend_count);
		snprintf(text, sizeof(text), T("RESTORE FRIEND CODE\n\nYour code %s\nbecomes %s\n(%s).\n"
			"Yours now is kept on the memory card.\nUse a code on one Vita at a time:\n"
			"friends see the one started last.\n\nCross: confirm   Circle: cancel"), friend_backup.current,
			friend_backup.code, list);
		break;
	case FRIEND_BACKUP_DONE:
		snprintf(text, sizeof(text), "%s\n\n%s\n\n%s", T("FRIEND CODE BACKUP"), friend_backup.said,
			T("Cross or Circle: back"));
		break;
	default:
		snprintf(text, sizeof(text), "%s\n\n%s\n\n%s", T("FRIEND CODE BACKUP"),
			T("Save your friend code (and friends list)\nto a file with a password, or restore one.\n"
			"Anyone with the file and its password\ncan be you: keep both safe."),
			T("Cross: back up   Square: restore\nCircle: back"));
		break;
	}
	vgxm_menu_set(text, -1);
}

/* the question before the leader removes a member (triangle) */
static void show_party_confirm(void)
{
	char text[512];

	snprintf(text, sizeof(text), T("REMOVE FROM PARTY\n\n%.20s  %s\n\nThey are told, and stay where they are.\n"
		"Cross: remove   O: back"), party_confirm_member.name, party_confirm_member.code);
	vgxm_menu_set(text, -1);
}

/* the question before removing a friend or blocking a player (triangle) */
static void show_friend_confirm(void)
{
	char text[1024];
	const struct p2p_friend_entry *entry = &friend_confirm_entry;

	if (friend_confirm_action == P2P_FRIENDS_REMOVE)
		snprintf(text, sizeof(text), T("REMOVE FRIEND\n\n%.16s  %s\n\nThey are told, and see you no more.\n"
			"Cross: remove   Square: block   O: keep"), entry->name, entry->code);
	else
		snprintf(text, sizeof(text), T("BLOCK\n\n%.16s  %s\n\nTheir requests and invites never reach you.\n"
			"Cross: block   O: back"), entry->name, entry->code);
	vgxm_menu_set(text, -1);
}

/* the public games as the server browser lists them (one line each: the
name, the players, the map, then [pw] for a password and PC for a Halo PC
map), the chosen one's Rules and Players lines below, in the overlay's own
font: no art */
static void show_browse(void)
{
	char text[3072];
	int length, index;
	char detail[64];

	length = snprintf(text, sizeof(text), "%s", T("PUBLIC GAMES"));
	if (strcmp(running_network, "online"))
		length += snprintf(text + length, sizeof(text) - length, "\n%s", T("Connection must be Online"));
	else if (!browse_count)
	{
		/* (why there is none: the brokers out of reach, or no public game:
		p2p_lobby_browse_status, in two lines of the overlay's width) */
		char line[96];
		int state = p2p_lobby_browse_status(line, sizeof(line));

		if (state == P2P_LOBBY_BROWSE_UNREACHABLE)
			length += snprintf(text + length, sizeof(text) - length, "\n%s",
				T("Can't reach the online game list:\ncheck your internet connection (Square: retry)"));
		else if (state == P2P_LOBBY_BROWSE_EMPTY)
			length += snprintf(text + length, sizeof(text) - length, "\n%.46s", line);
		else
		{
			p2p_status_shown(detail, sizeof(detail));
			length += snprintf(text + length, sizeof(text) - length, "\n");
			length += snprintf(text + length, sizeof(text) - length, T("Looking for games (%.24s)"), detail);
		}
	}
	/* (a game a line: its tag first (crossplay_guard.h: VITA, V+PC for a
	Vita's game PC players join too, VPC for this project's PC build's game,
	or the PC build's: OPENCE, CHUPA, DAMN, PC), its name, players of most, map, [pw] for a password, CE for
	a Halo PC (Custom Edition) map, SRV for a dedicated server's, EXP for a
	large PC game, COOP EXP for a PC co-op one: both asked about once) */
	for (index = 0; index < browse_count && length < (int)sizeof(text); index++)
	{
		const struct p2p_lobby_entry *entry = &browse_entries[index];
		/* (the PC build's game: VPC, VPC+PC) */
		const char *tag = entry->platform == CROSSPLAY_PLATFORM_VITA ? (entry->marks & CROSSPLAY_HOST_PC_BUILD ?
			(entry->crossplay ? "VPC+PC" : "VPC") : entry->crossplay ? "V+PC" : "VITA") :
			entry->platform == CROSSPLAY_PLATFORM_DAMNATION ? "DAMN" : crossplay_guard_tag(entry->platform);
		/* (the panel's 46 characters at most: the badges past them cut) */
		char row[47];
		int end;

		/* (a map not said: "-", as the game's browsers show it) */
		end = snprintf(row, sizeof(row), "%-6.6s %-12.12s %2d/%-2d %-10.10s%s%s%s%s", tag, entry->name, entry->players,
			entry->maximum, entry->map[0] ? entry->map : "-", entry->locked ? " [pw]" : "", entry->pc_map ? " CE" : "",
			entry->dedicated ? " SRV" : "", entry->platform != CROSSPLAY_PLATFORM_VITA && (entry->coop ||
			entry->experimental) ? " EXP" : "");
		if (end >= (int)sizeof(row))
			end = (int)sizeof(row) - 1;

		while (end > 0 && row[end - 1] == ' ')
			row[--end] = 0;
		length += snprintf(text + length, sizeof(text) - length, "\n%s", row);
	}
	/* (the chosen game's Rules and Players lines; one that could not be
	joined this run says so) */
	if (browse_selected < browse_count && length < (int)sizeof(text))
	{
		const struct p2p_lobby_entry *entry = &browse_entries[browse_selected];

		if (entry->rules[0])
			length += snprintf(text + length, sizeof(text) - length, "\n%s%.*s", entry->failed ? T("FAILED: ") : "",
				entry->failed ? 46 - lang_utf8_length(T("FAILED: ")) : 46, entry->rules);
		if (entry->players_line[0] && length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n%.46s", entry->players_line);
		/* (its tag in words, and a PC game's badges) */
		if (length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n%.46s",
				crossplay_guard_host_line(entry->platform, entry->marks, entry->crossplay));
		if (entry->platform != CROSSPLAY_PLATFORM_VITA && (entry->coop || entry->experimental) &&
			length < (int)sizeof(text))
			length += snprintf(text + length, sizeof(text) - length, "\n%.46s", entry->coop ?
				T("PC co-op: experimental on the Vita") : T("Experimental: over 16 players may lag"));
	}
	if (length < (int)sizeof(text))
		snprintf(text + length, sizeof(text) - length, "\n%s", T("Cross: join   Square: refresh   Circle: back"));
	vgxm_menu_set(text, browse_count ? browse_selected + 1 : 0);
}

static size_t format_box(char *formatted, size_t size, const char *title, const char *text, const char *footer);

/* the large PC game's question (crossplay_guard.h), in the overlay's lines */
static void show_large(void)
{
	char body[256], text[1024];

	int verdict = browse_large.coop ? CROSSPLAY_GUARD_COOP : CROSSPLAY_GUARD_LARGE;

	snprintf(body, sizeof(body), "%.20s  %d/%d  %.20s\n\n%s", browse_large.name, browse_large.players,
		browse_large.maximum, browse_large.map, crossplay_guard_question(verdict, browse_large.players));
	format_box(text, sizeof(text), crossplay_guard_title(verdict), body, T("\n\nCross: join anyway   Circle: back"));
	vgxm_menu_set(text, -1);
}

/* the map chosen on the Modded maps page, or NULL */
static struct map_entry *selected_map(void)
{
	struct line lines[MAXIMUM_LINES];
	int count;

	if (page != PAGE_MAPS)
		return NULL;
	count = page_lines(lines);
	if (page_selected[PAGE_MAPS] < count && lines[page_selected[PAGE_MAPS]].type == LINE_MAP)
		return &maps[lines[page_selected[PAGE_MAPS]].index];
	return NULL;
}

static void show_delete(void)
{
	char text[1024], size_name[16];
	const struct map_entry *map = selected_map();

	if (!map)
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	size_text(size_name, sizeof(size_name), map->size);
	snprintf(text, sizeof(text), T("DELETE MAP\n\n%.40s.%s  (%s)\nand its picture and description, if any.\n\n"
		"It cannot be undone.\nCross: delete   O: keep"), map->name, map->extension, size_name);
	vgxm_menu_set(text, -1);
}

/* (the Forge page's File Share) a map variant deleted, asked first */
static void show_forge_delete(void)
{
	char text[1024];
	const struct forge_store_entry *entry = forge_delete_index >= 0 && forge_delete_index < forge_entry_count ?
		&forge_entries[forge_delete_index] : NULL;

	if (!entry)
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	snprintf(text, sizeof(text), T("DELETE MAP VARIANT\n\n%.31s  (%.20s)\n\nIt cannot be undone.\nCross: delete   O: keep"),
		entry->damaged ? entry->file_name : entry->title, forge_map_title(entry->map_name));
	vgxm_menu_set(text, -1);
}

/* (the Forge page, in a Forge game) every placed character cleared, asked
first */
static void show_forge_clear(void)
{
	char text[512];
	int alive = 0, dead = 0;

	if (!forge_panel_ai_counts(&alive, &dead))
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	snprintf(text, sizeof(text), T("CLEAR ALL AI\n\nEvery AI you placed, %d alive and %d dead,\nout of the layout: they don't come back\n"
		"next round.\n\nCross: clear   O: keep"), alive, dead);
	vgxm_menu_set(text, -1);
}

static void forge_clear_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if (pressed & VITA_BUTTON_CROSS)
	{
		forge_panel_clear_ai(1);
		set_notice("%s", T("All AI cleared"));
		screen = SCREEN_LIST;
	}
}

static void forge_delete_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if ((pressed & VITA_BUTTON_CROSS) && forge_delete_index >= 0 && forge_delete_index < forge_entry_count)
	{
		const struct forge_store_entry *entry = &forge_entries[forge_delete_index];

		if (forge_store_delete(entry->map_name, entry->file_name))
			set_notice(T("Deleted \"%s\"."), entry->damaged ? entry->file_name : entry->title);
		else
			set_notice(T("\"%s\" couldn't be deleted."), entry->damaged ? entry->file_name : entry->title);
		forge_delete_index = -1;
		forge_variant = 0;
		forge_scan();
		screen = SCREEN_LIST;
	}
}

/* the entry chosen on the Mod manager page, or NULL */
static struct halo_mod_entry *selected_mod(void)
{
	struct line lines[MAXIMUM_LINES];
	int count;

	if (page != PAGE_MODS || mods_state != 2)
		return NULL;
	count = page_lines(lines);
	if (page_selected[PAGE_MODS] < count && lines[page_selected[PAGE_MODS]].type == LINE_MOD)
		return &mods_view.entries[lines[page_selected[PAGE_MODS]].index];
	return NULL;
}

/* the chosen entry's facts (cross on its line) */
static void show_mod_details(void)
{
	char body[768], text[1280], size_name[24], modes[64];
	const struct halo_mod_entry *entry = selected_mod();
	int length = 0;

	if (!entry)
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	size_text(size_name, sizeof(size_name), (unsigned long long)entry->kilobytes << 10);
	mod_modes_text(modes, sizeof(modes), entry->modes);
	length += snprintf(body + length, sizeof(body) - length, "%s: %s\n", T("Type"), mod_kind_name(entry->kind));
	length += snprintf(body + length, sizeof(body) - length, "%s: %s%.47s\n", T("File"),
		entry->kind == HALO_MOD_KIND_MCC_MOD ? "mods/" : "maps/", entry->file);
	length += snprintf(body + length, sizeof(body) - length, "%s: %s\n", T("Size"), size_name);
	length += snprintf(body + length, sizeof(body) - length, "%s: %s\n", T("From"),
		entry->source == HALO_MOD_SOURCE_MAP_SHARE ? T("Received through map sharing") : T("Copied in by hand"));
	length += snprintf(body + length, sizeof(body) - length, "%s: %s\n", T("Works in"), modes);
	if (entry->kind == HALO_MOD_KIND_MCC_MOD)
		length += snprintf(body + length, sizeof(body) - length, T("Maps: %d, %d of them play\n"), entry->maps,
			entry->maps_valid);
	length += snprintf(body + length, sizeof(body) - length, "%s: %s%s%s\n", T("Check"),
		entry->valid ? T("Passed") : T("Failed"), entry->why[0] ? " - " : "", entry->why);
	length += snprintf(body + length, sizeof(body) - length, "%s: %s\n", T("Needs"),
		entry->needs[0] ? entry->needs : T("Nothing more"));
	snprintf(body + length, sizeof(body) - length, "%s: %s", T("Status"),
		entry->enabled ? T("On: in the map lists") : T("Off: kept, but in no map list"));
	format_box(text, sizeof(text), entry->kind == HALO_MOD_KIND_MCC_MOD ? entry->title : entry->file, body,
		T("\n\nLeft/right: on/off   Circle: back"));
	vgxm_menu_set(text, -1);
}

/* the delete's question (square on its line) */
static void show_mod_delete(void)
{
	char text[1024], size_name[24];
	const struct halo_mod_entry *entry = selected_mod();

	if (!entry)
	{
		screen = SCREEN_LIST;
		show_list();
		return;
	}
	size_text(size_name, sizeof(size_name), (unsigned long long)entry->kilobytes << 10);
	if (entry->kind == HALO_MOD_KIND_MCC_MOD)
		snprintf(text, sizeof(text), T("DELETE MOD\n\n%.40s  (%s)\nand everything in its folder.\n\n"
			"It cannot be undone.\nCross: delete   O: keep"), entry->file, size_name);
	else
		snprintf(text, sizeof(text), T("DELETE MAP\n\n%.40s  (%s)\nand its picture and description, if any.\n\n"
			"It cannot be undone.\nCross: delete   O: keep"), entry->file, size_name);
	vgxm_menu_set(text, -1);
}

/* why Host or Join cannot open System Link now (NULL: it can) */

static const char *guide_blocker(void)
{
	int state = multiplayer_state();

	if (strcmp(chosen_network(), running_network))
		return T("Restart the game first: Connection changed");
	if (state == SYSTEM_LINK_STATE_PLAYING)
		return T("Leave the level first: Start, then Quit");
	if (state == SYSTEM_LINK_STATE_IN_GAME)
		return T("In a game: Start, then Quit, to leave it");
	if (state == SYSTEM_LINK_STATE_HOSTING)
		return T("Already hosting: the game's lobby is open");
	if (state == SYSTEM_LINK_STATE_JOINING || state == SYSTEM_LINK_STATE_LOBBY)
		return T("Already in a lobby: B there leaves it");
	if (strcmp(running_network, "adhoc") && state != SYSTEM_LINK_STATE_STARTING &&
		!halo_multiplayer_status[SYSTEM_LINK_STATUS_ADDRESS])
		return T("No Wi-Fi: connect the Vita to a network");
	return NULL;
}

/* the game's answer, as the guide says it */
static const char *guide_answer_text(int answer)
{
	switch (answer)
	{
	case SYSTEM_LINK_ANSWER_IN_PLAY:
		return T("Leave the level first: Start, then Quit");
	case SYSTEM_LINK_ANSWER_IN_LOBBY:
		return T("Already in a lobby: B there leaves it");
	case SYSTEM_LINK_ANSWER_NO_NETWORK:
		return strcmp(running_network, "adhoc") ? T("No Wi-Fi: connect the Vita to a network") :
			T("Not in the ad hoc group: join it first");
	default:
		return T("The game could not open it (see its message)");
	}
}

/* Host a game, Join a game: the steps, as the game's menus name the
buttons (the Xbox's, or with the Button icons PlayStation the Vita's), then
cross opens the game's System Link screen */
static void show_guide(void)
{
	char text[3072], line[128];
	int length, step = 1, host = guide_action == ACTION_HOST;
	int adhoc = !strcmp(running_network, "adhoc"), online = !strcmp(running_network, "online");
	const char *blocker = guide_blocker();

	vita_line(line, sizeof(line));
	length = snprintf(text, sizeof(text), "%s\n\n%s", host ? T("HOST A GAME") : T("JOIN A GAME"), line);
	if (adhoc && vita_adhoc_state(NULL, 0) != 2)
	{
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length, T("%d The system's dialog joins ad hoc room %d"), step++,
			choice_of("HALO_ADHOC_ROOM") + 1);
	}
	length += snprintf(text + length, sizeof(text) - length, "\n");
	length += snprintf(text + length, sizeof(text) - length,
		T("%d The game's System Link screen opens\n%d %s to join if asked, %s on a profile,%s%s again"), step, step + 1,
		menu_button('A'), menu_button('A'), playstation_terms() ? "\n  " : " ", menu_button('A'));
	step += 2;
	if (host)
	{
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length,
			T("%d SYSTEM LINK GAMES: %s creates a game\n%d %s on a map, %s on a game type"), step, menu_button('Y'),
			step + 1, menu_button('A'), menu_button('A'));
		step += 2;
		/* (the Custom Edition maps are in the map list with PC maps on) */
		if (!choice_of("HALO_CUSTOM_EDITION"))
			length += snprintf(text + length, sizeof(text) - length, "\n%s", T("  (PC maps: Modded maps, PC maps On)"));
		/* (the game as others see it: the Play page's rows) */
		length += snprintf(text + length, sizeof(text) - length, "\n");
		if (play_text_named("HALO_NET_LOBBY_NAME")->value[0])
			length += snprintf(text + length, sizeof(text) - length, T("  As \"%s\", %s players at most"),
				play_text_named("HALO_NET_LOBBY_NAME")->value, setting_named("HALO_NET_MAX_PLAYERS")->names[choice_of(
				"HALO_NET_MAX_PLAYERS")]);
		else
			length += snprintf(text + length, sizeof(text) - length, T("  As your profile's name, %s players at most"),
				setting_named("HALO_NET_MAX_PLAYERS")->names[choice_of("HALO_NET_MAX_PLAYERS")]);
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length,
			T("%d Wait in the lobby for the others to\n  join; %s there starts the game sooner"), step, menu_button('A'));
	}
	else
	{
		/* (where the games listed are: a whole sentence each, for the
		translations) */
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length, adhoc ?
			T("%d SYSTEM LINK GAMES: %s on the host's game\n  (the games in the room show there)") : online ?
			T("%d SYSTEM LINK GAMES: %s on the host's game\n  (the games you are connected to show there)") :
			T("%d SYSTEM LINK GAMES: %s on the host's game\n  (the games on this network show there)"), step,
			menu_button('A'));
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length,
			T("%d Wait in the lobby for the host to start\n  A map you lack comes from the host"), step + 1);
	}
	/* (online: how the code's lookup goes; System Link lists the host's
	game once connected) */
	if (online && !host)
	{
		char detail[160], english[96];

		/* (shown in the language chosen; whether it is connected, by the
		English, p2p_status's) */
		p2p_status(english, sizeof(english));
		p2p_status_shown(detail, sizeof(detail));
		/* (its first clause: "connected to the host", "code ABCD-EFGH
		found", "no game has code ABCD-EFGH", in the line's 46) */
		detail[strcspn(detail, ":;(")] = 0;
		while (detail[0] && detail[strlen(detail) - 1] == ' ')
			detail[strlen(detail) - 1] = 0;
		length += snprintf(text + length, sizeof(text) - length, "\n");
		length += snprintf(text + length, sizeof(text) - length, T("Online: %.38s"), detail);
		/* (a public game's password, when the listing says it was wrong) */
		if (play_join_state() == -1)
			length += snprintf(text + length, sizeof(text) - length, "\n%s", T("  The password was wrong: back, and again"));
		else if (play_join_state() == -2)
			length += snprintf(text + length, sizeof(text) - length, "\n%s", T("  The game is gone from the public games"));
		else if (!strstr(english, "connected to the host"))
			length += snprintf(text + length, sizeof(text) - length, "\n%s", T("  Wait for \"connected to the host\""));
	}
	else if (online)
		length += snprintf(text + length, sizeof(text) - length, "\n%s",
			T("Online: your code shows on the Play page\n  once the game is made; tell it to the others"));
	/* (the Xbox's names, and which Vita button each is: the game's own
	menus show the Xbox's icons) */
	if (!playstation_terms())
		length += snprintf(text + length, sizeof(text) - length, "\n%s", T("Menus: A Cross, B Circle, X Square, Y Triangle"));
	if (guide_waiting)
		length += snprintf(text + length, sizeof(text) - length, "\n\n%s\n%s", T("Opening System Link..."),
			T("Circle: back"));
	else if (guide_problem[0] || blocker)
		length += snprintf(text + length, sizeof(text) - length, "\n\n!%s\n%s",
			guide_problem[0] ? guide_problem : blocker, T("Circle: back"));
	else
		length += snprintf(text + length, sizeof(text) - length, "\n\n%s", T("Cross: open System Link   Circle: back"));
	vgxm_menu_set(text, -1);
}

static void show(void)
{
	last_shown = now_us();
	if (screen == SCREEN_CODE)
		show_code();
	else if (screen == SCREEN_BROWSE)
		show_browse();
	else if (screen == SCREEN_DELETE)
		show_delete();
	else if (screen == SCREEN_GUIDE)
		show_guide();
	else if (screen == SCREEN_LARGE)
		show_large();
	else if (screen == SCREEN_FORGE_DELETE)
		show_forge_delete();
	else if (screen == SCREEN_FORGE_CLEAR)
		show_forge_clear();
	else if (screen == SCREEN_MOD_DETAILS)
		show_mod_details();
	else if (screen == SCREEN_MOD_DELETE)
		show_mod_delete();
	else if (screen == SCREEN_FRIEND_CODE)
		show_friend_code();
	else if (screen == SCREEN_FRIEND_CONFIRM)
		show_friend_confirm();
	else if (screen == SCREEN_FRIEND_BACKUP)
		show_friend_backup();
	else if (screen == SCREEN_PARTY_CONFIRM)
		show_party_confirm();
	else if (screen == SCREEN_BAN_CONFIRM)
		show_ban_confirm();
	else
		show_list();
}

static void change(struct setting *setting, int step)
{
	int choice = setting->choice + step;

	/* (past the choices this build does not offer) */
	while (choice >= 0 && choice < setting->count && choice_disabled(setting, choice))
		choice += step;
	if (choice < 0 || choice >= setting->count)
		return;
	/* (Custom is not chosen: the Profile row reads it when a row it sets
	is changed) */
	if (strcmp(setting->variable, "HALO_PROFILE") == 0 && choice == PROFILE_CUSTOM)
		return;
	/* (in a party: Online alone - a party plays online, as the Halo 3
	lobbies' NETWORK; the owner, Oct 21 2026) */
	if (strcmp(setting->variable, "HALO_VITA_NETWORK") == 0 && strcmp(setting->values[choice], "online") != 0 &&
		p2p_party_in_party())
	{
		set_notice("%s", T("In a party: Online only (leave the party first)"));
		return;
	}
	setting->choice = choice;
	apply_value(setting);
	if (strcmp(setting->variable, "HALO_PLAYER_MODEL") == 0)
		player_models_panel_choose(choice);
	if (strcmp(setting->variable, "HALO_ARMOR_COLOR") == 0)
		player_cosmetics_panel_choose_color(choice);
	if (strcmp(setting->variable, "HALO_VISOR_COLOR") == 0)
		player_cosmetics_panel_choose_visor(choice);
	/* (a row that applies after a restart says so on its help line from
	now on, and every line does for a few seconds) */
	if (setting->restart)
	{
		restart_pending = 1;
		restart_offered = 0;
		set_notice("%s", T("Restart the game for this change"));
	}
	if (strcmp(setting->variable, "HALO_PROFILE") == 0)
	{
		/* (a profile sets its rows; Custom leaves them as they are) */
		if (apply_profile(choice))
		{
			restart_pending = 1;
			restart_offered = 0;
			set_notice("%s", T("Restart the game for this change"));
		}
	}
	else
	{
		/* (a row a profile sets, changed: the profile it now matches, or
		Custom) */
		struct setting *profile = setting_named("HALO_PROFILE");

		profile->choice = matching_profile();
		apply_value(profile);
	}
	if (strcmp(setting->variable, "HALO_LANGUAGE") == 0)
		language_apply();
	if (strcmp(setting->variable, "XV_FPS") == 0)
		vgxm_overlay_enable(atoi(setting->values[choice]));
	if (strcmp(setting->variable, "HALO_UPSCALE_FILTER") == 0)
		vgxm_upscale_filter_set(choice);
	/* (listed or not takes effect at once, also while hosting) */
	if (strcmp(setting->variable, "HALO_NET_HOST_PUBLIC") == 0)
		play_listing_set_public(choice);
	if (strcmp(setting->variable, "HALO_NET_COOP_PUBLIC") == 0)
		play_listing_set_coop_public(choice);
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	if (setting->dev)
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: dev switch %s=%s (settings panel)", setting->variable,
			setting->values[choice][0] ? setting->values[choice] : "(default)");
		vita_host_log(line);
	}
}

/* a row set as the panel sets it, its profile worked out again (applied,
saved); 0 if there is no such row. The game's Server Setup sets the hosted
game's settings so (port/linux/game/menu_functions.c), and (debug)
HALO_TEST_COMMANDS "@set VARIABLE value" (main.c) */
int vita_settings_set(const char *variable, const char *value)
{
	struct setting *setting = setting_named(variable);

	/* (the game's MOD MANAGER turned a map or a mod off or on, or deleted
	one: the lists, kept as this panel keeps them) */
	if (!strcmp(variable, "HALO_MAPS_DISABLED") || !strcmp(variable, "HALO_MODS_DISABLED"))
	{
		int maps_list = !strcmp(variable, "HALO_MAPS_DISABLED");
		char *list = maps_list ? maps_disabled : mods_disabled;
		char line[1200];

		list_sanitize(list, sizeof(maps_disabled), value);
		if (list[0])
			setenv(variable, list, 1);
		else
			unsetenv(variable);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
		save();
		snprintf(line, sizeof(line), "settings: %s=%s (the game)", variable, list);
		vita_host_log(line);
		return 1;
	}

	/* (a Play page text: as typed) */
	if (setting && setting->kind == KIND_TEXT)
	{
		char line[96];

		play_text_set(play_text_named(variable), value, 1);
		__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
		save();
		/* (a password is never written to halo.log) */
		if (!strcmp(variable, "HALO_NET_LOBBY_PASSWORD"))
			snprintf(line, sizeof(line), "settings: password %s (the game)",
				play_text_named(variable)->value[0] ? "set" : "none");
		else
			snprintf(line, sizeof(line), "settings: %s=%s (the game)", variable, play_text_named(variable)->value);
		vita_host_log(line);
		return 1;
	}
	if (!setting || setting->kind != KIND_CHOICE)
		return 0;
	change(setting, find_choice(setting, value) - setting->choice);
	{
		char line[128];

		snprintf(line, sizeof(line), "settings: %s=%s (the game)", variable, setting->values[setting->choice]);
		vita_host_log(line);
	}
	return 1;
}

/* the name of what a guide's action does, for a message over the game */
static const char *guide_title(int action)
{
	return action == ACTION_HOST ? T("Host a game") : action == ACTION_CAMPAIGN ? T("Host co-op campaign") :
		T("Join a game");
}

/* asks the game for its System Link screen, or Campaign's (the answer:
guide_poll) */
static void guide_request(void)
{
	char line[96];

	guide_problem[0] = 0;
	halo_system_link_answer = SYSTEM_LINK_ANSWER_NONE;
	__atomic_store_n(&halo_system_link_request, guide_action == ACTION_HOST ? SYSTEM_LINK_REQUEST_HOST :
		guide_action == ACTION_CAMPAIGN ? SYSTEM_LINK_REQUEST_CAMPAIGN : SYSTEM_LINK_REQUEST_JOIN, __ATOMIC_RELEASE);
	guide_waiting = 1;
	guide_sent = now_us();
	if (guide_action == ACTION_CAMPAIGN)
		snprintf(line, sizeof(line), "settings: host co-op campaign: asked the game for its Campaign screen (%s)",
			running_network);
	else
		snprintf(line, sizeof(line), "settings: %s a game: asked the game for its System Link screen (%s)",
			guide_action == ACTION_HOST ? "host" : "join", running_network);
	vita_host_log(line);
}

static void close_panel(void)
{
	if (screen == SCREEN_BROWSE || screen == SCREEN_LARGE)
		p2p_lobby_browse(0);
	/* (Friend code backup left: a restore opened let go, nothing typed kept;
	work going on goes on) */
	if (screen == SCREEN_FRIEND_BACKUP)
	{
		if (friend_backup.step == FRIEND_BACKUP_CONFIRM)
			p2p_friends_backup_cancel();
		memset(friend_backup.password, 0, sizeof(friend_backup.password));
		friend_backup.step = FRIEND_BACKUP_MENU;
		friend_backup.ime_next = 0;
	}
	panel_open = 0;
	screen = SCREEN_LIST;
	/* (it opens again on the tab's own page) */
	page = tab;
	held_after_close = previous_buttons;
	vgxm_menu_set(NULL, 0);
	/* a change that applies after a restart (Connection, Crossplay, the
	sound voices...), the panel closed at the game's main menu: offer the
	restart now, Cross restarting the game at once, Circle leaving it for
	later (not asked again until another such change); in a game, the help
	line's "Restart the game for this change" as before */
	if (restart_pending && !restart_offered && halo_main_menu_idle())
	{
		restart_offered = 2;
		vita_settings_question(T("Halo CE"), T("Your change applies after a restart. Restart the game now?"));
	}
}

/* Publish from the game/event thread; only the input thread writes the menu. */
static pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;
static char pending_message[3072];
static int message_pending, message_visible;
/* map sharing's question (port/linux/game/map_share.c): cross yes, circle
no; -1 until answered */
static char pending_question[3072];
static int question_pending, question_visible, question_withdrawn;
static volatile int question_result = -1;
/* map sharing's progress: redrawn when it changes; circle cancels */
static char pending_progress[3072];
static int progress_changed, progress_visible, progress_hide;
static volatile int progress_cancel;

/* the characters of the `bytes` first bytes of UTF-8 text (a translated
text's accented letters are two bytes, one character) */
static int utf8_columns(const char *text, size_t bytes)
{
    int count = 0;
    size_t index;

    for (index = 0; index < bytes && text[index]; index++)
        if (((unsigned char)text[index] & 0xC0) != 0x80)
            count++;
    return count;
}

/* `title`, `text` and `footer` in lines of the overlay's width (46
characters, not bytes: UTF-8's continuation bytes take no column, and a
line never ends inside a character) */
static size_t format_box(char *formatted, size_t size, const char *title, const char *text, const char *footer)
{
    size_t used = 0;
    int column = 0;
    const char *parts[] = {title, "\n\n", text, footer};
    for (int part = 0; part < 4; ++part)
    {
        const char *cursor = parts[part];
        while (*cursor && used < size - 1)
        {
            if (*cursor != '\n')
            {
                size_t word = strcspn(cursor, " \n");
                if (column && word && (cursor == parts[part] || cursor[-1] == ' ') &&
                    column + utf8_columns(cursor, word) > 46)
                {
                    formatted[used++] = '\n';
                    column = 0;
                    continue;
                }
            }
            char ch = *cursor++;
            int continuation = ((unsigned char)ch & 0xC0) == 0x80;
            if (column >= 46 && ch != '\n' && !continuation)
            {
                formatted[used++] = '\n';
                column = 0;
                if (used >= size - 1) break;
                /* (a space the line ended at does not start the next) */
                if (ch == ' ') continue;
            }
            formatted[used++] = ch;
            column = ch == '\n' ? 0 : column + !continuation;
        }
    }
    formatted[used] = 0;
    return used;
}

void vita_settings_message(const char *title, const char *text)
{
    char formatted[3072];
    size_t used = format_box(formatted, sizeof(formatted), title, text, T("\n\nCross / Circle: close"));

    pthread_mutex_lock(&message_lock);
    memcpy(pending_message, formatted, used + 1);
    message_pending = 1;
    pthread_mutex_unlock(&message_lock);
}

/* a yes or no question over the game (NULL withdraws it) */
void vita_settings_question(const char *title, const char *text)
{
    char formatted[3072];
    size_t used = text ? format_box(formatted, sizeof(formatted), title ? title : "", text,
        T("\n\nCross: yes     Circle: no")) : 0;

    pthread_mutex_lock(&message_lock);
    question_result = -1;
    if (text)
    {
        memcpy(pending_question, formatted, used + 1);
        question_pending = 1;
        question_withdrawn = 0;
    }
    else
    {
        question_pending = 0;
        question_withdrawn = 1;
    }
    pthread_mutex_unlock(&message_lock);
}

int vita_settings_question_answer(void)
{
    return question_result;
}

/* a progress line over the game (NULL hides it), which circle cancels */
void vita_settings_progress(const char *title, const char *text)
{
    char formatted[3072];
    size_t used = text ? format_box(formatted, sizeof(formatted), title ? title : "", text,
        T("\n\nCircle: cancel")) : 0;

    pthread_mutex_lock(&message_lock);
    if (text)
    {
        if (!progress_visible && !progress_changed)
            progress_cancel = 0;
        memcpy(pending_progress, formatted, used + 1);
        progress_changed = 1;
        progress_hide = 0;
    }
    else
    {
        progress_changed = 0;
        progress_hide = 1;
    }
    pthread_mutex_unlock(&message_lock);
}

int vita_settings_progress_cancelled(void)
{
    return progress_cancel;
}

/* (the input thread) the question and the progress line, below a message:
nonzero when one holds the buttons */
static int question_progress_input(unsigned long pressed)
{
    int shown_now = 0;

    pthread_mutex_lock(&message_lock);
    if (question_withdrawn)
    {
        question_withdrawn = 0;
        if (question_visible)
        {
            question_visible = 0;
            vgxm_menu_set(NULL, 0);
        }
    }
    if (question_pending)
    {
        vgxm_menu_set(pending_question, -1);
        question_pending = 0;
        question_visible = 1;
        shown_now = 1;
        panel_open = 0;
    }
    if (progress_hide)
    {
        progress_hide = 0;
        if (progress_visible && !question_visible)
            vgxm_menu_set(NULL, 0);
        progress_visible = 0;
    }
    if (progress_changed && !question_visible)
    {
        vgxm_menu_set(pending_progress, -1);
        progress_changed = 0;
        progress_visible = 1;
        panel_open = 0;
    }
    pthread_mutex_unlock(&message_lock);

    if (question_visible)
    {
        if (!shown_now && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE)))
        {
            question_result = (pressed & VITA_BUTTON_CROSS) ? 1 : 0;
            question_visible = 0;
            vgxm_menu_set(NULL, 0);
            if (restart_offered == 2)
            {
                restart_offered = 1;
                if (question_result == 1)
                {
                    char line[96];

                    vita_host_log("settings: restarting the game for the changes that need it");
                    /* (it does not come back unless it failed) */
                    snprintf(line, sizeof(line), "settings: the restart failed (0x%08X)",
                        (unsigned int)vita_host_restart_game());
                    vita_host_log(line);
                    vita_settings_message(T("Halo CE"),
                        T("The game could not restart itself: close it and start it again for the change."));
                }
            }
        }
        return 1;
    }
    if (progress_visible)
    {
        if (pressed & VITA_BUTTON_CIRCLE)
            progress_cancel = 1;
        return 1;
    }
    return 0;
}

/* the Controls tab's rows as shipped, its pages' too (but the gyro's
rows, Show dev settings and the Update channel, which are no controls): the
touch zones Off, the buttons in Xita's layout, look and crouch as they were */
static void reset_controls(void)
{
	int index;

	for (index = 0; index < SETTING_COUNT; index++)
	{
		struct setting *setting = &settings[index];

		if (pages[setting->page].tab != TAB_CONTROLS || setting->kind != KIND_CHOICE ||
			strcmp(setting->variable, "HALO_DEV_SETTINGS") == 0 || strncmp(setting->variable, "HALO_GYRO", 9) == 0 ||
			strcmp(setting->variable, "HALO_BUTTON_ICONS") == 0 || strcmp(setting->variable, "HALO_UPDATE_CHANNEL") == 0)
			continue;
		setting->choice = shipped_choice[index];
		apply_value(setting);
	}
	__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
	save();
	set_notice("%s", T("Controls as shipped"));
	vita_host_log("settings: controls reset (settings panel)");
}

static void act(const struct setting *setting)
{
	switch (setting->action)
	{
	/* (in the background: the answer shows under the version as it comes) */
	case ACTION_UPDATE_CHECK:
		/* port: an update found: the press downloads it, or says where it
		is (vita_update.c); else a look for one */
		if (vita_update_running())
			break;
		if (vita_update_start() < 0)
			update_check_request();
		break;
	/* (online: a game joined by its code, or picked from the public games,
	then Join's steps with how the lookup goes, as Join a game was in 1.0.3) */
	case ACTION_JOIN_CODE:
		guide_action = ACTION_JOIN;
		guide_problem[0] = 0;
		code_for_join = 1;
		screen = guide_blocker() ? SCREEN_GUIDE : SCREEN_CODE;
		break;
	/* (friends: their code typed, sent once online) */
	case ACTION_FRIEND_ADD:
		screen = SCREEN_FRIEND_CODE;
		break;
	case ACTION_FRIEND_BACKUP:
		memset(friend_backup.password, 0, sizeof(friend_backup.password));
		friend_backup.step = FRIEND_BACKUP_MENU;
		friend_backup.ime_next = 0;
		screen = SCREEN_FRIEND_BACKUP;
		break;
	case ACTION_PARTY_MESSAGE:
		if (p2p_party_status(NULL, 0) < 2)
			set_notice("%s", p2p_party_outcome_text(P2P_PARTY_NOT_IN_PARTY));
		else if (vita_ime_open(T("Party chat"), "", P2P_PARTY_CHAT_LENGTH, 0) != 0)
			set_notice("%s", T("The keyboard did not open"));
		else
		{
			party_message_pending = 1;
			vgxm_menu_set(NULL, 0);
		}
		break;
	case ACTION_ADD_BOT:
	case ACTION_DROP_BOT:
		if (!__atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE))
		{
			host_player_asked = setting->action == ACTION_ADD_BOT ? HALO_HOST_PLAYER_ADD_BOT : HALO_HOST_PLAYER_DROP_BOT;
			__atomic_store_n(&halo_host_player_request, host_player_asked << 8, __ATOMIC_RELEASE);
			vita_host_log(setting->action == ACTION_ADD_BOT ? "settings: players: a bot added" : "settings: players: a bot dropped");
		}
		break;
	case ACTION_CLEAR_BANS:
		if (!__atomic_load_n(&halo_host_player_request, __ATOMIC_ACQUIRE))
		{
			host_player_asked = HALO_HOST_PLAYER_CLEAR_BANS;
			__atomic_store_n(&halo_host_player_request, HALO_HOST_PLAYER_CLEAR_BANS << 8, __ATOMIC_RELEASE);
			vita_host_log("settings: players: the session's bans cleared");
		}
		break;
	case ACTION_PARTY_LEAVE:
	{
		int outcome = p2p_party_leave();

		set_notice("%s", outcome == P2P_PARTY_DONE ? T("You left the party") : p2p_party_outcome_text(outcome));
		vita_host_log(outcome == P2P_PARTY_DONE ? "settings: party: left (settings panel)" :
			"settings: party: leave refused (not in a party)");
		party_refresh(1);
		break;
	}
	case ACTION_BROWSE:
		guide_action = ACTION_JOIN;
		guide_problem[0] = 0;
		code_for_join = 1;
		if (guide_blocker())
		{
			screen = SCREEN_GUIDE;
			break;
		}
		screen = SCREEN_BROWSE;
		browse_count = browse_selected = 0;
		p2p_lobby_browse(1);
		break;
	/* (ad hoc: the room's group joined through the system's dialog, the
	panel closed meanwhile; the game's System Link screen then finds the
	others) */
	case ACTION_ADHOC_JOIN:
		if (strcmp(running_network, "adhoc"))
			set_notice("%s", T("First the game switches to Ad hoc, at its menus"));
		else if (vita_adhoc_state(NULL, 0) == 2)
			set_notice(T("In room %d's group already"), choice_of("HALO_ADHOC_ROOM") + 1);
		else
		{
			close_panel();
			vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
			vita_host_log("settings: ad hoc: joining the room's group (settings panel)");
		}
		break;
	case ACTION_SAVE_REPORT:
		report_start();
		break;
	case ACTION_RESET_CONTROLS:
		reset_controls();
		break;
	case ACTION_PAGE:
		page = setting->opens;
		if (page == PAGE_MAPS)
			maps_scan();
		if (page == PAGE_FORGE)
			forge_scan();
		/* (the game looks: the page says so until it answers) */
		if (page == PAGE_MODS)
			mods_request_scan();
		break;
	/* (its progress line then takes the screen) */
	case ACTION_CE_EXTRACT:
		if (vita_ce_installer_start() == 0)
			set_notice(T("Taking them from %.28s"), maps_installer);
		else
			set_notice("%s", T("No installer, or the files are there"));
		maps_scan();
		break;
	case ACTION_HOST:
	case ACTION_JOIN:
		guide_action = setting->action;
		guide_problem[0] = 0;
		code_for_join = 0;
		screen = SCREEN_GUIDE;
		break;
	/* (the Campaign screen at once: Y on its difficulty hosts co-op; with
	Ad hoc, the room's group joined first, guide_input's way) */
	case ACTION_CAMPAIGN:
		guide_action = ACTION_CAMPAIGN;
		guide_problem[0] = 0;
		if (guide_blocker())
		{
			set_notice("%s", guide_blocker());
			break;
		}
		if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2)
		{
			adhoc_pending = ACTION_CAMPAIGN;
			close_panel();
			vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
			return;
		}
		guide_request();
		break;
	}
}

/* (every frame) the game's answer to guide_request: opened, the panel
closes; not, the guide says why (a message over the game when the panel
is closed: the ad hoc dialog came first). The game answers within a
frame or two: one that has not in 3 s (busy loading) is asked no more */
static void guide_poll(unsigned long long now)
{
	const char *problem = NULL;

	if (!guide_waiting)
		return;
	if (__atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE) == SYSTEM_LINK_REQUEST_NONE)
	{
		int answer = halo_system_link_answer;

		guide_waiting = 0;
		if (answer == SYSTEM_LINK_ANSWER_OPENED)
		{
			if (panel_open)
				close_panel();
			return;
		}
		problem = guide_answer_text(answer);
	}
	else if (now - guide_sent > 3000000ULL)
	{
		int request = __atomic_load_n(&halo_system_link_request, __ATOMIC_ACQUIRE);

		/* (unless the game took it meanwhile: then its answer, next frame) */
		if (request == SYSTEM_LINK_REQUEST_NONE || !__atomic_compare_exchange_n(&halo_system_link_request, &request,
			SYSTEM_LINK_REQUEST_NONE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return;
		guide_waiting = 0;
		problem = T("The game did not answer: try again");
	}
	else
		return;
	snprintf(guide_problem, sizeof(guide_problem), "%s", problem);
	/* (the guide redrawn at once) */
	last_shown = 0;
	/* (Host co-op campaign has no screen of its own: the help line says it) */
	if (panel_open && guide_action == ACTION_CAMPAIGN)
		set_notice("%s", problem);
	else if (!panel_open || screen != SCREEN_GUIDE)
		vita_settings_message(guide_title(guide_action), problem);
}

/* the guide's buttons: cross opens System Link (with Ad hoc, after the
system's dialog has joined the room's group), circle goes back */
static void guide_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		/* (an answer still to come closes nothing: it is kept, unshown) */
		screen = SCREEN_LIST;
		guide_problem[0] = 0;
		return;
	}
	if (!(pressed & VITA_BUTTON_CROSS) || guide_waiting)
		return;
	guide_problem[0] = 0;
	if (guide_blocker())
		return;
	if (!strcmp(running_network, "adhoc") && vita_adhoc_state(NULL, 0) != 2)
	{
		/* (the panel closes: the system's dialog takes the screen) */
		adhoc_pending = guide_action;
		close_panel();
		vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
		return;
	}
	guide_request();
}

/* the code screen's buttons */
static void code_input(unsigned long pressed, unsigned long buttons, unsigned long long now)
{
	static const char alphabet[] = P2P_CODE_ALPHABET;
	int step = 0;

	if (pressed & VITA_BUTTON_CIRCLE)
	{
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_CROSS)
	{
		char code[P2P_CODE_SIZE];

		snprintf(code, sizeof(code), "%.4s-%.4s", code_typed, code_typed + 4);
		if (strcmp(running_network, "online"))
			set_notice("%s", T("Connection must be Online"));
		else
		{
			p2p_join_code(code);
			set_notice(T("Looking up %s..."), code);
		}
		screen = code_for_join && !strcmp(running_network, "online") ? SCREEN_GUIDE : SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_LEFT)
		code_cursor = (code_cursor + P2P_CODE_LENGTH_TYPED - 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_RIGHT)
		code_cursor = (code_cursor + 1) % P2P_CODE_LENGTH_TYPED;
	if (pressed & VITA_BUTTON_UP)
		step = 1;
	else if (pressed & VITA_BUTTON_DOWN)
		step = -1;
	else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 150000)
		step = (buttons & VITA_BUTTON_UP) ? 1 : -1;
	if (step)
	{
		const char *at = strchr(alphabet, code_typed[code_cursor]);
		int position = at ? (int)(at - alphabet) : 0;
		int size = (int)sizeof(alphabet) - 1;

		code_typed[code_cursor] = alphabet[(position + step + size) % size];
		last_move = now;
	}
}

/* the friend's code screen's buttons (code_input's, for 12 characters) */
static void friend_code_input(unsigned long pressed, unsigned long buttons, unsigned long long now)
{
	static const char alphabet[] = P2P_CODE_ALPHABET;
	int step = 0;

	if (pressed & VITA_BUTTON_CIRCLE)
	{
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_CROSS)
	{
		int result = p2p_friends_add(friend_code_typed);

		set_notice("%s", p2p_friends_add_text(result));
		{
			char line[160];

			snprintf(line, sizeof(line), "settings: friends: a code typed: %s", p2p_friends_add_text(result));
			vita_host_log(line);
		}
		friends_refresh(1);
		screen = SCREEN_LIST;
		return;
	}
	if (pressed & VITA_BUTTON_LEFT)
		friend_code_cursor = (friend_code_cursor + P2P_FRIEND_CODE_LENGTH - 1) % P2P_FRIEND_CODE_LENGTH;
	if (pressed & VITA_BUTTON_RIGHT)
		friend_code_cursor = (friend_code_cursor + 1) % P2P_FRIEND_CODE_LENGTH;
	if (pressed & VITA_BUTTON_UP)
		step = 1;
	else if (pressed & VITA_BUTTON_DOWN)
		step = -1;
	else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 150000)
		step = (buttons & VITA_BUTTON_UP) ? 1 : -1;
	if (step)
	{
		const char *at = strchr(alphabet, friend_code_typed[friend_code_cursor]);
		int position = at ? (int)(at - alphabet) : 0;
		int size = (int)sizeof(alphabet) - 1;

		friend_code_typed[friend_code_cursor] = alphabet[(position + step + size) % size];
		last_move = now;
	}
}

/* an action on an entry, its outcome said; a join goes on to Join's steps,
as Join with a code's */
static void friend_do(const struct p2p_friend_entry *entry, int action)
{
	struct p2p_friend_entry copy = *entry;
	int outcome = p2p_friends_act(copy.id, copy.kind, action);

	/* (a friend's game with a password: typed on the system's keyboard,
	then joined as any: ime_input) */
	if (outcome == P2P_FRIENDS_PASSWORD)
	{
		snprintf(friend_password_id, sizeof(friend_password_id), "%s", copy.id);
		snprintf(friend_password_name, sizeof(friend_password_name), "%s", copy.name);
		if (vita_ime_open(T("Password"), "", PLAY_TEXT_LENGTH, 1) == 0)
		{
			friend_password_pending = 1;
			vgxm_menu_set(NULL, 0);
			return;
		}
		set_notice("%s", T("The keyboard did not open"));
		return;
	}
	{
		char line[160];

		snprintf(line, sizeof(line), "settings: friends: %s, action %d: %s", copy.name, action,
			p2p_friends_outcome_text(outcome));
		vita_host_log(line);
	}
	if (outcome == P2P_FRIENDS_DONE && (action == P2P_FRIENDS_JOIN || action == P2P_FRIENDS_INVITE_ACCEPT))
	{
		set_notice(T("Joining %.16s's game..."), copy.name);
		guide_action = ACTION_JOIN;
		guide_problem[0] = 0;
		code_for_join = 1;
		screen = SCREEN_GUIDE;
	}
	else if (outcome == P2P_FRIENDS_DONE && action == P2P_FRIENDS_INVITE)
		set_notice(T("Invited %.16s"), copy.name);
	else
		set_notice("%s", p2p_friends_outcome_text(outcome));
	friends_refresh(1);
}

/* an entry's buttons: cross its first thing, square its second, triangle
the question before removing a friend or blocking a request */
static void friend_input(const struct p2p_friend_entry *entry, unsigned long pressed)
{
	if (pressed & VITA_BUTTON_TRIANGLE)
	{
		if (entry->kind == P2P_FRIEND_KIND_FRIEND || entry->kind == P2P_FRIEND_KIND_REQUEST ||
			entry->kind == P2P_FRIEND_KIND_INVITE)
		{
			friend_confirm_entry = *entry;
			friend_confirm_action = entry->kind == P2P_FRIEND_KIND_FRIEND ? P2P_FRIENDS_REMOVE : P2P_FRIENDS_BLOCK;
			screen = SCREEN_FRIEND_CONFIRM;
		}
		return;
	}
	if (pressed & VITA_BUTTON_CROSS)
	{
		switch (entry->kind)
		{
		case P2P_FRIEND_KIND_REQUEST:
			friend_do(entry, P2P_FRIENDS_ACCEPT);
			break;
		case P2P_FRIEND_KIND_INVITE:
			friend_do(entry, P2P_FRIENDS_INVITE_ACCEPT);
			break;
		case P2P_FRIEND_KIND_FRIEND:
		{
			char warning[160];

			/* (the party's leader: told first of a game with fewer open slots
			than the party; cross again joins) */
			if (entry->joinable && p2p_party_join_warning(entry->players, entry->maximum, warning, sizeof(warning)) &&
				!(!strcmp(party_warned_id, entry->id) && now_us() - party_warned_time < 8000000ULL))
			{
				snprintf(party_warned_id, sizeof(party_warned_id), "%s", entry->id);
				party_warned_time = now_us();
				set_notice("%.110s %s", warning, T("Cross again: join"));
				break;
			}
			party_warned_id[0] = 0;
			friend_do(entry, P2P_FRIENDS_JOIN);
			break;
		}
		case P2P_FRIEND_KIND_BLOCKED:
			friend_do(entry, P2P_FRIENDS_UNBLOCK);
			break;
		}
		return;
	}
	if (pressed & VITA_BUTTON_SQUARE)
	{
		switch (entry->kind)
		{
		case P2P_FRIEND_KIND_REQUEST:
			friend_do(entry, P2P_FRIENDS_DECLINE);
			break;
		case P2P_FRIEND_KIND_INVITE:
			friend_do(entry, P2P_FRIENDS_INVITE_IGNORE);
			break;
		case P2P_FRIEND_KIND_FRIEND:
			friend_do(entry, P2P_FRIENDS_INVITE);
			break;
		case P2P_FRIEND_KIND_OUTGOING:
			friend_do(entry, P2P_FRIENDS_CANCEL);
			break;
		}
	}
}

/* a party line's buttons (a member, an invite, a friend to invite) */
static void party_input(const struct line *line, unsigned long pressed)
{
	char log[160];
	int outcome;

	if (line->type == LINE_PARTY_MEMBER)
	{
		struct p2p_party_member member = party_members_view[line->index];

		if (member.self)
			return;
		if ((pressed & VITA_BUTTON_TRIANGLE) && p2p_party_leading())
		{
			party_confirm_member = member;
			screen = SCREEN_PARTY_CONFIRM;
			return;
		}
		if (pressed & VITA_BUTTON_CROSS)
		{
			outcome = p2p_party_mute(member.id, !member.muted);
			if (outcome == P2P_PARTY_DONE)
				set_notice(member.muted ? T("%.20s unmuted") : T("%.20s muted"), member.name);
			else
				set_notice("%s", p2p_party_outcome_text(outcome));
		}
	}
	else if (line->type == LINE_PARTY_INVITE && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE)))
	{
		struct p2p_party_invite invite = party_invites_view[line->index];
		int accept = (pressed & VITA_BUTTON_CROSS) != 0;

		outcome = p2p_party_answer(invite.id, accept);
		set_notice("%s", outcome != P2P_PARTY_DONE ? p2p_party_outcome_text(outcome) : accept ? T("Joining the party...") :
			T("Party invite declined"));
		snprintf(log, sizeof(log), "settings: party: %s's invite %s: %s", invite.name, accept ? "accepted" : "declined",
			p2p_party_outcome_text(outcome));
		vita_host_log(log);
	}
	else if (line->type == LINE_PARTY_PRESET)
	{
		if (pressed & VITA_BUTTON_LEFT)
			party_preset = (party_preset + P2P_PARTY_PRESETS - 1) % P2P_PARTY_PRESETS;
		else if (pressed & VITA_BUTTON_RIGHT)
			party_preset = (party_preset + 1) % P2P_PARTY_PRESETS;
		else if (pressed & VITA_BUTTON_CROSS)
		{
			outcome = p2p_party_chat_preset(party_preset);
			set_notice("%s", outcome == P2P_PARTY_DONE ? T("Sent to your party") : p2p_party_outcome_text(outcome));
			snprintf(log, sizeof(log), "settings: party: quick chat %d: %s", party_preset, p2p_party_outcome_text(outcome));
			vita_host_log(log);
		}
	}
	else if (line->type == LINE_PARTY_FRIEND && (pressed & VITA_BUTTON_CROSS))
	{
		struct p2p_friend_entry entry = friends_view[party_friends_view[line->index]];

		outcome = p2p_party_invite(entry.id);
		if (outcome == P2P_PARTY_DONE)
			set_notice(T("Invited %.16s to your party"), entry.name);
		else
			set_notice("%s", p2p_party_outcome_text(outcome));
		snprintf(log, sizeof(log), "settings: party: invite %s: %s", entry.name, p2p_party_outcome_text(outcome));
		vita_host_log(log);
	}
	party_refresh(1);
}

/* the leader's question's buttons */
static void party_confirm_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if (pressed & VITA_BUTTON_CROSS)
	{
		int outcome = p2p_party_kick(party_confirm_member.id);

		screen = SCREEN_LIST;
		if (outcome == P2P_PARTY_DONE)
			set_notice(T("Removed %.20s from the party"), party_confirm_member.name);
		else
			set_notice("%s", p2p_party_outcome_text(outcome));
		party_refresh(1);
	}
}

/* Friend code backup's end: what came of it said, what was typed wiped (a
restore opened let go) */
static void friend_backup_finish(const char *said)
{
	if (friend_backup.step == FRIEND_BACKUP_CONFIRM)
		p2p_friends_backup_cancel();
	memset(friend_backup.password, 0, sizeof(friend_backup.password));
	snprintf(friend_backup.said, sizeof(friend_backup.said), "%s", said);
	friend_backup.step = FRIEND_BACKUP_DONE;
	friend_backup.ime_next = 0;
}

/* the system's keyboard for the step: the file's name, the password, the
password again */
static void friend_backup_keyboard(void)
{
	const char *title = T("The password again");
	int name = friend_backup.step == FRIEND_BACKUP_NAME;

	friend_backup.ime_next = 0;
	if (name)
		title = T("Backup file name");
	else if (friend_backup.step == FRIEND_BACKUP_PASSWORD)
		title = friend_backup.restore ? T("The backup's password") : T("A password (8 characters or more)");
	if (vita_ime_open(title, name ? P2P_FRIENDS_BACKUP_DEFAULT_NAME : "", name ? P2P_FRIENDS_BACKUP_NAME_SIZE - 1 :
		P2P_FRIENDS_BACKUP_PASSWORD_MAXIMUM, !name) != 0)
	{
		friend_backup_finish(T("The keyboard did not open"));
		return;
	}
	friend_backup.ime_pending = 1;
	vgxm_menu_set(NULL, 0);
}

/* (ime_input) what was typed for the step (result: the keyboard's) */
static void friend_backup_typed(int result, const char *typed)
{
	int outcome;

	if (result <= 0)
	{
		/* (cancelled: the question again, nothing kept) */
		memset(friend_backup.password, 0, sizeof(friend_backup.password));
		friend_backup.step = FRIEND_BACKUP_MENU;
		return;
	}
	switch (friend_backup.step)
	{
	case FRIEND_BACKUP_NAME:
		if (!p2p_friends_backup_name(typed, friend_backup.name))
			friend_backup_finish(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_BAD_NAME));
		else
		{
			friend_backup.step = FRIEND_BACKUP_PASSWORD;
			friend_backup.ime_next = 1;
		}
		break;
	case FRIEND_BACKUP_PASSWORD:
		if (friend_backup.restore)
		{
			outcome = p2p_friends_backup_restore(friend_backup.name, typed);
			if (outcome == P2P_FRIENDS_BACKUP_WORKING)
				friend_backup.step = FRIEND_BACKUP_WORKING;
			else
				friend_backup_finish(p2p_friends_backup_text(outcome));
			vita_host_log("settings: friends: a friend code backup being opened (settings panel)");
			break;
		}
		outcome = p2p_friends_backup_password_check(typed);
		if (outcome != P2P_FRIENDS_BACKUP_IDLE)
			friend_backup_finish(p2p_friends_backup_text(outcome));
		else
		{
			snprintf(friend_backup.password, sizeof(friend_backup.password), "%.64s", typed);
			friend_backup.step = FRIEND_BACKUP_AGAIN;
			friend_backup.ime_next = 1;
		}
		break;
	case FRIEND_BACKUP_AGAIN:
		if (strcmp(typed, friend_backup.password))
			friend_backup_finish(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_MISMATCH));
		else
			friend_backup.step = FRIEND_BACKUP_LIST;
		break;
	}
}

/* Friend code backup's buttons (each frame: the work watched) */
static void friend_backup_input(unsigned long pressed)
{
	char text[256];
	int outcome;

	if (friend_backup.ime_next)
	{
		friend_backup_keyboard();
		return;
	}
	switch (friend_backup.step)
	{
	case FRIEND_BACKUP_MENU:
		if (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE))
		{
			friend_backup.restore = (pressed & VITA_BUTTON_SQUARE) != 0;
			friend_backup.step = FRIEND_BACKUP_NAME;
			friend_backup_keyboard();
		}
		else if (pressed & VITA_BUTTON_CIRCLE)
			screen = SCREEN_LIST;
		break;
	case FRIEND_BACKUP_LIST:
		if (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE))
		{
			int with_list = (pressed & VITA_BUTTON_CROSS) != 0;

			outcome = p2p_friends_backup_export(friend_backup.name, friend_backup.password, with_list);
			memset(friend_backup.password, 0, sizeof(friend_backup.password));
			if (outcome == P2P_FRIENDS_BACKUP_WORKING)
				friend_backup.step = FRIEND_BACKUP_WORKING;
			else
				friend_backup_finish(p2p_friends_backup_text(outcome));
			vita_host_log(with_list ? "settings: friends: backing up the friend code and the list (settings panel)" :
				"settings: friends: backing up the friend code (settings panel)");
		}
		else if (pressed & VITA_BUTTON_CIRCLE)
			friend_backup_finish(T("Not backed up"));
		break;
	case FRIEND_BACKUP_WORKING:
		outcome = p2p_friends_backup_state(text, sizeof(text));
		if (outcome == P2P_FRIENDS_BACKUP_READY)
		{
			if (p2p_friends_backup_found(friend_backup.code, friend_backup.current, sizeof(friend_backup.code),
				&friend_backup.friend_count))
			{
				friend_backup.step = FRIEND_BACKUP_CONFIRM;
			}
			else
				friend_backup_finish(p2p_friends_backup_text(P2P_FRIENDS_BACKUP_NOTHING));
		}
		else if (outcome == P2P_FRIENDS_BACKUP_SAVED)
		{
			char said[192];

			snprintf(said, sizeof(said), "%s\nux0:data/haloce-vita/\n%s.hfb\n%s", T("Backup saved to"),
				friend_backup.name, T("Keep it and its password safe."));
			friend_backup_finish(said);
		}
		else if (outcome != P2P_FRIENDS_BACKUP_WORKING)
			friend_backup_finish(outcome == P2P_FRIENDS_BACKUP_IDLE ? p2p_friends_backup_text(P2P_FRIENDS_BACKUP_NOTHING) :
				text);
		break;
	case FRIEND_BACKUP_CONFIRM:
		if (pressed & VITA_BUTTON_CROSS)
		{
			outcome = p2p_friends_backup_confirm();
			friend_backup.step = FRIEND_BACKUP_DONE;
			if (outcome == P2P_FRIENDS_BACKUP_RESTORED)
			{
				char code[P2P_FRIEND_CODE_SIZE];

				p2p_friends_code(code, sizeof(code));
				snprintf(text, sizeof(text), "%s\n%s", T("Friend code restored:"), code);
				friend_backup_finish(text);
				vita_host_log("settings: friends: the friend code restored (settings panel)");
			}
			else
				friend_backup_finish(p2p_friends_backup_text(outcome));
			friends_refresh(1);
		}
		else if (pressed & VITA_BUTTON_CIRCLE)
			friend_backup_finish(T("Not restored: your friend code stays"));
		break;
	default:
		if (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE))
		{
			friend_backup.step = FRIEND_BACKUP_MENU;
			friend_backup.said[0] = 0;
			screen = SCREEN_LIST;
		}
		break;
	}
}

/* the question's buttons */
static void friend_confirm_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if (pressed & VITA_BUTTON_CROSS)
	{
		screen = SCREEN_LIST;
		friend_do(&friend_confirm_entry, friend_confirm_action);
	}
	else if ((pressed & VITA_BUTTON_SQUARE) && friend_confirm_action == P2P_FRIENDS_REMOVE)
	{
		screen = SCREEN_LIST;
		friend_do(&friend_confirm_entry, P2P_FRIENDS_BLOCK);
	}
}

/* joins a public game chosen (a locked one's password first, on the
system's keyboard) */
static void browse_join(const struct p2p_lobby_entry *entry)
{
	if (entry->locked)
	{
		browse_join_pending = 1;
		if (vita_ime_open(T("Password"), "", PLAY_TEXT_LENGTH, 1) == 0)
		{
			vgxm_menu_set(NULL, 0);
			return;
		}
		browse_join_pending = 0;
	}
	play_join_entry(entry, NULL);
	set_notice(T("Joining %.20s..."), entry->name);
	p2p_lobby_browse(0);
	screen = SCREEN_GUIDE;
}

/* the public lobby screen's buttons */
static void browse_input(unsigned long pressed)
{
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		p2p_lobby_browse(0);
		screen = SCREEN_LIST;
		return;
	}
	if ((pressed & VITA_BUTTON_UP) && browse_selected > 0)
		browse_selected--;
	if ((pressed & VITA_BUTTON_DOWN) && browse_selected < browse_count - 1)
		browse_selected++;
	/* (every host asked to publish again) */
	if (pressed & VITA_BUTTON_SQUARE)
		p2p_lobby_refresh();
	if ((pressed & VITA_BUTTON_CROSS) && browse_selected < browse_count)
	{
		const struct p2p_lobby_entry *entry = &browse_entries[browse_selected];

		/* (crossplay_guard.h: a large or co-op PC game, Experimental, asked
		about once, on a screen of its own; a Vita's game never) */
		if (entry->platform != CROSSPLAY_PLATFORM_VITA && (entry->experimental || entry->coop) &&
			crossplay_guard_answered(entry->name, (unsigned long)(now_us() / 1000)) != 1)
		{
			browse_large = *entry;
			screen = SCREEN_LARGE;
			return;
		}
		browse_join(entry);
	}
}

/* ... its buttons: the answer remembered, so that the game's lobby does
not ask again (port/linux/game/network_crossplay_guard.c) */
static void large_input(unsigned long pressed)
{
	char line[96];

	if (!(pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE)))
		return;
	crossplay_guard_answer(browse_large.name, (pressed & VITA_BUTTON_CROSS) != 0, (unsigned long)(now_us() / 1000));
	snprintf(line, sizeof(line), "settings: the %s PC game %.20s (%d of %d players): %s",
		browse_large.coop ? "co-op" : "large", browse_large.name, browse_large.players, browse_large.maximum,
		(pressed & VITA_BUTTON_CROSS) ? "join anyway" : "no");
	vita_host_log(line);
	screen = SCREEN_BROWSE;
	if (pressed & VITA_BUTTON_CROSS)
		browse_join(&browse_large);
}

/* the delete confirmation's buttons */
static void delete_input(unsigned long pressed)
{
	struct map_entry *map = selected_map();

	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if ((pressed & VITA_BUTTON_CROSS) && map)
	{
		if (halo_cache_map_in_use(map->name))
			set_notice(T("%.30s is in play: leave it first"), map->name);
		else
		{
			map_delete(map);
			maps_scan();
		}
		screen = SCREEN_LIST;
	}
}

/* the Mod manager's details: left and right turn it off or on, circle
(or cross) goes back */
static void mod_details_input(unsigned long pressed)
{
	struct halo_mod_entry *entry = selected_mod();

	if (!entry || (pressed & (VITA_BUTTON_CIRCLE | VITA_BUTTON_CROSS)))
		screen = SCREEN_LIST;
	else if (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT))
		mod_entry_toggle(entry, (pressed & VITA_BUTTON_LEFT) ? 1 : 0);
}

/* the Mod manager's delete question: cross asks the game to delete it */
static void mod_delete_input(unsigned long pressed)
{
	struct halo_mod_entry *entry = selected_mod();

	if (pressed & VITA_BUTTON_CIRCLE)
		screen = SCREEN_LIST;
	else if ((pressed & VITA_BUTTON_CROSS) && entry)
	{
		if (!entry->deletable)
			set_notice("%s", T("Its name isn't one this game takes: use VitaShell"));
		else
			mods_request_delete(entry);
		screen = SCREEN_LIST;
	}
}

/* the public games again (not this machine's own game); the chosen one
stays chosen while the list changes under it */
static void browse_refresh(void)
{
	/* (the list taken once: p2p_lobby_entry by index took it again for
	each, and a game that moved up between two - its players changed, one
	above it gone - was read twice) */
	static struct p2p_lobby_entry games[P2P_LOBBY_MAXIMUM_ENTRIES];
	char selected[sizeof(games[0].id)] = "";
	int index, vita, count;

	if (browse_selected < browse_count)
		memcpy(selected, browse_entries[browse_selected].id, sizeof(selected));
	browse_count = 0;
	count = p2p_lobby_entries(games, P2P_LOBBY_MAXIMUM_ENTRIES);
	/* (the Vitas' games first, then the PCs', as the menus' browsers group
	them, each in the list's order) */
	for (vita = 1; vita >= 0; vita--)
	{
		for (index = 0; browse_count < BROWSE_LINES && index < count; index++)
		{
			struct p2p_lobby_entry const *entry = &games[index];

			if (entry->own || crossplay_guard_vita_game(entry->platform, entry->marks) != vita)
				continue;
			if (selected[0] && !strcmp(entry->id, selected))
				browse_selected = browse_count;
			browse_entries[browse_count++] = *entry;
		}
	}
	if (browse_selected >= browse_count)
		browse_selected = browse_count ? browse_count - 1 : 0;
}

/* a Play page text row: the system's keyboard opened on its text
(nonzero if it opened) */
static int text_edit(const struct setting *setting)
{
	const struct play_text *text = play_text_named(setting->variable);
	int password = !strcmp(setting->variable, "HALO_NET_LOBBY_PASSWORD");

	if (vita_ime_open(password ? T("Password (empty: none)") : T("Lobby name"), text->value, PLAY_TEXT_LENGTH,
		password) != 0)
	{
		set_notice("%s", T("The keyboard did not open"));
		return 0;
	}
	ime_setting = setting;
	vgxm_menu_set(NULL, 0);
	return 1;
}

/* (each frame while the keyboard is up) nonzero while it is: the text
typed goes to its row (or the password to the chosen public game's join),
and the panel is back */
static int ime_input(void)
{
	char typed[P2P_PARTY_CHAT_SIZE + 8];
	int result;

	if (!ime_setting && !browse_join_pending && !party_message_pending && !friend_password_pending &&
		!friend_backup.ime_pending)
	{
		return 0;
	}
	result = vita_ime_poll(typed, sizeof(typed));
	if (result == 0)
		return 1;
	if (friend_backup.ime_pending)
	{
		friend_backup.ime_pending = 0;
		friend_backup_typed(result, typed);
		memset(typed, 0, sizeof(typed));
	}
	else if (friend_password_pending)
	{
		friend_password_pending = 0;
		if (result > 0)
		{
			int outcome = p2p_friends_join_password(friend_password_id, typed);

			if (outcome == P2P_FRIENDS_DONE)
			{
				set_notice(T("Joining %.16s's game..."), friend_password_name);
				guide_action = ACTION_JOIN;
				guide_problem[0] = 0;
				code_for_join = 1;
				screen = SCREEN_GUIDE;
			}
			else
				set_notice("%s", p2p_friends_outcome_text(outcome));
		}
		memset(typed, 0, sizeof(typed));
	}
	else if (party_message_pending)
	{
		party_message_pending = 0;
		if (result > 0 && typed[0])
		{
			int outcome = p2p_party_chat_send(typed);

			set_notice("%s", outcome == P2P_PARTY_DONE ? T("Sent to your party") : p2p_party_outcome_text(outcome));
			vita_host_log(outcome == P2P_PARTY_DONE ? "settings: party: a message sent (settings panel)" :
				"settings: party: a message not sent");
		}
	}
	else if (ime_setting)
	{
		struct play_text *text = play_text_named(ime_setting->variable);

		if (result > 0)
		{
			char line[96];

			play_text_set(text, typed, 1);
			__atomic_add_fetch(&halo_settings_generation, 1, __ATOMIC_RELEASE);
			save();
			if (!strcmp(text->variable, "HALO_NET_LOBBY_PASSWORD"))
				snprintf(line, sizeof(line), "settings: password %s (settings panel)", text->value[0] ? "set" : "none");
			else
				snprintf(line, sizeof(line), "settings: %s=%s (settings panel)", text->variable, text->value);
			vita_host_log(line);
		}
		ime_setting = NULL;
	}
	else
	{
		browse_join_pending = 0;
		if (result > 0 && browse_selected < browse_count)
		{
			play_join_entry(&browse_entries[browse_selected], typed);
			set_notice(T("Joining %.20s..."), browse_entries[browse_selected].name);
			p2p_lobby_browse(0);
			screen = SCREEN_GUIDE;
		}
	}
	if (panel_open)
		show();
	return 1;
}

/* (each frame) a line of text the game's menus asked for
(system_link_shortcut.h, port/linux/game/menu_functions.c): the system's
keyboard opened on it, and the text typed handed back; nonzero while the
keyboard is up (the game then gets no buttons) */
static int game_text_input(void)
{
	int state = __atomic_load_n(&halo_text_input_state, __ATOMIC_ACQUIRE);
	char typed[HALO_TEXT_INPUT_SIZE];
	int result;

	if (state == HALO_TEXT_INPUT_REQUESTED)
	{
		/* (not over the panel: the game's menus are not taking presses then) */
		if (panel_open)
			return 0;
		halo_text_input_title[HALO_TEXT_INPUT_SIZE - 1] = 0;
		halo_text_input_text[HALO_TEXT_INPUT_SIZE - 1] = 0;
		if (vita_ime_open(halo_text_input_title, halo_text_input_text, halo_text_input_maximum,
			halo_text_input_password) != 0)
		{
			vita_host_log("ime: the game's typing did not open");
			__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_CANCELLED, __ATOMIC_RELEASE);
			return 0;
		}
		__atomic_store_n(&halo_text_input_state, HALO_TEXT_INPUT_OPEN, __ATOMIC_RELEASE);
		return 1;
	}
	if (state != HALO_TEXT_INPUT_OPEN)
		return 0;
	result = vita_ime_poll(typed, sizeof(typed));
	if (result == 0)
		return 1;
	if (result > 0)
		snprintf(halo_text_input_text, sizeof(halo_text_input_text), "%s", typed);
	memset(typed, 0, sizeof(typed));
	__atomic_store_n(&halo_text_input_state, result > 0 ? HALO_TEXT_INPUT_DONE : HALO_TEXT_INPUT_CANCELLED,
		__ATOMIC_RELEASE);
	/* (the presses that closed the keyboard are not the game's) */
	held_after_close = previous_buttons;
	return 1;
}

/* to the next tab shown, or the one before */
static void tab_step(int step)
{
	int next = tab;

	do
		next = (next + step + TAB_COUNT) % TAB_COUNT;
	while (!tab_shown(next));
	tab = page = next;
	notice[0] = 0;
	/* (Multiplayer's Modded maps row says how many, Game's Forge row the
	map variants) */
	if (tab == TAB_MULTIPLAYER)
		maps_scan();
	if (tab == TAB_GAME)
		forge_scan();
}

/* the Player model row's choices, the game's (the Spartan, then each model
found in the maps folder), and the profile's choice among them */
static void player_models_choices(void)
{
	struct setting *setting = setting_named("HALO_PLAYER_MODEL");
	int index, count = player_models_panel_count();

	if (!setting)
		return;
	if (count > MAXIMUM_CHOICES)
		count = MAXIMUM_CHOICES;
	if (count < 1)
		count = 1;
	for (index = 0; index < count; index++)
	{
		snprintf(player_model_values[index], sizeof(player_model_values[index]), "%d", index);
		snprintf(player_model_names[index], sizeof(player_model_names[index]), "%s", player_models_panel_name(index));
		setting->values[index] = player_model_values[index];
		setting->names[index] = player_model_names[index];
	}
	setting->count = count;
	setting->choice = player_models_panel_chosen();
	if (setting->choice < 0 || setting->choice >= count)
		setting->choice = 0;
}

/* the Armor color and Visor color rows' choices (the game's names) and the
profile's choice among them */
static void cosmetics_choices(void)
{
	static const char *const variables[2] = { "HALO_ARMOR_COLOR", "HALO_VISOR_COLOR" };
	int row;

	for (row = 0; row < 2; row++)
	{
		struct setting *setting = setting_named(variables[row]);
		int index, count = row ? player_cosmetics_panel_visor_count() : player_cosmetics_panel_color_count();

		if (!setting)
			continue;
		if (count > MAXIMUM_CHOICES)
			count = MAXIMUM_CHOICES;
		for (index = 0; index < count; index++)
		{
			snprintf(cosmetics_values[row][index], sizeof(cosmetics_values[row][index]), "%d", index);
			setting->values[index] = cosmetics_values[row][index];
			setting->names[index] = row ? player_cosmetics_panel_visor_name(index) : player_cosmetics_panel_color_name(index);
		}
		setting->count = count;
		setting->choice = row ? player_cosmetics_panel_visor_chosen() : player_cosmetics_panel_color_chosen();
		if (setting->choice < 0 || setting->choice >= count)
			setting->choice = 0;
	}
}

static void panel_begin(void)
{
	panel_open = 1;
	player_models_choices();
	cosmetics_choices();
	/* (Dev hidden since: the first tab) */
	if (!tab_shown(tab))
		tab = page = TAB_GAME;
	if (tab == TAB_MULTIPLAYER)
		maps_scan();
	if (tab == TAB_GAME)
		forge_scan();
	show();
}

int vita_settings_panel_open(void)
{
	return panel_open;
}

/* (each frame) the connection the game runs (network_connection.c, switched
live: the panel's running_network follows it, halo_network_connection), and
ad hoc play's group for it (system_link_shortcut.h): its state, and the
game's requests - the room's group joined through the system's dialog, as
the guide's Host and Join join it (the room and the dialog way the tab's),
or left (the connection left Ad hoc: the Wi-Fi back) */
static void game_adhoc_requests(void)
{
	int request = __atomic_load_n(&halo_adhoc_request, __ATOMIC_ACQUIRE);
	int running = __atomic_load_n(&halo_network_connection, __ATOMIC_ACQUIRE);
	int adhoc;

	if (running >= HALO_NETWORK_WIFI && running <= HALO_NETWORK_ONLINE)
	{
		static const char *const names[] = { "wifi", "adhoc", "online" };

		if (strcmp(running_network, names[running]))
		{
			char line[96];

			snprintf(running_network, sizeof(running_network), "%s", names[running]);
			snprintf(line, sizeof(line), "settings: the game runs the connection %s now", running_network);
			vita_host_log(line);
		}
	}
	adhoc = !strcmp(running_network, "adhoc");
	if (request == HALO_ADHOC_REQUEST_JOIN)
	{
		/* (not over the panel, whose own buttons the dialog would take) */
		if (adhoc && vita_adhoc_state(NULL, 0) != 1 && vita_adhoc_state(NULL, 0) != 2)
		{
			if (panel_open)
				close_panel();
			vita_adhoc_connect(choice_of("HALO_ADHOC_DIALOG_MODE"), choice_of("HALO_ADHOC_ROOM") + 1);
			vita_host_log("settings: ad hoc: joining the room's group (the game)");
		}
	}
	else if (request == HALO_ADHOC_REQUEST_LEAVE)
	{
		vita_adhoc_leave();
		vita_host_log("settings: ad hoc: the group left (the game left Ad hoc)");
	}
	/* (the group's state set before the request is taken: the game, seeing
	it taken, reads the join's own state, never the one before) */
	__atomic_store_n(&halo_adhoc_group, adhoc ? vita_adhoc_state(NULL, 0) : HALO_ADHOC_GROUP_OFF, __ATOMIC_RELEASE);
	if (request != HALO_ADHOC_REQUEST_NONE)
		__atomic_store_n(&halo_adhoc_request, HALO_ADHOC_REQUEST_NONE, __ATOMIC_RELEASE);
}

int vita_settings_input(const struct vita_host_pad *pad)
{
	unsigned long buttons = pad->buttons;
	unsigned long pressed = buttons & ~previous_buttons;
	/* port: SELECT + D-pad down opens or closes the panel at once (the
	owner's: holding SELECT + START was a stretch); SELECT + START held, as
	before 1.1.0-beta.4, still does */
	int chord = (buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_DOWN);
	int both = chord || ((buttons & VITA_BUTTON_SELECT) && (buttons & VITA_BUTTON_START));
	unsigned long long now = now_us();
    int message_opened = 0;

	previous_buttons = buttons;
	held_after_close &= buttons;
	game_adhoc_requests();
	if (held_after_close && !panel_open)
		return 1;
	/* (the system's ad hoc dialog reads the pad itself: the game must not
	act on the same presses) */
	if (vita_adhoc_state(NULL, 0) == 1)
		return 1;
	/* (nor while the system's keyboard is up) */
	if (ime_input())
		return 1;
	/* (the game's menus' own typing, the same keyboard: OpenCE's multiplayer
	screens) */
	if (game_text_input())
		return 1;
	/* (Host or Join with Ad hoc: the dialog done, System Link next, or a
	message saying it did not join) */
	if (adhoc_pending)
	{
		if (vita_adhoc_state(NULL, 0) == 2)
		{
			guide_action = adhoc_pending;
			guide_request();
		}
		else
			vita_settings_message(guide_title(adhoc_pending),
				T("The Vita did not join the ad hoc group. Try again, or Game > Show dev settings: On, then Dev > Ad hoc dialog."));
		adhoc_pending = 0;
	}
	guide_poll(now);
	/* (the game's answer closed the panel just now: the buttons still held,
	the cross that asked included, are not the game's) */
	if (held_after_close && !panel_open)
		return 1;
    pthread_mutex_lock(&message_lock);
    if (message_pending)
    {
        vgxm_menu_set(pending_message, -1);
        message_pending = 0;
        message_visible = 1;
        message_opened = 1;
        panel_open = 0;
    }
    pthread_mutex_unlock(&message_lock);
    if (message_visible)
    {
        both_since = 0;
        if (!message_opened && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_CIRCLE)))
        {
            message_visible = 0;
            vgxm_menu_set(NULL, 0);
            /* (what the message covered comes back) */
            pthread_mutex_lock(&message_lock);
            if (question_visible)
            {
                question_visible = 0;
                question_pending = 1;
            }
            /* port: not a progress line withdrawn meanwhile (a thread's last
            message comes with its line's end: shown again, the line stayed
            over the game for good, holding the buttons - the update download
            and the CE installer looked frozen) */
            else if (progress_visible && !progress_hide)
                progress_changed = 1;
            pthread_mutex_unlock(&message_lock);
        }
        return 1;
    }
    if (question_progress_input(pressed))
    {
        both_since = 0;
        return 1;
    }
	/* port: SELECT + D-pad left: the Party page at once (party chat, in play
	and in the menus), or closed if it is the one open; the game sees
	neither (the panel's SELECT + down's way; not SELECT + up, Forge's
	monitor and Spartan) */
	/* (in play with the in-game chat - a network game, or a party's -
	the combo is the chat's quick chat: the game's, vita_pad.c and
	port/linux/game/game_chat.c, the panel closed) */
	if ((buttons & VITA_BUTTON_SELECT) && (pressed & VITA_BUTTON_LEFT) && !panel_open &&
		__atomic_load_n(&halo_game_chat_active, __ATOMIC_ACQUIRE))
	{
		both_since = 0;
		return 0;
	}
	if ((buttons & VITA_BUTTON_SELECT) && (pressed & VITA_BUTTON_LEFT))
	{
		both_since = 0;
		if (panel_open && page == PAGE_PARTY)
			close_panel();
		else
		{
			tab = TAB_MULTIPLAYER;
			page = PAGE_PARTY;
			screen = SCREEN_LIST;
			notice[0] = 0;
			if (!panel_open)
				panel_begin();
			else
				show();
			vita_host_log("settings: party: the Party page (SELECT + left)");
		}
		return 1;
	}
	/* (the game's menus asked for the panel: the Halo 3 main menu's Start,
	which the panel never sees pressed, being held already; the Halo 3
	multiplayer pause's PARTY, its Party page) */
	{
		int request = __atomic_exchange_n(&halo_settings_panel_request, 0, __ATOMIC_ACQ_REL);

		if (request && !panel_open)
		{
			if (request == HALO_SETTINGS_PANEL_PARTY)
			{
				tab = TAB_MULTIPLAYER;
				page = PAGE_PARTY;
				screen = SCREEN_LIST;
				notice[0] = 0;
				vita_host_log("settings: party: the Party page (the multiplayer pause's PARTY)");
			}
			else if (request == HALO_SETTINGS_PANEL_MULTIPLAYER)
			{
				tab = TAB_MULTIPLAYER;
				page = TAB_MULTIPLAYER;
				screen = SCREEN_LIST;
				notice[0] = 0;
				vita_host_log("settings: the Multiplayer tab (the offline Multiplayer screen's A)");
			}
			panel_begin();
			return 1;
		}
	}
	if (both)
	{
		if (!both_since)
			both_since = now;
		/* at once (SELECT + down), or held for 0.8 s (SELECT + START): the
		panel opens or closes, once per hold */
		if (both_since != 1 && (chord || now - both_since > 800000))
		{
			if (panel_open)
				close_panel();
			else
				panel_begin();
			both_since = 1;
		}
		/* (the pair never reaches the game while held: no pause menu) */
		return 1;
	}
	both_since = 0;
	if (!panel_open)
		return 0;
	if (screen == SCREEN_CODE)
	{
		code_input(pressed, buttons, now);
		show();
		return 1;
	}
	if (screen == SCREEN_FRIEND_CODE)
	{
		friend_code_input(pressed, buttons, now);
		show();
		return 1;
	}
	if (screen == SCREEN_FRIEND_CONFIRM)
	{
		friend_confirm_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_FRIEND_BACKUP)
	{
		int step = friend_backup.step;

		friend_backup_input(pressed);
		/* (not over the keyboard just opened) */
		if (!friend_backup.ime_pending && (pressed || step != friend_backup.step || screen != SCREEN_FRIEND_BACKUP ||
			now - last_shown > 500000))
		{
			show();
		}
		return 1;
	}
	if (screen == SCREEN_PARTY_CONFIRM)
	{
		party_confirm_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_BAN_CONFIRM)
	{
		ban_confirm_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_BROWSE)
	{
		browse_input(pressed);
		/* (the keyboard took the screen for a password) */
		if (browse_join_pending)
			return 1;
		/* (the list as it fills, twice a second) */
		if (screen == SCREEN_BROWSE && now - last_shown > 500000)
			browse_refresh();
		if (pressed || now - last_shown > 500000)
			show();
		return 1;
	}
	if (screen == SCREEN_DELETE)
	{
		delete_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	/* (the game's answer to the Mod manager page: shown once it came) */
	if (mods_poll() && page == PAGE_MODS && !pressed)
		show();
	if (screen == SCREEN_MOD_DETAILS || screen == SCREEN_MOD_DELETE)
	{
		if (screen == SCREEN_MOD_DETAILS)
			mod_details_input(pressed);
		else
			mod_delete_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_FORGE_DELETE)
	{
		forge_delete_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_FORGE_CLEAR)
	{
		forge_clear_input(pressed);
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_LARGE)
	{
		large_input(pressed);
		/* (the keyboard took the screen for a password) */
		if (browse_join_pending)
			return 1;
		if (pressed)
			show();
		return 1;
	}
	if (screen == SCREEN_GUIDE)
	{
		guide_input(pressed);
		/* (redrawn twice a second: the game's state, its answer) */
		if (panel_open && (pressed || now - last_shown > 500000))
			show();
		return 1;
	}
	if (pressed & VITA_BUTTON_CIRCLE)
	{
		/* (a page a row opened: back to where that row is, on it - its
		tab, or the page it is on: the Mod manager to Modded maps, Players
		and Infection to Vita and PC. Party is the tab's: SELECT + left and
		the multiplayer pause open it at once, "Multiplayer > Party") */
		int parent = page == PAGE_PARTY ? tab : page_parent(page);

		if (parent >= TAB_COUNT)
		{
			page = parent;
			notice[0] = 0;
			if (page == PAGE_MAPS)
				maps_scan();
			show();
		}
		else if (page != tab)
		{
			page = tab;
			notice[0] = 0;
			show();
		}
		else
			close_panel();
		return 1;
	}
	if (pressed & (VITA_BUTTON_L | VITA_BUTTON_R))
	{
		tab_step((pressed & VITA_BUTTON_L) ? -1 : 1);
		show();
		return 1;
	}
	{
		/* up and down step over the lines that can be chosen, and repeat
		while held */
		struct line lines[MAXIMUM_LINES];
		int count = page_lines(lines);
		int *selected = &page_selected[page];
		int step = 0;

		if (pressed & VITA_BUTTON_UP)
			step = -1;
		else if (pressed & VITA_BUTTON_DOWN)
			step = 1;
		else if ((buttons & (VITA_BUTTON_UP | VITA_BUTTON_DOWN)) && now - last_move > 250000)
			step = (buttons & VITA_BUTTON_UP) ? -1 : 1;
		if (*selected >= count)
			*selected = count - 1;
		if (step && count)
		{
			int tries;

			for (tries = 0; tries < count; tries++)
			{
				*selected = (*selected + step + count) % count;
				if (!line_passive(&lines[*selected]))
					break;
			}
			last_move = now;
		}
		if (count && (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS | VITA_BUTTON_SQUARE |
			VITA_BUTTON_TRIANGLE)))
		{
			struct line *line = &lines[*selected];

			if (line->type == LINE_FRIEND)
			{
				struct p2p_friend_entry entry = friends_view[line->index];

				friend_input(&entry, pressed);
				if (!panel_open)
					return 1;
			}
			else if (line->type == LINE_PARTY_MEMBER || line->type == LINE_PARTY_INVITE ||
				line->type == LINE_PARTY_FRIEND || line->type == LINE_PARTY_PRESET)
			{
				party_input(line, pressed);
			}
			else if (line->type == LINE_HOST_PLAYER)
				host_player_input(line, pressed);
			else if (line->type == LINE_CHAT_PLAYER)
				chat_player_input(line, pressed);
			else if (pressed == VITA_BUTTON_TRIANGLE)
				;
			else if (line->type == LINE_MOD)
			{
				struct halo_mod_entry *entry = &mods_view.entries[line->index];

				if (pressed & VITA_BUTTON_SQUARE)
					screen = SCREEN_MOD_DELETE;
				else if (pressed & VITA_BUTTON_CROSS)
					screen = SCREEN_MOD_DETAILS;
				else
					mod_entry_toggle(entry, (pressed & VITA_BUTTON_LEFT) ? 1 : 0);
			}
			else if (line->type == LINE_VARIANT)
			{
				if (pressed & VITA_BUTTON_SQUARE)
				{
					forge_delete_index = line->index;
					screen = SCREEN_FORGE_DELETE;
				}
			}
			else if (line->type == LINE_FORGE)
			{
				int step = (pressed & VITA_BUTTON_LEFT) ? -1 : 1, variants = 0, tries;

				if (line->index == FORGE_LINE_CLEAR_DEAD && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_RIGHT)))
				{
					int dead = 0;

					forge_panel_ai_counts(NULL, &dead);
					forge_panel_clear_ai(0);
					if (dead == 1)
						set_notice("%s", T("1 dead AI cleared"));
					else
						set_notice(T("%d dead AI cleared"), dead);
				}
				else if (line->index == FORGE_LINE_CLEAR_ALL && (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_RIGHT)))
					screen = SCREEN_FORGE_CLEAR;
				else if (line->index == FORGE_LINE_START)
				{
					if (pressed & (VITA_BUTTON_CROSS | VITA_BUTTON_RIGHT))
					{
						notice[0] = 0;
						forge_start();
						if (!panel_open)
							return 1;
					}
				}
				else if (line->index == FORGE_LINE_MAP && forge_map >= 0 && (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT)))
				{
					/* (the next map in the maps folder, either way) */
					int count = forge_panel_map_count(), map = forge_map;

					for (tries = 0; tries < count; tries++)
					{
						map = (map + step + count) % count;
						if (forge_panel_map_available(map))
							break;
					}
					if (map != forge_map)
					{
						forge_map = map;
						forge_variant = 0;
					}
				}
				else if (line->index == FORGE_LINE_NETWORK && (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT)))
				{
					forge_network = step > 0;
					vita_host_log(forge_network ? "settings: Forge on the network" : "settings: Forge local");
				}
				else if (line->index == FORGE_LINE_VARIANT && (pressed & (VITA_BUTTON_LEFT | VITA_BUTTON_RIGHT)))
				{
					forge_map_variant(0, &variants);
					if (forge_variant + step >= 0 && forge_variant + step <= variants)
						forge_variant += step;
				}
			}
			else if (line->type == LINE_MAP)
			{
				struct map_entry *map = &maps[line->index];

				if (pressed & VITA_BUTTON_SQUARE)
					screen = SCREEN_DELETE;
				else if (pressed & VITA_BUTTON_LEFT)
					map_set_disabled(map->name, 1);
				else if (pressed & VITA_BUTTON_RIGHT)
					map_set_disabled(map->name, 0);
				else
					map_set_disabled(map->name, !map_disabled(map->name));
			}
			else if (line->type == LINE_SETTING && settings[line->index].kind == KIND_TEXT)
			{
				/* (the system's keyboard: the panel hides meanwhile, and the
				text comes back in ime_input) */
				if ((pressed & VITA_BUTTON_CROSS) && text_edit(&settings[line->index]))
					return 1;
			}
			else if (line->type == LINE_SETTING && settings[line->index].kind == KIND_ACTION)
			{
				if (pressed & (VITA_BUTTON_RIGHT | VITA_BUTTON_CROSS))
				{
					notice[0] = 0;
					act(&settings[line->index]);
					if (!panel_open)
						return 1;
				}
			}
			else if (line->type == LINE_SETTING && !(pressed & VITA_BUTTON_SQUARE))
			{
				struct setting *setting = &settings[line->index];
				int step = (pressed & VITA_BUTTON_LEFT) ? -1 : 1;

				if (setting->choice + step <= choice_last(setting))
					change(setting, step);
			}
		}
	}
	/* (redrawn on a press, and every half second for the status line: a
	code appears when the game starts hosting; the report's progress) */
	if (pressed || now - last_shown > 500000)
		show();
	return 1;
}
