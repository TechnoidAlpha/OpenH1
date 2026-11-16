"""display.ambient_occlusion multiplies a surface's ambient light alone, never
what a light adds (port/linux/src/xgpu.h's XGPU_AMBIENT_OCCLUSION_LIGHT_*,
issue 560: the flashlight on a character and a plasma bolt's glow on a wall
were darkened by it).

The shaders of a surface lit by the occlusion (display.ambient_occlusion
"world"'s), as the emitters write them (tools/ambient_occlusion_light/check.c),
are read: a model lit for each pixel
has its ambient and distant lights by the occlusion before its point lights
or the enhanced dynamic lights add theirs; a model lit for each vertex adds
its point lights' light, handed on apart, to its ambient light by the
occlusion; a structure's lightmap pass has its light by the occlusion before
the enhanced dynamic lights add theirs. Without the occlusion the shaders
read none. The SPIR-V emitter's modules are checked by spirv-val where it is
installed.
"""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
CC = shutil.which("cc") or shutil.which("clang")
VALIDATOR = shutil.which("spirv-val")
SHARED = ["xgpu.h", "xgpu_backend.h", "xgpu_hdr.h", "xgpu_shader.h", "xgpu_msl.h", "xgpu_spirv.h", "xgpu_stream.h",
          "nv2a_vsh.c", "nv2a_psh.c", "xgpu_text.c", "xgpu_msl.c", "xgpu_spirv.c"]


@pytest.fixture(scope="module")
def check(tmp_path_factory):
    if not CC:
        pytest.skip("no C compiler")
    build = tmp_path_factory.mktemp("ambient_occlusion_light")
    (build / "src").mkdir()
    for name in SHARED:
        shutil.copy(SOURCE / name, build / "src" / name)
    # (the XDK's enumerations, as tools/msl_corpus/build.sh takes them)
    pdb = (ROOT / "port/include/xdk/xdk_pdb.h").read_text()
    enums = pdb.split("/* ---------- enumerations", 1)[1].split("/* ---------- structures and unions", 1)[0]
    (build / "xdk_enums.h").write_text("/* ---------- enumerations" + enums)
    executable = build / "check"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", f"-I{build}", f"-I{ROOT / 'tools/msl_corpus/stub'}",
                    f"-I{build / 'src'}", "-o", str(executable), str(ROOT / "tools/ambient_occlusion_light/check.c")] +
                   [str(build / "src" / name) for name in SHARED if name.endswith(".c")] + ["-lm"], check=True)
    return executable


@pytest.fixture(scope="module")
def shaders(check):
    text = subprocess.run([str(check), "msl"], check=True, capture_output=True, text=True).stdout
    parts = re.split(r"^==== (\w+) (\d)\n", text, flags=re.M)
    return {(parts[index], int(parts[index + 1])): parts[index + 2] for index in range(1, len(parts), 3)}


def body(source):
    """the fragment function, after the functions put in before it"""
    return source[source.index("fragment xgpu_fragment_out"):]


def test_none_without_the_occlusion(shaders):
    for (name, occlusion), source in shaders.items():
        if not occlusion:
            assert "occlusion" not in source, name


def test_model_lit_for_each_pixel(shaders):
    source = shaders["model", 1]
    function = source[source.index("static inline float3 model_lighting"):]
    function = function[:function.index("\n}\n")]
    ambient, occluded, point = (function.index("float3 light ="), function.index("light *= occlusion;"),
                                function.index("light +="))
    # the ambient and distant lights by the occlusion, then the point lights
    assert ambient < occluded < point
    assert function.count("occlusion") == 2 and "float occlusion" in function
    assert "v0.rgb = model_lighting(u, in.xWorldNormal, in.xWorldPosition, occlusion);" in body(source)


def test_model_lit_by_the_dynamic_lights(shaders):
    source = body(shaders["model_dynamic", 1])
    added = source[source.index("v0.rgb = dynamic_light_add("):]
    added = added[:added.index(";")]
    # the occlusion in the base's lighting alone, the lights' sum added after
    assert "model_lighting(u, in.xWorldNormal, occlusion)" in added
    assert "occlusion" not in added[added.index("dynamic_light_sum("):]


def test_model_lit_for_each_vertex(shaders):
    source = body(shaders["vertex", 1])
    assert "v0.rgb = saturate(in.xLightAmbient * occlusion + in.xLightPoint);" in source
    vertex = shaders["vertex_split", 1]
    ambient = vertex[vertex.index("float3 n = xWorldNormal.xyz;"):vertex.index("output.xLightPoint = float3(0.0);")]
    # the ambient and distant lights' registers (c[-82], c[-73] to c[-69]),
    # none of the point lights' (c[-79] to c[-74])
    assert set(re.findall(r"c\[(\d+)\]", ambient)) == {str(96 + n) for n in (-73, -69, -82, -72, -71, -70)}
    assert vertex.count("output.xLightPoint +=") == 2


def test_lightmap_pass(shaders):
    for name in ("lightmap", "lightmap_dynamic"):
        source = body(shaders[name, 1])
        assert source.count("result.rgb *= occlusion;") == 1, name
        if name == "lightmap_dynamic":
            # the lightmap's light by the occlusion before the lights add theirs
            assert source.index("result.rgb *= occlusion;") < source.index("dynamic_light_add")


def test_spirv_modules_valid(check, tmp_path):
    subprocess.run([str(check), "spirv", str(tmp_path)], check=True)
    modules = sorted(tmp_path.glob("*.spv"))
    assert len(modules) == 11
    if not VALIDATOR:
        pytest.skip("no spirv-val")
    for module in modules:
        subprocess.run([VALIDATOR, "--target-env", "vulkan1.3", str(module)], check=True)
