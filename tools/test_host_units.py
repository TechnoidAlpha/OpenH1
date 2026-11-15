"""Runs the C tests of the guest's host (port/android/host) that need
neither Android nor the guest: the image loader's checks (host_loader.c),
the processor's registers and frame walk (host_arch.c), and the guest's
memory system calls and crash report (host_memory.c), on the build
machine's processor (AArch64 or x86-64), with its C compiler."""
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "port" / "android" / "host"
TESTS = ROOT / "port" / "android" / "tests"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def run_test(tmp_path, name, sources, includes=(), arguments=(), flags=()):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if platform.machine().lower() not in ("x86_64", "amd64", "aarch64", "arm64"):
        pytest.skip("the host runs on AArch64 and x86-64 only")
    binary = tmp_path / name
    subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1", "-D_GNU_SOURCE", *flags,
         f"-I{ROOT / 'port' / 'android' / 'include'}", f"-I{HOST}", *[f"-I{path}" for path in includes],
         "-pthread", *[str(source) for source in sources], "-o", str(binary)],
        check=True,
    )
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_host_loader(tmp_path):
    run_test(tmp_path, "host_loader_test", [TESTS / "host_loader_test.c", HOST / "host_loader.c"])


def test_host_arch(tmp_path):
    run_test(tmp_path, "host_arch_test", [TESTS / "host_arch_test.c", HOST / "host_arch.c"])


def test_host_memory(tmp_path):
    if platform.system() != "Linux":
        pytest.skip("host_memory.c is the Linux and Android hosts' (macOS's is host_memory_base.c and port/macos, "
                    "Windows's port/windows/host)")
    run_test(tmp_path, "host_memory_test",
             [TESTS / "host_memory_test.c", HOST / "host_arch.c", HOST / "host_memory_watch.c",
              HOST / "host_watch_hash.c"])


def host_log_ring(broken=False):
    """log_keep and host_log_recent, as host_common.c has them"""
    text = (HOST / "host_common.c").read_text()
    start = text.index("/* the host's last lines, each ended by a newline")
    end = text.index("void host_log_set_sink(")
    code = text[start:end]
    if broken:
        # the negative control: the first line kept may be part of one
        code = code.replace("if (start && !(start > oldest", "if (0 && !(start > oldest")
    return code


@pytest.mark.parametrize("broken", [False, True])
def test_host_log_ring(tmp_path, broken):
    """the host's last lines, kept for Save Report's host.log (host_common.c):
    whole lines, the newest kept, a small buffer's; the negative control
    takes part of a line first"""
    (tmp_path / "host_log_ring.inc").write_text(host_log_ring(broken))
    if not broken:
        run_test(tmp_path, "host_log_ring_test", [TESTS / "host_log_ring_test.c"], [tmp_path])
        return
    with pytest.raises(AssertionError):
        run_test(tmp_path, "host_log_ring_test", [TESTS / "host_log_ring_test.c"], [tmp_path])


def host_crash_record():
    """the crash record and host_abort, as host_common.c has them"""
    text = (HOST / "host_common.c").read_text()
    record = text[text.index("/* ---------- the crash record */"):text.index("void host_fatal(")]
    abort = text[text.index("/* (the record is the SIGABRT's"):text.index("void host_exit(")]
    return record + abort


@pytest.mark.parametrize("broken", [False, True])
def test_host_crash_signal(tmp_path, broken):
    """a fatal signal writes the crash record (host_memory_watch.c's handlers):
    abort(), the guest's raise(), host_abort, a SIGABRT sent from outside and a
    segmentation fault, each with its frames, the process then ended by the
    signal; an exit writes none. The negative control leaves SIGABRT
    unhandled, as it was: no record for an abort"""
    if platform.system() not in ("Linux", "Darwin"):
        pytest.skip("the POSIX hosts' handlers (Windows's are host_memory_watch_win.c's)")
    (tmp_path / "host_crash_record.inc").write_text(host_crash_record())
    watch = HOST / "host_memory_watch.c"
    if broken:
        text = watch.read_text()
        assert ", SIGABRT," in text
        watch = tmp_path / "host_memory_watch.c"
        watch.write_text(text.replace(", SIGABRT,", ",").replace('#include "host', f'#include "{HOST}/host'))
    sources = [TESTS / "host_crash_signal_test.c", HOST / "host_arch.c", watch, HOST / "host_watch_hash.c"]
    # (the frame records the report walks, as the host is built: tools/linux_guest_build.py)
    flags = ["-fno-omit-frame-pointer"]
    if not broken:
        run_test(tmp_path, "host_crash_signal_test", sources, [tmp_path], [str(tmp_path)], flags)
        return
    with pytest.raises(AssertionError):
        run_test(tmp_path, "host_crash_signal_test", sources, [tmp_path], [str(tmp_path)], flags)


def test_frame_pacing_divisor(tmp_path):
    """battery saver's frames a second at each display's refresh rate
    (port/linux/src/frame_pacing.h, frame_pacing_divisor)"""
    run_test(tmp_path, "frame_pacing_test", [TESTS / "frame_pacing_test.c"], [ROOT / "port" / "linux" / "src"])


