"""The real vehicle_hijack_seat_allowed (vehicle_hijack.c) and network_standard.c's rule: an enemy's Ghost or Warthog
driver, a Warthog's gunner, a landed Banshee's pilot may be hijacked; an ally, the Scorpion, a turret, a passenger, a
flying Banshee never; the rule is an OpenH1 host's (its record and its word), never a Legacy game's, a local game's
setting, a demo's recording's."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["enemy", "seat", "banshee", "rule-off", "host", "client", "local"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # (an ally's seat taken)
    "ally-hijacked": (("if (!facts->rule || facts->opence || !facts->enemy ||",
                       "if (!facts->rule || facts->opence ||"), "enemy"),
    # (the rule not asked)
    "rule-ignored": (("if (!facts->rule || facts->opence ||", "if (facts->opence ||"), "rule-off"),
    # (the Scorpion taken as a Warthog is)
    "scorpion": (("\tcase _vehicle_hijack_kind_jeep:\n", "\tcase _vehicle_hijack_kind_tank:\n\tcase _vehicle_hijack_kind_jeep:\n"),
                 "seat"),
    # (a Warthog's passenger taken)
    "passenger": (("return facts->part == _vehicle_hijack_seat_driver || facts->part == _vehicle_hijack_seat_gunner;",
                   "return TRUE;"), "seat"),
    # (a flying Banshee taken)
    "flying-banshee": (("facts->ground_clearance <= VEHICLE_HIJACK_FLYER_GROUND_CLEARANCE", "TRUE"), "banshee"),
    # (a Legacy host plays it)
    "legacy-host": (("return network_standard_globals.hosting && network_standard_globals.hosting_vehicle_hijack;",
                     "return network_standard_globals.hosting_vehicle_hijack;"), "host"),
    # (a Legacy game's client plays the record's word)
    "legacy-client": (("if (!network_standard_globals.joined || !game)\n\t\t\treturn FALSE;\n\t\tif (network_standard_globals.vehicle_hijack_heard",
                       "if (!game)\n\t\t\treturn FALSE;\n\t\tif (network_standard_globals.vehicle_hijack_heard"), "client"),
    # (a client's own setting, not its host's)
    "client-own-setting": (("return TEST_FLAG(game->standard_flags, _network_game_vehicle_hijack_bit);\n\tdefault:",
                            "return network_standard_globals.local_vehicle_hijack;\n\tdefault:"), "client"),
    # (a demo played by this machine's setting)
    "demo-own-setting": (("if (demo_playback_rules(&rules))\n\t{\n\t\tgame = demo_playback_recorded_game();",
                          "if (FALSE && demo_playback_rules(&rules))\n\t{\n\t\tgame = demo_playback_recorded_game();"),
                         "local"),
}


def generated(fault=None):
    header = read("port/linux/game/vehicle_hijack.h")
    defines = "".join(re.findall(r"^#define VEHICLE_HIJACK_\w+ [^\n]*\n", header, re.M))
    config = "\n".join([
        enum_with(header, "_vehicle_hijack_kind_none"),
        enum_with(header, "_vehicle_hijack_seat_driver"),
        defines,
        structure(header, "vehicle_hijack_seat_facts"),
        enum_with(read("source/networking/network_game_manager.h"), "_network_game_vehicle_hijack_bit"),
        enum_with(read("port/linux/game/demo.h"), "_demo_rule_vehicle_hijack_bit"),
    ]) + "\n"
    code = "\n".join([
        function(read("port/linux/game/vehicle_hijack.c"), "vehicle_hijack_seat_allowed"),
        function(read("port/linux/game/network_standard.c"), "network_standard_vehicle_hijack"),
    ])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vehicle_hijack", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("vehicle_hijack", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def default_of(source, name):
    entry = re.search(r'\{ "' + re.escape(name) + r'", _config_boolean, "(\w+)"', source)
    assert entry, f"no setting {name}"
    return entry.group(1)


def test_default_off():
    """Hijacking is the host's choice and off unless it turns it on: config.toml's default for game.vehicle_hijack
    (port_config.c) is false, so a host that never touched it plays stock Halo (and so do its clients). The negative
    control: the default turned on is seen."""
    source = read("port/linux/src/port_config.c")
    assert default_of(source, "game.vehicle_hijack") == "false"
    faulty = source.replace('{ "game.vehicle_hijack", _config_boolean, "false"', '{ "game.vehicle_hijack", _config_boolean, "true"')
    assert default_of(faulty, "game.vehicle_hijack") != "false"
