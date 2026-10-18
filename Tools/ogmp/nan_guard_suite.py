"""nan_guard: one machine's NaN stays on that machine.

A guard (the host's AI) beside the guest; for 3 s the guest's records of
its own character go out non-finite (what a physics blow-up there sends). No character or object on the
host may turn non-finite (bridge nan_scan), and the guard keeps running.

    python -I Tools/ogmp.py run nan_guard
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded
from .weapons_ping_suite import _char_c
from .combat_suite import _common


def nan_guard(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    b = _common(g, g.bridge.me()["id"])
    gp = _char_c(host, b)["pos"]
    guard = host.bridge.call("spawn", pos=[gp[0] + 1.0, gp[1] + 0.3, gp[2]], teams="guard")["object_id"]
    time.sleep(3.0)

    g.bridge.call("net_nan", seconds=3.0)  # its records go out non-finite (a blow-up there)
    scans = []
    for _ in range(8):
        time.sleep(1.0)
        s = host.bridge.call("nan_scan")
        scans.append([c["id"] for c in s["characters"]] + [o["id"] for o in s["objects"]])
    m["host_non_finite_ids_per_second"] = scans
    m["guest_non_finite"] = [c["id"] for c in g.bridge.call("nan_scan")["characters"]]
    if any(scans):
        fails.append(f"the guest's NaN reached the host: {scans[-1]}")
    if not host.alive():
        fails.append("the host crashed")
    m["guard_on_host"] = _char_c(host, guard) is not None
    return {"passed": not fails, "metrics": m, "notes": fails}
