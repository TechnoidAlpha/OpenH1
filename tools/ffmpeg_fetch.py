"""FFmpeg's Bink decoders (port/third_party/ffmpeg; display.movies,
port/linux/src/bink_ffmpeg.c), fetched at configure time as musl and SDL3
are (tools/android_build.py, fetch_third_party): the release's archive,
checked against FFMPEG_SHA256, of which the files sources.txt lists and every
header they include are kept in <third_party>/ffmpeg-<version>, and the
configuration FFmpeg's configure would write for a build of only the Bink
demuxer and decoders (its config/), written here instead.

The archive is read as text: nothing in it is run (not its configure, nor
any other script). The configuration is FFmpeg's plain C: no assembly, no
threads, no hardware decoding, no network, no encoders, nothing GPL; every
HAVE_, CONFIG_ and ARCH_ name the sources test is 0 but those below.
"""

import io
import re
import shutil
import tarfile
from pathlib import Path
from typing import Dict, List

from .build_common import FFMPEG_DIR, download

FFMPEG_VERSION = "8.1.3"
FFMPEG_URL = f"https://ffmpeg.org/releases/ffmpeg-{FFMPEG_VERSION}.tar.xz"
FFMPEG_SHA256 = "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"

# the components (configure's --enable-decoder=bink,binkaudio_rdft,
# binkaudio_dct --enable-demuxer=bink, and what they select)
DECODERS = ["bink", "binkaudio_rdft", "binkaudio_dct"]
DEMUXERS = ["bink"]
ENABLED = [
    "CONFIG_AVCODEC", "CONFIG_AVFORMAT", "CONFIG_AVUTIL", "CONFIG_STATIC", "CONFIG_SMALL",
    # (a corrupt movie read past its packet stops at its end)
    "CONFIG_SAFE_BITSTREAM_READER",
    "CONFIG_BLOCKDSP", "CONFIG_HPELDSP", "CONFIG_WMA_FREQS",
    *(f"CONFIG_{name.upper()}_DECODER" for name in DECODERS),
    *(f"CONFIG_{name.upper()}_DEMUXER" for name in DEMUXERS),
    # what the guests' C library (musl) and its maths have
    "HAVE_UNISTD_H", "HAVE_CLOCK_GETTIME", "HAVE_GETTIMEOFDAY", "HAVE_NANOSLEEP", "HAVE_USLEEP",
    "HAVE_POSIX_MEMALIGN", "HAVE_ISATTY", "HAVE_LOCALTIME_R", "HAVE_GMTIME_R", "HAVE_STRERROR_R",
    "HAVE_SYSCONF", "HAVE_FCNTL",
    "HAVE_ATANF", "HAVE_ATAN2F", "HAVE_CBRT", "HAVE_CBRTF", "HAVE_COPYSIGN", "HAVE_COSF", "HAVE_ERF",
    "HAVE_EXP2", "HAVE_EXP2F", "HAVE_EXPF", "HAVE_HYPOT", "HAVE_ISFINITE", "HAVE_ISINF", "HAVE_ISNAN",
    "HAVE_LDEXPF", "HAVE_LLRINT", "HAVE_LLRINTF", "HAVE_LOG2", "HAVE_LOG2F", "HAVE_LOG10F", "HAVE_LRINT",
    "HAVE_LRINTF", "HAVE_POWF", "HAVE_RINT", "HAVE_ROUND", "HAVE_ROUNDF", "HAVE_SINF", "HAVE_TRUNC",
    "HAVE_TRUNCF",
]
# names the sources test that are not configure's
NOT_CONFIGURATION = {"HAVE_AV_CONFIG_H", "CONFIG_H"}
STRINGS = {
    "FFMPEG_CONFIGURATION": '"OpenH1: the Bink demuxer and decoders (port/third_party/ffmpeg)"',
    "FFMPEG_LICENSE": '"LGPL version 2.1 or later"',
    "CONFIG_THIS_YEAR": "2026",
    "FFMPEG_DATADIR": '""',
    "AVCONV_DATADIR": '""',
    "CC_IDENT": '"clang"',
    "OS_NAME": "openh1",
    "EXTERN_PREFIX": '""',
    "EXTERN_ASM": "",
    "BUILDSUF": '""',
    "SLIBSUF": '""',
    "SWS_MAX_FILTER_SIZE": "256",
}

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
NAME = re.compile(r"\b((?:HAVE|CONFIG|ARCH)_[A-Z0-9_]+)\b")
DEFINE = re.compile(r"^\s*#\s*define\s+((?:HAVE|CONFIG|ARCH)_[A-Z0-9_]+)\b", re.M)
# (the processors' own code, which ARCH_ 0 leaves out)
ARCHITECTURES = {"x86", "arm", "aarch64", "ppc", "mips", "loongarch", "riscv", "wasm"}
# the files the configuration takes the place of
GENERATED = {"config.h", "config_components.h", "libavutil/avconfig.h", "libavutil/ffversion.h",
             "libavcodec/codec_list.c", "libavcodec/parser_list.c", "libavcodec/bsf_list.c",
             "libavformat/demuxer_list.c", "libavformat/muxer_list.c", "libavformat/protocol_list.c"}
