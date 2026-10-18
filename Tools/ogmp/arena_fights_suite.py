"""arena_fights: the online arena fought for real, looking for NaN.

Stucco Courtyard (the user's NaN in UpdateVision, 2026-10-18), the Overhaul
mod, bots, the randomizer, a guest joining mid-match. The bots fight each
other; the guest keeps picking up knives and throwing them at bots (stuck in
bodies, pulled out, rounds starting over them). Every two seconds every game
is scanned for non-finite transforms and bones (bridge nan_scan) and its log
for NaN lines; the first one found stops the run with what it was.

    python -I Tools/ogmp.py run arena_fights
"""

import os
import time

from .scenarios import PROFILES, join, script_errors, wait_loaded
from .arena_suite import CAMPAIGN, STUCCO, Log
from .weapons_ping_suite import _char_c, _where
from .combat_suite import _common, _face_and_go


def _nan_lines(inst):
    return [l for l in inst.log().splitlines() if "nan)" in l or "NaN in" in l]


def _knife_near(inst, me_pos):
    best, best_d = None, 1e9
    for it in inst.bridge.call("state").get("items", []):
        if it["held_by"] == -1 and "knife" in it.get("file", ""):
            d = sum((a - b) ** 2 for a, b in zip(it["pos"], me_pos)) ** 0.5
            if d < best_d:
                best, best_d = it, d
    return best


def arena_fights(ctx, profile="typical", seconds=240.0, fighters=6):
    from .online_mod_suite import OVERHAUL

    m, fails = {"nan": None}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        hlog = Log(host)
        host.bridge.call("host", level=STUCCO, campaign=CAMPAIGN)
        wait_loaded(host)
        host.bridge.call("exec", target="level",
                         code=f'SetConfigValueInt("oo_arena_bots", {fighters}); SetConfigValueBool("oo_arena_random", true);')
        time.sleep(20.0)  # the bots fight a while before anyone joins (as the user's session)
        g = ctx.instance("client1")
        g.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, g)
        t1 = time.time()
        while time.time() - t1 < 60.0 and g.bridge.me() is None:
            time.sleep(1.0)  # (a joiner's character comes with the next round)
        me = _common(g, g.bridge.me()["id"])
        machines = {"host": host, "guest": g}

        t0, last_scan, throws, picks = time.time(), 0.0, 0, 0
        while time.time() - t0 < seconds:
            # The guest: to the nearest loose knife, take it, throw it at the nearest bot
            try:
                c = _char_c(g, me)
                if c is not None and not c.get("knocked_out"):
                    held = [it for it in g.bridge.call("state").get("items", []) if it["held_by"] == c["id"]]
                    if held:
                        bots = [x for x in g.bridge.state() if x.get("common_id") != me and not x.get("knocked_out")]
                        if bots:
                            tgt = min(bots, key=lambda x: sum((a - b) ** 2 for a, b in zip(x["pos"], c["pos"])))
                            g.bridge.call("exec", target=c["id"],
                                          code=f"SetTargetID({tgt['id']}); going_to_throw_item = true; going_to_throw_item_time = time;")
                            throws += 1
                            time.sleep(0.8)
                    else:
                        k = _knife_near(g, c["pos"])
                        if k is not None and _face_and_go(g, c["id"], k["pos"], 0.6):
                            g.bridge.hold(steps=40, drop=1.0)
                            picks += 1
                else:
                    g.bridge.release_all()
            except Exception:
                pass
            time.sleep(0.25)

            if time.time() - last_scan > 2.0:
                last_scan = time.time()
                for name, inst in machines.items():
                    if not inst.alive():
                        fails.append(f"{name} crashed")
                        break
                    scan = inst.bridge.call("nan_scan")
                    lines = _nan_lines(inst)
                    if scan["objects"] or scan["characters"] or lines:
                        m["nan"] = {"on": name, "after_s": round(time.time() - t0), "scan": scan, "log": lines[:3]}
                        break
                if m["nan"] or fails:
                    break

        m["throws_tried"], m["pickups_tried"] = throws, picks
        m["fights"] = hlog.drain().count("ohl arena fight")
        if m["nan"]:
            fails.append(f"NaN on the {m['nan']['on']} after {m['nan']['after_s']} s")
        for name, inst in machines.items():
            errs = script_errors(inst) if inst.alive() else []
            if errs:
                fails.append(f"{name} script errors: " + "; ".join(f"{l} x{n}" for l, n in errs[:3]))
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old
    return {"passed": not fails, "metrics": m, "notes": fails}
