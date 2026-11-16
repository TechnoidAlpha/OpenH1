"""The player a damage is credited to, with its owner or a parent gone (source/objects/damage.c, #725)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCE = "source/objects/damage.c"
CASES = ["player_unit", "parented", "several_parents", "ai_unit", "unparented", "none", "owner_gone", "parent_gone",
         "slot_reused"]
GONE = ["owner_gone", "parent_gone", "slot_reused"]


def generated(fault=None):
    code = function(read(SOURCE), "get_player_index_from_object_or_parents")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_damage_owner_lookup(case):
    status, output = run(build("damage_owner_lookup", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("case", GONE)
def test_negative_control(case):
    # the strict lookup, as the original's
    fault = ("object = object_try_and_get(object_index);", "object = object_get(object_index);")
    status, output = run(build("damage_owner_lookup", generated(fault)), case)
    assert status == CHECK_FAILED, output
