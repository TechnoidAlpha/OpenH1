"""A particle's first update covers at least the Xbox's frame: one whose sequence runs out within it (the plasma
grenade's and the Shade's bolts of lightning, a single sprite at 30 a second) is never drawn, as on the Xbox, at any
frame rate."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ['single_sprite_unseen', 'longer_sequence_seen', 'first_update_a_frame_old']


def generated(fault=None):
    header = read('source/effects/particles.h')
    particles = read('source/effects/particles.c')
    config = '\n'.join([
        enum_with(read('source/cseries/cseries.h'), 'TICKS_PER_SECOND'),
        enum_with(particles, 'MAXIMUM_PARTICLE_UNRENDERED_FRAME_COUNT'),
        enum_with(header, '_particle_datum_animates_backwards_bit'),
        enum_with(particles, '_particle_definition_can_animate_backwards_bit'),
        enum_with(particles, '_particle_state_next_sequence_initial'),
        structure(header, 'particle_datum'),
        'static void particle_die(long particle_index);',
    ]) + '\n'
    code = '\n'.join([
        function(particles, 'particle_die'),
        function(particles, 'particle_next_sequence'),
        function(particles, 'particle_next_frame'),
        function(particles, 'particle_update_frame_time'),
        function(particles, 'particles_update'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('particle_first_frame', generated()), case)
    assert status == 0, output


FAULTS = [
    # (the first update the frame's own time: the bolts drawn between the ticks)
    ('MAX(dt, first_dt)', 'dt', 'single_sprite_unseen'),
    # (every update at least the Xbox's frame: the particles no longer move smoothly between the ticks)
    ('was_new && dt > 0.f ? MAX(dt, first_dt) : dt', 'dt > 0.f ? MAX(dt, first_dt) : dt', 'first_update_a_frame_old'),
]


@pytest.mark.parametrize('fault', FAULTS, ids=['frame-time', 'every-update'])
def test_negative_control(fault):
    before, after, case = fault
    status, output = run(build('particle_first_frame', generated((before, after))), case)
    assert status == CHECK_FAILED, output
