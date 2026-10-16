"""Phase 5, part 3: the HUD from replicated state. Who hit a character last
(aschar.as attacked_by_id) travels in the vitals, so damage numbers and the
kill feed come out the same on every machine. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""    int32_t block_stunned_by = -1;
    uint8_t primary_slot = 1;""", """    int32_t block_stunned_by = -1;
    int32_t attacked_by = -1;      // who hit it last (the HUD's damage numbers and kill feed)
    uint8_t primary_slot = 1;"""),
("""//    uint8 block_stunned (x100 s); int32 block_stunned_by;
""", """//    uint8 block_stunned (x100 s); int32 block_stunned_by; int32 attacked_by;
"""),
])

patch("Online/char_snapshot.cpp", [
("""        Put<int32_t>(out, v.block_stunned_by);
        Put<uint8_t>(out, v.primary_slot);""", """        Put<int32_t>(out, v.block_stunned_by);
        Put<int32_t>(out, v.attacked_by);
        Put<uint8_t>(out, v.primary_slot);"""),
("""        if (!Get(data, size, offset, stun) || !Get(data, size, offset, v.block_stunned_by)) {""",
 """        if (!Get(data, size, offset, stun) || !Get(data, size, offset, v.block_stunned_by) ||
            !Get(data, size, offset, v.attacked_by)) {"""),
])

patch("Objects/movementobject.cpp", [
("""                                      "block_stunned_by_id", "weapon_slots", "primary_weapon_slot"};""",
 """                                      "block_stunned_by_id", "weapon_slots", "primary_weapon_slot", "attacked_by_id"};"""),
("""    v.block_stunned_by = stunner == -1 ? -1 : online->GetOriginalID(stunner);
    v.primary_slot""", """    v.block_stunned_by = stunner == -1 ? -1 : online->GetOriginalID(stunner);
    int attacker = ASGetIntVar("attacked_by_id");
    v.attacked_by = attacker == -1 ? -1 : online->GetOriginalID(attacker);
    v.primary_slot"""),
("""    ASSetIntVar("block_stunned_by_id", v.block_stunned_by == -1 ? -1 : online->GetObjectID(v.block_stunned_by));
""", """    ASSetIntVar("block_stunned_by_id", v.block_stunned_by == -1 ? -1 : online->GetObjectID(v.block_stunned_by));
    ASSetIntVar("attacked_by_id", v.attacked_by == -1 ? -1 : online->GetObjectID(v.attacked_by));
"""),
])
print("ok")
