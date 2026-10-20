"""A/B two engine builds side by side, no exe swapping: a snapshot is the
build's exe and DLLs copied to test/builds/<name> (the exe loads its DLLs from
its own folder; data comes from the repo as usual). "current" is Build/Release.

    python -I Tools/ogmp/ab.py snap <name>             snapshot Build/Release
    python -I Tools/ogmp/ab.py perf <a> <b> [runs=2]   frame-timer zones of the two fixed
                                                       scenes, builds alternating a b a b
    python -I Tools/ogmp/ab.py shots <a> <b> [key=value ...]
                                                       the fixed views of a level from both builds,
                                                       with these config keys, compared
    python -I Tools/ogmp/ab.py list
"""

import glob
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ogmp.instance import ROOT, TEST_DIR, EXE_NAME, user_game_running  # noqa: E402
from ogmp.scenarios import Context  # noqa: E402
from ogmp import render_suite  # noqa: E402

BUILDS = TEST_DIR + "/builds"
SCENES = [("slaver_path", "Data/Levels/og_story/02_Slaver_Camp.xml"), ("raider_camp", "Data/Levels/LugaruStory/Raider_camp.xml")]


def exe_of(name):
    if name == "current":
        return ROOT + "/Build/Release/" + EXE_NAME
    exe = f"{BUILDS}/{name}/{EXE_NAME}"
    if not os.path.exists(exe):
        sys.exit(f"no snapshot {name} (ab.py snap {name})")
    return exe


def snap(name):
    dst = f"{BUILDS}/{name}"
    os.makedirs(dst, exist_ok=True)
    src = ROOT + "/Build/Release"
    for f in glob.glob(src + "/*.dll") + [f"{src}/{EXE_NAME}", f"{src}/{EXE_NAME[:-4]}.pdb", f"{src}/steam_appid.txt"]:
        if os.path.exists(f):
            shutil.copy2(f, dst)
    print(f"{name}: {EXE_NAME} of {time.ctime(os.path.getmtime(src + '/' + EXE_NAME))}")


def zones_once(exe, out_dir):
    os.environ["OGMP_EXE"] = exe
    os.environ["OGMP_WORKDIR"] = ROOT
    ctx = Context(out_dir)
    res = {}
    try:
        for scene, level in SCENES:
            inst = render_suite._scene_instance(ctx, "ab_" + scene, level, {"frame_timers": "1"}, width=1920, height=1080)
            time.sleep(8.0)
            res[scene] = render_suite._sample(inst)
            inst.stop()
    finally:
        for inst in ctx.instances:
            try:
                inst.stop()
            except Exception:
                pass
        os.environ.pop("OGMP_EXE", None)
        os.environ.pop("OGMP_WORKDIR", None)
    return res


def perf(a, b, runs):
    if user_game_running():
        sys.exit("the user's game is running")
    out = f"{TEST_DIR}/results/{time.strftime('%Y%m%d_%H%M%S')}_ab_{a}_{b}"
    os.makedirs(out, exist_ok=True)
    got = {a: [], b: []}
    for r in range(runs):
        for name in (a, b):
            got[name].append(zones_once(exe_of(name), out))
    json.dump(got, open(out + "/zones.json", "w", encoding="utf-8"), indent=1)

    def mean(name, scene, path, key):
        vals = [run[scene]["zones"][path][key] for run in got[name] if path in run[scene]["zones"]]
        return sum(vals) / len(vals) if vals else None

    for scene, _ in SCENES:
        print(f"== {scene}   (ms, mean of {runs}: {a} -> {b})")
        for path, z in got[a][0][scene]["zones"].items():
            depth = path.count("/")
            if depth > 4 or (z["gpu_ms"] < 0.05 and z["cpu_ms"] < 0.05) or path.startswith("Graphics::SwapToScreen()"):
                continue
            cells = []
            for key in ("cpu_ms", "gpu_ms"):
                va, vb = mean(a, scene, path, key), mean(b, scene, path, key)
                if va is None or vb is None:
                    cells.append(f"{key[:3]} -")
                    continue
                pct = (vb - va) / va * 100.0 if va > 0.001 else 0.0
                cells.append(f"{key[:3]} {va:6.2f} -> {vb:6.2f} ({pct:+4.0f}%)")
            print(f"  {'  ' * depth}{path.split('/')[-1][:36]:36}  " + "   ".join(cells))
    print("->", out)


def shots_once(exe, out_dir, tag, keys, scene=SCENES[0]):
    os.environ["OGMP_EXE"] = exe
    os.environ["OGMP_WORKDIR"] = ROOT
    ctx = Context(out_dir)
    got = {}
    try:
        all_keys = {"test_start_paused": "1"}
        all_keys.update(keys)
        inst = render_suite._scene_instance(ctx, "ab_" + tag, scene[1], all_keys)
        time.sleep(6.0)
        me = inst.bridge.me()
        x, y, z = me["pos"]
        for view, cam, target in render_suite.VIEWS:
            inst.bridge.call("view", pos=[x + cam[0], y + cam[1], z + cam[2]], target=[x + target[0], y + target[1], z + target[2]], fov=70.0)
            time.sleep(1.5)
            shot = f"{out_dir}/{tag}_{view}.png"
            render_suite._shot(inst, shot)
            got[view] = shot
        inst.stop()
    finally:
        for inst in ctx.instances:
            try:
                inst.stop()
            except Exception:
                pass
        os.environ.pop("OGMP_EXE", None)
        os.environ.pop("OGMP_WORKDIR", None)
    return got


def shots(a, b, keys):
    if user_game_running():
        sys.exit("the user's game is running")
    out = f"{TEST_DIR}/results/{time.strftime('%Y%m%d_%H%M%S')}_abshots_{a}_{b}"
    os.makedirs(out, exist_ok=True)
    sa = shots_once(exe_of(a), out, "a_" + a, keys)
    sb = shots_once(exe_of(b), out, "b_" + b, keys)
    for view in sa:
        r = render_suite._compare(sa[view], sb[view], f"{out}/diff_{view}.png")
        print(f"{view:6} {r}")
    print("->", out)


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("snap", "perf", "shots", "list"):
        print(__doc__)
        return 2
    if args[0] == "snap" and len(args) == 2:
        snap(args[1])
    elif args[0] == "perf" and len(args) >= 3:
        perf(args[1], args[2], int(args[3]) if len(args) > 3 else 2)
    elif args[0] == "shots" and len(args) >= 3:
        shots(args[1], args[2], dict(kv.split("=", 1) for kv in args[3:]))
    elif args[0] == "list":
        for d in sorted(glob.glob(BUILDS + "/*")):
            exe = f"{d}/{EXE_NAME}"
            if os.path.exists(exe):
                print(f"{os.path.basename(d):20} {time.ctime(os.path.getmtime(exe))}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
