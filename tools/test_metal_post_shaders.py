"""The Metal backend's anti-aliasing passes (tools/metal_post_shaders.py,
port/macos/xgpu_metal_post_shaders.c) and where the front end runs them.

The generated file must still be what the tool writes from Vulkan's GLSL
(tools/vulkan_post_shaders.py) and SMAA.hlsl (with glslangValidator and spirv-cross on PATH), and each function
must compile for the Mac (with Xcode's Metal compiler). The passes'
drawing itself is checked on the GPU by port/macos/tests/xgpu_metal_test.m
(tools/test_macos_metal.py)."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


@pytest.mark.skipif(not (shutil.which("glslangValidator") and shutil.which("spirv-cross")),
                    reason="needs glslangValidator and spirv-cross")
def test_generated_matches(tmp_path):
    arguments = [sys.executable, str(ROOT / "tools/metal_post_shaders.py"), "--check", "--build-dir", str(tmp_path)]
    if sys.platform == "darwin" and shutil.which("xcrun"):
        arguments.append("--metal")
    subprocess.run(arguments, check=True)


def test_every_build_draws_smaa():
    """Every build takes "smaa" as SMAA (Vulkan's passes, and Metal's)."""
    device = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    values = device[device.index("anti_aliasing_values[] ="):]
    values = values[:values.index("};")]
    assert re.findall(r'\{ "smaa", (\w+), 0, 1.0f \}', values) == ["_anti_aliasing_smaa"]
