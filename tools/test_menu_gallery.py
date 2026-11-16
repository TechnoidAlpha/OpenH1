"""tools/menu_gallery.py's parts that need no game: the screens found in menu files, the pictures'
difference, the layout checks on made-up layouts, the page."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402
import menu_gallery  # noqa: E402

MENUS = """<?xml version="1.0" encoding="UTF-8"?>
<!-- The port's settings screen, in the PC version's style (tools/port_settings.py) -->
<menus root="main_menu/main_menu">
	<widget name="main_menu/main_menu" width="640" height="480">
		<child widget="main_menu/main_menu_list"/>
	</widget>
	<widget name="main_menu/main_menu_list" width="200" height="100">
		<on event="a" open="main_menu/settings/settings_page"/>
	</widget>
	<widget name="main_menu/settings/settings_page" width="640" height="480">
		<child widget="main_menu/settings/held_screen"/>
	</widget>
	<widget name="main_menu/settings/held_screen" width="100" height="20"/>
	<widget name="main_menu/audio/audio_screen" width="640" height="480"/>
	<widget name="main_menu/settings/settings_saving_profile" width="640" height="480"/>
	<widget name="main_menu/vr_display/vr_display_screen" width="640" height="480"/>
</menus>
"""


def test_screens_from_the_menu_files(tmp_path):
    (tmp_path / "main_menu.xml").write_text(MENUS)
    screens = menu_gallery.enumerate_screens(tmp_path)
    # the root, a screen an event opens, a *_screen no widget holds; not a child, a transient one, VR's
    assert [screen.name for screen in screens] == ["main_menu/main_menu", "main_menu/settings/settings_page",
                                                   "main_menu/audio/audio_screen"]
    assert all(screen.generator == "tools/port_settings.py" and screen.file == "main_menu.xml"
               for screen in screens)
    assert screens[2].label == "audio/audio_screen" and screens[2].slug == "main_menu-audio-audio_screen"
    with_vr = menu_gallery.enumerate_screens(tmp_path, vr=True)
    assert "main_menu/vr_display/vr_display_screen" in [screen.name for screen in with_vr]


def test_the_real_menus_have_their_screens():
    screens = {screen.name for screen in menu_gallery.enumerate_screens(menu_gallery.ROOT / menu_gallery.MENUS)}
    assert "main_menu/main_menu" in screens
    assert "main_menu/settings_select/player_setup/player_profile_edit/audio_settings/audio_settings_screen" in screens
    assert len(screens) > 50


def picture(width, height, colour=(0, 0, 0)):
    return capture.Picture(width, height, [bytes(colour) * width for _ in range(height)])


def test_pictures_differ():
    a = picture(100, 100)
    assert menu_gallery.pictures_differ(a, picture(100, 100)) == 0.0
    assert menu_gallery.pictures_differ(a, picture(50, 100)) == 1.0
    b = picture(100, 100)
    # one pixel a little brighter, then one much brighter: under the fraction, the same
    b.rows[5] = bytes([10, 10, 10]) + b.rows[5][3:]
    assert menu_gallery.pictures_differ(a, b) == 0.0
    b.rows[5] = bytes([200, 0, 0]) + b.rows[5][3:]
    assert menu_gallery.pictures_differ(a, b, fraction=0.00001) == 0.0001
    assert menu_gallery.pictures_differ(a, b) == 0.0
    # a whole row changed: a difference
    b.rows[6] = bytes([255, 255, 255]) * 100
    assert menu_gallery.pictures_differ(a, b) > 0.0


def test_subsample():
    a = capture.Picture(4, 4, [bytes(range(row * 12, row * 12 + 12)) for row in range(4)])
    small = menu_gallery.subsample(a, 2)
    assert (small.width, small.height) == (2, 2)
    assert small.rows[0] == bytes([0, 1, 2, 6, 7, 8])
    assert small.rows[1] == bytes([24, 25, 26, 30, 31, 32])


def layout(*items, screen=(-106, 0, 746, 480)):
    widgets = [{"kind": "widget", "id": index, "parent": -1, "type": 1, "focused": False,
                "name": f"pc\\main_menu\\test\\widget_{index}", "box": [0, 0, 640, 480], "clip": None}
               for index in range(10)]
    return {"frame": 1, "prompts": "keyboard", "touch_menus": False, "screen": list(screen),
            "screen_name": "pc\\main_menu\\test", "items": widgets + list(items)}


def text(widget, words, box, extent, clip=(-106, 0, 852, 480), whole=None):
    return {"kind": "text", "widget": widget, "scale": 1.0, "text": words, "box": list(box), "clip": list(clip),
            "extent": list(extent), "whole": list(whole or extent)}


def checks(found):
    return sorted(problem["check"] for problem in found)


KEYBOARD = menu_gallery.VARIANTS["keyboard-16x9"]
CONTROLLER = menu_gallery.VARIANTS["controller-16x10"]
PHONE = menu_gallery.VARIANTS["touch-phone"]


def test_a_clean_layout_passes():
    found = menu_gallery.check_layout(layout(
        text(1, "OUTPUT DEVICE", (-68, 125, 226, 144), (-65, 125, 45, 142)),
        text(2, "SYSTEM DEFAULT", (230, 125, 370, 143), (238, 125, 360, 142)),
        # (the next row's letters a pixel into this one's descent: not an overlap)
        text(3, "3D AUDIO", (-68, 141, 226, 160), (-65, 141, 2, 158)),
        {"kind": "glyph", "widget": 4, "key": True, "name": "Esc", "box": [600, 450, 630, 466]}), KEYBOARD)
    assert found == []


def test_text_cut_by_its_clip_and_past_its_box():
    found = menu_gallery.check_layout(layout(
        text(1, "A LONG LABEL", (0, 100, 100, 120), (0, 100, 140, 118), clip=(0, 0, 120, 480)),
        text(2, "LINES", (0, 200, 100, 220), (0, 200, 90, 218), whole=(0, 200, 90, 260)),
        text(3, "WIDE", (0, 300, 100, 320), (0, 300, 130, 318))), KEYBOARD)
    assert checks(found) == ["clipped", "cut", "overflow"]
    assert "LINES" in [problem for problem in found if problem["check"] == "cut"][0]["message"]


def test_text_over_text_and_off_the_screen():
    found = menu_gallery.check_layout(layout(
        text(1, "FIRST", (0, 100, 200, 120), (0, 100, 120, 118)),
        text(2, "SECOND", (50, 105, 250, 125), (50, 105, 170, 123)),
        text(3, "GONE", (700, 400, 800, 420), (720, 400, 790, 418))), KEYBOARD)
    assert checks(found) == ["offscreen", "overlap"]


def test_the_other_inputs_glyphs():
    controller_button = {"kind": "glyph", "widget": 1, "key": False, "name": "b-button", "box": [0, 0, 20, 20]}
    dpad = {"kind": "glyph", "widget": 1, "key": False, "name": "dpad-up", "box": [30, 0, 50, 20]}
    key = {"kind": "glyph", "widget": 2, "key": True, "name": "Esc", "box": [60, 0, 80, 20]}
    assert checks(menu_gallery.check_layout(layout(controller_button, dpad, key), KEYBOARD)) == ["glyphs"]
    assert checks(menu_gallery.check_layout(layout(controller_button, dpad, key), CONTROLLER)) == ["glyphs"]
    assert checks(menu_gallery.check_layout(layout(controller_button, dpad), CONTROLLER)) == []


def test_the_phones_safe_area_and_touch_targets():
    screen = (-213, 0, 853, 480)
    # 2400x1080: 2.25 pixels a unit; the sides' 162 pixels are 72 units, the foot's 58 are 26
    clip = (-213, 0, 1066, 480)
    found = menu_gallery.check_layout(layout(
        text(1, "NEAR THE NOTCH", (-210, 100, 0, 120), (-205, 100, -100, 118), clip),
        text(2, "INSIDE", (0, 200, 200, 220), (0, 200, 60, 218), clip),
        text(3, "ON THE HOME BAR", (0, 460, 200, 478), (0, 460, 100, 476), clip),
        {"kind": "target", "widget": 2, "target": 0, "box": [0, 200, 400, 220]},
        {"kind": "target", "widget": 2, "target": 0, "box": [0, 230, 400, 290]},
        screen=screen), PHONE)
    assert checks(found) == ["safe-area", "safe-area", "touch-target"]
    touch = [problem for problem in found if problem["check"] == "touch-target"][0]
    assert touch["message"].startswith("1 of 2 touch targets are under 7 mm across, the smallest 2.7 mm")
    # the same on a computer: neither
    assert checks(menu_gallery.check_layout(layout(
        text(1, "NEAR THE NOTCH", (-210, 100, 0, 120), (-205, 100, -100, 118), clip),
        {"kind": "target", "widget": 2, "target": 0, "box": [0, 200, 400, 220]}, screen=screen), CONTROLLER)) == []


def test_the_page(tmp_path):
    screens = [menu_gallery.Screen("main_menu/a_screen", "main_menu.xml", "tools/ce_menus.py"),
               menu_gallery.Screen("main_menu/b_screen", "main_menu.xml", "tools/port_settings.py")]
    variants = ["keyboard-16x9"]
    same = picture(30, 20)
    other = picture(30, 20, (255, 255, 255))
    after = {("main_menu/a_screen", "keyboard-16x9"): {"picture": same, "digest": "1", "opened": True},
             ("main_menu/b_screen", "keyboard-16x9"): {"picture": other, "digest": "2", "opened": True}}
    before = {("main_menu/a_screen", "keyboard-16x9"): {"picture": same, "digest": "1", "opened": True},
              ("main_menu/b_screen", "keyboard-16x9"): {"picture": same, "digest": "3", "opened": True}}
    problem = {"check": "overlap", "message": "\"A\" (x) is drawn over \"B\" (y)", "box": [0, 0, 1, 1]}
    problems = {("main_menu/b_screen", "keyboard-16x9"): [problem]}
    page = menu_gallery.write_page(tmp_path, screens, variants, after, problems).read_text()
    assert page.count('<section class="screen"') == 2 and "tools/port_settings.py" in page
    assert 'name="viewport"' in page and "&quot;A&quot; (x) is drawn over" in page
    page = menu_gallery.write_page(tmp_path, screens, variants, after, problems, before, {}).read_text()
    # only the screen whose pixels differ, before | after, its problem new
    assert page.count('<section class="screen"') == 1 and "b_screen" in page and "1 unchanged" in page
    assert "before/keyboard-16x9/main_menu-b_screen.png" in page and 'class="badge new"' in page


def test_configure_arguments(tmp_path):
    (tmp_path / "build.ninja").write_text("# comment\nconfigure_args = --release --android-ndk $\n"
                                          "    /path/ndk --compiler-launcher ccache\n\nrule x\n")
    assert menu_gallery.configure_arguments(tmp_path) == ["--release", "--android-ndk", "/path/ndk",
                                                          "--compiler-launcher", "ccache"]


def test_remote_arguments_leave_out_its_own():
    assert menu_gallery.remote_arguments(["--remote", "h", "--out", "x", "--screens", "audio", "--data=m",
                                          "--compare", "abc"]) == ["--screens", "audio", "--compare", "abc"]


def test_results_differ_by_what_the_menus_drew():
    words = text(1, "OK", (0, 0, 64, 48), (0, 0, 32, 48))
    shown = layout(words, screen=(0, 0, 64, 48))
    background = picture(64, 48)
    moved = picture(64, 48)
    # the background moved (outside the letters): the same
    moved.rows[40] = bytes([255, 255, 255]) * 64
    before = {"digest": "a", "picture": background, "layout": shown}
    assert not menu_gallery.results_differ(before, {"digest": "b", "picture": moved, "layout": shown})
    # the letters' pixels changed: different
    letters = picture(64, 48)
    letters.rows = [bytes([255, 255, 255]) * 32 + row[96:] for row in letters.rows]
    assert menu_gallery.results_differ(before, {"digest": "c", "picture": letters, "layout": shown})
    # other words: different, whatever the pixels
    other = layout(text(1, "CANCEL", (0, 0, 64, 48), (0, 0, 60, 48)), screen=(0, 0, 64, 48))
    assert menu_gallery.results_differ(before, {"digest": "d", "picture": background, "layout": other})
    # no layout: the whole picture's blocks; a row of pixels is the background's motion, a block is not
    assert not menu_gallery.results_differ({"digest": "a", "picture": background},
                                           {"digest": "b", "picture": moved})
    assert menu_gallery.results_differ({"digest": "a", "picture": background},
                                       {"digest": "c", "picture": letters})


def test_a_change_to_the_menus_alone_needs_no_build():
    assert menu_gallery.menus_only(["port/assets/menus/ce/main_menu.xml", "tools/port_settings.py", "AGENTS.md"])
    assert not menu_gallery.menus_only(["port/assets/menus/ce/main_menu.xml", "port/linux/game/menu_tags.c"])
    assert menu_gallery.menus_only([])


def test_a_line_cut_at_its_right_edge():
    cut = text(1, "SYSTEM DEFAULT", (230, 125, 370, 143), (233, 125, 370, 142))
    cut["wide"] = [233, 125, 420, 142]
    wrapped = text(2, "TWO LINES OF WORDS", (0, 200, 100, 240), (0, 200, 98, 238))
    wrapped["wide"] = [0, 200, 170, 219]
    found = menu_gallery.check_layout(layout(cut, wrapped), KEYBOARD)
    assert checks(found) == ["cut"] and "right edge" in found[0]["message"]


def test_the_tours_setup_opens_real_screens_with_the_games_functions():
    """each screen the tour opens with the game's functions first is one of the menus', and each function one its
    menus run (or the tour's "close keyboard"), as its line names them"""
    screens = {screen.name for screen in menu_gallery.enumerate_screens(menu_gallery.ROOT / menu_gallery.MENUS)}
    runs = set()
    for path in (menu_gallery.ROOT / menu_gallery.MENUS).glob("*.xml"):
        runs.update(re.findall(r' run="([^"]+)"', path.read_text().replace("&amp;", "&")))
    for name, functions in menu_gallery.TOUR_SETUP.items():
        assert name in screens, name
        for function in functions:
            assert function == "close keyboard" or function in runs, function
        assert menu_gallery.tour_line(name).split("\t") == [name, *functions]
    assert menu_gallery.tour_line("main_menu/main_menu") == "main_menu/main_menu"


def test_parity_counts_no_layout_on_either_side_as_not_compared():
    """two runs that drew nothing of a screen are not its parity: it fails, but for a screen the game shows only
    under the keyboard; and a layout of another screen than the one opened is not compared either"""
    import menu_parity
    screen = "main_menu/settings_select/player_setup/player_profile_edit/player_profile_edit_screen"
    assert menu_parity.compare_layouts(screen, None, None)[0].startswith("NOT COMPARED")
    assert menu_parity.compare_layouts(
        "main_menu/settings_select/player_setup/player_profile_edit/name_edit/profile_name_screen", None, None) == []
    assert menu_parity.compare_pictures(None, None)[0].startswith("NOT COMPARED")
    other = {"screen_name": "pc\\main_menu\\main_menu", "items": []}
    assert menu_parity.compare_layouts(screen, other, other, opened=True)[0].startswith("NOT COMPARED")
    own = {"screen_name": "pc\\" + screen.replace("/", "\\"), "items": []}
    assert menu_parity.compare_layouts(screen, own, own, opened=True) == []


def test_parity_tall_layout_is_deliberate_only_for_settings():
    """a Settings screen laid out in the tall layout may differ in its places and sizes, but only with the
    classic's rows drawn, in order (a run of them where it scrolls); any other screen, or other rows, differs"""
    import menu_parity
    base = "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\audio_settings\\"

    def layout(name, rows, y):
        items = [{"kind": "widget", "id": 1, "name": base + "audio_settings_screen", "box": [0, 0, 640, 480]}]
        items += [{"kind": "widget", "id": 2 + index, "parent": 1, "name": base + row, "box": [28, y + 25 * index,
                                                                                              372, y + 25 * index + 24]}
                  for index, row in enumerate(rows)]
        return {"screen_name": name, "screen": [0, 0, 640, 480], "items": items}

    screen = base + "audio_settings_screen"
    classic = layout(screen, ["op_output_device", "op_spatial", "op_reverb"], 124)
    assert menu_parity.is_tall_layout(classic, layout(screen, ["op_output_device", "op_spatial", "op_reverb"], 100))
    assert menu_parity.is_tall_layout(classic, layout(screen, ["op_spatial", "op_reverb"], 80))
    assert menu_parity.compare_layouts(screen.replace("\\", "/")[3:], classic,
                                       layout(screen, ["op_output_device", "op_spatial", "op_reverb"], 100)) == []
    assert not menu_parity.is_tall_layout(classic, layout(screen, ["op_reverb", "op_spatial"], 100))
    assert not menu_parity.is_tall_layout(classic, layout(screen, [], 100))
    assert menu_parity.is_tall_layout(layout(screen, [], 124), layout(screen, [], 100))
    other = "pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen"
    assert not menu_parity.is_tall_layout(layout(other, ["op_a"], 124), layout(other, ["op_a"], 100))
    # (a segmented row's segments on one side, its spinner on the other: whichever fit its row)
    spinner = layout(screen, ["op_spatial"], 124)
    spinner["items"].append({"kind": "widget", "id": 9, "parent": 2, "name": base + "spatial_spinner", "box": [1, 2, 3, 4]})
    segments = layout(screen, ["op_spatial"], 100)
    segments["items"] += [{"kind": "widget", "id": 9 + index, "parent": 2, "name": base + f"spatial_seg{index}",
                           "box": [1, 2, 3, 4]} for index in range(3)]
    assert menu_parity.is_tall_layout(spinner, segments)
    assert menu_parity.engine_presses(["down", "click@ok"], "classic") == ["down", menu_parity.CLICKS["ok"][0]]
    assert menu_parity.engine_presses(["click@ok"], "new") == [menu_parity.CLICKS["ok"][1]]
