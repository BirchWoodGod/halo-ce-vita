/*
LATENCY_METER.H

A network game's round trip to the host, at the screen's top right while
the game is played (latency_meter.c).
*/

#ifndef __LATENCY_METER_H
#define __LATENCY_METER_H

/* whether it is shown: Latency meter On, a network game being played */
boolean latency_meter_shown(void);
/* (the scoreboard's Ping column, game_engine.c) a player's ping in
milliseconds and its colour, NONE when it is not known here (a client: its
own players' as it measures them, the rest as the host told it) */
long latency_meter_player_ping(short player_index, real_argb_color *color);
/* (interface.c's overlays, over the whole screen) */
void latency_meter_draw(void);

#endif
