"""The check that the APK's native libraries load on kernels of 16 KB pages (tools/android_elf_alignment.py): a
library linked with -z max-page-size=16384 passes, one linked for 4 KB pages (the negative control) is refused,
and the build runs the check on both of the APK's libraries.

    python -m pytest -q tools/test_android_elf_alignment.py
"""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import android_elf_alignment  # noqa: E402


def library(tmp_path, page):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc or sys.platform != "linux":
        pytest.skip("needs a C compiler that links ELF shared libraries")
    source = tmp_path / "library.c"
    source.write_text("int answer = 42;\nint function(void) { return answer; }\n")
    output = tmp_path / f"library_{page}.so"
    subprocess.run([cc, "-shared", "-fPIC", f"-Wl,-z,max-page-size={page}", "-Wl,-z,common-page-size=4096",
                    str(source), "-o", str(output)], check=True)
    return output


def test_16k_library_passes(tmp_path):
    path = library(tmp_path, 16384)
    assert android_elf_alignment.problems(path) == []
    stamp = tmp_path / "stamp"
    assert android_elf_alignment.main([str(stamp), str(path)]) == 0
    assert stamp.read_text() == f"{path}\n"


def test_4k_library_refused(tmp_path):
    path = library(tmp_path, 4096)
    found = android_elf_alignment.problems(path)
    assert found and "less than 0x4000" in found[0]
    stamp = tmp_path / "stamp"
    assert android_elf_alignment.main([str(stamp), str(path)]) == 1
    assert not stamp.exists()


def test_build_checks_both_libraries():
    text = (ROOT / "tools" / "android_build.py").read_text()
    assert 'inputs=[libmain, staged_sdl],\n            implicit=[Path("tools/android_elf_alignment.py")]' in text
    assert text.count("max-page-size=16384") >= 2
