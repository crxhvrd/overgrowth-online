"""The script API for mods (Docs/Markdown/Overgrowth Online Script API.md): a host and two guests,
each with a test mod whose level hook listens and says what it hears as level messages
("apitest ...", test/mods/online_api_test). Checks: the version, ids, the player list, names and
characters, shared ids, every route a message can take (to everyone, to the host, to one player,
to oneself, guest to guest and back), long and non-ASCII text, refused messages, and the
online:joined / online:left events."""

import time

from .instance import ROOT
from .scenarios import LEVEL, join, wait_loaded

MOD = ROOT + "/test/mods/online_api_test"


def _run(inst, code):
    inst.bridge.call("exec", target="level", code=code)


def _heard(inst):
    return inst.bridge.call("level_messages", prefix="apitest")["messages"]


def _report(inst, tag, expr):
    """Run an expression in the level script; its value as text."""
    inst.bridge.call("level_messages")  # (clear)
    _run(inst, f'level.SendMessage("apitest {tag} " + ({expr}));')
    time.sleep(0.3)
    for line in _heard(inst):
        if line.startswith(f"apitest {tag} "):
            return line[len(f"apitest {tag} "):]
    return None


def online_api(ctx):
    m, fails = {}, []
    host = ctx.instance("host", mods=[MOD])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    c1 = ctx.instance("client1", mods=[MOD])
    join(ctx, host, c1)
    c2 = ctx.instance("client2", mods=[MOD])
    join(ctx, host, c2)
    time.sleep(2.0)
    insts = {"host": host, "c1": c1, "c2": c2}

    # --- events at the start: everyone hears everyone join (themselves too) ----------------
    joined = {name: sorted({int(l.split()[3]) for l in _heard(inst) if l.startswith("apitest joined ")})
              for name, inst in insts.items()}
    m["joined_events"] = joined

    # --- ids, version, list, characters -------------------------------------------------------
    ids = {}
    for name, inst in insts.items():
        r = _report(inst, "me", 'Online_ApiVersion() + " " + Online_GetOwnPlayerID() + " " + Online_GetHostPlayerID() + " " + '
                                'Online_GetPlayerIDs().length()')
        if r is None:
            fails.append(f"{name}: no answer from the level script")
            continue
        version, own, host_id, count = (int(x) for x in r.split())
        ids[name] = own
        m[f"{name}_me"] = {"version": version, "own": own, "host": host_id, "players": count}
        if version != 1 or host_id != 0 or count != 3:
            fails.append(f"{name}: version {version}, host {host_id}, {count} players (want 1, 0, 3)")
    if len(ids) == 3:
        if ids["host"] != 0 or len(set(ids.values())) != 3 or min(ids["c1"], ids["c2"]) < 1:
            fails.append(f"player ids: {ids}")
        for name, want in joined.items():
            if want != sorted(ids.values()):
                fails.append(f"{name}: joined events for {want}, players are {sorted(ids.values())}")

        # every machine agrees on each player's character (by shared id) and back to the player
        shared = {}
        for name, inst in insts.items():
            for who, pid in ids.items():
                r = _report(inst, "char", f'Online_ToSharedID(Online_GetPlayerCharacter({pid})) + " " + '
                                          f'Online_GetCharacterPlayer(Online_GetPlayerCharacter({pid})) + " " + '
                                          f'(Online_GetPlayerName({pid}).length() > 0)')
                if r is None:
                    fails.append(f"{name}: no answer about {who}")
                    continue
                sid, back, named = r.split()
                shared.setdefault(who, set()).add(int(sid))
                if int(back) != pid or named != "true" or int(sid) < 0:
                    fails.append(f"{name} about {who}: character {sid}, its player {back}, named {named}")
        m["shared_character_ids"] = {k: sorted(v) for k, v in shared.items()}
        for who, sids in shared.items():
            if len(sids) != 1:
                fails.append(f"the machines disagree on {who}'s character: shared ids {sorted(sids)}")

    # --- routes -----------------------------------------------------------------------------
    def route(tag, sender, code, want):
        """want: {receiver name: from player id}; nobody else may hear it."""
        for inst in insts.values():
            inst.bridge.call("level_messages")
        _run(insts[sender], code)
        time.sleep(1.2)
        got = {}
        for name, inst in insts.items():
            for line in _heard(inst):
                parts = line.split(" ", 5)
                if line.startswith("apitest got ") and len(parts) == 6 and parts[5].endswith(tag):
                    got[name] = int(parts[3])
        m[f"route_{tag}"] = got
        if got != want:
            fails.append(f"route {tag}: heard by {got}, want {want}")

    if len(ids) == 3:
        h, a, b = ids["host"], ids["c1"], ids["c2"]
        route("r1", "host", 'Online_Send("apitest:msg", "host to all r1");', {"c1": h, "c2": h})
        route("r2", "c1", 'Online_Send("apitest:msg", "guest to all r2");', {"host": a, "c2": a})
        route("r3", "c1", 'Online_SendToHost("apitest:msg", "guest to host r3");', {"host": a})
        route("r4", "c1", f'Online_SendToPlayer({b}, "apitest:msg", "guest to guest r4");', {"c2": a})
        route("r5", "host", f'Online_SendToPlayer({b}, "apitest:msg", "host to guest r5");', {"c2": h})
        route("r6", "c2", f'Online_SendToPlayer({b}, "apitest:msg", "to myself r6");', {"c2": b})
        route("r7", "host", 'Online_SendToHost("apitest:msg", "host to itself r7");', {"host": h})
        # guest 2 pings guest 1, whose hook answers its sender only: guest to guest both ways
        route("r8", "c2", f'Online_SendToPlayer({a}, "apitest:ping", "r8");', {"c2": a})

        # long, spaced, non-ASCII text arrives whole
        for inst in insts.values():
            inst.bridge.call("level_messages")
        _run(c1, 'string t = ""; for(int i = 0; i < 1000; ++i) { t += "тест ü " + i % 10; } Online_Send("apitest:msg", t + " r9");')
        time.sleep(1.5)
        sizes = {}
        for name, inst in insts.items():
            for line in _heard(inst):
                parts = line.split(" ", 5)
                if line.startswith("apitest got ") and len(parts) == 6 and parts[5].endswith(" r9"):
                    sizes[name] = (int(parts[4]), len(parts[5].encode("utf-8")), parts[5].count("тест ü "))
        m["long_text"] = sizes
        if set(sizes) != {"host", "c2"} or any(s[0] != s[1] or s[2] != 1000 for s in sizes.values()):
            fails.append(f"long text: {sizes} (want host and c2, the length as sent, 1000 pieces)")

        # refused: a reserved channel, an oversized one, too long a text; no session needed to refuse
        r = _report(c1, "refused", 'Online_Send("online:joined", "x") + " " + '
                                   'Online_Send("apitest:msg" + "0123456789012345678901234567890123456789012345678901234567890", "x") + " " + '
                                   'Online_Listen("apitest:nothing", "NoSuchFunction")')
        m["refused"] = r
        if r != "false false false":
            fails.append(f"refused sends/listen came back {r} (want false false false)")

        # --- a guest leaves: the others hear it ---------------------------------------------------
        for inst in insts.values():
            inst.bridge.call("level_messages")
        _run(c2, "Online_Close();")  # (leaves the session cleanly; the host hears at once)
        t0 = time.time()
        left = {}
        while time.time() - t0 < 20.0 and len(left) < 2:
            time.sleep(1.0)
            for name in ("host", "c1"):
                for line in _heard(insts[name]):
                    if line.startswith("apitest left "):
                        left[name] = int(line.split()[3])
        m["left_events"] = left
        if left != {"host": b, "c1": b}:
            fails.append(f"left events: {left} (want host and c1 hearing {b})")

    return {"passed": not fails, "metrics": m, "notes": fails}
