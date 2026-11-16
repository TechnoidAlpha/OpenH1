"""Tests of tools/symbolize_crash.py, offline: a small made-up crash's lines
(host_memory_watch.c's report_crash) and a tiny guest image written here.
The addresses are placed as the builds give them (the base register's
host addresses less the base, the x32 and Android guests' as they are, the
host's from its image's start), the reference address must be the image's
(another build's image is refused), the guest image an executable carries
is found, and the frames come out with their functions; with the pinned
LLVM, an image linked here from converted Darwin assembly
(tools/android_asm_convert.py) gives the source line of its crash."""
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import symbolize_crash  # noqa: E402
from symbolize_crash import SymbolizeError  # noqa: E402

IMAGE_BASE = 0xa0000000
# the image's functions: (name, offset from IMAGE_BASE)
FUNCTIONS = [("guest_start", 0x000), ("write_to_error_file", 0x100), ("object_update", 0x200),
             ("crash_here", 0x300)]
BASE = 0x0000000300000000
HOST = 0x0000000102a4c000

CRASH = f"""signal 11 at address 0x0: pc {BASE + IMAGE_BASE + 0x310:016x} lr {BASE + IMAGE_BASE + 0x248:016x} sp {BASE + 0x7fff0000:016x} fp {BASE + 0x7fff0010:016x}
  guest memory at {BASE:016x}
  host image at {HOST:016x}: halo
  in the guest image: addr2line -e halo_guest.elf 0x{IMAGE_BASE + 0x310:x} 0x{IMAGE_BASE + 0x248:x}
  x0  0000000000000000 0000000000000001 0000000000000002 0000000000000003
  sp  {BASE + 0x7fff0000:016x} pc  {BASE + IMAGE_BASE + 0x310:016x} cpsr 60000000
  frame  0: return {BASE + IMAGE_BASE + 0x248:016x}
  frame  1: return {BASE + IMAGE_BASE + 0x40:016x}
  frame  2: return {HOST + 0x1234:016x}
  frame  3: return 00007ff800001000

--- the log's last lines
reference address: {IMAGE_BASE + 0x100:x}
"""


def elf(functions, is64=True, base=IMAGE_BASE, machine=183):
    """a tiny ELF file: one loadable segment of code at base, and a symbol
    table of functions"""
    code_size = 0x1000
    strtab = b"\0"
    names = []
    for name, _ in functions:
        names.append(len(strtab))
        strtab += name.encode() + b"\0"
    shstrtab = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    if is64:
        sym = struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0) + b"".join(
            struct.pack("<IBBHQQ", names[i], 0x12, 0, 1, base + offset, 16) for i, (_, offset) in enumerate(functions))
        ehsize, phentsize, shentsize, symentsize = 64, 56, 64, 24
    else:
        sym = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0) + b"".join(
            struct.pack("<IIIBBH", names[i], base + offset, 16, 0x12, 0, 1) for i, (_, offset) in enumerate(functions))
        ehsize, phentsize, shentsize, symentsize = 52, 32, 40, 16
    text_offset = 0x1000
    sym_offset = text_offset + code_size
    str_offset = sym_offset + len(sym)
    shstr_offset = str_offset + len(strtab)
    sh_offset = (shstr_offset + len(shstrtab) + 7) & ~7
    sections = [  # name, type, flags, address, offset, size, link, info, align, entsize
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 6, base, text_offset, code_size, 0, 0, 16, 0),
        (7, 2, 0, 0, sym_offset, len(sym), 3, 1, 8, symentsize),
        (15, 3, 0, 0, str_offset, len(strtab), 0, 0, 1, 0),
        (23, 3, 0, 0, shstr_offset, len(shstrtab), 0, 0, 1, 0),
    ]
    ident = b"\x7fELF" + bytes([2 if is64 else 1, 1, 1]) + bytes(9)
    if is64:
        header = ident + struct.pack("<HHIQQQIHHHHHH", 2, machine, 1, base, ehsize, sh_offset, 0, ehsize,
                                     phentsize, 1, shentsize, len(sections), 4)
        program = struct.pack("<IIQQQQQQ", 1, 5, text_offset, base, base, code_size, code_size, 0x1000)
        table = b"".join(struct.pack("<IIQQQQIIQQ", *section) for section in sections)
    else:
        header = ident + struct.pack("<HHIIIIIHHHHHH", 2, machine, 1, base, ehsize, sh_offset, 0, ehsize,
                                     phentsize, 1, shentsize, len(sections), 4)
        program = struct.pack("<IIIIIIII", 1, text_offset, base, base, code_size, code_size, 5, 0x1000)
        table = b"".join(struct.pack("<IIIIIIIIII", *section) for section in sections)
    data = bytearray(sh_offset + len(table))
    data[:len(header)] = header
    data[len(header):len(header) + len(program)] = program
    data[sym_offset:sym_offset + len(sym)] = sym
    data[str_offset:str_offset + len(strtab)] = strtab
    data[shstr_offset:shstr_offset + len(shstrtab)] = shstrtab
    data[sh_offset:] = table
    return bytes(data)


