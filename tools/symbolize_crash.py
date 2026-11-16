#!/usr/bin/env python3
"""Puts function names and source lines on a crash's frames:

    python tools/symbolize_crash.py <report> <build>

The report is a crash report's zip (Report a Problem's, with crash.txt and
report.md), its crash.txt, or a log with the crash's lines in it (halo.log,
debug.txt, the console's copy). The build is the one that crashed: its
folder, or its files, as --guest and --host name them.

The macOS, iOS, Linux and Android builds' crash lines
(port/android/host/host_memory_watch.c) give each address as the host saw
it. The guest's are its 32-bit addresses; with the base register (the macOS
and iOS builds, configure.py --guest-base) the host's addresses of the
guest are the base ("guest memory at") plus them, without it (the x32 guest
of 64-bit x86 Linux, Android's) they are the guest's own. These are looked
up in the guest image, halo_guest.debug.elf (the image with its source
lines, beside build/<platform>/halo; the builds carry halo_guest.elf, the
same image stripped of them, which names the functions only), and the
host's (the "host image at" line places it) in the host's executable (the
macOS executable through atos, which reads its debugging information where
the build left it; the others' through llvm-symbolizer). Without an image
file the guest image the executable carries is used (the macOS app's
Contents/MacOS/halo, the Linux release's bin/openh1, the APK's
assets/halo_guest.elf). The game's reference address (debug.txt's, which
report.md repeats: write_to_error_file's address) must be the image's, or
the image is another build's and the tool stops.

A release's build has its halo_guest.debug.elf in the release, beside it:
OpenH1-<machine>[-debug]-symbols.zip (OpenH1-macOS-symbols.zip,
OpenH1-Linux-symbols.zip, OpenH1-Linux-ARM64-debug-symbols.zip,
OpenH1-Android-symbols.zip...: tools/release_names.py), in the release the
report's Build line names (build-<n>, its flavour release or debug; the
releases keep the last five builds). Give the zip as it is downloaded, with
the player's download if the host's frames are wanted too:

    python tools/symbolize_crash.py report.zip OpenH1-Linux-symbols.zip OpenH1-Linux.zip

When the report names a release's build and the image is not that build's,
or has no source lines, the tool says which zip to fetch.

The Windows build's crash lines (port/windows/host/host_memory_watch_win.c)
name each frame's module and the offset in it: its build is the folder of
halo.exe with its halo.pdb and halo_guest.debug.elf, or a release's
OpenH1-Windows[-debug]-symbols.zip (halo.pdb and halo_guest.debug.elf)
with the zip of the same name without -symbols (halo.exe). The crash reports that
reach Sentry need none of this for the host's frames: Sentry symbolizes
them itself.

llvm-symbolizer is the guests' pinned LLVM's (python tools/guest_toolchain.py
fetches it and prints its bin/), or the PATH's, or --symbolizer's.
"""

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ---------- the Windows build's lines

# the report's lines with an address in them: the exception (its own
# address) and each frame (a return address, one past its call, but the
# first frame's, the exception's address again)
EXCEPTION = re.compile(r"exception \w+ at [0-9A-Fa-f]+ \((halo\.exe|halo_guest\.elf)\+0x([0-9A-Fa-f]+)")
FRAME = re.compile(r"frame +(\d+): [0-9A-Fa-f]+ (halo\.exe|halo_guest\.elf)\+0x([0-9A-Fa-f]+)")

# ---------- the other builds' lines (host_memory_watch.c's report_crash)

SIGNAL = re.compile(r"signal (\d+) at address (\S+): pc ([0-9A-Fa-f]+) lr ([0-9A-Fa-f]+)")
GUEST_BASE = re.compile(r"guest memory at ([0-9A-Fa-f]+)")
HOST_IMAGE = re.compile(r"host image at ([0-9A-Fa-f]+): (.*)$")
RETURN = re.compile(r"frame +(\d+): return ([0-9A-Fa-f]+)")
# debug.txt's line, and report.md's
REFERENCE = re.compile(r"reference address: ([0-9A-Fa-f]+)", re.IGNORECASE)

