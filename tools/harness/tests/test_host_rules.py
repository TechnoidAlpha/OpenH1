"""The gametype's rules a player host changes during a game (port/linux/game/network_host_rules.c, Host Controls'
RULES, and source/game/game_engine.c's game_engine_vehicle_set_changed): a vehicle set changed in a game under way
swaps the vehicles nobody rides for the new set's at the map's placements, keeps a ridden one until it is empty and
then places the vehicle its pad waited for; a changed loadout is the next spawn's and not a living player's; the
flags every machine draws by change at once only where every machine hears the host's message; the message is
taken whole, and its flags alone."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["vehicles", "loadout", "sets", "live", "message"]

RULES_FUNCTIONS = ("host_rules_flag", "network_host_rules_vehicle_step", "network_host_rules_set_vehicles",
                   "network_host_rules_change",
                   "network_host_rules_client_side", "network_host_rules_live_for", "network_host_rules_client_flags",
                   "network_host_rules_message_valid")
ENGINE_FUNCTIONS = ("game_engine_object_being_deleted", "game_engine_vehicle_empty", "game_engine_vehicle_pad_clear", "game_engine_vehicle_home_place",
                    "game_engine_vehicle_set_changed", "game_engine_update_vehicle_set_change",
                    "game_engine_loadout_weapon_definition", "game_engine_give_loadout")

# a fault in the code, the file it is in ("rules" or "engine"), and the case that must catch it
NEGATIVE_CONTROLS = {
    "ridden-deleted": ("engine", ("if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || "
                                  "game_engine_vehicle_empty(vehicle))\n\t\t{", "if (1)\n\t\t{"), "vehicles"),
    "empty-kept": ("engine", ("if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || "
                              "game_engine_vehicle_empty(vehicle))\n\t\t{",
                              "if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))\n\t\t{"), "vehicles"),
    "new-not-placed": ("engine", ("object_index = object_new_from_scenario(placement, palette);",
                                  "object_index = NONE;"), "vehicles"),
    "pad-ignored": ("engine", ("if (game_engine_vehicle_pad_clear(&placement->position))", "if (1)"), "vehicles"),
    "not-automatic-placed": ("engine", ("TEST_FLAG(placement->placement_flags, "
                                        "_scenario_object_placement_not_automatic_bit) ||", ""), "vehicles"),
    "other-set-placed": ("engine", ("!game_engine_vehicle_placement_allowed(placement, palette))", "0)"), "vehicles"),
    "leaving-kept": ("engine", ("\t\tif (vehicle)\n\t\t\tobject_delete(game_engine_vehicles_leaving[index]);\n", ""),
                     "vehicles"),
    "waiting-never-placed": ("engine", ("\t\t\tgame_engine_vehicle_home_place(index);\n", ""), "vehicles"),
    "deleted-hold-pads": ("engine", ("\t\t\t!game_engine_object_being_deleted(iterator.index) &&\n", ""), "vehicles"),
    "deleted-counted-again": ("engine", ("\t\tif (!game_engine_object_being_deleted(iterator.index))\n\t\t\t"
                                         "vehicles[count++] = iterator.index;", "\t\tvehicles[count++] = iterator.index;"),
                              "vehicles"),
    "client-swaps": ("engine", ("if (!game_engine || network_game_distributed_client() || !scenario)",
                                "if (!game_engine || !scenario)"), "vehicles"),
    "primary-not-written": ("rules", ("\t\t*weapon = (byte)", "\t\t(void)(byte)"), "loadout"),
    "loadout-old-primary": ("engine", ("byte const weapons[2] = { options->primary_weapon, options->secondary_weapon };",
                                       "byte const weapons[2] = { _loadout_weapon_assault_rifle, "
                                       "options->secondary_weapon };"), "loadout"),
    "custom-always": ("rules", ("\tif (custom)\n\t\tsets[count++] = VARIANT_VEHICLE_SET_CUSTOM;",
                                "\tsets[count++] = VARIANT_VEHICLE_SET_CUSTOM;"), "sets"),
    "free-for-all-one-side": ("rules", ("\tif (!variant->universal_variant.teams)\n\t\toptions->vehicle_set[1] = set;",
                                        ""), "sets"),
    "pc-types-as-xbox-set": ("rules", ("options->vehicle_set[0] < HOST_RULES_XBOX_VEHICLE_SETS ?\n\t\t"
                                       "options->vehicle_set[0] : 0;", "options->vehicle_set[0];"), "sets"),
    "flags-live-with-opence": ("rules", ("(standard && opence_machines == 0)", "standard"), "live"),
    "flags-live-in-legacy": ("rules", ("(standard && opence_machines == 0)", "opence_machines == 0"), "live"),
    "vehicles-wait": ("rules", ("return lobby || !network_host_rules_client_side(rule) ||", "return lobby ||"), "live"),
    "message-any-flags": ("rules", (" &&\n\t\t!(message->flags & ~HOST_RULES_CLIENT_FLAGS);", ";"), "message"),
    "message-other-format": ("rules", ("return message->format == HOST_RULES_FORMAT &&", "return"), "message"),
    "client-takes-every-flag": ("rules", ("return (current & ~HOST_RULES_CLIENT_FLAGS) | (sent & HOST_RULES_CLIENT_FLAGS);",
                                          "return sent;"), "message"),
}


def generated(fault=None):
    engine_h = read("source/game/game_engine.h")
    engine_c = read("source/game/game_engine.c")
    rules_c = read("port/linux/game/network_host_rules.c")
    rules_h = read("port/linux/game/network_host_rules.h")
    config = "\n".join([
        enum_with(engine_h, "_game_variant_draw_object_in_motion_sensor_bit = 0,"),
        enum_with(engine_h, "_loadout_category = 0,"),
        enum_with(engine_h, "_loadout_weapon_none = 0,"),
        enum_with(engine_h, "_variant_vehicle_warthog = 0,"),
        enum_with(engine_c, "_game_engine_vehicles_default = 0,"),
        enum_with(engine_c, "_weapon_list_assault_rifle = 0,"),
        rules_h[rules_h.index("/* ---------- constants */"):rules_h.index("/* ---------- structures */")],
        re.search(r"struct host_rules_message\n\{.*?\};", rules_h, re.S).group(0),
        *(f"#define {name} {constant(rules_c, name)}" for name in ("HOST_RULES_VEHICLE_SETS",
                                                                   "HOST_RULES_XBOX_VEHICLE_SETS")),
        re.search(r"#define VEHICLE_PAD_CLEAR_SQUARED [\d.f]+", engine_c).group(0),
    ]) + "\n"
    rules = "\n\n".join(function(rules_c, name) for name in RULES_FUNCTIONS)
    engine = "\n\n".join(function(engine_c, name) for name in ENGINE_FUNCTIONS)
    if fault:
        where, change = fault
        if where == "rules":
            rules = mutated(rules, *change)
        else:
            engine = mutated(engine, *change)
    return (("config.inc", config), ("rules.inc", rules), ("engine.inc", engine))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("host_rules", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    where, change, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("host_rules", generated((where, change))), case)
    assert status == CHECK_FAILED, f"a fault ({control}) passed '{case}': the test cannot see it\n{output}"
