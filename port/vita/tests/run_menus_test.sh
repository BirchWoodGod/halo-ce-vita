#!/bin/bash
# OpenCE's multiplayer screens in the game's menus (port/linux/game/menu_tags.c),
# on the Linux harness (any build of this tree; a --release one checks the
# release builds' tag accessors too, tag_groups.h), every press scripted
# (HALO_TEST_PAD), the screens named in the log (HALO_UI_LOG):
#
#   fresh   a first start (no saves): the main menu, down and back, then
#           Multiplayer opens OpenCE's screen; B back, and Multiplayer again;
#           down to Create Game Internet (Server Setup, online off: the
#           screen that says internet play is off), B, B
#   level   a campaign level first (a10, left with Save and Quit), then the
#           main menu and Multiplayer twice as above: ui.map's tags and ours
#           are added again
#   xbox    without the Halo PC files: the Xbox's Multiplayer screen, nothing
#           added
#   h3probe  Menu style Halo 3 (HALO_MENU_STYLE=halo3): the Halo 3 set's screen
#            stands for the Xbox's main menu (menus/halo3)
#   h3classic  Menu style Halo CE: the Xbox's main menu, no Halo 3 screen
#   h3xbox   Halo 3 without the Halo PC files: the Xbox's main menu (the
#            Halo PC files are required, as for OpenCE's screens: the owner,
#            Oct 18 2026)
#   h3broken a Halo 3 PNG missing: the set left out, the Xbox's main menu
#   h3style  Halo CE at first, Halo 3 chosen while Multiplayer is open: back on
#            the main menu, the Halo 3 screen
#   h3online the main menu's live servers: five made-up games
#            (HALO_TEST_BROWSER_GAMES), the three with the most players shown
#   h3touch  taps (HALO_TEST_TAPS, as the Vita's touch screen's): FORGE on the
#            main menu opens the Forge lobby, the lobby's Leave closes it
#   h3resume a profile's saved game (a10 on Normal, saved and quit): the lobby
#            says at Last Checkpoint, and on Heroic that it replaces it
#   h3pause  a level's map gets the in-game set (ingame.txt): in The Silent
#            Cartographer, Start opens the Halo 3 pause screen; SAVE AND QUIT
#            saves and goes back to the Halo 3 main menu
#   h3campaign  a profile with every level (HALO_TEST_PROFILE, never in the
#            repository; skipped without): CAMPAIGN, the profile, the Halo 3
#            campaign lobby; Mission: The Silent Cartographer, Difficulty:
#            Heroic, Start Game: b30 loads on Heroic
#   h3mp     the Halo 3 multiplayer lobby (menus/halo3/multiplayer.xml) stands
#            for the Xbox's Multiplayer screen; internet play off: NETWORK is
#            SYSTEM LINK; choosing ONLINE starts internet play live (its status
#            Connecting...: TEST-NET brokers), SYSTEM LINK stops it; START GAME
#            hosts a system link game (the profile, then the game hosted with
#            the lobby's GAME and MAP: its lobby, no map or game type list); B
#            goes back to the lobby, and START GAME again hosts at once (the
#            profile not asked again)
#   h3browser  internet play on (its brokers unreachable: TEST-NET addresses)
#            with 16 made-up games of both platforms (HALO_TEST_BROWSER_GAMES,
#            HALO_TEST_BROWSER_PC): BROWSE GAMES, the profile, the Halo 3
#            games browser with the online list's games (CUSTOM GAMES), a
#            locked game the Halo 3 password screen, whose JOIN GAME says the
#            game is no longer listed
#   h3create  Square on ONLINE: GAME OPTIONS (Create Game); its GAME TYPE picker,
#            the name typed (HALO_TEST_TEXT_INPUT), START GAME as Server Setup's
#   h3hostonline  START GAME on ONLINE (HALO_TEST_PROFILE's profile; skipped
#            without), the lobby's MAP Blood Gulch: SELECT PROFILE, then the
#            game hosted on Blood Gulch and its lobby - no Create Game, map or
#            game type list, nothing but Halo 3 screens -; B: the multiplayer
#            lobby; Menu style Halo CE's CREATE GAME INTERNET still
#            OpenCE's Server Setup (HALO_TEST_SHOTS: the lobby's pictures)
#   h3mptouch  taps: MULTIPLAYER, the lobby's MAP, BLOOD GULCH in its picker,
#            the lobby's Back
#   h3coop   co-op from the Halo 3 campaign lobby (HALO_TEST_PROFILE; skipped
#            without): NETWORK, ONLINE CO-OP with internet play off (it
#            goes online live), SYSTEM LINK CO-OP (back to Same Wi-Fi), START
#            GAME:
#            the level hosted as co-op (as the Xbox's difficulty list's Y
#            hosted it) and the game's lobby, the waiting screen, open
#   h3scenes the lobbies' cameras (scene= in menus/halo3; menus.scenes,
#            HALO_MENU_SCENES): CAMPAIGN (HALO_TEST_PROFILE's profile; a new
#            one's campaign starts at once without), back, MULTIPLAYER, back,
#            FORGE, back: each lobby's camera written into its point of
#            ui.map and glided to, ring_loop asleep meanwhile, the main menu
#            waking it; then the same with HALO_MENU_SCENES=0: none
#   h3lobbyscenes  the lobbies' scenes (scene_object= in menus/halo3:
#            menu_scene_objects.c; the menu_campaign, menu_multiplayer and
#            menu_monitor scene packs, built from the maps folder's own maps the
#            first time: scene_packs.c), HALO_TEST_PROFILE's profile: CAMPAIGN's
#            lobby a Warthog held mid-jump (frozen: its physics off) with the
#            Master Chief seated as its driver and two Marines as its gunner
#            and passenger, MULTIPLAYER's three white Spartans (a rocket
#            launcher in a held airborne frame, the flag, a sniper rifle
#            crouching), FORGE's 343 Guilty Spark looking at a Warthog; each
#            made as its lobby opens, no effect or particle made meanwhile (no
#            dust), let go as the main menu comes back; the Customization
#            screen's preview (the console's "pack preview": shown in a colour,
#            recoloured, hidden); then with HALO_SCENE_PACKS=0, Menu style Halo
#            CE, no campaign map (the multiplayer scene alone) and no map at
#            all: no scene where its pack cannot be, nothing else changed, no
#            crash
#   h3profiles  the Halo 3 SELECT PROFILE (menus/halo3/profiles.xml) stands for
#            the Xbox's campaign profile screen: a first start (no profile)
#            shows CREATE NEW PROFILE alone, a name of spaces refused, then
#            Rookie made (blam.sav, its name) and the lobby opened; back on the
#            list, the card's Spartan (the scene packs' menu_spartan, in
#            Rookie's colour, in the card's window), let go with the screen; with
#            HALO_TEST_PROFILE's profile too: Triangle with that profile's name
#            refused (taken), Rookie made, played, then Square and its
#            question's DELETE PROFILE delete it, the other profile's file left
#            byte for byte; with Menu style Halo CE, the Xbox's screen
#   h3proftouch  taps: CAMPAIGN, SELECT PROFILE's New Profile (its icon and
#            label): Rookie made, the lobby; the lobby's Back: SELECT PROFILE
#   h3customize  CUSTOMIZATION (menus/halo3/customize.xml), with
#            HALO_TEST_PROFILE's profile: its list's card shows the profile's
#            Spartan (as SELECT PROFILE's); the name typed, the colour, the stick
#            layout, the look sensitivity and invert look changed, Circle saves
#            them; blam.sav and SaveMeta.xbx read back (the other fields kept),
#            then the harness started again on that save: the editor shows
#            them, and leaving it saves nothing; the preview (the
#            menu_spartan pack's Spartan in the editor's window, in front of
#            its own camera): shown in the profile's colour, made again in
#            RED as COLOR changes, never the swatch meanwhile, let go with the
#            editor; with HALO_SCENE_PACKS=0 the swatch alone
#   h3nameplate  the Halo 3 nameplate (the main menu's, the lobbies' and the
#            pause screen's, port h3 nameplate) with HALO_TEST_PROFILE's profile
#            and no profile chosen yet (a start): the profile's name and its
#            colour's bar, not the system's user name in green; CUSTOMIZATION
#            renames it and makes it RED, Circle saves: back at the main menu
#            the nameplate says the new name on the red bar at once
#   h3mpcustom  the Halo 3 multiplayer lobby's MAP picker with a custom map (the
#            mod manager's test content: mygulch.map, Blood Gulch's copy under
#            another name): after the Xbox's thirteen, MYGULCH, the rows
#            scrolling to it, its "?" picture and description as the Xbox's list
#            shows them; chosen, the lobby says MYGULCH and START GAME (system
#            link) hosts the game on it (its pregame: one machine cannot start
#            a game of one player on the Linux harness); then started again on
#            that save with mygulch turned off: the picker lists the thirteen
#            only and the lobby is back on Battle Creek
#   h3renamed  maps players renamed (Grunt pirate, insider.19: cursed-bloodgulch):
#            Blood Gulch's Xbox map copied as cursed-bloodgulch.map and gulch_two.map
#            (both named bloodgulch inside), and with HALO_TEST_CE_MAPS' extinction.map
#            a Custom Edition copy, cursed-extinction.map, beside it: the multiplayer
#            level list (the Halo 3 MAP picker's and Halo CE's Create Game's) has each
#            under its file's name after the thirteen, Blood Gulch once; the picker's
#            first custom row is CURSED-BLOODGULCH, chosen for the lobby. With
#            HALO_TEST_PROFILE, local games: cursed-bloodgulch loads and plays (its
#            cache file named cursed-bloodgulch), then Blood Gulch is its own copy
#            (precached, not the renamed one), then cursed-bloodgulch again found by
#            its name in the cache files
#   h3customrows  CUSTOMIZATION's preview the same on every row (insider.17:
#            the Spartan lost its visor's reflection and its shading with
#            VIBRATION, the last row, focused - Ben 10 and xal, on the
#            Performance and the lowest Custom presets): with the testers'
#            presets' values and HALO_TEST_PROFILE's profile, the editor
#            opened, the preview's Spartan shown, then every row focused in
#            turn down to VIBRATION, up and down again; HALO_PIXEL_SHADER_LOG
#            (rasterizer_xbox.c) names each pixel shader the first time it is
#            set whole: the models' (texture modes 00018421, the base, detail,
#            multipurpose maps and the cube map) set before the rows are
#            walked, and none of theirs new while they are (the menus'
#            widgets left alpha stages in the shared definition, which the
#            next frame's models ran: rasterizer_xbox_models.c
#            set_environment_shader_pixel_shader)
#   h3models  CUSTOMIZATION's MODEL (port/linux/game/player_models.c) with the
#            Custom Edition maps of HALO_TEST_CE_MAPS (extinction.map,
#            Covenant_V_Marines_Beta_5.map) and HALO_TEST_PROFILE's profile:
#            the maps folder looked through (two models), MODEL stepped to the
#            Extinction Spartan (its pack built the first time, the preview
#            making it in the profile's colour), to the protected map's, back;
#            Circle keeps the choice in t:\player_models.txt (blam.sav as it
#            was); started again, the editor shows it; skipped without them
#   h3modelsmany  CUSTOMIZATION's MODEL stepped through fourteen models
#            (fourteen links to HALO_TEST_CE_MAPS' extinction.map under names
#            of their own, a pack each), each resting: every one shown, each
#            passed let go (scene_packs_release), none refused for want of
#            room; then eight quick presses: the models passed not loaded
#   h3damaged  a profile of a 0-byte blam.sav and one of garbage: SELECT
#            PROFILE says each is damaged and plays neither, CUSTOMIZATION
#            edits neither, CREATE NEW PROFILE still makes one; with
#            HALO_TEST_PROFILE's profile and a damaged savegame.bin, the lobby
#            says the saved game is damaged and starts at Mission Start
#   browserprofile  the online browsers ask for the profile first, as SYSTEM LINK and CREATE
#            GAME do (the owner, insider.10: "the profile switch picker didn't show up when
#            I click server browser"): Menu style Halo CE's Multiplayer, SERVER BROWSER: the
#            profile screen, then OpenCE's server browser (B: the profile again, B: Multiplayer);
#            the Halo 3 lobby's BROWSE GAMES on ONLINE: the profile, then the games browser;
#            internet play off: SERVER BROWSER says how to turn it on, no profile
#   h3players  the Halo 3 PLAYERS screen (menus/halo3/players.xml, MUTE CHAT too; the owner: "Can I move the
#            cursor over the player like on Halo 3 and add friend?"), HALO_TEST_PROFILE's
#            profile: SYSTEM LINK CO-OP hosted from the campaign lobby, the game's lobby;
#            Triangle: PLAYERS, this machine's player and three made up
#            (HALO_TEST_PLAYERS_FAKE=3); Cross on one: its options; ADD FRIEND with friends
#            not online says go online first; this machine's own player has no options;
#            Circle: the game's lobby again (the friend request itself: run_netns_online_test.sh
#            friendsplayers)
#   h3friends the Halo 3 FRIENDS screen (menus/halo3/friends.xml; port/linux/src/p2p_friends.c)
#            over a save's friends made at test time (run_p2p_friends_test.sh seed: two
#            friends, a request, a request sent, a blocked player; shown online,
#            HALO_TEST_FRIENDS_SHOW=online), internet play off: FRIENDS on the main menu, the
#            code and every entry listed; ADD FRIEND types a code (queued until online); APPEAR
#            OFFLINE and FRIENDS CAN JOIN switched; Square declines the request (nothing sent);
#            Cross on the joinable friend says go online first; Triangle's REMOVE FRIEND
#            removes them; the list written and read back by a second start
#   h3party  the Halo 3 PARTY screen (menus/halo3/party.xml; port/linux/src/p2p_party.h):
#            FRIENDS' PARTY row opens it (internet play off: no party, go online
#            first), B goes back to FRIENDS; a friend's options offline have no
#            INVITE TO PARTY (the party's own steps: run_netns_online_test.sh party)
#   h3friendsbackup FRIENDS' FRIEND CODE BACKUP (port/linux/src/p2p_friends_backup.inc): a save's
#            friends (as h3friends'); BACK UP FRIEND CODE, the name and the password twice typed
#            (HALO_TEST_TEXT_INPUT), WITH FRIENDS LIST: the file written. A second start on a new
#            save (another friend code) with that file: RESTORE FRIEND CODE, a wrong password
#            said and nothing changed, then the right one: CONFIRM, the first start's code and
#            friends; no log holds the password
#   h3friendspanel the lobbies' FRIENDS panel: the multiplayer lobby shows who is online
#            and the requests, Triangle opens the FRIENDS screen, B back; the Forge lobby too
#   h3partyplates  the party as Halo 3 shows it (the owner: "when they join your party it
#            should be another nameplate added"): a made-up party (HALO_TEST_PARTY_PLATES)
#            of the player and two, joining and leaving on the clock: no plate alone; on the
#            main menu Bravo's nameplate (LEADER) under the player's, then Charlie's under
#            it; MULTIPLAYER: the lobby shows them too; Charlie leaves (one plate), then
#            Bravo (none). The lobby's FRIENDS panel lists no party rows (the plates do).
#            The main menu's live servers (five made up) move down under the plates.
#            Frames 00-alone 01-party 02-lobby 03-left (HALO_TEST_SHOTS keeps them)
#   h3mppause the Halo 3 multiplayer pause (menus/halo3/mppause.xml, standing for the Xbox's
#            1p_pause_game) in a local Team Slayer game on Blood Gulch with an offline bot
#            (HALO_NETWORK_TEST local, HALO_TEST_PROFILE's profile played; skipped without):
#            Start opens it over the game - RESUME, PLAYERS, SETTINGS, END GAME (the host's),
#            no CHANGE TEAMS -, the game not paused (its ticks go on); SETTINGS edits the
#            profile's controls (right: the stick layout, Circle saves and plays it); END GAME
#            asks, Circle back; RESUME closes it; as in a party (HALO_TEST_MP_PAUSE_PARTY) PARTY
#            shows, five rows, and asks for the settings panel's Party page; Menu style Halo
#            CE: the Xbox's own pause, nothing of Halo 3's (system link host and joiner:
#            run_netns_online_test.sh h3mppause)
#   h3carnage the Halo 3 CARNAGE REPORT (menus/halo3/carnage.xml, port/linux/game/carnage_report.c)
#            at a local game's end (HALO_NETWORK_TEST local, offline bots, HALO_TEST_PROFILE's
#            profile played; skipped without): Slayer on Chill Out to 3 with three bots, Team
#            Slayer on Blood Gulch to 3 with five, CTF on Blood Gulch and Oddball on Chill
#            Out with five, each ended at 45 s (debug.network_test_end, as a time limit
#            ends one). Each: the report opens in the game's own report's place
#            once the scores come up (the pad's "carnage" step), its rows ranked as the game
#            ranks them - places in order, a place shared only by players alike in score,
#            kills, deaths and assists, the first the winner; a team game's team rows first
#            the winning team's, each team's kills its players' -, this machine's player
#            marked; R shows DETAILS, L SUMMARY again; Circle on the
#            host's report leaves nothing; Cross goes on to the lobby (CTF's DETAILS: GRABS and
#            RETURNS; Oddball's BALL TIME a time, "0:14"). Then with
#            menus.carnage_report classic (HALO_CARNAGE_REPORT) and with Menu style Halo CE:
#            no Halo 3 report, the game's own. HALO_TEST_SHOTS: carnage_summary.bmp and
#            carnage_details.bmp of each
#   h3friendsfit the lobbies' FRIENDS panel's line with no one to show ("Go online to see
#            your friends", cut short in the name's column on insider.14) in its own
#            box across the panel, in one or two lines, never cut: the Forge lobby in
#            English and in Spanish (HALO_TEST_LANG_DIR: the Vita's translations on Linux)
#   h3oddrows the public lists' odd listings (HALO_TEST_BROWSER_ODD: the owner's
#            photo of beta.7's browser, Oct 18 2026) in the Halo 3 games browser
#            and OpenCE's (Menu style Halo CE): a name that names nothing shows
#            its host's player's, one of other alphabets "?" for what the font
#            lacks, co-op its difficulty and level (or "-"), every missing field
#            "-", and a name or map wider than its column cut with "..."
#   profilepaths every multiplayer entry path asks for the profile first: Halo CE's SERVER
#            BROWSER, SYSTEM LINK, DIRECT LINK (a code: the System Link screen) and CREATE GAME
#            SYSTEM LINK open the Xbox's profile screen; Halo 3's START GAME (SYSTEM LINK, ONLINE's
#            Create Game, AD HOC), BROWSE GAMES (SYSTEM LINK, ONLINE, AD HOC) and JOIN WITH CODE
#            open SELECT PROFILE (halo3/join.xml)
#   browserrows OpenCE's server browser (Menu style Halo CE) with 16 made-up games
#            of varied names (HALO_TEST_BROWSER_NAMES), PC ones among them: every row
#            named, and the glyph cache never going round each frame (the owner's
#            photo of insider.10, Oct 18 2026: only the first row's name drawn on
#            the Vita, each row below one column fewer)
#   h3livebrowse  BROWSE GAMES pressed as the live lobby goes live (SYSTEM LINK,
#            HALO_TEST_PROFILE): the presses kept, the games browser, the live
#            game closed, no SELECT PROFILE for the profile it played with
#   browserreopen  the Halo 3 games browser opened and closed again and again
#            (the testers, insider.15: names lost, then a crash), 34 made-up
#            games of varied names, rules and players, 12 times more; the
#            widgets' memory held as it opens the same every time, never full
#            (halo.log "memory: the widget_memory_pool is full"), at most
#            three quarters of it in use
#   h3lan    the Halo 3 LAN browser (halo3/network.xml) with 14 made-up games of both
#            platforms (HALO_TEST_BROWSER_GAMES, HALO_TEST_BROWSER_PC): BROWSE
#            GAMES on SYSTEM LINK, the profile, then the LAN browser in place of
#            the Xbox's System Link list, no game locked; down to the third
#            game, A: it is not listed by the game (a made-up one), and says
#            so; Y sorts by name, Square looks again, a tap on the first game's
#            row (under VITA GAMES) presses A on it, a tap on Back goes back to the profile; then with no games: "No games found on your network"
#   h3groups the browsers' groups (the owner, Oct 19 2026): Halo 3's games
#            browser (SYSTEM LINK) and OpenCE's (online), 14 made-up games of
#            both platforms: VITA GAMES first, then PC GAMES, each with its
#            count; the first game the fullest Vita game, never a header; the
#            D-pad's right and left: Vita games, PC games, all games
#            (network.browser_filter), the first game of each; at the Vita's
#            picture (HALO_GL_VITA_DISPLAY), its frames with HALO_TEST_SHOTS
#   h3adhoc  ad hoc play from the Halo 3 lobby: with the connection Ad hoc
#            (HALO_NET_ADHOC) the lobby opens on AD HOC; BROWSE GAMES out of the
#            ad hoc group opens the joining dialog, the system's dialog asked
#            for, joined (HALO_ADHOC_TEST=join), then the profile and the LAN
#            browser (AD HOC GAMES, "No games found near you"); START GAME with
#            the join failing (HALO_ADHOC_TEST=fail) says so, A asks again, B
#            goes back to the lobby; with the connection Same Wi-Fi, NETWORK:
#            AD HOC switches to ad hoc play live (the system's dialog asked
#            for), BROWSE GAMES opens the ad hoc LAN browser, and SYSTEM LINK
#            leaves ad hoc play again
#   pctext   the PC build's text box (port/linux/src/pc_text_entry.c), typed into through
#            SDL's own events (HALO_TEST_KEYS) and the gamepad's on-screen keyboard: a new
#            profile's name by the keyboard (the editing keys, a paste, UTF-8 left out, 11 at
#            most; no key reaching the menu behind) and by a gamepad alone; Escape and Y
#            cancel (and nothing else); JOIN WITH CODE's code; a locked game's password
#   pcclassic  Menu style Halo CE on the PC build: a new profile's name typed in the PC build's
#            text box (not the Xbox's on-screen keyboard) by the keyboard and by a gamepad's
#            on-screen keyboard, the profile made and the campaign going on; Escape cancels
#   pcnoadhoc  the PC build (HALO_TEST_PLATFORM=pc) has no AD HOC: the NETWORK picker
#            goes SYSTEM LINK, OFFLINE and back up to SYSTEM LINK without it; the connection Ad hoc in
#            the settings is System Link there, ad hoc play never started; the
#            campaign lobby's NETWORK has no AD HOC CO-OP (HALO_TEST_PROFILE)
#   h3adhoccoop  AD HOC CO-OP from the Halo 3 campaign lobby (HALO_TEST_PROFILE;
#            skipped without), the connection Ad hoc and the join answered
#            (HALO_ADHOC_TEST=join): START GAME, the joining dialog, then the
#            level hosted as co-op and the game's lobby open by themselves; with
#            the connection Same Wi-Fi AD HOC CO-OP switches to ad hoc play live
#   h3coopguide  the settings panel's Host co-op campaign
#            (HALO_SYSTEM_LINK_TEST=campaign): the Campaign screen opens, the
#            Halo 3 campaign lobby behind it set to SYSTEM LINK CO-OP
#   h3mods   the Halo 3 MOD MANAGER (menus/halo3/mods.xml, the main menu's) over
#            content made at test time (mods_data: a modded Xbox map made from
#            Blood Gulch, a corrupt file, a Custom Edition header with nothing
#            after it, an MCC mod whose map is corrupt, with links out of its
#            folder): each listed with its kind, size and the game's check
#            (the corrupt ones failed, said why, no crash); mygulch turned off
#            (out of the map lists, the bots' playlist and map sharing) and on
#            again; its details; Square on broken.map, Circle keeps it, Square
#            and YES deletes it (only it: the maps folder's other files, the
#            links' targets kept); the mod's folder deleted, the links in it
#            gone, what they pointed at kept
#   profileio  the profile read from memory, not its file, every frame (insider.15's testers:
#            SELECT PROFILE's plates and the live lobby opened blam.sav 3-45 times a frame on the
#            game thread, each behind a map copy's writes on the card): HALO_TEST_PROFILE's
#            profile, a Vita-like card (HALO_IO_THROTTLE_KBPS, _WRITE_KBPS), the game thread's file
#            calls logged (HALO_IO_TRACE_GAME): the live lobby on SYSTEM LINK (Battle Creek copied
#            to the cache meanwhile), then ONLINE not live, START GAME: SELECT PROFILE's plates;
#            blam.sav and z:\last*.txt each opened once at most by the game thread
#   h3livelobby  the live lobby (HALO_TEST_PROFILE): MULTIPLAYER on SYSTEM LINK opens its game
#            once its scene stands, START GAME its game's lobby at once (no profile asked, no
#            other server), B back to the lobby (the game kept), B closes it; ONLINE with its
#            brokers unreachable never live, START GAME as before; the lobby's scene whole
#            behind the game's lobby, never a flag's cloth left (screenshots, the Vita's picture);
#            going live keeps the lobby as it looked (LadyCassie, insider.20): the scene not made
#            again, the nameplate kept (no other machine in the game), and no copy of the
#            playlist's first map (Carousel) begun as its server starts on the lobby's map
#   h3modspicker  MOD MANAGER, mygulch off, then MULTIPLAYER, MAP: the lobby's map
#            picker (the list START GAME hosts from) leaves it out; again with
#            it off (HALO_MAPS_DISABLED) and turned on in MOD MANAGER: the
#            picker offers it
#   h3modstouch  taps: MOD MANAGER on the main menu, mygulch's row (off), the
#            screen's Back
#   pausesave  the campaign pause's save and quit writes the save and the level
#           resumes from it (insider.19, dalmor's video: the classic pause's SAVE
#           LAST CHECKPOINT did nothing): The Truth and Reconciliation's checkpoint
#           at tick 600, Start, the save and quit row - Menu style Halo CE the
#           Xbox's SAVE AND QUIT (its saving screen), Halo 3 the Halo 3 pause's
#           SAVE AND QUIT -: savegame.bin (HALO_TEST_PERSISTENT_DIR) changed on
#           disk, debug.txt's "saved the last checkpoint", the main menu again;
#           started again, the level resumes at that checkpoint's tick. With a
#           Custom Edition campaign map (HALO_TEST_CE_CAMPAIGN, e.g. a converted
#           Ruby's a30; skipped without), Halo PC's pause: SAVE LAST CHECKPOINT
#           taken out, the fourth row down QUIT, which writes savegame_custom.bin
#           (the stock savegame.bin byte for byte as it was) and the map resumes
#           from it
#   h3gametypes  the Halo 3 GAME TYPES (menus/halo3/gametypes.xml), HALO_TEST_PROFILE's
#            profile, the lobby live on SYSTEM LINK: EDIT GAME TYPES lists the game's 26 game
#            types; a new one from SLAYER, named (HALO_TEST_TEXT_INPUT), its kills to win,
#            respawn time, shields and vehicle set changed, SAVE: blam.lst and SaveMeta.xbx as
#            the Xbox writes them, the lobby's GAME on it and its live game hosted with its rules,
#            GAME's picker, START GAME; COPY, RENAME and DELETE (its question); one of the game's
#            own refused deletion; DISCARD CHANGES leaves the editor. Then the game plays by it
#            (a local game of it, HALO_NETWORK_TEST saved=: its rules logged, Ghosts alone), Menu
#            style Halo CE's Xbox screens open it, and screenshots in Spanish and with the
#            keyboard's icons (HALO_TEST_GAMETYPES_SHOTS: where the screenshots go)
#   h3offlineend  a local game over (the network test's, ended at 30 s): back in the Halo 3
#            multiplayer lobby on OFFLINE, not split screen's postgame map list, the game let
#            go; START GAME there starts a new local game (HALO_TEST_PROFILE; skipped without)
#   h3offline  the Halo 3 multiplayer lobby on OFFLINE (HALO_TEST_PROFILE's profile; skipped
#            without): NETWORK OFFLINE, GAME ODDBALL, MAP CHILL OUT, START GAME: split screen's
#            SELECT PROFILE (A joins, A the profile, A), then the local game started at once with
#            the lobby's GAME and MAP and the settings' offline bot (HALO_BOTS) - none of the
#            Xbox's split screen map list, game type list or lobby -; in the game the pause says
#            Oddball on Chill Out. Then a saved game type: Infection copied in GAME TYPES (Vita
#            Infect), the lobby's GAME on it, Blood Gulch, START GAME: the local game plays
#            Infection. Then alone (no bots): started (a Vita's build, --linux-net-vita: one
#            player is enough) or, where a local game needs two, split screen's own lobby after
#            5 s (HALO_TEST_SHOTS: the pictures)
#   msgboxes  the Halo 3 dialog (menus/halo3/dialog.xml; the owner, Oct 21 2026: one
#            Halo 3 look for every message box and prompt) with Menu style Halo 3: on the
#            main menu error 6 (CONNECTION LOST: Circle closes it), error 31 (DAMAGED FILE:
#            Cross on OK), the error that offers to quit (STORAGE FULL: CONTINUE), the
#            platform's message (map sharing's: OK) and two questions (YES, then Circle:
#            NO) - HALO_TEST_COMMANDS' @error, @abort, @say, @ask -, each answered as
#            pressed (@answer) and none of the Xbox's boxes; Menu style Halo CE: the Xbox's
#            boxes, the platform's own for the message and question; then (with
#            HALO_TEST_PROFILE) a level's pause: REVERT TO CHECKPOINT's question
#            (Circle: back to the pause screen; again, REVERT TO CHECKPOINT: reverted),
#            RESTART MISSION's (Circle: not restarted), SAVE AND QUIT's "Saving" boxes and
#            the main menu; and Menu style Halo CE's pause: the Xbox's question
#            (HALO_TEST_SHOTS: each dialog's picture)
#   ceplusmain  Menu style Halo CE's main menu (menus/classic/main.xml: the Xbox's list with the
#           port's rows, classic_patch): FORGE, FRIENDS, FILE SHARE and MOD MANAGER under SETTINGS,
#           each opens its screen (the Halo 3 Forge lobby, FRIENDS, FILE SHARE, MOD MANAGER) and B
#           comes back to the Xbox's main menu; Menu style Halo 3: the map's own list, its main menu
#           the Halo 3 one
#   cepluspregame  the Xbox's pregame (Menu style Halo CE, HALO_TEST_PROFILE; skipped without) of a
#           system link game hosted on Blood Gulch (CREATE GAME SYSTEM LINK, the profile, Y, the map,
#           the game type) with map variants of it (forge_files_test --write): its line under the
#           Xbox's buttons; the right trigger opens LOBBY OPTIONS (menus/classic/pregame.xml), no
#           INVITE FRIENDS on system link; MAP VARIANT steps through the map's variants (hosted, the
#           joiners asked to play it); PLAYERS opens the players' screen, B back to LOBBY OPTIONS,
#           B to the pregame
#   cepluspause  the Xbox's multiplayer pause with the port's rows (menus/classic/pause.xml, a local
#           Team Slayer game on Blood Gulch; HALO_TEST_PROFILE, skipped without): RESUME GAME focused,
#           PLAYERS opens the players' screen, B; PARTY asks for the settings panel's Party page;
#           RESUME GAME closes it, the game going on
#   ceplusforge  FORGE on Menu style Halo CE's main menu (HALO_TEST_PROFILE; skipped without): the
#           Forge lobby's NETWORK on SYSTEM LINK, START FORGE: the profile, the network Forge game
#           hosted and the Xbox's pregame (no live lobby with the Xbox's menus)
#   pause   Campaign from the main menu (a new profile, Normal), the level's
#           pause menu, Quit to Main Menu: a game map's widget runs its
#           function and is back at the main menu, and asked about deleting
#           a profile (HALO_TEST_WIDGET_FUNCTION=76, never run) the main
#           menu's widget may, a game map's may not
#           (ui_widget_event_handler_functions.c ui_widget_function_denied)
#
# Each must log no "is not a tag index" (a tag of ours read as the empty
# one), no assertion or exception, and exit by itself.
#
#   run_menus_test.sh [CASE...]      (default: fresh level xbox pause)
#   HALO_TEST_VITA        the harness (default build/linux/halo of this tree)
#   HALO_TEST_DATA        a folder with the game's maps folder (the Xbox maps)
#   HALO_TEST_DATA_MENUS  the same with Halo PC's bitmaps.map and loc.map in
#                         its maps folder (never in the repository; skipped
#                         when missing)
#   HALO_MENUS_FOLDER     the menus' XML (default this tree's
#                         port/vita/app0/menus: the game looks beside its
#                         binary, <tree>/build/linux, and a copy of the
#                         harness elsewhere found none and kept the Xbox's
#                         Multiplayer screen)
#   HALO_TEST_SHOTS       a folder for each case's frames (HALO_SCREENSHOT_DIR, every
#                         60 frames, in <folder>/<case>); none by default
#   HALO_TEST_OUT         where the logs go (deleted after a pass when the test made it; HALO_TEST_KEEP=1 keeps them)
#   HALO_TEST_PROFILE     a player profile's blam.sav (never in the repository; the cases that
#                         need one skipped without); h3campaign and h3pause play
#                         blam-unlocked.sav from the same folder when it is there (every
#                         level open)
# On a gxm-null harness (configure.py --linux-d3d gxm-null) the taps, the saved frames and the
# PC text box's typing are SKIPs: it draws nothing to tap, save or type into.
set -u
# (Bots online, bots.online: full by default, which fills a game hosted
# online with bots; off here unless a run asks: run_netns_online_test.sh hostbots)
export HALO_BOTS_ONLINE=${HALO_BOTS_ONLINE:-off}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
binary=$(readlink -f "${HALO_TEST_VITA:-$root/build/linux/halo}")
data=${HALO_TEST_DATA:-$root/../data2276}
menus_data=${HALO_TEST_DATA_MENUS:-$root/../triage/menus/data}
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/halo_menus_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
all_cases="fresh level xbox pause h3renamed pausesave h3probe h3classic h3xbox h3broken h3style h3online h3touch h3campaign h3resume h3pause h3mp h3browser h3create h3mptouch h3coop h3coopguide h3scenes h3profiles h3proftouch h3customize h3customrows h3models h3nameplate h3damaged h3mods h3modspicker h3modstouch h3mpcustom h3lan h3adhoc h3adhoccoop h3oddrows h3lobbyscenes h3modelsmany h3friends h3friendsbackup h3friendspanel h3partyplates h3party h3partyonline browserprofile h3players browserrows profilepaths h3mppause h3friendsfit h3hostonline h3livelobby h3groups browserreopen h3livebrowse profileio h3vitapc h3createwho h3carnage h3gametypes h3offline h3offlineend msgboxes ceplusmain cepluspregame cepluspause ceplusforge"
# (the PC build's own, asked for by name: not in the default list)
pc_cases="pctext pcclassic pcnoadhoc"
cases=${*:-$all_cases}
ce_maps=${HALO_TEST_CE_MAPS:-$root/../custom-maps-dl}
status=0
fail() { echo "FAIL ($1): $2"; status=1; }
# (a gxm-null harness, configure.py --linux-d3d gxm-null: the Vita's renderer recording into
# nothing - no picture to save, tap or type into (the touch, screenshot and keyboard checks are
# the GL build's: SKIP here, not FAIL))
gxm_null=0
grep -aq "HALO_CDRAM_HIDDEN_MB" "$binary" 2>/dev/null && gxm_null=1
skip_null() { # CASE WHAT: TRUE (and SKIP said) on a gxm-null harness
	[ $gxm_null = 1 ] || return 1
	echo "--- $1"; echo "SKIP ($1): $2 (a gxm-null harness: the GL build's check)"
	return 0
}
# (the campaign cases' profile: every level unlocked - blam-unlocked.sav beside HALO_TEST_PROFILE's
# blam.sav when there is one, else HALO_TEST_PROFILE itself)
campaign_profile=${HALO_TEST_PROFILE:-}
[ -n "$campaign_profile" ] && [ -r "$(dirname "$campaign_profile")/blam-unlocked.sav" ] &&
	campaign_profile=$(dirname "$campaign_profile")/blam-unlocked.sav

