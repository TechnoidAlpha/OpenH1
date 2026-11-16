"""A write that makes no progress fails (port/linux/src/xbox_files.c's write_at)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCES = "port/linux/src/xbox_files.c"
CASES = ["zero_at_once", "partial_then_zero", "interrupted", "short_writes", "error", "empty"]


def generated(fault=None):
    code = function(read(SOURCES), "write_at")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_file_write_progress(case):
    status, output = run(build("file_write_progress", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # a write of nothing tried again, as before (#725)
    (("if (result == 0)\n", "if (0)\n"), "zero_at_once"),
    (("if (result == 0)\n", "if (0)\n"), "partial_then_zero"),
    # what was written before not reported
    (("if (bytes_written)\n\t\t\t\t*bytes_written = total;\n\t\t\treturn FALSE;\n\t\t}\n\t\ttotal",
      "return FALSE;\n\t\t}\n\t\ttotal"), "partial_then_zero"),
])
def test_negative_control(fault, case):
    status, output = run(build("file_write_progress", generated(fault)), case)
    assert status == CHECK_FAILED, output
