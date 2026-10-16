//-----------------------------------------------------------------------------
//           Name: gns_net_framework.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See gns_net_framework.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "gns_net_framework.h"

#ifdef USE_GNS_NETWORK_FRAMEWORK
#include <Internal/config.h>
#include <Logging/logdata.h>
#include <Online/online.h>

#include <vector>

extern Config config;

namespace {

const uint16_t kDefaultPort = 25744;
GNSNetFramework* g_framework = nullptr;
bool g_gns_initialized = false;

NetFrameworkConnectionState MapState(ESteamNetworkingConnectionState state) {
    switch (state) {
        case k_ESteamNetworkingConnectionState_Connecting:
            return NetFrameworkConnectionState::Connecting;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            return NetFrameworkConnectionState::ClosedByPeer;
        case k_ESteamNetworkingConnectionState_Connected:
            return NetFrameworkConnectionState::Connected;
        case k_ESteamNetworkingConnectionState_FindingRoute:
            return NetFrameworkConnectionState::FindingRoute;
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            return NetFrameworkConnectionState::ProblemDetectedLocally;
        default:
            return NetFrameworkConnectionState::Unknown;
    }
}

void DebugOutput(ESteamNetworkingSocketsDebugOutputType type, const char* msg) {
    LOGI << "GNS: " << msg << std::endl;
}

}  // namespace

NetworkingMessage::NetworkingMessage() : msg(nullptr) {
}

NetworkingMessage::NetworkingMessage(SteamNetworkingMessage_t* msg) : msg(msg) {
}

const void* NetworkingMessage::GetData() const {
    return msg->GetData();
}

uint64_t NetworkingMessage::GetSize() const {
    return msg->GetSize();
}

void NetworkingMessage::Release() {
    if (msg != nullptr) {
        msg->Release();
        msg = nullptr;
    }
}

GNSNetFramework::GNSNetFramework(Online* online_callback) : isns(nullptr), utils(nullptr), online(online_callback) {
}

uint16_t GNSNetFramework::DefaultPort() {
    int port = config["mp_port"].toNumber<int>();
    return (port > 0 && port < 65536) ? (uint16_t)port : kDefaultPort;
}

NetConnectionID GNSNetFramework::GetFreeConnectionID() {
    for (int i = 1; i < 255; i++) {
        if (connection_id_map.find(i) == connection_id_map.end()) {
            return i;
        }
    }

    LOGF << "Ran out of connection ids" << std::endl;
    return 0;
}

NetListenSocketID GNSNetFramework::GetFreeListenSocketID() {
    for (int i = 1; i < 255; i++) {
        if (listen_socket_map.find(i) == listen_socket_map.end()) {
            return i;
        }
    }

    LOGF << "Ran out of listen socket ids" << std::endl;
    return 0;
}

HSteamNetConnection GNSNetFramework::Handle(NetConnectionID conn_id) const {
    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    auto it = connection_id_map.find(conn_id);
    return it != connection_id_map.end() ? it->second : k_HSteamNetConnection_Invalid;
}

void GNSNetFramework::Initialize() {
#ifdef OVERHAUL_STEAM_SOCKETS
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
    // Direct IP (LAN, the tests) without Steam's certificates: they come with
    // the relay network config, which may not be there yet (or at all when
    // offline). P2P connections are still authenticated by Steam.
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 2);
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
#endif
    utils->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, DebugOutput);
    utils->SetGlobalCallback_SteamNetConnectionStatusChanged(&GNSNetFramework::OnStatusChangedStatic);

    // Wolfire's buffer and packet settings. Its send rate floor was 9 MB/s: over
    // home uploads and relays that keeps bandwidth estimation from backing off,
    // and a congested link drops everything, keepalives too (friend timeouts)
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_MTU_PacketSize, 1300);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 4096 * 60);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 128 * 1024);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 1024 * 1024 * 10);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_TimeoutConnected, 30000);  // (10 s by default)
}

