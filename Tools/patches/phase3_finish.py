"""Phase 3 finishing touches: capture bones fresh from the physics bodies at
send time, coarser morph change threshold, characters behind a receiver's
camera at 5 Hz. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# The bodies as they are now, not as the last animation update stored them
# (StoreNetworkBones runs every few steps): bones and root agree in time
patch("Objects/movementobject.cpp", [
("""    const std::vector<NetworkBone>& bones = rigged_object_->network_bones;
    const std::vector<PhysicsBone>& physics = rigged_object_->skeleton_.physics_bones;
    out.bones.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        out.bones[i].rotation = bones[i].rotation1;
        out.bones[i].translation = bones[i].translation1;
        out.bones[i].scale = bones[i].scale;
        out.bones[i].present = i < physics.size() && physics[i].bullet_object != nullptr;
    }""", """    const std::vector<PhysicsBone>& physics = rigged_object_->skeleton_.physics_bones;
    out.bones.resize(physics.size());
    for (size_t i = 0; i < physics.size(); ++i) {
        const BulletObject* b_o = physics[i].bullet_object;
        CharSnapshot::Bone& bone = out.bones[i];
        bone.present = b_o != nullptr;
        bone.scale = physics[i].display_scale;
        if (b_o != nullptr) {
            bone.rotation = QuaternionFromMat4(b_o->transform.GetRotationPart());
            bone.translation = b_o->transform.GetTranslationPart();
        }
    }"""),
])

patch("Online/online.cpp", [
("""int MorphStep(float v) {
    return (int)std::round(v * 200.0f);  // smaller changes wait for the next refresh
}""", """int MorphStep(float v) {
    return (int)std::round(v * 50.0f);  // changes under 2 % wait for the next refresh
}"""),
("""    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
    };""", """    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
        bool has_cam;
        vec3 cam_pos;
        vec3 cam_dir;
    };"""),
("""            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f)});""",
 """            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f), false, vec3(0.0f), vec3(0.0f)});"""),
("""            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
            }
        }""", """            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
            }

            // Their view (the camera they send us, 5 Hz)
            Camera* cam = ps->second.camera_id >= 0 ? ActiveCameras::GetCamera(ps->second.camera_id) : nullptr;
            if (cam != nullptr) {
                rc.has_cam = true;
                rc.cam_pos = cam->GetPos();
                rc.cam_dir = cam->GetFacing();
            }
        }"""),
("""            double interval = rc.has_pos ? StreamIntervalMs(distance(rc.pos, r.position)) : kStreamCaptureMs;""",
 """            double interval = rc.has_pos ? StreamIntervalMs(distance(rc.pos, r.position)) : kStreamCaptureMs;
            if (rc.has_cam) {
                vec3 to = r.position - rc.cam_pos;
                if (length(to) > 8.0f && dot(to, rc.cam_dir) < 0.0f) {
                    interval = std::max(interval, 1000.0 / 5.0);  // behind them
                }
            }"""),
])
print("ok")
