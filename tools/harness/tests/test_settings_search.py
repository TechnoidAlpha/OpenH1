"""Settings' search, the menu engine's (port/linux/game/menu_search.c): what is typed finds the rows of every tab
and page of the real model (port/assets/menus/model/settings.xml, written by tools/port_settings.py), by their labels,
the other words they are found by (tools/port_settings.py's SETTINGS_SEARCH_WORDS), their choices' words and their
help; the labels' first, a whole word before the start of one before a part of one; no row by a word it has not."""

import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, build, constant, mutated, read, run, structure  # noqa: E402

# a fault in the code, and the query whose check must catch it
NEGATIVE_CONTROLS = {
    # a label's words found no better than a help's: VSYNC no longer first for "vsync"
    "label-as-help": ("MENU_SEARCH_LABEL_WORD = 100,", "MENU_SEARCH_LABEL_WORD = 20,", "vsync"),
    # the other words (SETTINGS_SEARCH_WORDS) not read: "fov" finds FIELD OF VIEW alone
    "no-other-words": ("if (row->search && (match = search_words_match(row->search, typed)) != 0)",
                       "if (0 && row->search && (match = search_words_match(row->search, typed)) != 0)", "fov"),
    # a help read for a word of two letters: "of" finds every row whose help has "of"
    "short-help": ("MENU_SEARCH_LEAST_HELP = 3,", "MENU_SEARCH_LEAST_HELP = 1,", "of"),
    # the start of a word as good as the whole word: SYNC's part of VSYNC equal to FRAME RATE & SYNC's word
    "start-as-word": ("return match == 2 ? MENU_SEARCH_LABEL_WORD : MENU_SEARCH_LABEL_START;",
                      "return MENU_SEARCH_LABEL_WORD;", "frame"),
    # the choices not read: "borderless" finds nothing
    "no-choices": ("if (row->search_choices && (match = search_words_match(row->search_choices, typed)) != 0)",
                   "if (0)", "borderless"),
    # one word typed enough: "frame rate xyzzy" finds the frame rate's rows
    "any-word": ("\t\tif (!found)\n\t\t\treturn 0;\n", "", "every-word"),
}

MODEL = ROOT / "port/assets/menus/model/settings.xml"
TEXTS = ("kind", "key", "setting", "label", "title", "when", "open", "run", "enabled", "crumb", "search",
         "search_choices")
FLAGS = ("same_place", "not_in_vr", "named", "status")


def c_string(text):
    if text is None:
        return "NULL"
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def model_data():
    """the model's screens and rows as C initializers (the reader's fields, menu_model.c's)"""
    screens, rows = [], []
    for screen in ElementTree.parse(MODEL).getroot().iter("screen"):
        first = len(rows)
        for element in screen:
            if element.tag not in ("row", "section", "button"):
                continue
            fields = {name: element.get(name) for name in TEXTS}
            if element.tag != "row":
                fields["kind"] = element.tag
            help_element, note_element = element.find("help"), element.find("note")
            fields["help"] = element.get("help") if help_element is None else help_element.text
            fields["note"] = None if note_element is None else note_element.text
            inits = [f".{name} = {c_string(value)}" for name, value in fields.items()]
            inits += [f".{name} = 1" for name in FLAGS if element.get(name) == "true"]
            rows.append("\t{ " + ", ".join(inits) + " },")
        about = screen.find("about")
        screens.append("\t{ " + ", ".join([f".name = {c_string(screen.get('name'))}",
                                            f".kind = {c_string(screen.get('kind'))}",
                                            f".layout = {c_string(screen.get('layout'))}",
                                            f".about = {c_string(None if about is None else about.text)}",
                                            f".first_row = {first}", f".row_count = {len(rows) - first}"]) + " },")
    return ("static struct halo_menu_model_screen test_screens[] =\n{\n" + "\n".join(screens) + "\n};\n"
            "static struct halo_menu_model_row test_rows[] =\n{\n" + "\n".join(rows) + "\n};\n")


def generated(fault=None):
    header = read("port/linux/include/halo_menus.h")
    # (and the model's sizes its structures have: the most geometries)
    sizes = "\n".join(re.findall(r"^#define HALO_MENU_MODEL_\w+ \d+$", header, flags=re.M))
    structures = sizes + "\nstruct halo_menu_handler;\n" + "\n".join(
        structure(header, name) for name in ("halo_menu_model_choice", "halo_menu_model_row",
                                             "halo_menu_model_screen", "halo_menu_model_geometry",
                                             "halo_menu_model_place", "halo_menu_model_look",
                                             "halo_menu_model_behaviour", "halo_menu_model"))
    search_header = read("port/linux/game/menu_search.h")
    code = read("port/linux/game/menu_search.c")
    under_test = re.sub(r'^#include [^\n]*\n', "", search_header + "\n" + code, flags=re.M)
    if fault:
        before, after, _ = NEGATIVE_CONTROLS[fault]
        under_test = mutated(under_test, before, after)
    return (("model.inc", structures), ("under_test.inc", under_test), ("model_data.inc", model_data()))


