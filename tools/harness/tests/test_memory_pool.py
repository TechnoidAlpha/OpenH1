"""The objects' memory pool as a release build checks it (source/memory/memory_pool.c): kept as the game uses it,
a block unlinked from its neighbours caught, and each allocation's and lookup's checks costing the same however many
blocks the pool has."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

SOURCE = "source/memory/memory_pool.c"
FUNCTIONS = ("memory_pool_block_compute_actual_size", "memory_pool_block_new", "memory_pool_verify",
             "memory_pool_block_get", "memory_pool_block_allocate", "memory_pool_block_free", "memory_pool_compact",
             "memory_pool_block_reallocate")
CASES = ["lifetime", "unlinked", "constant"]

# a fault in the code under test and the case that must catch it
NEGATIVE_CONTROLS = {
    # every block walked again, as the checked build does
    "walk-every-block": (("if (block->next_block && block->next_block!=pool->last_block)", "if (FALSE)"),
                         "constant"),
    # the neighbours not asked
    "neighbours-unasked": (("other_block = NULL;\n#else", "other_block = block;\n#else"), "unlinked"),
}


def generated(fault=None):
    source = read(SOURCE)
    prototypes = "\n".join(re.findall(r"^static [^;{]*?\([^;{]*?\);", source, re.M | re.S))
    code = prototypes + "\n" + "\n".join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    header = read("source/memory/memory_pool.h")
    types = structure(header, "memory_pool_block") + "\n" + structure(header, "memory_pool") + "\n"
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("memory_pool", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("memory_pool", generated(fault)), case)
    assert status == CHECK_FAILED and "check failed:" in output, f"{control} escaped its CHECK: {status}: {output}"
