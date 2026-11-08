//-----------------------------------------------------------------------------
//           Name: sampler.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See sampler.h.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "sampler.h"

#include <Scripting/angelscript/ascontext.h>
#include <Logging/logdata.h>

#include <angelscript.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

namespace Sampler {

namespace {

const int kNativeFrames = 48;
const int kScriptFrames = 8;
const int kScripts = 32;

struct Sample {
    unsigned char native_n = 0;
    unsigned char script_n = 0;
    uint64_t native[kNativeFrames];
    asIScriptFunction* script_fn[kScriptFrames];
    int script_line[kScriptFrames];
    asIScriptContext* script_ctx;
};

// Written by the main thread only; read by the sampler while the main thread is paused
asIScriptContext* volatile scripts[kScripts];
volatile int script_depth = 0;

std::vector<Sample> samples;  // sized before sampling starts: no allocation while the main thread is paused
std::atomic<int> sample_count(0);
std::atomic<bool> running(false);
std::thread worker;
double sampled_seconds = 0.0;
double sample_interval_ms = 1.0;

#ifdef _WIN32
DWORD main_thread_id = 0;
HANDLE main_thread = nullptr;

// The main thread's native stack, by the unwind tables (RtlVirtualUnwind); a
// stack that can't be read ends the walk
int WalkNative(CONTEXT* c, uint64_t* out) {
    int n = 0;
    __try {
        while (n < kNativeFrames && c->Rip != 0) {
            out[n++] = c->Rip;
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(c->Rip, &image_base, nullptr);
            if (entry == nullptr) {  // (a leaf function: the return address is on top)
                c->Rip = *(DWORD64*)c->Rsp;
                c->Rsp += 8;
            } else {
                void* handler_data = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c->Rip, entry, c, &handler_data, &establisher, nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// The innermost script running (its stack holds the scripts that called it). The
// main thread may be paused while it grows that stack: a read that fails ends it
void TakeScript(Sample& s) {
    s.script_n = 0;
    s.script_ctx = nullptr;
    int depth = script_depth;
    if (depth <= 0 || depth > kScripts) {
        return;
    }
    __try {
        asIScriptContext* ctx = scripts[depth - 1];
        if (ctx != nullptr && ctx->GetState() == asEXECUTION_ACTIVE) {
            s.script_ctx = ctx;
            asUINT size = ctx->GetCallstackSize();
            for (asUINT level = 0; level < size && s.script_n < kScriptFrames; ++level) {
                asIScriptFunction* fn = ctx->GetFunction(level);
                if (fn != nullptr) {  // (null: where a nested call's state was pushed)
                    s.script_fn[s.script_n] = fn;
                    s.script_line[s.script_n] = ctx->GetLineNumber(level);
                    ++s.script_n;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// A function read from a paused thread may be torn: only one the engine knows by its id
bool KnownFunction(asIScriptFunction* fn) {
    __try {
        asIScriptEngine* engine = fn->GetEngine();
        return engine != nullptr && engine->GetFunctionById(fn->GetId()) == fn;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Take(Sample& s) {
    CONTEXT c;
    memset(&c, 0, sizeof(c));
    c.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(main_thread, &c)) {
        s.native_n = 0;
    } else {
        s.native_n = (unsigned char)WalkNative(&c, s.native);
    }

    TakeScript(s);
}

void Run(double seconds, double interval_ms) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    const double end_s = seconds;

    while (sample_count < (int)samples.size()) {
        QueryPerformanceCounter(&now);
        double t = double(now.QuadPart - start.QuadPart) / freq.QuadPart;
        if (t >= end_s) {
            break;
        }
        if (SuspendThread(main_thread) != (DWORD)-1) {
            Take(samples[sample_count]);
            ResumeThread(main_thread);
            ++sample_count;
        }
        if (timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(interval_ms * 10000.0);  // (100 ns units, relative)
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, 100);
        } else {
            Sleep(1);
        }
    }
    QueryPerformanceCounter(&now);
    sampled_seconds = double(now.QuadPart - start.QuadPart) / freq.QuadPart;
    if (timer != nullptr) {
        CloseHandle(timer);
    }
    running = false;
}

std::string NativeName(uint64_t address, std::unordered_map<uint64_t, std::string>& cache) {
    auto found = cache.find(address);
    if (found != cache.end()) {
        return found->second;
    }
    std::string name;
    char buffer[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
    memset(buffer, 0, sizeof(buffer));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
        name = symbol->Name;
    } else {
        HMODULE module = nullptr;
        char path[MAX_PATH] = "";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)address, &module) &&
            GetModuleFileNameA(module, path, MAX_PATH)) {
            const char* base = strrchr(path, '\\');
            name = std::string("[") + (base != nullptr ? base + 1 : path) + "]";
        } else {
            name = "[unknown]";
        }
    }
    cache[address] = name;
    return name;
}
#endif

std::string ScriptName(asIScriptFunction* fn, std::map<asIScriptFunction*, std::string>& cache) {
    auto found = cache.find(fn);
    if (found != cache.end()) {
        return found->second;
    }
    std::string name = fn->GetDeclaration(true, true, false);
    cache[fn] = name;
    return name;
}

std::string ScriptLine(asIScriptContext* ctx, int line) {
    ASContext* owner = ctx != nullptr ? (ASContext*)ctx->GetUserData(0) : nullptr;
    if (owner == nullptr || line <= 0) {
        return "?";
    }
    LineFile corrected = owner->module.GetCorrectedLine(line);
    std::string file = corrected.file.GetOriginalPathStr();
    size_t slash = file.find_last_of("/\\");
    return (slash == std::string::npos ? file : file.substr(slash + 1)) + ":" + std::to_string(corrected.line_number);
}

Json::Value Ranked(const std::map<std::string, int>& counts, int total, int top) {
    std::vector<std::pair<int, std::string>> order;
    for (const auto& it : counts) {
        order.push_back({it.second, it.first});
    }
    std::sort(order.begin(), order.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    Json::Value out(Json::arrayValue);
    for (int i = 0; i < (int)order.size() && i < top; ++i) {
        Json::Value v;
        v["name"] = order[i].second;
        v["samples"] = order[i].first;
        v["percent"] = total > 0 ? 100.0 * order[i].first / total : 0.0;
        out.append(v);
    }
    return out;
}

}  // namespace

void Init() {
#ifdef _WIN32
    main_thread_id = GetCurrentThreadId();
    main_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, main_thread_id);
#endif
}

void PushScript(asIScriptContext* ctx) {
    int depth = script_depth;
    if (depth < kScripts) {
        scripts[depth] = ctx;
    }
    script_depth = depth + 1;  // (after the slot: a pause in between sees the one before)
}

void PopScript() {
    if (script_depth > 0) {
        script_depth = script_depth - 1;
    }
}

bool Start(double seconds, double interval_ms) {
#ifdef _WIN32
    if (running || main_thread == nullptr || seconds <= 0.0) {
        return false;
    }
    if (worker.joinable()) {
        worker.join();
    }
    interval_ms = std::max(0.25, std::min(50.0, interval_ms));
    sample_interval_ms = interval_ms;
    samples.assign((size_t)(seconds * 1000.0 / interval_ms) + 64, Sample());
    sample_count = 0;
    sampled_seconds = 0.0;
    running = true;
    worker = std::thread(Run, seconds, interval_ms);
    LOGI << "Sampler: " << seconds << " s of the main thread, every " << interval_ms << " ms" << std::endl;
    return true;
#else
    (void)seconds;
    (void)interval_ms;
    return false;
#endif
}

bool Running() {
    return running;
}

Json::Value Result(int top) {
    Json::Value out;
    out["running"] = (bool)running;
#ifdef _WIN32
    if (running) {
        return out;
    }
    if (worker.joinable()) {
        worker.join();
    }

    static bool symbols = false;
    if (!symbols) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE || GetLastError() == ERROR_INVALID_PARAMETER;
    }

    std::unordered_map<uint64_t, std::string> native_cache;
    std::map<asIScriptFunction*, std::string> script_cache;
    std::map<std::string, int> native_self, native_incl, script_self, script_incl, script_lines;
    int total = sample_count;
    int in_script = 0;

    for (int i = 0; i < total; ++i) {
        const Sample& s = samples[i];
        std::set<std::string> seen;
        for (int f = 0; f < s.native_n; ++f) {
            std::string name = NativeName(s.native[f], native_cache);
            if (f == 0) {
                ++native_self[name];
            }
            if (seen.insert(name).second) {
                ++native_incl[name];
            }
        }
        if (s.script_n == 0) {
            continue;
        }
        ++in_script;
        std::set<std::string> seen_script;
        for (int f = 0; f < s.script_n; ++f) {
            if (!KnownFunction(s.script_fn[f])) {
                break;
            }
            std::string name = ScriptName(s.script_fn[f], script_cache);
            if (f == 0) {
                ++script_self[name];
                ++script_lines[name + " @ " + ScriptLine(s.script_ctx, s.script_line[0])];
            }
            if (seen_script.insert(name).second) {
                ++script_incl[name];
            }
        }
    }

    out["samples"] = total;
    out["seconds"] = sampled_seconds;
    out["interval_ms"] = sample_interval_ms;
    out["in_script_percent"] = total > 0 ? 100.0 * in_script / total : 0.0;
    out["native_self"] = Ranked(native_self, total, top);
    out["native_inclusive"] = Ranked(native_incl, total, top);
    out["script_self"] = Ranked(script_self, total, top);
    out["script_inclusive"] = Ranked(script_incl, total, top);
    out["script_lines"] = Ranked(script_lines, total, top);  // (the innermost frame; a line is where it last called out)
#endif
    return out;
}

}  // namespace Sampler
