#include "harness.h"

/* particles_update (effects/particles.c): a particle's first update covers at
least the Xbox's frame, so a particle whose sequence runs out within that frame
(a single sprite at 30 a second, the plasma grenade's bolts of lightning) dies
before it is drawn, as on the Xbox, however short the frames between the ticks
are; a longer one comes to the screen as old as on the Xbox */
typedef unsigned short word;
typedef struct { real alpha, red, green, blue; } real_argb_color;
struct location { long leaf_index; short cluster_index; };
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(n, lo, hi) ((n) < (lo) ? (lo) : (n) > (hi) ? (hi) : (n))
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define FLT_MAX 3.402823466e+38F
#define match_vassert(file, line, condition, message) ((void)0)
#include "config.inc"

struct tag_block { long count; void *address; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])
struct tag_reference { unsigned long group_tag; long index; };
struct bitmap_group_sequence { struct tag_block sprites; };
struct bitmap_group { struct tag_block sequences; };
struct particle_definition
{
	unsigned long flags;
	struct tag_reference bitmap;
	struct tag_reference effect;
	short first_sequence_index;
	short initial_sequence_count;
	short looping_sequence_count;
	short final_sequence_count;
};

static struct data_array *particle_data;
static struct particle_definition the_definition;
static struct bitmap_group_sequence sequences[4];
static struct bitmap_group the_bitmap = { { 4, sequences } };
static struct { long frame_index; } render;
static int physics_updates;
static long particles_drawn;

#define particle_get(index) ((struct particle_datum *)datum_get(particle_data, (index)))
#define particle_definition_get(index) (&the_definition)
#define bitmap_group_get(index) (&the_bitmap)
#define profile_enter(section) ((void)0)
#define profile_exit(section) ((void)0)
#define render_interpolation_particles_due() ((void)0)
#define local_random_range(lower, upper) (lower)
#define particle_effect_new(particle, group_tag, index, scale) ((void)0)

static long data_next_index(struct data_array *array, long index)
{
	for (index = index == NONE ? 0 : DATUM_INDEX_TO_ABSOLUTE_INDEX(index) + 1; index < array->maximum_count; index++)
	{
		struct datum_header *header = (struct datum_header *)(array->data + index * array->size);

		if (header->identifier)
			return ((long)(unsigned short)header->identifier << 16) | index;
	}
	return NONE;
}

static boolean particle_update_physics(long particle_index, real dt)
{
	physics_updates++;
	return TRUE;
}

static real particles_leftover_ticks;
static long particles_update_ticks;
static long particles_tick_frame_indices[MAXIMUM_PARTICLE_UNRENDERED_FRAME_COUNT + 1];
static long particles_tick_frame_head;

#include "under_test.inc"

/* a particle as particle_new leaves it: its first sequence picked, the frame
before the first */
static long particle_made(real frames_per_second)
{
	long index = datum_new(particle_data);
	struct particle_datum *particle = particle_get(index);

	particle->definition_index = 0;
	particle->lifespan = 1.0f;
	particle->frame_span = 1.0f / frames_per_second;
	particle->frame_time = -1.0f;
	particle->frame_index = NONE;
	particle->last_rendered_frame_index = render.frame_index;
	particle->state = _particle_state_next_sequence_initial;
	particle_next_sequence(index);
	return index;
}

/* the frames drawn until the particle is gone, a frame each of dt */
static int frames_drawn(long index, real dt, int frames)
{
	int drawn = 0;

	while (frames-- > 0)
	{
		render.frame_index++;
		particles_update(dt, 1.0f / TICKS_PER_SECOND);
		if (!((struct datum_header *)datum_get(particle_data, index))->identifier)
			break;
		particle_get(index)->last_rendered_frame_index = render.frame_index;
		drawn++;
	}
	return drawn;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	int sequence;

	particle_data = game_state_data_new("particle", 8, sizeof(struct particle_datum));
	the_definition.bitmap.index = 0;
	the_definition.effect.index = NONE;
	the_definition.initial_sequence_count = 1;
	for (sequence = 0; sequence < 4; sequence++)
		sequences[sequence].sprites.count = 1;

	CASE("single_sprite_unseen")
	{
		/* the bolts: one sprite at 30 a second, dead before a frame of 60,
		144 or 30 a second draws it */
		static real const rates[] = { 60.0f, 144.0f, 30.0f };
		int rate;

		for (rate = 0; rate < (int)NUMBEROF(rates); rate++)
		{
			long index = particle_made(30.0f);
			int drawn = frames_drawn(index, 1.0f / rates[rate], 20);

			CHECK(drawn == 0, "drawn for %d frames at %g a second", drawn, rates[rate]);
		}
	}
	CASE("longer_sequence_seen")
	{
		/* four sprites at 30 a second: drawn from the frame after the one that
		made it, for the rest of its 4 30ths of a second, at 60 a second as at
		30 */
		long index;
		int drawn;

		sequences[0].sprites.count = 4;
		index = particle_made(30.0f);
		drawn = frames_drawn(index, 1.0f / 60.0f, 20);
		CHECK(drawn == 6, "drawn for %d frames of 60 a second", drawn);
		index = particle_made(30.0f);
		drawn = frames_drawn(index, 1.0f / 30.0f, 20);
		CHECK(drawn == 3, "drawn for %d frames of 30 a second", drawn);
	}
	CASE("first_update_a_frame_old")
	{
		/* a particle's first update ages it by the Xbox's frame, and each
		after it by the frame's own time; a paused frame ages nothing */
		long index;

		sequences[0].sprites.count = 100;
		index = particle_made(30.0f);
		particles_update(0.0f, 1.0f / TICKS_PER_SECOND);
		CHECK(particle_get(index)->age == 0.0f, "a paused frame aged it %g", particle_get(index)->age);
		particles_update(1.0f / 120.0f, 1.0f / TICKS_PER_SECOND);
		CHECK(particle_get(index)->age == 1.0f / TICKS_PER_SECOND, "its first update aged it %g",
			particle_get(index)->age);
		particles_update(1.0f / 120.0f, 1.0f / TICKS_PER_SECOND);
		CHECK(particle_get(index)->age == 1.0f / TICKS_PER_SECOND + 1.0f / 120.0f, "its second update aged it %g",
			particle_get(index)->age);
		CHECK(physics_updates == 3, "%d physics updates", physics_updates);
	}
	return 0;
}
