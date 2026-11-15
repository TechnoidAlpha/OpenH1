"""Server Setup's Start is START GAME on the whole screen (the controller audit: on GAME NAME it began renaming the
game, on a setting's row it did nothing, and START GAME was eleven presses down): the screen's own Start handler
starts the game, as its START GAME button's does, and takes the press before the focused row (ui_widget.c and the
menu engine run a widget's handlers before passing what is left to its focused child), so no row of the screen has a
Start of its own, while A still edits each row (tools/port_settings.py's _server_settings)."""

import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import mutated, read  # noqa: E402

SETUP = "port/assets/menus/ce/main_menu.multiplayer_type_select.server_settings.xml"
BEHAVIOUR = "port/assets/menus/model/behaviour/main_menu.multiplayer_type_select.server_settings.xml"
BASE = "main_menu/multiplayer_type_select/server_settings"
START = 'run="ss start game" open="main_menu/multiplayer_type_select/lobby/lobby_screen"'


def handlers(text):
    """each widget's handlers (event: its attributes as written), by name"""
    found = {}
    root = ElementTree.fromstring(text)
    for widget in [*root.iter("widget"), *root.iter("behaviour")]:
        found[widget.get("name")] = {on.get("event"): " ".join(f'{key}="{value}"' for key, value in on.attrib.items()
                                                                if key != "event") for on in widget.iter("on")}
    return found


def faults(text):
    """what is wrong with the screen's Start and A"""
    wrong = []
    widgets = handlers(text)
    screen = widgets.get(f"{BASE}/server_settings_screen", {})
    if screen.get("start") != START:
        wrong.append("the screen's Start does not start the game")
    if widgets.get(f"{BASE}/button_ok", {}).get("start") != START:
        wrong.append("START GAME's Start does not start the game")
    for name, events in widgets.items():
        if re.fullmatch(re.escape(BASE) + r"/op_\w+", name) and "start" in events:
            wrong.append(f"{name}: a Start of its own")
    if widgets.get(f"{BASE}/op_server_name", {}).get("a") != 'run="ss edit server name"':
        wrong.append("A no longer edits GAME NAME")
    return wrong


@pytest.mark.parametrize("path", [SETUP, BEHAVIOUR])
def test_start_starts_the_game(path):
    assert faults(read(path)) == []


# a fault in the files, and what must be caught
NEGATIVE_CONTROLS = {
    # the files before the change: GAME NAME's Start renaming the game, the screen with no Start
    "row-start": (('\t\t<on event="a" run="ss edit server name"/>\n',
                   '\t\t<on event="a" run="ss edit server name"/>\n\t\t<on event="start" run="ss edit server name"/>\n'),
                  "op_server_name: a Start of its own"),
    "screen-without-start": ((f'\t\t<on event="start" {START}/>\n\t\t<on event="b" run="gamespy back handler"/>\n',
                              '\t\t<on event="b" run="gamespy back handler"/>\n'),
                             "the screen's Start does not start the game"),
}


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative_control(fault):
    change, expected = NEGATIVE_CONTROLS[fault]
    assert any(expected in each for each in faults(mutated(read(SETUP), *change))), fault
