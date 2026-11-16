#include "harness.h"

/* powerup_hit_effect (port/linux/game/powerup_hit_effects.c): a hit on the
overshield powerup's dirt plays the projectile's engineer force field effect
(the active camouflage's), when the projectile responds to both alike and
the effect only shows and sounds; any other hit plays its own */
#include "config.inc"

#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

typedef unsigned short word;
struct tag_reference { unsigned long group_tag; char *name; long name_length; long index; };
struct tag_block { long count; void *address; void *definition; };

#include "structures.inc"

struct projectile_definition
{
	struct { struct tag_block material_responses; } projectile;
};

struct item_datum { long definition_index; };
struct equipment_definition { struct { short powerup_type; } equipment; };

enum { _response_disappear, _response_detonate, _response_reflect, _response_overpenetrate, _response_attach };
enum { OVERSHIELD = 10, CAMOUFLAGE = 11, NOT_EQUIPMENT = 12 };
enum { DIRT_EFFECT = 100, FORCE_FIELD_EFFECT = 101, DIRT_POTENTIAL_EFFECT = 102, FORCE_FIELD_POTENTIAL_EFFECT = 103,
	STATEFUL_EFFECT = 104 };

static struct item_datum overshield_item = { OVERSHIELD };
static struct item_datum camouflage_item = { CAMOUFLAGE };
static struct equipment_definition overshield_definition = { { _equipment_powerup_overshield } };
static struct equipment_definition camouflage_definition = { { _equipment_powerup_active_camouflage } };

static struct item_datum *equipment_try_and_get(long object_index)
{
	return object_index == OVERSHIELD ? &overshield_item : object_index == CAMOUFLAGE ? &camouflage_item : NULL;
}

static struct equipment_definition *equipment_definition_get(long index)
{
	return index == OVERSHIELD ? &overshield_definition : &camouflage_definition;
}

/* (as overshield_effect.c's: none, or an effect that would change the game, one
that is not only shown and heard, refused) */
static boolean overshield_effect_hit_effect_allowed(long effect_index)
{
	return effect_index != NONE && effect_index != STATEFUL_EFFECT;
}

#include "under_test.inc"

static struct projectile_material_response_definition responses[40];
static struct projectile_definition projectile;

static void responses_new(short force_field_response, long force_field_effect)
{
	short index;

	memset(responses, 0, sizeof(responses));
	for (index = 0; index < 40; index++)
	{
		responses[index].default_effect.index = NONE;
		responses[index].potential_effect.index = NONE;
	}
	responses[_material_dirt].default_response = _response_disappear;
	responses[_material_dirt].default_effect.index = DIRT_EFFECT;
	responses[_material_engineer_force_field].default_response = force_field_response;
	responses[_material_engineer_force_field].default_effect.index = force_field_effect;
	projectile.projectile.material_responses.count = 33;
	projectile.projectile.material_responses.address = responses;
}

static long hit(long object_index, short material_type, short response, long effect_index)
{
	return powerup_hit_effect(object_index, material_type, &projectile, &responses[material_type], response,
		effect_index);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	responses_new(_response_disappear, FORCE_FIELD_EFFECT);
	CASE("overshield")
	{
		CHECK(hit(OVERSHIELD, _material_dirt, _response_disappear, DIRT_EFFECT) == FORCE_FIELD_EFFECT,
			"a hit on the overshield's dirt plays the force field's effect");
	}
	CASE("camouflage")
	{
		CHECK(hit(CAMOUFLAGE, _material_dirt, _response_disappear, DIRT_EFFECT) == DIRT_EFFECT,
			"another powerup keeps its own");
	}
	CASE("not_equipment")
	{
		CHECK(hit(NOT_EQUIPMENT, _material_dirt, _response_disappear, DIRT_EFFECT) == DIRT_EFFECT,
			"what is not equipment keeps its own");
	}
	CASE("other_material")
	{
		/* (a map whose overshield has another material: its own) */
		responses[_material_glass].default_effect.index = 77;
		CHECK(hit(OVERSHIELD, _material_glass, _response_disappear, 77) == 77, "another material keeps its own");
	}
	CASE("different_response")
	{
		/* (needles stick in dirt and glance off a force field: the sticking's own) */
		responses_new(_response_reflect, FORCE_FIELD_EFFECT);
		responses[_material_dirt].default_response = _response_attach;
		CHECK(hit(OVERSHIELD, _material_dirt, _response_attach, DIRT_EFFECT) == DIRT_EFFECT,
			"a response unlike the force field's keeps its own");
	}
	CASE("potential")
	{
		/* (the charged bolt: no default effect, a potential detonation) */
		responses[_material_dirt].potential_response = _response_detonate;
		responses[_material_dirt].potential_effect.index = DIRT_POTENTIAL_EFFECT;
		responses[_material_engineer_force_field].potential_response = _response_detonate;
		responses[_material_engineer_force_field].potential_effect.index = FORCE_FIELD_POTENTIAL_EFFECT;
		CHECK(hit(OVERSHIELD, _material_dirt, _response_detonate, DIRT_POTENTIAL_EFFECT) ==
			FORCE_FIELD_POTENTIAL_EFFECT, "a potential response plays the force field's potential effect");
		CHECK(hit(OVERSHIELD, _material_dirt, _response_disappear, DIRT_EFFECT) == FORCE_FIELD_EFFECT,
			"its default response still plays the default");
	}
	CASE("none")
	{
		/* (a rocket: nothing on the camouflage, nothing on the overshield) */
		responses_new(_response_disappear, NONE);
		CHECK(hit(OVERSHIELD, _material_dirt, _response_disappear, DIRT_EFFECT) == NONE,
			"no force field effect: none");
	}
	CASE("stateful")
	{
		responses_new(_response_disappear, STATEFUL_EFFECT);
		CHECK(hit(OVERSHIELD, _material_dirt, _response_disappear, DIRT_EFFECT) == DIRT_EFFECT,
			"an effect that changes the game is not played in its place");
	}
	CASE("few_responses")
	{
		/* (a projectile with no engineer force field's response) */
		projectile.projectile.material_responses.count = _material_engineer_force_field;
		CHECK(hit(OVERSHIELD, _material_dirt, _response_disappear, DIRT_EFFECT) == DIRT_EFFECT,
			"no force field response: its own");
	}
	return 0;
}
