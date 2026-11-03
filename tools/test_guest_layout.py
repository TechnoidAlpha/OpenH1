"""The guests' memory layout agrees between their two halves, for each
guest architecture (tools/guest_arch.py), whatever computer runs the test:
the Xbox window that the guest hands out (port/linux/src/platform.h's
PLATFORM_CONTIGUOUS_SIZE) is the one the host reserves for it
(port/android/include/halo_android_abi.h's HALO_GUEST_WINDOW_SIZE, for the
host's processor), the image base and reserve are the build scripts', and
the caches are the window's (port/linux/include/halo_port_capacity.h).

The headers are run through clang's preprocessor for each target; nothing is
compiled, so neither a guest's C library nor an arm64 computer is needed."""
import re
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, Sequence

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from guest_arch import ARM64_32, GUEST_ARCHES, X32  # noqa: E402
from tools import build_common  # noqa: E402
import guest_image_check  # noqa: E402

ABI_HEADER = ROOT / "port" / "android" / "include" / "halo_android_abi.h"
# (the hosts' processors: the guest's ELF architecture)
HOST_TARGETS = {"aarch64": "aarch64-linux-gnu", "x86_64": "x86_64-linux-gnu"}
DESKTOP_WINDOW = 0x20000000
# the Xbox's texture cache, in 16 KB pages (22 MB)
XBOX_TEXTURE_CACHE_PAGES = 0x580


def clang() -> str:
    cc = shutil.which("clang")
    if not cc:
        pytest.skip("needs clang")
    return cc


def macros(flags: Sequence[str], text: str) -> Dict[str, int]:
    """the integer macros that text defines, preprocessed with flags"""
    output = subprocess.run([clang(), *flags, "-E", "-dM", "-x", "c", "-"], input=text, capture_output=True,
                            text=True, check=True).stdout
    values, names = {}, {}
    for line in output.splitlines():
        match = re.fullmatch(r"#define (\w+) \(?(0x[0-9A-Fa-f]+|\d+)[uUlL]*\)?", line.strip())
        if match:
            values[match.group(1)] = int(match.group(2), 0)
        match = re.fullmatch(r"#define (\w+) ([A-Za-z_]\w*)", line.strip())
        if match:
            names[match.group(1)] = match.group(2)
    # (and those defined as another's)
    for name, other in names.items():
        if other in values:
            values[name] = values[other]
    return values


def without_includes(path: Path) -> str:
    """a header's text without its #include lines, which need the build's
    include paths and only declare"""
    return "".join(line for line in path.read_text().splitlines(keepends=True)
                   if not line.lstrip().startswith("#include"))


def guest_macros(flags: Sequence[str]) -> Dict[str, int]:
    text = (without_includes(ROOT / "port" / "linux" / "src" / "platform.h")
            + without_includes(ROOT / "port" / "linux" / "include" / "halo_port_capacity.h"))
    return macros(flags, text)


def host_macros(elf_arch: str, defines: Sequence[str] = ()) -> Dict[str, int]:
    return macros([f"--target={HOST_TARGETS[elf_arch]}", "-ffreestanding", *defines], ABI_HEADER.read_text())


def clang_takes(flag: str) -> bool:
    """whether the PATH's clang knows the flag (the builds' is the pinned
    LLVM's: an older system clang, as Ubuntu 22.04's 14, does not know
    -fno-define-target-os-macros, which the macros read here do not need)"""
    return subprocess.run([clang(), flag, "-E", "-x", "c", "-"], input="", capture_output=True,
                          text=True).returncode == 0


def guest_flags(arch) -> Sequence[str]:
    # (as tools/android_build.py's guest_abi_flags)
    flags = [*arch.target_flags, "-DHALO_GUEST=1", *(["-DHALO_GUEST_MACHO=1"] if arch.darwin_assembly else []),
             *arch.defines]
    return [flag for flag in flags if flag != "-fno-define-target-os-macros" or clang_takes(flag)]


@pytest.mark.parametrize("name", sorted(GUEST_ARCHES))
def test_window_is_the_hosts(name):
    arch = GUEST_ARCHES[name]
    guest = guest_macros(guest_flags(arch))
    host = host_macros(arch.elf_arch)
    assert guest["PLATFORM_CONTIGUOUS_BASE"] == host["HALO_GUEST_WINDOW_BASE"]
    assert guest["PLATFORM_CONTIGUOUS_SIZE"] == host["HALO_GUEST_WINDOW_SIZE"]


