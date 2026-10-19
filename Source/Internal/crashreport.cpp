//-----------------------------------------------------------------------------
//           Name: crashreport.cpp
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
#include "crashreport.h"

#include <Internal/filesystem.h>
#include <Internal/common.h>
#include <Internal/error.h>

#include <Utility/strings.h>
#include <Version/version.h>
#include <Logging/logdata.h>
#include <Compat/fileio.h>
#include <Scripting/angelscript/ascrashdump.h>

void DumpScenegraphState();

#if PLATFORM_LINUX
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#if PLATFORM_MACOSX
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#endif

#if PLATFORM_WINDOWS
#include <tchar.h>
#include <strsafe.h>
#endif

#if BREAKPAD && PLATFORM_MACOSX
#include "client/mac/handler/exception_handler.h"
#endif
#if BREAKPAD && PLATFORM_LINUX
#include "client/linux/handler/exception_handler.h"
#endif
#if BREAKPAD && PLATFORM_WINDOWS
#include "client/windows/handler/exception_handler.h"
#endif

#include <sstream>

/*
 * The following is the old windows minidump routine, currently replaced with breakpad
#if PLATFORM_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tchar.h>
#include <strsafe.h>
#include <string>
#include "tinyxml.h"
#include "Version/version.h"
#include "Internal/filesystem.h"
#include "Logging/logdata.h"

int GenerateDump(EXCEPTION_POINTERS* pExceptionPointers) {
    // Get temp directory path
    TCHAR szPath[MAX_PATH];
    DWORD dwBufferSize = MAX_PATH;
    GetTempPath( dwBufferSize, szPath );
    // Create a folder in the temp directory with the name of the application
    TCHAR szFileName[MAX_PATH];
    TCHAR szFileNameDir[MAX_PATH];
    LPTSTR szAppName = _T("Overgrowth");
    StringCchPrintf( szFileName, MAX_PATH, _T("%s%s"), szPath, szAppName );
    StringCchPrintf( szFileNameDir, MAX_PATH, _T("%s%s"), szPath, szAppName );
    CreateDirectory( szFileName, NULL );
    // Create an informative file name including the application name and date of error
    SYSTEMTIME stLocalTime;
    GetLocalTime( &stLocalTime );
    StringCchPrintf( szFileName, MAX_PATH, _T("%s%s\\%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp"),
                szPath, szAppName, TEXT(GetShortBuildTag().c_str()),
        stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
        stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
        GetCurrentProcessId(), GetCurrentThreadId());
    // Open the file and write the minidump to it
    HANDLE hDumpFile = CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    MINIDUMP_EXCEPTION_INFORMATION ExpParam;
    ExpParam.ThreadId = GetCurrentThreadId();
    ExpParam.ExceptionPointers = pExceptionPointers;
    ExpParam.ClientPointers = TRUE;
    BOOL bMiniDumpSuccessful = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
        hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
        // Copy logfile.txt to same location as minidump
        LogSystem::Flush();
        StringCchPrintf(szFileName, MAX_PATH, _T("%s%s\\logfile.txt"), szPath, szAppName);
        TCHAR szLogFileName[MAX_PATH];
        StringCchPrintf(szLogFileName, MAX_PATH, _T("%slogfile.txt"), GetWritePath(CoreGameModID).c_str());
        CopyFile(szLogFileName, szFileName, false);
    // Display error message with instructions
    MessageBox(NULL,
            "A fatal error occured! Please send the most recent .dmp and logfile.txt files to bugs@wolfire.com, along with the what version of the game you were playing and a description of what exactly was happening in the game when this error occured.",
            "Fatal Error",
            MB_OK |
            MB_ICONHAND |
            MB_TOPMOST |
            MB_SETFOREGROUND);
    // In Windows Explorer, open the folder containing the .dmp
    std::string nav_command = "explorer "+std::string(szFileNameDir);
    system(nav_command.c_str());
    return EXCEPTION_EXECUTE_HANDLER;
}
#elif defined(PLATFORM_LINUX) && defined(__GNUG__) //We use GNUG for the backtrace

#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <Version/version.h>
#include "unix_compat.h"

#include "Internal/filesystem.h"
#include "Logging/logdata.h"


const static bool do_coredump_on_crash = false;

//Coredump routine crashes on linux.
//General signal handler for anything that usually cause a coredump
static void segfault_coredump( int sig )
{
    char time_string[256];
    char final_folder[256];
    time_t t = time(NULL);
    struct tm *l_t = localtime(&t);

    //Reset meaning for later.
    signal( sig, SIG_DFL );

    char full_message[2048];
    snprintf( full_message, 2048, "A fatal error occured!" );

    LOGF << "Changing directory for core dump" << std::endl; //Change dir for the core dump
    chdir(GetWritePath(CoreGameModID).c_str());

    strftime( time_string, 256, "%y-%m-%d_%H-%M-%S_%z", l_t );
    snprintf( final_folder, 256, "crash_%s_%s_%s", GetBuildVersion(), GetBuildIDString(), time_string );
    mkdir( final_folder, S_IRWXU );
    chdir( final_folder );

    //Get a backtrace
    {
        void *array[10];
        size_t size;

        // get void*'s for all entries on the stack
        size = backtrace(array, 10);

        int backtrace_file = creat( "backtrace", S_IRUSR | S_IWUSR );
        // print out all the frames to stderr
        LOGF << "Error: signal " <<  sig << std::endl;
        backtrace_symbols_fd(array, size, backtrace_file);
    }

    snprintf( full_message, 2048,
        "A fatal error occured! Please send the most recent crash folder \"%s%s\" to bugs@wolfire.com, along with a description of what exactly was happening in the game when this error occured.",
        GetWritePath(CoreGameModID).c_str(),
        final_folder );


    LOGF << "Game crashed, flushing the log systems." << std::endl;
    LogSystem::Flush();

    MessageBox(NULL,
            full_message,
            "Fatal Error",
            MB_OK |
            MB_ICONHAND |
            MB_TOPMOST |
            MB_SETFOREGROUND);


    //We resend ourselves the signal so the default handler will dump the core. and get the correct exit code.
    kill(getpid(),sig);
}

int RunWithCrashReport( int argc, char* argv[], int (*func) (int, char*[])) {
    #if PLATFORM_WINDOWS && !defined(_DEBUG) && defined(_DEPLOY)

        __try
        {
            return func(argc, argv);
        }
        __except(GenerateDump(GetExceptionInformation()))
        {
            LogSystem::Flush();
            return 1;
        }
}
*/

