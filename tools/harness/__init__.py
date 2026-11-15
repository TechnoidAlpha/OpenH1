"""Asset-free regression tests: the engine's real code compiled against a fake world (README.md)."""

import functools
import os
import platform
import re
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
HARNESS = Path(__file__).resolve().parent


def read(relative):
    """A source file of the repository, as the compiler reads it."""
    return (ROOT / relative).read_text(encoding="latin-1")


def function(source, name):
    """A function's definition (static, inline or not), taken by matching braces; LookupError if not found."""
    match = re.search(r"^(?:static |__inline )?[\w *]+?\b" + re.escape(name) + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    if not match:
        raise LookupError(f"function not found in the sources: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def inline(source, name):
    """An __inline header function, as a static one."""
    return function(source, name).replace("__inline", "static", 1)


def enum_with(source, member):
    """The whole enum that declares a member."""
    at = source.index(member)
    start = source.rindex("enum", 0, at)
    return source[start:source.index("};", at) + 2]


def structure(source, name):
    """An actual named structure's definition, including nested unions/structures."""
    match = re.search(r"^struct\s+" + re.escape(name) + r"\s*\{", source, re.M)
    if not match:
        raise LookupError(f"structure not found in the sources: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():source.index(";", end) + 1]


def constant(source, name):
    """An enum constant's or #define's integer value, following a name it is set to (as the port's capacities are)."""
    match = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\w+)", source) or \
        re.search(r"#define\s+" + re.escape(name) + r"\s+(\w+)", source)
    if not match:
        raise LookupError(f"constant not found in the sources: {name}")
    value = match.group(1)
    if re.fullmatch(r"0x[0-9A-Fa-f]+|\d+", value):
        return int(value, 0)
    return constant(source + read("port/linux/include/halo_port_capacity.h"), value)


def player_settings_slots():
    """port/linux/game/player_settings.h's slots of a reader's values kept for each local player
    (PLAYER_SETTINGS_SLOTS, PLAYER_SETTINGS_SLOT), as the readers of a player's own settings use them"""
    header = read("port/linux/game/player_settings.h")
    return "".join(re.findall(r"^#define PLAYER_SETTINGS_SLOTS?\b(?:[^\n]*\\\n)*[^\n]*\n", header, re.M))


# the latest texts mutated() made, which mark a build that includes one as a negative control's (build)
_faulty = []


def mutated(text, before, after):
    """The code with one deliberate fault (a negative control); LookupError once the code no longer has it."""
    if text.count(before) != 1:
        raise LookupError(f"negative control no longer applies (found {text.count(before)} times): {before!r}")
    faulty = text.replace(before, after, 1)
    _faulty.append(faulty)
    del _faulty[:-64]
    return faulty


_directory = tempfile.TemporaryDirectory(prefix="halo-harness-")


# The game's 32-bit x86 ABI runs where the processor is x86's (with a 32-bit C runtime: gcc-multilib, lib32-glibc).
# Elsewhere (Apple silicon's macOS, which has no 32-bit runtime at all; 64-bit ARM Linux) the tests compile for the
# machine's own 64-bit ABI instead, and those whose results depend on the game's ABI are skipped (needs_32_bit_abi).
NATIVE_64_BIT = platform.system() == "Darwin" or platform.machine().lower() not in ("x86_64", "amd64", "i386", "i686")

# (without fused multiply-add, as every build compiles the game: AArch64's compilers fuse by default)
ABI_FLAGS = ["-ffp-contract=off"] if NATIVE_64_BIT else ["-m32"]

NEEDS_32_BIT_ABI = ("needs the game's 32-bit x86 ABI, which this machine cannot run: run it on x86-64 Linux with "
                    "gcc-multilib (tools/harness/README.md)")


def needs_32_bit_abi(why):
    """A test file's mark: skipped, with its reason, where the tests compile for a 64-bit ABI."""
    return pytest.mark.skipif(NATIVE_64_BIT, reason=f"{NEEDS_32_BIT_ABI}: {why}")


def skip_without_32_bit_abi(dependent, *names):
    """Where the tests compile for a 64-bit ABI, skips a case or a negative control that dependent ({name: why}) names,
    by its own name or its case's (a negative control of a case that cannot pass there would pass for nothing)."""
    if NATIVE_64_BIT:
        for name in names:
            if name in dependent:
                pytest.skip(f"{NEEDS_32_BIT_ABI}: {dependent[name]}")


CHECK_FAILED = 1  # a case's exit status when a CHECK fails (harness.h)


# A negative control's build, whose generated includes hold a fault mutated() made, is compiled without optimisation:
# most of the tests' time was the controls' -O2 compiles, and a control asks only that its case fail. The tests'
# own builds keep -O2. (HARNESS_OPTIMISE=-O0 compiles every build so, to check that each case passes there too.)
TEST_OPTIMISATION = os.environ.get("HARNESS_OPTIMISE", "-O2")
CONTROL_OPTIMISATION = "-O0"

_controls = set()  # the generated includes found to be a control's


def build(test, generated):
    """tests/<test>.c compiled with the generated includes ((name, text), ...) for the game's 32-bit ABI (or, where
    it cannot run, the machine's own: NATIVE_64_BIT); without optimisation when it is a negative control's."""
    if generated not in _controls and any(faulty in text for _, text in generated for faulty in _faulty):
        _controls.add(generated)
    return _compile(test, generated, CONTROL_OPTIMISATION if generated in _controls else TEST_OPTIMISATION)


@functools.lru_cache(maxsize=None)
def _compile(test, generated, optimisation):
    work = Path(_directory.name) / f"{test}-{optimisation[1:]}-{abs(hash(generated)):x}"
    work.mkdir(parents=True, exist_ok=True)
    for name, text in generated:
        (work / name).write_text(text)
    executable = work / (test + (".exe" if os.name == "nt" else ""))
    command = [os.environ.get("CC", "clang"), *ABI_FLAGS, "-std=gnu99", optimisation, "-Wall", "-Werror",
               "-Wno-unused-function", "-Wno-unused-variable", "-I", str(HARNESS / "include"), "-I", str(work),
               str(HARNESS / "tests" / f"{test}.c"), "-o", str(executable)]
    # MSVC's runtime includes maths; there is no separate m.lib on Windows.
    if os.name == "nt":
        command.append("-D_CRT_SECURE_NO_WARNINGS=")
    else:
        command.append("-lm")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{test}.c does not compile:\n{result.stderr}")
    return executable


def run(executable, case):
    """Run one case: (exit status, output)."""
    result = subprocess.run([str(executable), case], capture_output=True, text=True, errors="replace", timeout=60)
    return result.returncode, (result.stdout + result.stderr).strip()
