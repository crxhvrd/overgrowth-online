"""Building a level together (docs_mp_editing_plan.md, Online/edit_sync.h): a host, a guest who
builds, a guest who doesn't, and a guest who joins later. Editor operations go through the test
bridge's "edit" command, which does what the editor's tools do. Checks: build mode and who may use
the editor; a builder's spawn, move, parameter, group and delete reaching everyone with the same
shared ids; the host's spawn reaching the guests; a non-builder's spawn going nowhere; a late
joiner getting the edits; leaving build mode closing the builder's editor."""

import time

from .scenarios import LEVEL, join, wait_loaded

BLOCK = "Data/Objects/block.xml"


def _edit(inst, **kw):
    return inst.bridge.call("edit", **kw)


def _build(inst, **kw):
    return inst.bridge.call("build", **kw)


def _blocks(inst):
    return _edit(inst, op="find", path=BLOCK)["objects"]


def _near(a, b, tol=0.05):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def _at(inst, pos):
    return [o for o in _blocks(inst) if _near(o["pos"], pos)]


def _player_id(inst):
    inst.bridge.call("level_messages")
    inst.bridge.call("exec", target="level", code='level.SendMessage("bt me " + Online_GetOwnPlayerID());')
    time.sleep(0.3)
    for line in inst.bridge.call("level_messages", prefix="bt me ")["messages"]:
        return int(line.split()[2])
    return -1