void GetCrashFolder(char* destpath, size_t maxsize) {
    char time_string[256];
#if PLATFORM_LINUX || PLATFORM_MACOSX
    time_t t = time(NULL);
    struct tm* l_t = localtime(&t);
    strftime(time_string, 256, "%y-%m-%d_%H-%M-%S", l_t);
#elif PLATFORM_WINDOWS
    SYSTEMTIME stLocalTime;
    GetLocalTime(&stLocalTime);
    FormatString(time_string, 256, "%04d-%02d-%02d_%02d-%02d-%02d",
                 stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
                 stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);
#endif
    FormatString(destpath, maxsize, "crash_%s_%s_%s", GetBuildVersion(), GetBuildIDString(), time_string);
}

#if BREAKPAD
#if PLATFORM_LINUX
static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded) {
    char tempPath[kPathSize];
    char sourcePath[kPathSize];
    char destPath[kPathSize];

    char crashFolder[kPathSize];

    LOGF << "Game crashed, flushing the log systems. DumpFile: " << descriptor.path() << std::endl;
    DumpScenegraphState();
    DumpAngelscriptStates();
    LogSystem::Flush();

    GetCrashFolder(crashFolder, kPathSize);

    // Create crash folder.
    FormatString(destPath, kPathSize, "%s/%s", GetWritePath(CoreGameModID).c_str(), crashFolder);
    mkdir(destPath, S_IRUSR | S_IWUSR | S_IXUSR);

    // Get the name of the dump.
    FormatString(tempPath, kPathSize, "%s", descriptor.path());
    char* last_component = tempPath;
    for (unsigned i = 0; i < strlen(tempPath) - 1; i++) {
        if (tempPath[i] == '/')
            last_component = &tempPath[i + 1];
    }

    // Copy the dump into the crash folder.
    FormatString(sourcePath, kPathSize, "%s", descriptor.path());
    FormatString(destPath, kPathSize, "%s/%s/%s", GetWritePath(CoreGameModID).c_str(), crashFolder, last_component);

    LOGF << sourcePath << " " << destPath << std::endl;
    LogSystem::Flush();
    copyfile(sourcePath, destPath);

    GetLogfilePath(sourcePath, kPathSize);
    FormatString(destPath, kPathSize, "%s/%s/logfile.txt", GetWritePath(CoreGameModID).c_str(), crashFolder);
    copyfile(sourcePath, destPath);

    GetHWReportPath(sourcePath, kPathSize);
    FormatString(destPath, kPathSize, "%s/%s/hwreport.txt", GetWritePath(CoreGameModID).c_str(), crashFolder);
    copyfile(sourcePath, destPath);

    GetScenGraphDumpPath(sourcePath, kPathSize);
    FormatString(destPath, kPathSize, "%s/%s/scene_dump.txt", GetWritePath(CoreGameModID).c_str(), crashFolder);
    copyfile(sourcePath, destPath);

    GetASDumpPath(sourcePath, kPathSize);
    FormatString(destPath, kPathSize, "%s/%s/as_dump.txt", GetWritePath(CoreGameModID).c_str(), crashFolder);
    copyfile(sourcePath, destPath);

    FormatString(destPath, kPathSize, "%s/%s/version_info.txt", GetWritePath(CoreGameModID).c_str(), crashFolder);
    FILE* f = my_fopen(destPath, "a");

    if (f) {
        fwrite(GetBuildIDString(), strlen(GetBuildIDString()), 1, f);
        fwrite("\n", 1, 1, f);
        fwrite(GetPlatform(), strlen(GetPlatform()), 1, f);
        fwrite("\n", 1, 1, f);
        fwrite(GetArch(), strlen(GetArch()), 1, f);
        fwrite("\n", 1, 1, f);
        fwrite(GetBuildVersion(), strlen(GetBuildVersion()), 1, f);
        fwrite("\n", 1, 1, f);
        fwrite(GetBuildTimestamp(), strlen(GetBuildTimestamp()), 1, f);
        fwrite("\n", 1, 1, f);

        fclose(f);
    }

    DisplayError(
        "Fatal Error",
        "A fatal error occured! Please compress the latest crash folder into a .zip and send to bugs@wolfire.com, please include a description of what exactly was happening in the game when this error occured.",
        _ok);

    return succeeded;
}
#elif PLATFORM_MACOSX

