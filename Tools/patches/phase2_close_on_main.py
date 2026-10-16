"""Closed connections are handled on the main thread: the cleanup deletes the
leaving player's character (scene, scripts, GL resources), which crashed the
host when it ran on the network thread. Run from the repo root."""
S = "Source/"


def edit(path, pairs):
    s = open(S + path, encoding="utf-8").read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:80], s.count(old))
        s = s.replace(old, new)
    open(S + path, "w", encoding="utf-8", newline="").write(s)


if "close_queue_lock" not in open(S + "Online/online.h", encoding="utf-8").read():
  edit("Online/online.h", [
    ("""    std::atomic<float> net_sim_loss{0.0f};
""", """    std::atomic<float> net_sim_loss{0.0f};

    // The network thread queues connections to close; the main thread closes
    // them (LateUpdate): closing deletes the player's character
    std::mutex close_queue_lock;
"""),
])

edit("Online/online.cpp", [
    ("""        ConnectionToBeClosed temp;
        temp.conn_id = conn_id;
        temp.reason = reason;

        online_session->connections_waiting_to_be_closed.push(temp);""", """        ConnectionToBeClosed temp;
        temp.conn_id = conn_id;
        temp.reason = reason;

        lock_guard<mutex> guard(close_queue_lock);
        online_session->connections_waiting_to_be_closed.push(temp);"""),
    ("""void Online::HandleConnectionsToBeClosed() {
    while (online_session->connections_waiting_to_be_closed.empty() == false) {
        ConnectionToBeClosed& connection = online_session->connections_waiting_to_be_closed.front();
        CloseConnectionImmediate(connection.conn_id, connection.reason);
        online_session->connections_waiting_to_be_closed.pop();
    }
}""", """// Main thread only (LateUpdate): CloseConnectionImmediate removes the player's
// character from the scene (scripts, GL resources). It used to run on the
// network thread, which crashed the host when a player dropped.
void Online::HandleConnectionsToBeClosed() {
    vector<ConnectionToBeClosed> todo;
    {
        lock_guard<mutex> guard(close_queue_lock);

        while (online_session->connections_waiting_to_be_closed.empty() == false) {
            todo.push_back(online_session->connections_waiting_to_be_closed.front());
            online_session->connections_waiting_to_be_closed.pop();
        }
    }

    for (const ConnectionToBeClosed& connection : todo) {
        CloseConnectionImmediate(connection.conn_id, connection.reason);
    }
}"""),
    ("""            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (IsHosting()) {
            HandleConnectionsToBeClosed();
        }
""", """            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
"""),
    ("""        online_session->client_connection_manager.Update();

        if (IsHosting()) {
            SyncHostSessionFlags();
        }""", """        online_session->client_connection_manager.Update();

        if (IsHosting()) {
            HandleConnectionsToBeClosed();  // on the main thread: it deletes characters
            SyncHostSessionFlags();
        }"""),
])

edit("Network/GNS/gns_net_framework.cpp", [
    ("""    if (result != k_EResultOK && result != k_EResultIgnored) {
        LOGE << "GNS: send failed on connection " << (int)conn_id << ": EResult " << (int)result << std::endl;
    }""", """    // NoConnection: the peer just dropped; the close is on its way (no spam)
    if (result != k_EResultOK && result != k_EResultIgnored && result != k_EResultNoConnection) {
        LOGE << "GNS: send failed on connection " << (int)conn_id << ": EResult " << (int)result << std::endl;
    }"""),
])
print("ok")
