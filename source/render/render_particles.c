/*
RENDER_PARTICLES.C

symbols in this file:
0017BD20 00b0:
	_local_player_is_first_person (0000)
0017BDD0 0030:
	_compare_rendered_particles (0000)
0017BE00 0490:
	_render_particles (0000)
0030E180 05f8:
	_render_particles_section (0000)
*/

/* ---------- headers */

#include <stdlib.h>

#include "camera/director.h"
#include "cseries/cseries.h"
#include "cseries/profile.h"
#include "effects/particles.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "render_sprite.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"

/* ---------- constants */

enum
{
#ifdef HALO_LINUX
	/* render_particles lists every visible particle in an array of this size
	without a bound check, so it must stay the size of the native builds'
	particle pool (effects/particles.c, halo_port_capacity.h) */
	MAXIMUM_RENDERED_PARTICLES = HALO_PORT_MAXIMUM_PARTICLES,
#else
	MAXIMUM_RENDERED_PARTICLES = 1024,
#endif
	MAXIMUM_RENDERED_PARTICLE_GROUPS = 512,
};

/* ---------- macros */

/* ---------- structures */

struct rendered_particle_datum
{
	short particle_index;
	short definition_index;
	short cluster_index;
	boolean attached_to_first_person_weapon;
	byte pad;
};

typedef char rendered_particle_size_assert[
	sizeof(struct rendered_particle_datum) == 8 ? 1 : -1];

/* ---------- prototypes */

/* ---------- globals */

struct profile_section render_particles_section =
{
	"render_particles",
	NONE,
	TRUE,
};

#ifdef HALO_LINUX
/* (port) HALO_RENDER_PROFILE=1: where render_particles' time goes, every
300 frames - the pool walk and visibility test, the sort, the sprites built
(build_sprite) and their draws (build_sprites_end) - and how many */
#include <stdlib.h>
#include <string.h>
unsigned long long vita_host_time_us(void);
void platform_log(const char *format, ...);
static int particles_profile_enabled = -1;
static unsigned long long particles_profile_us[4];
static unsigned long particles_profile_counts[4], particles_profile_frames;
#define PARTICLES_PROFILE_NOW() (particles_profile_enabled > 0 ? vita_host_time_us() : 0)
#define PARTICLES_PROFILE_ADD(index, since) do { if (particles_profile_enabled > 0) \
	particles_profile_us[index] += vita_host_time_us() - (since); } while (0)
#endif

/* ---------- public code */

boolean local_player_is_first_person(
	short local_player_index)
{
	boolean result =
		director_get_perspective(local_player_index) ==
		_director_perspective_first_person;

	if (!result)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_get(player_index);

		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);

			if (unit->object.parent_object_index != NONE &&
				unit->unit.parent_seat_index != NONE)
			{
				struct unit_datum *parent_unit =
					unit_get(unit->object.parent_object_index);
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
					&unit_definition_get(parent_unit->definition_index)->unit.seats,
					unit->unit.parent_seat_index,
					struct unit_seat);

				if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
					result = TRUE;
			}
		}
	}

	return result;
}

int __cdecl compare_rendered_particles(
	void const *left_pointer,
	void const *right_pointer)
{
	struct rendered_particle_datum const *left = left_pointer;
	struct rendered_particle_datum const *right = right_pointer;
	int result = left->definition_index - right->definition_index;

	if (!result)
	{
		result = left->cluster_index - right->cluster_index;
		if (!result)
		{
			result =
				left->attached_to_first_person_weapon -
				right->attached_to_first_person_weapon;
		}
	}

	return result;
}

