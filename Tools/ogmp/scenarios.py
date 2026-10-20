"""Test scenarios. Each takes a Context and returns a dict:
    {"passed": bool, "metrics": {...}, "notes": [...]}
Instances are started through ctx.instance(...) so the runner can always
stop them, screenshot them and keep their logs.
"""

import math
import os
import re
import shutil
import statistics
import time

from .bridge import BridgeError
from .instance import TEST_DIR, Instance, log_errors

LEVEL = "Project60/4_red_hills.xml"

PROFILES = {
    "lan": dict(latency_ms=0, jitter_ms=0, loss=0.0),
    "good": dict(latency_ms=20, jitter_ms=5, loss=0.0),
    "typical": dict(latency_ms=45, jitter_ms=15, loss=0.01),
    "bad": dict(latency_ms=90, jitter_ms=40, loss=0.04),
}


class Context:
    def __init__(self, out_dir):
        self.out_dir = out_dir
        self.instances = []

    def instance(self, name, **kw):
        # OGMP_MODS: extra mod folders for every instance (e.g. the Overhaul mod), separated by ;
        extra = [m for m in os.environ.get("OGMP_MODS", "").split(";") if m]
        if extra:
            kw["mods"] = list(kw.get("mods") or []) + extra
        inst = Instance(name, index=len(self.instances), **kw)
        self.instances.append(inst)
        inst.start()
        return inst

    def shot(self, inst, tag):
        """Engine screenshot. glReadPixels rows come bottom-up and the saver
        swaps red and blue, so the file is fixed here once it exists."""
        path = f"{self.out_dir}/{inst.name}_{tag}.jpg"
        try:
            inst.bridge.call("screenshot", path=path)
        except BridgeError:
            inst.capture(path.replace(".jpg", ".png"))
            return
        end = time.time() + 10
        while time.time() < end and not os.path.exists(path):
            time.sleep(0.2)
        time.sleep(0.3)
        try:
            from PIL import Image
            img = Image.open(path).convert("RGB")
            r, g, b = img.split()
            Image.merge("RGB", (b, g, r)).transpose(Image.FLIP_TOP_BOTTOM).save(path, quality=90)
        except Exception:
            pass

    def close(self):
        for inst in self.instances:
            inst.stop()


# Helpers -----------------------------------------------------------------------------------

def wait_loaded(inst, timeout=150):
    return inst.bridge.wait(lambda: inst.bridge.status()["level_loaded"], timeout, what=f"{inst.name} level load")


def controlled(chars):
    """This machine's own character: the one on controller 0. Online, the
    other players' characters also report controlled, on virtual controllers,
    and the remote flag is not dependable on clients."""
    mine = [c for c in chars if c["controlled"] and c["controller_id"] == 0]
    return next((c for c in mine if not c["remote"]), mine[0] if mine else None)


def speed(c):
    v = c["vel"]
    return math.hypot(v[0], v[2])


def dist(a, b):
    return math.dist(a, b)


KNOWN_STOCK_ERRORS = ("campaign_common.as",)  # main menu include that stock 1.4 also fails


def script_errors(inst):
    return [(line, n) for line, n in log_errors(inst.log())
            if ("[e][as]" in line or "[e][us]" in line or "ERR:" in line) and not any(k in line for k in KNOWN_STOCK_ERRORS)]


def face(inst, char_id, target_pos, own_pos):
    """Turn the camera (and so the movement direction) toward a point."""
    dx, dz = target_pos[0] - own_pos[0], target_pos[2] - own_pos[2]
    yaw = math.degrees(math.atan2(-dx, -dz))
    inst.bridge.call("exec", target=char_id, code=f"target_rotation = {yaw:.2f}; cam_rotation = {yaw:.2f};")


# Scenarios -----------------------------------------------------------------------------------

def boot(ctx, level=LEVEL):
    inst = ctx.instance("solo", level=level)
    t0 = time.time()
    wait_loaded(inst)
    load_s = time.time() - t0
    chars = inst.bridge.state()
    me = inst.bridge.me()
    ctx.shot(inst, "boot")
    errs = script_errors(inst)
    passed = me is not None and not errs
    return {"passed": passed, "metrics": {"load_s": round(load_s, 1), "characters": len(chars)},
            "notes": [f"script error: {l} x{n}" for l, n in errs] or ["player found" if me else "no controlled character"]}


