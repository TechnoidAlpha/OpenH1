"""debug.txt's runs of lines written again (source/cseries/errors.c, write_to_debug_file): a line, or a few in turn
(a failure and its caller's, each frame: lastprof.txt's two lines filled 60 MB in minutes), counted once they come
twice in a row and written down as "last N lines repeated M times" when another line comes or the log ends; a pass
not finished written as it came."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

CASES = ["one-line", "two-lines", "four-lines", "unfinished-pass", "distinct", "long-lines", "end"]
FUNCTIONS = ("debug_file_port_recent", "debug_file_port_repeated", "debug_file_port_written",
             "debug_file_port_run_begin", "debug_file_port_prefix", "debug_file_port_write", "write_to_debug_file")

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "one-line-runs-only": (("period <= DEBUG_FILE_PORT_REPEAT_LINES &&", "period <= 1 &&"),
                           "debug_file_port_run_begin", "two-lines"),
    "unfinished-pass-lost": (("index < partial;", "index < 0;"), "write_to_debug_file", "unfinished-pass"),
    "count-not-written": (("if (passes)\n", "if (0)\n"), "write_to_debug_file", "one-line"),
    "end-not-written": (("if (string && !strncmp(", "if (!string || !strncmp("), "debug_file_port_repeated", "end"),
}


def generated(fault=None, faulty_function=None):
    errors = read("source/cseries/errors.c")
    config = re.search(r"^#define DEBUG_FILE_PORT_MAXIMUM_SIZE .*$", errors, re.M).group(0) + "\n"
    config += "#define DEBUG_FILE_PORT_REPEAT_LINES %d\n" % constant(errors, "DEBUG_FILE_PORT_REPEAT_LINES")
    text = structure(errors, "debug_file_port_repeats") + "\n"
    text += ("static FILE *debug_file_port = NULL;\nstatic long debug_file_port_size = 0;\n"
             "static struct debug_file_port_repeats debug_file_port_repeats;\n"
             "static volatile long debug_file_port_lock = 0;\nstatic boolean debug_file_port_rolled = FALSE;\n"
             "static void debug_file_port_write(FILE *file, char const *prefix, char const *string);\n")
    for name in FUNCTIONS:
        code = function(errors, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("debug_log_repeats", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("debug_log_repeats", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
