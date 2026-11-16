/* A map's enumeration draws each model with each of its effects once a
pass (port/linux/game/render_enumerate.c): the draws it leaves out are the
same as one it made, and it makes every draw the objects' tags ask for. */
#include "harness.h"

#define NUMBEROF(a) ((short)(sizeof(a) / sizeof((a)[0])))
#include "enums.inc"

#define OBJECT_DEFINITION_TAG 1
#define UNIT_DEFINITION_TAG 2
#define WEAPON_DEFINITION_TAG 3
#define STAND_IN_OBJECT_INDEX 0x7ffe0000
#define _object_mask_unit (FLAG(0) | FLAG(1))
#define _object_transparency_self_occludes_bit 4
#define INVALID_MODIFIER_TYPE 99
#define MOST 2200

struct shader { struct { short type; } base; };
struct render_model_effect
{
	short type;
	long source_object_index;
	real_point3d source_object_centroid;
	struct shader *modifier_shader;
	real intensity, parameter;
};
struct reference { long index; };
struct object_definition
{
	struct
	{
		short type;
		unsigned long flags;
		struct reference model, modifier_shader;
		short forced_shader_permutation_index;
	} object;
};
struct weapon_definition { struct { struct { struct reference first_person_model; } interface_definition; } weapon; };
struct game_globals_first_person_interface { struct reference hands; };
struct block { long count; void *elements; };
struct game_globals { struct block first_person_interface; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->elements)[index])

static struct object_definition objects[MOST];
static int object_count;
static struct weapon_definition weapons[16];
static int weapon_count;
static struct game_globals_first_person_interface interfaces[4];
static struct game_globals globals;
static struct shader shaders[16];
static struct { struct { real_point3d position; } camera; } render;
static const int model_states[5];

struct tag_iterator { long group; int next; };
static void tag_iterator_new(struct tag_iterator *iterator, long group) { iterator->group = group; iterator->next = 0; }
static long tag_iterator_next(struct tag_iterator *iterator)
{
	int count = iterator->group == WEAPON_DEFINITION_TAG ? weapon_count : object_count;

	while (iterator->next < count)
	{
		int index = iterator->next++;

		if (iterator->group != UNIT_DEFINITION_TAG || TEST_FLAG(_object_mask_unit, FLAG(objects[index].object.type)))
			return index;
	}
	return NONE;
}
static struct object_definition *object_definition_get(long index) { CHECK(index >= 0 && index < object_count, "object %ld", index); return &objects[index]; }
static struct shader *shader_definition_get(long index) { CHECK(index >= 0 && index < 16, "shader %ld", index); return &shaders[index]; }
static struct weapon_definition *weapon_definition_get(long index) { return &weapons[index]; }
static struct game_globals *scenario_get_game_globals(void) { return &globals; }
static boolean shader_type_is_valid_for_modifier(short type) { return type != INVALID_MODIFIER_TYPE; }

/* each draw: its model, state and effect, as what tells draws apart */
struct draw { long model; short state, type, permutation; long modifier; int intensity, parameter; unsigned long flags; };
static struct draw drawn[200000], expected[200000];
static int drawn_count, expected_count;

static struct draw draw_make(long model_index, short state, struct render_model_effect const *effect, short permutation,
	unsigned long flags)
{
	struct draw draw;

	memset(&draw, 0, sizeof(draw));
	draw.model = model_index;
	draw.state = state;
	draw.permutation = permutation;
	draw.flags = flags;
	draw.modifier = NONE;
	if (effect)
	{
		CHECK(effect->source_object_index == STAND_IN_OBJECT_INDEX, "the stand-in object");
		draw.type = effect->type;
		draw.modifier = effect->modifier_shader ? (long)(effect->modifier_shader - shaders) : NONE;
		draw.intensity = (int)(effect->intensity * 2.0f);
		draw.parameter = (int)(effect->parameter * 2.0f);
	}
	return draw;
}

static void model_draw(long model_index, short state, struct render_model_effect const *effect, short permutation,
	unsigned long flags)
{
	CHECK(drawn_count < 200000, "draws");
	drawn[drawn_count++] = draw_make(model_index, state, effect, permutation, flags);
}

/* (whether the pass has the renderer's lights: the unlit effects are drawn
in the passes without them alone) */
static boolean dynamic_lights;
static boolean pass_dynamic_lights(void) { return dynamic_lights; }

