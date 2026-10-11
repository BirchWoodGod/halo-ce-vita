/*
HALO_PORT_CAPACITY.H

Memory capacity of the native builds (Windows, Linux, Android), sized for the
session limits in halo_port_limits.h, which includes this file. Game sources
use these values only under #ifdef HALO_LINUX, so the byte-matching MSVC build
keeps the Xbox sizes (given in parentheses below).

Every machine in a session must be built with the same values: the game is
simulated in lockstep on every machine, and a pool that runs full changes the
simulation (an object or a deterministic effect is not created).
*/

#ifndef __HALO_PORT_CAPACITY_H
#define __HALO_PORT_CAPACITY_H

/* ---------- game state

The Xbox game state is 0x345000 bytes at 0x80061000 and ends where the tag
cache begins (0x803A6000). Cache files are linked to that tag cache address,
so the game state cannot grow in place. The native builds put a 16 MB game
state above the tag cache (which ends at 0x819A6000), inside the Xbox memory
window (0x80000000-0x88000000, port/linux/src/platform.h) and below everything
the window hands out top-down (texture and sound caches, Direct3D resources).

The CPU part holds about 13.6 MB of pools at the sizes below (the Xbox pools
fill 3,165,260 of its 0x305000 bytes); the GPU part holds only the decal
vertices, as on the Xbox. A change to a pool's size changes the game state's
layout: saved games of builds before it no longer load. */

#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x81A00000 /* (0x80061000) */
#define HALO_PORT_GAME_STATE_CPU_SIZE 0xFC0000 /* (0x305000) */
#define HALO_PORT_GAME_STATE_GPU_SIZE 0x40000 /* (0x40000) */
#define HALO_PORT_GAME_STATE_SIZE (HALO_PORT_GAME_STATE_CPU_SIZE+HALO_PORT_GAME_STATE_GPU_SIZE)

/* ---------- AI

The Xbox's sizes. Upstream's co-op makes these four times larger (and the
game state 4 MB more) for its extra enemies (port/linux/game/coop_enemies.c);
the Vita keeps the campaign's: a larger game state would leave single
player's saves behind (their layout) and take memory the Vita does not have,
and its co-op host has no extra enemies (network.coop_enemies_mode "none",
port_config.c). With these the extra enemies find no room
(coop_enemies.c leaves a level's 256 actors to it). */

#define HALO_PORT_MAXIMUM_ACTORS 256 /* (256) */
#define HALO_PORT_MAXIMUM_PROPS 768 /* (768) */
#define HALO_PORT_MAXIMUM_SWARMS 32 /* (32) */
#define HALO_PORT_MAXIMUM_SWARM_COMPONENTS 256 /* (256) */

/* ---------- objects */

#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192 /* (2048) */
#define HALO_PORT_OBJECT_MEMORY_POOL_SIZE 0x800000 /* (0x100000) */
/* each of the two reference lists of every cluster partition (collideable
objects, noncollideable objects, lights) */
#define HALO_PORT_MAXIMUM_CLUSTER_REFERENCES 8192 /* (2048) */
#define HALO_PORT_MAXIMUM_RENDERED_OBJECTS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES 1024 /* (256) */
/* objects one explosion can damage */
#define HALO_PORT_MAXIMUM_AREA_OF_EFFECT_OBJECTS 256 /* (64) */
/* object references shared by all script object lists */
#define HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP 1024 /* (128) */

/* ---------- effects, particles, lights and sounds */

#define HALO_PORT_MAXIMUM_EFFECTS 2048 /* (256) */
#define HALO_PORT_MAXIMUM_EFFECT_LOCATIONS 4096 /* (512) */
#define HALO_PORT_MAXIMUM_PARTICLES 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_PARTICLE_SYSTEMS 256 /* (64) */
#define HALO_PORT_MAXIMUM_SYSTEM_PARTICLES 4096 /* (512) */
#define HALO_PORT_MAXIMUM_CONTRAILS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CONTRAIL_POINTS 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_LIGHTS_PER_MAP 4096 /* (896) */
#define HALO_PORT_MAXIMUM_GAME_LOOPING_SOUNDS 4096 /* (1024) */

#endif /* __HALO_PORT_CAPACITY_H */
