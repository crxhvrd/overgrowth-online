"""Phase 3: bone positions from joints. Most bone positions follow from the
parent bone's transform and the shared ball joint, so a record carries every
rotation but only the positions that the joint does not predict within
2.5 mm. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""struct Bone {
    quaternion rotation;
    vec3 translation;
    float scale = 1.0f;
};""", """struct Bone {
    quaternion rotation;
    vec3 translation;
    float scale = 1.0f;
    bool derived = false;  // read without a position: Resolve() rebuilds it from the joint
};

// How a bone hangs off its parent: the shared joint, as an offset from each
// bone's display origin in that bone's own frame (at rest). Both sides build
// it from the same skeleton, so a bone whose joint holds needs no position.
struct BoneLink {
    int parent = -1;
    vec3 joint_in_parent;
    vec3 joint_in_child;
};

struct Rig {
    std::vector<BoneLink> links;
    std::vector<int> order;  // every bone once, parents first
    bool Matches(size_t bone_count) const { return links.size() == bone_count && order.size() == bone_count; }
};

const float kLinkTolerance = 0.0025f;  // a joint off by more (a stretched ragdoll) sends the position

// Where a bone sits if its joint with the parent holds
vec3 PredictFromParent(const BoneLink& link, const quaternion& parent_rotation, const vec3& parent_translation,
                       const quaternion& rotation);"""),
("""//   [the bones' box around pos: int16 min[3], uint16 size[3] (cm);
//    per bone: uint32 offset in the box (11/11/10 bits x/y/z), uint32 rotation (smallest three)],""",
 """//   [the bones' box around pos: int16 min[3], uint16 size[3] (cm);
//    a bit per bone: position sent (else rebuilt from the joint with its parent);
//    per bone: uint32 rotation (smallest three);
//    per bone with a position: uint32 offset in the box (11/11/10 bits x/y/z)],"""),
("""void Write(std::vector<uint8_t>& out, const Frame& f);

// Reads the record at offset and advances it. False on malformed data.
bool Read(const uint8_t* data, size_t size, size_t& offset, Frame& f);""",
 """// With a rig, bones whose joints hold go without a position (most of them)
void Write(std::vector<uint8_t>& out, const Frame& f, const Rig* rig = nullptr, int* positions_sent = nullptr);

// Reads the record at offset and advances it. False on malformed data.
// Bones sent without a position are marked derived: call Resolve.
bool Read(const uint8_t* data, size_t size, size_t& offset, Frame& f);

// Rebuilds the derived bones from their parents. False if the rig does not fit.
bool Resolve(Frame& f, const Rig& rig);"""),
])

