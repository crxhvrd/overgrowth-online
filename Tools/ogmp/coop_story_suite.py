"""Co-op story rules: cutscenes, checkpoints, the host's starting weapons
(Online::CoopCutsceneUpdate, Online::CoopUpdate checkpoint revives,
Online::FindSafeSpot, Data/Scripts/coop_story.as).

    coop_cutscene    a dialogue hides the guest's character on both machines
                     (never the host's) and holds it still: forward held on the
                     guest moves it nowhere, in the opening dialogue (playing as
                     the guest's character appears) and in a later one; when it
                     ends the guest stands beside the host, on the ground
    coop_checkpoint  a downed player gets up beside the one who reached the
                     goal's checkpoint, both ways (the guest reaching it: the
                     host's copy of the guest must count)
    coop_weapons     the host starts the level armed: the guest gets copies in
                     the same slots on both machines; a dialogue hides the
                     guest's copies too; after the level starts over the guest
                     has them again
"""

import math
import time

from .scenarios import PROFILES, join, script_errors, wait_loaded
from .coop_suite import CAMPAIGN, _messages, _probe, _in_dialogue, _ko, _down, _common, _local


def _start(ctx, level_id, profile, skip=True):
    host = ctx.instance("host")
    host.bridge.call("net_sim", **PROFILES[profile])
    host.bridge.call("host", level=f"load_campaign_level {level_id}", campaign=CAMPAIGN)
    wait_loaded(host)
    client = ctx.instance("client1")
    client.bridge.call("net_sim", **PROFILES[profile])
    join(ctx, host, client)
    time.sleep(3.0)
    if skip:
        _skip_dialogues(host)
    return host, client


def _skip_dialogues(host):
    for _ in range(8):
        if not _in_dialogue(host):
            return
        host.bridge.hold(steps=10, skip_dialogue=1.0)
        time.sleep(1.5)


def _char(inst, cid):
    return next((c for c in inst.bridge.state() if c["id"] == cid), None)


def _vec(inst, expr, tag):
    """A vec3 from the level scripts."""
    v = _probe(inst, f'"" + ({expr}).x + " " + ({expr}).y + " " + ({expr}).z', tag)
    return [float(x) for x in v.split()] if v else None


def _teleport_own(inst, pos):
    me = inst.bridge.me()["id"]
    inst.bridge.call("exec", target="level",
                     code=f'MovementObject@ m = ReadCharacterID({me}); m.position = vec3({pos[0]}, {pos[1]}, {pos[2]}); '
                          f'm.velocity = vec3(0.0f); m.Execute("FixDiscontinuity();");')


PROGRESS = ('int p = -1; for(int i = 0; i < GetNumHotspots(); ++i) { Hotspot@ h = ReadHotspot(i); '
            'if(ReadObjectFromID(h.GetID()).GetScriptParams().HasParam("goal_0") && h.HasVar("progress")) '
            '{ p = h.GetIntVar("progress"); } } level.SendMessage("probe_progress " + p);')


def _progress(inst):
    _messages(inst, "probe_progress")
    inst.bridge.call("exec", target="level", code=PROGRESS)
    time.sleep(0.4)
    got = _messages(inst, "probe_progress")
    return int(got[-1].split()[1]) if got else None


def _apart(a, b):
    return round(math.dist(a["pos"], b["pos"]), 2)


# ---- Cutscenes -----------------------------------------------------------------

def _held_still(host, client, guest_on_host, tag, m, fails):
    """Forward held on the guest for 2 s in a dialogue: how far its character went."""
    me = client.bridge.me()["id"]
    before = [_char(host, guest_on_host)["pos"], _char(client, me)["pos"]]
    client.bridge.hold(steps=240, up=1.0)
    time.sleep(3.0)
    after = [_char(host, guest_on_host)["pos"], _char(client, me)["pos"]]
    moved = [round(math.dist(a, b), 2) for a, b in zip(before, after)]
    m[f"guest_moved_in_{tag}_host_client_m"] = moved
    if max(moved) > 0.5:
        fails.append(f"in the {tag} the guest's hidden character moved {moved} m (host, client) on forward input")