if [ ! -r "$menus_data/maps/bitmaps.map" ] || [ ! -r "$menus_data/maps/loc.map" ]; then
	echo "SKIP: no Halo PC bitmaps.map and loc.map in $menus_data/maps (HALO_TEST_DATA_MENUS)"
	exit 0
fi
export HALO_MENUS_FOLDER=${HALO_MENUS_FOLDER:-$root/port/vita/app0/menus/}
if [ ! -r "$HALO_MENUS_FOLDER/menus.txt" ]; then
	echo "SKIP: no menus.txt in $HALO_MENUS_FOLDER (HALO_MENUS_FOLDER)"
	exit 0
fi
mkdir -p "$out"

run() { # NAME DATA SECONDS PAD [VAR=value...]
	# (Menu style Halo CE unless the case says otherwise: the classic cases' pads walk
	# Halo CE's menus; Halo 3 has been the default since 1.1.1)
	local name=$1 folder=$2 seconds=$3 pad=$4
	shift 4
	rm -rf "$out/$name"
	mkdir -p "$out/$name/data" "$out/$name/save"
	# (a player profile of the tests', as the Vita keeps it: HALO_TEST_PROFILE's blam.sav)
	if [ -n "${profile_from:-}" ]; then
		mkdir -p "$out/$name/save/u/UDATA/869079979327"
		cp "$profile_from" "$out/$name/save/u/UDATA/869079979327/blam.sav"
		# (and its save folder's SaveMeta.xbx, beside it: without it the game finds no profile)
		[ -r "$(dirname "$profile_from")/SaveMeta.xbx" ] &&
			cp "$(dirname "$profile_from")/SaveMeta.xbx" "$out/$name/save/u/UDATA/869079979327/"
	fi
	# (or a whole save folder of the tests', as another case's run left it)
	[ -n "${save_from:-}" ] && cp -r "$save_from"/. "$out/$name/save/"
	ln -sfn "$(cd "$folder" && pwd)/maps" "$out/$name/data/maps"
	# (a mods folder of the case's: mods_data's, copied with its links as links)
	[ -n "${mods_from:-}" ] && cp -a "$mods_from" "$out/$name/data/mods"
	[ -n "${init_line:-}" ] && printf '%s\n' "$init_line" > "$out/$name/data/init.txt"
	local shots=()
	if [ -n "${HALO_TEST_SHOTS:-}" ]; then
		rm -rf "$HALO_TEST_SHOTS/$name"
		mkdir -p "$HALO_TEST_SHOTS/$name"
		shots=("HALO_SCREENSHOT_DIR=$HALO_TEST_SHOTS/$name" HALO_SCREENSHOT_EVERY=60)
	fi
	(cd "$out/$name" && exec env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen HALO_DATA_ROOT="$out/$name/data" \
		HALO_SAVE_ROOT="$out/$name/save" HALO_NO_VSYNC=1 HALO_FRAME_CAP=30 HALO_EXIT_AFTER="$seconds" HALO_FULLSCREEN=0 \
		HALO_HIDDEN_WINDOW=1 HALO_NO_AUDIO=1 HALO_TICK_THREAD=1 HALO_UPDATE_AUTO=false HALO_NET_ONLINE=false \
		HALO_UI_LOG=1 HALO_MENU_STYLE=classic "HALO_TEST_PAD=$pad" "${shots[@]}" "$@" timeout -k 5 $((seconds + 90)) "$binary" > "$out/$name/run.log" 2>&1)
	echo $? > "$out/$name/exit"
}

# (the main menu: down and up; Multiplayer, back, Multiplayer again; Create
# Game Internet (the fourth item), back, back)
pad="wait:150:2000 down:150:800 up:150:800 down:150:2500 a wait:150:3000 b wait:150:2500 a wait:150:3000"
pad="$pad down:150:600 down:150:600 down:150:600 a wait:150:2500 b wait:150:2000 b wait:150:2000"

check() { # NAME SCREENS_WANTED
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "menus:|ui: screen" "$log" | head -16
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqi "is not a tag index" "$log" "$debug" 2>/dev/null && fail "$name" "a tag was read as the empty one"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	grep -aq "menus: .*menus.txt cannot be read" "$log" && fail "$name" "the menus' XML was not found (HALO_MENUS_FOLDER)"
	if [ "$2" = pc ]; then
		[ "$(grep -ac 'ui: screen pc.mp.screen' "$log")" -ge 3 ] || fail "$name" "OpenCE's Multiplayer screen did not open twice"
		grep -aq "ui: screen pc.mp.offline" "$log" || fail "$name" "Create Game Internet did not say internet play is off"
		grep -aq "menus: the Halo PC pictures and text read" "$log" || fail "$name" "the Halo PC pictures were not read"
	else
		grep -aq "menus: OpenCE" "$log" && fail "$name" "OpenCE's screens were added without the Halo PC files"
		[ "$(grep -ac 'ui: screen ui.shell.main_menu.multiplayer_type_select.multiplayer_type_select_screen' "$log")" -ge 2 ] ||
			fail "$name" "the Xbox's Multiplayer screen did not open twice"
	fi
}

# (pause: Campaign, a new profile, Normal, the level started; its cinematic
# skipped, the pause menu, down to Quit to Main Menu, A)
pause_pad=${HALO_TEST_PAUSE_PAD:-"a:150:3000 start:150:3000 wait:150:6000 a:150:45000 start:150:3000 down:150:800 down:150:800 down:150:800 a:150:3000 wait:150:8000"}
# (pausesave: in the level, Start, down three - the save and quit row, or Halo PC's
# QUIT once SAVE LAST CHECKPOINT is out -, A)
pausesave_pad="wait:150:1000 level:150:30000 start:150:4000 down:150:800 down:150:800 down:150:800 a:150:10000 wait:150:3000"
# pausesave_run NAME STYLE LEVEL FILE DOWNS: the save written by the pause's row, then
# resumed from it in a second run
pausesave_run() {
	local name=$1 style=$2 level=$3 file=$4 pad=$pausesave_pad folder=$pausesave_maps
	[ "$5" = 4 ] && pad=${pad/start:150:4000 /start:150:4000 down:150:800 }
	local seed=$out/${name}_seed dir=u/UDATA/TEST log debug saved resumed before after
	rm -rf "$seed"
	mkdir -p "$seed/$dir"
	# (an old save of the same size, all zeroes, to be overwritten; and the
	# other save, which must be left alone)
	head -c 16777216 /dev/zero > "$seed/$dir/$file"
	printf 'the other save' > "$seed/$dir/$( [ "$file" = savegame.bin ] && echo savegame_custom.bin || echo savegame.bin)"
	before=$(md5sum < "$seed/$dir/$file")
	save_from=$seed run "$name" "$folder" 120 "$pad" "HALO_TEST_COMMANDS=60:map_name $level;L150:@skip;L600:game_save_totally_unsafe" \
		HALO_MENU_STYLE="$style" HALO_CUSTOM_EDITION=1 'HALO_TEST_PERSISTENT_DIR=u:\UDATA\TEST\'
	log=$out/$name/run.log debug=$out/$name/data/debug.txt
	echo "--- $name"
	grep -aE "ui: screen|save and quit|campaign save" "$log" | head -12
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	saved=$(grep -ao "save and quit: .* at tick [0-9]* written to $file" "$log" | grep -o "tick [0-9]*" | head -1)
	[ -n "$saved" ] || fail "$name" "the pause's row did not save and quit into $file"
	grep -aq "saved the last checkpoint to persistent storage" "$debug" || fail "$name" "debug.txt: the save was not written"
	after=$(md5sum < "$out/$name/save/$dir/$file")
	[ "$(stat -c %s "$out/$name/save/$dir/$file")" = 16777216 ] && [ "$before" != "$after" ] ||
		fail "$name" "$file did not change on disk"
	[ "$(cat "$out/$name/save/$dir/$( [ "$file" = savegame.bin ] && echo savegame_custom.bin || echo savegame.bin)")" = 'the other save' ] ||
		fail "$name" "the other save was changed"
	# (the main menu after the save; Halo PC's QUIT goes there at once, its press
	# still held as it opens on the harness, which may open Campaign after it)
	local menu='ui.shell.main_menu.main_menu'
	[ "$style" = halo3 ] && menu='pc.h3.main.screen'
	awk -v menu="ui: screen $menu" '/save and quit: .* written to/ { saved = 1 } saved && $0 ~ menu { found = 1 }
		END { exit !found }' "$log" || fail "$name" "not back at the main menu after the save"
	# (started again on that save, the same difficulty: resumed at the checkpoint)
	save_from=$out/$name/save init_line="map_name $level" run "${name}_resume" "$folder" 25 "wait:150:1000" \
		HALO_MENU_STYLE="$style" HALO_CUSTOM_EDITION=1 'HALO_TEST_PERSISTENT_DIR=u:\UDATA\TEST\'
	log=$out/${name}_resume/run.log
	grep -aE "campaign save" "$log" | head -3
	resumed=$(grep -ao "campaign save: resumed at tick [0-9]*" "$log" | grep -o "tick [0-9]*" | head -1)
	[ -n "$saved" ] && [ "$resumed" = "$saved" ] || fail "$name" "started again, the level did not resume at the checkpoint ($saved; resumed: ${resumed:-no})"
}

check_pause() {
	local log=$out/pause/run.log
	echo "--- pause"
	grep -aE "ui: screen|widgets: |test pad: " "$log" | head -40
	[ "$(cat "$out/pause/exit")" = 0 ] || fail pause "exit $(cat "$out/pause/exit")"
	grep -aqiE "assert|exception|halt" "$log" && fail pause "an assertion, exception or halt"
	grep -aq "widgets: function 76 from the main menu's widget allowed" "$log" ||
		fail pause "the main menu's widgets may not delete a profile"
	grep -aq "widgets: function 76 from a game map's widget refused" "$log" ||
		fail pause "a game map's widget may delete a profile"
	[ "$(grep -ac 'ui: screen ui.shell.main_menu.main_menu' "$log")" -ge 2 ] ||
		fail pause "the pause menu's Quit to Main Menu did not go back to the main menu"
}