def mp_build(ctx):
    m, fails = {}, []
    host = ctx.instance("host")
    host.bridge.call("host", level=LEVEL)
    wait_loaded(host)
    c1 = ctx.instance("client1")
    join(ctx, host, c1)
    c2 = ctx.instance("client2")
    join(ctx, host, c2)
    time.sleep(1.5)
    guests = {"c1": c1, "c2": c2}
    everyone = {"host": host, **guests}
    pid = {name: _player_id(inst) for name, inst in guests.items()}
    m["players"] = pid

    # --- build mode, builders, the editor ----------------------------------------------------
    _build(host, builder=pid["c1"], may=True)
    _build(host, on=True)
    time.sleep(2.5)
    state = {name: _build(inst) for name, inst in everyone.items()}
    m["state"] = {k: (v["build_mode"], v["may_edit"]) for k, v in state.items()}
    if not all(v["build_mode"] for v in state.values()):
        fails.append(f"build mode not everywhere: {m['state']}")
    if not state["c1"]["may_edit"] or state["c2"]["may_edit"]:
        fails.append(f"who may edit: {m['state']} (want c1 yes, c2 no)")
    opened = {name: _build(inst, editor=True)["editor_ok"] for name, inst in everyone.items()}
    m["editor_opened"] = opened
    if opened != {"host": True, "c1": True, "c2": False}:
        fails.append(f"opening the editor: {opened} (want host and c1, not c2)")

    def settle(seconds=2.0):
        time.sleep(seconds)

    # --- 1. the builder spawns a block -------------------------------------------------------------
    P, P2, Q, R = [12.0, 101.0, 13.0], [15.0, 102.0, 11.0], [-14.0, 103.0, 9.0], [5.0, 104.0, -16.0]
    _edit(c1, op="spawn", path=BLOCK, pos=P)
    settle()
    spawned = {name: _at(inst, P) for name, inst in everyone.items()}
    m["spawn"] = {k: [(o["id"], o["shared"]) for o in v] for k, v in spawned.items()}
    if any(len(v) != 1 for v in spawned.values()):
        fails.append(f"the builder's block: {m['spawn']} (want one on every machine)")
    elif len({v[0]["shared"] for v in spawned.values()}) != 1:
        fails.append(f"the builder's block has different shared ids: {m['spawn']}")
    elif not spawned["c1"][0]["selected"]:
        fails.append("the builder's block came back unselected")

    # --- 2. moves it, 3. names a parameter -----------------------------------------------------------
    if len(spawned["c1"]) == 1:
        mine = spawned["c1"][0]["id"]
        _edit(c1, op="move", obj=mine, pos=P2)
        settle()
        moved = {name: len(_at(inst, P2)) for name, inst in everyone.items()}
        m["move"] = moved
        m["blocks_after_move"] = {name: [(o["id"], [round(x, 2) for x in o["pos"]]) for o in _blocks(inst)] for name, inst in everyone.items()}
        if any(v != 1 for v in moved.values()):
            fails.append(f"the move: {moved}")
        mine = _at(c1, P2)[0]["id"] if _at(c1, P2) else mine
        _edit(c1, op="param", obj=mine, key="build_test", value="hello world")
        settle()
        values = {}
        for name, inst in everyone.items():
            here = _at(inst, P2)
            values[name] = _edit(inst, op="get", obj=here[0]["id"], key="build_test").get("value") if here else None
        m["param"] = values
        if any(v != "hello world" for v in values.values()):
            fails.append(f"the parameter: {values}")

    # --- 4. the host spawns one; 5. the non-builder's goes nowhere ---------------------------------
    _edit(host, op="spawn", path=BLOCK, pos=Q)
    _edit(c2, op="spawn", path=BLOCK, pos=R)
    settle()
    m["host_spawn"] = {name: len(_at(inst, Q)) for name, inst in everyone.items()}
    if any(v != 1 for v in m["host_spawn"].values()):
        fails.append(f"the host's block: {m['host_spawn']}")
    m["refused_spawn"] = {name: len(_at(inst, R)) for name, inst in everyone.items()}
    if m["refused_spawn"]["host"] or m["refused_spawn"]["c1"]:
        fails.append(f"the non-builder's block reached others: {m['refused_spawn']}")

    # --- 6. the builder groups its block with the host's --------------------------------------------
    a, b = _at(c1, P2), _at(c1, Q)
    if a and b:
        _edit(c1, op="group", objs=[a[0]["id"], b[0]["id"]])
        settle()
        grouped = {}
        for name, inst in everyone.items():
            x, y = _at(inst, P2), _at(inst, Q)
            grouped[name] = (x[0]["parent"] if x else None, y[0]["parent"] if y else None)
        m["group"] = grouped
        for name, (pa, pb) in grouped.items():
            if pa is None or pa == -1 or pa != pb:
                fails.append(f"{name}: the two blocks aren't in one group: {grouped[name]}")
        # (no id twice: the group's isn't one of its blocks')
        for name, inst in everyone.items():
            ids = [o["id"] for o in _at(inst, P2) + _at(inst, Q)]
            if any(pa == i for i in ids for pa in grouped[name]):
                fails.append(f"{name}: the group has a block's id: group {grouped[name]}, blocks {ids}")

    # --- 7. a guest joining now gets the edits ------------------------------------------------------
    c3 = ctx.instance("client3")
    join(ctx, host, c3)
    settle(3.0)
    late = {"P2": len(_at(c3, P2)), "Q": len(_at(c3, Q)), "R": len(_at(c3, R))}
    pa, pb = (_at(c3, P2) or [{}])[0].get("parent"), (_at(c3, Q) or [{}])[0].get("parent")
    m["late_joiner"] = {**late, "grouped": pa is not None and pa != -1 and pa == pb}
    if late != {"P2": 1, "Q": 1, "R": 0} or not m["late_joiner"]["grouped"]:
        fails.append(f"the late joiner: {m['late_joiner']} (want the grouped blocks at P2 and Q, none at R)")

    # --- 8. the builder deletes the group -------------------------------------------------------------
    g = _at(c1, P2)
    if g and g[0]["parent"] not in (None, -1):
        _edit(c1, op="delete", obj=g[0]["parent"])
        settle()
        m["delete"] = {name: len(_at(inst, P2)) + len(_at(inst, Q)) for name, inst in {**everyone, "c3": c3}.items()}
        if any(v != 0 for v in m["delete"].values()):
            fails.append(f"the delete: {m['delete']} (blocks left)")

    # --- 9. two builders: presence and claims ---------------------------------------------------------
    S, S2, S3 = [3.0, 105.0, 21.0], [4.0, 105.0, 24.0], [6.0, 105.0, 27.0]
    _build(host, builder=pid["c2"], may=True)
    settle(2.5)
    m["c2_editor"] = _build(c2, editor=True)["editor_ok"]
    _edit(c1, op="spawn", path=BLOCK, pos=S)
    settle()
    held = _at(c1, S)
    if held and m["c2_editor"]:
        _edit(c1, op="select", objs=[held[0]["id"]])
        settle(1.5)
        hs = _build(host)
        shared = str(held[0]["shared"])
        c1_presence = hs["presence"].get(str(pid["c1"]), {})
        m["presence_on_host"] = {"editing": c1_presence.get("editing"), "selection": c1_presence.get("selection")}
        m["claims_on_host"] = hs["claims"]
        if not c1_presence.get("editing") or held[0]["shared"] not in (c1_presence.get("selection") or []):
            fails.append(f"the host doesn't see c1's presence: {m['presence_on_host']}")
        if hs["claims"].get(shared) != pid["c1"]:
            fails.append(f"claims on the host: {hs['claims']} (want {shared} -> {pid['c1']})")
        seen = _build(c2)["presence"].get(str(pid["c1"]), {})
        if not seen.get("editing"):
            fails.append("c2 doesn't see c1 building")
        # c2 moves what c1 holds: refused, everyone keeps it at S
        theirs = _at(c2, S)
        if theirs:
            _edit(c2, op="move", obj=theirs[0]["id"], pos=S2)
            settle()
            m["claimed_move"] = {name: (len(_at(inst, S)), len(_at(inst, S2))) for name, inst in everyone.items()}
            if any(v != (1, 0) for v in m["claimed_move"].values()):
                fails.append(f"a move of a claimed block went through: {m['claimed_move']} (want it at S everywhere)")
            told = [line for line in _build(c2)["chat"] if "put back" in line]
            m["claim_told"] = told[-1:] if told else []
            if not told:
                fails.append("c2 wasn't told its move of a claimed block was put back")
            # c1 lets go: c2's move goes through
            _edit(c1, op="select", objs=[])
            settle(1.5)
            theirs = _at(c2, S)
            if theirs:
                _edit(c2, op="move", obj=theirs[0]["id"], pos=S3)
                settle()
                m["free_move"] = {name: len(_at(inst, S3)) for name, inst in everyone.items()}
                if any(v != 1 for v in m["free_move"].values()):
                    fails.append(f"a move after the claim ended: {m['free_move']}")

    # --- 10. a builder's undo and redo -------------------------------------------------------------
    U1, U2 = [-8.0, 106.0, -12.0], [-9.0, 106.0, -15.0]
    _edit(c1, op="spawn", path=BLOCK, pos=U1)
    settle()
    mine = _at(c1, U1)
    if mine:
        # c1's drag, frames only for a while (the others see it move): nobody else sends it as
        # their edit (the host's scan did, and c1's undo lost the move); then c1 lets go
        others = {"host": host, "c2": c2}
        sent0 = {n: _build(i)["batches_sent"] for n, i in others.items()}
        _edit(c1, op="preview", obj=mine[0]["id"], pos=U2)
        settle(2.5)
        m["drag_frames"] = {"at_U2": {n: len(_at(i, U2)) for n, i in others.items()},
                            "sent": {n: _build(i)["batches_sent"] - sent0[n] for n, i in others.items()}}
        if any(m["drag_frames"]["sent"].values()) or any(v != 1 for v in m["drag_frames"]["at_U2"].values()):
            fails.append(f"c1's drag frames: {m['drag_frames']} (want shown at U2 on the others, sent by none of them)")
        _edit(c1, op="move", obj=mine[0]["id"], pos=U2)
        settle()
        where = lambda: {name: (len(_at(inst, U1)), len(_at(inst, U2))) for name, inst in everyone.items()}
        m["undo"] = {"moved": where()}
        _edit(c1, op="undo")
        settle()
        m["undo"]["undo_move"] = where()
        _edit(c1, op="undo")
        settle()
        m["undo"]["undo_spawn"] = where()
        _edit(c1, op="redo")
        settle()
        m["undo"]["redo_spawn"] = where()
        _edit(c1, op="redo")
        settle()
        m["undo"]["redo_move"] = where()
        want = {"moved": (0, 1), "undo_move": (1, 0), "undo_spawn": (0, 0), "redo_spawn": (1, 0), "redo_move": (0, 1)}
        for stage, expect in want.items():
            got = m["undo"][stage]
            if any(v != expect for v in got.values()):
                fails.append(f"undo/redo, {stage}: {got} (want {expect} everywhere)")

    # --- 11. the host's autosave: in its write folder, with the builders' work; the level's file untouched
    import glob
    import os
    from .instance import ROOT
    level_file = ROOT + "/Data/Levels/" + LEVEL
    before = os.path.getmtime(level_file) if os.path.exists(level_file) else None
    _build(host, autosave=True)
    settle(1.5)
    saves = glob.glob(host.writedir + "Data/Levels/autosave/*_autosave.xml")
    m["autosave"] = [os.path.basename(f) for f in saves]
    if not saves:
        fails.append("no autosave file in the host's write folder")
    elif open(saves[0], encoding="utf-8", errors="replace").read().count("block.xml") < 1:
        fails.append("the autosave has none of the builders' blocks")
    if before is not None and os.path.getmtime(level_file) != before:
        fails.append("the level's own file was written")

    # --- 11b. a reflection capture placed by the builder; an edit later every machine re-renders it
    RC = [2.0, 104.0, 2.0]
    _edit(c1, op="spawn", path="Data/Objects/local_reflection_capture.xml", pos=RC)
    settle(3.0)
    _edit(c1, op="spawn", path=BLOCK, pos=[2.0, 103.0, 4.0])
    settle(3.5)
    m["refl"] = {name: (r["refl_captures"], r["refl_dirty"]) for name, r in ((n, _build(i)) for n, i in {**everyone, "c3": c3}.items())}
    if any(v != (1, 0) for v in m["refl"].values()):
        fails.append(f"the reflection capture after an edit (captures marked, still unrendered): {m['refl']} (want (1, 0) everywhere)")

    # --- 12. saving while building: the save state everywhere; a builder's request, refused once,
    # then answered on the host's prompt, then saved at once when builders may save
    saved_file = host.writedir + "Data/Levels/mp_build_saved.xml"
    m["save"] = {"save_to": _build(host, save_to=saved_file).get("save_to_ok")}
    settle(2.5)
    st = _build(c1)
    m["save"]["after_host_save"] = (st["saved_ago"], st["unsaved"], st["save_state"])
    if not m["save"]["save_to"] or not (0 <= st["saved_ago"] < 10) or st["unsaved"] != 0:
        fails.append(f"the host's save as a builder sees it: {m['save']}")
    if not any("saved the level" in line for line in st["chat"]):
        fails.append("the builder wasn't told the host saved")
    V, W, X = [7.0, 107.0, -6.0], [9.0, 107.0, -8.0], [11.0, 107.0, -10.0]
    _edit(c1, op="spawn", path=BLOCK, pos=V)
    settle(3.0)
    m["save"]["unsaved_after_spawn"] = _build(c1)["unsaved"]
    if m["save"]["unsaved_after_spawn"] < 1:
        fails.append(f"the builder doesn't see the unsaved change: {m['save']}")
    # refused: "Not now"
    _build(c1, save=True)
    settle(1.5)
    m["save"]["request_on_host"] = _build(host)["save_request"]
    if m["save"]["request_on_host"] != pid["c1"]:
        fails.append(f"the host has no save request from c1: {m['save']['request_on_host']}")
    _build(host, answer_save=False)
    settle(1.5)
    if not any("didn't save" in line for line in _build(c1)["chat"]):
        fails.append("the builder wasn't told the host didn't save")
    # answered: "Save"
    settle(2.0)  # (a request at most every 3 s)
    _build(c1, save=True)
    settle(1.5)
    if not any("asks you to save" in line for line in _build(host)["chat"]):
        fails.append("the host wasn't asked in the chat")
    _build(host, answer_save=True)
    settle(3.0)
    st = _build(c1)
    m["save"]["after_answer"] = (st["saved_ago"], st["unsaved"])
    if st["unsaved"] != 0 or not (0 <= st["saved_ago"] < 10):
        fails.append(f"after the host saved on request: {m['save']['after_answer']}")
    if open(saved_file, encoding="utf-8", errors="replace").read().count("block.xml") < 1:
        fails.append("the saved level has none of the builders' blocks")
    # builders may save: at once
    _build(host, builders_save=True)
    _edit(c1, op="spawn", path=BLOCK, pos=W)
    settle(3.5)
    _build(c1, save=True)
    settle(3.0)
    hs, st = _build(host), _build(c1)
    m["save"]["builders_may_save"] = {"host": (hs["saved_ago"], hs["unsaved"], hs["save_request"]), "c1": (st["saved_ago"], st["unsaved"])}
    if hs["save_request"] != -1 or hs["unsaved"] != 0 or st["unsaved"] != 0:
        fails.append(f"a builder's save when builders may save: {m['save']['builders_may_save']}")
    _build(host, builders_save=False)
    _edit(c1, op="spawn", path=BLOCK, pos=X)  # (an edit for the navmesh to follow)
    settle(2.0)

    # --- 13. the host restarts the level (the navmesh rebuilt for the edits first), then turns building
    # off: the builder's editor closes --------------------------------------------------------------
    _build(host, restart=True)
    settle(2.0)
    _build(host, on=False)
    settle(3.0)
    after = _build(c1)
    m["after_build"] = {"build_mode": after["build_mode"], "may_edit": after["may_edit"], "editor_open": after["editor_open"]}
    if after["build_mode"] or after["may_edit"] or after["editor_open"]:
        fails.append(f"after build mode: {m['after_build']}")
    hs = _build(host)
    m["navmesh"] = {"builds": hs["nav_builds"], "ms": round(hs["nav_build_ms"])}
    # (the game runs again, the host's editor still open: nothing is sent as an edit)
    sent = hs["sent"]
    settle(3.0)
    m["sent_in_play"] = _build(host)["sent"] - sent
    if m["sent_in_play"]:
        fails.append(f"the host sent {m['sent_in_play']} edit batch(es) while playing")
    if hs["nav_builds"] != 1:
        fails.append(f"the host's navmesh on the restart: {m['navmesh']} (want one rebuild)")

    # --- 14. edit lines in the chat; reflection captures refreshed on every machine ------------------
    lines = [line for line in _build(c3)["chat"] if " placed " in line or " removed " in line or " changed " in line]
    m["chat_lines"] = lines[-4:]
    if not any(" placed " in line for line in lines):
        fails.append("no edit lines in the late joiner's chat")
    m["refl_refreshes"] = {name: _build(inst)["refl_refreshes"] for name, inst in {**everyone, "c3": c3}.items()}
    if any(v < 1 for v in m["refl_refreshes"].values()):
        fails.append(f"reflection captures never refreshed: {m['refl_refreshes']}")
    m["sync"] = {name: {k: _build(inst)[k] for k in ("scans", "sent", "applied", "refused", "edited")} for name, inst in {**everyone, "c3": c3}.items()}

    return {"passed": not fails, "metrics": m, "notes": fails}