def fake_symbolizer(tmp_path):
    """an llvm-symbolizer that answers each address with its function's
    line (FUNCTIONS) and the address, as llvm-symbolizer prints"""
    script = tmp_path / "llvm-symbolizer"
    script.write_text(f"""#!{sys.executable}
import sys
functions = {[(IMAGE_BASE + offset, name) for name, offset in FUNCTIONS]!r}
for argument in sys.argv[1:]:
    if argument.startswith("-"):
        continue
    address = int(argument, 16)
    name = [n for a, n in functions if a <= address][-1] if address >= functions[0][0] else "??"
    print(name)
    print(f"source/test/{{name}}.c:{{address - {IMAGE_BASE}}}:0" if name != "??" else "??:0:0")
    print()
""")
    script.chmod(0o755)
    return str(script)


def test_addresses_are_placed():
    place = symbolize_crash.place
    # (the base register's: the host's address less the base)
    assert place(BASE + 0xa0000310, BASE, HOST) == ("guest", 0xa0000310)
    assert place(HOST + 0x40, BASE, HOST) == ("host", 0x40)
    # (an address below the base is the host's, even under 4 GB)
    assert place(0x1000, BASE, None) == ("", 0x1000)
    # (no base: the x32 and Android guests' addresses are their own, below 4 GB)
    assert place(0x40001234, None, 0x555555554000) == ("guest", 0x40001234)
    assert place(0x555555555000, None, 0x555555554000) == ("host", 0x1000)
    assert place(0x7f0000000000, None, None) == ("", 0x7f0000000000)


def test_the_crash_is_parsed():
    crash = symbolize_crash.parse_crash("debug.txt's earlier lines\n" + CRASH)
    assert crash["base"] == BASE and crash["host"] == HOST and crash["host_name"] == "halo"
    addresses = [entry for entries in crash["addresses"].values() for entry in entries]
    assert addresses[:2] == [("pc", BASE + IMAGE_BASE + 0x310, False), ("lr", BASE + IMAGE_BASE + 0x248, True)]
    assert [address for _, address, _ in addresses[2:]] == [BASE + IMAGE_BASE + 0x248, BASE + IMAGE_BASE + 0x40,
                                                            HOST + 0x1234, 0x7ff800001000]
    # (the crash's lines end at the log's)
    assert not any(line.startswith("---") for _, line in crash["lines"])
    with pytest.raises(SymbolizeError):
        symbolize_crash.parse_crash("no crash here\n")


