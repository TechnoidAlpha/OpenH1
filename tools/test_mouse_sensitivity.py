"""The mouse's turn in Counter-Strike 2's units (port/linux/include/mouse_sensitivity.h): its arithmetic in numbers
(port/linux/tests/mouse_sensitivity_test.c), and that the game uses it for the mouse alone: the platform layer's mouse
turns by it, each event's counts at once, and the zoom scales the mouse by input.zoom_sensitivity while the sticks,
the touch controls and VR keep the game's scaling."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_mouse_sensitivity_numbers(tmp_path):
    """N counts at sensitivity S turn N * 0.022 * S degrees; zoomed, CS2's and the game's scaling; a stick and the
    mouse in one tick each by its own"""
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "mouse_sensitivity_test"
    # (the header alone: port/linux/include stands in for the C library's headers)
    shutil.copy(ROOT / "port" / "linux" / "include" / "mouse_sensitivity.h", tmp_path)
    build = subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-O1", f"-I{tmp_path}",
                            str(ROOT / "port" / "linux" / "tests" / "mouse_sensitivity_test.c"), "-lm", "-o",
                            str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "PASS" in result.stdout, result.stdout + result.stderr


def test_mouse_alone_uses_it():
    """the mouse's look (xinput_sdl.c) turns its counts by mouse_radians_per_count, with nothing of the frame's time
    or the field of view; player_control.c's sticks and touch keep the game's one over the magnification, and only
    the mouse's turn takes mouse_zoom_look_scale"""
    xinput = (ROOT / "port/linux/src/xinput_sdl.c").read_text()
    look = xinput[xinput.index("int halo_linux_mouse_look("):]
    look = look[:look.index("\n}\n")]
    assert "radians = mouse_radians_per_count(mouse_sensitivity());" in look
    assert "*yaw = -x * radians;" in look and "* radians * vertical_sensitivity;" in look
    assert not re.search(r"time|fov|field_of_view|scale", look.split("radians = ")[1])
    control = (ROOT / "source/game/player_control.c").read_text()
    scale_look = control[control.index("static void player_control_scale_look("):]
    scale_look = scale_look[:scale_look.index("\n}\n")]
    assert "1.f / unit_get_zoom_magnification(" in scale_look and "mouse" not in scale_look
    mouse = control[control.index("if (halo_linux_mouse_look(gamepad_index, &mouse_yaw, &mouse_pitch))"):]
    mouse = mouse[:mouse.index("input->facing_delta.pitch += mouse_pitch;")]
    assert "mouse_zoom_look_scale(" in mouse and "unit_get_zoom_magnification" not in mouse
    # (the mouse's turn is added after the sticks' and touch's is made, on its own)
    assert control.index("player_control_scale_look(player->unit_index, control->zoom_level, &look_delta);") < \
        control.index("if (halo_linux_mouse_look(gamepad_index, &mouse_yaw, &mouse_pitch))")
    fov = (ROOT / "port/linux/game/view_fov.c").read_text()
    assert 'strcmp(config_string("input.zoom_sensitivity"), "halo")' in fov


def test_menu_sliders_match_their_rows():
    """menu_functions.c's settings_sliders and its track's layout are tools/port_settings.py's Slider rows', and the
    slider's functions are ones menu_tags.c names"""
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings

    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    table = functions[functions.index("} const settings_sliders[] ="):]
    table = table[:table.index("};")]
    # (a slider's stops as their words, or numbers, and values: a number, or
    # the setting's word a stop writes)
    def stops_of(choices):
        return tuple((shown, value if value.isalpha() else float(value)) for shown, value in choices)

    rows = {(f"op_{row[1].split('.', 1)[1]}", row[1], row[2]._replace(stops=stops_of(row[2].stops)))
            for page in port_settings.PAGES.values() for row in page["rows"]
            if row[0] != port_settings.SECTION and isinstance(row[2], port_settings.Slider)}
    assert rows
    stops = {name: tuple((word or f"{float(value):g}", text or float(value))
                         for value, word, text in re.findall(r'\{ (-?[\d.]+), (?:"(\w+)"|NULL)(?:, "(\w+)")? \}',
                                                             body))
             for name, body in re.findall(r"settings_slider_stop const (\w+)\[\] =\n\{\n(.*?)\n\};", functions, re.S)}
    found = set()
    for row, setting, numbers, whole, stops_name in re.findall(
            r'\{ "(\w+)", "([\w.]+)", ([-\d., ]+?), (TRUE|FALSE),\s+(?:NULL, 0|SLIDER_STOPS\((\w+)\)) \}', table):
        found.add((row, setting, port_settings.Slider(*map(float, numbers.split(", ")), whole=whole == "TRUE",
                                                      stops=stops[stops_name] if stops_name else ())))
    assert found == rows
    for name in ("SLIDER_TRACK_WIDTH", "SLIDER_THUMB_WIDTH"):
        assert re.search(rf"#define {name} {getattr(port_settings, name)}\b", functions), name
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    for name in ("left", "right", "edit"):
        assert f'"port slider {name}"' in tags