patch("Online/char_snapshot.cpp", [
("""#include <Math/vec3math.h>
""", """#include <Math/vec3math.h>
#include <Math/mat4.h>
"""),
("""uint32_t PackQuat(const quaternion& in) {""", """vec3 PredictFromParent(const BoneLink& link, const quaternion& parent_rotation, const vec3& parent_translation,
                       const quaternion& rotation) {
    vec3 joint = parent_translation + Mat4FromQuaternion(parent_rotation).GetRotatedvec3(link.joint_in_parent);
    return joint - Mat4FromQuaternion(rotation).GetRotatedvec3(link.joint_in_child);
}

uint32_t PackQuat(const quaternion& in) {"""),
("""void Write(std::vector<uint8_t>& out, const Frame& f) {""", """void Write(std::vector<uint8_t>& out, const Frame& f, const Rig* rig, int* positions_sent) {"""),
("""        for (int b = 0; b < bone_count; ++b) {
            uint32_t packed = 0;
            for (int i = 0; i < 3; ++i) {
                float d = f.bones[b].translation[i] - f.position[i];
                float t = std::isfinite(d) ? (d * 100.0f - lo[i]) / size[i] : 0.0f;
                packed |= (uint32_t)std::round(std::max(0.0f, std::min(1.0f, t)) * kBoxSteps[i]) << kBoxShift[i];
            }
            Put<uint32_t>(out, packed);
            Put<uint32_t>(out, PackQuat(f.bones[b].rotation));
        }
    }""", """        // Rotations as the receiver will see them; then positions, parents
        // first, each checked against what the receiver would rebuild from
        // the joint (with the rotations and parent positions it will have)
        std::vector<uint32_t> rot(bone_count), offset(bone_count, 0);
        std::vector<quaternion> seen_rot(bone_count);
        std::vector<vec3> seen_pos(bone_count);
        std::vector<uint8_t> mask((bone_count + 7) / 8, 0);
        bool linked = rig != nullptr && rig->Matches(bone_count);
        int sent = 0;

        for (int b = 0; b < bone_count; ++b) {
            rot[b] = PackQuat(f.bones[b].rotation);
            seen_rot[b] = UnpackQuat(rot[b]);
        }

        for (int k = 0; k < bone_count; ++k) {
            int b = linked ? rig->order[k] : k;
            const vec3& actual = f.bones[b].translation;

            if (linked && rig->links[b].parent >= 0) {
                const BoneLink& link = rig->links[b];
                vec3 predicted = PredictFromParent(link, seen_rot[link.parent], seen_pos[link.parent], seen_rot[b]);
                if (distance(predicted, actual) <= kLinkTolerance) {
                    seen_pos[b] = predicted;
                    continue;
                }
            }

            uint32_t packed = 0;
            vec3 d;
            for (int i = 0; i < 3; ++i) {
                float rel = actual[i] - f.position[i];
                float t = std::isfinite(rel) ? (rel * 100.0f - lo[i]) / size[i] : 0.0f;
                uint32_t q = (uint32_t)std::round(std::max(0.0f, std::min(1.0f, t)) * kBoxSteps[i]);
                packed |= q << kBoxShift[i];
                d[i] = (lo[i] + q / kBoxSteps[i] * size[i]) / 100.0f;
            }
            offset[b] = packed;
            seen_pos[b] = f.position + d;
            mask[b / 8] |= (uint8_t)(1u << (b % 8));
            ++sent;
        }

        for (uint8_t m : mask) {
            Put<uint8_t>(out, m);
        }
        for (int b = 0; b < bone_count; ++b) {
            Put<uint32_t>(out, rot[b]);
        }
        for (int b = 0; b < bone_count; ++b) {
            if (mask[b / 8] & (1u << (b % 8))) {
                Put<uint32_t>(out, offset[b]);
            }
        }
        if (positions_sent != nullptr) {
            *positions_sent = sent;
        }
    }"""),
("""        for (int b = 0; b < bone_count; ++b) {
            uint32_t packed, rot;
            if (!Get(data, size, offset, packed) || !Get(data, size, offset, rot)) {
                return false;
            }
            vec3 d;
            for (int i = 0; i < 3; ++i) {
                float t = (float)((packed >> kBoxShift[i]) & kBoxMask[i]) / kBoxSteps[i];
                d[i] = (lo[i] + t * box[i]) / 100.0f;
            }
            f.bones[b].translation = f.position + d;
            f.bones[b].rotation = UnpackQuat(rot);
            f.bones[b].scale = 1.0f;
        }
    }""", """        uint8_t mask[(kMaxBones + 7) / 8];
        for (int m = 0; m < (bone_count + 7) / 8; ++m) {
            if (!Get(data, size, offset, mask[m])) {
                return false;
            }
        }
        for (int b = 0; b < bone_count; ++b) {
            uint32_t rot;
            if (!Get(data, size, offset, rot)) {
                return false;
            }
            f.bones[b].rotation = UnpackQuat(rot);
            f.bones[b].scale = 1.0f;
            f.bones[b].derived = (mask[b / 8] & (1u << (b % 8))) == 0;
        }
        for (int b = 0; b < bone_count; ++b) {
            if (f.bones[b].derived) {
                continue;
            }
            uint32_t packed;
            if (!Get(data, size, offset, packed)) {
                return false;
            }
            vec3 d;
            for (int i = 0; i < 3; ++i) {
                float t = (float)((packed >> kBoxShift[i]) & kBoxMask[i]) / kBoxSteps[i];
                d[i] = (lo[i] + t * box[i]) / 100.0f;
            }
            f.bones[b].translation = f.position + d;
        }
    }"""),
("""uint64_t Hash(const uint8_t* data, size_t size) {""", """bool Resolve(Frame& f, const Rig& rig) {
    bool any = false;
    for (const Bone& b : f.bones) {
        any = any || b.derived;
    }
    if (!any) {
        return true;
    }
    if (!rig.Matches(f.bones.size())) {
        return false;
    }

    for (int b : rig.order) {
        Bone& bone = f.bones[b];
        if (!bone.derived) {
            continue;
        }
        const BoneLink& link = rig.links[b];
        if (link.parent < 0 || f.bones[link.parent].derived) {
            return false;  // not a bone the sender could have left out
        }
        bone.translation = PredictFromParent(link, f.bones[link.parent].rotation, f.bones[link.parent].translation, bone.rotation);
        bone.derived = false;
    }

    return true;
}

uint64_t Hash(const uint8_t* data, size_t size) {"""),
])

# --- Engine side: the rig from the skeleton, used when capturing and receiving ---------

patch("Objects/riggedobject.h", [
("""#include <Math/quaternions.h>
#include <Math/overgrowth_geometry.h>
""", """#include <Math/quaternions.h>
#include <Math/overgrowth_geometry.h>

#include <Online/char_snapshot.h>
"""),
("""    bool network_proxy = false;                        // Overhaul Online: shown from another machine's stream (MovementObject sets it)
""", """    bool network_proxy = false;                        // Overhaul Online: shown from another machine's stream (MovementObject sets it)
    const CharSnapshot::Rig &NetworkRig();             // Overhaul Online: joints between physics bones, for compact snapshots
    CharSnapshot::Rig network_rig;
"""),
])

