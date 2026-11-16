"""The menus' models' file (port/linux/game/menu_models_file.c, menu_models.h): tags written through their schema and
read back (the round trip: blocks, data, data in the map's file, references and tag indices the file's, then a tag
table's), and a file refused before anything reads it: a byte changed, cut short, of another version, or made whole
again with a block outside its tags or two blocks sharing bytes. The game's validator then checks what passes, as it
checks a map's (tag_validate.c, run on the retail maps by tools/map_validate.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run, structure  # noqa: E402

CASES = ["round_trip", "damaged", "short", "version", "outside", "shared"]

SOURCE = "port/linux/game/menu_models_file.c"

# a fault in the file's code, and the case that must catch it
NEGATIVE_CONTROLS = {
    "crc-not-compared": ("menu_models_crc(0, file + MENU_MODELS_HEADER_SIZE, size - MENU_MODELS_HEADER_SIZE) != "
                         "header->body_crc", "0", "damaged"),
    "any-version": ("header->version != MENU_MODELS_VERSION", "0", "version"),
    "block-past-the-tags-passed-over": ('relocation_refuse(relocation, "a pointer is outside the tags");', ";",
                                        "outside"),
    "shared-passed-over": ('relocation_refuse(relocation, "two pointers share bytes");', ";", "shared"),
    "reference-kept": ("written->index = index;", "(void)index;", "round_trip"),
    "reference-not-remapped": ("reference->index = tag_indices[absolute_index];", ";", "round_trip"),
}


def types():
    """what the file's code is written against: the tag structures, the schema's header, the geometry buffers and the
    file's header, as the game has them"""
    groups = read("source/tag_files/tag_groups.h")
    geometry = read("source/rasterizer/rasterizer_geometry.h")
    schema = re.sub(r'#include "tag_files/tag_groups.h"\n', "", read("port/linux/game/tag_schema.h"))
    header = read("port/linux/game/menu_models.h")
    parts = ["#pragma GCC diagnostic ignored \"-Wmultichar\"\n"]
    parts += [structure(groups, name) for name in ("tag_block", "tag_reference", "tag_data")]
    parts += [structure(geometry, name) for name in ("vertex_buffer", "triangle_buffer")]
    parts += [schema, header]
    return "\n".join(parts) + "\n"


def code(text):
    """the file's code, without its includes (the test's types stand for them)"""
    return re.sub(r"^#include .*\n", "", text, flags=re.M)


def executable(text):
    return build("menu_models", (("menu_models_types.inc", types()), ("under_test.inc", code(text))))


@pytest.mark.parametrize("case", CASES)
def test_menu_models(case):
    status, output = run(executable(read(SOURCE)), case)
    assert status == 0, output


@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    before, after, case = NEGATIVE_CONTROLS[name]
    status, output = run(executable(mutated(read(SOURCE), before, after)), case)
    assert status == CHECK_FAILED, f"{name}: {case} did not catch it ({status}): {output}"
