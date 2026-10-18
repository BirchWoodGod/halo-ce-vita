/*
FORGE.H

Forge's sandbox, a spike (forge.c): the monitor's flying camera, objects of
the map spawned, grabbed, turned and deleted in a local game.
*/

#ifndef __FORGE_H
#define __FORGE_H

#include "cseries.h"
#include "real_math.h"

/* what the monitor's controls ask of the flying camera this frame */
struct forge_camera_input
{
	real_euler_angles2d facing_delta;
	/* along the camera's yaw: i forward, j left, k up */
	real_vector3d translation;
};

/* a console command that starts with "forge" (hs.c, as typed, init.txt,
HALO_TEST_COMMANDS): what follows the word. TRUE if it was understood */
boolean forge_command(
	char const *arguments);

/* (director.c, each frame before the cameras update) whether this local
player is the monitor: its flying camera then moves by input, and the
player's own controls are not read */
boolean forge_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input);

/* (main.c, each frame once the cameras have updated, before the tick
starts) the monitor's buttons and the object it holds */
void forge_update(
	void);

/* (director.c) puts a local player's director on the flying camera at
position, looking along forward (fly), or back on the player's own */
void director_forge_camera(
	short local_player_index,
	boolean fly,
	real_point3d const *position,
	real_vector3d const *forward);

#endif
