"""Phase 5: stress_8. A host and 7 clients (8 instances on this machine) with
30 NPCs in two teams fighting around them, bots driving every player.

Passes when the host's upload stays within 1.5 Mbit/s, nobody drops out,
every machine has every character, and the message queues don't grow.
Frame rates are reported, not judged: 8 games on one machine share it.
"""

import math
import time

from .bots import ChaseBot
from .scenarios import script_errors, session


def stress_8(ctx, profile="lan", clients=7, npcs=30, seconds=45.0, upload_limit_kbit=1500.0):
    m, fails = {}, []
    host, joined = session(ctx, clients, profile=profile)
    everyone = [host] + joined

    # 30 NPCs in a ring around the host, alternating teams so they fight
    hid = host.bridge.me()["id"]
    base = next(c["pos"] for c in host.bridge.state() if c["id"] == hid)
    for i in range(npcs):
        ang = i * 2.0 * math.pi / npcs
        r = 7.0 + (i % 3) * 2.5
        pos = [base[0] + r * math.cos(ang), base[1] + 1.0, base[2] + r * math.sin(ang)]
        host.bridge.call("spawn", pos=pos, teams="stress_a" if i % 2 == 0 else "stress_b")
    time.sleep(6.0)

    counts = [len(inst.bridge.state()) for inst in everyone]
    m["characters_per_machine"] = counts
    if len(set(counts)) != 1 or counts[0] < npcs + clients + 1:
        fails.append(f"not every machine has every character: {counts}")

    # Fight: every player chases its nearest character
    bots = [ChaseBot(inst, seed=11 + i) for i, inst in enumerate(everyone)]
    before = [inst.bridge.status() for inst in everyone]
    queues = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        for bot in bots:
            bot.step()
        if int(time.time() - t0) % 10 == 0:
            queues.append(host.bridge.status()["online"].get("out_queue", 0))
    after = [inst.bridge.status() for inst in everyone]
    for bot in bots:
        bot.inst.bridge.release_all()

    def rate(i, key):
        secs = (after[i]["epoch_ms"] - before[i]["epoch_ms"]) / 1000.0
        return (after[i]["online"][key] - before[i]["online"][key]) * 8 / secs / 1000.0

    def fps(i):
        secs = (after[i]["epoch_ms"] - before[i]["epoch_ms"]) / 1000.0
        return (after[i]["frame"] - before[i]["frame"]) / secs

    up = rate(0, "bytes_out")
    m["host_kbit_up"] = round(up, 1)
    m["host_kbit_up_per_client"] = round(up / clients, 1)
    m["client_kbit_down"] = [round(rate(i, "bytes_in"), 1) for i in range(1, len(everyone))]
    m["client_kbit_up"] = [round(rate(i, "bytes_out"), 1) for i in range(1, len(everyone))]
    m["host_snapshot_kbit"] = round(rate(0, "snapshot_bytes_out"), 1)
    m["host_idle_skips"] = after[0]["online"].get("idle_skips", 0) - before[0]["online"].get("idle_skips", 0)
    m["fps_host_clients"] = [round(fps(i), 1) for i in range(len(everyone))]
    m["host_out_queue"] = queues
    m["persistent_messages"] = after[0]["online"].get("persistent_messages")

    if up > upload_limit_kbit:
        fails.append(f"host upload {up:.0f} kbit/s > {upload_limit_kbit:.0f}")
    if queues and max(queues[len(queues) // 2:]) > max(50, 2 * max(queues[:max(1, len(queues) // 2)])):
        fails.append(f"the host's outgoing queue grows: {queues}")
    alive = [inst.alive() for inst in everyone]
    if not all(alive):
        fails.append(f"instances died: {alive}")
    errs = sum((script_errors(inst) for inst in everyone), [])
    if errs:
        fails.append(f"script errors: {errs[:3]}")
    return {"passed": not fails, "metrics": m, "notes": fails}
