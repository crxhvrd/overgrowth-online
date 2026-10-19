"""item_flares: a guest's sky has one sun. Every item makes a glint flare that
starts as a "distant" one at full strength (a sun) until the item's update
hides it; a client skipped that update (held, stuck, and every loose item:
the host runs those), so each item left a sun in the sky (a user's
screenshot, 2026-10-20). The host lays out knives, the guest takes one: the
guest's distant flares that show must be the sun alone.

    python -I Tools/ogmp.py run item_flares
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded
from .weapons_ping_suite import _pick_up
from .combat_suite import _common


def _suns(inst):
    return sum(1 for v, distant in inst.bridge.call("flares")["flares"] if distant and v > 0.0)


def item_flares(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    gme = g.bridge.me()
    b = _common(g, gme["id"])
    p = gme["pos"]
    knives = [host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[p[0] + 1.0 + i, p[1] + 0.5, p[2] + 1.0])["object_id"] for i in range(4)]
    time.sleep(3.0)
    m["picked"] = _pick_up(g, b, knives[0])
    time.sleep(3.0)
    m["suns_host"] = [_suns(host) for _ in range(5)]
    m["suns_guest"] = [_suns(g) for _ in range(5)]
    m["flares_guest"] = len(g.bridge.call("flares")["flares"])
    if max(m["suns_guest"]) > 1:
        fails.append(f"the guest's sky shows {max(m['suns_guest'])} suns (items' glints left on)")
    if max(m["suns_host"]) > 1:
        fails.append(f"the host's sky shows {max(m['suns_host'])} suns")
    return {"passed": not fails, "metrics": m, "notes": fails}
