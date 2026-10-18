"""Weapons with guests on their own pings (sync_suite.GUESTS: ~75 and ~195 ms
round trips): a host, two guests and a guard the host runs.

    weapons_ping, one knife through every state:
      1. picked up by guest 2 (bad line)
      2. thrown by guest 2 into the guard (stuck in an AI)
      3. the guard killed with it in: it rides the body
      4. pulled out of the body by guest 1
      5. thrown by guest 1 into guest 2 (stuck in a player, through the host)
      6. pulled out of guest 2's body by the host's player
      7. dropped by the host's player (on the ground)
      8. picked up from the ground by guest 1, dropped by a knockout
    Whatever each step does (an aim can miss), every machine must agree:
    who holds it, whom it is stuck in, and where it is.

    python -I Tools/ogmp.py run weapons_ping
"""

import math
import time

from .combat_suite import _common, _face_and_go, _local
from .scenarios import script_errors
from .sync_suite import ping_session

NAMES = ["host", "guest1", "guest2"]


def _char_c(inst, cid):
    """A character by its common id."""
    return next((c for c in inst.bridge.state() if c.get("common_id") == cid), None)


def _where(inst, knife):
    """(holder, stuck in, position) of the knife here, characters by common id."""
    st = inst.bridge.call("state")
    common = {c["id"]: c.get("common_id") for c in st["characters"]}
    it = next((i for i in st.get("items", []) if i["common_id"] == knife), None)
    if it is None:
        return None
    h, s = it["held_by"], it["stuck_in"]
    return [-1 if h == -1 else common.get(h, f"?{h}"), -1 if s == -1 else common.get(s, f"?{s}"), it["pos"]]


def _agree(machines, knife, settle=2.5):
    """Every machine's view of the knife; the faults (empty = they agree)."""
    time.sleep(settle)
    views = [_where(inst, knife) for inst in machines]
    faults = []
    if any(v is None for v in views):
        return views, ["the knife is missing on " + ", ".join(NAMES[i] for i, v in enumerate(views) if v is None)]
    if len({v[0] for v in views}) > 1:
        faults.append("holder differs: " + ", ".join(f"{NAMES[i]} {v[0]}" for i, v in enumerate(views)))
    if len({v[1] for v in views}) > 1:
        faults.append("stuck in differs: " + ", ".join(f"{NAMES[i]} {v[1]}" for i, v in enumerate(views)))
    if views[0][0] != -1:  # held: where on its holder (each machine shows the body at its own moment)
        for i, inst in enumerate(machines):
            c = _char_c(inst, views[i][0])
            if c is not None:
                views[i][2] = [views[i][2][k] - c["pos"][k] for k in range(3)]
    spread = max(math.dist(a[2], b[2]) for a in views for b in views)
    limit = 0.35 if views[0][0] == -1 and views[0][1] == -1 else 0.6  # free: the host's; else on a (streamed) body
    if spread > limit:
        faults.append(f"positions {spread:.2f} m apart: " + ", ".join(f"{NAMES[i]} {[round(x, 2) for x in v[2]]}" for i, v in enumerate(views)))
    return [[v[0], v[1], [round(x, 2) for x in v[2]]] for v in views], faults


def _face(inst, me, pos):
    c = _char_c(inst, me)
    dx, dz = pos[0] - c["pos"][0], pos[2] - c["pos"][2]
    yaw = math.degrees(math.atan2(-dx, -dz))
    inst.bridge.call("exec", target=c["id"], code=f"target_rotation = {yaw:.1f}; cam_rotation = {yaw:.1f};")
    return math.hypot(dx, dz)


def _pick_up(inst, me, knife, seconds=10.0):
    """Walk to the knife (on the ground or in a body), hold the item key in reach."""
    t0 = time.time()
    while time.time() - t0 < seconds:
        w = _where(inst, knife)
        if w is None or w[0] == me:
            break
        if _face_and_go(inst, _char_c(inst, me)["id"], w[2], 0.6):
            inst.bridge.hold(steps=-1, drop=1.0)
            t1 = time.time()
            while time.time() - t1 < 1.5 and (_where(inst, knife) or [None])[0] != me:
                time.sleep(0.1)
            inst.bridge.release_all()
            time.sleep(0.3)
    inst.bridge.release_all()
    w = _where(inst, knife)
    return w is not None and w[0] == me


