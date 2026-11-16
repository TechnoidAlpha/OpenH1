"""display.ambient_occlusion "world" in the light (port/linux/src/xgpu.h's XGPU_AMBIENT_OCCLUSION_WORLD,
docs/world-ao.md "Shading"): the emitters' shaders of a surface the world's occlusion lights (as
tools/ambient_occlusion_light/check.c writes them, with the source "world") multiply its ambient light alone,
before any light adds its own, in each of the three draws (a model lit for each pixel, by its point lights or the
enhanced dynamic lights; a model lit for each vertex; the structure's lightmap pass, which reads the proxies and
never the field); without it (or with a light's bits alone, without XGPU_AMBIENT_OCCLUSION_WORLD) they have none
of its code. And what they find is what the
CPU's reference finds (port/linux/game/world_ao_shading.c, on world_proxies.c's arithmetic): the Metal emitter's
functions' very text run as C++ (tools/world_ao_light/msl_check.cpp) and the SPIR-V emitter's whole shaders run on
the CPU (tools/world_ao_light/spirv_run.py), at a made-up scene's points (tools/world_ao_light/scene.c: a floor and
a wall, spheres, a capsule, two boxes and a vehicle's occlusion field, a box's baked, on a turned and scaled frame,
read from an atlas). The modules pass spirv-val where it is installed."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
GAME = ROOT / "port/linux/game"
INCLUDE = ROOT / "port/linux/include"
TOOL = ROOT / "tools/world_ao_light"
CC = shutil.which("cc") or shutil.which("clang")
CXX = shutil.which("c++") or shutil.which("clang++")
VALIDATOR = shutil.which("spirv-val")
SHARED = ["xgpu.h", "xgpu_backend.h", "xgpu_hdr.h", "xgpu_shader.h", "xgpu_msl.h", "xgpu_spirv.h", "xgpu_stream.h",
          "nv2a_vsh.c", "nv2a_psh.c", "xgpu_text.c", "xgpu_msl.c", "xgpu_spirv.c"]
CASES = ["model", "model_dynamic", "vertex", "lightmap", "lightmap_dynamic"]
# (the scene's points, and the largest difference from the reference)
POINTS = 150
TOLERANCE = 1.0e-3


def check_build(directory, emitters=None):
    """tools/ambient_occlusion_light/check.c, with the emitters' sources (or changed ones: {name: text})"""
    (directory / "src").mkdir(parents=True, exist_ok=True)
    for name in SHARED:
        text = (emitters or {}).get(name)
        if text is None:
            shutil.copy(SOURCE / name, directory / "src" / name)
        else:
            (directory / "src" / name).write_text(text)
    pdb = (ROOT / "port/include/xdk/xdk_pdb.h").read_text()
    enums = pdb.split("/* ---------- enumerations", 1)[1].split("/* ---------- structures and unions", 1)[0]
    (directory / "xdk_enums.h").write_text("/* ---------- enumerations" + enums)
    executable = directory / "check"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", f"-I{directory}", f"-I{ROOT / 'tools/msl_corpus/stub'}",
                    f"-I{directory / 'src'}", "-o", str(executable), str(ROOT / "tools/ambient_occlusion_light/check.c")] +
                   [str(directory / "src" / name) for name in SHARED if name.endswith(".c")] + ["-lm"], check=True)
    return executable


def msl_of(check, source):
    text = subprocess.run([str(check), "msl", source], check=True, capture_output=True, text=True).stdout
    parts = re.split(r"^==== (\w+) (\d)\n", text, flags=re.M)
    return {(parts[index], int(parts[index + 1])): parts[index + 2] for index in range(1, len(parts), 3)}


@pytest.fixture(scope="module")
def check(tmp_path_factory):
    if not CC:
        pytest.skip("no C compiler")
    return check_build(tmp_path_factory.mktemp("world_ao_light"))


@pytest.fixture(scope="module")
def shaders(check):
    return {source: msl_of(check, source) for source in ("bare", "world", "view")}


