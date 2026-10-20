"""Renderer checks (docs_gl46_plan.md, stage 0), run before and after every
renderer change:

  render_compare  the reference levels from fixed views, the game paused from
                  its first frame (test_start_paused): lossless shots compared
                  with the references (render_capture writes them)
  gl_errors       the same levels and a Large Battle with a GL debug context
                  ("gl_debug"): any error, undefined or deprecated behaviour fails
  render_check    the two above in one pass (each level loaded once, two at
                  a time); render_quick: one level, for iterating
  perf_baseline   GPU and CPU time per frame-timer zone in the reference scenes
                  (perf_capture writes the baseline); slower than it by 5% fails
  vk_errors       the Vulkan renderer (stage 5) with the validation layer and its
                  synchronization checks: any warning or error fails
  vk_textures     every texture a level loads from files, read back on OpenGL and
                  on Vulkan: the same bytes (uploads, formats, padding, cube maps)
  vk_parity       the reference views rendered on OpenGL and on Vulkan in one run,
                  each pair compared (not against the references): the renderers agree

References live in test/references/<set>/ (OGMP_REFS picks the set, default
"stage5"), on this PC only.
"""
import json
import math
import os
import time

from .instance import TEST_DIR
from .scenarios import join, wait_loaded

# (stage0: before the hue-keeping bloom; stage4a: before the integer noise hash)
REFS = TEST_DIR + "/references/" + os.environ.get("OGMP_REFS", "stage5")
OVERHAUL = "D:/OvergrowthAI/mod/overhaul"

# The levels the renderer is judged on: water and grass, a story camp, desert
# terrain, a village. Views: offsets from the player's start (camera, target)
RENDER_SCENES = [
    ("slaver_landing", "Data/Levels/og_story/01_Slaver_Camp_Landing.xml"),
    ("slaver_path", "Data/Levels/og_story/02_Slaver_Camp.xml"),
    ("raider_camp", "Data/Levels/LugaruStory/Raider_camp.xml"),
    ("lugaru_village", "Data/Levels/LugaruStory/Village.xml"),
]
VIEWS = [
    ("near", (5.0, 2.5, 5.0), (0.0, 1.0, 0.0)),
    ("wide", (-14.0, 9.0, -6.0), (4.0, 0.0, 6.0)),
    ("low", (2.0, 0.8, -7.0), (-2.0, 1.2, 10.0)),
]
SHOT_SIZE = (1280, 720)



def _scene_instance(ctx, name, level, keys, mods=None, width=SHOT_SIZE[0], height=SHOT_SIZE[1]):
    all_keys = {"limit_fps_in_game": "false", "vsync": "0"}
    all_keys.update(keys)
    inst = ctx.instance(name, width=width, height=height, keys=all_keys, mods=mods or [], level=level)
    wait_loaded(inst)
    return inst


def _shot(inst, path):
    inst.bridge.call("screenshot", path=path)
    end = time.time() + 10
    while time.time() < end and not os.path.exists(path):
        time.sleep(0.2)
    time.sleep(0.4)


def _compare(a_path, b_path, diff_path):
    from PIL import Image, ImageChops
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    if a.size != b.size:
        return {"size": [a.size, b.size], "mean_diff": 255.0, "changed": 1.0, "psnr": 0.0}
    diff = ImageChops.difference(a, b)
    hist = diff.convert("L").histogram()
    pixels = a.size[0] * a.size[1]
    mean = sum(i * n for i, n in enumerate(hist)) / pixels
    changed = sum(hist[9:]) / pixels
    mse = sum(i * i * n for i, n in enumerate(hist)) / pixels
    psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    if mean > 0.0:
        diff.point(lambda v: min(255, v * 8)).save(diff_path)
    return {"mean_diff": round(mean, 4), "changed": round(changed, 6), "psnr": round(psnr, 2)}


