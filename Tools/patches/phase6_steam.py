"""Phase 6, part 3: Steam invites and P2P.

- Our network framework on Steam's sockets (OVERHAUL_STEAM_SOCKETS): Steam
  init instead of GameNetworkingSockets_Init, relay access, and P2P: a listen
  socket friends reach through Steam, and connections to a Steam user.
- "Invite friends" (the pause menu): a P2P listen socket, a friends-only
  lobby (Wolfire's was private: no "Join Game" from the friends list), rich
  presence "connect" = "+connect_lobby <id>", and Steam's invite overlay.
- Joining: an accepted invite or "Join Game" (GameLobbyJoinRequested /
  GameRichPresenceJoinRequested), or "+connect_lobby <id>" at start (Steam
  started us for it; joined once the menu runs). In the lobby the client
  connects P2P to its owner and leaves it.
- Wolfire bug: JoinLobby parsed the 64-bit lobby id with strtoul (32 bits on
  Windows).
- Leaving or stopping: the lobby is left and the rich presence cleared.
- Player names: the Steam name unless "playername" is set in the config.
- "ohl_host_on_load" (config): the next level loads hosted. Mods that can't
  call online functions (they must compile on stock 1.4) set it before
  loading a map, e.g. Overhaul's Large Battles.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# ---- the framework ---------------------------------------------------------------
patch("Network/GNS/gns_net_framework.h", [
("""#include <steamnetworkingsockets.h>
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>""", """#ifdef OVERHAUL_STEAM_SOCKETS
#include <cstddef>
#include <steam_api.h>  // Steam's sockets (Steamworks SDK): IP, and P2P through Valve's relays
#else
#include <steamnetworkingsockets.h>
#endif
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>"""),
("""    bool HasFriendInviteOverlay() const { return false; }
""", """#ifdef OVERHAUL_STEAM_SOCKETS
    bool HasFriendInviteOverlay() const { return isns != nullptr; }
#else
    bool HasFriendInviteOverlay() const { return false; }
#endif
    // Steam: a listen socket friends reach through Steam, and a connection to
    // a Steam user (0 / false in the GNS build)
    NetListenSocketID CreateListenSocketP2P();
    bool ConnectP2P(uint64_t steam_id, NetConnectionID* id);
"""),
])

patch("Network/GNS/gns_net_framework.cpp", [
("""    if (!g_gns_initialized) {
        SteamNetworkingErrMsg err;

        if (!GameNetworkingSockets_Init(nullptr, err)) {
            LOGF << "GameNetworkingSockets_Init failed: " << err << std::endl;
            return;
        }

        g_gns_initialized = true;
    }

    g_framework = this;
    isns = SteamNetworkingSockets_Lib();
    utils = SteamNetworkingUtils_Lib();""", """#ifdef OVERHAUL_STEAM_SOCKETS
    // Steam's sockets: SteamAPI_Init ran at start (Steamworks::Initialize)
    (void)g_gns_initialized;
    isns = SteamNetworkingSockets();
    utils = SteamNetworkingUtils();

    if (isns == nullptr || utils == nullptr) {
        LOGE << "Steam networking is unavailable (is Steam running?)" << std::endl;
        isns = nullptr;
        return;
    }

    utils->InitRelayNetworkAccess();  // P2P through Valve's relays: no port forwarding
    g_framework = this;
#else
    if (!g_gns_initialized) {
        SteamNetworkingErrMsg err;

        if (!GameNetworkingSockets_Init(nullptr, err)) {
            LOGF << "GameNetworkingSockets_Init failed: " << err << std::endl;
            return;
        }

        g_gns_initialized = true;
    }

    g_framework = this;
    isns = SteamNetworkingSockets_Lib();
    utils = SteamNetworkingUtils_Lib();