GUEST_SPAN = 1 << 32
# the most the host's code can span, without its executable to tell
HOST_SPAN = 256 << 20
# the function whose address the reference address is (errors.c, report.c)
REFERENCE_FUNCTION = "write_to_error_file"
# the build a report names: report.md's "- Build: build-1781 (release, commit
# ...)" and "- Platform: macOS", debug.txt's "build: ..." and "platform: ..."
# (port/linux/src/report.c, updater_build_text)
BUILD = re.compile(r"^(?:- )?build: build-(\d+) \((release|debug)", re.IGNORECASE | re.MULTILINE)
PLATFORM = re.compile(r"^(?:- )?platform: (.+?)\s*$", re.IGNORECASE | re.MULTILINE)
# report.c's report_platform -> tools/release_names.py's platform
PLATFORMS = {
    "macOS": "macos",
    "iOS": "ios",
    "Android": "android",
    "Windows": "windows",
    "Linux x86-64": "linux-x86_64",
    "Linux x86-64, VR": "linux-x86_64-vr",
    "Linux arm64": "linux-arm64",
    "Linux arm64, VR": "steam-frame-vr",
}
RELEASES = "https://github.com/OpenH1/goath1/releases/tag"


class SymbolizeError(Exception):
    pass


# ---------- reading the report

def report_text(path: Path) -> Tuple[str, Optional[str]]:
    """the crash's lines (a zip's crash.txt, or the file), and the reference
    address the report gives, if any (a zip's report.md's, else the last
    one before the crash in the text)"""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            names = {Path(name).name: name for name in archive.namelist()}
            if "crash.txt" not in names:
                raise SymbolizeError(f"{path} has no crash.txt: it is not a crash's report")
            text = archive.read(names["crash.txt"]).decode("utf-8", "replace")
            reference = None
            for name in ("report.md", "debug.txt"):
                if name in names and reference is None:
                    reference = last_reference(archive.read(names[name]).decode("utf-8", "replace"))
            return text, reference or last_reference(text)
    text = path.read_text(encoding="utf-8", errors="replace")
    return text, last_reference(text)


def report_context(path: Path) -> str:
    """all of a report's text (a zip's report.md, debug.txt and crash.txt,
    or the file), where its build is named"""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            return "\n".join(archive.read(name).decode("utf-8", "replace") for name in archive.namelist()
                             if Path(name).name in ("report.md", "debug.txt", "crash.txt"))
    return path.read_text(encoding="utf-8", errors="replace")


def symbols_hint(text: str) -> Optional[str]:
    """where the image of the release's build the report names is: its
    release's symbols zip, or None for a local build (or a report that does
    not say)"""
    build, platform = BUILD.search(text), PLATFORM.search(text)
    if build is None:
        return None
    number, flavor = build.group(1), build.group(2).lower()
    if platform is None or platform.group(1) not in PLATFORMS:
        return (f"the release build-{number}'s OpenH1-<machine>{'-debug' if flavor == 'debug' else ''}-symbols.zip "
                f"({RELEASES}/build-{number})")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import release_names

    name = release_names.symbols_name(PLATFORMS[platform.group(1)], flavor)
    return (f"{name}.zip of the release build-{number} ({RELEASES}/build-{number}; the releases keep the last "
            f"five builds)")


def last_reference(text: str, before: Optional[int] = None) -> Optional[str]:
    """the last reference address in text (before the offset before)"""
    found = None
    for match in REFERENCE.finditer(text):
        if before is not None and match.start() > before:
            break
        found = match.group(1).lower()
    return found


def parse_crash(text: str) -> Dict:
    """the last crash in text: {"lines": [(index, line)], "base", "host",
    "host_name", "addresses": {index: [(label, address, is_return)]},
    "offset": where in text it begins}"""
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if SIGNAL.search(line):
            start = index
    if start is None:
        raise SymbolizeError("no crash's lines (\"signal N at address ...\") in the report")
    crash = {"base": None, "host": None, "host_name": None, "addresses": {}, "start": start,
             "offset": sum(len(line) + 1 for line in lines[:start])}
    end = len(lines)
    for index in range(start, len(lines)):
        line = lines[index]
        if index > start and (SIGNAL.search(line) or line.startswith("--- ")):
            end = index
            break
        match = SIGNAL.search(line)
        if match:
            crash["addresses"][index] = [("pc", int(match.group(3), 16), False),
                                         ("lr", int(match.group(4), 16), True)]
            continue
        match = GUEST_BASE.search(line)
        if match:
            crash["base"] = int(match.group(1), 16)
            continue
        match = HOST_IMAGE.search(line)
        if match:
            crash["host"] = int(match.group(1), 16)
            crash["host_name"] = match.group(2).strip()
            continue
        match = RETURN.search(line)
        if match:
            crash["addresses"][index] = [("", int(match.group(2), 16), True)]
    crash["lines"] = list(enumerate(lines))[start:end]
    return crash


