"""spirv_check.py <scene.json> <model.spv> <lightmap.spv> <tolerance>: the SPIR-V emitter's display.ambient_occlusion
"world" shaders, as debug.world_ao_view writes them (the pixel's colour is the occlusion), run on the CPU
(spirv_run.py) at the made-up scene's points (scene.c) and compared with port/linux/game/world_ao_shading.c's
occlusion there. Prints the largest difference; exits 1 if it is past the tolerance."""
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spirv_run  # noqa: E402

U_WORLD_AO = 27  # (xgpu_spirv.c's member: past the other 27)


def main():
    scene = json.loads(Path(sys.argv[1]).read_text())
    modules = [spirv_run.Module(_words(Path(path))) for path in sys.argv[2:4]]
    tolerance = float(sys.argv[4])
    width, height, depth = scene["size"]
    table, atlas, layers = scene["table"], scene["atlas"], scene["layers"]
    origin, voxel, range_ = scene["origin"], scene["voxel"], scene["range"]
    side = 16 * 9  # (the atlas's: XGPU_WORLD_SDF_ATLAS bricks of XGPU_WORLD_SDF_BRICK_SPAN voxels)
    parameters = scene["parameters"]
    proxies = scene["proxies"]
    block = {0: [[origin[0], origin[1], origin[2], 1.0 / voxel], [float(width), float(height), float(depth), float(layers)]],
             1: [parameters[0:4], parameters[4:8]],
             2: [proxies[index:index + 4] for index in range(0, len(proxies), 4)],
             3: [scene["grid"][0:4], scene["grid"][4:8]],
             4: [scene["cells"][index:index + 4] for index in range(0, len(scene["cells"]), 4)]}

    def sample(name, uvw):
        if name == "world_sdf_table":
            # (read by texel: the table's four bytes as RGBA8)
            at = ((uvw[2] * height + uvw[1]) * width + uvw[0]) * 4
            return [spirv_run.f32(table[at + part] / 255.0) for part in range(4)]
        if name == "world_ao_scenery":
            # (the static scenery's pages: each layer one value, scene.c's, in the coverage's format, read as
            # 1, 1, 1, the byte)
            layer = min(max(int(round(uvw[2])), 0), len(scene["pages"]) - 1)
            return [1.0, 1.0, 1.0, spirv_run.f32(scene["pages"][layer])]
        if name == "world_ao_objects":
            # (the vehicles' occlusion fields' atlas: four halves a texel, filtered, clamped to its edge)
            size = scene["objects"]
            base, fraction = [], []
            for axis in range(3):
                coordinate = uvw[axis] * size[axis] - 0.5
                floor_value = math.floor(coordinate)
                base.append(int(floor_value))
                fraction.append(coordinate - floor_value)
            result = [0.0, 0.0, 0.0, 0.0]
            for corner in range(8):
                at, weight = [], 1.0
                for axis in range(3):
                    bit = (corner >> axis) & 1
                    at.append(min(max(base[axis] + bit, 0), size[axis] - 1))
                    weight *= fraction[axis] if bit else 1.0 - fraction[axis]
                texel = ((at[2] * size[1] + at[1]) * size[0] + at[0]) * 4
                for part in range(4):
                    result[part] += weight * scene["objects_texels"][texel + part]
            return [spirv_run.f32(value) for value in result]
        if name != "world_sdf":
            return [1.0, 1.0, 1.0, 1.0]  # (the surface's own texture: white)
        size = (side, side, layers * 9)
        base, fraction = [], []
        for axis in range(3):
            coordinate = uvw[axis] * size[axis] - 0.5
            floor_value = math.floor(coordinate)
            base.append(int(floor_value))
            fraction.append(coordinate - floor_value)
        result = 0.0
        for corner in range(8):
            at, weight = [], 1.0
            for axis in range(3):
                bit = (corner >> axis) & 1
                at.append(min(max(base[axis] + bit, 0), size[axis] - 1))
                weight *= fraction[axis] if bit else 1.0 - fraction[axis]
            result += weight * max(atlas[(at[2] * side + at[1]) * side + at[0]] / 127.0, -1.0)
        return [spirv_run.f32(result), 0.0, 0.0, 1.0]

    worst = 0.0
    for case in scene["cases"]:
        # (and a model's as a two-sided shader's cards: its uniforms' world_ao.w 1)
        for cards in (0, 1) if not case["lightmap"] else (0,):
            inputs = {"u": {U_WORLD_AO: [case["owner"], case["reference"], 0.0, float(cards)]}, "world_ao": block,
                      "xWorldPosition": case["point"], "xWorldNormal": case["normal"] + [1.0]}
            colour = spirv_run.run(modules[case["lightmap"]], inputs, sample)
            difference = abs(colour[0] - case["cards" if cards else "occlusion"])
            if difference > worst:
                worst = difference
                if difference > tolerance:
                    print(f"at {case}{' as cards' if cards else ''}: {colour[0]}")
    print(f"{len(scene['cases'])} points, largest difference {worst:.3g}; {scene['field_shaded']} the field shades")
    return 1 if worst > tolerance else 0


def _words(path):
    data = path.read_bytes()
    return [int.from_bytes(data[at:at + 4], "little") for at in range(0, len(data), 4)]


if __name__ == "__main__":
    sys.exit(main())
