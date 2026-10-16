"""Phase 4 (combat), part 2: temporary ownership. A throw, rear choke or body
drag runs on the grabber's machine: it takes the grabbed character for the
interaction (both animations stay in step there), everyone else sees it as a
stream, and it is handed back once free and settled; the owner then brings
the body in line (OnNetworkResume, e.g. ragdoll). Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# --- message ------------------------------------------------------------------------------

patch("Online/Message/combat_messages.h", [
("""}  // namespace OnlineMessages""", """// A character handed to another machine for a throw, choke or drag (active),
// or handed back (not active). Clients send theirs to the host, which keeps
// the table and tells everyone.
class OwnerOverride : public OnlineMessageBase {
   public:
    CommonObjectID character = -1;
    int32_t owner = 0;  // PlayerID
    bool active = false;
    float max_seconds = 0.0f;

    OwnerOverride();
    OwnerOverride(CommonObjectID character, int32_t owner, bool active, float max_seconds);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
}  // namespace OnlineMessages"""),
])

patch("Online/Message/combat_messages.cpp", [
("""}  // namespace OnlineMessages""", """OwnerOverride::OwnerOverride() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

OwnerOverride::OwnerOverride(CommonObjectID character, int32_t owner, bool active, float max_seconds)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
      character(character),
      owner(owner),
      active(active),
      max_seconds(max_seconds) {
}

binn* OwnerOverride::Serialize(void* object) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "c", m->character);
    binn_object_set_int32(l, "o", m->owner);
    binn_object_set_bool(l, "a", m->active);
    binn_object_set_float(l, "s", m->max_seconds);
    return l;
}

void OwnerOverride::Deserialize(void* object, binn* l) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    BOOL active = FALSE;
    binn_object_get_int32(l, "c", &m->character);
    binn_object_get_int32(l, "o", &m->owner);
    binn_object_get_bool(l, "a", &active);
    binn_object_get_float(l, "s", &m->max_seconds);
    m->active = active != FALSE;
}

void OwnerOverride::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    Online::Instance()->ReceiveOwnerOverride(m->character, (PlayerID)m->owner, m->active, m->max_seconds, peer);
}

void* OwnerOverride::Construct(void* mem) {
    return new (mem) OwnerOverride();
}

void OwnerOverride::Destroy(void* object) {
    static_cast<OwnerOverride*>(object)->~OwnerOverride();
}
}  // namespace OnlineMessages"""),
])

# --- online: the override table --------------------------------------------------------

patch("Online/online.h", [
("""    HitRequest,
    HitResult>""", """    HitRequest,
    HitResult,
    OwnerOverride>"""),
("""    std::atomic<uint64_t> hit_results{0};      // answers about our attacks""",
 """    std::atomic<uint64_t> hit_results{0};      // answers about our attacks
    std::atomic<uint64_t> takeovers{0};        // characters we took for a throw, choke or drag

    // Temporary ownership: characters handed to another machine for a throw,
    // choke or drag (main thread only)
    struct OwnerOverrideEntry {
        PlayerID owner = 0;
        double since_ms = 0.0;
        double max_ms = 0.0;
    };
    unordered_map<CommonObjectID, OwnerOverrideEntry> owner_overrides;"""),
("""    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);
""", """    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);
    bool TakeOver(ObjectID id, float max_seconds);
    void ReleaseTakeOver(ObjectID id);
    void ReceiveOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID from);
    void UpdateTakeOvers(SceneGraph* scenegraph);
    void BroadcastOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID except, bool peers_locked);
"""),
])

