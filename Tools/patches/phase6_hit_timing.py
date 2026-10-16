"""Phase 6, part 10: a hit lands when its attacker is seen landing it.

Friend test: "AI characters lag behind their actions, so the guest can't
even see how he died". The defender decided a hit the moment the request
came in, but showed the attacker (a proxy) about one trip plus the playback
buffer in the past: the damage came before the swing.
- HitRequest carries the attacker's time (OnlineClockMs at the hit).
- The defender holds it until its picture of the attacker reaches that time
  (CharSnapshot::Buffer::RenderSrcMs), at most 250 ms, then decides it the
  same way as before. A held hit also leaves the defender time to block what
  it sees coming.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/Message/combat_messages.h", [
("""    float max_range = 0.0f;   // the attacker's reach, for the defender's own range check

    HitRequest();""", """    float max_range = 0.0f;   // the attacker's reach, for the defender's own range check
    uint32_t src_ms = 0;      // the attacker's time (OnlineClockMs): held until the defender shows it

    HitRequest();"""),
])

patch("Online/Message/combat_messages.cpp", [
("""    binn_object_set_float(l, "r", m->max_range);
    return l;""", """    binn_object_set_float(l, "r", m->max_range);
    binn_object_set_uint32(l, "s", m->src_ms);
    return l;"""),
("""    binn_object_get_float(l, "r", &m->max_range);
}""", """    binn_object_get_float(l, "r", &m->max_range);
    binn_object_get_uint32(l, "s", &m->src_ms);
}"""),
])

patch("Online/online.h", [
("""    void SendAvatarCharacter(ObjectID character, const string& path);""",
 """    // Hits held until the defender shows its attacker landing them
    struct HeldHit {
        OnlineMessageRef ref;
        PeerID from;
        double resolve_at_ms;
    };
    vector<HeldHit> held_hits;
    void ResolveHitRequest(OnlineMessages::HitRequest* m, bool peers_locked);
    void ResolveHeldHits();
    void SendAvatarCharacter(ObjectID character, const string& path);"""),
])

patch("Online/online.cpp", [
# The attacker stamps it
("""    OnlineMessageRef ref = message_handler.Create<OnlineMessages::HitRequest>(
        GetOriginalID(attacker), GetOriginalID(target), type, attack_path, dir, pos, damage_mult, knockback_mult, max_range);""",
 """    OnlineMessageRef ref = message_handler.Create<OnlineMessages::HitRequest>(
        GetOriginalID(attacker), GetOriginalID(target), type, attack_path, dir, pos, damage_mult, knockback_mult, max_range);
    static_cast<OnlineMessages::HitRequest*>(ref.GetData())->src_ms = (uint32_t)OnlineClockMs();"""),
# The defender holds it until it shows the attacker there
("""    if (IsHosting() && GetAvatarOwner(attacker_id) != from) {
        return;  // only the attacker's owner asks about its attacks
    }

    int result = 0;  // _miss""", """    if (IsHosting() && GetAvatarOwner(attacker_id) != from) {
        return;  // only the attacker's owner asks about its attacks
    }

    // Shown in the past here (a proxy): hold it until we see the attacker land it
    const double kMaxHoldMs = 250.0;
    if (attacker->network_proxy && m->src_ms != 0) {
        double wait = (double)m->src_ms - attacker->net_buffer.RenderSrcMs();
        if (wait > 1.0) {
            held_hits.push_back({ref, from, OnlineClockMs() + std::min(wait, kMaxHoldMs)});
            return;
        }
    }

    ResolveHitRequest(m, true);
}

// Held hits whose attacker we now show landing them (main thread, LateUpdate)
void Online::ResolveHeldHits() {
    double now = OnlineClockMs();
    size_t done = 0;
    while (done < held_hits.size() && held_hits[done].resolve_at_ms <= now) {
        ResolveHitRequest(static_cast<OnlineMessages::HitRequest*>(held_hits[done].ref.GetData()), false);
        ++done;
    }
    held_hits.erase(held_hits.begin(), held_hits.begin() + done);
}

void Online::ResolveHitRequest(OnlineMessages::HitRequest* m, bool peers_locked) {
    ObjectID target_id = GetObjectID(m->target);
    ObjectID attacker_id = GetObjectID(m->attacker);
    MovementObject* target = CharacterByID(target_id);
    MovementObject* attacker = CharacterByID(attacker_id);
    if (target == nullptr || attacker == nullptr || !IsLocalAvatar(target_id)) {
        return;  // gone, or not ours any more
    }

    int result = 0;  // _miss"""),
("""    ++hits_resolved;
    SendToOwnerOf(attacker_id, message_handler.Create<OnlineMessages::HitResult>(m->attacker, m->target, m->type, result), true);
}""", """    ++hits_resolved;
    SendToOwnerOf(attacker_id, message_handler.Create<OnlineMessages::HitResult>(m->attacker, m->target, m->type, result), peers_locked);
}"""),
# LateUpdate
("""        CoopUpdate(scenegraph);
""", """        CoopUpdate(scenegraph);
        ResolveHeldHits();
"""),
])
print("ok")
# (follow-up, applied by hand: held_hits.clear() in ChangeLevel and in StopMultiplayer)
