"""A demo of a network co-op game this machine hosted plays its ticks as the host's (demo.c's
demo_playback_connection and demo_playback_network_game, through main.c's game_connection and
network_game_globals.c's network_game_get_game), and only its ticks."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['recording', 'coop_tick', 'coop_frames', 'multiplayer_tick', 'local_tick']
# (faults, and the case that must catch each)
CONTROLS = [
    # (the playback's ticks as a local game's: the cause, the co-op host's code left out)
    ('if (demo_playback_connection(&connection))\n\t\treturn connection;', '', 'coop_tick'),
    # (... with the host's connection but no network game: its players uncounted)
    ('if (demo_game)\n\t\treturn demo_game;', '', 'coop_tick'),
    # (the host's connection outside the ticks too)
    ('demo_player.playing && demo_recorder.in_tick &&', 'demo_player.playing &&', 'coop_frames'),
    # (a multiplayer game's ticks as its host's)
    ('_scenario_type_solo && !game_engine_running();\n}\nboolean demo_playback_connection',
     '_scenario_type_solo;\n}\nboolean demo_playback_connection', 'multiplayer_tick'),
]


def generated(fault=None):
    demo = read('port/linux/game/demo.c')
    config = '\n'.join([
        enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
        enum_with(read('source/scenario/scenario_definitions.h'), '_scenario_type_solo'),
    ]) + '\n'
    code = '\n'.join([
        function(demo, 'demo_playback_as_host'),
        function(demo, 'demo_playback_connection'),
        function(demo, 'demo_playback_network_game'),
        function(read('source/main/main.c'), 'game_connection'),
        function(read('source/networking/network_game_globals.c'), 'network_game_get_game'),
        function(read('port/linux/game/network_coop.c'), 'network_coop_active'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('demo_coop_host', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('demo_coop_host', generated((before, after))), case)
    assert status == CHECK_FAILED, output
