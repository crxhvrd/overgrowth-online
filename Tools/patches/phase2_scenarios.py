"""Phase 2 scenarios + Instance.kill. Run from tools/: python -I patches/phase2_scenarios.py"""

p = "ogmp/instance.py"
s = open(p, encoding="utf-8").read()
a = """    def alive(self):"""
b = '''    def kill(self):
        """Hard kill (no quit): a crashed or unplugged player."""
        if self.bridge is not None:
            self.bridge.close()
            self.bridge = None
        if self.pid is not None:
            subprocess.run(["taskkill.exe", "/F", "/PID", str(self.pid)], capture_output=True)
        self.pid = None

    def alive(self):'''
if "def kill(self):" not in s:
    assert s.count(a) == 1
    s = s.replace(a, b, 1)
    open(p, "w", encoding="utf-8", newline="\n").write(s)

p = "ogmp/scenarios.py"
s = open(p, encoding="utf-8").read()
ADD = '''

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
    ours = {k: v for k, v in groups.items() if k in ("ClockSync",)}
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
    passed = len(names) == 3 and me2 is not None and len(v2) >= len(hv) and (not gaps or max(gaps) < 1.0)
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
'''
if "def clock_sync(ctx" not in s:
    s = s.replace("\n\nSCENARIOS = {", ADD + "\n\nSCENARIOS = {", 1)
    s = s.replace('''    "bot_duel": bot_duel,
}''', '''    "bot_duel": bot_duel,
    "connect_good": lambda ctx: connect(ctx, profile="good"),
    "unit_tests": unit_tests,
    "clock_sync": clock_sync,
    "late_join": late_join,
    "drop_rejoin": drop_rejoin,
    "clean_leave": clean_leave,
}''', 1)
    s = s.replace('''    "baseline": ["connect", "connect_typical", "connect_bad", "bot_duel"],''', '''    "baseline": ["connect", "connect_typical", "connect_bad", "bot_duel"],
    "phase2": ["unit_tests", "connect", "connect_good", "connect_typical", "connect_bad", "clock_sync",
               "late_join", "drop_rejoin", "clean_leave", "bot_duel"],''', 1)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
print("ok")
