"""One shared session that runs every network check back to back, switching
the simulated network live instead of relaunching the game per check.

    full_session: camera, then per profile (lan, typical, bad): input latency
    (what the player sees: 25 cm of movement on screen), bandwidth, view
    error, proxy accuracy, clock sync; then a late join (with a relay check:
    one client's character seen by another through the host), a dropped
    player, a clean leave.
"""

import statistics
import time

from .scenarios import PROFILES, dist, join, players, session, wait_loaded


def _char(inst, cid):
    return next((c for c in inst.bridge.state() if c["id"] == cid), None)


def _truth_offset(a, b):
    """b's clock minus a's, from two status snapshots (clock_ms - epoch_ms is each process's origin)."""
    sa, sb = a.bridge.status(), b.bridge.status()
    return (sb["clock_ms"] - sb["epoch_ms"]) - (sa["clock_ms"] - sa["epoch_ms"])


def _camera_jitter(inst, seconds=3.0):
    """(median, max) camera move between polls, and when the max came (s into the window)."""
    jumps, last, t0 = [], None, time.time()
    while time.time() - t0 < seconds:
        pos = inst.bridge.status()["camera_pos"]
        if last is not None:
            jumps.append((dist(pos, last), time.time() - t0))
        last = pos
    if not jumps:
        return 0.0, 0.0, 0.0
    worst = max(jumps)
    return statistics.median(j for j, _ in jumps), worst[0], worst[1]


def _moved(inst, cid, start, threshold=0.25):
    c = _char(inst, cid)
    return c is not None and dist(c["pos"], start) > threshold


def _measure_move(host, client, cid, key):
    """The client holds a key: when does it see its own character move 25 cm, and when does the host?"""
    start_c = _char(client, cid)["pos"]
    start_h = _char(host, cid)["pos"]
    own = seen = None
    t0 = time.time()
    client.bridge.hold(steps=-1, **{key: 1.0})

    while time.time() - t0 < 5.0 and (own is None or seen is None):
        if own is None and _moved(client, cid, start_c):
            own = (time.time() - t0) * 1000
        if seen is None and _moved(host, cid, start_h):
            seen = (time.time() - t0) * 1000

    return own, seen


