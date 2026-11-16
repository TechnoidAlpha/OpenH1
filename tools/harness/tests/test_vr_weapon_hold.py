"""The VR mode's first-person weapon held in the hand (port/linux/game/vr_hands.c's weapon_hold, weapon_root_find and
support_hold, with source/math/matrix_math.c): the game's animations swing the whole weapon and both arms about the
camera (the plasma pistol's charged shot, a vent, a melee, a reload), which on the flat screen reads as recoil and in
the hand flings the weapon out of it; in VR the weapon's root is held where the idle pose has it from the camera (in
the controller, the shots' own kick in the camera), everything moved with it rigidly, so that what moves within the
weapon (a slide) still does. The root is the gun node the weapon's parts hang from, not a part hanging apart nor the
graph's root; the left hand on a long gun's foregrip follows the animation's hand near the grip (a pump) and stays on
the weapon, in the idle pose's grip, as the animation takes it away. No VR runtime: a fake graph and poses."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["root", "held", "support_near", "support_far"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the weapon left where the animation swung it: out of the hand, the arm after it
    "not-held": (("\t\tmatrix4x3_multiply(&move, &node_matrices[node_index], &node_matrices[node_index]);",
                  "\t\t;"), "held"),
    # the move taken the wrong way round (the root's inverse first): not where the idle pose has it
    "move-reversed": (("matrix4x3_multiply(&held, &inverse, &move);", "matrix4x3_multiply(&inverse, &held, &move);"),
                      "held"),
    # the camera's kick ignored: the weapon held from the idle pose's camera at the origin
    "camera-ignored": (("matrix4x3_multiply(camera, &vr_hands.idle[root], &held);",
                        "held = vr_hands.idle[root];"), "held"),
    # the first of the weapon's nodes taken for its root: a part hanging apart
    "first-found": (("\t\tif (count > best_count)\n", "\t\tif (count > best_count * 0 && best == NONE)\n"), "root"),
    # the root of the graph, which the arms' model draws too, taken for the weapon's
    "arms-root": (("\t\tif (!TEST_FLAG(node_models[node_index], _vr_hands_node_weapon_bit) ||\n"
                   "\t\t\tTEST_FLAG(node_models[node_index], _vr_hands_node_hands_bit))",
                   "\t\tif (!TEST_FLAG(node_models[node_index], _vr_hands_node_weapon_bit))"), "root"),
    # the left hand on the animation's whatever it does: off the gun in a reload
    "support-follows": (("\t\tif (away > 0.0f)\n\t\t{\n\t\t\treal_matrix4x3 idle;",
                         "\t\tif (FALSE)\n\t\t{\n\t\t\treal_matrix4x3 idle;"), "support_far"),
    # the left hand on the idle pose's grip always: no pump
    "support-idle": (("\taway = away * away * (3.0f - 2.0f * away);", "\taway = 1.0f;"), "support_near"),
    # the idle pose's grip from the camera, not on the weapon as it is
    "grip-from-camera": (("weapon = animated[root], inverse;", "weapon = vr_hands.idle[root], inverse;"),
                         "support_far"),
}


def generated(fault=None):
    vr = read("port/linux/game/vr_hands.c")
    maths = read("source/math/matrix_math.c")
    real_math = read("source/math/real_math.h")

    nodes = constant(read("source/models/model_animation_definitions.h"), "MAXIMUM_NODES_PER_ANIMATION")
    decls = f"#define MAXIMUM_NODES_PER_ANIMATION {nodes}\n"
    decls += f"#define TAG_STRING_LENGTH {constant(read('source/tag_files/tag_files.h'), 'TAG_STRING_LENGTH')}\n"
    decls += structure(real_math, "real_matrix4x3") + "\ntypedef struct real_matrix4x3 real_matrix4x3;\n"
    for member in ("_vr_hands_node_weapon_bit", "_node_left_hand", "NUMBER_OF_SIDES,"):
        decls += enum_with(vr, member) + "\n"
    decls += structure(vr, "vr_hands_graph_node") + "\n"
    for name in ("SUPPORT_STAYS", "SUPPORT_LEAVES"):
        decls += re.search(r"^#define " + name + r" .*$", vr, re.M).group(0) + "\n"

    text = "".join(function(maths, name) + "\n" for name in ("matrix4x3_inverse", "matrix4x3_multiply"))
    for name in ("vector_dot", "vector_between", "graph_node", "node_descends", "weapon_root_find",
                 "quaternion_from_matrix", "matrix_from_quaternion", "matrix_blend", "weapon_hold", "support_hold"):
        text += function(vr, name) + "\n"
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vr_weapon_hold", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("vr_weapon_hold", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
