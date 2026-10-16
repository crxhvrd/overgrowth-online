"""Phase 4 (combat), part 1: hit requests resolved by the defender's owner,
and the owner's game state (vitals) streamed to every proxy. Run from the
repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# --- vitals in the snapshot record ----------------------------------------------------

patch("Online/char_snapshot.h", [
("""struct Frame {
    int32_t id = -1;      // common object id (the host's)""", """// The owner's game state of a character, for everyone else's scripts (AI,
// HUD, the hit code) to read on the proxy, whose own script stands still
const uint8_t kVitalOnGround = 1;
const uint8_t kVitalStartled = 2;
const uint8_t kVitalBlocking = 4;

struct Vitals {
    bool valid = false;
    uint8_t knocked_out = 0;   // aschar.as: _awake, _unconscious, _dead
    uint8_t state = 0;         // aschar.as: movement, ground, attack, hit reaction, ragdoll
    uint8_t flags = 0;
    uint8_t ko_shield = 0;
    float temp_health = 1.0f;
    float permanent_health = 1.0f;
    float blood_health = 1.0f;
    float block_health = 1.0f;
    float duck_amount = 0.0f;
    int32_t target_id = -1;    // common ids
    uint8_t tethered = 0;
    int32_t tether_id = -1;
};

struct Frame {
    int32_t id = -1;      // common object id (the host's)"""),
("""    bool has_morphs = false;
    std::vector<Morph> morphs;  // only the ones that changed, or all of them now and then
};""", """    bool has_morphs = false;
    std::vector<Morph> morphs;  // only the ones that changed, or all of them now and then
    Vitals vitals;
};"""),
("""//   [uint8 morph count, per morph: uint8 index, int16 disp, int16 mod (x1000)]""",
 """//   [uint8 morph count, per morph: uint8 index, int16 disp, int16 mod (x1000)]
//   [vitals: uint8 knocked_out, state, flags, ko_shield; int16 temp, permanent, blood,
//    block health (x1000); uint8 duck (x255); int32 target; uint8 tethered; int32 tether]
//   flags byte: 1 = morphs, 2 = vitals"""),
])

patch("Online/char_snapshot.cpp", [
("""    bool morphs = f.has_morphs && morph_count > 0;
    Put<uint8_t>(out, morphs ? 1 : 0);""", """    bool morphs = f.has_morphs && morph_count > 0;
    Put<uint8_t>(out, (uint8_t)((morphs ? 1 : 0) | (f.vitals.valid ? 2 : 0)));"""),
("""            Put<int16_t>(out, Q16(f.morphs[m].mod_weight, kMorphScale));
        }
    }
}""", """            Put<int16_t>(out, Q16(f.morphs[m].mod_weight, kMorphScale));
        }
    }

    if (f.vitals.valid) {
        const Vitals& v = f.vitals;
        Put<uint8_t>(out, v.knocked_out);
        Put<uint8_t>(out, v.state);
        Put<uint8_t>(out, v.flags);
        Put<uint8_t>(out, v.ko_shield);
        Put<int16_t>(out, Q16(v.temp_health, kMorphScale));
        Put<int16_t>(out, Q16(v.permanent_health, kMorphScale));
        Put<int16_t>(out, Q16(v.blood_health, kMorphScale));
        Put<int16_t>(out, Q16(v.block_health, kMorphScale));
        Put<uint8_t>(out, (uint8_t)std::max(0.0f, std::min(255.0f, std::round(v.duck_amount * 255.0f))));
        Put<int32_t>(out, v.target_id);
        Put<uint8_t>(out, v.tethered);
        Put<int32_t>(out, v.tether_id);
    }
}"""),
("""            f.morphs[m].disp_weight = disp / kMorphScale;
            f.morphs[m].mod_weight = mod / kMorphScale;
        }
    }

    return true;""", """            f.morphs[m].disp_weight = disp / kMorphScale;
            f.morphs[m].mod_weight = mod / kMorphScale;
        }
    }

    f.vitals = Vitals();

    if (flags & 2) {
        Vitals& v = f.vitals;
        int16_t temp, perm, blood, block;
        uint8_t duck;
        if (!Get(data, size, offset, v.knocked_out) || !Get(data, size, offset, v.state) ||
            !Get(data, size, offset, v.flags) || !Get(data, size, offset, v.ko_shield) ||
            !Get(data, size, offset, temp) || !Get(data, size, offset, perm) ||
            !Get(data, size, offset, blood) || !Get(data, size, offset, block) ||
            !Get(data, size, offset, duck) || !Get(data, size, offset, v.target_id) ||
            !Get(data, size, offset, v.tethered) || !Get(data, size, offset, v.tether_id)) {
            return false;
        }
        v.valid = true;
        v.temp_health = temp / kMorphScale;
        v.permanent_health = perm / kMorphScale;
        v.blood_health = blood / kMorphScale;
        v.block_health = block / kMorphScale;
        v.duck_amount = duck / 255.0f;
    }

    return true;"""),
("""        out.has_morphs = false;
        out.morphs.clear();
""", """        out.has_morphs = false;
        out.morphs.clear();
        out.vitals = a.vitals;  // the game state of the frame being shown
"""),
])

