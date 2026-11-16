"""A cache slot is played only once it is a whole copy of its map (port/linux/src/xbox_files.c's cache slots'
sources, source/cache/cache_files_windows.c's cache_files_precache_map_mark)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCES = "port/linux/src/xbox_files.c"
CACHE = "source/cache/cache_files_windows.c"
FUNCTIONS = ["names_match", "cache_slot_source_path", "cache_slot_source_stat", "cache_slot_source_load",
             "cache_slot_source_write", "cache_slot_source_forget", "cache_slot_source_record",
             "cache_slot_source_played", "cache_slot_source_matches"]
CASES = ["whole_copy", "failed_copy", "version_before", "torn_source", "another_map", "copy_of_another",
         "map_replaced"]


def generated(fault=None):
    source = read(SOURCES)
    constants = "\n".join(re.findall(r"^#define (?:MAXIMUM_CACHE_SLOT_SOURCES|CACHE_SLOT_SOURCE_VERSION) .*$",
                                     source, re.M)).replace("HALO_PORT_CACHED_MAP_FILES", "32")
    structure = re.search(r"^struct cache_slot_source\n\{.*?^\};", source, re.M | re.S).group(0)
    code = structure + "\nstatic struct cache_slot_source cache_slot_sources[MAXIMUM_CACHE_SLOT_SOURCES];\n" + \
        "\n".join(function(source, name) for name in FUNCTIONS) + "\n" + \
        function(read(CACHE), "cache_files_precache_map_mark")
    if fault:
        code = mutated(code, *fault)
    return (("constants.inc", constants), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_map_cache_whole(case):
    status, output = run(build("map_cache_whole", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # a copy marked whole however it ended
    (("map_unpack_take(map_file_index) &&\n\t\tcached_map_file_adopt(map_file_index) &&", ""), "failed_copy"),
    # the copy's map not checked against the one it was copying
    (("_stricmp(map_file->header.name, cache_file_globals.copying_to_map_file_name) == 0 &&", ""),
     "copy_of_another"),
    # a slot played whatever map its source file names
    (("names_match(source->name, name, sizeof(source->name)) &&", ""), "another_map"),
    # ... or checksum
    (("source->checksum == checksum &&", ""), "another_map"),
    # the map file's own size and time not checked
    (("now.size_low == source->size_low &&", ""), "map_replaced"),
])
def test_negative_control(fault, case):
    status, output = run(build("map_cache_whole", generated(fault)), case)
    assert status == CHECK_FAILED, output
