/*
COOP_PROGRESS.H

Network co-op's campaign progress (coop_progress.c): a level won together
recorded in each machine's players' profiles, as a solo or split screen
win records it, so that the campaign's next level is unlocked.
*/

#ifndef __COOP_PROGRESS_H
#define __COOP_PROGRESS_H

/* the host: the co-op level (a map name) won, on its difficulty */
void coop_progress_host_won(char const *map_name, short difficulty);
/* a client: its co-op level began (network_game_create_game_objects) */
void coop_progress_client_level_started(char const *map_name, short difficulty);
/* a client: its round ended and its host sent the lobby (switching to
pregame), then the lobby's settings (their map) */
void coop_progress_client_round_ended(void);
void coop_progress_client_lobby_map(char const *map_name);

#endif /* __COOP_PROGRESS_H */
