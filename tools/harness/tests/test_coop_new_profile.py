"""Co-op's cards with no profile (port/linux/game/menu_screen.c's screen_actions, screens/coop.xml): a card that
asks for a new profile's name first ("new game if no plyr profiles", its other screen) goes on, once the profile is
made, to the screen it opens with a profile: HOST to Choose Mission, JOIN to the co-op list. JOIN used Select
Profile's (pm_empty_creating_profile), which came back to co-op's screen, JOIN to be chosen again. The screen after the
name is the menus' widget of that name (ce/*.xml, the classic engine's) and its behaviour (model/behaviour/*.xml, the
new engine's): their "deleted" handler makes the profile and opens the next screen."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read  # noqa: E402

MENUS = ROOT / "port/assets/menus"
NEW_PROFILE = "new game if no plyr profiles"

# a fault, in the file it is in, that the check must catch
NEGATIVE_CONTROLS = {
    # JOIN's name asked by Select Profile's screen, which comes back to co-op's screen
    "join-back-to-coop": ("port/linux/game/menu_screen.c",
                          '"main_menu/campaign/coop_join_creating_profile" },',
                          '"main_menu/profile_manager/pm_empty_creating_profile" },'),
    # the new engine's behaviour making the profile and opening nothing
    "behaviour-opens-nothing": ("port/assets/menus/model/behaviour/main_menu.campaign.xml",
                                '<on event="deleted" run="new campaign decision" open="screens/coop_join"/>',
                                '<on event="deleted" run="new campaign decision"/>'),
    # the classic engine's widget going back to co-op's screen
    "widget-elsewhere": ("port/assets/menus/ce/main_menu.campaign.xml",
                         '<on event="deleted" run="new campaign decision" open="screens/coop_join"/>',
                         '<on event="deleted" run="new campaign decision" open="screens/coop"/>'),
}


def actions(source):
    """screen_actions' rows by name: before, run, then, open, otherwise (None for NULL)"""
    table = re.search(r"static struct screen_action const screen_actions\[\] =\n\{\n(.*?)\n\};", source, re.S).group(1)
    rows = {}
    for row in re.finditer(r'^\t\{ ("[^"]+"(?:,\s*(?:NULL|"[^"]*"))*)', table, re.M):
        fields = [None if field == "NULL" else field.strip('"')
                  for field in re.findall(r'NULL|"[^"]*"', row.group(1))]
        rows[fields[0]] = dict(zip(("tab", "before", "run", "then", "open", "otherwise"), fields[1:] + [None] * 6))
    return rows


def deleted_handler(files, element, name):
    """the "deleted" handler of the element (widget, behaviour) of the name in the files' texts: (run, open)"""
    for text in files:
        found = re.search(r"<" + element + r' name="' + re.escape(name) + r'"[^>]*>(.*?)</' + element + ">", text,
                          re.S)
        if found:
            handler = re.search(r'<on event="deleted"([^>]*)/>', found.group(1))
            if not handler:
                return None, None
            run = re.search(r'run="([^"]*)"', handler.group(1))
            opened = re.search(r'open="([^"]*)"', handler.group(1))
            return run and run.group(1), opened and opened.group(1)
    raise AssertionError(f"no {element} {name}")


def problems(texts):
    """what keeps a co-op card from going on after a new profile, from the files' texts (by path)"""
    rows = actions(texts["port/linux/game/menu_screen.c"])
    cards = re.findall(r'<card [^>]*do="([^"]+)"', texts["port/assets/menus/screens/coop.xml"])
    widgets = [text for path, text in texts.items() if path.startswith("port/assets/menus/ce/")]
    behaviours = [text for path, text in texts.items() if path.startswith("port/assets/menus/model/behaviour/")]
    found = []
    checked = 0
    for card in cards:
        action = rows[card]
        if action["run"] != NEW_PROFILE:
            continue
        checked += 1
        if not action["open"] or not action["otherwise"]:
            found.append(f"{card}: no screen with a profile, or none for the name")
            continue
        for element, files in (("widget", widgets), ("behaviour", behaviours)):
            run, opened = deleted_handler(files, element, action["otherwise"])
            if run != "new campaign decision" or opened != action["open"]:
                found.append(f"{card}: {action['otherwise']}'s {element} runs {run!r} and opens {opened!r}, "
                              f"not the profile made and {action['open']}")
    if checked < 2:
        found.append(f"only {checked} of co-op's cards ask for a profile (HOST and JOIN do)")
    return found


def texts():
    paths = ["port/linux/game/menu_screen.c", "port/assets/menus/screens/coop.xml"]
    paths += [str(path.relative_to(ROOT)) for folder in ("ce", "model/behaviour")
              for path in sorted((MENUS / folder).glob("*.xml"))]
    return {path: read(path) for path in paths}


def test_coop_cards_go_on_after_new_profile():
    assert problems(texts()) == []


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative(fault):
    path, before, after = NEGATIVE_CONTROLS[fault]
    faulty = texts()
    faulty[path] = mutated(faulty[path], before, after)
    assert problems(faulty), fault
