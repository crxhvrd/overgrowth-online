"""guest_waits_host: a guest that loads a level before the host waits for it.

The host's level change makes guests load the same level at once (parallel
loads). A guest that finished first used to walk straight into the level:
an empty map, no character, the host not there yet. The host here finishes
each load 12 s late (test key test_load_delay_ms); after a Retry the guest
must stay on its loading screen until the host has loaded, then come in with
its character.

    python -I Tools/ogmp.py run guest_waits_host
"""

import re
import time
from datetime import datetime

from .coop_suite import CAMPAIGN as STORY_CAMPAIGN
from .scenarios import PROFILES, join, wait_loaded

DELAY_MS = 12000


def _host_loaded_times(log):
    """Wall times (s since epoch) of the host's finished loads (the line right after them)."""
    out = []
    for line in log.splitlines():
        if "GPU memory level loaded" in line:
            m = re.match(r"(\d{4}/\d\d/\d\d \d\d:\d\d:\d\d)", line)
            if m:
                out.append(datetime.strptime(m.group(1), "%Y/%m/%d %H:%M:%S").timestamp())
    return out


def guest_waits_host(ctx, profile="lan", level_id="river_village_intro"):
    m, fails = {}, []
    host = ctx.instance("host", keys={"test_load_delay_ms": str(DELAY_MS)})
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=f"load_campaign_level {level_id}", campaign=STORY_CAMPAIGN)
    wait_loaded(host)
    g = ctx.instance("client1")
    g.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, g)
    time.sleep(4.0)

    t0 = time.time()
    host.bridge.call("exec", target="level", code="OnlineRetry();")
    samples = []  # (seconds since the Retry, level_loaded, waiting_for_input)
    while time.time() - t0 < 120:
        try:
            s = g.bridge.call("status", timeout=1.0)
            samples.append((time.time(), bool(s.get("level_loaded")), bool(s.get("waiting_for_input"))))
        except Exception:
            pass  # (loading: no answer)
        try:
            host.bridge.call("start_session", timeout=0.5)  # the host's "press any key" once it can
        except Exception:
            pass
        if samples and samples[-1][1] and not samples[-1][2] and time.time() - t0 > DELAY_MS / 1000.0 + 5:
            break
        time.sleep(0.3)

    loaded_at = [t for t in _host_loaded_times(host.log()) if t >= int(t0)]
    m["host_loaded_s"] = round(loaded_at[0] - t0, 1) if loaded_at else None
    # (the first samples can still be the old level: count from the guest's reload on)
    start = next((i for i, (t, loaded, waiting) in enumerate(samples) if not loaded or waiting), len(samples))
    samples = samples[start:]
    in_game = [t for t, loaded, waiting in samples if loaded and not waiting]
    waited = [t for t, loaded, waiting in samples if loaded and waiting]
    m["guest_in_level_s"] = round(in_game[0] - t0, 1) if in_game else None
    m["guest_waited_s"] = round(waited[-1] - waited[0], 1) if len(waited) > 1 else 0.0
    time.sleep(3.0)
    m["guest_has_character"] = g.bridge.me() is not None

    if m["host_loaded_s"] is None:
        fails.append("no finished host load found after the Retry")
    elif m["guest_in_level_s"] is None:
        fails.append("the guest never came into the level")
    elif m["guest_in_level_s"] < m["host_loaded_s"] - 1.0:  # (log times are whole seconds)
        fails.append(f"the guest was in the level at {m['guest_in_level_s']} s, the host loaded at {m['host_loaded_s']} s")
    if not m["guest_has_character"]:
        fails.append("the guest has no character")
    for name, inst in (("host", host), ("guest", g)):
        if not inst.alive():
            fails.append(f"the {name} crashed")
    return {"passed": not fails, "metrics": m, "notes": fails}