static bool minidumpCallback(const char *dump_dir,
                             const char *minidump_id,
                             void *context,
                             bool succeeded) {
    LOGF << "Game crashed, flushing the log systems. DumpFile: " << dump_dir << " " << minidump_id << std::endl;
    DumpScenegraphState();
    DumpAngelscriptStates();
    LogSystem::Flush();
}

static bool filterCallback(void *context) {
    return true;
}

#elif PLATFORM_WINDOWS
static bool dumpCallback(const wchar_t* dump_path,
                         const wchar_t* minidump_id,
                         void* context,
                         EXCEPTION_POINTERS* exinfo,
                         MDRawAssertionInfo* assertion,
                         bool succeeded) {
    char tempPath[kPathSize];
    char destPath[kPathSize];

    WCHAR szWritePath[kPathSize];
    WCHAR szCrashFolder[kPathSize];
    WCHAR szSource[kPathSize];
    WCHAR szDest[kPathSize];

    LOGF << "Game crashed, flushing the log systems." << std::endl;
    if (succeeded == false) {
        LOGF << "Program failed at generating a minidump" << std::endl;
    }
    DumpScenegraphState();
    DumpAngelscriptStates();
    LogSystem::Flush();

    // Convert write path to windows UTF16 format
    GetWritePath(CoreGameModID, tempPath, kPathSize);
    NormalizePathSeparators(tempPath);  // Ensure write path has correct slashes
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szWritePath, kPathSize);

    // Convert crash folder name to windows UTF16 format
    GetCrashFolder(tempPath, kPathSize);
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szCrashFolder, kPathSize);

    // Create the crash folder.
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s", szWritePath, szCrashFolder);
    CreateDirectoryW(szDest, NULL);

    // Copy the dump into the crash folder
    StringCchPrintfW(szSource, kPathSize, L"%s\\%s.dmp", dump_path, minidump_id);
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\%s.dmp", szWritePath, szCrashFolder, minidump_id);
    MoveFileW(szSource, szDest);

    // Copy the logfile into the crash folder
    GetLogfilePath(tempPath, kPathSize);
    NormalizePathSeparators(tempPath);
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szSource, kPathSize);
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\logfile.txt", szWritePath, szCrashFolder);
    CopyFileW(szSource, szDest, FALSE);

    // Copy the hwreport into the crash folder
    GetHWReportPath(tempPath, kPathSize);
    NormalizePathSeparators(tempPath);
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szSource, kPathSize);
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\hwreport.txt", szWritePath, szCrashFolder);
    CopyFileW(szSource, szDest, FALSE);

    // Write version information to file
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\version_info.txt", szWritePath, szCrashFolder);
    HANDLE versionfile = CreateFileW(szDest, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

    // Copy the scene_dump.txt into the crash folder
    GetScenGraphDumpPath(tempPath, kPathSize);
    NormalizePathSeparators(tempPath);
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szSource, kPathSize);
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\scene_dump.txt", szWritePath, szCrashFolder);
    CopyFileW(szSource, szDest, FALSE);

    // Copy the as_dump.txt into the crash folder
    GetASDumpPath(tempPath, kPathSize);
    NormalizePathSeparators(tempPath);
    MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, szSource, kPathSize);
    StringCchPrintfW(szDest, kPathSize, L"%s\\%s\\as_dump.txt", szWritePath, szCrashFolder);
    CopyFileW(szSource, szDest, FALSE);

    if (versionfile != INVALID_HANDLE_VALUE) {
        DWORD os;
        WriteFile(versionfile, GetBuildIDString(), strlen(GetBuildIDString()), &os, NULL);
        WriteFile(versionfile, "\r\n", 2, &os, NULL);
        WriteFile(versionfile, GetPlatform(), strlen(GetPlatform()), &os, NULL);
        WriteFile(versionfile, "\r\n", 2, &os, NULL);
        WriteFile(versionfile, GetArch(), strlen(GetArch()), &os, NULL);
        WriteFile(versionfile, "\r\n", 2, &os, NULL);
        WriteFile(versionfile, GetBuildVersion(), strlen(GetBuildVersion()), &os, NULL);
        WriteFile(versionfile, "\r\n", 2, &os, NULL);
        WriteFile(versionfile, GetBuildTimestamp(), strlen(GetBuildTimestamp()), &os, NULL);
        WriteFile(versionfile, "\r\n", 2, &os, NULL);
        CloseHandle(versionfile);
    }

    DisplayError(
        "Fatal Error",
        "A fatal error occured! Please compress the latest crash folder into a .zip and send to bugs@wolfire.com, please include a description of what exactly was happening in the game when this error occured.",
        _ok);

    // In Windows Explorer, open the folder containing the .dmp
    GetWritePath(CoreGameModID, tempPath, kPathSize);
    NormalizePathSeparators(tempPath);
    FormatString(destPath, kPathSize, "explorer %s", tempPath);
    system(destPath);

    return succeeded;
}

