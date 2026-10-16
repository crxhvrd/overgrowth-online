//-----------------------------------------------------------------------------
//           Name: char_snapshot_test.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Unit tests for character snapshots (Online/char_snapshot.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <Online/char_snapshot.h>
#include <Math/vec3math.h>

#include <tut/tut.hpp>

#include <cmath>
#include <cstdlib>

namespace tut {
struct datacharsnapshot {
    int placeholder;
};

typedef test_group<datacharsnapshot> tg_charsnap;
tg_charsnap test_group_charsnap("CharSnapshot");

typedef tg_charsnap::object charsnap;

using namespace CharSnapshot;

static float Rand(float lo, float hi) {
    return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}

static float QuatAngle(const quaternion& a, const quaternion& b) {
    float d = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * std::acos(std::min(1.0f, d));
}

// A character walking along x at speed m/s, 20 bones around it
static Frame Walker(uint32_t src_ms, float speed) {
    Frame f;
    f.id = 42;
    f.src_ms = src_ms;
    f.position = vec3(speed * src_ms / 1000.0f, 1.0f, 0.0f);
    f.velocity = vec3(speed, 0.0f, 0.0f);
    f.facing = vec3(1.0f, 0.0f, 0.0f);
    f.bones.resize(20);

    for (int b = 0; b < 20; ++b) {
        f.bones[b].translation = f.position + vec3(0.0f, b * 0.05f - 0.5f, 0.0f);
        f.bones[b].rotation = quaternion(vec4(0.0f, 1.0f, 0.0f, src_ms * 0.001f));
    }

    return f;
}

template <>
template <>
void charsnap::test<1>() {
    // Smallest three: under a quarter degree anywhere (0.23 worst), q and -q alike
    srand(7);
    float worst = 0.0f;

    for (int i = 0; i < 5000; ++i) {
        quaternion q = normalize(quaternion(Rand(-1, 1), Rand(-1, 1), Rand(-1, 1), Rand(-1, 1)));
        worst = std::max(worst, QuatAngle(q, UnpackQuat(PackQuat(q))));
        quaternion n(-q[0], -q[1], -q[2], -q[3]);
        worst = std::max(worst, QuatAngle(q, UnpackQuat(PackQuat(n))));
    }

    ensure("rotation error", worst < 0.0045f);
    ensure("degenerate packs as identity", QuatAngle(quaternion(), UnpackQuat(PackQuat(quaternion(0, 0, 0, 0)))) < 1e-3f);
}

template <>
template <>
void charsnap::test<2>() {
    // Round trip of a full record, then a truncated one
    Frame f = Walker(1234, 4.0f);
    f.bones[3].scale = 1.5f;
    f.bones[5].present = false;  // a connector bone without a body: junk at the origin
    f.bones[5].translation = vec3(0.0f);
    f.has_morphs = true;
    f.morphs.resize(5);
    for (int m = 0; m < 5; ++m) {
        f.morphs[m].index = (uint8_t)(m * 3);  // sparse: only some morphs travel
    }
    f.morphs[2].disp_weight = 0.75f;
    f.morphs[4].mod_weight = 2.5f;

    f.vitals.valid = true;
    f.vitals.knocked_out = 1;
    f.vitals.state = 4;
    f.vitals.flags = kVitalOnGround | kVitalBlocking;
    f.vitals.temp_health = -0.25f;
    f.vitals.blood_health = 0.5f;
    f.vitals.target_id = 12345;
    f.vitals.block_stunned = 0.5f;
    f.vitals.attacked_by = 4321;
    f.vitals.slots[1] = 77;
    f.vitals.slots[3] = 78;

    std::vector<uint8_t> buf;
    Write(buf, f);
    Write(buf, Walker(1300, 4.0f));  // two records back to back

    size_t offset = 0;
    Frame g, h;
    ensure("read 1", Read(buf.data(), buf.size(), offset, g));
    ensure("read 2", Read(buf.data(), buf.size(), offset, h));
    ensure("consumed", offset == buf.size());
    ensure("id", g.id == 42 && g.src_ms == 1234 && h.src_ms == 1300);
    ensure("position", distance(g.position, f.position) < 1e-5f);
    ensure("velocity", distance(g.velocity, f.velocity) < 0.01f);
    ensure("facing", distance(g.facing, f.facing) < 1e-3f);
    ensure("bones", g.bones.size() == 20);

    ensure("absent bone", !g.bones[5].present && g.bones[4].present);

    for (int b = 0; b < 20; ++b) {
        if (b == 5) {
            continue;
        }
        ensure("bone translation (junk does not stretch the box)", distance(g.bones[b].translation, f.bones[b].translation) < 0.0005f);
        ensure("bone rotation", QuatAngle(g.bones[b].rotation, f.bones[b].rotation) < 0.0045f);
    }

    ensure("scale", std::fabs(g.bones[3].scale - 1.5f) < 1e-6f && g.bones[4].scale == 1.0f);
    ensure("morphs", g.has_morphs && g.morphs.size() == 5 && std::fabs(g.morphs[2].disp_weight - 0.75f) < 0.001f &&
                         std::fabs(g.morphs[4].mod_weight - 2.5f) < 0.001f && g.morphs[4].index == 12);
    ensure("no morphs", !h.has_morphs);
    ensure("vitals", g.vitals.valid && g.vitals.knocked_out == 1 && g.vitals.state == 4 && g.vitals.flags == (kVitalOnGround | kVitalBlocking) &&
                         std::fabs(g.vitals.temp_health + 0.25f) < 0.001f && std::fabs(g.vitals.blood_health - 0.5f) < 0.001f &&
                         g.vitals.target_id == 12345 && std::fabs(g.vitals.block_stunned - 0.5f) < 0.011f &&
                         g.vitals.attacked_by == 4321);
    ensure("weapon slots", g.vitals.slots[1] == 77 && g.vitals.slots[3] == 78 && g.vitals.slots[0] == -1 && g.vitals.slots[2] == -1);
    ensure("no vitals", !h.vitals.valid);
    ensure("compact: 8 bytes a bone", buf.size() < 2 * (50 + 20 * 8) + 40 + 40);  // (+ the first record's vitals)

    for (size_t cut = 0; cut < buf.size() / 2; cut += 7) {
        size_t o = 0;
        Frame t;
        ensure("truncated record fails", !Read(buf.data(), cut, o, t));
    }
}

template <>
template <>
void charsnap::test<3>() {
    // 30 Hz stream, 40 ms latency with 0-20 ms jitter, sampled at 120 Hz:
    // the shown pose follows the true one, a little behind and without jumps
    srand(11);
    Buffer buffer;
    std::vector<std::pair<double, Frame> > in_flight;
    const double offset = 5000.0;  // the owner's clock starts elsewhere
    double worst_error = 0.0, worst_step = 0.0, last_x = -1.0;
    int extrapolated = 0, samples = 0;

    for (double now = 0.0; now < 6000.0; now += 1000.0 / 120.0) {
        uint32_t src = (uint32_t)(now + offset);

        if (((int)(now / (1000.0 / 120.0))) % 4 == 0) {
            in_flight.push_back(std::make_pair(now + 40.0 + Rand(0.0f, 20.0f), Walker(src, 5.0f)));
        }

        for (size_t i = 0; i < in_flight.size();) {
            if (in_flight[i].first <= now) {
                buffer.Push(in_flight[i].second, now);
                in_flight.erase(in_flight.begin() + i);
            } else {
                ++i;
            }
        }

        Frame out;
        if (now > 1000.0 && buffer.Sample(now, out)) {
            double truth = 5.0 * buffer.RenderSrcMs() / 1000.0;
            worst_error = std::max(worst_error, std::fabs(out.position[0] - truth));
            if (last_x >= 0.0) {
                worst_step = std::max(worst_step, std::fabs(out.position[0] - last_x));
            }
            last_x = out.position[0];
            extrapolated += buffer.Extrapolating() ? 1 : 0;
            ++samples;
        }
    }

    ensure("on the true path", worst_error < 0.01);
    ensure("smooth (5 m/s at 120 Hz is 4 cm a step)", worst_step < 0.06);
    ensure("rarely late", extrapolated < samples / 20);
    ensure("delay about jitter + one interval", buffer.DelayMs() > 30.0 && buffer.DelayMs() < 75.0);
}

template <>
template <>
void charsnap::test<4>() {
    // A character stands still (the owner sends nothing), then walks off:
    // it must not glide from the old frame to the new one
    Buffer buffer;
    double now = 0.0;
    Frame still = Walker(1000, 0.0f);
    still.position = vec3(3.0f, 1.0f, 0.0f);

    for (int i = 0; i < 10; ++i, now += 33.0) {
        Frame f = still;
        f.src_ms = 1000 + i * 33;
        buffer.Push(f, now + 30.0);
    }

    Frame out;
    for (; now < 2000.0; now += 8.0) {
        buffer.Sample(now, out);
    }

    ensure("held at the still pose", distance(out.position, still.position) < 0.01f);

    // Moves again: one fresh frame, 1 m away, arriving with the usual lag
    Frame moved = still;
    moved.src_ms = (uint32_t)(now + 970.0);
    moved.position = vec3(4.0f, 1.0f, 0.0f);
    buffer.Push(moved, now);
    buffer.Sample(now + 8.0, out);
    ensure("no glide", distance(out.position, still.position) < 0.35f);
}

template <>
template <>
void charsnap::test<5>() {
    // The stream stops while moving: extrapolate 100 ms at most, then hold
    Buffer buffer;
    double now = 0.0;

    for (int i = 0; i < 30; ++i, now += 33.0) {
        buffer.Push(Walker(i * 33, 5.0f), now + 20.0);
    }

    Frame out;
    for (double t = now; t < now + 1000.0; t += 8.0) {
        buffer.Sample(t, out);
    }

    float last_x = 5.0f * (29 * 33) / 1000.0f;
    ensure("extrapolating", buffer.Extrapolating());
    ensure("capped at 100 ms", std::fabs(out.position[0] - (last_x + 0.5f)) < 0.01f);
    ensure("bones follow", std::fabs(out.bones[0].translation[0] - (last_x + 0.5f)) < 0.01f);
}

template <>
template <>
void charsnap::test<6>() {
    // A chain of four 30 cm bones joined end to end, bent at random: only the
    // root needs a position; a stretched joint sends its bone's position
    srand(3);
    Rig rig;
    rig.links.resize(4);
    for (int b = 0; b < 4; ++b) {
        rig.order.push_back(b);
        if (b > 0) {
            rig.links[b].parent = b - 1;
            rig.links[b].joint_in_parent = vec3(0.0f, 0.15f, 0.0f);
            rig.links[b].joint_in_child = vec3(0.0f, -0.15f, 0.0f);
        }
    }

    std::vector<uint8_t> buf;

    for (int trial = 0; trial < 50; ++trial) {
        Frame f;
        f.id = 7;
        f.position = vec3(Rand(-50, 50), Rand(0, 10), Rand(-50, 50));
        f.bones.resize(4);
        f.bones[0].translation = f.position + vec3(0.0f, 0.2f, 0.0f);

        for (int b = 0; b < 4; ++b) {
            f.bones[b].rotation = normalize(quaternion(Rand(-1, 1), Rand(-1, 1), Rand(-1, 1), Rand(-1, 1)));
            if (b > 0) {
                f.bones[b].translation = PredictFromParent(rig.links[b], f.bones[b - 1].rotation, f.bones[b - 1].translation, f.bones[b].rotation);
            }
        }

        bool stretch = trial % 2 == 1;
        if (stretch) {
            // Bone 2 pulled 5 cm off its joint; bone 3 hangs off bone 2
            vec3 pull(0.05f, 0.0f, 0.0f);
            f.bones[2].translation += pull;
            f.bones[3].translation += pull;
        }

        buf.clear();
        int sent = -1;
        Write(buf, f, &rig, &sent);
        ensure("positions sent", sent == (stretch ? 2 : 1));

        size_t offset = 0;
        Frame g;
        ensure("read", Read(buf.data(), buf.size(), offset, g));
        ensure("derived marked", g.bones[1].derived && g.bones[3].derived && !g.bones[0].derived);
        ensure("resolve", Resolve(g, rig));

        for (int b = 0; b < 4; ++b) {
            ensure("bone where it was", distance(g.bones[b].translation, f.bones[b].translation) < kLinkTolerance + 0.0005f);
        }
    }

    // The last record again, on a machine whose skeleton does not fit
    size_t offset = 0;
    Frame h;
    ensure("read", Read(buf.data(), buf.size(), offset, h));
    ensure("a skeleton that does not fit is refused", !Resolve(h, Rig()));
}
template <>
template <>
void charsnap::test<7>() {
    // Idle: breathing is not worth a record; a step, a turn, a hit or a pickup is
    Frame a = Walker(1000, 0.0f);
    a.vitals.valid = true;
    Frame b = a;
    b.bones[5].translation += vec3(0.0f, 0.004f, 0.0f);
    ensure("breathing (4 mm)", Similar(a, b));
    b.bones[5].translation += vec3(0.0f, 0.02f, 0.0f);
    ensure("a 2 cm move", !Similar(a, b));
    b = a;
    b.bones[3].rotation = quaternion(vec4(0.0f, 1.0f, 0.0f, 1.05f));
    ensure("a 3 degree turn", !Similar(a, b));
    b = a;
    b.vitals.temp_health -= 0.1f;
    ensure("a hit", !Similar(a, b));
    b = a;
    b.vitals.slots[0] = 7;
    ensure("a weapon picked up", !Similar(a, b));
}
template <>
template <>
void charsnap::test<8>() {
    // A far character streamed at 2 Hz while it walks: it glides from frame to
    // frame (no stop and go); marked as resumed after standing still, it holds
    for (int resumed = 0; resumed < 2; ++resumed) {
        Buffer buffer;
        double now = 0.0;
        for (int i = 0; i < 8; ++i) {
            Frame f = Walker(1000 + i * 500, 2.0f);
            f.resumed = resumed == 1 && i == 7;
            buffer.Push(f, now + 30.0);
            now += 500.0;
        }
        // Halfway between the last two frames (as played back): halfway between their spots
        Frame out;
        double t = now - 250.0;
        for (double s = t - 2000.0; s <= t; s += 8.0) {
            buffer.Sample(s, out);
        }
        Frame a = Walker(1000 + 6 * 500, 2.0f);
        Frame b = Walker(1000 + 7 * 500, 2.0f);
        float from_a = distance(out.position, a.position);
        if (resumed == 0) {
            ensure("a slow stream glides", from_a > 0.1f && distance(out.position, b.position) > 0.1f);
        } else {
            ensure("resumed: held, then the new spot", from_a < 0.05f || distance(out.position, b.position) < 0.35f);
        }
    }

    // The flag survives the trip
    std::vector<uint8_t> buf;
    Frame f = Walker(1000, 1.0f);
    f.resumed = true;
    Write(buf, f);
    size_t offset = 0;
    Frame g;
    ensure("read", Read(buf.data(), buf.size(), offset, g));
    ensure("resumed flag", g.resumed);
}
}  // namespace tut
