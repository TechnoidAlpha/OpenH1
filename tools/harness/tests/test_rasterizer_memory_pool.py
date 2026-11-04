"""The real rasterizer memory pool (rasterizer_memory_pool.c) and rasterizer_transparent_geometry_flush: a map's first
frame, every model drawn in one window, releases the transparent geometry's memory as it draws and empties the queue,
and keeps the fog screen's for the window's end."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["window-memory-survives-release", "never-overlap", "flush-when-half-spent", "no-flush-with-room",
         "window-begin-resets"]

POOL_FUNCTIONS = ["rasterizer_memory_pool_initialize", "rasterizer_memory_pool_begin", "rasterizer_memory_alloc",
                  "rasterizer_memory_alloc_const", "rasterizer_memory_alloc_const_for_window",
                  "rasterizer_memory_pool_half_spent", "rasterizer_memory_pool_release_queued"]

# a fault (in a function) and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-release": (("rasterizer_transparent_geometry_flush", "rasterizer_memory_pool_release_queued();", ""),
                   "flush-when-half-spent"),
    "no-pool-check": (("rasterizer_transparent_geometry_flush", "!rasterizer_memory_pool_half_spent()", "1"),
                      "flush-when-half-spent"),
    "queue-into-window": (("rasterizer_memory_alloc",
                           "size <= (unsigned long)(global_memory_window_start-global_memory_index)",
                           "global_memory_index+size <= RASTERIZER_MEMORY_POOL_SIZE"), "never-overlap"),
    "window-not-reset": (("rasterizer_memory_pool_begin", "global_memory_window_start = RASTERIZER_MEMORY_POOL_SIZE;",
                          ""), "window-begin-resets"),
}


def generated(fault=None):
    pool = read("source/rasterizer/rasterizer_memory_pool.c")
    transparent = read("source/rasterizer/rasterizer_transparent_geometry.c")
    globals_start = pool.index("/* ---------- globals */")
    code = [pool[globals_start:pool.index("/* ---------- public code */")]]
    for name in POOL_FUNCTIONS:
        code.append(function(pool, name))
    code.append(function(transparent, "rasterizer_transparent_geometry_flush"))
    if fault:
        name, before, after = fault
        names = POOL_FUNCTIONS + ["rasterizer_transparent_geometry_flush"]
        source = transparent if name == "rasterizer_transparent_geometry_flush" else pool
        at = names.index(name) + 1
        code[at] = mutated(function(source, name), before, after)
    return (("under_test.inc", "\n\n".join(code) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("rasterizer_memory_pool", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("rasterizer_memory_pool", generated(fault)), case)
    assert status == CHECK_FAILED, f"the pool with a fault ({control}) passed '{case}': the test cannot see it"
