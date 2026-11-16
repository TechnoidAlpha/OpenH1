/*
POWERUP_HIT_EFFECTS.C

A hit on the overshield powerup, as it is drawn and heard. In every map that
has it, the overshield's collision model has one material, "force field",
whose type was left as the first, dirt, so a shot at the powerup threw up
dirt; the active camouflage's model has the same material, "force field", of
the type engineer force field, so a shot at it sparks as the plasma shields
do. A hit on the overshield powerup on a material of the type dirt plays
what the projectile plays on an engineer force field (the camouflage's),
when that effect only shows and sounds and the projectile responds to both
alike (needles stick in dirt, but glance off a force field: such a hit keeps
its own). How the projectile responds stays the hit's own, so nothing of
the game, and nothing the machines send each other, changes.
*/

#include "cseries.h"
#include "game/game_globals.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "items/projectile_definitions.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"

/* port/linux/game/overshield_effect.c */
boolean overshield_effect_hit_effect_allowed(long effect_index);

/* ---------- public code */

/* the effect a projectile's hit on an object plays: the one it would
(effect_index, from response's material response, the hit material's),
or, on the overshield powerup's dirt, its engineer force field's */
long powerup_hit_effect(
	long object_index,
	short material_type,
	struct projectile_definition const *definition,
	struct projectile_material_response_definition const *material_response,
	short response,
	long effect_index)
{
	struct item_datum *equipment;
	struct equipment_definition const *equipment_definition;
	struct projectile_material_response_definition const *force_field;
	boolean potential;
	long force_field_effect_index;

	if (material_type != _material_dirt || !definition || !material_response ||
		!VALID_INDEX(_material_engineer_force_field, definition->projectile.material_responses.count))
	{
		return effect_index;
	}
	equipment = equipment_try_and_get(object_index);
	if (!equipment)
		return effect_index;
	equipment_definition = equipment_definition_get(equipment->definition_index);
	if (!equipment_definition || equipment_definition->equipment.powerup_type != _equipment_powerup_overshield)
		return effect_index;

	/* (the hit's response was its potential one, or its default) */
	potential = material_response->potential_response &&
		!(response == material_response->default_response && effect_index == material_response->default_effect.index);
	force_field = TAG_BLOCK_GET_ELEMENT(
		&definition->projectile.material_responses,
		_material_engineer_force_field,
		struct projectile_material_response_definition);
	if (potential)
	{
		if (force_field->potential_response != response)
			return effect_index;
		force_field_effect_index = force_field->potential_effect.index;
	}
	else
	{
		if (force_field->default_response != response)
			return effect_index;
		force_field_effect_index = force_field->default_effect.index;
	}
	/* (none: the camouflage plays none either) */
	if (force_field_effect_index != NONE && !overshield_effect_hit_effect_allowed(force_field_effect_index))
		return effect_index;

	return force_field_effect_index;
}