def coop_cutscene(ctx, profile="typical"):
    m, fails = {}, []
    host, client = _start(ctx, "river_village_intro", profile, skip=False)
    hme, cme = host.bridge.me(), client.bridge.me()
    guest_on_host = _local(host, _common(client, cme["id"]))
    host_on_client = _local(client, _common(host, hme["id"]))

    # The level's opening dialogue, already playing when the guest's character appeared
    m["opening_dialogue_host_client"] = [_in_dialogue(host), _in_dialogue(client)]
    if m["opening_dialogue_host_client"] == [True, True]:
        _held_still(host, client, guest_on_host, "opening dialogue", m, fails)
    else:
        fails.append(f"no opening dialogue on both after the join: {m['opening_dialogue_host_client']}")
    _skip_dialogues(host)
    time.sleep(2.0)

    # The guest away from the host first: at the second checkpoint (walkable ground)
    far = _vec(host, "ReadObjectFromID(85).GetTranslation()", "probe_far")
    _teleport_own(client, far)
    time.sleep(3.0)
    m["apart_before_m"] = _apart(host.bridge.me(), client.bridge.me())

    host.bridge.call("exec", target="level", code='level.SendMessage("start_dialogue \\"Civ1\\"");')
    time.sleep(2.5)
    m["in_dialogue_host_client"] = [_in_dialogue(host), _in_dialogue(client)]
    if m["in_dialogue_host_client"] != [True, True]:
        fails.append(f"the dialogue didn't play on both: {m['in_dialogue_host_client']}")
    m["guest_visible_on_host_client"] = [_char(host, guest_on_host)["visible"], _char(client, cme["id"])["visible"]]
    m["host_visible_on_host_client"] = [_char(host, hme["id"])["visible"], _char(client, host_on_client)["visible"]]
    if m["guest_visible_on_host_client"] != [False, False]:
        fails.append(f"the guest's character shows in the cutscene (host, client): {m['guest_visible_on_host_client']}")
    if m["host_visible_on_host_client"] != [True, True]:
        fails.append(f"the host's character is hidden in the cutscene (host, client): {m['host_visible_on_host_client']}")
    if m["in_dialogue_host_client"] == [True, True]:
        _held_still(host, client, guest_on_host, "later dialogue", m, fails)

    _skip_dialogues(host)
    time.sleep(3.0)
    m["in_dialogue_after"] = [_in_dialogue(host), _in_dialogue(client)]
    g_c, h_c = _char(client, cme["id"]), _char(client, host_on_client)
    g_h, h_h = _char(host, guest_on_host), _char(host, hme["id"])
    m["guest_visible_after_host_client"] = [g_h["visible"], g_c["visible"]]
    m["apart_after_on_client_m"] = _apart(g_c, h_c)
    m["apart_after_on_host_m"] = _apart(g_h, h_h)
    m["height_vs_host_m"] = round(abs(g_c["pos"][1] - h_c["pos"][1]), 2)
    m["guest_on_ground_down"] = [g_c.get("on_ground"), g_c.get("knocked_out")]
    if m["guest_visible_after_host_client"] != [True, True]:
        fails.append(f"the guest stays hidden after the cutscene: {m['guest_visible_after_host_client']}")
    if not (0.6 <= m["apart_after_on_client_m"] <= 4.5):
        fails.append(f"after the cutscene the guest is {m['apart_after_on_client_m']} m from the host (client's view)")
    if m["apart_after_on_host_m"] > 5.0:
        fails.append(f"after the cutscene the guest is {m['apart_after_on_host_m']} m from the host (host's view)")
    if m["height_vs_host_m"] > 0.8 or not g_c.get("on_ground") or g_c.get("knocked_out", 0) != 0:
        fails.append(f"the guest isn't standing on the ground beside the host: height {m['height_vs_host_m']}, "
                     f"on_ground/knocked_out {m['guest_on_ground_down']}")

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


# ---- Checkpoints ----------------------------------------------------------------

CHECKPOINT_0 = 477  # og_story 01: goal_0 "reach 0"
CHECKPOINT_1 = 85   # goal_1 "reach 1"


def _checkpoint_round(m, fails, tag, reacher, downed, reacher_me, downed_me, reacher_on_downed, hotspot, want_progress, host):
    _ko(downed)
    time.sleep(2.0)
    if not _down(downed, downed_me):
        fails.append(f"{tag}: couldn't knock the player out")
        return
    spot = _vec(host, f"ReadObjectFromID({hotspot}).GetTranslation()", "probe_cp")
    _teleport_own(reacher, spot)
    t0, prog, up = time.time(), None, False
    while time.time() - t0 < 12.0:
        time.sleep(0.5)
        prog = _progress(host)
        up = not _down(downed, downed_me)
        if up and prog is not None and prog >= want_progress:
            break
    time.sleep(1.5)
    # A goal can start a dialogue: it moves the host's character, and at its
    # end the guest stands beside the host again. Judge after it
    m[f"{tag}_dialogue"] = _in_dialogue(host)
    if m[f"{tag}_dialogue"]:
        _skip_dialogues(host)
        time.sleep(3.0)
    m[f"{tag}_progress"] = prog
    m[f"{tag}_back_up"] = up
    if prog is None or prog < want_progress:
        fails.append(f"{tag}: the checkpoint didn't count (progress {prog}, want {want_progress})")
    if not up:
        fails.append(f"{tag}: the downed player didn't get up")
        return
    d, r = _char(downed, downed_me), _char(downed, reacher_on_downed)
    m[f"{tag}_beside_reacher_m"] = _apart(d, r)
    m[f"{tag}_height_vs_reacher_m"] = round(abs(d["pos"][1] - r["pos"][1]), 2)
    m[f"{tag}_on_ground"] = d.get("on_ground")
    if not (0.6 <= m[f"{tag}_beside_reacher_m"] <= 4.5):
        fails.append(f"{tag}: got up {m[f'{tag}_beside_reacher_m']} m from the one who reached the checkpoint")
    if m[f"{tag}_height_vs_reacher_m"] > 0.8 or not d.get("on_ground"):
        fails.append(f"{tag}: not standing on the ground (height {m[f'{tag}_height_vs_reacher_m']}, on_ground {d.get('on_ground')})")