patch("Online/online.cpp", [
# Ownership consults the overrides first
("""    // Is this avatar controlled by a playerstate? If yes, is it us?
    for (const auto& it : online_session->player_states) {""", """    // Handed to a machine for a throw, choke or drag (Overhaul Online)
    auto ov = owner_overrides.find(GetOriginalID(avatar_id));
    if (ov != owner_overrides.end()) {
        return ov->second.owner == online_session->local_player_id;
    }

    // Is this avatar controlled by a playerstate? If yes, is it us?
    for (const auto& it : online_session->player_states) {"""),
("""PlayerID Online::GetAvatarOwner(const ObjectID avatar_id) const {
    if (online_session != nullptr) {""", """PlayerID Online::GetAvatarOwner(const ObjectID avatar_id) const {
    auto ov = owner_overrides.find(GetOriginalID(avatar_id));
    if (ov != owner_overrides.end()) {
        return ov->second.owner;
    }

    if (online_session != nullptr) {"""),
# A level change or the end of the session drops them
("""    if (!host_started_level || online_session == nullptr) {
        stream_latest.clear();
        stream_sent.clear();
        stream_owned.clear();
        return;
    }""", """    if (!host_started_level || online_session == nullptr) {
        stream_latest.clear();
        stream_sent.clear();
        stream_owned.clear();
        owner_overrides.clear();
        return;
    }"""),
("""        if (IsHosting()) {
            HandleConnectionsToBeClosed();  // on the main thread: it deletes characters""", """        UpdateTakeOvers(scenegraph);

        if (IsHosting()) {
            HandleConnectionsToBeClosed();  // on the main thread: it deletes characters"""),
# A player who leaves gives back what they held
("""            // Remove playerstate
            online_session->player_states.erase(player_id);""", """            // Hand back what they held for a throw, choke or drag
            for (auto ov = owner_overrides.begin(); ov != owner_overrides.end();) {
                if (ov->second.owner == player_id) {
                    BroadcastOwnerOverride(ov->first, 0, false, 0.0f, player_id, true);
                    ov = owner_overrides.erase(ov);
                } else {
                    ++ov;
                }
            }

            // Remove playerstate
            online_session->player_states.erase(player_id);"""),
("""vector<ObjectID> Online::GetLocalAvatarIDs() const {""", r"""// ---- temporary ownership (throws, chokes, drags) ----------------------------------

void Online::BroadcastOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID except, bool peers_locked) {
    OnlineMessageRef ref = message_handler.Create<OnlineMessages::OwnerOverride>(cid, (int32_t)owner, active, max_seconds);

    if (IsClient()) {
        Send(ref);
        return;
    }

    vector<NetConnectionID> conns;
    {
        std::unique_lock<mutex> guard(online_session->connected_peers_lock, std::defer_lock);
        if (!peers_locked) {
            guard.lock();
        }
        for (const Peer& peer : online_session->connected_peers) {
            if (peer.peer_id != except) {
                conns.push_back(peer.conn_id);
            }
        }
    }

    for (NetConnectionID conn : conns) {
        SendTo(conn, ref);
    }
}

// Take a character another machine runs, for a throw, choke or drag that
// starts now on ours. True if we run it now (or always did).
bool Online::TakeOver(ObjectID id, float max_seconds) {
    if (!IsActive() || online_session == nullptr) {
        return true;
    }

    if (IsLocalAvatar(id)) {
        return true;
    }

    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    if (o == nullptr || o->GetType() != _movement_object) {
        return false;
    }

    CommonObjectID cid = GetOriginalID(id);
    OwnerOverrideEntry& e = owner_overrides[cid];
    e.owner = online_session->local_player_id;
    e.since_ms = OnlineClockMs();
    e.max_ms = max_seconds * 1000.0;
    static_cast<MovementObject*>(o)->SetNetworkProxy(false);  // now, not next update: the caller acts on it at once
    BroadcastOwnerOverride(cid, e.owner, true, max_seconds, online_session->local_player_id, false);
    ++takeovers;
    LOGI << "Took over character " << id << " for up to " << max_seconds << " s" << std::endl;
    return true;
}

void Online::ReleaseTakeOver(ObjectID id) {
    CommonObjectID cid = GetOriginalID(id);
    auto it = owner_overrides.find(cid);

    if (it == owner_overrides.end() || online_session == nullptr || it->second.owner != online_session->local_player_id) {
        return;
    }

    owner_overrides.erase(it);
    BroadcastOwnerOverride(cid, online_session->local_player_id, false, 0.0f, online_session->local_player_id, false);
    LOGI << "Handed back character " << id << std::endl;
}

void Online::ReceiveOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID from) {
    if (!host_started_level || online_session == nullptr) {
        return;
    }

    auto it = owner_overrides.find(cid);

    if (IsHosting()) {
        // The host keeps the table: the first taker holds it until it lets go
        if (active && it != owner_overrides.end() && it->second.owner != from) {
            BroadcastOwnerOverride(cid, it->second.owner, true, (float)(it->second.max_ms / 1000.0), 0, true);  // tells the latecomer
            return;
        }
        if (!active && (it == owner_overrides.end() || it->second.owner != from)) {
            return;
        }
        owner = from;
    }

    if (active) {
        OwnerOverrideEntry& e = owner_overrides[cid];
        e.owner = owner;
        e.since_ms = OnlineClockMs();
        e.max_ms = max_seconds * 1000.0;
    } else if (it != owner_overrides.end()) {
        owner_overrides.erase(it);
    }

    if (IsHosting()) {
        BroadcastOwnerOverride(cid, owner, active, max_seconds, from, true);
    }
}

// Ours to hand back: when the grabbed one is free and settled, or the time is up
void Online::UpdateTakeOvers(SceneGraph* sg) {
    if (online_session == nullptr || sg == nullptr) {
        return;
    }

    const int kMovementState = 0, kRagdollState = 4, kAwake = 0, kTetheredFree = 0;  // aschar.as
    double now = OnlineClockMs();
    vector<ObjectID> release;

    for (auto& it : owner_overrides) {
        if (it.second.owner != online_session->local_player_id) {
            continue;
        }

        ObjectID id = GetObjectID(it.first);
        Object* o = sg->GetObjectFromID(id);
        double held = now - it.second.since_ms;

        if (o == nullptr || o->GetType() != _movement_object || held > it.second.max_ms) {
            release.push_back(id);
            continue;
        }

        MovementObject* mo = static_cast<MovementObject*>(o);
        if (held < 600.0 || !mo->NetworkVitalsSupported() || mo->ASGetIntVar("tethered") != kTetheredFree) {
            continue;
        }

        int state = mo->ASGetIntVar("state");
        bool settled = state == kMovementState || mo->ASGetIntVar("knocked_out") != kAwake ||
                       (state == kRagdollState && length(mo->velocity) < 1.5f);

        if (settled) {
            release.push_back(id);
        }
    }

    for (ObjectID id : release) {
        ReleaseTakeOver(id);
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {"""),
])

