//-----------------------------------------------------------------------------
//           Name: online_utility.cpp
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
#include <Online/online_utility.h>
#include <Main/engine.h>
#include <Internal/config.h>
#include <Internal/filesystem.h>
#include <algorithm>

#if ENABLE_STEAMWORKS
#include <isteamfriends.h>
#include <Steam/steamworks.h>
#endif

using std::stringstream;

// Name is invalid if (less than 3 letters) or (contains any non alpha numerical characters)
bool OnlineUtility::IsSafeAssetPath(const std::string& path) {
    if (path.size() < 6 || path.size() > 255 || path.compare(0, 5, "Data/") != 0 || path.find("..") != std::string::npos) {
        return false;
    }
    for (char c : path) {
        // Printable, and nothing that ends a script string or leaves the folder
        // tree (a character path goes into a script: no quote, no backslash)
        bool ok = c >= 32 && c < 127 && c != '"' && c != '\\' && c != ':' && c != '<' && c != '>' && c != '|' && c != '?' && c != '*';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool OnlineUtility::IsSafeCharacterPath(const std::string& path) {
    return IsSafeAssetPath(path) && path.size() > 4 && path.compare(path.size() - 4, 4, ".xml") == 0 && FileExists(path, kAnyPath);
}

bool OnlineUtility::IsValidPlayerName(const std::string& name) {
    if (name.length() <= 2 || name.length() > 24)  // (shown over heads, in feeds, in level messages)
        return false;

    for (char c : name) {
        bool valid_char = (48 <= c && c <= 57) || (65 <= c && c <= 90) || (97 <= c && c <= 122) || c == 32;
        if (!valid_char) {
            return false;
        }
    }

    return true;
}

std::string OnlineUtility::GetDefaultPlayerName() {
#if ENABLE_STEAMWORKS
    if (SteamFriends() != nullptr) {
        const char* name_ptr = SteamFriends()->GetPersonaName();
        if (name_ptr != nullptr) {
            std::string default_name(name_ptr);
            if (IsValidPlayerName(default_name)) {
                return default_name;
            }
        }
    }
#endif

    // Steam name isn't valid, use fallback
    return "Unknown Rabbit";
}

std::string OnlineUtility::GetPlayerName() {
    std::string name = config.GetRef("playername").str();
#if ENABLE_STEAMWORKS
    // Overgrowth Online: the Steam name, unless one is set in the config
    if ((name.empty() || name == "UNKNOWN" || name == "Player") && Steamworks::Instance()->IsConnected() &&
        SteamFriends() != nullptr) {
        name = SteamFriends()->GetPersonaName();
    }
#endif
    return name;
}

// Overgrowth Online: the stock machine translations only change text, and the
// game turns them back on at every start: they don't count for online play
static bool CountsOnline(ModInstance* mod) {
    return mod->IsActive() && !mod->IsCore() && mod->id.str() != "com-wolfire-machine-translations";
}

// The host compares this string with the one a joining client sends, so it must
// not depend on the order the mods were found in: Steam Workshop mods are added
// as their UGC callbacks arrive, which differs from one machine to the next
// (two players with the same subscriptions were turned away). Sorted by id.
std::string OnlineUtility::GetActiveModsString() {
    vector<std::string> entries;
    for (ModInstance* mod : ModLoading::Instance().GetAllMods()) {
        if (CountsOnline(mod)) {
            if (mod->SupportsOnline()) {
                entries.push_back(mod->id.str());
            } else {
                entries.push_back("[" + mod->id.str() + "]");
            }
        }
    }
    std::sort(entries.begin(), entries.end());

    stringstream modlist;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            modlist << ", ";
        }
        modlist << entries[i];
    }
    return modlist.str();
}

// Overgrowth Online: the version of the package, which every package gives
// its mod (mod.xml); players must have the same
std::string OnlineUtility::GetOvergrowthOnlineVersion() {
    for (ModInstance* mod : ModLoading::Instance().GetAllMods()) {
        if (mod->IsActive() && mod->id.str() == "crxhvrd-overhaul") {
            return mod->version.str();
        }
    }
    return "";
}

// "0.34.10" after "0.34.9"; "" (no version) before any
int OnlineUtility::CompareVersions(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') {
            x = x * 10 + (isdigit((unsigned char)a[i]) ? a[i] - '0' : 0);
            ++i;
        }
        while (j < b.size() && b[j] != '.') {
            y = y * 10 + (isdigit((unsigned char)b[j]) ? b[j] - '0' : 0);
            ++j;
        }
        if (x != y) {
            return x < y ? -1 : 1;
        }
        ++i;
        ++j;
    }
    return a.empty() == b.empty() ? 0 : (a.empty() ? -1 : 1);
}

std::string OnlineUtility::GetActiveModsDigest() {
    // FNV-1a, 64 bits
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : GetActiveModsString()) {
        hash = (hash ^ c) * 1099511628211ull;
    }

    char text[17];
    snprintf(text, sizeof(text), "%016llx", (unsigned long long)hash);
    return text;
}

bool OnlineUtility::HasActiveIncompatibleMods() {
    for (auto& it : ModLoading::Instance().GetAllMods()) {
        if (CountsOnline(it) && !it->SupportsOnline()) {
            return true;
        }
    }
    return false;
}

std::string OnlineUtility::GetActiveIncompatibleModsString() {
    bool active_mods = false;
    stringstream modlist;
    vector<ModInstance*> mods = ModLoading::Instance().GetAllMods();
    for (auto& mod : mods) {
        if (CountsOnline(mod) && !mod->SupportsOnline()) {
            if (active_mods) {
                modlist << ", ";
            }
            active_mods = true;
            modlist << "\"" << mod->id << "\"";
        }
    }

    return modlist.str();
}

void OnlineUtility::HandleCommand(PlayerID playerID, const std::string& message) {
    auto command = OnlineUtility::CommandFromString(message);
    // TODO send command to angelscript
}

bool OnlineUtility::IsCommand(const std::string& message) {
    return message.size() > 1 && message.front() == '/';
}

std::vector<std::string> OnlineUtility::CommandFromString(const std::string& message) {
    std::string command_str = message.substr(1);
    std::vector<std::string> command;

    size_t start = command_str.find_first_not_of(' ');
    size_t end = command_str.find_first_of(' ', start);
    while (start != std::string::npos) {
        command.push_back(command_str.substr(start, end != std::string::npos ? end - start : std::string::npos));
        start = command_str.find_first_not_of(' ', end);
        end = command_str.find_first_of(' ', start);
    }
    return command;
}
