"""Production pickup counts, safe placement and checkpoint flags over a fake campaign."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ['counts', 'eligibility', 'health-kits', 'floor-and-walls', 'occupied-and-full', 'checkpoint-and-bsp', 'copies']


def generated(fault=None):
    source = re.sub(r'^#include.*\n', '', read('port/linux/game/coop_scaling.h') + '\n' +
                    read('port/linux/game/coop_pickups.c'), flags=re.M)
    if fault:
        source = mutated(source, *fault)
    return (('under_test.inc', source),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('coop_pickups', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('(COOP_SCALING_MAXIMUM_GROWTH - 1) * 100', '100000'), 'counts'),
    (('source->item.flags &= ~PICKUPS_COUNT_MASK;', '(void)source;'), 'checkpoint-and-bsp'),
    (('collision_test_sphere(center, radius + 0.02f, source_index)', 'FALSE'), 'floor-and-walls'),
    (('!TEST_FLAG(item->object.flags, _object_outside_of_map_bit)', 'TRUE'), 'checkpoint-and-bsp'),
    ((' || type == _equipment_powerup_health', ''), 'health-kits'),
])
def test_negative_control(fault, case):
    status, output = run(build('coop_pickups', generated(fault)), case)
    assert status == CHECK_FAILED, output