def search(text, fault=None):
    """[(key, score, crumb)] the search finds for what is typed, and how many match in all"""
    status, output = run(build("settings_search", generated(fault)), f"query:{text}")
    assert status == 0, output
    lines = output.splitlines()
    matched = int(lines[-1].split()[1])
    return [tuple(line.split(" ", 2)) if line.count(" ") >= 2 else (line.split()[0], line.split()[1], "")
            for line in lines[:-1]], matched


def keys(found):
    return [key for key, _, _ in found[0]]


def check(name, fault=None):
    """a case's checks: AssertionError where one fails"""
    if name == "fov":
        found = search("fov", fault)
        assert keys(found)[:1] == ["fov"] and found[0][0][2] == "VIDEO › DISPLAY", found
        # (FIELD OF VIEW by its other word, and the first-person pages' fields of view by theirs: no other row)
        assert set(keys(found)) == {"fov", "vehicle_fov", "scale_scoped_fov", "viewmodel_fov"}, found
    elif name == "vsync":
        found = search("vsync", fault)
        assert keys(found)[:1] == ["vsync"], found
        assert found[0][0][2] == "VIDEO › FRAME RATE & SYNC", found
        assert int(found[0][0][1]) > max(int(score) for _, score, _ in found[0][1:]), found
    elif name == "case":
        assert search("VSync", fault) == search("vsync", fault)
        assert search("  vsync ", fault) == search("vsync", fault)
    elif name == "borderless":
        # (WINDOW by its choice BORDERLESS, and its help; BUTTON LAYOUT by its choice FISHSTICKS alone)
        assert keys(search("borderless", fault))[:1] == ["mode"]
        assert keys(search("fishsticks", fault)) == ["button_preset"]
    elif name == "sens":
        found = keys(search("sens", fault))
        for key in ("mouse_sensitivity", "mouse_vertical_sensitivity", "zoom_sensitivity", "look_sensitivity",
                    "gyro_sensitivity"):
            assert key in found, (key, found)
    elif name == "frame":
        # (a whole word of a label before a part of one: FRAME RATE & SYNC's SYNC before VSYNC's; before the
        # start of one: HDR MODE before MODEL DETAIL, though a label that starts with the word is a little more)
        found = keys(search("sync", fault))
        assert found.index("frame_rate_sync") < found.index("vsync"), found
        found = keys(search("mode", fault))
        assert found.index("hdr_mode") < found.index("model_detail"), found
    elif name == "every-word":
        found = keys(search("frame rate", fault))
        assert found[:2] == ["frame_rate_sync", "show_frame_rate"], found
        assert search("frame rate xyzzy", fault) == ([], 0)
    elif name == "of":
        # (a word of two letters finds labels' and choices' words alone, no help's: FIELD OF VIEW's, KING OF THE
        # HILL's)
        for key, _, _ in search("of", fault)[0]:
            row = next(row for row in ElementTree.parse(MODEL).getroot().iter("row") if row.get("key") == key)
            words = f"{row.get('label')} {row.get('search_choices') or ''}"
            assert re.search(r"\b(OF|of)\b", words), (key, words)
    elif name == "nothing":
        assert search("xyzzy", fault) == ([], 0)
        assert search("radar", fault) == ([], 0)
        assert search("", fault) == ([], 0)
    elif name == "kinds":
        # (Key Bindings' actions, the status rows and the labels the game writes are no rows to find: a Discord
        # account's status, Button Remapping's actions; the hint rows neither)
        found = keys(search("discord", fault))
        assert "discord_account" not in found and "link_status" not in found, found
        assert not any(key.startswith(("command_", "pad_", "hint_", "link_master_")) for key in found), found
    elif name == "hint":
        status, output = run(build("settings_search", generated(fault)), "hint:radar")
        assert status == 0 and "INDICATOR OPTIONS" in output, output
        status, output = run(build("settings_search", generated(fault)), "hint:vsync")
        assert status == 0 and output == "none", output
    else:
        raise LookupError(name)


CASES = ["fov", "vsync", "case", "borderless", "sens", "frame", "every-word", "of", "nothing", "kinds", "hint"]


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    check(case)


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative_control(fault):
    """the code with a deliberate fault must fail its case"""
    with pytest.raises(AssertionError):
        check(NEGATIVE_CONTROLS[fault][2], fault)


def test_geometry():
    """the most rows the engine keeps room for: the model's at most (menu_engine.c's ENGINE_SEARCH_ROWS)"""
    engine = read("port/linux/game/menu_engine.c")
    geometry = ElementTree.parse(MODEL).getroot().find("geometry")
    assert int(geometry.get("search_results")) <= constant(engine, "ENGINE_SEARCH_ROWS")
    assert int(geometry.get("search_crumbs")) <= constant(engine, "ENGINE_SEARCH_GROUPS")