def place(address: int, base: Optional[int], host: Optional[int], host_size: int = HOST_SPAN) -> Tuple[str, int]:
    """where an address of the report is: ("guest", its guest address),
    ("host", its offset from the host's image, of host_size bytes) or ("",
    address): the system's libraries, say"""
    if base:
        if base <= address < base + GUEST_SPAN:
            return "guest", address - base
    elif address < GUEST_SPAN:
        return "guest", address
    if host is not None and host <= address < host + host_size:
        return "host", address - host
    return "", address


# ---------- the files of a build

def elf_header(data: bytes, start: int = 0) -> Optional[Dict]:
    """an ELF file's class, machine, loadable segments (vaddr, memsz, flags)
    and section headers' place, or None if data has no ELF file at start"""
    if data[start:start + 4] != b"\x7fELF" or data[start + 4] not in (1, 2) or data[start + 5] != 1:
        return None
    is64 = data[start + 4] == 2
    machine, = struct.unpack_from("<H", data, start + 18)
    if is64:
        phoff, shoff = struct.unpack_from("<QQ", data, start + 32)
        phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, start + 54)
    else:
        phoff, shoff = struct.unpack_from("<II", data, start + 28)
        phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, start + 42)
    segments = []
    for index in range(phnum):
        at = start + phoff + index * phentsize
        if is64:
            kind, flags, offset, vaddr, _, filesz, memsz = struct.unpack_from("<IIQQQQQ", data, at)
        else:
            kind, offset, vaddr, _, filesz, memsz, flags = struct.unpack_from("<IIIIIII", data, at)
        if kind == 1:
            segments.append((vaddr, memsz, flags, offset + filesz))
    return {"is64": is64, "machine": machine, "segments": segments, "shoff": shoff, "shentsize": shentsize,
            "shnum": shnum, "size": max([shoff + shentsize * shnum] + [end for *_, end in segments])}


def elf_symbol_table(data: bytes) -> List[Tuple[int, int, str]]:
    """an ELF file's functions and objects in its symbol table: (value,
    type: 1 an object, 2 a function, name). The arm64 guests' symbols,
    converted from Darwin's assembly (tools/android_asm_convert.py), have no
    type: a symbol in the code's section is a function, one elsewhere an
    object (the assembler's mapping symbols, $x and $d, are neither)."""
    header = elf_header(data)
    if header is None:
        raise SymbolizeError("not an ELF file")
    is64 = header["is64"]
    sections = []
    for index in range(header["shnum"]):
        at = header["shoff"] + index * header["shentsize"]
        if is64:
            _, kind, flags, _, offset, size, link, _, _, entsize = struct.unpack_from("<IIQQQQIIQQ", data, at)
        else:
            _, kind, flags, _, offset, size, link, _, _, entsize = struct.unpack_from("<IIIIIIIIII", data, at)
        sections.append((kind, flags, offset, size, link, entsize))
    found = []
    for kind, _, offset, size, link, entsize in sections:
        if kind != 2 or not entsize:  # SHT_SYMTAB
            continue
        strings = sections[link][2]
        for at in range(offset, offset + size, entsize):
            if is64:
                name, info, _, section, value, _ = struct.unpack_from("<IBBHQQ", data, at)
            else:
                name, value, _, info, _, section = struct.unpack_from("<IIIBBH", data, at)
            symbol_type = info & 0xf
            if not name or not 0 < section < min(len(sections), 0xff00) or symbol_type not in (0, 1, 2):
                continue
            end = data.index(b"\0", strings + name)
            text = data[strings + name:end].decode("utf-8", "replace")
            if text.startswith("$"):
                continue
            if symbol_type == 0:
                symbol_type = 2 if sections[section][1] & 4 else 1  # SHF_EXECINSTR
            found.append((value, symbol_type, text))
    return found


def elf_symbols(data: bytes) -> Dict[str, int]:
    """{name: value} of an ELF file's functions and objects"""
    return {name: value for value, _, name in elf_symbol_table(data)}