def mp_build_level(ctx):
    """Build a level (the Multiplayer menu): the level hosted in build mode with the host in the
    editor; players who join may build; a guest loads a level outside the campaigns; a level of the
    host's own (its write folder) reaches a joiner as a file, and everyone on a level change."""
    m, fails = {}, []
    host = ctx.instance("host")
    # (the menu's run_file: Online_BuildOnLoad, Online_Host, then the level)
    _build(host, build_on_load=True)
    host.bridge.call("host", level="Data/Levels/nothing.xml")
    wait_loaded(host)
    time.sleep(2.0)
    hs = _build(host)
    m["host"] = {k: hs[k] for k in ("build_mode", "editor_open", "everyone_builds", "level_name")}
    hst = host.bridge.status()
    m["host"]["waiting_for_input"] = hst["waiting_for_input"]
    # (the editor's cursor: a level loads with it hidden; the editor shows it, and nothing grabs the mouse)
    m["host"]["cursor"] = {"visible": hst["cursor_visible"], "grabbed": hst["grab_mouse"]}
    if not hst["cursor_visible"] or hst["grab_mouse"]:
        fails.append(f"no cursor in the host's editor: {m['host']['cursor']}")
    if not (hs["build_mode"] and hs["editor_open"] and hs["everyone_builds"]):
        fails.append(f"the hosted level isn't building: {m['host']}")
    if m["host"]["waiting_for_input"]:
        fails.append("the host is still on the press-any-key screen (black behind the editor)")
    # (what the host sees: the level, not black: a fade's clock stood still while building)
    shot = (host.writedir + "build_level_host.png").replace("\\", "/")
    host.bridge.call("screenshot", path=shot)
    time.sleep(2.0)
    from PIL import Image, ImageStat
    m["host"]["brightness"] = round(sum(ImageStat.Stat(Image.open(shot).convert("L")).mean), 1)
    if m["host"]["brightness"] < 20:
        fails.append(f"the host's screen is black: brightness {m['host']['brightness']}")

    c1 = ctx.instance("client1")
    join(ctx, host, c1)
    time.sleep(3.0)
    s1 = _build(c1)
    m["c1"] = {"may_edit": s1["may_edit"], "level": c1.bridge.status()["level"]}
    if not s1["may_edit"]:
        fails.append(f"a player who joined may not build: {m['c1']}")
    P = [3.0, 2.0, 4.0]
    m["c1_editor"] = _build(c1, editor=True)["editor_ok"]
    _edit(c1, op="spawn", path=BLOCK, pos=P)
    time.sleep(2.5)
    m["spawn"] = {"host": len(_at(host, P)), "c1": len(_at(c1, P))}
    if m["spawn"] != {"host": 1, "c1": 1}:
        fails.append(f"the joiner's block: {m['spawn']}")

    # The host saves to its write folder: a joiner from now on gets that file
    rel = "Data/Levels/mp_build_level_test.xml"
    m["save_to"] = _build(host, save_to=host.writedir + rel).get("save_to_ok")
    time.sleep(1.5)
    m["level_name_after_save"] = _build(host)["level_name"]
    c2 = ctx.instance("client2")
    join(ctx, host, c2)
    time.sleep(3.0)
    s2 = _build(c2)
    m["c2"] = {"received": s2["level_files_received"], "level": c2.bridge.status()["level"], "blocks": len(_at(c2, P)), "may_edit": s2["may_edit"]}
    if s2["level_files_received"] != 1 or "_online" not in m["c2"]["level"] or m["c2"]["blocks"] != 1 or not s2["may_edit"]:
        fails.append(f"the joiner and the host's own file: {m['c2']} (want the file received, loaded, with the block, may build)")

    # The host loads its own file again: everyone gets it and loads it (and nobody waits on a key).
    # Time frozen first: it stays frozen (a load cleared the pause, and the level ran under the editors)
    _build(host, freeze=True)
    time.sleep(1.0)
    host.bridge.call("load_level", path=rel, discard=True)
    time.sleep(4.0)
    for inst in (host, c1, c2):
        wait_loaded(inst, timeout=120)
    time.sleep(3.0)
    m["waiting_after_change"] = {name: inst.bridge.status()["waiting_for_input"] for name, inst in {"host": host, "c1": c1, "c2": c2}.items()}
    if any(m["waiting_after_change"].values()):
        fails.append(f"after the level change someone waits on the key press: {m['waiting_after_change']}")
    # (still frozen everywhere, the host's editor open)
    m["paused_after_change"] = {name: inst.bridge.status()["paused"] for name, inst in {"host": host, "c1": c1, "c2": c2}.items()}
    m["host_editor_after_change"] = _build(host)["editor_open"]
    if not all(m["paused_after_change"].values()) or not m["host_editor_after_change"]:
        fails.append(f"after the level change: paused {m['paused_after_change']}, host editor {m['host_editor_after_change']} (want time still frozen, the editor open)")
    _build(host, freeze=False)
    time.sleep(1.0)
    m["reload"] = {}
    for name, inst in {"host": host, "c1": c1, "c2": c2}.items():
        st = _build(inst)
        m["reload"][name] = {"level": inst.bridge.status()["level"], "blocks": len(_at(inst, P)), "received": st["level_files_received"], "sent": st["level_files_sent"]}
    if any(v["blocks"] != 1 for v in m["reload"].values()):
        fails.append(f"after the level change: {m['reload']} (want the block everywhere)")
    if m["reload"]["c1"]["received"] != 1 or m["reload"]["c2"]["received"] != 2:
        fails.append(f"the level change's file, once to each: {m['reload']} (want c1 1, c2 2 received)")

    # F1 is the host's own editor: closed, the session goes on and the game runs for everyone;
    # open again, with the cursor (realtime: nobody is paused either way)
    def clocks():
        return {name: inst.bridge.status()["game_time"] for name, inst in {"host": host, "c1": c1}.items()}
    _build(host, f1=True)
    time.sleep(2.0)
    t0 = clocks()
    time.sleep(2.0)
    t1 = clocks()
    hb = _build(host)
    m["f1_close"] = {"session": hb["build_mode"], "editor_open": hb["editor_open"],
                     "ran": {k: round(t1[k] - t0[k], 2) for k in t0}}
    if not hb["build_mode"] or hb["editor_open"] or min(m["f1_close"]["ran"].values()) < 1.0:
        fails.append(f"F1 didn't close the host's editor with the game running: {m['f1_close']}")
    _build(host, f1=True)
    time.sleep(2.0)
    hb, hst = _build(host), host.bridge.status()
    m["f1_open"] = {"session": hb["build_mode"], "editor_open": hb["editor_open"], "paused": hst["paused"], "cursor": hst["cursor_visible"]}
    if not (hb["build_mode"] and hb["editor_open"] and not hst["paused"] and hst["cursor_visible"]):
        fails.append(f"F1 again didn't open the editor: {m['f1_open']}")

    return {"passed": not fails, "metrics": m, "notes": fails}


