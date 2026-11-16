"""The port's screens' rows, buttons and prompts hold what is in them (issues 669 and 670; harness/menu_fit.py): in
each list of rows (Server Setup's, VR's, the setup options') no row over the next, its height its pitch's
(port_settings.option_row_height); every one-line text on or in a framed part (a button, a row, a prompt, the
lobby's players' rows and controls) and every spinner's value in such a row with its capitals on its middle
(port_settings.caps_text_y); every footer prompt's key cap and words inside its picture (port_settings.
prompt_fit_width); Choose Mission's arrows as far from its cards at any width, NOT YET BEATEN in its box, the
missions' objectives on the lines shown (issue 681); the port's questions' messages and buttons in their box, the
buttons' focus rings clear of it (issue 696); the pause boxes' footers' middle their legends' (issue 596) at the
campaign pause menu's spacing (issue 690); the map lists' chooser's words and arrows on its middle, its arrows in from
its ends, and CONTINUE's picture in the middle of its item (issue 687); the map lists' description laid out across its
box, centred, its words wrapped to it (issue 704). The files as shipped, and the generator's output with a fault, which
each check must find."""

import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import menu_fit, menu_layout  # noqa: E402

port_settings = menu_layout.port_settings
_PAUSE_BOX_SVG = port_settings.pause_box_svg
CHECKS = {"rows": menu_fit.rows_overlapping, "centred": menu_fit.texts_off_middle,
          "prompts": menu_fit.prompt_overflows, "missions": menu_fit.mission_overflows,
          "dialogs": menu_fit.dialog_overflows, "pause": menu_fit.pause_box_misfits,
          "chooser": menu_fit.chooser_off_middle, "continue": menu_fit.continue_art_off_middle,
          "description": menu_fit.map_description_misfits}


def generated():
    """the generator's files, as it writes them now, over the shipped files' other widgets"""
    files = {**port_settings.settings_files(), **port_settings.multiplayer_files(), **port_settings.campaign_files()}
    roots = [ElementTree.fromstring("\n".join(lines).split("?>", 1)[-1]) for lines in files.values()]
    ours = menu_layout.widgets_of(roots)
    return {**menu_layout.widgets(), **ours}, sorted(ours)


@pytest.mark.parametrize("check", CHECKS)
def test_shipped(check):
    problems = CHECKS[check](menu_layout.widgets(), menu_fit.port_names())
    assert not problems, "\n".join(problems)


@pytest.mark.parametrize("check", CHECKS)
def test_generated(check):
    """the generator's output is what is shipped, and passes too"""
    widgets, names = generated()
    assert set(names) == set(menu_fit.port_names())
    problems = CHECKS[check](widgets, names)
    assert not problems, "\n".join(problems)


def test_server_setup_value_column():
    """Server Setup's values (the game's name, typed, as the spinners' values) in one column, in large_ui; the
    invite link, long, in small_ui"""
    every = menu_layout.widgets()
    base = f"{port_settings.MT}/server_settings"
    places = {}
    for row in ("server_name", "max_players", "game_mode", "listing", "password", "invite"):
        for child, x, _, _ in every[f"{base}/op_{row}"].children:
            if child.endswith(("_value", "_spinner")):
                attributes = every[child].attributes
                places[row] = (x + int(attributes.get("left", 0)), int(attributes["width"]), attributes["font"])
    assert len(places) == 6, places
    assert len({(x, width) for x, width, _ in places.values()}) == 1, places
    assert {font for row, (*_, font) in places.items() if row != "invite"} == {"ui\\large_ui"}


# a fault in the generator, and the check that must find it
NEGATIVE_CONTROLS = {
    # the rows as tall as before, whatever their pitch (issue 669)
    "rows-28-tall": ("option_row_height", lambda pitch: 28, "rows"),
    # the rows' picture as tall as the pitch: no room between them
    "rows-no-gap": ("OPTION_ROW_GAP", -1, "rows"),
    # capitals placed without the ascent (text_y as a box's top): low in every box (issue 670)
    "text-y-without-ascent": ("caps_text_y", lambda height, top=0, large=False: (height - 10) // 2 - top, "centred"),
    # the prompts as wide as laid out by hand (issue 670's ADD PLAYER)
    "prompts-not-fitted": ("prompt_fit_width", lambda words, width, text_x, browser=False, lingering=False: width,
                           "prompts"),
    # a key's cap measured without its gap
    "cap-without-gap": ("KEY_CAP_GAP", 0, "prompts"),
    # Choose Mission's arrows at the screen's margin, moved to a wide screen's edges, as before (issue 681)
    "arrows-at-margin": ("mission_arrow_place", lambda side, tall=False: (24, -48) if side == "left" else (592, 48),
                         "missions"),
    # NOT YET BEATEN in the name's column, as before: too narrow on a card beside the chosen one
    "unbeaten-in-text-column": ("MISSION_UNBEATEN_INSET", 10, "missions"),
    # the objective on one line, as before: Keyes's and The Library's cut short
    "objective-one-line": ("MISSION_OBJECTIVE_LINES", 1, "missions"),
    # a question's message wider than the box, past its edges (issue 696's long mission name past them)
    "message-past-box": ("DIALOG_MESSAGE", (176, 194, 269, 94), "dialogs"),
    # the buttons' pill to their picture's edges, as the redraw's focus ring was (issue 696: the ring on the box)
    "button-pill-to-edges": ("TEXT_BUTTON_PILL", (0, 0, 128, 22), "dialogs"),
    # the pause box's legends where the Xbox's 159-high box had them, not in its footer's middle (issue 596)
    "legends-off-footer": ("PAUSE_BOX_LEGENDS_BOTTOM", 26, "pause"),
    # the footer's line in its band, as before: the band's middle under the legends' (issue 596)
    "footer-line-in-band": ("pause_box_svg", lambda piece, height, header=False: _PAUSE_BOX_SVG(piece, height, header)
                            .replace(f'y="{height - 28 - port_settings.PAUSE_BOX_DIVIDER:g}"', f'y="{height - 28}"'),
                            "pause"),
    # the chooser's words where they were, high in their row (issue 687)
    "chooser-words-high": ("MAP_KIND_SPINNER", (9, 2, 238), "chooser"),
    # the chooser's arrows near the row's ends, as before (issue 687)
    "chooser-arrows-at-ends": ("MAP_KIND_ARROW_INSET", 3, "chooser"),
    # CONTINUE's picture against the item's top, as before (issue 687)
    "continue-art-high": ("CONTINUE_ART_WIDTH", 50, "continue"),
    # the map lists' description in the PC version's 146-unit column at the box's left, as before (issue 704)
    "description-pc-column": ("map_desc_column", lambda: (439, 146, 87, 396), "description"),
}


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control, monkeypatch):
    name, value, check = NEGATIVE_CONTROLS[control]
    monkeypatch.setattr(port_settings, name, value)
    monkeypatch.setattr(port_settings, "_PROMPT_WIDTHS", set())
    widgets, names = generated()
    if control == "cap-without-gap":
        # (the files laid out without it, checked with it)
        monkeypatch.setattr(port_settings, "KEY_CAP_GAP", 3)
    assert CHECKS[check](widgets, names), f"the generator with a fault ({control}) passed '{check}'"
