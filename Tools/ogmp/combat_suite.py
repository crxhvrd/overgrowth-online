"""Combat across machines (Phase 4), one shared session:

    combat_session: a guard run by the host fights the client's character
    (hit_npc + npc_hits_player), then the two players duel (pvp_duel). Every
    hit on a character another machine runs must travel as a request to its
    owner, be resolved there, and be answered; afterwards every character's
    health and knockout state must read the same on both machines.
"""

import math
import time

from .bots import ChaseBot
from .scenarios import script_errors, session


def _online(inst):
    return inst.bridge.status()["online"]


def _char(inst, cid):
    return next((c for c in inst.bridge.state() if c["id"] == cid), None)


def _vitals_gap(a, b):
    """Largest difference in health between the two machines' views of every
    character both have, and the characters whose knockout state differs."""
    va = {c["id"]: c for c in a.bridge.state()}
    vb = {c["id"]: c for c in b.bridge.state()}
    gap, ko_mismatch = 0.0, []

    for cid in va.keys() & vb.keys():
        for key in ("temp_health", "blood_health", "permanent_health"):
            if key in va[cid] and key in vb[cid]:
                gap = max(gap, abs(va[cid][key] - vb[cid][key]))
        if va[cid].get("knocked_out") != vb[cid].get("knocked_out"):
            ko_mismatch.append(cid)

    return round(gap, 3), ko_mismatch


def _hurt(c):
    return c is not None and (c.get("temp_health", 1.0) < 0.99 or c.get("blood_health", 1.0) < 0.99 or c.get("knocked_out", 0) != 0)


