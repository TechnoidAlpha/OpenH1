"""A map's scope and eye passes must each prepare their pipelines once, and a
later structure whose materials an earlier enumeration drew none."""
from pathlib import Path
import shutil
import subprocess

import pytest

from test_death_timing import function

ROOT = Path(__file__).resolve().parents[1]


def test_map_preparation_keeps_each_view(tmp_path):
    compiler = shutil.which("cc")
    if not compiler:
        pytest.skip("a C compiler is needed")
    code = (ROOT / "port/linux/game/render_enumerate.c").read_text()
    source = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
#define FALSE 0
#define TRUE 1
#define NONE (-1)
#define LONG_BITS 32
#define MAXIMUM_ENUMERATED_MAPS 64
#define TEST_FLAG(value, bit) (((value) & (1UL << (bit))) != 0)
#define SET_FLAG(value, bit, on) ((on) ? ((value) |= 1UL << (bit)) : ((value) &= ~(1UL << (bit))))
struct enumerated_materials { short count; unsigned long keys[1]; };
static struct { boolean new_map; unsigned long *structure_bsps_enumerated[2];
    struct enumerated_materials *materials_enumerated[2]; } enumerate;
static short structure;
static unsigned long checksum=1;
static long global_scenario_index=1;
static const char *map_name="first";
static int enabled=1;
static unsigned long cache_files_get_checksum(void) { return checksum; }
static const char *tag_get_name(long index) { (void)index; return map_name; }
static short global_structure_bsp_index_get(void) { return structure; }
static int config_boolean(const char *name) { (void)name; return enabled; }
static const char *config_string(const char *name) { (void)name; return ""; }
/* the structure's materials: whether one is new to the view's enumerations */
static boolean materials_new=1;
static struct enumerated_materials *materials_asked;
static int structure_bsp;
static void *global_structure_bsp_get(void) { return &structure_bsp; }
static boolean structure_materials(void *structure_bsp, struct enumerated_materials *materials, boolean add)
{ (void)structure_bsp; assert(!add); materials_asked=materials; return materials_new; }
static void platform_log(const char *format, ...) { (void)format; }
"""
    start = code.index("static struct\n", code.index("/* the maps enumerated (a map"))
    end = code.index("static short enumerated_map_count = 0;", start)
    source += code[start:end] + "static short enumerated_map_count = 0;\n"
    for name in ("map_structures_enumerated", "render_enumerate_new_map", "render_enumerate_pending"):
        source += function(code, name) + "\n"
    source += r"""
static void prepare(int scope) {
    assert(render_enumerate_pending(scope));
    *enumerate.structure_bsps_enumerated[scope] |= 1UL << structure;
    assert(!render_enumerate_pending(scope));
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc>1) {
        enabled=0; render_enumerate_new_map();
        assert(!render_enumerate_pending(0) && !render_enumerate_pending(1));
        return 0;
    }
    render_enumerate_new_map();
    prepare(1); /* The scope prepares first; the eyes must still prepare. */
    prepare(0);
    structure=1; prepare(0); prepare(1);
    structure=0; render_enumerate_new_map();
    assert(!render_enumerate_pending(0) && !render_enumerate_pending(1));
    checksum=2; map_name="second"; render_enumerate_new_map();
    prepare(0); prepare(1);
    checksum=1; map_name="first"; render_enumerate_new_map();
    assert(!render_enumerate_pending(0) && !render_enumerate_pending(1));
    /* A structure whose materials the map's enumerations drew is not drawn,
    and counts as enumerated; one with new materials is. */
    structure=2; materials_new=0; materials_asked=0;
    assert(!render_enumerate_pending(0) && materials_asked==enumerate.materials_enumerated[0] && materials_asked);
    assert(TEST_FLAG(*enumerate.structure_bsps_enumerated[0], 2));
    structure=3; materials_new=1; prepare(0);
    /* The map's first structure is drawn whatever its materials. */
    checksum=3; map_name="third"; render_enumerate_new_map(); materials_new=0; structure=0;
    assert(render_enumerate_pending(0));
    structure=NONE; assert(!render_enumerate_pending(0));
    structure=32; assert(!render_enumerate_pending(1));
    return 0;
}
"""
    path = tmp_path / "enumerate.c"
    path.write_text(source)
    binary = tmp_path / "enumerate"
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    subprocess.run([str(binary), "disabled"], check=True)

    # Merging both views into one map bitset must miss the second preparation.
    path.write_text(source.replace("return &enumerated_maps[index].structure_bsps[view];",
                                   "return &enumerated_maps[index].structure_bsps[0];"))
    subprocess.run([compiler, "-std=c99", str(path), "-o", str(binary)], check=True)
    broken = subprocess.run([str(binary)], capture_output=True)
    assert broken.returncode != 0