# --- the character: read the owner's vitals, write them on the proxy -------------------

patch("Objects/movementobject.h", [
("""    void CaptureNetworkPose(CharSnapshot::Frame& out, bool with_morphs);
""", """    void CaptureNetworkPose(CharSnapshot::Frame& out, bool with_morphs);
    void CaptureNetworkVitals(CharSnapshot::Vitals& v);
    void ApplyNetworkVitals(const CharSnapshot::Vitals& v);
    bool NetworkVitalsSupported();
    int net_vitals_support = -1;  // -1 unchecked, 0 no (not a character script), 1 yes
"""),
])

patch("Objects/movementobject.cpp", [
("""    const std::vector<MorphTarget>& morphs = rigged_object_->morph_targets;
    out.has_morphs = with_morphs && !morphs.empty();""", """    CaptureNetworkVitals(out.vitals);

    const std::vector<MorphTarget>& morphs = rigged_object_->morph_targets;
    out.has_morphs = with_morphs && !morphs.empty();"""),
("""// Overhaul Online: the pose we stream for a character we simulate""", """// Overhaul Online: the script variables that carry a character's game state
bool MovementObject::NetworkVitalsSupported() {
    if (net_vitals_support == -1) {
        static const char* kVars[] = {"knocked_out", "state", "on_ground", "startled", "active_blocking", "ko_shield",
                                      "temp_health", "permanent_health", "blood_health", "block_health",
                                      "duck_amount", "target_id", "tethered", "tether_id"};
        net_vitals_support = 1;
        for (const char* v : kVars) {
            if (!ASHasVar(v)) {
                net_vitals_support = 0;
                break;
            }
        }
    }
    return net_vitals_support == 1;
}

void MovementObject::CaptureNetworkVitals(CharSnapshot::Vitals& v) {
    v.valid = NetworkVitalsSupported();
    if (!v.valid) {
        return;
    }

    Online* online = Online::Instance();
    v.knocked_out = (uint8_t)ASGetIntVar("knocked_out");
    v.state = (uint8_t)ASGetIntVar("state");
    v.flags = (ASGetBoolVar("on_ground") ? CharSnapshot::kVitalOnGround : 0) |
              (ASGetBoolVar("startled") ? CharSnapshot::kVitalStartled : 0) |
              (ASGetBoolVar("active_blocking") ? CharSnapshot::kVitalBlocking : 0);
    v.ko_shield = (uint8_t)std::max(0, std::min(255, ASGetIntVar("ko_shield")));
    v.temp_health = ASGetFloatVar("temp_health");
    v.permanent_health = ASGetFloatVar("permanent_health");
    v.blood_health = ASGetFloatVar("blood_health");
    v.block_health = ASGetFloatVar("block_health");
    v.duck_amount = ASGetFloatVar("duck_amount");
    int target = ASGetIntVar("target_id");
    v.target_id = target == -1 ? -1 : online->GetOriginalID(target);
    v.tethered = (uint8_t)ASGetIntVar("tethered");
    int tether = ASGetIntVar("tether_id");
    v.tether_id = tether == -1 ? -1 : online->GetOriginalID(tether);
}

// On a proxy: its own script stands still (aschar.as Update), so these are
// what everyone here reads. A knockout raises the same level messages the
// owner's script raised there (the HUD and level scripts listen for them).
void MovementObject::ApplyNetworkVitals(const CharSnapshot::Vitals& v) {
    if (!v.valid || !NetworkVitalsSupported()) {
        return;
    }

    Online* online = Online::Instance();
    int old_ko = ASGetIntVar("knocked_out");
    ASSetIntVar("knocked_out", v.knocked_out);
    ASSetIntVar("state", v.state);
    ASSetBoolVar("on_ground", (v.flags & CharSnapshot::kVitalOnGround) != 0);
    ASSetBoolVar("startled", (v.flags & CharSnapshot::kVitalStartled) != 0);
    ASSetBoolVar("active_blocking", (v.flags & CharSnapshot::kVitalBlocking) != 0);
    ASSetIntVar("ko_shield", v.ko_shield);
    ASSetFloatVar("temp_health", v.temp_health);
    ASSetFloatVar("permanent_health", v.permanent_health);
    ASSetFloatVar("blood_health", v.blood_health);
    ASSetFloatVar("block_health", v.block_health);
    ASSetFloatVar("duck_amount", v.duck_amount);
    ASSetIntVar("target_id", v.target_id == -1 ? -1 : online->GetObjectID(v.target_id));
    ASSetIntVar("tethered", v.tethered);
    ASSetIntVar("tether_id", v.tether_id == -1 ? -1 : online->GetObjectID(v.tether_id));

    const int kAwake = 0, kUnconscious = 1, kDead = 2;  // aschar.as
    if (old_ko == kAwake && v.knocked_out != kAwake && scenegraph_ != nullptr && scenegraph_->level != nullptr) {
        bool died = v.knocked_out == kDead || (v.knocked_out == kUnconscious && v.blood_health <= 0.0f);
        scenegraph_->level->Message((died ? "character_died " : "character_knocked_out ") + std::to_string(GetID()));
    }
}

// Overhaul Online: the pose we stream for a character we simulate"""),
("""    if (network_proxy) {
        CharSnapshot::Frame pose;
        if (net_buffer.Sample(OnlineClockMs(), pose)) {
            ApplyNetworkPose(pose);
        }
    }""", """    if (network_proxy) {
        CharSnapshot::Frame pose;
        if (net_buffer.Sample(OnlineClockMs(), pose)) {
            ApplyNetworkPose(pose);
            ApplyNetworkVitals(pose.vitals);
        }
    }"""),
("""    as_context->RegisterObjectProperty("MovementObject", "bool remote", asOFFSET(MovementObject, remote));""",
 """    as_context->RegisterObjectProperty("MovementObject", "bool remote", asOFFSET(MovementObject, remote));
    as_context->RegisterObjectProperty("MovementObject", "bool network_proxy", asOFFSET(MovementObject, network_proxy));  // Overhaul Online"""),
])

