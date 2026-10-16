"""Phase 3: ownership and state streaming.

Each character is simulated by its owner (a player's machine for that
player's character, the host for NPCs and free avatars); everyone else shows
it from the owner's stream (Online/char_snapshot.h) through a jitter buffer.
Replaces Wolfire's MovementObjectUpdate (all bones, string keys, ~120 Hz, host
only). Run from the repo root."""
import os

S = "Source/"
MARK = "//    Modified By: Overhaul Online (crxhvrd)\n"


def edit(path, pairs):
    s = open(S + path, encoding="utf-8").read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90], s.count(old))
        s = s.replace(old, new)
    open(S + path, "w", encoding="utf-8", newline="").write(s)


def span(path, start, end, new, keep_end=True):
    """Replace from start up to end (end kept unless keep_end is False)."""
    s = open(S + path, encoding="utf-8").read()
    assert s.count(start) == 1, (path, start[:90], s.count(start))
    a = s.index(start)
    b = s.index(end, a)
    if not keep_end:
        b += len(end)
    s = s[:a] + new + s[b:]
    open(S + path, "w", encoding="utf-8", newline="").write(s)


def mark(path):
    s = open(S + path, encoding="utf-8").read()
    if "Modified By: Overhaul Online" in s:
        return
    lines = s.split("\n")
    for i, line in enumerate(lines[:6]):
        if line.startswith("//      Developer:"):
            lines.insert(i + 1, MARK.rstrip("\n"))
            break
    open(S + path, "w", encoding="utf-8", newline="").write("\n".join(lines))


for f in ["Objects/movementobject.h", "Objects/movementobject.cpp", "Objects/riggedobject.h", "Objects/riggedobject.cpp",
          "GUI/dimgui/dimgui.cpp", "Online/online_connection_states.cpp"]:
    mark(f)

# --- MovementObject -------------------------------------------------------------------

edit("Objects/movementobject.h", [
    ("""#include <Online/online_datastructures.h>
""", """#include <Online/online_datastructures.h>
#include <Online/char_snapshot.h>
"""),
    ("""    list<OnlineMessageRef> incoming_material_sound_events;
    list<OnlineMessageRef> incoming_movement_object_frames;
    list<OnlineMessageRef> incoming_cut_lines;
    bool disable_network_bone_interpolation = false;
    TimeInterpolator network_time_interpolator;
    float last_walltime_diff = 0.0f;
""", """    list<OnlineMessageRef> incoming_material_sound_events;
    list<OnlineMessageRef> incoming_cut_lines;

    // Overhaul Online ownership: another machine simulates this character
    // (its player's, or the host for NPCs); we show the owner's stream
    bool network_proxy = false;
    CharSnapshot::Buffer net_buffer;
    void ApplyNetworkPose(const CharSnapshot::Frame& pose);
    void CaptureNetworkPose(CharSnapshot::Frame& out, bool with_morphs);
"""),
])

