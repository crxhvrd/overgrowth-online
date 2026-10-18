//-----------------------------------------------------------------------------
//           Name: steamworks_matchmaking.h
//      Developer: Wolfire Games LLC
//    Description: Steamworks wrapper for Overgrowth, to simplify use and
//                 minimize state.
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

// steam_api.h requires cstddef for offsetof but doesn't include it itself
#include <cstddef>

#if ENABLE_STEAMWORKS || ENABLE_GAMENETWORKINGSOCKETS
#include <isteamnetworkingsockets.h>
#endif

#if ENABLE_STEAMWORKS
#include <steam_api.h>
#endif

#include <string>
#include <unordered_set>
#include <vector>

#if ENABLE_STEAMWORKS

namespace std {

template <>
struct hash<CSteamID> {
    size_t operator()(const CSteamID &id) const {
        return hash<uint64_t>()(id.ConvertToUint64());
    }
};

}  // namespace std

class SteamworksMatchmaking {
   private:
    friend class Steamworks;
    SteamworksMatchmaking();

    std::unordered_set<CSteamID> lobbies;  // we are we assuming multiple??
    bool creating = false;           // a lobby is on its way (LobbyEnter_t)
    bool overlay_on_enter = false;   // the invite overlay opens once it is made
    std::string presence_game;       // what friends see of our game (set when changed)
    std::string presence_players;
    std::string presence_mods;
    uint64_t last_presence_request_ms = 0;

    // Server browser: our lobby is public (config "oo_public_game", on unless
    // set off) and carries what the browser shows; the list we asked Steam for
    bool lobby_public = false;
    bool lobby_has_location = false;
    int browse_state = 0;  // 0 never asked, 1 asking, 2 ready, 3 failed
    std::vector<uint64_t> browse_lobbies;
    CCallResult<SteamworksMatchmaking, LobbyMatchList_t> lobby_list_call;
    void OnLobbyList(LobbyMatchList_t *data, bool io_failure);
    void SetBrowserData(CSteamID lobby);
    static bool WantPublic();

   public:
    ~SteamworksMatchmaking();

    // activate game overlay to the invite dialog
    // invitations sent from this dialog will be for the provided lobby

    // Overgrowth Online: our lobby (created the first time, friends only) and
    // Steam's invite overlay for it
    void InviteFriends();

    // Overgrowth Online (host): friends can join without an invite ("Join Game" in
    // their friends list, Join a friend in the menu): our lobby, made once;
    // `overlay` also opens Steam's invite overlay
    void OpenToFriends(bool overlay);

    // Overgrowth Online (host): what friends see in Join a friend
    void SetHostPresence(const std::string &game, int players, const std::string &mods);

    // Overgrowth Online: friends in this game, a line each, tab separated: Steam id,
    // name, state (host | playing | game: the normal game), what they host,
    // players, mods ("same" | "other" | "": unknown) against `mods`
    std::string FriendGames(const std::string &mods);

    // Overgrowth Online: join the game a friend hosts (false: they host none)
    bool JoinFriend(const std::string &steam_id);

    // Overgrowth Online, the server browser: ask Steam for the public games
    // (worldwide); PublicGames: the first line "busy" | "ready" | "failed" |
    // "none", then a line each, tab separated: lobby id, host, game, players,
    // version, mods ("same" | "other" against `mods`), ping ms (-1 unknown)
    void RequestPublicGames();
    std::string PublicGames(const std::string &mods, const std::string &version);

    void OpenInviteDialog();

    // Leave every lobby, clear the "Join Game" presence (the session ended)
    void LeaveAll();

    // Our lobby (0: none), for the test bridge
    uint64_t CurrentLobby() const { return lobbies.empty() ? 0 : lobbies.begin()->ConvertToUint64(); }

    // for command-line jobby loining
    void JoinLobby(const std::string &lobbyIDStr);

    // leave lobby
    void LeaveLobby(CSteamID lobbyID);

    // get Steam friend name
    std::string GetSteamNickname(const CSteamID &friendID);

    static const char *StatusToString(ESteamNetworkingConnectionState state);

    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyCreated, LobbyCreated_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyEnter, LobbyEnter_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyDataUpdate, LobbyDataUpdate_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnRichPresenceJoinRequested, GameRichPresenceJoinRequested_t);
};
#endif