# --- the character: switching between owned and proxy ----------------------------------

patch("Objects/movementobject.h", [
("""    bool NetworkVitalsSupported();
""", """    bool NetworkVitalsSupported();
    void SetNetworkProxy(bool proxy);  // owned here or shown from the owner's stream
"""),
])

patch("Objects/movementobject.cpp", [
("""    bool proxy = online->IsActive() && !online->IsLocalAvatar(GetID());
    if (proxy != network_proxy) {
        network_proxy = proxy;
        rigged_object_->network_proxy = proxy;
        net_buffer.Clear();
    }""", """    SetNetworkProxy(online->IsActive() && !online->IsLocalAvatar(GetID()));"""),
("""// Overhaul Online: the script variables that carry a character's game state""", """// Overhaul Online: from now on run here (false) or show the owner's stream
// (true). Running it again, its script brings the body in line with the game
// state that was streamed into it (OnNetworkResume: a ragdoll stays a ragdoll).
void MovementObject::SetNetworkProxy(bool proxy) {
    if (proxy == network_proxy) {
        return;
    }

    network_proxy = proxy;
    rigged_object_->network_proxy = proxy;
    net_buffer.Clear();

    if (!proxy && HasFunction("void OnNetworkResume()")) {
        Execute("OnNetworkResume();");
    }
}

// Overhaul Online: the script variables that carry a character's game state"""),
])

# --- script binding ------------------------------------------------------------------------

