"""Online arena: arena_pvp. Two players in Magma Arena (online_arena.as), with
the Overhaul mod:

    - the host alone: no AI, only its own character; "Waiting for another player";
    - a client joins: a match starts, both stand on different spots, on their
      own teams, both up; a weapon match arms both, on both screens;
    - the host goes down: the client wins the round, on both screens and in the
      points; the next round (or match) gets both up again.
"""

import math
import os
import time

from .scenarios import PROFILES, join, script_errors, wait_loaded

ARENA = "arenas/Magma_Arena.xml"
STUCCO = "arenas/stucco_courtyard_arena.xml"  # arena_level.as: spawn markers, no battle setups
CAMPAIGN = "com-wolfire-arena"


class Log:
    """Level messages seen by an instance (reading them clears them: keep all)."""

    def __init__(self, inst):
        self.inst, self.lines = inst, []

    def drain(self):
        self.lines += self.inst.bridge.call("level_messages", prefix="ohl arena")["messages"]
        return self.lines

    def texts(self):
        return [l[len("ohl arena text "):] for l in self.drain() if l.startswith("ohl arena text")]

    def scores(self):
        return [l for l in self.drain() if l.startswith("ohl arena score")]


def _chars(inst):
    return inst.bridge.state()


def _wait(cond, timeout, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return True
        time.sleep(step)
    return False


def _score(line):
    """"ohl arena score <max> #id pts ..." -> (max, {id: pts})"""
    t = line.split()
    return int(t[3]), {int(t[i][1:]): int(t[i + 1]) for i in range(4, len(t) - 1, 2) if t[i].startswith("#")}


def arena_pvp(ctx, profile="typical", arena=ARENA):
    from .online_mod_suite import OVERHAUL

    m, fails = {}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        hlog = Log(host)
        host.bridge.call("host", level=arena, campaign=CAMPAIGN)
        wait_loaded(host)
        host.bridge.call("exec", target="level", code='SetConfigValueInt("oo_arena_bots", 0);')  # (players only)
        time.sleep(4.0)
        m["host_alone_characters"] = len(_chars(host))
        if m["host_alone_characters"] != 1:
            fails.append(f"alone the host has {m['host_alone_characters']} characters (AI spawned?)")
        m["host_alone_text"] = hlog.texts()[-1:]
        if "Waiting for another player" not in hlog.texts():
            fails.append(f"alone the host doesn't wait for players: {hlog.texts()[-3:]}")

        client = ctx.instance("client1")
        client.bridge.call("net_sim", **PROFILES[profile])
        clog = Log(client)
        join(ctx, host, client)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old

    # A match starts
    if not _wait(lambda: "Fight!" in hlog.texts() and "Fight!" in clog.texts(), 30.0):
        fails.append(f"no fight: host {hlog.texts()[-4:]}, client {clog.texts()[-4:]}")
        return {"passed": False, "metrics": m, "notes": fails}
    time.sleep(1.0)
    hme, cme = host.bridge.me()["id"], client.bridge.me()["id"]
    hchars, cchars = _chars(host), _chars(client)
    m["characters_host_client"] = [len(hchars), len(cchars)]
    if m["characters_host_client"] != [2, 2]:
        fails.append(f"characters host/client {m['characters_host_client']} (AI spawned, or a player missing)")
    # The client's characters are the host's, one for one (not a stand-in of its own)
    m["common_ids_host_client"] = [sorted(c["id"] for c in hchars), sorted(c["common_id"] for c in cchars)]
    if m["common_ids_host_client"][0] != m["common_ids_host_client"][1]:
        fails.append(f"the client's characters aren't the host's: {m['common_ids_host_client']}")
    paths_host = {c["id"]: c.get("char_path") for c in hchars}
    paths_client = {c["common_id"]: c.get("char_path") for c in cchars}
    m["char_paths_host_client"] = [paths_host, paths_client]
    if paths_host != paths_client:
        fails.append(f"a client character is another than the host's (a stand-in?): {paths_host} vs {paths_client}")
    me_c = client.bridge.me()
    client_on_host = next((c for c in hchars if c["common_id"] == me_c["common_id"]), None)
    host_me = next(c for c in hchars if c["id"] == hme)
    if client_on_host is not None:
        import math
        m["players_apart_m"] = round(math.dist(host_me["pos"], client_on_host["pos"]), 1)
        m["teams"] = [host_me.get("teams"), client_on_host.get("teams")]
        if m["players_apart_m"] < 2.0:
            fails.append(f"players on the same spot ({m['players_apart_m']} m)")
        if m["teams"][0] == m["teams"][1]:
            fails.append(f"players on one team: {m['teams']}")
    m["up_host_client"] = [all(c["knocked_out"] == 0 for c in hchars), all(c["knocked_out"] == 0 for c in cchars)]
    if m["up_host_client"] != [True, True]:
        fails.append(f"not everyone up at the fight: {m['up_host_client']}")

    # The client plays its own: its camera on it, and it runs where told
    me_c = client.bridge.me()
    cam = client.bridge.status().get("camera_pos")
    m["client_camera_from_own_m"] = round(math.dist(cam, me_c["pos"]), 1) if cam else None
    if cam is None or math.dist(cam, me_c["pos"]) > 5.0:
        fails.append(f"the client's camera isn't on its character ({m['client_camera_from_own_m']} m)")
    start = me_c["pos"]
    client.bridge.hold(up=1.0)
    time.sleep(1.5)
    client.bridge.release("up")
    m["client_ran_m"] = round(math.dist(start, client.bridge.me()["pos"]), 1)
    if m["client_ran_m"] < 1.0:
        fails.append(f"the client's character doesn't run ({m['client_ran_m']} m)")

    scores = clog.scores()
    max_points = _score(scores[-1])[0] if scores else -1
    if scores:  # the points name two different characters there, its own one of them
        ids = list(_score(scores[-1])[1].keys())
        m["client_score_ids"] = ids
        if len(set(ids)) != 2 or cme not in ids:
            fails.append(f"the client's points don't name both players: {scores[-1]} (own {cme})")
    m["match_points"] = max_points
    m["intro"] = [t for t in clog.texts() if t][:3]
    if max_points == 1:  # a weapon match: each holds one, everywhere
        for inst, me_id, other_id in ((host, hme, client_on_host and client_on_host["id"]),
                                      (client, cme, next((c["id"] for c in cchars if c["id"] != cme), None))):
            held = [it["held_by"] for it in inst.bridge.call("state").get("items", [])]
            m[f"{inst.name}_held_by"] = held
            if me_id not in held or other_id not in held:
                fails.append(f"{inst.name}: a player is unarmed in a weapon match (held by {held})")

    # The host goes down: the client takes the round
    host.bridge.call("exec", target="level",
                     code=f'ReadCharacterID({hme}).Execute("SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);");')
    won = lambda log: any(t.endswith("wins the round!") for t in log.texts())
    if not _wait(lambda: won(hlog) and won(clog), 10.0):
        fails.append(f"no round winner: host {hlog.texts()[-3:]}, client {clog.texts()[-3:]}")
    m["round_text"] = [t for t in clog.texts() if t.endswith("wins the round!")][-1:]
    scores = clog.scores()
    if scores:
        mx, pts = _score(scores[-1])
        m["client_points_on_client"] = pts.get(cme)
        if pts.get(cme) != 1:
            fails.append(f"the client's point isn't on its screen: {scores[-1]}")

    # Next round or match: both up again
    n_fights = clog.texts().count("Fight!")
    if not _wait(lambda: clog.texts().count("Fight!") > n_fights, 15.0):
        fails.append(f"no next round: {clog.texts()[-4:]}")
    time.sleep(1.0)
    m["up_again_host_client"] = [all(c["knocked_out"] == 0 for c in _chars(host)), all(c["knocked_out"] == 0 for c in _chars(client))]
    if m["up_again_host_client"] != [True, True]:
        fails.append(f"not everyone up for the next round: {m['up_again_host_client']}")
    m["texts_client"] = [t for t in clog.texts() if t][-8:]
    ctx.shot(client, "arena_client")

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


WATERFALL = "arenas/waterfall_arena.xml"  # its setups are weapon fights


def _held_drawn_away(inst, limit=1.5):
    """Held items drawn more than `limit` m from the character holding them here."""
    r = inst.bridge.call("state")
    pos = {c["id"]: c["pos"] for c in r["characters"]}
    away = []
    for it in r.get("items", []):
        h = it.get("held_by", -1)
        if h in pos and "draw_pos" in it and math.dist(it["draw_pos"], pos[h]) > limit:
            away.append((it.get("label"), round(math.dist(it["draw_pos"], pos[h]), 1)))
    return away


def _held(inst):
    """{character's common id: [labels of the items it holds]} on this machine."""
    r = inst.bridge.call("state")
    common = {c["id"]: c["common_id"] for c in r["characters"]}
    out = {}
    for it in r.get("items", []):
        if it.get("held_by", -1) in common:
            out.setdefault(common[it["held_by"]], []).append(it.get("label", "?"))
    return {k: sorted(v) for k, v in out.items()}


def arena_weapons(ctx, profile="typical", matches=4):
    """Weapons over several matches (old ones deleted, new ones made each
    round): every player holds the same weapon on both screens, every time."""
    from .online_mod_suite import OVERHAUL

    m, fails = {"rounds": []}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        hlog = Log(host)
        host.bridge.call("host", level=WATERFALL, campaign=CAMPAIGN)
        wait_loaded(host)
        host.bridge.call("exec", target="level", code='SetConfigValueInt("oo_arena_bots", 0);')  # (players only)
        client = ctx.instance("client1")
        client.bridge.call("net_sim", **PROFILES[profile])
        clog = Log(client)
        join(ctx, host, client)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old

    fights = 0
    for n in range(matches):
        if not _wait(lambda: clog.texts().count("Fight!") > fights and hlog.texts().count("Fight!") > fights, 40.0):
            fails.append(f"round {n + 1}: no fight ({clog.texts()[-3:]})")
            break
        fights = clog.texts().count("Fight!")
        time.sleep(2.0)
        held_h, held_c = _held(host), _held(client)
        m["rounds"].append({"host": held_h, "client": held_c})
        if held_h != held_c:
            fails.append(f"round {n + 1}: held weapons differ: host {held_h}, client {held_c}")
        away = [_held_drawn_away(host), _held_drawn_away(client)]
        m["rounds"][-1]["drawn_away_host_client"] = away
        if away != [[], []]:
            fails.append(f"round {n + 1}: held weapons drawn away from their holder (host, client): {away}")
        hme = host.bridge.me()["id"]
        host.bridge.call("exec", target="level",
                         code=f'ReadCharacterID({hme}).Execute("SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);");')
    ctx.shot(client, "weapons_client")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


def _holder(inst, item_common):
    """Common id of the character holding this item here, -1 nobody, None no such item."""
    r = inst.bridge.call("state")
    common = {c["id"]: c["common_id"] for c in r["characters"]}
    it = next((i for i in r.get("items", []) if i["common_id"] == item_common), None)
    if it is None:
        return None
    return common.get(it["held_by"], -1) if it["held_by"] != -1 else -1


def _item_pos(inst, item_common):
    return next((i["pos"] for i in inst.bridge.call("state").get("items", []) if i["common_id"] == item_common), None)


def _pick_up(inst, me_id, item_common, other, want_holder, seconds=15.0):
    """Walk to the item and press the pick-up key until both machines agree."""
    from .combat_suite import _face_and_go
    t0 = time.time()
    while time.time() - t0 < seconds:
        if _holder(inst, item_common) == want_holder and _holder(other, item_common) == want_holder:
            break
        pos = _item_pos(inst, item_common)
        if pos is not None and _face_and_go(inst, me_id, pos, 0.8):
            inst.bridge.hold(steps=30, drop=1.0)
        time.sleep(0.2)
    inst.bridge.release_all()
    time.sleep(1.0)


def arena_pickups(ctx, profile="typical", arena=STUCCO):
    """Weapon pick-ups in an arena: hands emptied; a sword beside the guest;
    the guest picks it up, drops it; the host picks it up. After each step
    both machines agree who holds it (and nobody else holds anything)."""
    from .online_mod_suite import OVERHAUL

    m, fails = {}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        hlog = Log(host)
        host.bridge.call("host", level=arena, campaign=CAMPAIGN)
        wait_loaded(host)
        host.bridge.call("exec", target="level", code='SetConfigValueInt("oo_arena_bots", 0);')  # (players only)
        client = ctx.instance("client1")
        client.bridge.call("net_sim", **PROFILES[profile])
        clog = Log(client)
        join(ctx, host, client)
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old

    if not _wait(lambda: "Fight!" in hlog.texts() and "Fight!" in clog.texts(), 30.0):
        return {"passed": False, "metrics": m, "notes": ["no fight"]}
    rounds = lambda: hlog.drain().count("ohl arena fight")  # once per round (texts are resent)
    fights = rounds()
    time.sleep(1.5)
    hme, cme = host.bridge.me()["id"], client.bridge.me()["id"]
    hc, cc = host.bridge.me()["common_id"], client.bridge.me()["common_id"]

    def step(name, want):
        h, c = _held(host), _held(client)
        m[name] = {"host": h, "client": c}
        if rounds() != fights:
            fails.append(f"{name}: a new round started meanwhile (someone fell?): the check is void")
        elif h != want or c != want:
            fails.append(f"{name}: held host {h}, client {c}, wanted {want}")
        away = [_held_drawn_away(host), _held_drawn_away(client)]
        if away != [[], []]:
            fails.append(f"{name}: held weapons drawn away from their holder (host, client): {away}")

    # 1. Empty hands (each machine drops its own character's)
    for inst, me in ((host, hme), (client, cme)):
        inst.bridge.call("exec", target=me, code="DropWeapon(); DropWeapon();")
    time.sleep(2.0)
    step("hands_emptied", {})

    # 2. A sword beside the guest; the guest picks it up
    cpos = next(c["pos"] for c in host.bridge.state() if c["common_id"] == cc)
    sword = host.bridge.call("spawn", path="Data/Items/DogWeapons/DogSword.xml",
                             pos=[cpos[0] + 4.0, cpos[1] + 0.6, cpos[2]])["object_id"]  # (away from the dropped ones)
    if not _wait(lambda: _holder(client, sword) is not None, 10.0):
        fails.append("the sword never reached the guest")
        return {"passed": False, "metrics": m, "notes": fails}
    time.sleep(1.0)
    _pick_up(client, cme, sword, host, cc)
    step("guest_picked_up", {cc: ["sword"]})

    # 3. The guest drops it
    client.bridge.call("exec", target=cme, code="DropWeapon();")
    time.sleep(2.0)
    step("guest_dropped", {})
    # Where it comes to rest, on both (wait for it to stop on the host)
    last, t0 = _item_pos(host, sword), time.time()
    while time.time() - t0 < 6.0:
        time.sleep(0.5)
        now = _item_pos(host, sword)
        if last and now and math.dist(last, now) < 0.01:
            break
        last = now
    time.sleep(0.5)
    m["sword_at_host_client"] = [_item_pos(host, sword), _item_pos(client, sword)]
    hp, cp = m["sword_at_host_client"]
    m["sword_gap_m"] = round(math.dist(hp, cp), 3) if hp and cp else None
    if hp and cp and math.dist(hp, cp) > 0.1:
        fails.append(f"the dropped sword lies in different places: {hp} vs {cp}")

    # 4. The host comes and picks it up
    if hp:
        host.bridge.call("exec", target=hme,
                         code=f"this_mo.position = vec3({hp[0] + 2.0}, {hp[1] + 0.5}, {hp[2]}); this_mo.velocity = vec3(0.0f);")
        time.sleep(1.0)
    _pick_up(host, hme, sword, client, hc)
    step("host_picked_up", {hc: ["sword"]})

    ctx.shot(client, "pickups_client")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}
