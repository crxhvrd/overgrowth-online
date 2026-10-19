"""fp_latejoin: two things a joining guest used to get wrong (0.38.3).

1. Late join: the host's character switches six times before the guest
   joins (as arena randomizer rounds do). The guest must apply only the
   latest one (it used to rebuild every rig of every round, then crashed).
2. First person: the guest plays in Overhaul's first person, which shrinks
   its own head (and bones near the eye) to nothing. That is the guest's own
   view only: the host's copy of the guest keeps its head (it used to be
   drawn as spikes).

    python -I Tools/ogmp.py run fp_latejoin
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded

MOD = "D:/OvergrowthAI/mod/overhaul"
SWITCHES = ["Data/Characters/rat.xml", "Data/Characters/male_cat.xml", "Data/Characters/turner.xml",
            "Data/Characters/female_rat.xml", "Data/Characters/cat.xml", "Data/Characters/lt_dog_big.xml"]

# The smallest volume scale (determinant) of the head chain's drawn bones:
# about 0 for a bone shrunk to nothing, about 1 for a normal one
PROBE = ('Skeleton@ s = this_mo.rigged_object().skeleton(); int b = s.IKBoneStart("head"); float mn = 100.0f; '
         'for(int e = 0, len = s.IKBoneLength("head"); e < len && b >= 0; ++e) { '
         'mat4 m = this_mo.rigged_object().GetDisplayBoneMatrix(b); vec3 o = m * vec3(0.0f); '
         'float d = abs(dot(cross(m * vec3(1.0f, 0.0f, 0.0f) - o, m * vec3(0.0f, 1.0f, 0.0f) - o), m * vec3(0.0f, 0.0f, 1.0f) - o)); '
         'mn = min(mn, d); b = s.GetParent(b); } '
         'level.SendMessage("probe_fp " + mn);')


def _probe(inst, cid):
    inst.bridge.call("level_messages", prefix="probe_fp")
    inst.bridge.call("exec", target=cid, code=PROBE)
    time.sleep(0.3)
    got = inst.bridge.call("level_messages", prefix="probe_fp")["messages"]
    return float(str(got[-1]).split()[-1]) if got else None


def fp_latejoin(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host", mods=[MOD])
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="session started")
    me = host.bridge.me()
    for path in SWITCHES:
        host.bridge.call("exec", target=me["id"], code=f'SwitchCharacter("{path}"); FixDiscontinuity();')
        time.sleep(1.2)
    m["host_path"] = host.bridge.me()["char_path"]

    g = ctx.instance("client1", mods=[MOD], keys={"ocam_enabled": "true", "ocam_fp": "true"})
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(4.0)

    gme = g.bridge.me()
    host_copy = next((c for c in host.bridge.state() if c["common_id"] == gme["common_id"]), None)
    guest_copy_of_host = next((c for c in g.bridge.state() if c["common_id"] == me["common_id"]), None)
    m["guest_sees_host_as"] = guest_copy_of_host["char_path"] if guest_copy_of_host else None
    m["guest_own_head"] = _probe(g, gme["id"])
    m["host_sees_guest_head"] = _probe(host, host_copy["id"]) if host_copy else None
    m["guest_alive"], m["host_alive"] = g.alive(), host.alive()

    g.stop()  # (the log is written out on exit)
    switches = [line for line in g.log().splitlines() if "Overgrowth Online: character" in line and " is now " in line]
    m["guest_switches_applied"] = len(switches)

    if m["guest_sees_host_as"] != SWITCHES[-1]:
        fails.append(f"the guest shows the host as {m['guest_sees_host_as']}, not the latest {SWITCHES[-1]}")
    if m["guest_switches_applied"] != 1:
        fails.append(f"the guest applied {m['guest_switches_applied']} switches of the host's character (want 1)")
    if m["guest_own_head"] is None or m["guest_own_head"] > 0.05:
        fails.append("first person did not shrink the guest's own head (test setup: nothing to show)")
    if m["host_sees_guest_head"] is None or m["host_sees_guest_head"] < 0.2:
        fails.append("the host draws the first-person guest's head shrunk")
    if not m["host_alive"]:
        fails.append("the host crashed")
    return {"passed": not fails, "metrics": m, "notes": fails}
