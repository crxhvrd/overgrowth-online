"""Phase 5: campaign_coop. A host and a client play the Overgrowth story
together for three levels.

    Level 1:
      - the client's character appears beside the host's;
      - a dialogue the host starts shows on the client, and the host's skip
        ends it there too;
      - respawn rules:
          - a downed client's own "reset" does nothing;
          - a checkpoint brings it back up beside the host;
          - with everyone down the host resets the level for both.
    Every level: the host finishes it (levelwin) and the client follows to the
    next one, with its own character again.
"""

import math
import time

from .scenarios import PROFILES, join, script_errors, wait_loaded

CAMPAIGN = "com-wolfire-overgrowth-campaign"
LEVELS = ["og_story/01_Slaver_Camp_Landing.xml", "og_story/02_Slaver_Camp.xml", "og_story/04_Main_Slaver_Camp.xml"]


def _messages(inst, prefix):
    return inst.bridge.call("level_messages", prefix=prefix)["messages"]


def _probe(inst, expr, tag):
    """A value from the level scripts (sent as a level message, read back)."""
    _messages(inst, tag)
    inst.bridge.call("exec", target="level", code=f'level.SendMessage("{tag} " + ({expr}));')
    time.sleep(0.4)
    got = _messages(inst, tag)
    return got[-1].split(" ", 1)[1] if got else None


def _in_dialogue(inst):
    return _probe(inst, 'level.DialogueCameraControl() ? "1" : "0"', "probe_dialogue") == "1"


def _ko(inst):
    """Knocks this machine's own character out (its machine runs it)."""
    me = inst.bridge.me()["id"]
    inst.bridge.call("exec", target="level",
                     code=f'ReadCharacterID({me}).Execute("SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);");')


def _down(inst, char_id):
    c = next((c for c in inst.bridge.state() if c["id"] == char_id), None)
    return c is not None and c.get("knocked_out", 0) != 0


def _level(inst):
    return inst.bridge.status().get("level", "")


def _common(inst, char_id):
    return next((c["common_id"] for c in inst.bridge.state() if c["id"] == char_id), None)


def _local(inst, common_id):
    return next((c["id"] for c in inst.bridge.state() if c["common_id"] == common_id), None)