def move(ctx, level=LEVEL):
    """Injected input moves the local player; frames until it responds."""
    inst = ctx.instance("solo", level=level)
    wait_loaded(inst)
    time.sleep(1.0)
    start = inst.bridge.me()
    if start is None:
        return {"passed": False, "metrics": {}, "notes": ["no controlled character"]}

    f0 = inst.bridge.call("ping")["frame"]
    inst.bridge.hold(steps=240, up=1.0)
    frames_to_move = None

    for _ in range(120):
        me = inst.bridge.me()
        if me and speed(me) > 1.0:
            frames_to_move = inst.bridge.call("ping")["frame"] - f0
            break

    time.sleep(2.0)
    end = inst.bridge.me()
    moved = dist(start["pos"], end["pos"])
    ctx.shot(inst, "moved")
    return {"passed": moved > 1.5, "metrics": {"moved_m": round(moved, 2), "frames_to_move": frames_to_move},
            "notes": []}


def connect(ctx, level=LEVEL, profile="lan"):
    """Stock online, host + 1 client over loopback: join, then measure."""
    host = ctx.instance("host")
    client = ctx.instance("client1")

    for inst in (host, client):
        inst.bridge.call("net_sim", **PROFILES[profile])

    host.bridge.call("host", level=level)
    wait_loaded(host)
    client.bridge.call("connect", ip="127.0.0.1")
    wait_loaded(client, timeout=180)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")
    time.sleep(2.0)

    hs = host.bridge.status()["online"]
    host_chars = host.bridge.state()
    client_me = client.bridge.me()
    notes = [f"host players: {[p['name'] + '/' + str(p['avatar_id']) for p in hs['players']]}",
             f"host characters: {len(host_chars)}"]

    if client_me is None:
        ctx.shot(host, "joined")
        ctx.shot(client, "joined")
        return {"passed": False, "metrics": {}, "notes": notes + ["client has no controlled character"]}

    cid = client_me["id"]

    # Idle bandwidth over 5 s
    b0h, b0c = host.bridge.status()["online"], client.bridge.status()["online"]
    time.sleep(5.0)
    b1h, b1c = host.bridge.status()["online"], client.bridge.status()["online"]
    idle_down = (b1c["bytes_in"] - b0c["bytes_in"]) * 8 / 5 / 1000
    idle_up = (b1c["bytes_out"] - b0c["bytes_out"]) * 8 / 5 / 1000

    def describe(chars):
        return [(c["id"], "R" if c["remote"] else "L", "c%d" % c["controller_id"], c.get("state"), c.get("knocked_out"),
                 [round(x, 1) for x in c["pos"]], round(speed(c), 1)) for c in chars]

    notes.append(f"client view before: {describe(client.bridge.state())}")

    # Input latency: the client presses forward; when does it see itself
    # move, and when does the host see it?
    t_press = time.time()
    client.bridge.hold(steps=-1, up=1.0)
    own_ms = host_ms = None
    end = time.time() + 5

    while time.time() < end and (own_ms is None or host_ms is None):
        if own_ms is None:
            me = next((c for c in client.bridge.state() if c["id"] == cid), None)
            if me and speed(me) > 1.0:
                own_ms = (time.time() - t_press) * 1000
        if host_ms is None:
            other = next((c for c in host.bridge.state() if c["id"] == cid), None)
            if other and speed(other) > 1.0:
                host_ms = (time.time() - t_press) * 1000

    # Moving: bandwidth and how far the two views of the client avatar differ
    b2h, b2c = host.bridge.status()["online"], client.bridge.status()["online"]
    errors = []
    t_move = time.time()

    while time.time() - t_move < 4.0:
        on_client = next((c for c in client.bridge.state() if c["id"] == cid), None)
        on_host = next((c for c in host.bridge.state() if c["id"] == cid), None)
        if on_client and on_host:
            errors.append(dist(on_client["pos"], on_host["pos"]))

    b3h, b3c = host.bridge.status()["online"], client.bridge.status()["online"]
    secs = time.time() - t_move
    notes.append(f"client view moving: {describe(client.bridge.state())}")
    notes.append(f"host view moving: {describe(host.bridge.state())}")
    client.bridge.release_all()
    move_down = (b3c["bytes_in"] - b2c["bytes_in"]) * 8 / secs / 1000
    move_up = (b3c["bytes_out"] - b2c["bytes_out"]) * 8 / secs / 1000
    pings = [p["ping_ms"] for p in host.bridge.status()["online"]["peers"]]
    ctx.shot(host, "moving")
    ctx.shot(client, "moving")
    metrics = {
        "profile": profile,
        "client_sees_own_move_ms": round(own_ms) if own_ms else None,
        "host_sees_move_ms": round(host_ms) if host_ms else None,
        "rtt_ms": round(pings[0]) if pings else None,
        "idle_kbit_down": round(idle_down, 1), "idle_kbit_up": round(idle_up, 1),
        "moving_kbit_down": round(move_down, 1), "moving_kbit_up": round(move_up, 1),
        "view_error_median_m": round(statistics.median(errors), 3) if errors else None,
        "view_error_max_m": round(max(errors), 3) if errors else None,
    }
    errs = script_errors(host) + script_errors(client)
    passed = own_ms is not None and host_ms is not None and not errs
    notes += [f"script error: {l} x{n}" for l, n in errs]
    return {"passed": passed, "metrics": metrics, "notes": notes}


