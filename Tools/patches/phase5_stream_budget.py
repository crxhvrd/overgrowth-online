"""Phase 5, part 8 (bandwidth, stress_8): the host's snapshots fit a budget.

- Every character has a priority for each receiver: near, in view, or
  fighting that receiver's character (its target or attacker) counts most.
  The wanted rates (30 Hz times the priority) are scaled down together when
  they don't fit the receiver's share of the host's snapshot budget
  (kSnapshotBudget, split over the receivers), between 1 and 30 Hz. With a
  few players nothing is throttled.
- Vitals (about 35 bytes of a record) go only when they changed for that
  receiver, twice more after a change (losses), and once a second. The host
  keeps a record of each capture with and without them. Clients always send
  them (one character; the host relays it as it came).
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""bool Similar(const Frame& a, const Frame& b, float pos_tol = 0.01f, float rot_tol = 0.026f);""",
 """bool Similar(const Frame& a, const Frame& b, float pos_tol = 0.01f, float rot_tol = 0.026f);
bool SameVitals(const Vitals& v, const Vitals& w);"""),
])

patch("Online/char_snapshot.cpp", [
("""    const Vitals& v = a.vitals;
    const Vitals& w = b.vitals;
    if (v.valid != w.valid) {
        return false;
    }
    if (v.valid && (v.knocked_out""", """    return SameVitals(a.vitals, b.vitals);
}

bool SameVitals(const Vitals& v, const Vitals& w) {
    if (v.valid != w.valid) {
        return false;
    }
    if (v.valid && (v.knocked_out"""),
])

patch("Online/online.h", [
("""    struct StreamRecord {
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
    };""", """    struct StreamRecord {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> lite;  // the same without vitals (our own captures)
        uint32_t src_ms = 0;
        uint64_t hash = 0;  // content without id and time: same hash = nothing moved
        vec3 position;
        PlayerID owner = 0;
        int vitals_version = 0;    // changes when its vitals do
        CommonObjectID target = -1;    // whom it fights (its target, who hit it last)
        CommonObjectID attacker = -1;
    };
    struct StreamSent {
        uint32_t src_ms = 0;
        uint64_t hash = 0;
        double sent_ms = -1e9;
        int vitals_version = -1;
        int vitals_repeats = 0;
        double vitals_ms = -1e9;
    };"""),
("""        CharSnapshot::Frame last_sent;  // the pose last streamed (idle characters are not resent)
        bool has_last = false;""", """        CharSnapshot::Frame last_sent;  // the pose last streamed (idle characters are not resent)
        bool has_last = false;
        CharSnapshot::Vitals last_vitals;
        int vitals_version = 0;"""),
])