def test_the_reference_address_is_found(tmp_path):
    report = tmp_path / "report.zip"
    with zipfile.ZipFile(report, "w") as archive:
        archive.writestr("OpenH1-report/crash.txt", CRASH)
        archive.writestr("OpenH1-report/report.md", "## The crash\n\n- Reference address: a0000100 (debug.txt's)\n")
    text, reference = symbolize_crash.report_text(report)
    assert reference == "a0000100" and text == CRASH
    # (a log's: the last session's before the crash)
    log = "reference address: 1234\n...\nreference address: a0000100\n" + CRASH.split("---")[0]
    crash = symbolize_crash.parse_crash(log)
    assert symbolize_crash.last_reference(log, crash["offset"]) == "a0000100"


@pytest.mark.parametrize("is64", [True, False])
def test_the_image_is_checked(is64):
    image = elf(FUNCTIONS, is64=is64)
    assert symbolize_crash.elf_symbols(image)["write_to_error_file"] == IMAGE_BASE + 0x100
    symbolize_crash.check_reference(image, "a0000100", Path("halo_guest.elf"))
    with pytest.raises(SymbolizeError, match="not the build that crashed"):
        symbolize_crash.check_reference(image, "a0000104", Path("halo_guest.elf"))
    with pytest.raises(SymbolizeError, match="not the game's guest image"):
        symbolize_crash.check_reference(elf(FUNCTIONS[2:], is64=is64), "a0000100", Path("other.elf"))


def test_functions_are_named():
    table = symbolize_crash.functions(elf(FUNCTIONS))
    assert symbolize_crash.nearest_function(table, IMAGE_BASE + 0x247) == "object_update+0x47"
    assert symbolize_crash.nearest_function(table, IMAGE_BASE - 1) is None


def test_the_embedded_image_is_found(tmp_path):
    image = elf(FUNCTIONS)
    # (an executable, itself an ELF file of other symbols, carrying the image)
    executable = elf([("main", 0)], base=0x1000, machine=62) + b"\0" * 13 + image + b"after"
    assert symbolize_crash.embedded_guest(executable) == image
    assert symbolize_crash.embedded_guest(elf([("main", 0)], base=0x1000)) is None
    host = tmp_path / "halo"
    host.write_bytes(executable)
    guest, found, host_path = symbolize_crash.find_build([tmp_path], None, None)
    assert found == image and guest == host and host_path == host


def test_the_frames_are_symbolized(tmp_path):
    image = tmp_path / "halo_guest.elf"
    image.write_bytes(elf(FUNCTIONS))
    out = symbolize_crash.symbolize_report(CRASH, "a0000100", image, image.read_bytes(), None,
                                           fake_symbolizer(tmp_path))
    text = "\n".join(out)
    # (the faulting instruction itself, the return addresses one before)
    assert "pc: guest a0000310 crash_here+0x10  source/test/crash_here.c:784" in text
    assert "lr: guest a0000248 object_update+0x48  source/test/object_update.c:583" in text
    assert "guest a0000040 guest_start+0x40" in text
    # (the host's frame without its executable, and an address of neither)
    assert "halo+0x1234" in text
    assert "00007ff800001000 (neither the guest's nor the host's image)" in text
    with pytest.raises(SymbolizeError, match="not the build that crashed"):
        symbolize_crash.symbolize_report(CRASH, "a0000200", image, image.read_bytes(), None,
                                         fake_symbolizer(tmp_path))


