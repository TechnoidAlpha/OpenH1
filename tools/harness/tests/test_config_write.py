"""config_write (port/linux/src/port_config.c): a setting set for now and its line in config.toml changed, a string as
long as a server's rotation (escaped as a TOML basic string) included, longer ones refused, and nothing written by a run
that only answers its command line (halo --probe, --version)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["long-string", "all-escaped", "too-long", "answer-only", "word-or-number"]
NAMES = ["config_name_hash", "config_setting_walk", "config_index_make", "config_setting_index", "config_text_is_false", "config_string_takes_number", "config_text_is_number",
         "config_set_from_text", "config_append", "config_copy",
         "config_line_key", "config_line_section", "config_find_line", "config_set_line", "config_value_line", "config_write"]

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "strings-kept-short": (("CONFIG_MAXIMUM_WRITTEN_STRING : 256", "256 : 256"), "config_write", "long-string"),
    "no-limit": (("strlen(value) > (config_settings", "strlen(value) > 99999 + 0 * (config_settings"), "config_write",
                 "too-long"),
    "probe-writes": (("\tif (config_answer_only)\n", "\tif (0)\n"), "config_write", "answer-only"),
    "number-quoted": (("&& !strcmp(setting->name", "&& 0 && !strcmp(setting->name"), "config_string_takes_number",
                      "word-or-number"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/port_config.c")
    types = "\n\n".join([
        enum_with(source, "_config_boolean,"), enum_with(source, "_environment_set_is_true,"),
        enum_with(source, "_platform_desktop = 1"), enum_with(source, "\t_scope_player"), structure(source, "config_setting"),
        structure(source, "config_value"), structure(source, "config_text"),
        f"#define CONFIG_MAXIMUM_WRITTEN_STRING {constant(source, 'CONFIG_MAXIMUM_WRITTEN_STRING')}",
    ]) + "\n"
    # (the settings' index by name: its slots and its state, as the source has them)
    text = source[source.index("#define CONFIG_INDEX_SLOTS"):source.index("static int config_index_state;")]
    text += "static int config_index_state;\n"
    for name in NAMES:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("types.inc", types), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("config_write", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("config_write", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
