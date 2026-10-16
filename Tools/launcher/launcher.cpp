//-----------------------------------------------------------------------------
//           Name: launcher.cpp
//    Description: Overgrowth Online launcher. Installed as Overgrowth.exe in the
//                 game folder (the stock exe kept as Overgrowth_stock.exe), so
//                 everything Steam starts - Play, an accepted invite
//                 ("+connect_lobby <id>"), Join Game in the friends list -
//                 starts Overgrowth Online instead, with the same arguments.
//
//                 - OvergrowthOnline\OvergrowthOnline.exe, working dir its folder,
//                   write dir Documents\Wolfire\OvergrowthOnline (its own caches
//                   and settings, never mixed with stock 1.4's).
//                 - Shift held at start, "-stock", or no Overgrowth Online:
//                   the stock game, exactly as before.
//                 - Waits for the game, so Steam shows the player in game.
//
//                 Build: Tools\launcher\build.bat (MSVC, no dependencies).
//-----------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <string>

static std::wstring Dir(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

// (0 if it isn't there)
static unsigned long long FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    return ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The arguments we were started with, without our own name
static std::wstring Arguments() {
    const wchar_t* cmd = GetCommandLineW();
    bool quoted = false;

    while (*cmd != 0 && (quoted || (*cmd != L' ' && *cmd != L'\t'))) {
        if (*cmd == L'"') {
            quoted = !quoted;
        }
        ++cmd;
    }
    while (*cmd == L' ' || *cmd == L'\t') {
        ++cmd;
    }
    return cmd;
}

// Without our own switch and Steam's lobby join (the stock game's parser
// refuses arguments it doesn't know)
static std::wstring ForStock(const std::wstring& args) {
    std::wstring out;
    size_t i = 0;
    int skip_next = 0;

    while (i < args.size()) {
        while (i < args.size() && (args[i] == L' ' || args[i] == L'	')) {
            ++i;
        }
        size_t start = i;
        bool quoted = false;
        while (i < args.size() && (quoted || (args[i] != L' ' && args[i] != L'	'))) {
            if (args[i] == L'"') {
                quoted = !quoted;
            }
            ++i;
        }
        std::wstring token = args.substr(start, i - start);
        if (token.empty()) {
            continue;
        }
        if (skip_next > 0) {
            --skip_next;
            continue;
        }
        if (token == L"-stock") {
            continue;
        }
        if (token == L"+connect_lobby") {
            skip_next = 1;
            continue;
        }
        out += (out.empty() ? L"" : L" ") + token;
    }
    return out;
}

static std::wstring Quote(const std::wstring& s) {
    // A trailing backslash would escape the closing quote
    std::wstring t = s;
    while (!t.empty() && (t.back() == L'\\' || t.back() == L'/')) {
        t.pop_back();
    }
    return L"\"" + t + L"/\"";
}

static int Run(const std::wstring& exe, const std::wstring& args, const std::wstring& work_dir) {
    std::wstring line = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(exe.c_str(), &line[0], nullptr, nullptr, FALSE, 0, nullptr, work_dir.c_str(), &si, &pi)) {
        std::wstring msg = L"Could not start\n" + exe;
        MessageBoxW(nullptr, msg.c_str(), L"Overgrowth", MB_OK | MB_ICONERROR);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, self, MAX_PATH * 4);
    std::wstring dir = Dir(self);
    std::wstring args = Arguments();
    std::wstring stock = dir + L"\\Overgrowth_stock.exe";
    std::wstring online_dir = dir + L"\\OvergrowthOnline";
    std::wstring online = online_dir + L"\\OvergrowthOnline.exe";

    bool want_stock = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 || (L" " + args + L" ").find(L" -stock ") != std::wstring::npos;

    if (want_stock || !FileExists(online)) {
        // The game is several MB; a launcher (ours, any version) never runs as
        // the stock game: it would start itself again and again
        if (FileSize(stock) < 2ull * 1024 * 1024) {
            MessageBoxW(nullptr, L"The original Overgrowth.exe is missing. In Steam: Overgrowth > Properties > Installed Files > "
                                 L"Verify integrity of game files, then run the Overgrowth Online installer again.",
                        L"Overgrowth", MB_OK | MB_ICONERROR);
            return 1;
        }
        return Run(stock, ForStock(args), dir);
    }

    // Our own write dir: Documents\Wolfire\OvergrowthOnline
    std::wstring write_dir;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        write_dir = std::wstring(docs) + L"\\Wolfire\\OvergrowthOnline";
        CoTaskMemFree(docs);
    }

    std::wstring ours = L"--working-dir " + Quote(online_dir);
    if (!write_dir.empty()) {
        ours += L" --write-dir " + Quote(write_dir);
    }
    return Run(online, ours + (args.empty() ? L"" : L" " + args), online_dir);
}
