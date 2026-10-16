"""Phase 5: the level-script channel the Overhaul mod uses online.

    level_channel: "mp_role" reaches both machines; an "ohl" message from the
    host reaches the client with ids translated; an "ohr" request from the
    client reaches the host tagged with the sender; an object a host script
    creates appears on the client.
"""

import time

from .scenarios import script_errors, session


def _messages(inst, prefix):
    return inst.bridge.call("level_messages", prefix=prefix)["messages"]


def _common_of(inst, local_id):
    return next((c["common_id"] for c in inst.bridge.state() if c["id"] == local_id), None)


def level_channel(ctx, profile="typical"):
    m, fails = {}, []
    host, (c1,) = session(ctx, 1, profile=profile)
    time.sleep(1.0)
    for inst in (host, c1):
        _messages(inst, "")  # start clean

    # Roles
    time.sleep(3.0)
    roles = {"host": _messages(host, "mp_role"), "client": _messages(c1, "mp_role")}
    m["roles"] = {k: (v[-1] if v else None) for k, v in roles.items()}
    if not roles["host"] or "mp_role host" not in roles["host"][-1]:
        fails.append("the host's level scripts never learned their role")
    if not roles["client"] or "mp_role client" not in roles["client"][-1]:
        fails.append("the client's level scripts never learned their role")
    my_local = c1.bridge.me()["id"]
    if roles["client"] and f"#{my_local} " not in roles["client"][-1]:
        fails.append(f"the client's role names the wrong character: {roles['client'][-1]} (ours {my_local})")

    # Host -> clients, ids translated
    hid = host.bridge.me()["id"]
    cid_on_host = next(c["id"] for c in host.bridge.state() if c["common_id"] == _common_of(c1, my_local))
    host.bridge.call("exec", target="level", code=f'level.SendMessage("ohl test #{cid_on_host} 42");')
    time.sleep(1.5)
    got = _messages(c1, "ohl test")
    m["ohl_on_client"] = got
    if got != [f"ohl test #{my_local} 42"]:
        fails.append(f"ohl: expected 'ohl test #{my_local} 42' on the client, got {got}")

    # Client -> host, tagged with the sender, ids translated
    hid_on_client = next(c["id"] for c in c1.bridge.state() if c["common_id"] == hid)
    c1.bridge.call("exec", target="level", code=f'level.SendMessage("ohr ping #{hid_on_client}");')
    time.sleep(1.5)
    got = _messages(host, "ohr")
    m["ohr_on_host"] = got
    if len(got) != 1 or not got[0].startswith("ohr ") or not got[0].endswith(f" ping #{hid}"):
        fails.append(f"ohr: expected 'ohr <player> ping #{hid}' on the host, got {got}")
    if _messages(c1, "ohr"):
        fails.append("ohr: the client's own level scripts got its request too")

    # A host script creates an item: the client gets it
    before = {it["common_id"] for it in c1.bridge.call("state").get("items", [])}
    host.bridge.call("exec", target="level",
                     code='int id = CreateObject("Data/Items/flint_knife.xml", true); '
                          f'ReadObjectFromID(id).SetTranslation(ReadCharacterID({hid}).position + vec3(1.0f, 1.0f, 0.0f));')
    time.sleep(2.0)
    after = {it["common_id"] for it in c1.bridge.call("state").get("items", [])}
    m["items_created_on_client"] = len(after - before)
    if len(after - before) != 1:
        fails.append(f"CreateObject on the host: {len(after - before)} new items on the client (expected 1)")

    errs = script_errors(host) + script_errors(c1)
    if errs:
        fails.append(f"script errors: {errs[:3]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


OVERHAUL = "D:/OvergrowthAI/mod/overhaul"
BATTLE_MAP = "overhaul_battles/forgotten_plains.xml"


def _last_state(inst):
    """The last battle state broadcast seen here: (phase, winner, [alive per team])."""
    msgs = _messages(inst, "ohl aiob state")
    if not msgs:
        return None
    t = msgs[-1].split()
    teams = [int(t[7 + i * 3]) for i in range((len(t) - 7) // 3)]
    return int(t[3]), int(t[4]), teams


def _player_ids(inst):
    """Every player's character here, from the level scripts' role message."""
    msgs = _messages(inst, "mp_role")
    t = msgs[-1].split() if msgs else []
    return {int(t[k][1:]) for k in range(4, len(t) - 1, 2)}


def pvp_battle(ctx, profile="typical"):
    """Large Battles online: a host and two clients with the Overhaul mod on a
    battle map, two teams of NPC fighters, every player on a team. The host
    runs the battle; the clients mirror it: same fighters, same standings,
    same winner. Client 1 picks team 0 in the lobby (the host's team), so
    client 2 lands on the emptier team 1. Every fighter has the same HUD name
    on both clients; a client's Trainer is off unless the host allows it."""
    import os
    from .bots import ChaseBot
    from .scenarios import PROFILES, join, wait_loaded

    m, fails = {}, []
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        keys = {"aiob_pending": "true", "aiob_level": BATTLE_MAP, "aiob_teams": "2", "aiob_player_mode": "1",
                "aiob_player_team": "0", "aiob_t0_count": "3", "aiob_t1_count": "3", "aiob_waves": "1",
                "aiob_time_limit": "1", "aiob_cleanup": "0", "aiob_manual_place": "false", "aiob_slowmo_finish": "false",
                "aiob_online_wait": "true", "aiob_online_players": "3"}
        host = ctx.instance("host", keys=keys)
        host.bridge.call("net_sim", **PROFILES[profile])
        host.bridge.call("host", level=BATTLE_MAP)
        wait_loaded(host)
        clients = []
        for i in range(2):
            c = ctx.instance(f"client{i + 1}")
            c.bridge.call("net_sim", **PROFILES[profile])
            join(ctx, host, c)
            clients.append(c)
            if i == 0:
                # The lobby: client 1 picks the host's team
                t0 = time.time()
                while time.time() - t0 < 20.0 and not _messages(c, "ohl aiob lobby"):
                    time.sleep(0.5)
                me = c.bridge.me()["id"]
                c.bridge.call("exec", target="level", code=f'level.SendMessage("ohr aiob pick #{me} 0");')
                time.sleep(2.5)
                seen = _messages(c, "ohl aiob lobby")
                m["lobby_after_pick"] = seen[-1] if seen else None
                if not seen or f"#{me} 0" not in seen[-1]:
                    fails.append(f"the host's lobby doesn't show client 1's pick: {seen[-1:] }")
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old

    # Everyone on a team, every fighter everywhere
    t0 = time.time()
    while time.time() - t0 < 60.0:
        teams = [next((c.get("teams", "") for c in inst.bridge.state() if c["id"] == inst.bridge.me()["id"]), "")
                 for inst in [host] + clients]
        if all(t.startswith("aiob_team_") for t in teams) and all(_messages(c, "ohl aiob cfg") or True for c in clients):
            break
        time.sleep(1.0)
    m["player_teams"] = teams
    if not all(t.startswith("aiob_team_") for t in teams):
        fails.append(f"not every player got a team: {teams}")

    time.sleep(6.0)  # spawning and the countdown

    def fighters(inst):
        return sorted(c["common_id"] for c in inst.bridge.state() if c.get("teams", "").startswith("aiob_team_"))

    fh = fighters(host)
    m["fighters"] = [len(fh)] + [len(fighters(c)) for c in clients]
    if any(fighters(c) != fh for c in clients):
        fails.append(f"the clients don't have the host's fighters: {m['fighters']}")
    if not (teams[0] == "aiob_team_0" and teams[1] == "aiob_team_0" and teams[2] == "aiob_team_1"):
        fails.append(f"teams: expected host 0, client 1 0 (picked), client 2 1 (emptiest), got {teams}")

    # HUD names: the host's, the same on both clients, one each
    time.sleep(5.5)  # the full list goes out every 5 s
    names = []
    for c in clients:
        got = {}
        for msg in _messages(c, "ohl hud names"):
            t = msg.split()
            for k in range(3, len(t) - 1, 2):
                got[int(t[k][1:])] = t[k + 1]
        time.sleep(2.1)  # a role message (it lists the players)
        pids = _player_ids(c)
        common = {ch["id"]: ch["common_id"] for ch in c.bridge.state()}
        by_common = {common[i]: n for i, n in got.items() if i in common and i not in pids}
        npcs = [ch["common_id"] for ch in c.bridge.state() if ch.get("teams", "").startswith("aiob_team_")
                and ch["id"] not in pids]
        if len(pids) != 3:
            fails.append(f"{c.name}: the role message lists {len(pids)} players, not 3")
        missing = [x for x in npcs if x not in by_common]
        if missing:
            fails.append(f"{c.name}: no HUD name for fighters {missing}")
        names.append(by_common)
    m["hud_names"] = names[0]
    if names[0] != names[1]:
        fails.append(f"the clients name the fighters differently: {names}")
    if len(set(names[0].values())) != len(names[0]):
        fails.append(f"HUD names repeat: {names[0]}")

    # Trainer: the host hasn't allowed it, so a client's own options are off
    for c in clients:
        me = c.bridge.me()["id"]
        c.bridge.call("exec", target="level",
                      code=f'level.SendMessage("probe_trainer " + (ReadCharacterID({me}).GetBoolVar("otr_net_blocked") ? 1 : 0));')
    time.sleep(0.5)
    blocked = [_messages(c, "probe_trainer") for c in clients]
    m["trainer_blocked"] = blocked
    if any(b != ["probe_trainer 1"] for b in blocked):
        fails.append(f"a client's Trainer isn't blocked: {blocked}")

    # Fight it out (the players too), until the host calls it or the clock runs out
    bots = [ChaseBot(inst, seed=7 + i, enemies_only=True) for i, inst in enumerate([host] + clients)]
    for inst in [host] + clients:
        _messages(inst, "")
    t0 = time.time()
    over = False
    while time.time() - t0 < 75.0 and not over:
        for bot in bots:
            bot.step()
        st = _last_state(host)
        over = st is not None and st[0] == 6  # kBattleOver
    for bot in bots:
        bot.inst.bridge.release_all()
    time.sleep(2.0)

    states = [_last_state(inst) for inst in [host] + clients]
    m["final_state_host_c1_c2"] = states
    m["battle_over"] = over
    ctx.shot(clients[0], "battle_client")
    if not over:
        fails.append("the battle never ended (time limit 1 min)")
    if states[0] is None or any(s != states[0] for s in states[1:]):
        fails.append(f"the clients' scoreboards differ from the host's: {states}")

    errs = script_errors(host) + sum((script_errors(c) for c in clients), [])
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}
