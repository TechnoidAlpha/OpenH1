"""The projectiles in the fast phase (source/items/projectiles.c, on port/linux/game/sim_rate.c's clock): a shot fired
at any sub-step of 60 or 120 Hz is, a whole number of 30ths of a second after it was fired, where a 30 Hz shot is that
long after it was (its head start of a whole 30 Hz tick, then its hold of a tick of time, then its sub-steps), strikes
a floor at the same point and time, and its fuse goes off on the same tick. The scorecard's scripts fire on the 30 Hz
grid (sub-step 0); this fires at every sub-step."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["classic", "head-start", "strike", "bounce", "fuse"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # no hold: a shot made late in a tick travels on at once (ahead of one made before it)
    "no-hold": (("if (!TEST_FLAG(flags, _projectile_substep_hold_crossed_bit) || substep_index != phase)",
                 "if (FALSE)"), "head-start"),
    # a head start of a sub-step, not a tick: the Halo 2 kind, its instant range cut
    "head-start-of-a-substep": (("\t\t\t\tprojectile_step(projectile_index, 1.0f, 0.0f, TRUE, TRUE);",
                                 "\t\t\t\tprojectile_step(projectile_index, tick_fraction, 0.0f, TRUE, TRUE);"),
                                "head-start"),
    # the arc's own sub-steps, not its tick's chord: a strike elsewhere than at 30
    "arc-not-chord": (("real gravity_ticks = (1.0f - segment_start) * 0.5f - (now_ticks - segment_start);",
                       "real gravity_ticks = step_ticks * 0.5f;"), "strike"),
    # after a strike, the line from the tick's start rather than the one from the strike: a bounce elsewhere
    "line-from-the-tick-start": (("real gravity_ticks = (1.0f - segment_start) * 0.5f - (now_ticks - segment_start);",
                                  "real gravity_ticks = 0.5f - now_ticks;"), "bounce"),
    # the timers counting each sub-step: a fuse k times as fast
    "timers-each-substep": (("\t\t\t\tsubstep_index == phase,\n", "\t\t\t\tTRUE,\n"), "fuse"),
    # a slow projectile's speed set at every sub-step, as the tick's update sets it, not once for the next tick
    "speed-set-each-substep": (("\t\t\t\tif (whole_tick)\n", "\t\t\t\tif (TRUE)\n"), "head-start"),
}


def generated(fault=None):
    header = read("port/linux/game/sim_rate.h")
    clock = read("port/linux/game/sim_rate.c")
    code = read("source/items/projectiles.c")

    decls = enum_with(header, "SIM_RATE_CLASSIC") + "\n" + enum_with(header, "NUMBER_OF_SIM_SUBSTEP_SLOTS") + "\n"
    start = header.index("typedef void (*sim_substep_procedure)")
    decls += header[start:header.index(";", start) + 1] + "\n"
    for member in ("_projectile_tracer_bit", "_projectile_substep_head_start_bit", "_projectile_oriented_along_velocity_bit",
                   "_projectile_action_detonate", "_projectile_detonation_timer_starts_immediately",
                   "MAXIMUM_PROJECTILE_COLLISIONS_PER_UPDATE"):
        decls += enum_with(code, member) + "\n"
    decls += structure(read("source/items/projectiles.h"), "_projectile_datum") + "\n"

    # the clock: sim_rate.c's globals and every function
    text = clock[clock.index("/* ---------- globals */"):] + "\n"
    # the projectiles' globals, prototypes and code
    text += re.search(r"static real const seconds_per_tick = [^;]*;", code).group(0) + "\n"
    text += re.search(r"static real projectile_update_seconds = [^;]*;", code).group(0) + "\n"
    text += code[code.index("enum\n{\n\tPROJECTILE_SEGMENT_SLOTS"):code.index("projectile_segments[PROJECTILE_SEGMENT_SLOTS];")]
    text += "projectile_segments[PROJECTILE_SEGMENT_SLOTS];\n"
    for name in ("projectile_step", "projectiles_substep", "projectile_substep_finish", "projectile_segment_get",
                 "projectile_segment_set", "projectile_set_speed_under_final"):
        prototype = re.search(r"^static \w+ " + name + r"\([^;{]*\);", code, re.M)
        text += prototype.group(0) + "\n"
    for name in ("projectile_set_action", "projectile_update", "projectiles_substep", "projectile_substep_finish",
                 "projectile_segment_get", "projectile_segment_set", "projectile_set_speed_under_final", "projectile_step"):
        text += function(code, name) + "\n"
    # what projectile_new marks a projectile made with
    new = function(code, "projectile_new")
    start = new.index("\t/* port: in sub-steps, its head start to come")
    end = new.index("\tSIM_TRACE(sim_trace_projectile_new")
    text += "static void mark_made(long projectile_index, struct projectile_datum *projectile)\n{\n" + new[start:end] + "}\n"
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("projectile_substeps", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("projectile_substeps", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