def bot_duel(ctx, level=LEVEL, profile="typical", seconds=40):
    """Host and client bots fight over the network: no crash, hits land on
    both sides, health agrees, traffic under combat."""
    from .bots import ChaseBot
    host = ctx.instance("host")
    client = ctx.instance("client1")

    for inst in (host, client):
        inst.bridge.call("net_sim", **PROFILES[profile])

    host.bridge.call("host", level=level)
    wait_loaded(host)
    client.bridge.call("connect", ip="127.0.0.1")
    wait_loaded(client, timeout=180)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")
    time.sleep(2.0)

    # Online is co-op by default: every player on one team. Opposing teams for
    # a duel, set on the host (it owns the characters; params are replicated)
    for i, c in enumerate(host.bridge.state()):
        host.bridge.call("exec", target=c["id"],
                         code=f'ReadObjectFromID(this_mo.GetID()).GetScriptParams().SetString("Teams", "duel_{i}");')

    bots = [ChaseBot(host, seed=1), ChaseBot(client, seed=2)]
    b0 = host.bridge.status()["online"]
    t0 = time.time()
    health_gap = []

    while time.time() - t0 < seconds:
        for bot in bots:
            bot.step()
        hv = {c["id"]: c for c in host.bridge.state()}
        cv = {c["id"]: c for c in client.bridge.state()}
        for cid in hv.keys() & cv.keys():
            if "temp_health" in hv[cid] and "temp_health" in cv[cid]:
                health_gap.append(abs(hv[cid]["temp_health"] - cv[cid]["temp_health"]))
        if int(time.time() - t0) % 10 == 0:
            pass

    secs = time.time() - t0
    b1 = host.bridge.status()["online"]
    final = host.bridge.state()
    ctx.shot(host, "duel")
    ctx.shot(client, "duel")
    for bot in bots:
        bot.inst.bridge.release_all()
    hurt = [c for c in final if c.get("temp_health", 1.0) < 0.99 or c.get("knocked_out", 0) != 0]
    errs = script_errors(host) + script_errors(client)
    alive = host.alive() and client.alive()
    metrics = {
        "profile": profile,
        "attacks": sum(b.attacks for b in bots),
        "characters_hurt": len(hurt),
        "host_kbit_up": round((b1["bytes_out"] - b0["bytes_out"]) * 8 / secs / 1000, 1),
        "host_kbit_down": round((b1["bytes_in"] - b0["bytes_in"]) * 8 / secs / 1000, 1),
        "health_gap_max": round(max(health_gap), 3) if health_gap else None,
    }
    passed = alive and not errs and len(hurt) > 0
    notes = [f"script error: {l} x{n}" for l, n in errs]
    if not alive:
        notes.append("an instance died")
    return {"passed": passed, "metrics": metrics, "notes": notes}


# Phase 2 -------------------------------------------------------------------------------------