# FFmpeg's own stand-in for stdatomic.h in a build without threads (the
# guests' C library has none), and the release's licence and version
EXTRA_FILES = ["compat/atomics/dummy/stdatomic.h"]
TOP_FILES = ["COPYING.LGPLv2.1", "VERSION"]
# (the tree's record of the sources it was made for: another list, or another
# release, makes it again)
STAMP = "openh1-sources.txt"


def ffmpeg_tree(third_party: Path) -> Path:
    """where fetch_ffmpeg keeps FFmpeg's files: <third_party>/ffmpeg-<version>"""
    return third_party / f"ffmpeg-{FFMPEG_VERSION}"


def ffmpeg_sources(tree: Path) -> List[Path]:
    """FFmpeg's sources the builds compile (port/third_party/ffmpeg/sources.txt), in tree"""
    return [tree / name for name in _source_names()]


def _source_names() -> List[str]:
    return [line.strip() for line in (FFMPEG_DIR / "sources.txt").read_text().splitlines()
            if line.strip() and not line.startswith("#")]


def include_closure(files: Dict[str, str], sources: List[str]) -> List[str]:
    """the sources and the headers they include, as the archive's paths"""
    seen, pending = set(), list(sources)
    while pending:
        relative = pending.pop()
        if relative in seen:
            continue
        seen.add(relative)
        for name in INCLUDE.findall(files[relative]):
            if name in GENERATED or ARCHITECTURES & set(Path(name).parts):
                continue
            for candidate in (Path(relative).parent / name, Path(name)):
                path = _normal(candidate.as_posix())
                if path in files:
                    pending.append(path)
                    break
    return sorted(seen)


def _normal(path: str) -> str:
    """a/b/../c as a/c"""
    parts: List[str] = []
    for part in path.split("/"):
        if part == "..":
            if parts:
                parts.pop()
        elif part not in ("", "."):
            parts.append(part)
    return "/".join(parts)


def configuration(files: Dict[str, str]) -> Dict[str, int]:
    """configure's names the files test, each 1 or 0"""
    names, defined = set(), set()
    for text in files.values():
        names.update(NAME.findall(text))
        defined.update(DEFINE.findall(text))
    names -= NOT_CONFIGURATION | defined | set(STRINGS)
    names.update(ENABLED)
    return {name: int(name in ENABLED) for name in sorted(names)}


def _write(path: Path, text: str) -> None:
    """text in path, left alone when it already is (no rebuild for nothing)"""
    if path.is_file() and path.read_text() == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


def _read_archive(data: bytes) -> Dict[str, str]:
    """the archive's regular files, as text by their paths in the release"""
    prefix = f"ffmpeg-{FFMPEG_VERSION}/"
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as tar:
        for member in tar:
            if not member.isfile() or not member.name.startswith(prefix):
                continue
            relative = member.name[len(prefix):]
            if relative.endswith((".c", ".h")) or relative in TOP_FILES:
                source = tar.extractfile(member)
                assert source is not None
                files[relative] = source.read().decode("utf-8", errors="surrogateescape")
    return files