def test_the_command_line(tmp_path):
    (tmp_path / "build").mkdir()
    (tmp_path / "build" / "halo_guest.elf").write_bytes(elf(FUNCTIONS))
    report = tmp_path / "crash.txt"
    report.write_text(CRASH)
    result = subprocess.run([sys.executable, str(ROOT / "tools/symbolize_crash.py"), str(report),
                             str(tmp_path / "build"), "--symbolizer", fake_symbolizer(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert "crash_here+0x10" in result.stdout
    report.write_text(CRASH.replace(f"reference address: {IMAGE_BASE + 0x100:x}", "reference address: 1"))
    result = subprocess.run([sys.executable, str(ROOT / "tools/symbolize_crash.py"), str(report),
                             str(tmp_path / "build"), "--symbolizer", fake_symbolizer(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode == 1 and "not the build that crashed" in result.stderr


def test_a_release_s_symbols_zip_is_read(tmp_path):
    """a release's OpenH1-<machine>-symbols.zip (tools/release_names.py), given as it is downloaded, with or
    without its build's zip: its halo_guest.debug.elf is the image"""
    symbols = tmp_path / "OpenH1-Linux-symbols.zip"
    with zipfile.ZipFile(symbols, "w") as archive:
        archive.writestr("halo_guest.debug.elf", elf(FUNCTIONS))
    build = tmp_path / "OpenH1-Linux.zip"
    with zipfile.ZipFile(build, "w") as archive:
        archive.writestr("OpenH1/bin/openh1", elf([("main", 0)], base=0x1000, machine=62))
    report = tmp_path / "crash.txt"
    report.write_text(CRASH)
    for given in ([symbols], [symbols, build]):
        result = subprocess.run([sys.executable, str(ROOT / "tools/symbolize_crash.py"), str(report),
                                 *map(str, given), "--symbolizer", fake_symbolizer(tmp_path)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "crash_here+0x10  source/test/crash_here.c:784" in result.stdout
    # (the build's zip unpacked beside the symbols: its single top folder left out)
    unpacked = symbolize_crash.unpack_zips([symbols, build, tmp_path / "other.apk"], tmp_path / "unpacked")
    assert unpacked == [tmp_path / "unpacked/build", tmp_path / "other.apk"]
    assert (unpacked[0] / "halo_guest.debug.elf").is_file() and (unpacked[0] / "bin/openh1").is_file()
    guest, image, host = symbolize_crash.find_build(unpacked[:1], None, None)
    assert guest.name == "halo_guest.debug.elf" and host == unpacked[0] / "bin/openh1"


@pytest.mark.parametrize("text, hint", [
    ("- Build: build-1781 (release, commit 65d2d298e)\n- Platform: macOS\n", "OpenH1-macOS-symbols.zip of the release build-1781"),
    ("build: build-1781 (debug, commit 65d2d298e)\nplatform: Linux arm64\n", "OpenH1-Linux-ARM64-debug-symbols.zip"),
    ("- Build: build-12 (release)\n- Platform: Linux arm64, VR\n", "OpenH1-Steam-Frame-symbols.zip"),
    ("- Build: build-12 (debug)\n- Platform: Android\n", "OpenH1-Android-debug-symbols.zip"),
    ("- Build: build-12 (release)\n- Platform: Windows\n", "OpenH1-Windows-symbols.zip"),
    ("- Build: build-12 (release)\n", "OpenH1-<machine>-symbols.zip"),
    ("- Build: a local build, not a release (debug)\n- Platform: macOS\n", None),
])
def test_the_release_s_symbols_are_named(text, hint):
    """a report of a release's build names the zip its image is in (report.c's Build and Platform lines)"""
    found = symbolize_crash.symbols_hint(text)
    if hint is None:
        assert found is None
    else:
        assert hint in found and "https://github.com/OpenH1/goath1/releases/tag/build-" in found


def test_every_report_platform_has_its_symbols():
    """report.c's report_platform names are each a platform of tools/release_names.py's"""
    import release_names

    report = (ROOT / "port/linux/src/report.c").read_text()
    body = report[report.index("static const char *report_platform(void)"):]
    body = body[:body.index("\n}\n")]
    import re
    assert set(re.findall(r'return "([^"]+)";', body)) == set(symbolize_crash.PLATFORMS)
    for platform in symbolize_crash.PLATFORMS.values():
        assert release_names.symbols_name(platform, "release").endswith("-symbols")


def test_the_wrong_build_names_the_right_zip(tmp_path):
    """a release's report with another build's image: the error names the zip to fetch"""
    image = elf(FUNCTIONS)
    with pytest.raises(SymbolizeError, match="OpenH1-macOS-symbols.zip of the release build-7"):
        symbolize_crash.symbolize_report(CRASH, "a0000200", tmp_path / "halo_guest.elf", image, None, None,
                                         symbolize_crash.symbols_hint("- Build: build-7 (release)\n- Platform: macOS\n"))
    # (an image without its lines: the frames have their functions, and the zip with the lines is named)
    (tmp_path / "halo_guest.elf").write_bytes(image)
    out = symbolize_crash.symbolize_report(CRASH, "a0000100", tmp_path / "halo_guest.elf", image, None,
                                           fake_symbolizer(tmp_path), "OpenH1-macOS-symbols.zip of build-7")
    assert any("no source lines" in line and "OpenH1-macOS-symbols.zip" in line for line in out)


def llvm_tool(name):
    """the pinned LLVM's tool (tools/guest_toolchain.py), if it is here"""
    import guest_toolchain
    archive = guest_toolchain.llvm_archive()
    if archive:
        pinned = guest_toolchain.default_cache() / archive[0].split(".tar")[0] / "bin" / name
        if pinned.is_file():
            return str(pinned)
    return None


def test_a_converted_image_gives_its_lines(tmp_path):
    """Darwin assembly with line tables, converted, assembled and linked as
    the arm64 guests are: llvm-symbolizer names the crash's line"""
    cc, ld, symbolizer = llvm_tool("clang"), llvm_tool("ld.lld"), llvm_tool("llvm-symbolizer")
    if not (cc and ld and symbolizer):
        pytest.skip("needs the guests' pinned LLVM (python tools/guest_toolchain.py)")
    from android_asm_convert import convert_file
    source = tmp_path / "crash.c"
    source.write_text("int g;\n"
                      "int write_to_error_file(void) { return g; }\n"
                      "int crash_here(int *p)\n"
                      "{\n"
                      "\t*(volatile int *)0 = *p;\n"
                      "\treturn 0;\n"
                      "}\n")
    subprocess.run([cc, "--target=arm64_32-apple-watchos", "-O2", "-gline-tables-only", "-fdebug-compilation-dir=.",
                    "-S", "crash.c", "-o", "crash.darwin.s"], cwd=tmp_path, check=True)
    convert_file(tmp_path / "crash.darwin.s", tmp_path / "crash.s", base_register=True)
    subprocess.run([cc, "--target=aarch64-linux-gnu", "-c", "crash.s", "-o", "crash.o"], cwd=tmp_path, check=True)
    subprocess.run([ld, "-m", "aarch64elf", "-static", "-nostdlib", "-e", "0", "--section-start=.text=0xa0000000",
                    "crash.o", "-o", "halo_guest.elf"], cwd=tmp_path, check=True)
    image = (tmp_path / "halo_guest.elf").read_bytes()
    symbols = symbolize_crash.elf_symbols(image)
    # (the store's instruction: the first in crash_here after the load)
    disassembly = subprocess.run([llvm_tool("llvm-objdump") or "llvm-objdump", "-d", "halo_guest.elf"],
                                 cwd=tmp_path, capture_output=True, text=True, check=True).stdout
    store = next(int(line.split(":")[0], 16) for line in disassembly.splitlines()
                 if line.strip() and line.split(":")[0].strip().isalnum() and "\tstr" in line
                 and int(line.split(":")[0], 16) >= symbols["crash_here"])
    crash = (f"signal 11 at address 0x0: pc {BASE + store:016x} lr {BASE + store:016x} sp 0 fp 0\n"
             f"  guest memory at {BASE:016x}\n")
    out = "\n".join(symbolize_crash.symbolize_report(crash, f"{symbols['write_to_error_file']:x}",
                                                     tmp_path / "halo_guest.elf", image, None, symbolizer))
    assert f"pc: guest {store:08x} crash_here+{store - symbols['crash_here']:#x}  crash.c:5:" in out, out