#include "under_test.inc"

static void expect(long model_index, short state, struct render_model_effect const *effect, short permutation,
	unsigned long flags)
{
	expected[expected_count++] = draw_make(model_index, state, effect, permutation, flags);
}

/* a model camouflaged: fading and fully cloaked (intensity 0.5 and 1), each
with the ordinary and the hyper-stealth parameters */
static void camouflage_expected(long model_index, short state, unsigned long flags)
{
	static real const intensities[] = { 0.5f, 1.0f };
	struct render_model_effect effect;
	int intensity, parameter;

	memset(&effect, 0, sizeof(effect));
	effect.type = _render_model_effect_type_active_camouflage;
	effect.source_object_index = STAND_IN_OBJECT_INDEX;
	for (intensity = 0; intensity < 2; intensity++)
	{
		for (parameter = 0; parameter < 2; parameter++)
		{
			effect.intensity = intensities[intensity];
			effect.parameter = (real)parameter;
			expect(model_index, state, &effect, 0, flags);
		}
	}
}

/* what the objects' tags ask for: each object's model with each of its
effects in each state (as before the draws were told apart) */
static void objects_expected(void)
{
	struct render_model_effect effect;
	int index;
	short state;

	for (index = 0; index < object_count; index++)
	{
		struct object_definition *definition = &objects[index];

		if (definition->object.model.index == NONE)
			continue;
		for (state = 0; state < 5; state++)
		{
			memset(&effect, 0, sizeof(effect));
			effect.source_object_index = STAND_IN_OBJECT_INDEX;
			if (definition->object.modifier_shader.index != NONE &&
				shaders[definition->object.modifier_shader.index].base.type != INVALID_MODIFIER_TYPE)
			{
				effect.modifier_shader = &shaders[definition->object.modifier_shader.index];
				expect(definition->object.model.index, state, &effect, 0, 0);
				effect.modifier_shader = NULL;
			}
			if (TEST_FLAG(_object_mask_unit, FLAG(definition->object.type)))
			{
				camouflage_expected(definition->object.model.index, state, 0);
			}
			if (TEST_FLAG(definition->object.flags, _object_transparency_self_occludes_bit))
			{
				effect.type = _render_model_effect_type_modifier;
				expect(definition->object.model.index, state, &effect, 0, 0);
				effect.type = _render_model_effect_type_none;
			}
			if (definition->object.forced_shader_permutation_index > 0)
				expect(definition->object.model.index, state, NULL, definition->object.forced_shader_permutation_index, 0);
		}
	}
}

static void first_person_expected(long model_index)
{
	unsigned long flags = FLAG(_render_model_first_person_bit);
	struct render_model_effect effect;
	int index;
	short state;

	if (model_index == NONE)
		return;
	for (state = 0; state < 5; state++)
	{
		expect(model_index, state, NULL, 0, flags);
		camouflage_expected(model_index, state, flags);
		memset(&effect, 0, sizeof(effect));
		effect.source_object_index = STAND_IN_OBJECT_INDEX;
		for (index = 0; index < object_count; index++)
		{
			long modifier = objects[index].object.modifier_shader.index;

			if (TEST_FLAG(_object_mask_unit, FLAG(objects[index].object.type)) && modifier != NONE &&
				shaders[modifier].base.type != INVALID_MODIFIER_TYPE)
			{
				effect.modifier_shader = &shaders[modifier];
				expect(model_index, state, &effect, 0, flags);
			}
		}
	}
}

static int draw_equal(struct draw const *a, struct draw const *b)
{
	return a->model == b->model && a->state == b->state && a->type == b->type && a->permutation == b->permutation &&
		a->modifier == b->modifier && a->intensity == b->intensity && a->parameter == b->parameter && a->flags == b->flags;
}

static int found(struct draw const *list, int count, struct draw const *draw)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (draw_equal(&list[index], draw))
			return TRUE;
	}
	return FALSE;
}

