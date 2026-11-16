"""The renderer's manifest (port/linux/src/xgpu_manifest.h): the C reader
reads what tools/render_manifest.py writes, refuses a pixel key that is not
its hash's, a merge is the same file however its recordings come, the
game's vertex shaders are read from its code, and the check of a map's
enumeration compares draws as a backend compiles them. The committed
manifest holds every draw of the stock maps' shipped records
(port/assets/prepared) as the settings derive it, so that the Mac's and
iOS's shader libraries, made from it, are not behind the records."""
import json
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
XDK = ROOT / "port" / "include" / "xdk"
# the reader's sources, and the headers of the derivation of a draw under the
# settings (xgpu_manifest_derive), beside the hosts' stand-in for the guest's
# platform.h (port/macos/xgpu_metal_platform.h), as tools/macos_build.py
# builds the Mac's shaders' tool
STANDIN_SOURCES = ("xgpu.h", "xgpu_backend.h", "xgpu_hdr.h", "xgpu_hdr.c", "xgpu_stream.h", "xgpu_manifest.h",
                   "xgpu_manifest.c")


def standin(folder):
    """the reader's sources in folder with the stand-in platform.h (and its
    xdk_enums.h, as tools/macos_build.py makes it); the sources to compile"""
    folder.mkdir(parents=True, exist_ok=True)
    for name in STANDIN_SOURCES:
        shutil.copy(SOURCE / name, folder / name)
    shutil.copy(ROOT / "port" / "macos" / "xgpu_metal_platform.h", folder / "platform.h")
    pdb = (XDK / "xdk_pdb.h").read_text(encoding="latin-1").splitlines()
    start = next(i for i, line in enumerate(pdb) if line.startswith("/* ---------- enumerations"))
    end = next(i for i, line in enumerate(pdb) if line.startswith("/* ---------- structures and unions"))
    defines = [line for line in (XDK / "xdk_d3d8.h").read_text(encoding="latin-1").splitlines()
               if re.match(r"#define D3D(FORMAT|SIZE|TEXTURE)_", line)]
    (folder / "xdk_enums.h").write_text("\n".join(pdb[start:end] + defines) + "\n", encoding="latin-1")
    return [folder / "xgpu_manifest.c", folder / "xgpu_hdr.c"]


def compile_reader(folder, source, binary):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    sources = standin(folder / "standin")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                    "-DXGPU_MANIFEST_READER_ONLY", f"-I{folder / 'standin'}", str(source), *map(str, sources), "-o",
                    str(binary), "-lm"], check=True)
    return binary

