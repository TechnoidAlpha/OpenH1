"""The high-res HUD's placeholders, asked after for each texture bound, are found through their filter exactly when
registered (the menus' bitmaps, asked after with them, are found through menu_files.c's own hashed slots: a stand-in
here, tools/test_menu_art.py's).

    python tools/harness/tests/test_texture_lookup_filter.py     # the cases, then how fast with and without filters
"""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["found", "replaced", "forgotten"]

# a fault in a filter, and the case that must catch it
NEGATIVE_CONTROLS = {
    "placeholders-not-filtered-in": (("hud", "\t\t\tplaceholder_filter[bit >> 3] |=", "\t\t\t(void)"), "found"),
    "placeholders-not-filtered-again": (("hud", "\t}\n\tplaceholder_filter_build();\n}", "\t}\n}"), "replaced"),
    "filter-bit-mismatch": (("hud", "\tunsigned long bit = placeholder_filter_bit(data);\n\tlong index;",
                             "\tunsigned long bit = placeholder_filter_bit(data) ^ 1;\n\tlong index;"), "found"),
}

# the filters' checks taken out: the lookups as they were (for the benchmark)
UNFILTERED = [("hud", "\tif (!(placeholder_filter[bit >> 3] & 1 << (bit & 7)))\n\t\treturn 0;\n", "")]

# the menus' bitmaps as registered, found by a walk (the model the HUD's filter is checked beside)
MENU_STAND_IN = r"""
static unsigned long menu_art[MAXIMUM_ART];
static long menu_art_count;
static void art_register(void const *texture, char const *png)
{
	unsigned long data = ((unsigned long const *)texture)[1];
	long index;

	(void)png;
	for (index = 0; index < menu_art_count && menu_art[index] != data; index++)
		;
	if (index == menu_art_count && menu_art_count < MAXIMUM_ART)
		menu_art[menu_art_count++] = data;
}
static void art_forget(void) { menu_art_count = 0; }
static unsigned int menu_art_texture(unsigned long data, unsigned long *levels)
{
	long index;

	for (index = 0; data && index < menu_art_count; index++)
	{
		if (menu_art[index] == data)
		{
			*levels = 1;
			return (unsigned int)index + 1;
		}
	}
	return 0;
}
"""


def first_function(source, *names):
    """The first of the names the source defines (the render thread's builds run the registrations on it)."""
    for name in names:
        try:
            return name, function(source, name)
        except LookupError:
            pass
    raise LookupError(f"none of these functions is in the sources: {names}")


def under_test(faults=()):
    hud = read("port/linux/src/hud_hires.c").replace("\r\n", "\n")
    for where, before, after in faults:
        hud = mutated(hud, before, after)
    text = MENU_STAND_IN
    text += hud[hud.index("#define MAXIMUM_PLACEHOLDERS"):hud.index("/* the sequences")]
    register, code = first_function(hud, "run_register_placeholder", "hud_hires_register_placeholder")
    text += code + "\n"
    if register == "run_register_placeholder":
        text += "#define placeholder_register(asset, texture) run_register_placeholder(asset, (texture) != NULL, (texture) ? (texture)[1] : 0)\n"
    else:
        text += "#define placeholder_register hud_hires_register_placeholder\n"
    text += function(hud, "hud_hires_placeholder_texture") + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("texture_lookup_filter", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("texture_lookup_filter", under_test((fault,))), case)
    assert status == CHECK_FAILED, f"the filters with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    status = pytest.main(["-q", __file__])
    print("without filters:", run(build("texture_lookup_filter", under_test(tuple(UNFILTERED))), "benchmark")[1])
    print("with filters:   ", run(build("texture_lookup_filter", under_test()), "benchmark")[1])
    sys.exit(status)