void render_particles(
	void)
{
	struct rendered_particle_datum rendered_particles[MAXIMUM_RENDERED_PARTICLES];
	short group_particle_counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short rendered_particle_count;
	struct build_sprite_data sprite_data;
	real_point3d position;
	real_vector3d direction;

#ifdef HALO_LINUX
	unsigned long long profile_from;

	if (particles_profile_enabled < 0)
	{
		const char *setting = getenv("HALO_RENDER_PROFILE");
		particles_profile_enabled = setting ? atoi(setting) : 0;
	}
	profile_from = PARTICLES_PROFILE_NOW();
#endif
	rendered_particle_count = 0;
	profile_enter(render_particles_section);

	if (render_particles_enabled)
	{
		short local_player_index = render.local_player_index;
		long particle_index;

		if (local_player_index == NONE ||
			!local_player_is_first_person(local_player_index))
		{
			local_player_index = MAXIMUM_LOCAL_PLAYERS;
		}

		for (particle_index = data_next_index(particle_data, NONE);
			particle_index != NONE;
			particle_index = data_next_index(particle_data, particle_index))
		{
			struct particle_datum *particle = particle_get(particle_index);
			boolean owned_by_local_player =
				particle->local_player_index == local_player_index;

			if (render_location_visible(&particle->location) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_first_person_bit) ||
					!owned_by_local_player) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_third_person_bit) ||
					owned_by_local_player))
			{
				struct rendered_particle_datum *rendered_particle =
					&rendered_particles[rendered_particle_count++];

				rendered_particle->particle_index = (short)particle_index;
				rendered_particle->definition_index =
					(short)particle->definition_index;
				rendered_particle->cluster_index =
					particle->location.cluster_index;
				rendered_particle->attached_to_first_person_weapon =
					owned_by_local_player &&
					TEST_FLAG(
						particle->flags,
						_particle_datum_dont_draw_third_person_bit);
			}
		}

#ifdef HALO_LINUX
		PARTICLES_PROFILE_ADD(0, profile_from);
		particles_profile_counts[0] += (unsigned long)rendered_particle_count;
		profile_from = PARTICLES_PROFILE_NOW();
#endif
		if (rendered_particle_count > 0)
		{
			short group_count = 0;

			qsort(
				rendered_particles,
				rendered_particle_count,
				sizeof(struct rendered_particle_datum),
				compare_rendered_particles);

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short *current_sprite_group;
				short previous_definition_index = NONE;
				short previous_cluster_index = NONE;
				boolean previous_attached_to_first_person_weapon = FALSE;

				while (rendered_particle_count-- > 0)
				{
					if (rendered_particle->definition_index ==
							previous_definition_index &&
						rendered_particle->cluster_index ==
							previous_cluster_index &&
						rendered_particle->attached_to_first_person_weapon ==
							previous_attached_to_first_person_weapon)
					{
						/* current_sprite_group is not initialised. Not reached unassigned: the first
						 * rendered particle cannot take this arm, because its definition index differs from
						 * the initial NONE (particle_new returns for NONE and otherwise calls
						 * particle_definition_get, whose tag lookup halts unless the low word of the index
						 * is non-negative; the field is written nowhere else). Source-policy approval
						 * pending (2026-09-27 audit). */
						(*current_sprite_group)++;
					}
					else
					{
						if (group_count >= MAXIMUM_RENDERED_PARTICLE_GROUPS)
							break;

						current_sprite_group =
							&group_particle_counts[group_count++];
						*current_sprite_group = 1;
						previous_definition_index =
							rendered_particle->definition_index;
						previous_cluster_index =
							rendered_particle->cluster_index;
						previous_attached_to_first_person_weapon =
							rendered_particle->attached_to_first_person_weapon;
					}

					rendered_particle++;
				}
			}
#ifdef HALO_LINUX
			PARTICLES_PROFILE_ADD(1, profile_from);
			particles_profile_counts[1] += (unsigned long)group_count;