#endif"""),
("""std::string GNSNetFramework::GetResultString(int result) {""", """NetListenSocketID GNSNetFramework::CreateListenSocketP2P() {
#ifdef OVERHAUL_STEAM_SOCKETS
    if (isns == nullptr) {
        return 0;
    }

    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    HSteamListenSocket listen = isns->CreateListenSocketP2P(0, 0, nullptr);

    if (listen == k_HSteamListenSocket_Invalid) {
        LOGE << "Steam: cannot open the P2P listen socket" << std::endl;
        return 0;
    }

    LOGI << "Steam: listening for friends (P2P)" << std::endl;
    NetListenSocketID socket_id = GetFreeListenSocketID();
    listen_socket_map[socket_id] = listen;
    return socket_id;
#else
    return 0;
#endif
}

bool GNSNetFramework::ConnectP2P(uint64_t steam_id, NetConnectionID* id) {
#ifdef OVERHAUL_STEAM_SOCKETS
    if (isns == nullptr) {
        return false;
    }

    SteamNetworkingIdentity identity;
    identity.Clear();
    identity.SetSteamID64(steam_id);
    LOGI << "Steam: connecting to " << steam_id << " (P2P)" << std::endl;
    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    HSteamNetConnection connection = isns->ConnectP2P(identity, 0, 0, nullptr);

    if (connection == k_HSteamNetConnection_Invalid) {
        return false;
    }

    NetConnectionID connection_id = GetFreeConnectionID();
    connection_id_map[connection_id] = connection;
    connection_id_map_reverse[connection] = connection_id;
    *id = connection_id;
    return true;
#else
    (void)steam_id;
    (void)id;
    return false;
#endif
}

std::string GNSNetFramework::GetResultString(int result) {"""),
])

# ---- Online ------------------------------------------------------------------------
patch("Online/online.h", [
("""    void ConnectByIPAddress(const string& address);""", """    void ConnectByIPAddress(const string& address);
    void ConnectP2P(uint64_t steam_id);  // Steam: to the host of the lobby we joined"""),
])

patch("Online/online.cpp", [
("""#if ENABLE_STEAMWORKS
    // TODO this is steam specific code, it needs to be moved to SteamNetFramework
    // Preferably called "ActivateFriendInviteOverlay()" to match HasFriendInviteOverlay()
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking) {
        if (!p2p_sockets_active) {
            HSteamListenSocket listen = matchmaking->ActivateGameOverlayInviteDialog();
            listen_sockets.insert(listen);
            p2p_sockets_active = true;
        } else {
            matchmaking->OpenInviteDialog();
        }
    } else {
        LOGE << "Couldn't open invite overlay page" << endl;
    }
#else""", """#if ENABLE_STEAMWORKS
    // Overhaul Online: friends reach us through Steam (P2P), the lobby carries the invite
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking) {
        if (!p2p_sockets_active) {
            NetListenSocketID listen = net.CreateListenSocketP2P();
            if (listen != 0) {
                listen_sockets.insert(listen);
                p2p_sockets_active = true;
            }
        }
        matchmaking->InviteFriends();
    } else {
        LOGE << "Couldn't open invite overlay page" << endl;
    }
#else"""),
("""void Online::ConnectByIPAddress(const string& address) {""", """void Online::ConnectP2P(uint64_t steam_id) {
    if (!IsActive()) {
        StartMultiplayer(MultiplayerMode::Client);

        NetConnectionID conn_id;

        if (net.ConnectP2P(steam_id, &conn_id) == false) {
            StopMultiplayer();
        }
    }
}

void Online::ConnectByIPAddress(const string& address) {"""),
("""        host_started_level = false;
        loading = false;
        p2p_sockets_active = false;
""", """        host_started_level = false;
        loading = false;
        p2p_sockets_active = false;

#if ENABLE_STEAMWORKS
        if (Steamworks::Instance()->GetMatchmaking() != nullptr) {
            Steamworks::Instance()->GetMatchmaking()->LeaveAll();  // the lobby and the "Join Game" presence
        }
#endif
"""),
])

patch("Online/online_utility.cpp", [
("""    return config.GetRef("playername").str();""", """    std::string name = config.GetRef("playername").str();
#if ENABLE_STEAMWORKS
    // Overhaul Online: the Steam name, unless one is set in the config
    if ((name.empty() || name == "UNKNOWN" || name == "Player") && Steamworks::Instance()->IsConnected() &&
        SteamFriends() != nullptr) {
        name = SteamFriends()->GetPersonaName();
    }
#endif
    return name;"""),
])

