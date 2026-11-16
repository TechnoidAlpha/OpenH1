"""The players' animation enhancements as a rule of the game (network_standard.c): the host's word on its clients,
a game of one machine's own setting, a demo's recording's in its playback; never the machine's own settings where
the game has a word of its own, so that no machine's settings change what its game simulates."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['client', 'host', 'local', 'playback', 'simulation-fixes']
# (faults, and the case that must catch each)
CONTROLS = [
    # (a client's own setting, as before: its host and it posed that player otherwise)
    ('return !TEST_FLAG(game->standard_flags, _network_game_classic_turning_bit);\n\t\tbreak;', 'break;',
     'client'),
    # (the host's own setting, not its lobby's word)
    ('if (network_standard_globals.hosting)\n\t\t\treturn !network_standard_globals.hosting_classic_turning;', '',
     'host'),
    # (a playback with this machine's settings)
    ('if (demo_playback_rules(&rules))\n\t\t\treturn TEST_FLAG(rules, _demo_rule_enhanced_animations_bit);', '',
     'playback'),
    ('if (demo_playback_rules(&rules))\n\t\t\treturn TEST_FLAG(rules, _demo_rule_turns_with_aim_bit);', '',
     'playback'),
    # (OpenH1's fixes of the simulation in a Legacy game, which OpenCE's machines play otherwise)
    ('return network_standard_globals.hosting;', 'return TRUE;', 'simulation-fixes'),
    ('return network_standard_globals.joined;', 'return TRUE;', 'simulation-fixes'),
    # (a playback by this machine's game, not its recording's)
    ('if (demo_playback_rules(&rules))\n\t\treturn TEST_FLAG(rules, _demo_rule_simulation_fixes_bit);', '',
     'simulation-fixes'),
]


def generated(fault=None):
    standard = read('port/linux/game/network_standard.c')
    config = '\n'.join([
        enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
        enum_with(read('source/networking/network_game_manager.h'), '_network_game_classic_turning_bit'),
        enum_with(read('port/linux/game/demo.h'), '_demo_rule_enhanced_animations_bit'),
    ]) + '\n'
    code = function(standard, 'network_standard_turns_with_aim') + '\n' + \
        function(standard, 'network_standard_enhanced_animations') + '\n' + \
        function(standard, 'network_standard_simulation_fixes')
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('game_rules', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('game_rules', generated((before, after))), case)
    assert status == CHECK_FAILED, output
