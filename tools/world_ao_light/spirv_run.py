"""A SPIR-V fragment shader run on the CPU, for the subset the emitter writes (port/linux/src/xgpu_spirv.c):
tools/test_world_ao_light.py runs display.ambient_occlusion "world"'s shaders this way and compares what they find
with port/linux/game/world_ao_shading.c's reference. Floats are rounded to 32 bits after each operation, as a GPU's;
the structured control flow is followed by its branches (the merges are only declarations)."""
import math
import struct

GLSL = {4: "FAbs", 6: "FSign", 8: "Floor", 30: "Log2", 73: "FindILsb", 17: "Acos", 18: "Atan", 25: "Atan2", 26: "Pow", 27: "Exp", 31: "Sqrt",
        32: "InverseSqrt", 37: "FMin", 40: "FMax", 43: "FClamp", 45: "SClamp", 46: "FMix", 49: "SmoothStep",
        66: "Length", 68: "Cross", 69: "Normalize"}


def f32(x):
    if isinstance(x, bool) or not isinstance(x, float):
        return x
    if math.isnan(x) or math.isinf(x):
        return x
    if abs(x) > 3.4028234663852886e38:
        return math.copysign(math.inf, x)
    return struct.unpack("f", struct.pack("f", x))[0]


def _map(function, *values):
    if isinstance(values[0], list):
        return [_map(function, *parts) for parts in zip(*(v if isinstance(v, list) else [v] * len(values[0])
                                                          for v in values))]
    return function(*values)


def _div(a, b):
    if b == 0.0:
        if a == 0.0 or math.isnan(a):
            return math.nan
        return math.copysign(math.inf, a) * math.copysign(1.0, b)
    return a / b


def _sqrt(x):
    return math.nan if x < 0.0 or math.isnan(x) else math.sqrt(x) if x != math.inf else math.inf


def _acos(x):
    return math.acos(x) if -1.0 <= x <= 1.0 else math.nan


def _atan(x):
    return math.nan if math.isnan(x) else math.atan(x)


def _fmax(a, b):
    return b if math.isnan(a) else a if math.isnan(b) else max(a, b)


def _fmin(a, b):
    return b if math.isnan(a) else a if math.isnan(b) else min(a, b)


def _mul(a, b):
    try:
        return a * b
    except OverflowError:
        return math.inf


class Module:
    def __init__(self, words):
        assert words[0] == 0x07230203
        self.instructions = []
        at = 5
        while at < len(words):
            count, opcode = words[at] >> 16, words[at] & 0xffff
            self.instructions.append((opcode, words[at + 1:at + count]))
            at += count
        self.types, self.constants, self.names, self.variables = {}, {}, {}, {}
        self.code = []
        for opcode, operands in self.instructions:
            if opcode == 5:  # OpName
                self.names[operands[0]] = self._string(operands[1:])
            elif opcode in (19, 20, 21, 22, 23, 25, 27, 28, 30, 32, 33):
                self.types[operands[0]] = (opcode, operands[1:])
            elif opcode == 43:  # OpConstant
                kind = self.types[operands[0]]
                bits = operands[2]
                self.constants[operands[1]] = (struct.unpack("f", struct.pack("I", bits))[0] if kind[0] == 22 else
                                               (bits - (1 << 32) if kind[1][1] and bits >> 31 else bits))
            elif opcode == 44:  # OpConstantComposite
                self.constants[operands[1]] = [self.constants[part] for part in operands[2:]]
            elif opcode in (41, 42):
                self.constants[operands[1]] = opcode == 41
            elif opcode == 59 and operands[2] != 7:  # a global OpVariable
                self.variables[operands[1]] = (operands[0], operands[2])
            if opcode not in (5, 71, 72) and (opcode == 54 or self.code):
                self.code.append((opcode, operands))
        for array, (opcode, operands) in self.types.items():
            if opcode == 28:
                self.types[array] = (28, (operands[0], self.constants[operands[1]]))

    @staticmethod
    def _string(words):
        data = b"".join(struct.pack("I", word) for word in words)
        return data.split(b"\0", 1)[0].decode()

    def zero(self, type_id):
        opcode, operands = self.types[type_id]
        if opcode == 22:
            return 0.0
        if opcode == 21:
            return 0
        if opcode == 20:
            return False
        if opcode == 23:
            return [self.zero(operands[0]) for _ in range(operands[1])]
        if opcode == 28:
            return [self.zero(operands[0]) for _ in range(operands[1])]
        if opcode == 30:
            return [self.zero(member) for member in operands]
        raise ValueError(f"no zero of type {opcode}")

    def variable(self, name):
        for id, variable_name in self.names.items():
            if variable_name == name and id in self.variables:
                return id
        return None


