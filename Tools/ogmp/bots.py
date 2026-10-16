"""Input-driven bots: they play through the same injected keys a person's
keyboard would produce, so the network carries real play.

    bot = ChaseBot(inst)       # inst: an Instance with an open bridge
    bot.step()                 # call repeatedly (about 10-20 times a second)
"""

import math
import random


class ChaseBot:
    """Faces the nearest other character, runs at it, attacks in range,
    sometimes jumps or blocks. Crude on purpose: it exists to make traffic."""

    def __init__(self, inst, seed=None, target_id=None, enemies_only=False):
        self.inst = inst
        self.rng = random.Random(seed)
        self.attacks = 0
        self.target_id = target_id  # only this one (else the nearest)
        self.enemies_only = enemies_only  # skip characters on our team (Teams parameter)

    def step(self):
        r = self.inst.bridge.call("state")
        chars = r["characters"]
        me = next((c for c in chars if c["id"] == r["my_avatar"]), None)
        if me is None or me.get("knocked_out", 0) != 0:
            self.inst.bridge.release_all()
            return
        others = [c for c in chars if c["id"] != me["id"] and c.get("knocked_out", 0) == 0 and
                  (self.target_id is None or c["id"] == self.target_id) and
                  (not self.enemies_only or c.get("teams", "") != me.get("teams", ""))]
        if not others:
            self.inst.bridge.release_all()
            return
        target = min(others, key=lambda c: math.dist(c["pos"], me["pos"]))
        dx, dz = target["pos"][0] - me["pos"][0], target["pos"][2] - me["pos"][2]
        yaw = math.degrees(math.atan2(-dx, -dz))
        self.inst.bridge.call("exec", target=me["id"], code=f"target_rotation = {yaw:.1f}; cam_rotation = {yaw:.1f};")
        d = math.hypot(dx, dz)

        if d > 1.6:
            self.inst.bridge.hold(up=1.0)
            if self.rng.random() < 0.05:
                self.inst.bridge.hold(steps=6, jump=1.0)
        else:
            self.inst.bridge.release("up")
            roll = self.rng.random()
            if roll < 0.55:
                self.inst.bridge.hold(steps=4, attack=1.0)
                self.attacks += 1
            elif roll < 0.7:
                self.inst.bridge.hold(steps=30, grab=1.0)  # hold block