# the Halo 3 set (menus/halo3): the screen the main menu became, or the Xbox's
check_h3() { # NAME halo3|classic
	local name=$1 log=$out/$1/run.log debug=$out/$1/data/debug.txt
	echo "--- $name"
	grep -aE "menus:|ui: screen" "$log" | head -16
	[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
	grep -aqi "is not a tag index" "$log" "$debug" 2>/dev/null && fail "$name" "a tag was read as the empty one"
	grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
	if [ "$2" = halo3 ]; then
		grep -aq "menus: Halo 3 screen .*h3/.*/screen for ui.shell.main_menu.main_menu" "$log" ||
			fail "$name" "the Halo 3 screen did not stand for the main menu"
		grep -aq "ui: screen pc.h3.*screen" "$log" || fail "$name" "the Halo 3 screen did not open"
	else
		grep -aq "ui: screen pc.h3" "$log" && fail "$name" "a Halo 3 screen opened with Menu style Halo CE"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$log" || fail "$name" "the Xbox's main menu did not open"
	fi
}
h3pad="wait:150:3000"
# (a profile's file, blam.sav: name, colour, flags, the levels' flags, the last level, the
# controller settings - source/saved games/player_profile.h - as "name color stick buttons look
# invert flight autocenter vibration_off levels")
profile_fields() {
	python3 - "$1" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
name = data[0:24].decode('utf-16-le').split('\0')[0]
color, flags = struct.unpack('<hH', data[24:28])
levels = data[28:38].hex()
last, = struct.unpack('<h', data[38:40])
buttons, stick, look, invert, vibration, flight, autocenter, help_off = data[40:48]
print('%s|%d|%d|%d|%d|%d|%d|%d|%d|%s|%d' % (name, color, stick, buttons, look, invert, flight, autocenter, vibration,
	levels, last))
PY
}
# (no Xbox profile screen opened: the campaign's, the settings' and their edit screens, the
# Xbox's keyboard's new profile)
h3_no_xbox_profiles() { # NAME
	grep -aE "ui: screen ui.shell.main_menu.(load_campaign_player_profile|player_profiles_select|settings_select|new_campaign)" \
		"$out/$1/run.log" && fail "$1" "an Xbox profile screen opened with Menu style Halo 3"
}
# (internet play on, nothing reached: its brokers, STUN and relays TEST-NET addresses)
h3net="HALO_NET_ONLINE=true HALO_NET_BROKERS=198.51.100.1:1883 HALO_NET_STUN=198.51.100.1:3478"
h3net="$h3net HALO_NET_RELAYS=198.51.100.1:47320 HALO_NET_ALLOW_UPNP=false HALO_DISCORD_APPLICATION="
# (FRIENDS on the main menu: down six, A)
h3friendspad="wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000"
# a save folder's friends (run_p2p_friends_test.sh seed: made at test time, never in the repository)
friends_seed() { # FOLDER
	rm -rf "$1"
	TMPDIR="$out" sh "$here/run_p2p_friends_test.sh" seed "$1" > "$out/friends_seed.log" 2>&1 ||
		{ fail friends "the friends could not be made (friends_seed.log)"; cat "$out/friends_seed.log"; }
}
# (the main menu: down to MULTIPLAYER, A: the lobby, its NETWORK row focused)
h3mppad="wait:150:3000 down:150:700 a wait:150:3000"

# the mod manager's content, made at test time in FOLDER (never in the
# repository): FOLDER/maps, links to the menus data's maps folder's files, a
# modded Xbox map (Blood Gulch's copy, its header named mygulch, as
# run_netns_online_test.sh makes one), a corrupt file (broken.map) and a
# Custom Edition header with nothing after it (cefake.map); FOLDER/mods, a mod
# (TestMod: its CampaignInfo.json, a corrupt maps/a10.map, links out of it to
# FOLDER/outside); FOLDER/outside, what those links point at
mods_data() { # FOLDER
	local folder=$1 file
	rm -rf "$folder"
	mkdir -p "$folder/maps" "$folder/mods/TestMod/maps" "$folder/mods/TestMod/assets" "$folder/outside"
	for file in "$menus_data"/maps/*; do
		ln -s "$(readlink -f "$file")" "$folder/maps/$(basename "$file")"
	done
	cp --reflink=auto "$(readlink -f "$menus_data/maps/bloodgulch.map")" "$folder/maps/mygulch.map"
	printf 'mygulch' | dd of="$folder/maps/mygulch.map" bs=1 seek=32 conv=notrunc status=none
	dd if=/dev/zero of="$folder/maps/mygulch.map" bs=1 seek=39 count=25 conv=notrunc status=none
	head -c 70000 /dev/urandom > "$folder/maps/broken.map"
	python3 -c 'import sys; h = bytearray(2048); h[0:4] = b"daeh"; h[4:8] = (609).to_bytes(4, "little"); h[0x60] = 1; open(sys.argv[1], "wb").write(h)' \
		"$folder/maps/cefake.map"
	head -c 5000 /dev/urandom > "$folder/mods/TestMod/maps/a10.map"
	printf '{"Title": "Test Mod"}' > "$folder/mods/TestMod/CampaignInfo.json"
	echo kept > "$folder/outside/sentinel.txt"
	ln -s "$folder/outside" "$folder/mods/TestMod/assets/outside"
	ln -s "$folder/outside/sentinel.txt" "$folder/mods/TestMod/assets/linked.txt"
}
# (the main menu: down four to MOD MANAGER, A: its list, broken.map first)
h3modspad="wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:4000"
# (MOD MANAGER, mygulch's row - two down -, A; B: the main menu, on MOD MANAGER; up three to MULTIPLAYER, A;
# down two to MAP, A: the map picker (the lists custom maps are offered in), B)
h3pickerpad="$h3modspad down:150:500 down:150:500 a wait:150:2000 b wait:150:3000 up:150:500 up:150:500 up:150:500 a wait:150:3000 down:150:500 down:150:500 a wait:150:3000 b wait:150:3000"
# (Menu style Halo CE: Multiplayer, down four to CREATE GAME INTERNET, A: OpenCE's Server Setup)
cmp_create="wait:150:3000 down:150:800 a wait:150:3000 down:150:500 down:150:500 down:150:500 a wait:150:4000"

for case in $cases; do
	case $case in
	fresh)
		run fresh "$menus_data" 40 "$pad"
		check fresh pc ;;
	level)
		init_line='map_name levels\a10\a10' run level "$menus_data" 90 "$pad" "HALO_TEST_COMMANDS=L150:@quit"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$out/level/run.log" || fail level "the level did not go back to the main menu"
		check level pc ;;
	pausesave)
		pausesave_maps=$out/pausesave_maps
		rm -rf "$pausesave_maps"
		mkdir -p "$pausesave_maps/maps"
		ln -s "$(cd "$menus_data" && pwd)"/maps/* "$pausesave_maps/maps/"
		pausesave_run pausesave_classic classic 'levels\a30\a30' savegame.bin 3
		grep -aq "ui: screen ui.shell.error.warning_saving_file_screen_ingame" "$out/pausesave_classic/run.log" ||
			fail pausesave_classic "the Xbox's SAVE AND QUIT did not show its saving screen"
		pausesave_run pausesave_halo3 halo3 'levels\a30\a30' savegame.bin 3
		grep -aq "ui: screen pc.h3.pause.screen" "$out/pausesave_halo3/run.log" || fail pausesave_halo3 "the Halo 3 pause did not open"
		grep -aqE "ui: screen (ui.shell.error.warning_saving_file_screen_ingame|pc.h3.dlg.savequit.screen)" "$out/pausesave_halo3/run.log" ||
			fail pausesave_halo3 "SAVE AND QUIT did not show its saving screen"
		if [ ! -r "${HALO_TEST_CE_CAMPAIGN:-}" ]; then
			echo "--- pausesave_custom"; echo "SKIP: no HALO_TEST_CE_CAMPAIGN (a Custom Edition campaign map)"; continue
		fi
		ln -s "$(readlink -f "$HALO_TEST_CE_CAMPAIGN")" "$pausesave_maps/maps/"
		custom=$(basename "$HALO_TEST_CE_CAMPAIGN" .map)
		# (Halo PC's pause: RESUME GAME, LOAD LAST CHECKPOINT, RESTART LEVEL, CHANGE
		# SETTINGS, QUIT - four rows down; with SAVE LAST CHECKPOINT still there, the
		# fourth would be CHANGE SETTINGS, and nothing saved)
		pausesave_run pausesave_custom classic "custom_maps\\$custom" savegame_custom.bin 4
		grep -aq "the campaign pause menu's SAVE LAST CHECKPOINT taken out" "$out/pausesave_custom/data/debug.txt" ||
			fail pausesave_custom "SAVE LAST CHECKPOINT was not taken out of Halo PC's pause" ;;
	pause)
		run pause "$menus_data" "${HALO_TEST_PAUSE_SECONDS:-120}" "$pause_pad" "HALO_TEST_COMMANDS=L150:@skip" \
			HALO_TEST_WIDGET_FUNCTION=76
		check_pause ;;
	xbox)
		run xbox "$data" 40 "$pad"
		check xbox xbox ;;
	h3probe)
		run h3probe "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3probe halo3 ;;
	h3classic)
		run h3classic "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=classic
		check_h3 h3classic classic ;;
	h3xbox)
		run h3xbox "$data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3xbox classic
		grep -aq "menus: OpenCE" "$out/h3xbox/run.log" && fail h3xbox "OpenCE's screens were added without the Halo PC files" ;;
	h3broken)
		rm -rf "$out/h3menus"
		cp -r "$HALO_MENUS_FOLDER" "$out/h3menus"
		sed -i "s/column\.png/no_such_picture.png/" "$out/h3menus/halo3/main.xml"
		HALO_MENUS_FOLDER="$out/h3menus/" run h3broken "$menus_data" 20 "$h3pad" HALO_MENU_STYLE=halo3
		check_h3 h3broken classic
		grep -aq "menus: Halo 3 set left out" "$out/h3broken/run.log" || fail h3broken "no line said the Halo 3 set was left out" ;;
	h3style)
		run h3style "$menus_data" 30 "wait:150:2000 down:150:800 a wait:150:5000 b wait:150:4000" \
			HALO_MENU_STYLE=classic "HALO_TEST_COMMANDS=150:@set HALO_MENU_STYLE halo3"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" "$out/h3style/run.log" || fail h3style "the Xbox's main menu was not first"
		check_h3 h3style halo3 ;;
	h3online)
		run h3online "$menus_data" 15 "$h3pad" HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=5
		check_h3 h3online halo3
		grep -aq "menus: h3 servers: 3 shown of 5: Test game 02 (14/16), Test game 04 (12/16), Test game 01 (7/16)" \
			"$out/h3online/run.log" || fail h3online "the three fullest games were not shown" ;;
	h3touch)
		skip_null h3touch "taps" && continue
		# (FORGE's row on the main menu, then the Forge lobby's Leave: icon and label)
		run h3touch "$menus_data" 20 "wait:150:12000" HALO_MENU_STYLE=halo3 "HALO_TEST_TAPS=4000:60,318 8000:140,455"
		check_h3 h3touch halo3
		grep -aq "ui: screen pc.h3.forge.lobby" "$out/h3touch/run.log" || fail h3touch "a tap on FORGE did not open the Forge lobby"
		[ "$(grep -ac 'ui: screen pc.h3.main.screen' "$out/h3touch/run.log")" -ge 2 ] ||
			fail h3touch "a tap on the lobby's Leave did not go back to the main menu" ;;
	h3resume)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3resume"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the lobby: Start Game on The Pillar of Autumn, Normal; in the level, saved and quit to the
		# main menu, CAMPAIGN focused; CAMPAIGN again, the profile, the lobby: its caption; Difficulty,
		# down, A: Heroic)
		profile_from=$HALO_TEST_PROFILE run h3resume "$menus_data" 170 "wait:150:3000 a wait:150:3000 a wait:150:3000 down:150:600 down:150:600 a wait:150:70000 a wait:150:3000 a wait:150:4000 wait:150:2000 down:150:700 a wait:150:2500 down:150:700 a wait:150:6000 wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_COMMANDS=L240:@skip;L420:game_save_totally_unsafe;L600:@quit"
		check_h3 h3resume halo3
		log=$out/h3resume/run.log
		grep -aq "menus: h3 campaign caption: The Pillar of Autumn on Normal.at Last Checkpoint" "$log" ||
			fail h3resume "the lobby did not say the saved game goes on from its checkpoint"
		grep -aq "menus: h3 campaign caption: The Pillar of Autumn on Heroic.at Mission Start (replaces the saved game)" "$log" ||
			fail h3resume "another difficulty did not warn that it replaces the saved game" ;;
	h3pause)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3pause"; echo "SKIP: no HALO_TEST_PROFILE (a blam.sav with every level)"; continue
		fi
		# (the lobby: Mission, The Silent Cartographer, Start Game; in the level - a10's start
		# is the cryo tube, no pausing - Start: the Halo 3 pause screen; down to SAVE AND QUIT, A)
		profile_from=$campaign_profile run h3pause "$menus_data" 170 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 a wait:150:60000 start:150:4000 wait:150:4000 down:150:1500 wait:150:4000 down:150:800 down:150:800 a:150:4000 wait:150:20000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_COMMANDS=L150:@skip
		log=$out/h3pause/run.log
		echo "--- h3pause"
		grep -aq "menus: the in-game screens: .* added to b30.map" "$log" || fail h3pause "the in-game screens were not built into b30.map"
		grep -aq "menus: Halo 3 screen h3/pause/screen for ui.shell.solo_game.pause_game.pause_game" "$log" ||
			fail h3pause "the Halo 3 pause screen did not stand for the level's"
		grep -aqE "ui: screen (ui.shell.error.warning_saving_file_screen_ingame|pc.h3.dlg.savequit.screen)" "$log" ||
			fail h3pause "SAVE AND QUIT did not save"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3pause "SAVE AND QUIT did not go back to the Halo 3 main menu"
		grep -aqiE "assert|exception|halt" "$log" && fail h3pause "an assertion, exception or halt" ;;
	h3campaign)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3campaign"; echo "SKIP: no HALO_TEST_PROFILE (a blam.sav with every level)"; continue
		fi
		# (the main menu: down to CAMPAIGN, A; the profile, A; the lobby: Mission, down three, A;
		# Difficulty - the picker opens on Normal -, down one, A; Start Game)
		profile_from=$campaign_profile run h3campaign "$menus_data" 75 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:700 a wait:150:2500 down:150:600 a wait:150:2500 down:150:700 a wait:150:15000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3campaign halo3
		log=$out/h3campaign/run.log
		grep -aq "menus: Halo 3 screen h3/campaign/screen for ui.shell.main_menu.solo_level_select.solo_level_select_screen" "$log" ||
			fail h3campaign "the Halo 3 campaign lobby did not stand for the level select"
		grep -aq "menus: h3 campaign: The Silent Cartographer on Normal" "$log" || fail h3campaign "the mission was not set"
		grep -aq "menus: h3 campaign: The Silent Cartographer on Heroic" "$log" || fail h3campaign "the difficulty was not set"
		grep -aq "menus: h3 campaign: start levels.b30.b30 on Heroic" "$log" || fail h3campaign "Start Game did not start b30 on Heroic"
		grep -aq "memory: loaded levels.b30.b30" "$log" || fail h3campaign "b30 did not load" ;;
	h3mp)
		# (NETWORK: the picker on SYSTEM LINK, up to ONLINE, A: online, live (its brokers TEST-NET
		# addresses: connecting); NETWORK again, down to SYSTEM LINK, A: internet play stopped;
		# down three to START GAME, A: the profile screen, A to join, the profile chosen, A: the
		# game hosted with the lobby's GAME and MAP, its lobby (no map list); B: the multiplayer
		# lobby, the game kept as its live game (no profile when the lobby opened: not live then);
		# START GAME again: that game's lobby at once, the profile not asked again)
		run h3mp "$menus_data" 70 "$h3mppad a wait:150:3000 up:150:600 a wait:150:5000 a wait:150:3000 down:150:600 a wait:150:3000 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:5000 b wait:150:4000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_NET_ONLINE=false
		check_h3 h3mp halo3
		log=$out/h3mp/run.log
		grep -aq "menus: Halo 3 screen h3/mp/screen for ui.shell.main_menu.multiplayer_type_select.multiplayer_type_select_screen" "$log" ||
			fail h3mp "the Halo 3 lobby did not stand for the Multiplayer screen"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: BATTLE CREEK" "$log" ||
			fail h3mp "the lobby's rows were not SYSTEM LINK, SLAYER, BATTLE CREEK"
		grep -aq "ui: screen pc.h3.network.screen" "$log" || fail h3mp "NETWORK did not open its picker"
		grep -aq "network: switching from Same Wi-Fi to Online" "$log" && grep -aq "Internet play: network thread started" "$log" ||
			fail h3mp "ONLINE did not start internet play, live"
		grep -aq 'network: status "Connecting..."' "$log" || fail h3mp "the status under NETWORK did not say Connecting..."
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3mp "the lobby did not show ONLINE"
		grep -aq "network: switching from Online to Same Wi-Fi" "$log" &&
			grep -aq "Internet play: stopped (its thread, sockets and peers gone)" "$log" ||
			fail h3mp "SYSTEM LINK did not stop internet play"
		grep -aq "ui: screen pc.h3.mp.offline" "$log" && fail h3mp "ONLINE said internet play is off"
		grep -aq "menus: h3 mp: host a system link game" "$log" || fail h3mp "START GAME did not host a system link game"
		grep -aq "menus: h3 mp: hosting Slayer on Battle Creek (the lobby's GAME and MAP); the game's lobby" "$log" ||
			fail h3mp "the game was not hosted with the lobby's GAME and MAP"
		[ "$(grep -ac "menus: Multiplayer > System Link: the game's server started; the game's lobby" "$log")" = 1 ] ||
			fail h3mp "the server did not start once, straight to the game's lobby (or the log named another screen)"
		grep -aq "menus: h3 live lobby: its game kept, live on SYSTEM LINK (Slayer on Battle Creek)" "$log" ||
			fail h3mp "back in the lobby, the game was not kept as its live game"
		grep -aq "menus: h3 live lobby: START GAME: the game's lobby, its countdown begun" "$log" ||
			fail h3mp "the second START GAME did not go to the live game's lobby"
		[ "$(grep -ac "ui: screen pc.h3.pregame.screen" "$log")" = 2 ] || fail h3mp "the game's lobby did not open twice"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected" "$log" &&
			fail h3mp "an Xbox screen (the map or game type list) opened on the way"
		[ "$(grep -ac "ui: screen pc.h3.join.screen" "$log")" = 1 ] || fail h3mp "SELECT PROFILE was not asked once only"
		sed -n '/ui: screen pc.h3.pregame.screen/,$p' "$log" | grep -a "ui: screen" | sed -n 2p | grep -q "pc.h3.mp.screen" ||
			fail h3mp "B on the game's lobby did not go back to the multiplayer lobby"
		grep -aq "ui: screen pc.mp.screen" "$log" && fail h3mp "OpenCE's Multiplayer screen opened with Menu style Halo 3" ;;
	h3browser)
		# (down five to BROWSE GAMES, A: the profile (A joins, A the profile, A): the games
		# browser; A on the first game - the fullest Vita game, locked -: its password; A: typed,
		# down, A: JOIN GAME)
		run h3browser "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 wait:150:600 a wait:150:4000 a wait:150:2000 down:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_PC=1 HALO_TEST_TEXT_INPUT=secret
		check_h3 h3browser halo3
		log=$out/h3browser/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3browser "NETWORK was not ONLINE with internet play on"
		grep -aq "ui: screen pc.h3.lan.screen" "$log" && grep -aq "menus: the LAN browser (system link and online)" "$log" ||
			fail h3browser "BROWSE GAMES did not open the Halo 3 games browser with the online games"
		grep -aq "menus: the server browser's game 1 of 16 chosen: Test game 02" "$log" ||
			fail h3browser "the browser did not list the 16 games, the fullest Vita game first"
		grep -aq "menus: Halo 3 screen h3/password/screen for pc.join.password.screen" "$log" ||
			fail h3browser "the locked game did not open the Halo 3 password screen"
		grep -aq "menus: the public game Test game 02 is no longer listed" "$log" ||
			fail h3browser "JOIN GAME did not try the game with its password"
		grep -aq "ui: screen pc.browser.screen\|ui: screen pc.join.password" "$log" &&
			fail h3browser "OpenCE's browser or password screen opened with Menu style Halo 3" ;;
	h3create)
		# (Square: GAME OPTIONS (Create Game); GAME TYPE, A: the picker, down, A: SLAYER PRO; down two
		# to NAME, A: typed; down five to BOTS (past WHO CAN JOIN), right: OFF; down to START GAME, A)
		run h3create "$menus_data" 50 "$h3mppad x wait:150:4000 a wait:150:3000 down:150:600 a wait:150:4000 down:150:500 down:150:500 a wait:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 right:150:800 down:150:500 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net "HALO_TEST_TEXT_INPUT=Vita Night" HALO_BOTS_ONLINE=full
		check_h3 h3create halo3
		log=$out/h3create/run.log
		grep -aq "ui: screen pc.h3.setup.screen" "$log" || fail h3create "Square did not open GAME OPTIONS (Create Game)"
		grep -aq "menus: h3 mp: game type Slayer Pro" "$log" || fail h3create "the GAME TYPE picker did not set the game type"
		grep -aq "menus: Server Setup: Vita Night, 16 players, public, Vitas; the profile, then the game's lobby" "$log" ||
			fail h3create "START GAME did not set up the game as Server Setup does"
		grep -aq "menus: GAME OPTIONS: Bots online off" "$log" || fail h3create "BOTS (right: OFF) did not set Bots online off"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen|pc.h3.join.screen)" "$log" ||
			fail h3create "START GAME did not open the profile" ;;
	h3vitapc)
		# (the port's PC build, Oct 20 2026: the LAN browser's 6 made-up games - its log line
		# names all 6 - the fifth (Test game 04) the PC build's: VITA (PC), grouped with the PCs';
		# the first (Test game 00) for Vitas only, which a viewer on the PC build
		# (HALO_TEST_PLATFORM=pc) sees marked VITAS ONLY)
		for who in vita pc; do
			run h3vitapc_$who "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:6000" \
				HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=6 HALO_TEST_BROWSER_PC=1 HALO_TEST_BROWSER_VITA_PC=1 HALO_TEST_PLATFORM=$who
			check_h3 h3vitapc_$who halo3
			log=$out/h3vitapc_$who/run.log
			echo "--- h3vitapc_$who"; grep -aE "menus: the LAN browser lists|menus: the browser's groups" "$log" | head -3 | cut -c1-400
			grep -aq "menus: the browser's groups (All games): VITA GAMES (2), PC GAMES (4)" "$log" ||
				fail h3vitapc_$who "the PC build's games (VITA (PC)) were not grouped with the PCs'"
			grep -aq "Test game 04 ([^)]*, VITA (PC))" "$log" || fail h3vitapc_$who "Test game 04 was not tagged VITA (PC)"
		done
		grep -aq "Test game 00 ([^)]*, VITA)" "$out/h3vitapc_vita/run.log" ||
			fail h3vitapc_vita "a game for Vitas only was not plain VITA to a Vita"
		grep -aq "Test game 00 ([^)]*, VITA VITAS ONLY)" "$out/h3vitapc_pc/run.log" ||
			fail h3vitapc_pc "a game for Vitas only was not marked VITAS ONLY to the PC build" ;;
	h3createwho)
		# (WHO CAN JOIN, the owner, Oct 20 2026: VITA, VITA PC, EVERYONE on a Vita; on the PC build
		# VITA PC and EVERYONE alone (strings_pc), VITA PC its default. GAME OPTIONS: down six to
		# WHO CAN JOIN, then right N times, down twice (past BOTS), A: START GAME. A Vita's right once: VITA PC; the
		# PC build's: EVERYONE; the PC build's none: VITA PC)
		for spec in "vita 1" "pc 1" "pc 0" "vita 0"; do
			set -- $spec
			who_pad="$h3mppad x wait:150:4000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500"
			[ "$2" = 1 ] && who_pad="$who_pad right:150:800"
			who_pad="$who_pad down:150:500 down:150:500 a wait:150:5000"
			run h3createwho_$1_$2 "$menus_data" 36 "$who_pad" HALO_MENU_STYLE=halo3 $h3net "HALO_TEST_PLATFORM=$1"
			check_h3 h3createwho_$1_$2 halo3
		done
		for spec in "vita_1 Vitas and Vita PC" "pc_1 Vitas and PC" "pc_0 Vitas and Vita PC" "vita_0 Vitas"; do
			name=h3createwho_${spec%% *} said=${spec#* }
			grep -a "menus: Server Setup: " "$out/$name/run.log" | head -1
			grep -aq "menus: Server Setup: .*, public, $said; the profile" "$out/$name/run.log" ||
				fail $name "WHO CAN JOIN was not $said"
		done ;;
	h3hostonline)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3hostonline"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (ONLINE, its brokers unreachable: MULTIPLAYER; down two to MAP, A: the picker, down nine to
		# BLOOD GULCH, A; down to START GAME, A: SELECT PROFILE (A joins, A the profile, A): the game
		# hosted on Blood Gulch, its lobby - its code and PUBLIC not shown while the brokers are not
		# reached: Connecting..., then why -; B: the multiplayer lobby (the brokers unreachable, the
		# connection stays Online, its status line saying so - insider.21 fell back to Same Wi-Fi,
		# its ONLINE rows then saying internet play was off); Menu style Halo CE: CREATE GAME
		# INTERNET is OpenCE's Server Setup as before)
		hostonline_pad="$h3mppad down:150:500 down:150:500 a wait:150:2500"
		for row in $(seq 1 9); do hostonline_pad="$hostonline_pad down:150:300"; done
		hostonline_pad="$hostonline_pad a wait:150:2500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000"
		hostonline_pad="$hostonline_pad @01-pregame:0:300 b wait:150:4000 @02-lobby:0:300"
		rm -rf "$out/h3hostonline_shots"; mkdir -p "$out/h3hostonline_shots"
		profile_from=$HALO_TEST_PROFILE run h3hostonline "$menus_data" 60 "$hostonline_pad" HALO_MENU_STYLE=halo3 $h3net \
			HALO_NET_HOST_PUBLIC=true HALO_SCREENSHOT_DIR="$out/h3hostonline_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		run h3hostonline_classic "$menus_data" 25 "$cmp_create" HALO_MENU_STYLE=classic $h3net
		check_h3 h3hostonline halo3
		log=$out/h3hostonline/run.log
		grep -aE "ui: screen|menus: h3 mp: (host|hosting)|menus: h3 join: (on|the profile)" "$log" | head -20
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE, GAME: SLAYER, MAP: BLOOD GULCH" "$log" ||
			fail h3hostonline "the lobby was not ONLINE on Blood Gulch"
		grep -aq "menus: h3 mp: host an online game" "$log" || fail h3hostonline "START GAME on ONLINE did not host at once"
		grep -aq "menus: h3 mp: hosting Slayer on Blood Gulch (the lobby's GAME and MAP); the game's lobby" "$log" ||
			fail h3hostonline "the game was not hosted on the lobby's map"
		grep -aq "join: the host's map is levels.test.bloodgulch.bloodgulch" "$log" ||
			fail h3hostonline "the hosted game is not on Blood Gulch"
		grep -aq "ui: screen pc.h3.pregame.screen" "$log" || fail h3hostonline "the game's lobby did not open"
		grep -a "menus: h3 pregame: \|network: Online\|network: Same Wi-Fi" "$log" | sed 's/code [A-Z0-9-]*/code (hidden)/' | head -8
		grep -aqE "menus: h3 pregame: .*; (connecting|not reached)" "$log" ||
			fail h3hostonline "the game's lobby did not say it was connecting (or why it is not listed)"
		grep -aqE "menus: h3 pregame: .*(; public|; code )" "$log" &&
			fail h3hostonline "the game's lobby showed its code or PUBLIC with the brokers not reached"
		grep -aq "network: Same Wi-Fi chosen" "$log" && fail h3hostonline "Online's failure switched the connection to Same Wi-Fi"
		sed -n '/ui: screen pc.h3.pregame.screen/,$p' "$log" | grep -a "ui: screen" | sed -n 2p | grep -q "pc.h3.mp.screen" ||
			fail h3hostonline "B on the game's lobby did not go back to the multiplayer lobby"
		grep -aq "ui: screen pc.h3.setup.screen" "$log" && fail h3hostonline "START GAME opened Create Game on the way"
		grep -a "ui: screen" "$log" | grep -av "pc.h3" &&
			fail h3hostonline "a screen not of the Halo 3 set opened (above)"
		grep -aq "ui: screen pc.setup.screen" "$out/h3hostonline_classic/run.log" ||
			fail h3hostonline "Menu style Halo CE's CREATE GAME INTERNET did not open OpenCE's Server Setup"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3hostonline" &&
			cp "$out"/h3hostonline_shots/0*.bmp "$HALO_TEST_SHOTS/h3hostonline/" 2>/dev/null ;;
	h3livelobby)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3livelobby"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the live lobby, Menu style Halo 3: MULTIPLAYER on SYSTEM LINK with a profile - its
		# nameplate's -: the lobby's game open once its scene stands, its players in the lobby; down
		# three to START GAME, A: the game's lobby at once (no SELECT PROFILE, no other server),
		# alone: no countdown; B: back to the lobby, the game kept; B: the main menu, the game closed.
		# Then ONLINE, its brokers unreachable: never live (connecting), START GAME as before -
		# SELECT PROFILE, a server of its own, the game's lobby; B: that game closed (the network
		# does not carry it: none left behind the lobby). Each lobby's scene whole (its flag
		# with its pole and holder) behind the game's lobby too, or none: deleted once, its flag's
		# cloth with it; the screenshots at the Vita's picture)
		d="down:150:500"
		live_pad="$h3mppad wait:150:5000 @01-lobby-live:0:300 $d $d $d a wait:150:4000 @02-pregame-live:0:300"
		live_pad="$live_pad b wait:150:4000 @03-lobby-again:0:300 b wait:150:4000 @04-main-menu:0:300"
		online_pad="$h3mppad wait:150:5000 @05-lobby-connecting:0:300 $d $d $d a wait:150:3000 a wait:150:2000 a wait:150:2000"
		online_pad="$online_pad a wait:150:5000 @06-pregame-not-live:0:300 b wait:150:4000 @07-lobby-not-live:0:300"
		rm -rf "$out/h3livelobby_shots"; mkdir -p "$out/h3livelobby_shots"
		profile_from=$HALO_TEST_PROFILE run h3livelobby "$menus_data" 50 "$live_pad" HALO_MENU_STYLE=halo3 \
			HALO_SCREENSHOT_DIR="$out/h3livelobby_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		profile_from=$HALO_TEST_PROFILE run h3livelobby_online "$menus_data" 50 "$online_pad" HALO_MENU_STYLE=halo3 $h3net \
			HALO_SCREENSHOT_DIR="$out/h3livelobby_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3livelobby halo3
		log=$out/h3livelobby/run.log
		grep -aE "ui: screen|menus: h3 live lobby|h3 scene objects?: (h3/mp|let go|.*deleted)|h3 join:" "$log" | head -30
		grep -aq "menus: h3 live lobby: open on SYSTEM LINK (Slayer on Battle Creek): others join it now" "$log" ||
			fail h3livelobby "the lobby did not open live on SYSTEM LINK"
		sed -n '1,/menus: h3 live lobby: open on/p' "$log" | grep -aq "h3 scene objects: h3/mp/screen: made" ||
			fail h3livelobby "the lobby went live before its scene stood"
		sed -n '1,/menus: h3 live lobby: open on/p' "$log" | grep -aq "h3 scene object 0: animation stand missile airborne, held" ||
			fail h3livelobby "the lobby went live before its scene was posed"
		grep -aqE "menus: h3 live lobby: Slayer on Battle Creek; players: [^;]+; 1/16" "$log" ||
			fail h3livelobby "the lobby did not show its game's player"
		grep -aq "ui: screen pc.h3.join.screen" "$log" && fail h3livelobby "SELECT PROFILE was asked (the nameplate's profile plays)"
		grep -aq "the game's server started" "$log" && fail h3livelobby "START GAME started another server"
		grep -aq "menus: h3 live lobby: START GAME: the game's lobby, its countdown begun" "$log" &&
			grep -aq "ui: screen pc.h3.pregame.screen" "$log" || fail h3livelobby "START GAME did not open the live game's lobby"
		grep -aq "menus: h3 live lobby: the game's lobby: waiting for players before the countdown" "$log" ||
			fail h3livelobby "alone, the countdown was begun"
		grep -aq "menus: h3 live lobby: back from the game's lobby: the countdown stopped, the game kept" "$log" ||
			fail h3livelobby "B on the game's lobby did not go back to the live lobby"
		grep -aq "menus: h3 live lobby: closed (the host left the lobby)" "$log" || fail h3livelobby "B on the lobby did not close it"
		sed -n '1,/menus: h3 live lobby: closed/p' "$log" | grep -aq "h3 scene object [0-9]: deleted" &&
			fail h3livelobby "the scene lost an object in the live lobby or its game's lobby (all of it, or none)"
		grep -aq "h3 scene objects: let go after .*, 0 flags left" "$log" ||
			fail h3livelobby "the scene was let go with a flag's cloth left behind"
		sed -n '/menus: h3 live lobby: open on/,/menus: h3 live lobby: closed/p' "$log" |
			grep -aq "h3 scene objects: h3/mp/screen: made" && fail h3livelobby "going live made the lobby's scene again"
		sed -n '/menus: h3 live lobby: open on/,/menus: h3 live lobby: closed/p' "$log" |
			grep -aq "menus: party plates: none (the nameplate hidden)" &&
			fail h3livelobby "going live hid the nameplate (no other machine in its game)"
		grep -aq "starting precaching of map 'carousel'" "$out/h3livelobby/data/debug.txt" &&
			fail h3livelobby "going live began a copy of the playlist's first map (Carousel), not the lobby's"
		grep -aq "starting precaching of map 'beavercreek'" "$out/h3livelobby/data/debug.txt" ||
			fail h3livelobby "the lobby's map (Battle Creek) was not copied as it went live"
		check_h3 h3livelobby_online halo3
		log=$out/h3livelobby_online/run.log
		grep -aE "ui: screen|menus: h3 live lobby|h3 scene objects: (h3/mp|let go)|h3 join:|network: status" "$log" | head -20
		grep -aq "menus: h3 live lobby: open on ONLINE" "$log" && fail h3livelobby "ONLINE went live with its brokers unreachable"
		grep -aq "menus: h3 live lobby: closed (the game it hosted before cannot go on from it)" "$log" ||
			fail h3livelobby "back in the lobby, the game hosted (not live) was left behind it"
		grep -aq "ui: screen pc.h3.join.screen" "$log" || fail h3livelobby "not live, START GAME did not ask for the profile"
		grep -aq "menus: Create Game > Internet: the game's server started; the game's lobby" "$log" ||
			fail h3livelobby "not live, START GAME did not host as before"
		grep -aq "h3 scene object [0-9]: deleted" <(sed -n '/ui: screen pc.h3.pregame.screen/,/ui: screen pc.h3.mp.screen/p' "$log") &&
			fail h3livelobby "the scene lost an object behind the game's lobby (a stray flag)"
		grep -aq "h3 scene objects: let go after .*, [1-9][0-9]* flags left" "$log" &&
			fail h3livelobby "a scene was let go with a flag's cloth left behind"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3livelobby" &&
			cp "$out"/h3livelobby_shots/0*.bmp "$HALO_TEST_SHOTS/h3livelobby/" 2>/dev/null ;;
	profileio)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- profileio"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (a first start at full speed: ui.map in the cache partition, as on a Vita that played before)
		profile_from=$HALO_TEST_PROFILE run profileio_fill "$menus_data" 15 "wait:150:3000" HALO_MENU_STYLE=halo3
		card="HALO_IO_THROTTLE_KBPS=10000 HALO_IO_THROTTLE_WRITE_KBPS=750 HALO_IO_TRACE_GAME=1 HALO_LOAD_PROFILE_MS=100"
		d="down:150:500"
		save_from=$out/profileio_fill/save run profileio_live "$menus_data" 40 \
			"$h3mppad wait:150:8000 $d $d $d a wait:150:5000 b wait:150:4000 b wait:150:3000" HALO_MENU_STYLE=halo3 $card
		save_from=$out/profileio_fill/save run profileio_join "$menus_data" 40 \
			"$h3mppad wait:150:5000 $d $d $d a wait:150:8000 b wait:150:3000 b wait:150:3000" HALO_MENU_STYLE=halo3 $h3net $card
		for name in profileio_live profileio_join; do
			log=$out/$name/run.log
			# (check_h3's, but for the render thread's skipped check of a cache file's header read
			# while the slow card's copy writes it: "render assertion skipped (torn read?)")
			echo "--- $name"
			[ "$(cat "$out/$name/exit")" = 0 ] || fail "$name" "exit $(cat "$out/$name/exit")"
			grep -aiE "assert|exception|halt" "$log" | grep -av "render assertion skipped (torn read?)" &&
				fail "$name" "an assertion, exception or halt"
			grep -aq "ui: screen pc.h3.*screen" "$log" || fail "$name" "the Halo 3 screen did not open"
			opens=$(grep -ac 'io-trace: open .*\\blam\.sav:' "$log")
			echo "$name: the game thread's file calls: $(grep -ac 'io-trace: ' "$log"), blam.sav opened $opens times;" \
				"$(grep -ac 'frame-hitch' "$log") frames over 100 ms"
			grep -a 'io-trace: open ' "$log" | sed 's/.*ms \([^:]*:[^:]*\):.*/\1/' | sort | uniq -c | sort -rn | head -5
			[ "$opens" -le 1 ] || fail profileio "$name: the game thread opened blam.sav $opens times (once at most wanted)"
			grep -aq 'frame-hitch.*blam\.sav' "$log" && fail profileio "$name: a frame waited on blam.sav"
			# (read once at most: a game hosted writes lastmpmp.txt and lastmpvr.txt, which is no read)
			for last in lastprof lastmpvr lastmpmp; do
				[ "$(grep -ac "io-trace: read .*/$last\.txt:" "$log")" -le 1 ] ||
					fail profileio "$name: z:\\$last.txt read more than once"
			done
		done
		grep -aq "menus: h3 live lobby: open on SYSTEM LINK" "$out/profileio_live/run.log" ||
			fail profileio "the live lobby did not open"
		grep -aq "starting precaching of map 'beavercreek'" "$out/profileio_live/data/debug.txt" ||
			fail profileio "no map copy in the live lobby"
		grep -aq "ui: screen pc.h3.join.screen" "$out/profileio_join/run.log" ||
			fail profileio "START GAME not live did not open SELECT PROFILE" ;;
	h3groups)
		# (the owner, Oct 19 2026: the Vitas' games first, then the PCs', each under its header;
		# the D-pad's right and left: Vita games, PC games, all games, kept as the setting.
		# Halo 3's games browser on SYSTEM LINK (down five to BROWSE GAMES, A: the profile, A A A),
		# 14 made-up games, half the PCs'; then OpenCE's browser (Menu style Halo CE) online. Each
		# at the Vita's picture, its frames kept when HALO_TEST_SHOTS is set)
		groups_pad="right:150:4000 right:150:4000 right:150:4000 left:150:4000 left:150:4000 wait:150:3000"
		run h3groups "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 down:150:800 $groups_pad" \
			HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		run h3groups_classic "$menus_data" 50 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:4000 $groups_pad" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3groups halo3
		for name in h3groups h3groups_classic; do
			log=$out/$name/run.log
			echo "--- $name"; grep -aE "menus: the browser's groups|menus: the server browser (shows|'s game)" "$log" | head -16
			[ "$(cat "$out/$name/exit")" = 0 ] || fail $name "exit $(cat "$out/$name/exit")"
			# (the filters in turn: all, Vita, PC, all, then back: PC, Vita; all games grouped
			# VITA GAMES then PC GAMES, each filter's its own group alone. Counts as the
			# made-up games', or more: the System Link list may show another's game on this
			# machine's network)
			order=$(grep -a "menus: the browser's groups" "$log" | sed 's/.*groups (\([^)]*\)).*/\1/' | uniq | tr '\n' ';')
			[ "$order" = "All games;Vita games;PC games;All games;PC games;Vita games;" ] ||
				fail $name "the filters were not all, Vita, PC, all, PC, Vita: $order"
			grep -a "menus: the browser's groups (All games)" "$log" |
				grep -avqE "\): VITA GAMES \(([7-9]|[1-9][0-9])\), PC GAMES \(([7-9]|[1-9][0-9])\)$" &&
				fail $name "all games were not grouped VITA GAMES, then PC GAMES"
			grep -a "menus: the browser's groups (Vita games)" "$log" | grep -aq "PC GAMES" &&
				fail $name "Vita games showed a PC game"
			grep -a "menus: the browser's groups (PC games)" "$log" | grep -aq "VITA GAMES" &&
				fail $name "PC games showed a Vita game"
			grep -aq "menus: the server browser shows the Vitas' games" "$log" && grep -aq "menus: the server browser shows the PCs' games" "$log" &&
				grep -aq "menus: the server browser shows every game" "$log" || fail $name "the D-pad did not step the filter"
			# (the first game chosen: the fullest Vita game; down once, the next Vita game; with
			# PC games alone, the fullest PC game; never a header)
			grep -aqE "menus: the server browser's game 1 of 1[4-9] chosen: Test game 02" "$log" ||
				fail $name "the first game was not the fullest Vita game"
			grep -aqE "menus: the server browser's game 1 of ([7-9]|1[0-9]) chosen: Test game 09" "$log" ||
				fail $name "PC games alone: the first game was not the fullest PC game"
			grep -aq "chosen: VITA GAMES\|chosen: PC GAMES" "$log" && fail $name "a header row was chosen"
		done
		grep -aqE "menus: the server browser's game 2 of 1[4-9] chosen: Test game 04" "$out/h3groups/run.log" ||
			fail h3groups "down from the first game did not choose the next Vita game"
		if [ -n "${HALO_TEST_SHOTS:-}" ]; then echo "frames in $HALO_TEST_SHOTS/h3groups*"; fi ;;
	h3mptouch)
		skip_null h3mptouch "taps" && continue
		# (MULTIPLAYER on the main menu; the lobby's MAP; BLOOD GULCH in the picker; the lobby's Back)
		run h3mptouch "$menus_data" 30 "wait:150:20000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TAPS=4000:60,298 8000:60,118 12000:100,263 16000:140,455"
		check_h3 h3mptouch halo3
		log=$out/h3mptouch/run.log
		grep -aq "ui: screen pc.h3.map.screen" "$log" || fail h3mptouch "a tap on MAP did not open its picker"
		grep -aq "menus: h3 mp: map Blood Gulch" "$log" || fail h3mptouch "a tap on BLOOD GULCH did not choose it"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3mptouch "a tap on the lobby's Back did not go back to the main menu" ;;
	h3coop)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3coop"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby, MISSION focused; up to NETWORK, A: the picker on LOCAL;
		# down, A: ONLINE CO-OP, online live (its brokers TEST-NET addresses: connecting); NETWORK
		# again, down, A: SYSTEM LINK CO-OP, internet play stopped; down three to START GAME, A:
		# hosted, the game's lobby)
		profile_from=$HALO_TEST_PROFILE run h3coop "$menus_data" 55 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 a wait:150:4000 a wait:150:2500 down:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_NET_ONLINE=false
		check_h3 h3coop halo3
		log=$out/h3coop/run.log
		grep -aq "ui: screen pc.h3.campnet.screen" "$log" || fail h3coop "NETWORK did not open its picker"
		grep -aq "menus: h3 campaign: network ONLINE CO-OP" "$log" &&
			grep -aq "network: switching from Same Wi-Fi to Online" "$log" ||
			fail h3coop "ONLINE CO-OP did not go online, live"
		grep -aq "network: switching from Online to Same Wi-Fi" "$log" || fail h3coop "SYSTEM LINK CO-OP did not leave online"
		grep -aq "menus: h3 campaign: network SYSTEM LINK CO-OP" "$log" || fail h3coop "SYSTEM LINK CO-OP was not chosen"
		grep -aq "menus: h3 campaign caption: .* on Normal Co-op at Mission Start" "$log" ||
			fail h3coop "the caption did not say co-op"
		grep -aq "menus: h3 campaign: host co-op levels.*on Normal (system link)" "$log" ||
			fail h3coop "START GAME did not host co-op"
		grep -aq "co-op: hosting levels.* on difficulty 1 from the campaign's menus" "$log" ||
			fail h3coop "the level was not hosted as co-op"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
			fail h3coop "the game's lobby (the waiting screen) did not open"
		grep -aq "menus: h3 campaign: co-op was not hosted" "$log" && fail h3coop "co-op was not hosted"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3coop "the game's lobby did not stay open (the co-op game ended)" ;;
	h3players)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3players"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby; up to NETWORK, A, down twice: SYSTEM LINK CO-OP; down three
		# to START GAME, A: hosted, the game's lobby; Triangle: PLAYERS, this machine's player and three
		# made up (HALO_TEST_PLAYERS_FAKE); down to the first other one, Cross: its options, Back
		# focused; down, Cross: ADD FRIEND, friends not online (online off): go online first; Cross on
		# this machine's own player: no options; Circle: the game's lobby again)
		players_pad="wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 a wait:150:2500"
		players_pad="$players_pad down:150:600 down:150:600 down:150:600 a wait:150:8000 y wait:150:2500 @00-players:0:300"
		players_pad="$players_pad down:150:700 a wait:150:2000 @01-options:0:300 down:150:700 a wait:150:2000 @02-added:0:300"
		# (then Cross on it again, down three: MUTE CHAT, Cross: muted here, the in-game chat's)
		players_pad="$players_pad a wait:150:2000 down:150:700 down:150:700 down:150:700 @03-mute-option:0:300 a wait:150:2000"
		players_pad="$players_pad @04-muted:0:300"
		players_pad="$players_pad up:150:700 a wait:150:2000 b wait:150:3000"
		# (co-op's most players its own, network.coop_players: 4 here; the lobby's PLAYERS says it)
		profile_from=$HALO_TEST_PROFILE run h3players "$menus_data" 80 "$players_pad" \
			HALO_MENU_STYLE=halo3 HALO_TEST_PLAYERS_FAKE=3 HALO_NET_COOP_PLAYERS=4
		check_h3 h3players halo3
		log=$out/h3players/run.log
		grep -a "menus: h3 players\|PLAYERS screen" "$log" | head -12
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
			fail h3players "the game's lobby (the waiting screen) did not open"
		grep -aq "menus: the PLAYERS screen opened (the game's lobby)" "$log" &&
			grep -aq "ui: screen pc.h3.players.screen" "$log" || fail h3players "Triangle in the game's lobby did not open PLAYERS"
		grep -aq "menus: h3 players: 4 players, this machine 0" "$log" || fail h3players "PLAYERS did not list the four players"
		grep -aqE "menus: h3 pregame: .*; players: [^;]*; 1/4$" "$log" ||
			fail h3players "the co-op lobby's PLAYERS did not say 1/4 (network.coop_players)"
		grep -aq "menus: h3 players: options for Spartan 1 (machine 10)" "$log" &&
			grep -aq "ui: screen pc.h3.players.options" "$log" || fail h3players "Cross on a player did not open its options"
		grep -aq "menus: h3 players: Go online first" "$log" || fail h3players "ADD FRIEND offline did not say go online first"
		grep -aq "menus: h3 players: Spartan 1 muted: their chat is hidden here" "$log" &&
			grep -aq "chat: Spartan 1 muted here" "$log" || fail h3players "MUTE CHAT did not mute the player's chat here"
		grep -aq "menus: h3 players: no options for .*, a player of this PS Vita" "$log" ||
			fail h3players "this machine's own player was given options"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3players "Circle did not go back to the game's lobby" ;;
	h3scenes)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3scenes"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the campaign lobby; B, B: the main menu; down, A: MULTIPLAYER;
		# B; down, A: FORGE; B)
		scenes_pad="wait:150:3000 a wait:150:3000 a wait:150:4000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:4000 b wait:150:3000 down:150:700 a wait:150:4000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3scenes "$menus_data" 45 "$scenes_pad" HALO_MENU_STYLE=halo3
		profile_from=$HALO_TEST_PROFILE run h3scenes_off "$menus_data" 45 "$scenes_pad" HALO_MENU_STYLE=halo3 HALO_MENU_SCENES=0
		check_h3 h3scenes halo3
		log=$out/h3scenes/run.log
		grep -aE "menus: h3 scene|host command \(menu scene\)" "$log"
		for scene in "h3/campaign/screen: camera point load_campaign at -15.00 5.00 0.00, yaw -20.0 pitch 25.0, over 0 ticks, field of view 50" \
			"h3/mp/screen: camera point multiplayer at -40.00 -1.80 15.50, yaw 0.0 pitch 0.0, over 0 ticks, field of view 55" \
			"h3/forge/lobby: camera point new_campaign at -10.00 -20.00 5.00, yaw 40.0 pitch 20.0, over 0 ticks, field of view 40"; do
			grep -aqF "menus: h3 scene: $scene" "$log" || fail h3scenes "no scene: $scene"
		done
		for command in "(sleep -1 ring_loop)" "(camera_set load_campaign 0)" "(camera_set multiplayer 0)" \
			"(camera_set new_campaign 0)" "(camera_set settings 0)" "(wake ring_loop)"; do
			grep -aqF "host command (menu scene): $command" "$log" || fail h3scenes "no host command $command"
		done
		[ "$(grep -ac 'menus: h3 scene: h3/main/screen: the ring' "$log")" -ge 4 ] ||
			fail h3scenes "the main menu did not have the ring's flight each time"
		[ "$(grep -acF 'host command (menu scene): (wake ring_loop)' "$log")" -ge 3 ] ||
			fail h3scenes "the main menu did not wake ring_loop after each lobby"
		grep -aqiE "hs_compile|script error|unknown function|not a valid" "$log" && fail h3scenes "a script command was refused"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3scenes "the three lobbies did not open"
		check_h3 h3scenes_off halo3
		grep -aq "menus: h3 scene\|host command (menu scene)" "$out/h3scenes_off/run.log" &&
			fail h3scenes "HALO_MENU_SCENES=0 still moved the camera"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$out/h3scenes_off/run.log")" -ge 3 ] ||
			fail h3scenes "HALO_MENU_SCENES=0: the three lobbies did not open" ;;
	h3lobbyscenes)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3lobbyscenes"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (as h3scenes: CAMPAIGN, the profile: the campaign lobby; B, B; down, A: MULTIPLAYER; B;
		# down, A: FORGE; B - each lobby long enough for its packs' first build)
		lobby_pad="wait:150:3000 a wait:150:3000 a wait:150:6000 b wait:150:2500 b wait:150:3000 down:150:700 a wait:150:6000 b wait:150:3000 down:150:700 a wait:150:8000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes "$menus_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		# (the multiplayer lobby, then the preview: in colour 5, recoloured 9, hidden)
		run h3lobbyscenes_preview "$menus_data" 26 "wait:150:3000 down:150:700 a wait:150:15000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_COMMANDS=330:pack preview 5;420:pack preview 9;510:pack preview off"
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_off "$menus_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3 HALO_SCENE_PACKS=0
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_classic "$menus_data" 30 "wait:150:3000 a wait:150:6000 b wait:150:3000" HALO_MENU_STYLE=classic
		# (a maps folder of ui.map, the Halo PC files and Blood Gulch: no campaign map; and of ui.map and
		# the Halo PC files alone: no map to build any pack from)
		for folder in nocampaign nomap; do
			rm -rf "$out/${folder}_data"
			mkdir -p "$out/${folder}_data/maps"
			maps="ui bitmaps loc"
			[ $folder = nocampaign ] && maps="$maps bloodgulch"
			for map in $maps; do ln -s "$(readlink -f "$menus_data/maps/$map.map")" "$out/${folder}_data/maps/$map.map"; done
		done
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_nocampaign "$out/nocampaign_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		profile_from=$HALO_TEST_PROFILE run h3lobbyscenes_nomap "$out/nomap_data" 60 "$lobby_pad" HALO_MENU_STYLE=halo3
		check_h3 h3lobbyscenes halo3
		log=$out/h3lobbyscenes/run.log
		grep -aE "scene packs: menu_|h3 scene object" "$log"
		for pack in "menu_campaign: built from a30" "menu_campaign: loaded from a30" "menu_multiplayer: built from bloodgulch" \
			"menu_multiplayer: loaded from bloodgulch" "menu_monitor: built from c20" "menu_monitor: loaded from c20"; do
			grep -aq "scene packs: $pack" "$log" || fail h3lobbyscenes "no \"$pack\""
		done
		for made in "objects: h3/campaign/screen: made" "objects: h3/mp/screen: made" "objects: h3/forge/lobby: made" \
			"object 0: vehicles.warthog.warthog (menu_campaign) at .* facing 268 pitch 22" \
			"object 1: characters.cyborg.cyborg seated as W-driver" \
			"object 2: characters.marine_armored.marine_armored seated as W-gunner" \
			"object 3: characters.marine_armored.marine_armored seated as W-passenger" \
			"object 0: weapons.rocket launcher.rocket launcher in its hands" \
			"object 0: characters.cyborg_mp.cyborg_mp (menu_multiplayer) at .* colour 0: made" \
			"object 0: animation stand missile airborne, held" \
			"object 1: weapons.flag.flag in its hands" \
			"object 2: weapons.sniper rifle.sniper rifle in its hands" \
			"object 2: animation crouch rifle idle, looped" \
			"object 1: characters.monitor.monitor (menu_monitor) at .* made"; do
			grep -aq "h3 scene $made" "$log" || fail h3lobbyscenes "no \"$made\""
		done
		[ "$(grep -ac 'h3 scene objects: let go after .* (at most 0 effects and 0 particles meanwhile)' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "the three scenes were not let go each, or an effect or particle was made (dust)"
		grep -aE "h3 scene objects: let go" "$log"
		grep -aqE "h3 scene object.*(could not|no seat|has no|no animation)" "$log" &&
			fail h3lobbyscenes "a scene's object, weapon, seat or animation was missing"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "the three lobbies did not open"
		check_h3 h3lobbyscenes_preview halo3
		log=$out/h3lobbyscenes_preview/run.log
		grep -aE "h3 scene object|preview" "$log"
		grep -aq "h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 5: made" "$log" ||
			fail h3lobbyscenes "the preview did not show a Spartan in colour 5"
		grep -aq "h3 scene object: recoloured 9" "$log" || fail h3lobbyscenes "the preview was not recoloured"
		grep -aq "h3 scene object 6: deleted" "$log" || fail h3lobbyscenes "the preview was not hidden"
		check_h3 h3lobbyscenes_off halo3
		grep -aqE "scene packs: |h3 scene object.* made" "$out/h3lobbyscenes_off/run.log" &&
			fail h3lobbyscenes "HALO_SCENE_PACKS=0: a pack or a scene all the same"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$out/h3lobbyscenes_off/run.log")" -ge 3 ] ||
			fail h3lobbyscenes "HALO_SCENE_PACKS=0: the three lobbies did not open"
		check_h3 h3lobbyscenes_classic classic
		grep -aqE "scene packs: menu_|h3 scene object" "$out/h3lobbyscenes_classic/run.log" &&
			fail h3lobbyscenes "Menu style Halo CE: a scene all the same"
		check_h3 h3lobbyscenes_nocampaign halo3
		log=$out/h3lobbyscenes_nocampaign/run.log
		grep -aE "scene packs: menu_|h3 scene objects" "$log"
		grep -aq "scene packs: menu_campaign: not built: none of its maps" "$log" ||
			fail h3lobbyscenes "no campaign map: the build did not say why it failed"
		grep -aq "h3 scene objects: h3/campaign/screen: the pack menu_campaign is failed: no scene" "$log" ||
			fail h3lobbyscenes "no campaign map: the campaign lobby did not go without its scene"
		grep -aq "h3 scene objects: h3/forge/lobby: the pack menu_.* is failed: no scene" "$log" ||
			fail h3lobbyscenes "no campaign map: the Forge lobby did not go without its scene"
		grep -aq "h3 scene objects: h3/mp/screen: made" "$log" ||
			fail h3lobbyscenes "no campaign map: the multiplayer lobby did not have its scene"
		grep -aq "h3 scene objects: h3/\(campaign/screen\|forge/lobby\): made" "$log" &&
			fail h3lobbyscenes "no campaign map: a campaign scene all the same"
		check_h3 h3lobbyscenes_nomap halo3
		log=$out/h3lobbyscenes_nomap/run.log
		grep -aq "h3 scene objects: .*: made" "$log" && fail h3lobbyscenes "no map: a scene all the same"
		[ "$(grep -ac 'h3 scene objects: .*: the pack menu_.* is failed: no scene' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "no map: the three lobbies did not each go without their scene"
		[ "$(grep -ac 'ui: screen pc.h3.campaign.screen\|ui: screen pc.h3.mp.screen\|ui: screen pc.h3.forge.lobby' "$log")" -ge 3 ] ||
			fail h3lobbyscenes "no map: the three lobbies did not open" ;;
	h3profiles)
		# (a first start: CAMPAIGN; Cross on CREATE NEW PROFILE: spaces, refused; Cross again: Rookie,
		# made and played, the lobby; Circle: SELECT PROFILE again, Rookie there)
		run h3profiles "$menus_data" 30 "$h3pad a wait:150:3000 a wait:150:2500 a wait:150:4000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=  |Rookie"
		check_h3 h3profiles halo3
		log=$out/h3profiles/run.log
		grep -aq "menus: Halo 3 screen h3/profiles/screen for ui.shell.main_menu.load_campaign_player_profile" "$log" ||
			fail h3profiles "SELECT PROFILE did not stand for the Xbox's campaign profile screen"
		grep -aq "menus: h3 profiles: 0 profiles (campaign)" "$log" &&
			grep -aq "menus: h3 profiles: CREATE NEW PROFILE (1 of 1)" "$log" ||
			fail h3profiles "a first start did not show CREATE NEW PROFILE alone"
		grep -aq 'menus: h3 profiles: the name "" was refused: A profile.s name needs a letter or a number.' "$log" ||
			fail h3profiles "a name of spaces was not refused"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "menus: h3 profiles: chose Rookie (campaign)" "$log" ||
			fail h3profiles "CREATE NEW PROFILE did not make and choose Rookie"
		grep -aq "ui: screen pc.h3.campaign.screen" "$log" || fail h3profiles "the new profile did not go on to the lobby"
		grep -aq "menus: h3 profiles: 1 profile (campaign): Rookie" "$log" || fail h3profiles "Rookie was not listed after"
		# (the card's Spartan: none for CREATE NEW PROFILE, Rookie's in its colour once listed, let go
		# with the screen)
		grep -aE "menus: h3 profiles: the card|h3 scene object 6" "$log"
		grep -aq "menus: h3 profiles: the card's Spartan asked for in colour" "$log" &&
			grep -aq "menus: h3 profiles: the card shows the Spartan" "$log" ||
			fail h3profiles "the card did not show Rookie's Spartan"
		grep -aq "menus: h3 profiles: the card's Spartan hidden" "$log" ||
			fail h3profiles "the card's Spartan was not let go (CREATE NEW PROFILE, or the screen gone)"
		file=$(ls "$out"/h3profiles/save/u/UDATA/*/blam.sav 2>/dev/null | head -1)
		[ -n "$file" ] && [ "$(profile_fields "$file" | cut -d'|' -f1)" = Rookie ] ||
			fail h3profiles "Rookie's blam.sav was not written"
		h3_no_xbox_profiles h3profiles
		# (Menu style Halo CE: the Xbox's campaign profile screen, as ever)
		profile_from=${HALO_TEST_PROFILE:-} run h3profiles_classic "$menus_data" 20 "wait:150:3000 a wait:150:4000" HALO_MENU_STYLE=classic
		grep -aq "ui: screen ui.shell.main_menu.load_campaign_player_profile" "$out/h3profiles_classic/run.log" ||
			fail h3profiles "Menu style Halo CE did not open the Xbox's campaign profile screen"
		grep -aq "ui: screen pc.h3" "$out/h3profiles_classic/run.log" && fail h3profiles "a Halo 3 screen opened with Menu style Halo CE"
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP (h3profiles): no HALO_TEST_PROFILE: its name taken, the delete"; continue
		fi
		# (the test profile's: CAMPAIGN; Triangle: its own name, taken; Triangle: Rookie, made and played;
		# Circle: SELECT PROFILE on Rookie; Square: the question on CANCEL, down, DELETE PROFILE)
		taken=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f1)
		profile_from=$HALO_TEST_PROFILE run h3profiles_delete "$menus_data" 40 "$h3pad a wait:150:3000 y wait:150:2500 y wait:150:4000 b wait:150:3000 x wait:150:2500 down:150:700 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=$taken|Rookie"
		check_h3 h3profiles_delete halo3
		log=$out/h3profiles_delete/run.log
		grep -aq "menus: h3 profiles: the name \"$taken\" was refused: Another profile has that name." "$log" ||
			fail h3profiles "the test profile's name was not refused as taken"
		grep -aq "menus: h3 profiles: 2 profiles (campaign): .*Rookie" "$log" || fail h3profiles "Rookie was not listed beside the test profile"
		grep -aq "ui: screen pc.h3.profdel.screen" "$log" || fail h3profiles "Square did not ask before deleting"
		grep -aq "menus: h3 profiles: deleted Rookie" "$log" || fail h3profiles "DELETE PROFILE did not delete Rookie"
		grep -a "menus: h3 profiles: [0-9]* profiles\? (campaign)" "$log" | tail -1 | grep -q "1 profile (campaign): $taken" ||
			fail h3profiles "the list after the delete was not the test profile alone"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3profiles_delete/save/u/UDATA/869079979327/blam.sav" ||
			fail h3profiles "the test profile's blam.sav changed"
		h3_no_xbox_profiles h3profiles_delete ;;
	h3proftouch)
		skip_null h3proftouch "taps" && continue
		# (CAMPAIGN's row on the main menu; SELECT PROFILE's New Profile; the lobby's Back)
		run h3proftouch "$menus_data" 22 "wait:150:14000" HALO_MENU_STYLE=halo3 HALO_TEST_TEXT_INPUT=Rookie \
			"HALO_TEST_TAPS=4000:60,278 8000:400,430 12000:140,455"
		check_h3 h3proftouch halo3
		log=$out/h3proftouch/run.log
		grep -aq "ui: screen pc.h3.profiles.screen" "$log" || fail h3proftouch "a tap on CAMPAIGN did not open SELECT PROFILE"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "ui: screen pc.h3.campaign.screen" "$log" ||
			fail h3proftouch "a tap on New Profile did not make one and go on to the lobby"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.profiles.screen' ||
			fail h3proftouch "a tap on the lobby's Back did not go back to SELECT PROFILE" ;;
	h3customize)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3customize"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CUSTOMIZATION: the profile, Cross; PLAYER NAME, Cross: typed; COLOR, right three times: RED;
		# past MODEL; STICK LAYOUT, right: SOUTHPAW; down two to LOOK SENSITIVITY, right twice; INVERT LOOK, Cross:
		# YES; Circle: saved; Circle)
		custom_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 a wait:150:2000"
		custom_pad="$custom_pad down:150:600 right:150:500 right:150:500 right:150:500 down:150:600 down:150:600 right:150:600"
		custom_pad="$custom_pad down:150:600 down:150:600 right:150:500 right:150:500 down:150:600 a wait:150:1500 b wait:150:3000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3customize "$menus_data" 45 "$custom_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Chief"
		check_h3 h3customize halo3
		log=$out/h3customize/run.log
		grep -aq "menus: Halo 3 screen h3/custprof/screen for ui.shell.main_menu.settings_select.player_setup.player_profile_select_screen" "$log" ||
			fail h3customize "CUSTOMIZATION did not stand for the Xbox's profile settings"
		grep -aq "ui: screen pc.h3.custom.screen" "$log" || fail h3customize "the editor did not open"
		grep -aq "menus: h3 customize: saved Vita Chief: color 2, stick 1, buttons 0, look 5, invert 1" "$log" ||
			fail h3customize "Circle did not save the changes"
		h3_no_xbox_profiles h3customize
		# (CUSTOMIZATION's profile list: the card shows the focused profile's Spartan, as SELECT
		# PROFILE's, before the editor's own preview)
		grep -aq "menus: h3 profiles: [0-9]* profiles* (customization)" "$log" &&
			grep -aq "menus: h3 profiles: the card's Spartan asked for in colour" "$log" &&
			grep -aq "menus: h3 profiles: the card shows the Spartan" "$log" ||
			fail h3customize "CUSTOMIZATION's profile list did not show the profile's Spartan"
		# (the preview: the scene packs' Spartan in the editor's window, in the profile's colour - its
		# RANDOM shown white -, made again in RED as COLOR changes, let go with the editor)
		grep -aE "h3 customize: .*preview|h3 scene object 6|h3 scene: h3/custom" "$log"
		grep -aq "menus: h3 scene: h3/custom/screen: camera point multiplayer" "$log" ||
			fail h3customize "the editor's own camera was not set"
		grep -aq "menus: h3 customize: preview Spartan asked for in colour 0" "$log" ||
			fail h3customize "the preview's Spartan was not asked for"
		grep -aq "menus: h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 0: made" "$log" &&
			grep -aq "menus: h3 customize: the preview shows the Spartan" "$log" ||
			fail h3customize "the preview did not show the Spartan"
		grep -aq "menus: h3 customize: preview Spartan recoloured 2" "$log" &&
			grep -aq "menus: h3 scene object 6: characters.cyborg_mp.cyborg_mp (menu_spartan) at .* colour 2: made" "$log" ||
			fail h3customize "the preview was not recoloured RED"
		grep -aq "menus: h3 customize: the preview shows the swatch" "$log" &&
			fail h3customize "the preview fell back to the swatch while it was recoloured"
		grep -aq "menus: h3 customize: preview Spartan hidden" "$log" && grep -aq "menus: h3 scene object 6: deleted" "$log" ||
			fail h3customize "the preview's Spartan was not let go with the editor"
		# (the profile's file: the fields changed, the others as they were; renamed, its folder too)
		file=$(ls "$out"/h3customize/save/u/UDATA/*/blam.sav 2>/dev/null | head -1)
		before=$(profile_fields "$HALO_TEST_PROFILE")
		after=$([ -n "$file" ] && profile_fields "$file")
		echo "profile before: $before"
		echo "profile after:  $after"
		[ "$(echo "$after" | cut -d'|' -f1-6)" = "Vita Chief|2|1|0|5|1" ] || fail h3customize "blam.sav does not hold the changes"
		[ "$(echo "$after" | cut -d'|' -f7-11)" = "$(echo "$before" | cut -d'|' -f7-11)" ] ||
			fail h3customize "blam.sav's other fields changed"
		[ -n "$file" ] && [ "$(tr -d '\0' < "$(dirname "$file")/SaveMeta.xbx")" = "Vita Chief" ] ||
			fail h3customize "SaveMeta.xbx does not name the profile Vita Chief"
		# (started again on that save: the editor shows the changes; Circle saves nothing; and with
		# HALO_SCENE_PACKS=0 no Spartan: the window keeps the colour's swatch)
		rm -rf "$out/h3customize_saved"
		cp -r "$out/h3customize/save" "$out/h3customize_saved"
		save_from=$out/h3customize_saved run h3customize_again "$menus_data" 25 \
			"$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000 b wait:150:3000" HALO_MENU_STYLE=halo3 \
			HALO_SCENE_PACKS=0
		check_h3 h3customize_again halo3
		grep -aqE "menus: h3 customize: the preview shows the Spartan|h3 scene object 6: .* made" "$out/h3customize_again/run.log" &&
			fail h3customize "HALO_SCENE_PACKS=0: the preview showed a Spartan all the same"
		grep -aq "menus: h3 customize: editing Vita Chief: color 2, stick 1, buttons 0, look 5, invert 1" "$out/h3customize_again/run.log" ||
			fail h3customize "the changes were not there after a restart"
		grep -aq "menus: h3 customize: Vita Chief: no changes" "$out/h3customize_again/run.log" ||
			fail h3customize "leaving the editor unchanged saved anyway" ;;
	h3customrows)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3customrows"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CUSTOMIZATION: the profile, Cross: the editor on PLAYER NAME, the preview's Spartan made;
		# then down through the nine rows below to VIBRATION, a second and a half on each, up to
		# AUTO-CENTER, down to VIBRATION again; Circle, Circle)
		rows_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:5000"
		rows_pad="$rows_pad @rows-start:0:300"
		for row in 1 2 3 4 5 6 7 8 9; do rows_pad="$rows_pad down:150:1500"; done
		rows_pad="$rows_pad up:150:1500 down:150:1500 @rows-end:0:300 b wait:150:2500 b wait:150:2500"
		# (the testers' presets: Performance - insider.17's halo.log, Ben 10 - and the lowest Custom - xal)
		performance="HALO_RENDER_SCALE=dynamic HALO_MODEL_LOD_SCALE=0.5 HALO_MIN_OBJECT_PIXELS=8 HALO_SCENERY_UPDATE_DIVISOR=4"
		performance="$performance HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=6 HALO_SUN_RAYS=0 HALO_VITA_SHADOWS=0"
		performance="$performance HALO_MAX_SCENE_LIGHTS=4 HALO_VITA_EFFECTS_QUALITY=1 HALO_PARTICLE_RENDER_DIVISOR=2 HALO_AI_THINK_DIVISOR=0"
		performance="$performance HALO_SOUND_MANAGER_DIVISOR=2 HALO_PARTICLE_BUDGET=reduced HALO_DYNAMIC_RES_MIN=0.5 HALO_UPSCALE_FILTER=0"
		lowest="HALO_RENDER_SCALE=dynamic HALO_MODEL_LOD_SCALE=0.35 HALO_MIN_OBJECT_PIXELS=12 HALO_SCENERY_UPDATE_DIVISOR=4"
		lowest="$lowest HALO_LIGHTING_REFRESH_DIVISOR=3 HALO_SOUND_OBSTRUCTION_TICKS=3 HALO_SUN_RAYS=0 HALO_VITA_SHADOWS=0"
		lowest="$lowest HALO_MAX_SCENE_LIGHTS=2 HALO_VITA_EFFECTS_QUALITY=0 HALO_PARTICLE_RENDER_DIVISOR=4 HALO_AI_THINK_DIVISOR=3"
		lowest="$lowest HALO_SOUND_MANAGER_DIVISOR=1 HALO_PARTICLE_BUDGET=low HALO_DYNAMIC_RES_MIN=0.5 HALO_UPSCALE_FILTER=0"
		for preset in performance lowest; do
			name=h3customrows_$preset
			# shellcheck disable=SC2086
			profile_from=$HALO_TEST_PROFILE run $name "$menus_data" 45 "$rows_pad" HALO_MENU_STYLE=halo3 \
				HALO_PIXEL_SHADER_LOG=1 ${!preset}
			check_h3 $name halo3
			log=$out/$name/run.log
			grep -aq "ui: screen pc.h3.custom.screen" "$log" || fail $name "the editor did not open"
			grep -aq "menus: h3 customize: the preview shows the Spartan" "$log" || fail $name "the preview did not show the Spartan"
			grep -aq "test pad: @rows-start" "$log" && grep -aq "test pad: @rows-end" "$log" ||
				fail $name "the rows were not walked"
			# (the models' shader set before the walk: the Spartan drawn)
			sed -n '1,/test pad: @rows-start/p' "$log" | grep -aq "pixel shader: [0-9a-f]*: modes 00018421," ||
				fail $name "no model was drawn before the rows were walked"
			# (and nothing new of theirs while the rows were)
			changed=$(sed -n '/test pad: @rows-start/,/test pad: @rows-end/p' "$log" | grep -a "pixel shader: [0-9a-f]*: modes 00018421,")
			echo "$name: models' pixel shaders new while the rows were walked: ${changed:-none}"
			[ -z "$changed" ] || fail $name "the preview's Spartan was drawn with another pixel shader on a row"
		done ;;
	h3nameplate)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3nameplate"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the main menu at a start: the nameplate; then CUSTOMIZATION as h3customize - the name typed,
		# COLOR right three times: RED -, Circle: saved, Circle: the main menu again, its nameplate)
		plate_name=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f1)
		plate_color=$(profile_fields "$HALO_TEST_PROFILE" | cut -d'|' -f2)
		plate_pad="$h3pad @00-plate-at-start:0:300 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000"
		plate_pad="$plate_pad a wait:150:3000 a wait:150:2000 down:150:600 right:150:500 right:150:500 right:150:500"
		plate_pad="$plate_pad b wait:150:3000 b wait:150:3000 up:150:500 up:150:500 up:150:500 up:150:500 up:150:500 wait:150:1000"
		plate_pad="$plate_pad @01-plate-customized:0:300"
		rm -rf "$out/h3nameplate_shots"
		mkdir -p "$out/h3nameplate_shots"
		profile_from=$HALO_TEST_PROFILE run h3nameplate "$menus_data" 40 "$plate_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Chief" HALO_NET_PLAYER_NAME=user549 HALO_SCREENSHOT_DIR="$out/h3nameplate_shots"
		check_h3 h3nameplate halo3
		log=$out/h3nameplate/run.log
		grep -a "menus: nameplate" "$log"
		# (a start, no profile chosen: the profile's name and colour, its frame the colour's - 0, the
		# green, for a profile of no colour -; never the user name)
		frame=$((plate_color < 0 || plate_color >= 18 ? 0 : plate_color + 1))
		grep -a "menus: nameplate:" "$log" | head -1 | grep -q "menus: nameplate: $plate_name, colour $plate_color (frame $frame)$" ||
			fail h3nameplate "at the start the nameplate did not show $plate_name's name and colour"
		grep -aq "menus: nameplate: user549" "$log" && fail h3nameplate "the nameplate showed the user name"
		grep -aq "menus: h3 customize: saved Vita Chief: color 2" "$log" || fail h3nameplate "CUSTOMIZATION did not save"
		# (after the save, at once: the new name on the red bar, frame 3)
		grep -aq "menus: nameplate: Vita Chief, colour 2 (frame 3)$" "$log" ||
			fail h3nameplate "the nameplate did not change to the saved name and colour"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' || fail h3nameplate "not back at the main menu"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3nameplate" &&
			cp "$out"/h3nameplate_shots/*.bmp "$HALO_TEST_SHOTS/h3nameplate/" 2>/dev/null ;;
	h3models)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ] || [ ! -r "$ce_maps/extinction.map" ] || [ ! -r "$menus_data/maps/sounds.map" ]; then
			echo "--- h3models"; echo "SKIP: no HALO_TEST_PROFILE, extinction.map (HALO_TEST_CE_MAPS) or sounds.map"; continue
		fi
		# (a maps folder of the data's and the Custom Edition maps, links)
		rm -rf "$out/models_data"
		mkdir -p "$out/models_data/maps"
		for file in "$(cd "$menus_data" && pwd)"/maps/*.map; do ln -sfn "$file" "$out/models_data/maps/"; done
		for file in extinction Covenant_V_Marines_Beta_5; do
			[ -r "$ce_maps/$file.map" ] && ln -sfn "$(cd "$ce_maps" && pwd)/$file.map" "$out/models_data/maps/$file.map"
		done
		# (CUSTOMIZATION: the profile, Cross; down two to MODEL; right: the Extinction Spartan, built and
		# shown; right: the protected map's; left: the Spartan (Extinction) again; Circle; Circle)
		models_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000"
		models_pad="$models_pad down:150:600 down:150:600 right:150:500 wait:150:8000 right:150:500 wait:150:8000"
		models_pad="$models_pad left:150:500 wait:150:4000 b wait:150:3000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3models "$out/models_data" 55 "$models_pad" HALO_MENU_STYLE=halo3
		check_h3 h3models halo3
		log=$out/h3models/run.log
		grep -aq "menus: h3 customize: 2 player models in the maps folder's Custom Edition maps; this profile's: -1" "$log" ||
			fail h3models "the editor did not find the two models"
		grep -aq "menus: h3 customize: model SPARTAN (EXTINCTION)" "$log" || fail h3models "MODEL did not step to the Extinction Spartan"
		grep -aq "scene packs: pm_[0-9a-f]*: built from extinction" "$log" || fail h3models "its pack was not built"
		grep -aq "menus: h3 scene object 6: characters.spartan.spartan_mp (pm_[0-9a-f]*) at .* made" "$log" &&
			grep -aq "menus: h3 customize: model Spartan (Extinction): loaded" "$log" ||
			fail h3models "the preview did not show the Extinction Spartan"
		grep -aq "menus: h3 scene object 6: protected.Covenant_V_Marines_Beta_5.bipd [0-9]* (pm_[0-9a-f]*) at .* made" "$log" ||
			fail h3models "the protected map's model was not shown"
		grep -aq "player models: 869079979327 chose Spartan (Extinction)" "$log" &&
			grep -aq "menus: h3 customize: birch: model 0 saved" "$log" || fail h3models "Circle did not keep the choice"
		grep -q "^869079979327 extinction 97888771 c5372723$" "$out/h3models/save/t/player_models.txt" ||
			fail h3models "t:\\player_models.txt does not hold the choice"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3models/save/u/UDATA/869079979327/blam.sav" ||
			fail h3models "blam.sav changed (the model is kept apart from it)"
		rm -rf "$out/h3models_saved"
		cp -r "$out/h3models/save" "$out/h3models_saved"
		save_from=$out/h3models_saved run h3models_again "$out/models_data" 25 \
			"$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:5000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3
		grep -aq "menus: h3 customize: 2 player models in the maps folder's Custom Edition maps; this profile's: 0" \
			"$out/h3models_again/run.log" || fail h3models "started again, the editor did not show the choice"
		;;
	h3modelsmany)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ] || [ ! -r "$ce_maps/extinction.map" ] || [ ! -r "$menus_data/maps/sounds.map" ]; then
			echo "--- h3modelsmany"; echo "SKIP: no HALO_TEST_PROFILE, extinction.map (HALO_TEST_CE_MAPS) or sounds.map"; continue
		fi
		# (a maps folder of the data's and fourteen links to extinction.map under names of their own:
		# fourteen models, each a pack of its own)
		rm -rf "$out/manymodels_data"
		mkdir -p "$out/manymodels_data/maps"
		for file in "$(cd "$menus_data" && pwd)"/maps/*.map; do ln -sfn "$file" "$out/manymodels_data/maps/"; done
		for index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14; do
			ln -sfn "$(cd "$ce_maps" && pwd)/extinction.map" "$out/manymodels_data/maps/model$index.map"
		done
		# (CUSTOMIZATION: the profile, Cross; down two to MODEL; right fourteen times, each model left to
		# rest and show; then right eight times quickly, and a rest)
		many_pad="$h3pad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:3000"
		many_pad="$many_pad down:150:600 down:150:600"
		for index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14; do many_pad="$many_pad right:150:3500"; done
		many_pad="$many_pad left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:80 left:80:4000 b wait:150:3000"
		profile_from=$HALO_TEST_PROFILE run h3modelsmany "$out/manymodels_data" 85 "$many_pad" HALO_MENU_STYLE=halo3
		check_h3 h3modelsmany halo3
		log=$out/h3modelsmany/run.log
		grep -aE "scene packs: pm_.*(loaded|let go|refused|waits)|h3 customize: model .*: (loaded|failed)" "$log" | cut -c1-160
		[ "$(grep -a 'menus: h3 scene object 6: characters.spartan.spartan_mp (pm_[0-9a-f]*) at .* made' "$log" |
			sed 's/.*(\(pm_[0-9a-f]*\)).*/\1/' | sort -u | wc -l)" -ge 14 ] ||
			fail h3modelsmany "the fourteen models were not each shown"
		grep -aqE "scene packs: pm_.*: (refused|waits)|h3 customize: model .*: failed" "$log" &&
			fail h3modelsmany "a model's pack was refused, or waited for room (the packs passed were kept)"
		[ "$(grep -ac 'scene packs: pm_[0-9a-f]*: let go' "$log")" -ge 13 ] ||
			fail h3modelsmany "the models passed were not let go"
		# (the quick presses, back from the fourteenth to the sixth: none of the seven passed loaded again)
		for index in 07 08 09 10 11 12 13; do
			[ "$(grep -ac "scene packs: pm_[0-9a-f]*: loaded from model$index " "$log")" = 1 ] ||
				fail h3modelsmany "model$index, passed quickly, was loaded again"
		done ;;
	h3damaged)
		# (two damaged profiles: Broken, a 0-byte blam.sav; Garbage, 512 bytes of garbage)
		rm -rf "$out/h3damaged_save"
		mkdir -p "$out/h3damaged_save/u/UDATA/000000000001" "$out/h3damaged_save/u/UDATA/000000000002"
		: > "$out/h3damaged_save/u/UDATA/000000000001/blam.sav"
		printf 'B\0r\0o\0k\0e\0n\0' > "$out/h3damaged_save/u/UDATA/000000000001/SaveMeta.xbx"
		head -c 512 /dev/urandom > "$out/h3damaged_save/u/UDATA/000000000002/blam.sav"
		printf 'G\0a\0r\0b\0a\0g\0e\0' > "$out/h3damaged_save/u/UDATA/000000000002/SaveMeta.xbx"
		# (CAMPAIGN: Cross on each, refused; CREATE NEW PROFILE, Cross: Rookie, the lobby; Circle twice:
		# the main menu, where no profile is in use; CUSTOMIZATION, on the first listed - a damaged
		# one: Rookie, made last, is listed last -: Cross, refused)
		save_from=$out/h3damaged_save run h3damaged "$menus_data" 45 "$h3pad a wait:150:3000 a wait:150:2000 down:150:700 a wait:150:2000 down:150:700 a wait:150:4000 b wait:150:3000 b wait:150:3000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_TEXT_INPUT=Rookie
		check_h3 h3damaged halo3
		log=$out/h3damaged/run.log
		for damaged in Broken Garbage; do
			grep -aq "menus: h3 profiles: $damaged is damaged: not chosen" "$log" ||
				fail h3damaged "$damaged (a damaged profile) was not refused"
		done
		grep -aq "menus: h3 profiles: .* is damaged: not edited" "$log" || fail h3damaged "CUSTOMIZATION edited a damaged profile"
		grep -aq "menus: h3 profiles: created Rookie" "$log" && grep -aq "ui: screen pc.h3.campaign.screen" "$log" ||
			fail h3damaged "CREATE NEW PROFILE did not make one and go on to the lobby"
		grep -aq "menus: h3 profiles: chose \(Broken\|Garbage\)" "$log" && fail h3damaged "a damaged profile was played"
		h3_no_xbox_profiles h3damaged
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP (h3damaged): no HALO_TEST_PROFILE: the damaged saved game"; continue
		fi
		# (the test profile with a saved game of garbage, as large as one: CAMPAIGN, the profile, the lobby)
		rm -rf "$out/h3damaged_game_save"
		mkdir -p "$out/h3damaged_game_save/u/UDATA/869079979327"
		head -c 16777216 /dev/urandom > "$out/h3damaged_game_save/u/UDATA/869079979327/savegame.bin"
		profile_from=$HALO_TEST_PROFILE save_from=$out/h3damaged_game_save run h3damaged_game "$menus_data" 25 "$h3pad a wait:150:3000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3damaged_game halo3
		log=$out/h3damaged_game/run.log
		grep -aq "menus: h3 campaign: the saved game is damaged: it can't be loaded" "$log" ||
			fail h3damaged "the lobby did not say the saved game is damaged"
		grep -aq "menus: h3 campaign caption: .* at Mission Start" "$log" || fail h3damaged "the lobby did not start at Mission Start"
		cmp -s "$HALO_TEST_PROFILE" "$out/h3damaged_game/save/u/UDATA/869079979327/blam.sav" ||
			fail h3damaged "the test profile's blam.sav changed" ;;
	h3oddrows)
		# (the Halo 3 lobby: down five to BROWSE GAMES, A: the profile (A joins, A the profile, A):
		# the games browser; OpenCE's: Multiplayer, SERVER BROWSER, the profile first (A A A))
		run h3oddrows "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_ODD=1
		run h3oddrows_classic "$menus_data" 45 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_ODD=1
		check_h3 h3oddrows halo3
		for name in h3oddrows h3oddrows_classic; do
			log=$out/$name/run.log
			coop="Co-op · Legendary"
			[ $name = h3oddrows_classic ] && coop="Co-op (Legendary)"
			for row in "Spartan | Slayer | Blood Gulch | 8/128" "Jos?? ??? Ma??ana | CTF | Chill Out | 3/16" \
				"true legendary run (cut) | $coop | The Pillar of Autumn (cut) | 3/4" "teto | Co-op | - | 1/4" \
				"BIG TEAM 24*7 | - | - | -" "HALO UNIVERSAL CUSTOM SERVER 24/ (cut) | Team Slayer | bigassv2,104 | 24/32"; do
				grep -aqF "$row" "$log" || fail h3oddrows "$name: no row \"$row\""
			done
			grep -aE "menus: browser row [0-9]+: " "$log" | grep -aqE "row [0-9]+:  ?\|" && fail h3oddrows "$name: a row without a name"
		done
		grep -aqF "xXxNoScopeLegendsxXxForeverMore (cut)" "$out/h3oddrows/run.log" ||
			fail h3oddrows "the long single-word name was not cut to its column" ;;
	profilepaths)
		# (every multiplayer entry path asks for the profile before its games, map list or lobby:
		# Menu style Halo CE's Xbox profile screen, Halo 3's SELECT PROFILE; the owner, insider.12)
		d="down:150:500"
		cmp="wait:150:3000 down:150:800 a wait:150:3000"
		run pp_c_browser "$menus_data" 22 "$cmp a wait:150:4000" HALO_MENU_STYLE=classic $h3net
		run pp_c_lan "$menus_data" 22 "$cmp $d a wait:150:4000" HALO_MENU_STYLE=classic HALO_NET_ONLINE=false
		run pp_c_code "$menus_data" 26 "$cmp $d $d a wait:150:3000 a wait:150:2000 $d a wait:150:5000" HALO_MENU_STYLE=classic \
			$h3net HALO_TEST_TEXT_INPUT=ABCD-EFGH
		run pp_c_create_lan "$menus_data" 22 "$cmp $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=classic HALO_NET_ONLINE=false
		run pp_h_start_link "$menus_data" 22 "$h3mppad $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 HALO_NET_ONLINE=false
		run pp_h_browse_link "$menus_data" 22 "$h3mppad $d $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 HALO_NET_ONLINE=false
		run pp_h_start_online "$menus_data" 22 "$h3mppad $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 $h3net
		run pp_h_browse_online "$menus_data" 22 "$h3mppad $d $d $d $d $d a wait:150:4000" HALO_MENU_STYLE=halo3 $h3net
		run pp_h_code_online "$menus_data" 28 "$h3mppad $d $d $d $d $d $d a wait:150:3000 a wait:150:2000 $d a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_TEXT_INPUT=ABCD-EFGH
		run pp_h_start_adhoc "$menus_data" 28 "$h3mppad $d $d $d a wait:150:8000" HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true \
			HALO_ADHOC_TEST=join
		run pp_h_browse_adhoc "$menus_data" 28 "$h3mppad $d $d $d $d $d a wait:150:8000" HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true \
			HALO_ADHOC_TEST=join
		echo "--- profilepaths"
		for name in pp_c_browser pp_c_lan pp_c_code pp_c_create_lan pp_h_start_link pp_h_browse_link pp_h_start_online \
			pp_h_browse_online pp_h_code_online pp_h_start_adhoc pp_h_browse_adhoc; do
			log=$out/$name/run.log
			case $name in pp_c_*) want='ui: screen ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen' ;;
			*) want='ui: screen pc.h3.join.screen' ;; esac
			if grep -aq "$want" "$log"; then
				echo "$name: the profile screen opened ($(grep -a "ui: screen" "$log" | tail -1 | sed 's/.*ui: screen //'))"
			else
				fail profilepaths "$name: no profile screen ($(grep -a "ui: screen" "$log" | tail -3 | sed 's/.*ui: screen //' | tr '\n' ' '))"
			fi
			grep -aqiE "assert|exception|halt" "$log" && fail profilepaths "$name: an assertion, exception or halt"
		done ;;
	browserrows)
		# (OpenCE's server browser, Menu style Halo CE: Multiplayer, SERVER BROWSER, the profile (A joins, A
		# the profile, A), with 16
		# made-up games of names of every sort, PC ones among them)
		# (the frame counter on, as on the owner's Vita: its digits are the HUD's tall font, whose
		# rows in the glyph cache leave room for fewer of the browser's glyphs)
		# (R twice: pages down, the PC GAMES group's rows under the Vitas')
		init_line='display_framerate 1' run browserrows "$menus_data" 36 "wait:150:2000 down:150:800 a wait:150:4000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 rt:150:4000 rt:150:4000" \
			HALO_MENU_STYLE=classic $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_NAMES=1 HALO_TEST_BROWSER_PC=1
		log=$out/browserrows/run.log
		echo "--- browserrows"
		grep -aE "menus: browser row [0-9]+: |text: the glyph cache" "$log" | sort -u | head -16
		[ "$(cat "$out/browserrows/exit")" = 0 ] || fail browserrows "exit $(cat "$out/browserrows/exit")"
		rows=$(grep -aE "menus: browser row [0-9]+: " "$log" | sed 's/.*browser row \([0-9]*\):.*/\1/' | sort -un | wc -l)
		[ "$rows" -ge 12 ] || fail browserrows "$rows rows filled (12 wanted)"
		grep -aE "menus: browser row [0-9]+: " "$log" | grep -aqE "row [0-9]+:  ?\|" && fail browserrows "a row without a name"
		for name in "DONT SKIP CINE." "Krazy's COOP" "~Yeti_Kraken~" "#1 xQuJoKe [V]" "the quick brown fox jumps over"; do
			grep -aqF "$name" "$log" || fail browserrows "no row named \"$name\""
		done
		# (every glyph of the screen kept: the cache going round each frame drew the later rows'
		# text as nothing on the Vita)
		grep -aq "text: the glyph cache .* goes round every frame" "$log" &&
			fail browserrows "the glyph cache went round every frame: the later rows' text would be missing on the Vita" ;;
	h3livebrowse)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3livebrowse"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the owner, insider.15: the first visit to MULTIPLAYER, BROWSE GAMES pressed as the lobby
		# went live: a press lost, then SELECT PROFILE asked for the profile the lobby already
		# played with. MULTIPLAYER on SYSTEM LINK with a profile, down five to BROWSE GAMES pressed
		# through the lobby going live - one every 300 ms from 600 ms after it opened -, A: the
		# games browser, the live game closed, no SELECT PROFILE; B back, then BROWSE GAMES again:
		# still none)
		d="down:150:300"
		pad="wait:150:3000 down:150:700 a wait:150:600 $d $d $d $d $d a wait:150:5000 b wait:150:5000 a wait:150:5000"
		profile_from=$HALO_TEST_PROFILE run h3livebrowse "$menus_data" 40 "$pad" HALO_MENU_STYLE=halo3
		check_h3 h3livebrowse halo3
		log=$out/h3livebrowse/run.log
		grep -aE "ui: screen|menus: h3 (live lobby|mp: browse|join)" "$log" | head -20
		grep -aq "menus: h3 live lobby: open on SYSTEM LINK" "$log" || fail h3livebrowse "the lobby did not go live"
		sed -n '/menus: h3 live lobby: open on/,$p' "$log" | grep -aq "menus: h3 live lobby: closed (the host browses the games)" ||
			fail h3livebrowse "BROWSE GAMES did not close the live game (pressed as it went live)"
		[ "$(grep -ac "ui: screen pc.h3.lan.screen" "$log")" = 2 ] || fail h3livebrowse "the games browser did not open twice"
		grep -aq "ui: screen pc.h3.join.screen" "$log" &&
			fail h3livebrowse "SELECT PROFILE asked for the profile the live lobby played with" ;;
	browserreopen)
		# (Halo 3's games browser opened and closed again and again, as the owner's crash, Oct 19 2026:
		# BROWSE GAMES, the profile, B and A again; OpenCE's browser opens once per run here - with
		# the test's brokers out of reach its SERVER BROWSER says internet play is off after the first)
		pad="$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000"
		for i in $(seq 12); do pad="$pad b wait:150:3000 a wait:150:5000"; done
		run browserreopen_h3 "$menus_data" 200 "$pad" \
			HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=34 HALO_TEST_BROWSER_NAMES=1 HALO_TEST_BROWSER_PC=1
		echo "--- browserreopen"
		for name in browserreopen_h3; do
			log=$out/$name/run.log
			[ "$(cat "$out/$name/exit")" = 0 ] || fail browserreopen "$name: exit $(cat "$out/$name/exit")"
			grep -aqiE "assert|exception|halt" "$log" && fail browserreopen "$name: an assertion, exception or halt"
			opens=$(grep -acE "ui: screen pc.(browser|h3.lan).screen" "$log")
			most=$(grep -a "ui: widget memory" "$log" | sed 's/.*at most \([0-9]*\) bytes.*/\1/' | sort -n | tail -1)
			# (the memory in use as each screen opens, the browser's: the same at the last open as at the second)
			held=$(grep -aA1 -E "ui: screen pc.(browser|h3.lan).screen" "$log" | grep -a "widget memory" | sed 's/.*memory \([0-9]*\) of.*/\1/' | sort -u | tr '\n' ' ')
			echo "$name: the browser opened $opens times; widget memory held as it opened: $held; at most ${most:-?} bytes"
			[ "$opens" -ge 10 ] || fail browserreopen "$name: the browser opened $opens times (10 wanted)"
			[ "$(echo $held | wc -w)" -le 2 ] || fail browserreopen "$name: the widget memory held grew from open to open ($held)"
			grep -aq "memory: the widget_memory_pool is full" "$log" &&
				fail browserreopen "$name: the widgets' memory ran out ($(grep -a "memory: the widget_memory_pool is full" "$log" | head -1 | sed 's/.*memory: //'))"
			# (a quarter of it free at the fullest: the browsers took 14-16 KB of the Xbox's 16)
			size=$(grep -a "ui: widget memory" "$log" | head -1 | sed 's/.* of \([0-9]*\) bytes.*/\1/')
			[ "${most:-0}" -le $((${size:-0} * 3 / 4)) ] ||
				fail browserreopen "$name: the widgets' memory $most of ${size:-?} bytes at the fullest"
		done ;;
	h3lan)
		# (down five to BROWSE GAMES, A: the profile (A joins, A the profile, A); the LAN browser:
		# down two, A; Y; Square; a tap on Back)
		run h3lan "$menus_data" 60 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 down:150:600 down:150:600 a wait:150:2000 y wait:150:2000 x wait:150:20000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_BROWSER_GAMES=14 HALO_TEST_BROWSER_PC=1 "HALO_TEST_TAPS=46000:150,122 50000:552,452"
		run h3lan_empty "$menus_data" 36 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:12000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3lan halo3
		log=$out/h3lan/run.log
		grep -aq "menus: h3 mp: browse the system link games; the profile, then the LAN browser" "$log" ||
			fail h3lan "BROWSE GAMES on SYSTEM LINK did not ask for the LAN browser"
		grep -aq "menus: the Halo 3 LAN browser for the System Link list" "$log" && grep -aq "ui: screen pc.h3.lan.screen" "$log" ||
			fail h3lan "the profile did not lead to the Halo 3 LAN browser"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.server_list" "$log" &&
			fail h3lan "the Xbox's System Link list opened with Menu style Halo 3"
		grep -aq "menus: the LAN browser (system link)" "$log" || fail h3lan "the LAN browser was not the system link one"
		grep -aq "menus: the LAN browser lists 14: Test game 02 (Hang 'Em High, King, 14/16, VITA+PC), Test game 04 (" "$log" ||
			fail h3lan "the LAN browser did not list the 14 games, the Vitas' first, the fullest first"
		grep -aq "menus: the browser's groups (All games): VITA GAMES (7), PC GAMES (7)" "$log" ||
			fail h3lan "the LAN browser did not group the games: VITA GAMES, then PC GAMES"
		grep -aq "menus: the server browser's game 3 of 14 chosen" "$log" || fail h3lan "down two did not choose the third game"
		grep -aq "menus: the LAN browser's game Test game .* is no longer listed" "$log" ||
			fail h3lan "A on a game the System Link list does not show did not say it is not listed"
		grep -aq "menus: the server browser sorts by name" "$log" || fail h3lan "Y did not sort the games by name"
		grep -aq "menus: the LAN browser looks again" "$log" || fail h3lan "Square did not look again"
		if [ $gxm_null = 1 ]; then
			echo "SKIP (h3lan): the taps on a row and on Back (a gxm-null harness: the GL build's check)"
		else
			grep -aq "menus: the LAN browser's game Test game 00 is no longer listed" "$log" ||
				fail h3lan "a tap on the first game's row (sorted by name, under VITA GAMES) did not press A on it"
			grep -a "ui: screen" "$log" | tail -1 | grep -qE '4way_start2join_screen|pc.h3.join.screen' ||
				fail h3lan "a tap on Back did not go back to the profile"
		fi
		check_h3 h3lan_empty halo3
		grep -aq 'menus: the server browser says "No games found on your network"' "$out/h3lan_empty/run.log" ||
			fail h3lan "with no games the LAN browser did not say none were found" ;;
	pctext)
		skip_null pctext "the PC build's text box typed into (keyboard)" && continue
		# (the PC build's text box, port/linux/src/pc_text_entry.c: typed into through SDL's own
		# events, HALO_TEST_KEYS, and the gamepad's on-screen keyboard, HALO_TEST_PAD; no
		# HALO_TEST_TEXT_INPUT. Each shot named pctext_* in the case's folder, and in HALO_TEST_SHOTS)
		# (the keyboard alone: Enter on CAMPAIGN and on CREATE NEW PROFILE; "xo" and an é (left out),
		# a paste of "okie" and an emoji (left out), Home, Delete, "R", End, ten X's (five fit: 11 at
		# most), five Backspaces; E and Tab pressed (the menu's X and Y: nothing behind the box);
		# Enter: Rookie made; Escape on the lobby: back)
		bs="key:BackSpace key:BackSpace key:BackSpace key:BackSpace key:BackSpace"
		run pctext_keyboard "$menus_data" 34 "" HALO_MENU_STYLE=halo3 HALO_SCREENSHOT_DIR="$out/pctext_keyboard" \
			"HALO_TEST_KEYS=wait:6000 key:Return wait:3000 key:Return typing @pctext_keyboard_open text:xo%C3%A9 clip:okie%F0%9F%98%80 key:ctrl+V key:Home key:Delete text:R key:End key:E key:Tab text:XXXXXXXXXX @pctext_keyboard_full $bs @pctext_keyboard_typed key:Return closed wait:4000 key:Escape wait:3000"
		# (a gamepad alone: CAMPAIGN, CREATE NEW PROFILE; on the grid, from q: SHIFT (the left shoulder),
		# right three, A: R; SHIFT off, right five, A A: oo; down, left, A: k; up, A: i; left five, A A
		# B: e (an e too many, deleted); Start: Rookie made)
		q="150:350"
		run pctext_pad "$menus_data" 46 "$h3pad a wait:150:3000 a wait:150:2500 white:$q right:$q right:$q right:$q a:$q white:$q right:$q right:$q right:$q right:$q right:$q a:$q a:$q down:$q left:$q a:$q up:$q a:$q left:$q left:$q left:$q left:$q left:$q a:$q a:$q b:$q @pctext_pad_typed start:150:4000 wait:150:3000" \
			HALO_MENU_STYLE=halo3 HALO_SCREENSHOT_DIR="$out/pctext_pad" HALO_TEST_KEYS=wait:1
		# (cancels: Escape on the box - not also the menu's back -; Y on the gamepad's - not also the
		# menu's New Profile)
		run pctext_cancel "$menus_data" 26 "" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_KEYS=wait:6000 key:Return wait:3000 key:Return typing text:Nope key:Escape closed wait:3000"
		run pctext_cancel_pad "$menus_data" 26 "$h3pad a wait:150:3000 a wait:150:2500 a:150:400 y:150:3000 wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_KEYS=wait:1
		# (JOIN WITH CODE on the multiplayer lobby, internet play on - its brokers TEST-NET addresses -:
		# the code typed; a locked game's password in the games browser, typed: JOIN GAME tries it)
		d="down:150:500"
		run pctext_code "$menus_data" 30 "$h3mppad $d $d $d $d $d $d a wait:150:3000 a wait:150:3000 $d a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_SCREENSHOT_DIR="$out/pctext_code" "HALO_TEST_KEYS=typing text:abcd-efgh @pctext_code key:Return"
		run pctext_password "$menus_data" 60 "$h3mppad $d $d $d $d $d a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 wait:150:600 a wait:150:4000 a wait:150:3000 $d a wait:150:4000" \
			HALO_MENU_STYLE=halo3 $h3net HALO_TEST_BROWSER_GAMES=16 HALO_TEST_BROWSER_PC=1 \
			HALO_SCREENSHOT_DIR="$out/pctext_password" "HALO_TEST_KEYS=typing text:sec text:ret @pctext_password key:Return"
		for name in pctext_keyboard pctext_pad pctext_cancel pctext_cancel_pad pctext_code pctext_password; do
			check_h3 $name halo3
			grep -aE "text entry: |test keys: (text|key|clip)" "$out/$name/run.log" | head -8
			grep -aq "menus: typing .*: typed\|menus: typing .*: cancelled" "$out/$name/run.log" &&
				fail pctext "$name: the tests' answers typed in place of the text box"
		done
		log=$out/pctext_keyboard/run.log
		grep -aq 'text entry: "New profile.s name" opened (up to 11 characters)$' "$log" ||
			fail pctext "keyboard: the box did not open (or opened with the on-screen keyboard after the keyboard)"
		grep -aq "text entry: pasted" "$log" || fail pctext "keyboard: Ctrl+V pasted nothing"
		grep -aq 'text entry: "New profile.s name": done: "Rookie"' "$log" ||
			fail pctext "keyboard: the name was not Rookie (the editing keys, the paste, the 11 at most, the UTF-8)"
		grep -aq "menus: h3 profiles: created Rookie" "$log" || fail pctext "keyboard: Rookie was not made"
		grep -aq "ui: screen pc.h3.profdel.screen" "$log" && fail pctext "keyboard: E reached the menu behind the box"
		[ "$(grep -ac 'text entry: .* opened' "$log")" = 1 ] || fail pctext "keyboard: Tab or Enter reached the menu behind the box"
		sed -n '/text entry: .*done/,$p' "$log" | grep -a "ui: screen" | tail -1 | grep -q "pc.h3.profiles.screen" ||
			fail pctext "keyboard: Escape on the lobby did not go back to SELECT PROFILE"
		log=$out/pctext_pad/run.log
		grep -aq "text entry: .*the on-screen keyboard shown" "$log" || fail pctext "pad: the on-screen keyboard was not shown"
		grep -aq 'text entry: "New profile.s name": done: "Rookie"' "$log" && grep -aq "menus: h3 profiles: created Rookie" "$log" ||
			fail pctext "pad: Rookie was not typed on the on-screen keyboard and made"
		log=$out/pctext_cancel/run.log
		grep -aq 'text entry: "New profile.s name": cancelled' "$log" || fail pctext "cancel: Escape did not cancel"
		grep -aq "menus: h3 profiles: created" "$log" && fail pctext "cancel: a profile was made"
		sed -n '/text entry: .*cancelled/,$p' "$log" | grep -aq "ui: screen" &&
			fail pctext "cancel: Escape also went back on the menu behind the box"
		log=$out/pctext_cancel_pad/run.log
		grep -aq 'text entry: "New profile.s name": cancelled' "$log" || fail pctext "cancel: Y did not cancel"
		[ "$(grep -ac 'text entry: .* opened' "$log")" = 1 ] || fail pctext "cancel: Y also pressed New Profile behind the box"
		grep -aq "menus: h3 profiles: created" "$log" && fail pctext "cancel: Y made a profile"
		log=$out/pctext_code/run.log
		grep -aq 'text entry: "Code (ABCD-EFGH)": done: "abcd-efgh"' "$log" && grep -aq "menus: joining the code abcd-efgh" "$log" ||
			fail pctext "code: the code typed was not joined"
		log=$out/pctext_password/run.log
		grep -aq 'text entry: "Password": done (a password of 6 characters)' "$log" || fail pctext "password: not typed"
		grep -aq "secret" "$log" && fail pctext "password: the password is in the log"
		grep -aq "menus: the public game Test game 02 is no longer listed" "$log" ||
			fail pctext "password: JOIN GAME did not try the game with the password typed"
		if [ -n "${HALO_TEST_SHOTS:-}" ]; then
			for shot in "$out"/pctext_*/pctext_*.bmp; do
				[ -s "$shot" ] && cp "$shot" "$HALO_TEST_SHOTS/"
			done
		fi ;;
	pcclassic)
		# (Menu style Halo CE on the PC build: the Xbox menus' names typed in the PC build's text box,
		# as the Halo 3 menus' are, not on the Xbox's on-screen keyboard - Grunt pirate, insider.18.
		# The keyboard alone: Enter on CAMPAIGN; the new profile's name box open on the name the Xbox
		# offers (New001); End, its six characters erased, "Rookie", Enter: made as the Xbox's Done
		# makes it, the difficulty next)
		bs="key:BackSpace key:BackSpace key:BackSpace key:BackSpace key:BackSpace key:BackSpace"
		run pcclassic_keyboard "$menus_data" 30 "" HALO_SCREENSHOT_DIR="$out/pcclassic_keyboard" \
			"HALO_TEST_KEYS=wait:5000 key:Return typing @pcclassic_keyboard_open key:End $bs text:Rookie @pcclassic_keyboard_typed key:Return closed wait:6000 @pcclassic_keyboard_after"
		# (a gamepad alone: A on CAMPAIGN; B six times erases New001; then pctext_pad's keys from q:
		# Rookie; Start)
		q="150:350"
		b6="b:$q b:$q b:$q b:$q b:$q b:$q"
		run pcclassic_pad "$menus_data" 40 "wait:150:5000 a wait:150:2500 $b6 white:$q right:$q right:$q right:$q a:$q white:$q right:$q right:$q right:$q right:$q right:$q a:$q a:$q down:$q left:$q a:$q up:$q a:$q left:$q left:$q left:$q left:$q left:$q a:$q a:$q b:$q @pcclassic_pad_typed start:150:4000 wait:150:3000" \
			HALO_SCREENSHOT_DIR="$out/pcclassic_pad" HALO_TEST_KEYS=wait:1
		# (Escape cancels: no profile, the Xbox's keyboard never drawn after)
		run pcclassic_cancel "$menus_data" 24 "" \
			"HALO_TEST_KEYS=wait:5000 key:Return typing text:Nope key:Escape closed wait:4000"
		for name in pcclassic_keyboard pcclassic_pad pcclassic_cancel; do
			check_h3 $name classic
			grep -aq "menus: typing .*: typed\|menus: typing .*: cancelled" "$out/$name/run.log" &&
				fail pcclassic "$name: the tests' answers typed in place of the text box"
		done
		log=$out/pcclassic_keyboard/run.log
		grep -aq 'text entry: "Player name" opened (up to 11 characters)$' "$log" ||
			fail pcclassic "keyboard: the text box did not open for the new profile's name"
		grep -aq 'text entry: "Player name": done: "Rookie"' "$log" || fail pcclassic "keyboard: Rookie was not typed"
		sed -n '/text entry: .*done/,$p' "$log" | grep -aq "ui: screen ui.shell.main_menu.difficulty_select" ||
			fail pcclassic "keyboard: the profile's name did not go on to the difficulty"
		log=$out/pcclassic_pad/run.log
		grep -aq "text entry: .*the on-screen keyboard shown" "$log" || fail pcclassic "pad: the on-screen keyboard was not shown"
		grep -aq 'text entry: "Player name": done: "Rookie"' "$log" || fail pcclassic "pad: Rookie was not typed on the on-screen keyboard"
		sed -n '/text entry: .*done/,$p' "$log" | grep -aq "ui: screen ui.shell.main_menu.difficulty_select" ||
			fail pcclassic "pad: the profile's name did not go on to the difficulty"
		for name in pcclassic_keyboard pcclassic_pad; do
			found=$(find "$out/$name/save" -name blam.sav | head -1)
			[ -n "$found" ] && [ "$(profile_fields "$found" | cut -d'|' -f1)" = Rookie ] &&
				echo "  ok: $name: the profile Rookie saved" || fail pcclassic "$name: no profile Rookie saved"
		done
		log=$out/pcclassic_cancel/run.log
		grep -aq 'text entry: "Player name": cancelled' "$log" || fail pcclassic "cancel: Escape did not cancel"
		grep -aq "ui: screen ui.shell.main_menu.difficulty_select" "$log" && fail pcclassic "cancel: the campaign went on"
		[ -z "$(find "$out/pcclassic_cancel/save" -name blam.sav)" ] || fail pcclassic "cancel: a profile was saved"
		;;
	pcnoadhoc)
		# (the PC build, HALO_TEST_PLATFORM=pc: no AD HOC - the PS Vitas' own -, the owner, Oct 20
		# 2026. NETWORK: the picker on SYSTEM LINK, down, A: OFFLINE, the next row; NETWORK, up, A:
		# SYSTEM LINK again (a Vita's: AD HOC); ONLINE never chosen, the brokers TEST-NET addresses all
		# the same. With the connection Ad hoc in the settings (HALO_NET_ADHOC, HALO_VITA_NETWORK=adhoc):
		# SYSTEM LINK, said in the log. The campaign lobby's NETWORK: LOCAL, ONLINE CO-OP, SYSTEM LINK
		# CO-OP and round to LOCAL, no AD HOC CO-OP)
		run pcnoadhoc "$menus_data" 34 "$h3mppad a wait:150:2500 @pcnoadhoc_picker down:150:600 a wait:150:3000 a wait:150:2500 up:150:600 a wait:150:3000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_PLATFORM=pc HALO_SCREENSHOT_DIR="$out/pcnoadhoc" $h3net HALO_NET_ONLINE=false
		run pcnoadhoc_setting "$menus_data" 20 "$h3mppad" HALO_MENU_STYLE=halo3 HALO_TEST_PLATFORM=pc HALO_NET_ADHOC=true \
			HALO_VITA_NETWORK=adhoc $h3net HALO_NET_ONLINE=false
		check_h3 pcnoadhoc halo3
		log=$out/pcnoadhoc/run.log
		grep -a "menus: h3 mp: network" "$log"
		[ "$(grep -a "menus: h3 mp: network" "$log" | sed 's/.*network //' | tr '\n' ' ')" = "OFFLINE SYSTEM LINK " ] ||
			fail pcnoadhoc "NETWORK did not go SYSTEM LINK, OFFLINE and up to SYSTEM LINK (AD HOC offered?)"
		grep -aq "NETWORK: AD HOC\|network: switching from .* to Ad hoc" "$log" && fail pcnoadhoc "AD HOC was offered on the PC build"
		[ -s "$out/pcnoadhoc/pcnoadhoc_picker.bmp" ] && [ -n "${HALO_TEST_SHOTS:-}" ] && cp "$out/pcnoadhoc/pcnoadhoc_picker.bmp" "$HALO_TEST_SHOTS/"
		check_h3 pcnoadhoc_setting halo3
		log=$out/pcnoadhoc_setting/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK" "$log" || fail pcnoadhoc "the connection Ad hoc did not open the lobby on SYSTEM LINK"
		grep -aq "network: Ad hoc is the PS Vitas' own: Same Wi-Fi (System Link) on this PC" "$log" ||
			fail pcnoadhoc "the setting's Ad hoc was not said to be System Link here"
		grep -aq "ad hoc: on\|network: Ad hoc (the connection" "$log" && fail pcnoadhoc "ad hoc play started on the PC build"
		if [ -r "${HALO_TEST_PROFILE:-}" ]; then
			# (CAMPAIGN, the profile; the lobby: up to NETWORK, A; down three, A: round to LOCAL past
			# SYSTEM LINK CO-OP, the last; a Vita's would be AD HOC CO-OP)
			profile_from=$HALO_TEST_PROFILE run pcnoadhoc_campaign "$menus_data" 34 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:3000" \
				HALO_MENU_STYLE=halo3 HALO_TEST_PLATFORM=pc $h3net HALO_NET_ONLINE=false
			check_h3 pcnoadhoc_campaign halo3
			log=$out/pcnoadhoc_campaign/run.log
			grep -a "menus: h3 campaign: network" "$log" | tail -1 | grep -q "network LOCAL" ||
				fail pcnoadhoc "the campaign's NETWORK, three down from LOCAL, was not LOCAL again (AD HOC CO-OP offered?)"
		else
			echo "SKIP (pcnoadhoc): no HALO_TEST_PROFILE: the campaign lobby's NETWORK"
		fi ;;
	h3adhoc)
		# (the connection Ad hoc: BROWSE GAMES, the joining dialog, joined: the profile (A joins,
		# A the profile, A), the LAN browser)
		run h3adhoc "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:6000 a wait:150:2000 a wait:150:2000 a wait:150:8000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=join
		# (the join failing: START GAME, the joining dialog; A again; B)
		run h3adhoc_fail "$menus_data" 30 "$h3mppad down:150:500 down:150:500 down:150:500 a wait:150:5000 a wait:150:5000 b wait:150:3000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=fail
		# (the connection Same Wi-Fi: NETWORK, down to AD HOC, A: ad hoc, live, the system's dialog
		# asked for (joined); down five to BROWSE GAMES, A: the profile (A joins, A the profile, A):
		# the ad hoc LAN browser; B, B: the lobby; NETWORK, up to SYSTEM LINK, A: Same Wi-Fi again)
		run h3adhoc_live "$menus_data" 65 "$h3mppad a wait:150:3000 down:150:600 a wait:150:5000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000 b wait:150:3000 b wait:150:3000 up:150:500 up:150:500 up:150:500 up:150:500 up:150:500 a wait:150:3000 up:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 HALO_ADHOC_TEST=join
		check_h3 h3adhoc halo3
		log=$out/h3adhoc/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: AD HOC" "$log" || fail h3adhoc "the lobby did not open on AD HOC with the connection Ad hoc"
		grep -aq "ui: screen pc.h3.adhoc.joining" "$log" || fail h3adhoc "BROWSE GAMES out of the group did not open the joining dialog"
		grep -aq "ad hoc: the system's dialog would join the room's group here (HALO_ADHOC_TEST=join)" "$log" ||
			fail h3adhoc "the joining dialog did not ask for the system's dialog"
		grep -aq "menus: h3 adhoc: in the ad hoc group: browse the games" "$log" ||
			fail h3adhoc "joined, BROWSE GAMES did not go on by itself"
		grep -aq "menus: the LAN browser (ad hoc)" "$log" || fail h3adhoc "the ad hoc LAN browser did not open"
		grep -aq 'menus: the server browser says "No games found near you"' "$log" ||
			fail h3adhoc "the ad hoc LAN browser did not say no games were found near"
		check_h3 h3adhoc_fail halo3
		log=$out/h3adhoc_fail/run.log
		grep -aq 'menus: h3 adhoc: the joining dialog says "The PS Vita did not join the ad hoc group.' "$log" ||
			fail h3adhoc "a failed join was not said"
		[ "$(grep -ac 'menus: h3 adhoc: the system.s dialog asked for' "$log")" -ge 2 ] ||
			fail h3adhoc "A after a failed join did not ask again"
		grep -aq "menus: h3 mp: host an ad hoc game" "$log" && fail h3adhoc "START GAME went on without the group"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.mp.screen' || fail h3adhoc "B did not go back to the lobby"
		check_h3 h3adhoc_live halo3
		log=$out/h3adhoc_live/run.log
		grep -aq "network: switching from Same Wi-Fi to Ad hoc" "$log" && grep -aq "ad hoc: on" "$log" ||
			fail h3adhoc "AD HOC did not switch to ad hoc play, live"
		grep -aq "ad hoc: the system's dialog would join the room's group here" "$log" ||
			fail h3adhoc "AD HOC did not ask for the system's dialog"
		grep -aq "menus: the LAN browser (ad hoc)" "$log" || fail h3adhoc "BROWSE GAMES did not open the ad hoc LAN browser"
		grep -aq "network: switching from Ad hoc to Same Wi-Fi" "$log" && grep -aq "ad hoc: off" "$log" &&
			grep -aq "ad hoc: the group left" "$log" || fail h3adhoc "SYSTEM LINK did not leave ad hoc play"
		grep -aq "ui: screen pc.h3.adhoc.restart" "$log" && fail h3adhoc "a restart dialog opened" ;;
	h3adhoccoop)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3adhoccoop"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (CAMPAIGN, the profile: the lobby on LOCAL, MISSION focused; up to NETWORK, A: the
		# picker; down three, A: AD HOC CO-OP; down three to START GAME, A: the joining dialog,
		# then co-op hosted by itself)
		profile_from=$HALO_TEST_PROFILE run h3adhoccoop "$menus_data" 45 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:10000" \
			HALO_MENU_STYLE=halo3 HALO_NET_ADHOC=true HALO_ADHOC_TEST=join
		profile_from=$HALO_TEST_PROFILE run h3adhoccoop_live "$menus_data" 30 "wait:150:3000 a wait:150:3000 a wait:150:3000 up:150:600 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:4000" \
			HALO_MENU_STYLE=halo3 HALO_ADHOC_TEST=join
		check_h3 h3adhoccoop halo3
		log=$out/h3adhoccoop/run.log
		grep -aq "menus: h3 campaign: network AD HOC CO-OP" "$log" || fail h3adhoccoop "AD HOC CO-OP was not chosen"
		grep -aq "ui: screen pc.h3.adhoc.joining" "$log" || fail h3adhoccoop "START GAME out of the group did not open the joining dialog"
		grep -aq "menus: h3 adhoc: in the ad hoc group: host co-op" "$log" || fail h3adhoccoop "joined, co-op was not hosted by itself"
		grep -aq "menus: h3 campaign: host co-op levels.*(ad hoc)" "$log" && grep -aq "co-op: hosting levels.* from the campaign's menus" "$log" ||
			fail h3adhoccoop "the level was not hosted as co-op"
		grep -a "ui: screen" "$log" | tail -1 | grep -qE 'connected_pregame_screen|pc.h3.pregame.screen' ||
			fail h3adhoccoop "the game's lobby (the waiting screen) did not open"
		check_h3 h3adhoccoop_live halo3
		grep -aq "network: switching from Same Wi-Fi to Ad hoc" "$out/h3adhoccoop_live/run.log" ||
			fail h3adhoccoop "AD HOC CO-OP with Same Wi-Fi did not switch to ad hoc play, live" ;;
	h3coopguide)
		# (3 s into the main menu the request opens the Campaign screen's profiles; the lobby
		# behind them is set to co-op. The Xbox's profiles screen opens on a new profile, whose
		# campaign starts at once, as ever: the lobby itself is h3coop's)
		run h3coopguide "$menus_data" 20 "wait:150:10000" HALO_MENU_STYLE=halo3 HALO_SYSTEM_LINK_TEST=campaign
		check_h3 h3coopguide halo3
		log=$out/h3coopguide/run.log
		grep -aq "system link: the settings panel's host co-op campaign: the Campaign screen opened" "$log" ||
			fail h3coopguide "the request did not open the Campaign screen"
		grep -aq "menus: h3 campaign: network SYSTEM LINK CO-OP (host co-op campaign)" "$log" ||
			fail h3coopguide "the lobby was not set to co-op" ;;
	h3mods)
		# (the list; mygulch off and on; its details; broken.map: Square, Circle keeps it, Square,
		# down to YES, A; cefake.map focused then: down two to TestMod, Square, down, A; B)
		mods_data "$out/h3mods-data"
		mods_from="$out/h3mods-data/mods" run h3mods "$out/h3mods-data" 75 "$h3modspad down:150:500 down:150:500 a wait:150:1500 a wait:150:1500 y wait:150:2500 b wait:150:2000 up:150:500 up:150:500 x wait:150:2000 b wait:150:1500 x wait:150:2000 down:150:500 a wait:150:3000 down:150:500 down:150:500 x wait:150:2000 down:150:500 a wait:150:3000 b wait:150:2000" \
			HALO_MENU_STYLE=halo3
		check_h3 h3mods halo3
		log=$out/h3mods/run.log
		grep -aq "ui: screen pc.h3.mods.screen" "$log" || fail h3mods "MOD MANAGER did not open its screen"
		grep -aq "menus: h3 mods: 4 entries" "$log" || fail h3mods "the four made-up entries were not listed"
		grep -aq "menus: h3 mods: mygulch.map: Xbox map, on, [0-9]* KB, passed, works in Multiplayer" "$log" ||
			fail h3mods "the modded Xbox map was not listed as one that plays in Multiplayer"
		grep -aq "menus: h3 mods: broken.map: Unknown file, on, 69 KB, failed: Not a Halo map" "$log" ||
			fail h3mods "the corrupt file was not shown as failing the check"
		grep -aq "menus: h3 mods: cefake.map: .*, failed: " "$log" ||
			fail h3mods "the empty Custom Edition header was not shown as failing the check"
		grep -aq "menus: h3 mods: TestMod: MCC mod, on, [0-9]* KB, failed: a10.map: " "$log" ||
			fail h3mods "the mod with a corrupt map was not shown as failing the check"
		grep -aq "mod manager: mygulch turned off: map lists no, bots playlist no, map sharing no" "$log" ||
			fail h3mods "mygulch off was still in a map list, the bots' playlist or map sharing"
		grep -aq "mod manager: mygulch turned on: map lists yes, bots playlist yes, map sharing yes" "$log" ||
			fail h3mods "mygulch on again was not back in the map lists, the bots' playlist and map sharing"
		grep -aq "ui: screen pc.h3.mods.details" "$log" || fail h3mods "Triangle did not open the details"
		[ "$(grep -ac 'ui: screen pc.h3.mods.confirm' "$log")" -ge 3 ] || fail h3mods "Square did not ask before deleting"
		grep -aq "mod manager: the map broken.map deleted" "$log" || fail h3mods "YES did not delete broken.map"
		[ "$(grep -ac 'menus: h3 mods: delete broken.map?' "$log")" = 2 ] ||
			fail h3mods "broken.map was not asked about twice (Circle, then YES)"
		[ -e "$out/h3mods-data/maps/broken.map" ] && fail h3mods "broken.map is still there"
		[ -e "$out/h3mods-data/maps/mygulch.map" ] && [ -e "$out/h3mods-data/maps/cefake.map" ] &&
			[ -L "$out/h3mods-data/maps/bitmaps.map" ] && [ -e "$out/h3mods-data/maps/bitmaps.map" ] ||
			fail h3mods "a file of the maps folder other than broken.map went"
		grep -aq "mod manager: the mod folder TestMod deleted" "$log" || fail h3mods "the mod's folder was not deleted"
		[ -e "$out/h3mods/data/mods/TestMod" ] && fail h3mods "the mod's folder is still there"
		[ -d "$out/h3mods/data/mods" ] || fail h3mods "the mods folder itself went"
		[ "$(cat "$out/h3mods-data/outside/sentinel.txt" 2>/dev/null)" = kept ] ||
			fail h3mods "a file a link in the mod pointed at was deleted (the link was followed)"
		grep -aq "menus: h3 mods: Deleted TestMod" "$log" || fail h3mods "the screen did not say the mod was deleted" ;;
	h3modspicker)
		mods_data "$out/h3mods-data"
		run h3modspicker "$out/h3mods-data" 50 "$h3pickerpad" HALO_MENU_STYLE=halo3
		run h3modspicker_on "$out/h3mods-data" 50 "$h3pickerpad" HALO_MENU_STYLE=halo3 HALO_MAPS_DISABLED=mygulch
		for name in h3modspicker h3modspicker_on; do
			check_h3 $name halo3
			grep -aq "ui: screen pc.h3.map.screen" "$out/$name/run.log" || fail $name "MAP did not open the lobby's map picker"
		done
		grep -aq "mod manager: mygulch turned off" "$out/h3modspicker/run.log" || fail h3modspicker "mygulch was not turned off"
		grep -aq "menus: h3 mp: the map picker: 13 maps (0 custom)" "$out/h3modspicker/run.log" ||
			fail h3modspicker "the map picker offered mygulch, turned off"
		grep -aq "mod manager: mygulch turned on" "$out/h3modspicker_on/run.log" || fail h3modspicker "mygulch was not turned on"
		grep -aq "menus: h3 mp: the map picker: 14 maps (1 custom)" "$out/h3modspicker_on/run.log" ||
			fail h3modspicker "the map picker did not offer mygulch, turned on again" ;;
	h3renamed)
		renamed_data=$out/renamed-data
		rm -rf "$renamed_data"
		mkdir -p "$renamed_data/maps"
		for file in "$menus_data"/maps/*; do
			ln -s "$(readlink -f "$file")" "$renamed_data/maps/$(basename "$file")"
		done
		ln -s "$(readlink -f "$menus_data/maps/bloodgulch.map")" "$renamed_data/maps/cursed-bloodgulch.map"
		ln -s "$(readlink -f "$menus_data/maps/bloodgulch.map")" "$renamed_data/maps/gulch_two.map"
		renamed_ce=
		if [ -r "$ce_maps/extinction.map" ]; then
			renamed_ce=1
			ln -s "$(readlink -f "$ce_maps/extinction.map")" "$renamed_data/maps/extinction.map"
			ln -s "$(readlink -f "$ce_maps/extinction.map")" "$renamed_data/maps/cursed-extinction.map"
		fi
		# (MULTIPLAYER, down two to MAP, A: the picker; down thirteen to the first custom row, A)
		renamed_pad="$h3mppad down:150:500 down:150:500 a wait:150:2000"
		for row in $(seq 1 13); do renamed_pad="$renamed_pad down:150:400"; done
		renamed_pad="$renamed_pad a wait:150:3000"
		run h3renamed "$renamed_data" 35 "$renamed_pad" HALO_MENU_STYLE=halo3 HALO_CUSTOM_EDITION=${renamed_ce:-0}
		check_h3 h3renamed halo3
		log=$out/h3renamed/run.log debug=$out/h3renamed/data/debug.txt
		list=$(grep -ao "custom edition: the multiplayer level list: 13 Xbox levels, then .*" "$debug" | tail -1 | tr -d '\r')
		echo "$list"
		for name in cursed-bloodgulch gulch_two ${renamed_ce:+cursed-extinction extinction}; do
			case "$list, " in *" $name, "*) ;; *) fail h3renamed "$name is not in the multiplayer level list" ;; esac
		done
		case "$list, " in *" bloodgulch, "*) fail h3renamed "Blood Gulch is listed twice" ;; esac
		grep -aq "menus: h3 mp: map Cursed-bloodgulch (a custom map: levels.test.cursed-bloodgulch.cursed-bloodgulch)" "$log" ||
			fail h3renamed "CURSED-BLOODGULCH was not the picker's first custom row, chosen"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: CURSED-BLOODGULCH" "$log" ||
			fail h3renamed "the lobby did not say CURSED-BLOODGULCH"
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3renamed_play"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (local games, as h3mppause's: the renamed copy, Blood Gulch, the copy again, each on
		# the save folder - its cache files - the one before left)
		renamed_from=
		for step in cursed-bloodgulch:play bloodgulch:stock cursed-bloodgulch:again; do
			level=${step%%:*} name=h3renamed_${step##*:}
			save_from=$renamed_from profile_from=$HALO_TEST_PROFILE run "$name" "$renamed_data" 45 "wait:150:3000" \
				HALO_MENU_STYLE=halo3 HALO_NETWORK_TEST=local:$level:slayer HALO_NETWORK_TEST_PROFILE=1 \
				HALO_NETWORK_TEST_START=3 HALO_BOTS=1
			renamed_from=$out/$name/save
			log=$out/$name/run.log debug=$out/$name/data/debug.txt
			echo "--- $name"
			grep -aE "network test: (map|starting)|join: the host's map" "$log" | head -4
			grep -aq "network test: tick 300 " "$log" || fail "$name" "$level did not play (no tick 300)"
			grep -aqiE "assert|exception|halt" "$log" && fail "$name" "an assertion, exception or halt"
			grep -aq "names itself .* refused" "$debug" && fail "$name" "a map was refused for the name inside it"
		done
		grep -aq "map 'cursed-bloodgulch' names itself 'bloodgulch' in its header: a renamed copy, known as 'cursed-bloodgulch'" \
			"$out/h3renamed_play/data/debug.txt" || fail h3renamed_play "cursed-bloodgulch was not taken as a renamed copy"
		python3 - "$out/h3renamed_play/save/z" <<'PY' || fail h3renamed_play "no cache file is named cursed-bloodgulch"
import glob, sys
names = [open(f, 'rb').read(0x40)[0x20:0x40].split(b'\0')[0] for f in glob.glob(sys.argv[1] + '/cache00*.map')]
sys.exit(0 if b'cursed-bloodgulch' in names and b'bloodgulch' not in names else 1)
PY
		grep -aq "starting precaching of map 'bloodgulch'" "$out/h3renamed_stock/data/debug.txt" ||
			fail h3renamed_stock "Blood Gulch was taken from the renamed copy's cache file"
		grep -aq "starting precaching of map 'cursed-bloodgulch'" "$out/h3renamed_again/data/debug.txt" &&
			fail h3renamed_again "the renamed copy's cache file was not found by its name (copied again)" ;;
	h3mpcustom)
		# (MULTIPLAYER: the lobby on SYSTEM LINK; down two to MAP, A: the picker; down thirteen: past the
		# last row it scrolls to MYGULCH; A: chosen; down to START GAME, A: the profile, A to join, A
		# the profile, A: the game hosted on mygulch, its lobby)
		mods_data "$out/h3mods-data"
		mpcustom_pad="$h3mppad down:150:500 down:150:500 a wait:150:2000 @00-picker:0:300"
		for row in $(seq 1 13); do mpcustom_pad="$mpcustom_pad down:150:400"; done
		mpcustom_pad="$mpcustom_pad @01-picker-custom:0:300 a wait:150:2000 @02-lobby-custom:0:300 down:150:500 a wait:150:3000"
		mpcustom_pad="$mpcustom_pad a wait:150:3000 a wait:150:3000 a wait:150:3000 wait:150:4000 @03-pregame:0:300"
		rm -rf "$out/h3mpcustom_shots"
		mkdir -p "$out/h3mpcustom_shots"
		run h3mpcustom "$out/h3mods-data" 55 "$mpcustom_pad" HALO_MENU_STYLE=halo3 HALO_SCREENSHOT_DIR="$out/h3mpcustom_shots"
		# (again on that save, mygulch off: MULTIPLAYER, MAP's picker, B)
		rm -rf "$out/h3mpcustom_saved"
		cp -r "$out/h3mpcustom/save" "$out/h3mpcustom_saved"
		save_from=$out/h3mpcustom_saved run h3mpcustom_off "$out/h3mods-data" 30 \
			"$h3mppad down:150:500 down:150:500 a wait:150:2000 b wait:150:2000" HALO_MENU_STYLE=halo3 HALO_MAPS_DISABLED=mygulch
		check_h3 h3mpcustom halo3
		check_h3 h3mpcustom_off halo3
		log=$out/h3mpcustom/run.log
		grep -aE "menus: h3 mp|menus: h3 maps|join: the host's map" "$log" | tail -8
		grep -aq "menus: h3 mp: the map picker: 14 maps (1 custom)" "$log" ||
			fail h3mpcustom "the picker did not list the custom map after the thirteen"
		grep -aq "menus: h3 maps: Mygulch (14 of 14)" "$log" || fail h3mpcustom "the rows did not scroll to MYGULCH"
		grep -aq "menus: h3 mp: map Mygulch (a custom map: levels.test.mygulch.mygulch)" "$log" ||
			fail h3mpcustom "choosing MYGULCH did not set the lobby's map to it"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: MYGULCH" "$log" ||
			fail h3mpcustom "the lobby did not say MYGULCH"
		grep -aq "join: the host's map is levels.test.mygulch.mygulch" "$log" ||
			fail h3mpcustom "START GAME did not host the game on mygulch"
		# (a game hosted without Server Setup's Max players: the Xbox's 16, not the build's 128)
		grep -aqE "menus: h3 pregame: .*; players: [^;]*; 1/16$" "$log" ||
			fail h3mpcustom "the system link lobby's PLAYERS did not say 1/16"
		grep -aq "/128" "$log" && fail h3mpcustom "a lobby said the build's 128 players"
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen|pc.h3.pregame.screen)" "$log" ||
			fail h3mpcustom "the hosted game's lobby did not open"
		log=$out/h3mpcustom_off/run.log
		grep -aq "menus: h3 mp: the last map used, levels.test.mygulch.mygulch, is not in the map list (off or gone)" "$log" ||
			fail h3mpcustom "mygulch off: the lobby did not leave it"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK, GAME: SLAYER, MAP: BATTLE CREEK" "$log" ||
			fail h3mpcustom "mygulch off: the lobby is not on Battle Creek"
		grep -aq "menus: h3 mp: the map picker: 13 maps (0 custom)" "$log" ||
			fail h3mpcustom "mygulch off: the picker still listed it"
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3mpcustom" &&
			cp "$out"/h3mpcustom_shots/*.bmp "$HALO_TEST_SHOTS/h3mpcustom/" 2>/dev/null ;;
	h3modstouch)
		skip_null h3modstouch "taps" && continue
		# (MOD MANAGER's row on the main menu; mygulch's row, the third; the screen's Back: icon and label)
		mods_data "$out/h3mods-data"
		run h3modstouch "$out/h3mods-data" 25 "wait:150:16000" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TAPS=4000:60,358 8000:200,142 12000:560,452"
		check_h3 h3modstouch halo3
		log=$out/h3modstouch/run.log
		grep -aq "ui: screen pc.h3.mods.screen" "$log" || fail h3modstouch "a tap on MOD MANAGER did not open it"
		grep -aq "menus: h3 mods: mygulch.map turned off" "$log" || fail h3modstouch "a tap on mygulch's row did not turn it off"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail h3modstouch "a tap on Back did not go back to the main menu" ;;
	h3friends)
		# (FRIENDS, the seventh item; ADD FRIEND: a code typed; APPEAR OFFLINE; FRIENDS CAN JOIN;
		# PARTY and FRIEND CODE BACKUP passed; Delta's request: Square declines; Bravo (focused
		# then): Cross joins (offline: go online first), Triangle, down to REMOVE FRIEND, A; B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3friends "$menus_data" 45 "$h3friendspad a wait:150:2500 down:150:500 a wait:150:1500 down:150:500 a wait:150:1500 down:150:500 down:150:500 down:150:500 x wait:150:2500 a wait:150:2000 y wait:150:2500 down:150:500 a wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online "HALO_TEST_TEXT_INPUT=abcd efgh-jklm"
		check_h3 h3friends halo3
		log=$out/h3friends/run.log
		grep -aq "ui: screen pc.h3.friends.screen" "$log" || fail h3friends "FRIENDS did not open its screen"
		grep -aq "menus: h3 friends: code [A-Z0-9]\{4\}-[A-Z0-9]\{4\}-[A-Z0-9]\{4\}; Go online to see your friends; 5 entries" "$log" ||
			fail h3friends "the code, the status and the five entries were not listed"
		grep -aq "menus: h3 friends: request Delta (.*): Wants to be your friend" "$log" || fail h3friends "the request was not listed"
		grep -aq "menus: h3 friends: friend Bravo (.*): Slayer on Blood Gulch \[JOIN\]" "$log" ||
			fail h3friends "the joinable friend was not listed with its game"
		grep -aq "menus: h3 friends: friend Charlie (.*): Co-op: The Pillar of Autumn, Legendary" "$log" ||
			fail h3friends "the co-op friend was not listed with its level and difficulty"
		grep -aq "menus: h3 friends: request sent .*: Request sent" "$log" || fail h3friends "the request sent was not listed"
		grep -aq "menus: h3 friends: blocked Echo" "$log" || fail h3friends "the blocked player was not listed"
		grep -aq "friends: a friend request to ABCD-EFGH-JKLM (sent once online)" "$log" ||
			fail h3friends "ADD FRIEND did not queue the typed code's request"
		grep -aq "menus: h3 friends: Friend request sent" "$log" || fail h3friends "ADD FRIEND did not say it was sent"
		grep -aq "menus: h3 friends: Appearing offline" "$log" || fail h3friends "APPEAR OFFLINE did not switch on"
		grep -aq "menus: h3 friends: Friends join only by your invites" "$log" || fail h3friends "FRIENDS CAN JOIN did not switch off"
		grep -aq "friends: declined Delta's friend request (nothing sent)" "$log" || fail h3friends "Square did not decline"
		grep -aq "menus: h3 friends: Go online first" "$log" || fail h3friends "Join offline did not say go online first"
		grep -aq "ui: screen pc.h3.friends.options" "$log" || fail h3friends "Triangle did not open the options"
		grep -aq "friends: removed Bravo" "$log" || fail h3friends "REMOVE FRIEND did not remove Bravo"
		# (a second start: the list as left)
		save_from="$out/h3friends/save" run h3friends_again "$menus_data" 25 "$h3friendspad wait:150:2000 b wait:150:2000" HALO_MENU_STYLE=halo3
		check_h3 h3friends_again halo3
		grep -aq "friends: this machine's friend code is .* (1 friends, 0 requests, 2 sent, 1 blocked)" "$out/h3friends_again/run.log" ||
			fail h3friends "the list was not written and read back as left"
		[ "$(grep -a "this machine's friend code is" "$log" | sed 's/.*code is \([^ ]*\).*/\1/')" = \
			"$(grep -a "this machine's friend code is" "$out/h3friends_again/run.log" | sed 's/.*code is \([^ ]*\).*/\1/')" ] ||
			fail h3friends "the code changed between starts" ;;
	h3friendsbackup)
		# (FRIENDS; down four to FRIEND CODE BACKUP, Cross: its question (Back focused); down, Cross:
		# BACK UP FRIEND CODE; the name, the password, again; down, Cross: WITH FRIENDS LIST; B)
		backup_pad="down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:2500"
		friends_seed "$out/h3friendsbackup-save"
		save_from="$out/h3friendsbackup-save" run h3friendsbackup "$menus_data" 40 \
			"$h3friendspad $backup_pad down:150:500 a wait:150:3000 down:150:500 a wait:150:6000 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=my-code|hunter2hunter2|hunter2hunter2"
		check_h3 h3friendsbackup halo3
		log=$out/h3friendsbackup/run.log
		grep -a "menus: h3 friends\|friends: backup" "$log" | grep -av "menus: h3 friends: \(request\|friend\|blocked\) " | head -12
		code1=$(grep -a "this machine's friend code is" "$log" | head -1 | sed 's/.*code is \([^ ]*\).*/\1/')
		grep -aq "menus: h3 friends: the friend code backup's question" "$log" || fail h3friendsbackup "FRIEND CODE BACKUP asked nothing"
		grep -aq "ui: screen pc.h3.friends.options" "$log" || fail h3friendsbackup "its question did not open"
		grep -aq "friends: backup: written to .*/my-code.hfb (the friend code and the friends list)" "$log" ||
			fail h3friendsbackup "the backup was not written with the list"
		grep -aq "menus: h3 friends: Saved to .*my-code.hfb. Keep it and its password safe." "$log" ||
			fail h3friendsbackup "where it went was not said"
		[ -s "$out/h3friendsbackup/save/my-code.hfb" ] || fail h3friendsbackup "no my-code.hfb in the save folder"
		# (a second start, a new save: its own code; the backup opened with a wrong password, then
		# the right one, CONFIRM)
		rm -rf "$out/h3friendsrestore-save"
		mkdir -p "$out/h3friendsrestore-save"
		cp "$out/h3friendsbackup/save/my-code.hfb" "$out/h3friendsrestore-save/"
		restore_pad="$backup_pad down:150:500 down:150:500 a wait:150:5000 wait:150:2000"
		restore_pad="$restore_pad $backup_pad down:150:500 down:150:500 a wait:150:6000 down:150:500 a wait:150:3000 b wait:150:2000"
		save_from="$out/h3friendsrestore-save" run h3friendsrestore "$menus_data" 50 "$h3friendspad $restore_pad" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=my-code|hunter2hunter3|my-code|hunter2hunter2"
		check_h3 h3friendsrestore halo3
		log=$out/h3friendsrestore/run.log
		grep -a "menus: h3 friends: [^rfb]\|friends: backup\|friend code is" "$log" | head -12
		code2=$(grep -a "this machine's friend code is" "$log" | head -1 | sed 's/.*code is \([^ ]*\).*/\1/')
		[ -n "$code1" ] && [ -n "$code2" ] && [ "$code1" != "$code2" ] || fail h3friendsbackup "the new save had no code of its own"
		grep -aq "friends: backup: .*my-code.hfb: Wrong password, or the file was changed" "$log" ||
			fail h3friendsbackup "a wrong password was not said"
		grep -aq "friends: backup: .*my-code.hfb opened: the friend code $code1, 2 friends (its list)" "$log" ||
			fail h3friendsbackup "the backup was not opened with the right password"
		grep -aq "friends: backup: restored: this machine's friend code is now $code1 (2 friends, 1 requests, 1 sent, 1 blocked" "$log" ||
			fail h3friendsbackup "the first start's code and friends were not restored"
		grep -aq "menus: h3 friends: Friend code restored: $code1" "$log" || fail h3friendsbackup "the restore was not said"
		ls "$out/h3friendsrestore/save/friends/" | grep -q "^replaced-" || fail h3friendsbackup "the replaced identity was not kept"
		grep -aq "hunter2" "$out/h3friendsbackup/run.log" "$log" "$out"/h3friends*/data/debug.txt 2>/dev/null &&
			fail h3friendsbackup "a log holds the password" ;;
	h3party)
		# (FRIENDS; down three to PARTY, Cross: the PARTY screen (internet play off: no party, how to
		# make one); Square (QUICK CHAT) and Triangle (type): not in a party, said; B: FRIENDS again
		# (on ADD FRIEND); down six to Bravo (FRIEND CODE BACKUP passed), Triangle: the options,
		# offline no INVITE TO PARTY; B, B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3party "$menus_data" 45 "$h3friendspad down:150:500 down:150:500 down:150:500 a wait:150:3000 x wait:150:1500 y wait:150:1500 b wait:150:2000 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 y wait:150:2500 b wait:150:2000 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online
		check_h3 h3party halo3
		log=$out/h3party/run.log
		grep -a "menus: h3 party\|PARTY screen" "$log" | head -6
		grep -aq "menus: h3 friends: the PARTY screen next" "$log" || fail h3party "Cross on PARTY did not ask for the PARTY screen"
		grep -aq "menus: the PARTY screen opened" "$log" && grep -aq "ui: screen pc.h3.party.screen" "$log" ||
			fail h3party "the PARTY screen did not open"
		grep -aq "menus: h3 party: Go online to make a party" "$log" || fail h3party "the PARTY screen did not say go online"
		[ "$(grep -ac "menus: h3 party: Not in a party" "$log")" -ge 2 ] ||
			fail h3party "QUICK CHAT and TYPE out of a party did not say so"
		grep -aq "ui: screen pc.h3.party.quick" "$log" && fail h3party "QUICK CHAT opened out of a party"
		grep -a "ui: screen" "$log" | grep -A1 "pc.h3.party.screen" | tail -1 | grep -q "pc.h3.friends.screen" ||
			fail h3party "B did not go back to FRIENDS"
		grep -aq "ui: screen pc.h3.friends.options" "$log" || fail h3party "Triangle did not open a friend's options" ;;
	h3mppause)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3mppause"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (in the game: Start, the pause; down two, SETTINGS, Cross: right (the stick layout),
		# Circle saves; down, END GAME, Cross: the question, Circle back; up three, RESUME, Cross)
		mppause_pad="wait:150:32000 start:150:3000 down:150:800 down:150:800 a:150:3000 right:150:1500 b:150:2500 down:150:800 a:150:2500 b:150:2000 up:150:600 up:150:600 up:150:600 a:150:3000"
		mppause_env="HALO_NETWORK_TEST=local:bloodgulch:team_slayer HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1"
		profile_from=$HALO_TEST_PROFILE run h3mppause "$menus_data" 70 "$mppause_pad" HALO_MENU_STYLE=halo3 $mppause_env
		# (in a party: down three to PARTY, Cross)
		profile_from=$HALO_TEST_PROFILE run h3mppause_party "$menus_data" 45 "wait:150:32000 start:150:3000 down:150:800 down:150:800 down:150:800 a:150:2500" \
			HALO_MENU_STYLE=halo3 HALO_TEST_MP_PAUSE_PARTY=1 $mppause_env
		profile_from=$HALO_TEST_PROFILE run h3mppause_classic "$menus_data" 45 "wait:150:32000 start:150:3000 b:150:2000" \
			HALO_MENU_STYLE=classic $mppause_env
		log=$out/h3mppause/run.log
		echo "--- h3mppause"
		grep -aE "ui: screen|menus: h3 mp" "$log" | head -16
		for name in h3mppause h3mppause_party h3mppause_classic; do
			[ "$(cat "$out/$name/exit")" = 0 ] || fail h3mppause "$name: exit $(cat "$out/$name/exit")"
			grep -aqiE "assert|exception|halt_and_catch_fire" "$out/$name/run.log" && fail h3mppause "$name: an assertion, exception or halt"
		done
		grep -aq "menus: Halo 3 screen h3/mppause/screen for ui.shell.multiplayer_game.pause_game.1p_pause_game" "$log" ||
			fail h3mppause "the Halo 3 multiplayer pause did not stand for the Xbox's"
		grep -aqE "menus: h3 mp pause: .* on Blood Gulch, teams, the host, 2 players; rows: resume, players, settings, end game" "$log" ||
			fail h3mppause "the team game's pause did not show RESUME, PLAYERS, SETTINGS, END GAME"
		grep -aqi "change teams" "$log" && fail h3mppause "CHANGE TEAMS was offered in the game"
		ticks=$(sed -n '/ui: screen pc.h3.mppause.screen/,$p' "$log" | grep -ac "network test: tick")
		[ "$ticks" -ge 3 ] || fail h3mppause "the game did not go on under the pause ($ticks ticks said)"
		grep -aq "menus: h3 mp settings: local player 0, profile edited" "$log" || fail h3mppause "SETTINGS did not edit the profile"
		grep -aq "menus: h3 mp settings: saved and played: stick 1" "$log" ||
			fail h3mppause "the stick layout changed in SETTINGS was not saved and played"
		grep -aq "ui: screen pc.h3.mppause.end_screen" "$log" || fail h3mppause "END GAME did not ask"
		sed -n '/ui: screen pc.h3.mppause.end_screen/,$p' "$log" | grep -aq "ui: screen pc.h3.mppause.screen" ||
			fail h3mppause "Circle on END GAME's question did not go back to the pause"
		grep -aq "ui: screen pc.h3.main.screen" <(sed -n '/ui: screen pc.h3.mppause.screen/,$p' "$log") &&
			fail h3mppause "the game was left (only its question was to be seen)"
		grep -aqE "menus: h3 mp pause: .*rows: resume, players, settings, party, end game" "$out/h3mppause_party/run.log" ||
			fail h3mppause "in a party the pause did not show PARTY"
		grep -aq "menus: h3 mp pause: the Party page asked for" "$out/h3mppause_party/run.log" ||
			fail h3mppause "PARTY did not ask for the Party page"
		grep -aq "ui: screen ui.shell.multiplayer_game.pause_game.1p_pause_game" "$out/h3mppause_classic/run.log" ||
			fail h3mppause "Menu style Halo CE did not open the Xbox's multiplayer pause"
		grep -aq "h3/mppause\|pc.h3.mppause" "$out/h3mppause_classic/run.log" &&
			fail h3mppause "Menu style Halo CE opened the Halo 3 multiplayer pause" ;;
	h3carnage)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3carnage"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (once the report is up: its picture, R (DETAILS) and its picture, L, Circle (the
		# host's: nothing), Cross: the lobby)
		carnage_pad="wait:150:1000 carnage:150:3000 @carnage_summary rt:150:2000 @carnage_details lt:150:1500 b:150:2000 a:150:4000"
		carnage_env="HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOT_SKILL=legendary HALO_TEST_CARNAGE_SECONDS=60"
		profile_from=$HALO_TEST_PROFILE run h3carnage "$menus_data" 160 "$carnage_pad" HALO_MENU_STYLE=halo3 $carnage_env \
			HALO_NETWORK_TEST=local:chillout:slayer HALO_NETWORK_TEST_SCORE=3 HALO_BOTS=3
		profile_from=$HALO_TEST_PROFILE run h3carnage_team "$menus_data" 160 "$carnage_pad" HALO_MENU_STYLE=halo3 $carnage_env \
			HALO_NETWORK_TEST=local:bloodgulch:team_slayer HALO_NETWORK_TEST_SCORE=3 HALO_BOTS=5
		profile_from=$HALO_TEST_PROFILE run h3carnage_ctf "$menus_data" 110 "$carnage_pad" HALO_MENU_STYLE=halo3 $carnage_env \
			HALO_NETWORK_TEST=local:bloodgulch:ctf HALO_NETWORK_TEST_END=45 HALO_BOTS=5
		profile_from=$HALO_TEST_PROFILE run h3carnage_oddball "$menus_data" 110 "$carnage_pad" HALO_MENU_STYLE=halo3 $carnage_env \
			HALO_NETWORK_TEST=local:chillout:oddball HALO_NETWORK_TEST_END=45 HALO_BOTS=5
		# (the game's own report: menus.carnage_report classic, and Menu style Halo CE)
		profile_from=$HALO_TEST_PROFILE run h3carnage_classic "$menus_data" 90 "wait:150:1000" HALO_MENU_STYLE=halo3 \
			HALO_CARNAGE_REPORT=classic $carnage_env HALO_NETWORK_TEST=local:chillout:slayer HALO_NETWORK_TEST_END=30 HALO_BOTS=3
		profile_from=$HALO_TEST_PROFILE run h3carnage_ce "$menus_data" 90 "wait:150:1000" HALO_MENU_STYLE=classic \
			$carnage_env HALO_NETWORK_TEST=local:chillout:slayer HALO_NETWORK_TEST_END=30 HALO_BOTS=3
		for name in h3carnage h3carnage_team h3carnage_ctf h3carnage_oddball h3carnage_classic h3carnage_ce; do
			log=$out/$name/run.log
			echo "--- $name"
			grep -aE "carnage: |menus: h3 carnage" "$log" | head -14
			[ "$(cat "$out/$name/exit")" = 0 ] || fail h3carnage "$name: exit $(cat "$out/$name/exit")"
			grep -av "render assertion skipped" "$log" | grep -aqiE "assert|exception|halt_and_catch_fire" &&
				fail h3carnage "$name: an assertion, exception or halt"
		done
		for name in h3carnage h3carnage_team h3carnage_ctf h3carnage_oddball; do
			log=$out/$name/run.log
			grep -aq "ui: screen pc.h3.carnage.screen" "$log" && grep -aq "menus: h3 carnage: the carnage report opened (the host)" "$log" ||
				{ fail h3carnage "$name: the carnage report did not open at the game's end"; continue; }
			# (the rows of the report as last said: ranked as the game ranks them)
			python3 - "$log" "$name" <<'PY' || fail h3carnage "$name: the report's rows were not ranked as the game ranks them"