edit("Objects/movementobject.cpp", [
    ("""void MovementObject::Update(float timestep) {
    // GridTest(position, scenegraph_->bullet_world_, 0.45f);
    ThreadedSound* si = Engine::Instance()->GetSound();
    Online* online = Online::Instance();
""", """void MovementObject::Update(float timestep) {
    // GridTest(position, scenegraph_->bullet_world_, 0.45f);
    ThreadedSound* si = Engine::Instance()->GetSound();
    Online* online = Online::Instance();

    // Overhaul Online: who simulates this character? Its player's machine, or
    // the host (NPCs, free avatars). Everyone else shows the owner's stream.
    bool proxy = online->IsActive() && !online->IsLocalAvatar(GetID());
    if (proxy != network_proxy) {
        network_proxy = proxy;
        rigged_object_->network_proxy = proxy;
        net_buffer.Clear();
    }
"""),
    ("""        if (game_timer.game_time < rigged_object_->blood_surface.sleep_time) {
            rigged_object_->blood_surface.Update(Engine::Instance()->GetSceneGraph(), timestep);
        }

    } else {
        ActiveCameras::Set(0);
""", """    }

    // Overhaul Online: only the owner simulates a character; everyone else
    // shows its stream (above) and keeps its blood running
    if (network_proxy) {
        if (game_timer.game_time < rigged_object_->blood_surface.sleep_time) {
            rigged_object_->blood_surface.Update(Engine::Instance()->GetSceneGraph(), timestep);
        }
    } else {
        ActiveCameras::Set(0);
"""),
    ("""void MovementObject::ClientBeforeDraw() {
    Online* online = Online::Instance();
    if (online->IsClient()) {
        rigged_object_->ClientBeforeDraw();
    }
}
""", """void MovementObject::ClientBeforeDraw() {
    if (network_proxy) {
        rigged_object_->ClientBeforeDraw();
    }
}

// Overhaul Online: show a pose from the owner's stream. The physics bones and
// the collision sphere follow it too: scripts here (AI sight, hits) look at them.
void MovementObject::ApplyNetworkPose(const CharSnapshot::Frame& pose) {
    position = pose.position;
    velocity = pose.velocity;
    if (length_squared(pose.facing) > 1e-6f) {
        facing = normalize(pose.facing);
    }

    std::vector<PhysicsBone>& bones = rigged_object_->skeleton_.physics_bones;
    if (pose.bones.size() == bones.size()) {
        rigged_object_->network_display_bone_matrices.resize(bones.size());

        for (size_t i = 0; i < bones.size(); ++i) {
            const CharSnapshot::Bone& b = pose.bones[i];
            rigged_object_->network_display_bone_matrices[i] = rigged_object_->NetworkBoneMatrix((int)i, b.rotation, b.translation, b.scale);
            BulletObject* b_o = bones[i].bullet_object;

            if (b_o) {
                mat4 world = Mat4FromQuaternion(b.rotation);
                world.SetTranslationPart(b.translation + world.GetRotatedvec3(b_o->com_offset));
                b_o->SetTransform(world);
                b_o->UpdateTransform();
            }
        }

        rigged_object_->UpdateCollisionObjects();
    }

    char_sphere->SetPosition(position);
    char_sphere->body->setInterpolationWorldTransform(char_sphere->body->getWorldTransform());
    char_sphere->UpdateTransform();
}

// Overhaul Online: the pose we stream for a character we simulate
void MovementObject::CaptureNetworkPose(CharSnapshot::Frame& out, bool with_morphs) {
    out.id = Online::Instance()->GetOriginalID(GetID());
    out.position = position;
    out.velocity = velocity;
    out.facing = GetFacing();

    const std::vector<NetworkBone>& bones = rigged_object_->network_bones;
    out.bones.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        out.bones[i].rotation = bones[i].rotation1;
        out.bones[i].translation = bones[i].translation1;
        out.bones[i].scale = bones[i].scale;
    }

    const std::vector<MorphTarget>& morphs = rigged_object_->morph_targets;
    out.has_morphs = with_morphs && !morphs.empty();
    out.morphs.resize(out.has_morphs ? morphs.size() : 0);
    for (size_t i = 0; i < out.morphs.size(); ++i) {
        out.morphs[i].disp_weight = morphs[i].disp_weight;
        out.morphs[i].mod_weight = morphs[i].mod_weight;
    }
}
"""),
    ("""    if (Online::Instance()->IsClient())
        return;

    SetCharAnimation(path, fade_speed, flags);""", """    if (network_proxy)  // Overhaul Online: its owner animates it
        return;

    SetCharAnimation(path, fade_speed, flags);"""),
])

# The old interpolation of host frames: now the owner's stream for any proxy
span("Objects/movementobject.cpp", "    // Get data from host, if we are a client and won't generate it locally.\n",
     "        for (auto& it : angelscript_update) {",
     """    // Overhaul Online: show the owner's stream (another player's character,
    // or on clients the host's NPCs)
    if (network_proxy) {
        CharSnapshot::Frame pose;
        if (net_buffer.Sample(OnlineClockMs(), pose)) {
            ApplyNetworkPose(pose);
        }
    }

    if (online->IsClient()) {
""")

# --- RiggedObject ---------------------------------------------------------------------

edit("Objects/riggedobject.h", [
    ("""    static const mat4 InterpolateBetweenTwoBones(const NetworkBone &current, const NetworkBone &next, float interp_step);
""", """    static const mat4 InterpolateBetweenTwoBones(const NetworkBone &current, const NetworkBone &next, float interp_step);
    // Overhaul Online: display matrix of physics bone i from its streamed world transform
    mat4 NetworkBoneMatrix(int bone, const quaternion &rotation, const vec3 &translation, float scale) const;
"""),
    ("""    std::vector<mat4> network_display_bone_matrices;  // Final interpolated bone matrices for rendering, from server when client.
""", """    std::vector<mat4> network_display_bone_matrices;  // Final interpolated bone matrices for rendering, from server when client.
    bool network_proxy = false;                        // Overhaul Online: shown from another machine's stream (MovementObject sets it)
"""),
])

