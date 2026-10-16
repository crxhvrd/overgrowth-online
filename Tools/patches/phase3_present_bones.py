"""Phase 3: stream only bones that have a physics body (15 of the rabbit's 52
are body-less connectors whose network entries were junk at the world origin,
which stretched the quantization box), and link bodies through those
connectors. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""    float scale = 1.0f;
    bool derived = false;  // read without a position: Resolve() rebuilds it from the joint
};""", """    float scale = 1.0f;
    bool present = true;   // has a physics body (connector bones have none: not streamed)
    bool derived = false;  // read without a position: Resolve() rebuilds it from the joint
};"""),
("""//   [the bones' box around pos: int16 min[3], uint16 size[3] (cm);
//    a bit per bone: position sent (else rebuilt from the joint with its parent);
//    per bone: uint32 rotation (smallest three);
//    per bone with a position: uint32 offset in the box (11/11/10 bits x/y/z)],""",
 """//   [a bit per bone: present (has a body); for the present ones:
//    the box their positions sit in around pos: int16 min[3], uint16 size[3] (cm);
//    a bit per bone: position sent (else rebuilt from the joint with its parent);
//    per bone: uint32 rotation (smallest three);
//    per bone with a position: uint32 offset in the box (11/11/10 bits x/y/z)],"""),
])

patch("Online/char_snapshot.cpp", [
# Writer: present mask first, box over the bones whose positions travel
("""    if (bone_count > 0) {
        // The box the bones sit in (cm grid, so both sides agree exactly)
        int lo[3], size[3];
        for (int i = 0; i < 3; ++i) {
            float mn = 0.0f, mx = 0.0f;
            for (int b = 0; b < bone_count; ++b) {
                float d = f.bones[b].translation[i] - f.position[i];""", """    if (bone_count > 0) {
        std::vector<uint8_t> present((bone_count + 7) / 8, 0);
        for (int b = 0; b < bone_count; ++b) {
            if (f.bones[b].present) {
                present[b / 8] |= (uint8_t)(1u << (b % 8));
            }
        }
        for (uint8_t m : present) {
            Put<uint8_t>(out, m);
        }

        // The box the bones sit in (cm grid, so both sides agree exactly)
        int lo[3], size[3];
        for (int i = 0; i < 3; ++i) {
            float mn = 0.0f, mx = 0.0f;
            for (int b = 0; b < bone_count; ++b) {
                if (!f.bones[b].present) {
                    continue;
                }
                float d = f.bones[b].translation[i] - f.position[i];"""),
("""        for (int k = 0; k < bone_count; ++k) {
            int b = linked ? rig->order[k] : k;
            const vec3& actual = f.bones[b].translation;
""", """        for (int k = 0; k < bone_count; ++k) {
            int b = linked ? rig->order[k] : k;
            const vec3& actual = f.bones[b].translation;
            if (!f.bones[b].present) {
                continue;
            }
"""),
("""            if (linked && rig->links[b].parent >= 0) {
                const BoneLink& link = rig->links[b];""", """            if (linked && rig->links[b].parent >= 0 && f.bones[rig->links[b].parent].present) {
                const BoneLink& link = rig->links[b];"""),
("""        for (int b = 0; b < bone_count; ++b) {
            Put<uint32_t>(out, rot[b]);
        }
        for (int b = 0; b < bone_count; ++b) {
            if (mask[b / 8] & (1u << (b % 8))) {
                Put<uint32_t>(out, offset[b]);
            }
        }""", """        for (int b = 0; b < bone_count; ++b) {
            if (f.bones[b].present) {
                Put<uint32_t>(out, rot[b]);
            }
        }
        for (int b = 0; b < bone_count; ++b) {
            if (mask[b / 8] & (1u << (b % 8))) {
                Put<uint32_t>(out, offset[b]);
            }
        }"""),
# Reader
("""    if (bone_count > 0) {
        int16_t lo[3];
        uint16_t box[3];""", """    if (bone_count > 0) {
        uint8_t present[(kMaxBones + 7) / 8];
        for (int m = 0; m < (bone_count + 7) / 8; ++m) {
            if (!Get(data, size, offset, present[m])) {
                return false;
            }
        }

        int16_t lo[3];
        uint16_t box[3];"""),
("""        for (int b = 0; b < bone_count; ++b) {
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
            }""", """        for (int b = 0; b < bone_count; ++b) {
            Bone& bone = f.bones[b];
            bone.scale = 1.0f;
            bone.present = (present[b / 8] & (1u << (b % 8))) != 0;

            if (!bone.present) {  // what an owner without a body for it holds: nothing
                bone.rotation = quaternion();
                bone.translation = vec3(0.0f);
                bone.derived = false;
                continue;
            }

            uint32_t rot;
            if (!Get(data, size, offset, rot)) {
                return false;
            }
            bone.rotation = UnpackQuat(rot);
            bone.derived = (mask[b / 8] & (1u << (b % 8))) == 0;
        }
        for (int b = 0; b < bone_count; ++b) {
            if (!f.bones[b].present || f.bones[b].derived) {
                continue;
            }"""),
("""        const BoneLink& link = rig.links[b];
        if (link.parent < 0 || f.bones[link.parent].derived) {""", """        const BoneLink& link = rig.links[b];
        if (link.parent < 0 || f.bones[link.parent].derived || !f.bones[link.parent].present) {"""),
])

