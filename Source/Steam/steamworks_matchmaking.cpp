//-----------------------------------------------------------------------------
//           Name: steamworks_matchmaking.cpp
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
#include "steamworks_matchmaking.h"

#include <Online/online.h>
#include <Online/online_utility.h>
#include <Main/engine.h>
#include <Internal/config.h>
#include <Logging/logdata.h>
#include <Steam/steamworks_util.h>

#if ENABLE_STEAMWORKS
#include <isteamnetworkingutils.h>
#endif

#include <algorithm>
#include <chrono>

static const char *LEVEL_PATH = "levelpath";

#if ENABLE_STEAMWORKS
const char *SteamworksMatchmaking::StatusToString(ESteamNetworkingConnectionState state) {
    switch (state) {
        case k_ESteamNetworkingConnectionState_None:
            return "None";
            break;

        case k_ESteamNetworkingConnectionState_Connecting:
            return "Connecting";
            break;

        case k_ESteamNetworkingConnectionState_FindingRoute:
            return "FindingRoute";
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            return "Connected";
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            return "ClosedByPeer";
            break;

        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            return "ProblemDetectedLocally";
            break;

        default:
            return "impossible internal state";
            break;
    }

    return "unknown";
}

SteamworksMatchmaking::SteamworksMatchmaking() {
    // initializing relay access on startup helps to start multiplayer faster
    // but if the game is closed before it has finished initializing
    // it will crash inside steam libraries
    // so we don't do it
    // FIXME: enable if steam fixes their lib
#if 0
    LOGI << "Initializing steam relay network access ..." << std::endl;
    SteamNetworkingUtils()->InitRelayNetworkAccess();
#endif  // 0
}

SteamworksMatchmaking::~SteamworksMatchmaking() {
    // if we free these it can cause crashes on exit on linux
    // apparently steam api doesn't properly wait for its internal threads
    // to shut down

    if (!lobbies.empty()) {
        LOGW << lobbies.size() << " lobbies remain at shutdown" << std::endl;
#if 0
        for (unsigned int i = 0; i < lobbies.size(); i++) {
            SteamMatchmaking()->LeaveLobby(lobbies[i]);
        }
#endif  // 0

        lobbies.clear();
    }
}

void SteamworksMatchmaking::InviteFriends() {
    OpenToFriends(true);
}

void SteamworksMatchmaking::OpenToFriends(bool overlay) {
    if (!lobbies.empty()) {
        if (overlay) {
            OpenInviteDialog();
        }
    } else if (creating) {
        overlay_on_enter = overlay_on_enter || overlay;
    } else {
        // Public (the server browser lists it) or friends only: an invite works
        // either way, and so does "Join Game" in the friends list
        lobby_public = WantPublic();
        LOGI << "Steam: creating our lobby (" << (lobby_public ? "public" : "friends only") << ")" << std::endl;
        creating = true;
        overlay_on_enter = overlay;
        SteamMatchmaking()->CreateLobby(lobby_public ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly, 8);
    }
}

void SteamworksMatchmaking::SetHostPresence(const std::string &game, int players, const std::string &mods) {
    ISteamFriends *friends = SteamFriends();
    std::string count = std::to_string(players);

    if (friends == nullptr || lobbies.empty()) {
        return;
    }
    if (game != presence_game) {
        presence_game = game;
        friends->SetRichPresence("oh_game", game.c_str());
    }
    if (count != presence_players) {
        presence_players = count;
        friends->SetRichPresence("oh_players", count.c_str());
    }
    if (mods != presence_mods) {
        presence_mods = mods;
        friends->SetRichPresence("oh_mods", mods.c_str());
    }

    // The server browser's view of it, and public or not as the host now wants
    CSteamID lobby = *lobbies.begin();
    SteamMatchmaking()->SetLobbyData(lobby, "oo_game", presence_game.c_str());
    SteamMatchmaking()->SetLobbyData(lobby, "oo_players", presence_players.c_str());
    SteamMatchmaking()->SetLobbyData(lobby, "oo_mods", presence_mods.c_str());
    if (WantPublic() != lobby_public) {
        lobby_public = WantPublic();
        SteamMatchmaking()->SetLobbyType(lobby, lobby_public ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly);
        LOGI << "Steam: our game is now " << (lobby_public ? "public" : "friends only") << std::endl;
    }
    if (!lobby_has_location) {
        SetBrowserData(lobby);  // (the ping location comes once Steam's relays have answered)
    }
}

static std::string FriendField(const char *text);  // (below)

