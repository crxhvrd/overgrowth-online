"""Scenario: the client view is driven by exactly one character and does not
jump around (the shaking camera the user saw). Run from tools/."""
p = "ogmp/scenarios.py"
s = open(p, encoding="utf-8").read()
ADD = '''

def client_camera(ctx, profile="lan"):
    """On a client exactly one character owns controller 0 and camera 0 (its
    own), and the camera stays put while everyone stands still."""
    host, (c1,) = session(ctx, 1, profile=profile)
    time.sleep(2.0)
    r = c1.bridge.call("state")
    chars = r["characters"]
    owners = [c for c in chars if c["controlled"] and c["controller_id"] == 0 and c.get("camera_id", 0) == 0]
    jumps = []
    last = None
    t0 = time.time()

    while time.time() - t0 < 4.0:
        pos = c1.bridge.status()["camera_pos"]
        if last is not None:
            jumps.append(dist(pos, last))
        last = pos

    owner_ids = [c["id"] for c in owners]
    metrics = {"camera_owners": owner_ids, "my_avatar": r["my_avatar"],
               "camera_jump_max_m": round(max(jumps), 3) if jumps else None,
               "camera_jump_median_m": round(statistics.median(jumps), 4) if jumps else None}
    ctx.shot(c1, "camera")
    passed = owner_ids == [r["my_avatar"]] and jumps and max(jumps) < 0.25
    notes = [f"client characters: {[(c['id'], c['controller_id'], c.get('camera_id'), c['remote']) for c in chars]}"]
    return {"passed": passed, "metrics": metrics, "notes": notes}
'''
if "def client_camera(ctx" not in s:
    s = s.replace("\n\nSCENARIOS = {", ADD + "\n\nSCENARIOS = {", 1)
    s = s.replace('''    "clean_leave": clean_leave,
}''', '''    "clean_leave": clean_leave,
    "client_camera": client_camera,
}''', 1)
    s = s.replace('''               "late_join", "drop_rejoin", "clean_leave", "bot_duel"],''', '''               "late_join", "drop_rejoin", "clean_leave", "client_camera", "bot_duel"],''', 1)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
print("ok")