def campaign_coop(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign=CAMPAIGN)  # by id, as the menu does
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)

    levels_seen = []
    for n, level in enumerate(LEVELS):
        if n > 0:
            # The host finishes the level; the campaign loads the next; the client follows
            host.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin");')
            t0 = time.time()
            while time.time() - t0 < 180.0:
                try:
                    if level in _level(host) and level in _level(client) and host.bridge.status()["level_loaded"] \
                            and client.bridge.status()["level_loaded"]:
                        break
                except Exception:
                    pass  # loading
                time.sleep(1.0)
            host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 90, what="the client on the new level")
            time.sleep(4.0)

        lh, lc = _level(host), _level(client)
        levels_seen.append([lh, lc])
        if level not in lh or level not in lc:
            fails.append(f"level {n + 1}: host on {lh}, client on {lc} (expected {level})")
            break

        hme, cme = host.bridge.me(), client.bridge.me()
        if hme is None or cme is None:
            fails.append(f"level {n + 1}: a player has no character (host {hme is not None}, client {cme is not None})")
            break
        on_host = _local(host, _common(client, cme["id"]))
        if on_host is None:
            fails.append(f"level {n + 1}: the host doesn't have the client's character")
            break

        if n == 0:
            m["players_apart_m"] = round(math.dist(hme["pos"], next(c["pos"] for c in host.bridge.state()
                                                                     if c["id"] == on_host)), 2)
            if m["players_apart_m"] < 0.8:
                fails.append(f"the client's character spawned inside the host's ({m['players_apart_m']} m)")
            _level_one(host, client, on_host, m, fails)

    m["levels"] = levels_seen
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def _level_one(host, client, client_on_host, m, fails):
    # The intro dialogue may be running: skip it on the host (its skip counts everywhere)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    # A dialogue the host starts plays on the client; the host's skip ends it there
    host.bridge.call("exec", target="level", code='level.SendMessage("start_dialogue \\"Civ1\\"");')
    time.sleep(2.5)
    m["dialogue_started_host_client"] = [_in_dialogue(host), _in_dialogue(client)]
    if m["dialogue_started_host_client"] != [True, True]:
        fails.append(f"a dialogue the host started: in dialogue host/client = {m['dialogue_started_host_client']}")
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)
    time.sleep(2.0)
    m["dialogue_after_skip_host_client"] = [_in_dialogue(host), _in_dialogue(client)]
    if any(m["dialogue_after_skip_host_client"]):
        fails.append(f"after the host's skip: in dialogue host/client = {m['dialogue_after_skip_host_client']}")

    # A downed client can't reset the level
    cme = client.bridge.me()["id"]
    _ko(client)
    time.sleep(2.0)
    _messages(host, "reset")
    client.bridge.call("exec", target="level", code='level.SendMessage("reset");')
    time.sleep(2.5)
    m["client_down_after_its_reset"] = _down(client, cme)
    if not m["client_down_after_its_reset"]:
        fails.append("the downed client's own reset went through (it should wait for the host)")
    if _messages(host, "reset"):
        fails.append("the client's reset reached the host")
    if not _down(host, client_on_host):
        fails.append("the host doesn't see the client down")

    # A checkpoint brings it back up beside the host
    host.bridge.call("exec", target="level", code='level.SendMessage("player_entered_checkpoint 1");')
    time.sleep(3.0)
    m["client_up_after_checkpoint"] = not _down(client, cme)
    if not m["client_up_after_checkpoint"]:
        fails.append("a checkpoint didn't bring the downed client back")
    else:
        hpos = host.bridge.me()["pos"]
        cpos = client.bridge.me()["pos"]
        m["revived_from_host_m"] = round(math.dist(hpos, cpos), 2)
        if m["revived_from_host_m"] > 5.0:
            fails.append(f"the client came back {m['revived_from_host_m']} m from the host")
        if _down(host, client_on_host):
            fails.append("the host still sees the client down after the checkpoint")

    # Everyone down: the host resets the level for both
    _messages(host, "reset")
    _messages(client, "reset")
    _ko(client)
    _ko(host)
    hme = host.bridge.me()["id"]
    resets = [0, 0]
    timeline = []
    for _ in range(9):
        time.sleep(1.0)
        resets[0] += len([x for x in _messages(host, "reset") if x == "reset"])
        resets[1] += len([x for x in _messages(client, "reset") if x == "reset"])
        cs = next((c for c in client.bridge.state() if c["id"] == cme), {})
        timeline.append([cs.get("knocked_out"), cs.get("state"), resets[1]])
    m["client_ko_state_resets_per_s"] = timeline
    m["after_all_down_up_host_client"] = [not _down(host, hme), not _down(client, cme)]
    m["resets_host_client"] = resets
    if m["after_all_down_up_host_client"] != [True, True]:
        fails.append(f"everyone down: not both up again after 9 s {m['after_all_down_up_host_client']}")
    if m["resets_host_client"][0] < 1 or m["resets_host_client"][1] < 1:
        fails.append(f"everyone down: resets seen host/client {m['resets_host_client']}")


def _has_color(char, rgb, tol=0.03):
    return any(all(abs(e[1 + k] - rgb[k]) < tol for k in range(3)) for e in char.get("palette", []))


