"""display.scale_scoped_fov (port/linux/include/zoom_fov.h): a scope's zoom at the player's field of view, the Xbox's
zoom levels off, the player's view magnified as much on."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CONTROL = (
    # the half angle scaled linearly, not its tangent: the zoom magnifies the player's view less than the Xbox's
    "return native * (requested / unzoomed);",
    "return tanf(atanf(native) * (atanf(requested) / atanf(unzoomed)));",
    "zoom")


def generated(fault=None):
    text = read("port/linux/include/zoom_fov.h")
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


def test_zoom():
    status, output = run(build("zoom_fov", generated()), "zoom")
    assert status == 0, output


def test_negative_control():
    status, output = run(build("zoom_fov", generated(CONTROL[:2])), CONTROL[2])
    assert status == CHECK_FAILED, output