def mp_build_session(ctx):
    """A realtime build session as players meet it (0.41.19): editing never pauses the game; every
    builder opens their own editor (F1, a real key event, one switch a press); a guest joins
    through the real loading screen (no key-press skip, no forced start) and plays until it opens
    its editor; what moves by itself (a guard spawned in the air, falling, its AI) isn't sent as
    an edit, not even while a builder holds it, until that builder moves it; the host freezes time
    for everyone and lets it run again; a guest without building rights is told why F1 does
    nothing; the host restarts the level; a level saved under a non-ASCII folder reaches a joiner."""
    m, fails = {}, []
    host = ctx.instance("host")
    _build(host, build_on_load=True)
    host.bridge.call("host", level="Data/Levels/nothing.xml")
    wait_loaded(host)
    time.sleep(2.0)

    def state(inst):
        b, s = _build(inst), inst.bridge.status()
        return {"session": b["build_mode"], "editor": b["editor_open"], "frozen": b.get("time_frozen"), "may": b["may_edit"],
                "banner": b.get("banner", ""), "waiting": s["waiting_for_input"], "paused": s["paused"], "sent": b.get("batches_sent")}

    def clock_runs(*insts):
        t0 = [i.bridge.status()["game_time"] for i in insts]
        time.sleep(1.5)
        return [round(i.bridge.status()["game_time"] - t, 2) for i, t in zip(insts, t0)]

    def connect(name):
        inst = ctx.instance(name, keys={"skip_loading_pause": "false"})  # (the real loading screen)
        inst.bridge.call("connect", ip="127.0.0.1")
        wait_loaded(inst, timeout=180)
        inst.bridge.wait(lambda: not inst.bridge.status()["waiting_for_input"], 60, what=f"{name} past its loading screen")
        time.sleep(4.0)
        return inst

    def editor_switches(inst):
        return sum(1 for l in inst.log().splitlines() if "Requesting editor-controlled mode" in l or "Requesting player-controlled mode" in l)

    m["host_start"] = state(host)
    if not (m["host_start"]["session"] and m["host_start"]["editor"] and not m["host_start"]["paused"] and "BUILD SESSION" in m["host_start"]["banner"]):
        fails.append(f"Build a level didn't start a running build session in the host's editor: {m['host_start']}")

    # A guest joins: playing, the game running, told how to build
    c1 = connect("client1")
    m["c1_join"] = state(c1)
    m["c1_welcome"] = [l for l in _build(c1).get("chat", []) if "build session" in l]
    m["ran_after_join"] = clock_runs(host, c1)
    if m["c1_join"]["editor"] or not m["c1_join"]["may"] or m["c1_join"]["paused"] or "F1: open the editor" not in m["c1_join"]["banner"]:
        fails.append(f"a guest who joined: {m['c1_join']} (want playing, may build, the banner's F1)")
    if not m["c1_welcome"]:
        fails.append("no welcome line for the guest")
    if min(m["ran_after_join"]) < 1.0:
        fails.append(f"the game doesn't run with the host in the editor: {m['ran_after_join']}")

    # The guest's F1, a real key: one switch, its editor open, the game still running for both
    before = editor_switches(c1)
    c1.bridge.call("key", key="F1")
    time.sleep(2.0)
    m["c1_f1"] = {"switches": editor_switches(c1) - before, "editor": _build(c1)["editor_open"], "ran": clock_runs(host, c1)}
    if m["c1_f1"]["switches"] != 1 or not m["c1_f1"]["editor"] or min(m["c1_f1"]["ran"]) < 1.0:
        fails.append(f"the guest's F1: {m['c1_f1']} (want one switch, its editor, the game running)")

    # A guard spawned in the air (an edit) falls and runs its AI: nothing of that is sent
    G = [0.0, 6.0, 4.0]
    made = _edit(host, op="spawn", path="Data/Objects/IGF_Characters/IGF_GuardActor.xml", pos=G)["made"]
    time.sleep(3.0)
    guard_host = made[0]["id"] if made else -1
    sent0 = {"host": _build(host)["batches_sent"], "c1": _build(c1)["batches_sent"]}
    guard_c1 = [o for o in _edit(c1, op="find", path="Data/Objects/IGF_Characters/IGF_GuardActor.xml")["objects"]]
    time.sleep(5.0)
    sent1 = {"host": _build(host)["batches_sent"], "c1": _build(c1)["batches_sent"]}
    m["guard"] = {"on_c1": len(guard_c1), "sent_while_it_moves": {k: sent1[k] - sent0[k] for k in sent0}}
    if len(guard_c1) != 1 or any(sent1[k] != sent0[k] for k in sent0):
        fails.append(f"the guard: {m['guard']} (want it on the guest, nothing sent as it falls and walks)")
    # ...nor while the guest holds it selected; the guest's move of it is one edit
    if guard_c1:
        gid = guard_c1[0]["id"]
        _edit(c1, op="select", objs=[gid])
        time.sleep(3.5)
        held = _build(c1)["batches_sent"] - sent1["c1"]
        target = [5.0, 2.0, -5.0]
        _edit(c1, op="move", obj=gid, pos=target)
        time.sleep(2.5)
        moved = _build(c1)["batches_sent"] - sent1["c1"] - held
        _edit(c1, op="select", objs=[])
        on_host = _edit(host, op="get", obj=guard_host).get("object", {}).get("pos", [0, 0, 0])
        m["guard_held"] = {"sent_while_held": held, "sent_for_the_move": moved, "host_pos": [round(v, 1) for v in on_host]}
        if held != 0 or moved != 1 or abs(on_host[0] - target[0]) > 1.5 or abs(on_host[2] - target[2]) > 1.5:
            fails.append(f"the held guard: {m['guard_held']} (want nothing while held, one edit for the move, there on the host)")

    # A block the guest places while the game runs: on the host at once
    P = [2.0, 1.0, 3.0]
    _edit(c1, op="spawn", path=BLOCK, pos=P)
    time.sleep(2.5)
    m["block"] = {"host": len(_at(host, P)), "c1": len(_at(c1, P))}
    if m["block"] != {"host": 1, "c1": 1}:
        fails.append(f"the guest's block: {m['block']}")

    # The host freezes time: everyone stops, editors work on; and lets it run again
    _build(host, freeze=True)
    time.sleep(2.0)
    m["frozen"] = {"ran": clock_runs(host, c1), "c1": state(c1)}
    _build(host, freeze=False)
    time.sleep(2.0)
    m["unfrozen_ran"] = clock_runs(host, c1)
    if max(m["frozen"]["ran"]) > 0.05 or not m["frozen"]["c1"]["frozen"] or "TIME FROZEN" not in m["frozen"]["c1"]["banner"]:
        fails.append(f"Freeze time: {m['frozen']}")
    if min(m["unfrozen_ran"]) < 1.0:
        fails.append(f"time didn't run again: {m['unfrozen_ran']}")

    # Without building rights the guest's editor closes, and its F1 says why
    _build(host, builder=1, may=False)
    time.sleep(3.0)
    c1.bridge.call("key", key="F1")
    time.sleep(2.0)
    m["no_rights"] = {"editor": _build(c1)["editor_open"], "line": [l for l in _build(c1).get("chat", []) if "decides who builds" in l]}
    if m["no_rights"]["editor"] or not m["no_rights"]["line"]:
        fails.append(f"a guest without building rights: {m['no_rights']}")
    _build(host, builder=1, may=True)

    # The host restarts the level: nothing sent for what the reset moves
    s0 = _build(host)["batches_sent"]
    _build(host, restart=True)
    time.sleep(4.0)
    m["restart_sent"] = _build(host)["batches_sent"] - s0
    if m["restart_sent"] != 0:
        fails.append(f"the restart was sent as {m['restart_sent']} edit(s)")

    # A level saved outside the game's folders under a non-ASCII one (as a user's Документы/s.xml):
    # a joiner gets it (its read took the path for the ANSI code page and sent nothing)
    import tempfile
    path = (tempfile.gettempdir() + "/ogmp_Документы/mp_build_session.xml").replace("\\", "/")  # (outside the working dir)
    m["save_to"] = _build(host, save_to=path).get("save_to_ok")
    time.sleep(2.0)
    c2 = connect("client2")
    s2 = _build(c2)
    m["c2"] = {"received": s2["level_files_received"], "level": c2.bridge.status()["level"], "blocks": len(_at(c2, P)), "may": s2["may_edit"]}
    if s2["level_files_received"] != 1 or m["c2"]["blocks"] != 1 or not s2["may_edit"]:
        fails.append(f"the joiner and a level saved under a non-ASCII folder: {m['c2']} (want the file, the block, may build)")

    return {"passed": not fails, "metrics": m, "notes": fails}

