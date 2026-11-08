//-----------------------------------------------------------------------------
//           Name: frame_timers.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See frame_timers.h.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "frame_timers.h"

#include <Graphics/graphics.h>
#include <Graphics/render_backend.h>
#include <Graphics/Vulkan/vk_backend.h>
#include <Internal/profiler.h>
#include <Internal/config.h>
#include <Logging/logdata.h>
#include <Threading/job_system.h>

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unordered_map>

bool g_frame_timer_cpu_zones = false;  // (Internal/profiler.h: g_on && g_cpu_zones, for PROFILER_ZONE)

namespace FrameTimers {

bool g_on = false;
bool g_cpu_zones = false;
bool g_debug_groups = false;

namespace {

using Clock = std::chrono::steady_clock;

const int kFrames = 4;  // in flight: a slot's results are read when it comes round again

struct Record {
    int zone;
    GLuint q_begin;
    GLuint q_end;
    double cpu_ms;
};

struct Open {
    int zone;  // -1: not timed (only a debug group)
    GLuint q_begin;
    Clock::time_point t0;
    bool group;
    bool cpu_only;
};

struct FrameSlot {
    std::vector<Record> records;
    std::vector<GLuint> queries;
    size_t used = 0;
    bool pending = false;
};

struct Acc {
    double gpu_ns = 0.0;
    double cpu_ms = 0.0;
    double calls = 0.0;
    double cpu_only_ms = 0.0;  // (counted at once, per frame begun: no GPU results to wait for)
    double cpu_only_calls = 0.0;
};

FrameSlot slots[kFrames];
int slot = 0;
std::vector<Open> stack;
std::unordered_map<std::string, int> zone_ids;
std::vector<std::string> zone_paths;
std::vector<int> zone_depths;
std::vector<Acc> acc;
std::unordered_map<uint64_t, int> cpu_zone_keys;  // (parent zone, literal name's address or formatted name's hash) -> zone

std::thread::id render_thread;
bool have_thread = false;

Clock::time_point window_start;
bool window_started = false;
int window_frames = 0;
Clock::time_point last_frame;
bool have_last_frame = false;
double frame_sum_ms = 0.0;
double frame_max_ms = 0.0;
int wall_frames = 0;
int cpu_frames = 0;  // frames begun in the window (the CPU-only zones' divisor)
std::vector<float> frame_list;
int windows_since_log = 0;
Snapshot last;

bool QueriesAvailable() {
    if (UseVulkan()) {  // (Vulkan: the queue's timestamps, stage 5.6)
        return VkBackend::TimestampValidBits() > 0;
    }
    return GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_timer_query;
}

double Ms(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

GLuint NextQuery(FrameSlot& s) {
    if (s.used == s.queries.size()) {
        size_t had = s.queries.size();
        s.queries.resize(had == 0 ? 256 : had * 2);
        // (Overgrowth Online stage 5.0: the queries through Graphics, one at a time)
        for (size_t i = had; i < s.queries.size(); ++i) {
            s.queries[i] = Graphics::Instance()->CreateQuery(Graphics::kTimestampQuery);
        }
    }
    return s.queries[s.used++];
}

// Digit runs become one '#': per-frame and per-object names add up
std::string Collapse(const char* name) {
    std::string out;
    bool digits = false;
    for (const char* c = name; *c; ++c) {
        if (isdigit((unsigned char)*c)) {
            if (!digits) {
                out += '#';
            }
            digits = true;
        } else {
            out += *c;
            digits = false;
        }
    }
    return out;
}

int ParentZone() {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (it->zone >= 0) {
            return it->zone;
        }
    }
    return -1;
}

int ZoneId(const std::string& name) {
    int parent = ParentZone();

    std::string path = parent < 0 ? name : zone_paths[parent] + "/" + name;
    auto found = zone_ids.find(path);
    if (found != zone_ids.end()) {
        return found->second;
    }

    int id = (int)zone_paths.size();
    zone_ids[path] = id;
    zone_paths.push_back(path);
    zone_depths.push_back(parent < 0 ? 0 : zone_depths[parent] + 1);
    acc.emplace_back();
    return id;
}

// An old frame's results, if the GPU has them all (never waits; else dropped)
void Collect(FrameSlot& s) {
    if (!s.pending) {
        return;
    }
    s.pending = false;

    if (s.records.empty()) {
        return;
    }

    Graphics* graphics = Graphics::Instance();
    if (!graphics->QueryAvailable(s.records.back().q_end)) {
        return;
    }

    for (const Record& r : s.records) {
        uint64_t a = graphics->QueryResult(r.q_begin);
        uint64_t b = graphics->QueryResult(r.q_end);
        Acc& z = acc[r.zone];
        z.gpu_ns += b > a ? double(b - a) : 0.0;
        z.cpu_ms += r.cpu_ms;
        z.calls += 1.0;
    }
    ++window_frames;
}

void CloseWindow(Clock::time_point now) {
    Snapshot snap;
    snap.window_s = std::chrono::duration<double>(now - window_start).count();
    snap.frames = window_frames;
    snap.frame_ms = wall_frames > 0 ? frame_sum_ms / wall_frames : 0.0;
    snap.frame_ms_max = frame_max_ms;
    snap.frame_list = frame_list;
    snap.worker_busy_ms = wall_frames > 0 ? Jobs::TakeWorkerBusyMs() / wall_frames : 0.0;
    if (!frame_list.empty()) {
        std::vector<float> sorted = frame_list;
        size_t at = std::min(sorted.size() - 1, (size_t)(sorted.size() * 0.99));
        std::nth_element(sorted.begin(), sorted.begin() + at, sorted.end());
        snap.frame_ms_p99 = sorted[at];
    }

    std::vector<int> order;
    for (int i = 0; i < (int)acc.size(); ++i) {
        if (acc[i].calls > 0.0 || acc[i].cpu_only_calls > 0.0) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [](int x, int y) { return zone_paths[x] < zone_paths[y]; });

    for (int i : order) {
        Zone z;
        z.path = zone_paths[i];
        z.depth = zone_depths[i];
        z.gpu_ms = acc[i].gpu_ns / 1.0e6 / window_frames;
        z.cpu_ms = acc[i].cpu_ms / window_frames;
        z.calls = acc[i].calls / window_frames;
        if (acc[i].cpu_only_calls > 0.0 && cpu_frames > 0) {
            z.cpu_ms += acc[i].cpu_only_ms / cpu_frames;
            z.calls += acc[i].cpu_only_calls / cpu_frames;
            z.cpu_only = acc[i].calls == 0.0;
        }
        snap.zones.push_back(z);
    }
    last = snap;

    for (Acc& a : acc) {
        a = Acc();
    }
    window_frames = 0;
    wall_frames = 0;
    cpu_frames = 0;
    frame_list.clear();
    frame_sum_ms = 0.0;
    frame_max_ms = 0.0;
    window_start = now;

    // The log, every ten seconds: the frame and the zones near the top
    if (++windows_since_log >= 10) {
        windows_since_log = 0;
        std::string line = "Frame timers: frame " + std::to_string(snap.frame_ms).substr(0, 5) + " ms (max " +
                           std::to_string(snap.frame_ms_max).substr(0, 5) + ")";
        for (const Zone& z : snap.zones) {
            if (z.depth <= 3 && z.gpu_ms + z.cpu_ms >= 0.05) {
                std::string leaf = z.path.substr(z.path.find_last_of('/') + 1);
                line += " | " + leaf + " gpu " + std::to_string(z.gpu_ms).substr(0, 5) + " cpu " + std::to_string(z.cpu_ms).substr(0, 5);
            }
        }
        LOGI << line << std::endl;
    }
}

}  // namespace

bool OnRenderThread() {
    return have_thread && std::this_thread::get_id() == render_thread;
}

void Init() {
    render_thread = std::this_thread::get_id();
    have_thread = true;
    g_debug_groups = config["gl_debug_groups"].toNumber<bool>() && (UseVulkan() || GLAD_GL_KHR_debug);  // (Vulkan: labels)
    SetCpuZones(config["frame_timers_cpu"].toNumber<bool>());
    SetEnabled(config["frame_timers"].toNumber<bool>());
}

void SetEnabled(bool on) {
    if (on && !QueriesAvailable()) {
        LOGW << "Frame timers: no timer queries (GL 3.3 / ARB_timer_query)" << std::endl;
        on = false;
    }
    if (on && !g_on) {
        window_started = false;
        have_last_frame = false;
        for (FrameSlot& s : slots) {
            s.records.clear();
            s.used = 0;
            s.pending = false;
        }
    }
    g_on = on;
    g_frame_timer_cpu_zones = g_on && g_cpu_zones;
    LOGI << "Frame timers " << (on ? "on" : "off") << std::endl;
}

void SetCpuZones(bool on) {
    g_cpu_zones = on;
    g_frame_timer_cpu_zones = g_on && g_cpu_zones;
}

void BeginFrame() {
    if (!g_on) {
        return;
    }

    Clock::time_point now = Clock::now();
    if (have_last_frame) {
        double ms = Ms(now - last_frame);
        frame_sum_ms += ms;
        frame_max_ms = std::max(frame_max_ms, ms);
        frame_list.push_back((float)ms);
        ++wall_frames;
    }
    last_frame = now;
    have_last_frame = true;

    // The slot we reuse held the frame kFrames - 1 ago
    slot = (slot + 1) % kFrames;
    FrameSlot& s = slots[slot];
    Collect(s);
    s.records.clear();
    s.used = 0;
    s.pending = true;
    stack.clear();  // (a zone left open by an early return)

    if (!window_started) {
        window_started = true;
        window_start = now;
    } else if (std::chrono::duration<double>(now - window_start).count() >= 1.0 && window_frames > 0) {
        CloseWindow(now);
    }
    ++cpu_frames;  // (this frame's update comes after its draw: counted in this window)
}

void Begin(const char* name) {
    std::string collapsed = Collapse(name);
    Open o;
    o.zone = -1;
    o.q_begin = 0;
    o.group = g_debug_groups;
    o.cpu_only = false;

    if (o.group) {
        Graphics::Instance()->PushDebugGroup(collapsed.c_str());
    }
    if (g_on) {
        o.zone = ZoneId(collapsed);
        o.q_begin = NextQuery(slots[slot]);
        Graphics::Instance()->WriteTimestamp(o.q_begin);
        o.t0 = Clock::now();
    }
    stack.push_back(o);
}

void End() {
    if (stack.empty()) {
        return;
    }

    Open o = stack.back();
    stack.pop_back();

    if (o.zone >= 0 && g_on) {
        FrameSlot& s = slots[slot];
        GLuint q_end = NextQuery(s);
        Graphics::Instance()->WriteTimestamp(q_end);
        s.records.push_back({o.zone, o.q_begin, q_end, Ms(Clock::now() - o.t0)});
    }
    if (o.group) {
        Graphics::Instance()->PopDebugGroup();
    }
}

int BeginCpu(const char* key, const char* name) {
    int parent = ParentZone();
    int zone;
    uint64_t k;
    if (key != nullptr) {
        k = ((uint64_t)(uintptr_t)key) ^ ((uint64_t)(uint32_t)(parent + 1) << 48);
    } else {
        // A formatted name, its digit runs collapsed in place, by its hash (FNV-1a)
        char collapsed[192];
        size_t n = 0;
        bool digits = false;
        for (const char* c = name; *c && n + 1 < sizeof(collapsed); ++c) {
            if (isdigit((unsigned char)*c)) {
                if (!digits) {
                    collapsed[n++] = '#';
                }
                digits = true;
            } else {
                collapsed[n++] = *c;
                digits = false;
            }
        }
        collapsed[n] = 0;
        k = 14695981039346656037ull;
        for (size_t i = 0; i < n; ++i) {
            k = (k ^ (unsigned char)collapsed[i]) * 1099511628211ull;
        }
        k = (k ^ (uint64_t)(uint32_t)(parent + 1)) * 1099511628211ull;
    }
    auto found = cpu_zone_keys.find(k);
    if (found != cpu_zone_keys.end()) {
        zone = found->second;
    } else {
        zone = ZoneId(Collapse(name));
        cpu_zone_keys[k] = zone;
    }

    Open o;
    o.zone = zone;
    o.q_begin = 0;
    o.group = false;
    o.cpu_only = true;
    o.t0 = Clock::now();
    stack.push_back(o);
    return (int)stack.size();
}

void EndCpu(int depth) {
    // (only the zone this one opened: a frame's start in between dropped it)
    if ((int)stack.size() != depth || !stack.back().cpu_only) {
        return;
    }
    Open o = stack.back();
    stack.pop_back();
    if (g_on) {
        Acc& z = acc[o.zone];
        z.cpu_only_ms += Ms(Clock::now() - o.t0);
        z.cpu_only_calls += 1.0;
    }
}

Snapshot Last() {
    return last;
}

void DrawOverlay() {
    if (!g_on || !config["frame_timers_overlay"].toNumber<bool>()) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Frame timers")) {
        ImGui::Text("Frame %.2f ms (max %.2f), %d frames timed in %.1f s", last.frame_ms, last.frame_ms_max, last.frames, last.window_s);
        ImGui::Separator();
        ImGui::Columns(4, "frame_timers_columns");
        ImGui::Text("Zone");
        ImGui::NextColumn();
        ImGui::Text("GPU ms");
        ImGui::NextColumn();
        ImGui::Text("CPU ms");
        ImGui::NextColumn();
        ImGui::Text("Calls");
        ImGui::NextColumn();
        for (const Zone& z : last.zones) {
            std::string leaf = z.path.substr(z.path.find_last_of('/') + 1);
            ImGui::Text("%*s%s", z.depth * 2, "", leaf.c_str());
            ImGui::NextColumn();
            ImGui::Text("%.3f", z.gpu_ms);
            ImGui::NextColumn();
            ImGui::Text("%.3f", z.cpu_ms);
            ImGui::NextColumn();
            ImGui::Text("%.1f", z.calls);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
    ImGui::End();
}

}  // namespace FrameTimers

// PROFILER_GPU_ZONE (Internal/profiler.h): cheap when nothing is on
FrameTimerZone::FrameTimerZone(const char* fmt, ...) : active(false) {
    if ((!FrameTimers::g_on && !FrameTimers::g_debug_groups) || !FrameTimers::OnRenderThread()) {
        return;
    }

    char name[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(name, sizeof(name), fmt, args);
    va_end(args);
    FrameTimers::Begin(name);
    active = true;
}

FrameTimerZone::~FrameTimerZone() {
    if (active) {
        FrameTimers::End();
    }
}

// PROFILER_ZONE: a CPU-only zone (the game update), on the render thread while
// the timers and their CPU zones are on. A name without arguments is looked up by
// its address. Returns the stack depth (0: not timed)
int FrameTimerCpuZone::Open(const char* fmt, ...) {
    if (!FrameTimers::OnRenderThread()) {
        return 0;
    }
    if (strchr(fmt, '%') == nullptr) {
        return FrameTimers::BeginCpu(fmt, fmt);
    }
    char name[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(name, sizeof(name), fmt, args);
    va_end(args);
    return FrameTimers::BeginCpu(nullptr, name);
}

void FrameTimerEndCpuZone(int depth) {
    FrameTimers::EndCpu(depth);
}
