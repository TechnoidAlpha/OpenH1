"""Settings > Video > Frame Rate & Sync's SHOW FRAME RATE (display.show_frame_rate, #512): a setting off by default
and kept in config.toml, a row of that page, read again as it changes (main.c's main_show_frame_rate_setting) and drawn
by main_framerate_render as the console's display_framerate is."""

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
