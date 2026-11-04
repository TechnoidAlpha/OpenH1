"""The lens flares' visibility tests read back frames late, each flare in a slot of its own
(source/rasterizer/rasterizer_lights.c; issue 526: flares went out and shone through walls)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ["shuffled", "shuffled-latency-1", "shuffled-latency-6", "turnover", "identifier-reused", "windows", "growing",
         "tests-off"]
CONTROLS = {
    # the slot of the flare's index in the frame's list, as before: another flare's count read
    "index-slots": (("short slot = lens_flare_test_slot_take(lens_flare_test_key(lens_flare_parameters),\n"
                     "\t\t\t\t\t\tlens_flare_parameters->light_identifier, lens_flare_test_frame);",
                     "short slot = lens_flare_index;"), "shuffled"),
    # a slot's count taken whatever test it is of: the slot's last flare's
    "any-test": (("if (!slot->first_test || !test || (((test - slot->first_test) & 0xffffffffUL) & 0x80000000UL))",
                  "if (FALSE)"), "turnover"),
    # a new light in a deleted one's datum given its slot
    "identifier-ignored": (("if (slot->key == key && slot->identifier == identifier)", "if (slot->key == key)"),
                           "identifier-reused"),
    # the windows' flares of one light in one slot
    "window-ignored": (("return (location * MAXIMUM_WINDOWS + window) * 2 + first_person;",
                        "return (location * MAXIMUM_WINDOWS + 0) * 2 + first_person;"), "windows"),
    # the count divided by this frame's pixels, not its test's
    "pixels-of-now": (("*occlusion_pixels = slot->pixels[test % LENS_FLARE_TEST_HISTORY];", ";"), "growing"),
    # a test the device did not number read as nothing
    "unnumbered-dark": (("visible_pixels= rasterizer_widget_get_occlusion_test_result(slot);", "visible_pixels= 0;"),
                        "tests-off"),
}
FUNCTIONS = ["screenshot_in_progress", "lens_flare_parameters_get", "lens_flare_occlusion_test_results_get",
             "lens_flare_test_key", "lens_flare_test_bucket", "lens_flare_test_slot_take", "lens_flare_test_slot_tested",
             "lens_flare_test_slot_count", "rasterizer_lights_reset_for_new_map", "rasterizer_lens_flare_submit",
             "rasterizer_lights_begin_for_new_frame", "rasterizer_lens_flares_submit_occlusion_tests"]


def generated(control=None):
    source = read("source/rasterizer/rasterizer_lights.c")
    enums = "\n".join(enum_with(source, member) for member in [
        "_rasterizer_target_render_primary", "_rasterizer_statistics_mode_geometry",
        "_lens_flare_first_person_weapon_flag", "_lens_flare_dynamic_light_flag",
        "_lens_flare_occlusion_offset_direction_toward_viewer", "_widget_type_internal_occlusion_test"])
    defines = "\n".join(re.findall(r"^#define LENS_FLARE_LIGHT_COLOR_ALPHA_MASK .+$", source, re.M))
    structures = "\n".join(structure(source, name) for name in [
        "lens_flare_occlusion_test_results", "rasterizer_lens_flare_submit_parameters"])
    start = source.index("static struct lens_flare_occlusion_test_results local_lens_flare_occlusion_test_results")
    end = source.index("\n", source.index("static unsigned long local_lens_flare_tests[")) + 1
    code = "\n".join(function(source, name) for name in FUNCTIONS)
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", "\n".join([enums, defines, structures, source[start:end], code])),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("lens_flare_tests", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("lens_flare_tests", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED and "check failed:" in output, f"{control} escaped its CHECK: {status}: {output}"