def _render_pass(ctx, out_dir):
    """Every scene and view once; the shot paths by name."""
    shots, fails = {}, []
    for scene, level in RENDER_SCENES:
        inst = _scene_instance(ctx, scene, level, {"test_start_paused": "1"})
        time.sleep(6.0)  # (streaming textures)
        me = inst.bridge.me()
        if me is None:
            fails.append(f"{scene}: no player character")
            inst.stop()
            continue
        x, y, z = me["pos"]
        for view, cam, target in VIEWS:
            inst.bridge.call("view", pos=[x + cam[0], y + cam[1], z + cam[2]],
                             target=[x + target[0], y + target[1], z + target[2]], fov=70.0)
            time.sleep(1.5)
            shot = f"{out_dir}/{scene}_{view}.png"
            _shot(inst, shot)
            shots[f"{scene}_{view}"] = shot
        inst.stop()
    return shots, fails


def render_compare(ctx, capture=False):
    """capture: shoot twice; the first set is the reference, the difference
    between the two each view's noise (characters' first steps before the
    pause, a raft that moved): compare allows three times it, per view."""
    import shutil
    m, fails = {}, []
    os.makedirs(REFS, exist_ok=True)
    noise_path = REFS + "/noise.json"
    shots, pass_fails = _render_pass(ctx, ctx.out_dir)
    fails += pass_fails

    if capture:
        for name, shot in shots.items():
            shutil.copyfile(shot, f"{REFS}/{name}.png")
        again_dir = ctx.out_dir + "/again"
        os.makedirs(again_dir, exist_ok=True)
        again, pass_fails = _render_pass(ctx, again_dir)
        fails += pass_fails
        noise = {}
        for name, shot in again.items():
            noise[name] = _compare(shot, f"{REFS}/{name}.png", f"{again_dir}/{name}_noise.png")
        json.dump(noise, open(noise_path, "w", encoding="utf-8"), indent=1)
        m["noise"] = noise
        return {"passed": not fails, "metrics": m, "notes": fails}

    _judge(shots, ctx.out_dir, m, fails)
    return {"passed": not fails, "metrics": m, "notes": fails}


def _judge(shots, out_dir, m, fails):
    """Each shot against its reference, allowed three times the view's noise."""
    noise_path = REFS + "/noise.json"
    noise = json.load(open(noise_path, encoding="utf-8")) if os.path.exists(noise_path) else {}
    for name, shot in shots.items():
        ref = f"{REFS}/{name}.png"
        if not os.path.exists(ref):
            fails.append(f"{name}: no reference (run render_capture)")
            continue
        r = _compare(shot, ref, f"{out_dir}/{name}_diff.png")
        n = noise.get(name, {"mean_diff": 0.0, "changed": 0.0})
        mean_max = max(0.05, n["mean_diff"] * 3.0 + 0.05)
        changed_max = max(0.0005, n["changed"] * 3.0 + 0.0005)
        r["allowed"] = {"mean_diff": round(mean_max, 4), "changed": round(changed_max, 6)}
        m[name] = r
        if r["mean_diff"] > mean_max or r["changed"] > changed_max:
            fails.append(f"{name}: differs (mean {r['mean_diff']} > {round(mean_max, 3)} or changed {r['changed']} > {round(changed_max, 5)}, psnr {r['psnr']})")


