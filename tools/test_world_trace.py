"""The distance-field scene's ray step (port/linux/include/world_cascades.h, docs/world-ao.md "The distance-field
scene") in both emitters against its CPU reference: the Metal emitter's world_trace (xgpu_msl.c), its very text run as
C++ (tools/world_trace/trace_check.cpp on tools/world_ao_light/metal_shim.h), and the SPIR-V emitter's
(xgpu_spirv.c), its module run on the CPU (tools/world_ao_light/spirv_run.py), each at a made-up scene's rays
(tools/world_trace/scene.c: a floor and a wall, a sphere, a capsule and a turned box, in cascades whose places wrap)
find what world_trace_ray (port/linux/game/world_cascades.c) finds: whether each ray hits, what, and how far. A fault
in either emitter is seen. The module passes spirv-val where it is installed."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_world_ao_light import CC, CXX, INCLUDE, SOURCE, VALIDATOR, check_build, faulted  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port/linux/game"
TOOL = ROOT / "tools/world_trace"
SHIM = ROOT / "tools/world_ao_light"
RAYS = 400
SPIRV_RAYS = 160
TOLERANCE = 1.0e-3


def functions_of(text):
    """the trace's functions, from the emitter's test shader"""
    return text[text.index("struct world_trace_hit\n"):text.index("struct world_trace_in\n")]


@pytest.fixture(scope="module")
def check(tmp_path_factory):
    if not CC:
        pytest.skip("no C compiler")
    return check_build(tmp_path_factory.mktemp("world_trace_check"))


@pytest.fixture(scope="module")
def reference(tmp_path_factory):
    """world_cascades.c and the scene, built; the scene's cascades and rays with the reference's results"""
    if not CC:
        pytest.skip("no C compiler")
    build = tmp_path_factory.mktemp("world_trace_reference")
    flags = ["-std=gnu99", "-O1", "-Wall", "-iquote", str(INCLUDE)]
    cascades = build / "world_cascades.o"
    subprocess.run([CC] + flags + ["-c", str(GAME / "world_cascades.c"), "-o", str(cascades)], check=True)
    library = build / "scene_library.o"
    subprocess.run([CC] + flags + ["-DSCENE_LIBRARY", "-c", str(TOOL / "scene.c"), "-o", str(library)], check=True)
    scene = build / "scene"
    subprocess.run([CC] + flags + [str(TOOL / "scene.c"), str(cascades), "-o", str(scene), "-lm"], check=True)
    subprocess.run([str(scene), str(build), str(SPIRV_RAYS)], check=True)
    return {"build": build, "objects": [cascades, library]}


def msl_check(reference, text, directory):
    if not CXX:
        pytest.skip("no C++ compiler")
    (directory / "world_trace_msl.inc").write_text(functions_of(text))
    executable = directory / "trace_check"
    subprocess.run([CXX, "-std=c++14", "-O1", "-Wall", "-Wno-unused-parameter", f"-I{directory}", "-iquote",
                    str(INCLUDE), f"-I{SHIM}", str(TOOL / "trace_check.cpp")] + [str(path) for path in reference["objects"]] +
                   ["-o", str(executable), "-lm"], check=True)
    return subprocess.run([str(executable), str(RAYS), str(TOLERANCE)], capture_output=True, text=True)


def msl_text(check):
    return subprocess.run([str(check), "trace"], check=True, capture_output=True, text=True).stdout


def spirv_check(reference, check, directory):
    module = directory / "trace.spv"
    subprocess.run([str(check), "trace", str(module)], check=True)
    return subprocess.run([sys.executable, str(TOOL / "spirv_check.py"), str(reference["build"]), str(module),
                           str(TOLERANCE)], capture_output=True, text=True)


def test_msl_finds_what_the_reference_finds(reference, check, tmp_path):
    result = msl_check(reference, msl_text(check), tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout)


def test_spirv_finds_what_the_reference_finds(reference, check, tmp_path):
    result = spirv_check(reference, check, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout)


def test_spirv_module_valid(check, tmp_path):
    module = tmp_path / "trace.spv"
    subprocess.run([str(check), "trace", str(module)], check=True)
    if not VALIDATOR:
        pytest.skip("no spirv-val")
    subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(module)], check=True)


# negative controls: each emitter's cascades a texel too short along z (the guard left out of the cascades' layers)
MSL_FAULT = ('".0 * float(cascade)) / ("', '".0 * float(cascade) - 1.0) / ("')
SPIRV_FAULT = ("constant_float(m, (float)(XGPU_WORLD_CASCADE_SPAN * XGPU_WORLD_CASCADES)));",
               "constant_float(m, (float)(XGPU_WORLD_CASCADE_SIDE * XGPU_WORLD_CASCADES)));")


def test_msl_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_msl.c", MSL_FAULT))
    assert msl_check(reference, msl_text(faulty), tmp_path).returncode == 1


def test_spirv_control(reference, tmp_path):
    if not CC:
        pytest.skip("no C compiler")
    faulty = check_build(tmp_path / "build", faulted("xgpu_spirv.c", SPIRV_FAULT))
    (tmp_path / "out").mkdir()
    assert spirv_check(reference, faulty, tmp_path / "out").returncode == 1


def test_the_constants_alike():
    # (world_cascades.h's for the CPU, xgpu.h's for the emitters)
    import re
    game = (INCLUDE / "world_cascades.h").read_text()
    emitters = (SOURCE / "xgpu.h").read_text()
    for name, theirs in (("WORLD_CASCADES", "XGPU_WORLD_CASCADES"), ("WORLD_CASCADE_SIDE", "XGPU_WORLD_CASCADE_SIDE"),
                         ("WORLD_CASCADE_RANGE_VOXELS", "XGPU_WORLD_CASCADE_RANGE_VOXELS"),
                         ("WORLD_TRACE_STEPS", "XGPU_WORLD_TRACE_STEPS"),
                         ("WORLD_TRACE_STRUCTURE", "XGPU_WORLD_TRACE_STRUCTURE"),
                         ("WORLD_TRACE_OBJECTS", "XGPU_WORLD_TRACE_OBJECTS")):
        ours = re.search(rf"\b{name} = (\w+),", game).group(1)
        assert ours == re.search(rf"#define {theirs} (\S+)", emitters).group(1), name
    for name in ("HIT", "LEAST_STEP"):
        ours = re.search(rf"#define WORLD_TRACE_{name} (\S+)", game).group(1)
        assert ours == re.search(rf"#define XGPU_WORLD_TRACE_{name} (\S+)", emitters).group(1), name
    assert "#define XGPU_WORLD_CASCADE_SPAN 65" in emitters
