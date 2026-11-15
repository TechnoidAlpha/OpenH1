"""display.ambient_occlusion "world" (docs/world-ao.md, port/linux/game/world_ao.c): nothing runs unless the
setting is "world" (off, subtle, strong and the Classic preset draw as before); the hooks where render.c and
scenario.c call it; the command stream's records and the bindings both backends take. Workstreams A and D grow it."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["off", "world", "window", "test", "rays"]
GAME = "port/linux/game/world_ao.c"
CONTROLS = [
    # on whatever the setting says
    ('world_ao.mode = setting && (!strcmp(setting, "world") || world_ao.rays) ?', 'world_ao.mode = setting ?', "off"),
    # the frame's work done when it is off
    ("\tif (!world_ao_mode())\n\t\treturn;\n\tworld_scene_frame_begin();", "\tworld_scene_frame_begin();", "off"),
    # an object's slot taken from its shape's place, not its owner's slot
    ("owner = (int)world_ao.proxies[index].v[1][3];", "owner = (int)index;", "window"),
    # the reference found without the field faded in
    ("instance = world_ao.volume && world_ao.fade > 0.0f && !baked ?", "instance = !baked ?", "window"),
    # the first-person weapon lit by it
    ("halo_world_ao_object(drawing ? WORLD_AO_OWNER_UNLIT : WORLD_AO_OWNER_NONE, 1.0f, 0);",
     "halo_world_ao_object(WORLD_AO_OWNER_NONE, 1.0f, 0);", "window"),
    # "ray_traced"'s rays without the backend's ray queries
    ("\tif (world_ao.rays && halo_world_ao_ray_queries())\n", "\tif (world_ao.rays)\n", "rays"),
    # why it is drawn as "world" said every time
    ("world_ao.rays && !world_ao.rays_missing_said && halo_world_ao_ray_queries_missing()",
     "world_ao.rays && halo_world_ao_ray_queries_missing()", "rays"),
    # the ray-traced scene's frame not handed on
    ("\t\tif (mode & WORLD_AO_RAYS)\n\t\t\tworld_rt_frame(world_ao_scene.frame);", "", "rays"),
    # an object's own instances not left out of its rays
    ("\t\t\trays_owner = world_rt_owner(object_index);", "\t\t\trays_owner = 0;", "rays"),
    # the scene kept past the map's end
    ("\tworld_rt_map_unloaded();\n\tworld_ao_scene_release();", "\tworld_ao_scene_release();", "rays"),
    # a window given while a map's pipelines are prepared (the world's then made in the load)
    ("\tif (render_enumerating)\n\t{\n\t\tworld_sdf_frame_prepared();\n\t\treturn;\n\t}", "", "window"),
    # the window's span told only while the setting is on (the manifest's world draws then lost)
    ("\t(void)rasterizer_target;\n\t/* (its span, whatever the setting: halo_world_ao_span) */\n"
     "\tworld_ao.span = WORLD_AO_SPAN_OPEN;\n\thalo_world_ao_span(WORLD_AO_SPAN_OPEN);\n",
     "\t(void)rasterizer_target;\n\tworld_ao.span = WORLD_AO_SPAN_OPEN;\n", "off"),
    ("\thalo_world_ao_span(!drawing ? world_ao.span : world_ao.span == WORLD_AO_SPAN_FOUND ?\n", "\t(void)(!drawing ? world_ao.span : world_ao.span == WORLD_AO_SPAN_FOUND ?\n", "off"),
    # the window found once a pixel told only while the setting is on (a map's preparation then makes the
    # pipelines without the pre-pass's reads and no twins)
    ("\tif (halo_world_ao_prepass_found())\n", "\tif (world_ao.window && halo_world_ao_prepass_found())\n", "off"),
    # the first-person weapon's span after it the plain window's, where the window is found once a pixel
    ("\thalo_world_ao_span(!drawing ? world_ao.span :", "\thalo_world_ao_span(!drawing ? WORLD_AO_SPAN_OPEN :", "off"),
    # the made-up field uploaded each window
    ("\tif (!world_ao_test.uploaded)\n\t\tworld_ao_test_field(camera_position);",
     "\tworld_ao_test_field(camera_position);", "test"),
]


def generated(fault=None):
    game = read(GAME)
    if fault:
        game = mutated(game, *fault)
    headers = [(name, read(f"port/linux/include/{name}"))
               for name in ("world_ao.h", "world_cascades.h", "world_proxies.h", "world_rt.h", "world_scene.h",
                            "world_sdf.h")]
    return tuple(headers) + (("under_test.inc", game),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_ao", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("world_ao", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_classic_preset_bypasses_it():
    # Classic sets display.ambient_occlusion "off", which world_ao_mode reads as off
    assert '{ "display.ambient_occlusion", "\\"off\\"" },' in read("port/linux/src/port_config.c")
    assert '#define GRAPHICS_AMBIENT_OCCLUSION "\\"off\\""' in read("port/linux/src/port_config.c")