def functions(data: bytes) -> List[Tuple[int, str]]:
    """an ELF file's functions, by address"""
    return sorted((value, name) for value, kind, name in elf_symbol_table(data) if kind == 2)


def embedded_guest(data: bytes) -> Optional[bytes]:
    """the guest image an executable carries (guest_image.S's .incbin): the
    ELF file in it, not at its start, that has the reference function"""
    at = data.find(b"\x7fELF", 1)
    while at >= 0:
        header = elf_header(data, at)
        if header and header["segments"] and at + header["size"] <= len(data):
            image = data[at:at + header["size"]]
            try:
                if REFERENCE_FUNCTION in elf_symbols(image):
                    return image
            except (struct.error, ValueError, IndexError):
                pass
        at = data.find(b"\x7fELF", at + 1)
    return None


def find_build(paths: List[Path], guest: Optional[Path], host: Optional[Path]) -> Tuple[Optional[Path], Optional[bytes],
                                                                                     Optional[Path]]:
    """the guest image (its file, and its bytes: perhaps the executable's
    or the APK's copy) and the host's executable, from the build's folder
    or files"""
    candidates = []
    for path in paths:
        if path.is_dir():
            candidates += [path / "halo_guest.debug.elf", path / "halo_guest.elf", path / "halo",
                           path / "Contents/MacOS/halo", path / "bin/openh1", path / "lib/openh1/halo",
                           path / "libmain.so",
                           *sorted(path.glob("*.apk"))]
        else:
            candidates.append(path)
    candidates = [path for path in candidates if path.is_file()]
    if guest is None:
        guest = next((path for path in candidates if path.name.startswith("halo_guest")), None)
    if host is None:
        host = next((path for path in candidates if path.name in ("halo", "openh1", "libmain.so")), None)
    image = guest.read_bytes() if guest else None
    if image is None:
        for path in candidates + ([host] if host and host not in candidates else []):
            if path.suffix == ".apk" and zipfile.is_zipfile(path):
                with zipfile.ZipFile(path) as apk:
                    if "assets/halo_guest.elf" in apk.namelist():
                        image, guest = apk.read("assets/halo_guest.elf"), path
                        break
            elif path == host:
                image = embedded_guest(path.read_bytes())
                if image is not None:
                    guest = path
                    break
    return guest, image, host


def unpack_zips(paths: List[Path], folder: Path) -> List[Path]:
    """the build's files with each zip given (a release's symbols zip, its
    Linux or Windows zip; not an APK, which is read as it is) unpacked into
    one folder (a zip's single top folder, if it has one, left out): that
    folder, and the files that are not zips"""
    rest, unpacked = [], folder / "build"
    for index, path in enumerate(paths):
        if path.is_dir() or path.suffix.lower() == ".apk" or not zipfile.is_zipfile(path):
            rest.append(path)
            continue
        target = folder / str(index)
        with zipfile.ZipFile(path) as archive:
            archive.extractall(target)  # (which keeps every name inside target)
        entries = list(target.iterdir())
        root = entries[0] if len(entries) == 1 and entries[0].is_dir() else target
        shutil.copytree(root, unpacked, dirs_exist_ok=True)
    return ([unpacked] if unpacked.exists() else []) + rest


def has_lines(image: bytes) -> bool:
    """the image has its source lines (halo_guest.debug.elf, not the
    builds' halo_guest.elf)"""
    return b".debug_line" in image


def check_reference(image: bytes, reference: Optional[str], where: Path, hint: Optional[str] = None) -> None:
    """the image is the build's that crashed: its reference function is at
    the reference address the report gives"""
    symbols = elf_symbols(image)
    if REFERENCE_FUNCTION not in symbols:
        raise SymbolizeError(f"{where} has no {REFERENCE_FUNCTION}: it is not the game's guest image")
    ours = symbols[REFERENCE_FUNCTION] & 0xffffffff
    if reference is None:
        print("warning: the report gives no reference address: the image is not checked against the build",
              file=sys.stderr)
        return
    if int(reference, 16) != ours:
        raise SymbolizeError(
            f"{where} is not the build that crashed: the report's reference address is {reference}, the image's "
            f"{REFERENCE_FUNCTION} is at {ours:x}. Give the guest image of the build the report names "
            f"(report.md's Build line)" + (f": {hint}." if hint else "."))