def join(ctx, host, client, timeout=180):
    """Connect a client to the running host and wait until it is in the level."""
    client.bridge.call("connect", ip="127.0.0.1")
    wait_loaded(client, timeout=timeout)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")


def session(ctx, clients=1, level=LEVEL, profile="lan"):
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=level)
    wait_loaded(host)
    joined = []

    for i in range(clients):
        c = ctx.instance(f"client{i + 1}")
        c.bridge.call("net_sim", **PROFILES[profile])
        join(ctx, host, c)
        joined.append(c)

    time.sleep(1.0)
    return host, joined


def players(inst):
    return inst.bridge.status()["online"]["players"]


def unit_tests(ctx):
    """The engine unit tests (tut), ours included."""
    import subprocess
    from .instance import EXE, ROOT, RUN_DIR
    wd = f"{RUN_DIR}/unittests/writedir/"
    os.makedirs(wd, exist_ok=True)
    res = subprocess.run([EXE, "--write-dir", wd, "--working-dir", ROOT + "/", "--run-unit-tests"],
                         capture_output=True, text=True, timeout=120)
    out = res.stdout + res.stderr
    open(f"{ctx.out_dir}/unit_tests.txt", "w", encoding="utf-8").write(out)
    groups = {}
    for line in out.splitlines():
        if ":" in line and not line.startswith(("---", " ", "tests summary")):
            name, marks = line.split(":", 1)
            groups[name.strip()] = marks.strip()
    summary = next((l for l in out.splitlines() if l.startswith("tests summary")), "no summary")
    ours = {k: v for k, v in groups.items() if k in ("ClockSync", "CharSnapshot")}
    upstream_known = {"Modloading"}  # already failing upstream (error-string test)
    failed = [k for k, v in groups.items() if ("F" in v or "E" in v) and k not in upstream_known]
    passed = bool(ours) and not failed
    return {"passed": passed, "metrics": {"summary": summary, **{f"group_{k}": v for k, v in groups.items()}},
            "notes": [f"failed group: {k}" for k in failed] or ["ours: " + ", ".join(ours)]}


def clock_sync(ctx, profile="typical"):
    """Each side estimate of the other clock vs the truth (the runner reads both clocks)."""
    host, (client,) = session(ctx, 1, profile=profile)
    time.sleep(8.0)  # 16 pings each way

    def truth():
        # remote - local: clock_ms - epoch_ms is each process fixed origin
        a, b = host.bridge.status(), client.bridge.status()
        return (b["clock_ms"] - b["epoch_ms"]) - (a["clock_ms"] - a["epoch_ms"])

    t = statistics.median([truth() for _ in range(5)])
    host_view = host.bridge.status()["online"]["peers"][0]
    client_view = client.bridge.status()["online"]["peers"][0]
    err_host = abs(host_view["clock_offset_ms"] - t)  # host estimate of client - host
    err_client = abs(client_view["clock_offset_ms"] + t)  # client estimate of host - client
    metrics = {"profile": profile, "true_offset_ms": round(t, 1),
               "host_estimate_error_ms": round(err_host, 2), "client_estimate_error_ms": round(err_client, 2),
               "host_rtt_ms": round(host_view["ping_ms"], 1), "transport_ping_ms": host_view["transport_ping_ms"],
               "samples": host_view["clock_samples"]}
    passed = err_host < 8.0 and err_client < 8.0 and host_view["clock_samples"] >= 8
    return {"passed": passed, "metrics": metrics, "notes": []}


def late_join(ctx, profile="typical"):
    """A third player joins a running two-player game."""
    host, (c1,) = session(ctx, 1, profile=profile)
    c1.bridge.hold(steps=240, up=1.0)
    time.sleep(3.0)
    c2 = ctx.instance("client2")
    c2.bridge.call("net_sim", **PROFILES[profile])
    t0 = time.time()
    join(ctx, host, c2)
    join_s = time.time() - t0
    time.sleep(3.0)
    hv = {c["id"]: c for c in host.bridge.state()}
    v2 = {c["id"]: c for c in c2.bridge.state()}
    common = hv.keys() & v2.keys()
    gaps = [dist(hv[i]["pos"], v2[i]["pos"]) for i in common]
    me2 = c2.bridge.me()
    names = [p["name"] + "/" + str(p["avatar_id"]) for p in players(host)]
    ctx.shot(c2, "joined")
    metrics = {"profile": profile, "join_s": round(join_s, 1), "host_players": len(names),
               "host_characters": len(hv), "late_client_characters": len(v2),
               "position_gap_max_m": round(max(gaps), 2) if gaps else None}
    # (a sanity check, not sync accuracy: a running character is shown ~0.2 s
    # late, about a metre; full_session measures sync time-aligned)
    passed = len(names) == 3 and me2 is not None and len(v2) >= len(hv) and (not gaps or max(gaps) < 2.0)
    return {"passed": passed, "metrics": metrics, "notes": [f"host players: {names}"]}


