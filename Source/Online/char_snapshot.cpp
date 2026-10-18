//-----------------------------------------------------------------------------
//           Name: char_snapshot.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See char_snapshot.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "char_snapshot.h"

#include <Math/vec3math.h>
#include <Math/mat4.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace CharSnapshot {

namespace {

template <typename T>
void Put(std::vector<uint8_t>& out, T v) {
    size_t at = out.size();
    out.resize(at + sizeof(T));
    memcpy(out.data() + at, &v, sizeof(T));
}

template <typename T>
bool Get(const uint8_t* data, size_t size, size_t& offset, T& v) {
    if (offset + sizeof(T) > size) {
        return false;
    }
    memcpy(&v, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

int16_t Q16(float v, float scale) {
    float q = std::round(v * scale);
    return (int16_t)std::max(-32767.0f, std::min(32767.0f, q));
}

const float kVelScale = 100.0f;  // cm/s
const float kUnitScale = 32767.0f;
const float kMorphScale = 1000.0f;
const float kInvSqrt2 = 0.70710678f;
const float kBoxSteps[3] = {2047.0f, 2047.0f, 1023.0f};  // 11/11/10 bits x/y/z (a character is tallest in y)
const int kBoxShift[3] = {21, 10, 0};
const uint32_t kBoxMask[3] = {2047u, 2047u, 1023u};

}  // namespace

vec3 PredictFromParent(const BoneLink& link, const quaternion& parent_rotation, const vec3& parent_translation,
                       const quaternion& rotation) {
    vec3 joint = parent_translation + Mat4FromQuaternion(parent_rotation).GetRotatedvec3(link.joint_in_parent);
    return joint - Mat4FromQuaternion(rotation).GetRotatedvec3(link.joint_in_child);
}

uint32_t PackQuat(const quaternion& in) {
    float q[4] = {in[0], in[1], in[2], in[3]};
    float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

    if (!(len > 1e-6f)) {  // also NaN
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        len = 1.0f;
    }

    int largest = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] /= len;
        if (std::fabs(q[i]) > std::fabs(q[largest])) {
            largest = i;
        }
    }

    float sign = q[largest] < 0.0f ? -1.0f : 1.0f;  // q and -q are the same rotation
    uint32_t v = (uint32_t)largest << 30;
    int shift = 20;

    for (int i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        // [-1/sqrt2, 1/sqrt2] -> [0, 1022]: an even grid, so 0 is exact (identity bones)
        float c = q[i] * sign / kInvSqrt2 * 0.5f + 0.5f;
        uint32_t bits = (uint32_t)std::max(0.0f, std::min(1022.0f, std::round(c * 1022.0f)));
        v |= bits << shift;
        shift -= 10;
    }

    return v;
}

bool Finite(const Frame& f) {
    auto fin3 = [](const vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); };
    if (!fin3(f.position) || !fin3(f.velocity) || !fin3(f.facing)) {
        return false;
    }
    for (const Bone& b : f.bones) {
        if (!fin3(b.translation) || !std::isfinite(b.scale) || !std::isfinite(b.rotation[0]) || !std::isfinite(b.rotation[1]) ||
            !std::isfinite(b.rotation[2]) || !std::isfinite(b.rotation[3])) {
            return false;
        }
    }
    return true;
}

