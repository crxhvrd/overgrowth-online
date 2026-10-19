"""splitscreen_join: a player with split screen on, and another player joins.

A tester's PC crashed the moment another player joined while split screen
was on. Split screen draws one viewport per player character, and online the
other players' characters count too. Host and guest both have split_screen
on; the guest joins. Both must keep running, with no GL problems and no NaN.

    python -I Tools/ogmp.py run splitscreen_join
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded


def splitscreen_join(ctx, profile="lan"):
    m, fails = {}, []
    keys = {"split_screen": "true"}
    host = ctx.instance("host", keys=keys)
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="session started")
    time.sleep(2.0)
    m["host_gl_before"] = host.bridge.call("gl_problems")["count"]
    ctx.shot(host, "alone")

    g = ctx.instance("client1", keys=keys)
    g.bridge.call("net_sim", **PROFILES[profile])
    try:
        join(ctx, host, g)
    except Exception as e:
        fails.append(f"join: {e}")
    for _ in range(10):
        time.sleep(1.0)
        if not (host.alive() and g.alive()):
            break

    m["host_alive"], m["guest_alive"] = host.alive(), g.alive()
    for inst, name in ((host, "host"), (g, "guest")):
        if not inst.alive():
            fails.append(f"the {name} died")
            continue
        gl = inst.bridge.call("gl_problems")
        m[f"{name}_gl"] = gl["count"]
        m[f"{name}_gl_list"] = gl.get("problems", [])[:5]
        nan = inst.bridge.call("nan_scan")
        m[f"{name}_nan_chars"] = len(nan.get("characters", []))
        ctx.shot(inst, "joined")
        if gl["count"] > m.get("host_gl_before", 0) and name == "host":
            fails.append("the host's GL debug output reports problems after the join")
        if name == "guest" and gl["count"] > 0:
            fails.append("the guest's GL debug output reports problems")
        if m[f"{name}_nan_chars"]:
            fails.append(f"NaN characters on the {name}")
    return {"passed": not fails, "metrics": m, "notes": fails}
