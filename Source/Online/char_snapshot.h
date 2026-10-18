//-----------------------------------------------------------------------------
//           Name: char_snapshot.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Character state streaming (ownership model). The machine
//                 that owns a character (its player, or the host for NPCs)
//                 captures its pose, packs it into a compact binary record
//                 and streams it; every other machine plays the stream back
//                 through a jitter buffer.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <Math/vec3.h>
#include <Math/quaternions.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace CharSnapshot {

const int kMaxBones = 64;
const int kMaxMorphs = 128;

// One physics bone: its world transform (the bullet object) and display scale
struct Bone {
    quaternion rotation;
    vec3 translation;
    float scale = 1.0f;
    bool present = true;   // has a physics body (connector bones have none: not streamed)
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
                       const quaternion& rotation);

struct Morph {
    uint8_t index = 0;  // into the character's morph targets (same order everywhere)
    float disp_weight = 0.0f;
    float mod_weight = 0.0f;
};

// The owner's game state of a character, for everyone else's scripts (AI,
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
    float block_stunned = 0.0f;  // after a blocked attack: throwable by whoever blocked it
    int32_t block_stunned_by = -1;
    int32_t attacked_by = -1;      // who hit it last (the HUD's damage numbers and kill feed)
    uint8_t primary_slot = 1;      // aschar.as weapon_slots: held left/right, sheathed left/right, scabbards
    int32_t slots[6] = {-1, -1, -1, -1, -1, -1};  // common item ids
};

struct Frame {
    int32_t id = -1;      // common object id (the host's)
    uint32_t src_ms = 0;  // the owner's clock (OnlineClockMs) at capture
    vec3 position;
    vec3 velocity;
    vec3 facing;
    std::vector<Bone> bones;
    bool has_morphs = false;
    std::vector<Morph> morphs;  // only the ones that changed, or all of them now and then
    bool resumed = false;       // the first record after the owner sent nothing because nothing moved
    Vitals vitals;
};

// Record layout, little endian, no keys:
//   int32 id, uint32 src_ms, float pos[3], int16 vel[3] (cm/s), int16 facing[3],
//   uint8 flags (1 = morphs), uint8 bone count,
//   [a bit per bone: present (has a body); for the present ones:
//    the box their positions sit in around pos: int16 min[3], uint16 size[3] (cm);
//    a bit per bone: position sent (else rebuilt from the joint with its parent);
//    per bone: uint32 rotation (smallest three);
//    per bone with a position: uint32 offset in the box (11/11/10 bits x/y/z)],
//   uint8 scaled bone count, per: uint8 index, float scale,
//   [uint8 morph count, per morph: uint8 index, int16 disp, int16 mod (x1000)]
//   [vitals: uint8 knocked_out, state, flags, ko_shield; int16 temp, permanent, blood,
//    block health (x1000); uint8 duck (x255); int32 target; uint8 tethered; int32 tether;
//    uint8 block_stunned (x100 s); int32 block_stunned_by; int32 attacked_by;
//    uint8 primary slot, uint8 filled slots, per: uint8 slot, int32 item]
//   flags byte: 1 = morphs, 2 = vitals, 4 = resumed after standing still
// With a rig, bones whose joints hold go without a position (most of them)
void Write(std::vector<uint8_t>& out, const Frame& f, const Rig* rig = nullptr, int* positions_sent = nullptr);

// Reads the record at offset and advances it. False on malformed data.
// Bones sent without a position are marked derived: call Resolve.
bool Read(const uint8_t* data, size_t size, size_t& offset, Frame& f);

// Rebuilds the derived bones from their parents. False if the rig does not fit.
bool Resolve(Frame& f, const Rig& rig);

// Every number in it finite (a NaN on the machine that sent it stays there:
// shown here, it spreads through the physics to whatever touches it)
bool Finite(const Frame& f);

// Where the record's content starts after the id and timestamp: hash from
// here to tell an unchanged pose (a still character costs nothing)
const size_t kContentOffset = 8;
uint64_t Hash(const uint8_t* data, size_t size);

// Nothing a viewer would see changed: positions within pos_tol (m), rotations
// within rot_tol (radians), the same vitals
bool Similar(const Frame& a, const Frame& b, float pos_tol = 0.01f, float rot_tol = 0.026f);
bool SameVitals(const Vitals& v, const Vitals& w);

uint32_t PackQuat(const quaternion& q);  // 2 bits index + 3 x 10 bits
quaternion UnpackQuat(uint32_t v);

// Playback of one character's stream. Frames are stamped with the owner's
// clock; the buffer learns the fastest path (minimum arrival lag) and the
// jitter on top of it, and plays the stream that far behind, plus one send
// interval, so there is nearly always a frame on both sides of the shown time.
class Buffer {
   public:
    static const int kLagWindow = 64;
    static const int kMaxFrames = 64;
    static constexpr double kHoldGapMs = 250.0;         // a resumed record after a longer gap: it stood still
    static constexpr double kSilenceMs = 1000.0;        // a gap this long counts as standing still anyway
    static constexpr double kMaxExtrapolateMs = 100.0;  // then hold
    static constexpr double kMarginMs = 4.0;

    void Clear();
    void Push(const Frame& f, double arrival_ms);

    // The pose to show at local time now_ms. False until a frame came in.
    // Morphs are not interpolated (applied on arrival).
    bool Sample(double now_ms, Frame& out);

    // Diagnostics
    double RenderSrcMs() const { return render_src_; }  // owner time shown by the last Sample
    double DelayMs() const;                              // playback offset over the fastest path
    double IntervalMs() const;
    bool Extrapolating() const { return extrapolating_; }
    int Size() const { return (int)frames_.size(); }

   private:
    std::deque<Frame> frames_;  // oldest first
    std::deque<double> lags_;   // arrival - src
    std::deque<double> gaps_;   // src deltas between frames

    bool started_ = false;
    bool extrapolating_ = false;
    double play_offset_ = 0.0;  // now - shown src time
    double last_now_ = 0.0;
    double render_src_ = 0.0;

    double Target() const;
};

}  // namespace CharSnapshot
