"""The AI's bipeds in the fast phase: of the micro lane (an actor's, a swarm's), their first sub-step of a tick
started from the velocity kept, not the root motion's set on the object, and their aim and input on the tick."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['root-motion', 'player', 'bump', 'lane']
CONTROLS = [
    ('velocity->i = kept->i + (velocity->i - prepared->i);', ';', 'root-motion'),
    ('velocity->i = kept->i + (velocity->i - prepared->i);', 'velocity->i = kept->i;', 'bump'),
    ('unit->unit.actor_index != NONE || ', '', 'lane'),
    (' || unit->unit.swarm_actor_index != NONE', '', 'lane'),
    # (an OpenCE player's biped in a mixed game: network_standard.c)
    ('return !network_standard_player_is_opence(unit->unit.player_index) &&', 'return TRUE &&', 'lane'),
    # (a rider in a vehicle's window: vehicle_riding_bipeds.c)
    (' &&\n\t\t\t\t!biped_riding_holds_lane(root_index);', ';', 'lane'),
    # (an OpenCE player's projectile)
    ('return !network_standard_player_is_opence(object_get(root_index)->object.owner_player_index);', 'return TRUE;',
     'lane'),
]


def generated(fault=None):
    config = enum_with(read('source/objects/object_types.h'), '_object_type_biped') + '\n'
    code = '\n'.join([
        function(read('source/units/bipeds.c'), 'biped_substep_start_velocity'),
        function(read('source/objects/objects.c'), 'object_lane_micro_candidate'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('ai_biped_substeps', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('ai_biped_substeps', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_ais_input_and_aim_stay_on_the_tick():
    # (a sub-step reads the input again for a player's biped only; the AI's throttle and facing are the tick's)
    step = function(read('source/units/bipeds.c'), 'biped_substep_move')
    assert 'if (substep_index > 0 && datum->speeds.valid && biped->unit.player_index != NONE)' in step
    # (the aim turns in the sub-steps for a player's unit only, and a vehicle a player guns (vehicles.c): the AI's
    # weapon fires on the tick, at its aim)
    update = function(read('source/units/units.c'), 'unit_update')
    assert ('if ((unit->unit.player_index != NONE || vehicle_gunned_by_player(unit_index)) && '
            'object_lane_moves_in_substeps(unit_index))') in update
    gunned = function(read('source/units/vehicles.c'), 'vehicle_gunned_by_player')
    assert 'unit_get(unit->unit.gunner_object_index)->unit.player_index!=NONE' in gunned


def test_the_ais_tick_in_its_own_update():
    # (an AI's biped is never left to the fast phase: its sub-steps run in its update, the rest of it after them)
    moves = function(read('source/units/bipeds.c'), 'biped_moves_in_substeps')
    assert 'biped_get(biped_index)->unit.player_index != NONE &&' in moves
    moving = function(read('source/units/bipeds.c'), 'biped_update_moving')
    assert moving.index('biped_substep_defer(') < moving.index('biped_substep_inline(')
    # (a lift carries the AI's riders in its update, as at 30: only the players' are held for the sub-steps)
    hold = function(read('source/objects/objects.c'), 'object_lane_hold_elevator_riders')
    assert 'biped->unit.player_index != NONE &&' in hold