def coop_character(ctx, profile="typical"):
    """Your co-op character: the host plays a cat in blue clothes, client 1 a rat
    with red fur, client 2 joins late. Every machine shows both choices, and
    they carry over to the next story level."""
    m, fails = {}, []
    choices = {"host": ("Data/Characters/male_cat.xml", None, (0.1, 0.2, 0.9)),
               "client1": ("Data/Characters/rat.xml", (0.9, 0.1, 0.1), None)}

    def keys(name):
        path, fur, cloth = choices[name]
        k = {"mp_character": path}
        if fur:
            k["mp_fur"] = " ".join(str(x) for x in fur)
        if cloth:
            k["mp_cloth"] = " ".join(str(x) for x in cloth)
        return k

    host = ctx.instance("host", keys=keys("host"))
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign=CAMPAIGN)
    wait_loaded(host)
    c1 = ctx.instance("client1", keys=keys("client1"))
    c1.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, c1)
    time.sleep(4.0)
    c2 = ctx.instance("client2")
    c2.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, c2)
    time.sleep(5.0)

    def check(tag):
        hid = _common(host, host.bridge.me()["id"])
        cid = _common(c1, c1.bridge.me()["id"])
        seen = {}
        for inst in (host, c1, c2):
            chars = {c["common_id"]: c for c in inst.bridge.state()}
            h, c = chars.get(hid), chars.get(cid)
            seen[inst.name] = [h and h["char_path"], c and c["char_path"]]
            if h is None or c is None:
                fails.append(f"{tag}: {inst.name} lacks a player's character")
                continue
            if h["char_path"] != choices["host"][0] or c["char_path"] != choices["client1"][0]:
                fails.append(f"{tag}: {inst.name} shows {h['char_path']} / {c['char_path']}")
            if not _has_color(h, choices["host"][2]):
                fails.append(f"{tag}: {inst.name} doesn't show the host's clothes colour")
            if not _has_color(c, choices["client1"][1]):
                fails.append(f"{tag}: {inst.name} doesn't show client 1's fur colour")
        m[tag] = seen

    check("level 1")

    host.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin");')
    t0 = time.time()
    while time.time() - t0 < 180.0:
        try:
            if all(LEVELS[1] in _level(i) and i.bridge.status()["level_loaded"] for i in (host, c1, c2)):
                break
        except Exception:
            pass
        time.sleep(1.0)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 90, what="everyone on level 2")
    time.sleep(6.0)
    check("level 2")

    errs = script_errors(host) + script_errors(c1) + script_errors(c2)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def coop_camera(ctx, profile="typical"):
    """Each player's camera follows its own character, with the Overhaul mod,
    in Lugaru's first level: the host walks 12 m off, and neither camera may
    frame both (the shared camera of local two-player counted the other
    machine's player)."""
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
        client = ctx.instance("client1")
        client.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, client)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old
    time.sleep(4.0)
    for _ in range(8):  # an intro dialogue pins the characters and owns the camera: the host skips it
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    hme = host.bridge.me()["id"]
    host.bridge.call("exec", target=hme, code="this_mo.position += vec3(12.0f, 0.5f, 0.0f); this_mo.velocity = vec3(0.0f);")
    time.sleep(3.0)
    for inst in (host, client):
        me = inst.bridge.me()
        cam = inst.bridge.status()["camera_pos"]
        d = math.dist(cam, me["pos"])
        m[f"{inst.name}_camera_from_own_m"] = round(d, 2)
        if d > 5.0:
            fails.append(f"{inst.name}: the camera is {d:.1f} m from its own character (framing both players?)")
    m["players_apart_m"] = round(math.dist(host.bridge.me()["pos"], next(c["pos"] for c in host.bridge.state()
                                                                           if c["common_id"] == _common(client, client.bridge.me()["id"]))), 1)
    if m["players_apart_m"] < 8.0:
        fails.append(f"the host didn't get away from the client ({m['players_apart_m']} m): the camera check proves nothing")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


TALK_HOTSPOT = 1217  # og_story 01: "Civ1", a "?" character; it and 78 start off, goal_3_pre switches them on
OTHER_TALK = 78


def _talk(client, hotspot):
    client.bridge.call("exec", target="level",
                       code=f'ReadObjectFromID({hotspot}).QueueScriptMessage("player_pressed_attack");')