import re, sys
log, name = sys.argv[1], sys.argv[2]
head, rows = None, []
for line in open(log, encoding='utf-8', errors='replace'):
    m = re.search(r'carnage: [^:]*report[^:]*: (type .*)', line)
    if m:
        head, rows = m.group(1), []
        continue
    m = re.search(r'carnage: row \d+: (team )?(.*) place (\d+)( \(tied\))?( won)? team (-?\d+) score (\S+) kills (\d+) '
                  r'deaths (\d+) assists (\d+) suicides (\d+) betrayals (\d+) objective (-?\d+) (-?\d+)(.*)', line)
    if m:
        rows.append(dict(team_row=bool(m.group(1)), name=m.group(2), place=int(m.group(3)), tied=bool(m.group(4)),
            won=bool(m.group(5)), team=int(m.group(6)), score=m.group(7), kills=int(m.group(8)), deaths=int(m.group(9)),
            assists=int(m.group(10)), local='[this machine]' in m.group(15)))
problems = []
def say(text):
    problems.append(text)
if not head or not rows:
    say('no report said')
teams = head and ', teams,' in head
def score(text):
    # (a time, "1:05", in seconds; a number as it is)
    if ':' in text:
        minutes, seconds = text.split(':', 1)
        return int(minutes or 0) * 60 + int(seconds)
    return int(text) if text.lstrip('-').isdigit() else 0
