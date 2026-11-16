"""Tests that the mistakes tools/build_common.py's OWN_CODE_ERRORS names stop
every platform's build of our own code: every unit of it that configure.py
writes (the guests' game, platform layer and runtime, and the hosts'), with
the warning flags its build gives it, fails on a function that can end
without its value, a call to a function without a declaration, a
declaration without its type, an integer returned as a pointer, and a
pointer of the wrong type. (clang's -w drops even -Werror= diagnostics: a
unit given -w again would let them all through.) The vendored libraries,
under a third_party folder, keep their own flags."""

import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Tuple

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import (android_build, build_common, linux_guest_build, macos_build, windows_build,  # noqa: E402
                   x86_game_abi)
from tools.guest_toolchain import GuestToolchain  # noqa: E402
from tools.ninja_syntax import Writer  # noqa: E402

CLANG = shutil.which("clang")
pytestmark = pytest.mark.skipif(not CLANG, reason="needs clang")

# each mistake, and the warning group its error names
MISTAKES = {
    "return-type": "int f(int x) { if (x) return 1; }\n",
    "implicit-function-declaration": "int f(void) { return undeclared_function(1); }\n",
    "implicit-int": "static f(void) { return 1; }\nint g(void) { return f(); }\n",
    "int-conversion": "int *f(void) { int a = 0; return a; }\n",
    "incompatible-pointer-types": "int f(void) { long a = 0; long *b = &a; int *c = b; return *c; }\n",
}
TOOLCHAIN = GuestToolchain(cc="clang", ld="ld.lld", ar="llvm-ar", objdump="llvm-objdump")


class Recorder(Writer):
    """a ninja writer that keeps each C or Objective-C unit's rule and
    flags"""

    def __init__(self) -> None:
        import io
        super().__init__(io.StringIO(), width=1 << 20)
        self.units: List[Tuple[str, str, str]] = []

    def build(self, outputs, rule, inputs=None, *args, **kwargs):
        variables = kwargs.get("variables") or {}
        cflags = dict(variables).get("cflags") if variables else None
        sources = inputs if isinstance(inputs, list) else [inputs] if inputs is not None else []
        for source in sources:
            if cflags is not None and Path(str(source)).suffix in (".c", ".m"):
                self.units.append((rule, Path(str(source)).as_posix(), str(cflags)))
        return super().build(outputs, rule, inputs, *args, **kwargs)


def warning_flags(cflags: str) -> List[str]:
    """what of a unit's flags decides its diagnostics: its warning flags, in
    order, and its language standard"""
    return [flag for flag in shlex.split(cflags) if flag == "-w" or flag.startswith(("-W", "-std="))
            and not flag.startswith("-Wl,")]


_verdicts: Dict[Tuple[str, ...], List[str]] = {}


def let_through(flags: List[str], tmp_path: Path) -> List[str]:
    """the mistakes a unit with these flags compiles without an error"""
    key = tuple(flags)
    if key not in _verdicts:
        passed = []
        for name, text in MISTAKES.items():
            source = tmp_path / f"{name}.c"
            source.write_text(text, encoding="utf-8")
            result = subprocess.run([CLANG, *flags, "-fsyntax-only", str(source)], capture_output=True,
                                    text=True)
            errors = [line for line in result.stderr.splitlines() if "error:" in line]
            if result.returncode == 0 or not any(line.endswith(f"-W{name}]") for line in errors):
                passed.append(name)
        _verdicts[key] = passed
    return _verdicts[key]


def own_units(units: List[Tuple[str, str, str]]) -> List[Tuple[str, str, str]]:
    return [unit for unit in units if "third_party" not in Path(unit[1]).parts]


def check(units: List[Tuple[str, str, str]], tmp_path: Path, expected_folders: List[str]) -> None:
    ours = own_units(units)
    for folder in expected_folders:
        assert any(source.startswith(folder) for _, source, _ in ours), f"no unit of {folder}"
    failures = {}
    for rule, source, cflags in ours:
        passed = let_through(warning_flags(cflags), tmp_path)
        if passed:
            failures[source] = (rule, passed, warning_flags(cflags))
    assert not failures, "\n".join(f"{source} ({rule}) lets through {passed}: {flags}"
                                   for source, (rule, passed, flags) in sorted(failures.items()))


