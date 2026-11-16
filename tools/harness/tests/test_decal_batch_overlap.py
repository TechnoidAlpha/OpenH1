"""Decals that overlap are never one draw (rasterizer_decal_batch_overlaps): a double multiply decal blends in its
pixel shader with display.hdr, and a draw's triangles read one another's writes in order only where the GPU orders
them, so a scorch over a scorch in one draw lost the one under it on Vulkan."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run


def constant_define(source, name):
    """the source's #define of name, as it is written"""
    return re.search(rf"^#define {name} .*$", source, re.M).group(0)

CASES = ["scorches", "apart", "touching", "inside", "later"]


def generated(fault=None):
    source = read("source/rasterizer/xbox/rasterizer_xbox_decals.c")
    code = constant_define(source, "DECAL_BATCH_OVERLAP_MARGIN") + "\n" + function(source, "rasterizer_decal_batch_overlaps")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("decal_batch_overlap", generated()), case)
    assert status == 0, output


FAULTS = {
    # (no decal overlaps: the batch as before)
    "never": (("\t\t\treturn TRUE;", "\t\t\treturn FALSE;"), "scorches"),
    # (the last decal of the batch alone compared)
    "last-only": (("for (batch_index = 0; batch_index < decal_batch_count; batch_index++)",
                   "for (batch_index = decal_batch_count - 1; batch_index >= 0 && batch_index == decal_batch_count - 1; "
                   "batch_index--)"), "later"),
    # (an axis left out)
    "no-z": (("bounds[4] <= other[5] + DECAL_BATCH_OVERLAP_MARGIN && other[4] <= bounds[5] + DECAL_BATCH_OVERLAP_MARGIN",
              "TRUE"), "apart"),
    # (edges that meet counted apart)
    # (no margin: two scorches a little apart off one floor counted apart)
    "no-margin": (("#define DECAL_BATCH_OVERLAP_MARGIN 0.05f", "#define DECAL_BATCH_OVERLAP_MARGIN 0.0f"), "scorches"),
}


@pytest.mark.parametrize("fault", FAULTS)
def test_negative_control(fault):
    status, output = run(build("decal_batch_overlap", generated(FAULTS[fault][0])), FAULTS[fault][1])
    assert status == CHECK_FAILED, f"{fault} escaped its check: {output}"
