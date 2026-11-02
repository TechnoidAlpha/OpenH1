"""Vendors FFmpeg's Bink decoders (port/third_party/ffmpeg) from an unpacked
FFmpeg release: the files sources.txt lists and every header they include,
copied unchanged, and the configuration FFmpeg's configure would write for a
build of only the Bink demuxer and decoders (config/), written here instead.

The configuration is FFmpeg's plain C: no assembly, no threads, no hardware
decoding, no network, no encoders, nothing GPL; every HAVE_, CONFIG_ and
ARCH_ name the sources test is 0 but those below.

    python3 tools/ffmpeg_vendor.py <ffmpeg-8.1.3>
"""

import re
import shutil
import sys
from pathlib import Path

VERSION = "8.1.3"
TARGET = Path(__file__).resolve().parent.parent / "port/third_party/ffmpeg"

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


def include_closure(tree: Path, sources: list) -> list:
    """the sources and the headers they include, as paths in the tree"""
    seen, pending = set(), list(sources)
    while pending:
        relative = pending.pop()
        if relative in seen:
            continue
        seen.add(relative)
        for name in INCLUDE.findall((tree / relative).read_text(errors="replace")):
            if name in GENERATED or ARCHITECTURES & set(Path(name).parts):
                continue
            for candidate in (Path(relative).parent / name, Path(name)):
                path = tree / candidate
                if path.is_file():
                    pending.append(path.resolve().relative_to(tree).as_posix())
                    break
    return sorted(seen)


def configuration(files: list, tree: Path) -> dict:
    names, defined = set(), set()
    for relative in files:
        text = (tree / relative).read_text(errors="replace")
        names.update(NAME.findall(text))
        defined.update(DEFINE.findall(text))
    names -= NOT_CONFIGURATION | defined | set(STRINGS)
    names.update(ENABLED)
    return {name: int(name in ENABLED) for name in sorted(names)}


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def main() -> None:
    tree = Path(sys.argv[1]).resolve()
    if (tree / "VERSION").read_text().strip() != VERSION:
        raise SystemExit(f"{tree} is not FFmpeg {VERSION}")
    sources = [line.strip() for line in (TARGET / "sources.txt").read_text().splitlines()
               if line.strip() and not line.startswith("#")]
    # (with stdatomic.h FFmpeg's stand-in for a build without threads: the
    # guests' C library has none)
    files = include_closure(tree, sources + ["compat/atomics/dummy/stdatomic.h"])
    for name in ("COPYING.LGPLv2.1", "CREDITS", "LICENSE.md", "VERSION"):
        files.append(name)
    for directory in ("libavutil", "libavcodec", "libavformat", "compat"):
        shutil.rmtree(TARGET / directory, ignore_errors=True)
    for relative in files:
        write(TARGET / relative, (tree / relative).read_text(errors="surrogateescape"))

    values = configuration([f for f in files if f.endswith((".c", ".h"))], tree)
    config = TARGET / "config"
    header = "/* written by tools/ffmpeg_vendor.py for FFmpeg's Bink decoders (README.md) */\n"
    components = {name: value for name, value in values.items()
                  if name.endswith(("_DECODER", "_ENCODER", "_DEMUXER", "_MUXER", "_PARSER", "_BSF",
                                    "_PROTOCOL", "_HWACCEL", "_INDEV", "_OUTDEV", "_FILTER"))}
    lines = [header, "#ifndef FFMPEG_CONFIG_H", "#define FFMPEG_CONFIG_H",
             *(f"#define {name} {value}" for name, value in STRINGS.items()),
             "#define av_restrict restrict",
             *(f"#define {name} {value}" for name, value in values.items() if name not in components),
             "#endif", ""]
    write(config / "config.h", "\n".join(lines))
    write(config / "config_components.h", "\n".join([
        header, "#ifndef FFMPEG_CONFIG_COMPONENTS_H", "#define FFMPEG_CONFIG_COMPONENTS_H",
        *(f"#define {name} {value}" for name, value in components.items()), "#endif", ""]))
    write(config / "libavutil/avconfig.h", "\n".join([
        header, "#ifndef AVUTIL_AVCONFIG_H", "#define AVUTIL_AVCONFIG_H",
        "#define AV_HAVE_BIGENDIAN 0", "#define AV_HAVE_FAST_UNALIGNED 0", "#endif", ""]))
    write(config / "libavutil/ffversion.h", "\n".join([
        header, "#ifndef AVUTIL_FFVERSION_H", "#define AVUTIL_FFVERSION_H",
        f'#define FFMPEG_VERSION "{VERSION}"', "#endif", ""]))
    lists = {
        "libavcodec/codec_list.c": ("FFCodec", "codec_list", [f"ff_{name}_decoder" for name in DECODERS]),
        "libavcodec/parser_list.c": ("FFCodecParser", "parser_list", []),
        "libavcodec/bsf_list.c": ("FFBitStreamFilter", "bitstream_filters", []),
        "libavformat/demuxer_list.c": ("FFInputFormat", "demuxer_list", [f"ff_{name}_demuxer" for name in DEMUXERS]),
        "libavformat/muxer_list.c": ("FFOutputFormat", "muxer_list", []),
        "libavformat/protocol_list.c": ("URLProtocol", "url_protocols", []),
    }
    for relative, (kind, array, entries) in lists.items():
        write(config / relative, "\n".join([
            f"static const {kind} * const {array}[] = {{", *(f"    &{entry}," for entry in entries),
            "    NULL };", ""]))


if __name__ == "__main__":
    main()