@pytest.mark.parametrize("name", sorted(GUEST_ARCHES))
def test_image_range_is_the_hosts(name):
    arch = GUEST_ARCHES[name]
    host = host_macros(arch.elf_arch)
    assert host["HALO_GUEST_IMAGE_BASE"] == arch.image_base
    # (tools/guest_image_check.py's copy of HALO_GUEST_IMAGE_RESERVE)
    assert host["HALO_GUEST_IMAGE_RESERVE"] == guest_image_check.IMAGE_RESERVE
    window_end = host["HALO_GUEST_WINDOW_BASE"] + host["HALO_GUEST_WINDOW_SIZE"]
    image_end = arch.image_base + host["HALO_GUEST_IMAGE_RESERVE"]
    assert image_end <= host["HALO_GUEST_WINDOW_BASE"] or arch.image_base >= window_end


@pytest.mark.parametrize("name", sorted(GUEST_ARCHES))
def test_instrumented_image_range_is_the_hosts(name):
    """an instrumented build's (configure.py --pgo=generate,
    HALO_PGO_GENERATE) larger range, which still keeps off the window and
    the x32 image's 2 GB line"""
    arch = GUEST_ARCHES[name]
    host = host_macros(arch.elf_arch, build_common.pgo_generate_defines(SimpleNamespace(port_pgo="generate")))
    assert host["HALO_GUEST_IMAGE_BASE"] == arch.image_base
    assert host["HALO_GUEST_IMAGE_RESERVE"] == guest_image_check.PGO_GENERATE_IMAGE_RESERVE
    assert guest_image_check.PGO_GENERATE_IMAGE_RESERVE > guest_image_check.IMAGE_RESERVE
    window_end = host["HALO_GUEST_WINDOW_BASE"] + host["HALO_GUEST_WINDOW_SIZE"]
    image_end = arch.image_base + host["HALO_GUEST_IMAGE_RESERVE"]
    assert image_end <= host["HALO_GUEST_WINDOW_BASE"] or arch.image_base >= window_end
    assert image_end <= 0x80000000 or arch.image_base >= 0x80000000
    # (and no other build has the define)
    for mode in ("use", "off"):
        assert build_common.pgo_generate_defines(SimpleNamespace(port_pgo=mode)) == []


def test_window_sizes():
    """every guest has the desktop builds' window and texture cache (the AArch64 guests' image above the window,
    the x86-64 guest's below it); an Xbox map takes the Xbox's 22 MB of it on the AArch64 guests, and their sound
    cache is the Xbox's"""
    x32 = guest_macros(guest_flags(X32))
    native = guest_macros(["--target=i686-linux-gnu"])
    arm64 = guest_macros(guest_flags(ARM64_32))
    assert x32["PLATFORM_CONTIGUOUS_SIZE"] == native["PLATFORM_CONTIGUOUS_SIZE"] == DESKTOP_WINDOW
    assert arm64["PLATFORM_CONTIGUOUS_SIZE"] == DESKTOP_WINDOW
    for name in ("HALO_PORT_TEXTURE_CACHE_PAGE_COUNT", "HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT",
                 "HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT", "HALO_PORT_SOUND_CACHE_SIZE"):
        assert x32[name] == native[name], name
    assert arm64["HALO_PORT_TEXTURE_CACHE_PAGE_COUNT"] == x32["HALO_PORT_TEXTURE_CACHE_PAGE_COUNT"] == 0x4000
    assert x32["HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT"] == x32["HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT"] == 0x4000
    assert arm64["HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT"] == XBOX_TEXTURE_CACHE_PAGES
    assert XBOX_TEXTURE_CACHE_PAGES < arm64["HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT"] < 0x4000
    assert arm64["HALO_PORT_SOUND_CACHE_SIZE"] == 0x400000


@pytest.mark.parametrize("name", sorted(GUEST_ARCHES))
def test_caches_fit_above_the_game_state(name):
    """the window hands out the caches top-down, above the game state (xbox_memory.c's caches_fit_the_window), with
    room left for the Direct3D resources a Custom Edition map makes"""
    guest = guest_macros(guest_flags(GUEST_ARCHES[name]))
    game_state_end = guest["HALO_PORT_GAME_STATE_BASE_ADDRESS"] + guest["HALO_PORT_GAME_STATE_CPU_SIZE"] + \
        guest["HALO_PORT_GAME_STATE_GPU_SIZE"] - guest["PLATFORM_CONTIGUOUS_BASE"]
    caches = guest["HALO_PORT_TEXTURE_CACHE_PAGE_COUNT"] * 0x4000 + guest["HALO_PORT_SOUND_CACHE_SIZE"]
    assert game_state_end + caches + 0x8000000 <= guest["PLATFORM_CONTIGUOUS_SIZE"]