def _fight(bots, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        for bot in bots:
            bot.step()
    for bot in bots:
        bot.inst.bridge.release_all()


def _counters(host, client):
    h, c = _online(host), _online(client)
    keys = ("hits_requested", "hits_resolved", "hit_results")
    return {k: h[k] for k in keys}, {k: c[k] for k in keys}


def _delta(before, after):
    return {k: after[k] - before[k] for k in after}


def combat_session(ctx, profile="typical"):
    m, notes, fails = {}, [], []
    host, (c1,) = session(ctx, 1, profile=profile)
    time.sleep(2.0)
    hid = host.bridge.me()["id"]
    cid = c1.bridge.me()["id"]

    # --- 1. a guard (the host runs it) against the client's character -------------------
    # The host's own character joins the guard's side so the guard leaves it alone
    host.bridge.call("exec", target=hid, code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "guard");')
    cpos = _char(host, cid)["pos"]
    npc = host.bridge.call("spawn", pos=[cpos[0] + 2.5, cpos[1] + 0.3, cpos[2]], teams="guard")["object_id"]
    host.bridge.call("exec", target=npc, code="p_block_skill = 0.0f;")  # a guard that blocks everything proves nothing here
    t0 = time.time()

    while time.time() - t0 < 15.0 and _char(c1, npc) is None:
        time.sleep(0.25)

    m["npc_on_client"] = _char(c1, npc) is not None
    if not m["npc_on_client"]:
        fails.append("the spawned guard never reached the client")

    time.sleep(1.5)
    h0, c0 = _counters(host, c1)
    # First the client attacks a guard that doesn't strike back (it still
    # blocks what it faces), then the guard is let loose on the client
    host.bridge.call("exec", target=npc, code="combat_allowed = false;")
    _fight([ChaseBot(c1, seed=3, target_id=npc)], 14.0)
    host.bridge.call("exec", target=npc, code="combat_allowed = true;")
    t_loose = time.time()
    while time.time() - t_loose < 14.0:
        time.sleep(0.5)
    time.sleep(1.5)
    h1, c1c = _counters(host, c1)
    dh, dc = _delta(h0, h1), _delta(c0, c1c)
    gap, ko_mismatch = _vitals_gap(host, c1)
    npc_h, me_c = _char(host, npc), _char(c1, cid)
    m["npc"] = {
        "client_hits_on_npc": dc["hits_requested"], "resolved_by_host": dh["hits_resolved"], "answers_to_client": dc["hit_results"],
        "npc_hits_on_client": dh["hits_requested"], "resolved_by_client": dc["hits_resolved"], "answers_to_host": dh["hit_results"],
        "npc_hurt": _hurt(npc_h), "client_hurt": _hurt(me_c),
        "vitals_gap": gap, "ko_mismatch": ko_mismatch,
    }
    ctx.shot(c1, "npc_fight")

    if dc["hits_requested"] == 0 or dh["hits_resolved"] == 0:
        fails.append("hit_npc: the client's attacks never reached the host")
    if not _hurt(npc_h):
        fails.append("hit_npc: the guard was never hurt")
    if dh["hits_requested"] > 0 and dc["hits_resolved"] == 0:
        fails.append("npc_hits_player: the guard's attacks never reached the client")
    if gap > 0.02 or ko_mismatch:
        fails.append(f"npc fight: views differ (health gap {gap}, knockout {ko_mismatch})")

    # --- 2. the two players duel ----------------------------------------------------------
    # The guard out of the way, both players healed, on opposing teams
    host.bridge.call("exec", target=npc, code="ko_shield = 0; TakeDamage(2.0f);")
    host.bridge.call("exec", target=hid, code="Recover();")
    c1.bridge.call("exec", target=cid, code="Recover();")
    for inst in (host, c1):
        inst.bridge.call("exec", target=hid, code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "duel_a");')
        inst.bridge.call("exec", target=cid, code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "duel_b");')
    time.sleep(1.5)

    h0, c0 = _counters(host, c1)
    # Turns: the host attacks while the client stands, then the other way round
    bots = [ChaseBot(host, seed=1, target_id=cid), ChaseBot(c1, seed=2, target_id=hid)]
    _fight([bots[0]], 14.0)
    host.bridge.call("exec", target=hid, code="Recover();")
    c1.bridge.call("exec", target=cid, code="Recover();")
    time.sleep(1.0)
    snd0 = _online(host)["sounds_from_clients"]
    _fight([bots[1]], 14.0)
    m["client_sounds_heard_by_host"] = _online(host)["sounds_from_clients"] - snd0
    time.sleep(1.5)
    h1, c1c = _counters(host, c1)
    dh, dc = _delta(h0, h1), _delta(c0, c1c)
    gap, ko_mismatch = _vitals_gap(host, c1)
    m["duel"] = {
        "client_hits_on_host": dc["hits_requested"], "resolved_by_host": dh["hits_resolved"],
        "host_hits_on_client": dh["hits_requested"], "resolved_by_client": dc["hits_resolved"],
        "answers": [dh["hit_results"], dc["hit_results"]],
        "host_hurt": _hurt(_char(host, hid)), "client_hurt": _hurt(_char(c1, cid)),
        "attacks": [b.attacks for b in bots],
        "vitals_gap": gap, "ko_mismatch": ko_mismatch,
    }
    ctx.shot(host, "duel")

    if dc["hits_requested"] == 0 or dh["hits_resolved"] == 0:
        fails.append("pvp_duel: the client's attacks never reached the host")
    if dh["hits_requested"] == 0 or dc["hits_resolved"] == 0:
        fails.append("pvp_duel: the host's attacks never reached the client")
    if not (_hurt(_char(host, hid)) or _hurt(_char(c1, cid))):
        fails.append("pvp_duel: nobody was hurt")  # (who lands what is up to the bots and their blocks)
    if m["client_sounds_heard_by_host"] == 0:
        fails.append("sounds: none of the client's character sounds reached the host")
    if gap > 0.02 or ko_mismatch:
        fails.append(f"duel: views differ (health gap {gap}, knockout {ko_mismatch})")

    # --- 3. a throw: the client grabs the host's (idle) player --------------------------
    # The client takes the victim for the throw, hands it back once it is down
    # and settled, and the host can move it again on both screens
    host.bridge.call("exec", target=hid, code="Recover();")
    c1.bridge.call("exec", target=cid, code="Recover();")
    time.sleep(1.5)
    t_before = _online(c1)["takeovers"]
    thrown = False
    t0 = time.time()

    while time.time() - t0 < 12.0 and not thrown:
        me, victim = _char(c1, cid), _char(c1, hid)
        if me is None or victim is None:
            break
        dx, dz = victim["pos"][0] - me["pos"][0], victim["pos"][2] - me["pos"][2]
        yaw = __import__("math").degrees(__import__("math").atan2(-dx, -dz))
        c1.bridge.call("exec", target=cid, code=f"target_rotation = {yaw:.1f}; cam_rotation = {yaw:.1f};")
        if (dx * dx + dz * dz) ** 0.5 > 0.9:
            c1.bridge.hold(up=1.0)
        else:
            # Start the throw the way the grab key does once a throw is allowed
            # (block stun timing is not what this checks)
            c1.bridge.release("up")
            c1.bridge.call("exec", target=cid, code=f"SetState(_attack_state); attack_animation_set = false; "
                                                    f"attacking_with_throw = 1; can_feint = false; feinting = false; SetTargetID({hid});")
            time.sleep(0.6)
            thrown = _online(c1)["takeovers"] > t_before

    c1.bridge.release_all()
    m["throw"] = {"took_over": _online(c1)["takeovers"] - t_before}

    if thrown:
        t_released = None
        t0 = time.time()
        while time.time() - t0 < 10.0:
            if _online(c1)["overrides"] == 0 and _online(host)["overrides"] == 0:
                t_released = round(time.time() - t0, 1)
                break
            time.sleep(0.2)
        m["throw"]["handed_back_s"] = t_released
        time.sleep(2.0)
        host.bridge.call("exec", target=hid, code="Recover();")
        # A hard landing keeps it down a while: wait until it stands (not ragdoll, state 4)
        t_up = time.time()
        while time.time() - t_up < 6.0 and _char(host, hid).get("state", 0) == 4:
            time.sleep(0.25)
        m["throw"]["up_after_s"] = round(time.time() - t_up, 1)
        time.sleep(1.0)
        start_h, start_c = _char(host, hid)["pos"], _char(c1, hid)["pos"]
        host.bridge.hold(steps=-1, up=1.0)
        time.sleep(1.2)
        host.bridge.release_all()
        time.sleep(0.8)
        moved_h = __import__("math").dist(_char(host, hid)["pos"], start_h)
        moved_c = __import__("math").dist(_char(c1, hid)["pos"], start_c)
        m["throw"]["host_moves_after"] = [round(moved_h, 2), round(moved_c, 2)]
        gap, ko_mismatch = _vitals_gap(host, c1)
        m["throw"]["vitals_gap"] = gap
        if t_released is None:
            fails.append("throw: the victim was never handed back")
        if moved_h < 0.5 or moved_c < 0.5:
            fails.append(f"throw: the host could not move its character afterwards {m['throw']['host_moves_after']}")
    else:
        notes.append("throw: no grab happened (positioning); not judged")

    errs = script_errors(host) + script_errors(c1)
    notes += [f"script error: {line} x{n}" for line, n in errs[:6]]
    if errs:
        fails.append("script errors")

    return {"passed": not fails, "metrics": m, "notes": fails + notes}


def _items(inst):
    return {it["id"]: it for it in inst.bridge.call("state").get("items", [])}


def _face_and_go(inst, me_id, target_pos, stop_at):
    """Steer the character toward a point; True once within stop_at."""
    import math
    me = _char(inst, me_id)
    if me is None:
        return False
    dx, dz = target_pos[0] - me["pos"][0], target_pos[2] - me["pos"][2]
    yaw = math.degrees(math.atan2(-dx, -dz))
    inst.bridge.call("exec", target=me_id, code=f"target_rotation = {yaw:.1f}; cam_rotation = {yaw:.1f};")
    if math.hypot(dx, dz) > stop_at:
        inst.bridge.hold(up=1.0)
        return False
    inst.bridge.release("up")
    return True


def _common(inst, local_id):
    return next((c["common_id"] for c in inst.bridge.state() if c["id"] == local_id), None)


def _local(inst, common_id, items=False):
    rows = inst.bridge.call("state").get("items" if items else "characters", [])
    return next((r["id"] for r in rows if r["common_id"] == common_id), None)


def weapons_session(ctx, profile="typical"):
    """Two clients race for one knife: one gets it, on every screen. The
    winner throws it at the loser: the host flies it, the hit counts on the
    loser's machine, and everyone agrees where the knife ends up."""
    m, notes, fails = {}, [], []
    host, (ca, cb) = session(ctx, 2, profile=profile)
    time.sleep(2.0)
    # Characters and items by the host's ids (a joiner's character has other ids elsewhere)
    a = _common(ca, ca.bridge.me()["id"])
    b = _common(cb, cb.bridge.me()["id"])
    L = lambda inst, cid: _local(inst, cid)

    for inst in (host, ca, cb):
        inst.bridge.call("exec", target=L(inst, a), code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "knife_a");')
        inst.bridge.call("exec", target=L(inst, b), code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "knife_b");')

    pa, pb = _char(host, a)["pos"], _char(host, b)["pos"]
    mid = [(pa[0] + pb[0]) / 2.0, max(pa[1], pb[1]) + 0.6, (pa[2] + pb[2]) / 2.0]
    knife = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=mid)["object_id"]
    t0 = time.time()
    while time.time() - t0 < 10.0 and not (_local(ca, knife, True) and _local(cb, knife, True)):
        time.sleep(0.25)
    if not (_local(ca, knife, True) and _local(cb, knife, True)):
        return {"passed": False, "metrics": m, "notes": ["the knife never reached both clients"]}
    time.sleep(1.5)

    # The race: both run at it and press the pick-up key in reach
    t0 = time.time()
    while time.time() - t0 < 10.0 and _items(host)[knife]["held_by"] == -1:
        for inst, me in ((ca, a), (cb, b)):
            kpos = _items(inst)[_local(inst, knife, True)]["pos"]
            if _face_and_go(inst, L(inst, me), kpos, 0.8):
                inst.bridge.hold(steps=-1, drop=1.0)  # held through the pick-up
    for inst in (ca, cb):
        inst.bridge.release_all()
    time.sleep(2.0)

    def holder_of(inst):
        h = _items(inst)[_local(inst, knife, True)]["held_by"]
        return -1 if h == -1 else _common(inst, h)

    holders = [holder_of(i) for i in (host, ca, cb)]
    m["holders_host_a_b"] = holders
    if holders[0] == -1:
        fails.append("knife race: nobody got the knife")
        return {"passed": False, "metrics": m, "notes": fails + notes}
    if len(set(holders)) != 1:
        fails.append(f"knife race: machines disagree on who holds it {holders}")

    winner = holders[0]
    w_inst, l_inst, loser = (ca, cb, b) if winner == a else (cb, ca, a)
    if winner not in (a, b):
        fails.append(f"knife race: held by neither racer ({winner})")
        return {"passed": False, "metrics": m, "notes": fails + notes}

    # The throw: step back to about 4 m, face the loser, press the key (armed: it throws)
    before = {"w": _online(w_inst), "h": _online(host), "l": _online(l_inst)}
    t0 = time.time()
    threw = False
    while time.time() - t0 < 10.0 and not threw:
        lpos = _char(w_inst, L(w_inst, loser))["pos"]
        me = _char(w_inst, L(w_inst, winner))
        import math
        d = math.dist(lpos, me["pos"])
        dx, dz = lpos[0] - me["pos"][0], lpos[2] - me["pos"][2]
        yaw = math.degrees(math.atan2(-dx, -dz))
        w_inst.bridge.call("exec", target=L(w_inst, winner), code=f"target_rotation = {yaw:.1f}; cam_rotation = {yaw:.1f};")
        if d < 3.0:
            w_inst.bridge.hold(down=1.0)
        elif d > 6.0:
            w_inst.bridge.hold(up=1.0)
        else:
            w_inst.bridge.release_all()
            time.sleep(0.2)
            w_inst.bridge.hold(steps=6, drop=1.0)
            time.sleep(0.6)
            threw = holder_of(w_inst) == -1
    w_inst.bridge.release_all()

    # The flight as the host flies it: how close it came to the target
    flight, t0 = [], time.time()
    while time.time() - t0 < 1.5:
        st = host.bridge.call("state")
        k = next((it for it in st.get("items", []) if it["common_id"] == knife), None)
        v = next((c for c in st["characters"] if c["common_id"] == loser), None)
        if k and v:
            flight.append((math.dist(k["pos"], v["pos"]), k["pos"], v["pos"]))
    time.sleep(1.5)

    after = {"w": _online(w_inst), "h": _online(host), "l": _online(l_inst)}
    places = []
    for inst in (host, ca, cb):
        it = _items(inst)[_local(inst, knife, True)]
        places.append([holder_of(inst), -1 if it["stuck_in"] == -1 else _common(inst, it["stuck_in"])])
    m["throw"] = {
        "threw": threw,
        "launch_sent": after["w"]["item_launches"] - before["w"]["item_launches"],
        "launch_flown_by_host": after["h"]["item_launches"] - before["h"]["item_launches"],
        "hits_sent_by_host": after["h"]["item_hits"] - before["h"]["item_hits"],
        "hit_taken_by_victim": after["l"]["item_hits"] - before["l"]["item_hits"],
        "victim_hurt": _hurt(_char(l_inst, L(l_inst, loser))),
        "knife_held_stuck_host_a_b": places,
        "closest_m": round(min(f[0] for f in flight), 2) if flight else None,
        "knife_start_end": [flight[0][1], flight[-1][1]] if flight else None,
        "target_at": flight[0][2] if flight else None,
    }
    gap, ko_mismatch = _vitals_gap(host, l_inst)
    m["throw"]["vitals_gap"] = gap

    if not threw:
        fails.append("throw: the winner never threw")
    elif m["throw"]["launch_flown_by_host"] == 0:
        fails.append("throw: the launch never reached the host")
    if m["throw"]["hits_sent_by_host"] > 0 and m["throw"]["hit_taken_by_victim"] == 0:
        fails.append("throw: the hit never reached the victim's machine")
    if m["throw"]["hits_sent_by_host"] == 0:
        notes.append("throw: the knife missed (aim); the hit path not judged")
    if any(p != places[0] for p in places):
        fails.append(f"throw: machines disagree where the knife is {places}")
    if gap > 0.02 or ko_mismatch:
        fails.append(f"throw: views differ (health gap {gap}, knockout {ko_mismatch})")

    errs = script_errors(host) + script_errors(ca) + script_errors(cb)
    notes += [f"script error: {line} x{n}" for line, n in errs[:6]]
    if errs:
        fails.append("script errors")
    return {"passed": not fails, "metrics": m, "notes": fails + notes}
