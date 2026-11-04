"""A decal takes the frames' local seed, in a tick too: the game's local seed never moves with what the frames left
of the decals (the vertex cache's room)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['tick', 'frame', 'permanent']


def generated(fault=None):
    collisions = read('source/physics/collisions.h')
    config = '\n'.join([
        enum_with(collisions, '_collision_test_front_facing_surfaces_bit'),
        enum_with(collisions, '_collision_result_structure'),
        enum_with(read('source/effects/decal_definitions.h'), '_decal_definition_water_effect_bit'),
    ]) + '\n'
    random_math = read('source/math/random_math.c')
    code = '\n'.join([
        function(random_math, 'get_global_local_random_seed_address'),
        function(random_math, 'random_math_port_set_ticking'),
        function(random_math, 'random_math_port_set_local_seeds'),
        function(random_math, 'random_math_port_begin_cosmetic'),
        function(random_math, 'random_math_port_end_cosmetic'),
        function(read('source/effects/decals.c'), 'decal_new'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('decal_seed', generated()), case)
    assert status == 0, output


FAULTS = [
    # (the decal on whichever seed the caller has: in a tick, the game's, as before)
    ('boolean ticking= random_math_port_begin_cosmetic();', 'boolean ticking= random_math_port_ticking;'),
    # (the tick left on the frames' seed after a decal)
    ('random_math_port_end_cosmetic(ticking);', ''),
]


@pytest.mark.parametrize('fault', FAULTS, ids=['game-seed', 'not-restored'])
def test_negative_control(fault):
    status, output = run(build('decal_seed', generated(fault)), 'tick')
    assert status == CHECK_FAILED, output
