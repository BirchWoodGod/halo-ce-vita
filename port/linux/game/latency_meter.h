/*
LATENCY_METER.H

A network game's round trip to the host, at the screen's top right while
the game is played (latency_meter.c).
*/

#ifndef __LATENCY_METER_H
#define __LATENCY_METER_H

/* Latency meter On (the settings panel, HALO_LATENCY_METER, config.toml's
network.latency_meter): the meter, the scoreboard's Ping column and the
host's ping table (network_distributed.c) */
boolean latency_meter_enabled(void);
/* whether it is shown: Latency meter On, a network game being played */
boolean latency_meter_shown(void);
/* (the scoreboard's Ping column, game_engine.c) a player's ping in
milliseconds and its colour, NONE when it is not known here (a client knows
its own players' and, from the host's table, the others') */
long latency_meter_player_ping(short player_index, real_argb_color *color);
/* (the Halo 3 scoreboard's ping bars, menu_functions.c) the bars of a
player's ping, 1 (poor) to 4 (good), as the meter's; 0 when it is not known
here; *ping (if not NULL) the milliseconds, NONE when not known */
short latency_meter_player_bars(short player_index, long *ping);
/* (interface.c's overlays, over the whole screen) */
void latency_meter_draw(void);

#endif
