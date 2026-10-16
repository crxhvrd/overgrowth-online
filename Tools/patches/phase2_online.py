"""Phase 2 online patches: clock sync on ping/pong, receive draining, idle
network thread, safe client shutdown from network callbacks, transport ping.
Run from the repo root: python -I tools/patches/phase2_online.py
"""
S = "Source/"
MARK = "//    Modified By: Overhaul Online (crxhvrd)\n"


def edit(path, pairs):
    s = open(S + path, encoding="utf-8").read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:80], s.count(old))
        s = s.replace(old, new)
    if MARK not in s and "//      Developer: Wolfire Games LLC\n" in s:
        s = s.replace("//      Developer: Wolfire Games LLC\n", "//      Developer: Wolfire Games LLC\n" + MARK, 1)
    open(S + path, "w", encoding="utf-8", newline="").write(s)


# ---- Peer: its clock estimate --------------------------------------------------------------
edit("Online/online_peer.h", [
    ("""    float current_ping_delta = 0.0f;
};""", """    float current_ping_delta = 0.0f;
    ClockSync clock;  // this peer's clock relative to ours (ping/pong)
};"""),
])
s = open(S + "Online/online_peer.h", encoding="utf-8").read()
if "#include <Online/clock_sync.h>" not in s:
    first = s.index("#include")
    s = s[:first] + "#include <Online/clock_sync.h>\n" + s[first:]
    open(S + "Online/online_peer.h", "w", encoding="utf-8", newline="").write(s)

# ---- Ping: the sender's clock --------------------------------------------------------------
edit("Online/Message/ping.h", [
    ("""    uint32_t ping_id;

   public:
    Ping(uint32_t ping_id);""", """    uint32_t ping_id;
    double send_ms;  // sender's OnlineClockMs()

   public:
    Ping(uint32_t ping_id, double send_ms = 0.0);"""),
])
edit("Online/Message/ping.cpp", [
    ("""Ping::Ping(uint32_t ping_id) : OnlineMessageBase(OnlineMessageCategory::TRANSIENT),
                               ping_id(ping_id) {
}""", """Ping::Ping(uint32_t ping_id, double send_ms) : OnlineMessageBase(OnlineMessageCategory::TRANSIENT),
                                               ping_id(ping_id),
                                               send_ms(send_ms) {
}"""),
    ("""    binn_object_set_uint32(l, "id", p->ping_id);

    return l;""", """    binn_object_set_uint32(l, "id", p->ping_id);
    binn_object_set_double(l, "t0", p->send_ms);

    return l;"""),
    ("""    binn_object_get_uint32(l, "id", &p->ping_id);
}""", """    binn_object_get_uint32(l, "id", &p->ping_id);
    binn_object_get_double(l, "t0", &p->send_ms);
}"""),
    ("""        Online::Instance()->SendTo<OnlineMessages::Pong>(peer->conn_id, p->ping_id);""",
     """        Online::Instance()->SendTo<OnlineMessages::Pong>(peer->conn_id, p->ping_id, p->send_ms, OnlineClockMs());"""),
])

# ---- Pong: echo + the responder's clock; feeds the peer's ClockSync --------------------------
edit("Online/Message/pong.h", [
    ("""    uint32_t ping_id;

   public:
    Pong(uint32_t ping_id);""", """    uint32_t ping_id;
    double ping_send_ms;  // t0, echoed
    double reply_ms;      // t1, the responder's OnlineClockMs()

   public:
    Pong(uint32_t ping_id, double ping_send_ms = 0.0, double reply_ms = 0.0);"""),
])
edit("Online/Message/pong.cpp", [
    ("""Pong::Pong(uint32_t ping_id) : OnlineMessageBase(OnlineMessageCategory::TRANSIENT),
                               ping_id(ping_id) {
}""", """Pong::Pong(uint32_t ping_id, double ping_send_ms, double reply_ms) : OnlineMessageBase(OnlineMessageCategory::TRANSIENT),
                                                                    ping_id(ping_id),
                                                                    ping_send_ms(ping_send_ms),
                                                                    reply_ms(reply_ms) {
}"""),
    ("""    binn_object_set_uint32(l, "id", p->ping_id);

    return l;""", """    binn_object_set_uint32(l, "id", p->ping_id);
    binn_object_set_double(l, "t0", p->ping_send_ms);
    binn_object_set_double(l, "t1", p->reply_ms);

    return l;"""),
    ("""    binn_object_get_uint32(l, "id", &p->ping_id);
}""", """    binn_object_get_uint32(l, "id", &p->ping_id);
    binn_object_get_double(l, "t0", &p->ping_send_ms);
    binn_object_get_double(l, "t1", &p->reply_ms);
}"""),
    ("""    Peer* peer = online->GetPeerFromID(from);
    if (peer != nullptr && online->online_session->last_ping_id == p->ping_id) {
        peer->current_ping_delta = game_timer.wall_time - online->online_session->last_ping_time;
    }""", """    // Runs under connected_peers_lock (CheckPendingMessages)
    double t2 = OnlineClockMs();
    Peer* peer = online->GetPeerFromID(from);
    if (peer != nullptr && p->ping_send_ms > 0.0) {
        peer->clock.AddSample(p->ping_send_ms, p->reply_ms, t2);
        peer->current_ping_delta = (float)(peer->clock.Rtt() / 1000.0);
    }"""),
])

