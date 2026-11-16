"""Where a client's players' shots leave from when not from their units' cameras (a VR build's hand): taken by the
host for its players' own machine only and within a player's reach, and the line of sight looked from it only where a
hand firing at the target can be. (This build sends none: it fires from the camera, vr_aim.c.)"""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure


def generated(fault=None):
    source = read('port/linux/game/network_damage.c')
    distributed = read('port/linux/game/network_distributed.c')
    header = read('port/linux/game/network_distributed.h')
    defines = '\n'.join(re.findall(r'^#define FIRE_ORIGIN_\w+ [^\n]+', source, re.M))
    fresh = re.search(r'^\s*(FIRE_ORIGIN_FRESH_TICKS) = ([^,]+),', source, re.M)
    state = re.search(r'^static struct damage_fire_origin\n\{.*?\} damage_fire_origins\[MAXIMUM_TRACKED_PLAYERS\];',
                      source, re.M | re.S).group()
    code = '\n'.join([
        '#define TICKS_PER_SECOND 30', f'#define {fresh.group(1)} ({fresh.group(2)})', defines,
        structure(header, 'distributed_vector'), structure(source, 'distributed_fire_origin'), state,
        function(distributed, 'distributed_vector_pack'), function(distributed, 'distributed_vector_unpack'),
        function(source, 'distributed_fire_origin_within_reach'),
        function(source, 'distributed_fire_origin_shape'),
        function(source, 'network_damage_handle_fire_origins'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['own', 'foreign', 'reach', 'shape'])
def test_case(case):
    status, output = run(build('fire_origins', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    ((' ||\n\t\t\t!distributed_machine_has_player(machine_index, origin.player_index)', ''), 'foreign'),
    (('if (!distributed_fire_origin_within_reach(&offset))\n\t\t\tcontinue;', ''), 'reach'),
    (('damage_fire_origins[origin.player_index].time = game_time_get();', ''), 'own'),
    (('along >= -FIRE_ORIGIN_BEHIND && ', ''), 'shape'),
    (('sideways_squared <= FIRE_ORIGIN_SIDEWAYS * FIRE_ORIGIN_SIDEWAYS', 'sideways_squared >= 0.0f'), 'shape'),
    ((' || offset.k > FIRE_ORIGIN_ABOVE', ''), 'shape'),
])
def test_negative_control(fault, case):
    status, output = run(build('fire_origins', generated(fault)), case)
    assert status == CHECK_FAILED, output
