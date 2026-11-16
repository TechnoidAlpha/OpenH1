"""Each weapon's own first-person offsets in config.toml (port/linux/src/port_config.c's config_weapons_read,
config_viewmodel_weapon, config_viewmodel_weapon_write): [display.viewmodel_weapon."<tag path>"] tables, read by
the real TOML parser and looked up by the path in any case, a slash for a backslash; written in place of the weapon's table in any
spelling (or at the end), taken out at all 0, never written to leave the file unreadable; a long table name still a
section's line (config_line_section, config_join_sections)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["read", "write_new", "write_replace", "write_zero", "refused", "long_section", "profile_zero"]
NAMES = ["config_append", "config_copy", "config_real_text", "config_line_section", "config_append_range",
         "config_join_sections", "config_weapon_paths_same", "config_weapon_find", "config_weapons_clear",
         "config_weapon_store", "config_weapons_read", "config_weapons_read_global", "config_viewmodel_weapon",
         "config_weapon_line_path",
         "config_weapon_set_table", "config_viewmodel_weapon_write"]

# a fault, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "slash-not-backslash": (("int ca = *a == '/' ? '\\\\' : tolower", "int ca = tolower"), "config_weapon_paths_same",
                            "read"),
    "case-matters": (("int cb = *b == '/' ? '\\\\' : tolower((unsigned char)*b);", "int cb = *b == '/' ? '\\\\' : *b;"),
                     "config_weapon_paths_same", "read"),
    "whole-numbers-dropped": (("\t\t\telse if (datum.type == TOML_INT64)\n\t\t\t\tvalues[value] = (double)datum.u.int64;\n",
                               ""), "config_weapons_read", "read"),
    "old-table-kept": (("\t\tif (!in_table)\n", "\t\tif (1)\n"), "config_weapon_set_table", "write_replace"),
    "only-quoted-spelling": (("result = toml_parse(header.buffer, (int)header.length);",
                              "result = toml_parse(strchr(header.buffer, '\\'') ? \"x\" : header.buffer, "
                              "(int)header.length);"), "config_weapon_line_path", "write_replace"),
    "zero-kept": (("\tif (!any)\n\t{\n\t\tif (index >= 0)", "\tif (!any && 0)\n\t{\n\t\tif (index >= 0)"),
                  "config_weapon_store", "write_zero"),
    # a profile's 0 taken out: config.toml's would show through it
    "profile-zero-dropped": (("int value, any = keep_zero;", "int value, any = 0;"), "config_weapon_store",
                             "profile_zero"),
    "unchecked-write": (("\t\tif (check.ok)\n", "\t\tif (1)\n"), "config_viewmodel_weapon_write", "refused"),
    "long-name-no-section": (("\tif (length >= size)\n\t\tlength = size - 1;", "\tif (length >= size)\n\t\treturn 0;"),
                             "config_line_section", "long_section"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/port_config.c")
    toml_header = read("port/third_party/tomlc17/tomlc17.h")
    toml_code = read("port/third_party/tomlc17/tomlc17.c")
    text = "static const char *const config_weapon_keys[CONFIG_WEAPON_VALUES] = " \
        '{ "x", "y", "z", "pitch", "yaw", "roll" };\n#define CONFIG_WEAPON_PATH_MAXIMUM 255\n' \
        "static const char *config_file_read_now = \"config.toml\";\n" + \
        structure(source, "config_weapon") + "\n" + structure(source, "config_weapons") + "\n" + \
        "static struct config_weapons config_weapons;\n"
    for name in NAMES:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("tomlc17.h", toml_header), ("tomlc17.inc", toml_code), ("under_test.inc", text))


def test_the_definitions_are_the_sources():
    # the test's definitions are port_config.c's
    source = read("port/linux/src/port_config.c")
    assert 'static const char *const config_weapon_keys[CONFIG_WEAPON_VALUES] = { "x", "y", "z", "pitch", "yaw", ' \
        '"roll" };' in source
    assert "#define CONFIG_WEAPON_PATH_MAXIMUM 255" in source
    assert '#define CONFIG_WEAPON_TABLE "display.viewmodel_weapon"' in source
    assert "#define CONFIG_WEAPON_VALUES 6" in read("port/linux/src/port_config.h")
    # read as the file is loaded and reloaded, and left out of the unknown keys
    assert "config_weapons_read_global(result.toptab);" in function(source, "config_load")
    assert "config_weapons_read_global(result.toptab);" in function(source, "config_reload")
    # (a profile's own, in its settings.toml, their 0s kept)
    assert "config_weapons_read(&profile->weapons, result.toptab, 1);" in function(source, "config_profile_read")
    assert "if (!strcmp(name, CONFIG_WEAPON_TABLE))" in function(source, "config_report_unknown_keys")


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("viewmodel_weapon_config", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("viewmodel_weapon_config", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