# ---- Online ------------------------------------------------------------------------------------
edit("Online/online.h", [
    ("""    std::deque<DelayedSend> net_sim_queue;  // network thread only
    void FlushDelayedSends();""", """    std::deque<DelayedSend> net_sim_queue;  // network thread only
    void FlushDelayedSends();

   public:
    // Transport-level round trip to a peer, ms (-1 if unknown)
    int GetPeerTransportPing(NetConnectionID conn_id);

   private:"""),
    ("""    void CheckNewMessages();""", """    int CheckNewMessages();  // messages received"""),
])
edit("Online/online.cpp", [
    # ping twice a second, with our clock in it
    ("""        // Send a ping every second.
        if (online_session != nullptr && game_timer.wall_time > online_session->last_ping_time + 1.0f) {
            online_session->last_ping_id = online_session->ping_counter++;
            online_session->last_ping_time = game_timer.wall_time;
            Send<OnlineMessages::Ping>(online_session->last_ping_id);
        }""", """        // Ping twice a second: round trip and clock offset per peer (clock_sync.h)
        if (online_session != nullptr && game_timer.wall_time > online_session->last_ping_time + 0.5f) {
            online_session->last_ping_id = online_session->ping_counter++;
            online_session->last_ping_time = game_timer.wall_time;
            Send<OnlineMessages::Ping>(online_session->last_ping_id, OnlineClockMs());
        }"""),
    # network thread: sleep only when a pass did nothing (it used to spin a core)
    ("""void Online::NetworkDataThread() {
    while (IsActive()) {
        net.Update();
        CheckNewMessages();
        SendMessages();
        FlushDelayedSends();""", """void Online::NetworkDataThread() {
    while (IsActive()) {
        uint64_t sent_before = net_msgs_out.load();
        net.Update();
        int received = CheckNewMessages();
        SendMessages();
        FlushDelayedSends();

        if (received == 0 && net_msgs_out.load() == sent_before) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }"""),
    # drain every waiting message, not one per peer per pass
    ("""void Online::CheckNewMessages() {""", """int Online::CheckNewMessages() {
    int received = 0;"""),
    ("""    for (auto& it : online_session->connected_peers) {
        int nrOfMessages = net.ReceiveMessageOnConnection(it.conn_id, &msg);

        if (nrOfMessages == -1) {
            LOGE << "Invalid connection handle: " << it.conn_id << endl;
        }

        for (int i = 0; i < nrOfMessages; i++) {""", """    for (auto& it : online_session->connected_peers) {
      for (int pass = 0; pass < 1024; ++pass) {
        int nrOfMessages = net.ReceiveMessageOnConnection(it.conn_id, &msg);

        if (nrOfMessages == -1) {
            LOGE << "Invalid connection handle: " << it.conn_id << endl;
        }

        if (nrOfMessages <= 0) {
            break;
        }

        received += nrOfMessages;

        for (int i = 0; i < nrOfMessages; i++) {"""),
    ("""            } else {
                LOGE << "Unrecognized package type: " << (int)package_header.package_type << endl;
            }
            msg.Release();
        }
    }
}""", """            } else {
                LOGE << "Unrecognized package type: " << (int)package_header.package_type << endl;
            }
            msg.Release();
        }
      }
    }

    return received;
}

int Online::GetPeerTransportPing(NetConnectionID conn_id) {
    ConnectionStatus status;
    net.GetConnectionStatus(conn_id, &status);
    return status.ms_ping;
}"""),
    # connection-change callbacks come on the network thread with GNS:
    # StopMultiplayer joins that thread, so queue it for the main loop
    ("""            CloseConnection(data->conn_id, reason);
            if (IsClient()) {
                StopMultiplayer();
""", """            CloseConnection(data->conn_id, reason);
            if (IsClient()) {
                QueueStopMultiplayer();  // the main loop stops it (we may be on the network thread)
"""),
    ("""        case NetFrameworkConnectionState::ProblemDetectedLocally: {
            ConnectionClosedReason reason = data->conn_info.end_reason;
            if (IsClient()) {
                if (ConnectionClosedReasonUtil::IsUnusualReason(reason)) {
                    string error = ConnectionClosedReasonUtil::GetErrorMessage(reason);
                    Engine::Instance()->QueueErrorMessage("Connection Lost", error);
                }
            }

            CloseConnection(data->conn_id, reason);""", """        case NetFrameworkConnectionState::ProblemDetectedLocally: {
            ConnectionClosedReason reason = data->conn_info.end_reason;
            CloseConnection(data->conn_id, reason);

            if (IsClient()) {
                // Lost the host: leave the session (it used to stay half open)
                QueueStopMultiplayer();

                if (ConnectionClosedReasonUtil::IsUnusualReason(reason)) {
                    string error = ConnectionClosedReasonUtil::GetErrorMessage(reason);
                    Engine::Instance()->QueueErrorMessage("Connection Lost", error);
                }
            }
"""),
])

s = open(S + "Online/online.cpp", encoding="utf-8").read()
if "#include <thread>" not in s:
    first = s.index("#include")
    s = s[:first] + "#include <thread>\n" + s[first:]
    open(S + "Online/online.cpp", "w", encoding="utf-8", newline="").write(s)

# Ping/Pong use the online clock
for path in ("Online/Message/ping.cpp", "Online/Message/pong.cpp"):
    s = open(S + path, encoding="utf-8").read()
    if "#include <Online/clock_sync.h>" not in s:
        s = s.replace("#include <Online/online.h>", "#include <Online/online.h>\n#include <Online/clock_sync.h>", 1)
        open(S + path, "w", encoding="utf-8", newline="").write(s)

print("ok")
