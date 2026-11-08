//-----------------------------------------------------------------------------
//           Name: job_system.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: A small job system for the game update (stage B1 of
//                 docs_perf_plan.md): a pool of worker threads and a parallel
//                 loop the main thread starts and helps with. Config "jobs":
//                 how many workers (0 = none: every loop runs on the calling
//                 thread; default: hardware threads minus one). What runs in a
//                 job must not call scripts, the asset manager or anything else
//                 that isn't thread-safe; CheckNotInJob guards those and counts
//                 what slips through (the test bridge's "perf": job_violations).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <functional>

namespace Jobs {

void Init(int workers = -1);  // -1: the config's "jobs", else hardware threads minus one
void Shutdown();
int WorkerCount();

// fn(begin, end) over [0, n) in chunks of `grain`, on the workers and the calling
// thread; returns when every chunk is done. One loop at a time, from the main
// thread (the one that called Init); a loop started inside a job, or from another
// thread, runs inline
void ParallelFor(int n, int grain, const std::function<void(int begin, int end)>& fn);

namespace detail {
extern thread_local bool in_job;
bool Violation(const char* what);  // logged once per `what`, counted; false
}  // namespace detail

inline bool InJob() {  // running a loop's chunk (a worker, or the main thread helping)
    return detail::in_job;
}
int ThreadIndex();  // 0: not a worker; 1..WorkerCount(): the workers

// For what must stay out of jobs: false inside one (logged once per `what`, counted).
// Inline: on the hot paths it guards, a thread-local read
inline bool CheckNotInJob(const char* what) {
    return !detail::in_job || detail::Violation(what);
}
int Violations();

double TakeWorkerBusyMs();  // the workers' busy time since the last call

}  // namespace Jobs