def render_check(ctx, scenes=None, battle=True, parallel=2):
    """render_compare and gl_errors in one pass: each level loaded once, with the
    GL debug context, shot from the fixed views and asked for GL problems; levels
    two at a time (the shots are of a paused game, so sharing the GPU changes
    nothing in them). The battle is checked for GL problems only.
    render_quick: one level."""
    import concurrent.futures
    import threading
    m, fails, shots = {}, [], {}
    lock = threading.Lock()
    if scenes is None and os.environ.get("OGMP_RENDER_SCENES"):  # (e.g. "slaver_landing,raider_camp")
        scenes = os.environ["OGMP_RENDER_SCENES"].split(",")
        battle = False
    jobs = [(scene, level) for scene, level in RENDER_SCENES if scenes is None or scene in scenes]
    if battle:
        jobs.append(("large_battle", "Data/Levels/" + BATTLE_MAP))

    def run(job):
        scene, level = job
        keys = {"gl_debug": "1"}
        mods = []
        if scene == "large_battle":
            keys.update(_battle_keys(teams=4, per_team=4))
            mods = [OVERHAUL]
        else:
            keys["test_start_paused"] = "1"
        all_keys = {"limit_fps_in_game": "false", "vsync": "0"}
        all_keys.update(keys)
        with lock:  # (instance numbers and ports are handed out in order)
            inst = ctx.instance(scene, width=SHOT_SIZE[0], height=SHOT_SIZE[1], keys=all_keys, mods=mods, level=level)
        out = {"shots": {}, "fails": [], "gl": None}
        try:
            wait_loaded(inst)
            time.sleep(8.0 if scene == "large_battle" else 6.0)  # (the battle runs; textures stream in)
            if scene != "large_battle":
                me = inst.bridge.me()
                if me is None:
                    out["fails"].append(f"{scene}: no player character")
                    return out
                x, y, z = me["pos"]
                for view, cam, target in VIEWS:
                    inst.bridge.call("view", pos=[x + cam[0], y + cam[1], z + cam[2]],
                                     target=[x + target[0], y + target[1], z + target[2]], fov=70.0)
                    time.sleep(1.5)
                    shot = f"{ctx.out_dir}/{scene}_{view}.png"
                    _shot(inst, shot)
                    out["shots"][f"{scene}_{view}"] = shot
            res = inst.bridge.call("gl_problems")
            out["gl"] = {"alive": res["alive"], "count": res["count"],
                         "problems": [f"{p['count']}x {p['message']}" for p in res["problems"]]}
            if not res["alive"]:
                out["fails"].append(f"{scene}: the GL debug output never reached the callback")
            if res["count"]:
                out["fails"].append(f"{scene}: {res['count']} GL problems ({len(res['problems'])} kinds)")
            if job == jobs[0]:
                # An error on purpose: the output reports errors, not only our marker
                provoked = inst.bridge.call("gl_problems", provoke=True)
                if provoked["count"] <= res["count"]:
                    out["fails"].append("a deliberate GL error went unreported")
        finally:
            inst.stop()
        return out

    with concurrent.futures.ThreadPoolExecutor(max_workers=parallel) as pool:
        for job, out in zip(jobs, pool.map(run, jobs)):
            shots.update(out["shots"])
            fails += out["fails"]
            if out["gl"] is not None:
                m["gl_" + job[0]] = out["gl"]
    _judge(shots, ctx.out_dir, m, fails)
    return {"passed": not fails, "metrics": m, "notes": fails}


def gl_errors(ctx):
    m, fails = {}, []
    scenes = [(s, lvl, []) for s, lvl in RENDER_SCENES] + [("large_battle", "Data/Levels/" + BATTLE_MAP, [OVERHAUL])]
    for scene, level, mods in scenes:
        keys = {"gl_debug": "1"}
        if scene == "large_battle":
            keys.update(_battle_keys(teams=4, per_team=4))
        inst = _scene_instance(ctx, scene, level, keys, mods=mods)
        time.sleep(8.0)  # (the level runs: AI, physics, items, particles)
        res = inst.bridge.call("gl_problems")
        m[scene] = {"alive": res["alive"], "count": res["count"],
                    "problems": [f"{p['count']}x {p['message']}" for p in res["problems"]]}
        if scene == scenes[0][0]:
            # An error on purpose: the output reports errors, not only our marker
            provoked = inst.bridge.call("gl_problems", provoke=True)
            m["reports_errors"] = provoked["count"] > res["count"]
            if not m["reports_errors"]:
                fails.append("a deliberate GL error went unreported")
        if not res["alive"]:
            fails.append(f"{scene}: the GL debug output never reached the callback")
        if res["count"]:
            fails.append(f"{scene}: {res['count']} GL problems ({len(res['problems'])} kinds)")
        inst.stop()
    return {"passed": not fails, "metrics": m, "notes": fails}


# ---- performance -----------------------------------------------------------------------

BATTLE_MAP = "overhaul_battles/forgotten_plains.xml"


