//-----------------------------------------------------------------------------
//           Name: clock_sync_test.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Unit tests for ClockSync (Online/clock_sync.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <Online/clock_sync.h>

#include <tut/tut.hpp>

#include <cmath>

namespace tut {
struct dataclocksync {
    int placeholder;
};

typedef test_group<dataclocksync> tg_clock;
tg_clock test_group_clock("ClockSync");

typedef tg_clock::object clocksync;

// Remote clock = local + offset; one-way delays out and back
static void Exchange(ClockSync& c, double t0, double offset, double out_ms, double back_ms) {
    double t1 = t0 + out_ms + offset;  // remote time when it replies
    double t2 = t0 + out_ms + back_ms;  // local time when the pong arrives
    c.AddSample(t0, t1, t2);
}

template <>
template <>
void clocksync::test<1>() {
    ClockSync c;
    ensure("empty", !c.HasEstimate());
    Exchange(c, 100.0, 1000.0, 20.0, 20.0);
    ensure("has estimate", c.HasEstimate());
    ensure("symmetric offset exact", std::fabs(c.Offset() - 1000.0) < 1e-6);
    ensure("rtt", std::fabs(c.Rtt() - 40.0) < 1e-6);
    ensure("remote to local", std::fabs(c.RemoteToLocal(1500.0) - 500.0) < 1e-6);
    ensure("local to remote", std::fabs(c.LocalToRemote(500.0) - 1500.0) < 1e-6);
}

template <>
template <>
void clocksync::test<2>() {
    // Queueing on one leg skews single samples; the quick ones win
    ClockSync c;
    double t = 0.0;

    for (int i = 0; i < 12; ++i, t += 500.0) {
        double extra_out = (i % 3 == 0) ? 0.0 : 30.0 + i;  // two in three are queued outbound
        Exchange(c, t, -250.0, 15.0 + extra_out, 15.0);
    }

    ensure("offset from the quick samples", std::fabs(c.Offset() - (-250.0)) < 0.5);
    ensure("min rtt", std::fabs(c.MinRtt() - 30.0) < 1e-6);
}

template <>
template <>
void clocksync::test<3>() {
    // A negative round trip (clock went backwards) is ignored
    ClockSync c;
    c.AddSample(100.0, 5000.0, 90.0);
    ensure("rejected", !c.HasEstimate());
}

template <>
template <>
void clocksync::test<4>() {
    // The window keeps the newest samples: a changed offset takes over
    ClockSync c;

    for (int i = 0; i < 40; ++i) {
        Exchange(c, i * 100.0, i < 20 ? 100.0 : 300.0, 10.0, 10.0);
    }

    ensure("window size", c.Samples() == ClockSync::kMaxSamples);
    ensure("newest offset", std::fabs(c.Offset() - 300.0) < 1e-6);
}

template <>
template <>
void clocksync::test<5>() {
    // The online clock never goes backwards
    double a = OnlineClockMs();
    double b = OnlineClockMs();
    ensure("monotonic", b >= a);
}
}  // namespace tut
