"""The game's name in its menus: what a player reads in port/assets/menus (text, labels, helps, notes, spinners'
words) says GOAT H1 (#1031), never OpenH1 nor OpenCE for this game. OpenCE is named only as the other game, whose
players may join (OPENCE CAN JOIN, its netcode)."""
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT  # noqa: E402

MENUS = ROOT / "port/assets/menus"
# the attributes a player reads (a widget's or a look's text, a spinner's items, a tab's label, a section's title,
# a choice's words); the others are names, sources and actions
SHOWN = {"text", "strings", "items", "label", "title", "shown"}
NAMES = re.compile(r"OpenH1|OPENH1|Open H1|OpenCE|OPENCE")
# OpenCE as the other game: its players, its netcode
OTHER_GAME = re.compile(r"OpenCE players|OpenCE's (own )?netcode|OPENCE CAN JOIN|ALLOW OPENCE PLAYERS")


def wrong_names(text):
    """the names in the text that are not the game's (OpenCE as the other game left out)"""
    return NAMES.findall(OTHER_GAME.sub("", text))


def menu_texts():
    """(file, what a player reads) of every menu file"""
    for path in sorted(MENUS.rglob("*.xml")):
        root = ElementTree.fromstring(path.read_bytes())
        for element in root.iter():
            for key, value in element.attrib.items():
                if key in SHOWN:
                    yield path.relative_to(ROOT).as_posix(), value
            if element.text and element.text.strip():
                yield path.relative_to(ROOT).as_posix(), element.text


def test_menus_say_goat_h1():
    found = [(path, text) for path, text in menu_texts() if wrong_names(text)]
    assert not found, found


def test_gametype_editor_row_is_goat_h1():
    """the row of the edit gametype screen (and its screen's header) that was OPENCE OPTIONS"""
    texts = [text for path, text in menu_texts() if path.endswith("opence_options_edit.port.xml")]
    assert "GOAT H1 OPTIONS" in texts and "OPENCE OPTIONS" not in texts


@pytest.mark.parametrize("text", ["OPENCE OPTIONS", "Choose the rules of OpenCE's gametypes", "STANDARD|OPENCE|CUSTOM",
                                  "This server runs OpenH1.", "OPENH1 30 TICK"])
def test_negative_control(text):
    """the check finds the old names where they named this game"""
    assert wrong_names(text)


def test_other_game_is_allowed():
    assert not wrong_names("GOAT H1 players at 60 ticks, OpenCE players at 30.")
    assert not wrong_names("Legacy games are OpenCE's netcode.")
