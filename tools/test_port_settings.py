"""The settings pages' rows fit above the footer on every platform
(tools/port_settings.py's PAGES, packed as menu_functions.c's
settings_rows_pack packs them)."""
import pytest

from tools import port_settings
from tools.port_settings import (PAGES, PAGE_TOP, SECTION, SETTINGS_ROW_HEIGHT,
                                 SETTINGS_ROW_STEP, SETTINGS_ROWS_BOTTOM, SETTINGS_SAME_PLACE, SETTINGS_SECTION_GAP,
                                 SETTINGS_SECTION_STEP, SETTINGS_TOP)

# the platforms each build's menus are for (menu_files.c's for_this_platform),
# every one the build may have at once: the desktop's (the VR build's in the
# headset, on a renderer that draws HDR), Android's, iOS's (desktop's menus
# but not "computer"'s, for a touchscreen)
BUILDS = {
    "desktop": {"desktop", "computer", "pointer", "hdr", "vr"},
    "android": {"android", "touch", "hdr"},
    "ios": {"desktop", "touch", "hdr"},
}


def _key(entry):
    label, setting, _choices, _help, _platform, *named = entry
    return named[0] if named else setting.split(".", 1)[1] if setting else None


def _profile_row(entry):
    """a row settings_row_profile hides with no profile"""
    return (entry[1] or "").startswith("profile.") or _key(entry) in ("name", "color")


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
