"""The menus' pictures drawn again from the player's maps (port/linux/src/menu_pictures.c): the scale a screen asks for,
the cache's decision (drawn, rendered again, or the original kept for a missing or modded map and failed renders), the
preparing pass's order and the pictures it counts (none with a missing or modded map; none at all, no pass), retries
(a render noted before it begins) and a pass resumed after a failure or a crash, its notes, the colour curve, the picture cut from a frame and laid out as the original frame is, the game's start's
phases (the intro movie, then the pass with the game to itself, then the main menu: issue 550)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["scale", "fallback", "cached", "failures", "order", "makeable", "retry", "resume", "notes", "colour", "cut", "frame", "budget", "give_up", "startup", "whole"]

SOURCE = "port/linux/src/menu_pictures.c"
FUNCTIONS = ("menu_picture_scale_for_height", "menu_picture_decide", "menu_picture_order", "menu_picture_note_begin",
             "menu_picture_note_end", "menu_picture_note_give_up", "menu_picture_overdue", "menu_picture_note_parse",
             "menu_picture_note_format", "picture_sorted_at", "picture_quantile", "menu_picture_colour_curve",
             "menu_picture_cut", "menu_picture_frame", "menu_pictures_startup_next",
             "menu_pictures_startup_holds_main_menu", "menu_pictures_phase_exclusive",
             "picture_be32", "menu_picture_png_whole")

# a fault in menu_pictures.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "scale-rounded-down": (("+ units - 1) / units", ") / units"), "menu_picture_scale_for_height", "scale"),
    "no-largest-scale": (("if (scale > MENU_PICTURE_LARGEST_SCALE)", "if (0)"), "menu_picture_scale_for_height",
                         "scale"),
    "modded-map-rendered": (("if (!made_for)\n\t\treturn 0;", "if (!made_for)\n\t\t;"), "menu_picture_decide", "fallback"),
    "first-checksum-only": (("index < pose->checksum_count &&", "index < 1 &&"), "menu_picture_decide", "fallback"),
    "small-picture-kept": (("note->scale < wanted_scale", "note->scale < 0"), "menu_picture_decide", "cached"),
    "another-pose-drawn": (("note->pose_hash != pose->hash || ", ""), "menu_picture_decide", "cached"),
    "another-map-drawn": ((" || note->map_checksum != map_checksum", ""), "menu_picture_decide", "cached"),
    "failures-forever": (("note->failures < MENU_PICTURE_FAILURES", "1"), "menu_picture_decide", "failures"),
    "larger-retried-forever": (("(note->scale < wanted_scale ? retry : 0)", "(note->scale < wanted_scale ? 2UL : 0)"),
                               "menu_picture_decide", "retry"),
    "maps-not-together": (("if (seen)\n\t\t\tcontinue;", "if (0)\n\t\t\tcontinue;"), "menu_picture_order", "order"),
    "unwanted-ordered": (("if (!(decisions[first] & (1UL << _menu_picture_render_bit)))\n\t\t\tcontinue;",
                          "if (0)\n\t\t\tcontinue;"), "menu_picture_order", "order"),
    "begin-not-counted": (("note->failures++;", ";"), "menu_picture_note_begin", "retry"),
    "begin-keeps-other-map": (("|| note->map_checksum != map_checksum)\n\t{", "&& 0)\n\t{"), "menu_picture_note_begin",
                              "retry"),
    "rendered-keeps-failures": (("note->failures = 0;", ";"), "menu_picture_note_end", "retry"),
    "larger-failure-hides-picture": (("if (note->result != _menu_picture_rendered)\n", "if (1)\n"),
                                     "menu_picture_note_end", "retry"),
    "modded-note-rendered": (("case _menu_picture_modded:\n\t\treturn 0;", "case _menu_picture_modded:"),
                             "menu_picture_decide", "failures"),
    "any-version-read": (("version != MENU_PICTURES_VERSION", "version < 0"), "menu_picture_note_parse", "notes"),
    "map-not-needed": (("have |= 2;", "have |= 0;"), "menu_picture_note_parse", "notes"),
    "failures-not-written": (("failures %ld", "falures %ld"), "menu_picture_note_format", "notes"),
    "curve-averaged-wrong": (("smoothed[at] = sum / SMOOTH;", "smoothed[at] = sum / (SMOOTH + 2);"),
                             "menu_picture_colour_curve", "colour"),
    "curve-from-reference-red": (("reference[index * 4 + channel]", "reference[index * 4]"),
                                 "menu_picture_colour_curve", "colour"),
    "cut-from-the-left": (("left = (frame_width - cut_width) / 2;", "left = 0;"), "menu_picture_cut", "cut"),
    "cut-from-the-top": (("top = (frame_height - cut_height) / 2;", "top = 0;"), "menu_picture_cut", "cut"),
    "red-and-blue-swapped": (("sum[0] += row[column * 4 + 2] * part;", "sum[0] += row[column * 4 + 0] * part;"),
                             "menu_picture_cut", "cut"),
    "no-picture-budget": (("now - picture_began > MENU_PICTURE_TIME_BUDGET || ", ""), "menu_picture_overdue", "budget"),
    "no-pass-budget": ((" || now - pass_began > MENU_PICTURES_TIME_BUDGET", ""), "menu_picture_overdue", "budget"),
    "give-up-retried": (("note->failures = MENU_PICTURE_FAILURES;", ";"), "menu_picture_note_give_up", "give_up"),
    "give-up-hides-picture": (("if (note->result != _menu_picture_rendered)\n\t\tnote->result = _menu_picture_failed;",
                               "note->result = _menu_picture_failed;"), "menu_picture_note_give_up", "give_up"),
    "pass-during-movie": (("main_menu_loaded && !movie_open ?", "main_menu_loaded ?"), "menu_pictures_startup_next",
                          "startup"),
    "pass-before-main-menu": (("main_menu_loaded && !movie_open ?", "!movie_open ?"), "menu_pictures_startup_next",
                              "startup"),
    "main-menu-during-pass": (("return pass_exclusive ? phase :", "return 0 ? phase :"), "menu_pictures_startup_next",
                              "startup"),
    "start-begins-again": (("\tdefault:\n\t\treturn _menu_pictures_startup_done;", "\tdefault:\n\t\treturn 0;"),
                           "menu_pictures_startup_next", "startup"),
    "menus-during-pass": ((" || (phase == _menu_pictures_startup_pictures && pass_exclusive)", ""),
                          "menu_pictures_startup_holds_main_menu", "startup"),
    "menus-during-intro": (("return phase == _menu_pictures_startup_intro ||", "return"),
                           "menu_pictures_startup_holds_main_menu", "startup"),
    "end-menu-held": (("phase == _menu_pictures_startup_pictures && pass_exclusive",
                       "phase == _menu_pictures_startup_pictures"), "menu_pictures_startup_holds_main_menu", "startup"),
    "movie-during-pass": (("return running && !ended;", "return 0;"), "menu_pictures_phase_exclusive", "startup"),
    "ended-pass-keeps-game": (("return running && !ended;", "return running;"), "menu_pictures_phase_exclusive",
                              "startup"),
    "cut-short-drawn": (("memcmp(tail + tail_size - 12, end, sizeof(end))", "memcmp(tail + tail_size - 12, end, 0)"),
                        "menu_picture_png_whole", "whole"),
    "any-size-drawn": (("== width && picture_be32(head + 20) == height", "== width || 1"), "menu_picture_png_whole", "whole"),
    "edges-not-repeated": (("(x < width ? x : width - 1)", "(x < width ? x : 0)"), "menu_picture_frame", "frame"),
}


def generated(fault=None, faulty_function=None):
    header = read("port/linux/include/menu_pictures.h")
    source = read(SOURCE)
    config = "#include <math.h>\n"
    for name in ("MENU_PICTURES_VERSION", "MENU_PICTURE_FAILURES", "MENU_PICTURE_LARGEST_UNITS",
                 "MENU_PICTURE_TIME_BUDGET", "MENU_PICTURES_TIME_BUDGET"):
        line = next(line for line in source.splitlines() if line.startswith(f"#define {name} "))
        config += line + "\n"
    start = source.index("static char const *const menu_picture_result_names[]")
    text = source[start:source.index(";", start) + 1] + "\n"
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("menu_pictures_h.inc", header), ("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_pictures", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("menu_pictures", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"menu_pictures.c with a fault ({control}) passed '{case}': the test cannot see it"
