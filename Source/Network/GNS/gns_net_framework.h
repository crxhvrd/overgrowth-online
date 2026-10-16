//-----------------------------------------------------------------------------
//           Name: gns_net_framework.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Network framework on Valve's open-source GameNetworkingSockets
//                 (Libraries/GameNetworkingSockets-1.4.0): UDP with reliable and
//                 unreliable messages, real disconnect detection and ping. Same
//                 shape as SteamNetFramework (the ISteamNetworkingSockets API is
//                 the same), without the Steam-only parts (relay, overlay,
//                 Steam callbacks). Selected with USE_GNS_NETWORK_FRAMEWORK.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#ifdef USE_GNS_NETWORK_FRAMEWORK
#include <Game/connection_closed_reason.h>
#include <Network/net_framework_common.h>

#ifdef OVERHAUL_STEAM_SOCKETS
#include <cstddef>
#include <steam_api.h>  // Steam's sockets (Steamworks SDK): IP, and P2P through Valve's relays
#else
#include <steamnetworkingsockets.h>
#endif
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>

#include <map>
#include <mutex>
#include <string>

// The other frameworks export these; online code relies on them unqualified
using std::map;
using std::string;

typedef uint8_t NetConnectionID;
typedef uint8_t NetListenSocketID;

class Online;

class NetworkingMessage {
   private:
    friend class GNSNetFramework;
    SteamNetworkingMessage_t* msg;
    NetworkingMessage(SteamNetworkingMessage_t* msg);

   public:
    NetworkingMessage();
    const void* GetData() const;
    uint64_t GetSize() const;
    void Release();
};

struct NetConnectionInfo {
    ConnectionClosedReason end_reason;
    NetFrameworkConnectionState connection_state;
};

struct NetFrameworkConnectionStatusChanged {
    NetConnectionID conn_id;
    NetConnectionInfo conn_info;
};

class GNSNetFramework {
    std::map<NetConnectionID, HSteamNetConnection> connection_id_map;
    std::map<HSteamNetConnection, NetConnectionID> connection_id_map_reverse;
    std::map<NetListenSocketID, HSteamListenSocket> listen_socket_map;
    mutable std::recursive_mutex maps_lock;  // main thread connects/listens, the network thread sends/receives
    std::mutex status_lock;  // status changes one at a time (Steam may deliver them on the main thread too)

    ISteamNetworkingSockets* isns;
    ISteamNetworkingUtils* utils;
    Online* online;

    NetConnectionID GetFreeConnectionID();
    NetListenSocketID GetFreeListenSocketID();
    HSteamNetConnection Handle(NetConnectionID conn_id) const;
    static void OnStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* data);
    void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* data);

   public:
    explicit GNSNetFramework(Online* online_callback);

    // Port for listening and for addresses without one: config "mp_port"
    static uint16_t DefaultPort();

    void Initialize();
    void Dispose();
    void Update();

#ifdef OVERHAUL_STEAM_SOCKETS
    bool HasFriendInviteOverlay() const { return isns != nullptr; }
#else
    bool HasFriendInviteOverlay() const { return false; }
#endif
    // Steam: a listen socket friends reach through Steam, and a connection to
    // a Steam user (0 / false in the GNS build)
    NetListenSocketID CreateListenSocketP2P();
    bool ConnectP2P(uint64_t steam_id, NetConnectionID* id);

    bool ConnectByIPAddress(const std::string& address, NetConnectionID* id);
    NetListenSocketID CreateListenSocketIP(const std::string& local_address);
    void CloseListenSocket(NetListenSocketID listen_socket_id);
    void GetConnectionStatus(NetConnectionID conn_id, ConnectionStatus* status) const;
    void CloseConnection(NetConnectionID conn_id, ConnectionClosedReason reason);
    int ReceiveMessageOnConnection(NetConnectionID conn_id, NetworkingMessage* msg);
    bool AcceptConnection(NetConnectionID conn_id, int* result);
    std::string GetResultString(int result);
    void SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush);
};
#endif
