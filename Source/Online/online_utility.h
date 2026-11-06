//-----------------------------------------------------------------------------
//           Name: online_utility.h
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

#include <Online/online_datastructures.h>

#include <string>
#include <vector>

class OnlineUtility {
   public:
    static bool IsValidPlayerName(const std::string& name);
    // Overgrowth Online: a path another machine sent: under Data/, no way up or out,
    // plain characters (it may end up in a script or an asset load)
    static bool IsSafeAssetPath(const std::string& path);
    static bool IsSafeCharacterPath(const std::string& path);  // ... and an existing .xml
    static std::string GetDefaultPlayerName();
    static std::string GetPlayerName();
    static std::string GetActiveModsString();
    static std::string GetOvergrowthOnlineVersion();  // the package's (its mod's version; "" without it)
    static int CompareVersions(const std::string& a, const std::string& b);  // <0, 0, >0 by dotted numbers
    static std::string GetActiveModsDigest();  // (short: friends compare it before joining)

    // Overgrowth Online, the server browser: a host's mods as its lobby lists them
    // ("id=name|id=name", by id; "|+3" at the end: three more that didn't fit), and
    // what stands between a player and that list (mod names, joined by '|'). A
    // lobby's text comes from anyone: CompareMods cleans what it reads
    struct LocalMod {
        std::string id;
        std::string name;
        bool active;
    };
    struct ModDifference {
        std::string theirs;   // all of the list
        std::string install;  // on the list, not on this machine
        std::string enable;   // on the list, off here
        std::string disable;  // on here, not on the list
    };
    static const size_t kModsListMaxBytes = 8000;  // (Steam's limit for a lobby value is 8192)
    static std::vector<LocalMod> GetLocalMods();   // what can count online (no core mods)
    static std::string MakeModsList(std::vector<LocalMod> mods, size_t max_bytes = kModsListMaxBytes);
    static std::string GetActiveModsList();
    static ModDifference CompareMods(const std::string& list, const std::vector<LocalMod>& mine);

    static bool HasActiveIncompatibleMods();
    static std::string GetActiveIncompatibleModsString();
    static bool IsCommand(const std::string& message);
    static void HandleCommand(PlayerID playerID, const std::string& message);

   private:
    static std::vector<std::string> CommandFromString(const std::string& message);  // removes '/' and split the rest of the string on ' '
};
