"""arena_stress: the online arena with everything on, round after round.

Waterfall Cave (weapon fights), the Overhaul mod, bots filling 8 fighters, the
randomizer on. Every round is ended as soon as it starts (everyone knocked
out), so each round runs the whole start again: hands and sheaths emptied, old
weapons deleted, species switched, fighters placed, weapons and shields. A
second guest joins mid-match (the replay of everything so far; a bot gives up
its place) and leaves again (a bot comes back). Every round all games must
still run and answer; at the end no script errors, no read of a deleted item.
"""

import os
import time

from .scenarios import PROFILES, join, script_errors, wait_loaded
from .arena_suite import CAMPAIGN, Log, _wait
from .coop_suite import _ko

WATERFALL = "arenas/waterfall_arena.xml"

KO_OURS = ('for(int i = 0; i < GetNumCharacters(); ++i) { MovementObject@ c = ReadCharacter(i); '
           'if(!c.remote) { c.Execute("SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);"); } }')


CDB = "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe"


def _watch(inst):
    """OGMP_CDB=<dir>: a debugger on the instance writes a full dump if it crashes."""
    out = os.environ.get("OGMP_CDB", "")
    if not out or not os.path.exists(CDB) or inst.pid is None:
        return None
    import subprocess
    dump = os.path.join(out, f"full_{inst.pid}.dmp").replace("\\", "/")
    cmds = os.path.join(out, f"cdb_{inst.pid}.txt")
    with open(cmds, "w") as f:
        f.write(f'sxe -c ".dump /ma /o {dump}; q" av' + chr(10) + 'g' + chr(10))
    log = open(os.path.join(out, f"cdb_{inst.pid}.log"), "w")
    return subprocess.Popen([CDB, "-p", str(inst.pid), "-cf", cmds], stdout=log, stderr=subprocess.STDOUT)


def _crash_dumps():
    d = os.path.join(os.environ.get("LOCALAPPDATA", ""), "CrashDumps")
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.startswith("OvergrowthOnline")) if os.path.isdir(d) else []


def arena_stress(ctx, profile="typical", rounds=30, fighters=8):
    from .online_mod_suite import OVERHAUL

    m, fails = {}, []
    dumps_before = set(_crash_dumps())
    old = os.environ.get("OGMP_MODS")
    os.environ["OGMP_MODS"] = OVERHAUL
    try:
        host = ctx.instance("host")
        host.bridge.call("net_sim", **PROFILES[profile])
        hlog = Log(host)
        host.bridge.call("host", level=WATERFALL, campaign=CAMPAIGN)
        wait_loaded(host)
        host.bridge.call("exec", target="level",
                         code=f'SetConfigValueInt("oo_arena_bots", {fighters}); SetConfigValueBool("oo_arena_random", true);')
        client = ctx.instance("client1")
        _watch(client)
        client.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, client)

        players = {"host": host, "client1": client}
        late = late_ref = None
        fights, timeline = 0, []
        for r in range(rounds):
            if not _wait(lambda: hlog.drain().count("ohl arena fight") > fights, 45.0):
                fails.append(f"round {r + 1}: no \"Fight!\" within 45 s (last texts {hlog.texts()[-3:]})")
                break
            fights = hlog.drain().count("ohl arena fight")
            time.sleep(1.5)  # a moment of fighting: weapons on, bots awake

            gone = [n for n, p in players.items() if not p.alive()]
            if gone:
                fails.append(f"round {r + 1}: {gone} no longer running (crash)")
                m["crashed_pids"] = {n: players[n].pid for n in gone}
                break
            try:
                chars = len(host.bridge.state())
                ccount = len(client.bridge.state())
            except Exception as e:
                fails.append(f"round {r + 1}: a game stopped answering ({e})")
                break
            timeline.append([r + 1, chars, ccount])

            # A second guest joins (round 5) and leaves (round 15)
            if r == 4:
                late = late_ref = ctx.instance("client2")
                _watch(late)
                late.bridge.call("net_sim", **PROFILES[profile])
                join(ctx, host, late)
                players["client2"] = late
            elif r == 14 and late is not None:
                late.stop()
                players.pop("client2", None)
                late = None

            # Everyone down: the round ends, the next one starts it all again
            host.bridge.call("exec", target="level", code=KO_OURS)
            for name, p in players.items():
                if name != "host":
                    try:
                        _ko(p)
                    except Exception:
                        pass

        m["rounds_done"] = len(timeline)
        m["characters_per_round_host_client"] = timeline[-6:]
    finally:
        if old is None:
            os.environ.pop("OGMP_MODS", None)
        else:
            os.environ["OGMP_MODS"] = old

    if m.get("rounds_done", 0) < rounds and not fails:
        fails.append(f"only {m.get('rounds_done', 0)} of {rounds} rounds")

    # Script trouble in the logs (stop the instances first: logs are buffered)
    named = [(n, i) for n, i in [("host", host), ("client1", client), ("client2", late_ref)] if i is not None]
    insts = [i for _, i in named]
    for i in insts:
        try:
            i.stop()
        except Exception:
            pass
    logs = {n: i.log() for n, i in named}
    m["no_item_object"] = {n: l.count("There is no item object") for n, l in logs.items()}
    m["slot_had_deleted_item"] = {n: l.count("Slot already has item") for n, l in logs.items()}
    if any(m["no_item_object"].values()):
        fails.append(f"a script read a deleted item: {m['no_item_object']}")
    errs = []
    for i in insts:
        errs += script_errors(i)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    new_dumps = sorted(set(_crash_dumps()) - dumps_before)
    if new_dumps:
        m["crash_dumps"] = new_dumps
        fails.append(f"crash dumps written: {new_dumps}")
    return {"passed": not fails, "metrics": m, "notes": fails}