static bool filterCallback(void* context, EXCEPTION_POINTERS* exinfo,
                           MDRawAssertionInfo* assertion) {
    return true;
}
#endif
#endif

// ---- Overgrowth Online: crash reports on Windows --------------------------------
// The old minidump routine above is commented out and breakpad is off, so a
// crash used to leave nothing: no dump, no box, and the log's last lines still
// in its buffer. Everything here runs once the process is already broken: no
// heap, no locks, static buffers, and the dump is written by a fresh thread
// (the crashed one may have no stack left). Not catchable in-process: heap
// corruption and /GS fast-fails (0xC0000374, 0xC0000409) end the process
// directly; Windows still records those (Event Viewer > Application, 1000).
#if PLATFORM_WINDOWS && !BREAKPAD
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <csignal>
#include <cstdlib>
#include <atomic>

namespace {
typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

MiniDumpWriteDumpFn g_write_dump = nullptr;
wchar_t g_folder[MAX_PATH * 2] = L"";
wchar_t g_log_path[MAX_PATH * 2] = L"";
DWORD g_main_thread = 0;
std::atomic<DWORD> g_crashing_thread{0};
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;
_invalid_parameter_handler g_previous_invalid_parameter = nullptr;

// One report at a time (static buffers: the crashed thread may be out of stack)
wchar_t g_dir[MAX_PATH * 2];
wchar_t g_base[MAX_PATH * 2];
wchar_t g_file[MAX_PATH * 2 + 16];
wchar_t g_module_path[MAX_PATH];
char g_module_name[MAX_PATH * 3];
char g_text[2048];
char g_line[1024];

struct DumpJob {
    EXCEPTION_POINTERS* ep;
    DWORD thread_id;
    BOOL ok;
};
DumpJob g_job;

void ToWide(const std::string& s, wchar_t* out, int size) {
    out[0] = L'\0';
    if (MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, size) == 0) {
        out[0] = L'\0';
    }
    for (wchar_t* c = out; *c; ++c) {
        if (*c == L'/') {
            *c = L'\\';
        }
    }
}