def coop_talk(ctx, profile="typical"):
    """The "?" characters for a guest. The test plays the host's goal 3 step:
    the host switches its talker on and sends the goal number. The guest must
    run the step it skipped (both talkers on), its talk must reach the host,
    which plays the dialogue on both machines and marks the guest's copy used;
    a talk to one the host doesn't offer is answered "used" (its "?" goes)."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign=CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    # Before the step a guest's talk asks nothing (switched off)
    _messages(host, "ohr")
    _talk(client, TALK_HOTSPOT)
    time.sleep(2.0)
    m["talk_while_off"] = [x for x in _messages(host, "ohr") if "coop_talk" in x]
    if m["talk_while_off"]:
        fails.append(f"a switched-off talker asked the host: {m['talk_while_off']}")

    # The host's goal 3 step
    host.bridge.call("exec", target="level",
                     code=f'ReadObjectFromID({TALK_HOTSPOT}).ReceiveScriptMessage("activate"); level.SendMessage("ohl og_progress 3");')
    time.sleep(2.0)
    for _ in range(8):  # (goal_3_pre's "Places2" plays on the host's side only if the host runs it: not here)
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    _messages(host, "ohr")
    _messages(client, "ohl coop_talked")
    _talk(client, TALK_HOTSPOT)
    # (reading messages clears them, and the dialogue probe reads too: collect as we go)
    t0, started, req, used = time.time(), [False, False], [], []
    while time.time() - t0 < 6.0 and started != [True, True]:
        time.sleep(0.5)
        req += [x for x in _messages(host, "ohr") if "coop_talk" in x]
        used += _messages(client, "ohl coop_talked")
        started = [_in_dialogue(host), _in_dialogue(client)]
    time.sleep(1.0)
    req += [x for x in _messages(host, "ohr") if "coop_talk" in x]
    used += _messages(client, "ohl coop_talked")
    m["request_on_host"] = req
    m["dialogue_host_client"] = started
    m["used_on_client"] = used
    if not req:
        fails.append("the guest's talk never reached the host (talker not switched on by the goal step?)")
    if started != [True, True]:
        fails.append(f"the guest's talk: in dialogue host/client = {started}")
    if f"ohl coop_talked {TALK_HOTSPOT}" not in used:
        fails.append("the guest wasn't told the dialogue was played")

    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)
    time.sleep(1.0)

    # Used now: the guest's talk asks nothing, and nothing plays again
    _messages(host, "ohr")
    _talk(client, TALK_HOTSPOT)
    time.sleep(3.0)
    m["second_talk_requests"] = [x for x in _messages(host, "ohr") if "coop_talk" in x]
    m["second_talk_dialogue"] = [_in_dialogue(host), _in_dialogue(client)]
    if m["second_talk_requests"] or any(m["second_talk_dialogue"]):
        fails.append(f"a used talk asked again {m['second_talk_requests']} / played {m['second_talk_dialogue']}")

    # 78: on for the guest, not offered by the host: answered "used", nothing plays
    _messages(client, "ohl coop_talked")
    _talk(client, OTHER_TALK)
    time.sleep(3.0)
    m["unoffered_answer"] = _messages(client, "ohl coop_talked")
    m["unoffered_dialogue"] = [_in_dialogue(host), _in_dialogue(client)]
    if f"ohl coop_talked {OTHER_TALK}" not in m["unoffered_answer"] or any(m["unoffered_dialogue"]):
        fails.append(f"a talk the host doesn't offer: answer {m['unoffered_answer']}, played {m['unoffered_dialogue']}")

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


GEAR = 1201  # og_story 01: the hero's backpack (TestBackpack), on bone 2 of character 0


def _gear_gap(inst, char_id):
    """How far the backpack sits from its character, here."""
    v = _probe(inst, f"distance(ReadObjectFromID({GEAR}).GetTranslation(), ReadCharacterID({char_id}).position)", "probe_gear")
    return float(v) if v not in (None, "") else None


def coop_gear(ctx, profile="typical"):
    """Gear on another player stays on their body: the host (the hero, with a
    backpack) runs; the backpack's distance from the host's character, as the
    client draws it, must stay what the host sees (it floated off: the client
    used the host's transforms, from another moment)."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign=CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    hme = host.bridge.me()["id"]
    on_client = _local(client, _common(host, hme))
    if on_client is None:
        return {"passed": False, "metrics": m, "notes": ["the client doesn't have the host's character"]}

    still_host, still_client = _gear_gap(host, hme), _gear_gap(client, on_client)
    host.bridge.hold(up=1.0)
    gaps_host, gaps_client = [], []
    t0 = time.time()
    while time.time() - t0 < 5.0:
        gaps_host.append(_gear_gap(host, hme))
        gaps_client.append(_gear_gap(client, on_client))
    host.bridge.release("up")
    gaps_host = [g for g in gaps_host if g is not None]
    gaps_client = [g for g in gaps_client if g is not None]
    m["still_host_client_m"] = [still_host, still_client]
    m["running_host_m"] = [min(gaps_host, default=-1), max(gaps_host, default=-1)]
    m["running_client_m"] = [min(gaps_client, default=-1), max(gaps_client, default=-1)]
    m["samples"] = [len(gaps_host), len(gaps_client)]
    if not gaps_client or not gaps_host:
        fails.append("no backpack readings")
    elif max(gaps_client) > max(gaps_host) + 0.15:
        fails.append(f"the backpack leaves the host's body on the client: up to {max(gaps_client)} m (host {max(gaps_host)} m)")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def _revive_round(m, fails, tag, reviver, downed, reviver_inst_me, downed_inst_me):
    """`reviver` drags `downed`'s body until it is up; then `downed` must walk."""
    from .combat_suite import _face_and_go
    r_me, d_me = reviver_inst_me, downed_inst_me
    d_on_r = _local(reviver, _common(downed, d_me))
    dpos = next(c["pos"] for c in reviver.bridge.state() if c["id"] == d_on_r)
    reviver.bridge.call("exec", target=r_me,
                        code=f"this_mo.position = vec3({dpos[0] + 1.5}, {dpos[1] + 0.3}, {dpos[2]}); this_mo.velocity = vec3(0.0f);")
    time.sleep(1.5)
    down_at = downed.bridge.me()["pos"]
    _ko(downed)
    if not _wait_until(lambda: _down(downed, d_me) and _down(reviver, d_on_r), 5.0):
        fails.append(f"{tag}: never went down on both machines")
        return
    time.sleep(1.5)
    t0 = time.time()
    while time.time() - t0 < 6.0:
        body = next(c["pos"] for c in reviver.bridge.state() if c["id"] == d_on_r)
        if _face_and_go(reviver, r_me, body, 1.0):
            break
        time.sleep(0.2)
    reviver.bridge.release_all()
    reviver.bridge.hold(steps=-1, crouch=1.0, grab=1.0)
    t0 = time.time()
    while time.time() - t0 < 10.0:
        time.sleep(0.5)
        if not _down(downed, d_me) and not _down(reviver, d_on_r):
            break
    reviver.bridge.release_all()
    m[tag + "_revive_s"] = round(time.time() - t0, 1)
    time.sleep(3.0)
    up = [not _down(downed, d_me), not _down(reviver, d_on_r)]
    me = downed.bridge.me()
    m[tag + "_after"] = {"up_own_other": up, "state": me.get("state"), "rise_m": round(me["pos"][1] - down_at[1], 2),
                         "moved_m": round(math.dist(me["pos"], down_at), 2)}
    if up != [True, True]:
        fails.append(f"{tag}: not revived: up on own/other machine {up}")
        return
    if abs(m[tag + "_after"]["rise_m"]) > 1.0 or me.get("state") != 0:
        fails.append(f"{tag}: not standing where it went down: {m[tag + '_after']}")
    # It walks again
    start = me["pos"]
    downed.bridge.hold(up=1.0)
    time.sleep(1.5)
    downed.bridge.release_all()
    m[tag + "_walked_m"] = round(math.dist(start, downed.bridge.me()["pos"]), 2)
    if m[tag + "_walked_m"] < 1.0:
        fails.append(f"{tag}: can't move after the revive (walked {m[tag + '_walked_m']} m)")
    time.sleep(1.0)


def coop_revive(ctx, profile="typical"):
    """Reviving a teammate, both ways: the guest revives the host, then the
    host revives the guest (crouch + grab on the body for the revive time).
    Each time the revived player is up on both machines, standing where it
    fell, and can walk."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level village", campaign="com-wolfire-lugaru-campaign")  # (dry land: og_story 01 starts by water)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)
    hme, cme = host.bridge.me()["id"], client.bridge.me()["id"]
    _messages(host, "ohl coop_revive")
    _messages(client, "ohl coop_revive")

    _revive_round(m, fails, "host_revived", client, host, cme, hme)
    ctx.shot(host, "host_revived")
    _revive_round(m, fails, "guest_revived", host, client, hme, cme)
    ctx.shot(client, "guest_revived")

    seen_h, seen_c = _messages(host, "ohl coop_revive"), _messages(client, "ohl coop_revive")
    m["revived_msgs_host_client"] = [sum(1 for x in seen_h if x.startswith("ohl coop_revived")),
                                     sum(1 for x in seen_c if x.startswith("ohl coop_revived"))]
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def _wait_until(cond, timeout, step=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return True
        time.sleep(step)
    return False


def hud_tags(ctx, profile="lan"):
    """The Overhaul HUD's tags, online: a player shows its nickname, anyone
    else an icon (enemy, teammate). Screenshots from both sides with a guard
    and an ally beside the guest; no script errors."""
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
        client = ctx.instance("client1")
        client.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, client)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old
    time.sleep(3.0)
    for _ in range(8):
        if not _in_dialogue(host):
            break
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)

    cme = client.bridge.me()
    cc = cme["common_id"]
    cpos = next(c["pos"] for c in host.bridge.state() if c["common_id"] == cc)
    team = next(c.get("teams") for c in host.bridge.state() if c["common_id"] == cc)
    # In front of the host's camera: a guard and an ally beyond the host, the guest between
    hme = host.bridge.me()
    cam = host.bridge.status()["camera_pos"]
    hp = hme["pos"]
    dx, dz = hp[0] - cam[0], hp[2] - cam[2]
    d = max(0.01, math.hypot(dx, dz))
    fx, fz = dx / d, dz / d
    ahead = lambda k, side: [hp[0] + fx * k - fz * side, hp[1] + 0.3, hp[2] + fz * k + fx * side]
    guard = host.bridge.call("spawn", pos=ahead(5.0, 2.0), teams="guard")["object_id"]
    ally = host.bridge.call("spawn", pos=ahead(5.0, -2.0), teams=team or "turner")["object_id"]
    time.sleep(1.0)
    # (the spawn sets the team before the clients have the character: again now;
    # and no fighting between the two, the feed shows only what we do)
    host.bridge.call("exec", target=ally, code=f'ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "{team or "turner"}");')
    for npc in (guard, ally):
        host.bridge.call("exec", target=npc, code="combat_allowed = false;")
    # A knockout shield on the guard (4, one broken): pips under its bar, on both machines
    host.bridge.call("exec", target=guard, code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetInt("Knockout Shield", 4); '
                     'ReadObjectFromID(this_mo.GetID()).UpdateScriptParams(); ko_shield = 3;')
    g = ahead(3.0, 0.0)
    client.bridge.call("exec", target=cme["id"], code=f"this_mo.position = vec3({g[0]}, {g[1]}, {g[2]}); this_mo.velocity = vec3(0.0f);")
    time.sleep(2.0)
    # The guard where the guest looks (its camera to its character, on 4 m)
    ccam = client.bridge.status()["camera_pos"]
    cpos2 = client.bridge.me()["pos"]
    vx, vz = cpos2[0] - ccam[0], cpos2[2] - ccam[2]
    vd = max(0.01, math.hypot(vx, vz))
    gx, gz = cpos2[0] + vx / vd * 4.0 + vz / vd * 1.2, cpos2[2] + vz / vd * 4.0 - vx / vd * 1.2
    g_on_host = next(c["pos"] for c in host.bridge.state() if c["id"] == guard)
    host.bridge.call("exec", target=guard, code=f"this_mo.position = vec3({gx}, {cpos2[1] + 0.5}, {gz}); this_mo.velocity = vec3(0.0f);")
    time.sleep(2.0)
    ctx.shot(client, "tags_client")
    ctx.shot(host, "tags_host")

    # The feed: the guest knocks the guard out; the ally falls (a hazard); a revive
    st = host.bridge.state()
    hc = next(c["id"] for c in st if c["common_id"] == cc)
    if guard is not None:
        host.bridge.call("exec", target=guard, code=f"attacked_by_id = {hc}; TakeDamage(0.3f);")
        time.sleep(0.5)
        host.bridge.call("exec", target=guard, code="SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);")
    if ally is not None:
        host.bridge.call("exec", target=ally, code="zone_killed = 1; SetKnockedOut(_dead); Ragdoll(_RGDL_INJURED);")
    host.bridge.call("exec", target="level", code=f'level.SendMessage("ohl coop_revived #{hme["id"]} #{hc}");')
    time.sleep(1.5)
    ctx.shot(client, "feed_client")
    ctx.shot(host, "feed_host")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def weather_sync(ctx, profile="lan"):
    """Weather and time of day (F9 > Weather) are the host's: set on the host,
    the guest's sky follows (sun colour, sky tint, ambient light). Shots of
    noon, dusk, night, rain and snow on both; off again = the level's own."""
    from .scenarios import session
    host, (client,) = session(ctx, 1, profile=profile)
    time.sleep(3.0)  # (mp_role on both)
    m, fails = {}, []
    sky = ('GetSunColor().x + " " + GetSunColor().y + " " + GetSunColor().z + " " + GetSkyTint().x + " " + '
           'GetSkyTint().y + " " + GetSkyTint().z + " " + GetSunAmbient()')

    def values(inst):
        got = _probe(inst, sky, "wxprobe")
        return [float(x) for x in got.split()] if got else []

    def weather(on, hour, kind):
        host.bridge.call("exec", target="level", code=(
            f'SetConfigValueBool("ow_time_on", {"true" if on else "false"}); SetConfigValueFloat("ow_hour", {hour}f); '
            f'SetConfigValueBool("ow_time_runs", false); SetConfigValueInt("ow_kind", {kind}); SetConfigValueFloat("ow_amount", 1.0f);'))
        time.sleep(3.0)  # (the host sends every 2 s)

    own = values(host)
    m["own"] = [round(v, 3) for v in own]
    for tag, hour, kind in [("noon", 12.0, 0), ("dusk", 18.6, 0), ("night", 23.0, 0), ("rain", 15.0, 2), ("snow", 10.0, 4)]:
        weather(True, hour, kind)
        ctx.shot(host, "host_" + tag)
        ctx.shot(client, "client_" + tag)
        hv, cv = values(host), values(client)
        m[tag] = {"host": [round(v, 3) for v in hv], "client": [round(v, 3) for v in cv]}
        if len(hv) != 7 or len(cv) != 7 or max(abs(a - b) for a, b in zip(hv, cv)) > 0.02:
            fails.append(f"{tag}: the guest's sky differs from the host's")
    weather(False, 12.0, 0)
    back = values(host)
    m["off_again"] = [round(v, 3) for v in back]
    if len(back) != 7 or max(abs(a - b) for a, b in zip(back, own)) > 0.001:
        fails.append("turned off, the host's sky isn't the level's own again")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def parallel_load(ctx, profile="lan"):
    """A level change: the guest starts loading when the host does (the host
    tells it at once), not after the host has finished. Judged from each log's
    "Load Level" line for the next level (whole seconds); a busy instance
    doesn't answer the bridge, so polling can't time it."""
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign=CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)

    nxt = LEVELS[1]
    host.bridge.call("exec", target="level", code='SendGlobalMessage("levelwin");')
    t0 = time.time()
    while time.time() - t0 < 180.0:
        try:
            if all(nxt in _level(i) and i.bridge.status()["level_loaded"] for i in (host, client)):
                break
        except Exception:
            pass  # (loading)
        time.sleep(0.5)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 90, what="the guest on the new level")
    time.sleep(3.0)
    hme, cme = host.bridge.me(), client.bridge.me()
    m["both_have_characters"] = hme is not None and cme is not None
    if not m["both_have_characters"]:
        fails.append("a player has no character on the new level")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")

    def load_start(inst):
        inst.stop()  # (the log is written out on exit)
        for line in inst.log().splitlines():
            if "Load Level:" in line and nxt.split("/")[-1] in line:
                hms = line.split(" ")[1].split(":")
                return int(hms[0]) * 3600 + int(hms[1]) * 60 + int(hms[2])
        return None

    hs, cs = load_start(host), load_start(client)
    m["guest_started_after_host_s"] = None if hs is None or cs is None else cs - hs
    if m["guest_started_after_host_s"] is None:
        fails.append("no Load Level line for the next level")
    elif m["guest_started_after_host_s"] > 2:
        fails.append("the guest started loading only when the host had finished")
    m["loads_seen"] = {"host": hs, "client": cs}
    return {"passed": not fails, "metrics": m, "notes": fails}


