"""The gametypes' pictures (menu_functions.c's gametype_picture_frame): each engine's frame in the PC version's
order, which the gametype lists' pictures (bitmaps/game_type_grafix) and Game Options' (rules_options) share; and
the gametype editor's panel (playlist_edit_description), which shows the gametype's own picture on every row, in
place of the PC version's pictures of its rows, which have no redraw."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    defines = "\n".join(re.findall(r"^#define PLAYLIST_EDIT_\w+ \d+$", source, re.M))
    code = "\n".join([enum_with(read("source/game/game_engine.h"), "game_engine_none = 0"),
                      enum_with(source, "_gametype_picture_ctf = 0"), function(source, "gametype_picture_frame"),
                      defines, function(source, "playlist_edit_description"), ""])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", ["frames", "editor"])
def test_case(case):
    status, output = run(build("gametype_pictures", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # (the engine's number as the frame, as the gametype lists had it: Capture the Flag's showed King's crown)
    (("\tswitch (engine)\n", "\treturn (short)(engine < 0 || engine > 5 ? 5 : engine);\n\tswitch (engine)\n"),
     "frames"),
    # (the row's picture left, the PC version's)
    (("\tpicture->animation.current_frame_index = frame == _gametype_picture_unknown ? 0 : frame;\n", ""), "editor"),
    # (an unknown engine's "?" shown)
    (("\tvisible_set(picture, frame != _gametype_picture_unknown);\n", ""), "editor"),
])
def test_negative_control(fault, case):
    status, output = run(build("gametype_pictures", generated(fault)), case)
    assert status == CHECK_FAILED, output


def enum_members(text, first):
    body = enum_with(text, first)
    return re.findall(r"^\s*(_\w+)", body[body.index("{"):], re.M)


def test_frames_are_the_pc_versions_order():
    """the PC version's own order of the pictures (ui_widget_game_data_input_functions.c), member for member"""
    ours = enum_members(read("port/linux/game/menu_functions.c"), "_gametype_picture_ctf = 0")
    pc = enum_members(read("source/interface/ui_widget_game_data_input_functions.c"), "_multiplayer_game_bitmap_ctf = 0")
    assert [name.replace("_gametype_picture_", "") for name in ours] == \
        [name.replace("_multiplayer_game_bitmap_", "") for name in pc]
