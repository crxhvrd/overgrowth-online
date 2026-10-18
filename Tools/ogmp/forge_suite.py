"""forged: a guest that cheats. Every message a modified game could send to
take over the session, as the security audit (2026-10-18) found them; after
each, the host must still run, unchanged where the guest had no say.

    malformed packages (garbage, compressed garbage, message types out of
    range) | removing the host's character | spawning an entity | a script
    in a character path | taking over the host's character from afar for
    ever | rewriting the host's player state | a 1000x hit on the host |
    teleporting the host's character | rewriting a level script param |
    a level message that loads a level | reviving oneself from afar.

    python -I Tools/ogmp.py run forged
"""

import math
import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded


def _char(inst, cid):
    return next((c for c in inst.bridge.state() if c["id"] == cid), None)


def forged(ctx, profile="lan"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    hme = host.bridge.me()
    h = hme["id"]
    gme = g.bridge.me()
    gc = next(c["common_id"] for c in g.bridge.state() if c["id"] == gme["id"])
    gp = gme["pos"]
    # The guest walks 30 m away from the host first (the takeover needs distance)
    host.bridge.call("exec", target="level", code=f"Online_PlaceAvatar({h}, vec3({gp[0] + 30.0:.2f}, {gp[1] + 0.5:.2f}, {gp[2]:.2f}), vec3(0, 0, 1));")
    time.sleep(2.0)
    before = _char(host, h)
    path_before = before.get("char_path")
    npc = host.bridge.call("spawn", pos=[before["pos"][0] + 2.0, before["pos"][1] + 0.3, before["pos"][2]], teams="host")["object_id"]
    host.bridge.call("exec", target=npc, code="combat_allowed = false;")  # (a bystander: the attacks are the guest's)
    host.bridge.call("exec", target=h, code='ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "host");')
    time.sleep(1.5)
    before = _char(host, h)
    objs_before = len(host.bridge.state())

    trail = []
    m["trail"] = trail

    def step(name, **kw):
        try:
            g.bridge.call("forge", kind=name, **kw)
        except Exception as e:
            fails.append(f"forge {name}: {e}")
        time.sleep(1.0)
        if not host.alive():
            fails.append(f"the host crashed after: {name}")
            return False
        c = _char(host, h)
        if c is not None:
            trail.append([name, round(math.dist(before["pos"], c["pos"]), 2), c.get("knocked_out"), round(c.get("temp_health", 0), 2)])
        return True

    attacks = [
        ("malformed", dict(mode=0)), ("malformed", dict(mode=1)), ("malformed", dict(mode=2)), ("malformed", dict(mode=3)),
        ("remove_object", dict(target=h)), ("remove_object", dict(target=npc)),
        ("create_entity", dict(path="Data/Objects/IGF_Characters/IGF_GuardActor.xml", pos=before["pos"])),
        ("avatar_character", dict(target=h, path='Data/Characters/rat.xml"; this_mo.position = vec3(0,-100,0); string x="')),
        ("avatar_character", dict(target=gc, path='Data/Characters/rat.xml"; LoadLevel("back_to_menu"); string x="')),
        ("owner_override", dict(target=h)),
        ("set_player_state", dict(player=0, target=npc)),
        ("hit", dict(target=h, attacker=gc, pos=before["pos"])),
        ("teleport", dict(target=h, pos=[0.0, -50.0, 0.0])),
        ("teleport", dict(target=npc, pos=[0.0, -50.0, 0.0])),
        ("sp_string", dict(target=npc, key="Teams", value="hacked")),
        ("level", dict(msg="ohr 0 coop_revive_done #" + str(gc) + " #" + str(gc))),
        ("level", dict(msg="loadlevel Data/Levels/Project60/1_wood_chopping.xml")),
    ]
    m["attacks"] = len(attacks)
    for name, kw in attacks:
        if not step(name, **kw):
            break
    time.sleep(2.0)

    if host.alive():
        after = _char(host, h)
        n = _char(host, npc)
        m["host_char_moved_m"] = round(math.dist(before["pos"], after["pos"]), 2) if after else None
        m["host_char_path"] = [path_before, after.get("char_path") if after else None]
        m["host_health"] = {k: after.get(k) for k in ("temp_health", "blood_health", "knocked_out")} if after else None
        m["npc_exists"] = n is not None
        m["npc_team"] = host.bridge.call("exec", target=npc, code='level.SendMessage("probe_team " + ReadObjectFromID(this_mo.GetID()).GetScriptParams().GetString("Teams"));') and None
        team = host.bridge.call("level_messages", prefix="probe_team")["messages"]
        m["npc_team"] = team[-1] if team else None
        m["objects"] = [objs_before, len(host.bridge.state())]
        st = host.bridge.status()
        m["host_level"] = st.get("level")
        names = [p["name"] for p in st["online"]["players"]]
        m["player_names"] = names
        m["guest_still_connected"] = len(st["online"]["players"]) == 2
        if after is None:
            fails.append("the host's character was removed")
        elif m["host_char_moved_m"] > 1.0:
            fails.append(f"the host's character moved {m['host_char_moved_m']} m")
        if after is not None and after.get("char_path") != path_before:
            fails.append(f"the host's character was changed: {m['host_char_path']}")
        if after is not None and (after.get("knocked_out") or after.get("temp_health", 1.0) < 0.5):
            fails.append(f"the forged hit hurt the host: {m['host_health']}")
        if not m["npc_exists"]:
            fails.append("the host's NPC was removed")
        if m["npc_team"] and "hacked" in m["npc_team"]:
            fails.append("a guest rewrote a script param of the host's NPC")
        if m["objects"][1] > m["objects"][0]:
            fails.append(f"a guest spawned characters: {m['objects']}")
        if "Hacked" in names:
            fails.append("a guest rewrote a player state")
        if "wood_chopping" in (m["host_level"] or ""):
            fails.append("a guest changed the host's level")
    return {"passed": not fails, "metrics": m, "notes": fails}
