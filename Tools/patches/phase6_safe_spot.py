"""Phase 6, part 7: safe spots for the other players (co-op).

A joining player was placed sideways off the host: in the first story level
that is the water, and the rabbit drowned at once. Now a spot near a base
point is tried beside, behind, then in front, and taken only on solid ground
at about the same height with nothing in between (a sea floor or a drop
fails); otherwise the base point itself (players overlap for a moment
rather than fall). Used for joining players (Online::CreateCharacter) and by
the level scripts when a level restarts (Online_SafeSpotNear).
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/online.h", [
("""    void SendAvatarCharacter(ObjectID character, const string& path);""",
 """    // A spot for player number `index` near `base` (facing `facing`): solid ground
    // at about the same height, or `base` itself (co-op spawns)
    vec3 SafeSpotNear(const vec3& base, const vec3& facing, int index);
    void SendAvatarCharacter(ObjectID character, const string& path);"""),
])

patch("Online/online.cpp", [
# Joining: a safe spot beside the host's character
("""        // Overhaul Online: beside the host's character, alternating sides, not inside it
        int n = (int)online_session->player_states.size();
        vec3 facing = mymo->GetFacing();
        vec3 right = normalize(vec3(-facing.z(), 0.0f, facing.x()) + vec3(0.0001f, 0.0f, 0.0f));
        position = mymo->position + right * (1.3f * (1 + n / 2) * (n % 2 == 0 ? 1.0f : -1.0f)) - facing * 0.6f;""",
 """        // Overhaul Online: near the host's character on solid ground (not in the water)
        position = SafeSpotNear(mymo->position, mymo->GetFacing(), (int)online_session->player_states.size());"""),
("""// ---- Your online character -----------------------------------------------------""",
 """vec3 Online::SafeSpotNear(const vec3& base, const vec3& facing, int index) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || sg->bullet_world_ == nullptr || index <= 0) {
        return base;
    }
    BulletWorld* world = sg->bullet_world_;
    const vec3 up(0.0f, 1.0f, 0.0f);

    // The ground under the base point
    vec3 hit;
    if (world->CheckRayCollision(base + up * 0.5f, base - up * 3.0f, &hit) == nullptr) {
        return base;
    }
    float above = base.y() - hit.y();  // how high a character's position sits over its ground
    float ground = hit.y();

    vec3 f = facing;
    f.y() = 0.0f;
    f = length_squared(f) > 1e-6f ? normalize(f) : vec3(0.0f, 0.0f, 1.0f);
    vec3 right(-f.z(), 0.0f, f.x());

    // Beside, behind, in front; further out for the later ones
    const float kSide[] = {1.2f, -1.2f, 0.0f, 1.2f, -1.2f, 0.0f, 2.4f, -2.4f};
    const float kBack[] = {0.0f, 0.0f, -1.2f, -1.2f, -1.2f, 1.2f, 0.0f, 0.0f};
    const int n = 8;
    for (int t = 0; t < n; ++t) {
        int k = (index - 1 + t) % n;
        vec3 spot = base + right * kSide[k] + f * kBack[k];

        // Nothing in between (a wall, a rock)
        if (world->CheckRayCollision(base + up * 0.3f, spot + up * 0.3f) != nullptr) {
            continue;
        }
        // Solid ground at about the same height (no drop, no sea floor)
        vec3 g;
        if (world->CheckRayCollision(spot + up * 1.0f, spot - up * 2.5f, &g) == nullptr) {
            continue;
        }
        if (std::fabs(g.y() - ground) > 0.6f) {
            continue;
        }
        return vec3(spot.x(), g.y() + above, spot.z());
    }
    return base;
}

// ---- Your online character -----------------------------------------------------"""),
])

patch("Scripting/angelscript/asfuncs.cpp", [
("""void AS_Online_Host() {
    Online::Instance()->StartHostingMultiplayer();
}""", """void AS_Online_Host() {
    Online::Instance()->StartHostingMultiplayer();
}

// Overhaul Online: a co-op spawn spot for player `index` near `base` (see Online::SafeSpotNear)
vec3 AS_Online_SafeSpotNear(const vec3& base, const vec3& facing, int index) {
    return Online::Instance()->SafeSpotNear(base, facing, index);
}"""),
("""    context->RegisterGlobalFunction("void Online_Host()", asFUNCTION(AS_Online_Host), asCALL_CDECL);""",
 """    context->RegisterGlobalFunction("void Online_Host()", asFUNCTION(AS_Online_Host), asCALL_CDECL);
    context->RegisterGlobalFunction("vec3 Online_SafeSpotNear(const vec3 &in base, const vec3 &in facing, int index)", asFUNCTION(AS_Online_SafeSpotNear), asCALL_CDECL);"""),
])
print("ok")
