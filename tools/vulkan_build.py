"""The shared Vulkan host renderer: the Linux builds and the Android app
build the same sources (tools/linux_guest_build.py, android_build.py).

The renderer's shared parts (the SPIR-V emitter, the Xbox's texture
decoding) are built beside a host stand-in for platform.h; the window and
command stream are the same on both platforms through SDL3. The Linux
builds' VR mode (HALO_VR) adds its eye passes and composition
(host_vulkan_vr.c) and the stream's VR records (port/linux/host/
host_vr_stream.c).
"""
import hashlib
import os
from pathlib import Path
from typing import List, Optional

from .build_common import (ASTC_CXX_FLAGS, ASTC_DIR, ASTC_HOST_CXX_SOURCES, ASTC_HOST_SOURCES, astc_encoder_sources,
                           download)
from .ninja_syntax import Writer

LINUX_DIR = Path("port/linux")

# The Khronos Vulkan headers, for the hosts' renderer (port/linux/vulkan),
# downloaded by the builds that have it when they are configured, as Vulkan-Headers has them
# (Apache-2.0 OR MIT). The host loads the Vulkan loader itself, so the build
# needs no Vulkan package.
VULKAN_HEADERS_TAG = "vulkan-sdk-1.4.363.0"
VULKAN_HEADERS_URLS = [
    "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/{tag}/include/{name}",
    "https://cdn.jsdelivr.net/gh/KhronosGroup/Vulkan-Headers@{tag}/include/{name}",
]
VULKAN_HEADERS = {
    "vulkan/vk_platform.h": "a2cd9085c66776845d2524c3b0e73ccf4827ba046aa6f676c4b8ddbed47d6552",
    "vulkan/vulkan_core.h": "2f64fd6c7c3f342b0e44f039f1c359b7f0e6cfcb71a7dca1cc5156282faa3b51",
    "vk_video/vulkan_video_codecs_common.h": "d2e7caa396c521d03d491a269572b1d31b925b5127d22fda14199951ebae89f8",
    "vk_video/vulkan_video_codec_h264std.h": "fded484cef9f90fbbd089e0647268f36e5f4292179fbc5428a2c9e8d7709bbe9",
    "vk_video/vulkan_video_codec_h264std_decode.h": "37b970c3d80536ad1ac074cfe19b58ee03c3075378e02179e1dc5e4351266821",
    "vk_video/vulkan_video_codec_h264std_encode.h": "227e092b53c4e7ca1a948ed021704511c1ed16040cd6188ff6703e5ae66db64d",
    "vk_video/vulkan_video_codec_h265std.h": "0b81f8986ada015ef2e127449eff9d9634899bb59a0a9277a4054e9ec4416c12",
    "vk_video/vulkan_video_codec_h265std_decode.h": "879a0dd370a1b1ad184638906c52bdfe7d80d639fbc5a5baa8b02d7c5b60b147",
    "vk_video/vulkan_video_codec_h265std_encode.h": "abb3e72af22e4e0a3dbe5dff7be1b275949388809fb3987830341e8f495ea1c7",
    "vk_video/vulkan_video_codec_av1std.h": "c75c1d324b97d247aef0008024bc7f3adb98c741ab7f66c882ec38fdacc7ee33",
    "vk_video/vulkan_video_codec_av1std_decode.h": "7e3a1ce177c12546d410f3179ce1b81f2da7a8eba4f525b29cd563ab4099c0e5",
    "vk_video/vulkan_video_codec_av1std_encode.h": "8d166b4543260a38347860443b1a59c7a5e86cdb0d0facaf4c704f667de030e3",
    "vk_video/vulkan_video_codec_vp9std.h": "0a47125865376a3fe7014b69ff6db9d04e30ebf8c4d15664f1d894649ad5c09d",
    "vk_video/vulkan_video_codec_vp9std_decode.h": "1ceb1a8d0e3370e508cf688a6e57dc314cd82186b60f3cef420ea4b1b483865d",
}
VULKAN_DIR = Path("port/linux/vulkan")
# the renderer's parts the hosts build from port/linux/src (the SPIR-V
# emitter, and the Xbox's texture decoding, VULKAN_DECODE_SOURCES), beside a
# stand-in for the guest's platform.h (port/linux/vulkan/
# xgpu_vulkan_platform.h), as the macOS build's host does
# (tools/macos_build.py, SHARED_SOURCES)
VULKAN_SHARED_SOURCES = ["xgpu.h", "xgpu_backend.h", "loading_presentation.h", "loading_art.h", "loading_art_tables.h", "loading_ring.h", "loading_scene.h", "xgpu_shader.h", "xgpu_spirv.h", "xgpu_spirv.c", "xgpu_hdr.h",
                         "xgpu_dynamic_lights_world.h", "xgpu_dynamic_lights_world.c", "vr.h", "vr_host.h", "vr_visor.h"]
VULKAN_DECODE_SOURCES = ["xbox_texture_decode.c"]
# the VR mode's: the stream's VR records and late latch (host_vr_stream.h),
# built beside the same stand-ins, and a stand-in for the guest's settings
# (xgpu_vulkan_port_config.h), which they read
VR_STREAM_SOURCES = [Path("port/linux/host/host_vr_stream.c")]

def fetch_vulkan_headers(vulkan_dir: Path) -> None:
    """Download the Vulkan headers into vulkan_dir (configure time, once; a
    header whose SHA-256 is wrong is downloaded again)."""
    for name, digest in VULKAN_HEADERS.items():
        header = vulkan_dir / name
        if header.is_file() and hashlib.sha256(header.read_bytes()).hexdigest() == digest:
            continue
        print(f"Downloading Vulkan-Headers' {name} ({VULKAN_HEADERS_TAG})")
        header.parent.mkdir(parents=True, exist_ok=True)
        data = download([url.format(tag=VULKAN_HEADERS_TAG, name=name) for url in VULKAN_HEADERS_URLS], digest)
        header.with_suffix(".tmp").write_bytes(data)
        header.with_suffix(".tmp").replace(header)


