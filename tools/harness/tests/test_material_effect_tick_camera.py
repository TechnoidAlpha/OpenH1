"""material_effect_visible (source/effects/material_effects.c) in the fast phase (port/linux/game/sim_rate.h): the
camera as the tick began, as a tick at 30 sees it, so a player's own spawn makes no landing footsteps at 60 and 120
(the frames between the spawn tick's sub-steps had brought the camera to it); the frames, the camera as it is."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

# the camera as it is in the fast phase too: the spawn's landing is made at 60 and 120
CONTROL = ("if (sim_substep_index() != NONE && material_effect_tick_cameras.taken[local_player_index])",
           "if (FALSE)", "spawn")


def generated(fault=None):
    code = read("source/effects/material_effects.c")
    if fault:
        code = mutated(code, *fault)
    start = code.index("static struct\n{\n\tboolean taken[")
    text = code[start:code.index("} material_effect_tick_cameras;", start)] + "} material_effect_tick_cameras;\n"
    text += "void material_effects_tick_begin(void);\n"
    text += function(code, "material_effect_visible") + "\n" + function(code, "material_effects_tick_begin") + "\n"
    return (("under_test.inc", text),)


def test_spawn():
    status, output = run(build("material_effect_tick_camera", generated()), "spawn")
    assert status == 0, output
    # the tick's own work takes the cameras before anything of the tick
    macro_step = function(read("source/game/game.c"), "game_tick_macro_step")
    assert macro_step.index("material_effects_tick_begin();") < macro_step.index("units_update();")


def test_negative_control():
    status, output = run(build("material_effect_tick_camera", generated(CONTROL[:2])), CONTROL[2])
    assert status == CHECK_FAILED, output
