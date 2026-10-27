"""zone_revive: a kill zone in co-op. The guest steps into Magma Barracks' lava
(lava_kill.as: zone_killed=1, blood damage, ragdoll, ignite) and dies; the
host revives it beside the host's character (the checkpoint's path). It must
stay up, and no machine may turn non-finite (a user's crash: lava, then a
revive drag, then NaN). The hotspot runs on every machine: the flags on the
host's copy of the guest are reported too. Round 1 revives through a
teammate's drag (a takeover first).

    python -I Tools/ogmp.py run zone_revive
"""

import time

from .scenarios import PROFILES, join, wait_loaded, script_errors
from .coop_suite import _in_dialogue

# A lava zone the host lays on flat ground beside the guest (story levels'
# zones sit over drops or set off dialogues on a death: Magma Barracks'
# lava starts one, Slaver Camp's falls go on into the void)
from .scenarios import LEVEL
LAVA_FILE = "Data/Objects/Hotspots/lava_kill.xml"
PROBE = 'level.SendMessage("probe_zk " + this_mo.GetID() + " " + zone_killed + " " + (on_fire ? 1 : 0) + " " + knocked_out);'


def _char_c(inst, cid):
    return next((c for c in inst.bridge.state() if c.get("common_id") == cid), None)


def _probe(inst, cid):
    c = _char_c(inst, cid)
    if c is None:
        return None
    inst.bridge.call("level_messages", prefix="probe_zk")
    inst.bridge.call("exec", target=c["id"], code=PROBE)
    time.sleep(0.2)
    got = inst.bridge.call("level_messages", prefix="probe_zk")["messages"]
    return got[-1].split()[2:] if got else None  # zone_killed, on_fire, knocked_out


def zone_revive(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    # The level's dialogues out of the way (they stand the dead up and lay them down again)
    t0 = time.time()
    quiet = 0
    while time.time() - t0 < 60.0 and quiet < 4:
        if _in_dialogue(host):
            quiet = 0
            host.bridge.hold(steps=10, skip_dialogue=1.0)
        else:
            quiet += 1
        time.sleep(1.0)
    m["dialogue_wait_s"] = round(time.time() - t0)
    gme = g.bridge.me()
    gp0 = gme["pos"]
    lava_id = host.bridge.call("spawn", path=LAVA_FILE, pos=[gp0[0] + 6.0, gp0[1], gp0[2]])["object_id"]
    host.bridge.call("exec", target="level", code=f"ReadObjectFromID({lava_id}).SetScale(vec3(1.5f, 1.0f, 1.5f));")
    LAVA = (gp0[0] + 6.0, gp0[1], gp0[2])
    time.sleep(2.0)
    gc = next(c["common_id"] for c in g.bridge.state() if c["id"] == gme["id"])
    hme = host.bridge.me()

    for rnd in range(2):
        # Into the lava
        while _in_dialogue(host):
            host.bridge.hold(steps=10, skip_dialogue=1.0)
            time.sleep(1.0)
        g.bridge.call("exec", target=gme["id"], code=f"this_mo.position = vec3({LAVA[0]}, {LAVA[1]}, {LAVA[2]}); this_mo.velocity = vec3(0, -2, 0);")
        time.sleep(1.0)
        t1 = time.time()  # (a death here may start the level's dialogue: it stands the dead up meanwhile)
        while time.time() - t1 < 20.0 and _in_dialogue(host):
            host.bridge.hold(steps=10, skip_dialogue=1.0)
            time.sleep(1.0)
        time.sleep(2.0)
        dead = (_char_c(g, gc) or {}).get("knocked_out")
        m[f"r{rnd}_after_lava"] = {"guest_knocked_out": dead, "guest_flags": _probe(g, gc), "host_copy_flags": _probe(host, gc)}
        if not dead:
            fails.append(f"round {rnd}: the lava didn't kill the guest (test setup)")
            break
        hc = m[f"r{rnd}_after_lava"]["host_copy_flags"]
        if hc and (hc[0] != "0" or hc[1] != "0"):
            fails.append(f"round {rnd}: the host's copy of the guest took the zone's flags {hc} (only its owner's hotspot may)")

        # Round 0: the host brings it back beside the host's character (a checkpoint).
        # Round 1: a teammate's drag first: the host takes the body over, lets go,
        # then the revive (coop_revive.as: release, then ReviveDo)
        hp = host.bridge.me()["pos"]
        hid = _char_c(host, gc)["id"]
        if rnd == 1:
            took = host.bridge.call("exec", target="level", code=f'level.SendMessage("probe_take " + (Online_TakeOver({hid}, 60.0f) ? 1 : 0));')
            time.sleep(2.0)
            host.bridge.call("exec", target="level", code=f"Online_ReleaseTakeOver({hid});")
            m["r1_taken_over"] = host.bridge.call("level_messages", prefix="probe_take")["messages"]
        # (on the host's own ground: beside it is the sea on this level)
        host.bridge.call("exec", target="level", code=f"Online_PlaceAvatar({hid}, vec3({hp[0]:.2f}, {hp[1] + 0.3:.2f}, {hp[2]:.2f}), vec3(0, 0, 1));")
        if True:  # what the guest's own script holds right after (diagnostic)
            DIAG = ('level.SendMessage("probe_diag ko=" + knocked_out + " st=" + state + " th=" + temp_health + " bh=" + blood_health + '
                    '" ph=" + permanent_health + " kos=" + ko_shield + " zk=" + zone_killed + " fire=" + (on_fire ? 1 : 0) + '
                    '" tet=" + tethered + " wd=" + water_depth + " y=" + this_mo.position.y);')
            g.bridge.call("level_messages", prefix="probe_diag")
            for _ in range(25):
                g.bridge.call("exec", target=gme["id"], code=DIAG)
                time.sleep(0.08)
            m[f"r{rnd}_diag"] = g.bridge.call("level_messages", prefix="probe_diag")["messages"]
        downs, t0 = [], time.time()
        while time.time() - t0 < 6.0:
            c = _char_c(g, gc)
            downs.append(c.get("knocked_out") if c else None)
            time.sleep(0.5)
        m[f"r{rnd}_after_revive"] = {"guest_knocked_out_every_0.5s": downs, "guest_flags": _probe(g, gc), "host_copy_flags": _probe(host, gc)}
        if downs and downs[-1]:
            fails.append(f"round {rnd}: revived, then dead again: {downs}")
        for name, inst in (("host", host), ("guest", g)):
            s = inst.bridge.call("nan_scan")
            if s["characters"] or s["objects"]:
                fails.append(f"round {rnd}: non-finite on the {name}: {[c['id'] for c in s['characters']]}")
        time.sleep(1.0)

    for name, inst in (("host", host), ("guest", g)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:4]]
    return {"passed": not fails, "metrics": m, "notes": fails}


