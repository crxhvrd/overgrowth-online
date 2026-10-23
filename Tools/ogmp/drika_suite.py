"""Drika's Story in story co-op (com-wolfire-drika: therium_campaign.as, its levels on
overgrowth_level.as and the Drika/Therium hotspots).

    drika_coop   host + guest on Arlissian Garden: both count it as a story level (the
                 co-op rules); the host walks into checkpoint 0, the "Folti" dialogue
                 plays and is skipped, and its last line (send_global_message
                 "levelwin level_2", run on both machines) takes BOTH to Ouranian
                 Sandstorm together. The guest's own levelwin, sent first, loads
                 nothing (its campaign script only records the unlock)

    python -I Tools/ogmp.py run drika_coop
"""

import time

from .scenarios import PROFILES, join, script_errors, wait_loaded
from .coop_suite import _in_dialogue

CAMPAIGN = "com-wolfire-drika"
CHECKPOINT_0 = (-4.84674, -7.06356, 67.8278)  # arlissiangarden.xml hotspot 1438 (checkpoint 0)


def _skip_dialogues(host, rounds=8):
    for _ in range(rounds):
        if not _in_dialogue(host):
            return
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)


def _level(inst):
    return inst.bridge.status().get("level", "")


def drika_coop(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level level_1", campaign=CAMPAIGN)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    _skip_dialogues(host)
    m["level_1"] = {"host": _level(host), "guest": _level(g)}
    for name, inst in (("host", host), ("guest", g)):
        rules = [l for l in inst.log().splitlines() if "co-op story rules" in l]
        m[f"{name}_rules"] = rules[-1][rules[-1].find("Overgrowth Online"):] if rules else None
        if not rules or "rules on" not in rules[-1]:
            fails.append(f"the {name} doesn't count Arlissian Garden as a story level ({m[f'{name}_rules']})")

    # The guest's own levelwin (its dialogue runs the same line): records, loads nothing
    g.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin level_2");')
    time.sleep(4.0)
    m["guest_after_own_levelwin"] = _level(g)
    if "arlissian" not in _level(g):
        fails.append(f"the guest's own levelwin took it away ({_level(g)})")
    if not any("the host loads it" in l for l in g.log().splitlines()):
        fails.append("the guest's campaign script didn't take the levelwin (no 'the host loads it' line)")

    # Checkpoint 0 (goal 0): its dialogue ends with the level's levelwin
    me = host.bridge.me()["id"]
    host.bridge.call("exec", target="level",
                     code=f"MovementObject@ c = ReadCharacterID({me}); c.position = vec3({CHECKPOINT_0[0]}, {CHECKPOINT_0[1] + 1.0}, {CHECKPOINT_0[2]}); "
                          f'c.velocity = vec3(0.0f); c.Execute("FixDiscontinuity();");')
    t0 = time.time()
    while time.time() - t0 < 60.0 and "sandstorm" not in _level(host):
        if _in_dialogue(host):
            host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.0)
    m["host_left_level_1_s"] = round(time.time() - t0, 1)
    if "sandstorm" not in _level(host):
        fails.append(f"the host didn't go on to Ouranian Sandstorm ({_level(host)})")
        return {"passed": False, "metrics": m, "notes": fails}
    wait_loaded(host)
    g.bridge.wait(lambda: "sandstorm" in _level(g) and g.bridge.status()["level_loaded"], 180, what="guest on level 2")
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")
    time.sleep(4.0)
    m["level_2"] = {"host": _level(host), "guest": _level(g)}

    # A level script's loadlevel on the guest (prayertemple's loadlevel_f runs on every
    # machine): the host's load is the one that counts, the guest stays
    g.bridge.call("exec", target="level", code='level.SendMessage("loadlevel \\"Data/Levels/drika/folticliffs.xml\\"");')
    time.sleep(5.0)
    m["guest_after_own_loadlevel"] = _level(g)
    if "sandstorm" not in _level(g):
        fails.append(f"the guest's own loadlevel took it away ({_level(g)})")

    # Both players there, on both machines; the guest's campaign loaded nothing itself
    for name, inst in (("host", host), ("guest", g)):
        players = [c for c in inst.bridge.state() if c.get("controlled")]
        m[f"{name}_players_on_level_2"] = len(players)
        if len(players) < 2:
            fails.append(f"level 2 on the {name}: {len(players)} player characters")
    guest_log = g.log()
    m["guest_campaign_lines"] = [l[l.find("]: ") + 3:][:120] for l in guest_log.splitlines() if "the host loads it" in l or "unlocking" in l]
    loads = [l for l in guest_log.splitlines() if "Load Level:" in l and "sandstorm" in l]
    m["guest_level_2_loads"] = len(loads)
    if len(loads) > 1:
        fails.append(f"the guest loaded level 2 {len(loads)} times (its own campaign script too?)")
    for name, inst in (("host", host), ("guest", g)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:5]]
    return {"passed": not fails, "metrics": m, "notes": fails}


DRIKA_LEVELS = [f"level_{i}" for i in range(1, 11)]


