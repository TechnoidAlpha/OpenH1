#!/usr/bin/env python3
"""Lays out the Linux release (tools/ci_build.py), and zips it:

    python tools/linux_package.py tree <folder>    (the tree of this computer's Linux build)
    python tools/linux_package.py zip dist/halo-linux-x86_64-release OpenH1-Linux.zip

The release is a tree laid out as a Unix prefix is, which a package manager
can copy to /usr (or /opt/<name>) as it is, and which a player unpacks
anywhere and starts with bin/openh1:

    install.sh                                 its installer (tools/linux_install.sh: the Steam Deck's)
    bin/openh1                                 the game
    lib/openh1/libSDL3.so.0                    its SDL (the rpath is $ORIGIN:$ORIGIN/../lib/openh1)
    share/openh1/brokers.txt                   internet play's brokers (network.brokers_file)
    share/openh1/masters.txt                   the master servers (network.masters_file)
    share/openh1/prepared.records              the stock maps' preparations' records (prepared_shipped.h)
    share/openh1/openh1.assets                 the textures', fonts' and menus' bytes (embedded_assets.h)
    share/openh1/relays.txt                    the relays offered (network.relays_file)
    share/openh1/vr                            only in the VR build's (--vr): install.sh's sign of it
    share/openh1/steam/<kind>.png              Steam's library artwork, which install.sh gives its shortcut
                                               (port/assets/steam, tools/steam_art.py; the VR build's OpenH1 VR's)
    share/applications/openh1.desktop          the desktop entry
    share/icons/hicolor/<n>x<n>/apps/openh1.png  its icon
    share/doc/openh1/                          the READMEs
    share/licenses/openh1/                     the project's licence and those of what it carries

The game finds its files from where it is (port/linux/src/install_layout.c).
install.sh, opened in the unpacked tree, copies it to ~/Applications/OpenH1
and gives it a menu entry and a Steam shortcut: the release's zips have it,
the tree for a package (tree, below) has not.

tree lays out a Linux build (build/linux_x86_64, or build/linux_arm64: the
one of this computer's architecture unless --build names another) in a
folder, for a package built from the sources: its contents go into the
package's /usr, with the SDL the build brings (libSDL3.so.0 and
SDL3-LICENSE.txt), without install.sh. zip writes the folder's files in
name order, with their Unix modes (executables by their place, as GitHub's
artifacts lose the modes), for unzip and the self-updater.
"""

import argparse
import platform
import shutil
import stat
import sys
import zipfile
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent
NAME = "openh1"  # install_layout.h's INSTALL_NAME

DESKTOP_ENTRY = ROOT / "port/assets/linux/openh1.desktop"
ICONS = ROOT / "port/assets/icon/hicolor"  # openh1-<size>.png (tools/app_icon.py)
BROKERS = ROOT / "port/assets/network/brokers.txt"
MASTERS = ROOT / "port/assets/network/masters.txt"
RELAYS = ROOT / "port/assets/network/relays.txt"
# the stock maps' preparations' records (port/linux/src/prepared_shipped.h)
PREPARED_RECORDS = ROOT / "port/assets/prepared/prepared.records"
ASSETS = "openh1.assets"
INSTALLER = ROOT / "tools/linux_install.sh"  # the tree's install.sh (and the Steam Frame package's)
STEAM_ART = ROOT / "port/assets/steam"  # <variant>/<kind>.png (tools/steam_art.py)
DOCUMENTS = [(ROOT / "README.md", "README.md"), (ROOT / "port/linux/README.md", "README.linux.md")]
PROJECT_LICENSE = ROOT / "LICENSE"  # the AGPL (earlier releases were CC0: LICENSES/)


