"""The settings pages' rows fit above the footer on every platform
(tools/port_settings.py's PAGES, packed as menu_functions.c's
settings_rows_pack packs them)."""
import re
from pathlib import Path

import pytest

from tools import port_settings
from tools.port_settings import (PAGES, PAGE_TOP, SECTION, SETTINGS_NOT_IN_VR, SETTINGS_ROW_HEIGHT,
                                 SETTINGS_ROW_STEP, SETTINGS_ROWS_BOTTOM, SETTINGS_SAME_PLACE, SETTINGS_SECTION_GAP,
                                 SETTINGS_SECTION_STEP, SETTINGS_TOP)

# the platforms each build's menus are for (menu_files.c's for_this_platform),
# every one the build may have at once: the desktop's (the VR build's in the
# headset, on a renderer that draws HDR; and out of it), Android's, iOS's
# (desktop's menus but not "computer"'s, for a touchscreen)
BUILDS = {
    "desktop": {"desktop", "computer", "pointer", "hdr", "vr"},
    "desktop out of VR": {"desktop", "computer", "pointer", "hdr"},
    "android": {"android", "touch", "hdr"},
    "ios": {"desktop", "touch", "hdr"},
}


def _key(entry):
    label, setting, _choices, _help, _platform, *named = entry
    return named[0] if named else setting.split(".", 1)[1] if setting else None


def _profile_row(entry):
    """a row settings_row_profile hides with no profile"""
    return (entry[1] or "").startswith("profile.") or _key(entry) in ("name", "color", "profiles")


def _rows_bottom(page, platforms, profile):
    """where the last row shown ends, the rows packed as settings_rows_pack
    packs them: a step apart, a section's title over its rows (none for a
    section with no row shown), a gap above a section after rows"""
    y = SETTINGS_TOP if "tab" in page else PAGE_TOP
    bottom, rows_above, section, previous_shown = y, False, False, False
    for entry in page["rows"]:
        if entry[0] == SECTION:
            section, previous_shown = True, False
            continue
        shown = entry[4] is None or bool(set(entry[4].split()) & platforms)
        if _key(entry) == "create_profile":
            shown = shown and not profile
        elif not profile and _profile_row(entry):
            shown = False
        if "vr" in platforms and _key(entry) in SETTINGS_NOT_IN_VR:
            shown = False
        # (one of the rows in the same place at most: Resolution's or Window
        # Size, the desktop's anti-aliasing or Android's)
        if shown and previous_shown and (_key(entry) in SETTINGS_SAME_PLACE or entry[1] == "display.window_size"):
            continue
        previous_shown = shown
        if not shown:
            continue
        if section:
            y += (SETTINGS_SECTION_GAP if rows_above else 0) + SETTINGS_SECTION_STEP
            section = False
        bottom = y + SETTINGS_ROW_HEIGHT
        y += SETTINGS_ROW_STEP
        rows_above = True
    return bottom


@pytest.mark.parametrize("folder", sorted(PAGES))
def test_settings_rows_fit(folder):
    """no page's rows reach the footer's band, which they would be drawn
    over (the list does not scroll), for any build, with a profile or
    without"""
    page = PAGES[folder]
    for build, platforms in BUILDS.items():
        for profile in (True, False):
            bottom = _rows_bottom(page, platforms, profile)
            assert bottom <= SETTINGS_ROWS_BOTTOM, \
                f"{folder}'s rows end at {bottom} on {build}{'' if profile else ' with no profile'}, past " \
                f"{SETTINGS_ROWS_BOTTOM}: move a row to a page with room"


def test_rows_bottom_counts_a_page_that_overflows():
    """the check sees an overflow: a tab of many rows"""
    page = {"tab": "TEST", "rows": [(SECTION, "A")] + [(f"R{n}", f"x.r{n}", port_settings.ON_OFF, "", None)
                                                        for n in range(15)]}
    assert _rows_bottom(page, BUILDS["desktop"], True) == SETTINGS_TOP + SETTINGS_SECTION_STEP + \
        14 * SETTINGS_ROW_STEP + SETTINGS_ROW_HEIGHT
    assert _rows_bottom(page, BUILDS["desktop"], True) > SETTINGS_ROWS_BOTTOM


def _main_menu_items():
    """the main menu's items (the headset's VR TUTORIAL left out), in their
    order, with their places"""
    menu = (Path(__file__).resolve().parent.parent / "port/assets/menus/ce/main_menu.xml").read_text()
    select = menu.split('<widget name="main_menu/main_menu_select_list"', 1)[1].split("</widget>", 1)[0]
    return [(name, int(x), int(y)) for name, x, y, platform in
            re.findall(r'<child widget="main_menu/main_menu_item_(\w+)" x="(\d+)" y="(\d+)"( platform="\w+")?/>',
                       select) if not platform]


def test_main_menu_items():
    """the main menu is CAMPAIGN, MULTIPLAYER, SETTINGS and QUIT (the
    profiles and the mods are Settings' PROFILES tab's), a row apart with no
    gap"""
    items = _main_menu_items()
    assert [name for name, _, _ in items] == ["load_camp", "multiplayer", "settings", "quit_game"]
    assert {after[2] - before[2] for before, after in zip(items, items[1:])} == {36}
    assert items[2][1:] == port_settings.SETTINGS_ITEM_PLACE
    assert items[3][1:] == port_settings.QUIT_ITEM_PLACE


def test_profiles_tab_opens_profiles_and_mods():
    """the PROFILES tab opens the profile manager (the main menu's Profile's
    was) and the Mods screen, beside its own rows"""
    page = PAGES["profile_settings"]
    assert page["tab"] == "PROFILES"
    links = {_key(entry): entry[2] for entry in page["rows"] if entry[0] != SECTION and not entry[1]}
    assert links["profiles"] == port_settings.PROFILE_MANAGER
    assert links["mods"] == f"{port_settings.MODS}/mods_screen"
    assert {"name", "color", "create_profile", "about"} <= set(links)
