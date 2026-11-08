//-----------------------------------------------------------------------------
//           Name: job_system.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See job_system.h.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "job_system.h"

#include <Threading/thread_name.h>
#include <Internal/config.h>
#include <Logging/logdata.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#define JOBS_PAUSE() _mm_pause()
#else
#define JOBS_PAUSE() std::this_thread::yield()
#endif

namespace Jobs {

namespace {

using Clock = std::chrono::steady_clock;

const int kSpins = 4000;          // a worker's busy wait for the next loop before it sleeps (~20-40 us)
const uint32_t kClosed = 0x7fffffffu;

// The loop running now. Chunks are claimed through `cursor`: the loop's generation
// in the high half, the next index in the low half. The main thread closes it (kClosed)
// before it touches the loop's data again, so a worker late for an old loop can't
// claim a chunk of the next one
struct Loop {
    const std::function<void(int, int)>* fn = nullptr;
    int n = 0;
    int grain = 1;
    std::atomic<int> left{0};  // chunks claimed or not that haven't finished
};

std::vector<std::thread> workers;
std::mutex mutex;
std::condition_variable wake;
std::atomic<uint32_t> published{0};  // the generation of the loop to work on
std::atomic<uint64_t> cursor{0};
std::atomic<bool> quitting{false};
Loop loop;
uint32_t generation = 0;  // (main thread)
std::thread::id owner;    // the thread that started the pool: the only one that runs loops on it
std::atomic<int64_t> busy_ns{0};
std::atomic<int> violations{0};
std::mutex violation_mutex;
std::set<std::string> violation_sites;

thread_local int t_index = 0;

uint64_t Pack(uint32_t gen, uint32_t pos) {
    return (uint64_t(gen) << 32) | pos;
}

// Chunks of loop `gen` until none are left (or it isn't the loop running any more)
void RunChunks(uint32_t gen) {
    for (;;) {
        uint64_t c = cursor.load(std::memory_order_acquire);
        uint32_t pos = uint32_t(c);
        if (uint32_t(c >> 32) != gen || pos == kClosed || int(pos) >= loop.n) {
            return;
        }
        if (!cursor.compare_exchange_weak(c, Pack(gen, pos + uint32_t(loop.grain)), std::memory_order_acq_rel)) {
            continue;
        }
        int begin = int(pos);
        int end = std::min(begin + loop.grain, loop.n);
        (*loop.fn)(begin, end);
        loop.left.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void WorkerMain(int index) {
    t_index = index;
    NameCurrentThread(("Jobs " + std::to_string(index)).c_str());
    uint32_t seen = 0;
    for (;;) {
        uint32_t gen = published.load(std::memory_order_acquire);
        for (int spin = 0; gen == seen && spin < kSpins && !quitting.load(std::memory_order_relaxed); ++spin) {
            JOBS_PAUSE();
            gen = published.load(std::memory_order_acquire);
        }
        if (gen == seen) {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [&] { return quitting.load() || published.load() != seen; });
            gen = published.load(std::memory_order_acquire);
        }
        if (quitting.load()) {
            return;
        }
        seen = gen;

        Clock::time_point t0 = Clock::now();
        detail::in_job = true;
        RunChunks(gen);
        detail::in_job = false;
        busy_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count(), std::memory_order_relaxed);
    }
}

}  // namespace

void Init(int count) {
    if (!workers.empty()) {
        Shutdown();
    }
    if (count < 0) {
        int hardware = (int)std::thread::hardware_concurrency();
        count = config.HasKey("jobs") ? config["jobs"].toNumber<int>() : hardware - 1;
        count = std::max(0, std::min(count, std::max(0, hardware - 1)));
    }
    count = std::min(count, 63);
    owner = std::this_thread::get_id();
    quitting = false;
    published = 0;
    generation = 0;
    cursor = Pack(0, kClosed);
    for (int i = 0; i < count; ++i) {
        workers.emplace_back(WorkerMain, i + 1);
    }
    LOGI << "Jobs: " << count << " worker threads" << (count == 0 ? " (every loop on the main thread)" : "") << std::endl;
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        quitting = true;
    }
    wake.notify_all();
    for (std::thread& t : workers) {
        t.join();
    }
    workers.clear();
}

int WorkerCount() {
    return (int)workers.size();
}

void ParallelFor(int n, int grain, const std::function<void(int begin, int end)>& fn) {
    if (n <= 0) {
        return;
    }
    grain = std::max(1, grain);
    if (workers.empty() || detail::in_job || n <= grain || std::this_thread::get_id() != owner) {
        bool was = detail::in_job;
        detail::in_job = true;  // (inline still counts as a job: the guards see the same thing with "jobs 0")
        fn(0, n);
        detail::in_job = was;
        return;
    }

    // The loop's data, then the cursor open for its generation, then the workers told
    loop.fn = &fn;
    loop.n = n;
    loop.grain = grain;
    loop.left.store((n + grain - 1) / grain, std::memory_order_relaxed);
    uint32_t gen = ++generation;
    if (gen == 0) {
        gen = ++generation;  // (0: no loop yet)
    }
    cursor.store(Pack(gen, 0), std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex);
        published.store(gen, std::memory_order_release);
    }
    wake.notify_all();

    // The main thread helps, then waits for chunks still running elsewhere
    detail::in_job = true;
    RunChunks(gen);
    detail::in_job = false;
    for (int spin = 0; loop.left.load(std::memory_order_acquire) > 0; ++spin) {
        if (spin < kSpins) {
            JOBS_PAUSE();
        } else {
            std::this_thread::yield();
        }
    }
    cursor.store(Pack(gen, kClosed), std::memory_order_release);
}

int ThreadIndex() {
    return t_index;
}

namespace detail {

thread_local bool in_job = false;

bool Violation(const char* what) {
    violations.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(violation_mutex);
    if (violation_sites.insert(what).second) {
        LOGE << "Jobs: " << what << " inside a job (thread " << t_index << "): not thread-safe" << std::endl;
    }
    return false;
}

}  // namespace detail

int Violations() {
    return violations.load(std::memory_order_relaxed);
}

double TakeWorkerBusyMs() {
    return busy_ns.exchange(0, std::memory_order_relaxed) / 1.0e6;
}

}  // namespace Jobs