# ---- matchmaking ------------------------------------------------------------------------
patch("Steam/steamworks_matchmaking.h", [
("""    std::unordered_set<CSteamID> lobbies;  // we are we assuming multiple??
    std::unordered_set<HSteamNetConnection> steamPendingConnections;
""", """    std::unordered_set<CSteamID> lobbies;  // we are we assuming multiple??
    bool creating = false;           // a lobby is on its way (LobbyEnter_t)
"""),
("""    // This function has a ton of side effects and requires refactor
    // it basically inits an entire mp state.
    HSteamListenSocket ActivateGameOverlayInviteDialog();

    //
    void OpenInviteDialog();
""", """    // Overhaul Online: our lobby (created the first time, friends only) and
    // Steam's invite overlay for it
    void InviteFriends();

    void OpenInviteDialog();

    // Leave every lobby, clear the "Join Game" presence (the session ended)
    void LeaveAll();
"""),
("""    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnConnectionChange, SteamNetConnectionStatusChangedCallback_t);""",
 """    STEAM_CALLBACK(SteamworksMatchmaking, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamworksMatchmaking, OnRichPresenceJoinRequested, GameRichPresenceJoinRequested_t);"""),
])

patch("Steam/steamworks_matchmaking.cpp", [
("""HSteamListenSocket SteamworksMatchmaking::ActivateGameOverlayInviteDialog() {
    LOGI << "SteamworksMatchmaking::ActivateGameOverlayInviteDialog()" << std::endl;

    SteamMatchmaking()->CreateLobby(k_ELobbyTypePrivate, 10);

    ISteamNetworkingSockets *isns = SteamNetworkingSockets();
    assert(isns);

    HSteamListenSocket listen = isns->CreateListenSocketP2P(0, 0, nullptr);

    return listen;
}
""", """void SteamworksMatchmaking::InviteFriends() {
    if (!lobbies.empty()) {
        OpenInviteDialog();
    } else if (!creating) {
        // Friends only: an invite works, and so does "Join Game" in the friends list
        LOGI << "Steam: creating our lobby" << std::endl;
        creating = true;
        SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, 8);
    }
}

void SteamworksMatchmaking::LeaveAll() {
    for (const CSteamID &lobby : lobbies) {
        SteamMatchmaking()->LeaveLobby(lobby);
    }
    lobbies.clear();
    creating = false;
    if (SteamFriends() != nullptr) {
        SteamFriends()->ClearRichPresence();
    }
}
"""),
("""    char *end = NULL;
    // We presume uint64 to be the same as the unsigned long returned by strtoul
    uint64 l = strtoul(lobbyIDStr.c_str(), &end, 10);
""", """    char *end = NULL;
    // (a lobby id is 64 bits: strtoul is 32 bits on Windows)
    uint64 l = strtoull(lobbyIDStr.c_str(), &end, 10);
"""),
("""    if (online->IsHosting()) {
        // we are the host
        // set lobby metadata
        SteamMatchmaking()->SetLobbyData(data->m_ulSteamIDLobby, LEVEL_PATH, "");

        // activate overlay
        SteamFriends()->ActivateGameOverlayInviteDialog(data->m_ulSteamIDLobby);
    } else {""", """    creating = false;

    if (online->IsHosting()) {
        // we are the host
        // set lobby metadata
        SteamMatchmaking()->SetLobbyData(data->m_ulSteamIDLobby, LEVEL_PATH, "");

        // "Join Game" in the friends list starts (or reaches) the game with this
        std::string connect = "+connect_lobby " + std::to_string(data->m_ulSteamIDLobby);
        SteamFriends()->SetRichPresence("connect", connect.c_str());
        SteamFriends()->SetRichPresence("status", "Hosting Overhaul Online");

        // activate overlay
        SteamFriends()->ActivateGameOverlayInviteDialog(data->m_ulSteamIDLobby);
    } else if (online->IsActive()) {
        LOGI << "Steam: already in a session, not joining that lobby" << std::endl;
        LeaveLobby(data->m_ulSteamIDLobby);
    } else {"""),
("""        // Start multiplayer with host
        CSteamID lobbyHost = SteamMatchmaking()->GetLobbyOwner(data->m_ulSteamIDLobby);
        ISteamNetworkingSockets *isns = SteamNetworkingSockets();
        assert(isns);

        SteamNetworkingIdentity id;
        id.SetSteamID(lobbyHost);

        HSteamNetConnection connection = isns->ConnectP2P(id, 0, 0, nullptr);
        std::vector<char> temp(1024, 0);
        int ret = isns->GetDetailedConnectionStatus(connection, &temp[0], temp.size());
        if (ret >= 0) {
            LOGI << "connection status: " << &temp[0] << std::endl;
        } else {
            LOGE << "GetDetailedConnectionStatus failed" << std::endl;
        }
        steamPendingConnections.insert(connection);

        // Leave lobby - maybe not?
        LeaveLobby(data->m_ulSteamIDLobby);

        if (!online->IsActive())
            online->StartMultiplayer(MultiplayerMode::Client);
    }""", """        // Overhaul Online: connect to the lobby's owner through Steam (P2P);
        // the session is that connection, the lobby is done
        CSteamID lobbyHost = SteamMatchmaking()->GetLobbyOwner(data->m_ulSteamIDLobby);
        LOGI << "Steam: joining the session of " << lobbyHost.ConvertToUint64() << std::endl;
        LeaveLobby(data->m_ulSteamIDLobby);
        online->ConnectP2P(lobbyHost.ConvertToUint64());
        SteamFriends()->SetRichPresence("status", "Playing Overhaul Online");
    }"""),
("""void SteamworksMatchmaking::OnConnectionChange(SteamNetConnectionStatusChangedCallback_t *data) {
    ISteamNetworkingSockets *isns = SteamNetworkingSockets();

    assert(data);
    LOGI << "SteamworksMatchmaking::OnConnectionChange " << data->m_hConn << " old:" << StatusToString(data->m_eOldState) << "  new: " << StatusToString(data->m_info.m_eState) << std::endl;

    /*/
    switch (data->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting: {
                    //EResult result = isns->AcceptConnection(data->m_hConn);
                    break;
            }

            default: {

            }
    }
    */

    auto it = steamPendingConnections.find(data->m_hConn);
    if (it != steamPendingConnections.end()) {
        // This is a Steam connection
        LOGI << "Steam connection" << std::endl;
        steamPendingConnections.erase(it);
    }
}""", """// "Join Game" in the friends list while we run: "+connect_lobby <id>"
void SteamworksMatchmaking::OnRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *data) {
    assert(data);
    std::string connect = data->m_rgchConnect;
    const std::string key = "+connect_lobby ";
    size_t at = connect.find(key);

    LOGI << "OnRichPresenceJoinRequested " << connect << std::endl;
    if (at != std::string::npos) {
        JoinLobby(connect.substr(at + key.size()));
    }
}"""),
])

