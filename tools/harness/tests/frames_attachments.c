#include "harness.h"

#include <stddef.h>

/* determinism_log.c's hash of the objects' memory pool, over objects laid out
as the game's (objects.h, the 32-bit ABI): an index of a light's, a looping
sound's, a contrail's or a particle system's attachment is the frames' (the
slot a new one takes follows the slots the frames freed) and is left out; an
effect's, and the rest of the object, are compared */
typedef unsigned short word;
typedef struct { real r, g, b; } real_rgb_color;
/* (the game's boolean is a byte) */
#define boolean byte
#include "config.inc"
#undef boolean
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* each object's definition: how many attachments it has (objects.c's
attachments_new makes that many) */
struct test_definition { struct { struct { long count; } attachments; } object; };
static struct test_definition definitions[2] = { { { { 2 } } }, { { { 4 } } } };
static struct test_definition *test_definition_get(long index)
{
	return &definitions[(index - 0xE4090295) & 1];
}
#define object_definition_get(index) test_definition_get(index)

#include "under_test.inc"

enum { OBJECT_BLOCK_SIZE = sizeof(struct memory_pool_block) + 0x274, OBJECTS = 2 };

static union
{
	struct memory_pool pool;
	byte bytes[sizeof(struct memory_pool) + OBJECTS * OBJECT_BLOCK_SIZE];
} pool;

static struct object_datum *object_at(int index)
{
	return (struct object_datum *)(pool.bytes + sizeof(struct memory_pool) + index * OBJECT_BLOCK_SIZE +
		sizeof(struct memory_pool_block));
}

/* a pool of two objects: a projectile with a contrail and an effect, the
other with a light */
static void pool_new(void)
{
	int index;

	memset(&pool, 0, sizeof(pool));
	pool.pool.signature = 0x6c6f6f70;
	pool.pool.size = (long)sizeof(pool);
	for (index = 0; index < OBJECTS; index++)
	{
		struct memory_pool_block *block = (struct memory_pool_block *)(pool.bytes + sizeof(struct memory_pool) +
			index * OBJECT_BLOCK_SIZE);
		struct object_datum *object = object_at(index);
		int attachment;

		block->header_signature = 0x68656164;
		block->size = OBJECT_BLOCK_SIZE;
		block->next_block = index + 1 < OBJECTS ?
			(struct memory_pool_block *)((byte *)block + OBJECT_BLOCK_SIZE) : NULL;
		if (!index)
			pool.pool.first_block = block;
		object->definition_index = 0xE4090295 + index;
		object->object.position.x = 51.5f;
		for (attachment = 0; attachment < MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT; attachment++)
		{
			object->object.attachment_types[attachment] = NONE;
			object->object.attachment_indices[attachment] = NONE;
		}
	}
	object_at(0)->object.attachment_types[0] = _object_attachment_type_contrail;
	object_at(0)->object.attachment_indices[0] = 0xF1000000;
	object_at(0)->object.attachment_types[1] = _object_attachment_type_effect;
	object_at(0)->object.attachment_indices[1] = 0xE6650004;
	object_at(1)->object.attachment_types[0] = _object_attachment_type_light;
	object_at(1)->object.attachment_indices[0] = 0xEC690010;
}