def test_game_data_scan(tmp_path):
    """the first start's search for disc images and maps folders, and the
    copy of a maps folder (port/linux/src/game_data_scan.c), in a made-up home
    folder, SD card and game folder"""
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    run_test(tmp_path, "game_data_scan_test",
             [TESTS / "game_data_scan_test.c", ROOT / "port" / "linux" / "src" / "game_data_scan.c",
              ROOT / "port" / "linux" / "src" / "posix_files.c"],
             [ROOT / "port" / "linux" / "src"], [str(scratch)])


def test_posix_net_interrupted(tmp_path):
    """a select interrupted by signals waits out its time: Winsock's is never
    interrupted, and the game takes a failed one as the connection lost"""
    if platform.system() != "Linux":
        pytest.skip("the hosts' sockets are Linux's")
    run_test(tmp_path, "posix_net_interrupt_test",
             [TESTS / "posix_net_interrupt_test.c", ROOT / "port" / "linux" / "src" / "posix_net.c"],
             [ROOT / "port" / "linux" / "src"])


def test_x32_boundary(tmp_path):
    """the x32 guest passes the host its pointers zero-extended
    (port/android/tests/x32_boundary_test.c), through the guest's own import
    stubs (tools/android_imports.py) and code generation (tools/guest_arch.py)"""
    if platform.machine().lower() not in ("x86_64", "amd64"):
        pytest.skip("the x32 guest runs on x86-64")
    cc, ld, objcopy, nm = (shutil.which(name) for name in ("clang", "ld.lld", "llvm-objcopy", "llvm-nm"))
    if not (cc and ld and objcopy and nm):
        pytest.skip("needs clang, ld.lld, llvm-objcopy and llvm-nm")
    sys.path.insert(0, str(ROOT / "tools"))
    from guest_arch import X32
    imports = tmp_path / "imports.list"
    imports.write_text("host_check\nhost_pointer\nhost_check_returned\n")
    subprocess.run([sys.executable, str(ROOT / "tools" / "android_imports.py"), "--arch", "x32",
                    str(tmp_path / "imports.s"), str(imports)], check=True)
    subprocess.run([cc, *X32.target_flags, "-O2", "-ffreestanding", "-c", str(TESTS / "x32_boundary_guest.c"),
                    "-o", str(tmp_path / "guest.o")], check=True)
    subprocess.run([cc, f"--target={X32.assembler_target('gnu')}", "-c", str(tmp_path / "imports.s"),
                    "-o", str(tmp_path / "imports.o")], check=True)
    (tmp_path / "guest.ld").write_text(
        "SECTIONS { . = 0x60000000; .text : { *(.text .text.*) } .rodata : { *(.rodata .rodata.*) } "
        ".data : { *(.data .data.*) } /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) } }\n")
    subprocess.run([ld, "-m", X32.ld_emulation, "-static", "-nostdlib", "-T", str(tmp_path / "guest.ld"),
                    str(tmp_path / "guest.o"), str(tmp_path / "imports.o"), "-o", str(tmp_path / "guest.elf")],
                   check=True)
    subprocess.run([objcopy, "-O", "binary", str(tmp_path / "guest.elf"), str(tmp_path / "guest.bin")], check=True)
    symbols = {}
    for line in subprocess.run([nm, str(tmp_path / "guest.elf")], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        address, _, name = line.split()
        symbols[name] = int(address, 16) - 0x60000000
    binary = tmp_path / "x32_boundary_test"
    subprocess.run([cc, "-std=gnu11", "-Wall", "-Werror", "-O1", "-D_GNU_SOURCE", "-pthread",
                    str(TESTS / "x32_boundary_test.c"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary), str(tmp_path / "guest.bin"), hex(symbols["guest_entry"]),
                             hex(symbols["__host_import_table"])], capture_output=True, text=True)
    assert result.returncode == 0 and result.stdout.strip() == "ok", result.stdout + result.stderr


def sdl_build():
    """an SDL3 the builds made (its headers and library), or None"""
    for headers in sorted(ROOT.glob("build/*/third_party/SDL3/include")):
        build = headers.parent.parent.parent
        # (one this machine runs: not the Android or iOS builds')
        if build.name.startswith(("android", "ios")):
            continue
        if platform.system() == "Darwin":
            libraries = sorted(build.glob("libSDL3.*dylib"))
        else:
            libraries = sorted(build.glob("**/libSDL3.so*"))
        if (headers / "SDL3" / "SDL.h").exists() and libraries:
            return headers, libraries[0]
    return None


def test_host_screenshot(tmp_path):
    """the screenshot key's files (port/android/host/host_screenshot.c):
    their names, the folder made, the PNG's pixels, two in one second, a
    folder that cannot be made; with an SDL3 a build made"""
    sdl = sdl_build()
    if not sdl:
        pytest.skip("no SDL3 built (ninja macos, linux_x86_64 or linux_arm64 first)")
    headers, library = sdl
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "host_screenshot_test"
    subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", f"-I{HOST}", f"-I{headers}",
         f"-I{ROOT / 'port' / 'linux' / 'src'}", str(TESTS / "host_screenshot_test.c"), str(HOST / "host_screenshot.c"),
         str(library), f"-Wl,-rpath,{library.parent}", "-o", str(binary)],
        check=True,
    )
    result = subprocess.run([str(binary), str(scratch)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
