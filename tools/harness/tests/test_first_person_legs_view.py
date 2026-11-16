"""The first-person legs seen from the view drawn as the body has them from its own (port/linux/game/
render_interpolation.c's render_interpolation_view_body, #697): the body is drawn between the ticks, with the view
between them, but the view drawn on foot is the newest step's (display.direct_camera), a tick's motion on and aimed
in the frame. Drawn where the body is, the legs trailed the view by that, more or less as each step and each frame's
aim came, and stepped against it walking and strafing. And the body faces the aim only once the aim has turned far
enough from it, then turns after it a tick at a time: turned by the view's own turn alone, the legs swung away from
the view and back with a jerk each tick as it turned (#826); turned to the view's heading, they stay under it."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

# the negative control: the legs left on the body, a tick behind the view drawn
LEFT_ON_THE_BODY = ("\tto = &drawn_views[local_player_index];", "\tto = &blended_views[local_player_index];")
# ... and turned by the view's turn alone, from the view between the ticks' heading: the body's lag kept
TURNED_BY_THE_VIEW = ("render_interpolation_body_heading(unit_index, &from_i, &from_j)",
                      "view_heading(from, &from_i, &from_j)")


def generated(fault=None):
    code = read("port/linux/game/render_interpolation.c")
    text = "\n".join(function(code, name) for name in
                     ["view_heading", "render_interpolation_body_heading", "render_interpolation_view_body"])
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


def test_strafing_legs_keep_to_the_view():
    status, output = run(build("first_person_legs_view", generated()), "strafing")
    assert status == 0, output
    status, output = run(build("first_person_legs_view", generated(LEFT_ON_THE_BODY)), "strafing")
    assert status == CHECK_FAILED, output


def test_turning_legs_stay_under_the_view():
    status, output = run(build("first_person_legs_view", generated()), "turning")
    assert status == 0, output
    status, output = run(build("first_person_legs_view", generated(TURNED_BY_THE_VIEW)), "turning")
    assert status == CHECK_FAILED, output
