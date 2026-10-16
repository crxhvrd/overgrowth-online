//-----------------------------------------------------------------------------
//           Name: clock_sync.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Per-peer clock offset and round trip from ping/pong time
//                 stamps (NTP style). Each side stamps with its own
//                 OnlineClockMs() (a steady clock, not frame time):
//                   t0 = ping sent (local), t1 = pong sent (remote),
//                   t2 = pong received (local)
//                   rtt = t2 - t0, offset = t1 - (t0 + t2) / 2 (remote - local)
//                 The estimate is the median offset of the recent samples
//                 with the shortest round trips (queueing only adds delay).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <deque>

double OnlineClockMs();

class ClockSync {
   public:
    static const int kMaxSamples = 16;

    void AddSample(double t0, double t1, double t2);
    void Reset() { samples_.clear(); }

    bool HasEstimate() const { return !samples_.empty(); }
    int Samples() const { return (int)samples_.size(); }

    double Offset() const;  // remote clock minus local clock, ms
    double Rtt() const;     // median round trip of the recent samples, ms
    double MinRtt() const;

    double RemoteToLocal(double remote_ms) const { return remote_ms - Offset(); }
    double LocalToRemote(double local_ms) const { return local_ms + Offset(); }

   private:
    struct Sample {
        double rtt;
        double offset;
    };

    std::deque<Sample> samples_;  // oldest first
};
