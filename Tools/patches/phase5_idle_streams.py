"""Phase 5, part 6 (bandwidth): a character that only breathes is not sent.
A capture within 1 cm and 1.5 degrees of the last one sent (every bone, the
root, facing) with the same vitals and no morph change keeps the old record;
the full morph refresh (every 30 captures, about 1 s) still goes out, so an
idle character costs about one record a second. The receiver already holds
a silent character still (CharSnapshot::Buffer, kHoldGapMs).
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""uint64_t Hash(const uint8_t* data, size_t size);""", """uint64_t Hash(const uint8_t* data, size_t size);

// Nothing a viewer would see changed: positions within pos_tol (m), rotations
// within rot_tol (radians), the same vitals
bool Similar(const Frame& a, const Frame& b, float pos_tol = 0.01f, float rot_tol = 0.026f);"""),
])

patch("Online/char_snapshot.cpp", [
("""uint64_t Hash(const uint8_t* data, size_t size) {""", """bool Similar(const Frame& a, const Frame& b, float pos_tol, float rot_tol) {
    if (a.bones.size() != b.bones.size() || distance_squared(a.position, b.position) > pos_tol * pos_tol ||
        distance_squared(a.facing, b.facing) > rot_tol * rot_tol) {
        return false;
    }

    const float min_dot = std::cos(rot_tol * 0.5f);
    for (size_t i = 0; i < a.bones.size(); ++i) {
        const Bone& x = a.bones[i];
        const Bone& y = b.bones[i];
        if (x.present != y.present || std::fabs(x.scale - y.scale) > 0.001f) {
            return false;
        }
        if (!x.present) {
            continue;
        }
        float d = x.rotation[0] * y.rotation[0] + x.rotation[1] * y.rotation[1] + x.rotation[2] * y.rotation[2] +
                  x.rotation[3] * y.rotation[3];
        if (std::fabs(d) < min_dot || distance_squared(x.translation, y.translation) > pos_tol * pos_tol) {
            return false;
        }
    }

    const Vitals& v = a.vitals;
    const Vitals& w = b.vitals;
    if (v.valid != w.valid) {
        return false;
    }
    if (v.valid && (v.knocked_out != w.knocked_out || v.state != w.state || v.flags != w.flags ||
                    v.ko_shield != w.ko_shield || v.target_id != w.target_id || v.tethered != w.tethered ||
                    v.tether_id != w.tether_id || v.block_stunned_by != w.block_stunned_by ||
                    v.attacked_by != w.attacked_by || v.primary_slot != w.primary_slot ||
                    std::fabs(v.temp_health - w.temp_health) > 0.0005f ||
                    std::fabs(v.permanent_health - w.permanent_health) > 0.0005f ||
                    std::fabs(v.blood_health - w.blood_health) > 0.0005f ||
                    std::fabs(v.block_health - w.block_health) > 0.0005f ||
                    std::fabs(v.duck_amount - w.duck_amount) > 0.004f ||
                    std::fabs(v.block_stunned - w.block_stunned) > 0.01f)) {
        return false;
    }
    if (v.valid) {
        for (int i = 0; i < 6; ++i) {
            if (v.slots[i] != w.slots[i]) {
                return false;
            }
        }
    }
    return true;
}

uint64_t Hash(const uint8_t* data, size_t size) {"""),
])

patch("Online/online.h", [
("""    struct StreamOwned {
        int captures = 0;
        vector<int> sent_morphs;  // the morph values last streamed (1/200 steps)
    };""", """    struct StreamOwned {
        int captures = 0;
        vector<int> sent_morphs;  // the morph values last streamed (1/200 steps)
        CharSnapshot::Frame last_sent;  // the pose last streamed (idle characters are not resent)
        bool has_last = false;
    };
    std::atomic<uint64_t> snapshot_idle_skips{0};"""),
])

patch("Online/online.cpp", [
("""            MovementObject* mo = static_cast<MovementObject*>(o);
            if (mo->network_proxy) {
                continue;
            }

            CommonObjectID cid = GetOriginalID(mo->GetID());""", """            MovementObject* mo = static_cast<MovementObject*>(o);
            if (mo->network_proxy) {
                auto was = stream_owned.find(GetOriginalID(mo->GetID()));
                if (was != stream_owned.end()) {
                    was->second.has_last = false;  // someone else streams it now: ours starts fresh
                }
                continue;
            }

            CommonObjectID cid = GetOriginalID(mo->GetID());"""),
("""            // Morphs: only the ones that moved since we last sent them
            owned.sent_morphs.resize(frame.morphs.size() * 2, std::numeric_limits<int>::min());
            size_t kept = 0;
            for (size_t i = 0; i < frame.morphs.size(); ++i) {
                int d = MorphStep(frame.morphs[i].disp_weight);
                int w = MorphStep(frame.morphs[i].mod_weight);
                if (all_morphs || d != owned.sent_morphs[i * 2] || w != owned.sent_morphs[i * 2 + 1]) {
                    owned.sent_morphs[i * 2] = d;
                    owned.sent_morphs[i * 2 + 1] = w;
                    frame.morphs[kept++] = frame.morphs[i];
                }
            }
            frame.morphs.resize(kept);
            frame.has_morphs = kept > 0;
""", """            // Morphs: only the ones that moved since we last sent them
            owned.sent_morphs.resize(frame.morphs.size() * 2, std::numeric_limits<int>::min());
            bool morphs_moved = all_morphs;
            for (size_t i = 0; i < frame.morphs.size() && !morphs_moved; ++i) {
                morphs_moved = MorphStep(frame.morphs[i].disp_weight) != owned.sent_morphs[i * 2] ||
                               MorphStep(frame.morphs[i].mod_weight) != owned.sent_morphs[i * 2 + 1];
            }

            // Only breathing since the last one sent: keep that one (the
            // morph refresh above still goes out about once a second)
            if (!morphs_moved && owned.has_last && CharSnapshot::Similar(frame, owned.last_sent)) {
                ++snapshot_idle_skips;
                continue;
            }
            owned.last_sent = frame;
            owned.last_sent.morphs.clear();
            owned.has_last = true;

            size_t kept = 0;
            for (size_t i = 0; i < frame.morphs.size(); ++i) {
                int d = MorphStep(frame.morphs[i].disp_weight);
                int w = MorphStep(frame.morphs[i].mod_weight);
                if (all_morphs || d != owned.sent_morphs[i * 2] || w != owned.sent_morphs[i * 2 + 1]) {
                    owned.sent_morphs[i * 2] = d;
                    owned.sent_morphs[i * 2 + 1] = w;
                    frame.morphs[kept++] = frame.morphs[i];
                }
            }
            frame.morphs.resize(kept);
            frame.has_morphs = kept > 0;
"""),
])
print("ok")
