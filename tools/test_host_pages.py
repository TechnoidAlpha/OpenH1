"""The guest's memory without the base register (port/android/host/host_memory.c: Android's, the Linux arm64 and
x86-64 builds') and its write tracking (host_memory_watch.c) on kernels whose pages are larger than the guest's
4 KB: 16 KB, as Android 15's kernels that use them, and 64 KB, as some ARM Linux ones. The build machine's kernel
(4 KB pages) plays them (port/android/tests/host_pages_sim.h), refusing every call a kernel of that size would
refuse. Negative controls: a host that takes its pages for 4 KB, and the memory with each of its rules taken out,
must fail.

    python -m pytest -q tools/test_host_pages.py
"""
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "port" / "android" / "host"
TESTS = ROOT / "port" / "android" / "tests"
MEMORY = HOST / "host_memory.c"

# (the rule taken out: the text of host_memory.c and what replaces it)
MUTATIONS = {
    # each host page given the protection asked of its guest pages, not their union
    "union": ("\t\tafter = host_page_union(host);\n",
              "\t\tafter = operation == UPDATE_UNMAP ? host_page_union(host) : protection;\n"),
    # a guest page mapped into a host page in use keeps what was there
    "zeroed": ("\t\t\tmemset((void *)(uintptr_t)from, 0, (size_t)(to - from));\n", ""),
    # the write tracking not told of the window's host pages whose protection is set again
    "forget": ("\tforget_window_range(first, host_round_up(end) - first);\n", ""),
    # a pool's host page with no guest page left kept mapped
    "give_back": ("\t\t\tif (kind == PAGE_POOL && (before >= 0 || *used))\n", "\t\t\tif (0)\n"),
}


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(tmp_path, mutation=None):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if sys.platform != "linux" or platform.machine().lower() not in ("x86_64", "amd64", "aarch64", "arm64"):
        pytest.skip("the host's memory is Linux's (AArch64 or x86-64)")
    if subprocess.run(["getconf", "PAGESIZE"], capture_output=True, text=True).stdout.strip() != "4096":
        pytest.skip("the simulation plays larger pages on a kernel of 4 KB pages")
    # (a mutated copy beside a copy of the test, which includes it as ../host/host_memory.c)
    test_source = TESTS / "host_memory_pages_test.c"
    if mutation:
        (tmp_path / "host").mkdir()
        (tmp_path / "tests").mkdir()
        old, new = MUTATIONS[mutation]
        text = MEMORY.read_text()
        assert text.count(old) == 1, f"the mutation {mutation} does not apply"
        (tmp_path / "host" / "host_memory.c").write_text(text.replace(old, new))
        test_source = tmp_path / "tests" / "host_memory_pages_test.c"
        shutil.copy(TESTS / "host_memory_pages_test.c", test_source)
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1", "-D_GNU_SOURCE",
             f"-I{ROOT / 'port' / 'android' / 'include'}", f"-I{HOST}", "-pthread"]
    simulated = ["-include", str(TESTS / "host_pages_sim.h")]
    objects = []
    for source, extra in ((TESTS / "host_pages_sim.c", []), (test_source, simulated),
                          (HOST / "host_memory_watch.c", simulated), (HOST / "host_watch_hash.c", simulated),
                          (HOST / "host_arch.c", simulated)):
        obj = tmp_path / (source.stem + ".o")
        subprocess.run([cc, *flags, *extra, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    binary = tmp_path / "host_memory_pages_test"
    subprocess.run([cc, "-pthread", *objects, "-o", str(binary)], check=True)
    return binary


def run(binary, *arguments, timeout=60):
    return subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=timeout)


@pytest.mark.parametrize("page_size", ["16384", "65536"])
def test_host_pages(tmp_path, page_size):
    result = run(build(tmp_path), page_size)
    assert result.returncode == 0, result.stdout + result.stderr


def test_host_pages_taken_for_4k(tmp_path):
    """a host that takes the kernel's 16 KB pages for 4 KB ones (as host_memory.c did) makes calls the kernel
    refuses"""
    result = run(build(tmp_path), "16384", "4096")
    assert result.returncode != 0
    assert "calls a kernel of them refuses" in result.stdout or result.returncode < 0, result.stdout


@pytest.mark.parametrize("mutation", sorted(MUTATIONS))
def test_host_pages_negative(tmp_path, mutation):
    binary = build(tmp_path, mutation)
    try:
        result = run(binary, "16384", timeout=15)
    except subprocess.TimeoutExpired:
        # (a write the tracking lets through to a page the kernel still refuses it: faulted on for ever, as the
        # game would hang)
        return
    assert result.returncode != 0, f"{mutation}: the test passes without the rule\n{result.stdout}"
    assert "FAIL" in result.stdout or result.returncode < 0