def _battle_keys(teams=8, per_team=12):
    keys = {"aiob_pending": "true", "aiob_level": BATTLE_MAP, "aiob_teams": str(teams), "aiob_player_mode": "1",
            "aiob_player_team": "0", "aiob_waves": "1", "aiob_time_limit": "10", "aiob_cleanup": "0",
            "aiob_manual_place": "false", "aiob_slowmo_finish": "false"}
    for t in range(teams):
        keys[f"aiob_t{t}_count"] = str(per_team)
    return keys


def _sample(inst, windows=4):
    """The frame timers' averages over a few one-second windows."""
    frame, zones = [], {}
    for _ in range(windows):
        time.sleep(1.1)
        p = inst.bridge.call("perf")
        if p["frames"] == 0:
            continue
        frame.append(p["frame_ms"])
        for z in p["zones"]:
            zones.setdefault(z["path"], []).append((z["gpu_ms"], z["cpu_ms"], z["calls"]))
    out = {"frame_ms": round(sum(frame) / len(frame), 3) if frame else None, "zones": {}}
    for path, vals in zones.items():
        n = len(vals)
        out["zones"][path] = {"gpu_ms": round(sum(v[0] for v in vals) / n, 4), "cpu_ms": round(sum(v[1] for v in vals) / n, 4),
                              "calls": round(sum(v[2] for v in vals) / n, 2)}
    return out


def _perf_scenes(ctx):
    keys = {"frame_timers": "1"}
    w, h = 1920, 1080
    yield "slaver_path", _scene_instance(ctx, "perf_slaver_path", "Data/Levels/og_story/02_Slaver_Camp.xml", keys, width=w, height=h)
    yield "raider_camp", _scene_instance(ctx, "perf_raider_camp", "Data/Levels/LugaruStory/Raider_camp.xml", keys, width=w, height=h)
    battle_keys = dict(keys)
    battle_keys.update(_battle_keys())
    yield "large_battle", _scene_instance(ctx, "perf_battle", "Data/Levels/" + BATTLE_MAP, battle_keys, mods=[OVERHAUL], width=w, height=h)
    # Co-op: the host's frame with a guest in (both on this PC: GPU shared)
    host = ctx.instance("perf_coop_host", width=w, height=h, keys={"frame_timers": "1", "limit_fps_in_game": "false", "vsync": "0"})
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign="com-wolfire-overgrowth-campaign")
    wait_loaded(host)
    guest = ctx.instance("perf_coop_guest", keys={"limit_fps_in_game": "false", "vsync": "0"})
    join(ctx, host, guest)
    yield "coop_host", host
    guest.stop()


def _perf_pass(ctx):
    out = {}
    for scene, inst in _perf_scenes(ctx):
        # (warm up: streaming, shader compiles; a battle until its fighters are all in)
        time.sleep(22.0 if scene == "large_battle" else 8.0)
        out[scene] = _sample(inst)
        inst.stop()
    return out


