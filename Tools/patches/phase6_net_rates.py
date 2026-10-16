"""Phase 6: friend dropped mid-arena ("Timeout; remote problem. Rx age server
10.8s relay 0.1s": the host heard nothing from the guest for 10.8 s while
Valve's relay answered; the guest still received the host).

- Wolfire's rate settings forced every connection to send at least 9 MB/s
  (SendRateMin). Fine on a LAN; over home uploads and relays it keeps
  bandwidth estimation from backing off: a congested link loses everything,
  keepalives too. The floor is now 128 KB/s (we stream about 150 KB/s per
  player at most); the ceiling stays 10 MB/s.
- A connection is dropped after 30 s of silence, not 10 (a stall or a route
  change is survived).
- Every 10 s the log gets each connection's ping, quality, rates and queues:
  the next drop shows what led to it.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Network/GNS/gns_net_framework.cpp", [
("""    // Same buffer and rate settings Wolfire used for the Steam framework
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_MTU_PacketSize, 1300);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 4096 * 60);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 1024 * 1024 * 9);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 1024 * 1024 * 10);""",
 """    // Wolfire's buffer and packet settings. Its send rate floor was 9 MB/s: over
    // home uploads and relays that keeps bandwidth estimation from backing off,
    // and a congested link drops everything, keepalives too (friend timeouts)
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_MTU_PacketSize, 1300);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 4096 * 60);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 128 * 1024);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 1024 * 1024 * 10);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_TimeoutConnected, 30000);  // (10 s by default)"""),
])

patch("Online/online.h", [
("""    vector<HeldHit> held_hits;""",
 """    vector<HeldHit> held_hits;
    double last_link_log_ms = 0.0;  // connection stats in the log every 10 s
    void LogLinks();"""),
])

patch("Online/online.cpp", [
("""        online_session->client_connection_manager.Update();
""", """        online_session->client_connection_manager.Update();

        if (OnlineClockMs() - last_link_log_ms > 10000.0) {
            last_link_log_ms = OnlineClockMs();
            LogLinks();
        }
"""),
("""// Held hits whose attacker we now show landing them (main thread, LateUpdate)""",
 """// Every 10 s: how each connection does (a drop shows what led to it)
void Online::LogLinks() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    for (const Peer& peer : online_session->connected_peers) {
        ConnectionStatus s;
        GetConnectionStatus(peer, &s);
        LOGI << "Link peer " << (int)peer.peer_id << ": ping " << s.ms_ping << " ms, quality " << s.connection_quality_local << "/"
             << s.connection_quality_remote << ", out " << (int)s.out_bytes_per_sec << " B/s, in " << (int)s.in_bytes_per_sec
             << " B/s, rate " << s.send_rate_bytes_per_second << ", pending " << s.pending_reliable << "+" << s.pending_unreliable
             << " (unacked " << s.sent_unacked_reliable << "), queue " << s.usec_queue_time / 1000 << " ms" << std::endl;
    }
}

// Held hits whose attacker we now show landing them (main thread, LateUpdate)"""),
])
print("ok")
