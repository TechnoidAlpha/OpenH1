"""spirv_check.py <directory> <module.spv> <tolerance>: the SPIR-V emitter's ray step through the distance-field
scene (port/linux/src/xgpu_spirv.c's world_trace, in the module xgpu_spirv_world_trace_test writes) run on the CPU
(tools/world_ao_light/spirv_run.py) at the made-up scene's rays (scene.c's rays.json, the texture's bytes in
cascades.bin) and compared with its reference there (port/linux/game/world_cascades.c's world_trace_ray): whether
each hits, what, and how far. Prints the largest difference of t; exits 1 if a ray differs past the tolerance."""
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "world_ao_light"))
import spirv_run  # noqa: E402

SPAN = 65  # (xgpu.h's XGPU_WORLD_CASCADE_SPAN)
CASCADES = 4


def main():
    directory = Path(sys.argv[1])
    data = (directory / "cascades.bin").read_bytes()
    texels = [value - 256 if value > 127 else value for value in data]
    scene = json.loads((directory / "rays.json").read_text())
    words = Path(sys.argv[2]).read_bytes()
    module = spirv_run.Module([int.from_bytes(words[at:at + 4], "little") for at in range(0, len(words), 4)])
    tolerance = float(sys.argv[3])
    size = (SPAN, SPAN, SPAN * CASCADES)

    def sample(name, uvw):
        # (the cascades: two signed bytes a texel, RG8 snorm, filtered, clamped to the edge)
        assert name == "world_cascades", name
        base, fraction = [], []
        for axis in range(3):
            coordinate = uvw[axis] * size[axis] - 0.5
            floor_value = math.floor(coordinate)
            base.append(int(floor_value))
            fraction.append(coordinate - floor_value)
        result = [0.0, 0.0]
        for corner in range(8):
            at, weight = [], 1.0
            for axis in range(3):
                bit = (corner >> axis) & 1
                at.append(min(max(base[axis] + bit, 0), size[axis] - 1))
                weight *= fraction[axis] if bit else 1.0 - fraction[axis]
            texel = ((at[2] * size[1] + at[1]) * size[0] + at[0]) * 2
            for part in range(2):
                result[part] += weight * max(texels[texel + part] / 127.0, -1.0)
        return [spirv_run.f32(result[0]), spirv_run.f32(result[1]), 0.0, 1.0]

    worst, unlike = 0.0, 0
    for index, ray in enumerate(scene["rays"]):
        inputs = {"origin": ray["origin"], "direction": ray["direction"],
                  "parameters": [ray["t_max"], float(ray["mask"]), 0.0, 0.0]}
        for cascade in range(CASCADES):
            inputs[f"cascade{cascade}"] = scene["block"][cascade]
        colour = spirv_run.run(module, inputs, sample)
        difference = abs(colour[1] - ray["t"])
        worst = max(worst, difference)
        if int(colour[0]) != ray["hit"] or int(colour[2]) != ray["kind"] or difference > tolerance:
            unlike += 1
            if unlike <= 10:
                print(f"ray {index}: {colour[:3]}, the reference's {ray['hit']} {ray['kind']} {ray['t']}")
    print(f"{len(scene['rays'])} rays ({sum(ray['hit'] for ray in scene['rays'])} hit), {unlike} differ, "
          f"the largest difference of t {worst:.3g}")
    return 1 if unlike else 0


if __name__ == "__main__":
    sys.exit(main())
