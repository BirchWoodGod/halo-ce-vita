/*
MAP_SHARE.H

Map sharing (map_share.c): a joiner without the host's custom map, or with
another copy of it, is offered the host's copy instead of being turned away,
and downloads it over its connection to the host in the lobby, or before it
joins the host's game in progress. The rules and checks are
map_share_protocol.c's.
*/

#ifndef __MAP_SHARE_H
#define __MAP_SHARE_H

/* ---------- structures */

struct network_game;
struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* ---------- prototypes/MAP_SHARE.C */

/* (a joiner, network_client_manager.c) The host's game settings `game`
name the level `level_name`, a custom map this machine lacks (`replacing`:
has another copy of), of which the host sent the fingerprint `identity`.
The question names the host (the game's name), and warns in a game joined
from the public lobby; the setting Map downloads (HALO_MAP_SHARE_FROM) may
refuse it (`why`). TRUE
when the joiner stays to ask the host for it (the map is then precached
once downloaded); FALSE when it cannot be offered (map sharing off, a host
that sent no fingerprint, a badly named map, not in the lobby): the caller
refuses as before, adding `why` (`why_size` characters; empty: nothing to
add). */
boolean map_share_client_offer(
	struct network_game_client *client,
	struct network_game const *game,
	char const *level_name,
	unsigned long identity,
	boolean replacing,
	char *why,
	long why_size);

/* (a joiner, network_client_manager.c) The host's game settings name
`level_name`, a Custom Edition map this machine has (the host's copy), with
PC maps off: the player is asked to turn PC maps on (then it is precached),
and leaves the game, told why, if not. FALSE when the joiner cannot be
asked (the game started: `why`): the caller refuses. */
boolean map_share_client_offer_pc_maps(
	struct network_game_client *client,
	char const *level_name,
	char *why,
	long why_size);

/* (network_client_manager.c) The host's game settings name a level other
than the one asked for: a download of another map stops. */
void map_share_client_map_changed(
	char const *level_name);

/* (network_client_manager.c) The joiner's client goes: its download stops
(the file deleted, the progress hidden). */
void map_share_client_dispose(
	struct network_game_client *client);

/* Whether a download is under way (or offered): the joiner reports no map
precached meanwhile. */
boolean map_share_client_busy(
	void);

/* (network_game_client_add_player) Whether the joiner adds no player to the
host's game now: while it asks the host about its map (the host's game may
be under way), and while it asks and downloads the map of a game in
progress, which it joins once the map is here. */
boolean map_share_client_holds_players(
	void);

/* (network_client_message_handler.c) The host started the game on
`level_name` (the host's copy `host_identity`, the game's map version):
FALSE when this machine cannot load it (a question or a download under way,
a map it cannot load as it is, an Xbox level whose file here is not the
Xbox map, whole: a game joined in progress), which it then leaves at its
next frame, the player told why, rather than stopping as a damaged disc. */
boolean map_share_client_game_starting(
	struct network_game_client *client,
	char const *level_name,
	unsigned long host_identity);

/* (cache_files.c) The map `level_name` could not be precached on this
joiner (cache_files_precache_failed: missing, cut short, a Halo PC map named
as an Xbox level with PC maps off...): the joiner leaves its game at its
next frame, the player told `why`, rather than stopping as a damaged disc.
FALSE when this machine is no joiner (it hosts, or plays alone). */
boolean map_share_client_precache_failed(
	char const *level_name,
	char const *why);

/* Each frame of a joiner: the question, the progress, the timeouts. FALSE
when the joiner leaves the game (the player said no, cancelled, or the
download failed; the player has been told why). */
boolean map_share_client_update(
	struct network_game_client *client);

/* (network_client_message_handler.c) the host's answer and data messages,
from the host: whole messages, header first */
void map_share_client_handle_answer(
	struct network_game_client *client,
	word *message,
	short message_size);
void map_share_client_handle_data(
	struct network_game_client *client,
	word *message,
	short message_size);

/* (network_server_message_handler.c) a joined machine's request */
void map_share_server_handle_request(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_size);

/* Each frame of a host (network_server_manager.c): sends what the uploads'
windows and the rate allow (lower while its game is under way); refuses
them as its game starts, but for those to machines joining it in progress,
and as it ends. */
void map_share_server_update(
	struct network_game_server *server);

/* (network_server_manager.c) Whether a machine joining the game in
progress, which adds no player, is kept: it was offered the map and is
asking, downloading it, or putting it in place (else the host drops a
machine that adds no player, as one that holds its place for nothing). */
boolean map_share_server_machine_waits(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine);

/* (network_server_message_handler.c) Whether a machine joining the game in
progress was refused the map (it cannot play): its players are not added. */
boolean map_share_server_machine_refused(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine);

/* (the lobby's machine list, ui_widget_game_data_input_functions.c) How
much of the map the host is sending to the machine `machine_index` it has
(percent), or NONE when it sends none. */
short map_share_server_machine_percent(
	long machine_index);

/* (network_server_manager.c) Whether the host's game waits to start: a
joined machine is downloading its map. */
boolean map_share_server_holds_start(
	struct network_game_server *server);

/* The host's game ended: every upload stops. */
void map_share_server_dispose(
	void);

#endif
