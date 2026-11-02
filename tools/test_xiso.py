"""The disc image extractor (port/linux/src/xiso.c) on made-up images: the
maps and the movies (the bink folder's .bik files) copied, what an image
cannot be trusted with refused, and a data root's movies kept. Built with
the build machine's C compiler, beside a stand-in for platform.h, as the iOS
host builds it (tools/ios_build.py)."""
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
DIRECTORY = 0x10


class Image:
    """an XDVDFS image of the game partition alone: folders of files, each
    file's bytes from its own sector (or a sector and size given, for an
    image that lies)"""

    def __init__(self):
        self.data = bytearray(34 * SECTOR)

    def _allocate(self, payload: bytes) -> int:
        sector = len(self.data) // SECTOR
        self.data += payload + b"\0" * (-len(payload) % SECTOR or 0)
        if not payload:
            self.data += b"\0" * SECTOR
        return sector

    @staticmethod
    def _table(entries) -> bytes:
        """entries (name, sector, size, attributes), each one's right link
        to the next"""
        table = bytearray()
        offsets = []
        for name, _sector, _size, _attributes in entries:
            offsets.append(len(table))
            length = 14 + len(name)
            table += b"\0" * (length + (-length % 4))
        for index, (name, sector, size, attributes) in enumerate(entries):
            right = offsets[index + 1] // 4 if index + 1 < len(entries) else 0
            struct.pack_into("<HHIIBB", table, offsets[index], 0, right, sector, size, attributes, len(name))
            table[offsets[index] + 14:offsets[index] + 14 + len(name)] = name
        table += b"\xff" * (-len(table) % SECTOR)
        return bytes(table)

    def folder(self, files: dict, lies: dict = None) -> tuple:
        """a folder's table, files {name: bytes}; lies {name: (sector, size)}"""
        entries = []
        for name, payload in files.items():
            sector = self._allocate(payload)
            entries.append((name.encode(), sector, len(payload), 0))
        for name, (sector, size) in (lies or {}).items():
            entries.append((name.encode(), sector, size, 0))
        table = self._table(entries)
        return self._allocate(table), len(table)

    def finish(self, folders: dict, path: Path, root_size: int = None) -> Path:
        """the root, of folders {name: (sector, size)}, and the descriptor"""
        root = self._table([(name.encode(), sector, size, DIRECTORY) for name, (sector, size) in folders.items()])
        root_sector = self._allocate(root)
        descriptor = bytearray(SECTOR)
        descriptor[0:20] = MAGIC
        struct.pack_into("<II", descriptor, 0x14, root_sector, root_size or len(root))
        descriptor[0x7EC:0x7EC + 20] = MAGIC
        self.data[0x10000:0x10000 + SECTOR] = descriptor
        path.write_bytes(bytes(self.data))
        return path


MAPS = {"ui.map": b"head" + bytes(60), "bloodgulch.map": bytes(range(256)) * 9}
MOVIES = {"intro.bik": b"BIKi" + bytes(range(200)) * 30, "attract1.bik": b"BIKi" + b"\x01" * 5000,
          "Credits_fr.BIK": b"BIKi" + b"\x02" * 100}


@pytest.fixture(scope="module")
def xiso(tmp_path_factory):
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("xiso_build")
    for name in ("xiso.c", "xiso.h", "posix.h"):
        shutil.copy(SOURCE / name, folder / name)
    (folder / "platform.h").write_text("void platform_log(const char *format, ...);\n")
    binary = folder / "xiso_test"
    # (HALO_GUEST: where the image was written beside the maps, as the
    # games' builds do)
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1",
                    "-D_GNU_SOURCE", "-DHALO_GUEST", f"-I{folder}", str(folder / "xiso.c"),
                    str(SOURCE / "posix_files.c"), str(ROOT / "port" / "linux" / "tests" / "xiso_test.c"),
                    "-o", str(binary)], check=True)

    def run(*arguments):
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True, check=True)
        return result.stdout.strip(), result.stderr
    return run


