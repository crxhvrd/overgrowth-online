"""A guest holds a knife the host deletes while a guard (the host's AI) fights
that guest: the host's view of the guest's hand names an item that is gone
there. The host must carry on (no "There is no item object").

    python -I Tools/ogmp.py run held_delete
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded
from .weapons_ping_suite import _char_c, _pick_up
from .combat_suite import _common


def held_delete(ctx):
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES["typical"])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES["typical"])
    join(ctx, host, g)
    time.sleep(3.0)
    b = _common(g, g.bridge.me()["id"])
    for inst in (host, g):
        inst.bridge.call("exec", target=_char_c(inst, b)["id"], code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "team_b");')
        inst.bridge.call("exec", target=_char_c(inst, host.bridge.me()["id"])["id"],
                         code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "guard");')
    gp = _char_c(host, b)["pos"]
    host.bridge.call("spawn", pos=[gp[0] + 2.5, gp[1] + 0.3, gp[2]], teams="guard")
    time.sleep(3.0)
    rounds = []
    for i in range(4):
        gp = _char_c(host, b)["pos"]
        k = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[gp[0] + 0.5, gp[1] + 0.5, gp[2]])["object_id"]
        time.sleep(2.0)
        got = _pick_up(g, b, k)
        time.sleep(0.5)
        host.bridge.call("exec", target="level", code=f"QueueDeleteObjectID({k});")
        time.sleep(2.0)
        alive = host.alive()
        rounds.append([got, alive])
        if not alive:
            break
        g.bridge.call("exec", target=_char_c(g, b)["id"], code="Recover();")
    log = open(host.writedir + "logfile.txt", encoding="utf-8", errors="replace").read()
    m = {"rounds_picked_alive": rounds, "no_item_object_lines": log.count("no item object")}
    fails = []
    if m["no_item_object_lines"] or not all(r[1] for r in rounds):
        fails.append("the host failed on a deleted held item")
    if not any(r[0] for r in rounds):
        fails.append("the guest never picked a knife up")
    return {"passed": not fails, "metrics": m, "notes": fails}


