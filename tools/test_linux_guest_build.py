"""Tests of how configure.py generates the Linux builds
(tools/linux_guest_build.py): each is its computer's build, drawn with Vulkan
in the host, with no OpenGL anywhere; VR (--vr) is a mode of the same build;
and a build that cannot be generated (here, a compiler that does not run) is
a target that stops with the reason instead of stopping configure.py for
every target."""
import io
import shutil
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import linux_guest_build  # noqa: E402
from tools.ninja_syntax import Writer  # noqa: E402

X86_64 = next(target for target in linux_guest_build.LINUX_GUESTS if target.name == "linux_x86_64")
ARM64 = next(target for target in linux_guest_build.LINUX_GUESTS if target.name == "linux_arm64")


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """a folder that configure.py runs in, with the build's settings, on a
    64-bit x86 computer (and no OpenGL headers: the build needs none);
    fetches are recorded"""
    (tmp_path / "port" / "linux" / "host").mkdir(parents=True)
    shutil.copy(ROOT / "port" / "linux" / "port.json", tmp_path / "port" / "linux" / "port.json")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(linux_guest_build, "native_linux_guest", lambda: X86_64)
    fetches = []
    monkeypatch.setattr(linux_guest_build, "fetch_third_party", lambda third_party: fetches.append(third_party))
    monkeypatch.setattr(linux_guest_build, "fetch_vulkan_headers", lambda path: fetches.append(path))
    monkeypatch.setattr(linux_guest_build, "fetch_openxr_headers", lambda path: fetches.append(path))
    return fetches


def generate(capsys, target=X86_64, **settings):
    output = io.StringIO()
    linux_guest_build.generate_linux_guest_build(Writer(output), SimpleNamespace(**settings), target)
    return output.getvalue(), capsys.readouterr().err


def test_only_on_its_own_processor(tree, capsys):
    text, err = generate(capsys, ARM64)
    assert tree == [] and err == ""
    assert "only on a 64-bit ARM Linux computer" in text
    assert not [line for line in text.splitlines() if line.startswith("build ")]


def test_no_vulkan_build_of_its_own():
    """the Vulkan renderer is the Linux builds', not a build beside them"""
    assert [target.name for target in linux_guest_build.LINUX_GUESTS] == ["linux_x86_64", "linux_arm64"]
    assert not hasattr(linux_guest_build, "vr_build")


def test_compiler_that_cannot_run_is_this_target_unavailable(tree, capsys):
    text, err = generate(capsys, linux_x86_64_cc="/nonexistent/clang")
    assert tree[0] == Path("build/linux_x86_64/third_party")
    assert "(linux_x86_64) unavailable" in err and "/nonexistent/clang" in err
    # (only the stopping target: nothing of the build written halfway)
    builds = [line for line in text.splitlines() if line.startswith("build ")]
    assert builds == ["build linux_x86_64: linux_x86_64_unavailable"], text


def test_pinned_llvm_that_cannot_be_fetched_is_this_target_unavailable(tree, capsys, monkeypatch):
    def unavailable(sln, explicit_cc):
        assert explicit_cc is None
        raise RuntimeError("no network")
    monkeypatch.setattr(linux_guest_build, "guest_toolchain", unavailable)
    text, err = generate(capsys)
    assert "(linux_x86_64) unavailable" in err and "no network" in err
    builds = [line for line in text.splitlines() if line.startswith("build ")]
    assert builds == ["build linux_x86_64: linux_x86_64_unavailable"], text


@pytest.mark.parametrize("native", linux_guest_build.LINUX_GUESTS, ids=lambda target: target.name)
@pytest.mark.parametrize("vr", [False, True], ids=["desktop", "openxr"])
def test_vulkan_build_has_runtime_and_guest_imports(tree, monkeypatch, native, vr):
    """Both sides of the guest ABI and the runtime must agree on the renderer
    (Vulkan's) and VR, including Vulkan
    types used by the OpenXR graphics binding."""
    monkeypatch.setattr(linux_guest_build, "native_linux_guest", lambda: native)
    monkeypatch.setattr(linux_guest_build, "guest_toolchain", lambda sln, cc: None)
    linux_guest_build.VULKAN_DIR.mkdir(parents=True)
    for name in ("host_vulkan.c", "host_vulkan_vr.c", "xgpu_vulkan_vr_shaders.c"):
        (linux_guest_build.VULKAN_DIR / name).touch()
    guest = {}

    def image(n, sln, config, **settings):
        guest.update(settings)
        return {"host_table_c": "imports.c", "image": "guest.bin", "checked": "guest.checked", "assets": "openh1.assets"}
    monkeypatch.setattr(linux_guest_build, "generate_guest_image", image)
    output = io.StringIO()
    linux_guest_build.generate_linux_guest_build(Writer(output), SimpleNamespace(port_vr=vr), native)
    graph = output.getvalue()
    third_party = Path("build") / native.name / "third_party"
    assert guest["arch"] == native.arch
    assert "-DHALO_VULKAN=1" in guest["platform_defines"]
    assert ("-DHALO_VR=1" in guest["platform_defines"]) == vr
    assert (Path("port/linux/host/vr_imports.list") in guest["extra_imports"]) == vr
    assert Path("port/linux/vulkan/host_imports.list") in guest["extra_imports"]
    assert ("port/linux/host/host_vr.c" in graph) == vr
    assert ("port/linux/host/host_vr_stream.c" in graph) == vr
    assert ("port/linux/vulkan/host_vulkan_vr.c" in graph) == vr
    assert ("port/linux/vulkan/xgpu_vulkan_vr_shaders.c" in graph) == vr
    assert f"-I{third_party}/vulkan-headers" in graph
    assert "-Iport/linux/host" in graph
    # (the host decodes the Xbox's textures itself)
    assert "xbox_texture_decode.c" in graph
    link = next(line for line in graph.splitlines() if "-pie -o $out" in line)
    for library in ("GLESv2", "EGL", "OpenGL", "GL "):
        assert f"-l{library}" not in link, link
    assert tree == [third_party, third_party / "vulkan-headers", *([third_party / "openxr"] if vr else [])]
    assert f"build {native.name}: phony" in graph
