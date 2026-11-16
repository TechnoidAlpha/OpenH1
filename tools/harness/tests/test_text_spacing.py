"""The high-res text's letter spacing (issues 744 and 754): a run of text (a line's characters up to a tab, an icon or
a change of style: draw_string.c's draw_string_port_run) set with the font's own advances and kerning, every letter as much closer
or further apart, its ink where the font tag's ink is (rasterizer_text.c's hires_text_run_lay_out), for the game's
unicode strings and its byte strings; a run the font cannot draw all of centred glyph by glyph, as before.
tools/test_text_spacing.py measures every string of the menus and the maps the same way (it needs the maps)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["even", "placed", "bytes", "tab", "lacking"]

TEXT = "source/rasterizer/rasterizer_text.c"
DRAW = "source/text/draw_string.c"

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # each glyph's advance centred on its character's, as the high-res text drew before issue 744
    "centred": (("\t\t\treturn hires_text_run.offsets[current];",
                 "\t\t\treturn (font_character->character_width - advance) * 0.5f;"), "even"),
    # the font's spacing, untracked: the run's ink wider or narrower than the tag's
    "untracked": (("real offset = font_pens[index] + (index - first) * tracking - tag_pens[index] +",
                   "real offset = font_pens[index] - tag_pens[index] +"), "placed"),
    # a word at a time (each word tracked on its own): its letters spaced unlike the next word's
    "words": (("return character == 0 || character == '\\t'", "return character == 0 || character == ' ' || character == '\\t'"),
              "even"),
    # the font's kerning left out
    "unkerned": (("font_pen += text_hires_kerning(hires_font, characters[previous], characters[index]);",
                  "font_pen += 0.0f;"), "even"),
    # the run's characters of a byte string taken from its start, not its line's
    "bytes-from-start": (("\t\t\tindex = draw_string_port_line.first;", "\t\t\tindex = 0;"), "bytes"),
    # a tab not ending a run
    "tab": (("return character == 0 || character == '\\t'", "return character == 0"), "tab"),
    # a run with a glyph the font lacks laid out as the others
    "lacking": (("\t\tif (advance < 0.0f)\n\t\t\treturn;", "\t\tif (advance < 0.0f)\n\t\t\tadvance = 5.0f;"), "lacking"),
}


def static_struct(source, name):
    """A static structure's definition and its initializer ("static struct { ... } name = ...;")."""
    match = re.search(r"^\} " + re.escape(name) + r" = [^;]*;", source, re.M)
    if not match:
        raise LookupError(f"static structure not found in the sources: {name}")
    return source[source.rindex("static struct\n{", 0, match.start()):match.end()]


def generated(fault=None):
    draw = read(DRAW)
    text = read(TEXT)
    enum = re.search(r"^enum\n\{\n\tMAXIMUM_PORT_RUN_CHARACTERS = \d+,\n\};", draw, re.M).group(0)
    code = "\n\n".join([
        enum,
        static_struct(draw, "draw_string_port_line"),
        function(draw, "draw_string_port_line_begin"),
        function(draw, "draw_string_port_run_break"),
        function(draw, "draw_string_port_run"),
        function(text, "font_character_ink_span"),
        static_struct(text, "hires_text_run"),
        function(text, "hires_text_run_lay_out"),
        function(text, "hires_text_glyph_offset"),
    ])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("text_spacing", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("text_spacing", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