def drika_levels(ctx):
    """drika_levels: each Drika level hosted (online, alone): it loads, and its level
    and hotspot scripts compile and run a few seconds without script errors."""
    return campaign_levels(ctx, CAMPAIGN, DRIKA_LEVELS, "drika")


T2_CAMPAIGN = "com-wolfire-timbles-therium"


def t2_level_ids():
    import re
    mod = open("R:/SteamLibrary/steamapps/common/Overgrowth/Data/Mods/wolfire_timbles_therium/mod.xml", encoding="utf-8", errors="replace").read()
    return re.findall(r'<Level id="([^"]+)"', mod)


def t2_levels(ctx):
    """t2_levels: drika_levels for Therium-2's 47 levels (about 20 minutes)."""
    return campaign_levels(ctx, T2_CAMPAIGN, t2_level_ids(), "t2")


def campaign_levels(ctx, campaign, level_ids, tag):
    m, fails = {}, []
    for i, level_id in enumerate(level_ids):
        inst = ctx.instance(f"{tag}_{level_id}")
        try:
            inst.bridge.call("host", level=f"load_campaign_level {level_id}", campaign=campaign)
            wait_loaded(inst)
            time.sleep(6.0)
            _skip_dialogues(inst, rounds=3)
            time.sleep(2.0)
            errs = script_errors(inst)
            m[level_id] = {"level": _level(inst).split("/")[-1], "script_errors": [f"{l[l.find(']: ') + 3:][:140]} x{n}" for l, n in errs[:4]]}
            if errs:
                fails.append(f"{level_id}: {len(errs)} script errors")
        except Exception as e:
            # A campaign's last level: skipping its ending dialogue sends the host to the menu
            if "no level loaded" in str(e) and "next-level directive" in inst.log():
                m[level_id] = {"level": "(the campaign's end: back to the menu)", "script_errors": []}
            else:
                fails.append(f"{level_id}: {e}")
        inst.stop()
    return {"passed": not fails, "metrics": m, "notes": fails}


FALL_ZONE = (-78.09, 150.0, -239.032)  # highmonastery.xml hotspot 776 ("Fall", y 123-164)


def drika_fall(ctx, profile="typical"):
    """drika_fall: High Monastery (level_6), the guest drops into the "Fall" zone under the
    monastery. Offline that plays "Fall" and ends in the main menu; in co-op the guest
    dies there and gets up beside the host (a zone's death), no dialogue, nobody leaves."""
    import math
    from .zone_revive_suite import _char_c
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level level_6", campaign=CAMPAIGN)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    _skip_dialogues(host, rounds=12)
    gme = g.bridge.me()
    gc = next(c["common_id"] for c in g.bridge.state() if c["id"] == gme["id"])
    g.bridge.call("exec", target=gme["id"], code=f"this_mo.position = vec3({FALL_ZONE[0]}, {FALL_ZONE[1]}, {FALL_ZONE[2]}); this_mo.velocity = vec3(0.0f, -3.0f, 0.0f);")
    t0 = time.time()
    dead = None
    while time.time() - t0 < 6.0:
        c = _char_c(g, gc) or {}
        if c.get("knocked_out"):
            dead = round(time.time() - t0, 1)
            break
        time.sleep(0.2)
    m["guest_dead_after_s"] = dead
    if dead is None:
        fails.append("the fall zone didn't kill the guest")
    dialogue = False
    t0 = time.time()
    up = None
    while time.time() - t0 < 12.0:
        dialogue = dialogue or _in_dialogue(host)
        c = _char_c(g, gc) or {}
        if dead is not None and up is None and not c.get("knocked_out"):
            up = round(time.time() - t0, 1)
        time.sleep(0.3)
    m["guest_up_after_s"] = up
    m["dialogue_played"] = dialogue
    m["levels"] = {"host": _level(host), "guest": _level(g)}
    if dialogue:
        fails.append("the Fall dialogue played")
    if "highmonastery" not in _level(host) or "highmonastery" not in _level(g):
        fails.append(f"someone left High Monastery ({m['levels']})")
    if dead is not None and up is None:
        fails.append("the guest stayed down")
    if up is not None:
        hid = host.bridge.me()["id"]
        hc = next(c["common_id"] for c in host.bridge.state() if c["id"] == hid)
        d = math.dist(g.bridge.me()["pos"], _char_c(g, hc)["pos"])
        m["guest_from_host_m"] = round(d, 2)
        if d > 6.0:
            fails.append(f"the guest got up {d:.1f} m from the host")
    for name, inst in (("host", host), ("guest", g)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:5]]
    return {"passed": not fails, "metrics": m, "notes": fails}


DHS_HOTSPOT = 1150  # highmonastery.xml: the Drika hotspot (set_enabled 862 false; wait; set_character 564 Hand; wait; ...; 862 false, 1121 true)