void GNSNetFramework::Dispose() {
    std::lock_guard<std::recursive_mutex> guard(maps_lock);

    if (isns != nullptr) {
        for (auto& it : connection_id_map) {
            isns->CloseConnection(it.second, 0, "shutdown", false);
        }

        for (auto& it : listen_socket_map) {
            isns->CloseListenSocket(it.second);
        }
    }

    connection_id_map.clear();
    connection_id_map_reverse.clear();
    listen_socket_map.clear();
}

void GNSNetFramework::Update() {
    if (isns != nullptr) {
        isns->RunCallbacks();  // connection state changes -> OnStatusChanged, on this (network) thread
    }
}

bool GNSNetFramework::ConnectByIPAddress(const std::string& address, NetConnectionID* id) {
    if (isns == nullptr) {
        return false;
    }

    SteamNetworkingIPAddr addr;
    addr.Clear();

    if (!addr.ParseString(address.c_str())) {
        LOGE << "Invalid host IP address " << address << std::endl;
        return false;
    }

    if (addr.m_port == 0) {
        addr.m_port = DefaultPort();
    }

    LOGI << "GNS: connecting to " << address << " port " << addr.m_port << std::endl;
    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    HSteamNetConnection connection = isns->ConnectByIPAddress(addr, 0, nullptr);

    if (connection == k_HSteamNetConnection_Invalid) {
        return false;
    }

    NetConnectionID connection_id = GetFreeConnectionID();
    connection_id_map[connection_id] = connection;
    connection_id_map_reverse[connection] = connection_id;
    *id = connection_id;
    return true;
}

NetListenSocketID GNSNetFramework::CreateListenSocketIP(const std::string& local_address) {
    if (isns == nullptr) {
        return 0;
    }

    SteamNetworkingIPAddr addr;
    addr.Clear();

    if (local_address.empty() || !addr.ParseString(local_address.c_str())) {
        addr.Clear();
        addr.m_port = DefaultPort();
    }

    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    HSteamListenSocket listen = isns->CreateListenSocketIP(addr, 0, nullptr);

    if (listen == k_HSteamListenSocket_Invalid) {
        LOGE << "GNS: cannot listen on port " << addr.m_port << std::endl;
        return 0;
    }

    LOGI << "GNS: listening on port " << addr.m_port << std::endl;
    NetListenSocketID socket_id = GetFreeListenSocketID();
    listen_socket_map[socket_id] = listen;
    return socket_id;
}

void GNSNetFramework::CloseListenSocket(NetListenSocketID listen_socket_id) {
    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    auto it = listen_socket_map.find(listen_socket_id);

    if (it != listen_socket_map.end() && isns != nullptr) {
        isns->CloseListenSocket(it->second);
        listen_socket_map.erase(it);
    }
}

void GNSNetFramework::CloseConnection(NetConnectionID conn_id, ConnectionClosedReason reason) {
    std::lock_guard<std::recursive_mutex> guard(maps_lock);
    auto it = connection_id_map.find(conn_id);

    if (it != connection_id_map.end()) {
        if (isns != nullptr) {
            isns->CloseConnection(it->second, static_cast<int>(reason), nullptr, false);
        }

        connection_id_map_reverse.erase(it->second);
        connection_id_map.erase(it);
    }
}

void GNSNetFramework::GetConnectionStatus(NetConnectionID conn_id, ConnectionStatus* status) const {
    *status = ConnectionStatus();
    HSteamNetConnection handle = Handle(conn_id);

    if (isns == nullptr || handle == k_HSteamNetConnection_Invalid) {
        return;
    }

    SteamNetConnectionRealTimeStatus_t s;

    if (isns->GetConnectionRealTimeStatus(handle, &s, 0, nullptr) != k_EResultOK) {
        return;
    }

    status->ms_ping = s.m_nPing;
    status->connection_quality_local = s.m_flConnectionQualityLocal;
    status->connection_quality_remote = s.m_flConnectionQualityRemote;
    status->out_packets_per_sec = s.m_flOutPacketsPerSec;
    status->out_bytes_per_sec = s.m_flOutBytesPerSec;
    status->in_packets_per_sec = s.m_flInPacketsPerSec;
    status->in_bytes_per_sec = s.m_flInBytesPerSec;
    status->send_rate_bytes_per_second = s.m_nSendRateBytesPerSecond;
    status->pending_unreliable = s.m_cbPendingUnreliable;
    status->pending_reliable = s.m_cbPendingReliable;
    status->sent_unacked_reliable = s.m_cbSentUnackedReliable;
    status->usec_queue_time = s.m_usecQueueTime;
}

