"""A camouflaged object's other parts (the ammo counter) drawn once, after its model parts in their own view (#701)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run
from harness.render_types import transparent_types

SOURCE = "source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c"
CASES = ["one-run", "split-run", "legs", "two-objects"]
# the old rule: the parts after any run of the object's model parts (the next
# group alone looked at), so a split run drew the counter twice
CONTROL = (("next_group = rasterizer_transparent_geometry_next_group(next_group))", "next_group = NULL)"),
           "split-run")


def generated(control=None):
    source = read(SOURCE)
    types = transparent_types() + "\n" + enum_with(source, "_render_model_effect_type_active_camouflage")
    code = "\n".join(function(source, name) for name in [
        "active_camouflage_same_view", "active_camouflage_last_model_group", "active_camouflage_part_follows"])
    if control:
        code = mutated(code, *control)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("camouflage_parts_order", generated()), case)
    assert status == 0, output


def test_negative_control():
    status, output = run(build("camouflage_parts_order", generated(CONTROL[0])), CONTROL[1])
    assert status == CHECK_FAILED, f"the old rule escaped its check: {output}"