bool SteamworksMatchmaking::WantPublic() {
    return !config.HasKey("oo_public_game") || config["oo_public_game"].toNumber<bool>();
}

// What the server browser lists: our marker, who hosts, the version, where
// we are (for the ping)
void SteamworksMatchmaking::SetBrowserData(CSteamID lobby) {
    ISteamMatchmaking *mm = SteamMatchmaking();
    if (mm == nullptr) {
        return;
    }
    mm->SetLobbyData(lobby, "oo", "1");
    mm->SetLobbyData(lobby, "oo_host", SteamFriends() != nullptr ? SteamFriends()->GetPersonaName() : "");
    mm->SetLobbyData(lobby, "oo_version", OnlineUtility::GetOvergrowthOnlineVersion().c_str());
    SteamNetworkPingLocation_t location;
    if (SteamNetworkingUtils() != nullptr && SteamNetworkingUtils()->GetLocalPingLocation(location) >= 0.0f) {
        char text[k_cchMaxSteamNetworkingPingLocationString];
        SteamNetworkingUtils()->ConvertPingLocationToString(location, text, sizeof(text));
        mm->SetLobbyData(lobby, "oo_loc", text);
        lobby_has_location = true;
    }
}

void SteamworksMatchmaking::RequestPublicGames() {
    ISteamMatchmaking *mm = SteamMatchmaking();
    if (mm == nullptr || browse_state == 1) {
        return;
    }
    if (SteamNetworkingUtils() != nullptr) {
        SteamNetworkingUtils()->InitRelayNetworkAccess();  // (the pings)
    }
    mm->AddRequestLobbyListStringFilter("oo", "1", k_ELobbyComparisonEqual);
    mm->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    mm->AddRequestLobbyListResultCountFilter(100);
    lobby_list_call.Set(mm->RequestLobbyList(), this, &SteamworksMatchmaking::OnLobbyList);
    browse_state = 1;
}

void SteamworksMatchmaking::OnLobbyList(LobbyMatchList_t *data, bool io_failure) {
    browse_lobbies.clear();
    if (io_failure || data == nullptr || SteamMatchmaking() == nullptr) {
        browse_state = 3;
        return;
    }
    for (uint32 i = 0; i < data->m_nLobbiesMatching; ++i) {
        browse_lobbies.push_back(SteamMatchmaking()->GetLobbyByIndex(i).ConvertToUint64());
    }
    browse_state = 2;
    LOGI << "Steam: " << browse_lobbies.size() << " public games" << std::endl;
}

std::string SteamworksMatchmaking::PublicGames(const std::string &mods, const std::string &version) {
    static const char *kStates[] = {"none", "busy", "ready", "failed"};
    std::string rows = kStates[browse_state];
    ISteamMatchmaking *mm = SteamMatchmaking();
    if (mm == nullptr) {
        return "failed";
    }
    for (uint64_t id : browse_lobbies) {
        CSteamID lobby(id);
        if (lobbies.count(lobby) != 0) {
            continue;  // (our own)
        }
        std::string their_mods = mm->GetLobbyData(lobby, "oo_mods");
        int ping = -1;
        SteamNetworkPingLocation_t location;
        const char *loc = mm->GetLobbyData(lobby, "oo_loc");
        if (SteamNetworkingUtils() != nullptr && loc[0] != 0 && SteamNetworkingUtils()->ParsePingLocationString(loc, location)) {
            ping = SteamNetworkingUtils()->EstimatePingTimeFromLocalHost(location);
        }
        rows += "\n" + std::to_string(id) + "\t" + FriendField(mm->GetLobbyData(lobby, "oo_host")) + "\t" +
                FriendField(mm->GetLobbyData(lobby, "oo_game")) + "\t" + FriendField(mm->GetLobbyData(lobby, "oo_players")) + "\t" +
                FriendField(mm->GetLobbyData(lobby, "oo_version")) + "\t" + (their_mods.empty() ? "" : (their_mods == mods ? "same" : "other")) +
                "\t" + std::to_string(ping);
    }
    (void)version;
    return rows;
}

// (a field of a FriendGames line: no tabs or line breaks)
static std::string FriendField(const char *text) {
    std::string field = text != nullptr ? text : "";
    for (char &c : field) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return field;
}