def _force_stick(host, knife, cid, holder_inst=None, holder=None):
    """A throw that missed or never left the hand: let go of it (its owner's
    machine), then stick it in as a hit does (the host's path)."""
    if holder_inst is not None and (_where(holder_inst, knife) or [None])[0] == holder:
        holder_inst.bridge.call("exec", target=_char_c(holder_inst, holder)["id"], code="DropWeapon();")
        time.sleep(2.0)
    w = _where(host, knife)
    if w is None or w[0] != -1 or w[1] != -1:
        return False
    return host.bridge.call("stick_item", item=knife, char=_char_c(host, cid)["id"]).get("stuck", False)


def _throw_at(inst, me, target, knife, seconds=8.0):
    """From 3-6 m, facing the target, tap the item key (armed: a throw)."""
    t0 = time.time()
    while time.time() - t0 < seconds:
        tgt = _char_c(inst, target)
        if tgt is None:
            break
        d = _face(inst, me, tgt["pos"])
        if d < 3.0:
            inst.bridge.hold(down=1.0)
        elif d > 6.0:
            inst.bridge.hold(up=1.0)
        else:
            inst.bridge.release_all()
            time.sleep(0.2)
            inst.bridge.call("exec", target=_char_c(inst, me)["id"],
                             code=f"SetTargetID({tgt['id']}); going_to_throw_item = true; going_to_throw_item_time = time;")
            time.sleep(0.8)
            w = _where(inst, knife)
            if w is not None and w[0] != me:
                inst.bridge.release_all()
                return True
        time.sleep(0.05)
    inst.bridge.release_all()
    return False


def _place(host, cid, near, dx, dz):
    """Put a player's character beside a point (the co-op placing path: its owner moves it)."""
    lid = _char_c(host, cid)["id"]
    host.bridge.call("exec", target="level", code=f"Online_PlaceAvatar({lid}, vec3({near[0] + dx:.2f}, {near[1] + 0.2:.2f}, {near[2] + dz:.2f}), vec3({-dx:.2f}, 0.0f, {-dz:.2f}));")