def halo_image(path: Path, movies: dict = MOVIES, movie_lies: dict = None, extra_folders: dict = None) -> Path:
    image = Image()
    folders = {"maps": image.folder(MAPS)}
    if movies is not None:
        folders["bink"] = image.folder(movies, movie_lies)
    folders.update(extra_folders or {})
    return image.finish(folders, path)


def test_maps_and_movies_copied(xiso, tmp_path):
    image = halo_image(tmp_path / "halo.iso", {**MOVIES, "readme.txt": b"not a movie"},
                       {"..\\escape.bik": (40, 10), "a/b.bik": (40, 10)})
    data = tmp_path / "data"
    data.mkdir()
    assert xiso("halo", image)[0] == "1"
    out, log = xiso("maps", image, data)
    assert out == "1", log
    assert {p.name: p.read_bytes() for p in (data / "maps").iterdir()} == MAPS
    # (only the .bik files with plain names)
    assert {p.name: p.read_bytes() for p in (data / "bink").iterdir()} == MOVIES
    assert not (data / "maps.partial").exists() and not (data / "bink.partial").exists()
    assert (data / "disc_image.txt").read_text() == f"{image}\n"
    assert not (tmp_path / "escape.bik").exists()


def test_disc_without_movies(xiso, tmp_path):
    image = halo_image(tmp_path / "halo.iso", movies=None)
    data = tmp_path / "data"
    data.mkdir()
    out, log = xiso("maps", image, data)
    assert out == "1", log
    assert (data / "maps" / "ui.map").exists()
    assert not (data / "bink").exists() and not (data / "bink.partial").exists()
    assert "no movies" in log
    assert xiso("movies", image, data)[0] == "0"


def test_movie_past_the_end_is_skipped(xiso, tmp_path):
    """a movie the image ends before, or larger than a movie can be, is not
    copied; the others are"""
    image = halo_image(tmp_path / "halo.iso", MOVIES,
                       {"attract2.bik": (60, 1 << 20), "attract3.bik": (40, (1 << 30) + 1)})
    data = tmp_path / "data"
    data.mkdir()
    out, log = xiso("maps", image, data)
    assert out == "1", log
    assert sorted(p.name for p in (data / "bink").iterdir()) == sorted(MOVIES)


def test_damaged_movies_folder_leaves_the_maps(xiso, tmp_path):
    """a bink folder whose table is too large to be one: the maps are copied,
    no movies"""
    image = Image()
    folders = {"maps": image.folder(MAPS), "bink": (40, 64 << 20)}
    path = image.finish(folders, tmp_path / "halo.iso")
    data = tmp_path / "data"
    data.mkdir()
    out, log = xiso("maps", path, data)
    assert out == "1", log
    assert (data / "maps" / "ui.map").exists() and not (data / "bink").exists()


def test_movies_alone_into_a_data_root(xiso, tmp_path):
    """bink_import.c's: the movies of an image into a data root with maps
    but no movies; a bink folder with movies is kept, an empty one filled"""
    image = halo_image(tmp_path / "halo.iso")
    data = tmp_path / "data"
    (data / "bink").mkdir(parents=True)
    out, log = xiso("movies", image, data)
    assert out == f"{len(MOVIES)}", log
    assert {p.name: p.read_bytes() for p in (data / "bink").iterdir()} == MOVIES

    kept = tmp_path / "kept"
    (kept / "bink").mkdir(parents=True)
    (kept / "bink" / "intro.bik").write_bytes(b"the player's own")
    out, log = xiso("movies", image, kept)
    assert out == f"{len(MOVIES)}", log
    assert [p.name for p in (kept / "bink").iterdir()] == ["intro.bik"]
    assert (kept / "bink" / "intro.bik").read_bytes() == b"the player's own"


def test_not_an_image(xiso, tmp_path):
    path = tmp_path / "not.iso"
    path.write_bytes(b"\0" * (40 * SECTOR))
    out, _ = xiso("movies", path, tmp_path)
    assert out.startswith("-1 This is not an Xbox disc image.")
    out, _ = xiso("maps", path, tmp_path)
    assert out.startswith("0 ")
    assert xiso("halo", path)[0] == "0"
    assert xiso("movies", tmp_path / "missing.iso", tmp_path)[0].startswith("-1 Could not open")
