//-----------------------------------------------------------------------------
//           Name: bullet_jobs.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Bullet's task scheduler over the job system (see bullet_jobs.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "bullet_jobs.h"

#include <Threading/job_system.h>
#include <Internal/config.h>
#include <Logging/logdata.h>

#include <LinearMath/btThreads.h>

#include <vector>

namespace {

// Bullet asks for loops over [iBegin, iEnd) in chunks of grainSize; Jobs::ParallelFor
// runs fn over [0, n) in chunks of `grain` on the workers and the main thread (inline
// when nested, or from another thread, which Bullet's loops then simply are)
class JobsTaskScheduler : public btITaskScheduler {
   public:
    JobsTaskScheduler() : btITaskScheduler("OvergrowthJobs") {}

    int getMaxNumThreads() const override { return Jobs::WorkerCount() + 1; }
    int getNumThreads() const override { return Jobs::WorkerCount() + 1; }
    void setNumThreads(int) override {}  // (the job system's own config)

    void parallelFor(int iBegin, int iEnd, int grainSize, const btIParallelForBody& body) override {
        int n = iEnd - iBegin;
        if (n <= 0) {
            return;
        }
        Jobs::ParallelFor(n, grainSize, [&](int begin, int end) {
            body.forLoop(iBegin + begin, iBegin + end);
        });
    }

    // Each chunk's sum in its own slot, added in order afterwards: the same total
    // whichever thread ran what. (Only Bullet's parallel solver sums; it isn't used)
    btScalar parallelSum(int iBegin, int iEnd, int grainSize, const btIParallelSumBody& body) override {
        int n = iEnd - iBegin;
        if (n <= 0) {
            return btScalar(0);
        }
        int grain = grainSize < 1 ? 1 : grainSize;
        std::vector<btScalar> sums((n + grain - 1) / grain, btScalar(0));
        Jobs::ParallelFor(n, grain, [&](int begin, int end) {
            sums[begin / grain] += body.sumLoop(iBegin + begin, iBegin + end);
        });
        btScalar total = 0;
        for (btScalar s : sums) {
            total += s;
        }
        return total;
    }
};

JobsTaskScheduler& Scheduler() {
    static JobsTaskScheduler* scheduler = new JobsTaskScheduler();  // (never freed: Bullet may ask at exit)
    return *scheduler;
}

bool installed = false;
bool enabled = true;

}  // namespace

namespace BulletJobs {

void ClaimMainThread() {
    btGetCurrentThreadIndex();  // (the first to ask is number 0)
}

bool Install() {
    enabled = !config.HasKey("physics_jobs") || config["physics_jobs"].toBool();
    if (btGetCurrentThreadIndex() != 0) {
        LOGE << "Bullet's threads: another thread asked Bullet first; physics stays single-threaded" << std::endl;
        return false;
    }
    btSetTaskScheduler(&Scheduler());
    installed = btGetTaskScheduler() == &Scheduler();
    LOGI << "Bullet's threads: " << (installed ? "on the job system" : "not installed") << ", "
         << Jobs::WorkerCount() << " workers" << (enabled ? "" : " (turned off: physics_jobs false)") << std::endl;
    return installed;
}

bool Active() {
    return installed && enabled && Jobs::WorkerCount() > 0;
}

}  // namespace BulletJobs