@pytest.fixture(scope="module")
def reference(tmp_path_factory):
    """the scene's objects (the reference's arithmetic: world_proxies.c, world_sdf_bake.c, world_ao_shading.c)
    and its points' occlusion, as JSON"""
    if not CC:
        pytest.skip("no C compiler")
    build = tmp_path_factory.mktemp("world_ao_reference")
    objects = []
    for name in ("world_proxies.c", "world_sdf_bake.c", "world_ao_shading.c"):
        objects.append(build / name.replace(".c", ".o"))
        subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE), "-c", str(GAME / name), "-o",
                        str(objects[-1])], check=True)
    library = build / "scene_library.o"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE), "-DSCENE_LIBRARY", "-c",
                    str(TOOL / "scene.c"), "-o", str(library)], check=True)
    scene = build / "scene"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE), str(TOOL / "scene.c")] +
                   [str(path) for path in objects] + ["-o", str(scene), "-lm"], check=True)
    json_path = build / "scene.json"
    json_path.write_text(subprocess.run([str(scene), "reference", str(POINTS)], check=True, capture_output=True,
                                        text=True).stdout)
    return {"build": build, "objects": objects + [library], "json": json_path}


def body(source):
    return source[source.index("fragment xgpu_fragment_out"):]


def test_none_without_it(shaders):
    # (a light's bits without the world's, and no occlusion: none of the world's code, binding or uniform, and no
    # occlusion at all: nothing else finds it)
    for (name, occlusion), text in shaders["bare"].items():
        for word in ("world_ao", "world_sdf", "world_proxy", "occlusion"):
            assert word not in text, (name, occlusion)
    # (the enhanced dynamic lights' own list of the world's lights, dynamic_light_world, is theirs)
    for (name, occlusion), text in shaders["world"].items():
        if not occlusion:
            assert not re.search(r"world_(ao|sdf|prox|cascade|trace)|WORLD_AO", text), name


def test_the_world_in_the_ambient_light_alone(shaders):
    world = shaders["world"]
    model = world["model", 1]
    function = model[model.index("static inline float3 model_lighting"):]
    function = function[:function.index("\n}\n")]
    assert function.index("float3 light =") < function.index("light *= occlusion;") < function.index("light +=")
    assert "v0.rgb = model_lighting(u, in.xWorldNormal, in.xWorldPosition, occlusion);" in body(model)
    dynamic = body(world["model_dynamic", 1])
    added = dynamic[dynamic.index("v0.rgb = dynamic_light_add("):]
    assert "model_lighting(u, in.xWorldNormal, occlusion)" in added[:added.index(";")]
    assert "v0.rgb = saturate(in.xLightAmbient * occlusion + in.xLightPoint);" in body(world["vertex", 1])
    for name in ("lightmap", "lightmap_dynamic"):
        text = body(world[name, 1])
        assert text.count("result.rgb *= occlusion;") == 1
        if name == "lightmap_dynamic":
            assert text.index("result.rgb *= occlusion;") < text.index("dynamic_light_add")
    # the occlusion found in the world before the light it multiplies, from the block and the draw's object
    for name in CASES:
        text = body(world[name, 1])
        assert "world_proxies_occlusion(world_ao, world_ao_objects, in.xWorldPosition, world_n, u.world_ao.x,\n" in text
        assert "texture3d<float> world_ao_objects [[texture(20)]]" in text
        assert text.index("float occlusion;") < text.index("texture stage 0")
        assert "[[buffer(23)]]" in text and "ambient_occlusion [[texture" not in text
        # the lightmap pass: the proxies alone, never the field
        lightmap = name.startswith("lightmap")
        assert ("world_sdf" in world[name, 1]) != lightmap, name
        assert ("[[texture(5)]]" in text) != lightmap, name
        # the lightmap pass: the static scenery's pages at its lightmap's coordinates where its page has a layer,
        # the dynamic objects' shapes alone there and every shape where it has none; a model, the dynamic objects'
        assert ("texture2d_array<float> world_ao_scenery [[texture(7)]]" in text) == lightmap, name
        assert "int limit = int(world_ao.parameters[1].y);" in text
        if lightmap:
            assert "if (u.world_ao.y < 0.0)\n\t\t\tlimit = 64;" in text
            assert "u.texture_scale[2].xy" in text and "proxies *= scenery;" in text
            # (the pages are the coverage's format, read as 1, 1, 1, the byte)
            assert "level(0.0)).a;" in text
        else:
            assert "limit = 64" not in text