READER = r"""
#include "xgpu_manifest.h"
#include <stdio.h>
int main(int argc, char **argv)
{
	struct xgpu_manifest manifest;
	char error[256];
	uint32_t index;

	(void)argc;
	if (!xgpu_manifest_read(argv[1], &manifest, error, sizeof(error)))
	{
		printf("error: %s\n", error);
		return 1;
	}
	printf("programs %u draws %u\n", manifest.vertex_program_count, manifest.draw_count);
	for (index = 0; index < manifest.vertex_shader_count; index++)
	{
		const struct xgpu_manifest_draw *shader = &manifest.vertex_shaders[index];

		printf("shader %08x%08x: program %s, packed %#x, elements %u\n", shader->key.vertex_program[1],
			shader->key.vertex_program[0], xgpu_manifest_vertex_program(&manifest, shader->key.vertex_program[0] |
			((uint64_t)shader->key.vertex_program[1] << 32)) ? "found" : "missing",
			shader->key.packed_attributes, shader->element_count);
	}
	for (index = 0; index < manifest.draw_count; index++)
	{
		const struct xgpu_manifest_draw *draw = &manifest.draws[index];
		const struct xgpu_manifest_vertex_program *program = xgpu_manifest_vertex_program(&manifest,
			draw->key.vertex_program[0] | ((uint64_t)draw->key.vertex_program[1] << 32));

		printf("draw %u: program %s, blend %u/%u/%u, samples %u, texture modes %#x, combiner 53 %#x, "
			"elements %u, stride %u, immediate %#x, stage 0 %u.%u border %#x\n", index,
			program ? "found" : "missing", draw->key.blend, draw->key.blend_source, draw->key.blend_destination,
			draw->key.samples, draw->key.pixel.texture_modes, draw->key.pixel.combiner_state[53], draw->element_count,
			draw->strides[0], draw->immediate_written, draw->stages[0].source, draw->stages[0].sampler_type,
			draw->stages[0].border_color);
	}
	xgpu_manifest_free(&manifest);
	return 0;
}
"""


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def pixel_key(combiner, texture_modes, samplers, fog):
    """an xgpu_stream_pixel_key's name and its record (its 264 bytes: three of
    pad[] and three of the structure's own padding after them)"""
    states = [0] * 57
    for index, value in combiner.items():
        states[index] = value
    data = struct.pack("<57I", *states) + struct.pack("<I", texture_modes) + bytes(samplers) + bytes(4) + bytes(4)
    data += struct.pack("<I", 0) + bytes([fog, 0, 0, 0, 0, 0, 0, 0, 0, 0]) + bytes(6)
    assert len(data) == 264
    name = f"{fnv(data):016x}"
    cs = ",".join(f"{index}:{value:#x}" for index, value in sorted(combiner.items()))
    fields = (f"tm={texture_modes:#x} sa={','.join(map(str, samplers))} ak=0,0,0,0 sg=0,0,0,0 at=0 fe={fog} ft=0 "
              "cn=0 ca=0 pt=0 pl=0 as=0 mv=0 sb=0 dl=0")
    return name, f"p {name} cs={cs} {fields}"


def manifest_text():
    pixel, pixel_line = pixel_key({0: 0x18200000, 53: 0x11001}, 0x1, [1, 0, 0, 0], 1)
    program = "00000000000000ab"
    words = "00000000" + "0020061b" + "0836106c" + "2070f800"
    draws = [
        f"d vp={program} pa=0x0 im=0 li=0 vw=0 bl=1,4,5,0 cm=15 dt=1,4,1 st=0,0,0,0,0 sm=0,0 cu=1,0,0 fi=0 db=0 "
        f"fm=1,2,1 px={pixel} l=0.0.3.12.0/3.0.5.4.12 w=0:16 t=1.1.3.3.0.0xff102030/0.0.0.0.0.0x0/0.0.0.0.0.0x0/"
        "0.0.0.0.0.0x0",
        f"d vp={program} pa=0x0 im=1 li=0 vw=0 bl=0,0,0,0 cm=15 dt=0,0,0 st=0,0,0,0,0 sm=0,0 cu=0,0,0 fi=0 db=0 "
        f"fm=1,0,4 px={pixel} i=0x9 t=0.0.0.0.0.0x0/0.0.0.0.0.0x0/0.0.0.0.0.0x0/0.0.0.0.0.0x0",
    ]
    return pixel_line, f"v {program} 1 {words}", draws


@pytest.fixture(scope="module")
def reader(tmp_path_factory):
    folder = tmp_path_factory.mktemp("reader")
    (folder / "reader.c").write_text(READER)
    return compile_reader(folder, folder / "reader.c", folder / "reader")


def merge(*arguments):
    subprocess.run([sys.executable, str(ROOT / "tools" / "render_manifest.py"), "merge", *map(str, arguments)],
                   check=True, capture_output=True)


