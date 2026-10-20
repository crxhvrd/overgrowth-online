"""Host + guest on the raft intro; a burst of guest (and host) screenshots: character drawing on a guest (streamed characters draw differently).

    python -I Tools/ogmp/coop_guest_shots.py <out dir> [exe]  (exe: e.g. a test/builds snapshot)
"""
import os
import sys
import time

out = os.path.abspath(sys.argv[1])
if len(sys.argv) > 2:
    os.environ["OGMP_EXE"] = sys.argv[2]
sys.path.insert(0, "D:/OvergrowthMP/Tools")
from ogmp.scenarios import Context, wait_loaded, join  # noqa: E402

os.makedirs(out, exist_ok=True)
ctx = Context(out)
try:
    host = ctx.instance("flk_host", width=960, height=540, keys={"limit_fps_in_game": "false", "vsync": "0"})
    host.bridge.call("host", level="load_campaign_level river_village_intro", campaign="com-wolfire-overgrowth-campaign")
    wait_loaded(host)
    guest = ctx.instance("flk_guest", width=960, height=540, keys={"limit_fps_in_game": "false", "vsync": "0"})
    join(ctx, host, guest)
    time.sleep(8.0)
    for i in range(16):
        guest.bridge.call("screenshot", path=f"{out}/guest_{i:02d}.png".replace(os.sep, "/"))
        time.sleep(0.12)
    for i in range(4):
        host.bridge.call("screenshot", path=f"{out}/host_{i:02d}.png".replace(os.sep, "/"))
        time.sleep(0.12)
finally:
    for inst in ctx.instances:
        try:
            inst.stop()
        except Exception:
            pass
print("done", out)