def run(module, inputs, sample):
    """the fragment's output (fragment_color): inputs maps the variables' names to their data (a missing member or
    element reads as 0); sample(image variable's name, coordinate) is a texel"""
    values = dict(module.constants)
    memory = {}
    names = {}
    for id, (pointer_type, storage) in module.variables.items():
        name = module.names.get(id, "")
        names[id] = name
        memory[id] = inputs.get(name)
    labels = {}
    code = module.code
    for index, (opcode, operands) in enumerate(code):
        if opcode == 248:
            labels[operands[0]] = index
    output = None
    at = 0

    def pointee(pointer):
        root, path = pointer
        data = memory[root]
        for step in path:
            if data is None:
                return None
            if isinstance(data, dict):
                data = data.get(step)
            else:
                data = data[step] if step < len(data) else None
        return data

    while True:
        opcode, operands = code[at]
        at += 1
        if opcode in (54, 248, 246, 247, 56):  # function, label, merges
            continue
        if opcode == 59:  # a function variable
            memory[operands[1]] = None
            values[operands[1]] = (operands[1], [])
            continue
        if opcode == 249:
            at = labels[operands[0]]
            continue
        if opcode == 250:
            at = labels[operands[1] if values[operands[0]] else operands[2]]
            continue
        if opcode == 253:
            return output
        if opcode == 252:
            return None
        if opcode == 62:  # OpStore
            root, path = values[operands[0]] if operands[0] in values else (operands[0], [])
            value = values[operands[1]]
            if names.get(root) == "fragment_color":
                output = value
            else:
                assert not path
                memory[root] = value
            continue
        result_type, result, rest = operands[0], operands[1], operands[2:]
        v = [values.get(part) for part in rest]
        if opcode == 61:  # OpLoad
            pointer = values[rest[0]] if rest[0] in values else (rest[0], [])
            if module.types[result_type][0] in (25, 27):
                values[result] = ("image", names.get(pointer[0], ""))
            else:
                data = pointee(pointer)
                values[result] = module.zero(result_type) if data is None else data
        elif opcode == 65:  # OpAccessChain
            base = values[rest[0]] if rest[0] in values else (rest[0], [])
            values[result] = (base[0], base[1] + [values[part] for part in rest[1:]])
        elif opcode == 12:  # OpExtInst
            name = GLSL[rest[1]]
            a = [values[part] for part in rest[2:]]
            if name == "FAbs":
                r = _map(abs, a[0])
            elif name == "FSign":
                r = _map(lambda x: 1.0 if x > 0.0 else -1.0 if x < 0.0 else 0.0, a[0])
            elif name == "Log2":
                r = _map(lambda x: math.log2(x) if x > 0.0 else (-math.inf if x == 0.0 else math.nan), a[0])
            elif name == "FindILsb":
                r = _map(lambda x: ((x & -x).bit_length() - 1) if x else -1, a[0])
            elif name == "Floor":
                r = _map(lambda x: float(math.floor(x)), a[0])
            elif name == "Acos":
                r = _map(_acos, a[0])
            elif name == "Atan":
                r = _map(_atan, a[0])
            elif name == "Atan2":
                r = _map(math.atan2, a[0], a[1])
            elif name == "Pow":
                r = _map(lambda x, y: math.pow(x, y) if x >= 0 else math.nan, a[0], a[1])
            elif name == "Exp":
                r = _map(math.exp, a[0])
            elif name == "Sqrt":
                r = _map(_sqrt, a[0])
            elif name == "InverseSqrt":
                r = _map(lambda x: _div(1.0, _sqrt(x)), a[0])
            elif name == "FMin":
                r = _map(_fmin, a[0], a[1])
            elif name == "FMax":
                r = _map(_fmax, a[0], a[1])
            elif name in ("FClamp", "SClamp"):
                r = _map(lambda x, low, high: min(max(x, low), high), a[0], a[1], a[2])
            elif name == "FMix":
                r = _map(lambda x, y, t: x * (1.0 - t) + y * t, a[0], a[1], a[2])
            elif name == "SmoothStep":
                def smooth(e0, e1, x):
                    t = min(max(_div(x - e0, e1 - e0), 0.0), 1.0)
                    return t * t * (3.0 - 2.0 * t)
                r = _map(smooth, a[0], a[1], a[2])
            elif name == "Length":
                r = _sqrt(sum(f32(_mul(x, x)) for x in a[0]))
            elif name == "Normalize":
                length = _sqrt(sum(x * x for x in a[0]))
                r = [_div(x, length) for x in a[0]]
            elif name == "Cross":
                x, y = a
                r = [x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]]
            values[result] = _map(f32, r)
        elif opcode == 129:
            values[result] = _map(lambda x, y: f32(x + y), v[0], v[1])
        elif opcode == 131:
            values[result] = _map(lambda x, y: f32(x - y), v[0], v[1])
        elif opcode == 133:
            values[result] = _map(lambda x, y: f32(_mul(x, y)), v[0], v[1])
        elif opcode == 136:
            values[result] = _map(lambda x, y: f32(_div(x, y)), v[0], v[1])
        elif opcode == 127:
            values[result] = _map(lambda x: -x, v[0])
        elif opcode == 142:  # OpVectorTimesScalar
            values[result] = [f32(_mul(x, v[1])) for x in v[0]]
        elif opcode == 148:  # OpDot
            total = 0.0
            for x, y in zip(v[0], v[1]):
                total = f32(total + f32(_mul(x, y)))
            values[result] = total
        elif opcode == 80:  # OpCompositeConstruct
            parts = []
            for part in v:
                parts.extend(part if isinstance(part, list) else [part])
            values[result] = parts
        elif opcode == 81:  # OpCompositeExtract
            data = v[0]
            for index in rest[1:]:
                data = data[index]
            values[result] = data
        elif opcode == 82:  # OpCompositeInsert
            data = list(v[1])
            data[rest[2]] = v[0]
            values[result] = data
        elif opcode == 79:  # OpVectorShuffle
            joined = list(v[0]) + list(v[1])
            values[result] = [joined[index] for index in rest[2:]]
        elif opcode == 77:  # OpVectorExtractDynamic
            values[result] = v[0][v[1]]
        elif opcode == 169:  # OpSelect
            values[result] = (_map(lambda c, x, y: x if c else y, v[0], v[1], v[2]) if isinstance(v[0], list) else
                              (v[1] if v[0] else v[2]))
        elif opcode in (180, 184, 186, 188, 190, 183):
            compare = {180: lambda x, y: x == y, 184: lambda x, y: x < y, 186: lambda x, y: x > y,
                       188: lambda x, y: x <= y, 190: lambda x, y: x >= y,
                       183: lambda x, y: math.isnan(x) or math.isnan(y) or x != y}[opcode]
            values[result] = _map(compare, v[0], v[1])
        elif opcode == 166:
            values[result] = _map(lambda x, y: x or y, v[0], v[1])
        elif opcode == 167:
            values[result] = _map(lambda x, y: x and y, v[0], v[1])
        elif opcode == 168:
            values[result] = _map(lambda x: not x, v[0])
        elif opcode == 110:  # OpConvertFToS
            values[result] = _map(lambda x: int(x) if math.isfinite(x) else 0, v[0])
        elif opcode == 111:
            values[result] = _map(float, v[0])
        elif opcode == 128:
            values[result] = _map(lambda x, y: x + y, v[0], v[1])
        elif opcode == 130:
            values[result] = _map(lambda x, y: x - y, v[0], v[1])
        elif opcode == 132:
            values[result] = _map(lambda x, y: x * y, v[0], v[1])
        elif opcode == 170:
            values[result] = _map(lambda x, y: x == y, v[0], v[1])
        elif opcode == 171:
            values[result] = _map(lambda x, y: x != y, v[0], v[1])
        elif opcode == 177:
            values[result] = _map(lambda x, y: x < y, v[0], v[1])
        elif opcode == 124:  # OpBitcast (floats to uints, uints to floats)
            def cast(x):
                if isinstance(x, float):
                    return struct.unpack("I", struct.pack("f", x))[0] if math.isfinite(x) else 0x7f800000
                return struct.unpack("f", struct.pack("I", x & 0xffffffff))[0]
            values[result] = _map(cast, v[0])
        elif opcode == 199:
            values[result] = _map(lambda x, y: x & y, v[0], v[1])
        elif opcode == 195:  # OpShiftRightArithmetic
            values[result] = _map(lambda x, y: x >> y, v[0], v[1])
        elif opcode == 196:  # OpShiftLeftLogical
            values[result] = _map(lambda x, y: (x << y) & 0xffffffff, v[0], v[1])
        elif opcode == 359:  # OpGroupNonUniformBitwiseOr: a subgroup of one invocation, its own
            values[result] = values[rest[2]]
        elif opcode in (87, 88):  # OpImageSample*Lod
            values[result] = sample(values[rest[0]][1], v[1])
        elif opcode == 100:  # OpImage: the sampled image's image
            values[result] = values[rest[0]]
        elif opcode == 95:  # OpImageFetch: a texel by its integer place
            values[result] = sample(values[rest[0]][1], v[1])
        else:
            raise NotImplementedError(f"opcode {opcode}")