def _dhs_state(inst):
    """(862 on, 1121 on, NPC 564's character file) on this machine."""
    inst.bridge.call("level_messages", prefix="probe_dhs")
    inst.bridge.call("exec", target="level",
                     code='level.SendMessage("probe_dhs " + (ReadObjectFromID(862).GetEnabled() ? 1 : 0) + " " + '
                          '(ReadObjectFromID(1121).GetEnabled() ? 1 : 0) + " " + ReadCharacterID(564).char_path);')
    time.sleep(0.3)
    got = inst.bridge.call("level_messages", prefix="probe_dhs")["messages"]
    return got[-1].split()[1:] if got else None


def drika_dhs(ctx, profile="typical"):
    """drika_dhs: the Drika hotspot online (High Monastery, level_6). The host runs the
    sequence, guests follow its progress: the "continue" signals go to the HOST only, yet
    the guest ends with the host's objects and the swapped NPC; a guest's own "continue"
    moves nothing; a second guest joining after the sequence ends with the same state."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level level_6", campaign=CAMPAIGN)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    _skip_dialogues(host, rounds=12)
    time.sleep(3.0)
    m["start"] = {"host": _dhs_state(host), "guest": _dhs_state(g)}

    # The guest's own signal: its copy follows the host, not its own messages
    g.bridge.call("exec", target="level", code='level.SendMessage("continue_drika_hotspot");')
    time.sleep(3.0)
    m["after_guest_signal"] = {"host": _dhs_state(host), "guest": _dhs_state(g)}
    if m["after_guest_signal"]["guest"] != m["start"]["guest"]:
        fails.append(f"the guest's own signal moved its copy ({m['after_guest_signal']['guest']})")

    # The host's two signals: the whole sequence, on both machines
    for _ in range(2):
        host.bridge.call("exec", target="level", code='level.SendMessage("continue_drika_hotspot");')
        time.sleep(2.5)
    time.sleep(3.0)
    m["end"] = {"host": _dhs_state(host), "guest": _dhs_state(g)}
    he, ge = m["end"]["host"], m["end"]["guest"]
    if not he or he[:2] != ["0", "1"] or "ouranian_hand" not in he[2]:
        fails.append(f"the host's sequence didn't run through ({he})")
    if he != ge:
        fails.append(f"the guest differs from the host: {ge} vs {he}")

    # A second guest joins after it all
    g2 = ctx.instance("client2")
    g2.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g2)
    time.sleep(6.0)
    m["late_joiner"] = _dhs_state(g2)
    if m["late_joiner"] != he:
        fails.append(f"the late joiner differs from the host: {m['late_joiner']} vs {he}")
    for name, inst in (("host", host), ("guest", g), ("late joiner", g2)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:5]]
    return {"passed": not fails, "metrics": m, "notes": fails}


def t2_coop(ctx, profile="typical"):
    """t2_coop: Therium-2 in co-op. Host + guest on "begin" (levelwin a -> S1-A1, b ->
    S1-B1): both count it as a story level; the guest's own "levelwin b" (its dialogue
    runs the line too) unlocks S1-B1 there and loads nothing; the host's takes both to
    S1-B1 together."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level begin", campaign=T2_CAMPAIGN)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    _skip_dialogues(host)
    for name, inst in (("host", host), ("guest", g)):
        rules = [l for l in inst.log().splitlines() if "co-op story rules" in l]
        m[f"{name}_rules"] = rules[-1][rules[-1].find("Overgrowth Online"):] if rules else None
        if not rules or "rules on" not in rules[-1]:
            fails.append(f"the {name} doesn't count Begin as a story level ({m[f'{name}_rules']})")

    g.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin b");')
    time.sleep(4.0)
    m["guest_after_own_levelwin"] = _level(g)
    if "begin" not in _level(g):
        fails.append(f"the guest's own levelwin took it away ({_level(g)})")
    unlock = [l[l.find("unlocking"):] for l in g.log().splitlines() if "the host loads it" in l]
    m["guest_unlock"] = unlock
    if not any("s1-b1" in l for l in unlock):
        fails.append(f"the guest didn't unlock S1-B1 by its level's path ({unlock})")

    host.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin b");')
    host.bridge.wait(lambda: "s1/b.xml" in _level(host) and host.bridge.status()["level_loaded"], 120, what="host on S1-B1")
    g.bridge.wait(lambda: "s1/b.xml" in _level(g) and g.bridge.status()["level_loaded"], 180, what="guest on S1-B1")
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")
    time.sleep(4.0)
    m["after"] = {"host": _level(host), "guest": _level(g)}
    for name, inst in (("host", host), ("guest", g)):
        players = [c for c in inst.bridge.state() if c.get("controlled")]
        m[f"{name}_players"] = len(players)
        if len(players) < 2:
            fails.append(f"S1-B1 on the {name}: {len(players)} player characters")
        if not inst.alive():
            fails.append(f"the {name} crashed")
        errs = script_errors(inst) if inst.alive() else []
        if errs:
            m[f"{name}_script_errors"] = [f"{l} x{n}" for l, n in errs[:5]]
            fails.append(f"script errors on the {name}")
    return {"passed": not fails, "metrics": m, "notes": fails}
