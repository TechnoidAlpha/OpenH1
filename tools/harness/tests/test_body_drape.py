"""A dead body's animated pose fitted to what is around it: raised onto the ground rising under it, moved out of a
wall a limb went through, and left as it is on open, flat ground (biped_limp_noodle_drape_dying)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ['flat', 'slope', 'steep', 'wall', 'limp', 'living']
CONTROLS = [
    ('position->z += MIN(rise, BIPED_DRAPE_MAXIMUM);', 'position->z += 0.f * rise;', 'slope'),
    ('MIN(rise, BIPED_DRAPE_MAXIMUM)', 'rise', 'steep'),
    ('position->x += push.i;\n\t\tposition->y += push.j;', ';', 'wall'),
    ('fabs(collision.plane.n.k) > 0.7f', 'fabs(collision.plane.n.k) > 2.f', 'steep'),
    ('start.z += BIPED_DRAPE_EMBEDDED;', 'start.z += 1.f;', 'steep'),
    ('TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit) ||', '', 'limp'),
]


def real_defines(source, *names):
    return ''.join('#define %s %sf\n' % (name, re.search(r'#define\s+' + name + r'\s+([\d.]+)f', source).group(1))
                   for name in names)


def generated(fault=None):
    noodle = read('source/units/biped_limp_noodle.c')
    config = '\n'.join([
        enum_with(read('source/objects/objects.h'), '_object_dead_bit'),
        enum_with(read('source/units/bipeds.h'), '_biped_limp_body_physics_active_bit'),
        enum_with(read('source/physics/collisions.h'), '_collision_test_for_bipeds_dead_flags'),
        '#define SET_FLAG(v,b,s) ((v)=(s)?((v)|FLAG(b)):((v)&~FLAG(b)))',
    ]) + '\n'
    config += real_defines(noodle, 'BIPED_DRAPE_CLEARANCE', 'BIPED_DRAPE_DEAD_ZONE', 'BIPED_DRAPE_MAXIMUM',
                           'BIPED_DRAPE_ABOVE', 'BIPED_DRAPE_EMBEDDED')
    code = function(noodle, 'biped_limp_noodle_drape_dying')
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('body_drape', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('body_drape', generated((before, after))), case)
    assert status == CHECK_FAILED, output
