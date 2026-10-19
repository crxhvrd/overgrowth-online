//-----------------------------------------------------------------------------
//           Name: crashreport.h
//      Developer: Wolfire Games LLC
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#pragma once

#include <string>

int RunWithCrashReport(int argc, char* argv[], int (*func)(int, char*[]));

// Overgrowth Online (Windows): a crash on any thread, abort(), a pure virtual
// call or a CRT invalid parameter writes a minidump, a summary (exception,
// module + offset) and a copy of the log to this folder, then Windows takes
// the crash as before (its Event Viewer record stays). UTF-8 paths. Until it
// is called the reports go to %TEMP%\OvergrowthOnline.
void CrashReportSetFolder(const std::string& folder, const std::string& log_path);
// (Again) the process's unhandled-exception filter: a library may replace it
void CrashReportInstall();
// Test bridge only: crash on purpose ("thread", "main", "abort", "stack")
void CrashReportTestCrash(const std::string& kind);