PROBE_FALL = 'level.SendMessage("probe_fd " + this_mo.GetID() + " " + (fall_death ? 1 : 0) + " " + zone_killed + " " + knocked_out);'


def _wait_up(inst, cid, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        c = _char_c(inst, cid)
        if c is not None and not c.get("knocked_out"):
            return round(time.time() - t0, 1)
        time.sleep(0.25)
    return None


def zone_respawn(ctx, profile="typical"):
    """zone_respawn: story co-op (Lugaru village, the Overhaul mod's aschar), the
    guest dies where nobody can reach the body; with the host up it must get up
    beside the host on its own (no checkpoint, no drag), and live on: (a) a fall
    death zone's death (aschar fall_death), then a 2.5 m drop that the stale flag
    would make lethal; (b) out of the world (y < -99); (c) a kill zone over a drop
    (zone_killed, Garden Duel's): the body falls on while the respawn waits, and
    standing up where it lies would drop the guest out of the world again.

        python -I Tools/ogmp.py run zone_respawn
    """
    import math
    import os
    from .online_mod_suite import OVERHAUL

    m, fails = {}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        host.bridge.call("host", level="load_campaign_level village", campaign="com-wolfire-lugaru-campaign")
        wait_loaded(host)
        g = ctx.instance("client1")
        g.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, g)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old
    time.sleep(4.0)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)
    gme = g.bridge.me()
    gc = next(c["common_id"] for c in g.bridge.state() if c["id"] == gme["id"])

    def flags():
        g.bridge.call("level_messages", prefix="probe_fd")
        g.bridge.call("exec", target=gme["id"], code=PROBE_FALL)
        time.sleep(0.2)
        got = g.bridge.call("level_messages", prefix="probe_fd")["messages"]
        return got[-1].split()[2:] if got else None  # fall_death, zone_killed, knocked_out

    KILLS = {"fall": 'ReceiveMessage("fall_death"); this_mo.position += vec3(0.0f, 3.0f, 0.0f); this_mo.velocity = vec3(0.0f, -2.0f, 0.0f);',
             "void": "this_mo.position = vec3(this_mo.position.x, -120.0f, this_mo.position.z); this_mo.velocity = vec3(0.0f);",
             "zone_drop": "this_mo.position = vec3(this_mo.position.x, -40.0f, this_mo.position.z); this_mo.velocity = vec3(0.0f); "
                          "zone_killed = 1; SetKnockedOut(_dead); Ragdoll(_RGDL_LIMP);"}
    for name, code in KILLS.items():
        g.bridge.call("exec", target=gme["id"], code=code)
        t0 = time.time()
        dead = False
        while time.time() - t0 < 4.0 and not dead:
            dead = bool((_char_c(g, gc) or {}).get("knocked_out"))
            time.sleep(0.2)
        m[f"{name}_dead_flags"] = flags()
        if not dead:
            fails.append(f"{name}: the guest didn't die (test setup)")
            continue
        up_s = _wait_up(g, gc, 10.0)
        m[f"{name}_up_after_s"] = up_s
        if up_s is None:
            fails.append(f"{name}: the guest stayed down (no respawn beside the host)")
            continue
        time.sleep(1.0)
        hid = host.bridge.me()["id"]
        hc = next(c["common_id"] for c in host.bridge.state() if c["id"] == hid)
        d = math.dist(g.bridge.me()["pos"], _char_c(g, hc)["pos"])  # (the guest's copy of the host's character)
        m[f"{name}_from_host_m"] = round(d, 2)
        if d > 6.0:
            fails.append(f"{name}: up {d:.1f} m from the host (not beside)")
        m[f"{name}_up_flags"] = flags()
        # A jump's landing (2.5 m): with the zone's flag left over, it kills
        g.bridge.call("exec", target=gme["id"], code="this_mo.position += vec3(0.0f, 2.5f, 0.0f); this_mo.velocity = vec3(0.0f);")
        time.sleep(3.0)
        c = _char_c(g, gc) or {}
        m[f"{name}_after_drop_knocked_out"] = c.get("knocked_out")
        if c.get("knocked_out"):
            fails.append(f"{name}: up again, then a 2.5 m drop killed the guest (flags {flags()})")
        if not (_char_c(host, gc) and host.bridge.me()):
            fails.append(f"{name}: the host lost the guest")
    for name, inst in (("host", host), ("guest", g)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:4]]
    return {"passed": not fails, "metrics": m, "notes": fails}