#endif

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short group_index;

				for (group_index = 0; group_index < group_count; group_index++)
				{
					struct particle_definition *definition =
						particle_definition_get(
							rendered_particle->definition_index);
					short particle_count = group_particle_counts[group_index];
					real total_radius = 0.0f;
					short built_particle_count = 0;
#ifdef HALO_LINUX
					unsigned long long group_from = PARTICLES_PROFILE_NOW();
#endif

					build_sprites_begin(
						&sprite_data,
						particle_count,
						definition->bitmap.index,
						&definition->shader,
						rendered_particle->attached_to_first_person_weapon
							? FLAG(_build_sprites_first_person_bit)
							: 0);

					while (particle_count-- > 0)
					{
						struct particle_datum *particle =
							particle_get(rendered_particle->particle_index);
						real radius =
							particle_get_radius(rendered_particle->particle_index);

						if (particle->object_index != NONE)
						{
							real_matrix4x3 const *node_matrix;

							if (TEST_FLAG(
								particle->flags,
								_particle_datum_attached_to_local_player_bit))
							{
								node_matrix =
									first_person_weapon_get_node_matrix(
										particle->local_player_index,
										particle->node_index);
							}
							else if (object_try_and_get(particle->object_index))
							{
								node_matrix = object_get_node_matrix(
									particle->object_index,
									particle->node_index);
							}
							else
							{
								particle_delete(rendered_particle->particle_index);
								rendered_particle++;
								continue;
							}

							matrix4x3_transform_point(
								node_matrix,
								&particle->position,
								&position);
							matrix4x3_transform_normal(
								node_matrix,
								&particle->direction,
								&direction);
						}
						else
						{
							position = particle->position;
							direction = particle->direction;
							/* January explicitly reasserts the detached sentinel here. */
							particle->object_index = NONE;
						}

						{
							real diameter =
								render_frustum_sphere_diameter_in_pixels(
									&render.frustum,
									&position,
									radius);

							if (diameter > definition->lod_cutoff)
							{
								real fade;
								real scale;
								real remaining_life;
								unsigned long flags;
								word particle_flags;

								fade = 1.0f;
								built_particle_count++;
								scale =
									(radius + radius) *
									definition->runtime_oo_width;
								remaining_life =
									particle->lifespan - particle->age;
								total_radius += radius;

								if (diameter < definition->minimum_pixels)
								{
									scale =
										(definition->minimum_pixels / diameter) * scale;
								}

								if (definition->fade_in_time > 0.0f &&
									particle->age < definition->fade_in_time)
								{
									fade = particle->age / definition->fade_in_time;
								}

								if (definition->fade_out_time > 0.0f &&
									remaining_life < definition->fade_out_time)
								{
									fade *=
										remaining_life / definition->fade_out_time;
								}

								flags = 0;
								particle_flags = particle->flags;
								SET_FLAG(
									flags,
									_build_sprite_u_mirror_bit,
									TEST_FLAG(
										particle_flags,
										_particle_datum_u_mirror_bit));
								SET_FLAG(
									flags,
									_build_sprite_v_mirror_bit,
									TEST_FLAG(
										particle_flags,
										_particle_datum_v_mirror_bit));

#ifdef HALO_LINUX
								/* (port) the particle's frame is the tick's, read
								mid-change once: not drawn this frame (render_epoch.h) */
								if (particle->sequence_index >= 0 && particle->frame_index >= 0)
#endif
								build_sprite(
									&sprite_data,
									definition->sprite_orientation,
									particle->sequence_index,
									particle->frame_index,
									&position,
									&direction,
									particle->rotation,
									scale,
									&particle->color,
									fade,
									flags);

								particle->last_rendered_frame_index =
									render.frame_index;
							}
						}

						rendered_particle++;
					}

					/* BUG (original): a fully culled group divides zero by zero.
					 * A corrected non-matching build would store 0.0f when
					 * built_particle_count is zero. */
					((struct shader_effect_definition *)sprite_data.shader)->
						secondary_map_radius =
						total_radius / built_particle_count;
#ifdef HALO_LINUX
					PARTICLES_PROFILE_ADD(2, group_from);
					particles_profile_counts[2] += (unsigned long)built_particle_count;
					group_from = PARTICLES_PROFILE_NOW();
#endif
					build_sprites_end(&sprite_data);
#ifdef HALO_LINUX
					PARTICLES_PROFILE_ADD(3, group_from);
					particles_profile_counts[3] += (unsigned long)sprite_data.sprite_count;
#endif
				}
			}
		}
	}

	profile_exit(render_particles_section);
#ifdef HALO_LINUX
	if (particles_profile_enabled > 0 && ++particles_profile_frames % 300 == 0)
	{
		platform_log("particles-profile (ms/frame): walk %.2f sort %.2f build %.2f draw %.2f; per frame %.0f visible, "
			"%.1f groups, %.0f built, %.0f sprites drawn",
			particles_profile_us[0] / 300000.0, particles_profile_us[1] / 300000.0, particles_profile_us[2] / 300000.0,
			particles_profile_us[3] / 300000.0, particles_profile_counts[0] / 300.0, particles_profile_counts[1] / 300.0,
			particles_profile_counts[2] / 300.0, particles_profile_counts[3] / 300.0);
		memset(particles_profile_us, 0, sizeof(particles_profile_us));
		memset(particles_profile_counts, 0, sizeof(particles_profile_counts));
	}
#endif
	return;
}

/* ---------- private code */
