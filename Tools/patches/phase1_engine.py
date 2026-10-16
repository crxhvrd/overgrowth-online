"""Phase 1 engine patches: test bridge hooks, input injection, network counters
and the lag/loss simulator, screenshot path, executable rename.
Run once from the repo root: python -I tools/patches/phase1_engine.py
"""
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
S = ROOT + "/Source/"
MARK = "//    Modified By: Overhaul Online (crxhvrd)\n"


def edit(path, pairs, mark=True):
    s = open(path, encoding="utf-8").read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:80], s.count(old))
        s = s.replace(old, new)
    if mark and MARK not in s:
        anchor = "//      Developer: Wolfire Games LLC\n"
        assert s.count(anchor) == 1, (path, "no Wolfire header")
        s = s.replace(anchor, anchor + MARK)
    open(path, "w", encoding="utf-8", newline="").write(s)


# ---- Engine: level-loaded accessor ------------------------------------------------------
edit(S + "Main/engine.h", [
    ("""    void ScriptableUICallback(const std::string& level);
""", """    void ScriptableUICallback(const std::string& level);
    bool IsLevelLoaded() const { return level_loaded_; }  // test bridge
"""),
])

# ---- Graphics: screenshot to an explicit path ----------------------------------------------
edit(S + "Graphics/graphics.h", [
    ("""    int queued_screenshot;
""", """    int queued_screenshot;
    std::string queued_screenshot_path;  // test bridge: save the next screenshot here
"""),
])
edit(S + "Graphics/graphics.cpp", [
    ("""        std::string path = ImageExport::FindEmptySequentialFile("Screenshots/OvergrowthScreenshot", ".jpg");
        ImageExport::SaveJPEG(path.c_str(), pixels, width, height);""",
     """        std::string path = queued_screenshot_path;
        queued_screenshot_path.clear();

        if (path.empty()) {
            path = ImageExport::FindEmptySequentialFile("Screenshots/OvergrowthScreenshot", ".jpg");
        }

        ImageExport::SaveJPEG(path.c_str(), pixels, width, height);"""),
])

# ---- Input: keys injected by automated tests (controller 0) -------------------------------
edit(S + "UserInput/input.h", [
    ("""    std::vector<Joystick::JoystickPress> GetJoystickInputs(int player_index);

   private:""", """    std::vector<Joystick::JoystickPress> GetJoystickInputs(int player_index);

    // Automated tests (Testing/test_bridge): bindings held for controller 0,
    // applied on every step after the real devices. depth >= 0.5 behaves like
    // a key press; steps_left < 0 means until released.
    struct InjectedKey {
        float depth = 1.0f;
        int steps_left = -1;
    };
    std::map<std::string, InjectedKey> injected_keys_;
    float injected_look_[2] = {0.0f, 0.0f};  // added once, on the next step

   private:
    void ApplyInjectedInput(PlayerInput& control);
"""),
])
edit(S + "UserInput/input.cpp", [
    ("""            mouse_.delta_[0] = 0;
            mouse_.delta_[1] = 0;
        }
    }
    // Zero all negative keydown counts""", """            mouse_.delta_[0] = 0;
            mouse_.delta_[1] = 0;
        }

        ApplyInjectedInput(control);
    }
    // Zero all negative keydown counts"""),
    ("""void Input::ProcessController(int controller_id, float timestep) {""",
     """void Input::ApplyInjectedInput(PlayerInput& control) {
    for (auto it = injected_keys_.begin(); it != injected_keys_.end();) {
        KeyState& ks = control.key_down[it->first];

        if (it->second.depth >= KeyState::kDepthThreshold) {
            PlayerInputKeyDown(&ks);  // the keyboard's own semantics
            ks.depth = it->second.depth;
        } else if (it->second.depth > 0.0f) {
            if (ks.count <= 0) {  // analog, like the joystick path
                ks.count *= -1;
                ++ks.count;
            }
            ks.depth = it->second.depth;
        }

        if (it->second.steps_left > 0 && --it->second.steps_left == 0) {
            it = injected_keys_.erase(it);
        } else {
            ++it;
        }
    }

    if (injected_look_[0] != 0.0f || injected_look_[1] != 0.0f) {
        control.key_down["look_right"].depth += injected_look_[0];
        control.key_down["look_down"].depth += injected_look_[1];
        injected_look_[0] = 0.0f;
        injected_look_[1] = 0.0f;
    }
}

void Input::ProcessController(int controller_id, float timestep) {"""),
])