def host_vulkan_renderer(n: Writer, prefix: str, label: str, build: Path, host_cc: str,
                         host_cflags: str, vulkan_headers: Path, vr: bool = False,
                         texture_decoding: bool = True, astc_cxx: Optional[str] = None) -> List[Path]:
    """Build the Vulkan host, returning objects: with vr, the VR mode's eye
    passes, composition and stream records (VR_STREAM_SOURCES); with
    texture_decoding (every host's), the Xbox's texture decoding
    (VULKAN_DECODE_SOURCES); with astc_cxx (a rule compiling C++: the
    Android app's), the DXT textures as ASTC on a device without BC
    (HALO_ASTC: port/linux/astc, with astc-encoder)."""
    # the Vulkan renderer (port/linux/vulkan), with the renderer's shared
    # parts copied beside a stand-in for the guest's platform.h, which
    # their quoted includes would otherwise find in port/linux/src; the
    # XDK's enumerations and texture format words for the stand-in
    host_obj_dir = build / "host" / "obj"
    host_objects: List[Path] = []
    shared_dir = build / "host" / "shared"
    xdk = Path("port/include/xdk")
    if os.name == "nt":
        # (no POSIX shell: tools/guest_build_steps.py's steps, the 64-bit
        # Windows build's)
        steps = "$python tools/guest_build_steps.py"
        n.rule(name=f"{prefix}_vulkan_copy", command=f"{steps} copy $in $out", description=f"{label} SHARED $out")
        n.rule(name=f"{prefix}_vulkan_xdk_enums",
               command=f"{steps} xdk-enums $out {xdk / 'xdk_pdb.h'} {xdk / 'xdk_d3d8.h'}",
               description=f"{label} SHARED $out")
    else:
        n.rule(name=f"{prefix}_vulkan_copy", command="cp $in $out", description=f"{label} SHARED $out")
        n.rule(name=f"{prefix}_vulkan_xdk_enums",
               command=(f"(sed -n '/^\\/\\* ---------- enumerations/,/^\\/\\* ---------- structures and unions/p' "
                        f"{xdk / 'xdk_pdb.h'} | sed '$$d' && "
                        f"grep -E '^#define D3D(FORMAT|SIZE|TEXTURE)_' {xdk / 'xdk_d3d8.h'}) > $out"),
               description=f"{label} SHARED $out")
    shared_copies: List[Path] = []
    shared_names = VULKAN_SHARED_SOURCES + (VULKAN_DECODE_SOURCES if texture_decoding else [])
    for name in shared_names:
        n.build(outputs=shared_dir / name, rule=f"{prefix}_vulkan_copy", inputs=LINUX_DIR / "src" / name)
        shared_copies.append(shared_dir / name)
    n.build(outputs=shared_dir / "platform.h", rule=f"{prefix}_vulkan_copy",
            inputs=VULKAN_DIR / "xgpu_vulkan_platform.h")
    if vr:
        n.build(outputs=shared_dir / "port_config.h", rule=f"{prefix}_vulkan_copy",
                inputs=VULKAN_DIR / "xgpu_vulkan_port_config.h")
        shared_copies.append(shared_dir / "port_config.h")
    n.build(outputs=shared_dir / "xdk_enums.h", rule=f"{prefix}_vulkan_xdk_enums",
            inputs=[xdk / "xdk_pdb.h", xdk / "xdk_d3d8.h"])
    shared_copies += [shared_dir / "platform.h", shared_dir / "xdk_enums.h"]
    vulkan_cflags = f"-I{shared_dir} -I{VULKAN_DIR} -I{vulkan_headers} {host_cflags}"
    if astc_cxx:
        vulkan_cflags += f" -DHALO_ASTC=1 -I{ASTC_DIR}"
    vulkan_sources = [shared_dir / name for name in shared_names if name.endswith(".c")]
    vr_sources = {"host_vulkan_vr.c", "xgpu_vulkan_vr_shaders.c"}
    vulkan_sources += sorted(source for source in VULKAN_DIR.glob("*.c")
                             if not source.name.startswith("guest_") and (vr or source.name not in vr_sources))
    if vr:
        vulkan_sources += VR_STREAM_SOURCES
    for source in vulkan_sources:
        obj = host_obj_dir / "vulkan" / (source.name + ".o")
        n.build(outputs=obj, rule=host_cc, inputs=source, order_only=shared_copies,
                variables={"cflags": vulkan_cflags})
        host_objects.append(obj)
    if astc_cxx:
        astc_cxx_flags = " ".join(["-g", "-fPIC", *ASTC_CXX_FLAGS])
        for source in ASTC_HOST_SOURCES:
            obj = host_obj_dir / "astc" / (source.name + ".o")
            n.build(outputs=obj, rule=host_cc, inputs=source, variables={"cflags": host_cflags})
            host_objects.append(obj)
        for source in ASTC_HOST_CXX_SOURCES:
            obj = host_obj_dir / "astc" / (source.name + ".o")
            n.build(outputs=obj, rule=astc_cxx, inputs=source,
                    variables={"cflags": f"{astc_cxx_flags} -Wall -I{ASTC_DIR}"})
            host_objects.append(obj)
        for source in astc_encoder_sources():
            obj = host_obj_dir / "astc" / (source.name + ".o")
            n.build(outputs=obj, rule=astc_cxx, inputs=source, variables={"cflags": f"{astc_cxx_flags} -w"})
            host_objects.append(obj)
    return host_objects
