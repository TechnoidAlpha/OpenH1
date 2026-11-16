"""The Pro 2v2 rules (README, "PRO 2V2"), for both kinds, compiled from the real code: a 2V2 host plays them whatever
its own settings (network_standard.c's hosting rules): its game record says so (_network_game_pro_rules_bit), so that
every OpenH1 machine draws the dynamic lights classic and shows no teammates' feeds (network_standard_pro_game); its
shield effects are the stock game's (network_shield_effects.c), its mouse aim assist blocked (network_aim_assist.c),
its weapons picked up by holding the action button; a joining machine whose mods replace sounds, bitmaps or models is
refused (network_distributed.c's distributed_pro_refused_mods), text alone allowed; each player's field of view is
capped at 110 across 16:9 (view_fov.c); and the players of a multiplayer game always collide (network_coop.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['pro_host', 'pro_client', 'crispy', 'not_pro', 'shields', 'aim_assist', 'pickup', 'mods', 'fov',
         'collisions']
# (faults, and the case that must catch each)
CONTROLS = [
    ('\t\tSET_FLAG(game->standard_flags, _network_game_pro_rules_bit, network_standard_pro_hosting());\n', '',
     'pro_client'),
    ('TEST_FLAG(game->standard_flags, _network_game_pro_rules_bit)', 'FALSE', 'pro_client'),
    ('\tcase _game_connection_network_server:\n\t\treturn network_standard_pro_hosting();\n', '', 'pro_host'),
    ('return network_standard_pro_hosting() ? _shield_effects_stock : host_effects();', 'return host_effects();',
     'shields'),
    ('return blocks || network_standard_pro_hosting();', 'return blocks;', 'aim_assist'),
    ('\tif (network_standard_globals.hosting_pro)\n\t\tnetwork_standard_globals.hosting_hold_to_pick_up = TRUE;\n',
     '', 'pickup'),
    ('network_standard_globals.hosting_hold_to_pick_up = network_standard_pro_hosting() ||\n',
     'network_standard_globals.hosting_hold_to_pick_up = FALSE ||\n', 'pickup'),
    ('!strstr(open, "sounds") && ', '', 'mods'),
    ('!strstr(open, "bitmaps") && ', '', 'mods'),
    ('!strstr(open, "models") &&\n', '', 'mods'),
    ('\t\t\t\t!strstr(open, "unlisted"))', '\t\t\t\t1)', 'mods'),
    ('return tangent > cap ? cap : tangent;', 'return tangent;', 'fov'),
    ('if (!network_coop_active() || !(game = network_game_get_game()))\n\t\treturn TRUE;\n\treturn !TEST_FLAG('
     'game->cooperative_flags, _network_game_cooperative_no_player_collisions_bit);',
     'if (!(game = network_game_get_game()))\n\t\treturn TRUE;\n\treturn !TEST_FLAG('
     'game->cooperative_flags, _network_game_cooperative_no_player_collisions_bit);', 'collisions'),
]


def generated(fault=None):
    standard = read('port/linux/game/network_standard.c')
    shields = read('port/linux/game/network_shield_effects.c')
    assist = read('port/linux/game/network_aim_assist.c')
    distributed = read('port/linux/game/network_distributed.c')
    fov = read('port/linux/game/view_fov.c')
    coop = read('port/linux/game/network_coop.c')
    config = '\n'.join([
        enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
        enum_with(read('source/networking/network_game_manager.h'), '_network_game_pro_rules_bit'),
        enum_with(read('source/networking/network_game_manager.h'), '_network_game_cooperative_no_player_collisions_bit'),
        enum_with(read('port/linux/game/network_shield_effects.h'), '_shield_effects_stock'),
        '#define DISTRIBUTED_CLIENT_BUILD_MODS_SIZE 512',
        '#define PRO_FIELD_OF_VIEW 110.0',
    ]) + '\n'
    code = '\n'.join([function(standard, name) for name in [
        'network_standard_server_setting', 'network_standard_rate_setting', 'network_standard_server_created',
        'network_standard_set_hosting_rules', 'network_standard_pro_hosting', 'network_standard_server_rate',
        'network_standard_stamp_game', 'network_standard_pro_game', 'network_standard_enhanced_lights_allowed',
        'network_standard_teammate_feeds_allowed']] +
        [function(shields, 'host_effects'), function(shields, 'network_shield_effects_hosted'),
         function(shields, 'network_shield_effects'), function(assist, 'host_blocks_mouse'),
         function(distributed, 'distributed_pro_refused_mods'), function(fov, 'fov_pro_capped'),
         function(coop, 'network_coop_player_collisions')])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('pro_rules', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('pro_rules', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_wired():
    """Where the rules are taken: the drawn lights and the map's preparation, the field of view, the shield effects
    sent and drawn, the host's refusal of a machine's mods, the mods' list a client sends, the held Settings rows."""
    device = read('port/linux/src/d3d8_device.c')
    assert '!strcmp(config_string("display.dynamic_lights"), "enhanced") && allowed' in \
        function(device, 'dynamic_lights_read_settings')
    assert 'network_standard_enhanced_lights_allowed()' in function(read('port/linux/game/render_prepared.c'),
                                                                    'prepared_derivation')
    assert 'requested_tangent = fov_pro_capped(requested_tangent);' in \
        function(read('port/linux/game/view_fov.c'), 'render_fov_adjust')
    shields = read('port/linux/game/network_shield_effects.c')
    for name in ('network_shield_effects', 'network_shield_effects_send', 'network_shield_effects_update'):
        assert 'network_shield_effects_hosted()' in function(shields, name), name
    handler = read('port/linux/game/network_distributed.c')
    assert 'distributed_pro_refused_mods(kept->mods, refused, sizeof(refused))' in handler
    assert 'network_game_server_kick_machine(machine_index, FALSE);' in handler
    assert '"%s%s%s%s [%s]"' in function(read('port/linux/src/port_mods.c'), 'port_mods_list')
    rows = read('port/linux/game/menu_functions.c')
    for row in ('op_dynamic_lights', 'op_shield_glow', 'op_shield_glow_intensity', 'op_overshield_effect',
                'op_mouse_aim_assist', 'op_direct_camera'):
        assert f'{{ "{row}",' in rows, row
