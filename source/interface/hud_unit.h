#ifndef __HUD_UNIT_H
#define __HUD_UNIT_H
#pragma once

struct player_datum;

void hud_initialize_unit_interface(
	void);
void hud_initialize_unit_interface_for_new_map(
	void);

void hud_dispose_unit_interface_from_old_map(
	void);
void hud_dispose_unit_interface(
	void);

void hud_fix_unit_data(
	short old_local_player_index,
	short new_local_player_index);

void hud_update_unit(
	void);
void hud_render_unit_interface(
	struct player_datum *player);
void hud_render_damage_indicators(
	short local_player_index);
void hud_play_unit_sounds(
	struct player_datum const *player,
	boolean show_hud);
#ifdef HALO_LINUX
/* (port) hud_play_unit_sounds from the render: made at the tick's join while
a tick runs on its own thread (hud_unit.c) */
void hud_play_unit_sounds_from_render(
	struct player_datum const *player,
	boolean show_hud);
#else
#define hud_play_unit_sounds_from_render hud_play_unit_sounds
#endif
void hud_tick_shield(
	long player_index,
	real amount);

/* port: network co-op (port/linux/game/network_coop.c) */
long hud_unit_port_script_flags(
	void);
void hud_unit_port_set_script_flags(
	long flags);
void scripted_hud_show_health(
	boolean show);
void scripted_hud_blink_health(
	boolean blink);
void scripted_hud_show_shield(
	boolean show);
void scripted_hud_blink_shield(
	boolean blink);
void scripted_hud_show_motion_sensor(
	boolean show);
void scripted_hud_blink_motion_sensor(
	boolean blink);

#endif // __HUD_UNIT_H