# ---- Steamworks: the lobby we were started for --------------------------------------
patch("Steam/steamworks.cpp", [
("""    if (updateUGC) {
        if (GetUGC()) {""", """    // Overhaul Online: Steam started us for an invite ("+connect_lobby <id>");
    // join once the menu runs (updateUGC: not in a level)
    if (updateUGC && matchmaking != nullptr && !g_launch_lobby.empty() && ++launch_lobby_frames > 30) {
        LOGI << "Steam: joining the lobby we were started for, " << g_launch_lobby << std::endl;
        matchmaking->JoinLobby(g_launch_lobby);
        g_launch_lobby.clear();
    }

    if (updateUGC) {
        if (GetUGC()) {"""),
])
patch("Steam/steamworks.h", [
("""    int poll_counter;
    const static int poll_freq;
""", """    int poll_counter;
    const static int poll_freq;
    int launch_lobby_frames = 0;  // Overhaul Online: menu frames before joining the start lobby
"""),
])

patch("Steam/steamworks.cpp", [
("""#include <Main/engine.h>
""", """#include <Main/engine.h>
#include <Online/online.h>
"""),
])

patch("Online/online_utility.cpp", [
("""#if ENABLE_STEAMWORKS
#include <isteamfriends.h>
#endif""", """#if ENABLE_STEAMWORKS
#include <isteamfriends.h>
#include <Steam/steamworks.h>
#endif"""),
])
print("ok")