def write_configuration(tree: Path) -> None:
    """config/ in tree: config.h, config_components.h, libavutil's avconfig.h
    and ffversion.h, and the components' lists, from the files kept there"""
    files = {path.relative_to(tree).as_posix(): path.read_text(errors="surrogateescape")
             for path in tree.rglob("*.[ch]") if path.relative_to(tree).parts[0] != "config"}
    values = configuration(files)
    config = tree / "config"
    header = "/* written by tools/ffmpeg_fetch.py for FFmpeg's Bink decoders (README.md) */\n"
    components = {name: value for name, value in values.items()
                  if name.endswith(("_DECODER", "_ENCODER", "_DEMUXER", "_MUXER", "_PARSER", "_BSF",
                                    "_PROTOCOL", "_HWACCEL", "_INDEV", "_OUTDEV", "_FILTER"))}
    lines = [header, "#ifndef FFMPEG_CONFIG_H", "#define FFMPEG_CONFIG_H",
             *(f"#define {name} {value}" for name, value in STRINGS.items()),
             "#define av_restrict restrict",
             *(f"#define {name} {value}" for name, value in values.items() if name not in components),
             "#endif", ""]
    _write(config / "config.h", "\n".join(lines))
    _write(config / "config_components.h", "\n".join([
        header, "#ifndef FFMPEG_CONFIG_COMPONENTS_H", "#define FFMPEG_CONFIG_COMPONENTS_H",
        *(f"#define {name} {value}" for name, value in components.items()), "#endif", ""]))
    _write(config / "libavutil/avconfig.h", "\n".join([
        header, "#ifndef AVUTIL_AVCONFIG_H", "#define AVUTIL_AVCONFIG_H",
        "#define AV_HAVE_BIGENDIAN 0", "#define AV_HAVE_FAST_UNALIGNED 0", "#endif", ""]))
    _write(config / "libavutil/ffversion.h", "\n".join([
        header, "#ifndef AVUTIL_FFVERSION_H", "#define AVUTIL_FFVERSION_H",
        f'#define FFMPEG_VERSION "{FFMPEG_VERSION}"', "#endif", ""]))
    lists = {
        "libavcodec/codec_list.c": ("FFCodec", "codec_list", [f"ff_{name}_decoder" for name in DECODERS]),
        "libavcodec/parser_list.c": ("FFCodecParser", "parser_list", []),
        "libavcodec/bsf_list.c": ("FFBitStreamFilter", "bitstream_filters", []),
        "libavformat/demuxer_list.c": ("FFInputFormat", "demuxer_list", [f"ff_{name}_demuxer" for name in DEMUXERS]),
        "libavformat/muxer_list.c": ("FFOutputFormat", "muxer_list", []),
        "libavformat/protocol_list.c": ("URLProtocol", "url_protocols", []),
    }
    for relative, (kind, array, entries) in lists.items():
        _write(config / relative, "\n".join([
            f"static const {kind} * const {array}[] = {{", *(f"    &{entry}," for entry in entries),
            "    NULL };", ""]))


def fetch_ffmpeg(third_party: Path) -> Path:
    """FFmpeg's files and their configuration in ffmpeg_tree(third_party)
    (configure time; downloaded once, and again when sources.txt or the
    version changes; the configuration written each time, unchanged files
    left alone). A failed download, or one whose SHA-256 is not
    FFMPEG_SHA256, raises RuntimeError. Returns the tree."""
    tree = ffmpeg_tree(third_party)
    sources = _source_names()
    stamp = "\n".join([FFMPEG_VERSION, *sources, ""])
    if not (tree / STAMP).is_file() or (tree / STAMP).read_text() != stamp:
        print(f"Downloading {FFMPEG_URL}")
        files = _read_archive(download([FFMPEG_URL], FFMPEG_SHA256))
        if files.get("VERSION", "").strip() != FFMPEG_VERSION:
            raise RuntimeError(f"{FFMPEG_URL} is not FFmpeg {FFMPEG_VERSION}")
        # (written beside it and then moved, so that an interrupted one leaves
        # no folder that a later configure would take as complete)
        partial = tree.with_name(tree.name + ".partial")
        shutil.rmtree(partial, ignore_errors=True)
        for relative in include_closure(files, sources + EXTRA_FILES) + TOP_FILES:
            path = partial / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(files[relative].encode("utf-8", errors="surrogateescape"))
        (partial / STAMP).write_bytes(stamp.encode())
        shutil.rmtree(tree, ignore_errors=True)
        partial.rename(tree)
    write_configuration(tree)
    return tree
