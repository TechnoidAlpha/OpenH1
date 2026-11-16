"""A hit on the overshield powerup (port/linux/game/powerup_hit_effects.c): the maps' tags make its force field dirt,
so a shot at it threw up dirt; it plays the projectile's engineer force field effect, the active camouflage's,
when the projectile responds to both alike and the effect only shows and sounds. Every other hit keeps its own."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["overshield", "camouflage", "not_equipment", "other_material", "different_response", "potential", "none",
         "stateful", "few_responses"]
CONTROLS = [
    # (any material of the overshield's: its glass too)
    ("if (material_type != _material_dirt || ", "if (", "other_material"),
    # (any powerup's)
    ("equipment_definition->equipment.powerup_type != _equipment_powerup_overshield", "FALSE", "camouflage"),
    # (the force field's effect whatever the projectile does)
    ("if (force_field->default_response != response)\n\t\t\treturn effect_index;", "", "different_response"),
    # (always the default's effect)
    ("force_field_effect_index = force_field->potential_effect.index;",
     "force_field_effect_index = force_field->default_effect.index;", "potential"),
    # (an effect that changes the game played in its place)
    ("!overshield_effect_hit_effect_allowed(force_field_effect_index)", "FALSE", "stateful"),
    # (none taken as not allowed: the dirt's)
    ("if (force_field_effect_index != NONE && ", "if (", "none"),
]


def generated(fault=None):
    materials = enum_with(read("source/game/game_globals.h"), "_material_dirt")
    materials = materials.replace("GAME_GLOBALS_TAG = 'matg',", "")
    config = "\n".join([
        materials,
        enum_with(read("source/items/equipment_definitions.h"), "_equipment_powerup_overshield"),
    ]) + "\n"
    definitions = read("source/items/projectile_definitions.h")
    start = definitions.index("struct projectile_material_response_definition\n{")
    structures = definitions[start:definitions.index("};", start) + 2] + "\n"
    code = function(read("port/linux/game/powerup_hit_effects.c"), "powerup_hit_effect")
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("structures.inc", structures), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("powerup_hit_effects", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS, ids=[control[2] for control in CONTROLS])
def test_negative_control(before, after, case):
    status, output = run(build("powerup_hit_effects", generated((before, after))), case)
    assert status == CHECK_FAILED, output
