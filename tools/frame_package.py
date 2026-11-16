#!/usr/bin/env python3
"""Packages the VR build for a Steam Frame: dist/halo-vr-frame/ and
dist/halo-vr-frame.tar.gz, which its install.sh (tools/linux_install.sh)
installs, on the Frame or, from a computer, through tools/frame_install.sh.

    python3 configure.py --vr && ninja linux_arm64
    python3 tools/frame_package.py
    python3 tools/frame_package.py --zip OpenH1-Steam-Frame.zip

--zip also writes the package as a zip (halo-vr-frame/ in it, the files with
their Unix modes), as the releases have it: tools/ci_build.py linux_arm64
release --vr makes the release's OpenH1-Steam-Frame.zip so.

The package is what ~/apps/halo-vr/ on the Frame needs beside the game's
data and the player's own files: the game (build/linux_arm64/halo), the
SDL3 it finds beside it, the launcher of its Steam shortcut
(port/linux/frame/halo-vr.sh), internet play's brokers.txt, its installer
(install.sh: tools/linux_install.sh, the Linux release's installer too,
which installs the package it is in when opened on the Frame), the icon of
its menu entry and Steam shortcut (openh1.png), that menu entry
(openh1.desktop, the Linux release's, which install.sh makes the
installed game's), Steam's library artwork for the shortcut
(steam-<kind>.png: OpenH1 VR's, tools/steam_art.py), a README
(port/linux/frame/README.md), the libraries' licenses (as
tools/ci_build.py's dist has them), VERSION (the commit) and SHA256SUMS.
Never config.toml: the player's own, which an install must not replace.

The game must be a VR build, for 64-bit ARM, and each library it or its
SDL3 needs must be in the package or one that SteamOS has (the C library,
Vulkan's loader); else this stops, as the Frame would not start it. Refer to "Steam Frame" in port/linux/README.md.
"""

import argparse
import datetime
import hashlib
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path
from typing import List

ROOT = Path(__file__).resolve().parent.parent
FRAME_DIR = ROOT / "port/linux/frame"
NAME = "halo-vr-frame"

# the libraries a Frame has (glibc's, and Vulkan's loader, which the game
# loads through SDL rather than links)
SYSTEM_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-aarch64.so.1",
    "libvulkan.so.1",
}
# (the package's name for each, from the repository: OpenH1's own, the AGPL,
# and those of what it carries; as tools/ci_build.py;
# SDL3's, from the build's folder, is added in main)
LICENSES = {
    "LICENSE.txt": "LICENSE",
    "mbedtls-LICENSE.txt": "port/third_party/mbedtls/LICENSE",
    "miniupnpc-LICENSE.txt": "port/third_party/miniupnpc/LICENSE",
    "CyborgInterfaceGothic-OFL.txt": "port/assets/fonts/CyborgInterfaceGothic-OFL.txt",
    "Overpass-OFL.txt": "port/assets/fonts/Overpass-OFL.txt",
    "Hack-LICENSE.txt": "port/assets/fonts/Hack-LICENSE.txt",
    "OpenCE-OFL.txt": "port/assets/fonts/OpenCE-OFL.txt",
    "Newtown-LICENSE.txt": "port/assets/fonts/Newtown-LICENSE.txt",
    "msdfgen-LICENSE.txt": "port/third_party/msdfgen/LICENSE.txt",
    "expat-COPYING.txt": "port/third_party/expat/COPYING",
    "opus-COPYING.txt": "port/third_party/opus/COPYING",
    "ffmpeg-COPYING.LGPLv2.1.txt": "port/third_party/ffmpeg/COPYING.LGPLv2.1",
    "lucide-LICENSE.txt": "port/assets/icons/lucide/LICENSE",
}
# the files an install never writes over (the player's)
PLAYER_FILES = {"config.toml", "halo-vr.env", "debug.txt", "halo-vr.log", "maps"}

EM_AARCH64 = 183
DT_NEEDED, DT_STRTAB = 1, 5
PT_LOAD, PT_DYNAMIC = 1, 2


def needed_libraries(path: Path) -> List[str]:
    """The DT_NEEDED entries of a 64-bit little-endian ARM ELF file; stops if
    it is not one."""
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != EM_AARCH64:
        sys.exit(f"{path}: not a 64-bit ARM executable (build with configure.py --vr, then ninja linux_arm64)")
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    loads, dynamic = [], None
    for index in range(phnum):
        kind, _, offset, vaddr, _, filesz, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phentsize)
        if kind == PT_LOAD:
            loads.append((vaddr, offset, filesz))
        elif kind == PT_DYNAMIC:
            dynamic = (offset, filesz)
    if dynamic is None:
        return []

    def file_offset(address: int) -> int:
        for vaddr, offset, filesz in loads:
            if vaddr <= address < vaddr + filesz:
                return offset + address - vaddr
        sys.exit(f"{path}: a dynamic entry outside the file")

    entries = [struct.unpack_from("<qQ", data, dynamic[0] + at) for at in range(0, dynamic[1], 16)]
    strtab = file_offset(next(value for tag, value in entries if tag == DT_STRTAB))
    return [data[strtab + value:data.index(b"\0", strtab + value)].decode() for tag, value in entries
            if tag == DT_NEEDED]


