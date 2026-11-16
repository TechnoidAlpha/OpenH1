"""Checks that the APK's native libraries load on kernels of 16 KB pages (Android 15 and later; Google Play requires
it): each loadable segment (PT_LOAD) of each 64-bit ELF file aligned to at least 16 KB, its offset in the file
and its address alike modulo that, as the linker's -z max-page-size=16384 makes them. A library linked for 4 KB
pages is refused on such a kernel. tools/android_build.py runs it on libmain.so and libSDL3.so; writes the stamp
file (its first argument) when all pass.

    python tools/android_elf_alignment.py STAMP LIBRARY...
"""
import struct
import sys
from pathlib import Path
from typing import List

PAGE = 0x4000
PT_LOAD = 1


def problems(path: Path, page: int = PAGE) -> List[str]:
    """what keeps the library at path from loading on pages of page bytes; none if nothing"""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        return [f"{path}: not a 64-bit ELF file"]
    endian = "<" if data[5] == 1 else ">"
    phoff, = struct.unpack_from(endian + "Q", data, 0x20)
    phentsize, phnum = struct.unpack_from(endian + "HH", data, 0x36)
    found = []
    loads = 0
    for index in range(phnum):
        p_type, _, p_offset, p_vaddr, _, _, _, p_align = struct.unpack_from(endian + "IIQQQQQQ", data,
                                                                            phoff + index * phentsize)
        if p_type != PT_LOAD:
            continue
        loads += 1
        if p_align < page:
            found.append(f"{path}: loadable segment {index} is aligned to {p_align:#x} bytes, less than {page:#x}")
        elif p_offset % page != p_vaddr % page:
            found.append(f"{path}: loadable segment {index}'s offset {p_offset:#x} and address {p_vaddr:#x} differ "
                         f"modulo {page:#x}")
    if not loads:
        found.append(f"{path}: no loadable segment")
    return found


def main(arguments: List[str]) -> int:
    if len(arguments) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    stamp, libraries = Path(arguments[0]), [Path(name) for name in arguments[1:]]
    found = [problem for library in libraries for problem in problems(library)]
    for problem in found:
        print(f"android_elf_alignment: {problem} (link it with -Wl,-z,max-page-size=16384)", file=sys.stderr)
    if found:
        return 1
    stamp.write_text("".join(f"{library}\n" for library in libraries))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
