"""A game engine goal's waypoint (a flag, the ball, a target: game_engine.c) drawn over its object between the ticks.
At 60 and 120 the goal is set in a tick's work and the object's sub-steps move it on before the frames draw it; the
goal's place is held to the object as it was when the goal was set, so that it is drawn over the object as the frame
draws it, not slid back by how far the object went since (the waypoint then stepped back through each tick and
jumped at the next)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

FAULT = ("\t\tmatrix4x3_transform_point(&drawn[0], &global_goal_object_points[goal_index], position);",
         "\t\trender_interpolation_object_point(object_index, position);")


def generated(fault=None):
    code = read("source/game/game_engine.c")
    decls = structure(code, "netgame_goal") + "\n"
    decls += "static struct netgame_goal global_goal[MAX(32, MULTIPLAYER_MAXIMUM_PLAYERS)];\n"
    decls += "static long global_goal_objects[NUMBEROF(global_goal)];\n"
    decls += "static real_point3d global_goal_object_points[NUMBEROF(global_goal)];\n"
    assert "static real_point3d global_goal_object_points[NUMBEROF(global_goal)];" in code
    text = function(code, "game_engine_set_goal_object") + "\n" + function(code, "game_engine_get_goal_drawn_position")
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text + "\n"))


def test_held_to_its_object():
    status, output = run(build("goal_drawn", generated()), "held_to_its_object")
    assert status == 0, output


def test_negative_control():
    # moved with the object from its newest nodes: slid back by the sub-steps run since the goal was set
    status, output = run(build("goal_drawn", generated(FAULT)), "held_to_its_object")
    assert status == CHECK_FAILED, f"the old way passed: the test cannot see it\n{output}"
