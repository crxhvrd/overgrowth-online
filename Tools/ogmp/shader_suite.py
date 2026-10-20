"""Shader checks (docs_gl46_plan.md stage 3.1).

  shader_collect  load a spread of levels (and the menus, and a battle) with config
                  shader_manifest: every program the game makes is recorded; the
                  union goes into Tools/ogmp/shader_manifest.txt (in git)
  shader_check    the game at its menu compiles and links every program of the
                  manifest (bridge shader_check, no level); any failure fails
  shader_glslang  the same, with the preprocessed sources written out (config
                  shader_debug), then glslangValidator (Vulkan SDK) on each: the
                  spec's verdict, not only NVIDIA's lenient one (other GPUs,
                  SPIR-V later). Reports; fails only with strict=True

    python -I Tools/ogmp.py run shader_check
"""

import concurrent.futures
import os
import threading
import time

from .instance import ROOT
from .scenarios import wait_loaded

MANIFEST = ROOT + "/Tools/ogmp/shader_manifest.txt"
OVERHAUL = "D:/OvergrowthAI/mod/overhaul"
COLLECT_LEVELS = [
    "Data/Levels/og_story/01_Slaver_Camp_Landing.xml",
    "Data/Levels/og_story/02_Slaver_Camp.xml",
    "Data/Levels/og_story/04_Main_Slaver_Camp.xml",
    "Data/Levels/LugaruStory/Raider_camp.xml",
    "Data/Levels/LugaruStory/Village.xml",
    "Data/Levels/arenas/Magma_Arena.xml",
    "Data/Levels/arenas/Cave_Arena.xml",
    "Data/Levels/lugaru_snow.xml",
    "Data/Levels/sea_cliffs.xml",
    "Data/Levels/challenge/tree_canyon_winter.xml",
]


def read_manifest(path=MANIFEST):
    entries = set()
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.rstrip("\n")
            if "\t" in line:
                flags, name = line.split("\t", 1)
                entries.add((int(flags), name))
    return entries


def shader_collect(ctx, parallel=2):
    from . import render_suite
    lock = threading.Lock()
    jobs = [("menu", "")] + [(os.path.basename(l)[:-4], l) for l in COLLECT_LEVELS] + [("battle", "Data/Levels/" + render_suite.BATTLE_MAP)]

    def run(job):
        name, level = job
        keys = {"shader_manifest": "1", "no_dialogues": "1", "limit_fps_in_game": "false"}
        mods = []
        if name == "battle":
            keys.update(render_suite._battle_keys(teams=4, per_team=4))
            mods = [OVERHAUL]
        with lock:
            inst = ctx.instance("shaders_" + name, keys=keys, mods=mods, level=level)
        try:
            if level:
                wait_loaded(inst)
                time.sleep(8.0)  # (the level runs: effects, characters, weapons)
            else:
                time.sleep(10.0)
                for script in ("main_menu_settings.as", "multiplayer_menu.as", "mainmenu.as"):
                    inst.bridge.call("menu", script=script)
                    time.sleep(2.0)
        finally:
            inst.stop()
        return read_manifest(inst.writedir + "Data/shader_manifest.txt")

    before = read_manifest()
    seen = set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=parallel) as pool:
        for got in pool.map(run, jobs):
            seen |= got
    union = before | seen
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        for flags, name in sorted(union, key=lambda e: (e[1], e[0])):
            f.write(f"{flags}\t{name}\n")
    m = {"seen": len(seen), "before": len(before), "now": len(union), "new": len(union - before)}
    return {"passed": len(seen) > 0, "metrics": m, "notes": [] if seen else ["nothing recorded"]}


def _glslang():
    sdk = os.environ.get("VULKAN_SDK", "")
    for exe in (os.path.join(sdk, "Bin", "glslangValidator.exe"), "R:/CoreFXProject/VulkanSDK/Bin/glslangValidator.exe"):
        if exe and os.path.exists(exe):
            return exe
    return None


STAGES = {".vert": "vert", ".frag": "frag", ".geom": "geom", ".tess_eval": "tese", ".tess_ctrl": "tesc"}


def glslang_dir(src_dir):
    """glslangValidator on every dumped source: {file: [error lines]} for the failing ones."""
    import subprocess
    exe = _glslang()
    if not exe:
        return None
    files = []
    for f in sorted(os.listdir(src_dir)):
        stage = next((st for ext, st in STAGES.items() if f.endswith(ext)), None)
        if stage:
            files.append((f, stage))

    def one(item):
        f, stage = item
        r = subprocess.run([exe, "-S", stage, os.path.join(src_dir, f)], capture_output=True, text=True, errors="replace")
        errs = [l.strip() for l in (r.stdout + r.stderr).splitlines() if l.startswith("ERROR")]
        return f, (errs if r.returncode != 0 else [])

    out = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for f, errs in pool.map(one, files):
            if errs:
                out[f] = errs
    return out, len(files)