void GNSNetFramework::OnStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* data) {
    if (g_framework != nullptr) {
        g_framework->OnStatusChanged(data);
    }
}

void GNSNetFramework::OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* data) {
    if (data->m_info.m_eState == k_ESteamNetworkingConnectionState_None) {
        return;  // our own CloseConnection on a handle we already dropped
    }

    // SteamAPI_RunCallbacks (main thread) and our RunCallbacks (network thread)
    // can both deliver these in the Steam build: one at a time, in order
    std::lock_guard<std::mutex> one_at_a_time(status_lock);

    NetFrameworkConnectionStatusChanged changed;
    {
        std::lock_guard<std::recursive_mutex> guard(maps_lock);

        if (data->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
            connection_id_map_reverse.find(data->m_hConn) == connection_id_map_reverse.end()) {
            // An incoming connection on our listen socket
            NetConnectionID connection_id = GetFreeConnectionID();
            connection_id_map[connection_id] = data->m_hConn;
            connection_id_map_reverse[data->m_hConn] = connection_id;
        }

        auto it = connection_id_map_reverse.find(data->m_hConn);

        if (it == connection_id_map_reverse.end()) {
            return;  // already closed on our side
        }

        changed.conn_id = it->second;
    }

    changed.conn_info.end_reason = static_cast<ConnectionClosedReason>(data->m_info.m_eEndReason);
    changed.conn_info.connection_state = MapState(data->m_info.m_eState);
    LOGI << "GNS: connection " << (int)changed.conn_id << " -> " << changed.conn_info.connection_state
         << " (" << data->m_info.m_szEndDebug << ")" << std::endl;
    online->OnConnectionChange(&changed);

    // Over: free the socket's resources. Our id stays until the session
    // closes it (a new connection must not take it over before then).
    if (isns != nullptr && (data->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                            data->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)) {
        isns->CloseConnection(data->m_hConn, 0, nullptr, false);
    }
}

int GNSNetFramework::ReceiveMessageOnConnection(NetConnectionID conn_id, NetworkingMessage* networking_message) {
    HSteamNetConnection handle = Handle(conn_id);

    if (isns == nullptr || handle == k_HSteamNetConnection_Invalid) {
        return -1;
    }

    SteamNetworkingMessage_t* msg = nullptr;
    int ret = isns->ReceiveMessagesOnConnection(handle, &msg, 1);
    *networking_message = NetworkingMessage(ret > 0 ? msg : nullptr);
    return ret;
}

bool GNSNetFramework::AcceptConnection(NetConnectionID conn_id, int* result) {
    EResult r = isns != nullptr ? isns->AcceptConnection(Handle(conn_id)) : k_EResultFail;
    *result = (int)r;
    return r == k_EResultOK;
}

NetListenSocketID GNSNetFramework::CreateListenSocketP2P() {
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

std::string GNSNetFramework::GetResultString(int result) {
    return "EResult " + std::to_string(result);
}

void GNSNetFramework::SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush) {
    HSteamNetConnection handle = Handle(conn_id);

    if (isns == nullptr || handle == k_HSteamNetConnection_Invalid) {
        return;
    }

    int flags = k_nSteamNetworkingSend_UseCurrentThread;
    flags |= reliable ? k_nSteamNetworkingSend_Reliable : (k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoDelay);

    if (flush) {
        flags |= k_nSteamNetworkingSend_NoNagle;
    }

    EResult result = isns->SendMessageToConnection(handle, buffer, bytes, flags, nullptr);

    // NoConnection: the peer just dropped; the close is on its way (no spam)
    if (result != k_EResultOK && result != k_EResultIgnored && result != k_EResultNoConnection) {
        LOGE << "GNS: send failed on connection " << (int)conn_id << ": EResult " << (int)result << std::endl;
    }
}

#endif
