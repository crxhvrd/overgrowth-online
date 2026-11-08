//-----------------------------------------------------------------------------
//           Name: job_system_test.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Unit tests for the job system (Threading/job_system.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <Threading/job_system.h>

#include <tut/tut.hpp>

#include <atomic>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

namespace tut {
struct datajobs {
    int placeholder;
};

typedef test_group<datajobs> tg_jobs;
tg_jobs test_group_jobs("Jobs");

typedef tg_jobs::object jobs;

// Every index once, whatever the size and the chunk
static bool EachOnce(int n, int grain) {
    std::vector<std::atomic<int>> hits(n);
    for (auto& h : hits) {
        h = 0;
    }
    Jobs::ParallelFor(n, grain, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            hits[i].fetch_add(1);
        }
    });
    for (auto& h : hits) {
        if (h != 1) {
            return false;
        }
    }
    return true;
}

template <>
template <>
void jobs::test<1>() {
    set_test_name("every index once, on workers and inline");
    Jobs::Init(4);
    ensure_equals("workers", Jobs::WorkerCount(), 4);
    ensure("n 1", EachOnce(1, 1));
    ensure("n 7 grain 3", EachOnce(7, 3));
    ensure("n 1000 grain 1", EachOnce(1000, 1));
    ensure("n 100000 grain 64", EachOnce(100000, 64));
    ensure("n 5 grain 100 (inline)", EachOnce(5, 100));
    Jobs::Shutdown();

    Jobs::Init(0);  // ("jobs 0": everything on the calling thread)
    ensure_equals("no workers", Jobs::WorkerCount(), 0);
    ensure("inline n 1000", EachOnce(1000, 8));
    Jobs::Shutdown();
}

template <>
template <>
void jobs::test<2>() {
    set_test_name("loop after loop: no chunk of one runs in another");
    Jobs::Init(6);
    bool ok = true;
    for (int round = 0; round < 3000 && ok; ++round) {
        int n = 1 + (round * 37) % 200;
        ok = EachOnce(n, 1 + round % 5);
    }
    ensure("3000 loops back to back", ok);
    Jobs::Shutdown();
}

template <>
template <>
void jobs::test<3>() {
    set_test_name("the workers do part of it; inside a job, InJob; a nested loop runs inline");
    Jobs::Init(4);
    std::set<std::thread::id> threads;
    std::atomic<int> in_job(0), nested(0);
    std::atomic<int64_t> spin(0);
    std::vector<std::thread::id> who(4000);
    Jobs::ParallelFor(4000, 1, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            who[i] = std::this_thread::get_id();
            in_job += Jobs::InJob() ? 1 : 0;
            for (int k = 0; k < 2000; ++k) {  // (some work, so the workers get chunks)
                spin += k;
            }
        }
        Jobs::ParallelFor(3, 1, [&](int b, int e) { nested += e - b; });
    });
    for (const std::thread::id& id : who) {
        threads.insert(id);
    }
    ensure("more than one thread did it", threads.size() > 1);
    ensure_equals("InJob in every chunk", in_job.load(), 4000);
    ensure("not in a job after", !Jobs::InJob());
    ensure_equals("nested loops ran whole", nested.load() % 3, 0);
    Jobs::Shutdown();
}

template <>
template <>
void jobs::test<4>() {
    set_test_name("CheckNotInJob: true outside, false (and counted) inside");
    Jobs::Init(2);
    ensure("outside", Jobs::CheckNotInJob("a unit test"));
    int before = Jobs::Violations();
    Jobs::ParallelFor(8, 1, [&](int, int) { Jobs::CheckNotInJob("a unit test"); });
    ensure_equals("counted", Jobs::Violations() - before, 8);
    Jobs::Shutdown();
}
}  // namespace tut