patch("Objects/riggedobject.cpp", [
("""mat4 RiggedObject::NetworkBoneMatrix(""", """// Overhaul Online: for each physics bone, its parent in the skeleton and the
// ball joint they share (a skeleton point), as offsets from each bone's display
// origin in its own rest frame. Identical on every machine for a character.
const CharSnapshot::Rig& RiggedObject::NetworkRig() {
    const std::vector<PhysicsBone>& pb = skeleton_.physics_bones;
    size_t n = pb.size();

    if (network_rig.Matches(n)) {
        return network_rig;
    }

    network_rig.links.assign(n, CharSnapshot::BoneLink());
    network_rig.order.clear();
    std::vector<std::vector<int> > children(n);

    for (size_t i = 0; i < n; ++i) {
        int p = i < skeleton_.parents.size() ? skeleton_.parents[i] : -1;
        if (p < 0 || p >= (int)n || p == (int)i || pb[i].bullet_object == nullptr || pb[p].bullet_object == nullptr ||
            i >= skeleton_.bones.size() || p >= (int)skeleton_.bones.size()) {
            continue;
        }

        int shared = -1;
        for (int a = 0; a < 2; ++a) {
            for (int b = 0; b < 2; ++b) {
                if (skeleton_.bones[i].points[a] == skeleton_.bones[p].points[b]) {
                    shared = skeleton_.bones[i].points[a];
                }
            }
        }
        if (shared < 0 || shared >= (int)skeleton_.points.size()) {
            continue;
        }

        // A bone's display origin at rest: its body position less the
        // centre-of-mass offset (as BulletObject::GetDisplayTransform does)
        vec3 joint = skeleton_.points[shared];
        vec3 origin_i = pb[i].initial_position + pb[i].initial_rotation.GetRotatedvec3(pb[i].bullet_object->com_offset * -1.0f);
        vec3 origin_p = pb[p].initial_position + pb[p].initial_rotation.GetRotatedvec3(pb[p].bullet_object->com_offset * -1.0f);

        CharSnapshot::BoneLink& link = network_rig.links[i];
        link.parent = p;
        link.joint_in_child = transpose(pb[i].initial_rotation).GetRotatedvec3(joint - origin_i);
        link.joint_in_parent = transpose(pb[p].initial_rotation).GetRotatedvec3(joint - origin_p);
        children[p].push_back((int)i);
    }

    // Parents first; anything left (a loop) becomes a root that sends its position
    std::vector<bool> placed(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (network_rig.links[i].parent >= 0) {
            continue;
        }
        std::vector<int> stack(1, (int)i);
        while (!stack.empty()) {
            int b = stack.back();
            stack.pop_back();
            if (placed[b]) {
                continue;
            }
            placed[b] = true;
            network_rig.order.push_back(b);
            for (int c : children[b]) {
                stack.push_back(c);
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        if (!placed[i]) {
            network_rig.links[i].parent = -1;
            network_rig.order.push_back((int)i);
        }
    }

    return network_rig;
}

mat4 RiggedObject::NetworkBoneMatrix("""),
])

# --- Online: capture with the rig, resolve on arrival ---------------------------------

patch("Online/online.cpp", [
("""            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            CharSnapshot::Write(r.bytes, frame);""", """            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            int positions = 0;
            CharSnapshot::Write(r.bytes, frame, &mo->rigged_object()->NetworkRig(), &positions);
            snapshot_positions_sent += positions;
            snapshot_bones_captured += frame.bones.size();"""),
("""        MovementObject* mo = static_cast<MovementObject*>(o);
        mo->net_buffer.Push(frame, now);
        ++snapshot_records_in;""", """        MovementObject* mo = static_cast<MovementObject*>(o);
        if (!CharSnapshot::Resolve(frame, mo->rigged_object()->NetworkRig())) {
            continue;  // a different skeleton here: nothing sensible to show
        }
        mo->net_buffer.Push(frame, now);
        ++snapshot_records_in;"""),
])

patch("Online/online.h", [
("""    std::atomic<uint64_t> snapshot_bytes_out{0};
""", """    std::atomic<uint64_t> snapshot_bytes_out{0};
    std::atomic<uint64_t> snapshot_bones_captured{0};  // bones in captured records
    std::atomic<uint64_t> snapshot_positions_sent{0};  // of those, positions the joints did not predict
"""),
])

patch("Testing/test_bridge.cpp", [
("""        o["snapshot_bytes_out"] = (Json::UInt64)online->snapshot_bytes_out.load();
""", """        o["snapshot_bytes_out"] = (Json::UInt64)online->snapshot_bytes_out.load();
        o["bones_captured"] = (Json::UInt64)online->snapshot_bones_captured.load();
        o["positions_sent"] = (Json::UInt64)online->snapshot_positions_sent.load();
"""),
])
print("ok")