/* every draw asked for made, none twice, none not asked for */
static void check_drawn(char const *what)
{
	int index, other;

	for (index = 0; index < expected_count; index++)
		CHECK(found(drawn, drawn_count, &expected[index]), "%s: draw %d (model %ld, state %d, type %d, modifier %ld, "
			"permutation %d) not made", what, index, expected[index].model, expected[index].state, expected[index].type,
			expected[index].modifier, expected[index].permutation);
	for (index = 0; index < drawn_count; index++)
	{
		CHECK(found(expected, expected_count, &drawn[index]), "%s: draw %d not asked for", what, index);
		for (other = 0; other < index; other++)
			CHECK(!draw_equal(&drawn[index], &drawn[other]), "%s: draw %d made again (as %d)", what, index, other);
	}
}

static void object_add(short type, long model, long modifier, unsigned long flags, short permutation)
{
	objects[object_count].object.type = type;
	objects[object_count].object.model.index = model;
	objects[object_count].object.modifier_shader.index = modifier;
	objects[object_count].object.flags = flags;
	objects[object_count].object.forced_shader_permutation_index = permutation;
	object_count++;
}

static void world(void)
{
	memset(shaders, 0, sizeof(shaders));
	shaders[9].base.type = INVALID_MODIFIER_TYPE;
	object_count = weapon_count = 0;
	/* (bipeds of one model and shield, one of another shield) */
	object_add(0, 1, 5, 0, 0);
	object_add(0, 1, 5, 0, 0);
	object_add(0, 1, 6, 0, 0);
	object_add(1, 2, NONE, 0, 0);
	/* (scenery: self-occluding, a forced permutation, two forced) */
	object_add(5, 3, NONE, FLAG(_object_transparency_self_occludes_bit), 0);
	object_add(5, 3, NONE, FLAG(_object_transparency_self_occludes_bit), 0);
	object_add(5, 3, NONE, 0, 3);
	object_add(5, 3, NONE, 0, 4);
	object_add(5, 3, NONE, 0, 3);
	object_add(5, NONE, 5, 0, 2);
	object_add(5, 4, 9, 0, 0);
	object_add(5, 4, 5, 0, 0);
	drawn_count = expected_count = 0;
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	world();
	if (!strcmp(name, "objects"))
	{
		object_effects_draw();
		objects_expected();
		CHECK(drawn_count < expected_count, "the shared models and effects drawn once (%d of %d draws)", drawn_count,
			expected_count);
		check_drawn("objects");
	}
	else if (!strcmp(name, "passes"))
	{
		object_effects_draw();
		drawn_count = 0;
		object_effects_draw();
		objects_expected();
		check_drawn("the second pass");
	}
	else if (!strcmp(name, "first-person"))
	{
		interfaces[0].hands.index = 7;
		interfaces[1].hands.index = 7;
		globals.first_person_interface.count = 2;
		globals.first_person_interface.elements = interfaces;
		weapons[0].weapon.interface_definition.first_person_model.index = 8;
		weapons[1].weapon.interface_definition.first_person_model.index = 8;
		weapons[2].weapon.interface_definition.first_person_model.index = 7;
		weapons[3].weapon.interface_definition.first_person_model.index = NONE;
		weapons[4].weapon.interface_definition.first_person_model.index = 11;
		weapon_count = 5;
		first_person_models_draw();
		first_person_expected(7);
		first_person_expected(8);
		first_person_expected(11);
		check_drawn("first person");
		/* (and again in the next pass) */
		drawn_count = 0;
		first_person_models_draw();
		check_drawn("first person's second pass");
	}
	else if (!strcmp(name, "lit"))
	{
		int index;
		int unlit_count;

		/* (a pass with the renderer's lights: no camouflage or self-occluding
		draw, which take none of them, and so fewer draws than without) */
		object_effects_draw();
		unlit_count = drawn_count;
		world();
		dynamic_lights = TRUE;
		object_effects_draw();
		for (index = 0; index < drawn_count; index++)
			CHECK(drawn[index].type != _render_model_effect_type_active_camouflage, "no camouflage in a lit pass");
		CHECK(drawn_count < unlit_count, "fewer draws in a lit pass (%d of %d)", drawn_count, unlit_count);
	}
	else if (!strcmp(name, "full"))
	{
		int index;

		/* (more models and effects than are kept: the rest drawn as new) */
		object_count = 0;
		for (index = 0; index < MOST; index++)
			object_add(0, 100 + index, NONE, 0, 0);
		object_effects_draw();
		objects_expected();
		check_drawn("past the table's room");
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
