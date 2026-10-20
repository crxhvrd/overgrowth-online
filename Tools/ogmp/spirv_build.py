"""Offline Vulkan build of every manifest program (docs_gl46_plan.md stage 3.2).

Each program's preprocessed stage sources (dumped by the game with shader_debug)
are linked with glslang's relaxed Vulkan mode (-R: loose uniforms into one
default block, bindings and locations mapped) into SPIR-V, then checked:
  - spirv-val on every module
  - each stage's outputs meet the next stage's inputs by name at the same location
  - fragment outputs: out_color at 0, out_vel at 1 (the engine binds them by name in GL)
Reflection (glslang -q and spirv-cross --reflect) is kept per program.

    python -I Tools/ogmp/spirv_build.py <dumped sources dir> <program_stages.json> [out dir]
"""

import concurrent.futures
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spirv_patch  # noqa: E402

SDK = os.environ.get("VULKAN_SDK") or "R:/CoreFXProject/VulkanSDK"
GLSLANG = os.path.join(SDK, "Bin", "glslangValidator.exe")
SPIRV_VAL = os.path.join(SDK, "Bin", "spirv-val.exe")
SPIRV_CROSS = os.path.join(SDK, "Bin", "spirv-cross.exe")
ORDER = ["vert", "tesc", "tese", "geom", "frag"]
FRAG_OUTPUTS = {"out_color": 0, "out_vel": 1}
UNIFORM_INIT = __import__("re").compile(r"^\s*uniform\s+\w+\s+\w+\s*(\[[^\]]*\])?\s*=")


def run(args, cwd=None):
    r = subprocess.run(args, capture_output=True, text=True, errors="replace", cwd=cwd)
    return r.returncode, r.stdout + r.stderr


def io_of(spv):
    code, out = run([SPIRV_CROSS, spv, "--reflect"])
    if code != 0:
        return None
    try:
        j = json.loads(out)
    except ValueError:
        return None
    ins = {v["name"]: v.get("location") for v in j.get("inputs", [])}
    outs = {v["name"]: v.get("location") for v in j.get("outputs", [])}
    return ins, outs


def build_one(idx, name, stages, src_dir, out_dir):
    """-> (problems: [(kind, detail)], files)"""
    d = os.path.join(out_dir, f"{idx:04d}")
    os.makedirs(d, exist_ok=True)
    present = [st for st in ORDER if st in stages]
    srcs = [os.path.join(src_dir, stages[st]) for st in present]
    missing = [s for s in srcs if not os.path.exists(s)]
    if missing or "vert" not in present or "frag" not in present:
        return [("no sources", f"{name}: {missing or present}")]
    # A uniform with an initial value: the value is lost in the default uniform block
    # (stage 3.4: post.frag's blur weights were)
    init_problems = []
    for st, src in zip(present, srcs):
        for line in open(src, encoding="utf-8", errors="replace"):
            if UNIFORM_INIT.match(line):
                init_problems.append(("uniform initializer", f"{name} {st}: {line.strip()[:120]}"))
    # Copied to short names: the dumped names are the whole permutation (past MAX_PATH for glslang)
    short = []
    for st, src in zip(present, srcs):
        dst = os.path.join(d, "s." + st)  # (glslang takes the stage from the extension; -S applies to every file)
        with open(src, "rb") as fi, open(dst, "wb") as fo:
            fo.write(fi.read())
        short.append("s." + st)
    code, out = run([GLSLANG, "-R", "-V", "--target-env", "vulkan1.2", "--amb", "--aml", "-l", "-q"] + short, cwd=d)
    open(os.path.join(d, "glslang.txt"), "w", encoding="utf-8").write(name + "\n" + out)
    if code != 0:
        errs = [l.strip() for l in out.splitlines() if l.startswith("ERROR")]
        return [("glslang", f"{name}: {(errs or [out.strip()[-300:]])[0][:300]}")]
    problems = list(init_problems)
    # One location per inter-stage variable, by name, in every stage (glslang maps each
    # stage by its own declaration order)
    try:
        mods = [(st, spirv_patch.Module(os.path.join(d, st + ".spv"))) for st in present]
        spirv_patch.unify_locations(mods)
        for st, m in mods:
            for n in spirv_patch.assign_bindings(m):  # (stage 3.3: the engine's bindings)
                problems.append(("unknown resource", f"{name} {st}: {n}"))
            m.save()
    except (OSError, KeyError, AssertionError) as e:
        problems.append(("patch", f"{name}: {e}"))
    io = {}
    for st in present:
        spv = os.path.join(d, st + ".spv")
        if not os.path.exists(spv):
            problems.append(("no module", f"{name}: {st}"))
            continue
        code, out = run([SPIRV_VAL, "--target-env", "vulkan1.2", spv])
        if code != 0:
            problems.append(("spirv-val", f"{name} {st}: {out.strip().splitlines()[0][:300] if out.strip() else code}"))
        io[st] = io_of(spv)
    # Each stage's outputs against the next stage's inputs
    for a, b in zip(present, present[1:]):
        if io.get(a) is None or io.get(b) is None:
            problems.append(("reflect", f"{name}: {a}->{b}"))
            continue
        outs, ins = io[a][1], io[b][0]
        for var, loc in ins.items():
            if var.startswith("gl_"):
                continue
            if var not in outs:
                problems.append(("io unmatched", f"{name}: {b} input {var} not written by {a}"))
            elif outs[var] != loc:
                problems.append(("io location", f"{name}: {var} {a} {outs[var]} != {b} {loc}"))
    # GLSL 4.60 for GL from the finished modules (stage 3.4: GL runs the Vulkan-shaped shaders)
    for st in present:
        code, out = run([SPIRV_CROSS, "--version", "460", "--no-es", st + ".spv", "--output", st + ".gl"], cwd=d)
        if code != 0:
            problems.append(("spirv-cross", f"{name} {st}: {out.strip()[:200]}"))
    if io.get("frag"):
        for var, loc in io["frag"][1].items():
            want = FRAG_OUTPUTS.get(var)
            if want is not None and loc != want:
                problems.append(("frag output", f"{name}: {var} at {loc}, not {want}"))
    json.dump({"name": name, "stages": present, "io": {k: v for k, v in io.items() if v}}, open(os.path.join(d, "program.json"), "w"), indent=1)
    return problems


def build_all(src_dir, program_stages, out_dir, workers=8):
    os.makedirs(out_dir, exist_ok=True)
    progs = [(i, p["name"], p["stages"]) for i, p in enumerate(program_stages)]
    json.dump([{"index": i, "name": p["name"], "flags": p.get("flags", 0)} for i, p in enumerate(program_stages)],
              open(os.path.join(out_dir, "index.json"), "w"), indent=1)
    kinds, failing = {}, 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for problems in pool.map(lambda p: build_one(p[0], p[1], p[2], src_dir, out_dir), progs):
            failing += bool(problems)
            for kind, detail in problems:
                kinds.setdefault(kind, []).append(detail)
    report = {"programs": len(progs), "failing": failing, "kinds": {k: {"count": len(v), "examples": v[:5]} for k, v in kinds.items()}}
    json.dump(report, open(os.path.join(out_dir, "report.json"), "w"), indent=1)
    return report


if __name__ == "__main__":
    src, stages_json = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(stages_json)), "spirv")
    rep = build_all(src, json.load(open(stages_json, encoding="utf-8")), out)
    print(json.dumps(rep, indent=1)[:4000])