# --- online: route hit requests to the defender's owner, answers to the attacker's -----

patch("Online/online.h", [
("""#include <Online/Message/pong.h>
""", """#include <Online/Message/pong.h>
#include <Online/Message/combat_messages.h>
"""),
("""    Ping,
    Pong>""", """    Ping,
    Pong,
    HitRequest,
    HitResult>"""),
("""    std::atomic<uint64_t> snapshot_bones_captured{0};  // bones in captured records""",
 """    std::atomic<uint64_t> hits_requested{0};   // attacks on characters other machines run
    std::atomic<uint64_t> hits_resolved{0};    // attacks on ours, decided here
    std::atomic<uint64_t> hit_results{0};      // answers about our attacks
    std::atomic<uint64_t> snapshot_bones_captured{0};  // bones in captured records"""),
("""    void ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size);
""", """    void ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size);

    // Combat across machines (Phase 4)
    void SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked);
    void SendHitRequest(ObjectID target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                        ObjectID attacker, float damage_mult, float knockback_mult, float max_range);
    void ReceiveHitRequest(const OnlineMessageRef& ref, OnlineMessages::HitRequest* m, PeerID from);
    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);
"""),
])

patch("Online/online.cpp", [
("""vector<ObjectID> Online::GetLocalAvatarIDs() const {""", r"""// Overhaul Online, Phase 4: a message about a character goes to the machine
// that runs it. A client sends to the host; the host forwards what it doesn't
// run to that player's connection. (Inside message handling the peers lock
// is already held.)
void Online::SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked) {
    if (!IsActive() || online_session == nullptr) {
        return;
    }

    if (IsClient()) {
        Send(ref);
        return;
    }

    PlayerID owner = GetAvatarOwner(id);
    if (owner == online_session->local_player_id) {
        return;  // ours: the caller handles it here
    }

    NetConnectionID conn = 0;
    bool found = false;
    {
        std::unique_lock<mutex> guard(online_session->connected_peers_lock, std::defer_lock);
        if (!peers_locked) {
            guard.lock();
        }
        for (const Peer& peer : online_session->connected_peers) {
            if (peer.peer_id == owner) {
                conn = peer.conn_id;
                found = true;
            }
        }
    }

    if (found) {
        SendTo(conn, ref);
    }
}

void Online::SendHitRequest(ObjectID target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                            ObjectID attacker, float damage_mult, float knockback_mult, float max_range) {
    if (!IsActive() || IsLocalAvatar(target)) {
        return;
    }

    OnlineMessageRef ref = message_handler.Create<OnlineMessages::HitRequest>(
        GetOriginalID(attacker), GetOriginalID(target), type, attack_path, dir, pos, damage_mult, knockback_mult, max_range);
    SendToOwnerOf(target, ref, false);
    ++hits_requested;
}

namespace {
bool KnownHitType(const string& type) {
    return type == "blockprepare" || type == "attackimpact" || type == "attackblocked" || type == "grabbed";
}

MovementObject* CharacterByID(ObjectID id) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    return (o != nullptr && o->GetType() == _movement_object) ? static_cast<MovementObject*>(o) : nullptr;
}

const float kHitRangeSlack = 0.75f;  // both machines see the other a little late
}  // namespace

// We run the defender: decide the hit on its own state, as its player saw it
void Online::ReceiveHitRequest(const OnlineMessageRef& ref, OnlineMessages::HitRequest* m, PeerID from) {
    if (!host_started_level || !KnownHitType(m->type)) {
        return;
    }

    ObjectID target_id = GetObjectID(m->target);
    ObjectID attacker_id = GetObjectID(m->attacker);
    MovementObject* target = CharacterByID(target_id);
    MovementObject* attacker = CharacterByID(attacker_id);

    if (target == nullptr || attacker == nullptr) {
        return;
    }

    if (!IsLocalAvatar(target_id)) {
        if (IsHosting()) {
            SendToOwnerOf(target_id, ref, true);  // a player's character: its machine decides
        }
        return;
    }

    if (IsHosting() && GetAvatarOwner(attacker_id) != from) {
        return;  // only the attacker's owner asks about its attacks
    }

    int result = 0;  // _miss
    bool reaches = m->max_range <= 0.0f || distance(attacker->position, target->position) <= m->max_range + kHitRangeSlack;

    if (reaches || m->type == "attackblocked") {
        result = target->ASWasHit(m->type, m->attack_path, m->dir, m->pos, attacker_id, m->damage_mult, m->knockback_mult);
    }

    ++hits_resolved;
    SendToOwnerOf(attacker_id, message_handler.Create<OnlineMessages::HitResult>(m->attacker, m->target, m->type, result), true);
}

// We run the attacker: act on the defender's answer (a block or dodge staggers us)
void Online::ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from) {
    if (!host_started_level || !KnownHitType(m->type)) {
        return;
    }

    ObjectID attacker_id = GetObjectID(m->attacker);
    ObjectID target_id = GetObjectID(m->target);
    MovementObject* attacker = CharacterByID(attacker_id);

    if (attacker == nullptr) {
        return;
    }

    if (!IsLocalAvatar(attacker_id)) {
        if (IsHosting()) {
            SendToOwnerOf(attacker_id, ref, true);
        }
        return;
    }

    ++hit_results;

    if (attacker->HasFunction("void OnHitResult(int target, const string &in type, int result)")) {
        attacker->Execute("OnHitResult(" + to_string(target_id) + ", \"" + m->type + "\", " + to_string(m->result) + ");");
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {"""),
])

