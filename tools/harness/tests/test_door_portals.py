"""Closed doors shut their portals to the lights (port/linux/game/door_portals.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, inline, mutated, read, run, structure

CASES = ["coverage", "pairing", "flood", "light"]
CONTROLS = {
    # the door's polygons met at any depth: a panel past the doorway covers it
    "any-depth": (("if (!(fabsf(distance) <= DOOR_PORTALS_DEPTH))", "if (FALSE)"), "coverage"),
    # an edge-on polygon (a jamb) taken as covering
    "edge-on-covers": (("if (!(fabsf(along) > DOOR_PORTALS_EDGE_ON * length))", "if (!(fabsf(along) > 0.0f))"),
                       "coverage"),
    # a portal the door covers in part paired with it
    "any-coverage-pairs": (("if (coverage >= DOOR_PORTALS_COVERED &&", "if (coverage > 0.0f &&"), "pairing"),
    # a moving door taken as resting open
    "moving-rests": (("machine->device.position == 1.0f ? 1 : NONE", "1"), "pairing"),
    # a door that moved keeps the portals it was paired with
    "moved-kept": (("door->structure != structure || memcmp(&door->position, &machine->object.position, "
                    "sizeof(door->position)))", "door->structure != structure)"), "pairing"),
    # the flood crosses the shut portals
    "flood-through": (("if (shut && door_portals_shut(portal_index))", "if (FALSE)"), "flood"),
    # every cluster the light reaches left out, not only those behind the door
    "blocked-all": (("open[index] &= ~shut[index];\n", ""), "light"),
    # a light's clusters behind the door kept from frame to frame
    "blocked-kept": (("memset(door_portals.light_blocks, 0, sizeof(door_portals.light_blocks));", ""), "light"),
}


def generated(control=None):
    source = read("port/linux/game/door_portals.c")
    math = read("source/math/real_math.h")
    enum = source[source.index("enum\n{\n\t/* (device_machines.c's _machine_type_door) */"):]
    enum = enum[:enum.index("};") + 2]
    defines = "\n".join(re.findall(r"^#define DOOR_PORTALS_\w+ .+$", source, re.M))
    table = re.search(r"^static struct\n\{\n\t/\* each object's.*?^\} door_portals;", source, re.M | re.S).group(0)
    helpers = "\n".join(inline(math, name) for name in [
        "vector_from_points3d", "dot_product3d", "magnitude_squared3d", "magnitude3d", "scale_vector3d",
        "cross_product3d", "point_from_line3d", "distance_squared3d"])
    code = "\n".join([structure(source, "door_portals_cluster_portal"), structure(source, "door_portals_cluster"),
                      structure(source, "door_portals_door"), table] +
                     [function(source, name) for name in [
                         "door_portals_meets", "door_portals_coverage", "door_portals_pair", "door_portals_update",
                         "door_portals_shut", "door_portals_flood", "door_portals_clusters_in_sphere",
                         "door_portals_light", "door_portals_light_blocked", "door_portals_light_filter"]])
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", enum + "\n" + defines + "\n" + helpers + "\n" + code),)


def test_door_type_is_the_games():
    machines = read("source/devices/device_machines.c")
    assert re.search(r"_machine_type_door = 0,", machines), "device_machines.c's door type changed"


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("door_portals", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("door_portals", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED and "check failed:" in output, f"{control} escaped its CHECK: {status}: {output}"
