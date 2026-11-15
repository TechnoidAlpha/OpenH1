"""A setting an older version had that this one has not (port/linux/src/port_config.c's config_retired_settings:
display.text_renderer, the fields drawing all the high-res text): taken out of config.toml as it is read, its line
(set or at its commented default), the comment above it and the blank line before that (config_migrate_retired),
said once in the log; never reported as an unknown setting (config_setting_retired); a file without one left as
it is."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["set", "commented", "kept", "known"]
NAMES = ["config_append", "config_copy", "config_line_key", "config_line_section", "config_find_line",
         "config_line_commented_default", "config_comment_start", "config_setting_retired", "config_migrate_retired"]

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the set line left in the file)
    "set-kept": (("for (commented = 0; commented < 2; commented++)", "for (commented = 1; commented < 2; commented++)"),
                 "config_migrate_retired", "set"),
    # (the commented default left in the file)
    "commented-kept": (("for (commented = 0; commented < 2; commented++)",
                        "for (commented = 0; commented < 1; commented++)"), "config_migrate_retired", "commented"),
    # (the comment above it left)
    "comment-kept": (("start = config_comment_start(current, line);", "start = line;"), "config_migrate_retired",
                     "set"),
    # (a retired setting reported as unknown)
    "reported": (("return 1;", "return 0;"), "config_setting_retired", "known"),
}


def table(source):
    match = re.search(r"^static const char \*const config_retired_settings\[\] =\n\{.*?^\};", source, re.M | re.S)
    if not match:
        raise LookupError("config_retired_settings")
    return match.group(0)


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/port_config.c")
    types = "\n\n".join(["#include <stdarg.h>", "#include \"tomlc17.h\"",
                         source[source.index("struct config_text\n{"):source.index("};", source.index("struct config_text\n{")) + 2]])
    text = table(source) + "\n"
    for name in NAMES:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("tomlc17.h", read("port/third_party/tomlc17/tomlc17.h")),
            ("toml.inc", read("port/third_party/tomlc17/tomlc17.c")),
            ("types.inc", types + "\n"), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("config_retired_settings", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("config_retired_settings", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
