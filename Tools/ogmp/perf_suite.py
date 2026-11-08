"""Phase 0 of docs_perf_plan.md: where the game's time goes, scene by scene, both renderers.

    perf_scenes    Slaver Path, Raider Camp, the Overhaul battle at 16 / 48 / 96 characters, each on
                   GL and Vulkan, and a co-op host (Vulkan): the frame's average, its slowest 1%
                   (the "1% low") and its worst, measured with the CPU zones off (they cost about
                   0.35 ms on a 3 ms frame); then the zone tree with them on (the top zones); and
                   the sampling profiler on the 96-character battle (the hottest native and script
                   functions); and how busy each thread is in it (Tools/thread_cycles.ps1, cycle
                   counts). Results: perf_scenes.json in the run's folder, and the latest also
                   in test/references/perf_plan/ for A/B against later builds. A battle's load moves
                   as its fight goes (who is down): compare its "per" costs (per character update,
                   per script call, per step), not only its frame time. The AI debug log is off, as
                   players have it (the harness turns it on for the gameplay tests).
    perf_profile   only the 96-character battle's profile (Vulkan), for a quick look.

        python -I Tools/ogmp.py run perf_scenes      (about 9 minutes)
"""

import json
import os
import shutil
import subprocess
import time

from .instance import TEST_DIR, TOOLS
from .scenarios import PROFILES, join, wait_loaded
from .render_suite import _scene_instance, _battle_keys, BATTLE_MAP
from .online_mod_suite import OVERHAUL

REFS = TEST_DIR + "/references/perf_plan"
SIZE = (1920, 1080)
PLAYER_KEYS = {"aio_debug_log": "false"}  # (Tools/ogmp/instance.py turns it on for every instance)

# A zone's cost per call, by its name (the last part of its path): what a battle compares fairly
PER = {"char_update_ms": "_movement_object # update", "script_call_ms": "Angelscript Update()",
       "animation_ms": "RiggedObject Update()", "step_ms": "Timestep", "physics_step_ms": "Bullet world update",
       "bullet_solve_ms": "stepSimulation", "contact_scripts_ms": "Collision effects", "particles_step_ms": "Looping through all particles"}
LEVELS = [("slaver_path", "Data/Levels/og_story/02_Slaver_Camp.xml", None),
          ("raider_camp", "Data/Levels/LugaruStory/Raider_camp.xml", None),
          ("battle_16", None, 2), ("battle_48", None, 6), ("battle_96", None, 12)]


def _frames(inst, windows):
    """Every frame's time over a few one-second windows: average, slowest 1%, worst; the job
    system's workers (busy ms per frame) and anything that ran in a job and shouldn't have."""
    frames, busy = [], []
    p = {}
    for _ in range(windows):
        time.sleep(1.05)
        p = inst.bridge.call("perf", frame_list=True)
        frames += p.get("frame_list", [])
        busy.append(p.get("worker_busy_ms", 0.0))
    if not frames:
        return {"frames": 0}
    s = sorted(frames)
    p99 = s[min(len(s) - 1, int(len(s) * 0.99))]
    return {"frames": len(frames), "frame_ms": round(sum(frames) / len(frames), 3), "p99_ms": round(p99, 3),
            "max_ms": round(s[-1], 3), "fps": round(1000.0 * len(frames) / sum(frames), 1), "low1_fps": round(1000.0 / p99, 1),
            "workers": p.get("workers"), "worker_busy_ms": round(sum(busy) / len(busy), 3), "job_violations": p.get("job_violations")}


def _per(acc):
    """Cost per call of the zones in PER, and how many characters update per step."""
    out = {}
    for key, leaf in PER.items():
        ms = sum(v[0] for p, v in acc.items() if p.split("/")[-1] == leaf)
        calls = sum(v[2] for p, v in acc.items() if p.split("/")[-1] == leaf)
        if calls > 0:
            out[key] = round(ms / calls, 4)
    steps = sum(v[2] for p, v in acc.items() if p.split("/")[-1] == "Timestep")
    updates = sum(v[2] for p, v in acc.items() if p.split("/")[-1] == PER["char_update_ms"])
    if steps > 0:
        out["steps_per_frame"] = round(steps, 2)
        out["characters"] = round(updates / steps, 1)
    return out


def _zones(inst, windows=3, top=18):
    """The zone tree with the CPU zones on: the biggest by CPU time (depth <= 6), and by GPU;
    and the costs per call (_per)."""
    inst.bridge.call("perf", cpu_zones=True)
    time.sleep(1.2)
    acc = {}
    for _ in range(windows):
        time.sleep(1.05)
        for z in inst.bridge.call("perf")["zones"]:
            a = acc.setdefault(z["path"], [0.0, 0.0, 0.0])
            a[0] += z["cpu_ms"] / windows
            a[1] += z["gpu_ms"] / windows
            a[2] += z["calls"] / windows
    inst.bridge.call("perf", cpu_zones=False)
    rows = [{"path": p, "cpu_ms": round(v[0], 3), "gpu_ms": round(v[1], 3), "calls": round(v[2], 1)}
            for p, v in acc.items() if p.count("/") <= 6]
    by_cpu = sorted(rows, key=lambda r: -r["cpu_ms"])[:top]
    by_gpu = sorted(rows, key=lambda r: -r["gpu_ms"])[:8]
    return {"by_cpu": by_cpu, "by_gpu": by_gpu, "per": _per(acc)}


