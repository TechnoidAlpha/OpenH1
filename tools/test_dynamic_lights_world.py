"""The enhanced dynamic lights' world as the hosts give it to the GPU
(port/linux/src/xgpu_dynamic_lights_world.c), checked by
tools/dynamic_lights_world_check.c: every point's walk of the GPU's smaller
tree ends in the cluster (or none) its walk of the game's BSP ended in, as
the shaders walked it before; a light the frame's walk mask leaves out is in
the cluster mask of every point its sphere's pixels walk from; and the mask
leaves out the lights it should.

The worlds are made up (random BSPs, malformed ones among them). A folder of
real ones, each a world's texels as the game sends them (a .bin file),
named by HALO_DYNAMIC_LIGHTS_WORLDS, is checked too.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
CC = shutil.which("cc") or shutil.which("clang")


@pytest.fixture(scope="module")
def check(tmp_path_factory):
    if not CC:
        pytest.skip("no C compiler")
    build = tmp_path_factory.mktemp("dynamic_lights_world")
    # (beside the host's stand-in for platform.h and the XDK's enumerations,
    # as the hosts' builds copy them: tools/macos_build.py, vulkan_build.py)
    for name in ("xgpu.h", "xgpu_dynamic_lights_world.h", "xgpu_dynamic_lights_world.c"):
        shutil.copy(SOURCE / name, build / name)
    shutil.copy(ROOT / "port/macos/xgpu_metal_platform.h", build / "platform.h")
    pdb = (ROOT / "port/include/xdk/xdk_pdb.h").read_text()
    enums = pdb.split("/* ---------- enumerations", 1)[1].split("/* ---------- structures and unions", 1)[0]
    d3d8 = [line for line in (ROOT / "port/include/xdk/xdk_d3d8.h").read_text().splitlines()
            if line.startswith(("#define D3DFORMAT_", "#define D3DSIZE_", "#define D3DTEXTURE_"))]
    (build / "xdk_enums.h").write_text("/* ---------- enumerations" + enums + "\n".join(d3d8) + "\n")
    executable = build / "check"
    subprocess.run([CC, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{build}", "-o", str(executable),
                    str(build / "xgpu_dynamic_lights_world.c"), str(ROOT / "tools/dynamic_lights_world_check.c"),
                    "-lm"], check=True)
    return executable


def test_made_up_worlds(check):
    result = subprocess.run([str(check)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.rstrip().endswith("ok")


def test_real_worlds(check):
    folder = os.environ.get("HALO_DYNAMIC_LIGHTS_WORLDS")
    if not folder:
        pytest.skip("HALO_DYNAMIC_LIGHTS_WORLDS names no folder of worlds")
    worlds = sorted(str(path) for path in Path(folder).glob("*.bin"))
    assert worlds, folder
    result = subprocess.run([str(check), *worlds], capture_output=True, text=True)
    print(result.stdout)
    assert result.returncode == 0, result.stdout + result.stderr
