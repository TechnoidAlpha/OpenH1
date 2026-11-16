"""Split screen's views draw the cinematic screen effect without its blur and video, keeping its filters, where the Xbox
stopped on a fatal error (rasterizer_cinematics.c's rasterizer_screen_effect_get_cinematic_parameters): network co-op's
split screen players see the single-player scripts' blur outside a cutscene (the Pillar of Autumn's cryo tube on
Normal)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

SPLIT = "if (main_get_window_count() > 1 &&"


def generated(fault=False):
    cinematics = read("source/rasterizer/rasterizer_cinematics.c")
    structures = "\n".join((
        "struct bitmap_data;",
        structure(read("source/rasterizer/rasterizer_cinematics.h"), "rasterizer_cinematic_screen_effect_parameters"),
        structure(cinematics, "rasterizer_cinematic_screen_effect_state"),
    ))
    code = "\n".join(function(cinematics, name) for name in (
        "rasterizer_screen_effects_time", "rasterizer_screen_effect_start", "rasterizer_screen_effect_set_convolution",
        "rasterizer_screen_effect_set_filter", "rasterizer_screen_effect_get_cinematic_parameters"))
    if fault:
        # (the code before the change: split screen's views given the blur, the Xbox's fatal error)
        code = mutated(code, SPLIT, "if (FALSE &&")
    return (("structures.inc", structures + "\n"), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["one-view", "split", "split-video"])
def test_screen_effect_split(case):
    status, output = run(build("screen_effect_split", generated()), case)
    assert status == 0, output


def test_split_blur_detected():
    """without the split screen's copy (the code before the change, past the Xbox's fatal error), its views are drawn
    with the blur"""
    status, output = run(build("screen_effect_split", generated(fault=True)), "split")
    assert status == CHECK_FAILED, output
