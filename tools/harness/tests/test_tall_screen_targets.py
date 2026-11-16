"""Every widget of a tall screen (menu_screen_tall.inc) that does something when A, a click or a tap is on it is one
the pointer can click: ui_widget.c's ui_mouse_widget_is_item notes as a target only a widget with handlers whose
parent is a list stepped through its children (a blocks.xml widget with left_right_tabs_items, up_down_tabs_children
and so on). The server browser's HOST & MORE, made straight on the screen's widget, took its key but no click or tap."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import mutated, read  # noqa: E402

TALL = "port/linux/game/menu_screen_tall.inc"
BLOCKS = "port/assets/menus/screens/blocks.xml"

# a fault in the code, and the widget that must be caught
NEGATIVE_CONTROLS = {
    # the code before the change: the band's button on the screen itself
    "header-on-screen": (("tall_add(tall, tall->header_bar, screens.button_tag",
                          "tall_add(tall, screen, screens.button_tag"), "made"),
    # the table's button on the body's stand-in that is no list (the screen)
    "table-button-on-screen": (("tall->table_bar = tall_add(tall, tall->body, tall_tag(\"bar\")",
                                "tall->table_bar = tall_add(tall, tall->body, tall_tag(\"layer\")"),
                               "tall->table_button.widget"),
}


def assignment(name):
    return re.compile(re.escape(name) + r"\s*=\s*(?:[^;]*?\?\s*)?tall_add\(tall,\s*([^,]+),\s*((?:[^,()]|\([^()]*\))+),")


def tag_names(expression):
    """the blocks a tag expression names: tall_tag("bar"), tall_tag(touch ? "bar" : "prompts"), screens.button_tag"""
    names = re.findall(r'"([^"]+)"', expression)
    member = re.fullmatch(r"\s*screens\.(\w+)_tag\s*", expression)
    return names + ([member.group(1)] if member else [])


def blocks():
    """each block's flags and whether it has handlers"""
    found = {}
    for match in re.finditer(r'<widget name="screens/([^"]+)"([^>]*?)(/?)>', read(BLOCKS)):
        flags = re.search(r'flags="([^"]*)"', match.group(2))
        body = read(BLOCKS)[match.end():read(BLOCKS).find("</widget>", match.end())] if not match.group(3) else ""
        found[match.group(1)] = (flags.group(1).split() if flags else [], "<on " in body)
    return found


def unclickable(source):
    """the targets (their widgets' expressions) the pointer cannot click"""
    known = blocks()
    wrong = []
    for target in re.finditer(r"tall_target\(tall, ([^,]+),", source):
        widget = target.group(1)
        made = list(assignment(widget).finditer(source[:target.start()]))
        assert made, f"{widget}: not made by tall_add"
        parent, tag = made[-1].group(1).strip(), made[-1].group(2)
        # (the widget's handlers, and its parent a list stepped through its children)
        if not all(known.get(name, ([], False))[1] for name in tag_names(tag)):
            wrong.append(widget)
            continue
        parents = list(assignment(parent).finditer(source))
        if not parents:
            wrong.append(widget)
            continue
        for name in (name for made_parent in parents for name in tag_names(made_parent.group(2))):
            if not any("_tabs_" in flag for flag in known.get(name, ([], False))[0]):
                wrong.append(widget)
                break
    return wrong


def test_targets_clickable():
    source = read(TALL)
    assert len(re.findall(r"tall_target\(tall, ", source)) >= 10
    assert unclickable(source) == []


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative_control(fault):
    change, widget = NEGATIVE_CONTROLS[fault]
    assert widget in unclickable(mutated(read(TALL), *change)), fault
