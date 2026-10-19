"""species_cycle: characters switch species while another machine draws them.

The arena randomizer switches species every round; the other machines switch
their copies (ApplyNetworkCharacter). Tails differ in length (rat, cat, dog,
wolf): a copy drawing a longer tail with the last one's arrays wrote past
them (the user's guest crash in CDrawTailImpl, 2026-10-18). The host's
player and a guard the host runs go through the tailed species twice; the
guest must still run.

    python -I Tools/ogmp.py run species_cycle
"""

import time

from .scenarios import PROFILES, LEVEL, join, wait_loaded

CYCLE = ["Data/Characters/rat.xml", "Data/Characters/male_cat.xml", "Data/Characters/lt_dog_big.xml",
         "Data/Characters/wolf.xml", "Data/Characters/turner.xml", "Data/Characters/female_rat.xml",
         "Data/Characters/cat.xml", "Data/Characters/male_wolf.xml", "Data/Characters/dog_soldier.xml",
         "Data/Characters/hooded_rat.xml"]


def species_cycle(ctx, profile="typical"):
    m, fails = {"switches": 0}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(3.0)
    me = host.bridge.me()
    # Both in front of the guest's camera (a tail is drawn when it's seen)
    gc = next(c for c in host.bridge.state() if c["id"] != me["id"])
    gp = gc["pos"]
    host.bridge.call("exec", target="level", code=f"Online_PlaceAvatar({me['id']}, vec3({gp[0]:.2f}, {gp[1] + 0.2:.2f}, {gp[2] - 2.0:.2f}), vec3(0, 0, 1));")
    guard = host.bridge.call("spawn", pos=[gp[0] + 0.8, gp[1] + 0.3, gp[2] - 2.2], teams="host")["object_id"]
    gme = g.bridge.me()
    g.bridge.call("exec", target=gme["id"], code="target_rotation = 0.0f; cam_rotation = 0.0f;")  # facing -z: them
    time.sleep(3.0)
    m["guest_sees_host_m"] = round(sum((a - b) ** 2 for a, b in zip(next(c for c in g.bridge.state() if c["id"] != gme["id"])["pos"], gme["pos"])) ** 0.5, 2)

    for rnd in range(2):
        for path in CYCLE:
            for cid in (me["id"], guard):
                host.bridge.call("exec", target=cid, code=f'SwitchCharacter("{path}"); FixDiscontinuity();')
            m["switches"] += 1
            time.sleep(1.2)
            if not g.alive() or not host.alive():
                break
        if not g.alive() or not host.alive():
            break
    m["guest_alive"], m["host_alive"] = g.alive(), host.alive()
    if not g.alive():
        fails.append(f"the guest crashed after {m['switches']} switches")
    if not host.alive():
        fails.append("the host crashed")

    # Poses name their skeleton now: after all the switches the guest must still
    # take the host's (a wrong id would freeze the character there)
    if g.alive() and host.alive():
        p = host.bridge.me()["pos"]
        host.bridge.call("exec", target="level", code=f"Online_PlaceAvatar({me['id']}, vec3({p[0] + 3.0:.2f}, {p[1] + 0.2:.2f}, {p[2]:.2f}), vec3(0, 0, 1));")
        time.sleep(3.0)
        hp = host.bridge.me()["pos"]
        seen = next((c for c in g.bridge.state() if c["common_id"] == me["common_id"]), None)
        m["guest_view_error_m"] = round(sum((a - b) ** 2 for a, b in zip(seen["pos"], hp)) ** 0.5, 2) if seen else None
        m["guest_view_path"] = seen["char_path"] if seen else None
        if m["guest_view_error_m"] is None or m["guest_view_error_m"] > 0.5:
            fails.append(f"the guest's copy of the host doesn't follow it after the switches ({m['guest_view_error_m']} m)")
        if m["guest_view_path"] != CYCLE[-1]:
            fails.append(f"the guest shows the host as {m['guest_view_path']}, not {CYCLE[-1]}")
    return {"passed": not fails, "metrics": m, "notes": fails}
