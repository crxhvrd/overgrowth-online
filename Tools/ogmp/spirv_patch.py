"""Small SPIR-V reader/patcher for decorations (docs_gl46_plan.md stage 3.2-3.3).

Reads names, variables, types and Location/Binding/DescriptorSet decorations, and
rewrites decoration values in place (same instruction size). Used to give a
program's stages one location per inter-stage variable (by name), and the
engine's bindings by name.
"""

import struct

OP_NAME, OP_DECORATE, OP_VARIABLE, OP_CONSTANT = 5, 71, 59, 43
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_MATRIX = 21, 22, 23, 24
OP_TYPE_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_TYPE_BOOL = 28, 30, 32, 20
DEC_BUILTIN, DEC_LOCATION, DEC_BINDING, DEC_SET = 11, 30, 33, 34
SC_UNIFORM_CONSTANT, SC_INPUT, SC_UNIFORM, SC_OUTPUT, SC_STORAGE_BUFFER = 0, 1, 2, 3, 12


class Module:
    def __init__(self, path):
        self.path = path
        data = open(path, "rb").read()
        self.words = list(struct.unpack(f"<{len(data) // 4}I", data))
        assert self.words[0] == 0x07230203, "not SPIR-V"
        self.names, self.vars, self.types, self.consts = {}, {}, {}, {}
        self.decorations = {}  # id -> {decoration: word index of its first literal}
        i = 5
        while i < len(self.words):
            op, count = self.words[i] & 0xFFFF, self.words[i] >> 16
            args = self.words[i + 1:i + count]
            if op == OP_NAME:
                self.names[args[0]] = _string(args[1:])
            elif op == OP_DECORATE:
                self.decorations.setdefault(args[0], {})[args[1]] = i + 3
            elif op == OP_VARIABLE:
                self.vars[args[1]] = (args[0], args[2])  # pointer type, storage class
            elif op == OP_CONSTANT:
                self.consts[args[1]] = args[2] if len(args) > 2 else 0
            elif op in (OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_BOOL, OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER):
                self.types[args[0]] = (op, args[1:])
            i += count

    def interface(self, storage):
        """{name: (var id, locations it takes, outer array length or None)} of Input/Output variables
        with a Location (built-ins have none)."""
        out = {}
        for vid, (ptr, sc) in self.vars.items():
            if sc != storage or DEC_LOCATION not in self.decorations.get(vid, {}):
                continue
            pointee = self.types[ptr][1][1]
            out[self.names.get(vid, f"%{vid}")] = (vid, pointee)
        return out

    def locations(self, type_id, strip_outer_array=False):
        op, a = self.types[type_id]
        if op == OP_TYPE_ARRAY:
            n = self.consts.get(a[1], 1)
            if strip_outer_array:
                return self.locations(a[0])
            return n * self.locations(a[0])
        if op == OP_TYPE_MATRIX:
            return a[1]
        if op == OP_TYPE_STRUCT:
            return sum(self.locations(m) for m in a)
        return 1  # (scalars and vectors; no doubles in the engine's shaders)

    def get(self, vid, decoration):
        at = self.decorations.get(vid, {}).get(decoration)
        return None if at is None else self.words[at]

    def set(self, vid, decoration, value):
        at = self.decorations.get(vid, {}).get(decoration)
        if at is None:
            raise KeyError(f"{self.names.get(vid, vid)} has no decoration {decoration}")
        self.words[at] = value

    def save(self, path=None):
        open(path or self.path, "wb").write(struct.pack(f"<{len(self.words)}I", *self.words))


def _string(words):
    raw = b"".join(struct.pack("<I", w) for w in words)
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


PER_VERTEX_INPUTS = {"geom", "tesc", "tese"}  # (their inputs are arrays of vertices)
PER_VERTEX_OUTPUTS = {"tesc"}


def unify_locations(modules):
    """modules: [(stage, Module)] in pipeline order. Every inter-stage variable gets
    one location by name (sorted names, packed), the same in each stage it appears.
    Vertex inputs and fragment outputs keep theirs. Returns {name: location}."""
    sizes = {}
    for stage, m in modules:
        if stage != "vert":
            for name, (vid, t) in m.interface(SC_INPUT).items():
                sizes[name] = max(sizes.get(name, 0), m.locations(t, stage in PER_VERTEX_INPUTS))
        if stage != "frag":
            for name, (vid, t) in m.interface(SC_OUTPUT).items():
                sizes[name] = max(sizes.get(name, 0), m.locations(t, stage in PER_VERTEX_OUTPUTS))
    assigned, loc = {}, 0
    for name in sorted(sizes):
        assigned[name] = loc
        loc += sizes[name]
    for stage, m in modules:
        if stage != "vert":
            for name, (vid, t) in m.interface(SC_INPUT).items():
                m.set(vid, DEC_LOCATION, assigned[name])
        if stage != "frag":
            for name, (vid, t) in m.interface(SC_OUTPUT).items():
                m.set(vid, DEC_LOCATION, assigned[name])
    return assigned


# The engine's bindings (stage 3.3). Uniform blocks in set 0 at the binding points the
# engine binds them to; textures (and texel buffers) in set 1 at their unit, "texN" -> N.
BLOCK_BINDINGS = {"InstanceInfo": 0, "ClusterInfo": 1, "ShadowCascades": 2, "gl_DefaultUniformBlock": 3, "LightProbeInfo": 4}
SET_BLOCKS, SET_TEXTURES = 0, 1


def resource_name(m, vid):
    """A sampler's name, or a block's type name (block variables are often unnamed)."""
    ptr, sc = m.vars[vid]
    if sc in (SC_UNIFORM, SC_STORAGE_BUFFER):
        return m.names.get(m.types[ptr][1][1], "")
    return m.names.get(vid, "")


def assign_bindings(m):
    """-> unknown resource names (left as glslang mapped them)."""
    unknown = []
    for vid, (ptr, sc) in m.vars.items():
        if DEC_BINDING not in m.decorations.get(vid, {}):
            continue
        name = resource_name(m, vid)
        if sc == SC_UNIFORM_CONSTANT:
            if name == "tex":
                unit = 0
            elif name.startswith("tex") and name[3:].isdigit():
                unit = int(name[3:])
            else:
                unknown.append(name)
                continue
            m.set(vid, DEC_BINDING, unit)
            if DEC_SET in m.decorations.get(vid, {}):
                m.set(vid, DEC_SET, SET_TEXTURES)
        elif sc == SC_UNIFORM and name in BLOCK_BINDINGS:
            m.set(vid, DEC_BINDING, BLOCK_BINDINGS[name])
            if DEC_SET in m.decorations.get(vid, {}):
                m.set(vid, DEC_SET, SET_BLOCKS)
        else:
            unknown.append(name)
    return unknown
