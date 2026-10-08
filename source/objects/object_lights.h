/*
OBJECT_LIGHTS.H

header included in hcex build.
*/

#ifndef __OBJECT_LIGHTS_H
#define __OBJECT_LIGHTS_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct render_lighting;

/* ---------- prototypes/OBJECT_LIGHTS.C */

void lights_initialize(void);
void lights_dispose(void);
void lights_initialize_for_new_map(void);
void lights_dispose_from_old_map(void);
/* port: the lights made again, empty, if their array is out of order (object_lights.c) */
boolean lights_port_recover(void);
boolean lights_enable(boolean enable);
long light_new(long definition_index, long object_index, short object_attachment_index, short object_function_index, short object_change_color_index);
long light_new_unattached(
	long definition_index,
	long object_index,
	short object_node_index,
	real_point3d const *position,
	real_vector3d const *direction,
	real scale);
void light_delete(long light_index);
#ifdef HALO_LINUX
/* (port) the effects' lights retired and moved: by the tick thread when it
has one, otherwise by lights_preprocess_scene (object_lights.c) */
void lights_update_unattached(void);
/* (port, debug) HALO_STRESS_LIGHTS=n: n effect lights a frame around the player */
void lights_stress_update(void);
#endif
#ifdef HALO_LINUX
/* port: the sky's queued lens flares submitted for the window drawn now */
void lights_port_submit_queued_lens_flares(
	void);
#endif
void lights_queue_lens_flare(
	long lens_flare_definition_index,
	real_point3d const *position,
	real_vector3d const *direction,
	real_vector3d const *up,
	real_rgb_color const *color,
	real scale);
void light_particle(
	real_point3d const *position,
	real_rgb_color *light,
	real_rgb_color *diffuse,
	boolean block);

real object_get_self_illumination(long object_index);
void lights_illumination_at_point(union real_point3d const *point, struct location const *location, union real_rgb_color *color);

void light_disconnect_from_map(long light_index);
void light_reconnect_to_map(long light_index);
void lights_disconnect_from_structure_bsp(
	void);
void lights_reconnect_to_structure_bsp(
	void);
void lights_prepare_for_object_dynamic(
	long object_index,
	struct render_lighting *lighting);
#ifdef HALO_LINUX
boolean lights_render_snapshot_enabled(void);
void lights_find_for_object_dynamic(long object_index, long *light_indices, short *light_count);
void lights_translate_for_object_dynamic(long const *light_indices, short light_count,
	struct render_lighting *lighting);
#endif

/* ---------- globals */

/* ---------- public code */

#endif // __OBJECT_LIGHTS_H