# ---------- the symbolizers

def find_symbolizer(explicit: Optional[str]) -> str:
    if explicit:
        return explicit
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import guest_toolchain
        archive = guest_toolchain.llvm_archive()
        if archive:
            name = archive[0].split(".tar")[0]
            pinned = guest_toolchain.default_cache() / name / "bin" / "llvm-symbolizer"
            if pinned.is_file():
                return str(pinned)
    except Exception:
        pass
    found = shutil.which("llvm-symbolizer") or shutil.which("llvm-addr2line")
    if not found:
        raise SymbolizeError("no llvm-symbolizer: run python tools/guest_toolchain.py (the guests' pinned LLVM), "
                             "or give --symbolizer")
    return found


def symbolize(symbolizer: str, image: Path, addresses: list, relative: bool = False) -> dict:
    """{address: [function, location, ...]}, llvm-symbolizer's answer
    (inlined calls first)"""
    if not addresses:
        return {}
    output = subprocess.run(
        [symbolizer, f"--obj={image}", "--inlines", "--functions=linkage"] + (["--relative-address"] if relative else [])
        + [hex(address) for address in addresses],
        capture_output=True, text=True, check=True).stdout
    answers = output.strip("\n").split("\n\n")
    return {address: answer.splitlines() for address, answer in zip(addresses, answers)}


def macho_text_address(data: bytes) -> Optional[int]:
    """a 64-bit Mach-O executable's __TEXT segment's address"""
    if data[:4] != b"\xcf\xfa\xed\xfe":
        return None
    count, = struct.unpack_from("<I", data, 16)
    at = 32
    for _ in range(count):
        command, size = struct.unpack_from("<II", data, at)
        if command == 0x19 and data[at + 8:at + 24].rstrip(b"\0") == b"__TEXT":
            return struct.unpack_from("<Q", data, at + 24)[0]
        at += size
    return None


def host_extent(executable: Path) -> int:
    """the bytes an executable's segments span from its start"""
    data = executable.read_bytes()
    if data[:4] == b"\xcf\xfa\xed\xfe":
        count, = struct.unpack_from("<I", data, 16)
        at, low, high = 32, None, 0
        for _ in range(count):
            command, size = struct.unpack_from("<II", data, at)
            if command == 0x19 and data[at + 8:at + 24].rstrip(b"\0") != b"__PAGEZERO":
                vmaddr, vmsize = struct.unpack_from("<QQ", data, at + 24)
                low = vmaddr if low is None else min(low, vmaddr)
                high = max(high, vmaddr + vmsize)
            at += size
        return high - low if low is not None else HOST_SPAN
    header = elf_header(data)
    if header and header["segments"]:
        low = min(vaddr for vaddr, *_ in header["segments"]) & ~0xfff
        return max(vaddr + memsz for vaddr, memsz, *_ in header["segments"]) - low
    return HOST_SPAN


def symbolize_host(symbolizer: str, executable: Path, host: int, offsets: List[int]) -> Dict[int, List[str]]:
    """{offset: [function, location]} of the host's executable, its
    addresses the report's less the image's (host)"""
    data = executable.read_bytes()
    text = macho_text_address(data)
    if text is not None:
        atos = shutil.which("atos")
        if atos:
            output = subprocess.run([atos, "-o", str(executable), "-l", hex(host)]
                                    + [hex(host + offset) for offset in offsets],
                                    capture_output=True, text=True).stdout.splitlines()
            return {offset: [line] for offset, line in zip(offsets, output)}
        answers = symbolize(symbolizer, executable, [text + offset for offset in offsets])
        return {offset: answers.get(text + offset, []) for offset in offsets}
    header = elf_header(data)
    if header is None:
        return {}
    first = min(vaddr for vaddr, *_ in header["segments"]) & ~0xfff
    answers = symbolize(symbolizer, executable, [first + offset for offset in offsets])
    return {offset: answers.get(first + offset, []) for offset in offsets}


# ---------- the report, symbolized

def nearest_function(table: List[Tuple[int, str]], address: int) -> Optional[str]:
    """the function an address is in, as name+0xoffset"""
    low, high = 0, len(table)
    while low < high:
        middle = (low + high) // 2
        if table[middle][0] <= address:
            low = middle + 1
        else:
            high = middle
    if not low:
        return None
    value, name = table[low - 1]
    return f"{name}+{address - value:#x}"