def shader_glslang(ctx, strict=False):
    import re
    res = shader_check(ctx, keys={"shader_debug": "1"}, keep_dir=True)
    src_dir = ctx.out_dir + "/compiled_shaders"
    got = glslang_dir(src_dir) if os.path.isdir(src_dir) else None
    if got is None:
        res["notes"].append("no glslangValidator (Vulkan SDK) or no sources written")
        res["passed"] = False
        return res
    failing, total = got
    kinds = {}
    for f, errs in failing.items():
        for e in errs:
            key = re.sub(r"ERROR: .*?:\d+: ", "", e)  # (the message without file and line)
            key = re.sub(r"'[^']*'", "'_'", key)
            kinds.setdefault(key, set()).add(f)
    res["metrics"]["glslang_files"] = total
    res["metrics"]["glslang_failing"] = len(failing)
    res["metrics"]["glslang_kinds"] = sorted(((len(v), k, sorted(v)[0]) for k, v in kinds.items()), reverse=True)[:25]
    if strict and failing:
        res["passed"] = False
        res["notes"].append(f"glslang: {len(failing)} of {total} sources fail")
    return res


def shader_spirv(ctx, strict=True):
    """Every manifest program built for Vulkan (spirv_build.py): glslang -R,
    spirv-val, stage inputs and outputs, fragment outputs."""
    import json
    from . import spirv_build
    res = shader_check(ctx, keys={"shader_debug": "1"}, keep_dir=True)
    stages_path = ctx.out_dir + "/program_stages.json"
    if not os.path.exists(stages_path):
        res["passed"] = False
        res["notes"].append("no program stages recorded")
        return res
    rep = spirv_build.build_all(ctx.out_dir + "/compiled_shaders", json.load(open(stages_path, encoding="utf-8")), ctx.out_dir + "/spirv")
    # The latest build where config shader_vulkan_glsl can find it (stage 3.4)
    import shutil
    from .instance import TEST_DIR
    if rep["failing"] == 0:
        shutil.rmtree(TEST_DIR + "/spirv", ignore_errors=True)
        shutil.copytree(ctx.out_dir + "/spirv", TEST_DIR + "/spirv")
        res["metrics"]["spirv_dir"] = TEST_DIR + "/spirv"
    res["metrics"]["spirv_programs"] = rep["programs"]
    res["metrics"]["spirv_failing"] = rep["failing"]
    res["metrics"]["spirv_kinds"] = {k: v["count"] for k, v in rep["kinds"].items()}
    res["metrics"]["spirv_examples"] = {k: v["examples"][:2] for k, v in rep["kinds"].items()}
    if strict and rep["failing"]:
        res["passed"] = False
        res["notes"].append(f"Vulkan build: {rep['failing']} of {rep['programs']} programs fail ({', '.join(f'{k} {v}' for k, v in res['metrics']['spirv_kinds'].items())})")
    return res


def shader_check(ctx, chunk=40, keys=None, keep_dir=False):
    entries = sorted(read_manifest(), key=lambda e: (e[1], e[0]))
    if not entries:
        return {"passed": False, "metrics": {}, "notes": ["empty manifest (run shader_collect)"]}
    all_keys = {"no_dialogues": "1"}
    all_keys.update(keys or {})
    inst = ctx.instance("shader_check", keys=all_keys)
    m, fails = {"programs": len(entries)}, []
    try:
        time.sleep(6.0)
        start = time.time()
        failures, stages = [], []
        for i in range(0, len(entries), chunk):
            part = entries[i:i + chunk]
            res = inst.bridge.call("shader_check", timeout=300.0, programs=[[flags, name] for flags, name in part])
            failures += res["failures"]
            stages += [{"flags": flags, "name": name, "stages": st} for (flags, name), st in zip(part, res.get("stages", []))]
        m["seconds"] = round(time.time() - start, 1)
        m["failures"] = failures[:20]
        if failures:
            fails.append(f"{len(failures)} compile/link failures (first: {failures[0][:300]})")
        if not inst.alive():
            fails.append("the game died compiling")
    finally:
        inst.stop()
    if keep_dir:
        import json
        import shutil
        src = inst.writedir + "Data/CompiledShaders"
        if os.path.isdir(src):
            shutil.copytree(src, ctx.out_dir + "/compiled_shaders", dirs_exist_ok=True)
        json.dump(stages, open(ctx.out_dir + "/program_stages.json", "w", encoding="utf-8"), indent=1)
    return {"passed": not fails, "metrics": m, "notes": fails}