patch("Objects/movementobject.cpp", [
("""    out.bones.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        out.bones[i].rotation = bones[i].rotation1;
        out.bones[i].translation = bones[i].translation1;
        out.bones[i].scale = bones[i].scale;
    }""", """    const std::vector<PhysicsBone>& physics = rigged_object_->skeleton_.physics_bones;
    out.bones.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        out.bones[i].rotation = bones[i].rotation1;
        out.bones[i].translation = bones[i].translation1;
        out.bones[i].scale = bones[i].scale;
        out.bones[i].present = i < physics.size() && physics[i].bullet_object != nullptr;
    }"""),
])

patch("Objects/riggedobject.h", [
("""    CharSnapshot::Rig network_rig;
""", """    CharSnapshot::Rig network_rig;
    float network_rig_scale = 0.0f;
"""),
])

patch("Objects/riggedobject.cpp", [
("""    if (network_rig.Matches(n)) {
        return network_rig;
    }
""", """    if (network_rig.Matches(n) && network_rig_scale == model_char_scale) {
        return network_rig;
    }
    network_rig_scale = model_char_scale;
"""),
# Walk through body-less connector bones too (assumes the connector moves
# with the parent; the sender checks every prediction anyway)
("""            for (int e = 0; e < 2; ++e) {
                int point = skeleton_.bones[parent_bone].points[e];
                auto it = at_point.find(point);
                if (it == at_point.end()) {
                    continue;
                }
                vec3 joint = skeleton_.points[point] * model_char_scale;

                for (int child : it->second) {""", """            // Joints: the parent's own ends, and the far ends of body-less
            // connector bones hanging off them
            std::vector<int> joints;
            for (int e = 0; e < 2; ++e) {
                int point = skeleton_.bones[parent_bone].points[e];
                joints.push_back(point);
                for (size_t c = 0; c < n; ++c) {
                    int cb = (int)c;  // physics bone i is skeleton bone i
                    if (pb[c].bullet_object != nullptr || cb < 0 || cb >= (int)skeleton_.bones.size()) {
                        continue;
                    }
                    if (skeleton_.bones[cb].points[0] == point) {
                        joints.push_back(skeleton_.bones[cb].points[1]);
                    } else if (skeleton_.bones[cb].points[1] == point) {
                        joints.push_back(skeleton_.bones[cb].points[0]);
                    }
                }
            }

            for (int point : joints) {
                auto it = at_point.find(point);
                if (it == at_point.end()) {
                    continue;
                }
                vec3 joint = skeleton_.points[point] * model_char_scale;

                for (int child : it->second) {"""),
])

print("ok")
