"""The connecting screen gives up on a host that never answers: a connect to an
unreachable address, the connecting screen (screenshot), and after the timeout
the session is over, the game still runs and is back on a menu.

    python -I Tools/ogmp/connect_cancel_check.py out.png
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ogmp.instance import Instance  # noqa: E402


def main():
    out = os.path.abspath(sys.argv[1])
    inst = Instance("connectcheck", width=1920, height=1080)
    inst.prepare()
    inst.start()
    try:
        time.sleep(6.0)
        inst.bridge.call("menu", script="multiplayer_menu.as")
        time.sleep(2.0)
        r = inst.bridge.call("connect", ip="10.255.255.1")  # (nothing answers there)
        print("connecting, online active:", r.get("active"))
        inst.bridge.call("menu", script="multiplayer_menu_connecting.as")
        time.sleep(5.0)
        inst.bridge.call("screenshot", path=out.replace(os.sep, "/"))
        time.sleep(22.0)  # the screen gives up after 20 s, then goes back after 3
        st = inst.bridge.status()
        print("after the timeout: alive", inst.alive(), "online", st.get("online", {}).get("active"), "level", repr(st.get("level", "")))
    finally:
        inst.stop()


if __name__ == "__main__":
    main()
