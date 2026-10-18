"""A screenshot of a menu screen at a real resolution (layout checks).

    python -I Tools/ogmp/menu_shot.py multiplayer_menu.as out.png [1920 1080]
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ogmp.instance import Instance  # noqa: E402


def main():
    script, out = sys.argv[1], os.path.abspath(sys.argv[2])
    w, h = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (1920, 1080)
    inst = Instance("menushot", width=w, height=h)
    inst.prepare()
    inst.start()
    try:
        time.sleep(6.0)  # the main menu
        inst.bridge.call("menu", script=script)
        time.sleep(5.0)  # in, and its animations done
        inst.bridge.call("screenshot", path=out.replace(os.sep, "/"))
        time.sleep(3.0)
        print("captured" if os.path.exists(out) else "no screenshot", out)
    finally:
        inst.stop()


if __name__ == "__main__":
    main()