def test_the_view_shows_it(shaders):
    for name in CASES:
        assert "result = float4(float3(occlusion), 1.0);" in body(shaders["view"][name, 1])
        assert "float3(occlusion)" not in shaders["world"][name, 1]


def test_spirv_modules_valid(check, tmp_path):
    for mode in ("spirv", "spirv_subgroups"):
        for source in ("bare", "world", "view"):
            (tmp_path / mode / source).mkdir(parents=True)
            subprocess.run([str(check), mode, str(tmp_path / mode / source), source], check=True)
    modules = sorted(tmp_path.glob("*/*/*.spv"))
    assert len(modules) == 66
    if not VALIDATOR:
        pytest.skip("no spirv-val")
    for module in modules:
        subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(module)], check=True)


def msl_check(reference, world_text, directory):
    if not CXX:
        pytest.skip("no C++ compiler")
    model = body_and_functions(world_text)
    (directory / "world_msl.inc").write_text(model)
    executable = directory / "msl_check"
    subprocess.run([CXX, "-std=c++14", "-O1", "-Wall", "-Wno-unused-parameter", f"-I{directory}", "-iquote",
                    str(INCLUDE), f"-I{TOOL}", str(TOOL / "msl_check.cpp")] +
                   [str(path) for path in reference["objects"]] + ["-o", str(executable), "-lm"], check=True)
    return subprocess.run([str(executable), str(POINTS * 20), str(TOLERANCE)], capture_output=True, text=True)


def body_and_functions(model_text):
    """the world's functions, from the model's shader"""
    start = model_text.index("struct xgpu_world_ao\n")
    return model_text[start:model_text.index("struct xgpu_fragment_in\n")]