edit("Objects/riggedobject.cpp", [
    ("""void RiggedObject::PreDrawFrame(float curr_game_time) {
""", """mat4 RiggedObject::NetworkBoneMatrix(int bone, const quaternion& rotation, const vec3& translation, float scale) const {
    const PhysicsBone& p_bone = skeleton_.physics_bones[bone];
    NetworkBone nb;
    nb.translation0 = p_bone.initial_position * -1.0f / model_char_scale;
    nb.model_char_scale = model_char_scale;
    nb.rotation0 = QuaternionFromMat4(transpose(p_bone.initial_rotation));
    nb.rotation1 = rotation;
    nb.translation1 = translation;
    nb.scale = scale;
    return InterpolateBetweenTwoBones(nb, nb, 0.0f);
}

void RiggedObject::PreDrawFrame(float curr_game_time) {
"""),
    ("""    CalcRootBoneVelocity();
    if (!online->IsClient()) {""", """    CalcRootBoneVelocity();
    if (!network_proxy) {  // Overhaul Online: a proxy's bones come from the stream"""),
    ("""void RiggedObject::ClientBeforeDraw() {
    if (Online::Instance()->IsClient()) {""", """void RiggedObject::ClientBeforeDraw() {
    if (network_proxy) {"""),
    ("""            if (online->IsClient() || m_t.weight > 0.0f ||""", """            if (network_proxy || m_t.weight > 0.0f ||"""),
    ("""                if (online->IsClient() || m_t.weight > 0.0f) {""", """                if (network_proxy || m_t.weight > 0.0f) {"""),
    ("""                if (online->IsClient()) {
                    MorphTargetStateStorage state;
                    if (GetOnlineIncomingMorphTargetState(state, morph->name.c_str())) {""", """                if (network_proxy) {
                    MorphTargetStateStorage state;
                    if (GetOnlineIncomingMorphTargetState(state, morph->name.c_str())) {"""),
    ("""        if (online->IsClient()) {
            MorphTargetStateStorage state;
            if (rigged_object != nullptr) {""", """        if (rigged_object != nullptr && rigged_object->network_proxy) {
            MorphTargetStateStorage state;
            if (rigged_object != nullptr) {"""),
])

# --- Debug GUI (the old interpolator's graphs) ----------------------------------------

edit("GUI/dimgui/dimgui.cpp", [
    ("""                            pending_bones = movobj->network_time_interpolator.full_interpolation;

                            graphs[id_prefix + "_host_walltime_diff"].Push(movobj->last_walltime_diff);
                            graphs[id_prefix + "_host_walltime_offset_shift"].Push(movobj->network_time_interpolator.host_walltime_offset_shift_vel);""",
     """                            pending_bones = (float)movobj->net_buffer.Size();

                            graphs[id_prefix + "_host_walltime_diff"].Push((float)movobj->net_buffer.DelayMs());
                            graphs[id_prefix + "_host_walltime_offset_shift"].Push((float)movobj->net_buffer.IntervalMs());"""),
    ("""                            graphs[id_prefix + "_mov_interpolation_step"].Push(movobj->network_time_interpolator.interpolation_step);""",
     """                            graphs[id_prefix + "_mov_interpolation_step"].Push(movobj->net_buffer.Extrapolating() ? 1.0f : 0.0f);"""),
    ("""                            graphs[id_prefix + "_pending_bones_history"].Push(movobj->network_time_interpolator.full_interpolation);""",
     """                            graphs[id_prefix + "_pending_bones_history"].Push((float)movobj->net_buffer.Size());"""),
    ("""                            ImGui::Checkbox("Disable Linear Interpolation", &movobj->disable_network_bone_interpolation);
""", ""),
    ("""Draw("Host Walltime Diff");""", """Draw("Playback Delay (ms)");"""),
    ("""Draw("Host Walltime Offset Shift");""", """Draw("Snapshot Interval (ms)");"""),
])

# A new player has no character yet: -1, not object 1 (ownership looks it up)
edit("Online/online_connection_states.cpp", [
    ("""    PlayerState player_state;
    player_state.playername = actor->player_name;
""", """    PlayerState player_state;
    player_state.playername = actor->player_name;
    player_state.object_id = -1;  // no character yet (ownership looks avatars up by id)
"""),
])

# --- Online ---------------------------------------------------------------------------