def answer_lines(answer: List[str]) -> List[str]:
    """llvm-symbolizer's pairs of lines as "function  file:line" lines"""
    if not answer or answer[0] == "??":
        return []
    if len(answer) == 1:
        return [answer[0]]
    # (the guest's paths are the checkout's, "./" before them: -fdebug-compilation-dir)
    return [f"{function}  {re.sub(r'^(?:[.]/)+', '', location)}" for function, location in zip(answer[0::2], answer[1::2])]


def symbolize_report(text: str, reference: Optional[str], guest_path: Optional[Path], image: Optional[bytes],
                     host_path: Optional[Path], symbolizer: Optional[str], hint: Optional[str] = None) -> List[str]:
    """the crash's lines, each address's function and line after it (hint:
    where the build's image with its lines is, symbols_hint's)"""
    crash = parse_crash(text)
    if reference is None:
        reference = last_reference(text, crash["offset"])
    image_file = guest_path
    if image is not None:
        check_reference(image, reference, guest_path, hint)
        if guest_path is None or not guest_path.name.startswith("halo_guest"):
            # (the executable's or the APK's copy, written out for llvm-symbolizer)
            handle = tempfile.NamedTemporaryFile(prefix="halo_guest.", suffix=".elf", delete=False)
            handle.write(image)
            handle.close()
            image_file = Path(handle.name)
    table = functions(image) if image is not None else []
    code = [(vaddr, vaddr + memsz) for vaddr, memsz, flags, _ in (elf_header(image)["segments"] if image else [])
            if flags & 1]

    wanted_guest, wanted_host = set(), set()
    placed = {}
    host_size = host_extent(host_path) if host_path else HOST_SPAN
    for index, entries in crash["addresses"].items():
        for label, address, is_return in entries:
            kind, value = place(address, crash["base"], crash["host"], host_size)
            lookup = value - 1 if is_return and value else value
            placed[(index, label)] = (kind, value, lookup)
            if kind == "guest":
                wanted_guest.add(lookup)
            elif kind == "host":
                wanted_host.add(lookup)
    tool = find_symbolizer(symbolizer) if (wanted_guest and image is not None) or (wanted_host and host_path) else None
    guest_answers = symbolize(tool, image_file, sorted(wanted_guest)) if wanted_guest and image is not None else {}
    host_answers = symbolize_host(tool, host_path, crash["host"], sorted(wanted_host)) \
        if wanted_host and host_path else {}
    if image_file is not None and image_file != guest_path:
        image_file.unlink()

    out = []
    if crash["base"]:
        out.append(f"(the guest's memory at {crash['base']:016x}: its addresses are the host's less it)")
    if image is None:
        out.append("(no guest image: the guest's frames are given as guest addresses only"
                   + (f"; its image is in {hint})" if hint else ")"))
    elif not has_lines(image):
        out.append("(the guest image has no source lines: the guest's frames are given their functions only; "
                   + (f"the image with them is in {hint})" if hint else "give halo_guest.debug.elf)"))
    for index, line in crash["lines"]:
        out.append(line)
        for label, address, is_return in crash["addresses"].get(index, []):
            kind, value, lookup = placed[(index, label)]
            prefix = f"        {label + ': ' if label else ''}"
            if kind == "guest":
                inside = any(low <= lookup < high for low, high in code)
                if image is None:
                    out.append(f"{prefix}guest {value:08x}")
                    continue
                if not inside:
                    out.append(f"{prefix}guest {value:08x} (not the image's code)")
                    continue
                lines = answer_lines(guest_answers.get(lookup, []))
                # (a return address's function is its call's, its offset its own)
                where = nearest_function(table, lookup)
                if where and lookup != value:
                    name, _, offset = where.rpartition("+")
                    where = f"{name}+{int(offset, 16) + value - lookup:#x}"
                if not lines:
                    out.append(f"{prefix}guest {value:08x} {where or '(not found)'}")
                    continue
                first, *rest = lines
                location = first.split("  ", 1)[1] if "  " in first else ""
                if location.startswith("??"):
                    # (an image without its lines: the builds' halo_guest.elf)
                    location = ""
                out.append(f"{prefix}guest {value:08x} {where or first.split('  ')[0]}  {location}".rstrip())
                out += [f"{' ' * len(prefix)}  inlined in {line}" for line in rest]
            elif kind == "host":
                answer = host_answers.get(lookup, [])
                lines = answer_lines(answer) if len(answer) != 1 else answer
                if not lines:
                    out.append(f"{prefix}{crash['host_name'] or 'host'}+{value:#x}")
                    continue
                out.append(f"{prefix}{crash['host_name'] or 'host'}+{value:#x} {lines[0]}")
                out += [f"{' ' * len(prefix)}  inlined in {line}" for line in lines[1:]]
            else:
                out.append(f"{prefix}{address:016x} (neither the guest's nor the host's image)")
    return out