def perf_baseline(ctx, capture=False):
    """capture: two passes; the first is the baseline, their difference each
    zone's noise. A zone is slower when it exceeds the baseline by 5% plus
    three times its noise (and 0.05 ms)."""
    m, fails = {}, []
    base_path = REFS + "/perf_baseline.json"
    noise_path = REFS + "/perf_noise.json"
    if capture:
        first = _perf_pass(ctx)
        second = _perf_pass(ctx)
        noise = {}
        for scene, s in first.items():
            t = second.get(scene, {"frame_ms": None, "zones": {}})
            noise[scene] = {"frame_ms": abs((s["frame_ms"] or 0) - (t["frame_ms"] or 0)),
                            "zones": {p: {k: abs(z[k] - t["zones"].get(p, z)[k]) for k in ("gpu_ms", "cpu_ms")}
                                      for p, z in s["zones"].items()}}
        os.makedirs(REFS, exist_ok=True)
        json.dump(first, open(base_path, "w", encoding="utf-8"), indent=1)
        json.dump(noise, open(noise_path, "w", encoding="utf-8"), indent=1)
        return {"passed": True, "metrics": {k: {"frame_ms": v["frame_ms"], "again": second.get(k, {}).get("frame_ms")}
                                            for k, v in first.items()}, "notes": []}

    if not os.path.exists(base_path):
        return {"passed": False, "metrics": {}, "notes": ["no baseline (run perf_capture)"]}
    base = json.load(open(base_path, encoding="utf-8"))
    noise = json.load(open(noise_path, encoding="utf-8")) if os.path.exists(noise_path) else {}
    now = _perf_pass(ctx)
    for scene, s in now.items():
        m[scene] = {"frame_ms": s["frame_ms"], "baseline": base.get(scene, {}).get("frame_ms")}
        # The battle is for reference only: its fight decides what is on screen
        # and when the shadow cascades and buffers update (measured swings of 2-10x)
        if scene not in base or scene == "large_battle":
            continue
        b, n = base[scene], noise.get(scene, {"frame_ms": 0.0, "zones": {}})
        if scene != "large_battle" and s["frame_ms"] and b["frame_ms"] and s["frame_ms"] > b["frame_ms"] * 1.05 + 3.0 * n["frame_ms"] + 0.1:
            fails.append(f"{scene}: frame {s['frame_ms']} ms (baseline {b['frame_ms']})")
        # Wider for the battle (the AI's: how many fight on screen changes from run
        # to run) and co-op (two games on one PC share its CPU and GPU)
        margin = 1.15 if scene in ("large_battle", "coop_host") else 1.05
        for path, z in s["zones"].items():
            bz = b["zones"].get(path)
            if not bz:
                continue
            # The swap is the wait for the display (frames are capped at its rate):
            # it grows when drawing gets faster
            if path.startswith("Graphics::SwapToScreen()"):
                continue
            nz = n["zones"].get(path, {"gpu_ms": 0.0, "cpu_ms": 0.0})
            # A zone called a different number of times (characters, objects on
            # screen): its time per call
            per_call = z["calls"] > 0 and bz["calls"] > 0 and abs(z["calls"] - bz["calls"]) > 0.1 * bz["calls"]
            # (the battle's CPU side is its AI's and skinning's, which vary with the
            # fight: reported, not judged; its GPU side is)
            for k in (("gpu_ms",) if scene == "large_battle" else ("gpu_ms", "cpu_ms")):
                if per_call:
                    now_v, base_v = z[k] / z["calls"], bz[k] / bz["calls"]
                    if now_v > base_v * margin + 0.01:
                        fails.append(f"{scene} {path}: {k} per call {round(now_v, 4)} (baseline {round(base_v, 4)})")
                elif z[k] > bz[k] * margin + 3.0 * nz[k] + 0.05:
                    fails.append(f"{scene} {path}: {k} {z[k]} (baseline {bz[k]})")
    return {"passed": not fails, "metrics": m, "notes": fails}


def render_check_vk(ctx):
    """render_check with every manifest program made from the latest Vulkan build
    (shader_spirv writes test/spirv): GLSL 4.60 back from the SPIR-V, loose uniforms
    in the default block (stage 3.4)."""
    from .instance import TEST_DIR
    if not os.path.exists(TEST_DIR + "/spirv/index.json"):
        return {"passed": False, "metrics": {}, "notes": ["no Vulkan build (run shader_spirv)"]}
    old = os.environ.get("OGMP_KEYS")
    os.environ["OGMP_KEYS"] = (old + ";" if old else "") + "shader_vulkan_glsl=" + TEST_DIR + "/spirv"
    try:
        return render_check(ctx)
    finally:
        if old is None:
            os.environ.pop("OGMP_KEYS", None)
        else:
            os.environ["OGMP_KEYS"] = old


# ---- the Vulkan renderer (docs_gl46_plan.md stage 5) ----------------------------------

