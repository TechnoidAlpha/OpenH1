"""A client's own players' hits shown ahead of the host's word: confirmed, refused and undone (network_damage.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ['hit-state', 'hit-unanswered', 'kill-confirmed', 'kill-host-state', 'kill-refused', 'killed-otherwise',
         'not-shown']
FUNCTIONS = ['distributed_hits_predicted', 'distributed_predicted_dealing', 'network_damage_predicting',
             'distributed_hit_predictable', 'distributed_predicted_hits_reset', 'distributed_predicted_hit_free',
             'distributed_predicted_hits_forget',
             'distributed_predict_hit', 'distributed_predicted_hit_answered', 'distributed_predicted_kill',
             'distributed_predicted_kill_confirmed', 'distributed_predicted_kill_refused',
             'network_damage_predicted_state', 'distributed_predicted_hits_update',
             'network_damage_prediction_statistics', 'distributed_predicted_kill_answered']
CONTROLS = [
    # the host's stale state taken whole: the hit's flare and damage lost until its word comes
    ('result->shield_vitality -= hit->shield_damage;', ';', 'hit-state'),
    # an answered hit taken off a state that has it already
    ('if (hit->answered)\n\t\t{', 'if (FALSE)\n\t\t{', 'hit-state'),
    # the kill announced as it is shown, not on the host's word
    ('\tdamage_predicting = FALSE;\n\tdamage_predicting_object = NONE;\n\tunit =',
     '\tdamage_predicting = FALSE;\n\tdamage_predicting_object = NONE;\n'
     '\tgame_engine_player_killed(NONE, NONE, 0, FALSE);\n\tunit =', 'kill-confirmed'),
    # a refused kill left lying dead
    ('unit_port_revive(object_index);', ';', 'kill-refused'),
    # a refusal not waited for past the round trip
    ('now - hit->time <= wait', 'now - hit->time < 0', 'kill-refused'),
    # shown in a Legacy game
    ('network_standard_agreed_rate() == 0 || ', '', 'not-shown'),
]


def generated(fault=None):
    network = read('port/linux/game/network_damage.c')
    config = enum_with(read('source/objects/object_types.h'), '_object_type_biped') + '\n'
    config += enum_with(read('source/objects/objects.h'), '_object_dead_bit') + '\n'
    config += enum_with(read('source/objects/damage.h'), '_damage_kill_instantly_bit') + '\n'
    config += enum_with(network, '_damage_event_player_effect') + '\n'
    config += enum_with(network, '_damage_event_friendly_fire_bit') + '\n'
    config += enum_with(network, '_damage_category_melee = 6') + '\n'
    config += structure(read('source/objects/objects.h'), 'location') + '\n'
    config += structure(read('source/objects/damage.h'), 'damage_data') + '\n'
    config += structure(read('source/objects/damage.h'), 'damage_network_state') + '\n'
    config += structure(network, 'distributed_damage') + '\n'
    config += structure(network, 'distributed_damage_event') + '\n'
    config += '#define MAXIMUM_PREDICTED_HITS 64\n'
    # the client's record of the hits it showed, as the game has it
    match = re.search(r'^static struct damage_predicted_hit\n\{.*?^static long damage_predicted_kills_refused;\n',
                      network, re.M | re.S)
    code = match.group(0) + '\n'.join(function(network, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('predicted_hits', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('predicted_hits', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_wiring():
    # The game deals a hit shown with the least damage, counts nothing of it, and takes the host's states less it;
    # the client's handler answers its hits and kills before replaying the host's.
    damage = read('source/objects/damage.c')
    assert 'network_damage_predicting() ?' in function(damage, 'object_cause_damage')
    aftermath = function(damage, 'object_damage_aftermath')
    assert aftermath.index('network_damage_predicting()') < aftermath.index('game_statistics_record_damage(')
    assert 'network_damage_predicted_state(' in function(damage, 'damage_set_network_state')
    network = read('port/linux/game/network_damage.c')
    deals = function(network, 'network_damage_deals')
    assert deals.index('network_test_note_hit("made"') < deals.index('distributed_predict_hit(')
    events = function(network, 'network_damage_handle_events')
    assert events.index('distributed_predicted_hit_answered(') < events.index('damage_replay_aftermath(')
    assert events.index('distributed_predicted_kill_answered(') < events.index('damage_replay_kill(')
    assert 'distributed_predicted_hits_update();' in function(network, 'network_damage_client_tick')
