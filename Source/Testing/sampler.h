//-----------------------------------------------------------------------------
//           Name: sampler.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: A sampling profiler for the main thread, for tests (the test
//                 bridge's "profile"): another thread pauses the main thread
//                 about once a millisecond, reads its native call stack (the
//                 exe's unwind tables) and the call stack of the script it runs
//                 (AngelScript), and lets it go; names come afterwards, on the
//                 main thread (DbgHelp and the PDB, the script modules). Phase 0
//                 of docs_perf_plan.md. Windows only; inert until started.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <JSON/json.h>

class asIScriptContext;

namespace Sampler {

void Init();  // on the main thread, once

// The scripts running on the main thread, innermost last (ASContext::run)
void PushScript(asIScriptContext* ctx);
void PopScript();

bool Start(double seconds, double interval_ms);  // false: running already, or unsupported
bool Running();
// When done: the hottest functions, native and script, self and inclusive.
// On the main thread, with the script modules sampled still loaded
// focus: a native function's name (a part of it); also who called it and in which script
// function, for the samples it was in ("focus_callers", "focus_scripts")
Json::Value Result(int top, const std::string& focus = std::string());

}  // namespace Sampler