def vk_errors(ctx, scenes=("slaver_landing", "raider_camp")):
    """The game on the Vulkan renderer ("renderer" = vulkan) with the validation layer
    and its synchronization checks ("vk_validation" = 2): the reference levels load
    and run, and frames keep being presented. A validation warning or error (also at
    shutdown), a start that fell back to OpenGL, or frames that stop coming fail it.
    A window capture per level goes to the results (until stage 5.5 draws the
    passes: the cleared window)."""
    m, fails = {}, []
    for scene, level in [(s, lvl) for s, lvl in RENDER_SCENES if s in scenes]:
        keys = {"renderer": "vulkan", "vk_validation": "2"}
        if os.path.exists(TEST_DIR + "/spirv/index.json"):
            keys["shader_vulkan_glsl"] = TEST_DIR + "/spirv"  # (the levels draw: stage 5.4)
        inst = _scene_instance(ctx, "vk_" + scene, level, keys)
        st = inst.bridge.call("status")
        r = m[scene] = {"renderer": st.get("renderer")}
        if st.get("renderer") != "vulkan":
            fails.append(f"{scene}: started on {st.get('renderer')}, not Vulkan")
            inst.stop()
            continue
        frames0 = st.get("vk_frames", 0)
        time.sleep(5.0)  # (the level runs: AI, physics, items, particles)
        st = inst.bridge.call("status")
        r["frames_in_5s"] = st.get("vk_frames", 0) - frames0
        r["draws"] = st.get("vk_draws", 0)
        r["pipelines"] = st.get("vk_pipelines", 0)
        inst.capture(f"{ctx.out_dir}/vk_{scene}.png")
        if scene == scenes[0]:
            # The swapchain made again: a resize, then minimised and restored
            from .instance import TOOLS, _powershell

            def window(*args):
                res = _powershell("-File", TOOLS + "/window_pid.ps1", "-ProcessId", str(inst.pid), *args)
                r.setdefault("window_ops", []).append(res.stdout.strip())
                if res.returncode != 0:
                    fails.append(f"{scene}: window_pid.ps1 {' '.join(args)}: {(res.stderr or res.stdout).strip()[:300]}")
            window("-Op", "resize", "-Width", "1024", "-Height", "600")
            time.sleep(2.0)
            window("-Op", "minimize")
            time.sleep(1.0)
            frames_min = inst.bridge.call("status").get("vk_frames", 0)
            time.sleep(1.0)
            r["frames_in_1s_minimized"] = inst.bridge.call("status").get("vk_frames", 0) - frames_min
            if r["frames_in_1s_minimized"] > 1:
                fails.append(f"{scene}: {r['frames_in_1s_minimized']} frames presented in 1 s while minimised")
            window("-Op", "restore")
            time.sleep(1.0)
            frames1 = inst.bridge.call("status").get("vk_frames", 0)
            time.sleep(2.0)
            st = inst.bridge.call("status")
            r["frames_in_2s_after_restore"] = st.get("vk_frames", 0) - frames1
            inst.capture(f"{ctx.out_dir}/vk_{scene}_resized.png")
            if r["frames_in_2s_after_restore"] < 20:
                fails.append(f"{scene}: {r['frames_in_2s_after_restore']} frames in 2 s after a resize and a restore")
        r["vk_problems"] = st.get("vk_problems")
        inst.stop()
        log = inst.log()
        r["device"] = next((l.split("Vulkan renderer on ", 1)[1].strip() for l in log.splitlines() if "Vulkan renderer on " in l), "")
        messages = [l.strip() for l in log.splitlines() if "Vulkan error" in l or "Vulkan warning" in l]
        r["validation_messages"] = len(messages)
        r["swapchains"] = sum(1 for l in log.splitlines() if "Vulkan swapchain: " in l)
        if scene == scenes[0] and r["swapchains"] < 2:
            fails.append(f"{scene}: the resize made no new swapchain")
        if messages:
            r["first_messages"] = [x[:400] for x in messages[:3]]
            fails.append(f"{scene}: {len(messages)} validation messages (also counts shutdown)")
        if r["frames_in_5s"] < 50:
            fails.append(f"{scene}: {r['frames_in_5s']} frames presented in 5 s")
        if "Vulkan device lost" in log or "the GPU stopped taking work" in log:
            fails.append(f"{scene}: the device was lost")

    # A Vulkan start that fails (here: the loader finds no driver) starts on OpenGL
    saved = {k: os.environ.get(k) for k in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES")}
    os.environ["VK_DRIVER_FILES"] = os.environ["VK_ICD_FILENAMES"] = ctx.out_dir + "/no_such_driver.json"
    try:
        scene, level = RENDER_SCENES[0]
        inst = _scene_instance(ctx, "vk_fallback", level, {"renderer": "vulkan", "vk_validation": "2"})
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    st = inst.bridge.call("status")
    r = m["fallback"] = {"renderer": st.get("renderer"), "level_loaded": st.get("level_loaded"), "gl_problems": st.get("gl_problems")}
    inst.capture(f"{ctx.out_dir}/vk_fallback.png")
    inst.stop()
    r["logged"] = "The Vulkan renderer didn't start" in inst.log()
    if st.get("renderer") != "gl" or not st.get("level_loaded") or not r["logged"]:
        fails.append(f"no driver: {r} (wanted OpenGL with the level loaded)")
    return {"passed": not fails, "metrics": m, "notes": fails}


def vk_textures(ctx, scene="slaver_landing"):
    """Stage 5.3: each texture a level loads from files, its first level read back on
    OpenGL and on Vulkan (validation and synchronization checks on) and hashed: the
    same bytes for the same file, or an upload, a format, the RGB padding or a cube
    map's faces is wrong. Validation messages fail it too."""
    level = dict(RENDER_SCENES)[scene]
    m, fails, digests = {}, [], {}
    for renderer in ("gl", "vulkan"):
        keys = {"renderer": renderer}
        if renderer == "vulkan":
            keys["vk_validation"] = "2"
        t0 = time.time()
        inst = _scene_instance(ctx, "tex_" + renderer, level, keys)
        load_s = time.time() - t0
        res = inst.bridge.call("texture_digest", timeout=180)
        st = inst.bridge.call("status")
        inst.stop()
        digests[renderer] = {f'{t["name"]}|{t["format"]}|{t["layers"]}': t for t in res["textures"]}  # (a file may load as sRGB and linear)
        r = m[renderer] = {"renderer": st.get("renderer"), "textures": len(res["textures"]), "start_and_load_s": round(load_s, 1)}
        if "vulkan" in res:
            r["vulkan"] = {k: (round(v, 1) if isinstance(v, float) else v) for k, v in res["vulkan"].items()}
        if st.get("renderer") != renderer:
            fails.append(f"asked for {renderer}, ran on {st.get('renderer')}")
        if renderer == "vulkan":
            messages = [l.strip() for l in inst.log().splitlines() if "Vulkan error" in l or "Vulkan warning" in l]
            r["validation_messages"] = len(messages)
            if messages:
                r["first_messages"] = [x[:400] for x in messages[:3]]
                fails.append(f"{len(messages)} validation messages")
            formats = [l.strip()[:200] for l in inst.log().splitlines() if "Vulkan formats:" in l or "Vulkan resources:" in l]
            if formats:
                r["resource_messages"] = formats[:10]
                fails.append(f"{len(formats)} resource-layer messages (formats or calls not handled)")
    gl, vk = digests["gl"], digests["vulkan"]
    common = sorted(set(gl) & set(vk))
    differ = [n for n in common if gl[n]["hash"] != vk[n]["hash"] or gl[n]["width"] != vk[n]["width"]]
    m["compared"] = len(common)
    m["differ"] = len(differ)
    m["only_gl"] = sorted(set(gl) - set(vk))[:10]
    m["only_vulkan"] = sorted(set(vk) - set(gl))[:10]
    if differ:
        m["differing"] = [{"name": n, "gl": gl[n], "vulkan": vk[n]} for n in differ[:8]]
        fails.append(f"{len(differ)} of {len(common)} textures read back differently")
    if len(common) < 50:
        fails.append(f"only {len(common)} textures compared")
    return {"passed": not fails, "metrics": m, "notes": fails}


VK_PARITY_MIN_PSNR = 40.0  # (GL against GL between runs is 41-55 dB on most views)
# Both renderers without anisotropic filtering: NVIDIA's GL and Vulkan drivers filter
# differently with it (Lugaru village's foliage: 34 dB with it, 41 without), the
# driver's doing, not the renderer's
VK_PARITY_KEYS = {"anisotropy": "1"}
# Beside the reference scenes: one without MSAA (other shader permutations, compiled at
# run time; the scene's depth read while it's attached). Its grass is alpha-tested, each
# pixel in or out, so the least difference flips pixels: GL against GL is 39 dB there,
# and its floor is lower (what it guards against measured 4-27 dB)
VK_PARITY_EXTRA = [("lugaru_village_no_msaa", "Data/Levels/LugaruStory/Village.xml", {"multisample": "1"}, 35.0)]


def vk_parity(ctx, scenes=None):
    """Stage 5.5: each reference view rendered on OpenGL and on Vulkan (validation on),
    the pair compared: PSNR below VK_PARITY_MIN_PSNR fails, unless GL itself differs
    as much between two runs there (characters and a raft move before the pause): a
    view under the floor is shot again on GL, and Vulkan fails only if it's more than
    3 dB further from GL than that (one GL-GL sample swings by 2-3 dB itself). Independent of the references, so a deliberate
    look change on both renderers doesn't break it."""
    m, fails = {}, []
    cases = [(scene, level, {}, VK_PARITY_MIN_PSNR) for scene, level in RENDER_SCENES] + VK_PARITY_EXTRA

    def shoot(scene, level, renderer, extra, tag, views):
        keys = {"test_start_paused": "1", "renderer": renderer}
        keys.update(VK_PARITY_KEYS)
        keys.update(extra)
        if renderer == "vulkan":
            keys.update({"vk_validation": "1", "shader_vulkan_glsl": TEST_DIR + "/spirv"})
        inst = _scene_instance(ctx, f"{scene}_{tag}", level, keys)
        time.sleep(6.0)
        shots = {}
        me = inst.bridge.me()
        if me is None:
            fails.append(f"{scene} {tag}: no player character")
            inst.stop()
            return shots
        x, y, z = me["pos"]
        for view, cam, target in VIEWS:
            if view not in views:
                continue
            inst.bridge.call("view", pos=[x + cam[0], y + cam[1], z + cam[2]],
                             target=[x + target[0], y + target[1], z + target[2]], fov=70.0)
            time.sleep(1.5)
            shots[view] = f"{ctx.out_dir}/{scene}_{view}_{tag}.png"
            _shot(inst, shots[view])
        inst.stop()
        if renderer == "vulkan":
            messages = [l for l in inst.log().splitlines() if "Vulkan error" in l or "Vulkan warning" in l]
            if messages:
                fails.append(f"{scene}: {len(messages)} validation messages (first: {messages[0].strip()[:200]})")
        return shots

    all_views = [v for v, _, _ in VIEWS]
    for scene, level, extra, min_psnr in cases:
        if scenes and scene not in scenes:
            continue
        gl = shoot(scene, level, "gl", extra, "gl", all_views)
        vk = shoot(scene, level, "vulkan", extra, "vulkan", all_views)
        results, again = {}, []
        for view in all_views:
            if view in gl and view in vk:
                results[view] = _compare(vk[view], gl[view], f"{ctx.out_dir}/{scene}_{view}_diff.png")
                results[view]["min_psnr"] = min_psnr
                if results[view]["psnr"] < min_psnr:
                    again.append(view)
        if again:
            gl2 = shoot(scene, level, "gl", extra, "gl_again", again)
            for view in again:
                if view in gl2:
                    noise = _compare(gl2[view], gl[view], f"{ctx.out_dir}/{scene}_{view}_gl_noise.png")["psnr"]
                    results[view]["gl_again_psnr"] = noise
                    results[view]["min_psnr"] = round(min(min_psnr, noise - 3.0), 2)
        for view, r in results.items():
            m[f"{scene}_{view}"] = r
            if r["psnr"] < r["min_psnr"]:
                fails.append(f"{scene}_{view}: {r['psnr']} dB (GL again: {r.get('gl_again_psnr')}; mean {r['mean_diff']}, changed {r['changed']})")
    return {"passed": not fails, "metrics": m, "notes": fails}
