//-----------------------------------------------------------------------------
//           Name: frame_timers.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: GPU and CPU time of the engine's PROFILER_GPU_ZONE zones per
//                 frame, switched on at run time (config "frame_timers", the
//                 test bridge's "perf"): a GL_TIMESTAMP pair per zone, read
//                 three frames later (never waiting on the GPU), CPU time
//                 beside it, averaged over one-second windows. Digits in zone
//                 names collapse ("Draw frame 812", "movement_object 1234
//                 draw"), so per-object zones add up by type. With
//                 "gl_debug_groups" the zones are also KHR_debug groups
//                 (RenderDoc, Nsight). Stage 0 of docs_gl46_plan.md.
//                 PROFILER_ZONE zones (the game update) are CPU-only zones in
//                 the same tree: phase 0 of docs_perf_plan.md.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace FrameTimers {

struct Zone {
    std::string path;  // "Draw frame #/Draw scene/Draw color pass"
    int depth = 0;
    double gpu_ms = 0.0;  // per frame, the zone and what it holds
    double cpu_ms = 0.0;
    double calls = 0.0;     // per frame
    bool cpu_only = false;  // (PROFILER_ZONE: no GPU time)
};

struct Snapshot {
    double window_s = 0.0;
    int frames = 0;           // frames whose GPU times arrived
    double frame_ms = 0.0;    // wall time from one frame to the next
    double frame_ms_max = 0.0;
    double frame_ms_p99 = 0.0;      // the slowest 1% start here ("1% low" = 1000 / this)
    double worker_busy_ms = 0.0;    // the job system's workers, all together, per frame
    std::vector<float> frame_list;  // every frame's wall time in the window, ms
    std::vector<Zone> zones;        // tree order
};

extern bool g_on;
extern bool g_cpu_zones;  // PROFILER_ZONE timed too (config "frame_timers_cpu", default off: ~0.35 ms on a 3 ms frame; the bridge's "perf")
extern bool g_debug_groups;

void SetCpuZones(bool on);

void Init();  // after the GL context: the config's settings
void SetEnabled(bool on);
void BeginFrame();  // Engine::Draw: the results of three frames ago, the window
void Begin(const char* name);
void End();
// A CPU-only zone; `key` (a literal name's address) skips the name's formatting
// and lookup after the first time. Returns the stack depth for EndCpu
int BeginCpu(const char* key, const char* name);
void EndCpu(int depth);
bool OnRenderThread();
Snapshot Last();     // the last full one-second window
void DrawOverlay();  // ImGui, with "frame_timers_overlay"

}  // namespace FrameTimers
