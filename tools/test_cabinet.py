"""The importers of Custom Edition's installer and of Halo PC's CD
(port/linux/src/cabinet.c, ce_installer.c, pc_disc.c: ChupathingyCE's) and
OpenH1's glue to them (map_import.c), on made-up files: Microsoft cabinets
written here, stored and LZX-compressed (verbatim and uncompressed LZX
blocks, matches far back and long), inside a made-up installer (a program
starting MZ) and on a made-up CD image (ISO 9660). What a cabinet, CD or
installer cannot be trusted with is refused, and damaged ones never crash
the reader nor leave a map half-written. Built with the build machine's C
compiler beside stand-ins for platform.h and port_config.h, as
tools/test_xiso.py builds xiso.c; with the address and undefined behavior
sanitizers where the compiler has them.

No real installer or CD is used: an import of the real ones is untested
here. With cabextract on the PATH, the cabinets written here are checked to
be ones it reads the same way, so that the LZX written is LZX, not only what
this reader takes for it."""
import functools
import heapq
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_xiso  # noqa: E402  (its made-up Xbox disc images)

FRAME = 32768
WINDOW_BITS = 21
POSITION_SLOTS = 50
# what a position slot's offsets start at, and their extra bits
EXTRA_BITS = [0 if slot < 4 else min((slot - 2) // 2, 17) for slot in range(POSITION_SLOTS)]
POSITION_BASE = [0]
for _slot in range(1, POSITION_SLOTS):
    POSITION_BASE.append(POSITION_BASE[-1] + (1 << EXTRA_BITS[_slot - 1]))


# ---------- LZX

class Bits:
    """LZX's bit stream: 16-bit little-endian words, each from its highest
    bit down"""

    def __init__(self):
        self.out = bytearray()
        self.word = 0
        self.count = 0

    def write(self, value: int, count: int):
        while count:
            take = min(count, 16 - self.count)
            count -= take
            self.word = (self.word << take) | ((value >> count) & ((1 << take) - 1))
            self.count += take
            if self.count == 16:
                self.out += struct.pack("<H", self.word)
                self.word = self.count = 0

    def align(self, whole: bool = False):
        if self.count:
            self.write(0, 16 - self.count)
        elif whole:
            self.write(0, 16)


def huffman_lengths(frequencies: list, limit: int) -> list:
    """a complete prefix code's lengths for the symbols used (at least two
    are given a code)"""
    used = [symbol for symbol, frequency in enumerate(frequencies) if frequency]
    while len(used) < 2:
        used.append(next(s for s in range(len(frequencies)) if s not in used))
    heap = [(frequencies[s] or 1, index, [s]) for index, s in enumerate(used)]
    heapq.heapify(heap)
    lengths = [0] * len(frequencies)
    serial = len(heap)
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        for symbol in a[2] + b[2]:
            lengths[symbol] += 1
        heapq.heappush(heap, (a[0] + b[0], serial, a[2] + b[2]))
        serial += 1
    assert max(lengths) <= limit
    return lengths


def canonical_codes(lengths: list) -> dict:
    codes = {}
    code = 0
    for length in range(1, 17):
        for symbol, symbol_length in enumerate(lengths):
            if symbol_length == length:
                codes[symbol] = (code, length)
                code += 1
        code <<= 1
    return codes


def write_lengths(bits: Bits, previous: list, lengths: list):
    """a tree's lengths as changes from previous, coded by a pretree sent
    first; runs of four or more zeros as symbols 17 and 18"""
    symbols = []
    index = 0
    while index < len(lengths):
        run = 0
        while index + run < len(lengths) and lengths[index + run] == 0 and run < 51:
            run += 1
        if run >= 20:
            symbols.append((18, run - 20, 5))
            index += run
            continue
        if run >= 4:
            symbols.append((17, run - 4, 4))
            index += run
            continue
        symbols.append(((previous[index] - lengths[index]) % 17, 0, 0))
        index += 1
    frequencies = [0] * 20
    for symbol, _extra, _count in symbols:
        frequencies[symbol] += 1
    pretree = huffman_lengths(frequencies, 15)
    codes = canonical_codes(pretree)
    for length in pretree:
        bits.write(length, 4)
    for symbol, extra, count in symbols:
        code, length = codes[symbol]
        bits.write(code, length)
        bits.write(extra, count)


def tokens(data: bytes, start: int, end: int):
    """literals and matches (length, offset) of data[start:end], none
    reaching past a frame's end nor before the stream's start"""
    table = {}
    position = start
    while position < end:
        frame_end = min(end, (position // FRAME + 1) * FRAME)
        best_length = best_offset = 0
        key = data[position:position + 3]
        if len(key) == 3:
            for candidate in reversed(table.get(key, [])[-8:]):
                offset = position - candidate
                length = 0
                while (length < 257 and position + length < frame_end and
                       data[candidate + length] == data[position + length]):
                    length += 1
                if length > best_length:
                    best_length, best_offset = length, offset
            table.setdefault(key, []).append(position)
        if best_length >= 3:
            for at in range(position + 1, position + best_length):
                table.setdefault(data[at:at + 3], []).append(at)
            yield ("match", best_length, best_offset)
            position += best_length
        else:
            yield ("literal", data[position], 0)
            position += 1


def match_symbols(length: int, offset: int):
    formatted = offset + 2
    slot = max(s for s in range(3, POSITION_SLOTS) if POSITION_BASE[s] <= formatted)
    header = min(length - 2, 7)
    return 256 + slot * 8 + header, (length - 9 if header == 7 else None), slot, formatted - POSITION_BASE[slot]


def lzx_compress(data: bytes, uncompressed: tuple = None) -> list:
    """data in LZX (no x86 translation), cut where each frame's bits end: a
    verbatim block, then with uncompressed (start, end) an uncompressed
    block there and a second verbatim block after it (its trees sent as
    changes from the first's)"""
    bits = Bits()
    bits.write(0, 1)
    pieces = []
    blocks = [("verbatim", 0, len(data))]
    if uncompressed:
        u_start, u_end = uncompressed
        assert u_start // FRAME == (u_end - 1) // FRAME and (u_end - u_start) % 2 == 0
        blocks = [("verbatim", 0, u_start), ("uncompressed", u_start, u_end), ("verbatim", u_end, len(data))]
    main_previous = [0] * (256 + POSITION_SLOTS * 8)
    length_previous = [0] * 249
    made = 0

    def frame_end_check(kind):
        # (each frame's end: the stream goes to a word's boundary)
        if made % FRAME == 0:
            if kind != "uncompressed":
                bits.align()
            pieces.append(len(bits.out))

    for kind, start, end in blocks:
        if start == end:
            continue
        bits.write(3 if kind == "uncompressed" else 1, 3)
        bits.write(end - start, 24)
        if kind == "uncompressed":
            bits.align(whole=True)
            bits.out += struct.pack("<III", 1, 1, 1)
            bits.out += data[start:end]
            made = end
            frame_end_check(kind)
            continue
        found = list(tokens(data, start, end))
        main_frequencies = [0] * len(main_previous)
        length_frequencies = [0] * 249
        for token in found:
            if token[0] == "literal":
                main_frequencies[token[1]] += 1
            else:
                main, length, _slot, _footer = match_symbols(token[1], token[2])
                main_frequencies[main] += 1
                if length is not None:
                    length_frequencies[length] += 1
        main_lengths = huffman_lengths(main_frequencies, 16)
        length_lengths = huffman_lengths(length_frequencies, 16)
        write_lengths(bits, main_previous[:256], main_lengths[:256])
        write_lengths(bits, main_previous[256:], main_lengths[256:])
        write_lengths(bits, length_previous, length_lengths)
        main_previous, length_previous = main_lengths, length_lengths
        main_codes = canonical_codes(main_lengths)
        length_codes = canonical_codes(length_lengths)
        for token in found:
            if token[0] == "literal":
                bits.write(*main_codes[token[1]])
                made += 1
            else:
                main, length, slot, footer = match_symbols(token[1], token[2])
                bits.write(*main_codes[main])
                if length is not None:
                    bits.write(*length_codes[length])
                bits.write(footer, EXTRA_BITS[slot])
                made += token[1]
            frame_end_check(kind)
    if made % FRAME:
        bits.align()
        pieces.append(len(bits.out))
    stream = bytes(bits.out)
    cuts = [0] + pieces
    return [stream[a:b] for a, b in zip(cuts, cuts[1:])]


# ---------- cabinets

def cabinet(folders: list, flags: int = 0) -> bytes:
    """a cabinet of folders [(compression, {name: bytes}, uncompressed)]:
    compression 0 stored, 3 LZX (2 MB window), anything else as given;
    uncompressed is an LZX block's (start, end) in the folder's stream"""
    files = []
    folder_entries = []
    data_parts = []
    for index, (compression, members, uncompressed) in enumerate(folders):
        stream = b"".join(members.values())
        offset = 0
        for name, payload in members.items():
            files.append((len(payload), offset, index, name))
            offset += len(payload)
        if compression == 3:
            pieces = lzx_compress(stream, uncompressed)
            sizes = [min(FRAME, len(stream) - at) for at in range(0, len(stream), FRAME)]
            assert len(pieces) == len(sizes), (len(pieces), len(sizes))
            blocks = list(zip(pieces, sizes))
            type_compress = 3 | (WINDOW_BITS << 8)
        else:
            blocks = [(stream[at:at + FRAME], len(stream[at:at + FRAME])) for at in range(0, len(stream), FRAME)]
            type_compress = compression
        folder_entries.append((len(blocks), type_compress))
        data_parts.append(b"".join(struct.pack("<IHH", 0, len(piece), size) + piece for piece, size in blocks))
    header_size = 36
    folders_size = 8 * len(folder_entries)
    files_table = b"".join(struct.pack("<IIHHHH", size, start, folder, 0x5a21, 0, 0x20) + name.encode() + b"\0"
                           for size, start, folder, name in files)
    first_data = header_size + folders_size + len(files_table)
    folder_table = bytearray()
    at = first_data
    for (count, type_compress), part in zip(folder_entries, data_parts):
        folder_table += struct.pack("<IHH", at, count, type_compress)
        at += len(part)
    total = at
    header = b"MSCF" + struct.pack("<IIIIIBBHHHHH", 0, total, 0, header_size + folders_size, 0, 3, 1,
                                   len(folder_entries), len(files), flags, 0x1234, 0)
    return header + bytes(folder_table) + files_table + b"".join(data_parts)


def map_bytes(seed: int, size: int) -> bytes:
    """a made-up map: repeats near and far (matches of every length and
    offset), and noise"""
    generator = random.Random(seed)
    out = bytearray(b"daeh" + struct.pack("<I", 609))
    while len(out) < size:
        choice = generator.random()
        if choice < 0.4 and len(out) > 300:
            back = generator.randrange(1, min(len(out), 1 << 20))
            length = generator.randrange(3, 300)
            for _ in range(length):
                out.append(out[-back])
        elif choice < 0.7:
            out += bytes(generator.randrange(4)) * generator.randrange(1, 64)
        else:
            out += bytes(generator.getrandbits(8) for _ in range(generator.randrange(1, 40)))
    return bytes(out[:size])


CE_MAPS = {
    "maps\\bitmaps.map": map_bytes(1, 150000),
    "maps\\bloodgulch.map": map_bytes(2, 70000),
    "maps\\Gephyrophobia.map": map_bytes(3, 40001),
    "maps\\loc.map": map_bytes(4, 999),
}
CE_OTHER = {"setup\\readme.txt": b"not a map" * 50, "maps\\notes.txt": b"nor this"}
STORED_MAPS = {"Maps\\sounds.map": map_bytes(5, 50000)}


def written_maps(members: dict) -> dict:
    return {name.split("\\")[-1].lower(): payload for name, payload in members.items() if name.lower().endswith(".map")}


@functools.lru_cache(maxsize=None)
def ce_cabinet(flags: int = 0) -> bytes:
    lzx_members = {**CE_OTHER, **CE_MAPS}
    return cabinet([(3, lzx_members, (40000, 52000)), (0, STORED_MAPS, None)], flags)


def installer(cab: bytes, decoy: bool = True) -> bytes:
    """a made-up installer: a program (MZ), a header that only looks like a
    cabinet's, then the cabinet, then more of the program"""
    program = bytearray(b"MZ" + random.Random(9).randbytes(70000))
    if decoy:
        program[5000:5008] = b"MSCF\0\0\0\0"
    return bytes(program) + cab + b"\0" * 1234


# ---------- CD images

SECTOR = 2048


def iso_image(entries: dict) -> bytes:
    """an ISO 9660 image of folders {path: bytes}, the folders' names their
    paths' parts (FILESCAB/CAB1.CAB)"""
    sectors = {}
    data = bytearray(18 * SECTOR)

    def allocate(payload: bytes) -> int:
        sector = len(data) // SECTOR
        data.extend(payload + b"\0" * (-len(payload) % SECTOR or (0 if payload else SECTOR)))
        return sector

    def record(name: bytes, sector: int, size: int, directory: bool) -> bytes:
        length = 33 + len(name)
        length += length % 2
        out = bytearray(length)
        out[0] = length
        struct.pack_into("<I", out, 2, sector)
        struct.pack_into(">I", out, 6, sector)
        struct.pack_into("<I", out, 10, size)
        struct.pack_into(">I", out, 14, size)
        out[25] = 2 if directory else 0
        out[32] = len(name)
        out[33:33 + len(name)] = name
        return bytes(out)

    tree = {}
    for path, payload in entries.items():
        parts = path.split("/")
        node = tree
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = payload

    def write_folder(node: dict) -> tuple:
        records = []
        for name, value in node.items():
            if isinstance(value, dict):
                sector, size = write_folder(value)
                records.append(record(name.encode(), sector, size, True))
            else:
                records.append(record(name.encode() + b";1", allocate(value), len(value), False))
        table = record(b"\0", 0, 0, True) + record(b"\1", 0, 0, True) + b"".join(records)
        table += b"\0" * (-len(table) % SECTOR)
        return allocate(table), len(table)

    root_sector, root_size = write_folder(tree)
    primary = bytearray(SECTOR)
    primary[0] = 1
    primary[1:6] = b"CD001"
    primary[6] = 1
    primary[156:156 + 34] = record(b"\0", root_sector, root_size, True)[:34]
    end = bytearray(SECTOR)
    end[0] = 255
    end[1:6] = b"CD001"
    data[16 * SECTOR:17 * SECTOR] = primary
    data[17 * SECTOR:18 * SECTOR] = end
    return bytes(data)


PC_CAB_MAPS = {"maps\\a10.map": map_bytes(11, 60000), "maps\\ui.map": map_bytes(12, 3000)}
PC_PLAIN_MAP = map_bytes(13, 5000)


@functools.lru_cache(maxsize=None)
def pc_disc() -> bytes:
    return iso_image({"FILESCAB/CAB1.CAB": cabinet([(3, PC_CAB_MAPS, None)]),
                      "FILESCAB/README.TXT": b"hello",
                      "EXTRA/MAPS/DAMNATION.MAP": PC_PLAIN_MAP})


# ---------- the reader

@pytest.fixture(scope="module")
def reader(tmp_path_factory):
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("cabinet_build")
    for name in ("cabinet.c", "cabinet.h", "ce_installer.c", "ce_installer.h", "pc_disc.c", "pc_disc.h",
                 "map_import.c", "map_import.h", "xiso.c", "xiso.h", "posix.h"):
        shutil.copy(SOURCE / name, folder / name)
    (folder / "platform.h").write_text(
        "void platform_log(const char *format, ...);\n"
        "void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);\n")
    (folder / "port_config.h").write_text("const char *config_string(const char *name);\n")
    binary = folder / "cabinet_test"
    command = [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1", "-g",
               "-D_GNU_SOURCE", "-DHALO_GUEST", f"-I{folder}",
               *(str(folder / name) for name in ("cabinet.c", "ce_installer.c", "pc_disc.c", "map_import.c", "xiso.c")),
               str(SOURCE / "posix_files.c"), str(ROOT / "port" / "linux" / "tests" / "cabinet_test.c"),
               "-o", str(binary)]
    sanitized = subprocess.run(command[:1] + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] +
                               command[1:], capture_output=True, text=True)
    if sanitized.returncode != 0:
        subprocess.run(command, check=True)

    def run(*arguments):
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stderr
        return result.stdout.strip(), result.stderr
    return run


def folder_contents(folder: Path) -> dict:
    return {p.name: p.read_bytes() for p in folder.iterdir()} if folder.exists() else {}


def expected_ce() -> dict:
    return {**written_maps(CE_MAPS), **written_maps(STORED_MAPS)}


def test_lzx_round_trip_matches_cabextract(tmp_path):
    """the cabinets written here are ones cabextract reads the same way
    (when it is installed)"""
    if not shutil.which("cabextract"):
        pytest.skip("no cabextract")
    path = tmp_path / "ce.cab"
    path.write_bytes(ce_cabinet())
    out = tmp_path / "out"
    subprocess.run(["cabextract", "-q", "-d", str(out), str(path)], check=True)
    for name, payload in {**CE_MAPS, **CE_OTHER, **STORED_MAPS}.items():
        assert (out / name.replace("\\", "/")).read_bytes() == payload, name


def test_installer_maps_written(reader, tmp_path):
    path = tmp_path / "halocesetup_en_1.00.exe"
    path.write_bytes(installer(ce_cabinet()))
    folder = tmp_path / "custom_maps"
    out, log = reader("ce", path, folder)
    assert out == "1", log
    assert folder_contents(folder) == expected_ce()


def test_import_keeps_the_players_maps(reader, tmp_path):
    """only maps the folder lacks are written; a second import writes
    nothing"""
    path = tmp_path / "halocesetup_en_1.00.exe"
    path.write_bytes(installer(ce_cabinet()))
    folder = tmp_path / "custom_maps"
    folder.mkdir()
    (folder / "bloodgulch.map").write_bytes(b"the player's own")
    (folder / "hugeass.map").write_bytes(b"another map")
    out, log = reader("import", path, folder)
    lines = [line.strip() for line in out.splitlines()]
    assert lines[0] == "1", log
    expected = {**expected_ce(), "bloodgulch.map": b"the player's own", "hugeass.map": b"another map"}
    assert folder_contents(folder) == expected
    assert int(lines[1]) == sum(len(v) for k, v in expected_ce().items() if k != "bloodgulch.map")
    out, log = reader("import", path, folder)
    assert [line.strip() for line in out.splitlines()] == ["1", "0"], log
    assert folder_contents(folder) == expected


def test_halo_pc_disc_maps_written(reader, tmp_path):
    """pc_disc.c: every cabinet's maps, and a maps folder stored as it is,
    into maps_pc (not offered by OpenH1, which does not play them; kept for
    a loader of Halo PC's maps)"""
    image = tmp_path / "halopc.iso"
    image.write_bytes(pc_disc())
    out, log = reader("pc", image, tmp_path)
    assert out == "1", log
    assert folder_contents(tmp_path / "maps_pc") == {**written_maps(PC_CAB_MAPS), "damnation.map": PC_PLAIN_MAP}


def test_kinds(reader, tmp_path):
    """told apart by contents: an Xbox disc image of Halo (also when it has
    a CD's file system), the installer, a CD image, anything else"""
    xbox = test_xiso.halo_image(tmp_path / "halo.iso")
    ce = tmp_path / "setup.exe"
    ce.write_bytes(installer(ce_cabinet()))
    cd = tmp_path / "halopc.iso"
    cd.write_bytes(pc_disc())
    program = tmp_path / "other.exe"
    program.write_bytes(b"MZ" + bytes(100000))
    nothing = tmp_path / "empty.iso"
    nothing.write_bytes(b"")
    assert reader("kind", xbox)[0] == "1"
    assert reader("kind", ce)[0] == "2"
    assert reader("kind", cd)[0] == "3"
    assert reader("kind", program)[0] == "0"
    assert reader("kind", nothing)[0] == "0"
    assert reader("kind", tmp_path / "missing.exe")[0] == "0"


def test_custom_maps_folder(reader, monkeypatch):
    """beside the maps folder, or where paths.custom_maps says (the stand-in
    reads it from CUSTOM_MAPS)"""
    monkeypatch.delenv("CUSTOM_MAPS", raising=False)
    assert reader("folder", "/games/halo")[0] == "/games/halo/custom_maps"
    monkeypatch.setenv("CUSTOM_MAPS", "/srv/maps")
    assert reader("folder", "/games/halo")[0] == "/srv/maps"


def refused(reader, tmp_path, data: bytes, kind: str = "ce", words: str = "") -> str:
    path = tmp_path / ("bad.exe" if kind == "ce" else "bad.iso")
    path.write_bytes(data)
    folder = tmp_path / "out"
    out, log = reader(kind, path, folder)
    assert out.startswith("0 "), (out, log)
    assert words in out, out
    # (nothing half-written)
    target = folder / "maps_pc" if kind == "pc" else folder
    assert not any(p.name.endswith(".partial") for p in target.glob("*")) if target.exists() else True
    return out


def file_entry(cab: bytes, name: str) -> int:
    """where a file's entry is in a cabinet's file table"""
    at = struct.unpack_from("<I", cab, 16)[0]
    for _ in range(struct.unpack_from("<H", cab, 28)[0]):
        end = cab.index(b"\0", at + 16)
        if cab[at + 16:end].decode() == name:
            return at
        at = end + 1
    raise KeyError(name)


def test_refusals(reader, tmp_path):
    cab = ce_cabinet()
    assert "not Custom Edition's installer" in refused(reader, tmp_path, b"MZ" + bytes(5000))
    assert "not Custom Edition's installer" in refused(reader, tmp_path, cab)  # (no program before it)
    # split across cabinets
    refused(reader, tmp_path, installer(ce_cabinet(flags=2)), words="not Custom Edition's installer")
    # MSZIP (1) and Quantum (2) are not read
    for compression in (1, 2):
        bad = cabinet([(compression, CE_MAPS, None)])
        refused(reader, tmp_path, installer(bad), words="compression this cannot read")
    # an LZX window past 2 MB
    bad = bytearray(cabinet([(3, CE_MAPS, None)]))
    struct.pack_into("<H", bad, 36 + 6, 3 | (22 << 8))
    refused(reader, tmp_path, installer(bytes(bad)), words="compression this cannot read")
    # the data cut short: the cabinet says it is longer than it is
    refused(reader, tmp_path, installer(cab)[:-(len(cab) // 2 + 1234)], words="not Custom Edition's installer")
    # a data block cut short, past the file's end
    bad = bytearray(cab)
    first_data = struct.unpack_from("<I", bad, 36)[0]
    struct.pack_into("<H", bad, first_data + 4, 60000)
    refused(reader, tmp_path, installer(bytes(bad)), words="damaged")
    # an LZX block of a type there is none of (0: the first word's bits
    # 14-12, after the translation bit, are its second byte's 6-4)
    bad = bytearray(cab)
    bad[first_data + 8 + 1] &= 0x8f
    refused(reader, tmp_path, installer(bytes(bad)), words="damaged")
    # a map past its folder's data
    bad = bytearray(cab)
    struct.pack_into("<I", bad, file_entry(cab, "maps\\loc.map"), 0x7fffffff)
    refused(reader, tmp_path, installer(bytes(bad)), words="damaged")


def test_cd_refusals(reader, tmp_path):
    image = bytearray(pc_disc())
    # (no CD file system)
    refused(reader, tmp_path, bytes(40 * SECTOR), kind="pc", words="not a CD image")
    # a CD without Halo PC's maps
    refused(reader, tmp_path, iso_image({"README.TXT": b"x"}), kind="pc", words="no Halo PC maps")
    # a directory table larger than one can be
    bad = bytearray(image)
    struct.pack_into("<I", bad, 16 * SECTOR + 156 + 10, 64 << 20)
    refused(reader, tmp_path, bytes(bad), kind="pc", words="damaged")
    # a record whose name is longer than it
    root_sector = struct.unpack_from("<I", image, 16 * SECTOR + 156 + 2)[0]
    bad = bytearray(image)
    bad[root_sector * SECTOR + 68 + 32] = 255
    refused(reader, tmp_path, bytes(bad), kind="pc", words="damaged")


def test_damaged_cabinets_never_crash(reader, tmp_path):
    """bytes changed at random through the cabinet's tables and its LZX: the
    reader ends (refusing, or writing what the damaged data makes: LZX has
    no checksum), under the sanitizers when they are built in, and never
    leaves a .partial behind"""
    cab = ce_cabinet()
    generator = random.Random(313)
    for attempt in range(40):
        bad = bytearray(cab)
        for _ in range(generator.randrange(1, 12)):
            at = generator.randrange(len(bad)) if attempt % 2 else generator.randrange(min(len(bad), 400))
            bad[at] = generator.getrandbits(8)
        path = tmp_path / f"bad{attempt}.exe"
        path.write_bytes(installer(bytes(bad), decoy=False))
        folder = tmp_path / f"out{attempt}"
        out, log = reader("ce", path, folder)
        assert out[:2] in ("0 ", "1 ", "0", "1"), out
        if folder.exists():
            assert not [p.name for p in folder.iterdir() if p.name.endswith(".partial")]
