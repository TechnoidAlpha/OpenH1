"""The shapes the menus draw at the display's resolution (port/linux/game/menu_shapes.c): a rounded box (the key caps:
issue 596's corners a pixel past the caps' sides) has nothing past its sides, its corners' blended edge inside the
radius; a picture with its corners cut round (the campaign's mission pictures, issue 687: a picture of what was round
their corners laid over them let them show through) is textured over the whole of it, its corners cut and blended to
nothing. The widgets with a radius draw so (source/interface/ui_widget.c), and the campaign's pictures have one."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, menu_layout, mutated, read, run  # noqa: E402

SOURCE = "port/linux/game/menu_shapes.c"
CASES = ["inside", "texture", "cut"]
FUNCTIONS = ("menu_shape_rectangle", "menu_shape_corner", "menu_shape_rounded", "menu_shape_pixel_edge",
             "menu_shape_rounded_picture")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the curves' blended edge outside the radius, as before: a pixel past the straight sides
    "edge-outside": (("radius - 1.0f, radius, rim, clear, segments);", "radius, radius + 1.0f, rim, clear, segments);"),
                     "inside"),
    # the picture's corners drawn white: only its middle textured
    "corners-untextured": (("menu_shape_corner(white, texture, pixels, center_x, center_y, sides[corner][0], "
                            "sides[corner][1],\n\t\t\t0.0f, solid, fill, fill, segments);",
                            "menu_shape_corner(white, NULL, pixels, center_x, center_y, sides[corner][0], "
                            "sides[corner][1],\n\t\t\t0.0f, solid, fill, fill, segments);"), "texture"),
    # the picture square: no radius
    "picture-square": (("\t\tfloorf(radius * pixels + 0.5f), 0.0f, color, 0);", "\t\t0.0f, 0.0f, color, 0);"),
                       "cut"),
}


def generated(fault=None):
    source = read(SOURCE)
    structure = re.search(r"^struct menu_shape_texture\n\{.*?^\};", source, re.M | re.S).group(0)
    code = structure + "\n\n" + "\n\n".join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_shapes", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("menu_shapes", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_campaign_pictures_are_cut_round():
    """Every picture of a mission (CONTINUE's, the panel's, the cards', a Custom Edition map's) is cut round, and no
    picture of corners is laid over one (issue 687)."""
    widgets = menu_layout.widgets()
    port_settings = menu_layout.port_settings
    pictures = [name for name, widget in widgets.items()
                if re.search(r"/(art|mp_map_grafix)", widget.attributes.get("bitmap", "")) and
                name.startswith(port_settings.CAMPAIGN) and "art_back" not in widget.attributes.get("bitmap", "")]
    pictures += [name for name, widget in widgets.items() if name.startswith(port_settings.CAMPAIGN) and
                 widget.attributes.get("bitmap") == port_settings.CAMPAIGN_CUSTOM_PICTURES]
    assert len(set(pictures)) >= 5, pictures
    for name in set(pictures):
        assert widgets[name].attributes.get("radius") == str(port_settings.ART_CORNER_RADIUS), name
    assert not [name for name in widgets if "corners" in name], "a picture of corners laid over a mission's"
