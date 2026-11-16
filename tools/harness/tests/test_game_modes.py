"""OpenCE's gametypes: options' defaults and range, Hardcore's restore of a killer, the modes a gametype allows,
Infection's round and state, Gun Game's weapons, Pro Doubles' rules (matchmaking's 2V2: competitive Halo 1 2v2's)
(port/linux/game/game_modes.c, infection.c, gun_game.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    engine = read('source/game/game_engine.h')
    engine_code = read('source/game/game_engine.c')
    network = read('port/linux/game/network_infection.h')
    enums = '\n'.join([
        enum_with(engine, 'game_engine_none = 0'), enum_with(engine, '_loadout_weapon_none = 0'),
        enum_with(engine, '_variant_vehicle_warthog = 0'), enum_with(engine, '_game_mode_none = 0'),
        enum_with(engine, '_gun_game_order_strongest_first = 0'),
        enum_with(network, '_distributed_infection_waiting = 0'),
        enum_with(network, '_distributed_infection_role_none = 0'),
        enum_with(engine, '_game_variant_draw_object_in_motion_sensor_bit = 0'),
        enum_with(engine, '_friendly_fire_on = 0'), enum_with(engine, '_loadout_category = 0'), enum_with(engine, '_radar_players_all = 0'),
        enum_with(engine_code, '_radar_motion_tracker = 0'), enum_with(engine_code, '_game_engine_weapons_normal = 0'),
        enum_with(engine_code, '_game_engine_vehicles_warthog,')]) + '\n'
    structures = '\n'.join([
        structure(engine, 'universal_variant'), structure(engine, 'slayer_variant'),
        'union game_engine_variant { struct slayer_variant slayer; long data[6]; };',
        structure(engine, 'game_variant').replace('wchar_t', 'unsigned short'),
        structure(engine, 'game_variant_options'),
        structure(network, 'distributed_infection_player'), structure(network, 'distributed_infection_state')]) + '\n'
    modes = read('port/linux/game/game_modes.c')
    infection = read('port/linux/game/infection.c')
    gun_game = read('port/linux/game/gun_game.c')
    defaults = re.search(r'/\* Infection\'s options when a gametype has none.*?\};', modes, re.S).group(0)
    pro = re.search(r'/\* Pro Doubles\' \(game_modes_pro_doubles\).*?\};', modes, re.S).group(0)
    order = re.search(r'static byte const gun_game_strongest_first\[\] =.*?\};', gun_game, re.S).group(0)
    code = '\n'.join([
        defaults, pro, enum_with(modes, 'GOAL_RADAR_NAV_POINT = 1'),
        function(modes, 'game_modes_option'), function(modes, 'game_modes_options_default'),
        function(modes, 'game_modes_options_sanitize'), function(modes, 'game_modes_player_killed'),
        function(modes, 'game_modes_variant_mode'), '#define GAME_MODES_KIND_HARDCORE 0x80',
        function(modes, 'game_modes_kind'), function(modes, 'game_modes_kind_name'),
        function(infection, 'infection_round_result'),
        function(infection, 'infection_state_valid'), order,
        '#define GUN_GAME_MAXIMUM_LEVELS NUMBEROF(gun_game_strongest_first)',
        function(gun_game, 'gun_game_levels_build'),
        function(engine_code, 'build_game_variant_team_slayer'), function(engine_code, 'game_variant_options_default'),
        function(modes, 'game_modes_pro_doubles')])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('structures.inc', structures), ('under_test.inc', code))


CASES = ['options_default', 'hardcore_restore', 'hardcore_no_restore', 'sanitize_defaults', 'sanitize_range',
         'sanitize_keeps', 'variant_mode', 'round_result', 'state_valid', 'gun_game_order', 'gun_game_missing',
         'gun_game_random', 'kind', 'kind_untrusted', 'pro_doubles']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('game_modes', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('!= FALSE;\n\tcsmemset(options->reserved, 0,', '!= FALSE;\n\tcsmemset(options->reserved, 1,'), 'sanitize_range'),
    (('unit->object.shield_vitality < 1.0f', 'TRUE'), 'hardcore_restore'),
    (('friendly_fire ||\n', ''), 'hardcore_no_restore'),
    (('return variant->universal_variant.teams ? _game_mode_infection', 'return 1 ? _game_mode_infection'),
     'variant_mode'),
    (('if (!humans)\n\t\treturn 1;', 'if (!humans)\n\t\treturn NONE;'), 'round_result'),
    (('state->revision < revision ||', ''), 'state_valid'),
    (('player->reserved[0] || player->reserved[1] || player->reserved[2]', 'player->reserved[0]'), 'state_valid'),
    (('if (game_engine_loadout_weapon_definition(weapon) != NONE)', 'if (TRUE)'), 'gun_game_missing'),
    (('&& !network_game_distributed_client()', ''), 'gun_game_random'),
    (('game_engine_index == game_engine_slayer && mode > _game_mode_none', 'mode > _game_mode_none'),
     'kind_untrusted'),
    (('if (!hardcore || game_engine_index', 'if (game_engine_index'), 'kind_untrusted'),
    (('PRO_DOUBLES_SCORE_TO_WIN = 50', 'PRO_DOUBLES_SCORE_TO_WIN = 25'), 'pro_doubles'),
    (('options->radar_players = _radar_players_none;\n\tvariant->game_engine_variant', 'variant->game_engine_variant'),
     'pro_doubles'),
    (('_game_variant_generic_starting_equipment_bit, TRUE);\n\t/* (other', '_game_variant_generic_starting_equipment_bit, '
      'FALSE);\n\t/* (other'), 'pro_doubles'),
])
def test_negative_control(fault, case):
    status, output = run(build('game_modes', generated(fault)), case)
    assert status == CHECK_FAILED, output