def _interp(rows, t, col):
    """rows sorted by rows[i][0]: value of column col at time t (None outside)."""
    lo, hi = 0, len(rows) - 1
    if not rows or t < rows[0][0] or t > rows[-1][0]:
        return None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if rows[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    a, b = rows[lo], rows[hi]
    f = 0.0 if b[0] == a[0] else (t - a[0]) / (b[0] - a[0])
    return [a[col + i] + (b[col + i] - a[col + i]) * f for i in range(3)]


def _accuracy(owner, observer, cid):
    """Time-aligned error of a proxy: what the observer shows (at the owner
    time it is playing) against the owner's own character at that time.
    Returns (median, p95) for the position and the mean physics bone."""
    key = str(cid)
    own = owner.bridge.call("track_get", stop=True)["tracks"].get(key, [])
    seen = observer.bridge.call("track_get", stop=True)["tracks"].get(key, [])
    pos_err, bone_err = [], []

    for row in seen:
        p = _interp(own, row[1], 2)
        b = _interp(own, row[1], 5)
        if p is not None:
            pos_err.append(dist(p, row[2:5]))
            bone_err.append(dist(b, row[5:8]))

    if not pos_err:
        return None

    def stats(v):
        v = sorted(v)
        return [round(statistics.median(v), 3), round(v[int(len(v) * 0.95)], 3)]

    return {"pos_m": stats(pos_err), "bones_m": stats(bone_err), "samples": len(pos_err)}


def full_session(ctx):
    m, notes, fails = {}, [], []
    host, (c1,) = session(ctx, 1, profile="lan")
    time.sleep(2.0)

    # Camera: only our own character drives our view, and it is as steady as the host's
    r = c1.bridge.call("state")
    owners = [c["id"] for c in r["characters"] if c["controlled"] and c["controller_id"] == 0 and c.get("camera_id", 0) == 0]
    c1.bridge.call("track", ids=[r["my_avatar"]])
    jc = _camera_jitter(c1)
    track = c1.bridge.call("track_get", stop=True)["tracks"].get(str(r["my_avatar"]), [])
    jh = _camera_jitter(host)
    steps = [(dist(a[2:5], b[2:5]), round(b[0] - track[0][0])) for a, b in zip(track, track[1:])]
    m["client_avatar_max_step"] = [round(max(steps)[0], 3), max(steps)[1]] if steps else None  # [m, ms into the window]
    if jc[1] > 0.05:
        m["camera_jump_at_s"] = round(jc[2], 2)
    mine = next((c for c in r["characters"] if c["id"] == r["my_avatar"]), {})
    m["avatar_bones_morphs"] = [mine.get("bones"), mine.get("morphs")]
    m["camera_owners_ok"] = owners == [r["my_avatar"]]
    m["camera_jitter_client"] = [round(jc[0], 3), round(jc[1], 3)]
    m["camera_jitter_host"] = [round(jh[0], 3), round(jh[1], 3)]
    if owners != [r["my_avatar"]]:
        fails.append(f"camera owners {owners}, mine {r['my_avatar']}")
    if jc[0] > jh[0] * 2 + 0.02 or jc[1] > jh[1] * 2 + 0.1:
        fails.append("client camera shakier than the host's")
    ctx.shot(c1, "camera")
    cid = c1.bridge.me()["id"]
    key = "up"

    # Reference: the host moving its own character, no network in the way
    # (time to 25 cm includes the character's own acceleration)
    hid = host.bridge.me()["id"]
    start = _char(host, hid)["pos"]
    t0 = time.time()
    host.bridge.hold(steps=-1, up=1.0)
    host_self = None
    while time.time() - t0 < 3.0 and host_self is None:
        if _moved(host, hid, start):
            host_self = (time.time() - t0) * 1000
    host.bridge.release_all()
    m["host_self_move_ms"] = round(host_self) if host_self else None
    time.sleep(1.5)

    for profile in ("lan", "typical", "bad"):
        for inst in (host, c1):
            inst.bridge.call("net_sim", **PROFILES[profile])
        time.sleep(4.0)  # let pings and buffers settle under the new conditions
        b0 = c1.bridge.status()["online"]
        time.sleep(3.0)
        b1 = c1.bridge.status()["online"]
        for inst in (host, c1):
            inst.bridge.call("track", ids=[cid])
        own, seen = _measure_move(host, c1, cid, key)
        errors, t_move = [], time.time()
        b2 = c1.bridge.status()["online"]

        while time.time() - t_move < 2.5:
            a, b = _char(c1, cid), _char(host, cid)
            if a and b:
                errors.append(dist(a["pos"], b["pos"]))

        b3 = c1.bridge.status()["online"]
        c1.bridge.release_all()
        accuracy = _accuracy(c1, host, cid)
        key = "down" if key == "up" else "up"  # walk back next time: stay on the map
        moving_s = time.time() - t_move
        t = statistics.median([_truth_offset(host, c1) for _ in range(3)])
        hp = host.bridge.status()["online"]["peers"][0]
        cp = c1.bridge.status()["online"]["peers"][0]
        m[profile] = {
            "rtt_ms": round(hp["ping_ms"]),
            "client_sees_own_move_ms": round(own) if own else None,
            "host_sees_move_ms": round(seen) if seen else None,
            "kbit_down_idle": round((b1["bytes_in"] - b0["bytes_in"]) * 8 / 3.0 / 1000, 1),
            "kbit_up_idle": round((b1["bytes_out"] - b0["bytes_out"]) * 8 / 3.0 / 1000, 1),
            "kbit_down_moving": round((b3["bytes_in"] - b2["bytes_in"]) * 8 / moving_s / 1000, 1),
            "kbit_up_moving": round((b3["bytes_out"] - b2["bytes_out"]) * 8 / moving_s / 1000, 1),
            "records_per_s_up": round((b3["records_out"] - b2["records_out"]) / moving_s, 1),
            "record_bytes": round((b3["snapshot_bytes_out"] - b2["snapshot_bytes_out"]) / max(1, b3["records_out"] - b2["records_out"])),
            "positions_sent_ratio": round((b3["positions_sent"] - b2["positions_sent"]) / max(1, b3["bones_captured"] - b2["bones_captured"]), 3),
            "proxy_accuracy": accuracy,
            "view_error_median_m": round(statistics.median(errors), 3) if errors else None,
            "clock_error_ms": round(max(abs(hp["clock_offset_ms"] - t), abs(cp["clock_offset_ms"] + t)), 1),
        }
        if own and host_self:
            m[profile]["network_adds_ms"] = round(own - host_self)  # info: host_self is one noisy sample
        if accuracy is None:
            fails.append(f"{profile}: no proxy accuracy samples")
        elif accuracy["pos_m"][0] > (0.05 if profile == "lan" else 0.15):
            fails.append(f"{profile}: proxy error {accuracy['pos_m'][0]} m median")
        if m[profile]["kbit_up_moving"] > 100:
            fails.append(f"{profile}: client upload {m[profile]['kbit_up_moving']} kbit/s (budget 100)")
        if own is None or seen is None:
            fails.append(f"{profile}: no movement seen (own {own}, host {seen})")
        clock_limit = 5 + PROFILES[profile]["jitter_ms"] / 2  # jitter blurs every sample
        if m[profile]["clock_error_ms"] > clock_limit:
            fails.append(f"{profile}: clock error {m[profile]['clock_error_ms']} ms (limit {clock_limit:.0f})")
        time.sleep(1.0)

    # Ownership: our own character answers our input locally, so the time to
    # see it move is the same on every network (a couple of frames of noise)
    owns = [m[p]["client_sees_own_move_ms"] for p in ("lan", "typical", "bad") if m[p]["client_sees_own_move_ms"]]
    m["own_move_spread_ms"] = max(owns) - min(owns) if len(owns) == 3 else None
    if m["own_move_spread_ms"] is None or m["own_move_spread_ms"] > 25:
        fails.append(f"own input depends on the network: {owns}")

    # Late join under typical conditions (everyone back on that profile, settled)
    for inst in (host, c1):
        inst.bridge.call("net_sim", **PROFILES["typical"])
    time.sleep(2.0)
    c2 = ctx.instance("client2")
    c2.bridge.call("net_sim", **PROFILES["typical"])
    t0 = time.time()
    join(ctx, host, c2)
    m["late_join_s"] = round(time.time() - t0, 1)
    time.sleep(4.0)
    hv = {c["id"]: c for c in host.bridge.state()}
    v2 = {c["id"]: c for c in c2.bridge.state()}
    gaps = [dist(hv[i]["pos"], v2[i]["pos"]) for i in hv.keys() & v2.keys()]
    m["late_join_gaps"] = {i: round(dist(hv[i]["pos"], v2[i]["pos"]), 2) for i in hv.keys() & v2.keys()}
    m["late_join_players"] = len(players(host))
    m["late_join_gap_max_m"] = round(max(gaps), 2) if gaps else None
    if m["late_join_players"] != 3 or c2.bridge.me() is None or (gaps and max(gaps) > 1.0):
        fails.append("late join")
    ctx.shot(c2, "late_join")

    # Relay: client 1 walks, client 2 sees it through the host (typical on both hops)
    for inst in (c1, c2):
        inst.bridge.call("track", ids=[cid])
    c1.bridge.hold(steps=-1, **{key: 1.0})
    time.sleep(2.5)
    c1.bridge.release_all()
    relay = _accuracy(c1, c2, cid)
    m["relay_accuracy"] = relay
    m["relay_delay_ms"] = next((round(c["net_delay_ms"]) for c in c2.bridge.state() if c["id"] == cid and c.get("proxy")), None)
    if relay is None or relay["pos_m"][0] > 0.15:
        fails.append(f"relay accuracy {relay}")

    # A player vanishes (process killed): the host must notice and clean up
    chars = len(host.bridge.state())
    c2.kill()
    t0 = time.time()
    try:
        host.bridge.wait(lambda: len(players(host)) == 2, 40, what="host drops the vanished player")
        m["drop_detect_s"] = round(time.time() - t0, 1)
    except Exception:
        m["drop_detect_s"] = None
        fails.append("vanished player never dropped")
    m["drop_characters"] = [chars, len(host.bridge.state())]

    # A clean leave
    c1.bridge.call("disconnect")
    t0 = time.time()
    try:
        host.bridge.wait(lambda: len(players(host)) == 1, 20, what="host sees the leave")
        m["leave_seen_s"] = round(time.time() - t0, 2)
        c1.bridge.wait(lambda: not c1.bridge.status()["online"]["active"], 20, what="client out of the session")
    except Exception:
        fails.append("clean leave")

    return {"passed": not fails, "metrics": m, "notes": fails + notes}
