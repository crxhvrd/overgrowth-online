"""Retry online: the host's pause menu Retry loads the level again for
everyone (level.as OnlineRetry), in the arena and in story co-op.

    retry_arena: a match with a guest; Retry; both back in a fresh match (a
        knife spawned before is gone, the guest has its character, the
        first score after has no points)
    retry_coop: a story level with a guest; Retry; both back in the same
        level by its campaign id, the guest with its character

    python -I Tools/ogmp.py run retry_arena retry_coop
"""

import time

from .arena_suite import ARENA, CAMPAIGN as ARENA_CAMPAIGN, Log, _score
from .coop_suite import CAMPAIGN as STORY_CAMPAIGN
from .scenarios import PROFILES, join, players, script_errors, wait_loaded


def _items(inst):
    return {it["common_id"] for it in inst.bridge.call("state").get("items", [])}


def _retry(host, client, marker):
    """Retry on the host; True once both are back with the marker gone."""
    host.bridge.call("exec", target="level", code="OnlineRetry();")
    time.sleep(3.0)
    wait_loaded(host)
    wait_loaded(client, timeout=180)
    # The host's "press any key" after the load, as after any level change
    host.bridge.wait(lambda: host.bridge.call("start_session")["started"], 60, what="all clients ready")
    t0 = time.time()
    while time.time() - t0 < 60.0:
        try:
            if client.bridge.me() is not None and len(players(host)) == 2 and marker not in _items(host) | _items(client):
                return True
        except Exception:
            pass
        time.sleep(1.0)
    return False


def _common_checks(m, fails, host, client):
    m["players_on_host"] = len(players(host))
    m["guest_has_character"] = client.bridge.me() is not None
    if not m["guest_has_character"]:
        fails.append("the guest has no character after Retry")
    for name, inst in (("host", host), ("guest", client)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append("script errors: " + "; ".join(f"{l} x{n}" for l, n in errs[:5]))


def retry_arena(ctx, profile="typical"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=ARENA, campaign=ARENA_CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(6.0)
    log = Log(client)
    m["scores_before"] = log.scores()[-1:] if log.scores() else []
    pos = host.bridge.me()["pos"]
    marker = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[pos[0], pos[1] + 1.0, pos[2]])["object_id"]
    time.sleep(2.0)
    log.lines = []

    m["reloaded"] = _retry(host, client, marker)
    if not m["reloaded"]:
        fails.append("Retry: not both back in a fresh level")
    time.sleep(8.0)
    scores = log.scores()
    m["first_score_after"] = scores[0] if scores else None
    if scores:
        pts = _score(scores[0])[1]
        if any(v != 0 for v in pts.values()):
            fails.append(f"the match after Retry doesn't start from 0: {scores[0]}")
    else:
        fails.append("no arena score after Retry (the match didn't start)")
    _common_checks(m, fails, host, client)
    ctx.shot(client, "after_retry")
    return {"passed": not fails, "metrics": m, "notes": fails}


def retry_coop(ctx, profile="typical", level_id="river_village_intro"):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=f"load_campaign_level {level_id}", campaign=STORY_CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(5.0)
    level_before = host.bridge.status().get("level")
    pos = host.bridge.me()["pos"]
    marker = host.bridge.call("spawn", path="Data/Items/flint_knife.xml", pos=[pos[0], pos[1] + 1.0, pos[2]])["object_id"]
    time.sleep(2.0)

    m["reloaded"] = _retry(host, client, marker)
    if not m["reloaded"]:
        fails.append("Retry: not both back in a fresh level")
    time.sleep(3.0)
    m["level_before_after"] = [level_before, host.bridge.status().get("level"), client.bridge.status().get("level")]
    if len(set(m["level_before_after"])) != 1:
        fails.append(f"Retry loaded another level: {m['level_before_after']}")
    _common_checks(m, fails, host, client)
    ctx.shot(client, "after_retry")
    return {"passed": not fails, "metrics": m, "notes": fails}