def test_read(tmp_path, reader):
    pixel, program, draws = manifest_text()
    recording = tmp_path / "recording.txt"
    recording.write_text("manifest 2\n" + "\n".join([program, pixel, *draws]) + "\n")
    manifest = tmp_path / "manifest.txt"
    merge("--replace", manifest, recording)
    result = subprocess.run([str(reader), str(manifest)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    lines = result.stdout.splitlines()
    assert lines[0] == "programs 1 draws 2"
    # (sorted: im=0 before im=1)
    assert ("program found, blend 1/4/5, samples 1, texture modes 0x1, combiner 53 0x11001, elements 2, stride 16, "
            "immediate 0, stage 0 1.1 border 0xff102030") in lines[1]
    assert "samples 4" in lines[2] and "elements 0" in lines[2] and "immediate 0x9" in lines[2]


def test_refuses_a_wrong_pixel_key(tmp_path, reader):
    pixel, program, draws = manifest_text()
    manifest = tmp_path / "manifest.txt"
    manifest.write_text("manifest 2\n" + "\n".join([program, pixel.replace("fe=1", "fe=0"), *draws]) + "\n")
    result = subprocess.run([str(reader), str(manifest)], capture_output=True, text=True)
    assert result.returncode == 1 and "its hash's" in result.stdout


def test_merge_is_the_same_file(tmp_path):
    pixel, program, draws = manifest_text()
    first, second = tmp_path / "first.txt", tmp_path / "second.txt"
    first.write_text("manifest 2\n" + "\n".join([program, pixel, draws[1]]) + "\n")
    second.write_text("manifest 2\n" + "\n".join([pixel, program, draws[0], draws[1]]) + "\n")
    one, other = tmp_path / "one.txt", tmp_path / "other.txt"
    merge("--replace", one, first, second)
    merge("--replace", other, second)
    merge(other, first)
    assert one.read_text() == other.read_text()


def test_vertex_shaders_from_the_code(tmp_path, reader):
    """the 67 programs of rasterizer_xbox_vertex_shaders.c, each with its
    declaration's layout (the first program a structure's: its normal,
    binormal and tangent packed, as recorded in play)"""
    manifest = tmp_path / "vertex.txt"
    subprocess.run([sys.executable, str(ROOT / "tools" / "render_manifest.py"), "vertex", str(manifest)], check=True,
                   capture_output=True)
    result = subprocess.run([str(reader), str(manifest)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    lines = result.stdout.splitlines()
    assert lines[0] == "programs 67 draws 0"
    shaders = [line for line in lines if line.startswith("shader ")]
    assert len(shaders) == 67 and all("program found" in line for line in shaders)
    assert "shader 0040b0e2af81b3dc: program found, packed 0xe, elements 7" in shaders
ENUMERATED = ("d vp=0000000000000001 pa=0x0 im=0 li=0 vw=0 bl=1,4,5,0 cm=7 dt=1,3,1 st=0,0,0,0,0 sm=0,0 "
              "cu=1,0,0 fi=0 db=0 fm=1,2,1 px=00000000000000aa l=0.0.3.12.0 w=0:12 "
              "t=1.1.0.0.0.0x0/1.1.3.0.2.0x5050505/0.0.0.0.0.0x0/0.0.0.0.0.0x0")


def recording(tmp_path, *after):
    """a recording: its enumeration (ENUMERATED), then each draw after it
    marked missed"""
    lines = ["manifest 2", "# enumerating: the map's", ENUMERATED, "# prepared: the misses"]
    for draw in after:
        lines += ["# missed: 70000010 70000020", draw]
    path = tmp_path / "recording.txt"
    path.write_text("\n".join(lines) + "\n")
    return path


def check(*arguments):
    return subprocess.run([sys.executable, str(ROOT / "tools" / "render_manifest.py"), "check", *map(str, arguments)],
                          capture_output=True, text=True)


def test_compiled_leaves_out_what_each_draw_sets():
    sys.path.insert(0, str(ROOT / "tools"))
    import render_manifest

    stenciled = ENUMERATED.replace("st=0,0,0,0,0", "st=1,2,0,0,0").replace("dt=1,3,1", "dt=0,0,0")
    assert render_manifest.compiled(stenciled) == render_manifest.compiled(ENUMERATED)
    # (a stage's border drawn by the shader: clamped to a colour the sampler has not)
    assert "t=1.1.0/1.1.1/0.0.0/0.0.0" in render_manifest.compiled(ENUMERATED)
    unblended = ENUMERATED.replace("bl=1,4,5,0", "bl=0,4,5,0")
    assert " bl=0 " in render_manifest.compiled(unblended)
    assert render_manifest.compiled(unblended) == render_manifest.compiled(unblended.replace("bl=0,4,5,0", "bl=0,1,1,0"))


def test_check_passes_what_only_the_draw_state_changes(tmp_path):
    result = check(recording(tmp_path, ENUMERATED.replace("cu=1,0,0", "cu=0,0,0")))
    assert result.returncode == 0, result.stdout
    assert "1 draws enumerated" in result.stdout and "0 missed" in result.stdout


def test_check_fails_another_program(tmp_path):
    result = check("--verbose", "1", recording(tmp_path, ENUMERATED.replace("px=00000000000000aa", "px=00000000000000bb")))
    assert result.returncode == 1
    assert "1 missed" in result.stdout and "differs in px" in result.stdout
    assert check("--exact", recording(tmp_path, ENUMERATED.replace("cu=1,0,0", "cu=0,0,0"))).returncode == 1


def test_check_finds_miss_without_stack_marker(tmp_path):
    path = recording(tmp_path, ENUMERATED.replace("st=0,0,0,0,0", "st=1,2,0,0,0"))
    path.write_text(path.read_text().replace("# missed: 70000010 70000020\n", ""))
    assert check("--exact", path).returncode == 1
    assert check(path).returncode == 0


def test_missed_marker_without_platform_stack_walker(tmp_path):
    from test_load_trace import build

    source = (SOURCE / "xgpu_manifest.c").read_text()
    function = source[source.index("static void missed_write(void)"):source.index("static void enumeration_add(")]
    source = '''
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#undef __x86_64__
#undef __i386__
static struct { FILE *file; } recorder;
''' + function + '''
int main(void) {
    char line[64];
    recorder.file = tmpfile();
    assert(recorder.file);
    missed_write();
    rewind(recorder.file);
    assert(fgets(line, sizeof(line), recorder.file));
    assert(!strcmp(line, "# missed:\\n"));
    fclose(recorder.file);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


# ---------- the committed manifest and the shipped records

DERIVER = r"""
#include "xgpu_manifest.h"
#include <stdio.h>
#include <string.h>
/* each draw of a manifest as the settings the stock maps' records are made
with derive it (tools/prepared_records.py: every setting's default, the
game's own first start: no multisampling, no HDR, the enhanced dynamic
lights, which light each pixel, no ambient occlusion), its hash as a
record's (render_prepared.c's prepared_draws) */
int main(int argc, char **argv)
{
	struct xgpu_manifest manifest;
	struct xgpu_manifest_settings settings;
	char error[256];
	uint32_t index;

	(void)argc;
	if (!xgpu_manifest_read(argv[1], &manifest, error, sizeof(error)))
	{
		printf("error: %s\n", error);
		return 1;
	}
	memset(&settings, 0, sizeof(settings));
	settings.samples = 1;
	settings.per_pixel_lighting = 1;
	settings.enhanced_lights = 1;
	for (index = 0; index < manifest.draw_count; index++)
	{
		struct xgpu_manifest_draw draw;

		xgpu_manifest_derive(&manifest.draws[index], &manifest.contexts[index], &settings, 0, &draw);
		printf("%016llx\n", (unsigned long long)xgpu_manifest_hash(&draw, sizeof(draw), XGPU_MANIFEST_HASH_START));
	}
	xgpu_manifest_free(&manifest);
	return 0;
}
"""

COMMITTED = ROOT / "port" / "assets" / "render" / "manifest.txt"
PREPARED = ROOT / "port" / "assets" / "prepared"
REMAKE = ("run tools/prepared_records.py again (it records the manifest in the same run as the records: see its "
          "header) and commit port/assets/prepared and port/assets/render/manifest.txt")


@pytest.fixture(scope="module")
def deriver(tmp_path_factory):
    folder = tmp_path_factory.mktemp("deriver")
    (folder / "deriver.c").write_text(DERIVER)
    return compile_reader(folder, folder / "deriver.c", folder / "deriver")


def derived_hashes(deriver, manifest):
    result = subprocess.run([str(deriver), str(manifest)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    return {int(line, 16) for line in result.stdout.split()}


def recorded_draws():
    """each shipped record's map and its draws' hashes (render_prepared.c's
    struct prepared_header, then the enumeration's hashes)"""
    manifest = json.loads((PREPARED / "manifest.json").read_text())
    pack = (PREPARED / manifest["file"]).read_bytes()
    for record in manifest["records"]:
        data = zlib.decompress(pack[record["offset"]:record["offset"] + record["compressed_size"]])
        header_size, = struct.unpack_from("<I", data, 8)
        draw_count, = struct.unpack_from("<I", data, 28)
        yield record["map"], set(struct.unpack_from(f"<{draw_count}Q", data, header_size))


def records_missing(hashes):
    return {name: len(draws - hashes) for name, draws in recorded_draws() if draws - hashes}


def test_manifest_holds_every_shipped_records_draw(deriver):
    """every draw of every stock map's shipped record is one of the committed
    manifest's (as the records' settings derive it): the Mac's and iOS's
    shader libraries, made from the manifest under every setting, have each
    shader the stock maps draw with"""
    missing = records_missing(derived_hashes(deriver, COMMITTED))
    assert not missing, (f"draws of the shipped records that port/assets/render/manifest.txt does not have, by map: "
                         f"{missing}: the Mac's and iOS's shader libraries would compile them as the maps load. {REMAKE}.")


def test_negative_a_draw_without_its_context(deriver, tmp_path):
    """the manifest's draws as no setting changes them, their contexts lost:
    the records' draws lit for each pixel by the enhanced lights are not
    derived, and are seen missing"""
    text = COMMITTED.read_text()
    stripped = tmp_path / "manifest.txt"
    stripped.write_text(re.sub(r" c[ltodhk]=[0-9,]+", "", text))
    assert re.search(r" cl=[12],", text), "no draw of the committed manifest is lit by its vertex program"
    assert records_missing(derived_hashes(deriver, stripped))


def test_settings_have_every_multisampling():
    """xgpu_manifest_settings' samples are display.anti_aliasing's
    (d3d8_device.c's anti_aliasing_values), so that a draw is derived for each"""
    device = (SOURCE / "d3d8_device.c").read_text(encoding="latin-1")
    table = device[device.index("anti_aliasing_values[] ="):]
    table = table[:table.index("};")]
    samples = {int(value) for value in re.findall(r"_anti_aliasing_\w+, (\d+), [0-9.]+f \}", table)} - {0}
    manifest = (SOURCE / "xgpu_manifest.c").read_text()
    listed = manifest[manifest.index("static const int settings_samples[] = {"):]
    listed = {int(value) for value in re.findall(r"\d+", listed[:listed.index("}")])}
    assert samples and samples <= listed and 1 in listed, (samples, listed)


WORLD_DERIVER = r"""
#include "xgpu_manifest.h"
#include "xgpu.h"
#include <stdio.h>
#include <string.h>

/* the committed manifest's draws under display.ambient_occlusion "world"
(the records' other settings): how many take the world's light, with
their contexts and with every context's world cleared */
int main(int argc, char **argv)
{
	struct xgpu_manifest manifest;
	struct xgpu_manifest_settings settings;
	char error[256];
	uint32_t index, world = 0, cleared = 0;

	(void)argc;
	if (!xgpu_manifest_read(argv[1], &manifest, error, sizeof(error)))
	{
		printf("error: %s\n", error);
		return 1;
	}
	memset(&settings, 0, sizeof(settings));
	settings.samples = 1;
	settings.per_pixel_lighting = 1;
	settings.enhanced_lights = 1;
	settings.ambient_occlusion = XGPU_MANIFEST_OCCLUSION_WORLD;
	for (index = 0; index < manifest.draw_count; index++)
	{
		struct xgpu_manifest_draw draw;
		struct xgpu_manifest_context context = manifest.contexts[index];

		xgpu_manifest_derive(&manifest.draws[index], &context, &settings, 0, &draw);
		world += (draw.key.pixel.ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD) != 0;
		context.world = 0;
		xgpu_manifest_derive(&manifest.draws[index], &context, &settings, 0, &draw);
		cleared += (draw.key.pixel.ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD) != 0;
	}
	printf("%u %u\n", world, cleared);
	xgpu_manifest_free(&manifest);
	return 0;
}
"""


def test_manifest_holds_the_worlds_draws(tmp_path):
    """the committed manifest notes which draws take display.ambient_occlusion
    "world"'s light (its window's opaque surfaces, noted whatever the setting
    it was recorded with), so that the Mac's and iOS's libraries, made under
    every setting, have the world's shaders; none without that context"""
    (tmp_path / "world.c").write_text(WORLD_DERIVER)
    binary = compile_reader(tmp_path, tmp_path / "world.c", tmp_path / "world")
    result = subprocess.run([str(binary), str(COMMITTED)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    world, cleared = map(int, result.stdout.split())
    assert world > 0, f"no draw of the committed manifest takes the world's light. {REMAKE}."
    assert cleared == 0


WORLD_FORMS = r"""
#include "xgpu_manifest.h"
#include "xgpu.h"
#include <stdio.h>
#include <string.h>

/* the committed manifest's draws under display.ambient_occlusion "world" in
every other setting's combination, each in its World AO forms as the Mac's
library is built (xgpu_manifest_world_ao_forms): how many read the
occlusion from the pass, are a twin writing its pixel's inputs, and are an
occluder's twin (of a found window's draw without the world's light: the
first-person weapon, a surface lighting itself); with argv[2] every
context's found cleared */
int main(int argc, char **argv)
{
	struct xgpu_manifest manifest;
	struct xgpu_manifest_settings settings;
	char error[256];
	uint32_t index, sampled = 0, inputs = 0, occluders = 0;
	int combination;

	if (!xgpu_manifest_read(argv[1], &manifest, error, sizeof(error)))
	{
		printf("error: %s\n", error);
		return 1;
	}
	for (combination = 0; xgpu_manifest_settings(combination, &settings); combination++)
	{
		if (settings.ambient_occlusion != XGPU_MANIFEST_OCCLUSION_WORLD)
			continue;
		for (index = 0; index < manifest.draw_count; index++)
		{
			struct xgpu_manifest_draw derived, forms[XGPU_MANIFEST_WORLD_AO_FORMS];
			struct xgpu_manifest_context context = manifest.contexts[index];
			int form, count;

			if (argc > 2)
				context.found = 0;
			xgpu_manifest_derive(&manifest.draws[index], &context, &settings, 0, &derived);
			count = xgpu_manifest_world_ao_forms(&derived, &context, &settings, forms);
			for (form = 1; form < count; form++)
			{
				uint8_t occlusion = forms[form].key.pixel.ambient_occlusion;

				sampled += (occlusion & 0xf0u) == (XGPU_AMBIENT_OCCLUSION_WORLD | XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED);
				inputs += (occlusion & 0xf0u) == XGPU_STREAM_WORLD_AO_INPUTS;
				occluders += occlusion == XGPU_STREAM_WORLD_AO_OCCLUDER;
			}
		}
	}
	printf("%u %u %u\n", sampled, inputs, occluders);
	xgpu_manifest_free(&manifest);
	return 0;
}
"""


def test_library_has_the_world_ao_forms(tmp_path):
    """the Mac's and iOS's libraries are built with each World AO form a map's
    preparation makes (xgpu_metal_prebuild.c, xgpu_manifest_world_ao_forms):
    besides the inline draw, where the committed manifest notes a draw's window
    found once a pixel (cf), the draw reading the pass, its twin writing the
    pre-pass's inputs, and the occluders' twins (the first-person weapon's,
    a self-illuminated surface's), so that none is compiled as a map
    prepares. Negative control: without cf, none of those is derived"""
    (tmp_path / "forms.c").write_text(WORLD_FORMS)
    binary = compile_reader(tmp_path, tmp_path / "forms.c", tmp_path / "forms")

    def counts(*extra):
        result = subprocess.run([str(binary), str(COMMITTED), *extra], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout
        return tuple(map(int, result.stdout.split()))

    sampled, inputs, occluders = counts()
    assert sampled > 0 and inputs > 0 and occluders > 0, (
        f"World AO forms derived from the committed manifest: {sampled} sampled, {inputs} input twins, "
        f"{occluders} occluder twins. {REMAKE}.")
    assert counts("without-found") == (0, 0, 0)