def weapons_ping(ctx):
    m, notes, fails = {"steps": {}}, [], []
    host, guests = ping_session(ctx)
    g1, g2 = guests
    machines = [host, g1, g2]
    h = host.bridge.me()["id"]
    a = _common(g1, g1.bridge.me()["id"])
    b = _common(g2, g2.bridge.me()["id"])
    time.sleep(4.0)

    # Everyone on their own team; the host's player on the guard's (it leaves the host alone)
    for inst in machines:
        for cid, team in ((h, "guard"), (a, "team_a"), (b, "team_b")):
            c = _char_c(inst, cid)
            inst.bridge.call("exec", target=c["id"], code=f'ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "{team}");')

    def step(name, ok_note, views_faults):
        views, faults = views_faults
        m["steps"][name] = {"did": ok_note, "views": views}
        for f in faults:
            fails.append(f"{name}: {f}")

    # 1. A knife at guest 2's feet; guest 2 picks it up
    p2 = _char_c(host, b)["pos"]
    knife = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[p2[0] + 0.6, p2[1] + 0.5, p2[2]])["object_id"]
    t0 = time.time()
    while time.time() - t0 < 10.0 and not all(_where(i, knife) for i in machines):
        time.sleep(0.25)
    time.sleep(1.5)
    got = _pick_up(g2, b, knife)
    step("1 guest2 picks up", got, _agree(machines, knife))

    # 2. A guard 4.5 m in front of guest 2 that only stands; guest 2 throws
    p2 = _char_c(host, b)["pos"]
    guard = host.bridge.call("spawn", pos=[p2[0] + 4.5, p2[1] + 0.3, p2[2]], teams="guard")["object_id"]
    host.bridge.call("exec", target=guard, code="combat_allowed = false; p_block_skill = 0.0f;")
    t0 = time.time()
    while time.time() - t0 < 10.0 and not all(_char_c(i, guard) for i in guests):
        time.sleep(0.25)
    time.sleep(1.5)
    threw = _throw_at(g2, b, guard, knife) if got else False
    step("2 guest2 throws at the guard", threw, _agree(machines, knife, 3.0))
    stuck_in_guard = m["steps"]["2 guest2 throws at the guard"]["views"][0][1] == guard
    if not stuck_in_guard:
        stuck_in_guard = _force_stick(host, knife, guard, g2, b)
        step("2b (missed) stuck in the guard by the host", stuck_in_guard, _agree(machines, knife, 2.0))

    # 3. The guard dies with it in
    host.bridge.call("exec", target=guard, code="ko_shield = 0; TakeDamage(2.0f);")
    step("3 guard killed", stuck_in_guard, _agree(machines, knife, 3.5))

    # 4. Guest 1 pulls it out of the body (or picks it up where it fell)
    gp = _char_c(host, guard)["pos"]
    _place(host, a, gp, 1.5, 0.0)
    time.sleep(2.0)
    got1 = _pick_up(g1, a, knife, 10.0)
    step("4 guest1 takes it from the body", got1, _agree(machines, knife))

    # 5. Guest 1 throws it into guest 2 (guest 2 is placed 4.5 m away, still)
    p1 = _char_c(host, a)["pos"]
    _place(host, b, p1, 0.0, 4.5)
    time.sleep(2.0)
    threw1 = _throw_at(g1, a, b, knife) if got1 else False
    step("5 guest1 throws at guest2", threw1, _agree(machines, knife, 3.0))
    v5 = m["steps"]["5 guest1 throws at guest2"]["views"]
    stuck_in_b = v5[0][1] == b
    if not stuck_in_b:
        stuck_in_b = _force_stick(host, knife, b, g1, a)
        step("5b (missed) stuck in guest2 by the host", stuck_in_b, _agree(machines, knife, 2.0))

    # 6. The host's player pulls it out of guest 2 (down, so a body to take it from)
    g2.bridge.call("exec", target=_char_c(g2, b)["id"], code="ko_shield = 0; TakeDamage(2.0f);")
    time.sleep(1.5)
    bp = _char_c(host, b)["pos"]
    _place(host, h, bp, 1.5, 0.0)
    time.sleep(2.0)
    goth = _pick_up(host, h, knife, 10.0)
    step("6 host takes it from guest2", [stuck_in_b, goth], _agree(machines, knife))

    # 7. The host's player drops it
    if goth:
        host.bridge.call("exec", target=h, code="DropWeapon();")
    step("7 host drops it", goth, _agree(machines, knife, 3.0))

    # 8. Guest 1 picks it up from the ground, then is knocked out holding it
    kp = _where(host, knife)[2]
    _place(host, a, kp, 1.2, 0.0)
    time.sleep(2.0)
    got2 = _pick_up(g1, a, knife)
    if got2:
        g1.bridge.call("exec", target=_char_c(g1, a)["id"], code="DropWeapon(); ko_shield = 0; TakeDamage(2.0f);")
    step("8 guest1 picks up, then drops it knocked out", got2, _agree(machines, knife, 4.0))

    # 9. Throws at a moving player: guest 2 strafes, so every machine sees it
    # somewhere else when the knife arrives (the host's flight decides)
    p1 = _char_c(host, a)["pos"]
    _place(host, h, p1, 15.0, 0.0)  # the host's player out of the way
    for i in range(4):
        g1.bridge.call("exec", target=_char_c(g1, a)["id"], code="Recover();")  # (knocked out in 8)
        time.sleep(1.5)
        p1 = _char_c(host, a)["pos"]
        k = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[p1[0] + 0.5, p1[1] + 0.5, p1[2]])["object_id"]
        t0 = time.time()
        while time.time() - t0 < 8.0 and not all(_where(x, k) for x in machines):
            time.sleep(0.25)
        time.sleep(1.0)
        got = _pick_up(g1, a, k)
        _place(host, b, _char_c(host, a)["pos"], 0.0, 4.5)
        time.sleep(1.5)
        g2.bridge.hold(steps=-1, **{("right" if i % 2 else "left"): 1.0})
        threw = _throw_at(g1, a, b, k, 5.0) if got else False
        time.sleep(0.4)
        g2.bridge.release_all()
        step(f"9.{i} guest1 throws at guest2 running", [got, threw], _agree(machines, k, 3.0))
        if got and not threw:
            g1.bridge.call("exec", target=_char_c(g1, a)["id"], code="DropWeapon();")
        g2.bridge.call("exec", target=_char_c(g2, b)["id"], code="Recover();")

    m["did"] = {k: v["did"] for k, v in m["steps"].items()}
    errs = sum((script_errors(inst) for inst in machines), [])
    notes += [f"script error: {line} x{n}" for line, n in errs[:8]]
    if errs:
        fails.append("script errors")
    for i, inst in enumerate(machines):
        if not inst.alive():
            fails.append(f"{NAMES[i]} crashed")
    return {"passed": not fails, "metrics": m, "notes": fails + notes}