def objectives(ctx, profile="lan"):
    """Campaign objectives in the HUD (the mod), host and guest alike. Slaver
    Path (og_story 02): two optional fights, then "reach 0" leads (a marker);
    the checkpoint reached, "defeat 18 5456 5439" (3 to defeat). Then a Lugaru
    level: every enemy. Read from each log's "[Overhaul] objective:" lines,
    with shots of both screens."""
    mod = "D:/OvergrowthAI/mod/overhaul"
    m, fails = {}, []
    host = ctx.instance("host", mods=[mod])
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level="load_campaign_level slaver_camp", campaign=CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1", mods=[mod])
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)

    def settle():
        time.sleep(2.0)
        for _ in range(8):
            if not _in_dialogue(host):
                break
            host.bridge.hold(steps=10, skip_dialogue=1.0)
            time.sleep(1.5)
        time.sleep(2.0)

    settle()
    ctx.shot(host, "start_host")
    ctx.shot(client, "start_client")

    host.bridge.call("exec", target="level", code='ReadObjectFromID(74).ReceiveScriptMessage("player_entered_checkpoint 0");')
    time.sleep(3.0)
    settle()
    ctx.shot(host, "defeat_host")
    ctx.shot(client, "defeat_client")

    host.bridge.call("load_level", path="set_campaign com-wolfire-lugaru-campaign")
    host.bridge.call("load_level", path="load_campaign_level village")
    t0 = time.time()
    while time.time() - t0 < 180.0:
        try:
            if all("Village" in _level(i) and i.bridge.status()["level_loaded"] for i in (host, client)):
                break
        except Exception:
            pass  # (loading)
        time.sleep(0.5)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 90, what="the guest on the Lugaru level")
    settle()
    ctx.shot(host, "lugaru_host")
    ctx.shot(client, "lugaru_client")

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    for name, inst in (("host", host), ("client", client)):
        inst.stop()  # (the log is written out on exit)
        lines = [l.split("objective: ", 1)[1].strip() for l in inst.log().splitlines() if "[Overhaul] objective:" in l]
        m[name] = lines
        wants = [("the checkpoint", lambda x: x.startswith("Go to the marker (marker)") and "optional" in x),
                 ("3 to defeat", lambda x: x.startswith("Defeat the enemies") and " 3 to defeat" in x),
                 ("Lugaru", lambda x: x.startswith("Defeat every enemy") or x.startswith("Defeat the last enemy"))]
        for what, ok in wants:
            if not any(ok(x) for x in lines):
                fails.append(f"{name}: no objective for {what}")
    return {"passed": not fails, "metrics": m, "notes": fails}
