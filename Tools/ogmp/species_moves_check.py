"""Can a player switched to another species roll, flip and jump-kick? One
instance, offline, og_story 01: our character becomes `path` (as the arena
randomizer switches it), then forward + crouch (a roll), jump + crouch in the
air (a flip), jump + attack near an enemy (the air attack).

    python -I Tools/ogmp/species_moves_check.py [Data/Characters/dog_soldier.xml]
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ogmp.instance import Instance, user_game_running  # noqa: E402

PROBE = ('level.SendMessage("probe_moves " + (flip_info.IsRolling() ? 1 : 0) + " " + (flip_info.IsFlipping() ? 1 : 0) + " " + '
         'species + " " + (attacking_with_throw) + " " + (state));')


def probe(inst, me):
    inst.bridge.call("level_messages", prefix="probe_moves")
    inst.bridge.call("exec", target=me, code=PROBE)
    time.sleep(0.15)
    got = inst.bridge.call("level_messages", prefix="probe_moves")["messages"]
    return got[-1] if got else None


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "Data/Characters/dog_soldier.xml"
    if user_game_running():
        sys.exit("the user's Overgrowth is running: not now")
    inst = Instance("speciescheck")
    inst.prepare()
    inst.start()
    try:
        inst.bridge.call("load_level", path="Data/Levels/og_story/01_Slaver_Camp_Landing.xml")
        t0 = time.time()
        while time.time() - t0 < 120 and not inst.bridge.status().get("level_loaded"):
            time.sleep(1.0)
        time.sleep(4.0)
        for _ in range(6):  # (the intro dialogue)
            inst.bridge.hold(steps=10, skip_dialogue=1.0)
            time.sleep(1.0)
        me = next(c["id"] for c in inst.bridge.state() if c["controlled"])  # (offline: the player's)
        print("before:", probe(inst, me))
        inst.bridge.call("exec", target=me, code=f'SwitchCharacter("{path}"); FixDiscontinuity();')
        time.sleep(2.0)
        print("switched:", probe(inst, me))

        # A flip: jump, then crouch in the air
        inst.bridge.hold(steps=-1, up=1.0)
        time.sleep(0.5)
        inst.bridge.hold(steps=20, jump=1.0)
        time.sleep(0.25)
        inst.bridge.hold(steps=12, crouch=1.0)
        flipped = []
        for _ in range(8):
            time.sleep(0.08)
            flipped.append(probe(inst, me))
        inst.bridge.release_all()
        print("flip window:", flipped)
        time.sleep(1.5)

        # A roll: forward, then crouch pressed while moving
        inst.bridge.hold(steps=-1, up=1.0)
        time.sleep(0.8)
        inst.bridge.hold(steps=12, crouch=1.0)
        rolled = []
        for _ in range(8):
            time.sleep(0.08)
            rolled.append(probe(inst, me))
        inst.bridge.release_all()
        print("roll window:", rolled)
        time.sleep(1.5)

    finally:
        inst.stop()


if __name__ == "__main__":
    main()