DWORD WINAPI DumpThread(LPVOID) {
    StringCchPrintfW(g_file, MAX_PATH * 2 + 16, L"%s.dmp", g_base);
    HANDLE file = CreateFileW(g_file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE || g_write_dump == nullptr) {
        return 0;
    }
    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = g_job.thread_id;
    info.ExceptionPointers = g_job.ep;
    info.ClientPointers = FALSE;
    // Stacks, the memory they point at, thread and module lists: a few MB (fits a chat upload)
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory |
                                         MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    g_job.ok = g_write_dump(GetCurrentProcess(), GetCurrentProcessId(), file, type, g_job.ep ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
    return 0;
}

void WriteText(const wchar_t* path, const char* text, bool append) {
    HANDLE file = CreateFileW(path, append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              append ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, text, (DWORD)strlen(text), &written, nullptr);
        CloseHandle(file);
    }
}

void WriteReport(EXCEPTION_POINTERS* ep, const char* kind) {
    DWORD me = GetCurrentThreadId();
    DWORD none = 0;
    if (!g_crashing_thread.compare_exchange_strong(none, me)) {
        if (none != me) {
            Sleep(INFINITE);  // another thread is reporting; it ends the process
        }
        return;  // a crash inside the report: leave it to Windows
    }

    // Where: <folder>\crash_<date>-<time>_<pid>
    if (g_folder[0] != L'\0') {
        StringCchCopyW(g_dir, MAX_PATH * 2, g_folder);
    } else {
        GetTempPathW(MAX_PATH, g_dir);
        StringCchCatW(g_dir, MAX_PATH * 2, L"OvergrowthOnline\\");
    }
    CreateDirectoryW(g_dir, nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    StringCchPrintfW(g_base, MAX_PATH * 2, L"%scrash_%04d%02d%02d-%02d%02d%02d_%lu", g_dir, t.wYear, t.wMonth, t.wDay,
                     t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());

    // The dump, from a fresh thread with its own stack
    g_job.ep = ep;
    g_job.thread_id = me;
    g_job.ok = FALSE;
    HANDLE dumper = CreateThread(nullptr, 256 * 1024, DumpThread, nullptr, 0, nullptr);
    if (dumper != nullptr) {
        WaitForSingleObject(dumper, 60000);
        CloseHandle(dumper);
    } else {
        DumpThread(nullptr);
    }

    // What and where, readable without symbols (the module tells a driver crash from ours)
    DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    void* address = ep ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    HMODULE module = nullptr;
    StringCchCopyW(g_module_path, MAX_PATH, L"?");
    if (address != nullptr &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &module)) {
        GetModuleFileNameW(module, g_module_path, MAX_PATH);
    }
    const wchar_t* slash = wcsrchr(g_module_path, L'\\');
    WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : g_module_path, -1, g_module_name, sizeof(g_module_name), nullptr, nullptr);
    unsigned long long offset = module ? (unsigned long long)((char*)address - (char*)module) : 0ULL;
    StringCchPrintfA(g_line, sizeof(g_line), "%s: exception 0x%08lX in %s+0x%llX, %sthread %lu", kind, code, g_module_name,
                     offset, me == g_main_thread ? "main " : "", me);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        size_t used = strlen(g_line);
        StringCchPrintfA(g_line + used, sizeof(g_line) - used, ", %s 0x%llX", op == 0 ? "reading" : (op == 1 ? "writing" : "executing"),
                         (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    StringCchPrintfA(g_text, sizeof(g_text), "Overgrowth Online crash report\r\nBuild: %s_%s %s\r\n%s\r\nDump written: %s\r\n",
                     GetBuildVersion(), GetBuildIDString(), GetBuildTimestamp(), g_line, g_job.ok ? "yes" : "no");
    StringCchPrintfW(g_file, MAX_PATH * 2 + 16, L"%s.txt", g_base);
    WriteText(g_file, g_text, false);

    // The log's last line says so too (each line is already on disk), and a copy
    // goes with the dump (the next start trims the log)
    if (g_log_path[0] != L'\0') {
        StringCchPrintfA(g_text, sizeof(g_text), "%04d/%02d/%02d %02d:%02d:%02d [f][__]: crashreport.cpp: Overgrowth Online crashed: %s\r\n",
                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, g_line);
        WriteText(g_log_path, g_text, true);
        StringCchPrintfW(g_file, MAX_PATH * 2 + 16, L"%s_log.txt", g_base);
        CopyFileW(g_log_path, g_file, FALSE);
    }
}

// Where the caller stands, as an exception record (abort and friends have none)
__declspec(noinline) void ReportHere(const char* kind) {
    CONTEXT context;
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0xE0000001;  // ours: not a hardware exception
#if defined(_M_X64)
    record.ExceptionAddress = (void*)context.Rip;
#endif
    EXCEPTION_POINTERS pointers = {&record, &context};
    WriteReport(&pointers, kind);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
    WriteReport(ep, "unhandled exception");
    return g_previous_filter ? g_previous_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void AbortSignal(int) {
    ReportHere("abort");  // (then the CRT finishes abort: Windows records it)
}

void __cdecl PureCall() {
    ReportHere("pure virtual call");
    abort();
}

void __cdecl InvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
    ReportHere("CRT invalid parameter");
    if (g_previous_invalid_parameter != nullptr) {
        g_previous_invalid_parameter(expression, function, file, line, reserved);
    }
    __fastfail(FAST_FAIL_INVALID_ARG);  // what the CRT's own handler does
}

DWORD WINAPI CrashingThread(LPVOID) {
    *(volatile int*)nullptr = 1;
    return 0;
}

#pragma warning(suppress : 4717)  // (on purpose: the stack overflow test)
__declspec(noinline) int Recurse(int n) {
    volatile char pad[4096];
    pad[0] = (char)n;
    return Recurse(n + 1) + pad[0];
}
}  // namespace

