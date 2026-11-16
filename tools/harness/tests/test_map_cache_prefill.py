"""The stock maps copied into the cache ahead (port/linux/game/map_cache_prefill.c)
as Settings' MAP STORAGE (game.map_cache_megabytes) changes: every map copied
at Instant Loading, none ahead at Disk Space Saver, the copy ahead ended
and maps given up now on the switch to it, every map copied again on the way
back."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

PREFILL = "port/linux/game/map_cache_prefill.c"
CASES = ["instant_copies_every_map", "saver_copies_none_ahead", "setting_unchanged", "switch_to_saver",
         "switch_back_rebuilds"]


def generated(fault=None):
    code = "/* ---------- constants */" + read(PREFILL).split("/* ---------- constants */", 1)[1]
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_map_cache_prefill(case):
    status, output = run(build("map_cache_prefill", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # copies ahead with a budget set
    (("\tif (map_cache_prefill.megabytes > 0)\n\t\treturn;\n", ""), "saver_copies_none_ahead"),
    # the budget read again for any setting's change
    (("|| megabytes == map_cache_prefill.megabytes)", ")"), "setting_unchanged"),
    # the switch not followed: no map given up
    (("given_up = cache_files_budget_update();", "given_up = 0;"), "switch_to_saver"),
    # the copy ahead left going on the switch
    (("\t\t\tcache_files_precache_map_queue_end();\n\t\t\tcache_files_precache_map_end();\n", ""),
     "switch_to_saver"),
    # back at Instant Loading, nothing more copied (the prefill left idle)
    (("\tmap_cache_prefill.idle = FALSE;\n\tcsmemset", "\tcsmemset"), "switch_back_rebuilds"),
    # a map whose copy failed before never tried again
    (("\tcsmemset(map_cache_prefill.failed, 0, sizeof(map_cache_prefill.failed));\n", ""), "switch_back_rebuilds"),
])
def test_negative_control(fault, case):
    status, output = run(build("map_cache_prefill", generated(fault)), case)
    assert status == CHECK_FAILED, output