def coop_checkpoint(ctx, profile="typical"):
    m, fails = {}, []
    host, client = _start(ctx, "river_village_intro", profile)
    hme, cme = host.bridge.me()["id"], client.bridge.me()["id"]
    host_on_client = _local(client, _common(host, hme))
    guest_on_host = _local(host, _common(client, cme))
    m["progress_start"] = _progress(host)

    # The host is down, the guest reaches checkpoint 0
    _checkpoint_round(m, fails, "guest_reaches", client, host, cme, hme, guest_on_host, CHECKPOINT_0, 1, host)
    _skip_dialogues(host)
    time.sleep(2.0)
    # The guest is down, the host reaches checkpoint 1
    _checkpoint_round(m, fails, "host_reaches", host, client, hme, cme, host_on_client, CHECKPOINT_1, 2, host)

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}


# ---- The host's starting weapons -----------------------------------------------------

def _held(inst, char_id):
    return sorted((it["file"], it["hidden"]) for it in inst.bridge.call("state")["items"] if it["held_by"] == char_id)


DIALOGUE_NAME = ('string n = ""; array<int> ids = GetObjectIDsType(_placeholder_object); '
                 'for(uint i = 0; i < ids.length(); ++i) { ScriptParams@ p = ReadObjectFromID(ids[i]).GetScriptParams(); '
                 'if(n == "" && p.HasParam("Dialogue") && p.HasParam("DisplayName")) { n = p.GetString("DisplayName"); } } '
                 'level.SendMessage("probe_dlg " + n);')


def coop_weapons(ctx, profile="typical"):
    m, fails = {}, []
    host, client = _start(ctx, "slaver_camp_night", profile)
    hme, cme = host.bridge.me()["id"], client.bridge.me()["id"]
    guest_on_host = _local(host, _common(client, cme))
    time.sleep(5.0)  # (coop_story.as: 3 s after the guest's character appears)

    host_files = sorted(f for f, _ in _held(host, hme))
    m["host_starts_with"] = host_files
    m["guest_holds_on_host"] = [f for f, _ in _held(host, guest_on_host)]
    m["guest_holds_on_client"] = [f for f, _ in _held(client, cme)]
    if not host_files:
        fails.append("the host's character starts unarmed here (test level?)")
    if sorted(m["guest_holds_on_host"]) != host_files or sorted(m["guest_holds_on_client"]) != host_files:
        fails.append(f"the guest's weapons (host's view {m['guest_holds_on_host']}, client's {m['guest_holds_on_client']}) "
                     f"aren't the host's {host_files}")

    # A dialogue hides the guest's weapons too, never the host's
    _messages(host, "probe_dlg")
    host.bridge.call("exec", target="level", code=DIALOGUE_NAME)
    time.sleep(0.4)
    got = _messages(host, "probe_dlg")
    name = got[-1].split(" ", 1)[1] if got and " " in got[-1] else ""
    m["dialogue"] = name
    if name:
        host.bridge.call("exec", target="level", code=f'level.SendMessage("start_dialogue \\"{name}\\"");')
        time.sleep(2.5)
        m["guest_weapons_hidden_host_client"] = [[h for _, h in _held(host, guest_on_host)], [h for _, h in _held(client, cme)]]
        m["host_weapons_hidden_host"] = [h for _, h in _held(host, hme)]
        if _in_dialogue(host):
            if not all(all(x) and x for x in m["guest_weapons_hidden_host_client"]):
                fails.append(f"the guest's weapons show in the cutscene: {m['guest_weapons_hidden_host_client']}")
            if any(m["host_weapons_hidden_host"]):
                fails.append("the host's weapons are hidden in the cutscene")
        _skip_dialogues(host)
        time.sleep(3.0)

    # Everyone down: the level starts over; the guest is armed again
    _ko(client)
    _ko(host)
    t0 = time.time()
    while time.time() - t0 < 25.0:
        time.sleep(1.0)
        if not _down(host, hme) and not _down(client, cme):
            break
    _skip_dialogues(host)
    time.sleep(6.0)
    m["guest_holds_after_restart_host_client"] = [[f for f, _ in _held(host, guest_on_host)], [f for f, _ in _held(client, cme)]]
    if any(sorted(x) != host_files for x in m["guest_holds_after_restart_host_client"]):
        fails.append(f"after the level started over the guest holds {m['guest_holds_after_restart_host_client']}")

    errs = script_errors(host) + script_errors(client)
    if errs:
        fails.append(f"script errors: {errs[:4]}")
    return {"passed": not fails, "metrics": m, "notes": fails}