def test_msl_finds_what_the_reference_finds(reference, shaders, tmp_path):
    result = msl_check(reference, shaders["world"]["model", 1], tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr


def spirv_check(reference, directory):
    return subprocess.run([sys.executable, str(TOOL / "spirv_check.py"), str(reference["json"]),
                           str(directory / "model-1.spv"), str(directory / "lightmap-1.spv"), str(TOLERANCE)],
                          capture_output=True, text=True)


@pytest.mark.parametrize("mode", ["spirv", "spirv_subgroups"])
def test_spirv_finds_what_the_reference_finds(reference, check, tmp_path, mode):
    """(with the subgroups' bitwise operations too: a subgroup of one pixel walks its own cell's shapes)"""
    subprocess.run([str(check), mode, str(tmp_path), "view"], check=True)
    result = spirv_check(reference, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr


# negative controls: each emitter with a fault in a shape the scene has, which the checks must see
SPIRV_FAULT = ("constant_float(m, 2.0f * XGPU_WORLD_AO_PI)), 0.0f, 1.0f));", "constant_float(m, XGPU_WORLD_AO_PI)), 0.0f, 1.0f));")
MSL_FAULT = ('"\\t\\tline = 2.0 * radius * h / 3.14159265358979 * (f1 - f0);\\n"',
             '"\\t\\tline = radius * h / 3.14159265358979 * (f1 - f0);\\n"')


def faulted(name, fault):
    text = (SOURCE / name).read_text()
    assert text.count(fault[0]) == 1, fault[0]
    return {name: text.replace(fault[0], fault[1])}


def test_the_field_is_found(reference):
    """(the scene's vehicle's field shades enough of its points for the checks to hold there)"""
    import json
    assert json.loads(reference["json"].read_text())["field_shaded"] >= POINTS // 10


def test_spirv_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_spirv.c", SPIRV_FAULT))
    (tmp_path / "out").mkdir()
    subprocess.run([str(faulty), "spirv", str(tmp_path / "out"), "view"], check=True)
    assert spirv_check(reference, tmp_path / "out").returncode == 1


# (and a vehicle's field read from the atlas without its place in it)
SPIRV_FIELD_FAULT = ("uvw = fmul(m, 3, fadd(m, 3, q, xyz(m, c0)), xyz(m, c1));", "uvw = fmul(m, 3, q, xyz(m, c1));")
MSL_FIELD_FAULT = ("(q + c0.xyz) * c1.xyz, level(0.0));", "q * c1.xyz, level(0.0));")


def test_spirv_field_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_spirv.c", SPIRV_FIELD_FAULT))
    (tmp_path / "out").mkdir()
    subprocess.run([str(faulty), "spirv", str(tmp_path / "out"), "view"], check=True)
    assert spirv_check(reference, tmp_path / "out").returncode == 1


def test_msl_field_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_msl.c", MSL_FIELD_FAULT))
    assert msl_check(reference, msl_of(faulty, "world")["model", 1], tmp_path).returncode == 1


def test_msl_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_msl.c", MSL_FAULT))
    assert msl_check(reference, msl_of(faulty, "world")["model", 1], tmp_path).returncode == 1


# (and a SIMD-group's walk taking the other lanes' shapes as its own)
MSL_SIMD_FAULT = ('"\\t\\t\\tif ((own & (1u << lowest)) == 0u)\\n"', '"\\t\\t\\tif (false)\\n"')


def test_msl_simd_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_msl.c", MSL_SIMD_FAULT))
    assert msl_check(reference, msl_of(faulty, "world")["model", 1], tmp_path).returncode == 1


def test_the_constants_alike():
    # (world_ao.h's for the CPU, xgpu.h's for the emitters)
    game = (INCLUDE / "world_ao.h").read_text()
    emitters = (SOURCE / "xgpu.h").read_text()
    for name in ("K", "STEP", "TAPS", "REFERENCE_LEAST", "REACH_FADE", "FLOOR", "GRID_X", "GRID_Y", "GRID_Z"):
        ours = re.search(rf"#define WORLD_AO_{name} (\S+)", game).group(1)
        theirs = re.search(rf"#define XGPU_WORLD_AO_{name} (\S+)", emitters).group(1)
        assert ours == theirs, name
    # (world_proxies.c's box's bound, the emitters' copy)
    ours = re.search(r"#define WORLD_PROXIES_LEAST (\S+)", (GAME / "world_proxies.c").read_text()).group(1)
    assert ours == re.search(r"#define XGPU_WORLD_AO_LEAST (\S+)", emitters).group(1)


def test_the_static_scenery_pages(reference):
    """the static scenery's pages (world_ao_scenery_page): each texel a floor covers is the shapes' product at its place,
    the texels about it its neighbours', and a page two places share is found"""
    result = subprocess.run([str(reference["build"] / "scene"), "pages"], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_static_scenery_pages_control(reference, tmp_path):
    """(a texel's place taken from the triangle's first corner alone: the check sees it)"""
    text = (GAME / "world_ao_shading.c").read_text()
    fault = "place[axis] = b[0] * t[axis] + b[1] * t[3 + axis] + b[2] * t[6 + axis];"
    assert text.count(fault) == 1
    (tmp_path / "world_ao_shading.c").write_text(text.replace(fault, "place[axis] = t[axis];"))
    objects = []
    for path in (GAME / "world_proxies.c", GAME / "world_sdf_bake.c", tmp_path / "world_ao_shading.c"):
        objects.append(tmp_path / path.name.replace(".c", ".o"))
        subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE), "-c", str(path), "-o",
                        str(objects[-1])], check=True)
    scene = tmp_path / "scene"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE), str(TOOL / "scene.c")] +
                   [str(path) for path in objects] + ["-o", str(scene), "-lm"], check=True)
    assert subprocess.run([str(scene), "pages"], capture_output=True, text=True).returncode == 1


def test_the_grid_finds_what_every_proxy_finds(reference):
    """the proxies' grid (world_ao_grid) leaves out only proxies that could not shade the pixel: at random windows of
    64 shapes and points about them, the grid's product is every proxy's, bit for bit"""
    result = subprocess.run([str(reference["build"] / "scene"), "cull", "8", "20000"], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout)