def key(r):
    return (score(r['score']), r['kills'], -r['deaths'], r['assists'])
players = [r for r in rows if not r['team_row']]
if not any(r['local'] for r in players):
    say("this machine's player is not marked")
ranked = sorted(players, key=lambda r: r['place'])
for a, b in zip(ranked, ranked[1:]):
    # (a time's score is ranked by its ticks: two shown alike, "0:02", may
    # differ by less than a second, the one with fewer kills placed above)
    if key(a) < key(b) and not (':' in a['score'] and ':' in b['score'] and score(a['score']) == score(b['score'])):
        say('%s placed above %s with less' % (a['name'], b['name']))
    # (a place is shared only by players alike; players alike as shown may
    # differ by less than a second of a time's score)
    if a['place'] == b['place'] and key(a) != key(b):
        say('%s and %s share a place, not alike' % (a['name'], b['name']))
if not teams:
    for index, r in enumerate(rows):
        if index and rows[index - 1]['place'] > r['place']:
            say('the rows are not in their places\' order')
    first = rows[0] if rows else None
    if first and first['won'] == first['tied']:
        say('the first row is not the winner (or a tie is)')
    for index, r in enumerate(ranked):
        expected = next(i for i, o in enumerate(ranked) if o['place'] == r['place']) + 1
        if r['place'] != expected:
            say('%s is placed %d, not %d (a shared place counts its players)' % (r['name'], r['place'], expected))
