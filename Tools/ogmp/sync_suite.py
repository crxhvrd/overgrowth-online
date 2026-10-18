"""Sync with guests on their own pings: a host and two guests, guest 1 on a
typical line, guest 2 on a bad one (latency both ways: the guest delays what
it sends, the host what it sends to that guest).

    sync_ping: every player walks and a guard the host runs chases guest 2.
        What each machine shows of every player and of the guard against its
        owner: time-aligned (the interpolation) and at the same moment (how far
        behind it is, and in metres). Then a guard fights each guest; every
        hit must be answered and all three machines must agree on everyone's
        vitals.

    python -I Tools/ogmp.py run sync_ping
"""

import math
import statistics
import time

from .bots import ChaseBot
from .scenarios import join, script_errors, wait_loaded, LEVEL

GUESTS = [
    dict(latency_ms=25, jitter_ms=8, loss=0.01),   # ~50 ms round trip
    dict(latency_ms=70, jitter_ms=30, loss=0.04),  # ~170 ms round trip, jittery, lossy
]


def _char(inst, cid):
    return next((c for c in inst.bridge.state() if c["id"] == cid), None)


def _at(rows, t, col):
    """rows sorted by column 0 (or 1): the position at time t, None outside."""
    if not rows or t < rows[0][0] or t > rows[-1][0]:
        return None
    lo, hi = 0, len(rows) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if rows[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    a, b = rows[lo], rows[hi]
    f = 0.0 if b[0] == a[0] else (t - a[0]) / (b[0] - a[0])
    return [a[col + i] + (b[col + i] - a[col + i]) * f for i in range(3)]


def _stats(v):
    if not v:
        return None
    v = sorted(v)
    return [round(statistics.median(v), 3), round(v[int(len(v) * 0.95)], 3), round(v[-1], 3)]


def _view(own, seen, shift):
    """own: the owner's samples; seen: an observer's. Interpolation error
    (owner pose at the owner time shown), staleness (owner pose now vs shown,
    m), delay (ms, capture to display) and the frames it jumped (> 0.25 m
    between samples). shift: observer clock to owner clock."""
    interp, stale, delay, jumps = [], [], [], 0
    prev = None
    for row in seen:
        if row[1] <= 0:
            continue  # no frame shown yet
        p = _at(own, row[1], 2)
        if p is not None:
            interp.append(math.dist(p, row[2:5]))
        q = _at(own, row[0] + shift, 2)
        if q is not None:
            stale.append(math.dist(q, row[2:5]))
        delay.append(row[0] + shift - row[1])
        if prev is not None and math.dist(prev, row[2:5]) > 0.25:
            jumps += 1
        prev = row[2:5]
    return {"interp_m": _stats(interp), "stale_m": _stats(stale), "delay_ms": _stats(delay), "jumps": jumps}


def _origin(inst):
    """Wall clock minus the process's online clock (all instances share the PC's clock)."""
    st = inst.bridge.status()
    return st["epoch_ms"] - st["clock_ms"]


def _vitals_gap(a, b):
    va = {c["id"]: c for c in a.bridge.state()}
    vb = {c["id"]: c for c in b.bridge.state()}
    gap, ko, worst = 0.0, [], None
    for cid in va.keys() & vb.keys():
        for key in ("temp_health", "blood_health", "permanent_health"):
            if key in va[cid] and key in vb[cid] and abs(va[cid][key] - vb[cid][key]) > gap:
                gap = abs(va[cid][key] - vb[cid][key])
                worst = [cid, key, round(va[cid][key], 3), round(vb[cid][key], 3)]
        if va[cid].get("knocked_out") != vb[cid].get("knocked_out"):
            ko.append(cid)
    return round(gap, 3), ko, worst


def _counters(inst):
    o = inst.bridge.status()["online"]
    return {k: o[k] for k in ("hits_requested", "hits_resolved", "hit_results", "hits_out_of_range")}


def sync_ping(ctx, level=LEVEL):
    m, notes, fails = {}, [], []
    host = ctx.instance("host")
    host.bridge.call("host", level=level)
    wait_loaded(host)
    guests = []

    for i, prof in enumerate(GUESTS):
        g = ctx.instance(f"client{i + 1}")
        g.bridge.call("net_sim", **prof)
        known = {p["conn_id"] for p in host.bridge.status()["online"]["peers"]}
        join(ctx, host, g)
        new = [p["conn_id"] for p in host.bridge.status()["online"]["peers"] if p["conn_id"] not in known]
        host.bridge.call("net_sim", conn_id=new[0], latency_ms=prof["latency_ms"], jitter_ms=prof["jitter_ms"])
        guests.append(g)

    machines = [host] + guests
    names = ["host", "guest1", "guest2"]
    ids = [inst.bridge.me()["id"] for inst in machines]
    time.sleep(5.0)  # buffers learn the new lines
    m["rtt_ms"] = [round(p["ping_ms"]) for p in host.bridge.status()["online"]["peers"]]

    # A guard the host runs, near guest 2, on the host's side (it hunts the guests)
    for inst in machines:
        inst.bridge.call("exec", target=ids[0], code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "guard");')
    gpos = _char(host, ids[2])["pos"]
    npc = host.bridge.call("spawn", pos=[gpos[0] + 3.0, gpos[1] + 0.3, gpos[2]], teams="guard")["object_id"]
    host.bridge.call("exec", target=npc, code="combat_allowed = false; p_block_skill = 0.0f;")
    t0 = time.time()
    while time.time() - t0 < 15.0 and not all(_char(g, npc) for g in guests):
        time.sleep(0.25)
    time.sleep(2.0)

    # --- 1. movement: everyone walks, the guard chases --------------------------------
    subjects = ids + [npc]
    owner_of = {ids[0]: 0, ids[1]: 1, ids[2]: 2, npc: 0}
    for inst in machines:
        inst.bridge.call("track", ids=subjects)
    keys = [("up", "left"), ("down", "right"), ("left", "up")]
    for k in range(4):  # zigzag: changes of direction are where a stream shows
        for inst, kk in zip(machines, keys):
            inst.bridge.release_all()
            inst.bridge.hold(steps=-1, **{kk[k % 2]: 1.0})
        time.sleep(1.2)
        if k == 2:  # what the buffers think, and how fast each machine runs
            m["buffers"] = {names[o]: {str(c["id"]): [round(c.get("net_delay_ms", 0)), round(c.get("net_interval_ms", 0)), c.get("net_frames")]
                                       for c in inst.bridge.state() if c.get("proxy")} for o, inst in enumerate(machines)}
            m["frame_ms"] = [round(inst.bridge.call("perf").get("frame_ms", 0), 1) for inst in machines]
    for inst in machines:
        inst.bridge.release_all()
    time.sleep(0.5)
    tracks = [inst.bridge.call("track_get", stop=True)["tracks"] for inst in machines]
    origin = [statistics.median([_origin(inst) for _ in range(3)]) for inst in machines]
    import json
    json.dump({"tracks": tracks, "origin": origin, "ids": ids, "npc": npc}, open(f"{ctx.out_dir}/sync_tracks.json", "w"))

    m["views"] = _views(tracks, origin, subjects, owner_of, names, npc, [])
    for k, v in m["views"].items():
        if v is None:
            fails.append(f"{k}: no samples")
        elif v["interp_m"][0] > 0.15:
            fails.append(f"{k}: interpolation {v['interp_m'][0]} m median")

    # --- 2. AI against each guest ------------------------------------------------------
    gpos1 = _char(host, ids[1])["pos"]
    npc1 = host.bridge.call("spawn", pos=[gpos1[0] + 2.5, gpos1[1] + 0.3, gpos1[2]], teams="guard")["object_id"]
    host.bridge.call("exec", target=npc1, code="p_block_skill = 0.0f;")
    host.bridge.call("exec", target=npc, code="combat_allowed = true;")
    t0 = time.time()
    while time.time() - t0 < 15.0 and not all(_char(g, npc1) for g in guests):
        time.sleep(0.25)
    before = [_counters(inst) for inst in machines]
    fighters = [ids[1], ids[2], npc, npc1]
    owner_of[npc1] = 0
    for inst in machines:
        inst.bridge.call("track", ids=fighters)
    bots = [ChaseBot(guests[0], seed=4, target_id=npc1), ChaseBot(guests[1], seed=5, target_id=npc)]
    t0 = last_up = time.time()
    while time.time() - t0 < 24.0:
        for b in bots:
            b.step()
        if time.time() - last_up > 3.0:  # back on their feet: both keep swinging
            last_up = time.time()
            for g, gid in zip(guests, ids[1:]):
                g.bridge.call("exec", target=gid, code="Recover();")
    for g in guests:
        g.bridge.release_all()
    tracks = [inst.bridge.call("track_get", stop=True)["tracks"] for inst in machines]
    m["fight_views"] = _views(tracks, origin, fighters, owner_of, names, npc, [npc1])
    for n in (npc, npc1):  # the guards stand down: the vitals settle
        host.bridge.call("exec", target=n, code="combat_allowed = false;")
    time.sleep(3.0)  # every answer home
    return _after_fight(ctx, m, notes, fails, machines, names, ids, before, guests)


def _views(tracks, origin, subjects, owner_of, names, npc, more_npcs):
    views = {}
    for s in subjects:
        own = tracks[owner_of[s]].get(str(s), [])
        sname = "guard" if s == npc else "guard2" if s in more_npcs else names[owner_of[s]]
        for o in range(len(tracks)):
            if o == owner_of[s]:
                continue
            seen = tracks[o].get(str(s), [])
            views[f"{names[o]} sees {sname}"] = _view(own, seen, origin[o] - origin[owner_of[s]]) if own and seen else None
    return views


def _after_fight(ctx, m, notes, fails, machines, names, ids, before, guests):
    host = machines[0]
    after = [_counters(inst) for inst in machines]
    d = [{k: a[k] - b[k] for k in a} for a, b in zip(after, before)]
    m["hits"] = {names[i]: d[i] for i in range(3)}
    requested = sum(x["hits_requested"] for x in d)
    resolved = sum(x["hits_resolved"] for x in d)
    answered = sum(x["hit_results"] for x in d)
    m["hits"]["total"] = [requested, resolved, answered]
    if requested == 0:
        fails.append("no hits across machines in the AI fights")
    elif resolved != requested or answered != requested:
        fails.append(f"hits lost: requested {requested}, resolved {resolved}, answered {answered}")

    gaps = {}
    for a, b in ((0, 1), (0, 2), (1, 2)):
        first = _vitals_gap(machines[a], machines[b])
        time.sleep(1.0)
        gap, ko, worst = _vitals_gap(machines[a], machines[b])
        gaps[f"{names[a]}-{names[b]}"] = [first, [gap, ko, worst]]
        if gap > 0.02 or ko:
            fails.append(f"vitals differ {names[a]}-{names[b]}: health {gap}, knockout {ko}")
    m["vitals_gaps"] = gaps
    hurt = {names[i]: {k: round(v, 2) for k, v in (_char(host, ids[i]) or {}).items() if k in ("temp_health", "blood_health")} for i in (1, 2)}
    m["guests_hurt_on_host"] = hurt
    ctx.shot(guests[1], "guest2_fight")

    errs = sum((script_errors(inst) for inst in machines), [])
    notes += [f"script error: {line} x{n}" for line, n in errs[:6]]
    if errs:
        fails.append("script errors")
    return {"passed": not fails, "metrics": m, "notes": fails + notes}
