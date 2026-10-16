"""Online arena, part 2 (engine): the arenas online are player against player.

- Level::Initialize: online, the arena scripts (multi_arena.as, arena_level.as)
  give way to online_arena.as: no AI, the host runs matches between players.
  Their _paths.xml (weapons, crowd sound, font) stay the level's.
- Online_PlaceAvatar(id, pos, facing) for the host's level script: a player's
  character up at full health at `pos`, facing `facing`, on the machine that
  runs it (the co-op revive: "ohg revive <cid> x y z [fx fz]").
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Game/level.cpp", [
("""    if (!level_specific_script_.empty()) {
        Path level_specific_script_path = FindScript(level_specific_script_);""",
 """    // Overhaul Online: online the arenas are player against player, no AI
    std::string level_script = level_specific_script_;
    if (Online::Instance()->IsActive() && (level_script == "multi_arena.as" || level_script == "arena_level.as")) {
        LOGI << "Overhaul Online: " << level_script << " -> online_arena.as" << std::endl;
        level_script = "online_arena.as";
    }

    if (!level_script.empty()) {
        Path level_specific_script_path = FindScript(level_script);"""),
("""            LOGE << "Could not find file specified in <Script> in level: " << level_specific_script_ << std::endl;""",
 """            LOGE << "Could not find file specified in <Script> in level: " << level_script << std::endl;"""),
])

patch("Online/online.h", [
("""    void CoopReviveRequest(const string& msg);      // "ohg revive <common id> x y z" from the host
    void ReviveAvatar(MovementObject* mo, const vec3& pos);""",
 """    void CoopReviveRequest(const string& msg);      // "ohg revive <common id> x y z [fx fz]" from the host
    void ReviveAvatar(MovementObject* mo, const vec3& pos, const vec3& facing = vec3(0.0f));
    void PlaceAvatar(ObjectID id, const vec3& pos, const vec3& facing);  // host: up at pos, wherever it runs"""),
("""    vector<std::pair<ObjectID, vec3> > pending_revives;""",
 """    struct PendingRevive {
        ObjectID id;
        vec3 pos;
        vec3 facing;
    };
    vector<PendingRevive> pending_revives;"""),
])

patch("Online/online.cpp", [
("""    for (const auto& r : pending_revives) {
        Object* o = sg != nullptr ? sg->GetObjectFromID(r.first) : nullptr;
        if (o != nullptr && o->GetType() == _movement_object && IsLocalAvatar(r.first)) {
            ReviveAvatar(static_cast<MovementObject*>(o), r.second);
        }
    }""", """    for (const auto& r : pending_revives) {
        Object* o = sg != nullptr ? sg->GetObjectFromID(r.id) : nullptr;
        if (o != nullptr && o->GetType() == _movement_object && IsLocalAvatar(r.id)) {
            ReviveAvatar(static_cast<MovementObject*>(o), r.pos, r.facing);
        }
    }"""),
("""    int cid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (sscanf(msg.c_str(), "ohg revive %d %f %f %f", &cid, &x, &y, &z) == 4) {
        pending_revives.push_back(std::make_pair(GetObjectID(cid), vec3(x, y, z)));
    }
}""", """    int cid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, fx = 0.0f, fz = 0.0f;
    int n = sscanf(msg.c_str(), "ohg revive %d %f %f %f %f %f", &cid, &x, &y, &z, &fx, &fz);
    if (n == 4 || n == 6) {
        pending_revives.push_back({GetObjectID(cid), vec3(x, y, z), vec3(fx, 0.0f, fz)});
    }
}

// Host: a player's character up at `pos` (facing `facing`, if any) on the
// machine that runs it (online arenas: round starts)
void Online::PlaceAvatar(ObjectID id, const vec3& pos, const vec3& facing) {
    if (!IsHosting()) {
        return;
    }
    if (IsLocalAvatar(id)) {
        pending_revives.push_back({id, pos, facing});  // (CoopUpdate, outside the calling script)
    } else {
        Send<OnlineMessages::SendLevelMessage>("ohg revive " + to_string((int)GetOriginalID(id)) + " " + to_string(pos.x()) + " " +
                                               to_string(pos.y()) + " " + to_string(pos.z()) + " " + to_string(facing.x()) + " " +
                                               to_string(facing.z()));
    }
}"""),
("""void Online::ReviveAvatar(MovementObject* mo, const vec3& pos) {""",
 """void Online::ReviveAvatar(MovementObject* mo, const vec3& pos, const vec3& facing) {"""),
("""    mo->Execute("this_mo.position = vec3(" + to_string(pos.x()) + ", " + to_string(pos.y()) + ", " + to_string(pos.z()) +
                "); this_mo.velocity = vec3(0.0f);");
}""", """    mo->Execute("this_mo.position = vec3(" + to_string(pos.x()) + ", " + to_string(pos.y()) + ", " + to_string(pos.z()) +
                "); this_mo.velocity = vec3(0.0f);");
    if (length_squared(facing) > 0.01f) {  // turned that way, the camera behind
        vec3 f = normalize(facing);
        mo->Execute("this_mo.SetRotationFromFacing(vec3(" + to_string(f.x()) + ", 0.0f, " + to_string(f.z()) +
                    ")); SetCameraFromFacing(); SetOnGround(true); FixDiscontinuity();");
    }
}"""),
])

patch("Scripting/angelscript/asfuncs.cpp", [
("""void AS_Online_Host() {
    Online::Instance()->StartHostingMultiplayer();
}
""", """void AS_Online_Host() {
    Online::Instance()->StartHostingMultiplayer();
}

// Overhaul Online (host): a player's character up at `pos`, facing `facing`, on its machine
void AS_Online_PlaceAvatar(int char_id, const vec3& pos, const vec3& facing) {
    Online::Instance()->PlaceAvatar(char_id, pos, facing);
}
"""),
("""    context->RegisterGlobalFunction("vec3 Online_SafeSpotNear(const vec3 &in base, const vec3 &in facing, int index)", asFUNCTION(AS_Online_SafeSpotNear), asCALL_CDECL);""",
 """    context->RegisterGlobalFunction("vec3 Online_SafeSpotNear(const vec3 &in base, const vec3 &in facing, int index)", asFUNCTION(AS_Online_SafeSpotNear), asCALL_CDECL);
    context->RegisterGlobalFunction("void Online_PlaceAvatar(int char_id, const vec3 &in pos, const vec3 &in facing)", asFUNCTION(AS_Online_PlaceAvatar), asCALL_CDECL);"""),
])
print("ok")