def notices(platform: str) -> List[Tuple[Path, str]]:
    """the licences of what a platform's build carries, which its release
    has beside it ((source, name in the release) pairs; tools/ci_build.py's
    for every platform; the Linux build's SDL3-LICENSE.txt is apart)"""
    pairs = [
        # internet play's UPnP (port/third_party/miniupnpc), in every build,
        # whose BSD license asks binaries to carry its notice
        (ROOT / "port/third_party/miniupnpc/LICENSE", "miniupnpc-LICENSE.txt"),
        # the text's font, Cyborg Interface Gothic, by fizzy, and the font of
        # what it lacks, Overpass (port/assets/fonts), embedded in every
        # build, whose SIL Open Font License asks each copy to carry it
        (ROOT / "port/assets/fonts/CyborgInterfaceGothic-OFL.txt", "CyborgInterfaceGothic-OFL.txt"),
        (ROOT / "port/assets/fonts/Overpass-OFL.txt", "Overpass-OFL.txt"),
        # the console's font (port/assets/fonts), embedded in every build,
        # whose MIT and Bitstream Vera licenses ask copies to carry them
        (ROOT / "port/assets/fonts/Hack-LICENSE.txt", "Hack-LICENSE.txt"),
        # the menus' headings' font (port/assets/fonts), embedded in every
        # build, whose SIL Open Font License asks each copy to carry it
        (ROOT / "port/assets/fonts/OpenCE-OFL.txt", "OpenCE-OFL.txt"),
        # the font OpenCE is made from, Newtown, in the public domain, whose
        # statement OpenCE-OFL.txt names
        (ROOT / "port/assets/fonts/Newtown-LICENSE.txt", "Newtown-LICENSE.txt"),
        # the high-res text's distance fields (port/third_party/msdfgen/msdf_gen.h,
        # msdfgen translated to C), in every build, whose MIT license asks
        # copies to carry its notice
        (ROOT / "port/third_party/msdfgen/LICENSE.txt", "msdfgen-LICENSE.txt"),
        # the menus' XML parser (port/third_party/expat), in every build,
        # whose MIT license asks copies to carry its notice
        (ROOT / "port/third_party/expat/COPYING", "expat-COPYING.txt"),
        # the MCC maps' BC texture decoder (port/third_party/bcdec), in every
        # build, whose MIT license asks copies to carry its notice
        (ROOT / "port/third_party/bcdec/LICENSE", "bcdec-LICENSE.txt"),
        # voice chat's codec (port/third_party/opus), in every build, whose
        # BSD license asks binaries to carry its notice
        (ROOT / "port/third_party/opus/COPYING", "opus-COPYING.txt"),
        # the Bink movies' decoders (port/third_party/ffmpeg), in every
        # build, whose LGPL asks copies to carry it
        (ROOT / "port/third_party/ffmpeg/COPYING.LGPLv2.1", "ffmpeg-COPYING.LGPLv2.1.txt"),
        # voice chat's speaker icons (port/assets/icons/lucide, drawn into
        # the menus' bitmaps), whose ISC license asks copies to carry its
        # notice
        (ROOT / "port/assets/icons/lucide/LICENSE", "lucide-LICENSE.txt"),
        # the JSON tokenizer (port/third_party/jsmn) of the server browser's
        # master servers' lists and a dedicated server's API, in every
        # build, whose MIT license asks copies to carry its notice
        (ROOT / "port/third_party/jsmn/LICENSE", "jsmn-LICENSE.txt"),
    ]
    if platform.startswith("linux") or platform in ("macos", "ios", "android"):
        # the self-updater's and the master servers' TLS
        # (port/third_party/mbedtls: posix_update.c, in the Linux, Apple and
        # Android hosts), whose Apache license asks binaries to carry it
        pairs.insert(0, (ROOT / "port/third_party/mbedtls/LICENSE", "mbedtls-LICENSE.txt"))
    if platform in ("macos", "ios", "android"):
        # the DXT textures' ASTC encoder (port/third_party/astc-encoder), in
        # the Apple and Android hosts, whose Apache license asks the same
        pairs.append((ROOT / "port/third_party/astc-encoder/LICENSE.txt", "astc-encoder-LICENSE.txt"))
    return pairs


