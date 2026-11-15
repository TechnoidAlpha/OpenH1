"""The VR mode's grenade thrown by the hand in the fast phase (port/linux/game/vr_grenade.c's vr_grenade_substep, with
source/items/projectiles.c's projectile_substep_launch, on port/linux/game/sim_rate.c's clock): the hand letting go in
the frame of any sub-step of 60 or 120 Hz lets the grenade go at that sub-step, from the hand (the eye of that
sub-step), along the hand's throw as hard as it threw it, and a whole number of 30ths of a second later it is as far
from where it left as a grenade let go of at 30 is (n * v * T30: its head start, then its hold). At 30 the throw is the
tick's alone, as it was. No VR runtime: a fake hand lets go at sub-steps j = 0 up to k - 1."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["classic", "throw", "drawn"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the slot releasing at 30 too: the throw no longer the tick's alone
    "slot-at-30": (("if (substep_count <= 1 || (unit_index = vr_grenade_player_unit()) == NONE)",
                    "if ((unit_index = vr_grenade_player_unit()) == NONE)"), "classic"),
    # the release waiting for the tick's last sub-step, not the hand's
    "release-at-the-tick-end": (("if (substep_count <= 1 || (unit_index",
                                 "if (substep_count <= 1 || substep_index != substep_count - 1 || (unit_index"),
                                "throw"),
    # let go of without the head start of one made at that sub-step: behind a 30 Hz throw
    "no-head-start": (("\n\tprojectile_step(projectile_index, 1.0f, 0.0f, TRUE, TRUE);\n\tprojectile_substep_finish",
                       "\n\tprojectile_substep_finish"), "throw"),
    # the head start without its hold: ahead of a 30 Hz throw
    "no-hold": (("\n\tprojectile->projectile.flags |= FLAG(_projectile_substep_held_bit) |",
                 "\n\tprojectile->projectile.flags |= 0 |"), "throw"),
    # the hand's power ignored: harder than the hand threw
    "power-ignored": (("*speed = magnitude3d(game_velocity) * throw_state->power;",
                       "*speed = magnitude3d(game_velocity);"), "throw"),
    # the hand's place not turned with the body
    "hand-not-turned": (("hand->y = eye.y + (throw_state->position[0] * s + throw_state->position[1] * c) * units;",
                         "hand->y = eye.y + throw_state->position[1] * units;"), "throw"),
    # the throw animation left where it was: the body's throw a tick or more behind 30's
    "animation-left": (("\t\tunit->object.animation.state.frame_index = vr_grenade.key_frame_index - 1;",
                        "\t\t;"), "throw"),
}


def generated(fault=None):
    header = read("port/linux/game/sim_rate.h")
    clock = read("port/linux/game/sim_rate.c")
    code = read("source/items/projectiles.c")
    vr = read("port/linux/game/vr_grenade.c")
    vr_header = read("port/linux/src/vr.h")
    units = read("source/units/units.h")

    decls = enum_with(header, "SIM_RATE_CLASSIC") + "\n" + enum_with(header, "NUMBER_OF_SIM_SUBSTEP_SLOTS") + "\n"
    start = header.index("typedef void (*sim_substep_procedure)")
    decls += header[start:header.index(";", start) + 1] + "\n"
    for member in ("_projectile_tracer_bit", "_projectile_substep_head_start_bit", "_projectile_oriented_along_velocity_bit",
                   "_projectile_action_detonate", "_projectile_detonation_timer_starts_immediately",
                   "MAXIMUM_PROJECTILE_COLLISIONS_PER_UPDATE"):
        decls += enum_with(code, member) + "\n"
    decls += structure(read("source/items/projectiles.h"), "_projectile_datum") + "\n"
    decls += enum_with(units, "_unit_grenade_throw_idle") + "\n" + enum_with(units, "_unit_state_throw_grenade") + "\n"
    decls += enum_with(vr_header, "HALO_VR_THROW_NONE") + "\n" + structure(vr_header, "halo_vr_throw") + "\n"

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
    for name in ("projectile_set_action", "projectile_update", "projectile_substep_launch", "projectile_accelerate",
                 "projectiles_substep", "projectile_substep_finish", "projectile_segment_get", "projectile_segment_set",
                 "projectile_set_speed_under_final", "projectile_step"):
        text += function(code, name) + "\n"
    # the VR grenade's state and code
    start = vr.index("static struct\n{\n\t/* the unit whose throw")
    text += vr[start:vr.index("};", vr.index("} vr_grenade =")) + 2] + "\n"
    text += re.search(r"^static void vr_grenade_substep\([^;{]*\);", vr, re.M).group(0) + "\n"
    for name in ("vr_grenade_player_unit", "vr_grenade_hand_throw", "vr_grenade_throw_advance", "vr_grenade_hand_release",
                 "vr_grenade_thrown", "vr_grenade_release", "vr_grenade_substep"):
        text += function(vr, name) + "\n"
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vr_grenade_throw", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("vr_grenade_throw", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