def drop_rejoin(ctx, profile="typical"):
    """A client vanishes (process killed): the host notices and removes it;
    the player comes back and joins again."""
    host, (c1,) = session(ctx, 1, profile=profile)
    chars_before = len(host.bridge.state())
    c1.kill()
    t0 = time.time()
    try:
        host.bridge.wait(lambda: len(players(host)) == 1, 40, what="host drops the player")
        detect_s = time.time() - t0
    except Exception:
        detect_s = None
    chars_after_drop = len(host.bridge.state())
    c2 = ctx.instance("client1b")
    c2.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, c2)
    time.sleep(2.0)
    back = len(players(host))
    me = c2.bridge.me()
    metrics = {"profile": profile, "detect_s": round(detect_s, 1) if detect_s is not None else None,
               "characters_before": chars_before, "characters_after_drop": chars_after_drop,
               "players_after_rejoin": back}
    passed = detect_s is not None and chars_after_drop < chars_before and back == 2 and me is not None
    return {"passed": passed, "metrics": metrics, "notes": []}


def clean_leave(ctx, profile="typical"):
    """A client leaves through Disconnect: gone from the host quickly, out of the session itself."""
    host, (c1,) = session(ctx, 1, profile=profile)
    c1.bridge.call("disconnect")
    t0 = time.time()
    host.bridge.wait(lambda: len(players(host)) == 1, 20, what="host sees the leave")
    leave_s = time.time() - t0
    c1.bridge.wait(lambda: not c1.bridge.status()["online"]["active"], 20, what="client leaves the session")
    return {"passed": True, "metrics": {"profile": profile, "host_sees_leave_s": round(leave_s, 2)}, "notes": []}


def client_camera(ctx, profile="lan"):
    """On a client exactly one character owns controller 0 and camera 0 (its
    own), and the camera stays put while everyone stands still."""
    host, (c1,) = session(ctx, 1, profile=profile)
    time.sleep(2.0)
    r = c1.bridge.call("state")
    chars = r["characters"]
    owners = [c for c in chars if c["controlled"] and c["controller_id"] == 0 and c.get("camera_id", 0) == 0]
    jumps = []
    last = None
    t0 = time.time()

    while time.time() - t0 < 4.0:
        pos = c1.bridge.status()["camera_pos"]
        if last is not None:
            jumps.append(dist(pos, last))
        last = pos

    owner_ids = [c["id"] for c in owners]
    metrics = {"camera_owners": owner_ids, "my_avatar": r["my_avatar"],
               "camera_jump_max_m": round(max(jumps), 3) if jumps else None,
               "camera_jump_median_m": round(statistics.median(jumps), 4) if jumps else None}
    ctx.shot(c1, "camera")
    passed = owner_ids == [r["my_avatar"]] and jumps and max(jumps) < 0.25
    notes = [f"client characters: {[(c['id'], c['controller_id'], c.get('camera_id'), c['remote']) for c in chars]}"]
    return {"passed": passed, "metrics": metrics, "notes": notes}