def _copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def stage_tree(dist: Path, executable: Path, library: Optional[Path], notices: Iterable[Tuple[Path, str]],
               installer: bool = True, vr: bool = False) -> None:
    """The release's tree in dist: the executable as bin/openh1, SDL in
    lib/openh1 (none: the system's), and notices ((source, name) pairs: the
    licences of what the build carries) in share/licenses/openh1; and, for
    the release (not a package's /usr), its installer, install.sh, at its
    top. The VR build's (vr) has share/openh1/vr, by which install.sh puts
    it beside the other build, not in its place. The release's has Steam's
    library artwork too (share/openh1/steam), which install.sh gives the
    game's Steam shortcut: OpenH1 VR's in the VR build's."""
    if installer:
        _copy(INSTALLER, dist / "install.sh")
        for art in sorted((STEAM_ART / ("openh1-vr" if vr else "openh1")).glob("*.png")):
            _copy(art, dist / "share" / NAME / "steam" / art.name)
    _copy(executable, dist / "bin" / NAME)
    if library:
        _copy(library, dist / "lib" / NAME / library.name)
    _copy(BROKERS, dist / "share" / NAME / BROKERS.name)
    _copy(MASTERS, dist / "share" / NAME / MASTERS.name)
    _copy(RELAYS, dist / "share" / NAME / RELAYS.name)
    _copy(PREPARED_RECORDS, dist / "share" / NAME / PREPARED_RECORDS.name)
    # (the textures', fonts' and menus' bytes, beside the build's executable:
    # port/linux/src/embedded_assets.h)
    _copy(executable.parent / ASSETS, dist / "share" / NAME / ASSETS)
    if vr:
        (dist / "share" / NAME / "vr").write_text(
            "The VR build (HALO_VR). install.sh installs it in ~/Applications/OpenH1-VR, beside the other build.\n")
    _copy(DESKTOP_ENTRY, dist / "share/applications" / f"{NAME}.desktop")
    for icon in sorted(ICONS.glob(f"{NAME}-*.png")):
        size = icon.stem.rsplit("-", 1)[1]
        _copy(icon, dist / "share/icons/hicolor" / f"{size}x{size}" / "apps" / f"{NAME}.png")
    for source, name in DOCUMENTS:
        _copy(source, dist / "share/doc" / NAME / name)
    _copy(PROJECT_LICENSE, dist / "share/licenses" / NAME / PROJECT_LICENSE.name)
    for source, name in notices:
        _copy(source, dist / "share/licenses" / NAME / name)


def mode(name: str) -> int:
    """a file's mode in the zip, from its place: the executables and the
    shared libraries of the tree, and its install.sh, are 0755"""
    parts = name.split("/")
    if name == "install.sh":
        return 0o755
    if parts[0] == "bin" and len(parts) == 2:
        return 0o755
    if ".so" in parts[-1] and parts[0] == "lib":
        return 0o755
    return 0o644


def write_zip(folder: Path, output: Path) -> None:
    """folder's files (and its folders) into the zip output, in name order"""
    entries = sorted(path for path in folder.rglob("*"))
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in entries:
            name = path.relative_to(folder).as_posix()
            if path.is_dir():
                info = zipfile.ZipInfo(name + "/", date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (stat.S_IFDIR | 0o755) << 16 | 0x10
                archive.writestr(info, b"")
                continue
            info = zipfile.ZipInfo.from_file(path, name)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode(name)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as file:
                archive.writestr(info, file.read(), compresslevel=9)
    print(f"{folder} -> {output}", flush=True)


def native_build() -> Path:
    """this computer's Linux build's folder: build/linux_arm64 on 64-bit ARM,
    else build/linux_x86_64"""
    arm = platform.machine().lower() in ("aarch64", "arm64")
    return ROOT / "build" / ("linux_arm64" if arm else "linux_x86_64")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    tree_command = commands.add_parser("tree", help="lay out a Linux build as the release's tree")
    tree_command.add_argument("folder", type=Path)
    tree_command.add_argument("--build", type=Path, default=native_build(),
                              help=f"the build's folder (default: {native_build().relative_to(ROOT)})")
    zip_command = commands.add_parser("zip", help="zip a staged release with its files' modes")
    zip_command.add_argument("folder", type=Path)
    zip_command.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command == "tree":
        library = args.build / "libSDL3.so.0"
        sdl_license = args.build / "SDL3-LICENSE.txt"
        if not (args.build / "halo").exists():
            parser.error(f"{args.build} has no build: enter ninja {args.build.name} first")
        if library.exists() and sdl_license.exists():
            stage_tree(args.folder, args.build / "halo", library,
                       notices("linux") + [(sdl_license, sdl_license.name)], installer=False)
        else:
            stage_tree(args.folder, args.build / "halo", None, notices("linux"), installer=False)
        print(f"{args.build} -> {args.folder}", flush=True)
    elif args.command == "zip":
        write_zip(args.folder, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