else:
    team_rows = [r for r in rows if r['team_row']]
    if not rows[0]['team_row']:
        say('a team game\'s report does not start with a team')
    if len(team_rows) >= 2 and score(team_rows[0]['score']) < score(team_rows[1]['score']):
        say('the first team has the lower score')
    if team_rows and team_rows[0]['won'] == team_rows[0]['tied']:
        say('the first team is not the winner (or a tie is)')
    if team_rows and team_rows[0]['tied'] and not (len(team_rows) >= 2 and team_rows[1]['tied'] and
            score(team_rows[0]['score']) == score(team_rows[1]['score'])):
        say('a tie of teams not alike')
    for t in team_rows:
        members = [r for r in players if r['team'] == t['team']]
        if sum(r['kills'] for r in members) != t['kills'] or sum(r['deaths'] for r in members) != t['deaths']:
            say("%s's kills or deaths are not its players'" % t['name'])
        if any(r['won'] != t['won'] for r in members):
            say("%s's players are not marked as their team" % t['name'])
print('%s: %s, %d rows: %s' % (name, head, len(rows), 'ranked' if not problems else '; '.join(problems)))
sys.exit(1 if problems else 0)
PY
			grep -aq "menus: h3 carnage: page 1, " "$log" || fail h3carnage "$name: R did not show DETAILS"
			sed -n '/menus: h3 carnage: page 1, /,$p' "$log" | grep -aq "menus: h3 carnage: page 0, " ||
				fail h3carnage "$name: L did not show SUMMARY again"
			grep -aq "menus: h3 carnage: the host goes on: the lobby" "$log" || fail h3carnage "$name: Cross did not go on to the lobby"
			grep -aq "LEAVE GAME: the game left\|ui: screen pc.h3.carnage.leave" "$log" && fail h3carnage "$name: the host's Circle asked to leave"
			grep -aq "menus: h3 carnage: closed with the game" "$log" || fail h3carnage "$name: the report did not close with the game"
		done
		grep -aq "carnage: .*report: type 1, teams" "$out/h3carnage_ctf/run.log" || fail h3carnage "CTF's report was not a team game's"
		grep -aq "carnage: .*report: type 3, free for all" "$out/h3carnage_oddball/run.log" ||
			fail h3carnage "Oddball's report was not a free for all game's"
		grep -aqE "carnage: row 1: .* score [0-9]+:[0-9][0-9] kills" "$out/h3carnage_oddball/run.log" ||
			fail h3carnage "Oddball's score was not a time"
		for name in h3carnage_classic h3carnage_ce; do
			log=$out/$name/run.log
			grep -aq "menus: h3 carnage: the game is over: the game's own report" "$log" ||
				fail h3carnage "$name: the game did not end with the game's own report"
			grep -aq "ui: screen pc.h3.carnage\|menus: h3 carnage: the carnage report opened" "$log" &&
				fail h3carnage "$name: the Halo 3 report opened (the game's own wanted)"
		done ;;
	h3friendsfit)
		# (FORGE: its lobby's FRIENDS panel, internet play off: its line; in English, then Spanish)
		run h3friendsfit "$menus_data" 22 "wait:150:3000 down:150:700 down:150:700 a wait:150:5000" HALO_MENU_STYLE=halo3
		run h3friendsfit_es "$menus_data" 22 "wait:150:3000 down:150:700 down:150:700 a wait:150:5000" HALO_MENU_STYLE=halo3 \
			HALO_LANGUAGE=es "HALO_TEST_LANG_DIR=$root/port/vita/app0/lang"
		check_h3 h3friendsfit halo3
		check_h3 h3friendsfit_es halo3
		grep -aE "friends panel: status" "$out/h3friendsfit/run.log" "$out/h3friendsfit_es/run.log"
		grep -aqE 'menus: h3 friends panel: status "Go online to see your friends" fits (one|two) lines?' "$out/h3friendsfit/run.log" ||
			fail h3friendsfit "the panel's line did not fit (English)"
		grep -aqE 'menus: h3 friends panel: status "Conéctate en línea para ver a tus amigos" fits (one|two) lines?' "$out/h3friendsfit_es/run.log" ||
			fail h3friendsfit "the panel's line did not fit (Spanish)"
		grep -aq 'menus: h3 friends panel: status .* cut' "$out/h3friendsfit/run.log" "$out/h3friendsfit_es/run.log" &&
			fail h3friendsfit "the panel's line was cut" ;;
	h3friendspanel)
		# (MULTIPLAYER: the lobby's panel; Triangle: FRIENDS; B, B: the main menu, MULTIPLAYER focused; down
		# to FORGE: its lobby's panel; Triangle; B)
		friends_seed "$out/h3friends-save"
		save_from="$out/h3friends-save" run h3friendspanel "$menus_data" 40 "$h3mppad wait:150:2500 y wait:150:2500 b wait:150:2000 b wait:150:2500 down:150:500 a wait:150:4000 y wait:150:2500 b wait:150:2000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_FRIENDS_SHOW=online
		check_h3 h3friendspanel halo3
		log=$out/h3friendspanel/run.log
		grep -aq "ui: screen pc.h3.mp.screen" "$log" || fail h3friendspanel "MULTIPLAYER did not open the lobby"
		grep -aq "menus: h3 friends panel: 2 ONLINE, 1 NEW, 3 shown" "$log" ||
			fail h3friendspanel "the lobby's panel did not show the two friends online and the request"
		[ "$(grep -ac "ui: screen pc.h3.friends.screen" "$log")" -ge 2 ] ||
			fail h3friendspanel "Triangle did not open FRIENDS from both lobbies"
		grep -aq "ui: screen pc.h3.forge.lobby" "$log" || fail h3friendspanel "the Forge lobby did not open" ;;
	h3partyonline)
		# (a party plays online, the owner, Oct 21 2026: in a party (HALO_TEST_IN_PARTY, as in
		# one) MULTIPLAYER's lobby opens on ONLINE whatever the connection was (Same Wi-Fi here:
		# internet play started, its brokers TEST-NET addresses); NETWORK, down to SYSTEM LINK, A:
		# refused, said why, the lobby still ONLINE; A closes the message)
		run h3partyonline "$menus_data" 30 "$h3mppad wait:150:3000 a wait:150:3000 down:150:600 a wait:150:3000 a wait:150:3000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_IN_PARTY=1 $h3net HALO_NET_ONLINE=false HALO_VITA_NETWORK=wifi
		check_h3 h3partyonline halo3
		log=$out/h3partyonline/run.log
		grep -a "menus: in a party\|menus: h3 mp lobby: NETWORK\|menus: h3 mp: network\|network: switching" "$log" | head -8
		grep -aq "menus: in a party (.*): the lobbies' NETWORK is ONLINE (a party plays online)" "$log" ||
			fail h3partyonline "in a party the lobbies were not put on ONLINE"
		grep -aq "network: switching from Same Wi-Fi to Online" "$log" || fail h3partyonline "in a party internet play did not start"
		grep -aq "menus: h3 mp lobby: NETWORK: ONLINE" "$log" || fail h3partyonline "in a party the lobby did not show ONLINE"
		grep -aq "menus: h3 mp lobby: NETWORK: SYSTEM LINK\|menus: h3 mp lobby: NETWORK: OFFLINE" "$log" &&
			fail h3partyonline "in a party the lobby showed another NETWORK"
		grep -aq "menus: in a party: another NETWORK than ONLINE refused (a party plays online)" "$log" ||
			fail h3partyonline "SYSTEM LINK in a party was not refused"
		grep -aq "menus: h3 mp: network SYSTEM LINK\|network: switching from Online to Same Wi-Fi" "$log" &&
			fail h3partyonline "SYSTEM LINK was chosen in a party"
		grep -aq "PARTY: You.re in a party, and a party plays online: the NETWORK stays ONLINE" "$log" ||
			fail h3partyonline "the player was not told why" ;;
	h3partyplates)
		# (the plates' clock starts at the main menu's first frame: Bravo, the leader, from 4 s
		# to 26 s, Charlie from 7 s to 17 s; alone at 2 s, the three at 10 s; MULTIPLAYER at about
		# 11 s, the lobby with both at 14 s, Charlie gone at 20 s, Bravo at 28 s)
		rm -rf "$out/h3partyplates_shots"
		mkdir -p "$out/h3partyplates_shots"
		plates_pad="wait:150:2000 @00-alone:0:300 wait:150:7500 @01-party:0:300 down:150:700 a wait:150:2500"
		plates_pad="$plates_pad @02-lobby:0:300 wait:150:5500 @03-left:0:300 wait:150:9000"
		run h3partyplates "$menus_data" 36 "$plates_pad" HALO_MENU_STYLE=halo3 HALO_NET_PLAYER_NAME=Birch HALO_TEST_BROWSER_GAMES=5 \
			"HALO_TEST_PARTY_PLATES=Bravo*@4-26,Charlie@7-17" HALO_SCREENSHOT_DIR="$out/h3partyplates_shots"
		check_h3 h3partyplates halo3
		log=$out/h3partyplates/run.log
		grep -a "menus: party plates\|ui: screen pc.h3.mp.screen\|friends panel" "$log"
		plates=$(grep -a "menus: party plates: \|ui: screen pc.h3.mp.screen" "$log" | sed 's/.*menus: party plates: //; s/.*ui: screen pc.h3.mp.screen.*/LOBBY/' | tr '\n' '|')
		echo "plates: $plates"
		case $plates in
		'1 under Birch: Bravo (leader)|2 under Birch: Bravo (leader), Charlie|LOBBY|1 under Birch: Bravo (leader)|none|') ;;
		*) fail h3partyplates "the plates did not go none, Bravo, Bravo and Charlie, the lobby, Bravo, none" ;;
		esac
		grep -aq "menus: party plates: none (the nameplate hidden)" "$log" && fail h3partyplates "the nameplate was hidden"
		if [ $gxm_null = 1 ]; then
			echo "SKIP (h3partyplates): the frames saved (a gxm-null harness: the GL build's check)"
		else
			ls "$out/h3partyplates_shots"/0[0-3]-*.bmp > /dev/null 2>&1 || fail h3partyplates "the frames were not saved"
		fi
		[ -n "${HALO_TEST_SHOTS:-}" ] && mkdir -p "$HALO_TEST_SHOTS/h3partyplates" &&
			cp "$out"/h3partyplates_shots/0*.bmp "$HALO_TEST_SHOTS/h3partyplates/" 2>/dev/null ;;
	browserprofile)
		# (Halo CE: Multiplayer, SERVER BROWSER (its first item): the profile (A joins, A the profile,
		# A), the server browser; B, B)
		run browserprofile "$menus_data" 40 "wait:150:3000 down:150:800 a wait:150:3000 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:4000 b wait:150:2500 b wait:150:2500" \
			HALO_MENU_STYLE=classic $h3net
		# (Halo 3: MULTIPLAYER, down five to BROWSE GAMES on ONLINE: the profile, then the games browser)
		run browserprofile_h3 "$menus_data" 40 "$h3mppad down:150:500 down:150:500 down:150:500 down:150:500 down:150:500 a wait:150:3000 a wait:150:2000 a wait:150:2000 a wait:150:5000" \
			HALO_MENU_STYLE=halo3 $h3net
		# (internet play off: SERVER BROWSER says so; no profile screen)
		run browserprofile_off "$menus_data" 25 "wait:150:3000 down:150:800 a wait:150:3000 a wait:150:4000" HALO_MENU_STYLE=classic
		check_h3 browserprofile classic
		check_h3 browserprofile_h3 halo3
		log=$out/browserprofile/run.log
		screens=$(grep -a "ui: screen" "$log" | sed 's/.*ui: screen //' | tr '\n' '|')
		echo "$screens"
		case $screens in
		*'pc\mp\screen|ui\shell\main_menu\multiplayer_type_select\connected\4way_profile_select\4way_start2join_screen|pc\browser\screen|ui\shell\main_menu\multiplayer_type_select\connected\4way_profile_select\4way_start2join_screen|pc\mp\screen'*) ;;
		*) fail browserprofile "SERVER BROWSER did not open the profile screen, then the server browser, B going back through both" ;;
		esac
		grep -aq "menus: Server Browser: the profile chosen; the server browser" "$log" ||
			fail browserprofile "the server browser did not stand for the System Link list after the profile"
		log=$out/browserprofile_h3/run.log
		grep -aqE "ui: screen (ui.shell.main_menu.multiplayer_type_select.connected.4way_profile_select.4way_start2join_screen|pc.h3.join.screen)" "$log" &&
			grep -aq "ui: screen pc.h3.lan.screen" "$log" || fail browserprofile "BROWSE GAMES on ONLINE did not ask for the profile first"
		awk '/4way_start2join_screen|pc.h3.join.screen/ { profile = 1 } /ui: screen pc.h3.lan.screen/ { if (!profile) exit 1; found = 1 }
			END { exit !found }' "$log" || fail browserprofile "the games browser opened before the profile"
		log=$out/browserprofile_off/run.log
		grep -aq "ui: screen pc.mp.offline" "$log" || fail browserprofile "internet play off: SERVER BROWSER did not say how to turn it on"
		grep -aq "4way_start2join_screen\|menus: Server Browser: the profile" "$log" &&
			fail browserprofile "internet play off: the profile screen opened anyway" ;;
	h3gametypes)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3gametypes"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the Halo 3 GAME TYPES, menus/halo3/gametypes.xml, HALO_TEST_PROFILE's profile: the lobby
		# live on SYSTEM LINK; down four to EDIT GAME TYPES, A: the list; Triangle: the bases, A on
		# SLAYER: the editor on a new one; NAME typed; GENERAL: kills to win right twice (50);
		# RESPAWN: respawn time right twice (10 s); PLAYER: shields right (off); WEAPONS AND
		# VEHICLES: vehicle set right (Ghosts); SLAYER SETTINGS looked at; SAVE. Circle: the lobby,
		# its GAME the new one, its live game on it; GAME's picker; START GAME: the game's lobby;
		# EDIT GAME TYPES again: COPY (typed), RENAME (typed), DELETE (the question, DELETE GAME
		# TYPE); Square on one of the game's own (refused); Triangle, SLAYER, GAME right (CTF),
		# Circle: SAVE CHANGES?, DISCARD CHANGES. Screenshots at the Vita's picture)
		d="down:150:450" u="up:150:450" r="right:150:450" w="wait:150:2300"
		gt_shots=${HALO_TEST_GAMETYPES_SHOTS:-$out/h3gametypes_shots}
		gt_pad="wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w @01-game-types:0:300 y $w @02-choose-a-base:0:300"
		gt_pad="$gt_pad a $w a wait:150:1500 @03-editor-new:0:300 $d $d a $w $r $r @04-general:0:300 b $w $d a $w $r $r"
		gt_pad="$gt_pad @05-respawn:0:300 b $w $d a $w $r @06-player:0:300 b $w $d a $w $d $d $r @07-weapons-vehicles:0:300 b $w"
		gt_pad="$gt_pad $d $d a $w @08-slayer-settings:0:300 b $w $d @09-editor-save:0:300 a wait:150:3000 @10-saved:0:300"
		gt_pad="$gt_pad b wait:150:4000 @11-lobby:0:300 $u $u $u a $w @12-game-picker:0:300 b $w $d $d a wait:150:4000"
		gt_pad="$gt_pad @13-pregame:0:300 b wait:150:4000"
		# (then on that save: EDIT GAME TYPES, on the lobby's GAME, Vita Slayer: its options, COPY
		# (typed); the copy's options, RENAME (typed); its options, DELETE: the question, DELETE GAME
		# TYPE; down to SLAYER (the game's own), Square: refused; Triangle, SLAYER: the editor, GAME
		# right (CTF), Circle: SAVE CHANGES?, DISCARD CHANGES)
		gt_files_pad="wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w a $w @14-options:0:300 $d a wait:150:3000"
		gt_files_pad="$gt_files_pad @15-copied:0:300 a $w $d $d a wait:150:3000 @16-renamed:0:300 a $w $d $d $d a $w"
		gt_files_pad="$gt_files_pad @17-delete-question:0:300 $d a wait:150:3000 @18-deleted:0:300 $d x $w @19-builtin-not-deleted:0:300"
		gt_files_pad="$gt_files_pad y $w a $w $d $r @20-editor-ctf:0:300 b $w @21-save-question:0:300 $d a wait:150:3000 @22-discarded:0:300"
		rm -rf "$gt_shots"; mkdir -p "$gt_shots"
		profile_from=$HALO_TEST_PROFILE run h3gametypes "$menus_data" 160 "$gt_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Slayer" HALO_BUTTON_ICONS=playstation \
			HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		rm -rf "$out/h3gametypes_saved"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_saved"
		save_from=$out/h3gametypes_saved run h3gametypes_files "$menus_data" 120 "$gt_files_pad" HALO_MENU_STYLE=halo3 \
			"HALO_TEST_TEXT_INPUT=Vita Copy|Night Copy" HALO_BUTTON_ICONS=playstation \
			HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3gametypes halo3
		check_h3 h3gametypes_files halo3
		cat "$out/h3gametypes_files/run.log" >> "$out/h3gametypes/run.log"
		log=$out/h3gametypes/run.log
		grep -aE "ui: screen pc.h3.(gametypes|gt)|h3 gametypes: [0-9]|h3 gametypes: .*(copied|renamed|deleted|can't)|h3 gametype editor: (saved|editing|the changes)|h3 mp: the game's rules|h3 live lobby: now" "$log" | cut -c1-220 | head -40
		grep -aq "menus: Halo 3 screen h3/gametypes/screen for ui.shell.main_menu.settings_select.multiplayer_setup.playlist_select.gametype_select_screen" "$log" ||
			fail h3gametypes "GAME TYPES did not stand for the Xbox's game type list"
		grep -aq "ui: screen ui.shell.main_menu.settings_select.multiplayer_setup" "$log" &&
			fail h3gametypes "an Xbox game type screen opened with Menu style Halo 3"
		grep -aq "menus: h3 gametypes: 27 game types: 0 of the player's (0 damaged), 27 of the game's" "$log" ||
			fail h3gametypes "the list was not the game's 27 game types (the Xbox's 26 and Infection)"
		for screen in gametypes gtbase gtedit gtopt gtact gtdel gtquit; do
			grep -aq "ui: screen pc.h3.$screen.screen" "$log" || fail h3gametypes "the screen h3/$screen did not open"
		done
		grep -aq "menus: h3 gametype editor: saved Vita Slayer (a new game type): slayer, score 50, lives 0, respawn 300, growth 0, suicide 300, shields 0, health 100, .*vehicles 3" "$log" ||
			fail h3gametypes "SAVE did not save Vita Slayer with the changes"
		grep -aq "menus: h3 gametypes: 28 game types: 1 of the player's (0 damaged), 27 of the game's" "$log" ||
			fail h3gametypes "the list did not have the saved game type (its checksum good)"
		grep -aq "menus: h3 mp lobby: .*GAME: VITA SLAYER" "$log" || fail h3gametypes "the lobby's GAME was not the saved game type"
		grep -aq "menus: h3 live lobby: now Vita Slayer on Battle Creek, sent to its joiners" "$log" &&
			grep -aq "menus: h3 mp: the game's rules: slayer, score 50, lives 0, respawn 300, growth 0, suicide 300, shields 0, .*vehicles 3" "$log" ||
			fail h3gametypes "the live lobby's game was not hosted with the saved game type's rules"
		grep -aq "ui: screen pc.h3.gametype.screen" "$log" || fail h3gametypes "the lobby's GAME picker did not open"
		grep -aq "menus: h3 live lobby: START GAME: the game's lobby" "$log" || fail h3gametypes "START GAME did not open the game's lobby"
		grep -aq "menus: h3 gametypes: Vita Slayer copied as Vita Copy." "$log" || fail h3gametypes "COPY did not copy it"
		grep -aq "menus: h3 gametypes: Vita Copy renamed Night Copy." "$log" || fail h3gametypes "RENAME did not rename the copy"
		grep -aq "menus: h3 gametypes: Night Copy was deleted." "$log" || fail h3gametypes "DELETE did not delete the copy"
		grep -aq "menus: h3 gametypes: The game's own game types can't be deleted." "$log" ||
			fail h3gametypes "Square on one of the game's own was not refused"
		grep -aq "menus: h3 gametype editor: game ctf" "$log" && grep -aq "menus: h3 gametype editor: the changes discarded" "$log" ||
			fail h3gametypes "the save question's DISCARD CHANGES did not leave the editor"
		[ "$(grep -a "menus: h3 gametypes: [0-9]* game types" "$log" | tail -1 | grep -o "[0-9]* of the player's")" = "1 of the player's" ] ||
			fail h3gametypes "the list did not end with the one game type of the player's"
		# (the files: one game type's blam.lst, as the Xbox writes it - its fields at the variant's
		# offsets, game_engine.h -, its SaveMeta.xbx named Vita Slayer)
		gt_files=$(grep -l . "$out"/h3gametypes/save/u/UDATA/*/blam.lst 2>/dev/null)
		[ "$(echo "$gt_files" | grep -c blam.lst)" = 1 ] || fail h3gametypes "not one game type's file: $gt_files"
		gt_fields=$([ -n "$gt_files" ] && python3 - "$(echo "$gt_files" | head -1)" <<'PY'
import struct, sys
block = open(sys.argv[1], "rb").read()
name = block[0:24].decode("utf-16-le").split("\0")[0]
engine, teams, flags = struct.unpack_from("<i?3xI", block, 0x18)
respawn, suicide, lives, health, score, weapons, vehicles = struct.unpack_from("<iiifiii", block, 0x30)
variant_flags, = struct.unpack_from("<H", block, 0x64)
print("|".join(str(v) for v in (name, engine, int(teams), flags & 8 and "noshields" or "shields", respawn, suicide, lives,
	health, score, weapons, vehicles, variant_flags, len(block))))
PY
)
		echo "game type file: $gt_fields"
		[ "$gt_fields" = "Vita Slayer|2|0|noshields|300|300|0|1.0|50|0|3|0|512" ] ||
			fail h3gametypes "blam.lst does not hold the game type as saved"
		[ -n "$gt_files" ] && [ "$(tr -d '\0' < "$(dirname "$(echo "$gt_files" | head -1)")/SaveMeta.xbx")" = "Vita Slayer" ] ||
			fail h3gametypes "SaveMeta.xbx does not name the game type Vita Slayer"
		# (the game plays by it: a local game on Blood Gulch of the saved game type, HALO_NETWORK_TEST's
		# saved=, the profile and a bot - the engine's variant logged, and Ghosts alone on the map)
		rm -rf "$out/h3gametypes_saved"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_saved"
		save_from=$out/h3gametypes_saved run h3gametypes_play "$menus_data" 45 "wait:150:40000" HALO_MENU_STYLE=halo3 \
			"HALO_NETWORK_TEST=local:bloodgulch:saved=Vita Slayer" HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1
		log=$out/h3gametypes_play/run.log
		echo "--- h3gametypes_play"; grep -aE "network test: (the saved|the game plays by|vehicles at tick)" "$log" | head -4 | cut -c1-220
		[ "$(cat "$out/h3gametypes_play/exit")" = 0 ] || fail h3gametypes_play "exit $(cat "$out/h3gametypes_play/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail h3gametypes_play "an assertion, exception or halt"
		grep -aq "network test: the saved game type Vita Slayer" "$log" || fail h3gametypes_play "the saved game type was not found"
		grep -aq "network test: the game plays by: slayer, score 50, lives 0, respawn 300, growth 0, suicide 300, shields 0, .*vehicles 3" "$log" ||
			fail h3gametypes_play "the game did not play by the saved game type's rules"
		grep -aq "network test: vehicles at tick [0-9]*: vehicles.ghost.ghost_mp([0-9]*) [0-9]*$" "$log" ||
			fail h3gametypes_play "the map did not have Ghosts alone (the vehicle set)"
		# (Menu style Halo CE: the Xbox's game type list and edit screen, on the same file: Multiplayer,
		# EDIT GAMETYPES, A on the list - the last used, Vita Slayer -: its edit screen, GAME OPTIONS, SLAYER:
		# its rules)
		rm -rf "$out/h3gametypes_saved"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_saved"
		save_from=$out/h3gametypes_saved run h3gametypes_classic "$menus_data" 45 \
			"wait:150:3000 down:150:700 a wait:150:3000 $d $d $d $d $d $d $d a wait:150:3000 @23-classic-list:0:300 a wait:150:3000 @24-classic-edit:0:300 $d a wait:150:3000 a wait:150:3000 @25-classic-rules:0:300" \
			HALO_MENU_STYLE=classic HALO_BUTTON_ICONS=playstation HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		log=$out/h3gametypes_classic/run.log
		echo "--- h3gametypes_classic"; grep -a "ui: screen" "$log"
		[ "$(cat "$out/h3gametypes_classic/exit")" = 0 ] || fail h3gametypes_classic "exit $(cat "$out/h3gametypes_classic/exit")"
		grep -aq "ui: screen ui.shell.main_menu.settings_select.multiplayer_setup.playlist_select.gametype_select_screen" "$log" &&
			grep -aq "ui: screen ui.shell.main_menu.settings_select.multiplayer_setup.playlist_edit.gametype_edit_screen" "$log" ||
			fail h3gametypes_classic "Menu style Halo CE did not keep the Xbox's game type screens"
		grep -aq "ui: screen pc.h3" "$log" && fail h3gametypes_classic "a Halo 3 screen opened with Menu style Halo CE"
		grep -aqi "checksum failed on playlist profile\|failed to retrieve playlist profile" "$log" "$out/h3gametypes_classic/data/debug.txt" &&
			fail h3gametypes_classic "the Xbox's screens could not read the game type"
		# (in Spanish: the list, the bases, the editor and a group, HALO_TEST_LANG_DIR)
		rm -rf "$out/h3gametypes_saved"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_saved"
		save_from=$out/h3gametypes_saved run h3gametypes_es "$menus_data" 70 \
			"wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w @26-es-game-types:0:300 a $w @27-es-options:0:300 a $w @28-es-editor:0:300 $d $d $d $d a $w @29-es-player:0:300 b $w b $w y $w @30-es-bases:0:300" \
			HALO_MENU_STYLE=halo3 HALO_LANGUAGE=es "HALO_TEST_LANG_DIR=$root/port/vita/app0/lang" HALO_BUTTON_ICONS=playstation \
			HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3gametypes_es halo3
		grep -aq "ui: screen pc.h3.gtopt.screen" "$out/h3gametypes_es/run.log" || fail h3gametypes_es "the editor's PLAYER group did not open"
		# (with the keyboard's icons, the PC's: the editor)
		save_from=$out/h3gametypes_saved run h3gametypes_keys "$menus_data" 50 \
			"wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w @31-keys-game-types:0:300 a $w a $w @32-keys-editor:0:300" \
			HALO_MENU_STYLE=halo3 HALO_BUTTON_ICONS=keyboard HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
		check_h3 h3gametypes_keys halo3
		# (a damaged game type: its blam.lst's score to win changed with its checksum left -
		# edited or damaged since -: GAME TYPES says it is damaged, up to it (the list on the
		# lobby's GAME, Slayer), its EDIT refused (no editor),
		# Square deletes it after the question; the lobby's GAME leaves it out, and a game of it
		# (HALO_NETWORK_TEST saved=) is not played by it)
		rm -rf "$out/h3gametypes_damaged_save"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_damaged_save"
		gt_damaged=$(ls "$out"/h3gametypes_damaged_save/u/UDATA/*/blam.lst 2>/dev/null | head -1)
		if [ -z "$gt_damaged" ]; then
			fail h3gametypes_damaged "no game type's file to damage"
		else
			python3 - "$gt_damaged" <<'PY'
import struct, sys
path = sys.argv[1]
block = bytearray(open(path, "rb").read())
struct.pack_into("<i", block, 0x40, 7)
open(path, "wb").write(block)
PY
			save_from=$out/h3gametypes_damaged_save run h3gametypes_damaged "$menus_data" 60 \
				"wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w $u @33-damaged-list:0:300 a $w a $w @34-damaged-edit-refused:0:300 x $w @35-damaged-delete-question:0:300 $d a wait:150:3000 @36-damaged-deleted:0:300 b $w" \
				HALO_MENU_STYLE=halo3 HALO_BUTTON_ICONS=playstation HALO_SCREENSHOT_DIR="$gt_shots" HALO_GL_VITA_DISPLAY=1 HALO_RENDER_SCALE=1
			check_h3 h3gametypes_damaged halo3
			log=$out/h3gametypes_damaged/run.log
			echo "--- h3gametypes_damaged"; grep -aE "h3 gametypes: |h3 gametype editor: |h3 mp: [0-9]+ game types|h3 mp lobby: .*GAME" "$log" | cut -c1-200 | head -12
			grep -aqE "menus: h3 mp: 27 game types, 1 of the player's left out" "$log" ||
				fail h3gametypes_damaged "the lobby's GAME did not leave the damaged game type out"
			grep -aq "menus: h3 mp lobby: .*GAME: VITA SLAYER" "$log" &&
				fail h3gametypes_damaged "the lobby's GAME was the damaged game type"
			grep -aq "menus: h3 gametypes: 28 game types: 1 of the player's (1 damaged), 27 of the game's" "$log" ||
				fail h3gametypes_damaged "GAME TYPES did not say the game type was damaged"
			grep -aq "menus: h3 gametypes: This game type's file is damaged and can't be loaded. Delete it." "$log" ||
				fail h3gametypes_damaged "EDIT of the damaged game type was not refused"
			grep -aqE "menus: h3 gametype editor: (editing|now)" "$log" &&
				fail h3gametypes_damaged "the editor opened on the damaged game type"
			grep -aq "ui: screen pc.h3.gtedit.screen" "$log" && fail h3gametypes_damaged "the editor's screen opened"
			grep -aq "menus: h3 gametypes: Vita Slayer was deleted." "$log" ||
				fail h3gametypes_damaged "Square did not delete the damaged game type"
			[ "$(grep -a "menus: h3 gametypes: [0-9]* game types" "$log" | tail -1 | grep -o "[0-9]* of the player's")" = "0 of the player's" ] ||
				fail h3gametypes_damaged "the list did not end with none of the player's"
			# (a game of it: Slayer's rules, the file not played)
			rm -rf "$out/h3gametypes_damaged_save"; cp -r "$out/h3gametypes/save" "$out/h3gametypes_damaged_save"
			gt_damaged=$(ls "$out"/h3gametypes_damaged_save/u/UDATA/*/blam.lst | head -1)
			printf '\007' | dd of="$gt_damaged" bs=1 seek=$((0x40)) conv=notrunc status=none
			save_from=$out/h3gametypes_damaged_save run h3gametypes_damaged_play "$menus_data" 30 "wait:150:20000" HALO_MENU_STYLE=halo3 \
				"HALO_NETWORK_TEST=local:bloodgulch:saved=Vita_Slayer" HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1
			log=$out/h3gametypes_damaged_play/run.log
			grep -aqE "network test: (the saved game type Vita Slayer is damaged|no saved game type Vita_Slayer): slayer" "$log" ||
				fail h3gametypes_damaged_play "a game of the damaged game type was not refused"
			grep -aq "network test: the game plays by: .*score 7" "$log" && fail h3gametypes_damaged_play "the damaged file's rules were played"
		fi
		# (Infection, the game's 27th game type: listed last as one of the game's own; its options,
		# COPY (typed): the copy's blam.lst keeps Infection's magic and settings in Slayer's part
		# (game_engine.h), the list says INFECTION for it, and a game of the copy plays Infection)
		gt_inf_downs=""; for gt_step in $(seq 26); do gt_inf_downs="$gt_inf_downs $d"; done
		profile_from=$HALO_TEST_PROFILE run h3gametypes_infection "$menus_data" 60 \
			"wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w$gt_inf_downs $w a $w $d a wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=Vita Infect" HALO_BUTTON_ICONS=playstation
		check_h3 h3gametypes_infection halo3
		log=$out/h3gametypes_infection/run.log
		echo "--- h3gametypes_infection"; grep -aE "h3 gametypes: (Infection|Vita Infect|[0-9]+ game types)" "$log" | cut -c1-200 | head -8
		grep -aq "menus: h3 gametypes: Infection (built-in) (27 of 27)" "$log" ||
			fail h3gametypes_infection "Infection was not the game's 27th game type"
		grep -aq "menus: h3 gametypes: Infection copied as Vita Infect." "$log" || fail h3gametypes_infection "COPY did not copy Infection"
		gt_inf_file=$(ls "$out"/h3gametypes_infection/save/u/UDATA/*/blam.lst 2>/dev/null | head -1)
		gt_inf=$([ -n "$gt_inf_file" ] && python3 - "$gt_inf_file" <<'PY'
import struct, sys
block = open(sys.argv[1], "rb").read()
name = block[0:24].decode("utf-16-le").split("\0")[0]
engine, = struct.unpack_from("<i", block, 0x18)
print(name, engine, block.find(b"INFC") >= 0)
PY
)
		echo "the copy: $gt_inf"
		[ "$gt_inf" = "Vita Infect 2 True" ] || fail h3gametypes_infection "the copy's file is not a Slayer variant with Infection's magic"
		rm -rf "$out/h3gametypes_saved"; cp -r "$out/h3gametypes_infection/save" "$out/h3gametypes_saved"
		save_from=$out/h3gametypes_saved run h3gametypes_infection_play "$menus_data" 40 "wait:150:30000" HALO_MENU_STYLE=halo3 \
			"HALO_NETWORK_TEST=local:bloodgulch:saved=Vita Infect" HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=3
		log=$out/h3gametypes_infection_play/run.log
		grep -aE "network test: the saved|infection: the round begins" "$log" | head -3 | cut -c1-200
		grep -aq "network test: the saved game type Vita Infect" "$log" || fail h3gametypes_infection_play "the saved copy was not found"
		grep -aq "infection: the round begins" "$log" || fail h3gametypes_infection_play "a game of the copy did not play Infection"
		for shot in "$gt_shots"/*.bmp; do
			[ -e "$shot" ] && command -v magick > /dev/null && magick "$shot" "${shot%.bmp}.png" && rm -f "$shot"
		done
		ls "$gt_shots" | head -40 ;;
	h3offlineend)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3offlineend"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (a local game - the network test's, Slayer on Chill Out with a bot, ended at 30 s -;
		# the carnage report goes on to the lobby by itself after 20 s; then START GAME, A joins,
		# A the profile, A: a new local game from the lobby)
		profile_from=$HALO_TEST_PROFILE run h3offlineend "$menus_data" 110 \
			"wait:150:1000 carnage:150:3000 wait:150:30000 @01-lobby:0:300 a:150:3000 a:150:1500 a:150:1500 a:150:1000 level:150:8000" \
			HALO_MENU_STYLE=halo3 HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_TEST_CARNAGE_SECONDS=20 \
			HALO_NETWORK_TEST=local:chillout:slayer HALO_NETWORK_TEST_END=30 HALO_BOTS=1
		log=$out/h3offlineend/run.log
		echo "--- h3offlineend"
		grep -aE "menus: h3 mp: (the local game|START GAME)|ui: screen pc.h3.(mp|carnage)" "$log" | cut -c1-160 | tail -8
		[ "$(cat "$out/h3offlineend/exit")" = 0 ] || fail h3offlineend "exit $(cat "$out/h3offlineend/exit")"
		grep -av "render assertion skipped" "$log" | grep -aqiE "assert|exception|halt_and_catch_fire|segmentation" &&
			fail h3offlineend "an assertion, exception or crash"
		grep -aq "menus: h3 mp: the local game over, back to the Halo 3 lobby (OFFLINE)" "$log" ||
			fail h3offlineend "the local game's end did not go back to the Halo 3 lobby"
		grep -aq "menus: h3 mp: the local game over: let go" "$log" || fail h3offlineend "the old local game was not let go"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.split_screen.splitscreen_map_select_postgame_wrapper" "$log" &&
			fail h3offlineend "split screen's postgame map list opened"
		grep -aq "menus: h3 mp: the local game starts: Slayer on Battle Creek, 1 player, 1 bot" "$log" ||
			fail h3offlineend "START GAME in the lobby after the game did not start a new local game"
		;;
	h3offline)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- h3offline"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (MULTIPLAYER; NETWORK, A: its picker on SYSTEM LINK, down two to OFFLINE (past AD HOC: the
		# harness is a Vita's, config_adhoc_offered), A; down to GAME, A: its
		# picker on SLAYER, down seven to ODDBALL, A; down to MAP, A, down six to CHILL OUT, A; down to
		# START GAME, A: split screen's SELECT PROFILE; A joins, A the profile, A: the game; in it,
		# Start: the pause)
		off_d="down:150:500"
		off_lobby="$h3mppad wait:150:1000 a:150:2000 $off_d $off_d a:150:2500"
		off_start="@01-lobby:0:300 $off_d a:150:3000 @02-select-profile:0:300 a:150:1500 a:150:1500 @03-ready:0:300 a:150:1000"
		off_start="$off_start @04-starting:0:300"
		off_pad="$off_lobby $off_d a:150:2500"
		for row in $(seq 7); do off_pad="$off_pad down:150:300"; done
		off_pad="$off_pad a:150:2500 $off_d a:150:2500"
		for row in $(seq 6); do off_pad="$off_pad down:150:300"; done
		off_pad="$off_pad a:150:2500 $off_start level:150:6000 @05-game:0:300 start:150:2500 @06-pause:0:300"
		profile_from=$HALO_TEST_PROFILE run h3offline "$menus_data" 80 "$off_pad" HALO_MENU_STYLE=halo3 HALO_BOTS=1
		# (a saved game type: Infection copied in GAME TYPES, as h3gametypes_infection does; then the lobby's
		# GAME up to it - the player's own first, the picker on SLAYER -, MAP Blood Gulch, START GAME)
		d="down:150:450" w="wait:150:2300"
		off_inf=""; for row in $(seq 26); do off_inf="$off_inf $d"; done
		profile_from=$HALO_TEST_PROFILE run h3offline_copy "$menus_data" 60 \
			"wait:150:3000 down:150:700 a wait:150:6000 $d $d $d $d a $w$off_inf $w a $w $d a wait:150:4000" \
			HALO_MENU_STYLE=halo3 "HALO_TEST_TEXT_INPUT=Vita Infect"
		off_pad="$off_lobby $off_d a:150:2500 up:150:500 a:150:2500 $off_d a:150:2500"
		for row in $(seq 9); do off_pad="$off_pad down:150:300"; done
		off_pad="$off_pad a:150:2500 $off_start level:150:8000 @05-game:0:300"
		save_from=$out/h3offline_copy/save run h3offline_infection "$menus_data" 85 "$off_pad" HALO_MENU_STYLE=halo3 HALO_BOTS=3
		# (alone: the lobby's GAME and MAP as they are - down past them to START GAME -, no bots)
		profile_from=$HALO_TEST_PROFILE run h3offline_alone "$menus_data" 60 \
			"$off_lobby $off_d $off_d $off_start wait:150:12000 @05-after:0:300" \
			HALO_MENU_STYLE=halo3 HALO_BOTS=0
		for name in h3offline h3offline_infection h3offline_alone; do
			check_h3 $name halo3
			log=$out/$name/run.log
			grep -aE "menus: h3 mp: (START GAME|the local game|network)|menus: h3 mp lobby: NETWORK: OFFLINE|infection: the round|h3 mp pause: " "$log" |
				cut -c1-200 | tail -8
			grep -aq "menus: h3 mp: network OFFLINE" "$log" || fail $name "NETWORK did not go OFFLINE"
			grep -aq "ui: screen pc.h3.join_split.screen" "$log" || fail $name "START GAME did not open split screen's SELECT PROFILE"
			grep -aE "ui: screen ui.shell.main_menu.multiplayer_type_select.split_screen.(splitscreen_map_select|gametype_select)" "$log" &&
				fail $name "the Xbox's split screen map or game type list opened (above)"
		done
		log=$out/h3offline/run.log
		grep -aq "menus: h3 mp lobby: NETWORK: OFFLINE, GAME: ODDBALL, MAP: CHILL OUT" "$log" ||
			fail h3offline "the lobby was not OFFLINE with ODDBALL on CHILL OUT"
		grep -aq "menus: h3 mp: the local game: Oddball on Chill Out (the lobby's GAME and MAP)" "$log" ||
			fail h3offline "SELECT PROFILE's Cross did not set up the local game with the lobby's GAME and MAP"
		grep -aq "menus: h3 mp: the game's rules: oddball, " "$log" || fail h3offline "the local game's rules were not Oddball's"
		grep -aq "menus: h3 mp: the local game starts: Oddball on Chill Out, 1 player, 1 bot$" "$log" ||
			fail h3offline "the local game was not started with the player and the bot"
		grep -aq "menus: the in-game screens: .* added to chillout.map" "$log" || fail h3offline "Chill Out did not load"
		grep -aq "menus: h3 mp pause: Oddball on Chill Out, free for all, the host, 2 players" "$log" ||
			fail h3offline "the game played was not Oddball on Chill Out with the player and the bot"
		grep -a "ui: screen" "$log" | grep -av "pc.h3" && fail h3offline "a screen not of the Halo 3 set opened (above)"
		log=$out/h3offline_infection/run.log
		grep -aq "menus: h3 gametypes: Infection copied as Vita Infect." "$out/h3offline_copy/run.log" ||
			fail h3offline_infection "GAME TYPES did not copy Infection"
		grep -aq "menus: h3 mp lobby: NETWORK: OFFLINE, GAME: VITA INFECT, MAP: BLOOD GULCH" "$log" ||
			fail h3offline_infection "the lobby's GAME was not the saved game type on Blood Gulch"
		grep -aq "menus: h3 mp: the local game starts: Vita Infect on Blood Gulch, 1 player, 3 bots" "$log" ||
			fail h3offline_infection "the local game of the saved game type was not started"
		grep -aq "infection: the round begins" "$log" || fail h3offline_infection "the local game did not play Infection"
		log=$out/h3offline_alone/run.log
		grep -aq "menus: h3 mp: the local game starts: Slayer on Battle Creek, 1 player, 0 bots" "$log" ||
			fail h3offline_alone "the local game was not started with the player alone"
		if grep -aq "menus: h3 mp: the local game did not start in 5 s" "$log"; then
			grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.split_screen.pregame.splitscreen_pregame_wrapper_normal" "$log" ||
				fail h3offline_alone "not started alone, split screen's lobby did not open"
			echo "  (alone: this build's local games need two players: split screen's lobby)"
		else
			grep -aq "menus: the in-game screens: .* added to beavercreek.map" "$log" ||
				fail h3offline_alone "the game alone neither loaded Battle Creek nor fell back to split screen's lobby"
		fi ;;
	msgboxes)
		# (the main menu: each dialog waited for - until/ -, its picture, then answered)
		dialogs="150:@error 6;350:@error 31;550:@abort 33;750:@say Halo: custom map|The download was damaged.;950:@ask Halo: custom map|Download mygulch (12.3 MB) from the host?;1150:@answer;1200:@ask FRIEND INVITE|Bravo invites you to their game. Join?;1400:@answer"
		run msgboxes "$menus_data" 110 "until/dlg\\error:150:1500 @error6 b:150:1500 until/h3\\main:150:500 until/dlg\\error:150:1500 @error31 a:150:1500 until/dlg\\abort:150:1500 @abort a:150:1500 until/dlg\\message:150:1500 @message a:150:1500 until/dlg\\question:150:1500 @question down:150:800 @question_yes a:150:1500 until/h3\\main:150:1500 until/dlg\\question:150:1500 @question2 b:150:1500 wait:150:4000" \
			HALO_MENU_STYLE=halo3 HALO_TEST_DIALOGS=1 "HALO_TEST_COMMANDS=$dialogs"
		log=$out/msgboxes/run.log
		echo "--- msgboxes"
		grep -aE "h3 dialog|ui: screen|question's answer" "$log"
		[ "$(cat "$out/msgboxes/exit")" = 0 ] || fail msgboxes "exit $(cat "$out/msgboxes/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail msgboxes "an assertion, exception or halt"
		for said in 'pc.h3.dlg.error.screen "CONNECTION LOST"' 'pc.h3.dlg.error.screen "DAMAGED FILE"' \
			'pc.h3.dlg.abort.screen "STORAGE FULL"' 'pc.h3.dlg.message.screen "CUSTOM MAP"' \
			'pc.h3.dlg.question.screen "CUSTOM MAP"' 'pc.h3.dlg.question.screen "FRIEND INVITE"'; do
			grep -aqE "menus: h3 dialog: $said" "$log" || fail msgboxes "no Halo 3 dialog $said"
		done
		grep -aq "ui: screen ui.shell.error" "$log" && fail msgboxes "an Xbox box opened with Menu style Halo 3"
		grep -aq "menus: h3 dialog: message dismissed" "$log" || fail msgboxes "OK did not dismiss the message"
		[ "$(grep -a "question's answer" "$log" | sed 's/.*answer: //' | tr '\n' ' ')" = "1 0 " ] ||
			fail msgboxes "the questions were not answered YES, then NO (Circle)"
		[ "$(grep -ac 'ui: screen pc.h3.main.screen' "$log")" -ge 7 ] ||
			fail msgboxes "a dialog did not go back to the main menu as it closed"
		# (Menu style Halo CE: the Xbox's boxes; the message and question the platform's - a run
		# nobody watches answers no)
		run msgboxes_classic "$menus_data" 110 "until/error_modal:150:1500 @classic_error6 a:150:1500 until/main_menu\\main_menu:150:500 until/error_modal:150:1500 @classic_error31 a:150:1500 until/error_abort:150:1500 @classic_abort a:150:1500 wait:150:6000" \
			HALO_MENU_STYLE=classic HALO_TEST_DIALOGS=1 "HALO_TEST_COMMANDS=$dialogs"
		log=$out/msgboxes_classic/run.log
		echo "--- msgboxes (Menu style Halo CE)"
		grep -aE "h3 dialog|ui: screen|question's answer" "$log"
		[ "$(cat "$out/msgboxes_classic/exit")" = 0 ] || fail msgboxes "Halo CE: exit $(cat "$out/msgboxes_classic/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail msgboxes "Halo CE: an assertion, exception or halt"
		[ "$(grep -ac 'ui: screen ui.shell.error.error_modal_fullscreen' "$log")" = 2 ] ||
			fail msgboxes "Halo CE: the Xbox's error box did not open for both errors"
		grep -aq "ui: screen ui.shell.error.error_abort_to_dashboard" "$log" || fail msgboxes "Halo CE: no Xbox abort box"
		grep -aq "h3 dialog\|pc.h3.dlg" "$log" && fail msgboxes "Halo CE: a Halo 3 dialog opened"
		[ "$(grep -a "question's answer" "$log" | sed 's/.*answer: //' | tr '\n' ' ')" = "0 0 " ] ||
			fail msgboxes "Halo CE: the questions were not the platform's (a hidden window's: no)"
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "SKIP: msgboxes' campaign: no HALO_TEST_PROFILE (a blam.sav with every level)"; continue
		fi
		# (a level from the Halo 3 lobby, its pause: REVERT TO CHECKPOINT, Circle; again, down to
		# REVERT TO CHECKPOINT, Cross; the pause, RESTART MISSION, Circle; SAVE AND QUIT)
		profile_from=$HALO_TEST_PROFILE run msgboxes_campaign "$menus_data" 200 "wait:150:3000 a wait:150:3000 a wait:150:3000 a wait:150:2500 down:150:600 down:150:600 down:150:600 a wait:150:2500 down:150:600 down:150:600 a level:150:25000 start:150:3000 until/h3\\pause down:150:1200 a:150:1500 until/dlg\\revert @revert b:150:1500 until/h3\\pause @pause_again a:150:1500 until/dlg\\revert down:150:800 @revert_yes a:1000:8000 start:150:3000 until/h3\\pause down:150:800 down:150:800 a:150:1500 until/dlg\\restart @restart b:150:1500 until/h3\\pause down:150:800 a:150:1000 until/dlg\\savequit @saving wait:150:1000 until/h3\\main:150:3000 @menu" \
			HALO_MENU_STYLE=halo3 HALO_TEST_COMMANDS="L150:@skip;L300:@skip"
		log=$out/msgboxes_campaign/run.log
		echo "--- msgboxes (a level's pause)"
		grep -aE "h3 dialog|ui: screen|pause menu" "$log" | tail -24
		[ "$(cat "$out/msgboxes_campaign/exit")" = 0 ] || fail msgboxes "campaign: exit $(cat "$out/msgboxes_campaign/exit")"
		grep -aqiE "assert|exception|halt" "$log" && fail msgboxes "campaign: an assertion, exception or halt"
		[ "$(grep -ac 'menus: h3 dialog: pc.h3.dlg.revert.screen "REVERT TO CHECKPOINT"' "$log")" = 2 ] ||
			fail msgboxes "campaign: REVERT TO CHECKPOINT's question did not open twice"
		[ "$(grep -ac 'pause menu: restart at checkpoint' "$log")" = 1 ] ||
			fail msgboxes "campaign: not reverted once (Circle must not, REVERT TO CHECKPOINT must)"
		# (one revert for REVERT TO CHECKPOINT's Cross, held a second as a hand may: a10's checkpoint is
		# its opening cinematic's, and the held press skipped it again each tick - ~9 reverts, insider.21)
		reverts=$(sed -n '/pause menu: restart at checkpoint/,/test pad: start/p' "$log" | grep -ac "game state: reverted to the checkpoint")
		echo "reverts for REVERT TO CHECKPOINT: $reverts"
		[ "$reverts" = 1 ] || fail msgboxes "campaign: REVERT TO CHECKPOINT reverted $reverts times (1 wanted)"
		grep -aq 'menus: h3 dialog: pc.h3.dlg.restart.screen "RESTART MISSION"' "$log" ||
			fail msgboxes "campaign: RESTART MISSION's question did not open"
		grep -aq "pause menu: restart level" "$log" && fail msgboxes "campaign: Circle restarted the mission"
		grep -aq 'menus: h3 dialog: pc.h3.dlg.savequit.screen "SAVING"' "$log" &&
			grep -aq 'ui: screen pc.h3.dlg.saved.screen' "$log" || fail msgboxes "campaign: SAVE AND QUIT's Halo 3 boxes did not open"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'pc.h3.main.screen' ||
			fail msgboxes "campaign: SAVE AND QUIT did not go back to the Halo 3 main menu"
		grep -aq "ui: screen ui.shell.error" "$log" && fail msgboxes "campaign: an Xbox box opened with Menu style Halo 3"
		# (Menu style Halo CE: a new profile's a10, the Xbox's pause and its question; Circle back)
		run msgboxes_campaign_classic "$menus_data" 110 "a:150:3000 start:150:3000 wait:150:6000 a level:150:25000 start:150:3000 until/pause_game:150:500 down:150:1500 a:150:1500 until/confirm_restartatsave:150:1500 @classic_revert b:150:1500 until/pause_game:150:1500 @classic_pause_back wait:150:3000" \
			HALO_MENU_STYLE=classic HALO_TEST_COMMANDS="L150:@skip;L300:@skip"
		log=$out/msgboxes_campaign_classic/run.log
		echo "--- msgboxes (a level's pause, Menu style Halo CE)"
		grep -aE "h3 dialog|ui: screen|pause menu" "$log" | tail -8
		[ "$(cat "$out/msgboxes_campaign_classic/exit")" = 0 ] || fail msgboxes "campaign, Halo CE: exit $(cat "$out/msgboxes_campaign_classic/exit")"
		grep -aq "ui: screen ui.shell.error.confirm_restartatsave_modal_fullscreen" "$log" ||
			fail msgboxes "campaign, Halo CE: the Xbox's question did not open"
		grep -a "ui: screen" "$log" | tail -1 | grep -q 'ui.shell.solo_game.pause_game.pause_game' ||
			fail msgboxes "campaign, Halo CE: Circle did not go back to the Xbox's pause"
		grep -aq "h3 dialog\|pc.h3.dlg" "$log" && fail msgboxes "campaign, Halo CE: a Halo 3 dialog opened"
		grep -aq "pause menu: restart at checkpoint" "$log" && fail msgboxes "campaign, Halo CE: Circle reverted" ;;
	ceplusmain)
		# (the main menu's rows under SETTINGS: down three to FORGE, four to FRIENDS, five to FILE
		# SHARE, six to MOD MANAGER; A, then B back)
		d="down:150:500"
		for row in "3 forge pc.h3.forge.lobby" "4 friends pc.h3.friends.screen" "5 fileshare pc.h3.fileshare.screen" \
			"6 mods pc.h3.mods.screen"; do
			set -- $row
			downs=""
			for i in $(seq "$1"); do downs="$downs $d"; done
			run "ceplusmain_$2" "$menus_data" 20 "wait:150:3000$downs a:150:4000 b:150:3000" HALO_SCENE_PACKS=0
			log=$out/ceplusmain_$2/run.log
			echo "--- ceplusmain ($2)"
			grep -aE "ui: screen|menus: Menu style" "$log" | head -6
			[ "$(cat "$out/ceplusmain_$2/exit")" = 0 ] || fail ceplusmain "$2: exit $(cat "$out/ceplusmain_$2/exit")"
			grep -aqiE "assert|exception|halt_and_catch_fire" "$log" && fail ceplusmain "$2: an assertion, exception or halt"
			grep -aqE "menus: Menu style Halo CE: the Xbox screens' [0-9]+ widgets with the port's rows" "$log" ||
				fail ceplusmain "$2: the Xbox screens were not given the port's rows"
			grep -aq "ui: screen $3" "$log" || fail ceplusmain "$2: the row did not open its screen ($3)"
			sed -n "/ui: screen $3/,\$p" "$log" | grep -aq "ui: screen ui.shell.main_menu.main_menu" ||
				fail ceplusmain "$2: B did not go back to the Xbox's main menu"
		done
		# (Menu style Halo 3: the map's own list, the Halo 3 main menu, none of the port's rows)
		run ceplusmain_h3 "$menus_data" 15 "wait:150:3000" HALO_MENU_STYLE=halo3
		log=$out/ceplusmain_h3/run.log
		grep -aqE "menus: Menu style Halo 3: the Xbox screens' [0-9]+ widgets as the map has them" "$log" ||
			fail ceplusmain "Menu style Halo 3: the Xbox screens kept the port's rows"
		grep -aq "ui: screen pc.h3.main.screen" "$log" || fail ceplusmain "Menu style Halo 3: not the Halo 3 main menu" ;;
	cepluspregame)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- cepluspregame"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (the test's map variants of Blood Gulch: forge_files_test --write, as run_forge_test.sh's)
		if ! ${CC:-cc} -O1 -D_GNU_SOURCE -I"$root/port/linux/game" -o "$out/forge_files_test" "$here/forge_files_test.c" \
			"$root/port/linux/game/forge_files.c" 2> "$out/forge_files_test.log"; then
			fail cepluspregame "forge_files_test did not build ($out/forge_files_test.log)"; continue
		fi
		mkdir -p "$out/cepluspregame-save/u/UDATA/869079979327" "$out/cepluspregame-save/forge/bloodgulch"
		cp "$HALO_TEST_PROFILE" "$out/cepluspregame-save/u/UDATA/869079979327/blam.sav"
		[ -r "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" ] &&
			cp "$(dirname "$HALO_TEST_PROFILE")/SaveMeta.xbx" "$out/cepluspregame-save/u/UDATA/869079979327/"
		"$out/forge_files_test" --write "$out/cepluspregame-save/forge/bloodgulch" > /dev/null
		# (MULTIPLAYER, CREATE GAME SYSTEM LINK - the fifth row -, the profile; Y creates; right nine
		# times to Blood Gulch, A, the game type, A; the pregame: the right trigger, down to MAP
		# VARIANT, A twice, up to PLAYERS, A; B, B)
		d="down:150:500"
		r=""; for i in $(seq 9); do r="$r right:150:400"; done
		save_from="$out/cepluspregame-save" run cepluspregame "$menus_data" 75 "wait:150:3000 down:150:800 a:150:3000 $d $d $d $d a:150:4000 a:150:2500 a:150:2500 a:150:4000 y:150:4000$r a:150:3000 a:150:3000 a:150:4000 rt:150:3000 $d a:150:2000 a:150:2000 up:150:1000 a:150:3000 b:150:2500 b:150:3000" \
			HALO_NET_ONLINE=false HALO_SCENE_PACKS=0
		log=$out/cepluspregame/run.log
		echo "--- cepluspregame"
		grep -aE "ui: screen|ce lobby|ce pregame|forge net: hosting" "$log" | tail -14
		[ "$(cat "$out/cepluspregame/exit")" = 0 ] || fail cepluspregame "exit $(cat "$out/cepluspregame/exit")"
		grep -aqiE "assert|exception|halt_and_catch_fire" "$log" && fail cepluspregame "an assertion, exception or halt"
		grep -aq "ui: screen ui.shell.main_menu.multiplayer_type_select.connected.pregame.connected_pregame_screen" "$log" ||
			fail cepluspregame "the Xbox's pregame did not open"
		grep -aq "ui: screen pc.ce.lobby.screen" "$log" || fail cepluspregame "the right trigger did not open LOBBY OPTIONS"
		grep -aq "menus: ce lobby options: PLAYERS, MAP VARIANT NONE" "$log" ||
			fail cepluspregame "LOBBY OPTIONS did not offer PLAYERS and MAP VARIANT (and no INVITE FRIENDS on system link)"
		[ "$(grep -ac "menus: ce pregame: the map variant .* is played on Blood Gulch" "$log")" -ge 2 ] ||
			fail cepluspregame "MAP VARIANT did not step through Blood Gulch's variants"
		grep -aq "forge net: hosting a game of a map variant of levels.test.bloodgulch.bloodgulch" "$log" ||
			fail cepluspregame "the map variant was not hosted (the joiners asked to play it)"
		sed -n '/ui: screen pc.ce.lobby.screen/,$p' "$log" | grep -aq "ui: screen pc.h3.players.screen" ||
			fail cepluspregame "PLAYERS did not open the players' screen"
		grep -a "ui: screen" "$log" | tail -1 | grep -q "connected_pregame_screen" ||
			fail cepluspregame "B, B did not go back to the pregame" ;;
	cepluspause)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- cepluspause"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (Start, down to PLAYERS, A, B - back on PLAYERS, as the game keeps a screen's row -; down
		# to PARTY, A; up twice, A: RESUME GAME)
		pause_env="HALO_NETWORK_TEST=local:bloodgulch:team_slayer HALO_NETWORK_TEST_PROFILE=1 HALO_NETWORK_TEST_START=3 HALO_BOTS=1"
		profile_from=$HALO_TEST_PROFILE run cepluspause "$menus_data" 55 "wait:150:32000 start:150:3000 down:150:1200 a:150:3000 b:150:2500 down:150:1200 a:150:2000 up:150:1000 up:150:1000 a:150:3000" \
			$pause_env
		log=$out/cepluspause/run.log
		echo "--- cepluspause"
		grep -aE "ui: screen|Party page|h3 players: [0-9]+ players" "$log" | tail -8
		[ "$(cat "$out/cepluspause/exit")" = 0 ] || fail cepluspause "exit $(cat "$out/cepluspause/exit")"
		grep -aqiE "assert|exception|halt_and_catch_fire" "$log" && fail cepluspause "an assertion, exception or halt"
		grep -aqE "menus: Menu style Halo CE: the Xbox screens' [0-9]+ widgets with the port's rows" <(sed -n '/new map: loading/,$p' "$log") ||
			fail cepluspause "the level's Xbox pause was not given the port's rows"
		grep -aq "ui: screen ui.shell.multiplayer_game.pause_game.1p_pause_game" "$log" || fail cepluspause "Start did not open the Xbox's pause"
		sed -n '/ui: screen ui.shell.multiplayer_game.pause_game.1p_pause_game/,$p' "$log" | grep -aq "ui: screen pc.h3.players.screen" ||
			fail cepluspause "PLAYERS did not open the players' screen"
		sed -n '/ui: screen pc.h3.players.screen/,$p' "$log" | grep -aq "ui: screen ui.shell.multiplayer_game.pause_game.1p_pause_game" ||
			fail cepluspause "B did not go back to the pause"
		grep -aq "menus: h3 mp pause: the Party page asked for" "$log" || fail cepluspause "PARTY did not ask for the Party page"
		grep -aq "requesting a player removal" "$out/cepluspause/data/debug.txt" 2>/dev/null &&
			fail cepluspause "a row left the game"
		grep -aq "ui: screen ui.shell.main_menu.main_menu" <(sed -n '/ui: screen ui.shell.multiplayer_game.pause_game.1p_pause_game/,$p' "$log") &&
			fail cepluspause "the game was left" ;;
	ceplusforge)
		if [ ! -r "${HALO_TEST_PROFILE:-}" ]; then
			echo "--- ceplusforge"; echo "SKIP: no HALO_TEST_PROFILE"; continue
		fi
		# (FORGE, the fourth row; up to NETWORK, A, down past LOCAL - the picker goes round - to
		# ONLINE, down to SYSTEM LINK, A; down three to START FORGE, A: the profile, A, A)
		d="down:150:600"
		profile_from=$HALO_TEST_PROFILE run ceplusforge "$menus_data" 45 "wait:150:3000 $d $d $d a:150:4000 up:150:600 a:150:2500 $d $d a:150:3000 $d $d $d a:150:4000 a:150:2500 a:150:2500 a:150:6000" \
			HALO_NET_ONLINE=false
		log=$out/ceplusforge/run.log
		echo "--- ceplusforge"
		grep -aE "ui: screen|forge lobby: (network|START)|forge net: hosting|network Forge" "$log" | tail -10
		[ "$(cat "$out/ceplusforge/exit")" = 0 ] || fail ceplusforge "exit $(cat "$out/ceplusforge/exit")"
		grep -aqiE "assert|exception|halt_and_catch_fire" "$log" && fail ceplusforge "an assertion, exception or halt"
		grep -aq "menus: forge lobby: network SYSTEM LINK" "$log" || fail ceplusforge "NETWORK did not choose SYSTEM LINK"
		grep -aq "menus: forge lobby: START FORGE (Halo CE menus): the profile, then the game hosted on" "$log" ||
			fail ceplusforge "START FORGE on a network did not host from the Xbox's menus"
		grep -aq "forge net: hosting a Forge game of" "$log" || fail ceplusforge "the network Forge game was not hosted"
		grep -a "ui: screen" "$log" | tail -1 | grep -q "connected_pregame_screen" ||
			fail ceplusforge "the Xbox's pregame did not open" ;;
	*)
		echo "usage: $0 [$(echo $all_cases $pc_cases | tr ' ' '|')]..." >&2; exit 2 ;;
	esac
done
echo "logs in $out"
[ $status = 0 ] && echo PASS
test_out_done $status
exit $status