def steam_open(ctx):
    """Steam build: the Join a friend screen opens, and a host is open to its
    friends once its level is up: its lobby (the "Join Game" presence) is made
    without Invite friends."""
    m, notes = {}, []
    host = ctx.instance("host")
    host.bridge.call("load_level", path="multiplayer_join_friend.as")
    time.sleep(4.0)
    ctx.shot(host, "join_a_friend")
    if not host.bridge.status()["online"].get("steam"):
        return {"passed": True, "metrics": {"lobby": "skipped"}, "notes": ["no Steam (test build, or Steam isn't running): the screen only"]}
    m["lobby_before_host"] = host.bridge.status()["online"]["steam_lobby"]
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 30, what="the host's level started")
    t0 = time.time()
    try:
        host.bridge.wait(lambda: host.bridge.status()["online"]["steam_lobby"] != "0", 30, what="our lobby, no invite")
        m["lobby_after_s"] = round(time.time() - t0, 1)
    except Exception as e:  # (reported below)
        notes.append(str(e))
    m["lobby"] = host.bridge.status()["online"]["steam_lobby"]
    passed = m["lobby_before_host"] == "0" and m["lobby"] != "0"
    return {"passed": passed, "metrics": m, "notes": notes}


def version_check(ctx):
    """Players need the same Overgrowth Online version (the mod's): a client
    with an older one is turned away and told; the same version joins."""
    mod = "D:/OvergrowthAI/mod/overhaul"
    old = TEST_DIR + "/tmp_mods/old/overhaul"
    shutil.rmtree(TEST_DIR + "/tmp_mods", ignore_errors=True)
    shutil.copytree(mod, old)
    xml = open(old + "/mod.xml", encoding="utf-8").read()
    open(old + "/mod.xml", "w", encoding="utf-8").write(re.sub(r"<Version>[^<]+</Version>", "<Version>0.0.1</Version>", xml))
    m, notes = {}, []
    host = ctx.instance("host", mods=[mod])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    stale = ctx.instance("client_old", mods=[old])
    stale.bridge.call("connect", ip="127.0.0.1")
    time.sleep(15.0)
    m["old_client_in_level"] = stale.bridge.status()["level_loaded"]
    stale.stop()  # (the log is written out on exit)
    m["old_client_told"] = "older than the host" in stale.log()
    same = ctx.instance("client_same", mods=[mod])
    try:
        join(ctx, host, same, timeout=120)
        m["same_version_joined"] = True
    except Exception as e:
        m["same_version_joined"] = False
        notes.append(str(e))
    host.stop()
    m["host_noted"] = "version mismatch: client 0.0.1" in host.log()
    passed = not m["old_client_in_level"] and m["old_client_told"] and m["host_noted"] and m["same_version_joined"]
    return {"passed": passed, "metrics": m, "notes": notes}


SCENARIOS = {
    "boot": boot,
    "version_check": version_check,
    "steam_open": steam_open,
    "move": move,
    "connect": connect,
    "connect_typical": lambda ctx: connect(ctx, profile="typical"),
    "connect_bad": lambda ctx: connect(ctx, profile="bad"),
    "bot_duel": bot_duel,
    "connect_good": lambda ctx: connect(ctx, profile="good"),
    "unit_tests": unit_tests,
    "clock_sync": clock_sync,
    "late_join": late_join,
    "drop_rejoin": drop_rejoin,
    "clean_leave": clean_leave,
    "client_camera": client_camera,
}


def _full_session(ctx):
    from .session_suite import full_session
    return full_session(ctx)


SCENARIOS["full_session"] = _full_session


def _combat_session(ctx):
    from .combat_suite import combat_session
    return combat_session(ctx)


SCENARIOS["combat_session"] = _combat_session


def _sync_ping(ctx):
    from .sync_suite import sync_ping
    return sync_ping(ctx)


SCENARIOS["sync_ping"] = _sync_ping


def _weapons_ping(ctx):
    from .weapons_ping_suite import weapons_ping
    return weapons_ping(ctx)


SCENARIOS["weapons_ping"] = _weapons_ping


def _retry_arena(ctx):
    from .retry_suite import retry_arena
    return retry_arena(ctx)


def _retry_coop(ctx):
    from .retry_suite import retry_coop
    return retry_coop(ctx)


SCENARIOS["retry_arena"] = _retry_arena
SCENARIOS["retry_coop"] = _retry_coop


def _held_delete(ctx):
    from .held_delete_suite import held_delete
    return held_delete(ctx)


SCENARIOS["held_delete"] = _held_delete


def _arena_fights(ctx):
    from .arena_fights_suite import arena_fights
    return arena_fights(ctx)


SCENARIOS["arena_fights"] = _arena_fights