static unsigned long pool_hash(void)
{
	return determinism_hash_objects(&pool.pool, (long)sizeof(pool), 0x811C9DC5UL);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	unsigned long hash;

	pool_new();
	hash = pool_hash();
	CASE("frames")
	{
		/* the frames' slots: another contrail's, light's, looping sound's or
		particle system's index, as two runs whose frames freed other slots
		give it; and the collision tests' mark and the render state's index */
		object_at(0)->object.attachment_indices[0] = 0xF1000001;
		CHECK(pool_hash() == hash, "a contrail's index is hashed");
		object_at(1)->object.attachment_indices[0] = 0xEC690011;
		CHECK(pool_hash() == hash, "a light's index is hashed");
		object_at(1)->object.attachment_types[2] = _object_attachment_type_looping_sound;
		object_at(1)->object.attachment_indices[2] = 0xEF620001;
		hash = pool_hash();
		object_at(1)->object.attachment_indices[2] = 0xEF620002;
		CHECK(pool_hash() == hash, "a looping sound's index is hashed");
		object_at(1)->object.attachment_types[3] = _object_attachment_type_particle_system;
		object_at(1)->object.attachment_indices[3] = 0xF0610001;
		hash = pool_hash();
		object_at(1)->object.attachment_indices[3] = 0xF0610005;
		CHECK(pool_hash() == hash, "a particle system's index is hashed");
		object_at(0)->object.magic_number = 7;
		object_at(1)->object.cached_render_state_index = 9;
		CHECK(pool_hash() == hash, "the mark or the render state's index is hashed");
	}
	CASE("past-the-count")
	{
		/* a slot past the definition's attachments is as the object was
		made (zero: a light's type), and hashed as it was */
		object_at(0)->object.attachment_types[5] = _object_attachment_type_light;
		object_at(0)->object.attachment_indices[5] = 0;
		hash = pool_hash();
		object_at(0)->object.attachment_indices[5] = 1;
		CHECK(pool_hash() != hash, "a slot past the attachments is left out");
	}
	CASE("effect")
	{
		/* an effect is the simulation's: its index is compared */
		object_at(0)->object.attachment_indices[1] = 0xE6650005;
		CHECK(pool_hash() != hash, "an effect's index is left out");
	}
	CASE("simulation")
	{
		/* what follows the attachments, and the type of an attachment */
		object_at(0)->object.first_widget_index = 3;
		CHECK(pool_hash() != hash, "the object's first widget is left out");
		object_at(0)->object.first_widget_index = 0;
		object_at(1)->object.regions_destroyed_flags = 1;
		CHECK(pool_hash() != hash, "the object's regions are left out");
		object_at(1)->object.regions_destroyed_flags = 0;
		object_at(0)->object.attachment_types[0] = _object_attachment_type_effect;
		CHECK(pool_hash() != hash, "an attachment's type is left out");
		object_at(0)->object.attachment_types[0] = _object_attachment_type_contrail;
		object_at(1)->object.position.y = 1.0f;
		CHECK(pool_hash() != hash, "the object's position is left out");
	}
	CASE("offsets")
	{
		/* determinism_compare.py's offsets in a block are the object's */
		int type;
		int frames_types = 0;

		CHECK(offsetof(struct object_datum, object.magic_number) == TOOL_OBJECT_MAGIC_NUMBER, "magic number");
		CHECK(offsetof(struct object_datum, object.attachment_types) == TOOL_OBJECT_ATTACHMENT_TYPES,
			"attachment types at %#x", (unsigned)offsetof(struct object_datum, object.attachment_types));
		CHECK(offsetof(struct object_datum, object.attachment_indices) == TOOL_OBJECT_ATTACHMENT_INDICES,
			"attachment indices at %#x", (unsigned)offsetof(struct object_datum, object.attachment_indices));
		CHECK(offsetof(struct object_datum, object.cached_render_state_index) ==
			TOOL_OBJECT_CACHED_RENDER_STATE_INDEX, "cached render state index");
		CHECK(TOOL_MAXIMUM_ATTACHMENTS_PER_OBJECT == MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT, "attachments");
		for (type = 0; type < NUMBER_OF_OBJECT_ATTACHMENT_TYPES; type++)
		{
			int listed = 0;
			size_t index;

			for (index = 0; index < sizeof(tool_frame_attachment_types) / sizeof(*tool_frame_attachment_types); index++)
				listed |= tool_frame_attachment_types[index] == type;
			CHECK(listed == (int)determinism_frames_attachment((char)type), "the tool and the log differ on type %d",
				type);
			frames_types += listed;
		}
		CHECK(frames_types == 4, "%d types are the frames'", frames_types);
	}

	return 0;
}