def windows_main(log: Path, folder: Path, symbolizer: Optional[str]) -> int:
    """the Windows build's lines: each frame's module and its offset"""
    symbolizer = find_symbolizer(symbolizer)
    executable, image = folder / "halo.exe", folder / "halo_guest.debug.elf"
    if not image.is_file():
        # (a release's before the image with its lines was published)
        image = folder / "halo_guest.elf"
    if not (folder / "halo.pdb").is_file():
        print(f"warning: no halo.pdb in {folder}", file=sys.stderr)
    base = None
    if image.is_file():
        header = elf_header(image.read_bytes())
        if header is None:
            sys.exit(f"{image} is not the guest image (an ELF file)")
        base = min(vaddr for vaddr, *_ in header["segments"]) & ~0xfff

    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    # each line's module and the offset to look up in it
    wanted = {}
    for index, line in enumerate(lines):
        match = EXCEPTION.search(line)
        if match:
            wanted[index] = (match.group(1), int(match.group(2), 16))
            continue
        match = FRAME.search(line)
        if match:
            offset = int(match.group(3), 16)
            wanted[index] = (match.group(2), offset if match.group(1) == "0" else offset - 1)
    host = symbolize(symbolizer, executable, sorted({o for m, o in wanted.values() if m == "halo.exe"}), True) \
        if executable.is_file() else {}
    guest = symbolize(symbolizer, image, sorted({base + o for m, o in wanted.values() if m == "halo_guest.elf"})) \
        if base is not None else {}

    for index, line in enumerate(lines):
        print(line)
        if index not in wanted:
            continue
        module, offset = wanted[index]
        answer = host.get(offset) if module == "halo.exe" else guest.get(base + offset if base is not None else -1)
        if not answer or answer[0] == "??":
            print(f"        (not found in {module})")
            continue
        for function, location in zip(answer[0::2], answer[1::2]):
            print(f"        {function}  {location}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report", type=Path, help="a crash report's zip, its crash.txt, or halo.log or debug.txt")
    parser.add_argument("build", type=Path, nargs="*",
                        help="the build that crashed: its folder (build/macos, the app, the Linux release's tree, "
                             "Windows's halo.exe with halo.pdb and halo_guest.debug.elf), or its files, or a "
                             "release's zips (OpenH1-<machine>[-debug]-symbols.zip, and the build's own)")
    parser.add_argument("--guest", type=Path, help="the guest image (halo_guest.debug.elf or halo_guest.elf)")
    parser.add_argument("--host", type=Path, help="the host's executable (halo, libmain.so)")
    parser.add_argument("--symbolizer", help="llvm-symbolizer (default: the guests' pinned LLVM's, or the PATH's)")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="symbolize_crash.") as folder:
        try:
            build = unpack_zips(args.build, Path(folder))
            if not zipfile.is_zipfile(args.report):
                raw = args.report.read_text(encoding="utf-8", errors="replace")
                if EXCEPTION.search(raw) or FRAME.search(raw):
                    if len(build) != 1 or not build[0].is_dir():
                        sys.exit("a Windows build's report needs the folder of its halo.exe, halo.pdb and "
                                 "halo_guest.debug.elf, or the release's OpenH1-Windows[-debug].zip and its -symbols.zip")
                    return windows_main(args.report, build[0], args.symbolizer)
            text, reference = report_text(args.report)
            guest_path, image, host_path = find_build(build, args.guest, args.host)
            for line in symbolize_report(text, reference, guest_path, image, host_path, args.symbolizer,
                                         symbols_hint(report_context(args.report))):
                print(line)
        except (SymbolizeError, zipfile.BadZipFile) as error:
            print(f"symbolize_crash.py: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