def _nan_guard(ctx):
    from .nan_guard_suite import nan_guard
    return nan_guard(ctx)


SCENARIOS["nan_guard"] = _nan_guard


def _species_cycle(ctx):
    from .species_cycle_suite import species_cycle
    return species_cycle(ctx)


SCENARIOS["species_cycle"] = _species_cycle


def _fp_latejoin(ctx):
    from .fp_latejoin_suite import fp_latejoin
    return fp_latejoin(ctx)


SCENARIOS["fp_latejoin"] = _fp_latejoin


def _splitscreen_join(ctx):
    from .splitscreen_suite import splitscreen_join
    return splitscreen_join(ctx)


SCENARIOS["splitscreen_join"] = _splitscreen_join


def _guest_waits_host(ctx):
    from .load_wait_suite import guest_waits_host
    return guest_waits_host(ctx)


SCENARIOS["guest_waits_host"] = _guest_waits_host


def _forged(ctx):
    from .forge_suite import forged
    return forged(ctx)


SCENARIOS["forged"] = _forged


def _zone_revive(ctx):
    from .zone_revive_suite import zone_revive
    return zone_revive(ctx)


SCENARIOS["zone_revive"] = _zone_revive


def _item_flares(ctx):
    from .item_flares_suite import item_flares
    return item_flares(ctx)


SCENARIOS["item_flares"] = _item_flares


def _weapons_session(ctx):
    from .combat_suite import weapons_session
    return weapons_session(ctx)


SCENARIOS["weapons_session"] = _weapons_session


def _level_channel(ctx):
    from .online_mod_suite import level_channel
    return level_channel(ctx)


SCENARIOS["level_channel"] = _level_channel


def _pvp_battle(ctx):
    from .online_mod_suite import pvp_battle
    return pvp_battle(ctx)


SCENARIOS["pvp_battle"] = _pvp_battle


def _stress_8(ctx):
    from .stress_suite import stress_8
    return stress_8(ctx)


def _stress_4(ctx):
    from .stress_suite import stress_8
    return stress_8(ctx, clients=3)


SCENARIOS["stress_8"] = _stress_8


def _campaign_coop(ctx):
    from .coop_suite import campaign_coop
    return campaign_coop(ctx)


SCENARIOS["campaign_coop"] = _campaign_coop


def _coop_character(ctx):
    from .coop_suite import coop_character
    return coop_character(ctx)


SCENARIOS["coop_character"] = _coop_character


def _coop_camera(ctx):
    from .coop_suite import coop_camera
    return coop_camera(ctx)


SCENARIOS["coop_camera"] = _coop_camera


def _coop_talk(ctx):
    from .coop_suite import coop_talk
    return coop_talk(ctx)


SCENARIOS["coop_talk"] = _coop_talk


def _coop_gear(ctx):
    from .coop_suite import coop_gear
    return coop_gear(ctx)


SCENARIOS["coop_gear"] = _coop_gear


def _coop_revive(ctx):
    from .coop_suite import coop_revive
    return coop_revive(ctx)


SCENARIOS["coop_revive"] = _coop_revive


def _coop_cutscene(ctx):
    from .coop_story_suite import coop_cutscene
    return coop_cutscene(ctx)


SCENARIOS["coop_cutscene"] = _coop_cutscene


def _coop_checkpoint(ctx):
    from .coop_story_suite import coop_checkpoint
    return coop_checkpoint(ctx)


SCENARIOS["coop_checkpoint"] = _coop_checkpoint


def _coop_weapons(ctx):
    from .coop_story_suite import coop_weapons
    return coop_weapons(ctx)


SCENARIOS["coop_weapons"] = _coop_weapons


def _arena_stress(ctx):
    from .arena_stress_suite import arena_stress
    return arena_stress(ctx)


SCENARIOS["arena_stress"] = _arena_stress


def _hud_tags(ctx):
    from .coop_suite import hud_tags
    return hud_tags(ctx)


SCENARIOS["hud_tags"] = _hud_tags


def _weather_sync(ctx):
    from .coop_suite import weather_sync
    return weather_sync(ctx)


SCENARIOS["weather_sync"] = _weather_sync


