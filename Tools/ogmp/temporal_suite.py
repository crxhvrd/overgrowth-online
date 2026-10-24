"""Temporal anti-aliasing and upscaler checks (Source/Graphics/temporal.h).

  taa_check   with config temporal_aa on (OGMP_KEYS adds more, e.g. the renderer):
              - static: a paused fixed view against the no-AA reference (OGMP_REFS):
                the same picture, edges smoothed (a low PSNR is a shifted image)
              - pan: the camera moved in steps, shot right after the last step
                and again settled at the same view: the motion is right when the
                two agree (wrong motion vectors smear: the first shot ghosts)
              - run: the player runs (unpaused) under a fixed camera, shots for
                looking at (characters write their own motion)
              - with temporal_aa_debug = 1 (OGMP_KEYS) the shots show the motion
  flare_check the sun's lens flare, the camera aimed at it: its occlusion query sees
              it whole (an upscaler draws the scene smaller: the flare must follow)
"""
import os
import time

from .render_suite import RENDER_SCENES, REFS, SHOT_SIZE, VIEWS, _compare, _scene_instance, _shot

SCENES = ("slaver_path", "raider_camp")
RUN_LEVEL = "Project60/4_red_hills.xml"  # (scenarios.LEVEL)


def taa_check(ctx, scenes=SCENES, mode="1"):
    m, fails = {}, []
    for scene, level in RENDER_SCENES:
        if scene not in scenes:
            continue
        inst = _scene_instance(ctx, "taa_" + scene, level, {"test_start_paused": "1", "temporal_aa": mode, "multisample": "1"})
        try:
            time.sleep(6.0)  # (streaming textures)
            me = inst.bridge.me()
            if me is None:
                fails.append(f"{scene}: no player character")
                continue
            x, y, z = me["pos"]
            r = m[scene] = {}

            # Static: converged under the jitter
            view, cam, target = VIEWS[0]
            inst.bridge.call("view", pos=[x + cam[0], y + cam[1], z + cam[2]], target=[x + target[0], y + target[1], z + target[2]], fov=70.0)
            time.sleep(1.5)
            shot = f"{ctx.out_dir}/{scene}_{view}_taa.png"
            _shot(inst, shot)
            ref = f"{REFS}/{scene}_{view}.png"
            if os.path.exists(ref):
                r["static_vs_noaa"] = _compare(shot, ref, f"{ctx.out_dir}/{scene}_{view}_taa_diff.png")
                if r["static_vs_noaa"]["psnr"] < 27.0:
                    fails.append(f"{scene}: static view differs from the no-AA one (psnr {r['static_vs_noaa']['psnr']})")

            # Pan: 30 steps sideways (and turning), as fast as the bridge goes
            steps = 30
            for i in range(steps + 1):
                t = i / steps
                inst.bridge.call("view", pos=[x + cam[0] - 3.0 * t, y + cam[1], z + cam[2] + 1.5 * t],
                                 target=[x + target[0] + 1.0 * t, y + target[1], z + target[2]], fov=70.0)
                time.sleep(0.02)
            moving = f"{ctx.out_dir}/{scene}_pan_moving.png"
            _shot(inst, moving)
            time.sleep(1.5)
            settled = f"{ctx.out_dir}/{scene}_pan_settled.png"
            _shot(inst, settled)
            r["pan_vs_settled"] = _compare(moving, settled, f"{ctx.out_dir}/{scene}_pan_diff.png")
            if r["pan_vs_settled"]["psnr"] < 30.0:
                fails.append(f"{scene}: just after the pan the frame differs from the settled one (psnr {r['pan_vs_settled']['psnr']})")

        finally:
            inst.stop()

    # Run: the game going, its own camera following the player running (every frame
    # moves: the camera's motion on the scene, the player's own on it). The story
    # levels open on a scene the player can't walk out of: the move test's level
    inst = _scene_instance(ctx, "taa_run", RUN_LEVEL, {"temporal_aa": mode, "multisample": "1"})
    try:
        time.sleep(4.0)
        start = inst.bridge.me()
        inst.bridge.hold(steps=900, up=1.0)
        time.sleep(1.0)
        for i in range(4):
            _shot(inst, f"{ctx.out_dir}/run_{i}.png")
            time.sleep(0.4)
        end = inst.bridge.me()
        if start and end:
            moved = m["run_moved_m"] = round(sum((a - b) ** 2 for a, b in zip(start["pos"], end["pos"])) ** 0.5, 2)
            if moved < 3.0:
                fails.append(f"run: the player moved {moved} m")
    finally:
        inst.stop()
    return {"passed": not fails, "metrics": m, "notes": fails}


def flare_check(ctx, mode="0"):
    """Paused at the run level's start, the camera at the player looking at the sun."""
    m, fails = {}, []
    inst = _scene_instance(ctx, "flare", RUN_LEVEL, {"test_start_paused": "1", "temporal_aa": mode, "multisample": "1"})
    try:
        time.sleep(3.0)
        me = inst.bridge.me()
        suns = [f for f in inst.bridge.call("flares")["flares"] if f[1] and f[0] > 0.0]
        if me is None or not suns:
            return {"passed": False, "metrics": {}, "notes": ["no player" if me is None else "no sun flare"]}
        x, y, z = me["pos"]
        d = suns[0][3:6]
        n = sum(c * c for c in d) ** 0.5
        d = [c / n for c in d]
        pos = [x, y + 1.5, z]
        inst.bridge.call("view", pos=pos, target=[pos[i] + d[i] * 10.0 for i in range(3)], fov=70.0)
        time.sleep(1.5)
        sun = [f for f in inst.bridge.call("flares")["flares"] if f[1] and f[0] > 0.0][0]
        m["sun_visible"] = round(sun[2], 3)
        _shot(inst, f"{ctx.out_dir}/flare_sun.png")
        if sun[2] < 0.5:
            fails.append(f"the sun's flare is {sun[2]:.2f} visible looking straight at it")
    finally:
        inst.stop()
    return {"passed": not fails, "metrics": m, "notes": fails}