std::string SteamworksMatchmaking::FriendGames(const std::string &mods) {
    ISteamFriends *friends = SteamFriends();
    std::string rows;

    if (friends == nullptr || SteamUtils() == nullptr) {
        return rows;
    }

    // Steam keeps our friends' presence; ask for it afresh now and then
    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    bool request = now - last_presence_request_ms > 5000;
    if (request) {
        last_presence_request_ms = now;
    }

    const AppId_t app = SteamUtils()->GetAppID();
    const int count = friends->GetFriendCount(k_EFriendFlagImmediate);

    for (int i = 0; i < count; ++i) {
        CSteamID id = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
        FriendGameInfo_t game;

        if (!friends->GetFriendGamePlayed(id, &game) || game.m_gameID.AppID() != app) {
            continue;
        }
        if (request) {
            friends->RequestFriendRichPresence(id);
        }

        std::string connect = FriendField(friends->GetFriendRichPresence(id, "connect"));
        std::string status = FriendField(friends->GetFriendRichPresence(id, "status"));
        std::string their_mods = FriendField(friends->GetFriendRichPresence(id, "oh_mods"));
        std::string state = connect.find("+connect_lobby ") != std::string::npos ? "host"
                            : status.find(" Online") != std::string::npos ? "playing"  // (Overgrowth Online; Overgrowth Online before)
                                                                                  : "game";
        std::string match = their_mods.empty() ? "" : their_mods == mods ? "same" : "other";

        rows += std::to_string(id.ConvertToUint64()) + "\t" + FriendField(GetSteamNickname(id).c_str()) + "\t" + state + "\t" +
                FriendField(friends->GetFriendRichPresence(id, "oh_game")) + "\t" +
                FriendField(friends->GetFriendRichPresence(id, "oh_players")) + "\t" + match + "\n";
    }

    return rows;
}

bool SteamworksMatchmaking::JoinFriend(const std::string &steam_id) {
    ISteamFriends *friends = SteamFriends();
    const std::string key = "+connect_lobby ";

    if (friends == nullptr) {
        return false;
    }

    CSteamID id((uint64)strtoull(steam_id.c_str(), nullptr, 10));
    std::string connect = FriendField(friends->GetFriendRichPresence(id, "connect"));
    size_t at = connect.find(key);

    LOGI << "Steam: joining friend " << steam_id << " (" << connect << ")" << std::endl;
    if (at == std::string::npos) {
        return false;
    }
    JoinLobby(connect.substr(at + key.size()));
    return true;
}

void SteamworksMatchmaking::LeaveAll() {
    for (const CSteamID &lobby : lobbies) {
        SteamMatchmaking()->LeaveLobby(lobby);
    }
    lobbies.clear();
    creating = false;
    overlay_on_enter = false;
    presence_game.clear();
    presence_players.clear();
    presence_mods.clear();
    if (SteamFriends() != nullptr) {
        SteamFriends()->ClearRichPresence();
    }
}

void SteamworksMatchmaking::OpenInviteDialog() {
    if (lobbies.begin() != lobbies.end()) {
        if (SteamMatchmaking != nullptr) {
            ISteamFriends *friends = SteamFriends();
            if (friends) {
                friends->ActivateGameOverlayInviteDialog(*lobbies.begin());
            }
        }
    }
}

void SteamworksMatchmaking::JoinLobby(const std::string &lobbyIDStr) {
    LOGI << "SteamworksMatchmaking::JoinLobby " << lobbyIDStr << std::endl;

    char *end = NULL;
    // (a lobby id is 64 bits: strtoul is 32 bits on Windows)
    uint64 l = strtoull(lobbyIDStr.c_str(), &end, 10);

    CSteamID lobbyID(l);

    LOGI << "parsed lobby id: \"" << lobbyID.ConvertToUint64() << "\"" << std::endl;

    SteamMatchmaking()->JoinLobby(lobbyID);
}

void SteamworksMatchmaking::LeaveLobby(CSteamID lobbyID) {
    SteamMatchmaking()->LeaveLobby(lobbyID);
    auto it = std::find(lobbies.begin(), lobbies.end(), lobbyID);
    if (it != lobbies.end()) {
        lobbies.erase(it);
    }
}

void SteamworksMatchmaking::OnLobbyCreated(LobbyCreated_t *data) {
    assert(data);

    if (data->m_eResult != k_EResultOK) {
        // (the host tries again in a moment: Online::OpenToFriends)
        LOGE << "Steam: couldn't create our lobby (result " << (int)data->m_eResult << ")" << std::endl;
        creating = false;
        overlay_on_enter = false;
    }
}