void CrashReportInstall() {
    if (g_write_dump == nullptr) {
        // Loaded now: a broken process may not manage LoadLibrary
        HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        if (dbghelp != nullptr) {
            g_write_dump = (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump");
        }
        g_main_thread = GetCurrentThreadId();
        ULONG reserve = 64 * 1024;  // room for the filter after a stack overflow
        SetThreadStackGuarantee(&reserve);
        signal(SIGABRT, AbortSignal);
        _set_purecall_handler(PureCall);
        g_previous_invalid_parameter = _set_invalid_parameter_handler(InvalidParameter);
    }
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(UnhandledFilter);
    if (previous != UnhandledFilter) {
        g_previous_filter = previous;
    }
}

void CrashReportSetFolder(const std::string& folder, const std::string& log_path) {
    ToWide(folder, g_folder, MAX_PATH * 2);
    ToWide(log_path, g_log_path, MAX_PATH * 2);
}

void CrashReportTestCrash(const std::string& kind) {
    if (kind == "thread") {
        HANDLE h = CreateThread(nullptr, 0, CrashingThread, nullptr, 0, nullptr);
        if (h != nullptr) {
            WaitForSingleObject(h, INFINITE);
        }
    } else if (kind == "main") {
        *(volatile int*)nullptr = 1;
    } else if (kind == "abort") {
        abort();
    } else if (kind == "stack") {
        Recurse(0);
    }
}
#else
void CrashReportInstall() {}
void CrashReportSetFolder(const std::string&, const std::string&) {}
void CrashReportTestCrash(const std::string&) {}
#endif

int RunWithCrashReport(int argc, char* argv[], int (*func)(int, char*[])) {
#if !BREAKPAD
    CrashReportInstall();
#endif
#if BREAKPAD
#if PLATFORM_LINUX
    google_breakpad::MinidumpDescriptor descriptor(P_tmpdir);
    google_breakpad::ExceptionHandler eh(descriptor, NULL, dumpCallback, NULL, true, -1);
#elif PLATFORM_MACOSX
    // google_breakpad::ExceptionHandler eh( "/tmp/", filterCallback, minidumpCallback, NULL, true, NULL );
#elif PLATFORM_WINDOWS

    fprintf(stderr, "Registering things\n");
    std::wstring dumpPathStr = L".\\";

    wchar_t dumpPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, dumpPath))
        dumpPathStr = std::wstring(dumpPath);

    google_breakpad::ExceptionHandler* eh = new google_breakpad::ExceptionHandler(
        dumpPathStr,
        filterCallback,
        dumpCallback,
        0,
        google_breakpad::ExceptionHandler::HANDLER_ALL,
        MiniDumpNormal,
        L"",
        0);
#endif
    // crash();
#endif
    return func(argc, argv);

#if BREAKPAD
#if PLATFORM_WINDOWS
    delete eh;
#endif
#endif
}