patch("Online/online.cpp", [
# Priority from distance and view
("""double StreamIntervalMs(float dist) {
    if (dist < 20.0f) {
        return 1000.0 / 30.0;
    } else if (dist < 50.0f) {
        return 1000.0 / 15.0;
    }
    return 1000.0 / 5.0;
}
""", """// The host's snapshots, all receivers together: 1.2 Mbit/s (stress_8: 8
// players and 30 NPCs within 1.5 Mbit/s with everything else)
const double kSnapshotBudget = 150000.0;  // bytes a second

// How much a receiver wants a character, 0..1 (times 30 Hz)
double StreamPriority(float dist, bool in_view, bool engaged) {
    double w = dist < 5.0f ? 1.0 : dist < 12.0f ? 0.6 : dist < 25.0f ? 0.35 : dist < 50.0f ? 0.2 : 0.08;
    if (!in_view) {
        w *= 0.3;
    }
    if (engaged) {
        w = 1.0;  // fighting each other: full rate
    }
    return w;
}
"""),
# Capture: both encodings, the vitals version, whom it fights
("""            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            int positions = 0;
            CharSnapshot::Write(r.bytes, frame, &mo->rigged_object()->NetworkRig(), &positions);
            snapshot_positions_sent += positions;""", """            if (!CharSnapshot::SameVitals(frame.vitals, owned.last_vitals)) {
                owned.last_vitals = frame.vitals;
                ++owned.vitals_version;
            }

            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            int positions = 0;
            CharSnapshot::Write(r.bytes, frame, &mo->rigged_object()->NetworkRig(), &positions);
            r.lite.clear();
            if (IsHosting() && frame.vitals.valid) {
                CharSnapshot::Frame lite = frame;
                lite.vitals.valid = false;
                CharSnapshot::Write(r.lite, lite, &mo->rigged_object()->NetworkRig());
            }
            r.vitals_version = owned.vitals_version;
            r.target = frame.vitals.valid ? frame.vitals.target_id : -1;
            r.attacker = frame.vitals.valid ? frame.vitals.attacked_by : -1;
            snapshot_positions_sent += positions;"""),
# Receivers: their character too
("""    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
        bool has_cam;
        vec3 cam_pos;
        vec3 cam_dir;
    };
    vector<Receiver> receivers;
    {
        lock_guard<mutex> guard(online_session->connected_peers_lock);
        for (const Peer& peer : online_session->connected_peers) {
            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f), false, vec3(0.0f), vec3(0.0f)});
        }
    }
""", """    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
        bool has_cam;
        vec3 cam_pos;
        vec3 cam_dir;
        CommonObjectID avatar;
    };
    vector<Receiver> receivers;
    {
        lock_guard<mutex> guard(online_session->connected_peers_lock);
        for (const Peer& peer : online_session->connected_peers) {
            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f), false, vec3(0.0f), vec3(0.0f), -1});
        }
    }
    double budget = kSnapshotBudget / std::max<size_t>(1, receivers.size());  // bytes a second each
"""),
("""            Object* o = sg->GetObjectFromID(ps->second.object_id);
            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
            }""", """            Object* o = sg->GetObjectFromID(ps->second.object_id);
            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
                rc.avatar = GetOriginalID(ps->second.object_id);
            }"""),
# The send loop: priorities scaled to the budget, vitals when due
("""        std::vector<uint8_t> bundle;
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
            if (rc.has_cam) {
                vec3 to = r.position - rc.cam_pos;
                if (length(to) > 8.0f && dot(to, rc.cam_dir) < 0.0f) {
                    interval = std::max(interval, 1000.0 / 5.0);  // behind them
                }
            }
            if (now - sent.sent_ms < interval - kStreamSlackMs) {
                continue;
            }
            if (r.hash == sent.hash && now - sent.sent_ms < kStreamHeartbeatMs) {
                continue;
            }
""", """        std::vector<uint8_t> bundle;
        int records = 0;

        // What they'd like (30 Hz times the priority), and the scale that fits their share
        struct Want {
            CommonObjectID cid;
            double rate;
        };
        vector<Want> wants;
        double want_bytes = 0.0;
        for (auto& it : stream_latest) {
            const StreamRecord& r = it.second;
            if (r.owner == rc.peer || r.bytes.empty()) {
                continue;  // their own character
            }
            float dist = rc.has_pos ? distance(rc.pos, r.position) : 10.0f;
            bool in_view = true;
            if (rc.has_cam) {
                vec3 to = r.position - rc.cam_pos;
                in_view = length(to) < 6.0f || dot(normalize(to), rc.cam_dir) > 0.35f;
            }
            bool engaged = rc.avatar >= 0 && (r.target == rc.avatar || r.attacker == rc.avatar);
            double rate = 30.0 * StreamPriority(dist, in_view, engaged);
            wants.push_back({it.first, rate});
            want_bytes += rate * (r.lite.empty() ? r.bytes.size() : r.lite.size());
        }
        double scale = want_bytes > budget ? budget / want_bytes : 1.0;

        for (const Want& want : wants) {
            const StreamRecord& r = stream_latest[want.cid];
            StreamSent& sent = stream_sent[std::make_pair(rc.peer, want.cid)];
            if (r.src_ms == sent.src_ms) {
                continue;
            }

            double interval = 1000.0 / std::max(1.0, std::min(30.0, want.rate * scale));
            if (now - sent.sent_ms < interval - kStreamSlackMs) {
                continue;
            }
            if (r.hash == sent.hash && now - sent.sent_ms < kStreamHeartbeatMs) {
                continue;
            }

            // Vitals when they changed for them (and twice more), and once a second
            bool vitals = r.lite.empty();
            if (!vitals) {
                if (sent.vitals_version != r.vitals_version) {
                    sent.vitals_version = r.vitals_version;
                    sent.vitals_repeats = 3;
                }
                if (sent.vitals_repeats > 0 || now - sent.vitals_ms > 1000.0) {
                    vitals = true;
                    sent.vitals_repeats = std::max(0, sent.vitals_repeats - 1);
                    sent.vitals_ms = now;
                }
            }
            const std::vector<uint8_t>& bytes = vitals ? r.bytes : r.lite;
"""),
("""            if (!bundle.empty() && bundle.size() + r.bytes.size() > kStreamBundleBytes) {
                snapshot_bytes_out += bundle.size();
                snapshot_records_out += records;
                SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
                bundle.clear();
                records = 0;
            }

            bundle.insert(bundle.end(), r.bytes.begin(), r.bytes.end());""", """            if (!bundle.empty() && bundle.size() + bytes.size() > kStreamBundleBytes) {
                snapshot_bytes_out += bundle.size();
                snapshot_records_out += records;
                SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
                bundle.clear();
                records = 0;
            }

            bundle.insert(bundle.end(), bytes.begin(), bytes.end());"""),
# Relayed records: whom they fight
("""            r.position = frame.position;
            r.owner = from;""", """            r.position = frame.position;
            r.owner = from;
            r.lite.clear();
            if (frame.vitals.valid) {
                r.target = frame.vitals.target_id;
                r.attacker = frame.vitals.attacked_by;
            }"""),
])
print("ok")