def version() -> str:
    """VERSION: the commit the package is of, which the install prints"""
    def git(*args: str) -> str:
        # (CI's container runs the build as a user other than the checkout's,
        # whose repository git would otherwise refuse as of dubious ownership)
        return subprocess.run(["git", "-c", f"safe.directory={ROOT}", *args], cwd=ROOT, capture_output=True,
                              text=True).stdout.strip()

    changes = " (with changes)" if git("status", "--porcelain", "-uno") else ""
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return (f"commit {git('rev-parse', 'HEAD') or 'unknown'}{changes}\n"
            f"branch {git('rev-parse', '--abbrev-ref', 'HEAD') or 'unknown'}\n"
            f"{git('log', '-1', '--format=%s')}\n"
            f"packaged {stamp}\n")


def executable(name: str) -> bool:
    """the package's files that run (0755; the others are 0644)"""
    return name in ("halo", "libSDL3.so.0", "halo-vr.sh", "install.sh")


def write_zip(dist: Path, output: Path) -> None:
    """the package's folder into the zip output, under its name, in name order,
    with each file's Unix mode (for unzip on the Frame, and install.sh)"""
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        info = zipfile.ZipInfo(f"{dist.name}/", date_time=(1980, 1, 1, 0, 0, 0))
        info.create_system = 3
        info.external_attr = (stat.S_IFDIR | 0o755) << 16 | 0x10
        archive.writestr(info, b"")
        for path in sorted(dist.iterdir()):
            info = zipfile.ZipInfo.from_file(path, f"{dist.name}/{path.name}")
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if executable(path.name) else 0o644)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", type=Path, default=ROOT / "build/linux_arm64",
                        help="the VR build's folder (default: build/linux_arm64)")
    parser.add_argument("--out", type=Path, default=ROOT / "dist",
                        help=f"where {NAME}/ and {NAME}.tar.gz go (default: dist)")
    parser.add_argument("--zip", type=Path,
                        help="also write the package as this zip (as the release's OpenH1-Steam-Frame.zip)")
    args = parser.parse_args()

    game = args.build / "halo"
    if not game.is_file():
        sys.exit(f"{game}: no game; build it first: python3 configure.py --vr && ninja linux_arm64")
    data = game.read_bytes()
    # (host_vr.c's: the runtime's negotiation, and its Vulkan binding)
    if b"xrNegotiateLoaderRuntimeInterface" not in data or b"XR_KHR_vulkan_enable2" not in data:
        sys.exit(f"{game}: not a VR build (python3 configure.py --vr, then ninja linux_arm64)")

    files = {
        "halo": game,
        "libSDL3.so.0": args.build / "libSDL3.so.0",
        "SDL3-LICENSE.txt": args.build / "third_party/SDL3/LICENSE.txt",
        "halo-vr.sh": FRAME_DIR / "halo-vr.sh",
        "install.sh": ROOT / "tools/linux_install.sh",
        "openh1.png": ROOT / "port/assets/icon/hicolor/openh1-256.png",
        "openh1.desktop": ROOT / "port/assets/linux/openh1.desktop",
        **{f"steam-{art.name}": art for art in sorted((ROOT / "port/assets/steam/openh1-vr").glob("*.png"))},
        "brokers.txt": ROOT / "port/assets/network/brokers.txt",
        "prepared.records": ROOT / "port/assets/prepared/prepared.records",
        "openh1.assets": args.build / "openh1.assets",
        "README.md": FRAME_DIR / "README.md",
        **{name: ROOT / source for name, source in LICENSES.items()},
    }
    assert not PLAYER_FILES & set(files)
    for name, source in files.items():
        if not source.is_file():
            sys.exit(f"{source}: missing (for {name})")
    # each library the game (or SDL3) needs: in the package, or the Frame's
    for name in ("halo", "libSDL3.so.0"):
        missing = [library for library in needed_libraries(files[name])
                   if library not in files and library not in SYSTEM_LIBRARIES]
        if missing:
            sys.exit(f"{name} needs {', '.join(missing)}, which is neither in the package nor one of the "
                     f"libraries every Steam Frame has (SYSTEM_LIBRARIES)")

    dist = args.out / NAME
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    for name, source in files.items():
        shutil.copyfile(source, dist / name)
        (dist / name).chmod(0o755 if executable(name) else 0o644)
    (dist / "VERSION").write_text(version(), encoding="utf-8")
    sums = [f"{hashlib.sha256((dist / name).read_bytes()).hexdigest()}  {name}\n"
            for name in [*files, "VERSION"]]
    (dist / "SHA256SUMS").write_text("".join(sums), encoding="utf-8")
    (dist / "VERSION").chmod(0o644)
    (dist / "SHA256SUMS").chmod(0o644)

    tarball = args.out / f"{NAME}.tar.gz"
    def anyone(info: tarfile.TarInfo) -> tarfile.TarInfo:
        # (the files are whoever unpacks them's, not this computer's user)
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        return info

    with tarfile.open(tarball, "w:gz") as archive:
        archive.add(dist, arcname=NAME, filter=anyone)
    if args.zip:
        write_zip(dist, args.zip)
    print((dist / "VERSION").read_text(encoding="utf-8"), end="")
    for line in sums:
        digest, name = line.split()
        print(f"  {name:28} {(dist / name).stat().st_size:>10,} bytes  {digest[:16]}")
    print(f"{dist}\n{tarball}" + (f"\n{args.zip}" if args.zip else "") + "\nInstall it: tools/frame_install.sh <frame>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
