"""Every widget of a tall screen (menu_screen_tall.inc) that does something when A, a click or a tap is on it is one
the D-pad and the stick reach, and A presses (issue 1065): the engine steps the focus through the screen's body (its
children, up and down) and through the lists in it (a bar's buttons, the tabs, the titles: left and right), so each
such widget must be in the body, or in a list stepped through its items that is (blocks.xml's *_tabs_* flags), and its
block must have an A handler. The server browser's HOST & MORE, in a bar of its own on the screen itself, out of the
body, took its key, a click and a tap but never the D-pad's focus, so a controller could not select it."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import mutated, read  # noqa: E402

TALL = "port/linux/game/menu_screen_tall.inc"
BLOCKS = "port/assets/menus/screens/blocks.xml"
BODY = "tall->body"

# a fault in the code, and the widget that must be caught
NEGATIVE_CONTROLS = {
    # the code before the change: the band's button's bar on the screen itself, out of the body
    "header-out-of-body": (("tall->header_bar = tall_add(tall, tall->body, tall_tag(\"bar\")",
                            "tall->header_bar = tall_add(tall, screen, tall_tag(\"bar\")"), "made"),
    # the panel's buttons' bars in the layer that takes no focus
    "panel-bar-in-layer": (("made_panel->bars[made] = tall_add(tall, tall->body,",
                            "made_panel->bars[made] = tall_add(tall, tall->owner,"), "button->widget"),
    # the bar of prompts in a block whose children the d-pad does not step
    "bar-not-stepped": (("tall->bar = tall_add(tall, tall->body, tall_tag(touch ? \"bar\" : \"prompts\")",
                         "tall->bar = tall_add(tall, tall->body, tall_tag(touch ? \"layer\" : \"layer\")"),
                        "prompt->widget"),
}


def assignment(name):
    return re.compile(re.escape(name) + r"\s*=\s*(?:[^;]*?\?\s*)?tall_add\(tall,\s*([^,]+),\s*((?:[^,()]|\([^()]*\))+),")


def tag_names(expression):
    """the blocks a tag expression names: tall_tag("bar"), tall_tag(touch ? "bar" : "prompts"), screens.button_tag"""
    names = re.findall(r'"([^"]+)"', expression)
    member = re.fullmatch(r"\s*screens\.(\w+)_tag\s*", expression)
    return names + ([member.group(1)] if member else [])


def blocks(text=None):
    """each block's flags and its handlers' events"""
    text = text if text is not None else read(BLOCKS)
    found = {}
    for match in re.finditer(r'<widget name="screens/([^"]+)"([^>]*?)(/?)>', text):
        flags = re.search(r'flags="([^"]*)"', match.group(2))
        body = text[match.end():text.find("</widget>", match.end())] if not match.group(3) else ""
        found[match.group(1)] = (flags.group(1).split() if flags else [], set(re.findall(r'<on event="(\w+)"', body)))
    return found


def reached(source, parent, known, seen=()):
    """whether a widget made in the parent (an expression) is one the d-pad's steps reach: the body, or a list in it
    (and so on up) that steps through its items or children"""
    parent = parent.strip()
    if parent == BODY:
        return True
    made = list(assignment(parent).finditer(source))
    if not made or parent in seen:
        return False
    for each in made:
        if not all(any("_tabs_" in flag for flag in known.get(name, ([], set()))[0]) for name in tag_names(each.group(2))):
            return False
        if not reached(source, each.group(1), known, seen + (parent,)):
            return False
    return True


def unreachable(source, blocks_text=None):
    """the targets (their widgets' expressions) the d-pad cannot reach, or A cannot press"""
    known = blocks(blocks_text)
    wrong = []
    for target in re.finditer(r"tall_target\(tall, ([^,]+),", source):
        widget = target.group(1)
        made = list(assignment(widget).finditer(source[:target.start()]))
        assert made, f"{widget}: not made by tall_add"
        parent, tag = made[-1].group(1), made[-1].group(2)
        if not all("a" in known.get(name, ([], set()))[1] for name in tag_names(tag)) or \
                not reached(source, parent, known):
            wrong.append(widget)
    return wrong


def test_targets_reached_by_the_dpad():
    source = read(TALL)
    assert len(re.findall(r"tall_target\(tall, ", source)) >= 10
    assert unreachable(source) == []


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative_control(fault):
    change, widget = NEGATIVE_CONTROLS[fault]
    assert widget in unreachable(mutated(read(TALL), *change)), fault


def test_negative_control_no_a_handler():
    """a block without an A handler is caught: the band's button's"""
    assert "a" in blocks()["button"][1]
    faulty = mutated(read(BLOCKS), '<widget name="screens/button" type="text" width="148" height="24" '
                     'bitmap="main_menu/parts/button_148x24" font="ui\\small_ui" color="#FFFFFFFF" text_x="8" '
                     'text_y="3">\n\t\t<on event="a" run="port screen do"/>\n',
                     '<widget name="screens/button" type="text" width="148" height="24" '
                     'bitmap="main_menu/parts/button_148x24" font="ui\\small_ui" color="#FFFFFFFF" text_x="8" '
                     'text_y="3">\n')
    assert "made" in unreachable(read(TALL), faulty)
