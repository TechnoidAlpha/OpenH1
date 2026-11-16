"""2V2 Classic's Instant Aim (network_standard.c): a matchmaking playlist's hosting rules make the next server an
OpenH1 game at the playlist's rate, closed to OpenCE's players, whatever network.game_mode and network.sim_rate say;
Classic's say every player's view is drawn as with display.direct_camera off, which the host's game record carries
(_network_game_no_direct_camera_bit) and every OpenH1 machine takes (network_standard_direct_camera_allowed), the
player's own setting untouched; Crispy's, and every other game's, leave each player's own."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['classic_host', 'classic_client', 'crispy', 'rules_cleared', 'legacy_client', 'out_of_game']
# (faults, and the case that must catch each)
CONTROLS = [
    # (the host's own setting kept: it would draw its players' views from the newest step)
    ('return !(network_standard_globals.hosting && network_standard_globals.hosting_no_direct_camera);',
     'return TRUE;', 'classic_host'),
    # (a client's own setting, not its host's word)
    ('TEST_FLAG(game->standard_flags, _network_game_no_direct_camera_bit)', 'FALSE', 'classic_client'),
    # (the record not stamped)
    ('\t\tSET_FLAG(game->standard_flags, _network_game_no_direct_camera_bit,\n'
     '\t\t\tnetwork_standard_globals.hosting && network_standard_globals.hosting_no_direct_camera);\n', '',
     'classic_client'),
    # (the playlist's rate not applied: the settings' 60)
    ('rate = network_standard_globals.rules_rate && sim_rate_supported(network_standard_globals.rules_rate) ?\n'
     '\t\tnetwork_standard_globals.rules_rate : setting;', 'rate = setting;', 'classic_host'),
    # (a Legacy network.game_mode taking a Classic 2V2 game to Legacy)
    ('rules || !(mode && !csstrcmp(mode, "legacy"));', '!(mode && !csstrcmp(mode, "legacy"));', 'classic_host'),
    # (OpenCE's players let in)
    ('network_standard_globals.hosting && !rules &&\n\t\tconfig_boolean("network.allow_opence");',
     'network_standard_globals.hosting &&\n\t\tconfig_boolean("network.allow_opence");', 'classic_host'),
]


def generated(fault=None):
    standard = read('port/linux/game/network_standard.c')
    config = '\n'.join([
        enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
        enum_with(read('source/networking/network_game_manager.h'), '_network_game_no_direct_camera_bit'),
    ]) + '\n'
    code = '\n'.join(function(standard, name) for name in [
        'network_standard_server_setting', 'network_standard_rate_setting', 'network_standard_server_created',
        'network_standard_set_hosting_rules', 'network_standard_pro_hosting', 'network_standard_server_rate',
        'network_standard_stamp_game',
        'network_standard_direct_camera_allowed'])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('instant_aim_rule', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('instant_aim_rule', generated((before, after))), case)
    assert status == CHECK_FAILED, output