# --- script binding -----------------------------------------------------------------------

patch("Scripting/angelscript/asfuncs.cpp", [
("""void AS_Online_SendKillMessage(uint32_t victim_id, uint32_t killer_id) {""", """// Overhaul Online: an attack on a character another machine runs (aschar.as NetWasHit)
void AS_Online_SendHitRequest(int target_id, const std::string& type, const std::string& attack_path, vec3 dir, vec3 pos,
                              int attacker_id, float damage_mult, float knockback_mult, float max_range) {
    Online::Instance()->SendHitRequest(target_id, type, attack_path, dir, pos, attacker_id, damage_mult, knockback_mult, max_range);
}

void AS_Online_SendKillMessage(uint32_t victim_id, uint32_t killer_id) {"""),
("""    context->RegisterGlobalFunction("void Online_SendKillMessage(uint victim, uint killer)", asFUNCTION(AS_Online_SendKillMessage), asCALL_CDECL);""",
 """    context->RegisterGlobalFunction("void Online_SendKillMessage(uint victim, uint killer)", asFUNCTION(AS_Online_SendKillMessage), asCALL_CDECL);
    context->RegisterGlobalFunction("void Online_SendHitRequest(int target_id, const string &in type, const string &in attack_path, vec3 dir, vec3 pos, int attacker_id, float damage_mult, float knockback_mult, float max_range)", asFUNCTION(AS_Online_SendHitRequest), asCALL_CDECL);"""),
])

# --- test bridge ----------------------------------------------------------------------------

patch("Testing/test_bridge.cpp", [
("""        o["bones_captured"] = (Json::UInt64)online->snapshot_bones_captured.load();""",
 """        o["bones_captured"] = (Json::UInt64)online->snapshot_bones_captured.load();
        o["hits_requested"] = (Json::UInt64)online->hits_requested.load();
        o["hits_resolved"] = (Json::UInt64)online->hits_resolved.load();
        o["hit_results"] = (Json::UInt64)online->hit_results.load();"""),
])
print("ok")
