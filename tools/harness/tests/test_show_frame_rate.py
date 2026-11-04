"""Settings > Interface's SHOW FRAME RATE (display.show_frame_rate, #512): a setting off by default and kept in
config.toml, a row of the Interface page, read again as it changes (main.c's main_show_frame_rate_setting) and drawn
by main_framerate_render as the console's display_framerate is."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

NEGATIVE_CONTROLS = {
    # read once, as the game started
    "read-once": ("\tif (read_at != config_changes())", "\tif (read_at == (unsigned long)-1)"),
}


def generated(fault=None):
    code = function(read("source/main/main.c"), "main_show_frame_rate_setting")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault])
    return (("under_test.inc", code + "\n"),)


def test_follows():
    status, output = run(build("show_frame_rate", generated()), "follows")
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("show_frame_rate", generated(control)), "follows")
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_setting_row_and_drawing():
    """off by default, with its environment variable; a row of the Interface page (the generated file has it); the
    frame rate drawn for it as for the console's variable"""
    config = read("port/linux/src/port_config.c")
    assert re.search(r'\{ "display.show_frame_rate", _config_boolean, "false", "HALO_SHOW_FRAME_RATE"', config)
    page = read("port/assets/menus/ce/main_menu.settings_select.player_setup.player_profile_edit."
                "interface_settings.xml")
    assert 'setting="display.show_frame_rate" values="true|false"' in page
    assert "SHOW FRAME RATE" in page
    render = function(read("source/main/main.c"), "main_framerate_render")
    assert "if (display_framerate || main_show_frame_rate_setting())" in render
    assert "| `display.show_frame_rate` | `false` |" in read("port/linux/README.md")
