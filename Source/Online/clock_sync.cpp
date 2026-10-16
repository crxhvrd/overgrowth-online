//-----------------------------------------------------------------------------
//           Name: clock_sync.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See clock_sync.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "clock_sync.h"

#include <algorithm>
#include <chrono>
#include <vector>

double OnlineClockMs() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double, std::milli>(steady_clock::now() - start).count();
}

namespace {

double Median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }

    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

void ClockSync::AddSample(double t0, double t1, double t2) {
    double rtt = t2 - t0;

    if (rtt < 0.0) {
        return;  // a clock went backwards: unusable
    }

    samples_.push_back({rtt, t1 - 0.5 * (t0 + t2)});

    while ((int)samples_.size() > kMaxSamples) {
        samples_.pop_front();
    }
}

double ClockSync::MinRtt() const {
    double best = 0.0;

    for (size_t i = 0; i < samples_.size(); ++i) {
        if (i == 0 || samples_[i].rtt < best) {
            best = samples_[i].rtt;
        }
    }

    return best;
}

double ClockSync::Offset() const {
    // Only the quick round trips: a slow one was queued somewhere, on one
    // leg or the other, which skews its offset by up to half the extra time
    double limit = MinRtt() * 1.25 + 2.0;
    std::vector<double> offsets;

    for (const Sample& s : samples_) {
        if (s.rtt <= limit) {
            offsets.push_back(s.offset);
        }
    }

    return Median(offsets);
}

double ClockSync::Rtt() const {
    std::vector<double> rtts;

    for (const Sample& s : samples_) {
        rtts.push_back(s.rtt);
    }

    return Median(rtts);
}
