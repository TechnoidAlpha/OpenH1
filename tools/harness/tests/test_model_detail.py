"""display.model_detail's level for each permutation: the most detailed it has, and a close-up's level only at the size the Xbox drew it at
(port/linux/game/model_detail.c, from eltanschauung's #282)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ["interior", "stock-size", "permutations", "rules"]
CONTROLS = {
    "shown-close-up": (("rules==_model_detail_rules_close_up ||", "TRUE ||"), "interior"),
    "no-fallback": (("permutation->geometry_indices[level]!=NONE &&", "TRUE &&"), "permutations"),
    "cutoff-excluded": (("return stock_pixels>=model->detail_cutoff_pixels[level];", "return stock_pixels>model->detail_cutoff_pixels[level];"), "stock-size"),
    "display-size": (("return model_detail_stock_close_up(model, stock_pixels) ?", "return TRUE ?"), "rules"),
    "debug-level-ignored": (("|| rasterizer_debug_options.debug_model_lod!=NONE", ""), "rules"),
}


def generated(control=None):
    source = read("port/linux/game/model_detail.c")
    code = enum_with(read("port/linux/game/model_detail.h"), "_model_detail_rules_stock")
    code += "\n" + enum_with(source, "MODEL_DETAIL_CLOSE_UP_CUTOFF_PIXELS")
    for name in ["model_detail_stock_close_up", "model_detail_rules", "model_detail_geometry_index"]:
        code += "\n" + function(source, name)
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("model_detail", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("model_detail", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