void SteamworksMatchmaking::OnLobbyEnter(LobbyEnter_t *data) {
    Online *online = Online::Instance();

    assert(data);

    LOGI << "OnLobbyEnter " << data->m_ulSteamIDLobby << " response " << data->m_EChatRoomEnterResponse << std::endl;

    creating = false;

    if (data->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        // A friend's game that closed, filled up or isn't open to us
        if (!online->IsActive()) {
            Engine::Instance()->QueueErrorMessage("Couldn't join", "That game has ended or is full.");
        }
        overlay_on_enter = false;
        return;
    }

    lobbies.insert(data->m_ulSteamIDLobby);

    if (online->IsHosting()) {
        // we are the host
        // set lobby metadata
        SteamMatchmaking()->SetLobbyData(data->m_ulSteamIDLobby, LEVEL_PATH, "");
        lobby_has_location = false;
        SetBrowserData(data->m_ulSteamIDLobby);  // (Overgrowth Online: the server browser lists it)

        // "Join Game" in the friends list starts (or reaches) the game with this
        std::string connect = "+connect_lobby " + std::to_string(data->m_ulSteamIDLobby);
        SteamFriends()->SetRichPresence("connect", connect.c_str());
        SteamFriends()->SetRichPresence("status", "Hosting Overgrowth Online");

        // activate overlay (asked for: Invite friends)
        if (overlay_on_enter) {
            SteamFriends()->ActivateGameOverlayInviteDialog(data->m_ulSteamIDLobby);
        }
        overlay_on_enter = false;
    } else if (online->IsActive()) {
        LOGI << "Steam: already in a session, not joining that lobby" << std::endl;
        LeaveLobby(data->m_ulSteamIDLobby);
    } else {
        // we are client
        // get lobby metadata

        // Overgrowth Online: connect to the lobby's owner through Steam (P2P);
        // the session is that connection, the lobby is done
        CSteamID lobbyHost = SteamMatchmaking()->GetLobbyOwner(data->m_ulSteamIDLobby);
        LOGI << "Steam: joining the session of " << lobbyHost.ConvertToUint64() << std::endl;
        LeaveLobby(data->m_ulSteamIDLobby);
        online->ConnectP2P(lobbyHost.ConvertToUint64());
        SteamFriends()->SetRichPresence("status", "Playing Overgrowth Online");
    }
}

void SteamworksMatchmaking::OnLobbyDataUpdate(LobbyDataUpdate_t *data) {
    assert(data);

    LOGI << "OnLobbyDataUpdate " << data->m_ulSteamIDLobby << std::endl;

    if (data->m_bSuccess) {
        LOGI << "Transition was a success" << std::endl;
    }
}

void SteamworksMatchmaking::OnLobbyJoinRequested(GameLobbyJoinRequested_t *data) {
    assert(data);

    LOGI << "OnLobbyJoinRequested" << std::endl;

    SteamMatchmaking()->JoinLobby(data->m_steamIDLobby);
}

void SteamworksMatchmaking::OnLobbyChatUpdate(LobbyChatUpdate_t *data) {
    Online *online = Online::Instance();
    assert(data);

    LOGI << "OnLobbyChatUpdate " << data->m_ulSteamIDLobby << std::endl;
    if (data->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) {
        LOGI << "User " << data->m_ulSteamIDUserChanged << " joined lobby" << std::endl;
        if (online->IsHosting()) {
            // Friend accepted invite
            // Leave lobby
            // For single player mode host should maybe stay in lobby and only clients leave as they enter game
            // LeaveLobby(data->m_ulSteamIDLobby);
            // Steam doesn't offer way to close overlay from app, user needs to do that
        }
    } else {
        LOGI << "User " << data->m_ulSteamIDUserChanged << " left lobby" << std::endl;
    }
}

// "Join Game" in the friends list while we run: "+connect_lobby <id>"
void SteamworksMatchmaking::OnRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *data) {
    assert(data);
    std::string connect = data->m_rgchConnect;
    const std::string key = "+connect_lobby ";
    size_t at = connect.find(key);

    LOGI << "OnRichPresenceJoinRequested " << connect << std::endl;
    if (at != std::string::npos) {
        JoinLobby(connect.substr(at + key.size()));
    }
}

std::string SteamworksMatchmaking::GetSteamNickname(const CSteamID &friendID) {
    std::string name = "<unknown>";

    ISteamFriends *friends = SteamFriends();
    assert(friends);

    if (friendID.IsValid()) {
        // query for friend nickname, might not exist
        const char *nickname = friends->GetPlayerNickname(friendID);
        if (nickname) {
            name = nickname;
        } else {
            // TODO: might not exist immediately when joining lobby
            //  need to wait for PersonaStateChange_t
            // when not exists guaranteed to be non-NULL but possibly empty
            name = friends->GetFriendPersonaName(friendID);
        }
    }

    return name;
}
#endif
