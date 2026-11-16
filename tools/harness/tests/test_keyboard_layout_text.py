"""The keyboard's layout types the characters (port/linux/src/sdl_platform.c's ascii_from_key, #726)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCES = "port/linux/src/sdl_platform.c"
CASES = ["nordic_shifted", "altgr", "us", "no_character", "beyond_ascii", "ctrl"]


def generated(fault=None):
    code = function(read(SOURCES), "ascii_from_key")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_keyboard_layout_text(case):
    status, output = run(build("keyboard_layout_text", generated()), case)
    assert status == 0, output


TYPED = "\tif (typed >= ' ' && typed < 0x7f)\n\t\treturn (CHAR)typed;\n"


@pytest.mark.parametrize("fault,case", [
    # a US keyboard's shift of the unshifted key, as before (#726)
    ((TYPED, ""), "nordic_shifted"),
    ((TYPED, ""), "altgr"),
])
def test_negative_control(fault, case):
    status, output = run(build("keyboard_layout_text", generated(fault)), case)
    assert status == CHECK_FAILED, output