edit("Online/online.h", [
    ("""#include <Online/Message/movement_object_update.h>
""", """#include <Online/Message/character_snapshots.h>
#include <Online/char_snapshot.h>
#include <Objects/riggedobject.h>
#include <Objects/movementobject.h>
"""),
    ("""    MovementObjectUpdate,
""", """    CharacterSnapshots,
"""),
    ("""    std::mutex close_queue_lock;
""", """    std::mutex close_queue_lock;

    // Character streaming (ownership): the newest packed record of every
    // character we send: the ones we simulate (captured at 30 Hz) and, on the
    // host, the players' characters it relays. Main thread only.
    struct StreamRecord {
        std::vector<uint8_t> bytes;
        uint32_t src_ms = 0;
        uint64_t hash = 0;  // content without id and time: same hash = nothing moved
        vec3 position;
        PlayerID owner = 0;
    };
    struct StreamSent {
        uint32_t src_ms = 0;
        uint64_t hash = 0;
        double sent_ms = -1e9;
    };
    struct StreamOwned {
        int captures = 0;
        uint64_t morph_hash = 0;
    };
    unordered_map<CommonObjectID, StreamRecord> stream_latest;
    map<std::pair<PeerID, CommonObjectID>, StreamSent> stream_sent;
    unordered_map<CommonObjectID, StreamOwned> stream_owned;
    double stream_next_capture_ms = 0.0;
    std::atomic<uint64_t> snapshot_records_out{0};
    std::atomic<uint64_t> snapshot_records_in{0};
    std::atomic<uint64_t> snapshot_bytes_out{0};

    // Test bridge: per tracked character, every step: our clock, the owner
    // time shown (proxies; ours = our clock), position, mean physics bone
    struct TrackSample {
        double clock_ms;
        double render_src_ms;
        vec3 position;
        vec3 bones;
    };
    unordered_map<ObjectID, vector<TrackSample>> track_samples;
"""),
    ("""    bool IsLocalAvatar(const ObjectID avatar) const;
""", """    bool IsLocalAvatar(const ObjectID avatar) const;
    PlayerID GetAvatarOwner(const ObjectID avatar) const;  // whose machine simulates it
    void StreamCharacters(SceneGraph* scenegraph);
    void ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size);
"""),
])

span("Online/online.cpp", """    if (IsHosting()) {
        for (Object* o : scenegraph_->movement_objects_) {""", """        if (is_tick) {
            for (Object* o : scenegraph_->objects_) {""", """    StreamCharacters(scenegraph_);

    // Test bridge tracking (only while a test asks for it)
    if (!track_samples.empty()) {
        double now = OnlineClockMs();

        for (auto& it : track_samples) {
            Object* o = scenegraph_->GetObjectFromID(it.first);
            if (o == nullptr || o->GetType() != _movement_object || it.second.size() >= 20000) {
                continue;
            }

            MovementObject* mo = static_cast<MovementObject*>(o);
            TrackSample s;
            s.clock_ms = now;
            s.render_src_ms = mo->network_proxy ? mo->net_buffer.RenderSrcMs() : now;
            s.position = mo->position;
            s.bones = vec3(0.0f);
            int n = 0;

            for (const PhysicsBone& bone : mo->rigged_object()->skeleton_.physics_bones) {
                if (bone.bullet_object != nullptr) {
                    s.bones += bone.bullet_object->transform.GetTranslationPart();
                    ++n;
                }
            }

            if (n > 0) {
                s.bones /= (float)n;
            }
            it.second.push_back(s);
        }
    }

    if (IsHosting()) {
""")