quaternion UnpackQuat(uint32_t v) {
    int largest = (int)(v >> 30);
    float q[4];
    float sum = 0.0f;
    int shift = 20;

    for (int i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        float c = (float)((v >> shift) & 1023u) / 1022.0f;
        q[i] = (c - 0.5f) * 2.0f * kInvSqrt2;
        sum += q[i] * q[i];
        shift -= 10;
    }

    q[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
    return quaternion(q[0], q[1], q[2], q[3]);
}

void Write(std::vector<uint8_t>& out, const Frame& f, const Rig* rig, int* positions_sent) {
    Put<int32_t>(out, f.id);
    Put<uint32_t>(out, f.src_ms);

    for (int i = 0; i < 3; ++i) {
        Put<float>(out, f.position[i]);
    }
    for (int i = 0; i < 3; ++i) {
        Put<int16_t>(out, Q16(f.velocity[i], kVelScale));
    }

    vec3 facing = f.facing;
    float facing_len = length(facing);
    if (facing_len > 1e-6f) {
        facing = facing / facing_len;
    }
    for (int i = 0; i < 3; ++i) {
        Put<int16_t>(out, Q16(facing[i], kUnitScale));
    }

    int bone_count = std::min((int)f.bones.size(), kMaxBones);
    int morph_count = std::min((int)f.morphs.size(), kMaxMorphs);
    bool morphs = f.has_morphs && morph_count > 0;
    Put<uint8_t>(out, (uint8_t)((morphs ? 1 : 0) | (f.vitals.valid ? 2 : 0) | (f.resumed ? 4 : 0)));
    Put<uint8_t>(out, (uint8_t)bone_count);

    if (bone_count > 0) {
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
                float d = f.bones[b].translation[i] - f.position[i];
                if (std::isfinite(d)) {
                    mn = std::min(mn, d);
                    mx = std::max(mx, d);
                }
            }
            lo[i] = std::max(-32767, (int)std::floor(mn * 100.0f));
            size[i] = std::max(1, std::min(65535, (int)std::ceil(mx * 100.0f) - lo[i]));
            Put<int16_t>(out, (int16_t)lo[i]);
        }
        for (int i = 0; i < 3; ++i) {
            Put<uint16_t>(out, (uint16_t)size[i]);
        }

        // Rotations as the receiver will see them; then positions, parents
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
            if (!f.bones[b].present) {
                continue;
            }

            if (linked && rig->links[b].parent >= 0 && f.bones[rig->links[b].parent].present) {
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
            if (f.bones[b].present) {
                Put<uint32_t>(out, rot[b]);
            }
        }
        for (int b = 0; b < bone_count; ++b) {
            if (mask[b / 8] & (1u << (b % 8))) {
                Put<uint32_t>(out, offset[b]);
            }
        }
        if (positions_sent != nullptr) {
            *positions_sent = sent;
        }
    }

    uint8_t scaled = 0;
    for (int b = 0; b < bone_count; ++b) {
        if (std::fabs(f.bones[b].scale - 1.0f) > 1e-4f) {
            ++scaled;
        }
    }
    Put<uint8_t>(out, scaled);
    for (int b = 0; b < bone_count; ++b) {
        if (std::fabs(f.bones[b].scale - 1.0f) > 1e-4f) {
            Put<uint8_t>(out, (uint8_t)b);
            Put<float>(out, f.bones[b].scale);
        }
    }

    if (morphs) {
        Put<uint8_t>(out, (uint8_t)morph_count);
        for (int m = 0; m < morph_count; ++m) {
            Put<uint8_t>(out, f.morphs[m].index);
            Put<int16_t>(out, Q16(f.morphs[m].disp_weight, kMorphScale));
            Put<int16_t>(out, Q16(f.morphs[m].mod_weight, kMorphScale));
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
        Put<uint8_t>(out, (uint8_t)std::max(0.0f, std::min(255.0f, std::round(v.block_stunned * 100.0f))));
        Put<int32_t>(out, v.block_stunned_by);
        Put<int32_t>(out, v.attacked_by);
        Put<uint8_t>(out, v.primary_slot);
        uint8_t filled = 0;
        for (int i = 0; i < 6; ++i) {
            filled += v.slots[i] != -1 ? 1 : 0;
        }
        Put<uint8_t>(out, filled);
        for (int i = 0; i < 6; ++i) {
            if (v.slots[i] != -1) {
                Put<uint8_t>(out, (uint8_t)i);
                Put<int32_t>(out, v.slots[i]);
            }
        }
    }
}

