"""The real hit sounds (hit_sounds.c) and network_standard.c's rule of the host's: a beep for damage this machine's
player dealt another player, none for themselves, for no damage, for a bot's or another machine's player, with
the attacker's own audio.hit_sounds off, or where the host forbids them (network.allow_hit_sounds false, which an OpenH1 game's record
carries to its OpenH1 clients, read in the lobby; never in an OpenCE machine's copy, nor a Legacy host's record);
hits closer together than the spacing beep once."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["local", "not-ours", "setting", "spacing", "host", "records", "client"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # (a beep for the player's damage of themselves)
    "self": (("attacker_player_index == NONE || victim_player_index == NONE ||\n\t\tattacker_player_index == "
              "victim_player_index)", "attacker_player_index == NONE || victim_player_index == NONE)"), "not-ours"),
    # (a beep for a hit that dealt nothing: a teammate whom friendly fire spares)
    "no-damage": (("if (!(shield_damage > 0.f || body_damage > 0.f ||", "if (!(TRUE || body_damage > 0.f ||"),
                  "not-ours"),
    # (a beep for any player's hit, not this machine's)
    "any-attacker": (("!attacker || attacker->local_player_index == NONE ||", "!attacker ||"), "not-ours"),
    # (no kill outright)
    "no-kill": (("TEST_FLAG(being_damaged_flags, _object_being_damaged_body_depleted_bit)))", "FALSE))"), "local"),
    # (the host's rule not asked)
    "rule-ignored": (("if (!hit_sounds.on[slot] || !network_standard_hit_sounds_allowed())",
                      "if (!hit_sounds.on[slot])"), "client"),
    # (the setting read once, never again)
    "read-once": (("if (!hit_sounds.read[slot] || hit_sounds.read_at[slot] != config_changes())",
                   "if (!hit_sounds.read[slot])"), "setting"),
    # (one setting for every local player, not the attacker's own)
    "machine-setting": (("config_player_boolean(attacker->local_player_index, \"audio.hit_sounds\")",
                         "config_player_boolean(0, \"audio.hit_sounds\")"), "setting"),
    # (no spacing: a shotgun's pellets beep each)
    "no-spacing": (("if (hit_sounds.played && now - hit_sounds.played_at < HIT_SOUND_SPACING_MILLISECONDS)",
                    "if (FALSE)"), "spacing"),
    # (one buffer: a beep restarted in it, cutting the last short)
    "one-buffer": (("hit_sounds.next = (short)((hit_sounds.next + 1) % HIT_SOUND_BUFFERS);", ""), "spacing"),
    # (a client takes its own setting, not its host's word)
    "client-own-rule": (("return !TEST_FLAG(game->standard_flags, _network_game_no_hit_sounds_bit);",
                         "return config_boolean(\"network.allow_hit_sounds\") != 0;"), "client"),
    # (the host's rule changed during the game)
    "read-in-game": (("\tif (in_lobby)\n\t{\n\t\tnetwork_standard_globals.hosting_hold_to_pick_up",
                      "\tif (TRUE)\n\t{\n\t\tnetwork_standard_globals.hosting_hold_to_pick_up"), "host"),
    # (an OpenCE machine sent the bit)
    "opence-sent-the-bit": (("\tSET_FLAG(legacy_game.standard_flags, _network_game_no_hit_sounds_bit, FALSE);\n",
                             ""), "records"),
    # (a Legacy host's record forbids them)
    "legacy-record": (("network_standard_globals.hosting && network_standard_globals.hosting_no_hit_sounds);",
                       "network_standard_globals.hosting_no_hit_sounds);"), "records"),
}


def module_state(source):
    """hit_sounds.c's #defines and its state (the static struct), as the module has them"""
    defines = "\n".join(re.findall(r"^#define HIT_SOUND_(?:VOLUME|SPACING_MILLISECONDS|BUFFERS) .*$", source, re.M))
    start = source.index("static struct\n{")
    end = source.index(";", source.index("} hit_sounds =", start)) + 1
    return defines + "\n" + source[start:end] + "\n"


def generated(fault=None):
    sounds = read("port/linux/game/hit_sounds.c")
    standard = read("port/linux/game/network_standard.c")
    config = enum_with(read("source/networking/network_game_manager.h"), "_network_game_no_hit_sounds_bit") + "\n" + \
        module_state(sounds)
    code = "\n".join([
        function(standard, "network_standard_game_for_machine"),
        function(standard, "network_standard_stamp_game"),
        function(standard, "network_standard_hit_sounds_allowed"),
        function(sounds, "hit_sounds_make"),
        function(sounds, "hit_sounds_play"),
        function(sounds, "hit_sounds_note_damage"),
    ])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hit_sounds", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hit_sounds", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