edit("Online/online.cpp", [
    ("""vector<ObjectID> Online::GetLocalAvatarIDs() const {""", r"""PlayerID Online::GetAvatarOwner(const ObjectID avatar_id) const {
    if (online_session != nullptr) {
        for (const auto& it : online_session->player_states) {
            if (it.second.object_id == avatar_id) {
                return it.first;
            }
        }
    }

    // Not a player's character: the host's (NPCs, free avatars)
    return IsHosting() && online_session != nullptr ? online_session->local_player_id : 0;
}

// Overhaul Online: ownership streaming. Every machine captures the characters
// it simulates (30 Hz) and sends them: a client to the host; the host to every
// client, along with the players' characters it relays. The rate per receiver
// falls with the distance to that receiver's character; a record that did not
// change (a still character, a settled ragdoll) goes out once a second.
namespace {
const double kStreamCaptureMs = 1000.0 / 30.0;
const double kStreamHeartbeatMs = 1000.0;
const double kStreamSlackMs = 8.0;  // captures land on frames, not exactly on the grid
const size_t kStreamBundleBytes = 1000;  // about one packet: a loss costs a few characters
const int kStreamMorphRefresh = 30;  // captures between full morph sets

double StreamIntervalMs(float dist) {
    if (dist < 20.0f) {
        return 1000.0 / 30.0;
    } else if (dist < 50.0f) {
        return 1000.0 / 15.0;
    }
    return 1000.0 / 5.0;
}

uint64_t MorphHash(const std::vector<MorphTarget>& morphs) {
    std::vector<uint8_t> q;
    for (const MorphTarget& m : morphs) {
        int16_t d = (int16_t)std::max(-32767.0f, std::min(32767.0f, std::round(m.disp_weight * 1000.0f)));
        int16_t w = (int16_t)std::max(-32767.0f, std::min(32767.0f, std::round(m.mod_weight * 1000.0f)));
        q.push_back((uint8_t)d);
        q.push_back((uint8_t)(d >> 8));
        q.push_back((uint8_t)w);
        q.push_back((uint8_t)(w >> 8));
    }
    return CharSnapshot::Hash(q.data(), q.size());
}
}  // namespace

void Online::StreamCharacters(SceneGraph* sg) {
    if (!host_started_level || online_session == nullptr) {
        stream_latest.clear();
        stream_sent.clear();
        stream_owned.clear();
        return;
    }

    double now = OnlineClockMs();
    PlayerID me = online_session->local_player_id;

    // 1. Capture the characters we simulate
    if (now >= stream_next_capture_ms) {
        stream_next_capture_ms = std::max(stream_next_capture_ms + kStreamCaptureMs, now - kStreamCaptureMs);

        for (Object* o : sg->movement_objects_) {
            MovementObject* mo = static_cast<MovementObject*>(o);
            if (mo->network_proxy) {
                continue;
            }

            CommonObjectID cid = GetOriginalID(mo->GetID());
            StreamOwned& owned = stream_owned[cid];
            uint64_t morph_hash = MorphHash(mo->rigged_object()->morph_targets);
            bool morphs = morph_hash != owned.morph_hash || owned.captures % kStreamMorphRefresh == 0;
            owned.morph_hash = morph_hash;
            ++owned.captures;

            CharSnapshot::Frame frame;
            mo->CaptureNetworkPose(frame, morphs);
            frame.src_ms = (uint32_t)now;

            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            CharSnapshot::Write(r.bytes, frame);
            r.src_ms = frame.src_ms;
            r.hash = CharSnapshot::Hash(r.bytes.data() + CharSnapshot::kContentOffset, r.bytes.size() - CharSnapshot::kContentOffset);
            r.position = mo->position;
            r.owner = me;
        }
    }

    // 2. Send what each receiver is due: a client sends to the host only
    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
    };
    vector<Receiver> receivers;
    {
        lock_guard<mutex> guard(online_session->connected_peers_lock);
        for (const Peer& peer : online_session->connected_peers) {
            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f)});
        }
    }

    for (Receiver& rc : receivers) {
        auto ps = online_session->player_states.find(rc.peer);
        if (IsHosting() && ps != online_session->player_states.end()) {
            Object* o = sg->GetObjectFromID(ps->second.object_id);
            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
            }
        }

        std::vector<uint8_t> bundle;
        int records = 0;

        for (auto& it : stream_latest) {
            const StreamRecord& r = it.second;
            if (r.owner == rc.peer || r.bytes.empty()) {
                continue;  // their own character
            }

            StreamSent& sent = stream_sent[std::make_pair(rc.peer, it.first)];
            if (r.src_ms == sent.src_ms) {
                continue;
            }

            double interval = rc.has_pos ? StreamIntervalMs(distance(rc.pos, r.position)) : kStreamCaptureMs;
            if (now - sent.sent_ms < interval - kStreamSlackMs) {
                continue;
            }
            if (r.hash == sent.hash && now - sent.sent_ms < kStreamHeartbeatMs) {
                continue;
            }

            if (!bundle.empty() && bundle.size() + r.bytes.size() > kStreamBundleBytes) {
                snapshot_bytes_out += bundle.size();
                snapshot_records_out += records;
                SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
                bundle.clear();
                records = 0;
            }

            bundle.insert(bundle.end(), r.bytes.begin(), r.bytes.end());
            ++records;
            sent.src_ms = r.src_ms;
            sent.hash = r.hash;
            sent.sent_ms = now;
        }

        if (!bundle.empty()) {
            snapshot_bytes_out += bundle.size();
            snapshot_records_out += records;
            SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
        }
    }
}

void Online::ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || !host_started_level) {
        return;
    }

    double now = OnlineClockMs();
    size_t offset = 0;
    CharSnapshot::Frame frame;

    while (offset < size) {
        size_t begin = offset;
        if (!CharSnapshot::Read(data, size, offset, frame)) {
            LOGW << "Malformed character snapshot from peer " << (int)from << std::endl;
            return;
        }

        ObjectID id = GetObjectID(frame.id);
        Object* o = sg->GetObjectFromID(id);
        if (o == nullptr || o->GetType() != _movement_object || IsLocalAvatar(id)) {
            continue;  // gone, or ours: we simulate it
        }
        if (IsHosting() && GetAvatarOwner(id) != from) {
            continue;  // only a character's owner streams it
        }

        MovementObject* mo = static_cast<MovementObject*>(o);
        mo->net_buffer.Push(frame, now);
        ++snapshot_records_in;

        if (frame.has_morphs) {
            RiggedObject* rig = mo->rigged_object();
            size_t n = std::min(frame.morphs.size(), rig->morph_targets.size());
            for (size_t i = 0; i < n; ++i) {
                MorphTargetStateStorage& m = rig->incoming_network_morphs[rig->morph_targets[i].name];
                m.name = rig->morph_targets[i].name;
                m.disp_weight = frame.morphs[i].disp_weight;
                m.mod_weight = frame.morphs[i].mod_weight;
                m.dirty = false;
            }
        }

        if (IsHosting()) {  // relay to the other players
            StreamRecord& r = stream_latest[frame.id];
            r.bytes.assign(data + begin, data + offset);
            r.src_ms = frame.src_ms;
            r.hash = CharSnapshot::Hash(r.bytes.data() + CharSnapshot::kContentOffset, r.bytes.size() - CharSnapshot::kContentOffset);
            r.position = frame.position;
            r.owner = from;
        }
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {"""),
])