bool Read(const uint8_t* data, size_t size, size_t& offset, Frame& f) {
    float pos[3];
    int16_t vel[3], facing[3];
    uint8_t flags, bone_count, scaled;

    if (!Get(data, size, offset, f.id) || !Get(data, size, offset, f.src_ms)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!Get(data, size, offset, pos[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!Get(data, size, offset, vel[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!Get(data, size, offset, facing[i])) {
            return false;
        }
    }
    if (!Get(data, size, offset, flags) || !Get(data, size, offset, bone_count) || bone_count > kMaxBones) {
        return false;
    }

    f.position = vec3(pos[0], pos[1], pos[2]);
    f.velocity = vec3(vel[0], vel[1], vel[2]) / kVelScale;
    f.facing = vec3(facing[0], facing[1], facing[2]) / kUnitScale;
    f.bones.resize(bone_count);

    if (bone_count > 0) {
        uint8_t present[(kMaxBones + 7) / 8];
        for (int m = 0; m < (bone_count + 7) / 8; ++m) {
            if (!Get(data, size, offset, present[m])) {
                return false;
            }
        }

        int16_t lo[3];
        uint16_t box[3];
        for (int i = 0; i < 3; ++i) {
            if (!Get(data, size, offset, lo[i])) {
                return false;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (!Get(data, size, offset, box[i]) || box[i] == 0) {
                return false;
            }
        }

        uint8_t mask[(kMaxBones + 7) / 8];
        for (int m = 0; m < (bone_count + 7) / 8; ++m) {
            if (!Get(data, size, offset, mask[m])) {
                return false;
            }
        }
        for (int b = 0; b < bone_count; ++b) {
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
    }

    if (!Get(data, size, offset, scaled)) {
        return false;
    }
    for (int s = 0; s < scaled; ++s) {
        uint8_t index;
        float value;
        if (!Get(data, size, offset, index) || !Get(data, size, offset, value) || index >= bone_count) {
            return false;
        }
        f.bones[index].scale = value;
    }

    f.has_morphs = (flags & 1) != 0;
    f.resumed = (flags & 4) != 0;
    f.morphs.clear();

    if (f.has_morphs) {
        uint8_t morph_count;
        if (!Get(data, size, offset, morph_count)) {
            return false;
        }
        f.morphs.resize(morph_count);
        for (int m = 0; m < morph_count; ++m) {
            int16_t disp, mod;
            if (!Get(data, size, offset, f.morphs[m].index) || !Get(data, size, offset, disp) || !Get(data, size, offset, mod)) {
                return false;
            }
            f.morphs[m].disp_weight = disp / kMorphScale;
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
        uint8_t stun;
        if (!Get(data, size, offset, stun) || !Get(data, size, offset, v.block_stunned_by) ||
            !Get(data, size, offset, v.attacked_by)) {
            return false;
        }
        v.block_stunned = stun / 100.0f;
        uint8_t filled;
        if (!Get(data, size, offset, v.primary_slot) || !Get(data, size, offset, filled) || filled > 6) {
            return false;
        }
        for (int i = 0; i < filled; ++i) {
            uint8_t slot;
            int32_t item;
            if (!Get(data, size, offset, slot) || !Get(data, size, offset, item) || slot >= 6) {
                return false;
            }
            v.slots[slot] = item;
        }
        v.valid = true;
        v.temp_health = temp / kMorphScale;
        v.permanent_health = perm / kMorphScale;
        v.blood_health = blood / kMorphScale;
        v.block_health = block / kMorphScale;
        v.duck_amount = duck / 255.0f;
    }

    return true;
}

bool Resolve(Frame& f, const Rig& rig) {
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
        if (link.parent < 0 || f.bones[link.parent].derived || !f.bones[link.parent].present) {
            return false;  // not a bone the sender could have left out
        }
        bone.translation = PredictFromParent(link, f.bones[link.parent].rotation, f.bones[link.parent].translation, bone.rotation);
        bone.derived = false;
    }

    return true;
}

bool Similar(const Frame& a, const Frame& b, float pos_tol, float rot_tol) {
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

    return SameVitals(a.vitals, b.vitals);
}

bool SameVitals(const Vitals& v, const Vitals& w) {
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

uint64_t Hash(const uint8_t* data, size_t size) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Playback --------------------------------------------------------------------

void Buffer::Clear() {
    frames_.clear();
    lags_.clear();
    gaps_.clear();
    started_ = false;
    extrapolating_ = false;
    play_offset_ = 0.0;
    render_src_ = 0.0;
}

double Buffer::IntervalMs() const {
    if (gaps_.empty()) {
        return 33.0;
    }
    // The longer of the usual gaps: a stream whose rate swings (the camera
    // turning, distance) keeps one delay instead of re-timing back and forth
    std::deque<double> sorted = gaps_;
    std::sort(sorted.begin(), sorted.end());
    return sorted[(sorted.size() - 1) * 3 / 4];
}

double Buffer::Target() const {
    if (lags_.empty()) {
        return 0.0;
    }
    std::deque<double> sorted = lags_;
    std::sort(sorted.begin(), sorted.end());
    double base = sorted.front();
    double jitter = sorted[(size_t)((sorted.size() - 1) * 0.95)] - base;
    return base + jitter + IntervalMs() + kMarginMs;
}

double Buffer::DelayMs() const {
    if (lags_.empty()) {
        return 0.0;
    }
    return play_offset_ - *std::min_element(lags_.begin(), lags_.end());
}

void Buffer::Push(const Frame& f, double arrival_ms) {
    double src = f.src_ms;

    if (!frames_.empty()) {
        double last = frames_.back().src_ms;
        if (src <= last) {
            return;  // duplicate or out of order
        }

        double gap = src - last;
        if (gap > kHoldGapMs && (f.resumed || gap > kSilenceMs)) {
            // The owner stopped sending because nothing changed: it stood
            // still until just before this frame, it did not glide here
            Frame hold = frames_.back();
            hold.src_ms = (uint32_t)(src - std::min(IntervalMs(), gap * 0.5));
            hold.velocity = vec3(0.0f);
            frames_.push_back(hold);
        } else {
            gaps_.push_back(gap);
            if (gaps_.size() > 16) {
                gaps_.pop_front();
            }
        }
    }

    frames_.push_back(f);
    frames_.back().morphs.clear();  // applied on arrival, not kept

    while ((int)frames_.size() > kMaxFrames) {
        frames_.pop_front();
    }

    lags_.push_back(arrival_ms - src);
    while ((int)lags_.size() > kLagWindow) {
        lags_.pop_front();
    }
}

bool Buffer::Sample(double now_ms, Frame& out) {
    if (frames_.empty()) {
        return false;
    }

    double target = Target();

    if (!started_ || std::fabs(play_offset_ - target) > 1000.0) {
        play_offset_ = target;
        started_ = true;
    } else {
        // Ease toward the target by playing slightly slower (10 %) or faster
        // (5 %: after a hitch the delay comes back within a couple of seconds);
        // a big change never as a jump (a snap forward or a rewind): later
        // by pausing until the frames cover it, sooner at 25 %
        double dt = std::max(0.0, std::min(100.0, now_ms - last_now_));
        double diff = target - play_offset_;
        bool big = std::fabs(diff) > 150.0;
        if (diff > 0.0) {
            play_offset_ += std::min(diff, dt * (big ? 1.0 : 0.10));
        } else {
            play_offset_ -= std::min(-diff, dt * (big ? 0.25 : 0.05));
        }
    }

    last_now_ = now_ms;
    render_src_ = now_ms - play_offset_;

    while (frames_.size() >= 2 && frames_[1].src_ms <= render_src_) {
        frames_.pop_front();
    }

    const Frame& a = frames_[0];
    extrapolating_ = false;

    if (render_src_ <= a.src_ms) {
        out = a;  // before the first frame: hold it
    } else if (frames_.size() >= 2) {
        const Frame& b = frames_[1];
        float t = (float)((render_src_ - a.src_ms) / ((double)b.src_ms - a.src_ms));
        out.id = b.id;
        out.src_ms = (uint32_t)render_src_;
        out.position = lerp(a.position, b.position, t);
        out.velocity = lerp(a.velocity, b.velocity, t);
        out.facing = lerp(a.facing, b.facing, t);
        out.has_morphs = false;
        out.morphs.clear();
        out.vitals = a.vitals;  // the game state of the frame being shown

        if (a.bones.size() == b.bones.size()) {
            out.bones.resize(b.bones.size());
            for (size_t i = 0; i < b.bones.size(); ++i) {
                out.bones[i].rotation = normalize(Slerp(a.bones[i].rotation, b.bones[i].rotation, t));
                out.bones[i].translation = lerp(a.bones[i].translation, b.bones[i].translation, t);
                out.bones[i].scale = a.bones[i].scale + (b.bones[i].scale - a.bones[i].scale) * t;
            }
        } else {
            out.bones = t < 0.5f ? a.bones : b.bones;
        }
    } else {
        // Late: carry on along the last velocity for a short while, then hold
        extrapolating_ = true;
        float dt = (float)(std::min(render_src_ - a.src_ms, kMaxExtrapolateMs) / 1000.0);
        vec3 shift = a.velocity * dt;
        out = a;
        out.position += shift;
        for (Bone& bone : out.bones) {
            bone.translation += shift;
        }
    }

    return true;
}

}  // namespace CharSnapshot
