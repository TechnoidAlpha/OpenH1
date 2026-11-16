"""The legends under a pause menu (source/interface/ui_widget.c's ui_widget_port_legend_row, issue 596): how much
wider than the Xbox's pictures and words the keyboard's caps and words are, which the pause box is made wider by
(port/linux/game/menu_tags.c's pause_box_widen), so that the row keeps the Xbox's gaps and the room either side of it;
the row given that room laid out at those gaps, centred where the Xbox's was; without it, the gaps narrowed to fit."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCE = "source/interface/ui_widget.c"
CASES = ["growth", "room", "narrowed"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the room the box was made wider by left out: the gaps narrowed in a box wide enough for them (issue 596's
    # "[Esc] = CANCEL [Enter] = SELECT" squeezed)
    "room-without-growth": (("2 * UI_WIDGET_PORT_LEGEND_MARGIN + extra_room);", "2 * UI_WIDGET_PORT_LEGEND_MARGIN);"),
                            "room"),
    # the growth measured at the narrowed gaps: the box made too narrow for them
    "growth-at-least-gaps": (("\treturn (short)MAX(x - (old_right - old_left), 0);",
                              "\treturn (short)MAX(x - gap_total - (old_right - old_left), 0);"), "growth"),
    # the row from the Xbox's left, not centred where its row was
    "row-from-left": (("start = (short)(old_left - (laid - (old_right - old_left)) / 2);", "start = old_left;"),
                      "room"),
}


def generated(fault=None):
    source = read(SOURCE)
    definitions = "\n".join(re.findall(r"^#define UI_WIDGET_PORT_LEGEND\w* [^\n]+$", source, re.M))
    structure = re.search(r"^struct ui_widget_port_legend\n\{.*?^\};", source, re.M | re.S).group(0)
    code = definitions + "\n\n" + structure + "\n\n" + function(source, "ui_widget_port_legend_row")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("legend_row", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("legend_row", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_pause_box_layout_is_the_generators():
    """The pause boxes' layout in menu_tags.c (the rows' place and pitch, the legends', the frames' first number of
    rows) is the one tools/port_settings.py draws their pictures for (pause_box_height), the campaign's pause menu's
    spacing in the multiplayer one too (issue 690)."""
    from harness import constant, menu_layout
    port_settings = menu_layout.port_settings
    tags = read("port/linux/game/menu_tags.c")
    assert constant(tags, "PAUSE_BOX_ROWS_TOP") == port_settings.PAUSE_BOX_ROWS_TOP
    assert constant(tags, "PAUSE_CAMPAIGN_ROW_PITCH") == port_settings.PAUSE_BOX_ROW_PITCH
    assert constant(tags, "PAUSE_BOX_ROW_HEIGHT") == port_settings.PAUSE_BOX_ROW_HEIGHT
    assert constant(tags, "PAUSE_BOX_LEGENDS_GAP") == port_settings.PAUSE_BOX_LEGENDS_GAP
    assert constant(tags, "PAUSE_BOX_LEGENDS_BOTTOM") == port_settings.PAUSE_BOX_LEGENDS_BOTTOM
    assert constant(tags, "PAUSE_BOX_FIRST_ROWS") == port_settings.PAUSE_BOX_BUTTONS[0]
    assert port_settings.PAUSE_BOX_BUTTONS == list(range(port_settings.PAUSE_BOX_BUTTONS[0],
                                                         port_settings.PAUSE_BOX_BUTTONS[0] +
                                                         len(port_settings.PAUSE_BOX_BUTTONS)))
    # (the Xbox's campaign box of four rows at the Xbox's 28 apart is the stock 159: the layout's measures are its)
    assert port_settings.pause_box_height(4) - 3 * (port_settings.PAUSE_BOX_ROW_PITCH - 28) == \
        constant(tags, "PAUSE_BOX_STOCK_HEIGHT")
    patch = function(tags, "pause_patch")
    assert "rows[row].vertical_offset = (short)(row * PAUSE_CAMPAIGN_ROW_PITCH);" in patch
    assert "children[list_child].vertical_offset = (short)(top + PAUSE_BOX_ROWS_TOP);" in patch
    assert "children[child].vertical_offset = (short)(top + height - PAUSE_BOX_LEGENDS_BOTTOM);" in patch