# ---- Online: byte counters and the lag/loss simulator --------------------------------------
edit(S + "Online/online.h", [
    ("""#include <mutex>
""", """#include <mutex>
#include <atomic>
#include <deque>
"""),
    ("""class Online {
   public:
    int compression_level;
""", """class Online {
   public:
    int compression_level;

    // Test bridge: traffic counters and the network simulator. Latency and
    // jitter are one-way and applied by the sender (both peers run it); loss
    // drops unreliable messages only. Order per connection is kept.
    std::atomic<uint64_t> net_bytes_out{0};
    std::atomic<uint64_t> net_bytes_in{0};
    std::atomic<uint64_t> net_msgs_out{0};
    std::atomic<uint64_t> net_msgs_in{0};
    std::atomic<uint64_t> net_sim_dropped{0};
    std::atomic<int> net_sim_latency_ms{0};
    std::atomic<int> net_sim_jitter_ms{0};
    std::atomic<float> net_sim_loss{0.0f};
"""),
    ("""    void SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush);""",
     """    void SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush);

    struct DelayedSend {
        NetConnectionID conn_id;
        std::vector<char> data;
        bool reliable;
        double release_ms;
    };
    std::deque<DelayedSend> net_sim_queue;  // network thread only
    void FlushDelayedSends();"""),
])
edit(S + "Online/online.cpp", [
    ("""void Online::SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush) {
    net.SendMessageToConnection(conn_id, buffer, bytes, reliable, flush);
}""", """static double NetSimNowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Online::SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush) {
    int latency = net_sim_latency_ms.load();
    int jitter = net_sim_jitter_ms.load();
    float loss = net_sim_loss.load();

    if (latency <= 0 && jitter <= 0 && loss <= 0.0f && net_sim_queue.empty()) {
        net_bytes_out += bytes;
        ++net_msgs_out;
        net.SendMessageToConnection(conn_id, buffer, bytes, reliable, flush);
        return;
    }

    if (!reliable && loss > 0.0f && (float)rand() / (float)RAND_MAX < loss) {
        ++net_sim_dropped;
        return;
    }

    double release = NetSimNowMs() + latency + (jitter > 0 ? rand() % (jitter + 1) : 0);

    for (auto it = net_sim_queue.rbegin(); it != net_sim_queue.rend(); ++it) {
        if (it->conn_id == conn_id) {
            release = std::max(release, it->release_ms);  // keep order per connection
            break;
        }
    }

    net_sim_queue.push_back({conn_id, std::vector<char>(buffer, buffer + bytes), reliable, release});
}

void Online::FlushDelayedSends() {
    if (net_sim_queue.empty()) {
        return;
    }

    double now = NetSimNowMs();
    lock_guard<mutex> guard(online_session->connected_peers_lock);

    for (auto it = net_sim_queue.begin(); it != net_sim_queue.end();) {
        if (it->release_ms > now) {
            ++it;
            continue;
        }

        bool connected = false;

        for (auto& peer : online_session->connected_peers) {
            connected = connected || peer.conn_id == it->conn_id;
        }

        if (connected) {
            net_bytes_out += it->data.size();
            ++net_msgs_out;
            net.SendMessageToConnection(it->conn_id, it->data.data(), (uint32_t)it->data.size(), it->reliable, true);
        }

        it = net_sim_queue.erase(it);
    }
}"""),
    ("""        net.Update();
        CheckNewMessages();
        SendMessages();
""", """        net.Update();
        CheckNewMessages();
        SendMessages();
        FlushDelayedSends();
"""),
    ("""        for (int i = 0; i < nrOfMessages; i++) {
            PackageHeader package_header;""", """        for (int i = 0; i < nrOfMessages; i++) {
            net_bytes_in += msg.GetSize();
            ++net_msgs_in;
            PackageHeader package_header;"""),
])

# ---- main: --test-bridge <port> --------------------------------------------------------------
edit(S + "Main/main.cpp", [
    ("""static std::string overloadedWriteDir;
""", """static std::string overloadedWriteDir;
static int test_bridge_port = 0;
"""),
    ("""        TCLAP::ValueArg<std::string> workingDirArg("", "working-dir", "Force set the working dir for the application.", false, "", "string");
        cmd.add(workingDirArg);""", """        TCLAP::ValueArg<std::string> workingDirArg("", "working-dir", "Force set the working dir for the application.", false, "", "string");
        cmd.add(workingDirArg);

        TCLAP::ValueArg<int> testBridgeArg("", "test-bridge", "Open the automated-test command port on 127.0.0.1:<port>.", false, 0, "port");
        cmd.add(testBridgeArg);"""),
    ("""        overloadedWriteDir = writeDirArg.getValue();
""", """        overloadedWriteDir = writeDirArg.getValue();
        test_bridge_port = testBridgeArg.getValue();
"""),
    ("""    engine->Initialize();
    Dialog::Initialize();
    PROFILER_LEAVE(&profiler_context);
""", """    engine->Initialize();
    Dialog::Initialize();
    PROFILER_LEAVE(&profiler_context);

    if (test_bridge_port > 0) {
        TestBridge::Instance()->Start(test_bridge_port);
    }
"""),
    ("""        STIMING_START_COARSE(STUpdate);
        engine->Update();""", """        STIMING_START_COARSE(STUpdate);
        TestBridge::Instance()->Pump(engine);
        engine->Update();"""),
    ("""    LOGI << "Cleanly disposing of loaded assets..." << std::endl;
    engine->Dispose();""", """    TestBridge::Instance()->Stop();

    LOGI << "Cleanly disposing of loaded assets..." << std::endl;
    engine->Dispose();"""),
])

s = open(S + "Main/main.cpp", encoding="utf-8").read()
if "#include <Testing/test_bridge.h>" not in s:
    first = s.index("#include")
    s = s[:first] + "#include <Testing/test_bridge.h>\n" + s[first:]
    open(S + "Main/main.cpp", "w", encoding="utf-8", newline="").write(s)

s = open(S + "Online/online.cpp", encoding="utf-8").read()
if "#include <chrono>" not in s:
    first = s.index("#include")
    s = s[:first] + "#include <chrono>\n" + s[first:]
    open(S + "Online/online.cpp", "w", encoding="utf-8", newline="").write(s)

# ---- CMake: the Testing folder ----------------------------------------------------------------
p = ROOT + "/Projects/CMakeLists.txt"
s = open(p, encoding="utf-8").read()
if "target_add_grouped_source(Overgrowth Testing)" not in s:
    anchor = "target_add_grouped_source(Overgrowth Online)"
    assert s.count(anchor) == 1, "no Online source group"
    s = s.replace(anchor, anchor + "\n    target_add_grouped_source(Overgrowth Testing)")
    open(p, "w", encoding="utf-8", newline="").write(s)

print("ok")