# --- Test bridge: proxies, playback delay, tracking, stream counters ------------------

edit("Testing/test_bridge.cpp", [
    ("""                c["pos"] = Vec(mo->position);
                c["vel"] = Vec(mo->velocity);
""", """                c["pos"] = Vec(mo->position);
                c["vel"] = Vec(mo->velocity);
                c["proxy"] = mo->network_proxy;

                if (mo->network_proxy) {
                    c["net_delay_ms"] = mo->net_buffer.DelayMs();
                    c["net_interval_ms"] = mo->net_buffer.IntervalMs();
                    c["net_frames"] = mo->net_buffer.Size();
                }
"""),
    ("""        o["dropped"] = (Json::UInt64)online->net_sim_dropped.load();
""", """        o["dropped"] = (Json::UInt64)online->net_sim_dropped.load();
        o["records_out"] = (Json::UInt64)online->snapshot_records_out.load();
        o["records_in"] = (Json::UInt64)online->snapshot_records_in.load();
        o["snapshot_bytes_out"] = (Json::UInt64)online->snapshot_bytes_out.load();
"""),
    ("""    } else if (cmd == "quit") {""", """    } else if (cmd == "track") {
        // Record these characters every step (clears the previous recording)
        online->track_samples.clear();
        for (const Json::Value& id : req["ids"]) {
            online->track_samples[id.asInt()];
        }
    } else if (cmd == "track_get") {
        Json::Value& t = out["tracks"];
        t = Json::Value(Json::objectValue);

        for (const auto& it : online->track_samples) {
            Json::Value rows(Json::arrayValue);
            for (const Online::TrackSample& s : it.second) {
                Json::Value row(Json::arrayValue);
                row.append(s.clock_ms);
                row.append(s.render_src_ms);
                for (int i = 0; i < 3; ++i) {
                    row.append(s.position[i]);
                }
                for (int i = 0; i < 3; ++i) {
                    row.append(s.bones[i]);
                }
                rows.append(row);
            }
            t[std::to_string(it.first)] = rows;
        }

        if (req.get("stop", false).asBool()) {
            online->track_samples.clear();
        }
    } else if (cmd == "quit") {"""),
])

for f in ("Online/Message/movement_object_update.h", "Online/Message/movement_object_update.cpp"):
    if os.path.exists(S + f):
        os.remove(S + f)

print("ok")