patch("Scripting/angelscript/asfuncs.cpp", [
("""void AS_Online_SendKillMessage(uint32_t victim_id, uint32_t killer_id) {""", """// Overhaul Online: a throw, choke or drag takes the grabbed character for its length
bool AS_Online_TakeOver(int char_id, float max_seconds) {
    return Online::Instance()->TakeOver(char_id, max_seconds);
}

void AS_Online_ReleaseTakeOver(int char_id) {
    Online::Instance()->ReleaseTakeOver(char_id);
}

void AS_Online_SendKillMessage(uint32_t victim_id, uint32_t killer_id) {"""),
("""    context->RegisterGlobalFunction("void Online_SendKillMessage(uint victim, uint killer)", asFUNCTION(AS_Online_SendKillMessage), asCALL_CDECL);""",
 """    context->RegisterGlobalFunction("void Online_SendKillMessage(uint victim, uint killer)", asFUNCTION(AS_Online_SendKillMessage), asCALL_CDECL);
    context->RegisterGlobalFunction("bool Online_TakeOver(int char_id, float max_seconds)", asFUNCTION(AS_Online_TakeOver), asCALL_CDECL);
    context->RegisterGlobalFunction("void Online_ReleaseTakeOver(int char_id)", asFUNCTION(AS_Online_ReleaseTakeOver), asCALL_CDECL);"""),
])

patch("Testing/test_bridge.cpp", [
("""        o["hit_results"] = (Json::UInt64)online->hit_results.load();""",
 """        o["hit_results"] = (Json::UInt64)online->hit_results.load();
        o["takeovers"] = (Json::UInt64)online->takeovers.load();
        o["overrides"] = (int)online->owner_overrides.size();"""),
])

# --- script --------------------------------------------------------------------------------

P = "Data/Scripts/aschar.as"
s = open(P, encoding="utf-8").read()


def rep(old, new):
    global s
    assert s.count(old) == 1, (old[:90], s.count(old))
    s = s.replace(old, new)


rep("""    if(!char.network_proxy || this_mo.network_proxy) {
        return char.WasHit(type, attack_path, dir, pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);
    }
""", """    if(!char.network_proxy || this_mo.network_proxy) {
        return char.WasHit(type, attack_path, dir, pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);
    }

    if(type == "grabbed") {
        // A throw plays out here, both bodies in step: take the victim for it
        if(Online_TakeOver(target, 5.0f)) {
            return char.WasHit(type, attack_path, dir, pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);
        }

        return _miss;
    }
""")
rep("""    return _miss;  // blockprepare: no block until it says so; grabbed: throws across machines come later
}""", """    return _miss;  // blockprepare: no block until it says so
}

// Overhaul Online: this character runs here again (another machine held it for
// a throw, choke or drag, or it just became ours). The game state streamed into
// it is current, the body is not: a ragdoll has to be one again
void OnNetworkResume() {
    if(state == _ragdoll_state || knocked_out != _awake) {
        state = _movement_state;  // Ragdoll() only acts on a change of state
        Ragdoll(knocked_out != _awake ? _RGDL_LIMP : _RGDL_FALL);
    }
}""")
rep("""        if((char.GetBoolVar("asleep") || dot(char.GetFacing(), this_mo.position - char.position) < 0.0) && (char.GetIntVar("species") != _dog && char.GetIntVar("species") != _wolf)) {
            if(this_mo.controlled) {
                AchievementEvent("player_sneak_attacked");
            }""", """        bool can_choke = (char.GetBoolVar("asleep") || dot(char.GetFacing(), this_mo.position - char.position) < 0.0) && (char.GetIntVar("species") != _dog && char.GetIntVar("species") != _wolf);

        // Overhaul Online: the choke runs here, take the victim for it
        if(can_choke && char.network_proxy && !Online_TakeOver(sneak_throw_id, 12.0f)) {
            can_choke = false;
        }

        if(can_choke) {
            if(this_mo.controlled) {
                AchievementEvent("player_sneak_attacked");
            }""")
rep("""void StartBodyDrag(string part, int part_id, int char_id) {
    MovementObject@ char = ReadCharacterID(char_id);""", """void StartBodyDrag(string part, int part_id, int char_id) {
    MovementObject@ char = ReadCharacterID(char_id);

    if(char.network_proxy && !Online_TakeOver(char_id, 60.0f)) {
        return;  // Overhaul Online: the drag runs here, the body has to be ours
    }
""")
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")
