/*
WEAPON_GAUGE.C

The plasma pistol's gauge, the ring of lights on the back of the gun (its
"pp gauge" meter shader, in its first-person and third-person models).

BUG (original): the gauge's value is the weapon's charge (its D out, the
primary trigger's charged fraction scaled by one), not its heat, so it fills
only while the trigger is held to overcharge, and goes back to empty at
every shot. Tapped quickly, the pistol heats until it overheats and vents
while the gauge shows a tick or two of charge (two ticks of eighteen) between
shots and is otherwise empty. The plasma rifle's gauge reads the rifle's
heat (its B out) and fills as its HUD bar does.

A weapon's meter whose value is a function of a charged export (primary or
secondary charged) shows the weapon's heat when that is more: tapped, the
ring fills with the heat, in step with the HUD's heat bar, and is full at
the overheat; held, it fills with the charge as before. In the stock maps
that is the plasma pistol's gauge alone (no other weapon's meter reads its
charge).

Only the drawing changes: the weapon's function values, its heat and
everything the game simulates are as they were, and nothing is sent between
machines.
*/

#include "cseries.h"
#include "real_math.h"
#include "items/weapon_definitions.h"
#include "items/weapon_export_function_mode.h"
#include "items/weapons.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"

/* ---------- private code */

/* the incoming function that an outgoing function's index (1-4) reads
is one of the weapon's charged exports */
static boolean weapon_gauge_reads_charge(
	struct weapon_definition const *definition,
	short function_index)
{
	short mode;

	if (function_index<1 || function_index>NUMBER_OF_INCOMING_OBJECT_FUNCTIONS)
		return FALSE;
	mode= definition->weapon.function_modes[function_index-1];
	return mode==_weapon_function_primary_charged || mode==_weapon_function_secondary_charged;
}

/* ---------- public code */

/* a meter's value (rasterizer_xbox_transparent_geometry.c), from its value
source (1-4: the object's A to D out), on the object it is drawn on */
real weapon_gauge_meter_value(
	long object_index,
	short value_source,
	real meter_value)
{
	struct weapon_datum *weapon;
	struct weapon_definition *definition;
	struct object_function_definition const *function;

	/* (a model drawn for no object has an identifier of its own, 0 or 1:
	the sky's, a flag's, a hill's) */
	if (object_index==NONE || !(object_index>>16) ||
		value_source<1 || value_source>NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS)
	{
		return meter_value;
	}
	weapon= (struct weapon_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_weapon);
	if (!weapon)
		return meter_value;
	definition= weapon_definition_get(weapon->definition_index);
	if (value_source>definition->object.functions.count)
		return meter_value;
	function= TAG_BLOCK_GET_ELEMENT(&definition->object.functions, value_source-1, struct object_function_definition);
	if (!weapon_gauge_reads_charge(definition, function->scale_function_by_function_index) &&
		!weapon_gauge_reads_charge(definition, function->scale_result_by_function_index) &&
		!weapon_gauge_reads_charge(definition, function->add_function_index))
	{
		return meter_value;
	}

	return MAX(meter_value, PIN(weapon->weapon.heat, 0.0f, 1.0f));
}