@pytest.fixture
def repository(monkeypatch):
    """configure.py's folder, with its downloads (musl, SDL, Sparkle, the
    Vulkan and OpenXR headers) and the pinned LLVM taken as there"""
    monkeypatch.chdir(ROOT)
    for module in (android_build, linux_guest_build, macos_build, windows_build):
        for name in ("fetch_third_party", "fetch_vulkan_headers", "fetch_openxr_headers", "fetch_sdl"):
            if hasattr(module, name):
                monkeypatch.setattr(module, name, lambda *args, **kwargs: None)
    monkeypatch.setattr(macos_build, "fetch_sparkle", lambda third_party: third_party / "Sparkle")
    monkeypatch.setattr(macos_build, "is_macos_arm64", lambda: True)
    for module in (android_build, linux_guest_build, macos_build, windows_build):
        monkeypatch.setattr(module, "guest_toolchain", lambda sln, explicit_cc: TOOLCHAIN)


GUEST_FOLDERS = ["source/", "port/linux/game/", "port/linux/src/", "port/android/guest/runtime/"]


def settings(**options):
    return SimpleNamespace(port_pgo="off", **options)


def test_the_mistakes_fail_with_the_flags_they_are_made_errors_by(tmp_path):
    """the flag sets themselves: after -Wall (the hosts'), and in place of
    -w (the guests' quiet units)"""
    assert let_through(["-std=gnu11", "-Wall", *build_common.OWN_CODE_ERRORS], tmp_path) == []
    assert let_through(["-std=gnu89", *build_common.QUIET_OWN_CODE_FLAGS], tmp_path) == []
    # (and why the quiet units do not take -w)
    assert let_through(["-std=gnu89", "-w", *build_common.OWN_CODE_ERRORS], tmp_path)


def test_android(repository, tmp_path):
    ndk = tmp_path / "ndk"
    (ndk / "toolchains" / "llvm" / "prebuilt" / "host" / "bin").mkdir(parents=True)
    n = Recorder()
    android_build.generate_android_build(n, settings(android_ndk=str(ndk)))
    check(n.units, tmp_path, [*GUEST_FOLDERS, "port/android/host/", "port/linux/vulkan/"])


@pytest.mark.parametrize("target", linux_guest_build.LINUX_GUESTS, ids=lambda target: target.name)
def test_linux(repository, tmp_path, target):
    """(each with VR, and arm64's with the base register, for their units)"""
    n = Recorder()
    arm64 = target.arch.base_register_flags is not None
    linux_guest_build._generate(n, settings(port_vr=True, guest_base=arm64), target, Path("port/linux/port.json"),
                                linux_guest_build.third_party_folder(target))
    check(n.units, tmp_path, [*GUEST_FOLDERS, "port/linux/host/", "port/android/host/", "port/linux/vulkan/"])


def test_windows(repository, tmp_path):
    n = Recorder()
    windows_build.generate_windows_build(n, settings(windows_guest=True))
    guest = {"image": windows_build.BUILD / "halo_guest.elf", "checked": windows_build.BUILD / "halo_guest.checked",
             "host_table_c": windows_build.BUILD / "host" / "host_import_table.c",
             "assets": windows_build.BUILD / "guest" / "gen" / "openh1.assets"}
    if sys.platform != "win32":
        windows_build._generate_host(n, settings(), guest)
    check(n.units, tmp_path, [*GUEST_FOLDERS, "port/windows/host/", "port/windows/src/"])


@pytest.mark.parametrize("name", ["macos", "ios"])
def test_apple(repository, tmp_path, name):
    from tools import ios_build
    platform = macos_build.macos_platform() if name == "macos" else ios_build.ios_platform("ios", "/sdk")
    n = Recorder()
    assert macos_build.generate_apple_build(n, settings(), platform)
    check(n.units, tmp_path, [*GUEST_FOLDERS, "port/macos/", "port/android/host/"]
          + (["port/ios/"] if name == "ios" else []))


def test_x86_game_abi(tmp_path):
    """the tests' 32-bit x86 builds of the engine's parts (tools/x86_game_abi.py)"""
    for flags in (x86_game_abi.GAME_FLAGS, x86_game_abi.PLATFORM_FLAGS, x86_game_abi.POSIX_FLAGS):
        assert let_through(warning_flags(" ".join(flags)), tmp_path) == []
