"""A frame whose work waits for a tick's boundary (a checkpoint, a structure switch, a death: game_time_tick_open_wait)
at game.sim_rate 60 and 120 begins no tick, and builds no update for one: an update built for a tick not begun stayed
queued, and every tick after took one built a tick earlier, its input with it, until the map was loaded again (OpenH1
#590: presses 0.6 s late on The Silent Cartographer after a few structure switches). A stall drops the time past the
frame's most ticks and owes none after."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CLAMP = "\t\t\telse if (game_time_substeps.boundary_wanted && steps_elapsed > substep_count - sim_substep_next())"
# (the clamp at every tick's end, whether or not the frame's work waits: OpenH1 #706)
CLAMP_ALWAYS = (CLAMP, "\t\t\telse if (steps_elapsed > substep_count - sim_substep_next())")
# (the view's time since the newest step as it was: the fraction's part past its last whole sub-step)
AGE_PART = ("\tage = game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND;\n",
            "\tage = fmodf(game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND *\n"
            "\t\t(real)sim_substep_count(), 1.0f) / (real)sim_substep_count();\n")
OLD_BREAK = ("\t\t\t\t\t\tif (server_updates <= 0)\n\t\t\t\t\t\t\tbreak;\n",
             "\t\t\t\t\t\tif (server_updates <= 0)\n\t\t\t\t\t\t\tbreak;\n"
             "\t\t\t\t\t\tif (game_time_substeps.boundary_wanted)\n\t\t\t\t\t\t{\n"
             "\t\t\t\t\t\t\tgame_time_globals->leftover_dt += (real)(steps_elapsed - step_index)/steps_per_second;\n"
             "\t\t\t\t\t\t\tbreak;\n\t\t\t\t\t\t}\n")


def generated(*faults):
    source = read("source/game/game_time.c")
    code = "\n".join((function(source, "game_time_finish_tick"), function(source, "game_time_tick_open_wait"),
                      function(source, "game_time_update"), function(source, "game_time_get_step_age")))
    for fault in faults:
        code = mutated(code, *fault)
    substeps = re.search(r"static struct\n\{\n\t/\* sub-steps run before.*?\} game_time_substeps;", source, re.S)
    large = re.search(r"enum\n\{\n\tSOME_LARGE_NUMBER_OF_TICKS[^}]*\};", source)
    waits = re.search(r"#define GAME_TIME_MAXIMUM_BOUNDARY_WAITS \d+", source)
    defines = "\n".join((large.group(0), structure(source, "game_time_globals_struct"), substeps.group(0), waits.group(0)))
    return (("under_test.inc", code), ("tick_boundary_defines.h", defines))


@pytest.mark.parametrize("case", ["local", "host", "stall"])
def test_no_update_left_queued(case):
    status, output = run(build("tick_boundary_queue", generated()), case)
    assert status == 0, output


def test_negative_control_ticks_begun_while_waiting():
    """without the clamp, the frame runs on past the boundary its work waits for"""
    status, output = run(build("tick_boundary_queue", generated((CLAMP, "\t\t\telse if (0)"))), "local")
    assert status == CHECK_FAILED and "boundary" in output, output


@pytest.mark.parametrize("case", ["local", "host"])
def test_negative_control_queue_left_behind(case):
    """the code before the fix: the loop stopped at the boundary, the update built for the tick after left queued"""
    status, output = run(build("tick_boundary_queue", generated((CLAMP, "\t\t\telse if (0)"), OLD_BREAK)), case)
    assert status == CHECK_FAILED and "queued" in output, output


def test_view_steady():
    """At 120 and 60 frames a second, a frame runs every sub-step whose time has come, and the view drawn from the
    newest step, carried on by the time since it (game_time_get_step_age; render_interpolation.c's
    render_interpolation_newest_camera), moves as far each frame as the frame's time at a steady speed (OpenH1 #706)"""
    status, output = run(build("tick_boundary_queue", generated()), "view")
    assert status == 0, output


def test_negative_control_view_before():
    """the code before: every frame stopped at a tick's end, and the view took the fraction's part past its last whole
    sub-step: a quarter of a tick short in every other frame, the view stepping 17 and 6 cm in turn"""
    status, output = run(build("tick_boundary_queue", generated(CLAMP_ALWAYS, AGE_PART)), "view")
    assert status == CHECK_FAILED and "view" in output, output


def test_negative_control_clamp_always():
    """stopped at every tick's end: the next tick's first sub-step left a frame late"""
    status, output = run(build("tick_boundary_queue", generated(CLAMP_ALWAYS)), "view")
    assert status == CHECK_FAILED and "left for the next frame" in output, output