def _parallel_load(ctx):
    from .coop_suite import parallel_load
    return parallel_load(ctx)


SCENARIOS["parallel_load"] = _parallel_load


def _objectives(ctx):
    from .coop_suite import objectives
    return objectives(ctx)


SCENARIOS["objectives"] = _objectives


# Renderer checks (docs_gl46_plan.md stage 0): render_suite.py
def _render(name, **kw):
    def run(ctx):
        from . import render_suite
        return getattr(render_suite, name)(ctx, **kw)
    return run


SCENARIOS["render_compare"] = _render("render_compare")
SCENARIOS["render_capture"] = _render("render_compare", capture=True)
SCENARIOS["gl_errors"] = _render("gl_errors")
SCENARIOS["render_check"] = _render("render_check")
SCENARIOS["render_quick"] = _render("render_check", scenes=["slaver_path"], battle=False)
SCENARIOS["render_check_vk"] = _render("render_check_vk")
SCENARIOS["vk_errors"] = _render("vk_errors")
SCENARIOS["vk_textures"] = _render("vk_textures")
SCENARIOS["vk_parity"] = _render("vk_parity")


# Shader checks (docs_gl46_plan.md stage 3.1): shader_suite.py
def _shaders(name, **kw):
    def run(ctx):
        from . import shader_suite
        return getattr(shader_suite, name)(ctx, **kw)
    return run


SCENARIOS["shader_collect"] = _shaders("shader_collect")
SCENARIOS["shader_check"] = _shaders("shader_check")
SCENARIOS["vk_shader_check"] = _shaders("vk_shader_check")
SCENARIOS["shader_glslang"] = _shaders("shader_glslang")
SCENARIOS["shader_spirv"] = _shaders("shader_spirv")
SCENARIOS["perf_baseline"] = _render("perf_baseline")
SCENARIOS["perf_capture"] = _render("perf_baseline", capture=True)


def _arena_pvp(ctx):
    from .arena_suite import arena_pvp
    return arena_pvp(ctx)


SCENARIOS["arena_pvp"] = _arena_pvp


def _arena_pvp_stucco(ctx):
    from .arena_suite import STUCCO, arena_pvp
    return arena_pvp(ctx, arena=STUCCO)


SCENARIOS["arena_pvp_stucco"] = _arena_pvp_stucco


def _arena_weapons(ctx):
    from .arena_suite import arena_weapons
    return arena_weapons(ctx)


SCENARIOS["arena_weapons"] = _arena_weapons


def _arena_pickups(ctx):
    from .arena_suite import arena_pickups
    return arena_pickups(ctx)


SCENARIOS["arena_pickups"] = _arena_pickups
SCENARIOS["stress_4"] = _stress_4

SUITES = {
    "smoke": ["boot", "move", "connect"],
    "baseline": ["connect", "connect_typical", "connect_bad", "bot_duel"],
    # One shared session for the network checks (about 2 minutes), plus the duel
    "phase2": ["unit_tests", "full_session", "bot_duel"],
    # Ownership: combat between owners is Phase 4 (HitRequest), so no duel here
    "phase3": ["unit_tests", "move", "full_session"],
    # Combat across machines: hit requests, defender-side resolution, vitals
    "phase4": ["unit_tests", "move", "combat_session", "weapons_session", "full_session"],
    # Overhaul online: the level channel, Large Battles, co-op story, 8 players
    "phase5": ["unit_tests", "level_channel", "pvp_battle", "campaign_coop", "stress_8"],
    # Overgrowth Online is story co-op for now
    # The renderer (docs_gl46_plan.md): before and after every renderer change
    "renderer": ["shader_check", "render_check", "shader_spirv", "render_check_vk", "vk_errors", "vk_textures", "vk_shader_check", "vk_parity", "perf_baseline"],
    "coop": ["unit_tests", "campaign_coop", "coop_character", "coop_camera", "coop_talk", "coop_gear", "coop_revive", "coop_cutscene", "coop_checkpoint", "coop_weapons", "late_join", "drop_rejoin"],
    "phase2_long": ["unit_tests", "connect", "connect_good", "connect_typical", "connect_bad", "clock_sync",
                    "late_join", "drop_rejoin", "clean_leave", "client_camera", "bot_duel"],
}