def _profile(inst, seconds=5.0, top=25):
    inst.bridge.call("profile", seconds=seconds, interval_ms=1.0)
    time.sleep(seconds + 1.0)
    r = inst.bridge.call("profile", top=top)
    while r.get("running"):
        time.sleep(0.5)
        r = inst.bridge.call("profile", top=top)
    return {k: r.get(k) for k in ("samples", "seconds", "in_script_percent", "native_self", "native_inclusive",
                                  "script_self", "script_inclusive", "script_lines")}


def _threads(inst, seconds=5.0):
    """How busy each thread is (share of one core, by cycle counts), and the whole process in cores."""
    out = os.path.join(TEST_DIR, "run", f"threads_{inst.pid}.json")
    subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", TOOLS + "/thread_cycles.ps1",
                    "-ProcessId", str(inst.pid), "-Seconds", str(seconds), "-Out", out], capture_output=True, timeout=120)
    try:
        r = json.load(open(out, encoding="utf-8-sig"))
        os.remove(out)
    except (OSError, ValueError):
        return {}
    busy = [{"core_percent": t["Core"], "module": t["Module"]} for t in r.get("Threads", []) if t["Core"] >= 0.5]
    return {"cores": round(r.get("Cores", 0.0), 2), "busy_threads": busy}


def _scene(ctx, name, renderer, level, per_team, profile=False):
    keys = {"frame_timers": "1", "frame_timers_cpu": "0", "renderer": renderer}
    keys.update(PLAYER_KEYS)
    mods = []
    if per_team is not None:
        keys.update(_battle_keys(per_team=per_team))
        level, mods = "Data/Levels/" + BATTLE_MAP, [OVERHAUL]
    inst = _scene_instance(ctx, f"perf_{renderer}_{name}", level, keys, mods=mods, width=SIZE[0], height=SIZE[1])
    try:
        # (warm up: streaming, shaders; a battle until its fighters are all in)
        time.sleep(22.0 if per_team is not None else 8.0)
        out = _frames(inst, 6)
        out["renderer"] = inst.bridge.status().get("renderer")
        out.update(_zones(inst))
        if per_team == 12:
            out["threads"] = _threads(inst)
        if profile:
            out["profile"] = _profile(inst)
        out["alive"] = inst.alive()
        return out
    finally:
        inst.stop()


def _coop_host(ctx):
    """The host's frame with a guest in (both on this PC: they share its CPU and GPU)."""
    w, h = SIZE
    host = ctx.instance("perf_coop_host", width=w, height=h,
                        keys=dict(PLAYER_KEYS, frame_timers="1", frame_timers_cpu="0", renderer="vulkan",
                                  limit_fps_in_game="false", vsync="0"))
    try:
        host.bridge.call("host", level="load_campaign_level river_village_intro", campaign="com-wolfire-overgrowth-campaign")
        wait_loaded(host)
        guest = ctx.instance("perf_coop_guest", keys=dict(PLAYER_KEYS, limit_fps_in_game="false", vsync="0"))
        try:
            join(ctx, host, guest)
            time.sleep(8.0)
            out = _frames(host, 6)
            out.update(_zones(host))
            return out
        finally:
            guest.stop()
    finally:
        host.stop()


def _line(name, renderer, r):
    if not r.get("frames"):
        return f"  {name:12} {renderer:6}  no frames"
    line = (f"  {name:12} {renderer:6}  {r['frame_ms']:8.2f} ms  {r['fps']:7.1f} fps   1% low {r['low1_fps']:7.1f} fps"
            f"   worst {r['max_ms']:7.2f} ms")
    per = r.get("per") or {}
    if "char_update_ms" in per:
        line += (f"   per char update {per['char_update_ms']:.4f} ms, per script call {per.get('script_call_ms', 0):.4f} ms,"
                 f" step {per.get('step_ms', 0):.2f} ms ({per.get('characters', 0):.0f} chars)")
    return line


def perf_scenes(ctx):
    m, fails, table = {}, [], []
    for renderer in ("gl", "vulkan"):
        for name, level, per_team in LEVELS:
            r = _scene(ctx, name, renderer, level, per_team, profile=(name == "battle_96" and renderer == "vulkan"))
            m[f"{renderer}/{name}"] = r
            table.append(_line(name, renderer, r))
            if not r.get("frames"):
                fails.append(f"{renderer}/{name}: no frames timed")
            if not r.get("alive", True):
                fails.append(f"{renderer}/{name}: the game died")
            if r.get("job_violations"):
                fails.append(f"{renderer}/{name}: {r['job_violations']} calls inside jobs that must stay out (see the log)")
    r = _coop_host(ctx)
    m["vulkan/coop_host"] = r
    table.append(_line("coop_host", "vulkan", r))

    path = os.path.join(ctx.out_dir, "perf_scenes.json")
    json.dump(m, open(path, "w", encoding="utf-8"), indent=1)
    os.makedirs(REFS, exist_ok=True)
    shutil.copy(path, os.path.join(REFS, "perf_scenes_latest.json"))
    return {"passed": not fails, "metrics": {"table": table}, "notes": fails}


def perf_profile(ctx):
    r = _scene(ctx, "battle_96", "vulkan", None, 12, profile=True)
    path = os.path.join(ctx.out_dir, "perf_profile.json")
    json.dump(r, open(path, "w", encoding="utf-8"), indent=1)
    p = r.get("profile") or {}
    top = [f"{e['percent']:.1f}% {e['name']}" for e in (p.get("script_inclusive") or [])[:10]]
    return {"passed": bool(r.get("frames")) and bool(p.get("samples")),
            "metrics": {"frame": _line("battle_96", "vulkan", r), "per": r.get("per"), "threads": r.get("threads"), "samples": p.get("samples"),
                        "in_script_percent": p.get("in_script_percent"), "script_inclusive": top},
            "notes": [] if p.get("samples") else ["the profiler took no samples"]}
